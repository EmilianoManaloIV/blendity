// SPDX-License-Identifier: GPL-2.0-or-later
// Task 0236 (ADR 0074): Sculpt Mode - the brushes touch only what is under them; a stroke's undo is a delta.
#include "test_common.h"

#include "../src/scene/mesh.h"
#include "../src/scene/subdiv_chain.h"
#include "../src/scene/dynamesh.h"
#include "../src/render/canvas.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>
#include <unordered_map>
#include <tuple>

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

static void tests_round258() {
  using platform::EventType;
  auto mouse = [](EventType t, int x, int y, int mods = 0) {
    platform::Event e;
    e.type = t, e.x = x, e.y = y, e.mods = mods;
    return e;
  };
  /* The Scene view pixel at the sphere's centre once the brush can hit it there. */
  auto centre_px = [](Editor &ed, GameObject *g) {
    Vec2 px;
    Vec3 w;
    float d;
    for (int i = 0; i < 100; i++) {
      step(ed);
      px = pixel_of(ed, g->world_position());
      if (ed.sculpt_hit_for_test(px.x, px.y, w, d)) break;
    }
    return px;
  };
  /* Two spheres a metre and a half apart joined into one object, Sculpt on it. */
  auto two_spheres = [&](Editor &ed) -> GameObject * {
    GameObject *a = sphere_scene(ed, 1);
    if (!a) return nullptr;
    ed.command("sculpt off");
    ed.command("create Sphere");
    GameObject *b = ed.active_object_for_test();
    if (!b) return nullptr;
    b->set_world_position(a->world_position() + Vec3(1.5f, 0.0f, 0.0f));
    ed.command("subdivide 1");
    ed.select_object(b->id);
    ed.select_object_add(a->id);  // (A active: it keeps the result)
    ed.command("join");
    GameObject *j = ed.active_object_for_test();
    if (!j) return nullptr;
    step(ed);
    for (int i = 0; i < 400 && ed.scene_camera_for_test().animating; i++) step(ed);
    ed.command("sculpt on");
    centre_px(ed, j);
    return j;
  };

  test("sculpt mask: a filled mask holds the surface against a Draw dab, a half mask halves it; a mask stroke is one delta step (no snapshot) that undoes and redoes exactly; Ctrl paints it, Ctrl+Alt takes it away", [&] {
    Editor ed;
    GameObject *g = sphere_scene(ed, 2);
    CHECK(g);
    if (!g) return;
    auto M = [&] { return g->mesh_filter()->mesh; };  // (an operation gives the object a mesh of its own: read it fresh)
    const Vec2 px = pixel_of(ed, g->world_position());
    ed.command("sculpt size 40");
    ed.command("sculpt strength 1");
    ed.command("sculpt falloff constant");
    /* The whole mask: nothing moves. */
    const std::vector<Vec3> round = M()->positions;
    ed.command("sculpt mask fill");
    ed.command("sculpt brush draw");
    ed.command(strprintf("sculpt dab %g %g", px.x, px.y));
    const size_t held = changed_count(round, M()->positions);
    CHECK(M()->mask_fits() && held == 0);
    ed.command("sculpt mask clear");
    /* The unmasked dab's move at the centre, then the same dab behind a half mask. */
    ed.command(strprintf("sculpt dab %g %g", px.x, px.y));
    const std::vector<uint32_t> ids = ed.sculpt_last_ids_for_test();
    uint32_t centre = ids.empty() ? 0 : ids[0];
    float best = 1e9f;
    for (uint32_t v : ids)
      if (length(round[v] - ed.sculpt_last_hit_for_test()) < best) best = length(round[v] - ed.sculpt_last_hit_for_test()), centre = v;
    const float full_move = length(M()->positions[centre] - round[centre]);
    ed.command("undo");
    step(ed);
    const size_t steps0 = ed.undo_steps_for_test(), snaps0 = ed.sculpt_undo_snapshots_for_test();
    ed.command("sculpt brush mask");
    ed.command("sculpt strength 0.5");
    ed.command(strprintf("sculpt dab %g %g", px.x, px.y));
    const std::vector<float> half = M()->sculpt_mask;
    size_t masked = 0;
    for (float v : half) masked += v > 0.0f ? 1 : 0;
    CHECK(masked > 0 && std::fabs(half[centre] - 0.5f) < 1e-4f);
    CHECK(ed.undo_steps_for_test() == steps0 + 1 && ed.sculpt_undo_snapshots_for_test() == snaps0);  // (a delta step, no snapshot)
    ed.command("sculpt brush draw");
    ed.command("sculpt strength 1");
    ed.command(strprintf("sculpt dab %g %g", px.x, px.y));
    const float half_move = length(M()->positions[centre] - round[centre]);
    std::printf("    the centre vertex moved %.5f unmasked, %.5f behind a half mask (%zu vertices masked)\n", full_move, half_move, masked);
    CHECK(full_move > 0.0f && std::fabs(half_move / full_move - 0.5f) < 0.05f);
    ed.command("undo");  // (the Draw dab)
    ed.command("undo");  // (the mask stroke)
    bool back = M()->mask_fits();
    for (float v : M()->sculpt_mask) back = back && v == 0.0f;
    CHECK(back && ed.undo_steps_for_test() == steps0);
    ed.command("redo");
    CHECK(M()->sculpt_mask == half);
    ed.command("undo");
    step(ed);
    /* Ctrl with the Draw brush paints the mask; Ctrl+Alt takes it away. */
    const Recti sr = ed.scene_rect_for_test();
    const int wx = sr.x + (int)px.x, wy = sr.y + (int)px.y;
    step(ed, {mouse(EventType::MouseMove, wx, wy, platform::MOD_CTRL)});
    step(ed, {mouse(EventType::MouseDown, wx, wy, platform::MOD_CTRL)});
    step(ed, {mouse(EventType::MouseUp, wx, wy, platform::MOD_CTRL)});
    step(ed);
    size_t ctrl_masked = 0;
    for (float v : M()->sculpt_mask) ctrl_masked += v > 0.0f ? 1 : 0;
    CHECK(ctrl_masked > 0 && changed_count(round, M()->positions) == 0);
    step(ed, {mouse(EventType::MouseMove, wx, wy, platform::MOD_CTRL | platform::MOD_ALT)});
    step(ed, {mouse(EventType::MouseDown, wx, wy, platform::MOD_CTRL | platform::MOD_ALT)});
    step(ed, {mouse(EventType::MouseUp, wx, wy, platform::MOD_CTRL | platform::MOD_ALT)});
    step(ed);
    size_t left = 0;
    for (float v : M()->sculpt_mask) left += v > 0.0f ? 1 : 0;
    std::printf("    Ctrl masked %zu vertices, Ctrl+Alt left %zu masked\n", ctrl_masked, left);
    CHECK(left == 0);
  });

  test("sculpt mask operations: fill and invert, grow by one ring, blur lowers the steepest step, a rectangle masks exactly the vertices projected into it; each an ordinary step", [&] {
    Editor ed;
    GameObject *g = sphere_scene(ed, 2);
    CHECK(g);
    if (!g) return;
    auto M = [&] { return g->mesh_filter()->mesh; };  // (an operation gives the object a mesh of its own: read it fresh)
    const Vec2 px = pixel_of(ed, g->world_position());
    const size_t snaps0 = ed.sculpt_undo_snapshots_for_test();
    ed.command("sculpt mask fill");
    bool all = M()->mask_fits();
    for (float v : M()->sculpt_mask) all = all && v == 1.0f;
    ed.command("sculpt mask invert");
    bool none = M()->mask_fits();
    for (float v : M()->sculpt_mask) none = none && v == 0.0f;
    CHECK(all && none);
    /* A hard-edged patch: grow adds its ring; blur softens its edge. */
    ed.command("sculpt size 30");
    ed.command("sculpt strength 1");
    ed.command("sculpt falloff constant");
    ed.command("sculpt brush mask");
    ed.command(strprintf("sculpt dab %g %g", px.x, px.y));  // (the stroke commits the pending step: fill and invert are two snapshots now)
    CHECK(ed.sculpt_undo_snapshots_for_test() == snaps0 + 2);
    const std::vector<float> patch = M()->sculpt_mask;
    const Mesh::VertexFaces &vf = M()->vertex_faces();
    auto ring_of = [&](uint32_t v, std::vector<uint32_t> &out) {  // (the edge neighbours, as the brush's ring: not a quad's far corner)
      out.clear();
      for (uint32_t k = vf.offsets[v]; k < vf.offsets[v + 1]; k++) {
        const size_t f = vf.faces[k];
        const uint32_t n = M()->face_size(f), *fv = M()->face_verts(f);
        for (uint32_t c = 0; c < n; c++)
          if (fv[c] == v) out.push_back(fv[(c + 1) % n]), out.push_back(fv[(c + n - 1) % n]);
      }
    };
    auto steepest = [&](const std::vector<float> &mk) {
      float worst = 0.0f;
      std::vector<uint32_t> ring;
      for (uint32_t v = 0; v < (uint32_t)M()->vert_count(); v++) {
        ring_of(v, ring);
        for (uint32_t nb : ring) worst = std::max(worst, std::fabs(mk[v] - mk[nb]));
      }
      return worst;
    };
    ed.command("sculpt mask grow");
    std::vector<uint32_t> ring;
    size_t ring_ok = 0, ring_total = 0;
    for (uint32_t v = 0; v < (uint32_t)M()->vert_count(); v++) {
      if (patch[v] < 1.0f) continue;
      ring_of(v, ring);
      for (uint32_t nb : ring) ring_total++, ring_ok += M()->sculpt_mask[nb] >= 1.0f ? 1 : 0;
    }
    CHECK(ring_total > 0 && ring_ok == ring_total);
    const float before_blur = steepest(M()->sculpt_mask);
    ed.command("sculpt mask blur");
    const float after_blur = steepest(M()->sculpt_mask);
    std::printf("    grow: %zu of %zu ring vertices masked; blur: the steepest step %.3f -> %.3f\n", ring_ok, ring_total, before_blur, after_blur);
    CHECK(before_blur >= 0.99f && after_blur < before_blur * 0.8f);
    /* The rectangle, against the test's own projection (Front Faces Only off: every vertex in it). */
    ed.command("sculpt mask clear");
    ed.command("sculpt front off");
    const float x0 = px.x - 25.0f, y0 = px.y - 40.0f, x1 = px.x + 25.0f, y1 = px.y + 10.0f;
    ed.command(strprintf("sculpt mask rect %g %g %g %g", x0, y0, x1, y1));
    size_t in = 0, wrong = 0;
    for (uint32_t v = 0; v < (uint32_t)M()->vert_count(); v++) {
      Vec2 p;
      const bool proj = ed.project_for_test(g->world_matrix().point(M()->positions[v]), p);
      const bool inside = proj && p.x >= x0 && p.x <= x1 && p.y >= y0 && p.y <= y1;
      in += inside ? 1 : 0;
      wrong += (M()->sculpt_mask[v] == 1.0f) != inside ? 1 : 0;
    }
    std::printf("    rect: %zu vertices project into it, %zu disagree with the mask\n", in, wrong);
    CHECK(in > 0 && wrong == 0);
  });

  test("sculpt hiding and groups: islands numbered, a hidden group leaves the render mesh and the id plane, is neither dabbed nor gathered, comes back with show all; the mask and the groups round-trip through the scene file", [&] {
    Editor ed;
    GameObject *g = two_spheres(ed);
    CHECK(g);
    if (!g) return;
    auto M = [&] { return g->mesh_filter()->mesh; };  // (an operation gives the object a mesh of its own: read it fresh)
    ed.command("sculpt group islands");
    std::set<int> groups;
    for (size_t f = 0; f < M()->face_count(); f++) groups.insert(M()->group_of(f));
    CHECK(groups.size() == 2);
    const size_t all_tris = M()->render_mesh().indices.size() / 3;
    size_t tris0 = 0;
    for (size_t f = 0; f < M()->face_count(); f++) tris0 += M()->group_of(f) == 0 ? M()->face_size(f) - 2 : 0;
    /* Which island is which: the centre of the group-1 faces. */
    Vec3 c1(0.0f);
    size_t n1 = 0;
    for (size_t f = 0; f < M()->face_count(); f++) {
      if (M()->group_of(f) != 1) continue;
      for (uint32_t c = 0; c < M()->face_size(f); c++) c1 += M()->positions[M()->face_verts(f)[c]], n1++;
    }
    c1 = c1 / (float)std::max<size_t>(1, n1);
    const Vec3 c1_world = g->world_matrix().point(c1);
    ed.command("sculpt hide group 1");
    const size_t shown_tris = M()->render_mesh().indices.size() / 3;
    std::printf("    %zu triangles, %zu with group 1 hidden (group 0 has %zu)\n", all_tris, shown_tris, tris0);
    CHECK(shown_tris == tris0 && shown_tris < all_tris);
    for (int i = 0; i < 20; i++) step(ed);  // (the view draws the render mesh again)
    const Vec2 p1 = pixel_of(ed, c1_world);
    const uint32_t id_hidden = ed.scene_id_for_test((int)p1.x, (int)p1.y);
    const std::vector<Vec3> before = M()->positions;
    const size_t steps0 = ed.undo_steps_for_test();
    ed.command("sculpt size 40");
    ed.command("sculpt strength 1");
    ed.command(strprintf("sculpt dab %g %g", p1.x, p1.y));
    std::printf("    the hidden island's centre pixel reads id %u (the object is %u); a dab there moved %zu vertices and made %zu step(s)\n", id_hidden, (uint32_t)g->id,
                changed_count(before, M()->positions), ed.undo_steps_for_test() - steps0);
    CHECK(id_hidden != (uint32_t)g->id && changed_count(before, M()->positions) == 0 && ed.undo_steps_for_test() == steps0);  // (what is behind shows through: another object, or nothing)
    /* A brush on the shown island wide enough to reach the hidden one gathers none of it. */
    const Vec2 p0 = pixel_of(ed, g->world_position());
    ed.command("sculpt size 600");
    ed.command(strprintf("sculpt dab %g %g", p0.x, p0.y));
    size_t hidden_moved = 0, shown_moved = 0;
    std::vector<uint8_t> vert_group1(M()->vert_count(), 0);
    for (size_t f = 0; f < M()->face_count(); f++)
      if (M()->group_of(f) == 1)
        for (uint32_t c = 0; c < M()->face_size(f); c++) vert_group1[M()->face_verts(f)[c]] = 1;
    for (uint32_t v = 0; v < (uint32_t)M()->vert_count(); v++) {
      if (length(M()->positions[v] - before[v]) == 0.0f) continue;
      (vert_group1[v] ? hidden_moved : shown_moved)++;
    }
    std::printf("    a 600 px dab on the shown island moved %zu of its vertices and %zu of the hidden island's\n", shown_moved, hidden_moved);
    CHECK(shown_moved > 0 && hidden_moved == 0);
    ed.command("undo");
    ed.command("sculpt show all");
    CHECK(M()->render_mesh().indices.size() / 3 == all_tris && !M()->any_hidden());
    /* The file: the mask and the groups come back. */
    ed.command("sculpt size 40");
    ed.command("sculpt brush mask");
    ed.command(strprintf("sculpt dab %g %g", p0.x, p0.y));
    Scene back;
    std::string err;
    const std::string text = save_scene_text(ed.scene());
    size_t pg_lines = 0, mk_lines = 0;
    for (size_t at = text.find("\npg "); at != std::string::npos; at = text.find("\npg ", at + 1)) pg_lines++;
    for (size_t at = text.find("\nmk "); at != std::string::npos; at = text.find("\nmk ", at + 1)) mk_lines++;
    std::printf("    the file holds %zu pg lines and %zu mk lines\n", pg_lines, mk_lines);
    CHECK(load_scene_text(text, back, err));
    GameObject *gb = back.find(g->id);  // (by id: the sample scene has a Sphere of its own)
    const Mesh *mb = gb && gb->mesh_filter() ? gb->mesh_filter()->mesh.get() : nullptr;
    CHECK(mb && mb->vert_count() == M()->vert_count());
    if (!mb || mb->vert_count() != M()->vert_count()) return;
    bool same_mask = mb->sculpt_mask.size() == M()->sculpt_mask.size();
    for (size_t i = 0; same_mask && i < mb->sculpt_mask.size(); i++) same_mask = std::fabs(mb->sculpt_mask[i] - M()->sculpt_mask[i]) < 1e-6f;
    std::printf("    the file: %zu mask values and %zu groups back\n", mb->sculpt_mask.size(), mb->face_group.size());
    CHECK(same_mask && mb->face_group == M()->face_group);
  });

  test("sculpt isolate and colours: Ctrl+Shift+click on an island hides the other group; the groups view gives the islands different colours; the mask darkens", [&] {
    Editor ed;
    GameObject *g = two_spheres(ed);
    CHECK(g);
    if (!g) return;
    auto M = [&] { return g->mesh_filter()->mesh; };  // (an operation gives the object a mesh of its own: read it fresh)
    ed.command("sculpt group islands");
    const Vec2 p0 = pixel_of(ed, g->world_position());
    const int g0 = [&] {  // (the group under the object's centre pixel: the first sphere's)
      Vec3 w;
      float d;
      if (!ed.sculpt_hit_for_test(p0.x, p0.y, w, d)) return -1;
      const Vec3 hl = g->world_matrix().inverse().point(w);
      size_t best_f = 0;
      float best = 1e30f;
      for (size_t f = 0; f < M()->face_count(); f++) {
        Vec3 c(0.0f);
        for (uint32_t k = 0; k < M()->face_size(f); k++) c += M()->positions[M()->face_verts(f)[k]];
        const float dd = length_sq(c / (float)M()->face_size(f) - hl);
        if (dd < best) best = dd, best_f = f;
      }
      return M()->group_of(best_f);
    }();
    const Recti sr = ed.scene_rect_for_test();
    step(ed, {mouse(EventType::MouseMove, sr.x + (int)p0.x, sr.y + (int)p0.y, platform::MOD_CTRL | platform::MOD_SHIFT)});
    step(ed, {mouse(EventType::MouseDown, sr.x + (int)p0.x, sr.y + (int)p0.y, platform::MOD_CTRL | platform::MOD_SHIFT)});
    step(ed, {mouse(EventType::MouseUp, sr.x + (int)p0.x, sr.y + (int)p0.y, platform::MOD_CTRL | platform::MOD_SHIFT)});
    step(ed);
    size_t hidden_other = 0, hidden_own = 0, other = 0;
    for (size_t f = 0; f < M()->face_count(); f++) {
      if (M()->group_of(f) == g0) hidden_own += M()->face_hidden(f) ? 1 : 0;
      else other++, hidden_other += M()->face_hidden(f) ? 1 : 0;
    }
    std::printf("    isolate: group %d clicked; %zu of %zu other faces hidden, %zu of its own\n", g0, hidden_other, other, hidden_own);
    CHECK(g0 >= 0 && other > 0 && hidden_other == other && hidden_own == 0);
    ed.command("sculpt show all");
    /* The colours. */
    ed.command("sculpt groups on");
    const std::vector<Vec3> *cols = ed.sculpt_colors_for_test(*M());
    std::set<std::tuple<int, int, int>> distinct;
    if (cols)
      for (const Vec3 &c : *cols) distinct.insert({(int)(c.x * 255), (int)(c.y * 255), (int)(c.z * 255)});
    CHECK(cols && distinct.size() >= 2);
    ed.command("sculpt groups off");
    ed.command("sculpt mask fill");
    const std::vector<Vec3> *dark = ed.sculpt_colors_for_test(*M());
    bool darker = dark && !dark->empty();
    if (dark)
      for (const Vec3 &c : *dark) darker = darker && c.x < 0.8f && c.y < 0.8f && c.z < 0.8f;
    std::printf("    %zu distinct group colours; the filled mask darkens every vertex: %d\n", distinct.size(), (int)darker);
    CHECK(darker);
  });

  test("sculpt mask gestures and the rest of the operations: Ctrl+click off the model inverts, Ctrl+drag masks a rectangle (front faces only when on), Esc drops a gesture, Shift with the Mask brush blurs, a Drag Dot mask stroke leaves the mask outside its last dot alone and undoes exactly; fill, sharpen, shrink; group from mask, by material, merged; hide masked, unmasked, all, invert", [&] {
    Editor ed;
    GameObject *g = sphere_scene(ed, 2);
    CHECK(g);
    if (!g) return;
    const uint64_t gid = g->id;
    auto G = [&] { return ed.scene().find(gid); };  // (an undo of a snapshot step rebuilds the scene: the object is looked up again)
    auto M = [&] { return G()->mesh_filter()->mesh; };
    const Vec2 px = pixel_of(ed, g->world_position());
    const float R = length(pixel_of(ed, g->world_position() + Vec3(0.0f, 0.5f, 0.0f)) - px);  // (the sphere's radius on screen)
    const Recti sr = ed.scene_rect_for_test();
    const int wx = sr.x + (int)px.x, wy = sr.y + (int)px.y;
    std::printf("    the sphere is %.0f px across on screen\n", 2.0f * R);
    ed.command("sculpt front off");
    /* Off the model: the view's corner, far from the sphere. */
    const int ox = sr.x + 30, oy = sr.y + 30;
    Vec3 w;
    float d;
    CHECK(!ed.sculpt_hit_for_test(30.0f, 30.0f, w, d));
    ed.command("sculpt mask clear");
    const size_t steps0 = ed.undo_steps_for_test();
    step(ed, {mouse(EventType::MouseMove, ox, oy, platform::MOD_CTRL)});
    step(ed, {mouse(EventType::MouseDown, ox, oy, platform::MOD_CTRL)});
    step(ed, {mouse(EventType::MouseUp, ox, oy, platform::MOD_CTRL)});
    step(ed, {mouse(EventType::MouseMove, wx, wy)});  // (the modifiers let go)
    step(ed);
    bool all_one = M()->mask_fits();
    for (float v : M()->sculpt_mask) all_one = all_one && v == 1.0f;
    CHECK(all_one);  // (Ctrl+click off the model: the empty mask inverted to full)
    ed.command("sculpt mask clear");
    /* Ctrl+drag off the model: the rectangle, dropped by Esc first. */
    step(ed, {mouse(EventType::MouseMove, ox, oy, platform::MOD_CTRL)});
    step(ed, {mouse(EventType::MouseDown, ox, oy, platform::MOD_CTRL)});
    step(ed, {mouse(EventType::MouseMove, wx, wy, platform::MOD_CTRL)});
    {
      platform::Event esc;
      esc.type = EventType::KeyDown, esc.key = platform::KEY_ESCAPE;
      step(ed, {esc});
    }
    step(ed, {mouse(EventType::MouseUp, wx, wy, platform::MOD_CTRL)});
    step(ed, {mouse(EventType::MouseMove, wx, wy)});  // (the modifiers let go)
    step(ed);
    size_t masked = 0;
    for (float v : M()->sculpt_mask) masked += v > 0.0f ? 1 : 0;
    CHECK(masked == 0);  // (Esc: nothing masked)
    step(ed, {mouse(EventType::MouseMove, ox, oy, platform::MOD_CTRL)});
    step(ed, {mouse(EventType::MouseDown, ox, oy, platform::MOD_CTRL)});
    step(ed, {mouse(EventType::MouseMove, wx, wy, platform::MOD_CTRL)});
    step(ed, {mouse(EventType::MouseUp, wx, wy, platform::MOD_CTRL)});
    step(ed, {mouse(EventType::MouseMove, wx, wy)});  // (the modifiers let go)
    step(ed);
    size_t in_rect = 0;
    masked = 0;
    for (uint32_t v = 0; v < (uint32_t)M()->vert_count(); v++) {
      Vec2 p;
      const bool proj = ed.project_for_test(G()->world_matrix().point(M()->positions[v]), p);
      in_rect += proj && p.x >= 30.0f && p.x <= (float)(int)px.x && p.y >= 30.0f && p.y <= (float)(int)px.y ? 1 : 0;  // (the drag ended on the whole pixel wx, wy)
      masked += M()->sculpt_mask[v] == 1.0f ? 1 : 0;
    }
    std::printf("    Ctrl+drag: %zu vertices project into the rectangle, %zu masked\n", in_rect, masked);
    CHECK(in_rect > 0 && masked == in_rect);
    /* Front Faces Only: the rectangle takes fewer (the back of the sphere left out). */
    ed.command("sculpt mask clear");
    ed.command("sculpt front on");
    ed.command(strprintf("sculpt mask rect %g %g %g %g", px.x - R * 0.4f, px.y - R * 0.4f, px.x + R * 0.4f, px.y + R * 0.4f));
    size_t front_masked = 0;
    for (float v : M()->sculpt_mask) front_masked += v == 1.0f ? 1 : 0;
    ed.command("sculpt mask clear");
    ed.command("sculpt front off");
    ed.command(strprintf("sculpt mask rect %g %g %g %g", px.x - R * 0.4f, px.y - R * 0.4f, px.x + R * 0.4f, px.y + R * 0.4f));
    size_t both_masked = 0;
    for (float v : M()->sculpt_mask) both_masked += v == 1.0f ? 1 : 0;
    std::printf("    the rectangle masks %zu front-facing vertices, %zu with the back too\n", front_masked, both_masked);
    CHECK(front_masked > 0 && front_masked < both_masked);
    /* Shift with the Mask brush blurs the rectangle's edge. */
    const Mesh::VertexFaces &vf = M()->vertex_faces();
    auto steepest = [&]() {
      float worst = 0.0f;
      const MeshPtr m = M();
      for (uint32_t v = 0; v < (uint32_t)m->vert_count(); v++)
        for (uint32_t k = vf.offsets[v]; k < vf.offsets[v + 1]; k++) {
          const size_t f = vf.faces[k];
          for (uint32_t c = 0; c < m->face_size(f); c++) worst = std::max(worst, std::fabs(m->sculpt_mask[v] - m->sculpt_mask[m->face_verts(f)[c]]));
        }
      return worst;
    };
    ed.command("sculpt mask clear");  // (a hard-edged disc, then Shift blurs its edge with a wider brush)
    ed.command("sculpt brush mask");
    ed.command("sculpt falloff constant");
    ed.command("sculpt strength 1");
    ed.command(strprintf("sculpt size %g", std::max(6.0f, R * 0.3f)));
    ed.command(strprintf("sculpt dab %g %g", px.x, px.y));
    const float before_blur = steepest();
    ed.command(strprintf("sculpt size %g", std::max(10.0f, R * 0.6f)));
    step(ed, {mouse(EventType::MouseMove, wx, wy, platform::MOD_SHIFT)});
    step(ed, {mouse(EventType::MouseDown, wx, wy, platform::MOD_SHIFT)});
    step(ed, {mouse(EventType::MouseUp, wx, wy, platform::MOD_SHIFT)});
    step(ed, {mouse(EventType::MouseMove, wx, wy)});  // (the modifiers let go)
    step(ed);
    const float after_blur = steepest();
    std::printf("    Shift with the Mask brush: the steepest step %.3f -> %.3f\n", before_blur, after_blur);
    CHECK(before_blur >= 0.99f && after_blur < before_blur);
    /* Drag Dot with the mask: a half mask under A, then Ctrl+Alt dragged from A to B takes the mask only under the last dot. */
    ed.command("sculpt mask clear");
    ed.command("sculpt falloff constant");
    ed.command(strprintf("sculpt size %g", std::max(5.0f, R * 0.25f)));
    ed.command("sculpt strength 1");
    const int dx = std::max(4, (int)(R * 0.45f));  // (A and B on the sphere, either side of the centre)
    ed.command(strprintf("sculpt dab %g %g", px.x - (float)dx, px.y));  // (A: masked 1)
    ed.command(strprintf("sculpt dab %g %g", px.x + (float)dx, px.y));  // (B: masked 1)
    const std::vector<float> two_dots = M()->sculpt_mask;
    const size_t steps_dot = ed.undo_steps_for_test();
    ed.command("sculpt stroke dot");
    ed.command("sculpt brush draw");  // (Ctrl makes it a mask stroke anyway)
    const int ax = wx - dx, bx = wx + dx;
    step(ed, {mouse(EventType::MouseMove, ax, wy, platform::MOD_CTRL | platform::MOD_ALT)});
    step(ed, {mouse(EventType::MouseDown, ax, wy, platform::MOD_CTRL | platform::MOD_ALT)});
    for (int i = 1; i <= 8; i++) step(ed, {mouse(EventType::MouseMove, ax + (2 * dx * i) / 8, wy, platform::MOD_CTRL | platform::MOD_ALT)});
    step(ed, {mouse(EventType::MouseUp, bx, wy, platform::MOD_CTRL | platform::MOD_ALT)});
    step(ed, {mouse(EventType::MouseMove, wx, wy)});  // (the modifiers let go)
    step(ed);
    const Vec3 last_hit = ed.sculpt_last_hit_for_test();
    const float r = ed.sculpt_last_radius_for_test();
    size_t wrong_outside = 0, cleared_inside = 0;
    for (uint32_t v = 0; v < (uint32_t)M()->vert_count(); v++) {
      const bool inside = length(M()->positions[v] - last_hit) <= r * 1.001f;
      if (!inside && M()->sculpt_mask[v] != two_dots[v]) wrong_outside++;
      if (inside && two_dots[v] > 0.0f && M()->sculpt_mask[v] == 0.0f) cleared_inside++;
    }
    std::printf("    Drag Dot (Ctrl+Alt) from A to B: %zu vertices outside the last dot changed, %zu under it cleared; %zu step(s)\n", wrong_outside, cleared_inside,
                ed.undo_steps_for_test() - steps_dot);
    CHECK(wrong_outside == 0 && cleared_inside > 0 && ed.undo_steps_for_test() == steps_dot + 1);
    ed.command("undo");
    CHECK(M()->sculpt_mask == two_dots);
    ed.command("sculpt stroke dots");
    /* Fill, sharpen, shrink. */
    ed.command("sculpt mask fill");
    bool filled = true;
    for (float v : M()->sculpt_mask) filled = filled && v == 1.0f;
    ed.command("sculpt mask clear");
    ed.command("sculpt brush mask");
    ed.command("sculpt strength 0.5");
    ed.command(strprintf("sculpt size %g", std::max(6.0f, R * 0.3f)));
    ed.command(strprintf("sculpt dab %g %g", px.x, px.y));  // (a half-masked disc)
    ed.command("sculpt mask sharpen");
    size_t sharpened_up = 0;
    for (float v : M()->sculpt_mask) sharpened_up += v > 0.5f ? 1 : 0;  // (inside the disc the mean is the same: unchanged; at the rim it is lower: the rim rises)
    const std::vector<float> sharp = M()->sculpt_mask;
    ed.command("sculpt mask shrink");
    size_t shrunk = 0, grown = 0;
    for (size_t i = 0; i < sharp.size(); i++) shrunk += M()->sculpt_mask[i] < sharp[i] ? 1 : 0, grown += M()->sculpt_mask[i] > sharp[i] ? 1 : 0;
    std::printf("    fill: %d; sharpen raised %zu rim vertices; shrink lowered %zu and raised %zu\n", (int)filled, sharpened_up, shrunk, grown);
    CHECK(filled && sharpened_up > 0 && shrunk > 0 && grown == 0);
    /* Groups from the mask, by material, merged; hiding by the mask. */
    ed.command("sculpt mask clear");
    ed.command("sculpt strength 1");
    ed.command(strprintf("sculpt dab %g %g", px.x, px.y));
    size_t all_masked_faces = 0;
    for (size_t f = 0; f < M()->face_count(); f++) {
      bool all = true;
      for (uint32_t c = 0; c < M()->face_size(f); c++) all = all && M()->sculpt_mask[M()->face_verts(f)[c]] > 0.5f;
      all_masked_faces += all ? 1 : 0;
    }
    ed.command("sculpt group mask");
    size_t new_group = 0;
    for (size_t f = 0; f < M()->face_count(); f++) new_group += M()->group_of(f) == 1 ? 1 : 0;
    CHECK(all_masked_faces > 0 && new_group == all_masked_faces);
    ed.command("sculpt hide masked");
    size_t hidden = 0;
    for (size_t f = 0; f < M()->face_count(); f++) hidden += M()->face_hidden(f) ? 1 : 0;
    CHECK(hidden == all_masked_faces);
    ed.command("sculpt hide invert");
    size_t hidden_inv = 0;
    for (size_t f = 0; f < M()->face_count(); f++) hidden_inv += M()->face_hidden(f) ? 1 : 0;
    CHECK(hidden_inv == M()->face_count() - all_masked_faces);
    ed.command("sculpt hide all");
    CHECK(M()->render_mesh().indices.empty());
    ed.command("sculpt show all");
    ed.command("sculpt hide unmasked");
    size_t hidden_un = 0;
    for (size_t f = 0; f < M()->face_count(); f++) hidden_un += M()->face_hidden(f) ? 1 : 0;
    std::printf("    %zu faces all masked: group from mask %zu, hide masked %zu, hide invert %zu, hide unmasked %zu of %zu\n", all_masked_faces, new_group, hidden, hidden_inv, hidden_un,
                M()->face_count());
    CHECK(hidden_un > 0 && hidden_un <= M()->face_count() - all_masked_faces);
    ed.command("sculpt show all");
    ed.command("sculpt group material");
    bool by_mat = true;
    for (size_t f = 0; f < M()->face_count(); f++) by_mat = by_mat && M()->group_of(f) == M()->material_of(f);
    ed.command("sculpt group merge");
    bool merged = true;
    for (size_t f = 0; f < M()->face_count(); f++) merged = merged && M()->group_of(f) == 0;
    CHECK(by_mat && merged);
  });

  test("sculpt groups through topology: a Subdivision modifier's children keep their parent's group and hidden state; an Array's copies too; a face-rebuilding operator drops the groups rather than misplace them; the status bar names hidden faces outside the mode", [&] {
    Editor ed;
    GameObject *g = two_spheres(ed);
    CHECK(g);
    if (!g) return;
    auto M = [&] { return g->mesh_filter()->mesh; };
    ed.command("sculpt group islands");
    ed.command("sculpt hide group 1");
    size_t hidden_corners = 0, hidden_faces = 0;
    for (size_t f = 0; f < M()->face_count(); f++) {
      if (!M()->face_hidden(f)) continue;
      hidden_faces++, hidden_corners += M()->face_size(f);
    }
    /* The modifier: a child quad per corner of a hidden face is hidden. */
    SubdivisionSurface *sub = g->add<SubdivisionSurface>();
    sub->levels = 1;
    ed.mark_changed_for_test("Subdivision");
    const Mesh *ev = g->evaluated_mesh();
    size_t ev_hidden = 0;
    for (size_t f = 0; ev && f < ev->face_count(); f++) ev_hidden += ev->face_hidden(f) ? 1 : 0;
    std::printf("    %zu hidden base faces (%zu corners): %zu hidden faces in the level-1 subdivision of %zu\n", hidden_faces, hidden_corners, ev_hidden, ev ? ev->face_count() : 0);
    CHECK(ev && ev_hidden == hidden_corners && ev->render_mesh().indices.size() / 3 == (ev->face_count() - ev_hidden) * 2);
    g->remove_component(sub);
    ed.mark_changed_for_test("Subdivision off");
    /* An Array modifier's copies carry the groups. */
    ArrayModifier *arr = g->add<ArrayModifier>();
    arr->count = 2;
    ed.mark_changed_for_test("Array");
    ev = g->evaluated_mesh();
    size_t arr_hidden = 0;
    for (size_t f = 0; ev && f < ev->face_count(); f++) arr_hidden += ev->face_hidden(f) ? 1 : 0;
    CHECK(ev && ev->face_count() == M()->face_count() * 2 && arr_hidden == hidden_faces * 2);
    g->remove_component(arr);
    ed.mark_changed_for_test("Array off");
    /* A face-rebuilding operator on a copy: the groups go with the old numbering. */
    Mesh rebuilt = *M();
    meshops::triangulate(rebuilt);
    rebuilt.sync_attributes();
    CHECK(rebuilt.face_count() != M()->face_count() && rebuilt.face_group.empty() && !rebuilt.any_hidden());
    /* The status bar outside the mode. */
    ed.command("sculpt off");
    step(ed);
    const std::string status = ed.statusbar_hint_for_test();
    std::printf("    status: %s\n", status.c_str());
    CHECK(status.find("hidden") != std::string::npos);
    ed.command("sculpt on");
    ed.command("sculpt show all");
    CHECK(!M()->any_hidden());
  });
}

