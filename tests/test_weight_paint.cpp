// SPDX-License-Identifier: GPL-2.0-or-later
// Weight Paint automasking, Add / Set Weight and an accumulating Blur (task 0519, round 539). Every stroke is a real
// mouse drag through step_frame_headless; only the console test uses the console's own forms.
#include "test_common.h"

#include <cmath>

using namespace bl;

namespace {

/* Pixels per local unit of the pad as the camera sees it (the pad is flat and faces the camera). */
float wpt_ppu(R68 &t) {
  int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  CHECK(t.ed.project_to_window(t.pad->world_matrix().point({0, 0, 0}), x0, y0));
  CHECK(t.ed.project_to_window(t.pad->world_matrix().point({1, 0, 0}), x1, y1));
  return (float)std::abs(x1 - x0);
}

/* A brush radius, in pixels, that covers `units` of the pad. */
int wpt_px(R68 &t, float units) { return std::max(2, (int)(units * wpt_ppu(t))); }

/* One mouse drag through local points: move, press, move along, release (a single point is a click). */
void wpt_drag(R68 &t, const std::vector<Vec3> &pts, int mods = 0) {
  std::vector<std::pair<int, int>> px;
  for (Vec3 p : pts) {
    int x = 0, y = 0;
    CHECK(t.ed.project_to_window(t.pad->world_matrix().point(p), x, y));
    px.push_back({x, y});
  }
  auto ev = [&](R34ET ty, int x, int y) {
    platform::Event e = r34_mouse(ty, x, y);
    e.mods = mods;
    return e;
  };
  t.ed.step_frame_headless({ev(R34ET::MouseMove, px[0].first, px[0].second)});
  t.ed.step_frame_headless({ev(R34ET::MouseDown, px[0].first, px[0].second)});
  for (size_t i = 1; i < px.size(); i++)
    for (int k = 1; k <= 6; k++)
      t.ed.step_frame_headless({ev(R34ET::MouseMove, px[i - 1].first + (px[i].first - px[i - 1].first) * k / 6,
                                   px[i - 1].second + (px[i].second - px[i - 1].second) * k / 6)});
  t.ed.step_frame_headless({ev(R34ET::MouseUp, px.back().first, px.back().second)});
  t.ed.step_frame_headless();
}

/* A strong, flat brush: a masked vertex's weight is then its automask squared (Add, strength 1, constant falloff; the blend takes the automask twice). */
void wpt_flat_brush(R68 &t, float units, const char *mode = "add", float weight = 1.0f) {
  t.ed.command("wpfalloff constant");
  t.ed.command("wpfront off");
  t.ed.command(std::string("wpbrush ") + mode + " " + std::to_string(wpt_px(t, units)) + " 1 " + std::to_string(weight));
}

const std::vector<float> &wpt_w(R68 &t) { return t.mesh().vertex_groups[0].weights; }

float wpt_max(const std::vector<float> &w, size_t b = 0, size_t e = SIZE_MAX) {
  float m = 0.0f;
  for (size_t i = b; i < std::min(e, w.size()); i++) m = std::max(m, w[i]);
  return m;
}

bool wpt_sane(const std::vector<float> &w) {
  for (float x : w)
    if (!std::isfinite(x) || x < 0.0f || x > 1.0f) return false;
  return true;
}

/* Two 5 by 5 grids in one mesh, 0.1 apart and mirror images across x = 0 (the first has the vertices [0, na)). */
MeshPtr wpt_two_islands(size_t &na) {
  auto m = std::make_shared<Mesh>();
  for (int side = 0; side < 2; side++) {
    MeshPtr g = primitives::grid(1.0f, 5, 5);
    const uint32_t base = (uint32_t)m->vert_count();
    const float cx = side == 0 ? -0.55f : 0.55f;
    for (const Vec3 &p : g->positions) m->add_vert({p.x + cx, p.y, p.z});
    for (size_t f = 0; f < g->face_count(); f++) {
      std::vector<uint32_t> vs(g->face_verts(f), g->face_verts(f) + g->face_size(f));
      for (uint32_t &v : vs) v += base;
      m->add_face(vs.data(), vs.size());
    }
    if (side == 0) na = m->vert_count();
  }
  return m;
}

/* A groove along z = -0.5 (floor 0.15 down) and a ridge along z = 0.5 (top 0.15 up) in a 2 by 2 pad. */
MeshPtr wpt_groove_ridge() {
  MeshPtr m = primitives::grid(2.0f, 20, 20);
  for (int x = 0; x <= 20; x++) {
    m->positions[(size_t)(5 * 21 + x)].y = -0.15f;
    m->positions[(size_t)(15 * 21 + x)].y = 0.15f;
  }
  return m;
}

/* A top (6 by 6, y = 0) with a vertical wall down its +x edge, as separate vertices (a box's top and side). */
MeshPtr wpt_box_top_and_side(size_t &ntop) {
  auto m = std::make_shared<Mesh>();
  MeshPtr g = primitives::grid(1.0f, 6, 6);
  for (const Vec3 &p : g->positions) m->add_vert(p);
  for (size_t f = 0; f < g->face_count(); f++) m->add_face(std::vector<uint32_t>(g->face_verts(f), g->face_verts(f) + g->face_size(f)).data(), g->face_size(f));
  ntop = m->vert_count();
  const int nz = 6, ny = 4;
  for (int r = 0; r <= ny; r++)
    for (int c = 0; c <= nz; c++) m->add_vert({0.5f, -0.5f * (float)r / (float)ny, -0.5f + (float)c / (float)nz});
  for (int r = 0; r < ny; r++)
    for (int c = 0; c < nz; c++) {
      const uint32_t a = (uint32_t)ntop + (uint32_t)(r * (nz + 1) + c), b = a + 1, cc = a + (uint32_t)(nz + 1) + 1, d = a + (uint32_t)(nz + 1);
      m->add_face({a, b, cc, d});  // faces +x
    }
  return m;
}

float wpt_variance(const std::vector<float> &w) {
  double mean = 0.0, var = 0.0;
  for (float x : w) mean += x;
  mean /= (double)std::max<size_t>(1, w.size());
  for (float x : w) var += (x - mean) * (x - mean);
  return (float)(var / (double)std::max<size_t>(1, w.size()));
}

void wpt_all_on(R68 &t) {
  for (const char *o : {"topology", "boundary", "cavity", "view", "area"}) t.ed.command(std::string("wpautomask ") + o + " on");
  t.ed.command("wpmirror on");
}

}  // namespace

