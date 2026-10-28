// SPDX-License-Identifier: GPL-2.0-or-later
// Task 0236 (ADR 0074): Sculpt Mode - the brushes touch only what is under them; a stroke's undo is a delta.
#include "test_common.h"

#include "../src/scene/mesh.h"
#include "../src/render/canvas.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

using namespace bl;

double wall_seconds() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

void step(Editor &ed, std::vector<platform::Event> events = {}) {
  ed.set_clock_for_test(ed.clock_for_test() + 0.02);
  ed.step_frame_headless(events);
}

/* A sphere alone in a fresh scene, Sculpt on, a frame drawn (the planes the brush reads). */
GameObject *sphere_scene(Editor &ed, int subdivisions = 0) {
  ed.init_headless(1400, 900);
  ed.set_clock_for_test(100.0);
  ed.command("select Cube");  // (the sample scene's cube sits where the sphere goes: it would hide it from the brush)
  ed.command("delete");
  ed.command("create Sphere");
  if (subdivisions > 0) ed.command(strprintf("subdivide %d", subdivisions));
  step(ed);
  for (int i = 0; i < 400 && ed.scene_camera_for_test().animating; i++) step(ed);  // (the camera's glide onto the new object: the id plane and the projection must agree)
  ed.command("sculpt on");
  GameObject *g = ed.active_object_for_test();
  /* The render mesh is built on a worker (task 0097): frames until the sphere is in the Scene view's id plane. */
  Vec3 w;
  float d;
  for (int i = 0; i < 100 && g; i++) {
    step(ed);
    Vec2 px;
    ed.project_for_test(g->world_position(), px);
    if (ed.sculpt_hit_for_test(px.x, px.y, w, d)) break;
  }
  step(ed);
  return g;
}

/* The Scene view pixel of a world point (view-relative, as `sculpt dab` wants). */
Vec2 pixel_of(Editor &ed, Vec3 w) {
  Vec2 px;
  ed.project_for_test(w, px);
  return px;
}

/* The mesh's signed volume (its faces fanned). */
double volume(const Mesh &m) {
  double v = 0.0;
  for (size_t f = 0; f < m.face_count(); f++) {
    const uint32_t n = m.face_size(f), *fv = m.face_verts(f);
    for (uint32_t c = 1; c + 1 < n; c++) {
      const Vec3 a = m.positions[fv[0]], b = m.positions[fv[c]], d = m.positions[fv[c + 1]];
      v += dot(a, cross(b, d)) / 6.0;
    }
  }
  return v;
}

/* The sum of squared Laplacians (each vertex minus its neighbours' mean) over `ids`. */
double laplacian_energy(const Mesh &m, const std::vector<uint32_t> &ids) {
  const Mesh::VertexFaces &vf = m.vertex_faces();
  double e = 0.0;
  for (uint32_t v : ids) {
    Vec3 sum(0.0f);
    int count = 0;
    for (uint32_t k = vf.offsets[v]; k < vf.offsets[v + 1]; k++) {
      const size_t f = vf.faces[k];
      const uint32_t n = m.face_size(f), *fvv = m.face_verts(f);
      for (uint32_t c = 0; c < n; c++)
        if (fvv[c] == v) sum += m.positions[fvv[(c + 1) % n]] + m.positions[fvv[(c + n - 1) % n]], count += 2;
    }
    if (count) e += (double)length_sq(m.positions[v] - sum / (float)count);
  }
  return e;
}

size_t changed_count(const std::vector<Vec3> &a, const std::vector<Vec3> &b) {
  size_t n = 0;
  for (size_t i = 0; i < a.size() && i < b.size(); i++) n += std::memcmp(&a[i], &b[i], sizeof(Vec3)) != 0;
  return n;
}

}  // namespace

