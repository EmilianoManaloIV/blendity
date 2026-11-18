// SPDX-License-Identifier: GPL-2.0-or-later
// Task 0236 (ADR 0074): Sculpt Mode - the brushes touch only what is under them; a stroke's undo is a delta.
#include "test_common.h"

#include "../src/scene/mesh.h"
#include "../src/scene/subdiv_chain.h"
#include "../src/scene/dynamesh.h"
#include "../src/scene/quad_remesh.h"
#include "../src/scene/dyntopo.h"
#include "../src/render/canvas.h"

#include <chrono>
#include <functional>
#include <thread>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
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
    /* Round 540: each brush keeps its own size and strength, so Draw has not the Mask brush's the lines above set: the same
     * numbers are given to it (the check is the Drag Dot mask stroke, not which brush's size it ran at). */
    ed.command(strprintf("sculpt size %g", std::max(5.0f, R * 0.25f)));
    ed.command("sculpt strength 1");
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

static void tests_round261() {
  /* Frames until the worker's result lands (or is dropped); the slowest frame printed. */
  auto wait_remesh = [](Editor &ed, double &slowest_ms) {
    slowest_ms = 0.0;
    for (int i = 0; i < 20000 && ed.sculpt_remesh_busy_for_test(); i++) {
      const auto t0 = std::chrono::steady_clock::now();
      step(ed);
      slowest_ms = std::max(slowest_ms, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
      if (ed.sculpt_remesh_busy_for_test()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return !ed.sculpt_remesh_busy_for_test();
  };

  test("quad remesh: the sphere to 2,000 faces on a worker - all quads, closed, the count within a quarter of the target, every vertex within 2 % of the radius, one undo step with the mode on; the editor's frames went on meanwhile (the slowest printed)", [&] {
    if (!meshops::quad_remesh_available()) {
      std::printf("    (this build has no QuadriFlow: the command says so and nothing changes)\n");
      Editor ed;
      GameObject *g = sphere_scene(ed, 1);
      CHECK(g);
      if (!g) return;
      const MeshPtr before = g->mesh_filter()->mesh;
      ed.command("sculpt remesh 2000");
      CHECK(!ed.sculpt_remesh_busy_for_test() && g->mesh_filter()->mesh == before);
      return;
    }
    Editor ed;
    GameObject *g = sphere_scene(ed, 2);
    CHECK(g);
    if (!g) return;
    const uint64_t gid = g->id;
    auto G = [&] { return ed.scene().find(gid); };
    auto M = [&] { return G()->mesh_filter()->mesh; };
    const MeshPtr before = M();
    const size_t steps0 = ed.undo_steps_for_test();
    const auto t_start = std::chrono::steady_clock::now();
    ed.command("sculpt remesh 2000");
    const double start_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t_start).count();
    CHECK(ed.sculpt_remesh_busy_for_test());
    double slowest = 0.0;
    const bool landed = wait_remesh(ed, slowest);
    const MeshPtr after = M();
    size_t bad = 0, nonq = 0;
    const bool closed = after != before && closed_quads(*after, bad, nonq);
    float worst = 0.0f;
    for (const Vec3 &p : after->positions) worst = std::max(worst, std::fabs(length(p) - 0.5f));
    std::printf("    quad remesh 2000: %zu vertices / %zu faces -> %zu / %zu (%zu bad edges, %zu non-quads); the farthest vertex %.4f off the sphere; the start took %.1f ms, the slowest frame while it worked %.1f ms\n",
                before->vert_count(), before->face_count(), after->vert_count(), after->face_count(), bad, nonq, worst, start_ms, slowest);
    CHECK(landed && closed && after->face_count() >= 1500 && after->face_count() <= 2500 && worst < 0.015f && ed.sculpt_mode_for_test());
    /* One step: undo brings the sphere back. */
    ed.command("sculpt size 10");
    ed.command("undo");
    CHECK(M()->vert_count() == before->vert_count() && M()->positions == before->positions && ed.undo_steps_for_test() == steps0);
  });

  test("quad remesh: two merged spheres (DynaMesh first) to 3,000 faces come out one closed shell of quads; a mask over the top survives; a stroke made while the worker runs makes the result drop", [&] {
    if (!meshops::quad_remesh_available()) return;
    Editor ed;
    GameObject *a = sphere_scene(ed, 1);
    CHECK(a);
    if (!a) return;
    ed.command("sculpt off");
    ed.command("create Sphere");
    GameObject *b = ed.active_object_for_test();
    b->set_world_position(a->world_position() + Vec3(0.75f, 0.0f, 0.0f));
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
    ed.command("sculpt dynamesh 48 1");
    ed.command("sculpt mask clear");
    {
      const MeshPtr m = M();
      for (uint32_t v = 0; v < (uint32_t)m->vert_count(); v++)
        if (m->positions[v].y > 0.25f) m->sculpt_mask[v] = 1.0f;
    }
    size_t masked0 = 0;
    for (float v : M()->sculpt_mask) masked0 += v > 0.5f ? 1 : 0;
    const double share0 = (double)masked0 / (double)M()->vert_count();
    ed.command("sculpt remesh 3000");
    double slowest = 0.0;
    CHECK(wait_remesh(ed, slowest));
    const MeshPtr after = M();
    size_t bad = 0, nonq = 0;
    const bool closed = closed_quads(*after, bad, nonq);
    std::vector<int> island;
    const int pieces = uvops::compute_islands(*after, nullptr, true, island);
    size_t masked1 = 0, low = 0;
    for (uint32_t v = 0; v < (uint32_t)after->vert_count(); v++) {
      if (!(after->sculpt_mask.size() == after->vert_count() && after->sculpt_mask[v] > 0.5f)) continue;
      masked1++, low += after->positions[v].y < 0.2f ? 1 : 0;
    }
    const double share1 = (double)masked1 / (double)after->vert_count();
    std::printf("    two spheres -> %zu quads in %d piece(s), %zu bad edges, %zu non-quads; masked %.1f %% -> %.1f %%, %zu masked well below the line; the slowest frame %.1f ms\n", after->face_count(), pieces, bad,
                nonq, 100.0 * share0, 100.0 * share1, low, slowest);
    CHECK(closed && pieces == 1 && after->face_count() >= 2000 && after->face_count() <= 4000);
    CHECK(masked1 > 0 && std::fabs(share1 - share0) < 0.12 && low == 0);
    /* A stroke while the worker runs: the result is dropped. */
    const MeshPtr held = M();
    Vec2 px;
    Vec3 w;
    float d;
    for (int i = 0; i < 100; i++) {
      step(ed);
      px = pixel_of(ed, G()->world_position());
      if (ed.sculpt_hit_for_test(px.x, px.y, w, d)) break;
    }
    ed.command("sculpt remesh 1500");
    CHECK(ed.sculpt_remesh_busy_for_test());
    ed.command("sculpt size 10");
    ed.command(strprintf("sculpt dab %g %g", px.x, px.y));  // (the mesh moved under the worker)
    CHECK(wait_remesh(ed, slowest));
    CHECK(M() == held);  // (dropped: the mesh is still the one the stroke worked on)
    /* A cancel through the console: the result is dropped whenever it ends. */
    ed.command("sculpt remesh 1500");
    CHECK(ed.sculpt_remesh_busy_for_test());
    ed.command("sculpt remesh cancel");
    CHECK(wait_remesh(ed, slowest));
    CHECK(M() == held);
    /* A mask painted while it works: the result is dropped too. */
    ed.command("sculpt remesh 1500");
    ed.command("sculpt mask invert");
    CHECK(wait_remesh(ed, slowest));
    CHECK(M() != held && M()->vert_count() == held->vert_count());  // (the invert's own step copied the mesh; the remesh did not land)
  });

  test("quad remesh: bad input is refused with a reason - a T-junction, a flipped face, an open plane, a vertex that is not a number, a face without area", [&] {
    if (!meshops::quad_remesh_available()) return;
    auto refused = [](const Mesh &m, const char *what) {
      Mesh out;
      std::string why;
      const bool ok = meshops::quad_remesh(m, {}, out, nullptr, &why);
      std::printf("    %s: refused %d (%s)\n", what, (int)!ok, why.c_str());
      return !ok && !why.empty();
    };
    Mesh t;  // (three faces on one edge)
    for (Vec3 p : {Vec3(0, 0, 0), Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(0, 0, 1), Vec3(0, -1, 0)}) t.add_vert(p);
    t.add_face({0, 1, 2}), t.add_face({0, 3, 1}), t.add_face({0, 1, 4});
    t.touch();
    CHECK(refused(t, "a T-junction"));
    Mesh flipped = *primitives::cube(1.0f);
    {
      const uint32_t *fv = flipped.face_verts(0);
      std::swap(flipped.corner_verts[flipped.face_offsets[0]], flipped.corner_verts[flipped.face_offsets[0] + 2]);
      (void)fv;
      flipped.touch();
    }
    CHECK(refused(flipped, "a flipped face"));
    CHECK(refused(*primitives::grid(2.0f, 4, 4), "an open plane (QuadriFlow stalled on one: refused for its open edges)"));
    Mesh nan = *primitives::cube(1.0f);
    nan.positions[0].x = std::numeric_limits<float>::quiet_NaN();
    nan.touch();
    CHECK(refused(nan, "a vertex that is not a number"));
    Mesh flat = *primitives::cube(1.0f);
    flat.positions[1] = flat.positions[0];  // (a face collapses to a line)
    flat.touch();
    CHECK(refused(flat, "a face without area"));
  });
}

static platform::Event mouse_event(platform::EventType t, int x, int y, int mods = 0) {
  platform::Event e;
  e.type = t, e.x = x, e.y = y, e.mods = mods;
  return e;
}

/* Task 0242: every edge in two faces, no face without area (a tombstone left behind), the faces' sizes. */
static bool manifold_faces(const Mesh &m, size_t &bad_edges, size_t &degenerate) {
  std::unordered_map<uint64_t, int> edges;
  degenerate = 0;
  for (size_t f = 0; f < m.face_count(); f++) {
    const uint32_t n = m.face_size(f), *fv = m.face_verts(f);
    bool same = true;
    for (uint32_t c = 1; c < n; c++) same = same && fv[c] == fv[0];
    degenerate += same ? 1 : 0;
    for (uint32_t c = 0; c < n; c++) edges[Mesh::edge_key(fv[c], fv[(c + 1) % n])]++;
  }
  bad_edges = 0;
  for (const auto &e : edges) bad_edges += e.second == 2 ? 0 : 1;
  return bad_edges == 0 && degenerate == 0;
}

static void tests_round262() {
  test("Dyntopo: a Draw stroke across the 1,490-vertex sphere adds vertices, the new edges under the brush lie within the detail size, the mesh stays manifold with no degenerate face; the stroke is one snapshot step that undoes and redoes; a stroke with Dyntopo off is a delta step again", [&] {
    Editor ed;
    GameObject *g = sphere_scene(ed, 1);
    CHECK(g);
    if (!g) return;
    const uint64_t gid = g->id;
    auto G = [&] { return ed.scene().find(gid); };
    auto M = [&] { return G()->mesh_filter()->mesh; };
    const MeshPtr before = M();
    const Vec2 px = pixel_of(ed, G()->world_position());
    const float R = length(pixel_of(ed, G()->world_position() + Vec3(0.0f, 0.5f, 0.0f)) - px);
    ed.command("sculpt size 10");
    ed.command("sculpt strength 0.5");
    ed.command("sculpt dyntopo 0.25");
    CHECK(ed.sculpt_dyntopo_for_test());
    const size_t steps0 = ed.undo_steps_for_test(), snaps0 = ed.sculpt_undo_snapshots_for_test();
    const MeshCounts c0 = mesh_counts();
    ed.command(strprintf("sculpt stroke %g %g %g %g 10", px.x - R * 0.4f, px.y, px.x + R * 0.4f, px.y));
    ed.command("sculpt size 11");  // (a command after: the pending step commits)
    const MeshPtr after = M();
    const MeshCounts c1 = mesh_counts() - c0;
    size_t bad = 0, degenerate = 0;
    const bool ok = manifold_faces(*after, bad, degenerate);
    /* The new edges (both ends new) against the detail size: a quarter of the brush's world radius - taken from the last dab. */
    const float r_local = ed.sculpt_last_radius_for_test(), detail = 0.25f * r_local;
    size_t new_edges = 0, in_range = 0;
    std::set<uint64_t> seen;
    for (size_t f = 0; f < after->face_count(); f++) {
      const uint32_t n = after->face_size(f), *fv = after->face_verts(f);
      for (uint32_t c = 0; c < n; c++) {
        const uint32_t a = fv[c], b = fv[(c + 1) % n];
        if (a < before->vert_count() || b < before->vert_count() || !seen.insert(Mesh::edge_key(a, b)).second) continue;
        const float len = length(after->positions[a] - after->positions[b]);
        new_edges++, in_range += len >= 0.4f * detail * 0.9f && len <= detail * 1.1f ? 1 : 0;
      }
    }
    std::printf("    Dyntopo stroke: %zu -> %zu vertices, %zu -> %zu faces; %zu bad edges, %zu degenerate faces; %zu of %zu new edges within [0.4, 1] x detail %.4f; %zu snapshot step(s), %llu render builds, %llu in place\n",
                before->vert_count(), after->vert_count(), before->face_count(), after->face_count(), bad, degenerate, in_range, new_edges, detail, ed.sculpt_undo_snapshots_for_test() - snaps0,
                (unsigned long long)c1.full_builds, (unsigned long long)c1.in_place);
    CHECK(after != before && after->vert_count() > before->vert_count() && ok && new_edges > 0 && in_range >= new_edges * 9 / 10);
    CHECK(ed.undo_steps_for_test() == steps0 + 1 && ed.sculpt_undo_snapshots_for_test() == snaps0 + 1);
    ed.command("undo");
    CHECK(M()->vert_count() == before->vert_count() && M()->positions == before->positions);
    ed.command("redo");
    CHECK(M()->vert_count() == after->vert_count());
    /* Dyntopo off: a delta step again. */
    ed.command("sculpt dyntopo off");
    const size_t snaps1 = ed.sculpt_undo_snapshots_for_test(), steps1 = ed.undo_steps_for_test();
    ed.command(strprintf("sculpt dab %g %g", px.x, px.y));
    CHECK(ed.undo_steps_for_test() == steps1 + 1 && ed.sculpt_undo_snapshots_for_test() == snaps1);
  });

  test("Dyntopo: on the 6k sphere a coarse detail removes vertices under the brush (collapses), the mesh staying manifold; a fully masked region gains vertices but moves none; Mirror X tessellates both sides", [&] {
    Editor ed;
    GameObject *g = sphere_scene(ed, 2);
    CHECK(g);
    if (!g) return;
    const uint64_t gid = g->id;
    auto G = [&] { return ed.scene().find(gid); };
    auto M = [&] { return G()->mesh_filter()->mesh; };
    const Vec2 px = pixel_of(ed, G()->world_position());
    const float R = length(pixel_of(ed, G()->world_position() + Vec3(0.0f, 0.5f, 0.0f)) - px);
    ed.command("sculpt size 30");
    ed.command("sculpt strength 0.2");
    ed.command("sculpt dyntopo 1.0");  // (the detail the brush radius: the sphere is far finer - edges collapse)
    const size_t verts0 = M()->vert_count();
    ed.command(strprintf("sculpt stroke %g %g %g %g 6", px.x - R * 0.3f, px.y, px.x + R * 0.3f, px.y));
    ed.command("sculpt size 31");
    size_t bad = 0, degenerate = 0;
    const bool ok = manifold_faces(*M(), bad, degenerate);
    std::printf("    coarse Dyntopo: %zu -> %zu vertices; %zu bad edges, %zu degenerate faces\n", verts0, M()->vert_count(), bad, degenerate);
    CHECK(M()->vert_count() < verts0 && ok);
    ed.command("undo");
    CHECK(M()->vert_count() == verts0);
    /* A mask: the region gains vertices (the mask carried to them) but nothing moves. */
    ed.command("sculpt dyntopo 0.2");
    ed.command("sculpt size 10");
    ed.command("sculpt strength 1");
    ed.command("sculpt mask fill");
    const std::vector<Vec3> held = M()->positions;
    const size_t verts1 = M()->vert_count();
    ed.command(strprintf("sculpt stroke %g %g %g %g 6", px.x - R * 0.3f, px.y, px.x + R * 0.3f, px.y));
    ed.command("sculpt size 11");
    size_t moved = 0;
    for (size_t v = 0; v < held.size() && v < M()->positions.size(); v++) moved += std::memcmp(&held[v], &M()->positions[v], sizeof(Vec3)) != 0 ? 1 : 0;
    size_t masked_new = 0;
    for (size_t v = held.size(); v < M()->vert_count(); v++) masked_new += M()->sculpt_mask.size() == M()->vert_count() && M()->sculpt_mask[v] > 0.99f ? 1 : 0;
    std::printf("    a masked region: %zu -> %zu vertices, %zu old ones moved, %zu of %zu new ones masked\n", verts1, M()->vert_count(), moved, masked_new, M()->vert_count() - held.size());
    CHECK(M()->vert_count() > verts1 && moved == 0 && masked_new == M()->vert_count() - held.size());
    ed.command("undo");
    ed.command("sculpt mask clear");
    /* Mirror X: both sides grow. */
    ed.command("sculpt symmetry x on");
    const size_t verts2 = M()->vert_count();
    ed.command(strprintf("sculpt stroke %g %g %g %g 6", px.x + R * 0.2f, px.y - R * 0.3f, px.x + R * 0.5f, px.y + R * 0.3f));
    ed.command("sculpt size 12");
    size_t left = 0, right = 0;
    for (size_t v = verts2; v < M()->vert_count(); v++) (M()->positions[v].x < 0.0f ? left : right)++;
    std::printf("    Mirror X: %zu new vertices on x < 0, %zu on x > 0\n", left, right);
    /* Both sides grow by a like amount, not the same: the mirrored dab splits edges in a different order, and the hit
     * comes from the rasterizer's depth, so a rounding step there moves it and the counts with it. Seen: Windows 90 / 77,
     * macOS 112 / 96, and 121 / 90 once round 462 computed the edge constants in double (no FMA on macOS) - so a third
     * apart, not a fifth. A broken mirror gives 0 on one side, which this still catches. */
    CHECK(left > 0 && right > 0 && std::fabs((double)left - (double)right) <= 0.34 * (double)std::max(left, right) + 2.0);
    ed.command("undo");
    ed.command("sculpt symmetry off");
    /* A fully masked region at a coarse detail: nothing collapses, nothing moves. */
    ed.command("sculpt dyntopo 1.0");
    ed.command("sculpt size 30");
    ed.command("sculpt mask fill");
    const size_t verts3 = M()->vert_count();
    const std::vector<Vec3> held3 = M()->positions;
    ed.command(strprintf("sculpt stroke %g %g %g %g 6", px.x - R * 0.3f, px.y, px.x + R * 0.3f, px.y));
    ed.command("sculpt size 31");
    size_t moved3 = 0;
    for (size_t v = 0; v < held3.size() && v < M()->positions.size(); v++) moved3 += std::memcmp(&held3[v], &M()->positions[v], sizeof(Vec3)) != 0 ? 1 : 0;
    std::printf("    a masked region at a coarse detail: %zu -> %zu vertices, %zu moved\n", verts3, M()->vert_count(), moved3);
    CHECK(M()->vert_count() >= verts3 && moved3 == 0);
    ed.command("sculpt mask clear");
    /* The time of a ten-dab stroke with and without Dyntopo (printed). */
    ed.command("sculpt dyntopo off");
    ed.command("sculpt size 10");
    auto t0 = std::chrono::steady_clock::now();
    ed.command(strprintf("sculpt stroke %g %g %g %g 10", px.x - R * 0.4f, px.y + R * 0.2f, px.x + R * 0.4f, px.y + R * 0.2f));
    const double plain_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    ed.command("undo");
    ed.command("sculpt dyntopo 0.25");
    t0 = std::chrono::steady_clock::now();
    ed.command(strprintf("sculpt stroke %g %g %g %g 10", px.x - R * 0.4f, px.y + R * 0.2f, px.x + R * 0.4f, px.y + R * 0.2f));
    const double dyn_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("    ten dabs on the 6k sphere: %.1f ms without Dyntopo, %.1f ms with (the version bumped and the tables rebuilt on each dab that changed faces)\n", plain_ms, dyn_ms);
  });

  test("Dyntopo: a loose vertex and a shape key survive a stroke; a sharp edge split by the brush stays sharp on both halves; the default cube, coarser than the brush, is tessellated under a dab in the middle of a face; hidden faces next to the brush are left alone; a stroke that changes nothing leaves no step", [&] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.set_clock_for_test(100.0);
    ed.command("select Cube");
    GameObject *g = ed.active_object_for_test();
    CHECK(g);
    if (!g) return;
    const uint64_t gid = g->id;
    auto G = [&] { return ed.scene().find(gid); };
    auto M = [&] { return G()->mesh_filter()->mesh; };
    {
      MeshPtr &mp = G()->mesh_filter()->mesh;
      mesh_make_mutable(mp);
      mp->add_vert(Vec3(3.0f, 3.0f, 3.0f));  // (a loose vertex)
      ShapeKey k;
      k.name = "Puff";
      k.delta.assign(mp->vert_count(), Vec3(0.0f, 0.1f, 0.0f));
      mp->shape_keys.push_back(k);
      for (size_t f = 0; f < mp->face_count(); f++) {  // (every cube edge sharp)
        const uint32_t n = mp->face_size(f), *fv = mp->face_verts(f);
        for (uint32_t c = 0; c < n; c++) mp->sharp_edges.push_back(Mesh::edge_key(fv[c], fv[(c + 1) % n]));
      }
      std::sort(mp->sharp_edges.begin(), mp->sharp_edges.end());
      mp->sharp_edges.erase(std::unique(mp->sharp_edges.begin(), mp->sharp_edges.end()), mp->sharp_edges.end());
      mp->touch();
      ed.mark_changed_for_test("a loose vertex, a key, sharp edges");
    }
    step(ed);
    for (int i = 0; i < 400 && ed.scene_camera_for_test().animating; i++) step(ed);
    ed.command("sculpt on");
    Vec2 px;
    Vec3 w;
    float d;
    for (int i = 0; i < 100; i++) {
      step(ed);
      px = pixel_of(ed, G()->world_position());
      if (ed.sculpt_hit_for_test(px.x, px.y, w, d)) break;
    }
    const size_t verts0 = M()->vert_count(), sharp0 = M()->sharp_edges.size();
    ed.command("sculpt dyntopo 0.5");
    ed.command("sculpt size 12");
    ed.command("sculpt strength 0");  // (the tessellation alone: the second press below must find nothing to do)
    const size_t steps0 = ed.undo_steps_for_test();
    /* One press and lift (a stroke of one dab) at an integer pixel: the no-change press below repeats it exactly - a
     * console dab and a mouse press read the depth a hair apart, and an edge split to just under one detail is over
     * the other. */
    const Recti sr = ed.scene_rect_for_test();
    const int ix = sr.x + (int)px.x, iy = sr.y + (int)px.y;
    auto press = [&] {
      step(ed, {mouse_event(platform::EventType::MouseMove, ix, iy)});
      step(ed, {mouse_event(platform::EventType::MouseDown, ix, iy)});
      step(ed, {mouse_event(platform::EventType::MouseUp, ix, iy)});
      step(ed);
    };
    press();
    ed.command("sculpt size 13");
    const MeshPtr after = M();
    const Vec3 loose = after->positions.back();
    size_t sharp_halves = 0;
    for (uint64_t k : after->sharp_edges) sharp_halves += (uint32_t)(k >> 32) >= 8 || (uint32_t)(k & 0xFFFFFFFFu) >= 8 ? 1 : 0;
    std::printf("    the cube under a dab mid-face: %zu -> %zu vertices, %zu -> %zu faces; the loose vertex at (%.0f %.0f %.0f); the key %zu deltas; %zu -> %zu sharp edges, %zu on a new vertex; %zu step(s)\n",
                verts0, after->vert_count(), (size_t)6, after->face_count(), loose.x, loose.y, loose.z, after->shape_keys.empty() ? (size_t)0 : after->shape_keys[0].delta.size(), sharp0,
                after->sharp_edges.size(), sharp_halves, ed.undo_steps_for_test() - steps0);
    CHECK(after->vert_count() > verts0 && after->face_count() > 6);
    CHECK(loose == Vec3(3.0f, 3.0f, 3.0f) || std::find(after->positions.begin(), after->positions.end(), Vec3(3.0f, 3.0f, 3.0f)) != after->positions.end());
    CHECK(after->shape_keys.size() == 1 && after->shape_keys[0].delta.size() == after->vert_count());
    CHECK(after->sharp_edges.size() >= sharp0 && sharp_halves > 0);
    CHECK(ed.undo_steps_for_test() == steps0 + 1);
    /* A stroke that changes nothing: the same spot, size and detail again with strength 0 - the region is at the detail
     * already (no split, no collapse) and nothing moves: no step, the mesh as it was. */
    ed.command("sculpt size 12");
    const size_t steps1 = ed.undo_steps_for_test();
    const MeshPtr held = M();
    press();
    std::printf("    a press and lift that changed nothing: %zu step(s), the mesh %s\n", ed.undo_steps_for_test() - steps1, M() == held ? "as it was" : "replaced");
    CHECK(ed.undo_steps_for_test() == steps1 && M() == held);
    ed.command("sculpt strength 0.3");
    /* Hidden faces beside the brush are left alone: hide the top, dab at the side near the top edge. */
    ed.command("undo");
    ed.command("sculpt group islands");
    {
      MeshPtr &mp = G()->mesh_filter()->mesh;
      mesh_make_mutable(mp);
      for (size_t f = 0; f < mp->face_count(); f++) {
        Vec3 c(0.0f);
        for (uint32_t k = 0; k < mp->face_size(f); k++) c += mp->positions[mp->face_verts(f)[k]];
        if (c.y > 0.49f * (float)mp->face_size(f)) mp->set_face_hidden(f, true);  // (the top face: its corners' sum, at a half each; a side face's upper half has less)
      }
      mp->touch();
      ed.mark_changed_for_test("the top hidden");
    }
    for (int i = 0; i < 20; i++) step(ed);
    size_t hidden_corners0 = 0;
    for (size_t f = 0; f < M()->face_count(); f++) hidden_corners0 += M()->face_hidden(f) ? M()->face_size(f) : 0;
    /* The front face just under the top edge: the projection of a point on it, moved down a pixel at a time until the
     * id plane has the object there (a pixel on the edge itself can miss; one platform's rounding had it miss and
     * the check below passed on nothing). */
    Vec2 side = pixel_of(ed, Vec3(w.x, G()->world_position().y + 0.45f, w.z));  // (the face the first dab hit - whichever the camera sees - just under the top)
    {
      Vec3 ws;
      float ds;
      int tries = 0;
      for (; tries < 12 && !ed.sculpt_hit_for_test(side.x, side.y, ws, ds); tries++) side.y += 1.0f;
      std::printf("    the pixel under the top edge found after %d step(s) down: hits %d\n", tries, (int)ed.sculpt_hit_for_test(side.x, side.y, ws, ds));
      CHECK(ed.sculpt_hit_for_test(side.x, side.y, ws, ds));
    }
    ed.command(strprintf("sculpt dab %g %g", side.x, side.y));
    ed.command("sculpt size 12");
    size_t hidden_faces = 0, hidden_corners = 0;
    for (size_t f = 0; f < M()->face_count(); f++)
      if (M()->face_hidden(f)) hidden_faces++, hidden_corners += M()->face_size(f);
    std::printf("    with the top hidden, a dab under the top edge: %zu hidden face(s) with %zu corners (was 1 with %zu)\n", hidden_faces, hidden_corners, hidden_corners0);
    CHECK(hidden_faces == 1 && hidden_corners == hidden_corners0);
    ed.command("sculpt show all");
  });
}

/* ---------------------------------------------------------------- round 368: the pen in sculpting (task 0345) */
namespace {
/* A Scrape dab (Flatten's, one-sided) at the sphere's front with the pen leaning `tilt` degrees: the moved vertices' mean move, in the
 * object's space (each moves along the plane's normal). */
Vec3 flatten_move(Vec2 tilt, Vec3 &right_local, bool mirror = false, Vec3 *pos_side = nullptr, Vec3 *neg_side = nullptr) {
  Editor ed;
  GameObject *g = sphere_scene(ed);
  if (!g) return Vec3(0.0f);
  ed.command("sculpt tilt on");  // (off at first - review 368)
  if (mirror) ed.command("sculpt symmetry x");
  const std::vector<Vec3> before = g->mesh_filter()->mesh->positions;
  ed.command("sculpt brush scrape");  // (Scrape: only what stands above the plane moves, so the moves add up along it)
  ed.command("sculpt size 40");
  ed.command("sculpt strength 1");
  ed.sculpt_pen_for_test(tilt, false);
  const Vec2 off = pixel_of(ed, g->world_position() + ed.scene_camera_for_test().right() * 0.2f);  // (off the centre, on the sphere: the area plane cuts the curve there)
  ed.command(strprintf("sculpt dab %g %g", off.x, off.y));
  const std::vector<Vec3> &after = g->mesh_filter()->mesh->positions;
  Vec3 sum(0.0f);
  for (size_t i = 0; i < after.size() && i < before.size(); i++) {
    sum += after[i] - before[i];
    if (pos_side && before[i].x > 1e-4f) *pos_side += after[i] - before[i];
    if (neg_side && before[i].x < -1e-4f) *neg_side += after[i] - before[i];
  }
  std::printf("    (tilt %g: %zu vertices changed, %zu under the brush)\n", tilt.x, changed_count(before, after), ed.sculpt_last_ids_for_test().size());
  right_local = normalize(g->world_matrix().inverse().dir(ed.scene_camera_for_test().right()));
  return sum;
}
}  // namespace

static void tests_round368() {
  test("sculpt with a pen (round 368): the eraser end inverts the brush - a Draw dab with it digs where the tip raises; in Mask mode it takes the mask away", [] {
    Editor ed;
    GameObject *g = sphere_scene(ed);
    CHECK(g != nullptr);
    if (!g) return;
    const Vec2 px = pixel_of(ed, g->world_position());
    ed.command("sculpt size 25");
    ed.command("sculpt strength 1");
    const double v0 = volume(*g->mesh_filter()->mesh);
    ed.command(strprintf("sculpt dab %g %g", px.x, px.y));
    const double v1 = volume(*g->mesh_filter()->mesh);
    ed.sculpt_pen_for_test(Vec2(0.0f, 0.0f), true);
    ed.command(strprintf("sculpt dab %g %g", px.x, px.y));
    ed.sculpt_pen_for_test(Vec2(0.0f, 0.0f), true);
    ed.command(strprintf("sculpt dab %g %g", px.x, px.y));
    const double v2 = volume(*g->mesh_filter()->mesh);
    std::printf("    the volume: %.5f, after the tip's dab %.5f, after two eraser dabs %.5f\n", v0, v1, v2);
    CHECK(v1 > v0 && v2 < v1 && v2 < v0);
    CHECK(!ed.sculpt_brush_for_test().carve);  // (the eraser inverts its own dabs only)
    /* The Mask brush: the tip paints the mask, the eraser end takes it away. */
    ed.command("sculpt brush mask");
    auto mask_sum = [&] {
      double s = 0.0;
      for (float m : g->mesh_filter()->mesh->sculpt_mask) s += m;
      return s;
    };
    ed.command(strprintf("sculpt dab %g %g", px.x, px.y));
    const double painted = mask_sum();
    ed.sculpt_pen_for_test(Vec2(0.0f, 0.0f), true);
    ed.command(strprintf("sculpt dab %g %g", px.x, px.y));
    const double taken = mask_sum();
    std::printf("    the mask: %.3f painted, %.3f after the eraser end\n", painted, taken);
    CHECK(painted > 0.0 && taken < painted);
  });
  test("sculpt with a pen (round 368): its tilt leans Flatten's and Scrape's plane - leaning 40 degrees right, the vertices move along a plane tipped to the view's right; upright, along the area's own", [] {
    Vec3 right_up, right_tilt;
    const Vec3 upright = flatten_move(Vec2(0.0f, 0.0f), right_up), leaned = flatten_move(Vec2(40.0f, 0.0f), right_tilt);
    const float a = length(upright) > 1e-9f ? dot(normalize(upright), right_up) : 0.0f;
    const float b = length(leaned) > 1e-9f ? dot(normalize(leaned), right_tilt) : 0.0f;
    std::printf("    the move's share along the view's right: upright %.3f, leaning 40 degrees %.3f\n", a, b);
    CHECK(length(upright) > 1e-6f && length(leaned) > 1e-6f && std::fabs(a) < 0.1f && b < -0.25f);  // (Scrape cuts along -n: a plane tipped right moves them left)
  });
  test("sculpt with a pen (round 368, review): with Mirror X the mirrored side leans the mirrored way; the lean turns the normal by the pen's angle and never flips it, even side-on", [] {
    Vec3 right, pos(0.0f), neg(0.0f);
    flatten_move(Vec2(40.0f, 0.0f), right, true, &pos, &neg);
    std::printf("    the +X side moved (%.4f %.4f %.4f), the -X side (%.4f %.4f %.4f)\n", pos.x, pos.y, pos.z, neg.x, neg.y, neg.z);
    CHECK(length(pos) > 1e-6f && length(neg) > 1e-6f);
    CHECK(std::fabs(pos.x + neg.x) < 0.1f * std::fabs(pos.x) + 1e-5f && std::fabs(pos.y - neg.y) < 0.1f * length(pos) + 1e-5f && std::fabs(pos.z - neg.z) < 0.1f * length(pos) + 1e-5f);
    Editor ed;
    ed.init_headless(1200, 900);
    const SceneCamera &cam = ed.scene_camera_for_test();
    const Vec3 facing = -cam.forward(), side_on = -cam.right();
    const Vec3 a = ed.sculpt_lean_for_test(facing, Vec2(40.0f, 0.0f)), b = ed.sculpt_lean_for_test(side_on, Vec2(55.0f, 0.0f));
    const float deg = std::acos(std::max(-1.0f, std::min(1.0f, dot(a, facing)))) * 180.0f / kPi;
    std::printf("    facing the view, leaning 40 degrees: turned %.2f degrees, toward the right %.3f; side-on, leaning 55 against it: dot %.3f\n", deg, dot(a, cam.right()), dot(b, side_on));
    CHECK(std::fabs(deg - 40.0f) < 0.1f && dot(a, cam.right()) > 0.6f && dot(b, side_on) > 0.0f);
  });
  test("sculpt with a pen (round 368): the pressure curve maps pressure for Sculpt and Paint - a straight line by default, a bowed one making half pressure a light touch; Carve switched by the Adobe preset's E in Sculpt", [] {
    Editor ed;
    GameObject *g = sphere_scene(ed);
    CHECK(std::fabs(ed.pen_pressure_for_test(0.3f) - 0.3f) < 1e-5f && std::fabs(ed.pen_pressure_for_test(1.0f) - 1.0f) < 1e-5f);  // (the cubic through two points: not bit-exact on every compiler)
    ed.pen_curve_for_test() = {{0.0f, 0.0f}, {0.5f, 0.1f}, {1.0f, 1.0f}};
    const float half = ed.pen_pressure_for_test(0.5f);
    std::printf("    half pressure through the bowed curve: %.3f\n", half);
    CHECK(std::fabs(half - 0.1f) < 1e-4f);
    if (!g) return;
    /* A half-pressure dab through the bowed curve moves the centre far less than a full one. */
    const Vec2 px = pixel_of(ed, g->world_position());
    ed.command("sculpt size 25");
    ed.command("sculpt strength 1");
    const double v0 = volume(*g->mesh_filter()->mesh);
    ed.command(strprintf("sculpt dab %g %g 0.5", px.x, px.y));
    const double light = volume(*g->mesh_filter()->mesh) - v0;
    ed.command(strprintf("sculpt dab %g %g 1", px.x, px.y));
    const double full = volume(*g->mesh_filter()->mesh) - v0 - light;
    std::printf("    the volume added: half pressure %.6f, full %.6f\n", light, full);
    CHECK(light > 0.0 && light < full * 0.3);
    const bool before = ed.sculpt_brush_for_test().carve;
    ed.run_action_for_test("brush.erase");
    CHECK(ed.sculpt_brush_for_test().carve != before);
  });
  test("the lesson's Try-it (round 368): create Sphere; sculpt on; sculpt brush clay; sculpt tilt on - the brush the pen's tilt leans", [] {
    Editor ed;
    ed.init_headless(1200, 900);
    ed.command("create Sphere");
    ed.command("sculpt on");
    ed.command("sculpt brush clay");
    ed.command("sculpt tilt on");
    CHECK(ed.sculpt_for_test() && ed.sculpt_brush_for_test().kind == 3 && ed.sculpt_brush_for_test().tilt);
  });
}

/* ---- round 508 (task 0492, part D): sculpting a dense mesh - the render mesh follows a dab by looking at what moved only ---- */

namespace {

/* True when a's render mesh draws the same triangles as a rebuild from scratch of the same mesh (corner by corner: position and
 * normal within `tol`) and its bounds are the vertices'. */
bool render_is_rebuild(const Mesh &a, float tol, std::string &why) {
  Mesh fresh;
  {
    MeshCopyBare bare;  // (no caches carried over: its render mesh is a full build)
    fresh = a;
  }
  const RenderMesh &x = a.render_mesh(), &y = fresh.render_mesh();
  if (x.indices.size() != y.indices.size()) return why = "the triangle counts differ", false;
  float worst_p = 0.0f, worst_n = 0.0f;
  for (size_t i = 0; i < x.indices.size(); i++) {
    worst_p = std::max(worst_p, length(x.positions[x.indices[i]] - y.positions[y.indices[i]]));
    worst_n = std::max(worst_n, length(x.normals[x.indices[i]] - y.normals[y.indices[i]]));
  }
  const AABB box = a.bounds();
  const bool bounds_same = std::memcmp(&x.bounds.min, &box.min, sizeof(Vec3)) == 0 && std::memcmp(&x.bounds.max, &box.max, sizeof(Vec3)) == 0;
  why = strprintf("positions off by %g, normals by %g, bounds %s", (double)worst_p, (double)worst_n, bounds_same ? "exact" : "STALE");
  return worst_p <= tol && worst_n <= tol && bounds_same;
}

/* The vertices within r of p, each pushed along its own radial direction by `amount` times a smooth falloff (what a Draw dab does). */
std::vector<uint32_t> push_patch(Mesh &m, Vec3 p, float r, float amount) {
  std::vector<uint32_t> ids;
  for (uint32_t v = 0; v < (uint32_t)m.positions.size(); v++) {
    const float d = length(m.positions[v] - p);
    if (d >= r) continue;
    const float w = (1.0f - d / r) * (1.0f - d / r);
    m.positions[v] += normalize(m.positions[v]) * (amount * w);
    ids.push_back(v);
  }
  return ids;
}

bool same_render(const Mesh &a, const Mesh &b) {
  const RenderMesh &x = a.render_mesh(), &y = b.render_mesh();
  return x.positions.size() == y.positions.size() && x.indices == y.indices &&
         std::memcmp(x.positions.data(), y.positions.data(), x.positions.size() * sizeof(Vec3)) == 0 &&
         std::memcmp(x.normals.data(), y.normals.data(), x.normals.size() * sizeof(Vec3)) == 0;
}

}  // namespace

static void tests_round508() {
  test("sculpting a 100k-face sphere (round 508): a dab told which vertices moved gives the render mesh a rebuild from scratch makes, bit for bit what the whole scan gives, with the normals of the faces around the dab only - at the texture seam, at a pole, and after a bump nobody told of", [] {
    MeshPtr told = primitives::uv_sphere(0.5f, 450, 225), scanned = primitives::uv_sphere(0.5f, 450, 225);
    CHECK(told->face_count() > 100000);
    /* Both are warm (a build, then one in-place move: the first in-place frame works the whole mesh out and keeps it). */
    auto bump = [](Mesh &m, const std::vector<uint32_t> &ids, bool tell) {
      m.deform_version++;
      if (tell) m.note_moved(ids);
      m.render_mesh();
    };
    for (Mesh *m : {told.get(), scanned.get()}) {
      m->render_mesh();
      m->positions[3] += Vec3(0.001f, 0.0f, 0.0f);
      bump(*m, {3}, m == told.get());
    }
    const MeshCounts c0 = mesh_counts();
    /* The seam's ends and the poles (they stand on the box's sides), the middle, a dab inside the box and one that grows it. */
    struct Spot {
      Vec3 at;
      float amount;
    };
    const Spot spots[] = {{Vec3(0.5f, 0.0f, 0.0f), 0.03f},  {Vec3(0.0f, 0.5f, 0.0f), 0.03f}, {Vec3(0.0f, -0.2f, 0.45f), 0.03f},
                          {Vec3(-0.5f, 0.0f, 0.0f), 0.03f}, {Vec3(0.35f, 0.35f, 0.0f), 0.01f}, {Vec3(0.35f, 0.35f, 0.0f), 0.4f}};
    size_t dab_faces = 0;
    for (const Spot &s : spots) {
      const MeshCounts before = mesh_counts();
      const std::vector<uint32_t> ids = push_patch(*told, s.at, 0.06f, s.amount);
      push_patch(*scanned, s.at, 0.06f, s.amount);
      CHECK(!ids.empty());
      bump(*told, ids, true);
      bump(*scanned, ids, false);
      dab_faces = std::max<size_t>(dab_faces, (size_t)((mesh_counts() - before).normal_faces / 2));  // (two meshes followed)
      CHECK(same_render(*told, *scanned));
      const AABB box = told->bounds();  // (each dab's box, not only the last: a stale one would stay wrong)
      CHECK(std::memcmp(&told->render_mesh().bounds.min, &box.min, sizeof(Vec3)) == 0 && std::memcmp(&told->render_mesh().bounds.max, &box.max, sizeof(Vec3)) == 0);
    }
    const MeshCounts c1 = mesh_counts() - c0;  // (before the rebuild below: that is a full build of its own)
    std::string why;
    CHECK(render_is_rebuild(*told, 1e-5f, why));
    std::printf("    %zu faces; the dab's normals were worked out for at most %zu faces; vs a rebuild: %s\n", told->face_count(), dab_faces, why.c_str());
    CHECK(c1.full_builds == 0 && c1.in_place >= 12 && c1.partial_normals >= 12);
    /* A count, not a time: the faces around the dab (a pole packs the most into a patch: 4 % of this mesh), not the mesh. */
    CHECK(dab_faces > 0 && dab_faces < told->face_count() / 10);
    /* A bump nobody told of between two told ones: the chain is broken, the whole scan finds the vertex it moved. */
    told->positions[1000] += Vec3(0.0f, 0.01f, 0.0f), scanned->positions[1000] += Vec3(0.0f, 0.01f, 0.0f);
    told->deform_version++, scanned->deform_version++;
    const std::vector<uint32_t> ids = push_patch(*told, Vec3(0.0f, 0.0f, -0.5f), 0.05f, 0.02f);
    push_patch(*scanned, Vec3(0.0f, 0.0f, -0.5f), 0.05f, 0.02f);
    bump(*told, ids, true);
    bump(*scanned, ids, false);
    CHECK(same_render(*told, *scanned));
    CHECK(render_is_rebuild(*told, 1e-5f, why));
  });

  test("sculpting a mesh with loose parts (round 508): an isolated vertex, a loose edge and a separate triangle moved by a dab told to the render mesh follow like the rest", [] {
    auto make = [] {
      MeshPtr mp = primitives::uv_sphere(0.5f, 64, 32);
      Mesh &m = *mp;
      const uint32_t a = m.add_vert(Vec3(2.0f, 0.0f, 0.0f)), b = m.add_vert(Vec3(2.5f, 0.0f, 0.0f)), c = m.add_vert(Vec3(2.0f, 0.5f, 0.0f)), d = m.add_vert(Vec3(2.5f, 0.5f, 0.0f));
      m.add_vert(Vec3(-2.0f, 0.0f, 0.0f));  // (a vertex with no face at all)
      m.add_face({a, b, c});                // (a separate triangle)
      m.add_loose_edge(b, d);
      m.touch();
      return mp;
    };
    MeshPtr told = make(), scanned = make();
    for (Mesh *m : {told.get(), scanned.get()}) {
      m->render_mesh();
      m->positions[5] += Vec3(0.001f, 0.0f, 0.0f);
      m->deform_version++;
      if (m == told.get()) m->note_moved({5});
      m->render_mesh();
    }
    const uint32_t first_loose = (uint32_t)told->positions.size() - 5;
    const std::vector<uint32_t> ids = {first_loose, first_loose + 1, first_loose + 2, first_loose + 3, first_loose + 4, 10, 11};
    for (Mesh *m : {told.get(), scanned.get()}) {
      for (uint32_t v : ids) m->positions[v] += Vec3(0.0f, 0.05f, 0.03f);
      m->deform_version++;
      if (m == told.get()) m->note_moved(ids);
    }
    CHECK(same_render(*told, *scanned));
    std::string why;
    CHECK(render_is_rebuild(*told, 1e-5f, why));
    std::printf("    vs a rebuild: %s\n", why.c_str());
  });

  test("sculpt a subdivided sphere (round 508): a stroke's render mesh, normals and bounds are a rebuild's; undo and redo bring the sphere back and the render mesh with it; a mirrored stroke follows too", [] {
    Editor ed;
    GameObject *g = sphere_scene(ed, 3);
    CHECK(g);
    if (!g) return;
    const MeshPtr mesh = g->mesh_filter()->mesh;
    const Vec2 px = pixel_of(ed, g->world_position());
    std::string why;
    CHECK(render_is_rebuild(*mesh, 1e-5f, why));  // (warm: the frames drew it)
    const std::vector<Vec3> before = mesh->positions;
    ed.command("sculpt size 25");
    ed.command("sculpt strength 1");
    ed.command(strprintf("sculpt stroke %g %g %g %g 12", px.x - 15.0f, px.y, px.x + 15.0f, px.y));
    step(ed);
    step(ed);
    const std::vector<Vec3> after = mesh->positions;
    CHECK(changed_count(before, after) > 0);
    CHECK(render_is_rebuild(*mesh, 1e-5f, why));
    std::printf("    stroke: %zu vertices moved; vs a rebuild: %s\n", changed_count(before, after), why.c_str());
    ed.command("undo");
    step(ed);
    CHECK(changed_count(before, mesh->positions) == 0);
    CHECK(render_is_rebuild(*mesh, 1e-5f, why));
    ed.command("redo");
    step(ed);
    CHECK(changed_count(after, mesh->positions) == 0);
    CHECK(render_is_rebuild(*mesh, 1e-5f, why));
    /* Mirror X: both sides move, and the render mesh follows both. (Round 562: on the sphere at rest - the stroke taken
     * back. Each of its twelve full-strength dabs now lands on the surface the dabs before it raised, not on the sphere
     * as last drawn, so it leaves a spike off the mirror plane; a dab on the spike's tip has no surface at its mirror
     * image, and this check is about the render mesh following a mirrored dab.) */
    ed.command("undo");
    step(ed);
    const std::vector<Vec3> mid = mesh->positions;
    ed.command("sculpt front off");
    ed.command("sculpt symmetry x");
    ed.command(strprintf("sculpt dab %g %g", px.x + 12.0f, px.y));
    step(ed);
    size_t left = 0, right = 0;
    for (size_t v = 0; v < mid.size(); v++) {
      if (std::memcmp(&mid[v], &mesh->positions[v], sizeof(Vec3)) == 0) continue;
      (mid[v].x < 0.0f ? left : right)++;
    }
    std::printf("    mirrored dab: %zu vertices moved on the left, %zu on the right\n", left, right);
    CHECK(left > 0 && right > 0);
    CHECK(render_is_rebuild(*mesh, 1e-5f, why));
  });

  test("sculpt a sphere with an Array modifier (round 508): the base's stroke shows in the evaluated copies and their render mesh is a rebuild's", [] {
    Editor ed;
    GameObject *g = sphere_scene(ed, 2);
    CHECK(g);
    if (!g) return;
    ArrayModifier *arr = g->add<ArrayModifier>();
    arr->count = 2;
    ed.mark_changed_for_test("Array");
    step(ed);
    const Vec2 px = pixel_of(ed, g->world_position());
    const std::vector<Vec3> before = g->mesh_filter()->mesh->positions;
    ed.command("sculpt size 25");
    ed.command("sculpt strength 1");
    ed.command(strprintf("sculpt stroke %g %g %g %g 8", px.x - 10.0f, px.y, px.x + 10.0f, px.y));
    step(ed);
    step(ed);
    const Mesh &base = *g->mesh_filter()->mesh;
    const Mesh *ev = g->evaluated_mesh();
    CHECK(changed_count(before, base.positions) > 0);
    CHECK(ev && ev->face_count() == base.face_count() * 2);
    if (!ev) return;
    size_t same = 0;
    for (size_t v = 0; v < base.positions.size(); v++) same += std::memcmp(&ev->positions[v], &base.positions[v], sizeof(Vec3)) == 0;
    CHECK(same == base.positions.size());  // (the first copy is the base, as sculpted)
    std::string why;
    CHECK(render_is_rebuild(*ev, 1e-5f, why));
    CHECK(render_is_rebuild(base, 1e-5f, why));
  });
}

/* ---- round 508 (test engineer): the sculpt's noted moves against a from-scratch rebuild, edge cases first ---- */

namespace {

/* render_is_rebuild's check for the flat (force_flat) render mesh: corner by corner against a copy that has no caches. */
bool flat_render_is_rebuild(const Mesh &a, float tol) {
  Mesh fresh;
  {
    MeshCopyBare bare;
    fresh = a;
  }
  const RenderMesh &x = a.render_mesh(true), &y = fresh.render_mesh(true);
  if (x.indices.size() != y.indices.size()) return false;
  for (size_t i = 0; i < x.indices.size(); i++)
    if (length(x.positions[x.indices[i]] - y.positions[y.indices[i]]) > tol || length(x.normals[x.indices[i]] - y.normals[y.indices[i]]) > tol) return false;
  const AABB fb = fresh.bounds();
  return std::memcmp(&x.bounds.min, &fb.min, sizeof(Vec3)) == 0 && std::memcmp(&x.bounds.max, &fb.max, sizeof(Vec3)) == 0;
}

bool same_bounds(const Mesh &a, const Mesh &b) {
  const AABB &x = a.render_mesh().bounds, &y = b.render_mesh().bounds;
  return std::memcmp(&x.min, &y.min, sizeof(Vec3)) == 0 && std::memcmp(&x.max, &y.max, sizeof(Vec3)) == 0;
}

/* push_patch at a scale: the sphere's radius is 0.5 * s. */
std::vector<uint32_t> push_patch_scaled(Mesh &m, Vec3 p, float r, float amount, float s) { return push_patch(m, p * s, r * s, amount * s); }

}  // namespace

static void tests_round508_te() {
  test("round 508 (test engineer): every sculpt brush, with no symmetry, each mirror axis and radial, leaves the render mesh what a rebuild from scratch makes", [] {
    Editor ed;
    GameObject *g = sphere_scene(ed, 3);
    CHECK(g);
    if (!g) return;
    const Vec2 px = pixel_of(ed, g->world_position());
    ed.command("sculpt size 30");
    ed.command("sculpt strength 1");
    ed.command("sculpt front off");
    const MeshCounts c0 = mesh_counts();
    const char *brushes[] = {"draw", "smooth", "grab", "clay", "inflate", "flatten", "pinch", "crease", "scrape", "snake", "mask"};
    const char *syms[] = {"off", "x", "y", "z", "radial"};
    int bad = 0, k = 0;
    for (const char *b : brushes)
      for (const char *s : syms) {
        ed.command("sculpt symmetry off");
        if (std::string(s) == "radial") ed.command("sculpt symmetry radial 3");
        else if (std::string(s) != "off") ed.command(std::string("sculpt symmetry ") + s);
        ed.command(std::string("sculpt brush ") + b);
        const float off = (float)(k++ % 7) * 4.0f - 12.0f;
        ed.command(strprintf("sculpt stroke %g %g %g %g 5", px.x + off, px.y - 6.0f, px.x + off + 20.0f, px.y + 6.0f));
        step(ed);
        step(ed);
        std::string why;
        const Mesh &m = *g->mesh_filter()->mesh;
        if (!render_is_rebuild(m, 1e-5f, why) || !flat_render_is_rebuild(m, 1e-5f)) {
          bad++;
          std::printf("    STALE after brush %s symmetry %s: %s\n", b, s, why.c_str());
        }
      }
    const MeshCounts d = mesh_counts() - c0;
    std::printf("    55 strokes: %llu in place, %llu full builds, %d stale\n", (unsigned long long)d.in_place, (unsigned long long)d.full_builds, bad);
    CHECK(bad == 0);
    CHECK(d.in_place > 0);
  });

  test("round 508 (test engineer): Grab and Snake Hook dragged with the mouse keep the render mesh a rebuild's after every frame, and an undo of the drag restores it", [] {
    for (const char *brush : {"grab", "snake"}) {
      Editor ed;
      GameObject *g = sphere_scene(ed, 3);
      CHECK(g);
      if (!g) return;
      const Vec2 px = pixel_of(ed, g->world_position());
      const Recti s = ed.scene_rect_for_test();
      const int wx = s.x + (int)px.x, wy = s.y + (int)px.y;
      ed.command("sculpt size 40");
      ed.command("sculpt strength 1");
      ed.command(std::string("sculpt brush ") + brush);
      const std::vector<Vec3> before = g->mesh_filter()->mesh->positions;
      step(ed, {mouse_event(platform::EventType::MouseMove, wx, wy)});
      step(ed, {mouse_event(platform::EventType::MouseDown, wx, wy)});
      int stale = 0;
      for (int i = 1; i <= 10; i++) {
        step(ed, {mouse_event(platform::EventType::MouseMove, wx + 6 * i, wy - 2 * i)});
        std::string why;
        if (!render_is_rebuild(*g->mesh_filter()->mesh, 1e-5f, why)) stale++;
      }
      step(ed, {mouse_event(platform::EventType::MouseUp, wx + 60, wy - 20)});
      step(ed);
      std::string why;
      CHECK(stale == 0);
      CHECK(changed_count(before, g->mesh_filter()->mesh->positions) > 0);
      CHECK(render_is_rebuild(*g->mesh_filter()->mesh, 1e-5f, why));
      ed.command("undo");
      step(ed);
      CHECK(changed_count(before, g->mesh_filter()->mesh->positions) == 0);
      CHECK(render_is_rebuild(*g->mesh_filter()->mesh, 1e-5f, why));
      ed.command("redo");
      step(ed);
      CHECK(render_is_rebuild(*g->mesh_filter()->mesh, 1e-5f, why));
      std::printf("    %s: %d stale frames; last: %s\n", brush, stale, why.c_str());
    }
  });

  test("round 508 (test engineer): with Dyntopo on the render mesh is still a rebuild's after the stroke and after a following stroke with it off (the notes must not leak across)", [] {
    Editor ed;
    GameObject *g = sphere_scene(ed, 2);
    CHECK(g);
    if (!g) return;
    const Vec2 px = pixel_of(ed, g->world_position());
    ed.command("sculpt size 40");
    ed.command("sculpt strength 1");
    ed.command("sculpt front off");
    std::string why;
    ed.command("sculpt dyntopo on");
    ed.command(strprintf("sculpt stroke %g %g %g %g 6", px.x - 15.0f, px.y, px.x + 15.0f, px.y));
    step(ed);
    step(ed);
    CHECK(render_is_rebuild(*g->mesh_filter()->mesh, 1e-5f, why));
    ed.command("sculpt dyntopo off");
    ed.command(strprintf("sculpt stroke %g %g %g %g 6", px.x - 15.0f, px.y + 10.0f, px.x + 15.0f, px.y + 10.0f));
    step(ed);
    step(ed);
    CHECK(render_is_rebuild(*g->mesh_filter()->mesh, 1e-5f, why));
    ed.command("sculpt dyntopo on");  // (the other way round: a plain stroke's notes, then a Dyntopo stroke's unnoted bumps)
    ed.command(strprintf("sculpt stroke %g %g %g %g 6", px.x - 15.0f, px.y - 10.0f, px.x + 15.0f, px.y - 10.0f));
    step(ed);
    CHECK(render_is_rebuild(*g->mesh_filter()->mesh, 1e-5f, why));
    ed.command("undo");
    step(ed);
    CHECK(render_is_rebuild(*g->mesh_filter()->mesh, 1e-5f, why));
    std::printf("    after Dyntopo and undo: %s\n", why.c_str());
  });

  test("round 508 (test engineer): a stroke, its undo, a different stroke, two undos and two redos: the render mesh is a rebuild's at every step", [] {
    Editor ed;
    GameObject *g = sphere_scene(ed, 3);
    CHECK(g);
    if (!g) return;
    const Vec2 px = pixel_of(ed, g->world_position());
    ed.command("sculpt size 30");
    ed.command("sculpt strength 1");
    std::string why;
    auto ok = [&] { return render_is_rebuild(*g->mesh_filter()->mesh, 1e-5f, why); };
    const std::vector<Vec3> p0 = g->mesh_filter()->mesh->positions;
    ed.command(strprintf("sculpt stroke %g %g %g %g 6", px.x - 20.0f, px.y, px.x, px.y));
    step(ed);
    const std::vector<Vec3> p1 = g->mesh_filter()->mesh->positions;
    CHECK(ok());
    ed.command("undo");
    step(ed);
    CHECK(ok() && changed_count(p0, g->mesh_filter()->mesh->positions) == 0);
    ed.command("sculpt brush inflate");
    ed.command(strprintf("sculpt stroke %g %g %g %g 6", px.x - 5.0f, px.y - 15.0f, px.x + 20.0f, px.y + 10.0f));
    step(ed);
    const std::vector<Vec3> p2 = g->mesh_filter()->mesh->positions;
    CHECK(ok() && changed_count(p0, p2) > 0);
    ed.command("undo");
    step(ed);
    CHECK(ok() && changed_count(p0, g->mesh_filter()->mesh->positions) == 0);
    ed.command("redo");
    step(ed);
    CHECK(ok() && changed_count(p2, g->mesh_filter()->mesh->positions) == 0);
    ed.command("undo");
    step(ed);
    CHECK(ok());
    ed.command("redo");
    step(ed);
    CHECK(ok() && changed_count(p2, g->mesh_filter()->mesh->positions) == 0);
    (void)p1;
  });

  test("round 508 (test engineer): duplicating the sculpted object between two dabs leaves two meshes, each a rebuild's, the copy's dab not showing on the original", [] {
    Editor ed;
    GameObject *g = sphere_scene(ed, 3);
    CHECK(g);
    if (!g) return;
    const Vec2 px = pixel_of(ed, g->world_position());
    ed.command("sculpt size 30");
    ed.command("sculpt strength 1");
    ed.command(strprintf("sculpt dab %g %g", px.x, px.y));
    step(ed);
    step(ed);
    std::string why;
    CHECK(render_is_rebuild(*g->mesh_filter()->mesh, 1e-5f, why));
    const size_t objects = ed.scene().objects().size();
    ed.command("duplicate");
    step(ed);
    CHECK(ed.scene().objects().size() == objects + 1);
    std::vector<GameObject *> spheres;
    for (const auto &up : ed.scene().objects()) {
      GameObject *o = up.get();
      if (o->mesh_filter() && o->mesh_filter()->mesh && o->mesh_filter()->mesh->face_count() > 1000) spheres.push_back(o);
    }
    CHECK(spheres.size() == 2);
    if (spheres.size() != 2) return;
    ed.command("sculpt on");
    GameObject *active = ed.active_object_for_test();
    GameObject *other = spheres[0] == active ? spheres[1] : spheres[0];
    const Vec3 home = other->world_position();
    active->set_world_position(home + Vec3(1.5f, 0.0f, 0.0f));  // (apart, so a dab can only hit the one being sculpted)
    for (int i = 0; i < 4; i++) step(ed);
    const std::vector<Vec3> other_before = other->mesh_filter()->mesh->positions;
    const Vec2 apx = pixel_of(ed, active->world_position());
    for (int i = 0; i < 3; i++) {
      ed.command(strprintf("sculpt dab %g %g", apx.x + 3.0f * i, apx.y));
      step(ed);
      CHECK(render_is_rebuild(*active->mesh_filter()->mesh, 1e-5f, why));
      CHECK(render_is_rebuild(*other->mesh_filter()->mesh, 1e-5f, why));
    }
    std::printf("    the duplicate was sculpted (%zu vertices changed), the other has %zu changed vertices\n",
                changed_count(other_before, active->mesh_filter()->mesh->positions), changed_count(other_before, other->mesh_filter()->mesh->positions));
    CHECK(changed_count(other_before, other->mesh_filter()->mesh->positions) == 0);
    CHECK(changed_count(other_before, active->mesh_filter()->mesh->positions) > 0);  // (the dabs landed: a test that moved nothing proves nothing)
  });

  test("round 508 (test engineer): the notes' edge cases on a 34k-vertex sphere - 40 told bumps before the render mesh looks (past the notes kept), ids out of range and repeated, an empty note, NaN in and out, a pole pulled in, a vertex moved and moved back, both render caches", [] {
    auto make = [] { return primitives::uv_sphere(0.5f, 260, 130); };
    MeshPtr told = make(), scanned = make();
    CHECK(told->vert_count() > 33000);
    for (Mesh *m : {told.get(), scanned.get()}) {
      m->render_mesh();
      m->render_mesh(true);
      m->positions[3] += Vec3(0.001f, 0.0f, 0.0f);
      m->deform_version++;
      if (m == told.get()) m->note_moved({3});
      m->render_mesh();
      m->render_mesh(true);
    }
    auto bump = [&](const std::vector<uint32_t> &ids, bool tell_extra = true) {
      told->deform_version++, scanned->deform_version++;
      if (tell_extra) told->note_moved(ids);
    };
    std::string why;
    /* 40 told bumps in a row before anything looks: the log keeps 32. A cache that far behind must scan, not trust a partial chain. */
    for (int i = 0; i < 40; i++) {
      const Vec3 at = normalize(Vec3(std::cos(i * 0.7f), std::sin(i * 0.3f), std::sin(i * 0.7f))) * 0.5f;
      const std::vector<uint32_t> ids = push_patch(*told, at, 0.04f, 0.01f);
      push_patch(*scanned, at, 0.04f, 0.01f);
      bump(ids);
    }
    CHECK(same_render(*told, *scanned) && same_bounds(*told, *scanned));
    CHECK(render_is_rebuild(*told, 1e-5f, why));
    CHECK(flat_render_is_rebuild(*told, 1e-5f));
    /* Ids out of range, repeated, and an empty note. */
    for (Mesh *m : {told.get(), scanned.get()}) m->positions[7] += Vec3(0.0f, 0.0f, 0.02f);
    bump({7, 7, 7, 0xFFFFFFF0u, (uint32_t)told->vert_count()});
    CHECK(same_render(*told, *scanned) && same_bounds(*told, *scanned));
    bump({});  // (a bump that moved nothing, said so)
    CHECK(same_render(*told, *scanned));
    /* NaN in, then back out. */
    for (Mesh *m : {told.get(), scanned.get()}) m->positions[100] = Vec3(std::nanf(""), 0.1f, 0.1f);
    bump({100});
    CHECK(same_bounds(*told, *scanned));
    told->render_mesh(), scanned->render_mesh();
    CHECK(std::isfinite(told->render_mesh().bounds.max.x) && std::isfinite(told->render_mesh().bounds.min.x));
    for (Mesh *m : {told.get(), scanned.get()}) m->positions[100] = Vec3(0.2f, 0.1f, 0.1f);
    bump({100});
    CHECK(same_render(*told, *scanned) && same_bounds(*told, *scanned));
    /* A pole pulled in: the box shrinks on that side. */
    uint32_t top = 0;
    for (uint32_t v = 0; v < (uint32_t)told->vert_count(); v++)
      if (told->positions[v].y > told->positions[top].y) top = v;
    for (Mesh *m : {told.get(), scanned.get()}) m->positions[top].y -= 0.08f;
    bump({top});
    CHECK(same_bounds(*told, *scanned));
    CHECK(render_is_rebuild(*told, 1e-5f, why));
    /* A vertex pushed out of the box and back in again between two looks: the final position is what counts. */
    const Vec3 p500 = told->positions[500];
    for (Mesh *m : {told.get(), scanned.get()}) m->positions[500] = p500 * 3.0f;
    bump({500});
    for (Mesh *m : {told.get(), scanned.get()}) m->positions[500] = p500;
    bump({500});
    CHECK(same_render(*told, *scanned) && same_bounds(*told, *scanned));
    CHECK(render_is_rebuild(*told, 1e-5f, why));
    /* Flat cache looked at first this time (the smooth one is behind by two bumps). */
    for (int i = 0; i < 2; i++) {
      const std::vector<uint32_t> ids = push_patch(*told, Vec3(0.0f, -0.5f, 0.0f), 0.05f, 0.03f * (i + 1));
      push_patch(*scanned, Vec3(0.0f, -0.5f, 0.0f), 0.05f, 0.03f * (i + 1));
      bump(ids);
      if (i == 0) told->render_mesh(true), scanned->render_mesh(true);
    }
    CHECK(flat_render_is_rebuild(*told, 1e-5f));
    CHECK(same_render(*told, *scanned) && same_bounds(*told, *scanned));
  });

  test("round 508 (test engineer): a mesh copied between two dabs, and the copy and the original each dabbed on, render like rebuilds", [] {
    MeshPtr a = primitives::uv_sphere(0.5f, 120, 60);
    a->render_mesh();
    a->positions[3] += Vec3(0.001f, 0, 0);
    a->deform_version++, a->note_moved({3});
    a->render_mesh();
    std::vector<uint32_t> ids = push_patch(*a, Vec3(0.5f, 0, 0), 0.08f, 0.02f);
    a->deform_version++, a->note_moved(ids);
    Mesh b = *a;  // (copied after a told bump, before the render mesh looked at it)
    std::string why;
    CHECK(render_is_rebuild(b, 1e-5f, why));
    ids = push_patch(b, Vec3(-0.5f, 0, 0), 0.08f, 0.05f);
    b.deform_version++, b.note_moved(ids);
    CHECK(render_is_rebuild(b, 1e-5f, why));
    ids = push_patch(*a, Vec3(0, 0.5f, 0), 0.08f, 0.04f);
    a->deform_version++, a->note_moved(ids);
    CHECK(render_is_rebuild(*a, 1e-5f, why));
    Mesh c = b;  // (and a copy of a copy, whose caches are warm)
    ids = push_patch(c, Vec3(0, 0, 0.5f), 0.08f, 0.04f);
    c.deform_version++, c.note_moved(ids);
    CHECK(render_is_rebuild(c, 1e-5f, why));
    CHECK(render_is_rebuild(b, 1e-5f, why));
    /* Assigning over a mesh whose caches are warm and whose notes are of another history. */
    Mesh d = *a;
    d.render_mesh();
    d = c;
    ids = push_patch(d, Vec3(0.5f, 0, 0), 0.08f, 0.03f);
    d.deform_version++, d.note_moved(ids);
    CHECK(render_is_rebuild(d, 1e-5f, why));
  });

  test("round 508 (test engineer): told dabs on a huge (radius 5e5) and a tiny (radius 5e-7) sphere give the box and normals of a rebuild", [] {
    for (float s : {1e6f, 1e-6f}) {
      MeshPtr told = primitives::uv_sphere(0.5f * s, 100, 50), scanned = primitives::uv_sphere(0.5f * s, 100, 50);
      for (Mesh *m : {told.get(), scanned.get()}) {
        m->render_mesh();
        m->positions[3] += Vec3(0.001f * s, 0, 0);
        m->deform_version++;
        if (m == told.get()) m->note_moved({3});
        m->render_mesh();
      }
      std::string why;
      for (int i = 0; i < 5; i++) {
        const Vec3 at = normalize(Vec3(std::cos(i * 1.3f), 0.4f, std::sin(i * 1.3f)));
        const std::vector<uint32_t> ids = push_patch_scaled(*told, at * 0.5f, 0.07f, i == 4 ? 0.3f : 0.02f, s);
        push_patch_scaled(*scanned, at * 0.5f, 0.07f, i == 4 ? 0.3f : 0.02f, s);
        told->deform_version++, scanned->deform_version++;
        told->note_moved(ids);
        CHECK(same_render(*told, *scanned) && same_bounds(*told, *scanned));
      }
      CHECK(render_is_rebuild(*told, 1e-5f * s, why) || (std::printf("    scale %g: %s\n", (double)s, why.c_str()), false));
    }
  });

  test("round 508 (test engineer): a position edit that bumps deform_version but does not say so, between two told dabs, is not lost", [] {
    MeshPtr told = primitives::uv_sphere(0.5f, 100, 50), scanned = primitives::uv_sphere(0.5f, 100, 50);
    for (Mesh *m : {told.get(), scanned.get()}) {
      m->render_mesh();
      m->positions[3] += Vec3(0.001f, 0, 0);
      m->deform_version++;
      if (m == told.get()) m->note_moved({3});
      m->render_mesh();
    }
    /* told dab, then a script move (bump, no note), then a told dab, all before the render mesh looks. */
    std::vector<uint32_t> ids = push_patch(*told, Vec3(0.5f, 0, 0), 0.07f, 0.02f);
    push_patch(*scanned, Vec3(0.5f, 0, 0), 0.07f, 0.02f);
    told->deform_version++, scanned->deform_version++;
    told->note_moved(ids);
    for (Mesh *m : {told.get(), scanned.get()}) m->positions[2000] += Vec3(0.05f, 0.05f, 0);
    told->deform_version++, scanned->deform_version++;
    ids = push_patch(*told, Vec3(-0.5f, 0, 0), 0.07f, 0.02f);
    push_patch(*scanned, Vec3(-0.5f, 0, 0), 0.07f, 0.02f);
    told->deform_version++, scanned->deform_version++;
    told->note_moved(ids);
    CHECK(same_render(*told, *scanned));
    std::string why;
    CHECK(render_is_rebuild(*told, 1e-5f, why));
    /* The same with the script's bump last: the cache is behind by a told bump and an unnoted one. */
    ids = push_patch(*told, Vec3(0, 0.5f, 0), 0.07f, 0.02f);
    push_patch(*scanned, Vec3(0, 0.5f, 0), 0.07f, 0.02f);
    told->deform_version++, scanned->deform_version++;
    told->note_moved(ids);
    for (Mesh *m : {told.get(), scanned.get()}) m->positions[3000] += Vec3(0.05f, 0, 0.05f);
    told->deform_version++, scanned->deform_version++;
    CHECK(same_render(*told, *scanned));
    CHECK(render_is_rebuild(*told, 1e-5f, why));
  });

  test("round 508 (review): a vertex moved with touch() only, between two told dabs and no frame drawn, still shows in the render mesh, its normals and box", [] {
    auto make = [] {
      MeshPtr mp = primitives::uv_sphere(0.5f, 64, 32);
      mp->touch();
      return mp;
    };
    MeshPtr told = make(), scanned = make();
    for (Mesh *m : {told.get(), scanned.get()}) {
      m->render_mesh();
      m->positions[5] += Vec3(0.001f, 0.0f, 0.0f);  // (a told dab, drawn: the cache is at it)
      m->deform_version++;
      if (m == told.get()) m->note_moved({5});
      m->render_mesh();
      m->positions[900] += Vec3(0.4f, 0.3f, 0.2f);  // (an edit someone made with touch() alone - a console line between two dabs)
      m->touch();
      m->positions[7] += Vec3(0.0f, 0.002f, 0.0f);  // (the next told dab, without vertex 900; no frame in between)
      m->deform_version++;
      if (m == told.get()) m->note_moved({7});
    }
    CHECK(same_render(*told, *scanned));
    std::string why;
    CHECK(render_is_rebuild(*told, 1e-5f, why));
    if (!why.empty()) std::printf("    vs a rebuild: %s\n", why.c_str());
  });
}

static void tests_round540() {
  test("per brush settings (round 540): Draw keeps its size and strength while Smooth has its own - through the console, and a brush used for the first time starts with the size you had", [] {
    Editor ed;
    ed.init_headless(1400, 900);
    const SculptBrush &b = ed.sculpt_brush_for_test();
    ed.command("sculpt size 30");
    ed.command("sculpt strength 0.2");
    ed.command("sculpt brush smooth");
    CHECK(b.radius == 30.0f);  // (never used: the size carries over)
    ed.command("sculpt size 90");
    ed.command("sculpt strength 0.8");
    ed.command("sculpt brush draw");
    CHECK(b.radius == 30.0f && b.strength == 0.2f);
    ed.command("sculpt brush smooth");
    CHECK(b.radius == 90.0f && b.strength == 0.8f);
    /* Weight Paint's brushes the same way. */
    ed.command("wpbrush add 40");
    ed.command("wpbrush subtract 70");
    ed.command("wpbrush add");
    CHECK(ed.wp_brush_for_test().radius == 40.0f);
    ed.command("wpbrush subtract");
    CHECK(ed.wp_brush_for_test().radius == 70.0f);
  });

  test("per brush settings (round 540): Unified Size shares one size between the brushes while the strength stays per brush", [] {
    Editor ed;
    ed.init_headless(1400, 900);
    const SculptBrush &b = ed.sculpt_brush_for_test();
    ed.command("brush unified sculpt size on");
    ed.command("sculpt size 30");
    ed.command("sculpt strength 0.2");
    ed.command("sculpt brush smooth");
    ed.command("sculpt size 90");
    ed.command("sculpt strength 0.8");
    ed.command("sculpt brush draw");
    CHECK(b.radius == 90.0f);
    CHECK(b.strength == 0.2f);
  });

  test("per brush settings (round 540): Paint's Smudge has its own size and colour; Spacing Pressure packs the dabs closer for a firm press", [] {
    Editor ed;
    ed.init_headless(1400, 900);
    PaintBrush &p = ed.paint_brush_for_test();
    ed.command("paint size 40");
    ed.command("paint color 1 0 0");
    ed.command("paint brush smudge");
    ed.command("paint size 80");
    ed.command("paint color 0 0 1");
    ed.command("paint brush paint");
    CHECK(p.radius == 40.0f && p.color.x == 1.0f && p.color.z == 0.0f);
    ed.command("paint brush smudge");
    CHECK(p.radius == 80.0f && p.color.z == 1.0f);
    /* Spacing: off, the field's; on, a firm press packs the dabs closer (1.5 - curve(pressure)). */
    ed.command("paint spacing 0.4");
    CHECK(ed.paint_spacing_for_test(1.0f) == 0.4f && ed.paint_spacing_for_test(0.25f) == 0.4f);
    ed.command("brush curve spacing on");
    CHECK(ed.paint_spacing_for_test(1.0f) < ed.paint_spacing_for_test(0.25f));
    ed.command("brush curve spacing on 0 1 1 1");  // (a flat curve: 1.5 - 1 of the field everywhere)
    CHECK(std::fabs(ed.paint_spacing_for_test(0.25f) - 0.2f) < 1e-4f);
  });

  test("per brush settings (round 540): the banks save to brushes.txt and load back; garbage loads what it can, NaN comes back clamped", [] {
    Editor ed;
    ed.init_headless(1400, 900);
    const std::string dir = fs::join(test_dir(), "r540_prefs_impl");
    fs::make_dirs(dir);
    ed.prefs_for_test(fs::join(dir, "prefs.txt"));
    ed.command("sculpt size 30");
    ed.command("sculpt brush smooth");
    ed.command("sculpt size 90");
    ed.command("brush unified paint colour on");
    ed.command("brush curve hardness on 0 1 1 1");
    ed.command("brush save");
    ed.sculpt_bank_for_test() = BrushBank();
    ed.command("sculpt size 5");
    ed.command("brush curve hardness off 0 0 1 1");
    ed.command("brush load");
    CHECK(ed.sculpt_brush_for_test().kind == 1 && ed.sculpt_brush_for_test().radius == 90.0f);
    ed.command("sculpt brush draw");
    CHECK(ed.sculpt_brush_for_test().radius == 30.0f);
    CHECK(ed.paint_bank_for_test().unified_color);
    CHECK(ed.paint_brush_for_test().hardness_pressure && ed.paint_brush_for_test().hardness_curve[0].y == 1.0f);
    /* Garbage and NaN. */
    ed.brushes_load_text_for_test("bank sculpt 0 0 0 0\nslot sculpt 0 nan 2 \nslot sculpt 99 5 5\nslot sculpt x\n\x01\x02 junk\npaint_hardness_curve 1 1\nslot sculpt 0 1e30 0.5\nslot sculpt 1");
    const SculptBrush &b = ed.sculpt_brush_for_test();
    CHECK(std::isfinite(b.radius) && b.radius >= 1.0f && b.radius <= 500.0f);
    CHECK(b.strength >= 0.0f && b.strength <= 1.0f);
    ed.brushes_load_text_for_test("");
    ed.brushes_load_text_for_test(std::string(300, '\0'));
  });

  test("per brush settings (round 540): brushes.txt of a headless Editor is inside the scratch folder, never the user's preferences folder; with no scratch preferences Save and Load refuse", [] {
    const std::string dir = fs::join(test_dir(), "r540_prefs_path");
    fs::make_dirs(dir);
    const char *had = std::getenv("BLENDITY_PREFS_DIR");
    const std::string before = had ? had : "";
    set_env("BLENDITY_PREFS_DIR", dir);  // (what a scratch run gives: set before the Editor works out its preferences folder)
    {
      Editor ed;
      ed.init_headless(1400, 900);
      const std::string path = fs::normalize(ed.brushes_path_for_test());
      std::printf("    brushes.txt: %s\n", path.c_str());
      CHECK(!path.empty());
      CHECK(path.rfind(fs::normalize(test_dir()), 0) == 0);
      CHECK(path.rfind(fs::normalize(fs::join(fs::home_dir(), ".blendity")), 0) != 0);
    }
    /* No variable is no folder: no path, and Save writes nothing. (Round 556: cleared with clear_env - set_env(k, "") now
     * means the run's own preferences folder, as it does for the project and the trash.) */
    clear_env("BLENDITY_PREFS_DIR");
    {
      Editor ed;
      ed.init_headless(1400, 900);
      CHECK(ed.brushes_path_for_test().empty());
      ed.command("brush save");
      ed.command("brush load");  // (a refusal, not a crash)
    }
    set_env("BLENDITY_PREFS_DIR", before);
  });

  test("per brush settings (round 540): Unified Size writes the shared size, not the brushes' own: Draw 30, Smooth 90, Unified on, 55 set and brushes switched, Unified off gives Draw 30 and Smooth 90 again", [] {
    Editor ed;
    ed.init_headless(1400, 900);
    const SculptBrush &b = ed.sculpt_brush_for_test();
    ed.command("sculpt size 30");
    ed.command("sculpt brush smooth");
    ed.command("sculpt size 90");
    ed.command("brush unified sculpt size on");
    ed.command("sculpt size 55");
    ed.command("sculpt brush draw");
    CHECK(b.radius == 55.0f);
    ed.command("sculpt brush grab");
    ed.command("sculpt brush smooth");
    CHECK(b.radius == 55.0f);
    ed.command("brush unified sculpt size off");
    CHECK(b.radius == 90.0f);  // (the active brush shows its own at once)
    ed.command("sculpt brush draw");
    CHECK(b.radius == 30.0f);
    ed.command("sculpt brush smooth");
    CHECK(b.radius == 90.0f);
    /* The same through a ticked box seen only by the frame's sync: the flag written, no command. */
    ed.sculpt_bank_for_test().unified_strength = true;
    ed.brush_banks_sync_for_test();
    ed.command("sculpt strength 0.9");
    ed.command("sculpt brush draw");
    CHECK(std::fabs(b.strength - 0.9f) < 1e-5f);
    ed.sculpt_bank_for_test().unified_strength = false;
    ed.brush_banks_sync_for_test();
    CHECK(std::fabs(b.strength - 0.5f) < 1e-5f);  // (Draw's own, never changed from the start)
  });
}

/* ---- round 540 (task 0520), the test engineer's tests: size / strength / colour per brush, the pressure curves, brushes.txt ---- */

static platform::Event r540_ev(platform::EventType t, int x, int y, float pressure = 1.0f) {
  platform::Event e;
  e.type = t, e.x = x, e.y = y, e.pressure = pressure;
  return e;
}

static platform::Event r540_key(int key, int mods = 0) {
  platform::Event e;
  e.type = platform::EventType::KeyDown, e.key = key, e.mods = mods;
  return e;
}

/* The Modeling Tools window open with Sculpt on over the sample scene's Cube, buttons recorded. */
static void r540_sculpt_window(Editor &ed) {
  ed.init_headless(1600, 1000);
  ed.set_clock_for_test(100.0);
  ed.command("layout Default");
  ed.command("window Modeling Tools");
  ed.command("alltools on");
  ed.command("select Cube");
  ed.command("sculpt on");
  ed.record_buttons_for_test(true);
  for (int i = 0; i < 4; i++) step(ed);
}

/* The Tools window's content is longer than the window (alltools): wheel it until the control `find` locates is inside the window
 * (a field below the screen takes no click; that is why the first version of these tests typed into nothing). */
static void r540_scroll_to(Editor &ed, const std::function<bool(Recti &)> &find) {
  for (int i = 0; i < 80; i++) {
    Recti r;
    if (!find(r)) {
      step(ed);
      continue;
    }
    const Recti w = ed.window_rect_for_test(WindowKind::Tools);
    if (r.y >= w.y + 24 && r.y + r.h <= w.y + w.h - 4) return;
    platform::Event e = r540_ev(platform::EventType::Wheel, w.x + w.w / 2, w.y + w.h / 2);
    e.wheel_y = r.y > w.y ? -1.0f : 1.0f;
    step(ed, {r540_ev(platform::EventType::MouseMove, w.x + w.w / 2, w.y + w.h / 2)});
    step(ed, {e});
  }
}

static bool r540_click(Editor &ed, const std::string &key, int clicks = 1) {
  Recti r;
  for (int i = 0; i < 4 && !ed.button_rect_for_test(key, r); i++) step(ed);
  if (!ed.button_rect_for_test(key, r)) return false;
  r540_scroll_to(ed, [&](Recti &o) { return ed.button_rect_for_test(key, o); });
  if (!ed.button_rect_for_test(key, r)) return false;
  /* A tick box's recorded rectangle is its whole value column: the box itself is at its left end. */
  const bool tick = key.rfind("field:Unified", 0) == 0;
  const int x = tick ? r.x + 6 : r.x + r.w / 2, y = r.y + r.h / 2;
  if (std::getenv("R540_DEBUG")) {
    const Recti w = ed.window_rect_for_test(WindowKind::Tools);
    std::printf("    [r540] %s at %d,%d %dx%d; Tools window %d,%d %dx%d\n", key.c_str(), r.x, r.y, r.w, r.h, w.x, w.y, w.w, w.h);
  }
  step(ed, {r540_ev(platform::EventType::MouseMove, x, y)});
  for (int c = 0; c < clicks; c++) {
    step(ed, {r540_ev(platform::EventType::MouseDown, x, y)});
    step(ed, {r540_ev(platform::EventType::MouseUp, x, y)});
  }
  step(ed);
  step(ed);
  return true;
}

/* A number typed into the Inspector-style field `key` ("field:Size"): double click, select all, type, Enter. */
static bool r540_type(Editor &ed, const std::string &key, const std::string &text) {
  if (!r540_click(ed, key, 2)) return false;
  step(ed, {r540_key(platform::KEY_A, platform::MOD_CTRL)});
  for (char c : text) {
    platform::Event e;
    e.type = platform::EventType::Text, e.codepoint = (uint32_t)c;
    step(ed, {e});
  }
  step(ed, {r540_key(platform::KEY_ENTER)});
  step(ed);
  step(ed);
  return true;
}

/* A dropdown opened by the option it shows now and one of its items clicked. */
static bool r540_pick(Editor &ed, const char *shown, const char *item) {
  step(ed);
  auto find = [&](Recti &o) {
    for (const ui::Context::ControlRec &k : ed.ui_for_test().controls)
      if (std::string(k.kind) == "combo" && k.label == shown && k.r.w > 0) {
        o = k.r;
        return true;
      }
    return false;
  };
  r540_scroll_to(ed, find);
  Recti c;
  if (!find(c)) return false;
  for (platform::EventType t : {platform::EventType::MouseMove, platform::EventType::MouseDown, platform::EventType::MouseUp})
    step(ed, {r540_ev(t, c.x + c.w / 2, c.y + c.h / 2)});
  step(ed);
  return r540_click(ed, std::string("menu:") + item);
}

static std::string r540_scratch_prefs_dir(const char *name) {
  const std::string d = fs::join(test_dir(), name);
  fs::make_dirs(d);
  return d;
}

/* The Cube with a surface, Paint on, a frame drawn: the view-relative pixel of the Cube's middle. */
static Vec2 r540_paint_scene(Editor &ed) {
  ed.init_headless(1400, 900);
  ed.set_clock_for_test(100.0);
  for (int i = 0; i < 5; i++) step(ed);
  ed.command("select Cube");
  ed.command("surface new R540Surf");
  ed.command("paint on");
  for (int i = 0; i < 4; i++) step(ed);
  const Recti v = ed.scene_view_rect();
  return Vec2((float)v.w * 0.5f, (float)v.h * 0.5f);
}

static void tests_round540_te() {
  test("per brush settings (round 540, te): choosing Draw, Smooth, Draw in the Brush box and typing a Size and a Strength each time gives every brush its own, by real clicks", [] {
    Editor ed;
    r540_sculpt_window(ed);
    const SculptBrush &b = ed.sculpt_brush_for_test();
    CHECK(b.kind == 0);
    const bool size_draw = r540_type(ed, "field:Size", "30");
    const bool str_draw = r540_type(ed, "field:Strength", "0.2");
    CHECK(size_draw && str_draw);
    CHECK(b.radius == 30.0f && std::fabs(b.strength - 0.2f) < 1e-5f);
    CHECK(r540_pick(ed, "Draw", "Smooth"));
    CHECK(b.kind == 1);
    CHECK(b.radius == 30.0f);  // (Smooth was never used: it starts with the size in hand)
    CHECK(r540_type(ed, "field:Size", "90"));
    CHECK(r540_type(ed, "field:Strength", "0.8"));
    CHECK(b.radius == 90.0f && std::fabs(b.strength - 0.8f) < 1e-5f);
    CHECK(r540_pick(ed, "Smooth", "Draw"));
    std::printf("    back on Draw: size %g strength %g\n", b.radius, b.strength);
    CHECK(b.kind == 0 && b.radius == 30.0f && std::fabs(b.strength - 0.2f) < 1e-5f);
    /* The Inspector shows the brush's own values too, not only the struct. */
    ed.step_frame_headless();
    CHECK(r540_pick(ed, "Draw", "Smooth"));
    CHECK(b.radius == 90.0f && std::fabs(b.strength - 0.8f) < 1e-5f);
    /* The tick box: Unified Size by a click, then a size typed on Smooth is Draw's too. */
    CHECK(r540_click(ed, "field:Unified Size"));
    CHECK(ed.sculpt_bank_for_test().unified_size);
    CHECK(r540_type(ed, "field:Size", "55"));
    CHECK(r540_pick(ed, "Smooth", "Draw"));
    CHECK(b.radius == 55.0f && std::fabs(b.strength - 0.2f) < 1e-5f);
  });

  test("per brush settings (round 540, te): Weight Paint's Add, Subtract, Draw and Blur each keep their Radius and Strength through the console, and an unused one starts with the last", [] {
    Editor ed;
    ed.init_headless(1400, 900);
    const WeightBrush &w = ed.wp_brush_for_test();
    ed.command("wpbrush add 40");
    ed.command("wpbrush blur 70");
    CHECK(w.radius == 70.0f);
    ed.command("wpbrush add");
    CHECK(w.radius == 40.0f);
    ed.command("wpbrush blur");
    CHECK(w.radius == 70.0f);
    ed.command("wpbrush draw");  // (never used: the size in hand, 70, carries over)
    CHECK(w.radius == 70.0f);
    ed.command("wpbrush add");
    CHECK(w.radius == 40.0f);
    /* The strength through the Weight Paint bank: the live brush's strength is switched along with the size. */
    ed.command("wpbrush add 40 0.9");
    ed.command("wpbrush blur 70 0.1");
    ed.command("wpbrush add");
    std::printf("    Add: radius %g strength %g\n", w.radius, w.strength);
    CHECK(w.radius == 40.0f && std::fabs(w.strength - 0.9f) < 1e-5f);
    ed.command("wpbrush blur");
    CHECK(w.radius == 70.0f && std::fabs(w.strength - 0.1f) < 1e-5f);
  });

  test("per brush settings (round 540, te): Paint, Smudge and Blend keep their own size, opacity and colour; the colour is shared when Unified Colour is on", [] {
    Editor ed;
    ed.init_headless(1400, 900);
    const PaintBrush &p = ed.paint_brush_for_test();
    ed.command("paint size 40");
    ed.command("paint opacity 0.3");
    ed.command("paint color 1 0 0");
    ed.command("paint brush smudge");
    CHECK(p.radius == 40.0f && p.color.x == 1.0f);  // (first use: what was in hand)
    ed.command("paint size 80");
    ed.command("paint opacity 0.9");
    ed.command("paint color 0 0 1");
    ed.command("paint brush blend");
    CHECK(p.radius == 80.0f);  // (Blend never used: Smudge's size carries over)
    ed.command("paint size 120");
    ed.command("paint color 0 1 0");
    ed.command("paint brush paint");
    CHECK(p.radius == 40.0f && std::fabs(p.opacity - 0.3f) < 1e-5f && p.color.x == 1.0f && p.color.y == 0.0f && p.color.z == 0.0f);
    ed.command("paint brush smudge");
    CHECK(p.radius == 80.0f && std::fabs(p.opacity - 0.9f) < 1e-5f && p.color.z == 1.0f && p.color.x == 0.0f);
    ed.command("paint brush blend");
    CHECK(p.radius == 120.0f && p.color.y == 1.0f);
    /* Unified Colour: one colour for all three, sizes still apart. */
    ed.command("brush unified paint colour on");
    ed.command("paint color 0.5 0.5 0.5");
    ed.command("paint brush paint");
    CHECK(p.radius == 40.0f && p.color.x == 0.5f && p.color.y == 0.5f && p.color.z == 0.5f);
    ed.command("paint brush smudge");
    CHECK(p.radius == 80.0f && p.color.x == 0.5f);
  });

  test("per brush settings (round 540, te): with Unified Size and Unified Strength both on, every brush shows the one size and strength; a tip change (round, flat) is not a brush switch", [] {
    Editor ed;
    ed.init_headless(1400, 900);
    const SculptBrush &b = ed.sculpt_brush_for_test();
    ed.command("sculpt size 30");
    ed.command("sculpt strength 0.2");
    ed.command("sculpt brush smooth");
    ed.command("sculpt size 90");
    ed.command("sculpt strength 0.8");
    ed.command("brush unified sculpt size on");
    ed.command("brush unified sculpt strength on");
    ed.command("sculpt brush draw");
    CHECK(b.radius == 90.0f && std::fabs(b.strength - 0.8f) < 1e-5f);
    ed.command("sculpt size 10");
    ed.command("sculpt brush clay");
    CHECK(b.radius == 10.0f && std::fabs(b.strength - 0.8f) < 1e-5f);
    /* Paint's round / flat / bristle tips are the same brush Mode: size and colour stay. */
    ed.command("paint size 33");
    ed.command("paint color 0.2 0.4 0.6");
    ed.command("paint brush flat");
    ed.command("paint brush bristle");
    ed.command("paint brush round");
    CHECK(ed.paint_brush_for_test().radius == 33.0f && ed.paint_brush_for_test().color.y == 0.4f);
  });

  test("per brush settings (round 540, te): a brush used for the first time starts with the size and strength you had, in all three modes, and the brushes already used keep theirs", [] {
    Editor ed;
    ed.init_headless(1400, 900);
    const SculptBrush &b = ed.sculpt_brush_for_test();
    ed.command("sculpt size 77");
    ed.command("sculpt strength 0.35");
    for (const char *name : {"smooth", "grab", "clay", "inflate", "flatten"}) {
      ed.command(std::string("sculpt brush ") + name);
      CHECK(b.radius == 77.0f && std::fabs(b.strength - 0.35f) < 1e-5f);  // (no jump to a default)
    }
    ed.command("sculpt size 12");
    ed.command("sculpt brush draw");
    CHECK(b.radius == 77.0f);  // (Draw was never touched after its first use)
    ed.command("sculpt brush flatten");
    CHECK(b.radius == 12.0f);
    ed.command("wpbrush add 61");
    ed.command("wpbrush draw");
    CHECK(ed.wp_brush_for_test().radius == 61.0f);
    ed.command("paint size 27");
    ed.command("paint brush blend");
    CHECK(ed.paint_brush_for_test().radius == 27.0f);
  });

  test("per brush settings (round 540, te): Hardness Pressure off leaves a dab's hardness at the field's whatever the pressure; on, half the pressure gives half the hardness; a flat curve keeps it full", [] {
    Editor ed;
    const Vec2 c = r540_paint_scene(ed);
    CHECK(ed.paint_for_test());
    ed.command("paint hardness 0.8");
    ed.command("paint size 12");
    auto dab = [&](float pressure) {
      ed.command(strprintf("paint dab %g %g %g", c.x, c.y, pressure));
      return ed.paint_last_dab_hardness_for_test();
    };
    /* Off (the default). */
    CHECK(!ed.paint_brush_for_test().hardness_pressure);
    const float off_low = dab(0.25f), off_high = dab(1.0f);
    std::printf("    off: pressure 0.25 -> %.3f, pressure 1 -> %.3f\n", off_low, off_high);
    CHECK(std::fabs(off_low - 0.8f) < 1e-5f && std::fabs(off_high - 0.8f) < 1e-5f);
    /* On, the default straight curve. */
    ed.command("brush curve hardness on");
    const float half = dab(0.5f), full = dab(1.0f), none = dab(0.0f), quarter = dab(0.25f);
    std::printf("    on: pressure 0.5 -> %.3f, 1 -> %.3f, 0 -> %.3f, 0.25 -> %.3f\n", half, full, none, quarter);
    CHECK(std::fabs(half - 0.4f) < 1e-4f && std::fabs(full - 0.8f) < 1e-4f && none < 1e-4f && std::fabs(quarter - 0.2f) < 1e-4f);
    /* A flat curve at the top: the full hardness at any pressure. */
    ed.command("brush curve hardness on 0 1 1 1");
    CHECK(std::fabs(dab(0.5f) - 0.8f) < 1e-4f && std::fabs(dab(0.05f) - 0.8f) < 1e-4f);
    /* Out-of-range and NaN pressure give a hardness in 0..1, never NaN. */
    ed.command("brush curve hardness on 0 0 1 1");
    const float over = dab(7.0f), neg = dab(-3.0f), nan = dab(std::numeric_limits<float>::quiet_NaN());
    CHECK(std::isfinite(over) && std::isfinite(neg) && std::isfinite(nan));
    CHECK(over >= 0.0f && over <= 1.0f && neg >= 0.0f && neg <= 1.0f && nan >= 0.0f && nan <= 1.0f);
    /* Off again: the field's. */
    ed.command("brush curve hardness off");
    CHECK(std::fabs(dab(0.3f) - 0.8f) < 1e-5f);
  });

  test("per brush settings (round 540, te): a pen stroke pressing at half pressure with Hardness Pressure on paints at half the Hardness field, through real mouse events", [] {
    Editor ed;
    const Vec2 c = r540_paint_scene(ed);
    CHECK(ed.paint_for_test());
    ed.command("paint hardness 0.6");
    ed.command("paint size 10");
    ed.command("brush curve hardness on");
    const Recti v = ed.scene_view_rect();
    const int x = v.x + (int)c.x, y = v.y + (int)c.y;
    step(ed, {r540_ev(platform::EventType::MouseMove, x, y, 0.5f)});
    step(ed, {r540_ev(platform::EventType::MouseDown, x, y, 0.5f)});
    step(ed, {r540_ev(platform::EventType::MouseMove, x + 14, y, 0.5f)});
    step(ed, {r540_ev(platform::EventType::MouseMove, x + 30, y, 0.5f)});
    step(ed, {r540_ev(platform::EventType::MouseUp, x + 30, y, 0.5f)});
    step(ed);
    std::printf("    last dab hardness at pen pressure 0.5: %.3f (the field 0.6)\n", ed.paint_last_dab_hardness_for_test());
    CHECK(std::fabs(ed.paint_last_dab_hardness_for_test() - 0.3f) < 1e-3f);
  });

  test("per brush settings (round 540, te): Spacing Pressure packs the dabs closer as the pen presses harder, never below 5 percent of the radius, and a missing or NaN pressure is full pressure", [] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.command("paint spacing 0.5");
    ed.command("brush curve spacing on");
    float last = 1e9f;
    for (float p : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
      const float s = ed.paint_spacing_for_test(p);
      std::printf("    spacing at pressure %.2f: %.3f\n", p, s);
      CHECK(s < last);  // (a firmer press, closer dabs)
      last = s;
    }
    CHECK(std::fabs(ed.paint_spacing_for_test(1.0f) - 0.25f) < 1e-4f);  // (0.5 * (1.5 - 1))
    CHECK(std::fabs(ed.paint_spacing_for_test(0.0f) - 0.75f) < 1e-4f);  // (0.5 * 1.5)
    CHECK(std::fabs(ed.paint_spacing_for_test(std::numeric_limits<float>::quiet_NaN()) - ed.paint_spacing_for_test(1.0f)) < 1e-4f);
    CHECK(ed.paint_spacing_for_test(50.0f) == ed.paint_spacing_for_test(1.0f) && ed.paint_spacing_for_test(-4.0f) == ed.paint_spacing_for_test(0.0f));
    /* The floor, with the smallest field spacing. */
    ed.command("paint spacing 0.05");
    CHECK(ed.paint_spacing_for_test(1.0f) >= 0.05f && ed.paint_spacing_for_test(0.0f) >= 0.05f);
    /* A curve the other way round: a firm press spreads the dabs. */
    ed.command("paint spacing 0.5");
    ed.command("brush curve spacing on 0 1 1 0");
    CHECK(ed.paint_spacing_for_test(1.0f) > ed.paint_spacing_for_test(0.0f));
    /* Off: the field's at any pressure, and hardness pressure alone does not touch it. */
    ed.command("brush curve spacing off");
    ed.command("brush curve hardness on");
    CHECK(ed.paint_spacing_for_test(0.1f) == 0.5f && ed.paint_spacing_for_test(1.0f) == 0.5f);
  });

  test("per brush settings (round 540, te): sizes on two brushes, two curves and the switches survive Save, a reset of the banks and Load, and a new Editor on the same preferences folder", [] {
    const std::string dir = r540_scratch_prefs_dir("r540_prefs_a");
    std::filesystem::remove(fs::join(dir, "brushes.txt"));
    std::string text;
    {
      Editor ed;
      ed.init_headless(1400, 900);
      ed.prefs_for_test(fs::join(dir, "prefs.txt"));
      const std::string path = fs::normalize(ed.brushes_path_for_test());
      CHECK(path.rfind(fs::normalize(test_dir()), 0) == 0);  // (never the user's folder: checked before anything is written)
      if (path.rfind(fs::normalize(test_dir()), 0) != 0) return;
      CHECK(path.size() > 11 && path.substr(path.size() - 11) == "brushes.txt");
      ed.command("sculpt size 31");
      ed.command("sculpt strength 0.25");
      ed.command("sculpt brush smooth");
      ed.command("sculpt size 92");
      ed.command("wpbrush add 44");
      ed.command("wpbrush blur 66");
      ed.command("paint size 21");
      ed.command("paint color 1 0.5 0.25");
      ed.command("paint brush smudge");
      ed.command("paint size 99");
      ed.command("paint color 0 0 1");
      ed.command("brush unified sculpt strength on");
      ed.command("brush unified paint colour on");
      ed.command("brush curve hardness on 0 0 0.5 0.9 1 1");
      ed.command("brush curve spacing on 0 1 1 0");
      ed.command("brush save");
      CHECK(fs::read_file(path, text) && text.size() > 50);
      std::printf("    brushes.txt: %zu bytes\n", text.size());
    }
    /* Reset the banks (sizes back to a fresh editor's), then Load. */
    {
      Editor ed;
      ed.init_headless(1400, 900);
      ed.prefs_for_test(fs::join(dir, "prefs.txt"));
      CHECK(ed.sculpt_brush_for_test().radius != 31.0f);
      ed.command("brush load");
      const SculptBrush &s = ed.sculpt_brush_for_test();
      CHECK(s.kind == 1 && s.radius == 92.0f);
      ed.command("sculpt brush draw");
      CHECK(s.radius == 31.0f);
      CHECK(ed.sculpt_bank_for_test().unified_strength && !ed.sculpt_bank_for_test().unified_size);
      CHECK(ed.wp_brush_for_test().mode == 3);  // (the last `wpbrush blur 66`)
      CHECK(ed.wp_brush_for_test().radius == 66.0f);
      ed.command("wpbrush add");
      CHECK(ed.wp_brush_for_test().radius == 44.0f);
      ed.command("wpbrush blur");
      CHECK(ed.wp_brush_for_test().radius == 66.0f);
      const PaintBrush &p = ed.paint_brush_for_test();
      CHECK(p.mode == 1 && p.radius == 99.0f && p.color.z == 1.0f);
      CHECK(ed.paint_bank_for_test().unified_color);
      CHECK(p.hardness_pressure && p.spacing_pressure);
      CHECK(p.hardness_curve.size() == 3 && std::fabs(p.hardness_curve[1].x - 0.5f) < 1e-5f && std::fabs(p.hardness_curve[1].y - 0.9f) < 1e-5f);
      CHECK(p.spacing_curve.size() == 2 && p.spacing_curve[0].y == 1.0f && p.spacing_curve[1].y == 0.0f);
      /* The loaded spacing curve (1 at pressure 0, 0 at 1) is in use: the field times 1.5 minus it, so a firm press spaces wider here. */
      CHECK(ed.paint_spacing_for_test(1.0f) > ed.paint_spacing_for_test(0.0f));
    }
    /* The preferences saving writes it beside prefs.txt too. */
    {
      Editor ed;
      ed.init_headless(1400, 900);
      ed.prefs_for_test(fs::join(dir, "prefs.txt"));
      std::filesystem::remove(fs::join(dir, "brushes.txt"));
      ed.command("sculpt size 17");
      ed.save_prefs_for_test();
      std::string t2;
      CHECK(fs::read_file(fs::join(dir, "brushes.txt"), t2) && t2.find("slot sculpt 0 17") != std::string::npos);
    }
    std::filesystem::remove(fs::join(dir, "brushes.txt"));
    std::filesystem::remove(fs::join(dir, "prefs.txt"));
  });

  test("per brush settings (round 540, te): a truncated, garbage, huge, negative or NaN brushes.txt loads what it can with every size and strength inside its Inspector range", [] {
    Editor ed;
    ed.init_headless(1400, 900);
    auto in_range = [&] {
      const SculptBrush &s = ed.sculpt_brush_for_test();
      const WeightBrush &w = ed.wp_brush_for_test();
      const PaintBrush &p = ed.paint_brush_for_test();
      bool ok = std::isfinite(s.radius) && s.radius >= 1.0f && s.radius <= 500.0f && std::isfinite(s.strength) && s.strength >= 0.0f && s.strength <= 1.0f;
      ok = ok && std::isfinite(w.radius) && w.radius >= 1.0f && w.radius <= 1000.0f && std::isfinite(w.strength) && w.strength >= 0.0f && w.strength <= 1.0f;
      ok = ok && std::isfinite(p.radius) && p.radius >= 1.0f && p.radius <= 500.0f && std::isfinite(p.opacity) && p.opacity >= 0.0f && p.opacity <= 1.0f;
      ok = ok && std::isfinite(p.color.x) && std::isfinite(p.color.y) && std::isfinite(p.color.z);
      return ok && s.kind >= 0 && s.kind < 11 && w.mode >= 0 && w.mode < 4 && p.mode >= 0 && p.mode < 3;
    };
    ed.command("sculpt size 40");
    ed.command("sculpt brush smooth");
    ed.command("sculpt size 60");
    ed.command("paint brush smudge");
    ed.command("paint size 50");
    ed.command("brush curve hardness on 0 0.2 1 1");
    const std::string good = ed.brushes_text_for_test();
    CHECK(good.size() > 100);
    /* Every truncation of a real file (stepping by 7 bytes), and the file with a byte flipped now and then. */
    int bad_loads = 0;
    for (size_t n = 0; n <= good.size(); n += 7) {
      ed.brushes_load_text_for_test(good.substr(0, n));
      if (!in_range()) bad_loads++;
    }
    uint32_t seed = 12345u;
    for (int round = 0; round < 60; round++) {
      std::string t = good;
      for (int k = 0; k < 6; k++) {
        seed = seed * 1664525u + 1013904223u;
        t[(seed >> 8) % t.size()] = (char)(seed >> 24);
      }
      ed.brushes_load_text_for_test(t);
      if (!in_range()) bad_loads++;
    }
    CHECK(bad_loads == 0);
    /* Hand-made bad lines: every one a slot of the brush that is live. */
    const char *bad[] = {
        "bank sculpt 1 0 0 0\nslot sculpt 1 nan 0.5\n",
        "bank sculpt 1 0 0 0\nslot sculpt 1 inf -inf\n",
        "bank sculpt 1 0 0 0\nslot sculpt 1 -5 7\n",
        "bank sculpt 1 0 0 0\nslot sculpt 1 1e30 1e30\n",
        "bank sculpt 1 0 0 0\nslot sculpt 1 1e999 0.5\n",
        "bank paint 1 0 0 1\nslot paint 1 20 2 nan nan nan\n",
        "bank paint 1 0 0 1\nslot paint 1 20 -3 1e30 -1e30 0\n",
        "bank weight -4 1 1 1\nslot weight -1 5 5\n",
        "bank sculpt 999999999999 1 1 1\n",
        "bank sculpt\nslot\nslot sculpt\nbank\n   \n\t\r\n",
        "paint_hardness_curve nan nan\npaint_spacing_curve 0 0\npaint_hardness_curve 0 0 1\n",
        "paint_hardness_curve 1 1 0 0\npaint_hardness_curve 0 1 0 1 0 1\n",
    };
    for (const char *t : bad) {
      ed.brushes_load_text_for_test(t);
      const bool ok = in_range();
      if (!ok) std::printf("    out of range after loading: %s\n", t);
      CHECK(ok);
    }
    /* A clamped value, exactly: 1e30 -> 500, -5 -> 1, strength 7 -> 1. */
    ed.brushes_load_text_for_test("bank sculpt 2 0 0 0\nslot sculpt 2 1e30 7\n");
    CHECK(ed.sculpt_brush_for_test().kind == 2 && ed.sculpt_brush_for_test().radius == 500.0f && ed.sculpt_brush_for_test().strength == 1.0f);
    ed.brushes_load_text_for_test("bank sculpt 2 0 0 0\nslot sculpt 2 -5 -1\n");
    CHECK(ed.sculpt_brush_for_test().radius == 1.0f && ed.sculpt_brush_for_test().strength == 0.0f);
    /* The editor goes on working after garbage: a switch and a size still land on the right brush. */
    ed.command("sculpt brush draw");
    ed.command("sculpt size 33");
    ed.command("sculpt brush smooth");
    ed.command("sculpt brush draw");
    CHECK(ed.sculpt_brush_for_test().radius == 33.0f);
  });

  test("per brush settings (round 540, te): a sculpt size typed as NaN or a brush saved while its size is NaN never writes a number the next start cannot read back inside the range", [] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.command("sculpt size nan");
    ed.command("sculpt strength nan");
    const SculptBrush &b = ed.sculpt_brush_for_test();
    std::printf("    after 'sculpt size nan': radius %g strength %g\n", b.radius, b.strength);
    CHECK(std::isfinite(b.radius) && std::isfinite(b.strength));
    const std::string t = ed.brushes_text_for_test();
    CHECK(t.find("nan") == std::string::npos && t.find("inf") == std::string::npos);
    ed.brushes_load_text_for_test(t);
    CHECK(std::isfinite(b.radius) && b.radius >= 1.0f && b.radius <= 500.0f);
  });

  test("per brush settings (round 540, te): a Spray stroke scatters smaller dabs without leaving its temporary size or strength in any brush's slot", [] {
    Editor ed;
    GameObject *g = sphere_scene(ed);
    CHECK(g != nullptr);
    if (!g) return;
    ed.command("sculpt size 30");
    ed.command("sculpt strength 0.2");
    ed.command("sculpt brush smooth");
    ed.command("sculpt size 70");
    ed.command("sculpt strength 0.9");
    ed.command("sculpt brush draw");
    ed.command("sculpt stroke spray");
    const SculptBrush &b = ed.sculpt_brush_for_test();
    const Vec2 px = pixel_of(ed, g->world_position());
    const Recti sr = ed.scene_rect_for_test();
    const int ix = sr.x + (int)px.x, iy = sr.y + (int)px.y;
    const double v0 = volume(*g->mesh_filter()->mesh);
    auto drag = [&] {
      step(ed, {r540_ev(platform::EventType::MouseMove, ix, iy)});
      step(ed, {r540_ev(platform::EventType::MouseDown, ix, iy)});
      for (int i = 1; i <= 6; i++) step(ed, {r540_ev(platform::EventType::MouseMove, ix + 6 * i, iy + 2 * i)});
      step(ed, {r540_ev(platform::EventType::MouseUp, ix + 36, iy + 12)});
      step(ed);
    };
    drag();
    std::printf("    after a Spray drag on Draw: size %g strength %g (volume change %.5f)\n", b.radius, b.strength, volume(*g->mesh_filter()->mesh) - v0);
    CHECK(std::fabs(volume(*g->mesh_filter()->mesh) - v0) > 0.0);  // (the stroke did something)
    CHECK(b.kind == 0 && b.radius == 30.0f && std::fabs(b.strength - 0.2f) < 1e-6f);
    ed.command("sculpt brush smooth");
    CHECK(b.radius == 70.0f && std::fabs(b.strength - 0.9f) < 1e-6f);
    drag();  // (a Spray stroke with Smooth live)
    CHECK(b.kind == 1 && b.radius == 70.0f && std::fabs(b.strength - 0.9f) < 1e-6f);
    ed.command("sculpt brush draw");
    CHECK(b.radius == 30.0f && std::fabs(b.strength - 0.2f) < 1e-6f);
    for (const auto &kv : ed.sculpt_bank_for_test().slots) std::printf("    slot %d: size %g strength %g\n", kv.first, kv.second.size, kv.second.strength);
  });

  test("per brush settings (round 540, te): a brushes.txt naming Weight Paint mode 9 (past the list) is ignored - no slot, no crash, the brush stays one of the four", [] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.command("wpbrush add 40");
    ed.command("wpbrush blur 70");
    ed.brushes_load_text_for_test("bank weight 9 0 0 0\nslot weight 9 5 0.5\nslot weight 4 6 0.6\nslot weight 3 88 0.4\n");
    const WeightBrush &w = ed.wp_brush_for_test();
    std::printf("    mode %d radius %g after a file naming mode 9\n", w.mode, w.radius);
    CHECK(w.mode >= 0 && w.mode < 4);
    CHECK(ed.weight_bank_for_test().slots.count(9) == 0 && ed.weight_bank_for_test().slots.count(4) == 0);
    CHECK(ed.weight_bank_for_test().slots.count(3) == 1);
    ed.command("wpbrush add");
    ed.command("wpbrush blur");
    CHECK(w.mode == 3 && w.radius == 88.0f);
    const std::string t = ed.brushes_text_for_test();
    CHECK(t.find("slot weight 9") == std::string::npos);
  });
}