void run_weight_paint_tests() {
  test("weight paint automasking: Topology paints only the island the stroke starts on; with Mirror X the mirrored island too", [] {
    size_t na = 0;
    MeshPtr m = wpt_two_islands(na);
    auto run = [&](bool topology, bool mirror) {
      R68 t(m);
      wpt_flat_brush(t, 0.3f);
      if (topology) t.ed.command("wpautomask topology on");
      if (mirror) t.ed.command("wpmirror on");
      wpt_drag(t, {Vec3(-0.15f, 0, 0)});  // the first island's inner edge, the second under the brush too
      const std::vector<float> &w = wpt_w(t);
      return std::pair<float, float>(wpt_max(w, 0, na), wpt_max(w, na));
    };
    const auto off = run(false, false), on = run(true, false), mir = run(true, true);
    std::printf("    islands (A, B) painted max: no automask %.2f/%.2f, Topology %.2f/%.2f, Topology + Mirror X %.2f/%.2f\n", off.first, off.second, on.first, on.second, mir.first, mir.second);
    CHECK(off.first > 0.5f && off.second > 0.5f);  // both islands are under the brush without the automask
    CHECK(on.first > 0.5f && on.second == 0.0f);
    CHECK(mir.first > 0.5f && mir.second > 0.5f);
  });

  test("weight paint automasking: Mesh Boundary leaves the open edge unpainted and Propagation Steps 3 grades three rings", [] {
    R68 t;
    wpt_flat_brush(t, 0.4f);
    t.ed.command("wpautomask boundary on");
    wpt_drag(t, {Vec3(0.5f, 0, 0), Vec3(1.2f, 0, 0)});
    const float edge = t.weight_at({1.0f, 0, 0}), inside = t.weight_at({0.6f, 0, 0});
    std::printf("    boundary on, steps 1: edge %.3f, ring 1 %.3f, inside %.3f\n", edge, t.weight_at({0.9f, 0, 0}), inside);
    CHECK(edge == 0.0f);
    CHECK(inside > 0.9f);

    R68 u;
    wpt_flat_brush(u, 0.4f);
    u.ed.command("wpautomask boundary on");
    u.ed.command("wpautomask boundarysteps 3");
    wpt_drag(u, {Vec3(0.5f, 0, 0), Vec3(1.2f, 0, 0)});
    const float r0 = u.weight_at({1.0f, 0, 0}), r1 = u.weight_at({0.9f, 0, 0}), r2 = u.weight_at({0.8f, 0, 0}), r3 = u.weight_at({0.7f, 0, 0});
    std::printf("    boundary on, steps 3: rings 0..3 = %.3f %.3f %.3f %.3f\n", r0, r1, r2, r3);
    CHECK(r0 == 0.0f);
    CHECK(r0 < r1 && r1 < r2 && r2 < r3);  // graded
    CHECK(r3 > 0.9f);
  });

  test("weight paint automasking: Mesh Boundary with the default step holds only the edge back, as Blender's", [] {
    /* Blender's init_boundary_masking_mesh: factor 1 - (1 - d / steps)^2, so at Propagation Steps 1 ring 1 (d = 1)
     * takes the full brush. The spec first said "the next ring at less than the inside"; that was wrong against
     * Blender and the spec was corrected (round 539), so this pins the edge at 0 and ring 1 equal to the inside. */
    R68 t;
    wpt_flat_brush(t, 0.4f);
    t.ed.command("wpautomask boundary on");
    wpt_drag(t, {Vec3(0.5f, 0, 0), Vec3(1.2f, 0, 0)});
    const float ring1 = t.weight_at({0.9f, 0, 0}), inside = t.weight_at({0.6f, 0, 0});
    const float edge = t.weight_at({1.0f, 0, 0});
    std::printf("    default steps: edge %.3f, ring 1 %.3f, inside %.3f\n", edge, ring1, inside);
    CHECK(edge == 0.0f && inside > 0.0f && std::fabs(ring1 - inside) < 1e-4f);
  });

  test("weight paint automasking: Mesh Boundary on a closed sphere paints as without it", [] {
    MeshPtr s = primitives::uv_sphere(0.5f, 24, 16);
    auto run = [&](bool boundary) {
      R68 t(s);
      wpt_flat_brush(t, 0.6f);
      if (boundary) t.ed.command("wpautomask boundary on");
      wpt_drag(t, {Vec3(0, 0, 0), Vec3(0.2f, 0, 0.1f)});
      return wpt_w(t);
    };
    const std::vector<float> a = run(false), b = run(true);
    size_t painted = 0, diff = 0;
    for (size_t i = 0; i < a.size() && i < b.size(); i++) {
      if (a[i] > 0.0f) painted++;
      if (std::fabs(a[i] - b[i]) > 1e-5f) diff++;
    }
    std::printf("    sphere: %zu vertices painted, %zu differ with Mesh Boundary on\n", painted, diff);
    CHECK(a.size() == b.size() && painted > 10 && diff == 0);
  });

  test("weight paint automasking: Cavity paints the groove's floor more than the ridge's top, Inverted the other way round", [] {
    MeshPtr m = wpt_groove_ridge();
    auto run = [&](const char *kind) {
      R68 t(m);
      wpt_flat_brush(t, 0.25f);
      t.ed.command(std::string("wpautomask ") + kind + " on");
      t.ed.command("wpautomask cavityfactor 1");
      wpt_drag(t, {Vec3(0, 0, -0.6f), Vec3(0, 0, 0.6f)});
      return std::pair<float, float>(t.weight_at({0, -0.15f, -0.5f}), t.weight_at({0, 0.15f, 0.5f}));
    };
    const auto cav = run("cavity"), inv = run("inverted");
    std::printf("    groove floor / ridge top: Cavity %.3f / %.3f, Inverted %.3f / %.3f\n", cav.first, cav.second, inv.first, inv.second);
    CHECK(cav.first > cav.second + 0.05f);
    CHECK(inv.second > inv.first + 0.05f);
  });

  test("weight paint automasking: View Normal at 30 degrees paints the top of a sphere seen from above, not its edge-on sides", [] {
    MeshPtr s = primitives::uv_sphere(0.5f, 24, 16);
    auto run = [&](bool view) {
      R68 t(s);
      wpt_flat_brush(t, 0.7f);  // Front Faces Only off, so the sides and the far half are in reach
      if (view) {
        t.ed.command("wpautomask view on");
        t.ed.command("wpautomask viewlimit 30");
      }
      wpt_drag(t, {Vec3(0, 0, 0)});
      float top = 0.0f, side = 0.0f;
      const Mesh &mm = t.mesh();
      for (size_t i = 0; i < mm.vert_count(); i++) {
        const float ny = mm.positions[i].y / length(mm.positions[i]);
        if (ny > 0.9f) top = std::max(top, mm.vertex_groups[0].weights[i]);
        if (ny < 0.2f) side = std::max(side, mm.vertex_groups[0].weights[i]);
      }
      return std::pair<float, float>(top, side);
    };
    const auto off = run(false), on = run(true);
    std::printf("    top / sides: no automask %.3f / %.3f, View Normal 30 deg %.3f / %.3f\n", off.first, off.second, on.first, on.second);
    CHECK(off.second > 0.3f);  // without the automask the sides are painted (the test means something)
    CHECK(on.first > 0.9f);
    CHECK(on.second == 0.0f);
  });

  test("weight paint automasking: Area Normal paints the top of a box a stroke started on, not the side it was dragged over", [] {
    size_t ntop = 0;
    MeshPtr m = wpt_box_top_and_side(ntop);
    auto run = [&](bool area) {
      R68 t(m);
      wpt_flat_brush(t, 0.35f);
      if (area) t.ed.command("wpautomask area on");
      wpt_drag(t, {Vec3(-0.2f, 0, 0), Vec3(0.5f, -0.25f, 0)});
      const std::vector<float> &w = wpt_w(t);
      return std::pair<float, float>(wpt_max(w, 0, ntop), wpt_max(w, ntop));
    };
    const auto off = run(false), on = run(true);
    std::printf("    top / side: no automask %.3f / %.3f, Area Normal %.3f / %.3f\n", off.first, off.second, on.first, on.second);
    CHECK(off.second > 0.3f);
    CHECK(on.first > 0.9f);
    CHECK(on.second == 0.0f);
  });

  test("weight paint automasking: Topology starts on the piece the cursor sees, not the one whose vertex is nearest on screen behind it", [] {
    /* A small near piece (0.5 up, towards the camera) over a larger far piece, both facing it. The cursor sits on the near
     * piece's face centre; the far piece's vertex straight below it (in the world) projects nearer the cursor than any
     * near-piece vertex does. */
    auto m = std::make_shared<Mesh>();
    auto add_grid = [&](float size, int n, Vec3 off) {
      MeshPtr g = primitives::grid(size, n, n);
      const uint32_t base = (uint32_t)m->vert_count();
      for (const Vec3 &p : g->positions) m->add_vert(p + off);
      for (size_t f = 0; f < g->face_count(); f++) {
        std::vector<uint32_t> vs(g->face_verts(f), g->face_verts(f) + g->face_size(f));
        for (uint32_t &v : vs) v += base;
        m->add_face(vs.data(), vs.size());
      }
    };
    add_grid(1.6f, 8, Vec3(0.1f, 0.0f, 0.1f));  // far piece: vertices at ..., 0.1, 0.3, ... (0.1 is in its set)
    const size_t nfar = m->vert_count();
    add_grid(0.4f, 2, Vec3(0, 0.5f, 0));  // near piece: vertices at -0.2, 0, 0.2
    const Vec3 cursor(0.1f, 0.5f, 0.1f);  // the near piece's face centre
    auto run = [&](bool topology) {
      R68 t(m);
      wpt_flat_brush(t, 0.35f);
      if (topology) t.ed.command("wpautomask topology on");
      int cx = 0, cy = 0;
      CHECK(t.ed.project_to_window(t.pad->world_matrix().point(cursor), cx, cy));
      float near_d = 1e9f, far_d = 1e9f;
      for (size_t i = 0; i < t.mesh().vert_count(); i++) {
        int x = 0, y = 0;
        if (!t.ed.project_to_window(t.pad->world_matrix().point(t.mesh().positions[i]), x, y)) continue;
        const float d = std::hypot((float)(x - cx), (float)(y - cy));
        (i < nfar ? far_d : near_d) = std::min(i < nfar ? far_d : near_d, d);
      }
      CHECK(far_d < near_d);  // the setup: a far vertex is nearer the cursor on screen
      wpt_drag(t, {cursor});
      const std::vector<float> &w = wpt_w(t);
      std::printf("    cursor px distance: nearest far vertex %.1f, nearest near vertex %.1f\n", far_d, near_d);
      return std::pair<float, float>(wpt_max(w, nfar), wpt_max(w, 0, nfar));
    };
    const auto off = run(false), on = run(true);
    std::printf("    near / far piece painted max: no automask %.2f / %.2f, Topology %.2f / %.2f\n", off.first, off.second, on.first, on.second);
    CHECK(off.first > 0.5f && off.second > 0.5f);  // both are under the brush without the automask
    CHECK(on.first > 0.5f);
    CHECK(on.second == 0.0f);
  });

  test("weight paint automasking: Area Normal on a sphere seen from above, started off-centre, paints the top under the brush, not the bottom or the +x side", [] {
    MeshPtr s = primitives::uv_sphere(0.5f, 24, 16);
    const Vec3 hit(0.2f, std::sqrt(0.25f - 0.04f), 0.0f);  // on the sphere's top, off the centre
    auto run = [&](bool area) {
      R68 t(s);
      wpt_flat_brush(t, 0.35f);  // Front Faces Only off: the far side is in reach of the brush
      if (area) t.ed.command("wpautomask area on");
      wpt_drag(t, {hit});
      float top = 0.0f, bottom = 0.0f, side = 0.0f;
      const Mesh &mm = t.mesh();
      for (size_t i = 0; i < mm.vert_count(); i++) {
        const Vec3 n = mm.positions[i] * (1.0f / length(mm.positions[i]));
        const float w = mm.vertex_groups[0].weights[i];
        if (n.y > 0.8f) top = std::max(top, w);
        if (n.y < -0.5f) bottom = std::max(bottom, w);
        if (n.x > 0.9f && std::fabs(n.y) < 0.4f) side = std::max(side, w);
      }
      return std::array<float, 3>{top, bottom, side};
    };
    const auto off = run(false), on = run(true);
    std::printf("    top / bottom / +x side: no automask %.3f / %.3f / %.3f, Area Normal %.3f / %.3f / %.3f\n", off[0], off[1], off[2], on[0], on[1], on[2]);
    CHECK(off[2] > 0.3f);  // without the automask the side is painted (the test means something)
    CHECK(on[0] > 0.5f);
    CHECK(on[1] == 0.0f);
    CHECK(on[2] == 0.0f);
  });

  test("weight paint automasking: Set Weight under a half automask settles at about half the Weight after several strokes", [] {
    R68 t;
    wpt_flat_brush(t, 0.3f, "set", 1.0f);
    t.ed.command("wpautomask cavity on");  // a flat pad: the cavity factor is exactly one half everywhere inside
    std::vector<float> seen;
    for (int i = 0; i < 6; i++) {
      wpt_drag(t, {Vec3(0, 0, 0)});
      seen.push_back(t.weight_at({0, 0, 0}));
    }
    std::printf("    Set Weight 1.0 under a 0.5 automask, after strokes 1..6: ");
    for (float w : seen) std::printf("%.3f ", w);
    std::printf("\n");
    CHECK(seen[0] > 0.2f && seen[0] < 0.3f);  // 0.25: one stroke's half of the half
    CHECK(seen[5] > 0.45f && seen[5] < 0.55f);  // not washed out to 1
  });

  test("weight paint: Add Weight is the default brush, Set Weight paints towards Weight, `wpbrush set` picks it, Ctrl paints 1 - Weight", [] {
    CHECK(std::string(kWeightBrushModes[0]) == "Add Weight");
    CHECK(std::string(kWeightBrushModes[1]) == "Subtract");
    CHECK(std::string(kWeightBrushModes[2]) == "Set Weight");
    CHECK(std::string(kWeightBrushModes[3]) == "Blur");
    R68 t;
    CHECK(t.ed.wp_brush_for_test().mode == 0);
    t.ed.command("wpbrush set");
    CHECK(t.ed.wp_brush_for_test().mode == 2);
    t.ed.command("wpbrush add");
    CHECK(t.ed.wp_brush_for_test().mode == 0);
    t.ed.command("wpbrush setweight");
    CHECK(t.ed.wp_brush_for_test().mode == 2);
    t.ed.command("wpbrush draw");  // the old name still works
    CHECK(t.ed.wp_brush_for_test().mode == 2);
    t.ed.command("wpbrush addweight");
    CHECK(t.ed.wp_brush_for_test().mode == 0);
    t.ed.command("wpbrush sub");
    CHECK(t.ed.wp_brush_for_test().mode == 1);
    t.ed.command("wpbrush blur");
    CHECK(t.ed.wp_brush_for_test().mode == 3);

    wpt_flat_brush(t, 0.3f, "set", 0.6f);
    wpt_drag(t, {Vec3(0, 0, 0)});
    const float first = t.weight_at({0, 0, 0});
    wpt_drag(t, {Vec3(0, 0, 0)});
    const float second = t.weight_at({0, 0, 0});
    t.ed.command("wpbrush set 40 1 0.2");  // Set Weight paints down as well as up
    wpt_drag(t, {Vec3(0, 0, 0)});
    const float down = t.weight_at({0, 0, 0});
    std::printf("    Set Weight 0.6 twice: %.3f then %.3f; then Weight 0.2: %.3f\n", first, second, down);
    CHECK(std::fabs(first - 0.6f) < 0.01f && std::fabs(second - 0.6f) < 0.01f);  // Add would pile up to 1
    CHECK(std::fabs(down - 0.2f) < 0.01f);

    R68 c;
    wpt_flat_brush(c, 0.3f, "set", 0.7f);
    wpt_drag(c, {Vec3(0, 0, 0)}, platform::MOD_CTRL);
    const float flipped = c.weight_at({0, 0, 0});
    std::printf("    Ctrl + Set Weight 0.7 paints %.3f (1 - Weight = 0.3)\n", flipped);
    CHECK(std::fabs(flipped - 0.3f) < 0.01f);
  });

  test("weight paint: Blur at strength 0.1 keeps evening the weights, each dab and each stroke moving them further", [] {
    R68 t;
    {
      Mesh &m = *mesh_make_mutable(t.pad->mesh_filter()->mesh);
      for (size_t i = 0; i < m.vert_count(); i++) m.vertex_groups[0].weights[i] = m.positions[i].x < 0.0f ? 1.0f : 0.0f;
      m.touch();
    }
    t.ed.step_frame_headless();
    t.ed.command("wpfalloff constant");
    t.ed.command("wpfront off");
    t.ed.command("wpbrush blur " + std::to_string(wpt_px(t, 0.4f)) + " 0.1 1");
    const float v0 = wpt_variance(wpt_w(t));
    wpt_drag(t, {Vec3(0, 0, 0)});  // a click: one dab
    const float v_click = wpt_variance(wpt_w(t));
    wpt_drag(t, {Vec3(-0.4f, 0, 0), Vec3(0.4f, 0, 0), Vec3(-0.4f, 0, 0), Vec3(0.4f, 0, 0)});  // many dabs
    const float v_drag = wpt_variance(wpt_w(t));
    wpt_drag(t, {Vec3(-0.4f, 0, 0), Vec3(0.4f, 0, 0), Vec3(-0.4f, 0, 0), Vec3(0.4f, 0, 0)});  // and again
    const float v_again = wpt_variance(wpt_w(t));
    std::printf("    weight variance: start %.5f, after a click %.5f, a drag %.5f, the drag again %.5f\n", v0, v_click, v_drag, v_again);
    CHECK(v_click < v0);
    CHECK(v_drag < v_click);
    CHECK(v_again < v_drag);
    CHECK(wpt_sane(wpt_w(t)));
  });

  test("weight paint automasking: every option on, odd meshes and NaN positions leave the weights finite and in 0..1", [] {
    struct Case {
      const char *name;
      MeshPtr mesh;
      bool empty_group = false;
    };
    std::vector<Case> cases;
    cases.push_back({"closed sphere", primitives::uv_sphere(0.5f, 16, 12)});
    {  // three faces on one edge
      auto m = std::make_shared<Mesh>();
      m->add_vert({0, 0, -0.5f});
      m->add_vert({0, 0, 0.5f});
      m->add_vert({0.5f, 0, 0});
      m->add_vert({-0.5f, 0, 0});
      m->add_vert({0, 0.5f, 0});
      m->add_face({0, 1, 2});
      m->add_face({1, 0, 3});
      m->add_face({0, 1, 4});
      cases.push_back({"non-manifold edge (3 faces)", m});
    }
    {  // a vertex in no face, where the stroke starts
      MeshPtr m = primitives::grid(2.0f, 6, 6);
      m->add_vert({0.02f, 0, 0});
      cases.push_back({"loose vertex", m});
    }
    cases.push_back({"open strip", primitives::grid(2.0f, 12, 1)});
    {
      MeshPtr m = primitives::grid(2.0f, 8, 8);
      m->positions[40].x = std::nanf("");
      m->positions[41].y = std::nanf("");
      m->positions[13] = Vec3(INFINITY, 0, 0);
      cases.push_back({"NaN / infinite positions", m});
    }
    cases.push_back({"empty group", primitives::grid(2.0f, 8, 8), true});
    for (Case &c : cases)
      for (const char *mode : {"add", "subtract", "set", "blur"}) {
        R68 t(c.mesh ? std::make_shared<Mesh>(*c.mesh) : nullptr);
        if (c.empty_group) mesh_make_mutable(t.pad->mesh_filter()->mesh)->vertex_groups[0].weights.clear();
        wpt_flat_brush(t, 0.4f, mode, 0.8f);
        wpt_all_on(t);
        wpt_drag(t, {Vec3(-0.4f, 0, 0), Vec3(0.4f, 0, 0)});
        wpt_drag(t, {Vec3(0.02f, 0, -0.4f), Vec3(0.02f, 0, 0.4f)}, platform::MOD_CTRL);
        const bool ok = wpt_sane(wpt_w(t));
        if (!ok) std::printf("    NOT SANE: %s with %s\n", c.name, mode);
        CHECK(ok);
      }
    std::printf("    %zu odd meshes x 4 brushes, every automask on with Mirror X: all weights finite in 0..1\n", cases.size());
  });

  test("weight paint automasking: a one-vertex island (a loose vertex) is painted alone with Topology on", [] {
    MeshPtr m = primitives::grid(2.0f, 6, 6);
    const uint32_t loose = m->add_vert({0.02f, 0, 0});
    R68 t(m);
    wpt_flat_brush(t, 0.5f);
    t.ed.command("wpautomask topology on");
    wpt_drag(t, {Vec3(0.02f, 0, 0)});
    const std::vector<float> &w = wpt_w(t);
    float others = 0.0f;
    for (size_t i = 0; i < w.size(); i++)
      if (i != loose) others = std::max(others, w[i]);
    std::printf("    loose vertex %.3f, every other vertex at most %.3f\n", w[loose], others);
    CHECK(w[loose] > 0.5f);
    CHECK(others == 0.0f);
  });

  test("weight paint automasking: the wpautomask console switches options, takes numbers, and refuses bad input without a change", [] {
    R68 t;
    WeightBrush &b = t.ed.wp_brush_for_test();
    CHECK(!b.am_topology && !b.am_boundary && b.am_cavity == 0 && !b.am_view_normal && !b.am_area_normal);  // all off by default
    t.ed.command("wpautomask topology");
    CHECK(b.am_topology);
    t.ed.command("wpautomask topology off");
    CHECK(!b.am_topology);
    t.ed.command("wpautomask boundary on");
    t.ed.command("wpautomask cavity on");
    CHECK(b.am_boundary && b.am_cavity == 1);
    t.ed.command("wpautomask inverted on");
    CHECK(b.am_cavity == 2);
    t.ed.command("wpautomask inverted off");
    CHECK(b.am_cavity == 1);  // Inverted off is back to plain Cavity
    t.ed.command("wpautomask cavity off");
    CHECK(b.am_cavity == 0);
    t.ed.command("wpautomask inverted on");
    CHECK(b.am_cavity == 2);
    t.ed.command("wpautomask cavity off");
    CHECK(b.am_cavity == 0);
    t.ed.command("wpautomask cavity on");
    t.ed.command("wpautomask inverted off");
    CHECK(b.am_cavity == 1);  // Cavity stays on
    t.ed.command("wpautomask cavity off");
    t.ed.command("wpautomask inverted off");
    CHECK(b.am_cavity == 0);  // inverted off leaves Off alone
    t.ed.command("wpautomask view on");
    t.ed.command("wpautomask area on");
    t.ed.command("wpautomask topology on");
    CHECK(b.am_view_normal && b.am_area_normal && b.am_topology);
    t.ed.command("wpautomask none");
    CHECK(!b.am_topology && !b.am_boundary && b.am_cavity == 0 && !b.am_view_normal && !b.am_area_normal);

    t.ed.command("wpautomask boundarysteps 3");
    t.ed.command("wpautomask cavityfactor 1.5");
    t.ed.command("wpautomask cavityblur 4");
    t.ed.command("wpautomask viewlimit 45");
    t.ed.command("wpautomask viewfalloff 0.5");
    t.ed.command("wpautomask arealimit 30");
    t.ed.command("wpautomask areafalloff 0.1");
    CHECK(b.am_boundary_steps == 3 && b.am_cavity_blur == 4);
    CHECK(std::fabs(b.am_cavity_factor - 1.5f) < 1e-5f && std::fabs(b.am_view_limit - 45.0f) < 1e-5f && std::fabs(b.am_view_falloff - 0.5f) < 1e-5f);
    CHECK(std::fabs(b.am_area_limit - 30.0f) < 1e-5f && std::fabs(b.am_area_falloff - 0.1f) < 1e-5f);
    t.ed.command("wpautomask boundarysteps 999");
    t.ed.command("wpautomask viewlimit 999");
    CHECK(b.am_boundary_steps == 20);
    CHECK(b.am_view_limit == 180.0f);
    t.ed.command("wpautomask boundarysteps 1e10");
    CHECK(b.am_boundary_steps == 20);
    t.ed.command("wpautomask cavityblur -1e10");
    CHECK(b.am_cavity_blur == 0);

    /* Bad input: nothing changes. */
    t.ed.command("wpautomask boundarysteps 3");
    t.ed.command("wpautomask viewlimit 45");
    const WeightBrush before = b;
    t.ed.command("wpautomask bogus on");
    t.ed.command("wpautomask");
    t.ed.command("wpautomask boundarysteps");
    t.ed.command("wpautomask boundarysteps nan");
    t.ed.command("wpautomask viewlimit inf");
    const bool same_flags = b.am_topology == before.am_topology && b.am_boundary == before.am_boundary && b.am_cavity == before.am_cavity;
    CHECK(same_flags && b.am_boundary_steps == before.am_boundary_steps && b.am_view_limit == before.am_view_limit);
    /* A word is not a number: it must not become 0 (an unparsed atof) and move the setting. */
    t.ed.command("wpautomask boundarysteps abc");
    t.ed.command("wpautomask viewlimit abc");
    std::printf("    after `boundarysteps abc` / `viewlimit abc`: steps %d (was %d), view limit %.1f (was %.1f)\n", b.am_boundary_steps, before.am_boundary_steps,
                b.am_view_limit, before.am_view_limit);
    CHECK(b.am_boundary_steps == before.am_boundary_steps);
    CHECK(b.am_view_limit == before.am_view_limit);
  });

  test("weight paint: the console wpstroke with an automask on paints the same as a mouse drag", [] {
    size_t na = 0;
    MeshPtr m = wpt_two_islands(na);
    R68 t(m);
    wpt_flat_brush(t, 0.3f);
    t.ed.command("wpautomask topology on");
    t.stroke({Vec3(-0.15f, 0, 0)});
    const std::vector<float> &w = wpt_w(t);
    std::printf("    console stroke: island A %.2f, island B %.2f\n", wpt_max(w, 0, na), wpt_max(w, na));
    CHECK(wpt_max(w, 0, na) > 0.5f && wpt_max(w, na) == 0.0f);
  });
}