static void tests_round256() {
  test("sculpt: a Draw dab on a sphere moves exactly the vertices within the brush's radius, each along its normal and less toward the edge; the rest are bitwise unchanged; Carve moves them inward; half the pressure moves the centre about half as far", [] {
    Editor ed;
    GameObject *g = sphere_scene(ed);
    CHECK(g && ed.sculpt_for_test());
    if (!g) return;
    const MeshPtr mesh = g->mesh_filter()->mesh;
    const std::vector<Vec3> before = mesh->positions;
    const Vec2 px = pixel_of(ed, g->world_position());  // (the sphere's front, toward the camera)
    ed.command("sculpt size 25");
    ed.command("sculpt strength 1");
    Vec3 hit_world;
    float hit_dist = 0.0f;
    const bool hit_ok = ed.sculpt_hit_for_test(px.x, px.y, hit_world, hit_dist);
    std::printf("    the pixel (%.0f, %.0f) hits %d at (%.3f, %.3f, %.3f), %.3f m away (the sphere's front is at |p| = 0.5)\n", px.x, px.y, (int)hit_ok, hit_world.x, hit_world.y, hit_world.z, hit_dist);
    CHECK(hit_ok && std::fabs(length(hit_world - g->world_position()) - 0.5f) < 0.05f);
    ed.command(strprintf("sculpt dab %g %g", px.x, px.y));
    const std::vector<uint32_t> ids = ed.sculpt_last_ids_for_test();
    const Vec3 hit = ed.sculpt_last_hit_for_test();
    const float r = ed.sculpt_last_radius_for_test();
    std::printf("    %zu vertices under a %.3f m brush at (%.2f, %.2f, %.2f); %zu changed\n", ids.size(), r, hit.x, hit.y, hit.z, changed_count(before, mesh->positions));
    CHECK(!ids.empty() && r > 0.0f && changed_count(before, mesh->positions) == ids.size());
    std::vector<uint8_t> in(mesh->vert_count(), 0);
    for (uint32_t v : ids) in[v] = 1;
    bool outward = true, falls = true, others_same = true, within = true;
    float centre_move = 0.0f, centre_d = 1e9f, edge_move = 0.0f, edge_d = 0.0f;
    for (uint32_t v = 0; v < (uint32_t)mesh->vert_count(); v++) {
      const Vec3 d = mesh->positions[v] - before[v];
      if (!in[v]) {
        others_same = others_same && length(d) == 0.0f;
        continue;
      }
      const float dist = length(before[v] - hit);
      within = within && dist <= r * 1.001f;
      const Vec3 n = normalize(before[v]);  // (a sphere: the normal is the radial direction)
      outward = outward && (length(d) == 0.0f || dot(d, n) > 0.0f);  // (a vertex at the very edge takes nothing)
      if (dist < centre_d) centre_d = dist, centre_move = length(d);
      if (dist > edge_d) edge_d = dist, edge_move = length(d);
    }
    falls = centre_move > edge_move;
    std::printf("    the centre moved %.4f (at %.3f), the edge %.4f (at %.3f)\n", centre_move, centre_d, edge_move, edge_d);
    CHECK(outward && falls && others_same && within);
    /* Carve: inward. */
    ed.command("undo");
    CHECK(changed_count(before, mesh->positions) == 0);
    ed.command("sculpt carve on");
    ed.command(strprintf("sculpt dab %g %g", px.x, px.y));
    bool inward = true;
    for (uint32_t v : ed.sculpt_last_ids_for_test()) {
      const Vec3 d = mesh->positions[v] - before[v];
      inward = inward && (length(d) == 0.0f || dot(d, normalize(before[v])) < 0.0f);
    }
    CHECK(inward);
    ed.command("sculpt carve off");
    ed.command("undo");
    /* Pressure. */
    ed.command("sculpt pressure strength");
    ed.command(strprintf("sculpt dab %g %g 0.5", px.x, px.y));
    float half = 0.0f;
    centre_d = 1e9f;
    for (uint32_t v : ed.sculpt_last_ids_for_test()) {
      const float dist = length(before[v] - hit);
      if (dist < centre_d) centre_d = dist, half = length(mesh->positions[v] - before[v]);
    }
    std::printf("    at pressure 0.5 the centre moved %.4f (full: %.4f)\n", half, centre_move);
    CHECK(half > centre_move * 0.35f && half < centre_move * 0.65f);
    ed.command("sculpt off");
    CHECK(!ed.sculpt_for_test());
  });

  test("sculpt: Smooth after a Draw dab lowers the touched vertices' Laplacian energy and keeps the volume within 2 %; Grab moves the vertices under the first dab with the drag and nothing else", [] {
    Editor ed;
    GameObject *g = sphere_scene(ed);
    CHECK(g);
    if (!g) return;
    const MeshPtr mesh = g->mesh_filter()->mesh;
    const double v0 = volume(*mesh);
    const size_t steps0 = ed.undo_steps_for_test();
    const Vec2 px = pixel_of(ed, g->world_position());
    /* A spike: a tiny brush at full strength, five times on one spot. */
    ed.command("sculpt size 10");
    ed.command("sculpt strength 1");
    for (int i = 0; i < 5; i++) ed.command(strprintf("sculpt dab %g %g", px.x, px.y));
    ed.command("sculpt brush smooth");
    ed.command("sculpt size 25");
    step(ed);
    ed.command(strprintf("sculpt dab %g %g", px.x, px.y));
    ed.command("undo");  // (the first smooth dab, taken back: it told us the set the brush covers)
    const std::vector<uint32_t> ids = ed.sculpt_last_ids_for_test();
    const double e1 = laplacian_energy(*mesh, ids), v1 = volume(*mesh);
    for (int i = 0; i < 3; i++) ed.command(strprintf("sculpt dab %g %g", px.x, px.y));
    const double e2 = laplacian_energy(*mesh, ids), v2 = volume(*mesh);
    std::printf("    Laplacian energy %.3g -> %.3g; volume %.4f -> %.4f -> %.4f\n", e1, e2, v0, v1, v2);
    CHECK(e2 < e1 * 0.9 && std::fabs(v2 - v1) < 0.02 * std::fabs(v0));  // (a Laplacian step shrinks a convex region a little: Blender's smooth does the same)
    /* Grab, on the sphere as it was: every stroke taken back. */
    while (ed.undo_steps_for_test() > steps0) ed.command("undo");
    step(ed);  // (a frame: the depth plane shows the sphere as it is again)
    const std::vector<Vec3> before = mesh->positions;
    ed.command("sculpt brush grab");
    ed.command(strprintf("sculpt stroke %g %g %g %g 8", px.x, px.y, px.x + 40.0f, px.y));
    const std::vector<uint32_t> grabbed = ed.sculpt_last_ids_for_test();
    const Vec3 hit = ed.sculpt_last_hit_for_test();
    uint32_t centre = 0;
    float best = 1e9f;
    for (uint32_t v : grabbed)
      if (length(before[v] - hit) < best) best = length(before[v] - hit), centre = v;
    const Vec2 was = pixel_of(ed, g->world_matrix().point(before[centre])), now = pixel_of(ed, g->world_matrix().point(mesh->positions[centre]));
    std::printf("    the grabbed centre vertex moved from (%.1f, %.1f) to (%.1f, %.1f) px for a 40 px drag; %zu vertices changed of %zu grabbed\n", was.x, was.y, now.x, now.y,
                changed_count(before, mesh->positions), grabbed.size());
    CHECK(!grabbed.empty() && std::fabs(now.x - was.x - 40.0f) < 6.0f && std::fabs(now.y - was.y) < 6.0f && changed_count(before, mesh->positions) <= grabbed.size());
    ed.command("sculpt off");
  });

  test("sculpt: a stroke is one undo step restoring every position bitwise, redo re-applies, a stroke that moves nothing adds none; during a stroke on a 25k-vertex sphere no render mesh is built from scratch (in place instead); the modes exclude each other and Play refuses it", [] {
    Editor ed;
    GameObject *g = sphere_scene(ed, 3);
    CHECK(g);
    if (!g) return;
    const MeshPtr mesh = g->mesh_filter()->mesh;
    std::printf("    the sphere: %zu vertices\n", mesh->vert_count());
    const std::vector<Vec3> before = mesh->positions;
    const size_t steps = ed.undo_steps_for_test();
    const Vec2 px = pixel_of(ed, g->world_position());
    Vec3 hw;
    float hd = 0.0f;
    const Vec2 left = pixel_of(ed, g->world_position() + Vec3(-0.5f, 0, 0)), right = pixel_of(ed, g->world_position() + Vec3(0.5f, 0, 0));
    std::printf("    the sphere projects from x %.0f to %.0f (centre %.0f, %.0f); its id plane along that row:", left.x, right.x, px.x, px.y);
    int first = -1, last = -1;
    for (int x = 0; x < ed.scene_rect_for_test().w; x++)
      if (ed.scene_id_for_test(x, (int)px.y) == (uint32_t)g->id) last = x, first = first < 0 ? x : first;
    std::printf(" ids from %d to %d; hits:", first, last);
    for (int off = -40; off <= 40; off += 10) std::printf(" %+d:%d", off, (int)ed.sculpt_hit_for_test(px.x + off, px.y, hw, hd));
    std::printf("\n");
    const MeshCounts c0 = mesh_counts();
    const double t0 = wall_seconds();
    ed.command("sculpt size 25");
    ed.command(strprintf("sculpt stroke %g %g %g %g 12", px.x - 15.0f, px.y, px.x + 15.0f, px.y));
    const double t1 = wall_seconds();
    step(ed);
    step(ed);
    const MeshCounts c1 = mesh_counts();
    std::printf("    13 dabs in %.2f ms; render meshes built %llu -> %llu, moved in place %llu -> %llu\n", (t1 - t0) * 1000.0, (unsigned long long)c0.full_builds,
                (unsigned long long)c1.full_builds, (unsigned long long)c0.in_place, (unsigned long long)c1.in_place);
    const std::vector<Vec3> after = mesh->positions;
    CHECK(changed_count(before, after) > 0 && ed.undo_steps_for_test() == steps + 1);
    CHECK(c1.full_builds == c0.full_builds && c1.in_place > c0.in_place);
    ed.command("undo");
    CHECK(changed_count(before, mesh->positions) == 0 && ed.undo_steps_for_test() == steps);
    ed.command("redo");
    CHECK(changed_count(after, mesh->positions) == 0 && ed.undo_steps_for_test() == steps + 1);
    /* A dab over nothing: no step. */
    ed.command("sculpt dab 2 2");
    CHECK(ed.undo_steps_for_test() == steps + 1);
    /* The modes. */
    ed.command("edit");
    CHECK(!ed.sculpt_for_test());
    ed.command("edit off");
    ed.command("sculpt on");
    ed.command("weightpaint on");
    CHECK(!ed.sculpt_for_test() && ed.weight_paint_for_test());
    ed.command("sculpt on");
    CHECK(ed.sculpt_for_test() && !ed.weight_paint_for_test());
    ed.command("sculpt off");
    ed.command("play");
    ed.command("sculpt on");
    CHECK(!ed.sculpt_for_test());
    ed.command("stop");
  });
}