/* ---------------------------------------------------------------- round 562: a stroke doesn't depend on how the view was drawn (task 0544) */

namespace {

/* The vertices' nearest and farthest distance from the object's origin (object space). */
std::pair<float, float> r562_radii(const Mesh &m) {
  float lo = 1e30f, hi = 0.0f;
  for (const Vec3 &p : m.positions) lo = std::min(lo, length(p)), hi = std::max(hi, length(p));
  return {lo, hi};
}

bool r562_same(const std::vector<Vec3> &a, const std::vector<Vec3> &b) { return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(Vec3)) == 0); }

}  // namespace

static void tests_round562() {
  test("sculpt (round 562): on a small dense sphere the brush lands on the mesh at every pixel across it - not up to a third of the radius off, as the depth plane's rounding put it - and every dab of a drag across it gathers vertices", [] {
    Editor ed;
    GameObject *g = sphere_scene(ed, 2);
    CHECK(g);
    if (!g) return;
    const Mesh &m = *g->mesh_filter()->mesh;
    const Vec3 centre = g->world_position();
    const Vec2 px = pixel_of(ed, centre);
    const float R = length(pixel_of(ed, centre + Vec3(0.0f, 0.5f, 0.0f)) - px);
    const std::pair<float, float> radii = r562_radii(m);
    int hits = 0, off = 0;
    float worst = 0.0f;
    for (int dx = -(int)(R * 0.85f); dx <= (int)(R * 0.85f); dx++)
      for (int dy = -2; dy <= 2; dy += 2) {
        Vec3 w;
        float d;
        if (!ed.sculpt_hit_for_test((float)((int)px.x + dx), (float)((int)px.y + dy), w, d)) continue;
        hits++;
        const float r = length(w - centre);
        worst = std::max(worst, std::max(radii.first - r, r - radii.second));
        off += r < radii.first * 0.98f || r > radii.second * 1.001f ? 1 : 0;  // (a facet's middle is a little inside its corners)
      }
    std::printf("    the sphere is %.0f px across, %zu faces, its vertices %.4f to %.4f from the centre; %d pixels hit, %d off the surface (the farthest %.5f outside that band)\n", 2.0f * R, m.face_count(),
                radii.first, radii.second, hits, off, worst);
    CHECK(hits > (int)R && off == 0);
    /* A drag across it with real events, a small brush: every dab finds vertices. */
    ed.command("sculpt brush draw");
    ed.command(strprintf("sculpt size %g", std::max(5.0f, R * 0.25f)));
    ed.command("sculpt strength 0.5");
    ed.command("sculpt stroke freehand");
    const Recti sr = ed.scene_rect_for_test();
    const int wx = sr.x + (int)px.x, wy = sr.y + (int)px.y, half = (int)(R * 0.6f);
    int dabs = 0, empty = 0;
    step(ed, {mouse_event(platform::EventType::MouseMove, wx - half, wy)});
    step(ed, {mouse_event(platform::EventType::MouseDown, wx - half, wy)});
    Vec3 last = ed.sculpt_last_hit_for_test();
    for (int x = wx - half + 2; x <= wx + half; x += 2) {
      const size_t before = ed.sculpt_dabs_for_test();
      step(ed, {mouse_event(platform::EventType::MouseMove, x, wy)});
      if (ed.sculpt_dabs_for_test() == before) continue;
      /* (A dab that found no surface leaves the last dab's point and count in place: the point must have moved.) */
      const Vec3 now = ed.sculpt_last_hit_for_test();
      dabs++, empty += ed.sculpt_last_gathered_for_test() == 0 || now == last ? 1 : 0;
      last = now;
    }
    step(ed, {mouse_event(platform::EventType::MouseUp, wx + half, wy)});
    step(ed);
    std::printf("    a drag of %d px across it: %d dabs, %d of them over no vertex\n", 2 * half, dabs, empty);
    CHECK(dabs >= half / 2 && empty == 0);
    ed.command("sculpt stroke dots");
  });

  test("sculpt (round 562): a dab, a stroke and a Dyntopo dab come out the same to the bit when the Scene view's depth plane is rounded differently (each depth up to 1e-4 off: what a wider span margin in the rasterizer did)", [] {
    Editor ed;
    GameObject *g = sphere_scene(ed, 2);
    CHECK(g);
    if (!g) return;
    const uint64_t gid = g->id;
    auto M = [&] { return ed.scene().find(gid)->mesh_filter()->mesh; };
    const Vec2 px = pixel_of(ed, g->world_position());
    const float R = length(pixel_of(ed, g->world_position() + Vec3(0.0f, 0.5f, 0.0f)) - px);
    ed.command(strprintf("sculpt size %g", std::max(5.0f, R * 0.3f)));
    ed.command("sculpt strength 1");
    const std::vector<Vec3> rest = M()->positions;
    auto work = [&] {
      ed.command(strprintf("sculpt dab %g %g", px.x - R * 0.3f, px.y));
      ed.command(strprintf("sculpt stroke %g %g %g %g 8", px.x - R * 0.4f, px.y + 2.0f, px.x + R * 0.4f, px.y - 3.0f));
    };
    work();
    const std::vector<Vec3> plain = M()->positions;
    const Vec3 plain_hit = ed.sculpt_last_hit_for_test();
    ed.command("undo");
    ed.command("undo");
    CHECK(r562_same(M()->positions, rest));
    CHECK(!r562_same(plain, rest));
    size_t differ = 0;
    for (uint32_t seed = 1; seed <= 3; seed++) {
      ed.noise_scene_depth_for_test(1e-4f, seed);  // (no frame from here to the strokes: they read the plane as it now is)
      work();
      differ += r562_same(M()->positions, plain) && ed.sculpt_last_hit_for_test() == plain_hit ? 0 : 1;
      ed.command("undo");
      ed.command("undo");
    }
    std::printf("    a dab and an 8-dab stroke on the 44 px sphere, the depth plane noised three ways: %zu of 3 differ from the plain run\n", differ);
    CHECK(differ == 0);
    /* Dyntopo: the tessellation follows the brush's point, so its counts would differ too. */
    for (int i = 0; i < 3; i++) step(ed);
    ed.command("sculpt dyntopo 0.5");
    ed.command(strprintf("sculpt dab %g %g", px.x, px.y));
    const std::vector<Vec3> dyn = M()->positions;
    const size_t dyn_faces = M()->face_count();
    ed.command("undo");
    ed.noise_scene_depth_for_test(1e-4f, 9);
    ed.command(strprintf("sculpt dab %g %g", px.x, px.y));
    std::printf("    a Dyntopo dab: %zu faces plain, %zu with the plane noised; the positions %s\n", dyn_faces, M()->face_count(), r562_same(M()->positions, dyn) ? "the same" : "differ");
    CHECK(M()->face_count() == dyn_faces && r562_same(M()->positions, dyn));
    ed.command("sculpt dyntopo off");
  });

  test("sculpt (round 562): the ray through the face grid finds what testing every face finds, to the bit - on and off the mesh, after a stroke and a Grab moved the faces (filed again, no rebuild), after an undo, with a face hidden, and on a cube whose faces are larger than the cells", [] {
    Editor ed;
    GameObject *g = sphere_scene(ed, 3);  // (24k faces: over the grid's threshold)
    CHECK(g);
    if (!g) return;
    const Vec2 px = pixel_of(ed, g->world_position());
    const float R = length(pixel_of(ed, g->world_position() + Vec3(0.0f, 0.5f, 0.0f)) - px);
    auto sweep = [&](const char *what) {
      int hit = 0, miss = 0, differ = 0;
      for (float y = px.y - R * 1.3f; y <= px.y + R * 1.3f; y += 2.5f)
        for (float x = px.x - R * 1.3f; x <= px.x + R * 1.3f; x += 2.5f) {
          const float a = ed.sculpt_ray_for_test(x, y, false), b = ed.sculpt_ray_for_test(x, y, true);
          differ += std::memcmp(&a, &b, sizeof(float)) != 0 ? 1 : 0;
          (b > 0.0f ? hit : miss)++;
        }
      std::printf("    %s: %d rays hit, %d miss, %d differ between the grid and every face (grid builds so far %zu, %zu entries)\n", what, hit, miss, differ, ed.sculpt_ray_builds_for_test(),
                  ed.sculpt_ray_entries_for_test());
      CHECK(hit > 50 && miss > 50 && differ == 0);
    };
    CHECK(g->mesh_filter()->mesh->face_count() >= 4096);
    sweep("the sphere at rest");
    const size_t builds0 = ed.sculpt_ray_builds_for_test();
    CHECK(builds0 == 1);
    ed.command(strprintf("sculpt size %g", R * 0.4f));
    ed.command("sculpt strength 1");
    ed.command(strprintf("sculpt stroke %g %g %g %g 10", px.x - R * 0.5f, px.y, px.x + R * 0.5f, px.y));
    sweep("after a 10-dab Draw stroke");
    ed.command("sculpt brush grab");
    const Recti sr = ed.scene_rect_for_test();
    const int wx = sr.x + (int)px.x, wy = sr.y + (int)px.y;
    step(ed, {mouse_event(platform::EventType::MouseMove, wx, wy)});
    step(ed, {mouse_event(platform::EventType::MouseDown, wx, wy)});
    for (int i = 1; i <= 6; i++) step(ed, {mouse_event(platform::EventType::MouseMove, wx + (int)(R * 0.1f * i), wy - i)});
    step(ed, {mouse_event(platform::EventType::MouseUp, wx + (int)(R * 0.6f), wy - 6)});
    step(ed);
    sweep("after a Grab drag");
    std::printf("    the strokes filed their faces again: %zu grid build(s) since the first\n", ed.sculpt_ray_builds_for_test() - builds0);
    CHECK(ed.sculpt_ray_builds_for_test() == builds0);  // (the moved faces were filed again; nothing was rebuilt)
    ed.command("undo");
    sweep("after an undo");
    ed.command("sculpt brush draw");
    /* A hidden face lets the ray through, both ways. */
    {
      MeshPtr &mp = ed.scene().find(g->id)->mesh_filter()->mesh;
      mesh_make_mutable(mp);
      const float before = ed.sculpt_ray_for_test(px.x, px.y, true);
      Vec3 w;
      float d;
      CHECK(ed.sculpt_hit_for_test(px.x, px.y, w, d));
      const Vec3 local = ed.scene().find(g->id)->world_matrix().inverse().point(w);
      size_t hidden = 0;
      for (size_t f = 0; f < mp->face_count(); f++) {
        Vec3 c(0.0f);
        for (uint32_t k = 0; k < mp->face_size(f); k++) c += mp->positions[mp->face_verts(f)[k]];
        if (length(c / (float)mp->face_size(f) - local) < 0.08f) mp->set_face_hidden(f, true), hidden++;
      }
      mp->touch();
      const float through = ed.sculpt_ray_for_test(px.x, px.y, true);
      std::printf("    %zu faces hidden under the pixel: the ray's first face at %.4f, was %.4f\n", hidden, through, before);
      CHECK(hidden > 0 && through == -1.0f);  // (the far side through the hole looks away: culled, as the view draws it - a miss)
      sweep("with faces hidden");
    }
    /* The default cube: twelve triangles far larger than any cell - the list every ray tests. */
    Editor ed2;
    ed2.init_headless(1400, 900);
    ed2.set_clock_for_test(100.0);
    ed2.command("select Cube");
    step(ed2);
    for (int i = 0; i < 400 && ed2.scene_camera_for_test().animating; i++) step(ed2);
    ed2.command("sculpt on");
    for (int i = 0; i < 20; i++) step(ed2);
    GameObject *cube = ed2.active_object_for_test();
    CHECK(cube);
    if (!cube) return;
    ed2.sculpt_ray_grid_from_for_test(0);
    const Vec2 cp = pixel_of(ed2, cube->world_position());
    int hit = 0, differ = 0;
    for (float y = cp.y - 200.0f; y <= cp.y + 200.0f; y += 7.0f)
      for (float x = cp.x - 200.0f; x <= cp.x + 200.0f; x += 7.0f) {
        const float a = ed2.sculpt_ray_for_test(x, y, false), b = ed2.sculpt_ray_for_test(x, y, true);
        differ += std::memcmp(&a, &b, sizeof(float)) != 0 ? 1 : 0;
        hit += b > 0.0f ? 1 : 0;
      }
    std::printf("    the cube through the grid: %d rays hit, %d differ\n", hit, differ);
    CHECK(hit > 20 && differ == 0);
  });

  test("sculpt (round 562, review): one vertex pulled a thousand sizes away leaves the grid a grid (its box is where the faces are, the stray ones tested by every ray); a long Grab files nothing until the next ray; with an Array modifier the planes' depth places the brush as before", [] {
    Editor ed;
    GameObject *g = sphere_scene(ed, 3);
    CHECK(g);
    if (!g) return;
    const uint64_t gid = g->id;
    const Vec2 px = pixel_of(ed, g->world_position());
    const float R = length(pixel_of(ed, g->world_position() + Vec3(0.0f, 0.5f, 0.0f)) - px);
    auto sweep = [&](const char *what) {
      int hit = 0, miss = 0, differ = 0;
      for (float y = px.y - R * 1.3f; y <= px.y + R * 1.3f; y += 3.5f)
        for (float x = px.x - R * 1.3f; x <= px.x + R * 1.3f; x += 3.5f) {
          const float a = ed.sculpt_ray_for_test(x, y, false), b = ed.sculpt_ray_for_test(x, y, true);
          differ += std::memcmp(&a, &b, sizeof(float)) != 0 ? 1 : 0;
          (b > 0.0f ? hit : miss)++;
        }
      std::printf("    %s: %d rays hit, %d miss, %d differ (grid builds %zu, %zu entries)\n", what, hit, miss, differ, ed.sculpt_ray_builds_for_test(), ed.sculpt_ray_entries_for_test());
      CHECK(hit > 20 && miss > 20 && differ == 0);
    };
    sweep("the sphere");
    const size_t plain = ed.sculpt_ray_entries_for_test();
    {
      /* The vertex farthest from the point under the pointer (on the far side), a thousand sphere sizes off. */
      MeshPtr &mp = ed.scene().find(gid)->mesh_filter()->mesh;
      mesh_make_mutable(mp);
      Vec3 w;
      float d;
      CHECK(ed.sculpt_hit_for_test(px.x, px.y, w, d));
      const Vec3 local = ed.scene().find(gid)->world_matrix().inverse().point(w);
      uint32_t far_v = 0;
      for (uint32_t v = 0; v < (uint32_t)mp->vert_count(); v++)
        if (length_sq(mp->positions[v] - local) > length_sq(mp->positions[far_v] - local)) far_v = v;
      mp->positions[far_v] = mp->positions[far_v] * 2000.0f;
      mp->touch();
    }
    sweep("with one vertex 1,000 sizes away");
    const size_t stray = ed.sculpt_ray_entries_for_test();
    std::printf("    the grid's entries: %zu on the plain sphere, %zu with the stray vertex\n", plain, stray);
    CHECK(stray > plain / 2 && stray < plain * 2);  // (the cells are still the sphere's: not one cell holding every face)
    /* A long Grab: thirty moves of the pointer, no ray between them - the faces are filed once, at the next ray. */
    ed.command("sculpt brush grab");
    ed.command(strprintf("sculpt size %g", R * 0.5f));
    const Recti sr = ed.scene_rect_for_test();
    const int wx = sr.x + (int)px.x, wy = sr.y + (int)px.y;
    step(ed, {mouse_event(platform::EventType::MouseMove, wx, wy)});
    step(ed, {mouse_event(platform::EventType::MouseDown, wx, wy)});
    const size_t held = ed.sculpt_ray_entries_for_test();
    for (int i = 1; i <= 30; i++) step(ed, {mouse_event(platform::EventType::MouseMove, wx + 8 * i, wy)});
    const size_t dragged = ed.sculpt_ray_entries_for_test();
    step(ed, {mouse_event(platform::EventType::MouseUp, wx + 240, wy)});
    step(ed);
    std::printf("    a 240 px Grab: %zu entries at the press, %zu after thirty moves\n", held, dragged);
    CHECK(dragged == held);
    sweep("after the Grab");
    ed.command("sculpt brush draw");
    ed.command("undo");
    /* An Array's copy can stand in front of the mesh: the ray against the mesh is not the brush's point there. */
    for (int i = 0; i < 3; i++) step(ed);
    GameObject *obj = ed.scene().find(gid);
    Vec3 a0, a1, b0, b1;
    float d;
    const bool plain0 = ed.sculpt_hit_for_test(px.x, px.y, a0, d);
    ed.noise_scene_depth_for_test(1e-3f, 5);
    const bool plain1 = ed.sculpt_hit_for_test(px.x, px.y, a1, d);
    ArrayModifier *arr = obj->add<ArrayModifier>();
    arr->count = 2;
    ed.mark_changed_for_test("Array");
    for (int i = 0; i < 3; i++) step(ed);
    const bool array0 = ed.sculpt_hit_for_test(px.x, px.y, b0, d);
    ed.noise_scene_depth_for_test(1e-3f, 5);
    const bool array1 = ed.sculpt_hit_for_test(px.x, px.y, b1, d);
    std::printf("    the point under the pointer with the depth plane noised: moved %.5f without a modifier, %.5f with an Array\n", length(a1 - a0), length(b1 - b0));
    CHECK(plain0 && plain1 && a0 == a1);
    CHECK(array0 && array1 && !(b0 == b1));
  });
}