static void tests_round259() {
  /* A sphere with a Subdivision Surface, Sculpt on, the frames until the brush hits the subdivided surface at the centre. */
  auto subdiv_sphere = [](Editor &ed, int levels, uint64_t &gid, Vec2 &px) -> GameObject * {
    GameObject *g = sphere_scene(ed, 1);
    if (!g) return nullptr;
    gid = g->id;
    ed.command(strprintf("sculpt subdiv %d", levels));
    Vec3 w;
    float d;
    for (int i = 0; i < 100; i++) {
      step(ed);
      g = ed.scene().find(gid);
      px = pixel_of(ed, g->world_position());
      if (ed.sculpt_hit_for_test(px.x, px.y, w, d)) break;
    }
    return ed.scene().find(gid);
  };
  auto bit_diff = [](const std::vector<Vec3> &a, const std::vector<Vec3> &b, float &worst) {
    size_t n = a.size() == b.size() ? 0 : SIZE_MAX;
    worst = 0.0f;
    for (size_t i = 0; n != SIZE_MAX && i < a.size(); i++) {
      if (std::memcmp(&a[i], &b[i], sizeof(Vec3)) == 0) continue;
      n++, worst = std::max(worst, length(a[i] - b[i]));
    }
    return n;
  };

  test("dynamic subdivision: a dab on a sphere with a level-2 Subdivision Surface changes the subdivided surface the next frame, in place (no full render build) and equal to the full subdivision bit for bit; undo takes the surface back", [&] {
    Editor ed;
    uint64_t gid = 0;
    Vec2 px;
    GameObject *g = subdiv_sphere(ed, 2, gid, px);
    CHECK(g && ed.sculpt_subdiv_levels_for_test() == 2);
    if (!g) return;
    auto G = [&] { return ed.scene().find(gid); };
    const Mesh *ev = G()->evaluated_mesh();
    CHECK(ev && ev != G()->mesh_filter()->mesh.get() && ev->vert_count() > G()->mesh_filter()->mesh->vert_count());
    if (!ev) return;
    const std::vector<Vec3> before = ev->positions;
    ed.command("sculpt size 6");  // (a 44-px sphere on screen: a small dab, so the footprint is a patch, not the half)
    ed.command("sculpt strength 1");
    const MeshCounts c0 = mesh_counts();
    ed.command(strprintf("sculpt dab %g %g", px.x, px.y));
    step(ed);
    const MeshCounts c1 = mesh_counts() - c0;
    ev = G()->evaluated_mesh();
    const size_t moved = changed_count(before, ev->positions);
    const Mesh full = meshops::subdivide(*G()->mesh_filter()->mesh, 2, true);
    float worst = 0.0f;
    const size_t diff = bit_diff(ev->positions, full.positions, worst);
    const meshops::SubdivChain *chain = G()->eval_chain_for_test();
    std::printf("    the dab moved %zu of %zu subdivided vertices (the chain recomputed %zu); %zu differ from the full subdivision (worst %.2e); the frame built %llu render meshes from scratch, %llu in place\n",
                moved, ev->vert_count(), chain ? chain->last_touched() : 0, diff, worst, (unsigned long long)c1.full_builds, (unsigned long long)c1.in_place);
    CHECK(moved > 0 && diff == 0 && chain && c1.full_builds == 0);
    ed.command("undo");
    step(ed);
    ev = G()->evaluated_mesh();
    CHECK(changed_count(before, ev->positions) == 0);
  });

  test("dynamic subdivision: levels 1-3, Catmull-Clark and simple, three dabs each - the chain equals the full subdivision bit for bit and the update touches under a tenth of the vertices (its time printed beside the full subdivision's)", [&] {
    Editor ed;
    uint64_t gid = 0;
    Vec2 px;
    GameObject *g = subdiv_sphere(ed, 1, gid, px);
    CHECK(g);
    if (!g) return;
    auto G = [&] { return ed.scene().find(gid); };
    ed.command("sculpt size 6");
    ed.command("sculpt strength 1");
    for (int levels = 1; levels <= 3; levels++)
      for (int smooth = 1; smooth >= 0; smooth--) {
        ed.command(strprintf("sculpt subdiv %d", levels));
        SubdivisionSurface *sub = G()->get<SubdivisionSurface>();
        if (sub && sub->smooth != (smooth == 1)) sub->smooth = smooth == 1, ed.mark_changed_for_test("Catmull-Clark or simple");
        for (int i = 0; i < 20; i++) step(ed);
        const size_t steps0 = ed.undo_steps_for_test();
        for (int i = 0; i < 2; i++) ed.command(strprintf("sculpt dab %g %g", px.x + 3.0f * i, px.y - 2.0f * i));
        const meshops::SubdivChain *chain_before = G()->eval_chain_for_test();
        const auto t0 = std::chrono::steady_clock::now();
        ed.command(strprintf("sculpt dab %g %g", px.x - 3.0f, px.y + 2.0f));
        const auto t1 = std::chrono::steady_clock::now();
        const Mesh *ev = G()->evaluated_mesh();
        const auto t2 = std::chrono::steady_clock::now();
        const Mesh full = meshops::subdivide(*G()->mesh_filter()->mesh, levels, smooth == 1);
        const auto t3 = std::chrono::steady_clock::now();
        float worst = 0.0f;
        const size_t diff = ev ? bit_diff(ev->positions, full.positions, worst) : SIZE_MAX;
        const meshops::SubdivChain *chain = G()->eval_chain_for_test();
        const double update_ms = std::chrono::duration<double, std::milli>(t2 - t1).count(), full_ms = std::chrono::duration<double, std::milli>(t3 - t2).count();
        std::printf("    level %d %s: %zu of %zu vertices recomputed by the dab, %zu differ from the full subdivision (worst %.2e); the update %.2f ms, the full subdivision %.2f ms, the chain %zu KB\n",
                    levels, smooth ? "Catmull-Clark" : "simple", chain ? chain->last_touched() : 0, ev ? ev->vert_count() : 0, diff, worst, update_ms, full_ms, chain ? chain->memory_bytes() / 1024 : 0);
        CHECK(ev && chain && chain == chain_before && diff == 0 && chain->last_touched() > 0 && chain->last_touched() < ev->vert_count() / 10);  // (the same chain followed the dab: no rebuild; the times are printed, never asserted)
        (void)t0;
        while (ed.undo_steps_for_test() > steps0) ed.command("undo");
        step(ed);
      }
  });

  test("dynamic subdivision: sculpt subdiv adds, sets and removes the modifier as undo steps; the lazy mouse toggles", [&] {
    Editor ed;
    GameObject *g = sphere_scene(ed, 0);
    CHECK(g);
    if (!g) return;
    const uint64_t gid = g->id;
    auto G = [&] { return ed.scene().find(gid); };
    ed.command("sculpt subdiv 2");
    CHECK(ed.sculpt_subdiv_levels_for_test() == 2 && G()->get<SubdivisionSurface>() && G()->get<SubdivisionSurface>()->levels == 2);
    ed.command("sculpt subdiv +");
    CHECK(ed.sculpt_subdiv_levels_for_test() == 3);
    ed.command("sculpt subdiv +");  // (3 is the most)
    CHECK(ed.sculpt_subdiv_levels_for_test() == 3);
    ed.command("sculpt subdiv off");
    CHECK(ed.sculpt_subdiv_levels_for_test() == 0 && !G()->get<SubdivisionSurface>());
    ed.command("undo");
    CHECK(ed.sculpt_subdiv_levels_for_test() == 3);
    ed.command("undo");
    CHECK(ed.sculpt_subdiv_levels_for_test() == 2);
    ed.command("undo");
    CHECK(ed.sculpt_subdiv_levels_for_test() == 0);
    ed.command("sculpt lazy toggle");
    const bool on = ed.sculpt_brush_for_test().lazy > 0.0f;
    ed.command("sculpt lazy toggle");
    CHECK(on && ed.sculpt_brush_for_test().lazy == 0.0f);
  });
}