/* The brush driven by the pointer: press, moves, release over the Scene view (window pixels), with modifiers. */
static void tests_round256_pointer() {
  using platform::EventType;
  auto mouse = [](EventType t, int x, int y, int mods = 0) {
    platform::Event e;
    e.type = t, e.x = x, e.y = y, e.mods = mods;
    return e;
  };
  test("sculpt: the gather is complete - a dab's set equals a brute-force count of the vertices within the radius (Front Faces Only off); a drag over the Scene view is one stroke, Shift held smooths, Alt held carves, a stroke cut off by an undo leaves no trace, and Esc leaves the mode", [&] {
    Editor ed;
    GameObject *g = sphere_scene(ed);
    CHECK(g);
    if (!g) return;
    const MeshPtr mesh = g->mesh_filter()->mesh;
    const Recti s = ed.scene_rect_for_test();
    const Vec2 px = pixel_of(ed, g->world_position());
    ed.command("sculpt size 25");
    ed.command("sculpt strength 1");
    ed.command("sculpt front off");
    const std::vector<Vec3> before = mesh->positions;
    ed.command(strprintf("sculpt dab %g %g", px.x, px.y));
    const std::vector<uint32_t> ids = ed.sculpt_last_ids_for_test();
    const Vec3 hit = ed.sculpt_last_hit_for_test();
    const float r = ed.sculpt_last_radius_for_test();
    size_t within = 0;
    for (const Vec3 &p : before) within += length(p - hit) <= r ? 1 : 0;
    std::printf("    %zu vertices within %.3f m by brute force, %zu gathered\n", within, r, ids.size());
    CHECK(within == ids.size() && within > 0);
    ed.command("undo");
    ed.command("sculpt front on");
    /* A drag: press at the centre, four moves to the right, release. */
    const int wx = s.x + (int)px.x, wy = s.y + (int)px.y;
    const size_t steps = ed.undo_steps_for_test();
    step(ed, {mouse(EventType::MouseMove, wx, wy)});
    step(ed, {mouse(EventType::MouseDown, wx, wy)});
    for (int i = 1; i <= 4; i++) step(ed, {mouse(EventType::MouseMove, wx + 4 * i, wy)});
    step(ed, {mouse(EventType::MouseUp, wx + 16, wy)});
    step(ed);
    const size_t moved = changed_count(before, mesh->positions);
    std::printf("    the drag moved %zu vertices in one step (%zu -> %zu)\n", moved, steps, ed.undo_steps_for_test());
    CHECK(moved > 0 && ed.undo_steps_for_test() == steps + 1);
    bool outward = true;
    for (uint32_t v = 0; v < (uint32_t)mesh->vert_count(); v++) {
      const Vec3 d = mesh->positions[v] - before[v];
      outward = outward && (length(d) == 0.0f || dot(d, normalize(before[v])) > 0.0f);
    }
    CHECK(outward);
    /* Alt held: carved. */
    ed.command("undo");
    step(ed, {mouse(EventType::MouseMove, wx, wy, platform::MOD_ALT)});
    step(ed, {mouse(EventType::MouseDown, wx, wy, platform::MOD_ALT)});
    step(ed, {mouse(EventType::MouseMove, wx + 6, wy, platform::MOD_ALT)});
    step(ed, {mouse(EventType::MouseUp, wx + 6, wy, platform::MOD_ALT)});
    step(ed);
    bool inward = true;
    for (uint32_t v = 0; v < (uint32_t)mesh->vert_count(); v++) {
      const Vec3 d = mesh->positions[v] - before[v];
      inward = inward && (length(d) == 0.0f || dot(d, normalize(before[v])) < 0.0f);
    }
    CHECK(changed_count(before, mesh->positions) > 0 && inward);
    ed.command("undo");
    /* Shift held: smoothed - a spike first, then the drag with Shift lowers its energy. */
    ed.command("sculpt size 10");
    for (int i = 0; i < 5; i++) ed.command(strprintf("sculpt dab %g %g", px.x, px.y));
    ed.command("sculpt size 25");
    ed.command(strprintf("sculpt dab %g %g", px.x, px.y));
    ed.command("undo");
    const std::vector<uint32_t> set = ed.sculpt_last_ids_for_test();
    const double e1 = laplacian_energy(*mesh, set);
    const float dist_before = ed.scene_camera_for_test().distance;
    step(ed, {mouse(EventType::MouseMove, wx, wy, platform::MOD_SHIFT)});
    step(ed, {mouse(EventType::MouseDown, wx, wy, platform::MOD_SHIFT)});
    for (int i = 1; i <= 3; i++) step(ed, {mouse(EventType::MouseMove, wx + 2 * i, wy, platform::MOD_SHIFT)});
    step(ed, {mouse(EventType::MouseUp, wx + 6, wy, platform::MOD_SHIFT)});
    step(ed);
    const double e2 = laplacian_energy(*mesh, set);
    std::printf("    Shift-drag: Laplacian energy %.3g -> %.3g over %zu vertices; the camera stayed at %.3f (Shift / Alt with the left button are the brush's, two dabs no frame)\n", e1, e2, set.size(),
                ed.scene_camera_for_test().distance);
    CHECK(e2 < e1 && ed.scene_camera_for_test().distance == dist_before);
    while (ed.undo_steps_for_test() > steps) ed.command("undo");
    CHECK(changed_count(before, mesh->positions) == 0);
    /* A stroke cut off by an undo mid-drag: what it moved goes back, no step is made. */
    step(ed, {mouse(EventType::MouseMove, wx, wy)});
    step(ed, {mouse(EventType::MouseDown, wx, wy)});
    step(ed, {mouse(EventType::MouseMove, wx + 6, wy)});
    CHECK(changed_count(before, mesh->positions) > 0);
    ed.command("undo");
    step(ed, {mouse(EventType::MouseUp, wx + 6, wy)});
    step(ed);
    CHECK(changed_count(before, mesh->positions) == 0 && ed.undo_steps_for_test() <= steps);
    /* Esc over the view leaves the mode. */
    platform::Event esc;
    esc.type = EventType::KeyDown, esc.key = platform::KEY_ESCAPE;
    step(ed, {mouse(EventType::MouseMove, wx, wy)});
    step(ed, {esc});
    CHECK(!ed.sculpt_for_test());
  });
}