/* ---------------------------------------------------------------- round 562, test engineer: degenerate input, strokes keeping the grid right, depth-noise independence */

namespace {

struct R562Sweep {
  int hit = 0, miss = 0, differ = 0, bad = 0;
};

/* The grid's ray against the every-face ray over a square of pixels round `c` (span in radii R, step in pixels): how many hit, miss,
 * differ in a single bit, and are neither -1 nor a finite distance. */
R562Sweep r562_sweep(Editor &ed, Vec2 c, float R, float span, float step_px) {
  R562Sweep s;
  for (float y = c.y - R * span; y <= c.y + R * span; y += step_px)
    for (float x = c.x - R * span; x <= c.x + R * span; x += step_px) {
      const float a = ed.sculpt_ray_for_test(x, y, false), b = ed.sculpt_ray_for_test(x, y, true);
      s.differ += std::memcmp(&a, &b, sizeof(float)) != 0 ? 1 : 0;
      s.bad += (!std::isfinite(a) || !std::isfinite(b) || (a <= 0.0f && a != -1.0f) || (b <= 0.0f && b != -1.0f)) ? 1 : 0;
      (b > 0.0f ? s.hit : s.miss)++;
    }
  return s;
}

/* A sphere's pixel radius at its centre. */
float r562_pixel_radius(Editor &ed, GameObject *g) {
  const Vec2 px = pixel_of(ed, g->world_position());
  return length(pixel_of(ed, g->world_position() + Vec3(0.0f, 0.5f, 0.0f)) - px);
}

using R562State = std::pair<std::vector<Vec3>, std::vector<float>>;

/* One dab and one 8-dab stroke with `brush` on the 44 px sphere, then the same with the depth plane noised three ways and no frame between:
 * how many of the three differ bitwise (positions and mask) from the plain run; -1 when the scene did not come up. */
int r562_noise_case(const char *brush) {
  Editor ed;
  GameObject *g = sphere_scene(ed, 2);
  CHECK(g);
  if (!g) return -1;
  const uint64_t gid = g->id;
  auto M = [&] { return ed.scene().find(gid)->mesh_filter()->mesh; };
  auto take = [&] { return R562State(M()->positions, M()->sculpt_mask); };
  auto same = [&](const R562State &a, const R562State &b) { return r562_same(a.first, b.first) && a.second == b.second; };
  const Vec2 px = pixel_of(ed, g->world_position());
  const float R = r562_pixel_radius(ed, g);
  ed.command(strprintf("sculpt brush %s", brush));
  ed.command(strprintf("sculpt size %g", std::max(5.0f, R * 0.3f)));
  ed.command("sculpt strength 1");
  const R562State rest = take();
  auto work = [&] {
    ed.command(strprintf("sculpt dab %g %g", px.x - R * 0.3f, px.y));
    ed.command(strprintf("sculpt stroke %g %g %g %g 8", px.x - R * 0.4f, px.y + 2.0f, px.x + R * 0.4f, px.y - 3.0f));
  };
  work();
  const R562State plain = take();
  CHECK(!same(plain, rest));  // (the stroke did something, or the comparison below means nothing)
  ed.command("undo");
  ed.command("undo");
  int differ = 0;
  for (uint32_t seed = 1; seed <= 3; seed++) {
    ed.noise_scene_depth_for_test(1e-4f, seed);
    work();
    differ += same(take(), plain) ? 0 : 1;
    ed.command("undo");
    ed.command("undo");
  }
  std::printf("    %s: a dab and an 8-dab stroke, the depth plane noised three ways: %d of 3 differ from the plain run\n", brush, differ);
  return differ;
}

}  // namespace