/* Task 0240: a closed mesh's checks - every edge in two faces, all quads, the signed volume. */
static bool closed_quads(const Mesh &m, size_t &bad_edges, size_t &non_quads) {
  std::unordered_map<uint64_t, int> edges;
  non_quads = 0;
  for (size_t f = 0; f < m.face_count(); f++) {
    const uint32_t n = m.face_size(f), *fv = m.face_verts(f);
    non_quads += n == 4 ? 0 : 1;
    for (uint32_t c = 0; c < n; c++) edges[Mesh::edge_key(fv[c], fv[(c + 1) % n])]++;
  }
  bad_edges = 0;
  for (const auto &e : edges) bad_edges += e.second == 2 ? 0 : 1;
  return bad_edges == 0 && non_quads == 0;
}
static double mesh_volume_d(const Mesh &m) {
  double v = 0.0;
  for (size_t f = 0; f < m.face_count(); f++) {
    const uint32_t n = m.face_size(f), *fv = m.face_verts(f);
    for (uint32_t c = 1; c + 1 < n; c++) v += (double)dot(m.positions[fv[0]], cross(m.positions[fv[c]], m.positions[fv[c + 1]]));
  }
  return v / 6.0;
}

static void tests_round260() {
  test("DynaMesh: the 24k-vertex sphere at resolution 64 comes out a closed shell of quads with its volume within 5 % and every vertex within a cell of the surface; the step undoes and redoes, the mode stays on, a stroke after it moves the new mesh", [&] {
    Editor ed;
    GameObject *g = sphere_scene(ed, 2);
    CHECK(g);
    if (!g) return;
    const uint64_t gid = g->id;
    auto G = [&] { return ed.scene().find(gid); };
    auto M = [&] { return G()->mesh_filter()->mesh; };
    const MeshPtr before = M();
    const double vol0 = mesh_volume_d(*before);
    const size_t steps0 = ed.undo_steps_for_test();
    ed.command("sculpt dynamesh 64 1");
    const MeshPtr after = M();
    CHECK(after != before && after->vert_count() > 0);
    size_t bad = 0, nonq = 0;
    const bool closed = closed_quads(*after, bad, nonq);
    const double vol1 = mesh_volume_d(*after);
    const float cell = 1.0f / 64.0f;  // (the sphere's diameter is 1: its longest side)
    float worst = 0.0f;
    for (const Vec3 &p : after->positions) worst = std::max(worst, std::fabs(length(p) - 0.5f));  // (the sphere's local centre is the origin, its radius 0.5)
    std::printf("    DynaMesh 64: %zu vertices / %zu faces -> %zu / %zu; %zu edges not in two faces, %zu non-quads; volume %.4f -> %.4f; the farthest vertex %.4f off the sphere (a cell %.4f)\n",
                before->vert_count(), before->face_count(), after->vert_count(), after->face_count(), bad, nonq, vol0, vol1, worst, cell);
    CHECK(closed && std::fabs(vol1 - vol0) < 0.05 * vol0 && worst < cell);
    CHECK(ed.sculpt_mode_for_test());  // (the mode stays on)
    /* A stroke on the new mesh. */
    Vec2 px;
    Vec3 w;
    float d;
    for (int i = 0; i < 100; i++) {
      step(ed);
      px = pixel_of(ed, G()->world_position());
      if (ed.sculpt_hit_for_test(px.x, px.y, w, d)) break;
    }
    const std::vector<Vec3> pos1 = M()->positions;
    ed.command("sculpt size 20");
    ed.command("sculpt strength 1");
    ed.command(strprintf("sculpt dab %g %g", px.x, px.y));
    const size_t moved = changed_count(pos1, M()->positions);
    CHECK(moved > 0 && M() == after && ed.undo_steps_for_test() == steps0 + 2);  // (DynaMesh, then the stroke: two steps)
    ed.command("undo");  // (the stroke)
    ed.command("undo");  // (DynaMesh)
    CHECK(M()->vert_count() == before->vert_count() && M()->positions == before->positions && ed.sculpt_mode_for_test());
    ed.command("redo");
    CHECK(M()->vert_count() == after->vert_count() && M()->positions == pos1);
  });

  test("DynaMesh: two spheres overlapping by half a radius become one closed shell with the inner walls gone and the union's volume; a mask over the top survives", [&] {
    Editor ed;
    GameObject *a = sphere_scene(ed, 1);
    CHECK(a);
    if (!a) return;
    ed.command("sculpt off");
    ed.command("create Sphere");
    GameObject *b = ed.active_object_for_test();
    const float dist = 0.75f;  // (radius 0.5: overlapping by half a radius)
    b->set_world_position(a->world_position() + Vec3(dist, 0.0f, 0.0f));
    ed.command("subdivide 1");
    ed.select_object(b->id);
    ed.select_object_add(a->id);
    ed.command("join");
    GameObject *j = ed.active_object_for_test();
    CHECK(j);
    if (!j) return;
    const uint64_t gid = j->id;
    auto G = [&] { return ed.scene().find(gid); };
    auto M = [&] { return G()->mesh_filter()->mesh; };
    step(ed);
    for (int i = 0; i < 400 && ed.scene_camera_for_test().animating; i++) step(ed);
    ed.command("sculpt on");
    /* A mask over the top (local y above 0.25). */
    MeshPtr m0 = M();
    ed.command("sculpt mask clear");
    m0 = M();
    for (uint32_t v = 0; v < (uint32_t)m0->vert_count(); v++)
      if (m0->positions[v].y > 0.25f) m0->sculpt_mask[v] = 1.0f;
    size_t masked0 = 0;
    for (float v : m0->sculpt_mask) masked0 += v > 0.5f ? 1 : 0;
    const double share0 = (double)masked0 / (double)m0->vert_count();
    const Vec3 ca(0.0f), cb(dist, 0.0f, 0.0f);  // (the join kept A's frame: A at the origin, B along x)
    const double r = 0.5, lens = kPi * (4.0 * r + dist) * (2.0 * r - dist) * (2.0 * r - dist) / 12.0, union_vol = 2.0 * (4.0 / 3.0) * kPi * r * r * r - lens;
    ed.command("sculpt dynamesh 64 1");
    const MeshPtr after = M();
    size_t bad = 0, nonq = 0;
    const bool closed = closed_quads(*after, bad, nonq);
    const double vol = mesh_volume_d(*after);
    const float cell = (float)(2.0 * r + dist) / 64.0f;
    size_t inner = 0;
    for (size_t f = 0; f < after->face_count(); f++) {
      Vec3 c(0.0f);
      for (uint32_t k = 0; k < after->face_size(f); k++) c += after->positions[after->face_verts(f)[k]];
      c = c / (float)after->face_size(f);
      inner += std::min(length(c - ca), length(c - cb)) < (float)r - 1.5f * cell ? 1 : 0;
    }
    std::vector<int> island;
    const int pieces = uvops::compute_islands(*after, nullptr, true, island);
    size_t masked1 = 0, low_masked = 0;
    for (uint32_t v = 0; v < (uint32_t)after->vert_count(); v++) {
      if (!(after->sculpt_mask.size() == after->vert_count() && after->sculpt_mask[v] > 0.5f)) continue;
      masked1++, low_masked += after->positions[v].y < 0.25f - 1.5f * cell ? 1 : 0;
    }
    const double share1 = (double)masked1 / (double)after->vert_count();
    std::printf("    two spheres -> %zu vertices / %zu quads in %d piece(s); %zu bad edges, %zu non-quads; %zu face centres inside a sphere (inner walls); volume %.4f against the union's %.4f; masked %.1f %% -> %.1f %%, %zu masked below the line\n",
                after->vert_count(), after->face_count(), pieces, bad, nonq, inner, vol, union_vol, 100.0 * share0, 100.0 * share1, low_masked);
    CHECK(closed && pieces == 1 && inner == 0 && std::fabs(vol - union_vol) < 0.05 * union_vol);
    CHECK(masked1 > 0 && std::fabs(share1 - share0) < 0.10 && low_masked == 0);
  });

  test("DynaMesh: the time at resolutions 64 and 128 on the sphere and the two spheres, printed; a mesh with no faces is refused", [&] {
    const MeshPtr sphere = primitives::uv_sphere(0.5f, 96, 64);
    Mesh two = *sphere;
    {
      Mesh other = *sphere;
      for (Vec3 &p : other.positions) p.x += 0.75f;
      const uint32_t off = (uint32_t)two.vert_count();
      two.positions.insert(two.positions.end(), other.positions.begin(), other.positions.end());
      for (size_t f = 0; f < other.face_count(); f++) {
        std::vector<uint32_t> fv(other.face_verts(f), other.face_verts(f) + other.face_size(f));
        for (uint32_t &v : fv) v += off;
        two.add_face(fv.data(), fv.size());
      }
      two.touch();
    }
    for (const Mesh *in : {sphere.get(), &two})
      for (int res : {64, 128}) {
        Mesh out;
        meshops::DynaMeshStats st;
        std::string why;
        const bool ok = meshops::dynamesh(*in, res, 1, out, &st, &why);
        size_t bad = 0, nonq = 0;
        const bool closed = ok && closed_quads(out, bad, nonq);
        std::printf("    %s at %d: %zu -> %zu vertices, %zu quads, %zu crossings; columns %.1f ms, nets %.1f ms, relax %.1f ms, %.1f ms in all; closed %d\n", in == sphere.get() ? "the sphere" : "two spheres", res,
                    st.verts_in, st.verts_out, st.faces_out, st.crossings, st.ms_columns, st.ms_nets, st.ms_smooth, st.ms_total, (int)closed);
        CHECK(ok && closed && st.ms_total < 10000.0);
      }
    Mesh empty, out;
    std::string why;
    CHECK(!meshops::dynamesh(empty, 64, 1, out, nullptr, &why) && !why.empty());
  });

  test("DynaMesh: two cubes touching along an edge at an even resolution (the ambiguous sign pattern) come out one closed manifold shell; a plane tilted against two axes is refused like a flat one; a fully masked 8-vertex cube keeps its mask on every vertex", [&] {
    /* Two unit cubes sharing the edge x = 1, y = 1. */
    const MeshPtr a = primitives::cube(1.0f);
    Mesh two = *a;
    for (Vec3 &p : two.positions) p += Vec3(0.5f, 0.5f, 0.5f);  // (A = [0,1]^3)
    {
      Mesh other = *a;
      for (Vec3 &p : other.positions) p += Vec3(1.5f, 1.5f, 0.5f);  // (B = [1,2] x [1,2] x [0,1])
      const uint32_t off = (uint32_t)two.vert_count();
      two.positions.insert(two.positions.end(), other.positions.begin(), other.positions.end());
      for (size_t f = 0; f < other.face_count(); f++) {
        std::vector<uint32_t> fv(other.face_verts(f), other.face_verts(f) + other.face_size(f));
        for (uint32_t &v : fv) v += off;
        two.add_face(fv.data(), fv.size());
      }
      two.touch();
    }
    for (int res : {16, 32}) {
      Mesh out;
      meshops::DynaMeshStats st;
      std::string why;
      const bool ok = meshops::dynamesh(two, res, 1, out, &st, &why);
      size_t bad = 0, nonq = 0;
      const bool closed = ok && closed_quads(out, bad, nonq);
      std::vector<int> island;
      const int pieces = ok ? uvops::compute_islands(out, nullptr, true, island) : 0;
      std::printf("    two cubes on an edge at %d: %zu quads, %zu bad edges, %zu non-quads, %d piece(s), %zu cells merged; volume %.3f of 2\n", res, out.face_count(), bad, nonq, pieces, st.filled_cells,
                  mesh_volume_d(out));
      CHECK(ok && closed && pieces == 1 && std::fabs(mesh_volume_d(out) - 2.0) < 0.25);
    }
    /* A tilted sheet: z = 0.5 x + 0.5 y over [-1, 1]^2. */
    MeshPtr sheet = primitives::grid(2.0f, 4, 4);
    for (Vec3 &p : sheet->positions) p.y = 0.5f * p.x + 0.5f * p.z;  // (the grid lies in x-z; its y is the height)
    sheet->touch();
    Mesh out;
    std::string why;
    const bool refused = !meshops::dynamesh(*sheet, 16, 0, out, nullptr, &why);
    std::printf("    a tilted sheet at 16: refused %d (%s)\n", (int)refused, why.c_str());
    CHECK(refused);
    /* A masked coarse cube. */
    Mesh cube = *a;
    cube.sculpt_mask.assign(cube.vert_count(), 1.0f);
    Mesh dyn;
    CHECK(meshops::dynamesh(cube, 32, 1, dyn, nullptr, &why));
    size_t masked = 0;
    for (float v : dyn.sculpt_mask) masked += v > 0.99f ? 1 : 0;
    std::printf("    a masked 8-vertex cube at 32: %zu of %zu output vertices masked\n", masked, dyn.vert_count());
    CHECK(dyn.sculpt_mask.size() == dyn.vert_count() && masked == dyn.vert_count());
  });
}

void run_sculpt_tests() {
  tests_round256();
  tests_round256_pointer();
  tests_round257();
  tests_round258();
  tests_round259();
  tests_round260();
}