/* Task 0237: the brush set, symmetry, the lazy mouse, alphas, stroke types. */
static void tests_round257() {
  using platform::EventType;
  auto mouse = [](EventType t, int x, int y) {
    platform::Event e;
    e.type = t, e.x = x, e.y = y;
    return e;
  };
  /* The set's plane: its weighted centre and mean normal (as the brushes fit it), and the farthest a vertex stands off it. */
  auto plane_spread = [](const Mesh &m, const std::vector<uint32_t> &ids, Vec3 centre) {
    float worst = 0.0f;
    Vec3 n(0.0f);
    for (uint32_t v : ids) n += normalize(m.positions[v] - centre);  // (a sphere: radial)
    n = normalize(n);
    Vec3 c(0.0f);
    for (uint32_t v : ids) c += m.positions[v];
    c = c / (float)ids.size();
    for (uint32_t v : ids) worst = std::max(worst, std::fabs(dot(m.positions[v] - c, n)));
    return worst;
  };

  test("sculpt brushes: six Clay Buildup dabs on a plane rise less than six Draw dabs (layers toward a plane, not a peak); Flatten brings a spike's tip down; Pinch draws the set toward the centre; Snake Hook pulls a strand along the drag", [&] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.set_clock_for_test(100.0);
    ed.command("select Cube");
    ed.command("delete");
    ed.command("create Plane");
    ed.command("subdivide 3");  // (an 80 x 80 grid: many vertices under the brush)
    step(ed);
    for (int i = 0; i < 400 && ed.scene_camera_for_test().animating; i++) step(ed);
    ed.command("sculpt on");
    GameObject *g = ed.active_object_for_test();
    CHECK(g);
    if (!g) return;
    Vec3 w;
    float d;
    Vec2 px;
    for (int i = 0; i < 100; i++) {
      step(ed);
      px = pixel_of(ed, g->world_position());
      if (ed.sculpt_hit_for_test(px.x, px.y, w, d)) break;
    }
    const MeshPtr mesh = g->mesh_filter()->mesh;
    const std::vector<Vec3> flat = mesh->positions;
    const size_t steps0 = ed.undo_steps_for_test();
    ed.command("sculpt size 40");
    ed.command("sculpt strength 1");
    auto six_dabs = [&](const char *brush) {
      ed.command(std::string("sculpt brush ") + brush);
      for (int i = 0; i < 6; i++) ed.command(strprintf("sculpt dab %g %g", px.x, px.y));
      float top = 0.0f;
      size_t moved = 0;
      for (uint32_t v = 0; v < (uint32_t)mesh->vert_count(); v++) {
        const Vec3 dd = mesh->positions[v] - flat[v];
        if (length(dd) == 0.0f) continue;
        moved++, top = std::max(top, dd.y);
      }
      while (ed.undo_steps_for_test() > steps0) ed.command("undo");
      return std::make_pair(moved, top);
    };
    const auto draw = six_dabs("draw"), clay = six_dabs("clay");
    const float r = ed.sculpt_last_radius_for_test();
    std::printf("    six dabs on a plane: Draw piles %zu vertices up to %.4f, Clay Buildup %zu up to %.4f (a layer is %.4f)\n", draw.first, draw.second, clay.first, clay.second, 0.25f * r);
    CHECK(clay.first > 0 && clay.second < draw.second * 0.8f && clay.second >= 0.25f * r * 0.7f);  // (layers that fill toward a plane, not a peak)
    /* Flatten: a spike on a sphere, then flattened. */
    Editor es;
    GameObject *gs = sphere_scene(es);
    CHECK(gs);
    if (!gs) return;
    const MeshPtr sm = gs->mesh_filter()->mesh;
    const size_t ssteps0 = es.undo_steps_for_test();
    const Vec2 sp = pixel_of(es, gs->world_position());
    es.command("sculpt size 10");
    es.command("sculpt strength 1");
    for (int i = 0; i < 5; i++) es.command(strprintf("sculpt dab %g %g", sp.x, sp.y));
    es.command("sculpt brush flatten");
    es.command("sculpt size 25");
    step(es);  // (a frame: the depth plane shows the spike, so the dab lands on its tip)
    CHECK(es.sculpt_brush_kind_for_test() == 5);
    es.command(strprintf("sculpt dab %g %g", sp.x, sp.y));
    es.command("undo");
    const std::vector<uint32_t> set = es.sculpt_last_ids_for_test();
    auto peak = [&] {  // (the spike's tip: the farthest of the set from the sphere's centre)
      float worst = 0.0f;
      for (uint32_t v : set) worst = std::max(worst, length(sm->positions[v]));
      return worst;
    };
    const float spread1 = plane_spread(*sm, set, Vec3(0.0f)), peak1 = peak();
    uint32_t tip = 0;
    for (uint32_t v = 0; v < (uint32_t)sm->vert_count(); v++)
      if (length(sm->positions[v]) > length(sm->positions[tip])) tip = v;
    for (int i = 0; i < 3; i++) es.command(strprintf("sculpt dab %g %g", sp.x, sp.y));
    (void)tip;
    const float spread2 = plane_spread(*sm, set, Vec3(0.0f)), peak2 = peak();
    std::printf("    (flatten: the last dab gathered %zu, hit (%.2f %.2f %.2f) r %.3f)\n", es.sculpt_last_ids_for_test().size(), es.sculpt_last_hit_for_test().x, es.sculpt_last_hit_for_test().y,
                es.sculpt_last_hit_for_test().z, es.sculpt_last_radius_for_test());
    std::printf("    Flatten: the spike's tip %.3f -> %.3f from the centre (the sphere 0.5); the set's spread off its plane %.4f -> %.4f\n", peak1, peak2, spread1, spread2);
    CHECK(peak1 > 0.6f && peak2 < peak1 - 0.08f);
    /* Pinch: the set's vertices closer to its centre. */
    while (es.undo_steps_for_test() > ssteps0) es.command("undo");
    const std::vector<Vec3> round = sm->positions;
    es.command("sculpt brush pinch");
    es.command(strprintf("sculpt dab %g %g", sp.x, sp.y));
    const std::vector<uint32_t> pinched = es.sculpt_last_ids_for_test();
    Vec3 c(0.0f);
    for (uint32_t v : pinched) c += round[v];
    c = c / (float)pinched.size();
    double before = 0.0, after = 0.0;
    for (uint32_t v : pinched) before += length(round[v] - c), after += length(sm->positions[v] - c);
    std::printf("    Pinch: the mean distance to the centre %.4f -> %.4f over %zu vertices\n", before / pinched.size(), after / pinched.size(), pinched.size());
    CHECK(!pinched.empty() && after < before * 0.95);  // (a half-strength step at the centre, less toward the edge: about 8 % over the set)
    /* Snake Hook: a drag pulls the centre vertex along. */
    es.command("undo");
    es.command("sculpt brush snake");
    const Recti sr = es.scene_rect_for_test();
    const int wx = sr.x + (int)sp.x, wy = sr.y + (int)sp.y;
    uint32_t centre = 0;
    float best = 1e9f;
    for (uint32_t v = 0; v < (uint32_t)sm->vert_count(); v++)
      if (length(round[v] - es.sculpt_last_hit_for_test()) < best) best = length(round[v] - es.sculpt_last_hit_for_test()), centre = v;
    const Vec2 was = pixel_of(es, gs->world_matrix().point(sm->positions[centre]));
    step(es, {mouse(EventType::MouseMove, wx, wy)});
    step(es, {mouse(EventType::MouseDown, wx, wy)});
    for (int i = 1; i <= 8; i++) step(es, {mouse(EventType::MouseMove, wx + 5 * i, wy)});
    step(es, {mouse(EventType::MouseUp, wx + 40, wy)});
    step(es);
    const Vec2 now = pixel_of(es, gs->world_matrix().point(sm->positions[centre]));
    std::printf("    Snake Hook: the centre vertex went from x %.1f to %.1f for a 40 px drag (the stroke's place moves with the pointer, past the silhouette)\n", was.x, now.x);
    CHECK(now.x > was.x + 5.0f);  // (at the default strength the strand trails the pointer: each dab pulls what is still under the moving place by half)
  });

  test("sculpt symmetry: Mirror X moves the mirrored vertex by the mirrored amount; Radial 6 makes six bumps; the lazy mouse steadies a zigzag; a checker alpha moves only the vertices under its white squares", [&] {
    Editor ed;
    GameObject *g = sphere_scene(ed);
    CHECK(g);
    if (!g) return;
    const MeshPtr mesh = g->mesh_filter()->mesh;
    const std::vector<Vec3> before = mesh->positions;
    const Vec2 px = pixel_of(ed, g->world_position());
    ed.command("sculpt size 20");
    ed.command("sculpt strength 1");
    ed.command("sculpt front off");
    ed.command("sculpt symmetry x");
    ed.command(strprintf("sculpt dab %g %g", px.x + 12.0f, px.y));
    size_t moved = 0, mirrored_ok = 0, mirrored_total = 0;
    for (uint32_t v = 0; v < (uint32_t)mesh->vert_count(); v++) {
      const Vec3 d = mesh->positions[v] - before[v];
      if (length(d) == 0.0f) continue;
      moved++;
      const Vec3 want(-before[v].x, before[v].y, before[v].z);
      for (uint32_t u = 0; u < (uint32_t)mesh->vert_count(); u++) {
        if (length(before[u] - want) > 1e-4f) continue;
        mirrored_total++;
        const Vec3 du = mesh->positions[u] - before[u];
        mirrored_ok += length(du - Vec3(-d.x, d.y, d.z)) < 1e-4f ? 1 : 0;
      }
    }
    const size_t one_side = ed.sculpt_last_ids_for_test().size();
    std::printf("    Mirror X: %zu vertices moved (%zu under the dab itself); %zu of %zu mirrors moved by the mirrored amount\n", moved, one_side, mirrored_ok, mirrored_total);
    CHECK(moved > one_side && mirrored_total > 0 && mirrored_ok == mirrored_total);
    ed.command("undo");
    /* On the seam (the dab straddling X = 0): the seam's vertices move by the sum of both instances and stay on the plane. */
    ed.command(strprintf("sculpt dab %g %g", px.x, px.y));
    size_t seam = 0, seam_ok = 0;
    for (uint32_t v = 0; v < (uint32_t)mesh->vert_count(); v++) {
      if (std::fabs(before[v].x) > 1e-5f || length(mesh->positions[v] - before[v]) == 0.0f) continue;
      seam++;
      seam_ok += std::fabs(mesh->positions[v].x) < 1e-5f ? 1 : 0;
    }
    std::printf("    Mirror X on the seam: %zu seam vertices moved, %zu stayed on the plane\n", seam, seam_ok);
    CHECK(seam > 0 && seam_ok == seam);
    ed.command("undo");
    /* Front Faces Only with Radial 6: the instances on the far side face away from the real camera but not from the mirrored one - they move too. */
    ed.command("sculpt front on");
    ed.command("sculpt symmetry radial 6");
    ed.command(strprintf("sculpt dab %g %g", px.x + 12.0f, px.y));
    const size_t radial_front = changed_count(before, mesh->positions);
    ed.command("undo");
    ed.command("sculpt symmetry off");
    ed.command(strprintf("sculpt dab %g %g", px.x + 12.0f, px.y));
    const size_t single_front = changed_count(before, mesh->positions);
    ed.command("undo");
    std::printf("    Radial 6 with Front Faces Only: %zu vertices moved against %zu for one dab\n", radial_front, single_front);
    CHECK(radial_front >= single_front * 4);
    ed.command("sculpt front off");
    ed.command("sculpt symmetry off");
    ed.command(strprintf("sculpt dab %g %g", px.x + 12.0f, px.y));
    const size_t single = changed_count(before, mesh->positions);
    ed.command("undo");
    ed.command("sculpt symmetry radial 6");
    ed.command(strprintf("sculpt dab %g %g", px.x + 12.0f, px.y));
    const size_t six = changed_count(before, mesh->positions);
    std::printf("    Radial 6: %zu vertices moved against %zu for one dab\n", six, single);
    CHECK(six >= single * 4 && six <= single * 6);
    ed.command("undo");
    ed.command("sculpt symmetry off");
    /* The lazy mouse: a zigzag of the pointer dabs along a steadier line. */
    ed.command("sculpt lazy 30");
    const Recti s = ed.scene_rect_for_test();
    const int wx = s.x + (int)px.x, wy = s.y + (int)px.y;
    step(ed, {mouse(EventType::MouseMove, wx - 20, wy)});
    step(ed, {mouse(EventType::MouseDown, wx - 20, wy)});
    for (int i = 1; i <= 10; i++) step(ed, {mouse(EventType::MouseMove, wx - 20 + 4 * i, wy + (i % 2 ? 20 : -20))});
    step(ed, {mouse(EventType::MouseUp, wx + 20, wy)});
    step(ed);
    float wander = 0.0f;
    for (const Vec2 &p : ed.sculpt_dab_px_for_test()) wander = std::max(wander, std::fabs(p.y - px.y));
    std::printf("    the lazy mouse: %zu dabs, the farthest %.1f px off the line for a pointer 20 px off it\n", ed.sculpt_dabs_for_test(), wander);
    CHECK(ed.sculpt_dabs_for_test() > 0 && wander < 12.0f);
    ed.command("undo");
    ed.command("sculpt lazy 0");
    /* A checker alpha: two white and two black squares. */
    const std::string alpha = test_dir() + "/checker_alpha.png";
    const uint32_t checker[4] = {0xFFFFFFFF, 0xFF000000, 0xFF000000, 0xFFFFFFFF};
    CHECK(write_png(alpha, checker, 2, 2, 2));
    ed.command("sculpt alpha " + alpha);
    ed.command("sculpt alpha noturn");
    ed.command("sculpt size 25");
    ed.command(strprintf("sculpt dab %g %g", px.x, px.y));
    const size_t under = ed.sculpt_last_gathered_for_test(), with_alpha = changed_count(before, mesh->positions);
    std::printf("    the checker alpha: %zu vertices under the brush, %zu moved\n", under, with_alpha);
    CHECK(with_alpha > 0 && with_alpha < under);
    ed.command("sculpt alpha off");
    ed.command("undo");
  });

  test("sculpt stroke types: Drag Rect stamps one dab as wide as the drag at the release; Spray scatters its dabs in the circle; Freehand dabs at every move", [&] {
    Editor ed;
    GameObject *g = sphere_scene(ed);
    CHECK(g);
    if (!g) return;
    const Vec2 px = pixel_of(ed, g->world_position());
    const Recti s = ed.scene_rect_for_test();
    const int wx = s.x + (int)px.x, wy = s.y + (int)px.y;
    ed.command("sculpt size 10");
    ed.command("sculpt strength 1");
    ed.command(strprintf("sculpt dab %g %g", px.x, px.y));
    const float r10 = ed.sculpt_last_radius_for_test();
    ed.command("undo");
    ed.command("sculpt stroke rect");
    step(ed, {mouse(EventType::MouseMove, wx, wy)});
    step(ed, {mouse(EventType::MouseDown, wx, wy)});
    for (int i = 1; i <= 4; i++) step(ed, {mouse(EventType::MouseMove, wx + 5 * i, wy)});
    step(ed, {mouse(EventType::MouseUp, wx + 20, wy)});
    step(ed);
    std::printf("    Drag Rect: %zu dab(s), the radius %.3f for a 20 px drag (a 10 px brush: %.3f)\n", ed.sculpt_dabs_for_test(), ed.sculpt_last_radius_for_test(), r10);
    CHECK(ed.sculpt_dabs_for_test() == 1 && std::fabs(ed.sculpt_last_radius_for_test() - 2.0f * r10) < 0.25f * r10);
    ed.command("undo");
    ed.command("sculpt stroke spray");
    ed.command("sculpt size 30");
    step(ed, {mouse(EventType::MouseMove, wx, wy)});
    step(ed, {mouse(EventType::MouseDown, wx, wy)});
    step(ed, {mouse(EventType::MouseUp, wx, wy)});
    step(ed);
    float farthest = 0.0f;
    for (const Vec2 &p : ed.sculpt_dab_px_for_test()) farthest = std::max(farthest, length(p - px));
    std::printf("    Spray: %zu dabs, the farthest %.1f px from the press (the brush 30)\n", ed.sculpt_dabs_for_test(), farthest);
    CHECK(ed.sculpt_dabs_for_test() >= 8 && farthest <= 30.0f * ed.ui_for_test().scale + 1.0f);
    ed.command("undo");
    ed.command("sculpt stroke freehand");
    step(ed, {mouse(EventType::MouseMove, wx, wy)});
    step(ed, {mouse(EventType::MouseDown, wx, wy)});
    for (int i = 1; i <= 3; i++) step(ed, {mouse(EventType::MouseMove, wx + 2 * i, wy)});
    step(ed, {mouse(EventType::MouseUp, wx + 6, wy)});
    step(ed);
    std::printf("    Freehand: %zu dabs for a press and three 2-px moves\n", ed.sculpt_dabs_for_test());
    CHECK(ed.sculpt_dabs_for_test() == 4);
    ed.command("undo");
    /* Drag Dot: one dab that ends where the pointer let go - the step holds the last place only. */
    const MeshPtr mesh = g->mesh_filter()->mesh;
    const std::vector<Vec3> before = mesh->positions;
    ed.command("sculpt stroke dot");
    ed.command("sculpt size 15");
    const size_t steps = ed.undo_steps_for_test();
    step(ed, {mouse(EventType::MouseMove, wx - 12, wy)});
    step(ed, {mouse(EventType::MouseDown, wx - 12, wy)});
    for (int i = 1; i <= 6; i++) step(ed, {mouse(EventType::MouseMove, wx - 12 + 4 * i, wy)});
    step(ed, {mouse(EventType::MouseUp, wx + 12, wy)});
    step(ed);
    const Vec3 hit = ed.sculpt_last_hit_for_test();
    const float r = ed.sculpt_last_radius_for_test();
    size_t moved = 0, near = 0;
    for (uint32_t v = 0; v < (uint32_t)mesh->vert_count(); v++) {
      if (length(mesh->positions[v] - before[v]) == 0.0f) continue;
      moved++, near += length(before[v] - hit) <= r * 1.001f ? 1 : 0;
    }
    std::printf("    Drag Dot: %zu vertices moved, %zu of them within the last dab's radius; %zu step(s)\n", moved, near, ed.undo_steps_for_test() - steps);
    CHECK(moved > 0 && near == moved && ed.undo_steps_for_test() == steps + 1);
    ed.command("sculpt stroke dots");
    ed.command("sculpt off");
  });
}

void run_sculpt_tests() {
  tests_round256();
  tests_round256_pointer();
  tests_round257();
}