static void tests_round562_te() {
  test("sculpt (round 562, te): a vertex pulled to NaN leaves the ray the same through the grid and through every face, never a NaN distance, and a dab still runs without losing a vertex or a face", [] {
    Editor ed;
    GameObject *g = sphere_scene(ed, 2);
    CHECK(g);
    if (!g) return;
    const uint64_t gid = g->id;
    const Vec2 px = pixel_of(ed, g->world_position());
    const float R = r562_pixel_radius(ed, g);
    Vec3 w;
    float d;
    CHECK(ed.sculpt_hit_for_test(px.x, px.y, w, d));
    const Vec3 local = ed.scene().find(gid)->world_matrix().inverse().point(w);
    size_t vc = 0, fc = 0;
    {
      MeshPtr &mp = ed.scene().find(gid)->mesh_filter()->mesh;
      mesh_make_mutable(mp);
      uint32_t best = 0;
      float bd = 1e30f;
      for (uint32_t v = 0; v < (uint32_t)mp->vert_count(); v++) {
        const float l = length(mp->positions[v] - local);
        if (l < bd) bd = l, best = v;
      }
      const float nan = std::numeric_limits<float>::quiet_NaN();
      mp->positions[best] = Vec3(nan, nan, nan);
      mp->touch();
      vc = mp->vert_count(), fc = mp->face_count();
    }
    ed.sculpt_ray_grid_from_for_test(0);  // (the grid on, whatever the face count)
    const R562Sweep s = r562_sweep(ed, px, R, 1.2f, R * 0.4f);
    std::printf("    NaN vertex, grid forced: %d rays hit, %d miss, %d differ between the grid and every face, %d neither finite nor -1\n", s.hit, s.miss, s.differ, s.bad);
    CHECK(s.differ == 0 && s.bad == 0 && s.hit > 0);
    ed.command("sculpt size 20");
    ed.command("sculpt strength 1");
    ed.command(strprintf("sculpt dab %g %g", px.x, px.y));  // (the face under the pixel is skipped: the ray goes through the hole or the brush finds nothing - it must not crash)
    const MeshPtr &after = ed.scene().find(gid)->mesh_filter()->mesh;
    CHECK(after->vert_count() == vc && after->face_count() == fc);
    const R562Sweep s2 = r562_sweep(ed, px, R, 1.2f, R * 0.4f);
    std::printf("    after a dab: %d differ, %d bad\n", s2.differ, s2.bad);
    CHECK(s2.differ == 0 && s2.bad == 0);
  });

  test("sculpt (round 562, te): a vertex at infinity leaves the grid's ray the same as every face's, no hang while filing its faces, no distance but -1 or a finite one", [] {
    Editor ed;
    GameObject *g = sphere_scene(ed, 2);
    CHECK(g);
    if (!g) return;
    const uint64_t gid = g->id;
    const Vec2 px = pixel_of(ed, g->world_position());
    const float R = r562_pixel_radius(ed, g);
    {
      MeshPtr &mp = ed.scene().find(gid)->mesh_filter()->mesh;
      mesh_make_mutable(mp);
      mp->positions[mp->vert_count() / 3].x = std::numeric_limits<float>::infinity();
      mp->positions[mp->vert_count() / 2].y = -std::numeric_limits<float>::infinity();
      mp->touch();
    }
    ed.sculpt_ray_grid_from_for_test(0);
    const R562Sweep s = r562_sweep(ed, px, R, 1.2f, R * 0.4f);
    std::printf("    infinite vertices, grid forced: %d hit, %d miss, %d differ, %d bad\n", s.hit, s.miss, s.differ, s.bad);
    CHECK(s.differ == 0 && s.bad == 0);
  });

  test("sculpt (round 562, te): faces collapsed to a point under the brush leave the grid's ray the same as every face's, and a dab beside them still moves the surface", [] {
    Editor ed;
    GameObject *g = sphere_scene(ed, 2);
    CHECK(g);
    if (!g) return;
    const uint64_t gid = g->id;
    const Vec2 px = pixel_of(ed, g->world_position());
    const float R = r562_pixel_radius(ed, g);
    Vec3 w;
    float d;
    CHECK(ed.sculpt_hit_for_test(px.x, px.y, w, d));
    const Vec3 local = ed.scene().find(gid)->world_matrix().inverse().point(w);
    {
      MeshPtr &mp = ed.scene().find(gid)->mesh_filter()->mesh;
      mesh_make_mutable(mp);
      for (int pass = 0; pass < 3; pass++) {  // (the nearest three faces, one after the other: a patch of zero-area faces and slivers round it)
        size_t bf = 0;
        float bd = 1e30f;
        for (size_t f = 0; f < mp->face_count(); f++) {
          const Vec3 c = mp->positions[mp->face_verts(f)[0]];
          const float l = length(c - local);
          bool flat = true;
          for (uint32_t k = 1; k < mp->face_size(f); k++) flat = flat && mp->positions[mp->face_verts(f)[k]] == c;
          if (!flat && l < bd) bd = l, bf = f;
        }
        const uint32_t n = mp->face_size(bf);
        const Vec3 c = mp->positions[mp->face_verts(bf)[0]];
        for (uint32_t k = 1; k < n; k++) mp->positions[mp->face_verts(bf)[k]] = c;
      }
      mp->touch();
    }
    ed.sculpt_ray_grid_from_for_test(0);
    const R562Sweep s = r562_sweep(ed, px, R, 1.3f, 2.5f);
    std::printf("    collapsed faces, grid forced: %d hit, %d miss, %d differ, %d bad\n", s.hit, s.miss, s.differ, s.bad);
    CHECK(s.hit > 50 && s.differ == 0 && s.bad == 0);
    ed.command(strprintf("sculpt size %g", std::max(5.0f, R * 0.3f)));
    ed.command("sculpt strength 1");
    const std::vector<Vec3> before = ed.scene().find(gid)->mesh_filter()->mesh->positions;
    ed.command(strprintf("sculpt dab %g %g", px.x + R * 0.6f, px.y));
    const size_t moved = changed_count(before, ed.scene().find(gid)->mesh_filter()->mesh->positions);
    std::printf("    a dab beside the collapsed faces moved %zu vertices\n", moved);
    CHECK(moved > 0);
    const R562Sweep s2 = r562_sweep(ed, px, R, 1.3f, 2.5f);
    CHECK(s2.differ == 0 && s2.bad == 0);
  });

  test("sculpt (round 562, te): an object scaled by 1e4 (and one by 1e-4) with its mesh scaled the other way looks the same, and the brush lands on the mesh at every pixel across it, the grid's ray equal to every face's", [] {
    for (const float k : {1e4f, 1e-4f}) {
      Editor ed;
      GameObject *g = sphere_scene(ed, 2);
      CHECK(g);
      if (!g) return;
      const uint64_t gid = g->id;
      {
        MeshPtr &mp = ed.scene().find(gid)->mesh_filter()->mesh;
        mesh_make_mutable(mp);
        for (Vec3 &p : mp->positions) p = p * k;
        mp->touch();
      }
      ed.scene().find(gid)->set_local_scale(Vec3(1.0f / k));
      for (int i = 0; i < 20; i++) step(ed);  // (the render mesh and the id plane follow)
      ed.sculpt_ray_grid_from_for_test(0);
      const Vec2 px = pixel_of(ed, ed.scene().find(gid)->world_position());
      const float R = r562_pixel_radius(ed, ed.scene().find(gid));
      const std::pair<float, float> radii = r562_radii(*ed.scene().find(gid)->mesh_filter()->mesh);
      const Mat4 inv = ed.scene().find(gid)->world_matrix().inverse();
      int hits = 0, off = 0;
      for (int i = -4; i <= 4; i++) {
        Vec3 w;
        float d;
        if (!ed.sculpt_hit_for_test(px.x + R * 0.2f * (float)i, px.y, w, d)) continue;
        hits++;
        const float r = length(inv.point(w));
        off += r < radii.first * 0.98f || r > radii.second * 1.001f ? 1 : 0;
      }
      const R562Sweep s = r562_sweep(ed, px, R, 1.3f, 2.5f);
      std::printf("    mesh x %g: %d of 9 pixels hit, %d off the surface; grid vs every face: %d hit, %d miss, %d differ, %d bad\n", k, hits, off, s.hit, s.miss, s.differ, s.bad);
      CHECK(hits >= 5 && off == 0);
      CHECK(s.hit > 50 && s.differ == 0 && s.bad == 0);
      ed.command(strprintf("sculpt size %g", std::max(5.0f, R * 0.3f)));
      ed.command("sculpt strength 1");
      const std::vector<Vec3> before = ed.scene().find(gid)->mesh_filter()->mesh->positions;
      ed.command(strprintf("sculpt dab %g %g", px.x, px.y));
      const size_t moved = changed_count(before, ed.scene().find(gid)->mesh_filter()->mesh->positions);
      std::printf("    a dab at the centre moved %zu vertices\n", moved);
      CHECK(moved > 0);
    }
  });

  test("sculpt (round 562, te): a fin and two copies of a face on one edge (a non-manifold edge) leave the grid's ray the same as every face's, and a dab keeps the counts", [] {
    Editor ed;
    GameObject *g = sphere_scene(ed, 2);
    CHECK(g);
    if (!g) return;
    const uint64_t gid = g->id;
    const Vec2 px = pixel_of(ed, g->world_position());
    const float R = r562_pixel_radius(ed, g);
    Vec3 w;
    float d;
    CHECK(ed.sculpt_hit_for_test(px.x, px.y, w, d));
    const Vec3 local = ed.scene().find(gid)->world_matrix().inverse().point(w);
    size_t vc = 0, fc = 0;
    {
      MeshPtr &mp = ed.scene().find(gid)->mesh_filter()->mesh;
      mesh_make_mutable(mp);
      size_t bf = 0;
      float bd = 1e30f;
      for (size_t f = 0; f < mp->face_count(); f++) {
        const float l = length(mp->face_center(f) - local);
        if (l < bd) bd = l, bf = f;
      }
      const std::vector<uint32_t> fv(mp->face_verts(bf), mp->face_verts(bf) + mp->face_size(bf));
      const uint32_t tip = mp->add_vert((mp->positions[fv[0]] + mp->positions[fv[1]]) * 0.5f * 1.4f);
      const uint32_t fin[3] = {fv[0], fv[1], tip};
      mp->add_face(fin, 3);  // (the edge fv[0]-fv[1] now has three faces)
      mp->add_face(fv.data(), fv.size());  // (the same face twice)
      std::vector<uint32_t> flipped(fv.rbegin(), fv.rend());
      mp->add_face(flipped.data(), flipped.size());  // (and its back)
      mp->sync_attributes();
      mp->touch();
      vc = mp->vert_count(), fc = mp->face_count();
    }
    ed.sculpt_ray_grid_from_for_test(0);
    const R562Sweep s = r562_sweep(ed, px, R, 1.3f, 2.5f);
    std::printf("    non-manifold edge and doubled faces, grid forced: %d hit, %d miss, %d differ, %d bad\n", s.hit, s.miss, s.differ, s.bad);
    CHECK(s.hit > 50 && s.differ == 0 && s.bad == 0);
    ed.command(strprintf("sculpt size %g", std::max(5.0f, R * 0.3f)));
    ed.command("sculpt strength 1");
    ed.command(strprintf("sculpt dab %g %g", px.x, px.y));
    const MeshPtr &after = ed.scene().find(gid)->mesh_filter()->mesh;
    CHECK(after->vert_count() == vc && after->face_count() == fc);
    const R562Sweep s2 = r562_sweep(ed, px, R, 1.3f, 2.5f);
    CHECK(s2.differ == 0 && s2.bad == 0);
  });

  test("sculpt (round 562, te): on an open plane a ray past its edge misses (-1, through the grid and through every face) and the brush finds nothing there, while rays across it hit", [] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.set_clock_for_test(100.0);
    ed.command("select Cube");
    ed.command("delete");
    ed.command("create Plane");
    ed.command("subdivide 3");
    step(ed);
    for (int i = 0; i < 400 && ed.scene_camera_for_test().animating; i++) step(ed);
    ed.command("sculpt on");
    GameObject *g = ed.active_object_for_test();
    CHECK(g);
    if (!g) return;
    const uint64_t gid = g->id;
    Vec3 w;
    float d;
    Vec2 c;
    for (int i = 0; i < 100; i++) {
      step(ed);
      c = pixel_of(ed, g->world_position());
      if (ed.sculpt_hit_for_test(c.x, c.y, w, d)) break;
    }
    CHECK(ed.sculpt_hit_for_test(c.x, c.y, w, d));
    ed.sculpt_ray_grid_from_for_test(0);
    /* The vertex farthest from the centre on screen: the plane's rim. */
    const Mat4 W = g->world_matrix();
    const MeshPtr &mp = g->mesh_filter()->mesh;
    Vec2 rim = c;
    float rim_px = 0.0f;  // (not "far": a macro in the Windows headers)
    for (const Vec3 &p : mp->positions) {
      const Vec2 q = pixel_of(ed, W.point(p));
      const float l = length(q - c);
      if (l > rim_px) rim_px = l, rim = q;
    }
    CHECK(rim_px > 20.0f);
    const Vec2 v = rim - c;
    int inside_hits = 0, outside_miss = 0, outside_total = 0, differ = 0, brush_hits = 0;
    for (float f = 0.0f; f <= 1.8f; f += 0.1f) {
      const float x = c.x + v.x * f, y = c.y + v.y * f;
      const float a = ed.sculpt_ray_for_test(x, y, false), b = ed.sculpt_ray_for_test(x, y, true);
      differ += std::memcmp(&a, &b, sizeof(float)) != 0 ? 1 : 0;
      if (f < 0.95f) {
        inside_hits += b > 0.0f ? 1 : 0;
      } else if (f > 1.05f) {
        outside_total++, outside_miss += b == -1.0f ? 1 : 0;
        brush_hits += ed.sculpt_hit_for_test(x, y, w, d) ? 1 : 0;
      }
    }
    std::printf("    rim %.0f px from the centre: %d rays hit across the plane, %d of %d past the rim miss, %d differ, the brush finds the plane past the rim at %d\n", rim_px,inside_hits, outside_miss, outside_total,
                differ, brush_hits);
    CHECK(inside_hits >= 8 && outside_total > 0 && outside_miss == outside_total && differ == 0 && brush_hits == 0);
    CHECK(ed.sculpt_ray_for_test(c.x, c.y, false) > 0.0f);
  });

  test("sculpt (round 562, te): Draw, Clay, Inflate and Smooth strokes with an undo and a redo between keep the grid's ray equal to every face's, and none of them builds the grid again", [] {
    Editor ed;
    GameObject *g = sphere_scene(ed, 3);
    CHECK(g);
    if (!g) return;
    const Vec2 px = pixel_of(ed, g->world_position());
    const float R = r562_pixel_radius(ed, g);
    CHECK(g->mesh_filter()->mesh->face_count() >= 4096);
    auto sweep = [&](const char *what) {
      const R562Sweep s = r562_sweep(ed, px, R, 0.9f, R * 0.35f);
      std::printf("    %s: %d hit, %d miss, %d differ, %d bad (grid builds %zu, entries %zu)\n", what, s.hit, s.miss, s.differ, s.bad, ed.sculpt_ray_builds_for_test(), ed.sculpt_ray_entries_for_test());
      CHECK(s.hit > 10 && s.differ == 0 && s.bad == 0);
    };
    sweep("at rest");
    const size_t builds0 = ed.sculpt_ray_builds_for_test();
    CHECK(builds0 == 1);
    ed.command(strprintf("sculpt size %g", R * 0.4f));
    ed.command("sculpt strength 1");
    auto stroke = [&](const char *brush, float dy) {
      ed.command(strprintf("sculpt brush %s", brush));
      ed.command(strprintf("sculpt stroke %g %g %g %g 8", px.x - R * 0.5f, px.y + dy, px.x + R * 0.5f, px.y - dy));
    };
    const std::vector<Vec3> rest = g->mesh_filter()->mesh->positions;
    stroke("draw", 3.0f);
    sweep("after Draw");
    stroke("clay", -4.0f);
    sweep("after Clay");
    ed.command("undo");
    sweep("after undoing Clay");
    ed.command("redo");
    sweep("after redoing Clay");
    stroke("inflate", 6.0f);
    sweep("after Inflate");
    stroke("smooth", 0.0f);
    sweep("after Smooth");
    ed.command("undo");
    ed.command("undo");
    sweep("after undoing Smooth and Inflate");
    ed.command("redo");
    sweep("after redoing Inflate");
    CHECK(changed_count(rest, ed.scene().find(g->id)->mesh_filter()->mesh->positions) > 0);  // (the strokes moved the surface)
    std::printf("    grid builds since the first: %zu\n", ed.sculpt_ray_builds_for_test() - builds0);
    CHECK(ed.sculpt_ray_builds_for_test() == builds0);
  });

  test("sculpt (round 562, te): many strokes and Grab drags on one place grow the grid's entries by a bounded amount (past three times the built size it is built again), and its ray stays equal to every face's", [] {
    Editor ed;
    GameObject *g = sphere_scene(ed, 3);
    CHECK(g);
    if (!g) return;
    const Vec2 px = pixel_of(ed, g->world_position());
    const float R = r562_pixel_radius(ed, g);
    const R562Sweep s0 = r562_sweep(ed, px, R, 0.9f, R * 0.35f);
    CHECK(s0.differ == 0);
    const size_t built = ed.sculpt_ray_entries_for_test();
    const size_t builds0 = ed.sculpt_ray_builds_for_test();
    CHECK(built > 0);
    ed.command(strprintf("sculpt size %g", R * 0.6f));
    ed.command("sculpt strength 1");
    ed.command("sculpt brush draw");
    size_t peak = built, grew = 0;
    for (int s = 0; s < 12; s++)
      for (int k = 0; k < 6; k++) {
        const size_t before = ed.sculpt_ray_entries_for_test();
        ed.command(strprintf("sculpt dab %g %g", px.x + R * 0.1f * (float)(k - 3), px.y));
        const size_t after = ed.sculpt_ray_entries_for_test();
        grew = std::max(grew, after > before ? after - before : (size_t)0);
        peak = std::max(peak, after);
      }
    ed.command("sculpt brush grab");
    const Recti sr = ed.scene_rect_for_test();
    const int wx = sr.x + (int)px.x, wy = sr.y + (int)px.y;
    for (int rep = 0; rep < 3; rep++) {
      step(ed, {mouse_event(platform::EventType::MouseMove, wx, wy)});
      step(ed, {mouse_event(platform::EventType::MouseDown, wx, wy)});
      for (int i = 1; i <= 6; i++) step(ed, {mouse_event(platform::EventType::MouseMove, wx + (int)(R * 0.08f * (float)i), wy - i)});
      step(ed, {mouse_event(platform::EventType::MouseUp, wx + (int)(R * 0.5f), wy - 6)});
      step(ed);
      peak = std::max(peak, ed.sculpt_ray_entries_for_test());
    }
    const size_t bound = 4 * built + 1024 + grew;  // (three times the built size is the rebuild's threshold; a rebuild after the surface grew counts a little more)
    std::printf("    built with %zu entries; peak %zu over 72 dabs and 3 drags (one dab adds up to %zu); bound %zu; grid builds since: %zu\n", built, peak, grew, bound,
                ed.sculpt_ray_builds_for_test() - builds0);
    CHECK(peak <= bound);
    const R562Sweep s1 = r562_sweep(ed, px, R, 0.9f, R * 0.35f);
    std::printf("    after: %d hit, %d miss, %d differ\n", s1.hit, s1.miss, s1.differ);
    CHECK(s1.differ == 0 && s1.bad == 0 && s1.hit > 10);
  });

  test("sculpt (round 562, te): a Smooth dab and stroke come out the same to the bit when the depth plane is noised", [] {
    CHECK(r562_noise_case("smooth") == 0);
  });

  test("sculpt (round 562, te): a Mask dab and stroke paint the same mask to the bit when the depth plane is noised", [] {
    CHECK(r562_noise_case("mask") == 0);
  });
}

void run_sculpt_tests() {
  tests_round256();
  tests_round256_pointer();
  tests_round257();
  tests_round258();
  tests_round259();
  tests_round260();
  tests_round261();
  tests_round262();
  tests_round368();
  tests_round508();
  tests_round508_te();
  tests_round540();
  tests_round540_te();
  tests_round562();
  tests_round562_te();
}
