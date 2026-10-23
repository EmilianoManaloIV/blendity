// SPDX-License-Identifier: GPL-2.0-or-later
// blendity_tests: the animation tests (task 0177: one file a subsystem; the groups keep their rounds' order).
#include "test_common.h"


/* ===================================================================== */
/* Round 64 (task 0042): curve objects, the Curve modifier, Array along a curve */
/* ===================================================================== */

/* A beam along +X from 0 to `len`, `w` thick, cut into `segs` along its length. */
static MeshPtr r64_beam(float len, float w, int segs) {
  MeshPtr m = std::make_shared<Mesh>();
  for (int i = 0; i <= segs; i++)
    for (int k = 0; k < 4; k++) {
      const float y = (k == 1 || k == 2) ? w : -w, z = (k >= 2) ? w : -w;
      m->add_vert({len * i / segs, y, z});
    }
  for (int i = 0; i < segs; i++)
    for (int k = 0; k < 4; k++) {
      const uint32_t a = (uint32_t)(i * 4 + k), b = (uint32_t)(i * 4 + (k + 1) % 4);
      m->add_face({a, b, b + 4, a + 4});
    }
  m->touch();
  return m;
}

/* An object with the beam and a Curve modifier, and a Curve object at the origin. */
struct R64Scene {
  Scene s;
  GameObject *beam = nullptr, *curve_obj = nullptr;
  Curve *curve = nullptr;
  CurveModifier *mod = nullptr;
  MeshPtr base;
  R64Scene(MeshPtr m) {
    base = m;
    beam = s.create("Beam");
    beam->add<MeshFilter>()->mesh = m;
    mod = beam->add<CurveModifier>();
    mod->object = "Path";
    curve_obj = s.create("Path");
    curve = curve_obj->add<Curve>();
  }
};

static void round64_tests() {
  test("curves: a straight curve along X leaves the mesh as it is (Bezier, Catmull-Rom and Poly)", [] {
    for (int type = 0; type < 3; type++) {
      R64Scene t(r64_beam(3.0f, 0.2f, 12));
      t.curve->type = type;
      for (int i = 0; i < 4; i++) t.curve->points.push_back({Vec3(i * 2.0f, 0, 0), 0.0f, 1.0f});
      const Mesh *m = t.beam->evaluated_mesh();
      float worst = 0.0f;
      for (size_t i = 0; i < m->positions.size(); i++) worst = std::max(worst, length(m->positions[i] - t.base->positions[i]));
      std::printf("    type %d: largest move %g\n", type, worst);
      CHECK(worst < 1e-4f);
    }
  });

  test("curves: a straight curve along each of the six deform axes leaves a mesh as it is; Tilt turns it, Radius scales it", [] {
    const Vec3 dirs[6] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {-1, 0, 0}, {0, -1, 0}, {0, 0, -1}};
    for (int axis = 0; axis < 6; axis++) {
      R64Scene t(primitives::uv_sphere(0.5f, 12, 8));
      t.mod->deform_axis = axis;
      t.curve->type = 2;
      t.curve->points = {{Vec3(0.0f), 0, 1}, {dirs[axis] * 4.0f, 0, 1}};
      const Mesh *m = t.beam->evaluated_mesh();
      float worst = 0.0f;
      for (size_t i = 0; i < m->positions.size(); i++) worst = std::max(worst, length(m->positions[i] - t.base->positions[i]));
      if (worst >= 1e-4f) std::printf("    axis %d: largest move %g\n", axis, worst);
      CHECK(worst < 1e-4f);
    }
    /* Tilt 90 degrees all along X turns the cross-section a quarter turn round X; Radius 2 doubles it. */
    R64Scene t(r64_beam(2.0f, 0.1f, 4));
    t.curve->type = 2;
    t.curve->points = {{Vec3(0, 0, 0), 90.0f, 2.0f}, {Vec3(5, 0, 0), 90.0f, 2.0f}};
    const Mesh *m = t.beam->evaluated_mesh();
    float worst = 0.0f;
    for (size_t i = 0; i < m->positions.size(); i++) {
      const Vec3 v = t.base->positions[i];
      const Vec3 turned = Quat::axis_angle({1, 0, 0}, kPi * 0.5f).rotate(Vec3(0, v.y, v.z)) * 2.0f + Vec3(v.x, 0, 0);
      worst = std::max(worst, length(m->positions[i] - turned));
    }
    std::printf("    tilt 90 and radius 2: off by %g\n", worst);
    CHECK(worst < 1e-4f);
  });

  test("curves: the Circle preset is round, and a closed curve that isn't flat meets itself without a twist", [] {
    Editor ed;
    ed.init_headless(400, 300);
    ed.command("create Curve: Circle");
    GameObject *g = ed.scene().find_by_name("BezierCircle");
    CHECK(g && g->curve());
    if (!g || !g->curve()) return;
    const CurvePath &path = g->curve()->local_path();
    float lo = 1e9f, hi = 0.0f;
    for (const Vec3 &p : path.pos) lo = std::min(lo, length(p)), hi = std::max(hi, length(p));
    std::printf("    circle radius from %.4f to %.4f\n", lo, hi);
    CHECK(lo > 0.99f && hi < 1.01f);
    /* A trefoil knot: the frame carried round comes back turned; the correction spreads that turn so the normal
     * at the end (just before the start) is the start's, turned only by the last short step. */
    Curve knot;
    knot.type = 2;
    knot.closed = true;
    for (int i = 0; i < 300; i++) {
      const float a = i / 300.0f * 2.0f * kPi;
      knot.points.push_back({Vec3(std::sin(a) + 2 * std::sin(2 * a), std::cos(a) - 2 * std::cos(2 * a), -std::sin(3 * a)), 0, 1});
    }
    const CurvePath kp = sample_curve(knot, Mat4::identity());
    Vec3 p0, t0, n0, pe, te, ne;
    float r;
    kp.at(0.0f, p0, t0, n0, r);
    kp.at(kp.total() - 1e-3f, pe, te, ne, r);
    std::printf("    trefoil: the normal meets itself within %.2f degrees\n", std::acos(std::min(1.0f, dot(n0, ne))) * kRad2Deg);
    CHECK(dot(n0, ne) > std::cos(3.0f * kDeg2Rad));
  });

  test("curves: with a curve's points selected, Delete removes the points, not the curve; selecting another object lets go of them", [] {
    Editor ed;
    ed.init_headless(800, 600);
    ed.command("create Curve: Path");
    ed.command("curve select Path 1 3");
    ed.command("delete");
    GameObject *g = ed.scene().find_by_name("Path");
    CHECK(g && g->curve() && g->curve()->points.size() == 3);
    if (g && g->curve() && g->curve()->points.size() == 3) CHECK(g->curve()->points[1].position.x == 0.0f);  // -2, 0, 2 left
    CHECK(ed.points_selected_for_test().empty());
    ed.command("curve select Path 0");
    ed.command("select Cube");
    CHECK(ed.points_selected_for_test().empty());
    ed.command("delete");
    CHECK(ed.scene().find_by_name("Path") != nullptr);  // the Cube went, the curve stayed
  });

  test("curves: a quarter circle bends a beam onto the arc, keeping each offset and the length along it", [] {
    const float R = 2.0f;
    R64Scene t(r64_beam(R * kPi * 0.5f * 0.9f, 0.1f, 40));
    t.curve->type = 2;
    for (int i = 0; i <= 512; i++) {
      const float a = i / 512.0f * kPi * 0.5f;
      t.curve->points.push_back({Vec3(R * std::sin(a), 0.0f, R * (1.0f - std::cos(a))), 0.0f, 1.0f});
    }
    const Mesh *m = t.beam->evaluated_mesh();
    const Vec3 c(0, 0, R);  // the arc's centre: the beam's +Z side faces it
    float worst_r = 0.0f, worst_a = 0.0f, worst_y = 0.0f;
    for (size_t i = 0; i < m->positions.size(); i++) {
      const Vec3 v = t.base->positions[i], p = m->positions[i];
      const Vec3 d = Vec3(p.x - c.x, 0.0f, p.z - c.z);
      worst_r = std::max(worst_r, std::fabs(length(d) - (R - v.z)));
      const float ang = std::atan2(p.x, c.z - p.z);  // turned from the start, round the centre
      if (v.x > 0.05f) worst_a = std::max(worst_a, std::fabs(ang * R - v.x) / v.x);
      worst_y = std::max(worst_y, std::fabs(p.y - v.y));
    }
    std::printf("    radius off by %g, length along the arc off by %.3f %%, height off by %g\n", worst_r, worst_a * 100.0f, worst_y);
    CHECK(worst_r < 1e-3f);
    CHECK(worst_a < 0.01f);
    CHECK(worst_y < 1e-4f);
  });

  test("curves: a closed curve wraps a long beam round; an open one goes on straight past its end", [] {
    const float r = 1.0f, L = 2.0f * kPi * r;
    R64Scene t(r64_beam(L * 1.5f, 0.05f, 60));
    t.curve->type = 2;
    t.curve->closed = true;
    for (int i = 0; i < 360; i++) {
      const float a = i / 360.0f * 2.0f * kPi;
      t.curve->points.push_back({Vec3(r * std::sin(a), 0.0f, r * (1.0f - std::cos(a))), 0.0f, 1.0f});
    }
    const CurvePath path = sample_curve(*t.curve, Mat4::identity());
    CHECK(path.closed && std::fabs(path.total() - L) < 1e-3f);
    Vec3 p0, p1, tg, n;
    float rad;
    path.at(0.3f, p0, tg, n, rad);
    path.at(0.3f + path.total(), p1, tg, n, rad);
    CHECK(length(p0 - p1) < 1e-4f);  // once round, the same place
    const Mesh *m = t.beam->evaluated_mesh();
    float off = 0.0f;
    for (const Vec3 &p : m->positions) off = std::max(off, std::fabs(length(Vec3(p.x, 0, p.z - r)) - r));
    CHECK(off < 0.06f);  // every vertex stays on the ring (within the beam's half width)
    /* Open, straight: a beam three times longer than the curve continues along its end's direction. */
    R64Scene o(r64_beam(9.0f, 0.1f, 9));
    o.curve->type = 2;
    o.curve->points = {{Vec3(0, 0, 0), 0, 1}, {Vec3(0, 0, -3), 0, 1}};  // along -Z
    const Mesh *om = o.beam->evaluated_mesh();
    const Vec3 end = om->positions[9 * 4];  // the far end's first corner, 6 past the curve's end
    CHECK(std::fabs(end.z - (-9.0f)) < 1e-3f && std::fabs(end.x - (-0.1f)) < 1e-3f);
  });

  test("curves: Array's Fit Curve fills the curve's length, Fit Length a length; changing the curve changes the count", [] {
    Scene s;
    GameObject *g = s.create("Posts");
    g->add<MeshFilter>()->mesh = primitives::cube(1.0f);
    auto *arr = g->add<ArrayModifier>();
    arr->fit_type = 2;
    arr->curve = "Fence";
    GameObject *fence = s.create("Fence");
    Curve *cv = fence->add<Curve>();
    cv->type = 2;
    cv->points = {{Vec3(0, 0, 0), 0, 1}, {Vec3(5.5f, 0, 0), 0, 1}};
    CHECK(g->evaluated_mesh()->vert_count() == 8 * 5);
    cv->points[1].position = Vec3(7.2f, 0, 0);
    CHECK(g->evaluated_mesh()->vert_count() == 8 * 7);
    fence->set_local_scale({2, 2, 2});  // the curve is measured in the array's space
    CHECK(g->evaluated_mesh()->vert_count() == 8 * 14);
    arr->fit_type = 1;
    arr->fit_length = 3.4f;
    CHECK(g->evaluated_mesh()->vert_count() == 8 * 3);
    arr->fit_length = std::numeric_limits<float>::quiet_NaN();
    CHECK(g->evaluated_mesh()->vert_count() == 8);
    arr->fit_type = 2;
    arr->curve = "Nothing";
    CHECK(g->evaluated_mesh()->vert_count() == 8);  // no curve: one copy
  });

  test("curves: moving the curve object or a point re-bends the mesh; a curve can't bend itself", [] {
    R64Scene t(r64_beam(2.0f, 0.1f, 8));
    t.curve->type = 1;
    t.curve->points = {{Vec3(0, 0, 0), 0, 1}, {Vec3(1, 0, 0.5f), 0, 1}, {Vec3(2, 0, 0), 0, 1}};
    const std::vector<Vec3> a = t.beam->evaluated_mesh()->positions;
    t.curve->points[1].position = Vec3(1, 0, -0.5f);
    const std::vector<Vec3> b = t.beam->evaluated_mesh()->positions;
    CHECK(a != b);
    t.curve_obj->set_world_position({0, 3, 0});
    const std::vector<Vec3> c = t.beam->evaluated_mesh()->positions;
    CHECK(b != c && std::fabs(c[0].y - (b[0].y + 3.0f)) < 1e-4f);
    /* A modifier naming its own object, a missing one, or a curve with one point: the mesh as it is. */
    for (const char *name : {"Beam", "Missing"}) {
      t.mod->object = name;
      CHECK(t.beam->evaluated_mesh()->positions == t.base->positions);
    }
    t.mod->object = "Path";
    t.curve->points.resize(1);
    CHECK(t.beam->evaluated_mesh()->positions == t.base->positions);
  });

  test("curves: odd points (NaN, huge, all in one place) and odd settings give a valid mesh", [] {
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    R64Scene t(r64_beam(2.0f, 0.1f, 6));
    std::vector<std::vector<CurvePoint>> sets = {
        {{Vec3(0, 0, 0), 0, 1}, {Vec3(nan, 0, 0), 0, 1}, {Vec3(2, 0, 0), nan, nan}},
        {{Vec3(1, 1, 1), 0, 1}, {Vec3(1, 1, 1), 0, 1}, {Vec3(1, 1, 1), 0, 1}},
        {{Vec3(0, 0, 0), 1e9f, 1e9f}, {Vec3(1e30f, 0, 0), 0, 1}, {Vec3(inf, 0, 0), 0, 1}},
        {{Vec3(0, 0, 0), 0, 0}, {Vec3(0, 1, 0), 0, 0}, {Vec3(0, 2, 0), 0, -5}}};
    int bad = 0;
    for (int type = 0; type < 3; type++)
      for (int closed = 0; closed < 2; closed++)
        for (int axis = 0; axis < 6; axis++)
          for (auto &pts : sets) {
            t.curve->type = type;
            t.curve->closed = closed != 0;
            t.curve->resolution = type == 0 ? 0 : 100000;
            t.curve->points = pts;
            t.mod->deform_axis = axis;
            const Mesh *m = t.beam->evaluated_mesh();
            for (const Vec3 &p : m->positions) bad += !(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z));
            bad += m->positions.size() != t.base->positions.size();
          }
    CHECK(bad == 0);
  });

  test("curves: points, type, closed, resolution and the modifiers round-trip through a scene file", [] {
    Scene s;
    GameObject *c = s.create("Rail");
    Curve *cv = c->add<Curve>();
    cv->type = 1;
    cv->closed = true;
    cv->resolution = 20;
    cv->points = {{Vec3(1, 2, 3), 15.0f, 0.5f}, {Vec3(-1, 0, 4), 0.0f, 2.0f}, {Vec3(0, 5, -2), -30.0f, 1.0f}};
    GameObject *g = s.create("Train");
    g->add<MeshFilter>()->mesh = primitives::cube(1.0f);
    auto *a = g->add<ArrayModifier>();
    a->fit_type = 2;
    a->curve = "Rail";
    auto *m = g->add<CurveModifier>();
    m->object = "Rail";
    m->deform_axis = 4;
    Scene back;
    std::string err;
    CHECK(load_scene_text(save_scene_text(s), back, err));
    const Curve *bc = back.find_by_name("Rail") ? back.find_by_name("Rail")->curve() : nullptr;
    CHECK(bc && bc->type == 1 && bc->closed && bc->resolution == 20 && bc->points.size() == 3);
    if (bc && bc->points.size() == 3)
      CHECK(bc->points[2].position.y == 5.0f && bc->points[2].tilt == -30.0f && bc->points[0].radius == 0.5f);
    GameObject *bg = back.find_by_name("Train");
    CHECK(bg && bg->get<ArrayModifier>() && bg->get<ArrayModifier>()->fit_type == 2 && bg->get<ArrayModifier>()->curve == "Rail");
    CHECK(bg && bg->get<CurveModifier>() && bg->get<CurveModifier>()->deform_axis == 4 && bg->get<CurveModifier>()->object == "Rail");
    if (bg) CHECK(bg->evaluated_mesh()->positions == g->evaluated_mesh()->positions);
    /* `set` reaches a point by its prefix, as filters' fields do. */
    Editor ed;
    ed.init_headless(400, 300);
    ed.command("create Curve: Path");
    ed.command("set Curve.P2Position 0 3 0");
    GameObject *p = ed.scene().find_by_name("Path");
    CHECK(p && p->curve() && p->curve()->points.size() == 5 && p->curve()->points[2].position.y == 3.0f);
  });

  test("curves: in the editor, moving a point or the curve re-bends the mesh with no stale picture", [] {
    Editor ed;
    r34_open(ed);
    ed.command("create Curve: Bezier");
    GameObject *beam = ed.scene().create("Beam");
    beam->add<MeshFilter>()->mesh = r64_beam(3.0f, 0.1f, 30);
    beam->add<MeshRenderer>();
    beam->add<CurveModifier>()->object = "BezierCurve";
    beam->set_world_position({-1.5f, 0.3f, 0});
    ed.mark_changed_for_test("R64 beam");
    r34_steps(ed, 3);
    ed.set_render_cache_verify(true);
    const uint64_t bad0 = Editor::render_cache_mismatches_all();
    const std::vector<Vec3> a = beam->evaluated_mesh()->positions;
    const R34Change c = r34_change(ed, [&] { ed.command("curve move BezierCurve 1 -0.5 0.8 -0.6"); }, 2);
    CHECK(c.d.renders > 0 && c.before != c.after);
    const std::vector<Vec3> b = beam->evaluated_mesh()->positions;
    CHECK(a != b);
    GameObject *cv = ed.scene().find_by_name("BezierCurve");
    for (float z : {0.2f, 0.4f, 0.6f}) {
      cv->set_world_position({0, 0, z});
      ed.mark_changed_for_test("R64 move curve");
      r34_steps(ed, 1);
    }
    r34_steps(ed, 2);
    CHECK(beam->evaluated_mesh()->positions != b);
    CHECK(Editor::render_cache_mismatches_all() == bad0);
    ed.set_render_cache_verify(false);
  });

  test("curves: clicking a curve selects it, clicking its point selects the point, the Move gizmo moves only that point, undo puts it back", [] {
    Editor ed;
    r34_open(ed);
    ed.command("create Curve: Path");
    GameObject *g = ed.scene().find_by_name("Path");
    CHECK(g && g->curve());
    if (!g || !g->curve()) return;
    g->set_world_position({0, 0.5f, 3.0f});
    ed.command("select Main Camera");  // something else selected, its gizmo far from the curve
    ed.mark_changed_for_test("R64 setup");
    r34_steps(ed, 3);
    /* The line between two points: the object is selected. */
    int x = 0, y = 0;
    CHECK(ed.project_to_window(g->world_matrix().point({0.5f, 0, 0}), x, y));
    r34_click(ed, x, y);
    r34_steps(ed, 1);
    CHECK(ed.active_object_for_test() == g);
    /* Its second point (the third sits on the origin, under the gizmo). */
    const Vec3 before[2] = {g->curve()->points[1].position, g->curve()->points[0].position};
    const Vec3 w = g->world_matrix().point(before[0]);
    CHECK(ed.project_to_window(w, x, y));
    r34_click(ed, x, y);
    r34_steps(ed, 1);
    CHECK(ed.points_selected_for_test() == std::vector<int>{1});
    CHECK(ed.active_object_for_test() == g);
    ed.command("tool move");
    r34_steps(ed, 2);
    r34_gizmo_drag_x(ed, w, 60);
    r34_steps(ed, 2);
    const Vec3 after[2] = {g->curve()->points[1].position, g->curve()->points[0].position};
    std::printf("    point 1 moved by %.3f along X\n", after[0].x - before[0].x);
    CHECK(after[0].x > before[0].x + 0.05f && std::fabs(after[0].y - before[0].y) < 1e-4f && std::fabs(after[0].z - before[0].z) < 1e-4f);
    CHECK(after[1].x == before[1].x && after[1].y == before[1].y && after[1].z == before[1].z);
    ed.command("undo");
    r34_steps(ed, 2);
    GameObject *u = ed.scene().find_by_name("Path");
    CHECK(u && u->curve() && u->curve()->points[1].position.x == before[0].x);
    /* The console selects points too. */
    ed.command("curve select Path 3 0");
    CHECK(ed.points_selected_for_test() == (std::vector<int>{3, 0}));
  });
}

static void round69_tests() {
  test("skins: at rest the mesh is unchanged; turning a bone 90 degrees turns the vertices it owns about its joint exactly", [] {
    R69Rig t;
    float rest = 0.0f;
    const Mesh *m = t.tube->evaluated_mesh();
    for (size_t i = 0; i < m->vert_count(); i++) rest = std::max(rest, length(m->positions[i] - t.base->positions[i]));
    CHECK(rest < 1e-5f);
    const Quat q = Quat::axis_angle({0, 0, 1}, kPi * 0.5f);
    t.upper->set_local_rotation(q);
    m = t.tube->evaluated_mesh();
    float off = 0.0f, still = 0.0f;
    for (size_t i = 0; i < m->vert_count(); i++) {
      const Vec3 p = t.base->positions[i];
      if (p.y >= 1.1f - 1e-4f) off = std::max(off, length(m->positions[i] - (q.rotate(p - Vec3(0, 1, 0)) + Vec3(0, 1, 0))));
      if (p.y <= 0.9f + 1e-4f) still = std::max(still, length(m->positions[i] - p));
    }
    std::printf("    upper part off by %g, lower part moved by %g\n", off, still);
    CHECK(off < 1e-4f && still < 1e-5f);
    /* Rest Position shows it unposed; moving the whole rig moves nothing relative to it (the tube isn't its child). */
    t.rig->get<Armature>()->rest_position = true;
    CHECK(t.tube->evaluated_mesh()->positions == t.base->positions);
  });

  test("skins: Dual Quaternion keeps a twisted joint's girth where Linear collapses it; both agree at rest", [] {
    R69Rig t;
    t.upper->set_local_rotation(Quat::axis_angle({0, 1, 0}, kPi * 150.0f / 180.0f));  // a twist along the bone
    const float linear = t.ring_radius(1.0f);
    t.rig->get<Armature>()->deform = 1;
    const float dq = t.ring_radius(1.0f);
    std::printf("    the joint's radius after a 150 degree twist: linear %.3f, dual quaternion %.3f (rest 0.2)\n", linear, dq);
    CHECK(linear < 0.1f && dq > 0.18f);
    t.upper->set_local_rotation(Quat());
    CHECK(std::fabs(t.ring_radius(1.0f) - 0.2f) < 1e-4f);
    /* A single bone's vertices: both methods the same. */
    t.upper->set_local_rotation(Quat::axis_angle({1, 0, 0}, 0.7f));
    const std::vector<Vec3> d = t.tube->evaluated_mesh()->positions;
    t.rig->get<Armature>()->deform = 0;
    const std::vector<Vec3> l = t.tube->evaluated_mesh()->positions;
    float off = 0.0f;
    for (size_t i = 0; i < d.size(); i++)
      if (t.base->positions[i].y > 1.15f) off = std::max(off, length(d[i] - l[i]));
    CHECK(off < 1e-4f);
  });

  test("skins: the glTF and FBX two-bone tubes import with bones, an armature, weights summing to 1, and pose like the original", [] {
    for (const char *file : {"tests/fixtures/rig_tube.glb", "tests/fixtures/rig_tube.fbx"}) {
      ImportResult r;
      CHECK(import_model(file, r));
      Scene s;
      instantiate_import(s, r, "Rig Import", nullptr);
      GameObject *lower = s.find_by_name("Lower"), *upper = s.find_by_name("Upper"), *tube = s.find_by_name("Tube");
      const bool shape = lower && upper && tube && lower->get<Bone>() && upper->get<Bone>() && upper->parent == lower && tube->get<SkinModifier>();
      std::printf("    %s: %s, %zu skins\n", file, shape ? "bones, armature and skin" : "MISSING PARTS", r.skins.size());
      CHECK(shape);
      if (!shape) continue;
      const GameObject *arm = s.find_by_name(tube->get<SkinModifier>()->object);
      CHECK(arm && arm->get<Armature>());
      const Mesh &m = *tube->mesh_filter()->mesh;
      CHECK(m.find_group("Lower") >= 0 && m.find_group("Upper") >= 0 && m.groups_fit());
      float worst = 0.0f;
      for (size_t i = 0; i < m.vert_count(); i++) {
        float sum = 0.0f;
        for (const VertexGroup &g : m.vertex_groups) sum += g.weights[i];
        worst = std::max(worst, std::fabs(sum - 1.0f));
      }
      if (!(worst < 1e-3f)) {
        std::printf("    weights off by %g; groups:", worst);
        for (const VertexGroup &g : m.vertex_groups) {
          float mx = 0.0f;
          for (float w : g.weights) mx = std::max(mx, w);
          std::printf(" %s (max %.2f)", g.name.c_str(), mx);
        }
        std::printf("; %zu vertices\n", m.vert_count());
      }
      CHECK(worst < 1e-3f);
      float rest = 0.0f;
      const Mesh *e = tube->evaluated_mesh();
      for (size_t i = 0; i < m.vert_count(); i++) rest = std::max(rest, length(e->positions[i] - m.positions[i]));
      std::printf("    at rest, moved by %g\n", rest);
      CHECK(rest < 1e-3f);
      /* Turning Upper moves its vertices; Lower's own stay. */
      upper->set_local_rotation(upper->local().rotation * Quat::axis_angle({1, 0, 0}, 1.0f));
      e = tube->evaluated_mesh();
      const std::vector<float> &uw = m.vertex_groups[(size_t)m.find_group("Upper")].weights;
      float moved_up = 0.0f, moved_low = 0.0f;
      const Mat4 &tw = tube->world_matrix();  // in metres: the FBX keeps centimetres inside a x100 scale
      for (size_t i = 0; i < m.vert_count(); i++) {
        const float d = length(tw.point(e->positions[i]) - tw.point(m.positions[i]));
        if (uw[i] > 0.99f) moved_up = std::max(moved_up, d);
        if (uw[i] == 0.0f) moved_low = std::max(moved_low, d);  // (auto weights reach a little way down)
      }
      CHECK(moved_up > 0.1f && moved_low < 1e-3f);
    }
  });

  test("skins: GameObject > Armature binds a selected mesh with automatic weights; posing is undoable and saved; Reset Pose and Apply as Rest", [] {
    Editor ed;
    ed.init_headless(800, 600);
    GameObject *col = ed.scene().create("Column");
    col->add<MeshFilter>()->mesh = primitives::cylinder(0.3f, 2.0f, 16);
    col->add<MeshRenderer>();
    ed.command("select Column");
    ed.mark_changed_for_test("R69 column");
    ed.step_frame_headless();
    ed.command("create Armature");
    GameObject *arm = ed.scene().find_by_name("Armature");
    GameObject *bone = ed.scene().find_by_name("Bone");
    CHECK(arm && arm->get<Armature>() && bone && bone->get<Bone>() && col->get<SkinModifier>());
    if (!arm || !bone || !col->get<SkinModifier>()) return;
    ed.command("bone add Bone 0.5");
    GameObject *tip = ed.scene().find_by_name("Bone.001");
    CHECK(tip && tip->parent == bone && tip->local().position.y == bone->get<Bone>()->length);
    ed.command("skin bind Column");
    const Mesh &m = *ed.scene().find_by_name("Column")->mesh_filter()->mesh;
    CHECK(m.find_group("Bone") >= 0 && m.find_group("Bone.001") >= 0);
    float worst = 0.0f;
    for (size_t i = 0; i < m.vert_count(); i++) {
      float sum = 0.0f;
      for (const VertexGroup &g : m.vertex_groups) sum += g.weights[i];
      worst = std::max(worst, std::fabs(sum - 1.0f));
    }
    CHECK(worst < 1e-4f);
    ed.step_frame_headless();
    /* Pose: one undo step; Reset Pose brings the rest back; a file keeps the pose and the rest. */
    const std::vector<Vec3> rest = ed.scene().find_by_name("Column")->evaluated_mesh()->positions;
    tip->set_local_rotation(Quat::axis_angle({1, 0, 0}, 0.8f));
    ed.mark_changed_for_test("R69 pose");
    ed.step_frame_headless();
    CHECK(ed.scene().find_by_name("Column")->evaluated_mesh()->positions != rest);
    Scene back;
    std::string err;
    CHECK(load_scene_text(save_scene_text(ed.scene()), back, err));
    CHECK(back.find_by_name("Column")->evaluated_mesh()->positions == ed.scene().find_by_name("Column")->evaluated_mesh()->positions);
    ed.command("undo");
    ed.step_frame_headless();
    CHECK(ed.scene().find_by_name("Column")->evaluated_mesh()->positions == rest);
    tip = ed.scene().find_by_name("Bone.001");
    tip->set_local_rotation(Quat::axis_angle({1, 0, 0}, 0.5f));
    ed.command("armature reset Armature");
    CHECK(ed.scene().find_by_name("Column")->evaluated_mesh()->positions == rest);
    tip->set_local_rotation(Quat::axis_angle({1, 0, 0}, 0.5f));
    ed.command("armature apply Armature");  // this pose is the rest now: the mesh shows unposed in it
    {
      /* Within rounding, not bit for bit: the bone's world matrix times its rest's inverse is the identity only up to
       * the last bit (macOS's clang fuses the multiply-adds differently and was 1 ulp off here). */
      const std::vector<Vec3> &now = ed.scene().find_by_name("Column")->evaluated_mesh()->positions;
      const std::vector<Vec3> &base = ed.scene().find_by_name("Column")->mesh_filter()->mesh->positions;
      float off = now.size() == base.size() ? 0.0f : 1e9f;
      for (size_t i = 0; i < now.size() && i < base.size(); i++) off = std::max(off, length(now[i] - base[i]));
      CHECK(off < 1e-5f);
    }
  });

  test("skins: a root bone resets, rests near 90 degrees stay exact, an unevenly scaled mesh keeps Dual Quaternion, a mesh under the armature follows it", [] {
    /* The root itself a bone (as imports often make it): Reset Pose puts it back too. */
    {
      Editor ed;
      ed.init_headless(400, 300);
      GameObject *hips = ed.scene().create("Hips");
      hips->add<Armature>();
      hips->add<Bone>();
      hips->set_local_rotation(Quat::axis_angle({0, 0, 1}, 0.7f));
      ed.command("armature reset Hips");
      const Quat q = ed.scene().find_by_name("Hips")->local().rotation;
      CHECK(std::fabs(q.w - 1.0f) < 1e-6f);
    }
    /* A rest rotation near 90 degrees about X (where Euler angles lose up to a degree): unchanged at rest. */
    {
      R69Rig t;
      const Quat near90 = Quat::euler({89.6f, 20.0f, 35.0f});
      t.upper->set_local_rotation(near90);
      t.upper->get<Bone>()->set_rest(t.upper->local());
      Scene back;
      std::string err;
      CHECK(load_scene_text(save_scene_text(t.s), back, err));  // through a file too
      const Mesh *m = back.find_by_name("Tube")->evaluated_mesh();
      float off = 0.0f;
      for (size_t i = 0; i < m->vert_count(); i++) off = std::max(off, length(m->positions[i] - t.base->positions[i]));
      std::printf("    a rest at (89.6, 20, 35) degrees, saved and loaded: moved by %g at rest\n", off);
      CHECK(off < 1e-4f);
    }
    /* An unevenly scaled mesh: Dual Quaternion still keeps the twisted joint's girth (blended in world space). */
    {
      R69Rig t;
      t.tube->set_local_scale({2.0f, 1.0f, 2.0f});
      t.rig->get<Armature>()->deform = 1;
      t.upper->set_local_rotation(Quat::axis_angle({0, 1, 0}, kPi * 150.0f / 180.0f));
      const Mesh *m = t.tube->evaluated_mesh();
      float sum = 0.0f;
      int n = 0;
      for (size_t i = 0; i < t.base->vert_count(); i++)
        if (std::fabs(t.base->positions[i].y - 1.0f) < 1e-4f) sum += length(Vec3(m->positions[i].x, 0, m->positions[i].z)), n++;
      std::printf("    scaled (2, 1, 2): the joint's local radius %.3f (rest 0.2)\n", sum / n);
      CHECK(n > 0 && sum / n > 0.18f);
    }
    /* A mesh parented under the armature follows it when the armature moves; the Skin's own vertex group limits it. */
    {
      R69Rig t;
      t.s.set_parent(t.tube, t.rig, -1, false);
      t.rig->set_world_position({3, 0, 0});
      {
        const Mesh *e = t.tube->evaluated_mesh();
        float off = 0.0f;
        for (size_t i = 0; i < e->vert_count(); i++) off = std::max(off, length(e->positions[i] - t.base->positions[i]));
        CHECK(off < 1e-4f);  // in its own space: unchanged (up to rounding through world space)
      }
      Mesh &bm = *mesh_make_mutable(t.tube->mesh_filter()->mesh);
      VertexGroup half{"Half", {}};
      for (const Vec3 &q : bm.positions) half.weights.push_back(q.x > 0.0f ? 1.0f : 0.0f);
      bm.vertex_groups.push_back(half);
      bm.touch();
      t.tube->get<SkinModifier>()->vertex_group = "Half";
      t.upper->set_local_rotation(Quat::axis_angle({0, 0, 1}, 0.5f));
      const Mesh *m = t.tube->evaluated_mesh();
      float moved_neg = 0.0f;
      for (size_t i = 0; i < bm.vert_count(); i++)
        if (bm.positions[i].x < 0.0f) moved_neg = std::max(moved_neg, length(m->positions[i] - bm.positions[i]));
      CHECK(moved_neg == 0.0f);
    }
  });

  test("skins: automatic weights give each vertex mostly to its nearest bone", [] {
    R69Rig t;
    Mesh m = *t.base;
    m.vertex_groups.clear();
    CHECK(skin_bind_automatic(*t.tube->get<SkinModifier>(), m));
    const std::vector<float> &lo = m.vertex_groups[(size_t)m.find_group("Lower")].weights;
    int right = 0, total = 0;
    for (size_t i = 0; i < m.vert_count(); i++) {
      const float y = m.positions[i].y;
      if (std::fabs(y - 1.0f) < 0.15f) continue;  // near the joint either may lead
      total++;
      right += (y < 1.0f) == (lo[i] > 0.5f);
    }
    std::printf("    %d of %d vertices mostly on their nearest bone\n", right, total);
    CHECK(total > 0 && right == total);
  });

  /* ------------------------------------------------------------------ round 70: IK and rig presets (task 0048) */
  test("IK: two bones reach a target exactly, point at one out of reach, and bend towards the pole", [] {
    R69Rig t;
    GameObject *target = t.s.create("Target");
    target->set_world_position({0.8f, 1.2f, 0.3f});
    IKConstraint *ik = t.upper->add<IKConstraint>();
    ik->target = "Target";
    CHECK(solve_constraints(t.s));
    auto tip = [&] { return t.upper->world_matrix().point({0, 1, 0}); };
    std::printf("    reachable: tip off the target by %g\n", length(tip() - target->world_position()));
    CHECK(length(tip() - target->world_position()) < 1e-4f);
    CHECK(length(t.lower->world_position()) < 1e-6f);  // the chain's root stays where it hangs
    /* Out of reach: the chain straightens towards it. */
    target->set_world_position({0.0f, 5.0f, 3.0f});
    CHECK(solve_constraints(t.s));
    const Vec3 dir = normalize(target->world_position());
    CHECK(dot(normalize(tip()), dir) > 0.9999f && std::fabs(length(tip()) - 2.0f) < 1e-3f);
    /* A pole: the knee lies in the plane of root, target and pole, on the pole's side. */
    GameObject *pole = t.s.create("Pole");
    pole->set_world_position({0.0f, 1.0f, 2.0f});
    ik->pole = "Pole";
    target->set_world_position({0.0f, 1.2f, 0.0f});
    CHECK(solve_constraints(t.s));
    const Vec3 knee = t.upper->world_position(), T = target->world_position(), P = pole->world_position();
    const Vec3 n = normalize(cross(T, P));
    std::printf("    knee off the pole plane by %g; tip off the target by %g\n", std::fabs(dot(knee, n)), length(tip() - T));
    CHECK(std::fabs(dot(knee, n)) < 1e-4f && knee.z > 0.5f && length(tip() - T) < 1e-4f);
    /* Nothing moved: nothing to solve. */
    CHECK(!solve_constraints(t.s));
    /* The skin follows the solved bones. */
    const Mesh *m = t.tube->evaluated_mesh();
    CHECK(length(m->positions.back() - t.base->positions.back()) > 0.5f);
  });

  test("IK: five bones converge on a target by CCD; weight blends with the pose; Reset Pose solves again", [] {
    Scene s;
    GameObject *root = s.create("Rig");
    root->add<Armature>();
    GameObject *parent = root, *end = nullptr;
    for (int k = 0; k < 5; k++) {
      GameObject *b = s.create(strprintf("B%d", k), parent);
      if (k) b->set_local_position({0, 0.5f, 0});
      b->add<Bone>()->length = 0.5f;
      b->get<Bone>()->set_rest(b->local());
      parent = end = b;
    }
    GameObject *target = s.create("Target");
    target->set_world_position({1.0f, 1.2f, 0.5f});
    IKConstraint *ik = end->add<IKConstraint>();
    ik->target = "Target";
    ik->chain_length = 5;
    ik->iterations = 100;
    CHECK(solve_constraints(s));
    const float err = length(end->world_matrix().point({0, 0.5f, 0}) - target->world_position());
    std::printf("    5 bones: tip off the target by %g\n", err);
    CHECK(err < 1e-3f);
    CHECK(length(s.find_by_name("B0")->world_position()) < 1e-6f);
    /* Reset Pose turns the bones back; the next solve puts them on the target again. */
    for (int k = 0; k < 5; k++) s.find_by_name(strprintf("B%d", k))->set_local_rotation(Quat());
    CHECK(solve_constraints(s));
    CHECK(length(end->world_matrix().point({0, 0.5f, 0}) - target->world_position()) < 1e-3f);
    /* Weight 0: the pose as it was. */
    for (int k = 0; k < 5; k++) s.find_by_name(strprintf("B%d", k))->set_local_rotation(Quat());
    ik->weight = 0.0f;
    solve_constraints(s);
    for (int k = 0; k < 5; k++) CHECK(std::fabs(s.find_by_name(strprintf("B%d", k))->local().rotation.w - 1.0f) < 1e-6f);
    /* A target the chain carries (a child of its bone) is refused rather than chased. */
    s.set_parent(target, s.find_by_name("B2"), -1, true);
    ik->weight = 1.0f;
    CHECK(!solve_constraints(s));
    /* Driven bones: the chain only. */
    CHECK(ik_drives(s, *s.find_by_name("B0")) && ik_drives(s, *end) && !ik_drives(s, *root));
  });

  test("IK: rig presets make the named bones with IK on the limbs; auto IK finds Mixamo, Unity and Rigify limbs", [] {
    Scene s;
    GameObject *h = create_rig(s, false, {});
    int iks = 0;
    s.for_each([&](GameObject &g) { iks += g.get<IKConstraint>() != nullptr; });
    CHECK(h && h->get<Armature>() && iks == 4);
    for (const char *n : {"Hips", "Spine", "Chest", "Neck", "Head", "LeftUpperLeg", "RightLowerLeg", "LeftHand", "RightFoot", "LeftFootIK", "RightElbowPole"})
      CHECK(s.find_by_name(n) && (std::strstr(n, "IK") || std::strstr(n, "Pole") || s.find_by_name(n)->get<Bone>()));
    /* Lifting a foot target bends the knee forward (the pole's side) and the foot stays on it. */
    GameObject *ft = s.find_by_name("LeftFootIK");
    ft->set_world_position(ft->world_position() + Vec3(0, 0.3f, 0.1f));
    CHECK(solve_constraints(s));
    GameObject *shin = s.find_by_name("LeftLowerLeg");
    const float err = length(shin->world_matrix().point({0, shin->get<Bone>()->length, 0}) - ft->world_position());
    std::printf("    humanoid: foot off its lifted target by %g, knee forward by %.3f\n", err, shin->world_position().z);
    CHECK(err < 1e-4f && shin->world_position().z > 0.05f);
    Scene q;
    create_rig(q, true, {});
    iks = 0;
    q.for_each([&](GameObject &g) { iks += g.get<IKConstraint>() != nullptr; });
    CHECK(iks == 4 && q.find_by_name("Tail") && q.find_by_name("HindLeftFootIK"));
    CHECK(create_rig(q, true, {})->name == "Quadruped.001");
    /* Auto IK by names: Mixamo, Unity and Rigify skeletons get four limbs each; a spine or a hand matches nothing. */
    const char *sets[3][8] = {
        {"mixamorig:LeftUpLeg", "mixamorig:LeftLeg", "mixamorig:RightUpLeg", "mixamorig:RightLeg", "mixamorig:LeftArm", "mixamorig:LeftForeArm",
         "mixamorig:RightArm", "mixamorig:RightForeArm"},
        {"LeftUpperLeg", "LeftLowerLeg", "RightUpperLeg", "RightLowerLeg", "LeftUpperArm", "LeftLowerArm", "RightUpperArm", "RightLowerArm"},
        {"thigh.L", "shin.L", "thigh.R", "shin.R", "upper_arm.L", "forearm.L", "upper_arm.R", "forearm.R"}};
    for (auto &names : sets) {
      Scene k;
      GameObject *root = k.create("Armature");
      root->add<Armature>();
      GameObject *hips = k.create("mixamorig:Hips", root);
      hips->add<Bone>();
      GameObject *spine = k.create("Spine", hips);
      spine->add<Bone>();
      for (int limb = 0; limb < 4; limb++) {
        GameObject *up = k.create(names[limb * 2], limb < 2 ? hips : spine);
        up->set_local_position({limb % 2 ? 0.2f : -0.2f, 0, 0});
        up->set_local_rotation(Quat::axis_angle({0, 0, 1}, kPi));
        up->add<Bone>()->length = 0.4f;
        GameObject *lo = k.create(names[limb * 2 + 1], up);
        lo->set_local_position({0, 0.4f, 0});
        lo->add<Bone>()->length = 0.4f;
        k.create(limb < 2 ? "LeftHand" : "LeftFoot", lo)->add<Bone>();
      }
      const int n = add_humanoid_ik(k, root);
      std::printf("    %s...: %d limbs\n", names[0], n);
      CHECK(n == 4);
      CHECK(k.find_by_name(names[1])->get<IKConstraint>() && !k.find_by_name(names[0])->get<IKConstraint>());
      CHECK(add_humanoid_ik(k, root) == 0);  // twice: nothing more
    }
  });

  test("IK: the console adds IK and presets; moving the target with the gizmo poses the leg, a driven bone refuses the gizmo, undo", [] {
    Editor ed;
    r34_open(ed);
    ed.command("rig humanoid");
    GameObject *rig = ed.scene().find_by_name("Humanoid");
    CHECK(rig && ed.active_object_for_test() == rig);
    rig->set_world_position({0, 0, 3.0f});
    ed.command("camera 0 1.2 9 0 0.8 3");  // facing the rig: the Move gizmo's X arrow points right on screen
    ed.mark_changed_for_test("R70 setup");
    r34_steps(ed, 2);
    GameObject *ft = ed.scene().find_by_name("RightFootIK");
    GameObject *shin = ed.scene().find_by_name("RightLowerLeg");
    auto foot_err = [&] { return length(shin->world_matrix().point({0, shin->get<Bone>()->length, 0}) - ft->world_position()); };
    CHECK(foot_err() < 1e-4f);  // the frame solved after the rig moved
    ft->set_world_position(ft->world_position() + Vec3(0, 0.25f, 0));  // a straight leg at rest: lift the foot so a sideways move stays in reach
    ed.mark_changed_for_test("R70 lift");
    r34_steps(ed, 1);
    CHECK(foot_err() < 1e-4f);
    ed.command("select RightFootIK");
    ed.command("tool move");
    r34_steps(ed, 2);
    const Vec3 before = ft->world_position();
    r34_gizmo_drag_x(ed, before, 8);  // about 0.2 m: still in the leg's reach
    r34_steps(ed, 2);
    std::printf("    target moved by %.3f; foot off it by %g\n", ft->world_position().x - before.x, foot_err());
    CHECK(ft->world_position().x > before.x + 0.02f && foot_err() < 1e-4f);
    /* The shin is turned by IK: the gizmo refuses it. */
    ed.command("select RightLowerLeg");
    r34_steps(ed, 2);
    const Vec3 shin_at = shin->world_position();
    const Quat q0 = shin->local().rotation;
    r34_gizmo_drag_x(ed, shin_at, 40);
    r34_steps(ed, 2);
    CHECK(length(shin->world_position() - shin_at) < 1e-5f && std::fabs(shin->local().rotation.x * q0.x + shin->local().rotation.y * q0.y + shin->local().rotation.z * q0.z + shin->local().rotation.w * q0.w) > 0.99999f);
    /* Undo the target's move: the leg follows it back. */
    ed.command("undo");
    r34_steps(ed, 2);
    ft = ed.scene().find_by_name("RightFootIK");
    shin = ed.scene().find_by_name("RightLowerLeg");
    CHECK(ft && shin && std::fabs(ft->world_position().x - before.x) < 1e-5f && foot_err() < 1e-4f);
    /* ik add on a plain skeleton, through a scene file. */
    ed.command("create Armature");
    ed.command("bone add Bone 1");
    ed.command("create Empty");
    GameObject *e = ed.scene().find_by_name("GameObject");
    CHECK(e != nullptr);
    if (!e) return;
    e->set_world_position({1.0f, 1.0f, 0.0f});
    ed.command("ik add Bone.001 GameObject - 2");
    GameObject *b1 = ed.scene().find_by_name("Bone.001");
    CHECK(b1 && b1->get<IKConstraint>() && b1->get<IKConstraint>()->target == "GameObject");
    Scene back;
    std::string err;
    CHECK(load_scene_text(save_scene_text(ed.scene()), back, err));
    const IKConstraint *ik = back.find_by_name("Bone.001") ? back.find_by_name("Bone.001")->get<IKConstraint>() : nullptr;
    CHECK(ik && ik->target == "GameObject" && ik->chain_length == 2 && ik->pole.empty());
  });

  test("IK: in Play a Rotator swinging a foot target animates the leg; Stop puts the pose back", [] {
    Editor ed;
    r34_open(ed);
    ed.command("rig humanoid");
    Scene &s0 = ed.scene();
    GameObject *ft = s0.find_by_name("RightFootIK");
    const Vec3 foot = ft->world_position();
    GameObject *spin = s0.create("Spin");
    spin->set_world_position(foot + Vec3(0, 0.25f, 0));
    spin->add<Rotator>()->degrees_per_second = {0, 3000, 0};
    s0.set_parent(ft, spin, -1, true);
    ft->set_world_position(foot + Vec3(0.15f, 0.25f, 0));  // a circle the leg reaches all the way round
    ed.mark_changed_for_test("R70 play setup");
    r34_steps(ed, 2);
    const Quat rest = s0.find_by_name("RightLowerLeg")->local().rotation;
    ed.command("play");
    /* Play runs on the clock: a fast machine's frames swing the target less each, so step until it has moved (CI's
     * runner once swung it 0.009 in 6 frames). */
    GameObject *pft = nullptr;
    Vec3 off;
    for (int i = 0; i < 200 && length(off) <= 0.01f; i++) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));  // (some real time each frame, however fast the machine)
      r34_steps(ed, 1);
      pft = ed.scene().find_by_name("RightFootIK");
      off = pft->world_position() - (foot + Vec3(0.15f, 0.25f, 0));
    }
    GameObject *shin = ed.scene().find_by_name("RightLowerLeg");
    const float err = length(shin->world_matrix().point({0, shin->get<Bone>()->length, 0}) - pft->world_position());
    std::printf("    in Play: the target swung by %.3f, the foot off it by %g\n", length(off), err);
    CHECK(length(off) > 0.01f && err < 1e-4f);
    ed.command("stop");
    r34_steps(ed, 2);
    const Quat q = ed.scene().find_by_name("RightLowerLeg")->local().rotation;
    CHECK(std::fabs(q.x * rest.x + q.y * rest.y + q.z * rest.z + q.w * rest.w) > 0.99999f);
  });

  test("IK: a second rig has its own targets; a target that circles back leaves no twist; Weight 0.5 doesn't add up; a pole the chain carries is refused", [] {
    /* Two humanoids: moving the second's foot target poses the second's leg, not the first's. */
    {
      Scene s;
      create_rig(s, false, {});
      GameObject *r2 = create_rig(s, false, {2.0f, 0, 0});
      CHECK(r2->name == "Humanoid.001" && s.find_by_name("LeftFootIK.001"));
      GameObject *shin2 = nullptr;
      s.for_each([&](GameObject &g) {  // the bones keep their names: the second rig's shin is the one under it
        if (!shin2 && g.name == "LeftLowerLeg" && s.is_ancestor(r2, &g)) shin2 = &g;
      });
      GameObject *t2 = s.find_by_name("LeftFootIK.001");
      CHECK(shin2 && t2 && shin2->get<IKConstraint>()->target == "LeftFootIK.001");
      t2->set_world_position(t2->world_position() + Vec3(0, 0.3f, 0));
      solve_constraints(s);
      CHECK(length(shin2->world_matrix().point({0, shin2->get<Bone>()->length, 0}) - t2->world_position()) < 1e-4f);
    }
    /* A target that goes round a circle and back: the same rotations as before the circle (no wound-up twist). */
    {
      R69Rig t;
      GameObject *target = t.s.create("Target");
      target->set_world_position({0.6f, 1.4f, 0.0f});
      t.upper->add<IKConstraint>()->target = "Target";
      solve_constraints(t.s);
      const Quat a0 = t.lower->local().rotation, b0 = t.upper->local().rotation;
      for (int k = 1; k <= 24; k++) {
        const float ang = k * 2.0f * kPi / 24.0f;
        target->set_world_position({0.6f * std::cos(ang), 1.4f, 0.6f * std::sin(ang)});
        solve_constraints(t.s);
      }
      const Quat a1 = t.lower->local().rotation, b1 = t.upper->local().rotation;
      auto same = [](Quat p, Quat q) { return std::fabs(p.x * q.x + p.y * q.y + p.z * q.z + p.w * q.w) > 0.99999f; };
      CHECK(same(a0, a1) && same(b0, b1));
      /* Weight 0.5: the same pose however many times the target moved on the way. */
      IKConstraint *ik = t.upper->get<IKConstraint>();
      ik->weight = 0.5f;
      target->set_world_position({0.5f, 1.0f, 0.2f});
      solve_constraints(t.s);
      const Quat once = t.lower->local().rotation;
      for (int k = 0; k < 10; k++) {
        target->set_world_position({0.5f + 0.01f * k, 1.0f, 0.2f});
        solve_constraints(t.s);
      }
      target->set_world_position({0.5f, 1.0f, 0.2f});
      solve_constraints(t.s);
      CHECK(same(once, t.lower->local().rotation));
      /* A pole carried by the chain would move with every solve: refused, so the editor goes idle. */
      ik->weight = 1.0f;
      GameObject *pole = t.s.create("Pole", t.lower);
      pole->set_local_position({0, 0.5f, 1.0f});
      ik->pole = "Pole";
      CHECK(!solve_constraints(t.s) && !solve_constraints(t.s));
    }
    /* A constraint whose target is gone doesn't hold the bone (the gizmo can turn it again). */
    {
      R69Rig t;
      t.upper->add<IKConstraint>()->target = "Nowhere";
      CHECK(!ik_drives(t.s, *t.upper));
    }
  });

  test("IK: G / R on a bone turned by IK is refused, on a free bone it turns it", [] {
    Editor ed;
    ed.init_headless(1000, 700);
    ed.command("rig humanoid");
    ed.step_frame_headless();
    Recti r = ed.scene_view_rect();
    const int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
    auto key = [&](int k) {
      platform::Event m, d, u;
      m.type = platform::EventType::MouseMove;
      m.x = d.x = u.x = cx + 40;
      m.y = d.y = u.y = cy;
      d.type = platform::EventType::KeyDown;
      d.key = u.key = k;
      u.type = platform::EventType::KeyUp;
      ed.step_frame_headless({m, d, u});
    };
    auto type = [&](const char *s) {
      for (const char *c = s; *c; c++) {
        platform::Event t;
        t.type = platform::EventType::Text;
        t.codepoint = (uint32_t)*c;
        t.x = cx + 40;
        t.y = cy;
        ed.step_frame_headless({t});
      }
    };
    /* The head has no IK: G, R (rotate), X, 30, Enter turns it, FK as before. */
    ed.command("select Head");
    ed.step_frame_headless();
    const Quat q0 = ed.scene().find_by_name("Head")->local().rotation;
    key(platform::KEY_G);
    CHECK(ed.transform_active_for_test());
    key(platform::KEY_R);
    key(platform::KEY_X);
    type("30");
    key(platform::KEY_ENTER);
    ed.step_frame_headless();
    const Quat q = ed.scene().find_by_name("Head")->local().rotation;
    CHECK(std::fabs(q.x * q0.x + q.y * q0.y + q.z * q0.z + q.w * q0.w) < 0.999f);
    /* IK drives the shin: G starts nothing (IK would only turn it back, after an undo step that changes nothing). */
    ed.command("select LeftLowerLeg");
    ed.step_frame_headless();
    key(platform::KEY_G);
    CHECK(!ed.transform_active_for_test());
    key(platform::KEY_ESCAPE);
    CHECK(ed.scene().find_by_name("LeftLowerLeg") != nullptr);
  });
  /* ------------------------------------------------------------------ round 72: the minimum machine (task 0050) */
  test("min spec: a thread ceiling holds every parallel loop to that many threads, and lifts again", [] {
    JobSystem &js = JobSystem::global();
    const int all = js.thread_count();
    auto distinct = [&] {
      std::mutex m;
      std::set<std::thread::id> ids;
      js.parallel_for(4096, 1, [&](int64_t a, int64_t b) {
        volatile double x = 0;
        for (int64_t i = a; i < b; i++)
          for (int k = 0; k < 2000; k++) x = x + std::sqrt((double)(i + k));
        std::lock_guard<std::mutex> lock(m);
        ids.insert(std::this_thread::get_id());
      });
      return (int)ids.size();
    };
    js.set_thread_limit(3);
    const int capped = distinct();
    std::printf("    ceiling 3: %d threads ran the loop (the machine has %d), thread_count() says %d\n", capped, all, js.thread_count());
    CHECK(capped <= 3 && js.thread_count() <= 3);
    js.set_thread_limit(0);
    CHECK(js.thread_count() == all);
    /* Lifted: more than the old ceiling again. How many threads join one short loop is up to the scheduler (on a
     * 4-core CI runner all four must), so take the best of a few tries rather than one (it failed CI on PRs #49, #50). */
    int lifted = 0;
    for (int tries = 0; tries < 8 && lifted <= 3; tries++) lifted = std::max(lifted, distinct());
    if (all >= 4) CHECK(lifted > 3);
    /* The dependency-free build's own pool (CI's): its workers already started, the ceiling still holds. */
    const JobSystem::Backend was = js.backend();
    js.set_backend(JobSystem::Backend::Builtin);
    (void)distinct();  // the workers start
    js.set_thread_limit(3);
    const int pool = distinct();
    js.set_thread_limit(0);
    js.set_backend(was);
    std::printf("    the built-in pool under a ceiling of 3: %d threads\n", pool);
    CHECK(pool <= 3);
  });

  test("min spec: Viewport Resolution at 50 % draws a quarter of the pixels, keeps clicks exact, and is part of the views' picture keys", [] {
    Editor ed;
    r34_open(ed);
    GameObject *cube = ed.scene().find_by_name("Cube");  // the scene's own cube
    CHECK(cube != nullptr);
    if (!cube) return;
    ed.command("select Main Camera");
    ed.mark_changed_for_test("R72 setup");
    r34_steps(ed, 3);
    const Recti view = ed.scene_view_rect();
    const R34Count before = r34_totals(ed);
    ed.command("viewscale 50");
    CHECK(ed.quality_tier() == Editor::TierCustom && std::fabs(ed.view_scale() - 0.5f) < 1e-6f);
    r34_steps(ed, 2);
    CHECK(r34_totals(ed).renders > before.renders);  // a new picture: the scale is in the key, the old one isn't reused
    const int sw = ed.scaled_scene_width_for_test(), sh = ed.scaled_scene_height_for_test();
    std::printf("    Scene view %d x %d drawn at %d x %d\n", view.w, view.h, sw, sh);
    CHECK(std::abs(sw - view.w / 2) <= 1 && std::abs(sh - view.h / 2) <= 1);
    /* Clicking the cube still selects it: the ids are scaled up to the full view. */
    int x = 0, y = 0;
    CHECK(ed.project_to_window(cube->world_position(), x, y));
    r34_click(ed, x, y);
    r34_steps(ed, 1);
    CHECK(ed.active_object_for_test() == cube);
    /* Back to 100 %: drawn at full size again. */
    ed.command("viewscale 100");
    const R34Count b2 = r34_totals(ed);
    r34_steps(ed, 2);
    CHECK(r34_totals(ed).renders > b2.renders);
  });

  test("min spec: the quality tiers set the views' scale, shadow map and frame rate; F12 keeps the scene's shadow map", [] {
    Editor ed;
    r34_open(ed);
    ed.command("set Render.ShadowResolution 2048");
    ed.command("quality low");
    CHECK(ed.quality_tier() == Editor::TierLow && std::fabs(ed.view_scale() - 0.67f) < 1e-6f && ed.max_fps() == 60);
    CHECK(ed.shadow_resolution_for_view() == 1024);
    r34_steps(ed, 2);
    CHECK(ed.shadow_map_size_for_test() == 1024);
    ed.command("render");
    r34_steps(ed, 2);
    std::printf("    Low: views at %.0f %%, their shadow map %d; F12's %d\n", ed.view_scale() * 100.0f, 1024, ed.shadow_map_size_for_test());
    CHECK(ed.shadow_map_size_for_test() == 2048);  // a render isn't a view: the scene's own setting
    ed.command("quality high");
    CHECK(ed.view_scale() == 1.0f && ed.shadow_resolution_for_view() == 2048);
    ed.command("quality medium");
    CHECK(std::fabs(ed.view_scale() - 0.85f) < 1e-6f);
    /* Two tiers at the same scale but different caps are different pictures: Custom at 85 % (no caps) -> Medium. */
    ed.command("set Render.ShadowResolution 4096");
    ed.command("quality high");
    ed.command("viewscale 85");
    r34_steps(ed, 2);
    CHECK(ed.shadow_map_size_for_test() == 4096);
    const R34Count t0 = r34_totals(ed);
    ed.command("quality medium");
    r34_steps(ed, 2);
    CHECK(r34_totals(ed).renders > t0.renders && ed.shadow_map_size_for_test() == 2048);
    const int rec = Editor::recommended_tier();
    CHECK(rec == Editor::TierLow || rec == Editor::TierHigh);
  });

  test("min spec: a moving caster is drawn over the still casters' cached shadow map, the same map bit for bit", [] {
    /* Raster level: still casters then the moving one over them == all at once. */
    std::vector<DrawItem> all;
    MeshPtr box = primitives::cube();
    for (int k = 0; k < 4; k++) {
      DrawItem it;
      it.mesh = &box->render_mesh();
      it.model = Mat4::trs({k * 1.5f - 2.0f, 0.5f + 0.3f * k, (k % 2) * 0.7f}, Quat::axis_angle({0, 1, 0}, 0.3f * k), {1, 1, 1});
      it.id = (uint32_t)(k + 1);
      all.push_back(it);
    }
    AABB b;
    for (const DrawItem &it : all) b.add(it.mesh->bounds.transformed(it.model));
    const Vec3 dir = normalize(Vec3(-0.4f, -1.0f, -0.3f));
    ShadowMap whole, still, composed;
    render_shadow_map(whole, all, dir, b, 512);
    render_shadow_map(still, std::vector<DrawItem>(all.begin(), all.begin() + 3), dir, b, 512);
    render_shadow_map(composed, std::vector<DrawItem>(all.begin() + 3, all.end()), dir, b, 512, &still);
    CHECK(whole.valid() && composed.valid() && whole.depth == composed.depth);
    /* Editor level: dragging one object draws the still layer once, then only the moving object. */
    Editor ed;
    r34_open(ed);
    ed.command("stress 20 Cube");
    r34_steps(ed, 3);
    GameObject *mover = ed.scene().find_by_name("Cube");
    CHECK(mover != nullptr);
    if (!mover) return;
    const uint64_t layer0 = ed.shadow_layer_renders_for_test();
    for (int k = 1; k <= 6; k++) {
      mover->set_local_position({0.05f * k, 0.5f, 0.0f});
      r34_steps(ed, 1);
    }
    const uint64_t layers = ed.shadow_layer_renders_for_test() - layer0;
    std::printf("    6 moved frames: the still layer drawn %llu time(s)\n", (unsigned long long)layers);
    CHECK(layers >= 1 && layers <= 2);
  });

  test("min spec: the sky is drawn only where nothing opaque is, matching a per-pixel evaluation within a display step", [] {
    Environment env;
    env.mode = Environment::Gradient;
    env.sky = {0.2f, 0.4f, 0.9f};
    env.equator = {0.8f, 0.8f, 0.85f};
    env.ground = {0.3f, 0.25f, 0.2f};
    env.strength = 1.0f;
    Image img;
    img.resize(160, 90);
    RenderTarget rt;
    rt.attach(img, {0, 0, 160, 90});
    Renderer3D r;
    const Mat4 v = Mat4::look_at({0, 1, 5}, {0, 1, 0}, {0, 1, 0}), p = Mat4::perspective(1.0f, 160.0f / 90.0f, 0.1f, 100.0f);
    LightingEnv le;
    RasterOptions opt;
    opt.shade = ShadeMode::Deferred;
    r.begin(&rt, v, p, le, opt);
    const Mat4 inv = (p * v).inverse();
    r.clear_environment(inv, env, ViewTransform::Standard, 0.0f);
    MeshPtr box = primitives::cube();
    DrawItem it;
    it.mesh = &box->render_mesh();
    it.model = Mat4::trs({0, 1, 0}, Quat(), {1, 1, 1});
    it.id = 7;
    r.add(it);
    r.flush();
    int worst = 0, sky = 0, covered = 0;
    for (int y = 0; y < 90; y++)
      for (int x = 0; x < 160; x++) {
        if (rt.depth[(size_t)y * 160 + x] < 1.0f) {
          covered++;
          continue;
        }
        sky++;
        const float nx = 2.0f * (x + 0.5f) / 160 - 1.0f, ny = 1.0f - 2.0f * (y + 0.5f) / 90;
        const Vec4 a = inv * Vec4(nx, ny, 0, 1), c = inv * Vec4(nx, ny, 1, 1);
        const Vec3 d = normalize(c.xyz() / c.w - a.xyz() / a.w);
        const uint32_t want = to_display_pixel(env.radiance(d), ViewTransform::Standard, 0.0f), got = img.pixels[(size_t)y * 160 + x];
        for (int s = 0; s < 24; s += 8) worst = std::max(worst, std::abs((int)(want >> s & 255) - (int)(got >> s & 255)));
      }
    std::printf("    %d sky pixels, %d covered; worst channel difference %d\n", sky, covered, worst);
    CHECK(sky > 1000 && covered > 300 && worst <= 1);
  });

  test("min spec: the sky's image is found by polynomial atan2 / asin within 1e-5 of the library's", [] {
    float worst = 0.0f;
    for (int i = 0; i < 20000; i++) {
      const float t = i * 0.000314159f * 2.0f, ph = std::sin(i * 0.37f) * 1.5f;
      const Vec3 d = normalize(Vec3(std::cos(t) * std::cos(ph), std::sin(ph), std::sin(t) * std::cos(ph)));
      const Vec2 uv = dir_to_equirect(d, 0.0f);
      float u = 0.5f + std::atan2(d.x, d.z) / (2.0f * kPi);
      u -= std::floor(u);
      const float v = 0.5f + std::asin(std::max(-1.0f, std::min(1.0f, d.y))) / kPi;
      float du = std::fabs(uv.x - u);
      du = std::min(du, 1.0f - du);  // the seam
      worst = std::max(worst, std::max(du, std::fabs(uv.y - v)));
    }
    std::printf("    worst difference %g of the image\n", worst);
    CHECK(worst < 1e-5f);
  });

  test("min spec: the texture cache lets go of the least recently used textures nothing holds once past its budget", [] {
    const std::string dir = fs::join(test_dir(), "r72_textures");
    fs::make_dirs(dir);
    std::vector<uint32_t> px(64 * 64, 0xFF336699u);
    const std::string a = fs::join(dir, "a.png"), b = fs::join(dir, "b.png"), c = fs::join(dir, "c.png");
    CHECK(write_png(a, px.data(), 64, 64, 64) && write_png(b, px.data(), 64, 64, 64) && write_png(c, px.data(), 64, 64, 64));
    texture_cache_clear();
    const size_t old_budget = texture_cache_budget();
    TexturePtr held = texture_load(a, true);
    CHECK(held != nullptr);
    if (!held) return;
    texture_cache_set_budget(held->memory_bytes() + 16);  // room for about one
    (void)texture_load(b, true);  // nothing holds b after this line
    (void)texture_load(c, true);
    std::printf("    budget %zu bytes: %zu cached, %zu bytes\n", texture_cache_budget(), texture_cache_size(), texture_cache_bytes());
    CHECK(texture_cache_size() <= 2);              // b went: the oldest that nothing held
    CHECK(texture_load(a, true) == held);          // the held one stayed (and is the same texture)
    CHECK(texture_load(b, true) != nullptr);       // an evicted one loads again
    texture_cache_set_budget(old_budget);
    texture_cache_clear();
  });

  test("min spec: undo keeps its newest steps within its memory budget and they still undo", [] {
    Editor ed;
    ed.init_headless(800, 600);
    ed.command("stress 300 Cube");
    ed.step_frame_headless();
    ed.set_undo_budget_for_test(600 * 300 * 3);  // about three steps of a 300-object scene
    GameObject *g = ed.scene().find_by_name("Cube");
    CHECK(g != nullptr);
    if (!g) return;
    for (int k = 1; k <= 12; k++) {
      ed.scene().find_by_name("Cube")->set_local_position({0.1f * k, 0, 0});
      ed.mark_changed_for_test("R72 move");
      ed.step_frame_headless();
    }
    const size_t steps = ed.undo_steps_for_test();
    std::printf("    12 moves: %zu steps kept, %zu bytes of their own\n", steps, ed.undo_bytes());
    CHECK(steps >= 1 && steps <= 4);
    ed.command("undo");
    ed.step_frame_headless();
    CHECK(std::fabs(ed.scene().find_by_name("Cube")->local().position.x - 1.1f) < 1e-5f);
  });
  /* ------------------------------------------------------------------ round 73: the minimum machine's GPU (task 0051) */
  test("min spec GPU: a scene too big for a card's memory goes with smaller textures, and to the CPU when even that won't fit", [] {
    const std::string png = fs::join(test_dir(), "r73_big.png");
    std::vector<uint32_t> px((size_t)2048 * 2048);
    for (size_t i = 0; i < px.size(); i++) px[i] = 0xFF000000u | (uint32_t)(i * 2654435761u >> 8);
    CHECK(write_png(png, px.data(), 2048, 2048, 2048));
    auto quad = primitives::quad(2.0f);
    std::vector<MaterialPtr> mats{make_material("Big", {1, 1, 1})};
    mats[0]->base_map.path = png;
    Environment env;
    env.mode = Environment::Color;
    PathTracer pt;
    pt.build({{&quad->render_mesh_tangents(), Mat4::identity(), &mats}}, {}, env);
    pt.set_camera(Mat4::look_at({0, 0, -2}, {0, 0, 0}, {0, 1, 0}), Mat4::perspective(40 * kDeg2Rad, 1, 0.1f, 10), 16, 16);
    gpu::Scene whole, fitted;
    int skipped = -1;
    CHECK(pt.build_gpu_scene_fit(whole, 0, &skipped) && skipped == 0);  // no limit known: whole
    CHECK(whole.textures.size() == 1 && whole.textures[0].w == 2048);
    CHECK(pt.build_gpu_scene_fit(fitted, 12, &skipped));  // 12 MB: a 2048 texture (16 MB) doesn't fit, 1024 (4 MB) does
    std::printf("    12 MB card: %d level(s) smaller, the texture %u x %u\n", skipped, fitted.textures.empty() ? 0u : fitted.textures[0].w,
                fitted.textures.empty() ? 0u : fitted.textures[0].h);
    CHECK(skipped == 1 && fitted.textures.size() == 1 && fitted.textures[0].w == 1024);
    /* A frame whose buffers alone pass the card's memory: no texture size helps, the CPU takes it. */
    pt.set_camera(Mat4::look_at({0, 0, -2}, {0, 0, 0}, {0, 1, 0}), Mat4::perspective(40 * kDeg2Rad, 1, 0.1f, 10), 1024, 1024);
    gpu::Scene none;
    CHECK(!pt.build_gpu_scene_fit(none, 16, &skipped));
  });

  test("min spec GPU: Render > Device offers GPU + CPU (Auto), which renders on both by speed", [] {
    Editor ed;
    ed.init_headless(400, 300);
    ed.command("set Render.Device 2");
    CHECK(ed.scene().render.device == 2);
    Scene back;
    std::string err;
    CHECK(load_scene_text(save_scene_text(ed.scene()), back, err) && back.render.device == 2);
    const PTSettings s = ed.pt_settings_for_test();
    if (gpu::available()) CHECK(s.use_cpu && !s.gpus.empty());
    else CHECK(s.gpus.empty());  // no GPU: the CPU alone, quietly
  });
  test("min spec GPU: a frame too big for the card renders on the CPU with a message; the next scene that fits goes back to the GPU", [] {
    if (!gpu::available()) {
      std::printf("    (no GPU here: skipped)\n");
      return;
    }
    auto quad = primitives::quad(2.0f);
    std::vector<MaterialPtr> mats{make_material("M", {0.8f, 0.8f, 0.8f})};
    Environment env;
    env.mode = Environment::Color;
    PathTracer pt;
    PTSettings st;
    st.gpus = {0};
    st.use_cpu = false;
    st.denoise = false;
    pt.set_settings(st);
    set_env("BLENDITY_GPU_VRAM_MB", "16");  // a 16 MB card: a 1024 x 1024 frame's buffers alone take 64 MB
    pt.build({{&quad->render_mesh_tangents(), Mat4::identity(), &mats}}, {}, env);
    pt.set_camera(Mat4::look_at({0, 0, -2}, {0, 0, 0}, {0, 1, 0}), Mat4::perspective(40 * kDeg2Rad, 1, 0.1f, 10), 1024, 1024);
    pt.render(1e9, 1);
    std::printf("    16 MB card, 1024 x 1024: \"%s\", parked %d\n", pt.gpu_error().c_str(), (int)pt.gpu_parked());
    CHECK(pt.gpu_parked() && pt.gpu_error().find("16 MB") != std::string::npos && pt.stats().gpu_samples.empty());
    /* A small frame and the scene built again: the same devices, on the GPU. */
    pt.set_camera(Mat4::look_at({0, 0, -2}, {0, 0, 0}, {0, 1, 0}), Mat4::perspective(40 * kDeg2Rad, 1, 0.1f, 10), 32, 32);
    pt.build({{&quad->render_mesh_tangents(), Mat4::identity(), &mats}}, {}, env);
    pt.render(1e9, 4);
    set_env("BLENDITY_GPU_VRAM_MB", "0");
    CHECK(!pt.gpu_parked() && !pt.stats().gpu_samples.empty() && pt.stats().gpu_samples[0] > 0);
  });

  test("min spec GPU: the console sets Device Auto", [] {
    Editor ed;
    ed.init_headless(400, 300);
    ed.command("device auto");
    CHECK(ed.scene().render.device == 2);
    ed.command("device cpu");
    CHECK(ed.scene().render.device == 0);
  });
  /* ------------------------------------------------------------------ round 74: the Rigging window (task 0052) */
  test("rigging window: Window > Rigging opens beside the Inspector, its buttons add bones, reset and apply the pose, add IK", [] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.record_buttons_for_test(true);
    ed.command("window Rigging");
    ed.command("create Armature");
    ed.step_frame_headless();
    ed.step_frame_headless();
    CHECK(ed.window_rect_for_test(WindowKind::Rigging).w > 0);
    auto click = [&](const char *label) {
      ed.step_frame_headless();
      Recti b;
      if (!ed.button_rect_for_test(label, b)) {
        std::printf("    no button '%s'\n", label);
        return false;
      }
      r34_click(ed, b.x + b.w / 2, b.y + b.h / 2);
      ed.step_frame_headless();
      return true;
    };
    GameObject *bone = ed.scene().find_by_name("Bone");
    CHECK(bone && ed.active_object_for_test() == bone);
    /* Add Bone: a child at the tip of the selected one. */
    CHECK(click("Add Bone"));
    GameObject *child = ed.scene().find_by_name("Bone.001");
    CHECK(child && child->parent == ed.scene().find_by_name("Bone") && std::fabs(child->local().position.y - 1.0f) < 1e-5f);
    if (!child) return;
    /* Reset Pose and Apply as Rest, by clicks. */
    child->set_local_rotation(Quat::axis_angle({1, 0, 0}, 0.6f));
    ed.mark_changed_for_test("R74 pose");
    ed.step_frame_headless();
    CHECK(click("Reset Pose"));
    CHECK(std::fabs(ed.scene().find_by_name("Bone.001")->local().rotation.w - 1.0f) < 1e-6f);
    ed.scene().find_by_name("Bone.001")->set_local_rotation(Quat::axis_angle({1, 0, 0}, 0.4f));
    ed.mark_changed_for_test("R74 pose 2");
    ed.step_frame_headless();
    CHECK(click("Apply as Rest"));
    const Bone *rb = ed.scene().find_by_name("Bone.001")->get<Bone>();
    CHECK(rb && std::fabs(rb->rest_rotation.x - Quat::axis_angle({1, 0, 0}, 0.4f).x) < 1e-6f);
    /* Add IK to the child bone: a target and a pole; moving the target turns the bones. */
    ed.command("select Bone.001");
    CHECK(click("Add IK to Bone"));
    GameObject *b1 = ed.scene().find_by_name("Bone.001");
    GameObject *target = ed.scene().find_by_name("Bone.001IK");
    CHECK(b1 && b1->get<IKConstraint>() && target && ed.scene().find_by_name("Bone.001Pole"));
    if (!b1 || !target) return;
    target->set_world_position(target->world_position() + Vec3(0.5f, -0.5f, 0.0f));
    ed.mark_changed_for_test("R74 move target");
    ed.step_frame_headless();
    const float tip_err = length(b1->world_matrix().point({0, b1->get<Bone>()->length, 0}) - target->world_position());
    std::printf("    IK added by a click: the tip off its moved target by %g\n", tip_err);
    CHECK(tip_err < 1e-3f);
  });

  test("rigging window: the Inspector's MeshFilter keeps modeling only, and its Rigging row opens the window with the mesh's groups", [] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.record_buttons_for_test(true);
    ed.command("create Cube");
    ed.command("vgroup add Top");
    ed.step_frame_headless();
    ed.step_frame_headless();
    Recti b;
    CHECK(!ed.button_rect_for_test("Weight Paint", b));  // not in the Inspector any more
    CHECK(ed.button_rect_for_test("Vertex Groups (1), Weight Paint: Rigging...", b));
    r34_click(ed, b.x + b.w / 2, b.y + b.h / 2);
    ed.step_frame_headless();
    CHECK(ed.window_rect_for_test(WindowKind::Rigging).w > 0);
    ed.step_frame_headless();
    CHECK(ed.button_rect_for_test("Weight Paint", b) && ed.button_rect_for_test("Top", b));  // the window shows the groups and the toggle
  });

  test("rigging window: while painting, selecting a bone paints the group named after it", [] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.command("create Cylinder");
    GameObject *mesh = ed.selected_object();
    CHECK(mesh != nullptr);
    if (!mesh) return;
    mesh->name = "Body";  // (the default scene has a Cylinder of its own)
    ed.mark_changed_for_test("R74 name");
    ed.step_frame_headless();
    ed.command("create Armature");  // with the cylinder selected: bound, with a group for the bone
    ed.command("bone add Bone 1");
    ed.command("skin bind " + mesh->name);
    ed.command("select " + mesh->name);
    ed.command("weightpaint on");
    ed.step_frame_headless();
    const Mesh *m = mesh->mesh_filter()->mesh.get();
    CHECK(m->find_group("Bone") >= 0 && m->find_group("Bone.001") >= 0);
    ed.command("select Bone.001");
    ed.step_frame_headless();
    CHECK(ed.weight_paint_active_for_test() && ed.wp_group_name_for_test() == "Bone.001");
    ed.command("select Bone");
    ed.step_frame_headless();
    CHECK(ed.wp_group_name_for_test() == "Bone");
  });

  test("rigging window: a saved layout with the Rigging tab loads back", [] {
    Editor ed;
    ed.init_headless(1200, 800);
    ed.command("window Rigging");
    ed.step_frame_headless();
    const std::string saved = ed.dock_serialize_for_test();
    Editor ed2;
    ed2.init_headless(1200, 800);
    ed2.command("layout " + saved);
    ed2.step_frame_headless();
    CHECK(ed2.window_rect_for_test(WindowKind::Rigging).w > 0 || ed2.dock_serialize_for_test() == saved);
  });
  test("rigging window: with a mesh and a bone selected, Add Armature (Skin) binds the mesh; the window's Weight Paint paints by a drag", [] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.record_buttons_for_test(true);
    ed.command("create Armature");  // nothing else selected: an armature on its own
    ed.command("create Cylinder");
    GameObject *mesh = ed.selected_object();
    CHECK(mesh != nullptr);
    if (!mesh) return;
    mesh->name = "Body";
    mesh->set_world_position({0, 1.0f, 3.0f});
    ed.mark_changed_for_test("R74 body");
    ed.step_frame_headless();
    ed.select_for_test(mesh->id, false);
    ed.select_for_test(ed.scene().find_by_name("Bone")->id, true);  // Ctrl+click: the mesh and a bone of the skeleton
    ed.command("window Rigging");
    auto click = [&](const char *label) {
      ed.step_frame_headless();
      ed.step_frame_headless();
      Recti b;
      if (!ed.button_rect_for_test(label, b)) {
        std::printf("    no button '%s'\n", label);
        return false;
      }
      r34_click(ed, b.x + b.w / 2, b.y + b.h / 2);
      ed.step_frame_headless();
      return true;
    };
    CHECK(click("Add Armature (Skin)"));
    const SkinModifier *sk = ed.scene().find_by_name("Body")->get<SkinModifier>();
    CHECK(sk && sk->object == "Armature" && ed.scene().find_by_name("Body")->mesh_filter()->mesh->find_group("Bone") >= 0);
    /* Weight Paint by the window's toggle, then a drag over the mesh raises the group's weights. */
    ed.command("select Body");
    CHECK(click("Weight Paint"));
    CHECK(ed.weight_paint_active_for_test());
    ed.command("wpbrush draw 80 1 0");  // Draw towards weight 0, a wide brush
    const std::vector<float> before = ed.scene().find_by_name("Body")->mesh_filter()->mesh->vertex_groups[0].weights;
    int x = 0, y = 0;
    CHECK(ed.project_to_window(ed.scene().find_by_name("Body")->world_position(), x, y));
    ed.command(strprintf("wpstroke %d %d %d %d", x - 10, y, x + 10, y));
    ed.step_frame_headless();
    const std::vector<float> &after = ed.scene().find_by_name("Body")->mesh_filter()->mesh->vertex_groups[0].weights;
    size_t changed = 0;
    for (size_t i = 0; i < before.size() && i < after.size(); i++) changed += before[i] != after[i];
    std::printf("    a stroke from the window's Weight Paint changed %zu weights\n", changed);
    CHECK(changed > 0);
  });
  test("learn: the Rigging window, quality tier and small GPU lessons' Try it buttons do what they say", [] {
    /* Each new section's "! cmd:" actions, run as the Learn tab runs them (';'-separated console commands). */
    auto action = [](const std::string &label) {
      for (int i = 0; i < lesson_count(); i++) {
        const std::string body = lesson(i).body;
        const size_t at = body.find("| " + label);
        if (at == std::string::npos) continue;
        const size_t start = body.rfind("! cmd:", at);
        if (start == std::string::npos) continue;
        return body.substr(start + 6, at - start - 6);
      }
      return std::string();
    };
    auto run = [](Editor &ed, const std::string &cmds) {
      size_t s = 0;
      while (s <= cmds.size()) {
        const size_t e = cmds.find(';', s);
        std::string c = cmds.substr(s, e == std::string::npos ? std::string::npos : e - s);
        while (!c.empty() && c.front() == ' ') c.erase(c.begin());
        while (!c.empty() && c.back() == ' ') c.pop_back();
        if (!c.empty()) ed.command(c);
        if (e == std::string::npos) break;
        s = e + 1;
      }
    };
    Editor ed;
    ed.init_headless(1400, 900);
    const std::string rig = action("Make a humanoid rig and open the Rigging window");
    CHECK(!rig.empty());
    run(ed, rig);
    ed.step_frame_headless();
    CHECK(ed.scene().find_by_name("Humanoid") && ed.window_rect_for_test(WindowKind::Rigging).w > 0);
    const std::string low = action("Switch to the Low tier (watch the views soften slightly)");
    CHECK(!low.empty());
    run(ed, low);
    CHECK(ed.quality_tier() == Editor::TierLow);
    run(ed, action("Feel the minimum machine: 8 threads, Low"));
    CHECK(JobSystem::global().thread_count() <= 8);
    run(ed, action("Every thread again"));
    CHECK(JobSystem::global().thread_limit() == 0);
    const std::string gpu = action("Render on the GPU and the CPU together (Auto)");
    CHECK(gpu.find("device auto") != std::string::npos);
    run(ed, "engine path; device auto");
    CHECK(ed.scene().render.device == 2);
  });
  /* ------------------------------------------------------------------ round 75: many assets at once (task 0053) */
  test("assets: Ctrl and Shift click select several materials, Delete asks once and deletes them all as one undo step", [] {
    /* Earlier runs' MA..MD (and the "MA 1", "MB 2" ... that newmat makes beside them) out of the scratch project: they
     * pile up run after run and push MC and MD out of the window's view. */
    {
      const std::string dir = fs::join(scratch_project(), "Assets/Materials");
      for (const DirEntry &de : fs::list(dir))
        if (de.name.size() > 2 && de.name[0] == 'M' && de.name[1] >= 'A' && de.name[1] <= 'D' && (de.name[2] == '.' || de.name[2] == ' ') &&
            fs::extension(de.name) == ".mat")
          std::remove(fs::join(dir, de.name).c_str());
    }
    Editor ed;
    r50_open(ed);
    ed.record_buttons_for_test(true);
    for (const char *n : {"MA", "MB", "MC", "MD"}) ed.command(std::string("newmat ") + n);
    ed.command("select Cube");
    ed.command("assignmat Assets/Materials/MA.mat");  // a slot that uses one of them
    ed.command("window Materials");
    r34_steps(ed, 3);
    auto click = [&](const std::string &key, int mods) {
      Recti b;
      if (!ed.button_rect_for_test(key, b)) {
        std::printf("    no '%s'\n", key.c_str());
        return false;
      }
      const int x = b.x + b.w / 2, y = b.y + b.h / 2;
      platform::Event m, d, u;
      m.type = platform::EventType::MouseMove, d.type = platform::EventType::MouseDown, u.type = platform::EventType::MouseUp;
      m.x = d.x = u.x = x, m.y = d.y = u.y = y;
      m.mods = d.mods = u.mods = mods;
      ed.step_frame_headless({m});
      ed.step_frame_headless({d});
      ed.step_frame_headless({u});
      ed.step_frame_headless();
      return true;
    };
    CHECK(click("mat:MA", 0));
    CHECK(click("mat:MC", platform::MOD_CTRL));
    CHECK(ed.materials_selected_for_test() == 2);
    CHECK(click("mat:MD", platform::MOD_SHIFT));  // the range from the last click (MC) to MD
    CHECK(ed.materials_selected_for_test() == 2);
    CHECK(click("mat:MA", platform::MOD_CTRL));  // and MA again
    CHECK(ed.materials_selected_for_test() == 3);
    CHECK(click("mat:MB", platform::MOD_CTRL | platform::MOD_SHIFT));  // Ctrl+Shift adds the range from MA to MB
    for (const char *n : {"MA", "MB", "MC", "MD"}) CHECK(ed.material_is_selected_for_test(n));  // (other tests' materials may lie between)
    CHECK(click("mat:MB", platform::MOD_CTRL));  // MB out again; the range's others out by hand below
    CHECK(!ed.material_is_selected_for_test("MB"));
    CHECK(click("mat:MA", 0));  // back to exactly MA, MC, MD
    CHECK(click("mat:MC", platform::MOD_CTRL) && click("mat:MD", platform::MOD_CTRL));
    CHECK(ed.materials_selected_for_test() == 3);
    /* Delete over the window: one question for all three. */
    Recti wr = ed.window_rect_for_test(WindowKind::Materials);
    platform::Event mv, kd;
    mv.type = platform::EventType::MouseMove, mv.x = wr.x + wr.w / 2, mv.y = wr.y + wr.h / 3;
    kd.type = platform::EventType::KeyDown, kd.key = platform::KEY_DELETE, kd.x = mv.x, kd.y = mv.y;
    ed.step_frame_headless({mv});
    ed.step_frame_headless({kd});
    ed.step_frame_headless();
    CHECK(ed.material_delete_pending());
    for (const char *n : {"MA", "MC", "MD"}) CHECK(fs::exists(resolve_asset_path(std::string("Assets/Materials/") + n + ".mat")));  // only asked
    Recti b;
    CHECK(ed.button_rect_for_test("Delete All", b));
    r34_click(ed, b.x + b.w / 2, b.y + b.h / 2);
    r34_steps(ed, 3);
    int left = 0;
    for (const char *n : {"MA", "MB", "MC", "MD"}) left += fs::exists(resolve_asset_path(std::string("Assets/Materials/") + n + ".mat"));
    std::printf("    deleted three of four: %d material asset(s) left\n", left);
    CHECK(left == 1 && !ed.material_delete_pending());
    GameObject *cube = ed.scene().find_by_name("Cube");
    CHECK(cube && !cube->get<MeshRenderer>()->materials.empty() && cube->get<MeshRenderer>()->materials[0] == nullptr);  // its slot: the default material now
    ed.command("undo");  // one step for the slots of all three
    r34_steps(ed, 2);
    cube = ed.scene().find_by_name("Cube");
    CHECK(cube && !cube->get<MeshRenderer>()->materials.empty() && cube->get<MeshRenderer>()->materials[0] &&
          cube->get<MeshRenderer>()->materials[0]->name == "MA");
  });

  test("assets: the Textures window takes Ctrl+A over its thumbnails and deletes every texture shown after one question", [] {
    Editor ed;
    r50_open(ed);
    ed.record_buttons_for_test(true);
    std::vector<std::string> paths;
    for (const char *p : {"Bricks", "Noise", "Wood Floor"}) {
      ProceduralTexturePtr t = ed.new_texture_asset_for_test(p);
      CHECK(t != nullptr);
      if (t) paths.push_back(resolve_asset_path(t->asset_path));
    }
    ed.command("window Textures");
    r34_steps(ed, 3);
    Recti b;
    CHECK(ed.button_rect_for_test("tex:Bricks", b));
    r34_click(ed, b.x + b.w / 2, b.y + b.h / 2);
    platform::Event a;
    a.type = platform::EventType::KeyDown, a.key = platform::KEY_A, a.mods = platform::MOD_CTRL, a.x = b.x + b.w / 2, a.y = b.y + b.h / 2;
    ed.step_frame_headless({a});
    ed.step_frame_headless();
    const size_t n = ed.textures_selected_for_test();
    std::printf("    Ctrl+A: %zu textures selected\n", n);
    CHECK(n == ed.texture_count_shown_for_test());
    /* With a search, Ctrl+A takes only what shows. */
    ed.set_texture_search_for_test("brick");
    r34_steps(ed, 2);
    ed.step_frame_headless({a});
    ed.step_frame_headless();
    CHECK(ed.textures_selected_for_test() == ed.texture_count_shown_for_test() && ed.texture_count_shown_for_test() >= 1);
    ed.set_texture_search_for_test("");
    ed.command("texdelete Bricks, Noise");  // the console path: names separated by commas
    r34_steps(ed, 2);
    CHECK(paths.size() == 3 && !fs::exists(paths[0]) && !fs::exists(paths[1]) && fs::exists(paths[2]));
  });

  test("assets: the Project window's Ctrl+click selects several files and Delete asks once", [] {
    Editor ed;
    r50_open(ed);
    ed.record_buttons_for_test(true);
    for (const char *nm : {"PA", "PB"}) ed.command(std::string("newmat ") + nm);
    ed.command("window Project");
    r34_steps(ed, 3);
    ed.project_open_folder_for_test(fs::parent(resolve_asset_path("Assets/Materials/PA.mat")), "p");  // (the folder holds other tests' materials too)
    r34_steps(ed, 3);
    auto click = [&](const std::string &key, int mods) {
      Recti b;
      if (!ed.button_rect_for_test(key, b)) {
        std::printf("    no '%s'\n", key.c_str());
        return false;
      }
      platform::Event m, d, u;
      m.type = platform::EventType::MouseMove, d.type = platform::EventType::MouseDown, u.type = platform::EventType::MouseUp;
      m.x = d.x = u.x = b.x + b.w / 2, m.y = d.y = u.y = b.y + b.h / 2;
      m.mods = d.mods = u.mods = mods;
      ed.step_frame_headless({m});
      ed.step_frame_headless({d});
      ed.step_frame_headless({u});
      return true;
    };
    CHECK(click("file:PA.mat", 0));
    CHECK(click("file:PB.mat", platform::MOD_CTRL));
    CHECK(ed.project_selected_count_for_test() == 2);
    /* Delete with the mouse over the files: one question, then both go to the trash. */
    Recti pb;
    CHECK(ed.button_rect_for_test("file:PB.mat", pb));
    platform::Event kd;
    kd.type = platform::EventType::KeyDown, kd.key = platform::KEY_DELETE, kd.x = pb.x + pb.w / 2, kd.y = pb.y + pb.h / 2;
    ed.step_frame_headless({kd});
    ed.step_frame_headless();
    Recti all;
    CHECK(ed.button_rect_for_test("Delete All", all));
    r34_click(ed, all.x + all.w / 2, all.y + all.h / 2);
    r34_steps(ed, 2);
    CHECK(!fs::exists(resolve_asset_path("Assets/Materials/PA.mat")) && !fs::exists(resolve_asset_path("Assets/Materials/PB.mat")));
  });
  /* ------------------------------------------------------------------ round 76: curve and lattice points by keys (task 0054) */
  test("points: Extrude grows a curve from its end, Ctrl+R cuts a segment where the mouse is, Alt+M merges, Delete keeps two, Alt+C closes", [] {
    Editor ed;
    r34_open(ed);
    ed.command("create Curve: Path");
    GameObject *g = ed.scene().find_by_name("Path");
    CHECK(g && g->curve());
    if (!g || !g->curve()) return;
    g->set_world_position({0, 0.5f, 3.0f});
    ed.command("select Path");
    ed.mark_changed_for_test("R76 setup");
    r34_steps(ed, 3);
    const Recti view = ed.scene_view_rect();
    auto key = [&](int k, int mods, int mx = -1, int my = -1) {
      platform::Event m, d, u;
      m.type = platform::EventType::MouseMove, d.type = platform::EventType::KeyDown, u.type = platform::EventType::KeyUp;
      m.x = d.x = u.x = mx >= 0 ? mx : view.x + view.w / 2;
      m.y = d.y = u.y = my >= 0 ? my : view.y + view.h / 2;
      d.key = u.key = k;
      d.mods = u.mods = mods;
      ed.step_frame_headless({m});
      ed.step_frame_headless({d, u});
      ed.step_frame_headless();
    };
    Curve &c = *g->curve();
    const int n0 = (int)c.points.size();
    /* Extrude (Unity's Ctrl+E) on the last point: one beyond it, in the curve's direction, selected. */
    ed.command(strprintf("curve select Path %d", n0 - 1));
    const Vec3 last = c.points.back().position, prev = c.points[(size_t)n0 - 2].position;
    key(platform::KEY_E, platform::MOD_CTRL);
    CHECK((int)g->curve()->points.size() == n0 + 1);
    CHECK(length(g->curve()->points.back().position - (last + (last - prev))) < 1e-5f);
    CHECK(ed.points_selected_for_test() == std::vector<int>{n0});
    ed.command("undo");  // one step
    r34_steps(ed, 2);
    g = ed.scene().find_by_name("Path");
    CHECK(g && (int)g->curve()->points.size() == n0);
    /* Ctrl+R over the middle of the first segment: a point on the curve's own path there. */
    const CurvePath &path = g->curve()->local_path();
    const Vec3 mid_local = path.pos[path.pos.size() / (size_t)(2 * std::max(1, n0 - 1))];
    int x = 0, y = 0;
    CHECK(ed.project_to_window(g->world_matrix().point(mid_local), x, y));
    key(platform::KEY_R, platform::MOD_CTRL, x, y);
    CHECK((int)g->curve()->points.size() == n0 + 1);
    float off = 1e30f;
    for (const Vec3 &p : path.pos) off = std::min(off, length(g->curve()->points[1].position - p));
    std::printf("    Ctrl+R: the new point %.4f from the old path (length %.3f)\n", off, path.total());
    CHECK(off < 0.02f * path.total());
    /* Merge at Center (Unity's Alt+M) of points 0 and 1: one at their middle. */
    const Vec3 p0 = g->curve()->points[0].position, p1 = g->curve()->points[1].position;
    ed.command("curve select Path 0 1");
    key(platform::KEY_M, platform::MOD_ALT);
    CHECK((int)g->curve()->points.size() == n0 && length(g->curve()->points[0].position - (p0 + p1) * 0.5f) < 1e-5f);
    /* Delete keeps two points. */
    std::string all = "curve select Path";
    for (int i = 0; i < (int)g->curve()->points.size(); i++) all += strprintf(" %d", i);
    ed.command(all);
    key(platform::KEY_DELETE, 0);
    CHECK(g->curve()->points.size() == (size_t)n0 && ed.scene().find_by_name("Path"));  // refused: the curve and its points stay
    ed.command("curve select Path 0");
    key(platform::KEY_DELETE, 0);
    CHECK((int)g->curve()->points.size() == n0 - 1);
    /* Alt+C toggles Closed. */
    const bool closed = g->curve()->closed;
    key(platform::KEY_C, platform::MOD_ALT);
    CHECK(g->curve()->closed != closed);
  });

  test("points: in Blender's keymap E extrudes the curve's first point backwards", [] {
    Editor ed;
    r34_open(ed);
    ed.command("keymap Blender");
    ed.command("create Curve: Path");
    GameObject *g = ed.scene().find_by_name("Path");
    if (!g || !g->curve()) return;
    g->set_world_position({0, 0.5f, 3.0f});
    ed.command("select Path");
    ed.mark_changed_for_test("R76 setup");
    r34_steps(ed, 3);
    const Vec3 a = g->curve()->points[0].position, b = g->curve()->points[1].position;
    const size_t n0 = g->curve()->points.size();
    ed.command("curve select Path 0");
    const Recti view = ed.scene_view_rect();
    platform::Event m, d, u;
    m.type = platform::EventType::MouseMove, d.type = platform::EventType::KeyDown, u.type = platform::EventType::KeyUp;
    m.x = d.x = u.x = view.x + view.w / 2, m.y = d.y = u.y = view.y + view.h / 2;
    d.key = u.key = platform::KEY_E;
    ed.step_frame_headless({m});
    ed.step_frame_headless({d, u});
    ed.step_frame_headless();
    CHECK(g->curve()->points.size() == n0 + 1 && length(g->curve()->points[0].position - (a + (a - b))) < 1e-5f);
  });

  test("points: Ctrl+R over a lattice's edge adds a row keeping the shape; Delete with a whole row selected removes one", [] {
    Editor ed;
    r34_open(ed);
    ed.command("create Lattice");
    GameObject *g = ed.scene().find_by_name("Lattice");
    CHECK(g && g->get<Lattice>());
    if (!g || !g->get<Lattice>()) return;
    g->set_world_position({0, 0.5f, 3.0f});
    g->set_local_scale({2, 2, 2});
    Lattice &l = *g->get<Lattice>();
    l.interpolation = 0;  // Linear: the shape is kept exactly
    l.set_control_point(7, l.control_point(7) + Vec3(0.2f, 0.1f, 0.0f));
    ed.command("select Lattice");
    ed.mark_changed_for_test("R76 lattice");
    r34_steps(ed, 3);
    std::vector<Vec3> probes;
    for (int i = 0; i < 27; i++) probes.push_back(Vec3((i % 3) * 0.5f - 0.5f, ((i / 3) % 3) * 0.5f - 0.5f, (i / 9) * 0.5f - 0.5f));
    std::vector<Vec3> before;
    for (const Vec3 &q : probes) before.push_back(g->get<Lattice>()->displacement(q));
    /* The mouse on the U edge between points 0 and 1. */
    int x0, y0, x1, y1;
    CHECK(ed.project_to_window(g->world_matrix().point(l.control_point(0)), x0, y0) && ed.project_to_window(g->world_matrix().point(l.control_point(1)), x1, y1));
    const Recti view = ed.scene_view_rect();
    (void)view;
    platform::Event m, d, u;
    m.type = platform::EventType::MouseMove, d.type = platform::EventType::KeyDown, u.type = platform::EventType::KeyUp;
    m.x = d.x = u.x = (x0 + x1) / 2, m.y = d.y = u.y = (y0 + y1) / 2;
    d.key = u.key = platform::KEY_R;
    d.mods = u.mods = platform::MOD_CTRL;
    ed.step_frame_headless({m});
    ed.step_frame_headless({d, u});
    ed.step_frame_headless();
    const Lattice &l2 = *ed.scene().find_by_name("Lattice")->get<Lattice>();
    CHECK(l2.points_u == 3 && l2.points_v == 2 && l2.points_w == 2);
    float worst = 0.0f;
    for (size_t i = 0; i < probes.size(); i++) worst = std::max(worst, length(l2.displacement(probes[i]) - before[i]));
    std::printf("    a row added along U: the deformation moved by at most %g\n", worst);
    CHECK(worst < 1e-4f);
    /* Delete with the whole first U row selected (u index 0): back to two along U. */
    std::string sel = "points select Lattice";
    for (int i = 0; i < l2.size(); i++)
      if (i % l2.points_u == 0) sel += strprintf(" %d", i);
    ed.command(sel);
    d.key = u.key = platform::KEY_DELETE;
    d.mods = u.mods = 0;
    ed.step_frame_headless({m});
    ed.step_frame_headless({d, u});
    ed.step_frame_headless();
    GameObject *g3 = ed.scene().find_by_name("Lattice");
    CHECK(g3 && g3->get<Lattice>()->points_u == 2);
    ed.command("undo");
    r34_steps(ed, 2);
    CHECK(ed.scene().find_by_name("Lattice")->get<Lattice>()->points_u == 3);
  });
  test("points: Ctrl+R on a Poly curve's segment puts the point where the mouse is along it, never on a corner; a merge of bad indices is refused", [] {
    Editor ed;
    r34_open(ed);
    ed.command("create Curve: Path");
    GameObject *g = ed.scene().find_by_name("Path");
    if (!g || !g->curve()) return;
    g->curve()->type = 2;  // Poly: one sample per segment
    g->set_world_position({0, 0.5f, 3.0f});
    ed.command("select Path");
    ed.mark_changed_for_test("R76 poly");
    r34_steps(ed, 3);
    const Vec3 a = g->curve()->points[0].position, b = g->curve()->points[1].position;
    const Vec3 at = a + (b - a) * 0.3f;  // 30 % along the first segment
    int x = 0, y = 0;
    CHECK(ed.project_to_window(g->world_matrix().point(at), x, y));
    platform::Event m, d, u;
    m.type = platform::EventType::MouseMove, d.type = platform::EventType::KeyDown, u.type = platform::EventType::KeyUp;
    m.x = d.x = u.x = x, m.y = d.y = u.y = y;
    d.key = u.key = platform::KEY_R;
    d.mods = u.mods = platform::MOD_CTRL;
    ed.step_frame_headless({m});
    ed.step_frame_headless({d, u});
    ed.step_frame_headless();
    const Vec3 p = g->curve()->points[1].position;
    std::printf("    Poly Ctrl+R at 30 %%: the new point %.3f from where the mouse was, %.3f from the nearest corner\n", length(p - at),
                std::min(length(p - a), length(p - b)));
    CHECK(length(p - at) < 0.05f * length(b - a) && std::min(length(p - a), length(p - b)) > 0.1f * length(b - a));
    const size_t n = g->curve()->points.size();
    ed.command("points merge Path 0 99");  // one point really there: nothing to merge, no crash
    ed.command("points merge Path -1 0");
    CHECK(g->curve()->points.size() == n);
  });
  /* ------------------------------------------------------------------ round 77: layouts for each stage (task 0055) */
  test("layouts: each stage's layout opens its windows and shows its tools; Show All Tools brings back the rest", [] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.record_buttons_for_test(true);
    ed.command("layout UV Unwrapping");
    ed.step_frame_headless();
    CHECK(ed.workspace_name() == "UV Unwrapping" && ed.window_rect_for_test(WindowKind::UVEditor).w > 0 && ed.window_rect_for_test(WindowKind::Scene).w > 0);
    CHECK(ed.shows(Editor::FocusUV) && ed.shows(Editor::FocusEdit) && !ed.shows(Editor::FocusRig));
    ed.command("layout Texturing & Materials");
    ed.step_frame_headless();
    CHECK(ed.window_rect_for_test(WindowKind::Materials).w > 0 && ed.window_rect_for_test(WindowKind::Textures).w > 0);
    CHECK(ed.shows(Editor::FocusMaterial) && !ed.shows(Editor::FocusEdit));
    ed.command("layout Rigging");
    ed.step_frame_headless();
    CHECK(ed.window_rect_for_test(WindowKind::Rigging).w > 0 && ed.shows(Editor::FocusRig));
    ed.command("layout Modeling & Topology");
    ed.step_frame_headless();
    CHECK(ed.shows(Editor::FocusEdit) && ed.shows(Editor::FocusCheck) && !ed.shows(Editor::FocusMaterial));
    /* Blocking Out's Modeling Tools window hides the other stages' sections and says how to get them back. */
    ed.command("layout Blocking Out");
    ed.step_frame_headless();
    ed.step_frame_headless();
    Recti b;
    bool found = false;
    for (const std::string &l : ed.button_labels_for_test())
      if (l.find("more for other stages: Show All Tools") != std::string::npos && ed.button_rect_for_test(l, b)) found = true;
    CHECK(found && !ed.shows(Editor::FocusEdit));
    if (found) {
      r34_click(ed, b.x + b.w / 2, b.y + b.h / 2);
      ed.step_frame_headless();
      CHECK(ed.show_all_tools() && ed.shows(Editor::FocusEdit));
    }
    ed.command("alltools off");
    CHECK(!ed.shows(Editor::FocusEdit));
    /* The console reaches what a layout hides: a Light while texturing. */
    ed.command("layout Texturing & Materials");
    CHECK(!ed.shows(Editor::FocusLight));
    ed.command("create Point Light");
    CHECK(ed.scene().find_by_name("Point Light") != nullptr);
    /* Default shows everything, as before. */
    ed.command("layout Default");
    CHECK(ed.shows(Editor::FocusRig) && ed.shows(Editor::FocusEdit) && ed.shows(Editor::FocusMaterial));
  });

  test("layouts: Save Layout keeps the arrangement and the focus under a name; Delete Layout removes it; built-in names are refused", [] {
    Editor ed;
    ed.init_headless(1200, 800);
    ed.command("layout Rigging");
    ed.command("window Lighting");
    ed.step_frame_headless();
    ed.command("layout save My Rig Stage");
    const std::vector<std::string> names = ed.workspace_names();
    CHECK(std::find(names.begin(), names.end(), "My Rig Stage") != names.end());
    ed.command("layout Default");
    ed.step_frame_headless();
    ed.command("layout My Rig Stage");
    ed.step_frame_headless();
    CHECK(ed.workspace_name() == "My Rig Stage" && ed.window_rect_for_test(WindowKind::Lighting).w > 0 && ed.shows(Editor::FocusRig) &&
          !ed.shows(Editor::FocusEdit));
    ed.command("layout save Default");  // a built-in's name: refused
    int defaults = 0;
    for (const std::string &n : ed.workspace_names()) defaults += n == "Default";
    CHECK(defaults == 1);
    ed.command("layout delete My Rig Stage");
    const std::vector<std::string> after = ed.workspace_names();
    CHECK(std::find(after.begin(), after.end(), "My Rig Stage") == after.end());
  });

  test("layouts: a Reference Image is an unlit plane the image's shape, facing the view asked for, out of renders and shadows", [] {
    const std::string png = fs::join(test_dir(), "r77_ref.png");
    std::vector<uint32_t> px((size_t)200 * 100, 0xFF808080u);
    CHECK(write_png(png, px.data(), 200, 100, 200));
    Editor ed;
    ed.init_headless(800, 600);
    ed.command("refimage " + png + " top");
    GameObject *g = ed.selected_object();
    CHECK(g && g->name.rfind("Reference r77_ref", 0) == 0);
    if (!g) return;
    const MeshRenderer *mr = g->get<MeshRenderer>();
    CHECK(mr && !mr->show_in_renders && !mr->cast_shadows && mr->materials.size() == 1 && mr->materials[0]->unlit);
    CHECK(std::fabs(g->local().scale.x - 2.0f) < 1e-5f && std::fabs(g->local().scale.y - 1.0f) < 1e-5f);  // 2:1
    const Vec3 facing = normalize(g->world_matrix().dir(Vec3(0, 0, -1)));  // the quad faces -Z before turning
    std::printf("    top view: the image faces (%.2f, %.2f, %.2f)\n", facing.x, facing.y, facing.z);
    CHECK(facing.y > 0.999f);
    ed.command("refimage " + png + " side");
    GameObject *s = ed.selected_object();
    CHECK(s && s != g && normalize(s->world_matrix().dir(Vec3(0, 0, -1))).x > 0.999f);
  });
  test("layouts: a lesson's layout button arranges the windows too, and your own layouts survive a restart (preferences)", [] {
    /* The lesson's "Use the UV Unwrapping layout" goes through dock_reset by name: the UV Editor opens beside the Scene. */
    {
      Editor ed;
      ed.init_headless(1400, 900);
      ed.command("lesson 1");
      ed.step_frame_headless();
      ed.dock_reset_for_test("UV Unwrapping");
      ed.step_frame_headless();
      CHECK(ed.workspace_name() == "UV Unwrapping" && ed.window_rect_for_test(WindowKind::UVEditor).w > 0 && ed.shows(Editor::FocusUV));
    }
    const std::string prefs = fs::join(test_dir(), "r77_prefs.txt");
    std::error_code ec;
    std::filesystem::remove(std::filesystem::u8path(prefs), ec);
    {
      Editor ed;
      ed.init_headless(1200, 800);
      ed.prefs_for_test(prefs);
      ed.command("layout Lighting & Rendering");
      ed.command("layout save Lights; and = Stuff");  // separators in a name are dropped, not saved broken
      ed.command("layout Rigging");
      ed.save_prefs_for_test();
    }
    {
      Editor ed;
      ed.init_headless(1200, 800);
      ed.prefs_for_test(prefs);
      ed.load_prefs_for_test();
      ed.step_frame_headless();
      CHECK(ed.workspace_name() == "Rigging" && ed.shows(Editor::FocusRig) && !ed.shows(Editor::FocusEdit));
      std::string mine;
      for (const std::string &n : ed.workspace_names())
        if (n.rfind("Lights", 0) == 0) mine = n;
      std::printf("    restored: layout '%s', and yours '%s'\n", ed.workspace_name().c_str(), mine.c_str());
      CHECK(!mine.empty() && mine.find(';') == std::string::npos && mine.find('=') == std::string::npos);
      ed.command("layout " + mine);
      ed.step_frame_headless();
      CHECK(ed.window_rect_for_test(WindowKind::Lighting).w > 0 && ed.shows(Editor::FocusLight));
    }
  });
  /* ---------------------------------------------------------------- round 78: bind poses (task 0056) */
  test("bind poses: a glTF and an FBX saved with the forearm bent import bent like Blender shows them, and Reset Pose gives the bound shape", [] {
    /* Blender's own posed tube (make_rig_posed.py), in glTF's axes: Blendity mirrors X. */
    std::vector<Vec3> blender;
    {
      std::ifstream f("tests/fixtures/rig_tube_posed_verts.txt");
      float x, y, z;
      while (f >> x >> y >> z) blender.push_back({-x, y, z});
    }
    CHECK(blender.size() == 132);
    /* The farthest any point of one set is from the other set (both ways). */
    auto apart = [](const std::vector<Vec3> &a, const std::vector<Vec3> &b) {
      float worst = 0.0f;
      for (int pass = 0; pass < 2; pass++) {
        const std::vector<Vec3> &p = pass ? b : a, &q = pass ? a : b;
        for (const Vec3 &v : p) {
          float best = 1e30f;
          for (const Vec3 &w : q) best = std::min(best, length(v - w));
          worst = std::max(worst, best);
        }
      }
      return worst;
    };
    for (const char *file : {"tests/fixtures/rig_tube_posed.glb", "tests/fixtures/rig_tube_posed.fbx"}) {
      ImportResult r;
      CHECK(import_model(file, r));
      Editor ed;
      ed.init_headless(800, 600);
      GameObject *root = instantiate_import(ed.scene(), r, "Posed Import", nullptr);
      GameObject *upper = ed.scene().find_by_name("Upper"), *tube = ed.scene().find_by_name("Tube");
      CHECK(root && upper && upper->get<Bone>() && tube && tube->get<SkinModifier>());
      if (!root || !upper || !upper->get<Bone>() || !tube || !tube->get<SkinModifier>()) continue;
      const Bone &ub = *upper->get<Bone>();
      /* The bones keep the file's pose; their rest is the bind pose: 60 degrees apart. */
      const Quat a = ub.rest_rotation, b = upper->local().rotation;
      const float bent = 2.0f * std::acos(std::min(1.0f, std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w)));
      const Mat4 &tw = tube->world_matrix();
      std::vector<Vec3> posed;
      for (const Vec3 &p : tube->evaluated_mesh()->positions) posed.push_back(tw.point(p));
      const float off = apart(posed, blender);
      std::printf("    %s: Upper posed %.2f degrees from its rest; %zu vertices, at most %.2g from Blender's posed mesh\n", file, bent * 57.29578f, posed.size(),
                  off);
      CHECK(std::fabs(bent * 57.29578f - 60.0f) < 0.1f);
      CHECK(off < 1e-3f * 2.0f);  // 1e-3 of the tube's size (2 m)
      /* Reset Pose: back to the file's vertices (the shape it was bound in), and Upper straight above Lower again. */
      ed.command("armature reset Upper");
      const Mesh &m = *tube->mesh_filter()->mesh;
      const Mesh *e = tube->evaluated_mesh();
      float rest = 0.0f;
      for (size_t i = 0; i < m.vert_count(); i++) rest = std::max(rest, length(tw.point(e->positions[i]) - tw.point(m.positions[i])));
      const Vec3 tip = upper->world_matrix().point(Vec3(0.0f));
      std::printf("    after Reset Pose: moved by %.2g from the file's mesh; Upper's head at (%.3f, %.3f, %.3f)\n", rest, tip.x, tip.y, tip.z);
      CHECK(rest < 1e-3f);
      CHECK(length(tip - Vec3(0.0f, 1.0f, 0.0f)) < 1e-3f);
    }
  });

  test("bind poses: a rig saved at rest imports as before (each bone's rest is where the file puts it)", [] {
    for (const char *file : {"tests/fixtures/rig_tube.glb", "tests/fixtures/rig_tube.fbx"}) {
      ImportResult r;
      CHECK(import_model(file, r));
      int binds = 0;
      for (const ImportedNode &n : r.nodes) binds += n.has_bind;
      Scene s;
      instantiate_import(s, r, "Rest Import", nullptr);
      float worst = 0.0f;
      int bones = 0;
      for (const char *name : {"Lower", "Upper"}) {
        const GameObject *g = s.find_by_name(name);
        const Bone *b = g ? g->get<Bone>() : nullptr;
        CHECK(b);
        if (!b) continue;
        bones++;
        worst = std::max(worst, length(b->rest_position - g->local().position) / std::max(1.0f, length(g->local().position)));
        const Quat q = b->rest_rotation, l = g->local().rotation;
        worst = std::max(worst, 1.0f - std::fabs(q.x * l.x + q.y * l.y + q.z * l.z + q.w * l.w));
      }
      std::printf("    %s: %d bones with a bind pose; rest differs from the saved transform by %.2g\n", file, binds, worst);
      CHECK(bones == 2 && binds >= 2 && worst == 0.0f);  // exactly: a bind equal to the saved transform keeps it bit for bit
    }
  });

  test("bind poses: a bind that can't be used (under a parent scaled to 0, or broken) leaves the bone resting where the file puts it", [] {
    ImportResult r;
    ImportedNode hidden, bone, mesh;
    hidden.name = "Hidden";
    hidden.scale = Vec3(0.0f);  // hide-by-scale: its place has no inverse
    bone.name = "Joint";
    bone.parent = 0;
    bone.position = {0.0f, 1.0f, 0.0f};
    bone.is_bone = true;
    bone.has_bind = true;
    bone.bind_world = Mat4::translate({3.0f, 4.0f, 5.0f});
    mesh.name = "Body";
    r.nodes = {hidden, bone, mesh};
    Scene s;
    instantiate_import(s, r, "Broken Bind", nullptr);
    const GameObject *g = s.find_by_name("Joint");
    const Bone *b = g ? g->get<Bone>() : nullptr;
    CHECK(b && length(b->rest_position - Vec3(0.0f, 1.0f, 0.0f)) < 1e-6f);
    /* A non-finite bind is the same. */
    r.nodes[0].scale = Vec3(1.0f);
    r.nodes[1].bind_world.m[12] = std::numeric_limits<float>::quiet_NaN();
    Scene s2;
    instantiate_import(s2, r, "Broken Bind", nullptr);
    g = s2.find_by_name("Joint");
    b = g ? g->get<Bone>() : nullptr;
    CHECK(b && length(b->rest_position - Vec3(0.0f, 1.0f, 0.0f)) < 1e-6f);
  });
  test("layouts: clicking the Layout dropdown opens its list (it crashed: the list outlived its names) and picking a row applies it", [] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.record_buttons_for_test(true);
    ed.step_frame_headless();
    const Recti r = ed.combo_rect_for_test("layout_combo");
    CHECK(r.w > 0 && r.h > 0);
    if (r.w <= 0) return;
    auto click = [&](int x, int y) {
      platform::Event m, d, u;
      m.type = platform::EventType::MouseMove, d.type = platform::EventType::MouseDown, u.type = platform::EventType::MouseUp;
      m.x = d.x = u.x = x, m.y = d.y = u.y = y;
      ed.step_frame_headless({m});
      ed.step_frame_headless({d});
      ed.step_frame_headless({u});
      ed.step_frame_headless();
    };
    /* Open it from a custom arrangement too (its names list then grows a "Custom" row each frame). */
    for (const char *start : {"Default", "custom"}) {
      if (std::string(start) == "custom") ed.command("layout 0,1,2|3,4");
      else ed.command("layout Default");
      ed.step_frame_headless();
      click(r.x + r.w / 2, r.y + r.h / 2);
      for (int i = 0; i < 3; i++) ed.step_frame_headless();  // the list draws, frame after frame
      Recti row;
      const bool listed = ed.button_rect_for_test("menu:UV Unwrapping", row);
      std::printf("    from %s: list open %d, popups %zu\n", start, (int)listed, ed.popup_count_for_test());
      CHECK(listed && ed.popup_count_for_test() == 1);
      if (!listed) continue;
      click(row.x + row.w / 2, row.y + row.h / 2);
      CHECK(ed.workspace_name() == "UV Unwrapping" && ed.popup_count_for_test() == 0);
    }
  });
  test("object fields: dragging an object from the Hierarchy onto an object field (a Boolean's Object) fills it in (Unity's object field) and keeps the Inspector where it was", [] {
    Editor ed;
    r34_open(ed);
    ed.record_buttons_for_test(true);
    ed.command("select Cube");
    ed.command("component BooleanModifier");
    r34_steps(ed, 3);
    auto at = [](platform::EventType t, int x, int y) {
      platform::Event e;
      e.type = t, e.x = x, e.y = y;
      return e;
    };
    using ET = platform::EventType;
    /* Press on the Cylinder's row, drag sideways into the Inspector, let go over the field. */
    auto drag_onto = [&](const std::string &object, const std::string &field) {
      int hx = 0, hy = 0;
      Recti f;
      if (!ed.hierarchy_row_for_test(object, hx, hy) || !ed.button_rect_for_test("field:" + field, f)) {
        std::printf("    no row '%s' or field '%s'\n", object.c_str(), field.c_str());
        return false;
      }
      const int fx = f.x + f.w / 2, fy = f.y + f.h / 2;
      ed.step_frame_headless({at(ET::MouseMove, hx, hy)});
      ed.step_frame_headless({at(ET::MouseDown, hx, hy)});
      for (int k = 1; k <= 4; k++) ed.step_frame_headless({at(ET::MouseMove, hx + (fx - hx) * k / 4, hy + (fy - hy) * k / 4)});
      ed.step_frame_headless({at(ET::MouseUp, fx, fy)});
      ed.step_frame_headless();
      return true;
    };
    GameObject *cube = ed.scene().find_by_name("Cube");
    CHECK(cube && cube->get<BooleanModifier>());
    if (!cube || !cube->get<BooleanModifier>()) return;
    CHECK(drag_onto("Cylinder", "Object"));
    std::printf("    Boolean's Object: '%s'; still showing Cube: %d\n", cube->get<BooleanModifier>()->object.c_str(),
                (int)(ed.active_object_for_test() == cube));
    CHECK(cube->get<BooleanModifier>()->object == "Cylinder");
    CHECK(ed.active_object_for_test() == cube);  // the drag didn't select the Cylinder (the Inspector stays on the Cube)
    ed.command("undo");
    r34_steps(ed, 2);
    cube = ed.scene().find_by_name("Cube");
    CHECK(cube && cube->get<BooleanModifier>() && cube->get<BooleanModifier>()->object.empty());  // one undo step
    /* A plain click on a row still selects it. */
    int hx = 0, hy = 0;
    CHECK(ed.hierarchy_row_for_test("Cylinder", hx, hy));
    ed.step_frame_headless({at(ET::MouseMove, hx, hy)});
    ed.step_frame_headless({at(ET::MouseDown, hx, hy)});
    ed.step_frame_headless({at(ET::MouseUp, hx, hy)});
    ed.step_frame_headless();
    CHECK(ed.active_object_for_test() && ed.active_object_for_test()->name == "Cylinder");
  });
  test("object fields: a wobbly click still selects, a row dropped in the Scene view ends up selected, and a drag ends with the button", [] {
    Editor ed;
    r34_open(ed);
    ed.record_buttons_for_test(true);
    ed.command("select Cube");
    r34_steps(ed, 2);
    auto at = [](platform::EventType t, int x, int y) {
      platform::Event e;
      e.type = t, e.x = x, e.y = y;
      return e;
    };
    using ET = platform::EventType;
    int hx = 0, hy = 0;
    CHECK(ed.hierarchy_row_for_test("Cylinder", hx, hy));
    /* A click that slides 8 pixels sideways on the same row: a drag that went nowhere, still a selection. */
    ed.step_frame_headless({at(ET::MouseMove, hx, hy)});
    ed.step_frame_headless({at(ET::MouseDown, hx, hy)});
    ed.step_frame_headless({at(ET::MouseMove, hx + 8, hy)});
    ed.step_frame_headless({at(ET::MouseUp, hx + 8, hy)});
    ed.step_frame_headless();
    CHECK(ed.active_object_for_test() && ed.active_object_for_test()->name == "Cylinder");
    /* Back to the Cube; drag the (unselected) Cylinder into the Scene view: it lands there and is selected. */
    ed.command("select Cube");
    r34_steps(ed, 2);
    int sx = 0, sy = 0;
    CHECK(ed.project_to_window(Vec3(2.0f, 0.0f, 2.0f), sx, sy));
    CHECK(ed.hierarchy_row_for_test("Cylinder", hx, hy));
    ed.step_frame_headless({at(ET::MouseMove, hx, hy)});
    ed.step_frame_headless({at(ET::MouseDown, hx, hy)});
    for (int k = 1; k <= 4; k++) ed.step_frame_headless({at(ET::MouseMove, hx + (sx - hx) * k / 4, hy + (sy - hy) * k / 4)});
    ed.step_frame_headless({at(ET::MouseUp, sx, sy)});
    /* The very next frame presses again (no idle frame between): the old drag must not still be live. */
    ed.step_frame_headless({at(ET::MouseDown, sx, sy)});
    CHECK(ed.hierarchy_drag_object() == nullptr);
    ed.step_frame_headless({at(ET::MouseUp, sx, sy)});
    ed.step_frame_headless();
    std::printf("    after the Scene-view drop: active '%s'\n", ed.active_object_for_test() ? ed.active_object_for_test()->name.c_str() : "-");
    CHECK(ed.active_object_for_test() && ed.active_object_for_test()->name == "Cylinder");
  });
  test("assets: `matdelete` takes comma-separated names with spaces, skips unknown ones, and split_list trims and drops empty parts", [] {
    const std::vector<std::string> parts = split_list("  Wood Floor , Bricks,, ,Tiles  ", ',');
    CHECK(parts.size() == 3 && parts[0] == "Wood Floor" && parts[1] == "Bricks" && parts[2] == "Tiles");
    CHECK(split_list("", ',').empty() && split_list(" , ", ',').empty());
    /* Earlier runs' copies out of the scratch project (newmat would name the new ones "R80 Oak 1"...). */
    for (const char *f : {"R80 Oak.mat", "R80Pine.mat"}) std::remove(resolve_asset_path(std::string("Assets/Materials/") + f).c_str());
    Editor ed;
    ed.init_headless(1000, 700);
    ed.command("newmat R80 Oak");
    ed.command("newmat R80Pine");
    CHECK(fs::exists(resolve_asset_path("Assets/Materials/R80 Oak.mat")) && fs::exists(resolve_asset_path("Assets/Materials/R80Pine.mat")));
    ed.command("matdelete R80 Oak, No Such Material, R80Pine");
    CHECK(!fs::exists(resolve_asset_path("Assets/Materials/R80 Oak.mat")) && !fs::exists(resolve_asset_path("Assets/Materials/R80Pine.mat")));
  });
  test("assets: a list's multi-selection - Ctrl+Shift with the last click gone starts a range, sync drops hidden ones, Ctrl+A returns the last", [] {
    const std::vector<std::string> shown{"A", "B", "C", "D"};
    MultiSelect<std::string> s;
    s.click(shown, 1, false, false);  // B
    s.click(shown, 3, false, true);   // Shift: B..D
    CHECK(s.items.size() == 3 && s.has("B") && s.has("C") && s.has("D") && s.anchor == "B");
    s.click(shown, 0, true, false);  // Ctrl: + A, the anchor moves to A
    CHECK(s.items.size() == 4 && s.anchor == "A");
    /* The anchor scrolled out of a narrower list: Ctrl+Shift starts the range at the click, keeping the rest. */
    const std::vector<std::string> narrow{"C", "D"};
    s.only("B");
    s.anchor = "Z";
    s.click(narrow, 1, true, true);
    CHECK(s.has("B") && s.has("D") && s.anchor == "D");
    /* sync: hidden ones drop; the edited one joins when shown; nothing edited, nothing selected. */
    s.items = {"A", "C"};
    s.sync(narrow, "D");
    CHECK(s.items.size() == 1 && s.items[0] == "D");
    s.items = {"C"};
    s.sync(narrow, "C");
    CHECK(s.items.size() == 1 && s.items[0] == "C");
    s.sync(narrow, "");
    CHECK(s.items.empty());
    const std::string *last = s.select_all(shown);
    CHECK(last && *last == "D" && s.items.size() == 4);
    CHECK(s.select_all({}) == nullptr);
  });
  /* ---------------------------------------------------------------- round 81: inpainted skin weights (task 0057) */
}

static void tests_round81() {
  test("skin weights: the cotangent Laplacian is symmetric with rows summing to 0, and the Voronoi masses add up to the surface's area", [] {
    Mesh m;
    r81_tube(m, {0, 0, 0}, {0, 2, 0}, 0.5f, 8, 16);
    std::vector<std::vector<std::pair<uint32_t, double>>> K;
    std::vector<double> mass;
    skinweights::cotangent_laplacian(m, K, mass);
    double worst_sum = 0.0, worst_sym = 0.0, total = 0.0;
    for (size_t i = 0; i < K.size(); i++) {
      double s = 0.0;
      for (const auto &[j, w] : K[i]) {
        s += w;
        double back = 0.0;
        for (const auto &[k, w2] : K[j])
          if (k == i) back = w2;
        worst_sym = std::max(worst_sym, std::fabs(w - back));
      }
      worst_sum = std::max(worst_sum, std::fabs(s));
      total += mass[i];
    }
    double area = 0.0;  // the quads' area, as two triangles each
    for (size_t f = 0; f < m.face_count(); f++) {
      const uint32_t *q = m.face_verts(f);
      const Vec3 a = m.positions[q[0]], b = m.positions[q[1]], c = m.positions[q[2]], d = m.positions[q[3]];
      area += 0.5 * (length(cross(b - a, c - a)) + length(cross(c - a, d - a)));
    }
    std::printf("    rows sum to within %.2g, symmetric within %.2g; mass %.5f against area %.5f\n", worst_sum, worst_sym, total, area);
    CHECK(worst_sum < 1e-9 && worst_sym < 1e-9);
    CHECK(std::fabs(total - area) < 1e-4 * area);
  });

  test("skin weights: inpainting between two known ends of a tube gives weights in [0, 1] summing to 1, changing smoothly along it", [] {
    Mesh m;
    r81_tube(m, {0, 0, 0}, {0, 2, 0}, 0.2f, 20, 12);
    const size_t n = m.vert_count();
    std::vector<uint8_t> known(n, 0);
    std::vector<std::vector<float>> w(2, std::vector<float>(n, 0.0f));
    for (size_t v = 0; v < n; v++) {
      const float y = m.positions[v].y;
      if (y < 0.5f) known[v] = 1, w[0][v] = 1.0f;
      if (y > 1.5f) known[v] = 1, w[1][v] = 1.0f;
    }
    InpaintReport rep;
    std::string err;
    CHECK(skinweights::inpaint(m, known, w, true, InpaintOptions{}, &rep, &err));
    m.vertex_groups = {{"Low", w[0]}, {"High", w[1]}};
    float jump = 0.0f, prev = 1.0f;
    bool falling = true;
    for (int r = 0; r <= 20; r++) {  // one vertex per ring, bottom to top
      const float x = w[0][(size_t)r * 12];
      falling = falling && x <= prev + 1e-4f;
      if (r) jump = std::max(jump, std::fabs(x - prev));
      prev = x;
    }
    std::printf("    %zu inpainted by %s in %.1f ms; partition error %.2g, the largest step between rings %.3f\n", rep.inpainted, rep.solver, rep.ms,
                r81_partition_error(m), jump);
    CHECK(rep.inpainted > 0 && r81_partition_error(m) < 1e-4f);
    CHECK(falling && jump < 0.2f);
  });

  test("skin weights: transferring a body's weights onto a copy of itself gives back the same weights", [] {
    Mesh body;
    r81_tube(body, {0, 0, 0}, {0, 2, 0}, 0.3f, 16, 12);
    const size_t n = body.vert_count();
    std::vector<float> low(n), high(n);
    for (size_t v = 0; v < n; v++) high[v] = std::max(0.0f, std::min(1.0f, body.positions[v].y - 0.5f)), low[v] = 1.0f - high[v];
    body.vertex_groups = {{"Low", low}, {"High", high}};
    Mesh copy = body;
    copy.vertex_groups.clear();
    InpaintReport rep;
    std::string err;
    CHECK(transfer_skin_weights(body, copy, InpaintOptions{}, &rep, &err));
    float worst = 0.0f;
    for (size_t v = 0; v < n; v++) worst = std::max({worst, std::fabs(r81_weight(copy, "Low", v) - low[v]), std::fabs(r81_weight(copy, "High", v) - high[v])});
    std::printf("    %zu matched, %zu inpainted; worst difference %.2g\n", rep.known, rep.inpainted, worst);
    CHECK(rep.known == n && worst < 1e-4f);
  });

  test("skin weights: a loose sleeve takes the arm's weights, not the torso's it passes close to (the paper's figure 1)", [] {
    /* The body: a torso (Y up, radius 0.3) and an arm out along +X from it, each weighted wholly to its bone. */
    Mesh body;
    r81_tube(body, {0, -1, 0}, {0, 1, 0}, 0.3f, 20, 24);
    const size_t torso_n = body.vert_count();
    r81_tube(body, {0.3f, 0.5f, 0}, {1.2f, 0.5f, 0}, 0.1f, 18, 12);
    std::vector<float> torso(body.vert_count(), 0.0f), arm(body.vert_count(), 0.0f);
    for (size_t v = 0; v < body.vert_count(); v++) (v < torso_n ? torso : arm)[v] = 1.0f;
    body.vertex_groups = {{"Torso", torso}, {"Arm", arm}};
    /* The sleeve: 3 cm off the arm (within the paper's 5 % of its size), starting right at the torso's side. */
    Mesh sleeve;
    r81_tube(sleeve, {0.32f, 0.5f, 0}, {0.9f, 0.5f, 0}, 0.13f, 12, 16);
    auto worst_torso = [&](const InpaintOptions &o) {
      Mesh s = sleeve;
      std::string err;
      InpaintReport rep;
      CHECK(transfer_skin_weights(body, s, o, &rep, &err));
      float t = 0.0f;
      for (size_t v = 0; v < s.vert_count(); v++) t = std::max(t, r81_weight(s, "Torso", v));
      std::printf("    angle %.0f, distance %.2f: %zu matched, %zu inpainted, most torso weight on the sleeve %.3f, partition error %.2g\n", o.angle,
                  o.distance, rep.known, rep.inpainted, t, r81_partition_error(s));
      CHECK(r81_partition_error(s) < 1e-3f);
      return t;
    };
    InpaintOptions plain;  // plain closest point (Blender's Data Transfer): anything near, any normal
    plain.angle = 180.0f, plain.distance = 10.0f;
    const float naive = worst_torso(plain), robust = worst_torso(InpaintOptions{});
    CHECK(naive > 0.5f);   // the sleeve's end by the torso copies the torso
    CHECK(robust < 0.1f);  // matched by normals, its end is inpainted from the arm
  });

  test("skin weights: vertices in the InpaintMask group are inpainted, not copied", [] {
    Mesh body;
    r81_tube(body, {0, 0, 0}, {0, 2, 0}, 0.3f, 16, 12);
    const size_t n = body.vert_count();
    std::vector<float> one(n, 1.0f);
    body.vertex_groups = {{"Bone", one}};
    Mesh copy = body;
    copy.vertex_groups.clear();
    std::vector<float> mask(n, 0.0f);
    size_t masked = 0;
    for (size_t v = 0; v < n; v++)
      if (std::fabs(copy.positions[v].y - 1.0f) < 0.2f) mask[v] = 1.0f, masked++;
    copy.vertex_groups.push_back({kInpaintMask, mask});
    InpaintReport rep;
    std::string err;
    CHECK(transfer_skin_weights(body, copy, InpaintOptions{}, &rep, &err));
    std::printf("    %zu masked: %zu matched, %zu inpainted; the mask group kept: %d\n", masked, rep.known, rep.inpainted, copy.find_group(kInpaintMask) >= 0);
    CHECK(masked > 0 && rep.inpainted == masked && rep.known == n - masked);
    CHECK(copy.find_group(kInpaintMask) >= 0 && r81_weight(copy, "Bone", n / 2) > 0.999f);
  });

  test("skin weights: automatic weights by inpainting - a hand hanging beside the thigh takes none of the leg, and a bent tube splits at the joint", [] {
    /* A hip block's leg and an arm hanging beside it (a loose part of the same mesh), with a bone in each. */
    Mesh m;
    r81_tube(m, {0, 0, 0}, {0, 1, 0}, 0.12f, 12, 12);          // the leg
    const size_t leg_n = m.vert_count();
    r81_tube(m, {0.22f, 0.3f, 0}, {0.22f, 1.2f, 0}, 0.08f, 12, 12);  // the arm, 0.02 from the leg's surface
    const std::vector<BoneSegment> bones{{"Leg", {0, 0, 0}, {0, 1, 0}}, {"Arm", {0.22f, 1.2f, 0}, {0.22f, 0.3f, 0}}};
    Mesh env = m;
    std::vector<float> e;
    float env_leg_on_arm = 0.0f;
    for (size_t v = leg_n; v < env.vert_count(); v++) {
      skinweights::envelope_weights(env.positions[v], bones, e);
      env_leg_on_arm = std::max(env_leg_on_arm, e[0]);
    }
    InpaintReport rep;
    std::string err;
    CHECK(automatic_skin_weights(m, bones, InpaintOptions{}, &rep, &err));
    float leg_on_arm = 0.0f;
    for (size_t v = leg_n; v < m.vert_count(); v++) leg_on_arm = std::max(leg_on_arm, r81_weight(m, "Leg", v));
    std::printf("    leg weight on the arm: distance weights %.3f, inpainted %.4f (%zu known, %zu inpainted, %zu by distance)\n", env_leg_on_arm, leg_on_arm,
                rep.known, rep.inpainted, rep.fallback);
    CHECK(env_leg_on_arm > 0.05f && leg_on_arm < 0.01f);
    CHECK(r81_partition_error(m) < 1e-3f);
    /* A two-bone tube: whole at each end, shared around the joint, smooth along it. */
    Mesh tube;
    r81_tube(tube, {0, 0, 0}, {0, 2, 0}, 0.2f, 20, 12);
    const std::vector<BoneSegment> two{{"Lower", {0, 0, 0}, {0, 1, 0}}, {"Upper", {0, 1, 0}, {0, 2, 0}}};
    CHECK(automatic_skin_weights(tube, two, InpaintOptions{}, &rep, &err));
    const float bottom = r81_weight(tube, "Lower", 0), top = r81_weight(tube, "Upper", tube.vert_count() - 1), mid = r81_weight(tube, "Lower", 10 * 12);
    float jump = 0.0f;
    for (int r = 1; r <= 20; r++) jump = std::max(jump, std::fabs(r81_weight(tube, "Lower", (size_t)r * 12) - r81_weight(tube, "Lower", (size_t)(r - 1) * 12)));
    std::printf("    two-bone tube: Lower %.3f at the bottom, %.3f at the joint; Upper %.3f at the top; largest step %.3f\n", bottom, mid, top, jump);
    CHECK(bottom > 0.999f && top > 0.999f && mid > 0.2f && mid < 0.8f && jump < 0.35f);
    CHECK(r81_partition_error(tube) < 1e-3f);
  });
  test("skin weights: `skin bind` inpaints the Blender tube's weights at once, a large mesh's on a worker thread, each one undo step", [] {
    Editor ed;
    ed.init_headless(1200, 800);
    ImportResult r;
    CHECK(import_model("tests/fixtures/rig_tube.glb", r));
    instantiate_import(ed.scene(), r, "Rig Import", nullptr);
    ed.mark_changed_for_test("Import");
    ed.step_frame_headless();
    GameObject *tube = ed.scene().find_by_name("Tube");
    CHECK(tube && tube->get<SkinModifier>());
    if (!tube || !tube->get<SkinModifier>()) return;
    /* Spoil the weights (an undo step, so undo comes back here), then bind again (at once: the tube is small). */
    {
      Mesh &m = *mesh_make_mutable(tube->mesh_filter()->mesh);
      for (VertexGroup &g : m.vertex_groups) std::fill(g.weights.begin(), g.weights.end(), 0.5f);
      m.touch();
    }
    ed.mark_changed_for_test("Spoil the weights");
    ed.step_frame_headless();  // (one frame's changes are one undo step: the bind below gets its own)
    ed.command("skin bind Tube");
    const Mesh &m = *tube->mesh_filter()->mesh;
    const float now = r81_partition_error(m);
    std::printf("    bound at once: partition error %.2g, Lower at the bottom %.3f\n", now, r81_weight(m, "Lower", 0));
    CHECK(!ed.skin_job_running_for_test() && now < 1e-3f && r81_weight(m, "Lower", 0) > 0.99f);
    ed.command("undo");
    tube = ed.scene().find_by_name("Tube");
    CHECK(tube && std::fabs(r81_weight(*tube->mesh_filter()->mesh, "Lower", 0) - 0.5f) < 1e-6f);  // the spoiled ones back
    /* The same on a worker thread: frames go on while it solves; it lands as one undo step. */
    ed.set_skin_async_min_verts_for_test(0);
    ed.command("skin bind Tube");
    int frames = 0;
    while (ed.skin_job_running_for_test() && frames < 2000) ed.step_frame_headless(), frames++;
    tube = ed.scene().find_by_name("Tube");
    CHECK(tube != nullptr);
    if (!tube) return;
    const Mesh &m2 = *tube->mesh_filter()->mesh;
    std::printf("    on a worker thread: landed after %d frame(s); partition error %.2g\n", frames, r81_partition_error(m2));
    CHECK(!ed.skin_job_running_for_test() && r81_partition_error(m2) < 1e-3f && r81_weight(m2, "Lower", 0) > 0.99f);
    ed.command("undo");
    tube = ed.scene().find_by_name("Tube");
    CHECK(tube && std::fabs(r81_weight(*tube->mesh_filter()->mesh, "Lower", 0) - 0.5f) < 1e-6f);
    /* Envelope stays available. */
    ed.command("skin bind Tube envelope");
    CHECK(r81_partition_error(*ed.scene().find_by_name("Tube")->mesh_filter()->mesh) < 1e-3f);
  });

  test("skin weights: Transfer Weights puts the body's weights on a sleeve, gives it the body's skeleton, and the sleeve bends with the arm", [] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.record_buttons_for_test(true);
    ImportResult r;
    CHECK(import_model("tests/fixtures/rig_tube.glb", r));
    instantiate_import(ed.scene(), r, "Rig Import", nullptr);
    GameObject *tube = ed.scene().find_by_name("Tube");
    CHECK(tube != nullptr);
    if (!tube) return;
    /* Two sleeves round the tube, 5 % wider, where the tube is. */
    auto make_sleeve = [&](const char *name) {
      auto mesh = std::make_shared<Mesh>(*tube->mesh_filter()->mesh);
      mesh->vertex_groups.clear();
      for (Vec3 &p : mesh->positions) p = Vec3(p.x * 1.05f, p.y, p.z * 1.05f);  // (the tube runs along its Y)
      GameObject *s = ed.scene().create(name, nullptr);
      s->add<MeshFilter>()->mesh = mesh;
      s->add<MeshRenderer>();
      s->set_world_matrix(tube->world_matrix());
      return s;
    };
    make_sleeve("SleeveA");
    make_sleeve("SleeveB");
    ed.step_frame_headless();
    /* By console. */
    ed.command("skin transfer Tube SleeveA");
    GameObject *a = ed.scene().find_by_name("SleeveA");
    const SkinModifier *sk = a ? a->get<SkinModifier>() : nullptr;
    CHECK(sk && sk->object == tube->get<SkinModifier>()->object);
    CHECK(a && r81_partition_error(*a->mesh_filter()->mesh) < 1e-3f);
    /* By the Rigging window: drag the Tube from the Hierarchy onto From, then Transfer Weights. */
    ed.command("select Tube");  // (shows its row: its parents expand)
    ed.command("select SleeveB");
    ed.command("window Rigging");
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    auto at = [](platform::EventType t, int x, int y) {
      platform::Event e;
      e.type = t, e.x = x, e.y = y;
      return e;
    };
    using ET = platform::EventType;
    int hx = 0, hy = 0;
    Recti f;
    const bool row = ed.hierarchy_row_for_test("Tube", hx, hy), field = ed.button_rect_for_test("field:skin_transfer_from", f);
    std::printf("    the Tube's row shown: %d, the From field: %d\n", (int)row, (int)field);
    const bool ready = row && field;
    CHECK(ready);
    if (!ready) return;
    const int fx = f.x + f.w / 2, fy = f.y + f.h / 2;
    ed.step_frame_headless({at(ET::MouseMove, hx, hy)});
    ed.step_frame_headless({at(ET::MouseDown, hx, hy)});
    for (int k = 1; k <= 4; k++) ed.step_frame_headless({at(ET::MouseMove, hx + (fx - hx) * k / 4, hy + (fy - hy) * k / 4)});
    ed.step_frame_headless({at(ET::MouseUp, fx, fy)});
    ed.step_frame_headless();
    Recti b;
    CHECK(ed.button_rect_for_test("Transfer Weights", b));
    ed.step_frame_headless({at(ET::MouseMove, b.x + b.w / 2, b.y + b.h / 2)});
    ed.step_frame_headless({at(ET::MouseDown, b.x + b.w / 2, b.y + b.h / 2)});
    ed.step_frame_headless({at(ET::MouseUp, b.x + b.w / 2, b.y + b.h / 2)});
    ed.step_frame_headless();
    GameObject *sb = ed.scene().find_by_name("SleeveB");
    CHECK(sb && sb->get<SkinModifier>() && sb->mesh_filter()->mesh->find_group("Upper") >= 0);
    /* Bend the arm: the sleeve's top follows; its bottom stays. */
    GameObject *upper = ed.scene().find_by_name("Upper");
    CHECK(upper != nullptr);
    if (!upper || !sb) return;
    upper->set_local_rotation(upper->local().rotation * Quat::axis_angle({1, 0, 0}, 1.0f));
    const Mesh &base = *sb->mesh_filter()->mesh;
    const Mesh *e = sb->evaluated_mesh();
    float top = 0.0f, bottom = 0.0f;
    for (size_t v = 0; v < base.vert_count(); v++) {
      const float d = length(e->positions[v] - base.positions[v]);
      if (r81_weight(base, "Upper", v) > 0.99f) top = std::max(top, d);
      if (r81_weight(base, "Lower", v) > 0.99f) bottom = std::max(bottom, d);
    }
    std::printf("    posed: the sleeve's upper part moved %.3f, its lower part %.4f\n", top, bottom);
    CHECK(top > 0.1f && bottom < 1e-3f);
  });
  test("skin weights: a transfer onto a renumbered copy finds each vertex's closest point and gives back the same weights", [] {
    Mesh body;
    r81_tube(body, {0, 0, 0}, {0, 2, 0}, 0.3f, 16, 12);
    const size_t n = body.vert_count();
    std::vector<float> high(n);
    for (size_t v = 0; v < n; v++) high[v] = std::max(0.0f, std::min(1.0f, body.positions[v].y - 0.5f));
    std::vector<float> low(n);
    for (size_t v = 0; v < n; v++) low[v] = 1.0f - high[v];
    body.vertex_groups = {{"Low", low}, {"High", high}};
    /* The copy's vertices in reverse order (so not "the same mesh by index") and 2 mm out (so not "the same position"):
     * each one's weights come from the closest point of the body's surface. Its faces are renumbered to match. */
    Mesh copy;
    for (size_t v = 0; v < n; v++) {
      const Vec3 p = body.positions[n - 1 - v];
      copy.add_vert({p.x * 1.0067f, p.y, p.z * 1.0067f});
    }
    for (size_t f = 0; f < body.face_count(); f++) {
      const uint32_t *q = body.face_verts(f);
      copy.add_face({(uint32_t)(n - 1 - q[0]), (uint32_t)(n - 1 - q[1]), (uint32_t)(n - 1 - q[2]), (uint32_t)(n - 1 - q[3])});
    }
    InpaintReport rep;
    std::string err;
    CHECK(transfer_skin_weights(body, copy, InpaintOptions{}, &rep, &err));
    float worst = 0.0f;
    for (size_t v = 0; v < n; v++) worst = std::max(worst, std::fabs(r81_weight(copy, "High", n - 1 - v) - high[v]));
    std::printf("    %zu matched; worst difference %.2g\n", rep.known, worst);
    CHECK(rep.known == n && worst < 1e-4f);
  });

  test("skin weights: a hand below its arm bone's end, beside the thigh, takes no leg weight - the leg bone can't see it", [] {
    Mesh m;
    r81_tube(m, {0, 0, 0}, {0, 1, 0}, 0.12f, 12, 12);  // the leg
    const size_t leg_n = m.vert_count();
    r81_tube(m, {0.22f, 0.3f, 0}, {0.22f, 1.2f, 0}, 0.08f, 18, 12);  // the arm with its hand at the bottom
    /* The arm bone stops above the hand: the hand's vertices are nearer the leg bone, through the leg's surface. */
    const std::vector<BoneSegment> bones{{"Leg", {0, 0, 0}, {0, 1, 0}}, {"Arm", {0.22f, 1.2f, 0}, {0.22f, 0.7f, 0}}};
    std::vector<float> e;
    float env_hand = 0.0f;
    for (size_t v = leg_n; v < m.vert_count(); v++)
      if (m.positions[v].y < 0.6f) skinweights::envelope_weights(m.positions[v], bones, e), env_hand = std::max(env_hand, e[0]);
    InpaintReport rep;
    std::string err;
    CHECK(automatic_skin_weights(m, bones, InpaintOptions{}, &rep, &err));
    float hand = 0.0f;
    for (size_t v = leg_n; v < m.vert_count(); v++)
      if (m.positions[v].y < 0.6f) hand = std::max(hand, r81_weight(m, "Leg", v));
    std::printf("    the hand's leg weight: by distance %.3f, inpainted %.4f\n", env_hand, hand);
    CHECK(env_hand > 0.5f && hand < 0.01f);
  });

  test("skin weights: short spine bones in a wide torso each get their own band and blend into the next (no hard steps)", [] {
    Mesh m;
    r81_tube(m, {0, 0, 0}, {0, 1, 0}, 0.15f, 40, 16);
    std::vector<BoneSegment> bones;
    const char *names[] = {"Hips", "Spine", "Chest", "Neck", "Head"};
    for (int i = 0; i < 5; i++) bones.push_back({names[i], {0, 0.2f * (float)i, 0}, {0, 0.2f * (float)(i + 1), 0}});
    InpaintReport rep;
    std::string err;
    CHECK(automatic_skin_weights(m, bones, InpaintOptions{}, &rep, &err));
    float step = 0.0f, weakest = 1.0f;
    for (const char *nm : names) {
      float most = 0.0f;
      for (size_t v = 0; v < m.vert_count(); v++) most = std::max(most, r81_weight(m, nm, v));
      weakest = std::min(weakest, most);
      for (int r = 1; r <= 40; r++) step = std::max(step, std::fabs(r81_weight(m, nm, (size_t)r * 16) - r81_weight(m, nm, (size_t)(r - 1) * 16)));
    }
    std::printf("    %zu trusted, %zu inpainted; every bone's strongest weight at least %.2f; largest step between rings %.3f\n", rep.known, rep.inpainted,
                weakest, step);
    CHECK(weakest > 0.5f && step < 0.35f && r81_partition_error(m) < 1e-3f);
  });

  test("skin weights: a NaN vertex, a wire edge and a zero-area face leave every vertex weighted, summing to 1, still by the direct solver", [] {
    Mesh m;
    r81_tube(m, {0, 0, 0}, {0, 2, 0}, 0.2f, 20, 12);
    const uint32_t a = m.add_vert({0.5f, 1.0f, 0.0f}), b = m.add_vert({0.6f, 1.0f, 0.0f});
    m.add_loose_edge(a, b);  // a wire hanging off nothing
    const uint32_t z0 = m.add_vert({1, 1, 1}), z1 = m.add_vert({1, 1, 1}), z2 = m.add_vert({1, 1, 1});
    m.add_face({z0, z1, z2});  // no area
    m.positions[5] = Vec3(std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f);
    m.sync_attributes();
    const std::vector<BoneSegment> two{{"Lower", {0, 0, 0}, {0, 1, 0}}, {"Upper", {0, 1, 0}, {0, 2, 0}}};
    InpaintReport rep;
    std::string err;
    CHECK(automatic_skin_weights(m, two, InpaintOptions{}, &rep, &err));
    bool finite = true;
    for (const VertexGroup &g : m.vertex_groups)
      for (float w : g.weights) finite = finite && finite_bits(w);
    std::printf("    %zu kept their first guess; solver: %s; partition error %.2g\n", rep.fallback, rep.solver, r81_partition_error(m));
    CHECK(finite && r81_partition_error(m) < 1e-3f && rep.fallback >= 5);
#ifdef BL_WITH_EIGEN
    CHECK(std::string(rep.solver) == "Eigen sparse Cholesky");  // (not thrown to conjugate gradients by a zero pivot)
#endif
  });

  test("skin weights: a source's InpaintMask isn't transferred or counted - the target's bone weights still sum to 1", [] {
    Mesh body;
    r81_tube(body, {0, 0, 0}, {0, 2, 0}, 0.3f, 16, 12);
    const size_t n = body.vert_count();
    std::vector<float> low(n), high(n), mask(n, 0.0f);
    for (size_t v = 0; v < n; v++) {
      high[v] = std::max(0.0f, std::min(1.0f, body.positions[v].y - 0.5f)), low[v] = 1.0f - high[v];
      if (body.positions[v].y > 1.8f) mask[v] = 1.0f;  // a collar painted on the body
    }
    body.vertex_groups = {{"Low", low}, {"High", high}, {kInpaintMask, mask}};
    Mesh cloth = body;
    cloth.vertex_groups.clear();
    for (Vec3 &p : cloth.positions) p = Vec3(p.x * 1.03f, p.y, p.z * 1.03f);
    InpaintReport rep;
    std::string err;
    CHECK(transfer_skin_weights(body, cloth, InpaintOptions{}, &rep, &err));
    CHECK(cloth.find_group(kInpaintMask) < 0 && cloth.vertex_groups.size() == 2 && r81_partition_error(cloth) < 1e-3f);
  });

  test("skin weights: a background bind can be cancelled, is dropped by an undo, and an editor closed mid-solve waits for it", [] {
    auto setup = [](Editor &ed, int rings) {
      ed.init_headless(1000, 700);
      ed.command("create Armature");
      GameObject *arm = ed.scene().find_by_name("Armature");
      GameObject *g = ed.scene().create("R81Body");
      auto mesh = std::make_shared<Mesh>();
      r81_tube(*mesh, {0, 0, 0}, {0, 2, 0}, 0.2f, rings, 64);
      std::vector<float> half(mesh->vert_count(), 0.5f);
      mesh->vertex_groups.push_back({arm && !arm->children.empty() ? arm->children[0]->name : "Bone", half});
      g->add<MeshFilter>()->mesh = mesh;
      g->add<MeshRenderer>();
      g->add<SkinModifier>()->object = arm ? arm->name : "Armature";
      ed.mark_changed_for_test("R81 body");
      ed.step_frame_headless();
      ed.set_skin_async_min_verts_for_test(0);
      return g;
    };
    auto first_weight = [](Editor &ed) {
      GameObject *g = ed.scene().find_by_name("R81Body");
      return g && !g->mesh_filter()->mesh->vertex_groups.empty() ? g->mesh_filter()->mesh->vertex_groups[0].weights[0] : -1.0f;
    };
    auto settle = [](Editor &ed) {
      for (int i = 0; i < 4000 && ed.skin_job_running_for_test(); i++) ed.step_frame_headless();
    };
    {
      Editor ed;
      setup(ed, 300);
      ed.command("skin bind R81Body");
      ed.command("skin cancel");
      settle(ed);
      std::printf("    cancelled: the first weight %.2f (0.50 before)\n", first_weight(ed));
      CHECK(!ed.skin_job_running_for_test() && std::fabs(first_weight(ed) - 0.5f) < 1e-6f);
    }
    {
      Editor ed;
      setup(ed, 300);
      ed.command("skin bind R81Body");
      ed.command("undo");  // (back past the body: the solve was for a scene that's gone)
      settle(ed);
      CHECK(!ed.skin_job_running_for_test() && ed.scene().find_by_name("R81Body") == nullptr);
    }
    {
      Editor ed;
      setup(ed, 300);
      ed.command("skin bind R81Body");
    }  // (its destructor cancels and joins the worker)
    CHECK(true);
  });
}

static void tests_round82() {
  /* ---------------------------------------------------------------- round 82: the marker auto-rig (task 0058) */
  /* A 1.7 m mannequin in a T-pose facing +Z (left on -X), as one mesh of tubes. */
  auto mannequin = [](Editor &ed) {
    auto m = std::make_shared<Mesh>();
    for (float sx : {-1.0f, 1.0f}) {
      r81_tube(*m, {0.1f * sx, 0.0f, 0}, {0.1f * sx, 0.92f, 0}, 0.07f, 18, 12);    // a leg
      r81_tube(*m, {0.15f * sx, 1.42f, 0}, {0.78f * sx, 1.42f, 0}, 0.05f, 24, 12);  // an arm
    }
    r81_tube(*m, {0, 0.85f, 0}, {0, 1.5f, 0}, 0.17f, 14, 16);   // the torso
    r81_tube(*m, {0, 1.5f, 0}, {0, 1.72f, 0}, 0.1f, 6, 12);     // the head
    GameObject *g = ed.scene().create("Mannequin");
    g->add<MeshFilter>()->mesh = m;
    g->add<MeshRenderer>();
    ed.command("select Mannequin");
    ed.mark_changed_for_test("R82 mannequin");
    ed.step_frame_headless();
    return g;
  };
  auto near_bone = [](Editor &ed, const char *bone, Vec3 at, float tolerance) {
    GameObject *b = ed.scene().find_by_name(bone);
    const float d = b ? length(b->world_position() - at) : 1e9f;
    if (!(d <= tolerance)) std::printf("    %s: %.3f from (%.2f, %.2f, %.2f)\n", bone, d, at.x, at.y, at.z);
    return d <= tolerance;
  };

  test("auto-rig: a humanoid's markers give a skeleton at its joints, weighted and posable, undone in one step (Mixamo's way)", [=] {
    Editor ed;
    ed.init_headless(1200, 800);
    GameObject *body = mannequin(ed);
    ed.command("autorig humanoid");
    CHECK(ed.autorig_for_test().active);
    /* One side placed by hand (the left, on -X); Symmetry places the right. */
    for (const char *line : {"autorig mark Chin 0 1.55 0", "autorig mark Groin 0 0.88 0", "autorig mark LeftWrist -0.7 1.42 0",
                             "autorig mark LeftElbow -0.45 1.42 0", "autorig mark LeftKnee -0.1 0.48 0"})
      ed.command(line);
    CHECK(ed.autorig_next_for_test().empty());
    ed.step_frame_headless();
    ed.command("autorig build");
    ed.step_frame_headless();
    const float tol = 0.02f * 1.72f;  // 2 % of the height
    CHECK(near_bone(ed, "LeftLowerArm", {-0.45f, 1.42f, 0}, tol) && near_bone(ed, "RightLowerArm", {0.45f, 1.42f, 0}, tol));
    CHECK(near_bone(ed, "LeftHand", {-0.7f, 1.42f, 0}, tol) && near_bone(ed, "RightHand", {0.7f, 1.42f, 0}, tol));
    CHECK(near_bone(ed, "LeftLowerLeg", {-0.1f, 0.48f, 0}, tol) && near_bone(ed, "RightLowerLeg", {0.1f, 0.48f, 0}, tol));
    CHECK(near_bone(ed, "Head", {0, 1.55f, 0}, tol) && near_bone(ed, "LeftUpperLeg", {-0.1f, 0.88f + 0.03f * 1.72f, 0}, tol));
    CHECK(ed.scene().find_by_name("LeftLowerArm") && ed.scene().find_by_name("LeftLowerArm")->get<IKConstraint>());
    body = ed.scene().find_by_name("Mannequin");
    const SkinModifier *sk = body ? body->get<SkinModifier>() : nullptr;
    CHECK(sk && ed.scene().find_by_name(sk->object) && body->mesh_filter()->mesh->find_group("LeftHand") >= 0);
    if (!sk) return;
    const Mesh &m = *body->mesh_filter()->mesh;
    std::printf("    %zu vertices, %zu groups; partition error %.2g\n", m.vert_count(), m.vertex_groups.size(), r81_partition_error(m));
    CHECK(r81_partition_error(m) < 1e-3f);
    /* Bend the left forearm: the hand moves, the hips don't. */
    GameObject *fore = ed.scene().find_by_name("LeftLowerArm");
    for (GameObject *ik_t : {ed.scene().find_by_name("LeftHandIK")})
      if (ik_t) ik_t->set_world_position(ik_t->world_position() + Vec3(0, 0.3f, 0));  // (IK drives the forearm: move its target)
    solve_constraints(ed.scene());
    const Mesh *e = body->evaluated_mesh();
    const Mat4 &w = body->world_matrix();
    float hand = 0.0f, hips = 0.0f;
    for (size_t v = 0; v < m.vert_count(); v++) {
      const Vec3 p = m.positions[v];
      const float d = length(w.point(e->positions[v]) - w.point(p));
      if (p.x < -0.68f) hand = std::max(hand, d);
      if (std::fabs(p.x) < 0.2f && p.y > 0.8f && p.y < 1.0f) hips = std::max(hips, d);
    }
    std::printf("    the left hand moved %.3f, the hips %.4f (forearm %s)\n", hand, hips, fore ? "found" : "missing");
    CHECK(hand > 0.05f && hips < 1e-3f);
    /* One undo: the rig and its bind went in together (a small mesh binds at once, in the same frame; a large one's
     * bind lands later as its own step). */
    ed.command("undo");
    body = ed.scene().find_by_name("Mannequin");
    CHECK(!ed.scene().find_by_name("LeftLowerArm") && body && !body->get<SkinModifier>() && body->mesh_filter()->mesh->find_group("LeftHand") < 0);
  });

  test("auto-rig: a quadruped's markers give a spine, neck, tail and four legs where they are; a front leg moves its paw, not the head", [=] {
    Editor ed;
    ed.init_headless(1200, 800);
    auto m = std::make_shared<Mesh>();
    r81_tube(*m, {0, 0.6f, -0.42f}, {0, 0.6f, 0.4f}, 0.15f, 16, 16);      // the body, nose towards +Z
    r81_tube(*m, {0, 0.65f, 0.38f}, {0, 0.82f, 0.68f}, 0.07f, 8, 12);     // neck and head
    r81_tube(*m, {0, 0.62f, -0.42f}, {0, 0.76f, -0.8f}, 0.025f, 8, 8);    // the tail
    for (float sx : {-1.0f, 1.0f})
      for (float z : {-0.3f, 0.3f}) r81_tube(*m, {0.1f * sx, 0.55f, z}, {0.1f * sx, 0.0f, z}, 0.04f, 14, 10);  // the legs
    GameObject *dog = ed.scene().create("Dog");
    dog->add<MeshFilter>()->mesh = m;
    dog->add<MeshRenderer>();
    ed.command("select Dog");
    ed.mark_changed_for_test("R82 dog");
    ed.step_frame_headless();
    ed.command("autorig quadruped");
    for (const char *line : {"autorig mark Nose 0 0.8 0.66", "autorig mark Skull 0 0.74 0.55", "autorig mark Withers 0 0.65 0.3",
                             "autorig mark Hips 0 0.6 -0.3", "autorig mark TailBase 0 0.62 -0.42", "autorig mark TailTip 0 0.75 -0.78",
                             "autorig mark FrontLeftKnee -0.1 0.3 0.3", "autorig mark FrontLeftPaw -0.1 0.03 0.3",
                             "autorig mark HindLeftHock -0.1 0.3 -0.3", "autorig mark HindLeftPaw -0.1 0.03 -0.3"})
      ed.command(line);
    CHECK(ed.autorig_next_for_test().empty());
    ed.command("autorig build");
    ed.step_frame_headless();
    const float tol = 0.02f * 0.82f;
    CHECK(near_bone(ed, "FrontLeftLowerLeg", {-0.1f, 0.3f, 0.3f}, tol) && near_bone(ed, "FrontRightLowerLeg", {0.1f, 0.3f, 0.3f}, tol));
    CHECK(near_bone(ed, "HindLeftLowerLeg", {-0.1f, 0.3f, -0.3f}, tol) && near_bone(ed, "HindRightLowerLeg", {0.1f, 0.3f, -0.3f}, tol));
    CHECK(near_bone(ed, "Head", {0, 0.74f, 0.55f}, tol) && near_bone(ed, "Neck", {0, 0.65f, 0.3f}, tol) && near_bone(ed, "Tail", {0, 0.62f, -0.42f}, tol));
    CHECK(ed.scene().find_by_name("Tail2") && ed.scene().find_by_name("FrontLeftLowerLeg")->get<IKConstraint>());
    dog = ed.scene().find_by_name("Dog");
    const Mesh &mesh = *dog->mesh_filter()->mesh;
    CHECK(r81_partition_error(mesh) < 1e-3f);
    /* Lift the front left foot: its paw follows; the head stays. */
    if (GameObject *t = ed.scene().find_by_name("FrontLeftFootIK")) t->set_world_position(t->world_position() + Vec3(0, 0.15f, 0.1f));
    solve_constraints(ed.scene());
    const Mesh *e = dog->evaluated_mesh();
    float paw = 0.0f, head = 0.0f;
    for (size_t v = 0; v < mesh.vert_count(); v++) {
      const Vec3 p = mesh.positions[v];
      const float d = length(e->positions[v] - p);
      if (p.x < -0.05f && p.z > 0.2f && p.y < 0.1f) paw = std::max(paw, d);
      if (p.z > 0.55f && p.y > 0.7f) head = std::max(head, d);
    }
    std::printf("    the front left paw moved %.3f, the head %.4f\n", paw, head);
    CHECK(paw > 0.05f && head < 1e-3f);
  });

  test("auto-rig: clicks in the front view put markers inside the body (mid-depth), Symmetry mirrors them, Remove Last takes one back", [=] {
    Editor ed;
    ed.init_headless(1200, 800);
    mannequin(ed);
    ed.command("autorig humanoid");
    for (int i = 0; i < 90; i++) ed.step_frame_headless();  // (the view turns to the front)
    auto click_at = [&](Vec3 world) {
      int x = 0, y = 0;
      if (!ed.project_to_window(world, x, y)) return false;
      platform::Event mv, d, u;
      mv.type = platform::EventType::MouseMove, d.type = platform::EventType::MouseDown, u.type = platform::EventType::MouseUp;
      mv.x = d.x = u.x = x, mv.y = d.y = u.y = y;
      ed.step_frame_headless({mv});
      ed.step_frame_headless({d});
      ed.step_frame_headless({u});
      return true;
    };
    CHECK(click_at({0, 1.6f, 0.1f}));  // the chin, on the head's front surface
    CHECK(click_at({-0.7f, 1.42f, 0.05f}));  // the left wrist
    const MarkerMap &mk = ed.autorig_for_test().markers;
    const bool have = mk.count("Chin") && mk.count("LeftWrist") && mk.count("RightWrist");
    CHECK(have);
    if (!have) return;
    const Vec3 c = mk.at("Chin"), lw = mk.at("LeftWrist"), rw = mk.at("RightWrist");
    std::printf("    chin (%.3f, %.3f, %.3f), left wrist (%.3f, %.3f, %.3f), right wrist (%.3f, %.3f, %.3f)\n", c.x, c.y, c.z, lw.x, lw.y, lw.z, rw.x, rw.y,
                rw.z);
    CHECK(std::fabs(c.z) < 0.02f && std::fabs(lw.z) < 0.01f);  // the middle of the head and of the arm, not their front
    CHECK(std::fabs(lw.x + 0.7f) < 0.02f && std::fabs(rw.x - 0.7f) < 0.02f && std::fabs(rw.y - lw.y) < 1e-4f);
    ed.autorig_remove_last_for_test();
    CHECK(!ed.autorig_for_test().markers.count("LeftWrist") && !ed.autorig_for_test().markers.count("RightWrist") &&
          ed.autorig_for_test().markers.count("Chin"));
    CHECK(ed.autorig_next_for_test() == "LeftWrist");
    ed.command("autorig cancel");
    CHECK(!ed.autorig_for_test().active);
  });
  /* A dog of tubes; legs bent at the hocks and elbows unless `straight`; nose towards +Z (or -Z, `backwards`). */
  auto dog_markers = [](bool straight, bool backwards) {
    const float s = backwards ? -1.0f : 1.0f, bend = straight ? 0.0f : 0.06f;
    std::vector<std::string> lines = {
        strprintf("autorig mark Nose 0 0.8 %g", 0.66f * s), strprintf("autorig mark Skull 0 0.74 %g", 0.55f * s),
        strprintf("autorig mark Withers 0 0.65 %g", 0.3f * s), strprintf("autorig mark Hips 0 0.6 %g", -0.3f * s),
        strprintf("autorig mark TailBase 0 0.62 %g", -0.42f * s), strprintf("autorig mark TailTip 0 0.75 %g", -0.78f * s),
        strprintf("autorig mark FrontLeftKnee %g 0.3 %g", -0.1f * s, (0.3f - bend) * s), strprintf("autorig mark FrontLeftPaw %g 0.03 %g", -0.1f * s, 0.3f * s),
        strprintf("autorig mark HindLeftHock %g 0.3 %g", -0.1f * s, (-0.3f - bend) * s),
        strprintf("autorig mark HindLeftPaw %g 0.03 %g", -0.1f * s, -0.3f * s)};
    return lines;
  };
  auto make_dog = [](Editor &ed, bool backwards) {
    const float s = backwards ? -1.0f : 1.0f;
    auto m = std::make_shared<Mesh>();
    r81_tube(*m, {0, 0.6f, -0.42f * s}, {0, 0.6f, 0.4f * s}, 0.15f, 16, 16);
    r81_tube(*m, {0, 0.65f, 0.38f * s}, {0, 0.82f, 0.68f * s}, 0.07f, 8, 12);
    r81_tube(*m, {0, 0.62f, -0.42f * s}, {0, 0.76f, -0.8f * s}, 0.025f, 8, 8);
    for (float sx : {-1.0f, 1.0f})
      for (float z : {-0.3f, 0.3f}) r81_tube(*m, {0.1f * sx, 0.55f, z}, {0.1f * sx, 0.0f, z}, 0.04f, 14, 10);
    GameObject *dog = ed.scene().create("Dog");
    dog->add<MeshFilter>()->mesh = m;
    dog->add<MeshRenderer>();
    ed.command("select Dog");
    ed.mark_changed_for_test("R82 dog");
    ed.step_frame_headless();
    return dog;
  };

  test("auto-rig: bent legs and elbows stay as placed - Build Rig leaves every bone at its rest and the mesh unmoved", [=] {
    Editor ed;
    ed.init_headless(1200, 800);
    make_dog(ed, false);
    ed.command("autorig quadruped");
    for (const std::string &l : dog_markers(false, false)) ed.command(l);
    ed.command("autorig build");
    ed.step_frame_headless();
    float worst_bone = 0.0f;
    ed.scene().for_each([&](GameObject &g) {
      const Bone *b = g.get<Bone>();
      if (!b) return;
      const Quat q = g.local().rotation, r = b->rest_rotation;
      worst_bone = std::max(worst_bone, 1.0f - std::fabs(q.x * r.x + q.y * r.y + q.z * r.z + q.w * r.w));
    });
    GameObject *dog = ed.scene().find_by_name("Dog");
    const Mesh &m = *dog->mesh_filter()->mesh;
    const Mesh *e = dog->evaluated_mesh();
    float moved = 0.0f;
    for (size_t v = 0; v < m.vert_count(); v++) moved = std::max(moved, length(e->positions[v] - m.positions[v]));
    std::printf("    after Build Rig: bones off their rest by %.2g (1 - |q.r|), the mesh moved %.2g\n", worst_bone, moved);
    CHECK(worst_bone < 1e-5f && moved < 1e-4f);
    /* A humanoid with the elbows a little in front of the shoulder-wrist line: the same. */
    Editor h;
    h.init_headless(1200, 800);
    mannequin(h);
    h.command("autorig humanoid");
    for (const char *line : {"autorig mark Chin 0 1.55 0", "autorig mark Groin 0 0.88 0", "autorig mark LeftWrist -0.7 1.42 0",
                             "autorig mark LeftElbow -0.45 1.40 0.04", "autorig mark LeftKnee -0.1 0.48 0.02"})
      h.command(line);
    h.command("autorig build");
    h.step_frame_headless();
    float worst = 0.0f;
    std::string which;
    h.scene().for_each([&](GameObject &g) {
      const Bone *b = g.get<Bone>();
      if (!b) return;
      const Quat q = g.local().rotation, r = b->rest_rotation;
      const float off = 1.0f - std::fabs(q.x * r.x + q.y * r.y + q.z * r.z + q.w * r.w);
      if (off > worst) worst = off, which = g.name;
    });
    std::printf("    humanoid with bent elbows: bones off their rest by %.2g (%s)\n", worst, which.c_str());
    CHECK(worst < 1e-5f);
  });

  test("auto-rig: left and right follow the body - markers placed on the wrong side swap, and a dog facing -Z has its left on +X", [=] {
    Editor ed;
    ed.init_headless(1200, 800);
    mannequin(ed);
    ed.command("autorig humanoid");
    for (const char *line : {"autorig mark Chin 0 1.55 0", "autorig mark Groin 0 0.88 0", "autorig mark LeftWrist 0.7 1.42 0",
                             "autorig mark LeftElbow 0.45 1.42 0", "autorig mark LeftKnee 0.1 0.48 0"})  // (the "left" ones on +X)
      ed.command(line);
    ed.command("autorig build");
    GameObject *la = ed.scene().find_by_name("LeftLowerArm");
    CHECK(la && la->world_position().x < 0.0f);
    Editor ed2;
    ed2.init_headless(1200, 800);
    make_dog(ed2, true);
    ed2.command("autorig quadruped");
    for (const std::string &l : dog_markers(true, true)) ed2.command(l);
    ed2.command("autorig build");
    GameObject *fl = ed2.scene().find_by_name("FrontLeftLowerLeg");
    std::printf("    the backwards dog's front left knee at x = %.2f\n", fl ? fl->world_position().x : 0.0f);
    CHECK(fl && fl->world_position().x > 0.0f && fl->world_position().z < 0.0f);
  });

  test("auto-rig: upside-down markers and unknown names are refused; New Scene ends a session; a large mesh binds in the background", [=] {
    Editor ed;
    ed.init_headless(1200, 800);
    mannequin(ed);
    ed.command("autorig humanoid");
    ed.command("autorig mark chin 0 1.55 0");  // (lower case: not a marker's name)
    CHECK(ed.autorig_for_test().markers.empty());
    for (const char *line : {"autorig mark Chin 0 0.5 0", "autorig mark Groin 0 1.5 0", "autorig mark LeftWrist -0.7 1.42 0",
                             "autorig mark LeftElbow -0.45 1.42 0", "autorig mark LeftKnee -0.1 0.48 0"})  // the groin above the chin
      ed.command(line);
    ed.command("autorig build");
    CHECK(!ed.scene().find_by_name("Hips") && ed.autorig_for_test().active);  // refused; the markers stay to fix
    ed.command("newscene");
    CHECK(!ed.autorig_for_test().active);
    /* Background: Build lands the rig at once, the bind later as its own step. */
    Editor big;
    big.init_headless(1200, 800);
    mannequin(big);
    big.set_skin_async_min_verts_for_test(0);
    big.command("autorig humanoid");
    for (const char *line : {"autorig mark Chin 0 1.55 0", "autorig mark Groin 0 0.88 0", "autorig mark LeftWrist -0.7 1.42 0",
                             "autorig mark LeftElbow -0.45 1.42 0", "autorig mark LeftKnee -0.1 0.48 0"})
      big.command(line);
    big.command("autorig build");
    CHECK(big.scene().find_by_name("LeftLowerArm") != nullptr);
    for (int i = 0; i < 4000 && big.skin_job_running_for_test(); i++) big.step_frame_headless();
    GameObject *body = big.scene().find_by_name("Mannequin");
    CHECK(body && r81_partition_error(*body->mesh_filter()->mesh) < 1e-3f && body->mesh_filter()->mesh->find_group("LeftHand") >= 0);
    big.command("undo");  // the bind (and the rig too, when the bind landed in the frame right after Build)
    body = big.scene().find_by_name("Mannequin");
    CHECK(body && body->mesh_filter()->mesh->find_group("LeftHand") < 0);
    /* Starting again on the same mesh brings the markers back. */
    big.command("select Mannequin");
    big.command("autorig humanoid");
    CHECK(big.autorig_for_test().markers.size() == 8);
  });
}

static void tests_round83_to_round85() {
  /* ---------------------------------------------------------------- round 83: keyframe animation (task 0059) */
  test("animation: Constant holds, Linear interpolates, Auto passes through every key without overshooting, and clips round-trip", [] {
    AnimTrack t;
    t.set_key(0, 0), t.set_key(10, 10), t.set_key(20, 0);
    float worst = 0.0f;
    for (float f = 0; f <= 20.0f; f += 0.25f) worst = std::max(worst, t.sample(f));
    CHECK(std::fabs(t.sample(10) - 10.0f) < 1e-6f && worst <= 10.0f + 1e-5f);  // through the peak, never above it
    /* Rising then rising more: the span between stays within its two keys. */
    AnimTrack m;
    m.set_key(0, 0), m.set_key(10, 1), m.set_key(20, 10);
    bool inside = true;
    for (float f = 10; f <= 20.0f; f += 0.25f) inside = inside && m.sample(f) >= 1.0f - 1e-5f && m.sample(f) <= 10.0f + 1e-5f;
    CHECK(inside);
    t.keys[0].interp = AnimLinear;
    CHECK(std::fabs(t.sample(5) - 5.0f) < 1e-5f);
    t.keys[0].interp = AnimConstant;
    CHECK(t.sample(9.9f) == 0.0f && t.sample(10.0f) == 10.0f);
    CHECK(t.sample(-5) == 0.0f && t.sample(50) == 0.0f);  // held outside the keys
    AnimationClip c;
    c.name = "Walk", c.frame_rate = 30, c.length = 48, c.loop = false;
    c.track("Hips/Left Leg", "Rotation", 2).set_key(12, 45.5f, AnimLinear);
    c.track("", "Light.Intensity", 0).set_key(3, 2.0f);
    AnimationClip back;
    CHECK(load_clip_text(save_clip_text(c), back));
    CHECK(back.frame_rate == 30 && back.length == 48 && !back.loop && back.tracks.size() == 2 && back.tracks[0].path == "Hips/Left Leg" &&
          back.tracks[0].keys[0].value == 45.5f && back.tracks[0].keys[0].interp == AnimLinear && back.tracks[1].path.empty());
    CHECK(wrap_frame(70, 60, 0) == 10.0f && wrap_frame(70, 60, 1) == 60.0f && wrap_frame(70, 60, 2) == 50.0f);
  });

  /* A box with a lid, for the editor tests. */
  auto box_with_lid = [](Editor &ed) {
    std::remove(resolve_asset_path("Assets/Animations/R83Box.anim").c_str());
    std::remove(resolve_asset_path("Assets/Animations/R83Spin.anim").c_str());
    GameObject *box = ed.scene().create("R83Box");
    GameObject *lid = ed.scene().create("Lid", box);
    lid->set_local_position({0, 2, 0});
    ed.command("select R83Box");
    ed.mark_changed_for_test("R83 box");
    ed.step_frame_headless();
    return box;
  };

  test("animation: the Animation window's clip on a box - keys on the lid, Preview scrubs without undo steps and puts the pose back", [=] {
    Editor ed;
    ed.init_headless(1400, 900);
    box_with_lid(ed);
    ed.command("anim new");
    CHECK(ed.scene().find_by_name("R83Box")->get<Animation>() && ed.anim_clip_for_test() &&
          fs::exists(resolve_asset_path("Assets/Animations/R83Box.anim")));
    ed.command("anim key Lid Position 1 0 0");
    ed.command("anim key Lid Position 1 30 1");
    ed.step_frame_headless();
    const size_t steps = ed.undo_steps_for_test();
    ed.command("anim preview on");
    for (int f : {0, 15, 30}) {
      ed.command("anim frame " + std::to_string(f));
      ed.step_frame_headless();
    }
    ed.command("anim frame 15");
    GameObject *lid = ed.scene().find_by_name("Lid");
    std::printf("    lid at frame 15: y = %.3f; undo steps %zu -> %zu\n", lid->local().position.y, steps, ed.undo_steps_for_test());
    CHECK(std::fabs(lid->local().position.y - 0.5f) < 1e-4f);
    CHECK(ed.undo_steps_for_test() == steps);  // scrubbing is no edit
    ed.command("anim preview off");
    CHECK(std::fabs(ed.scene().find_by_name("Lid")->local().position.y - 2.0f) < 1e-6f);  // back where it was
    /* The file has the keys. */
    AnimationClip back;
    std::string text;
    CHECK(fs::read_file(resolve_asset_path("Assets/Animations/R83Box.anim"), text) && load_clip_text(text, back));
    CHECK(back.find("Lid", "Position", 1) && back.find("Lid", "Position", 1)->keys.size() == 2);
  });

  test("animation: Record turns a move into keys at the playhead (one undo step takes them back); renaming the clip keeps it playing", [=] {
    Editor ed;
    ed.init_headless(1400, 900);
    box_with_lid(ed);
    ed.command("anim new");
    ed.command("anim record on");
    ed.command("anim frame 20");
    ed.step_frame_headless();
    GameObject *lid = ed.scene().find_by_name("Lid");
    lid->set_local_position({0.5f, 2.0f, 0});
    ed.mark_changed_for_test("Move Lid");
    ed.step_frame_headless();
    AnimationClipPtr clip = ed.anim_clip_for_test();
    const AnimTrack *x = clip ? clip->find("Lid", "Position", 0) : nullptr;
    std::printf("    recorded: %s\n", x ? strprintf("Lid x key at %g = %g", x->keys[0].frame, x->keys[0].value).c_str() : "nothing");
    CHECK(x && x->keys.size() == 1 && x->keys[0].frame == 20.0f && x->keys[0].value == 0.5f);
    CHECK(clip && clip->find("Lid", "Position", 1) && !clip->find("", "Position", 0));  // the lid's three axes, the box untouched
    ed.command("undo");
    clip = ed.anim_clip_for_test();
    CHECK(clip && !clip->find("Lid", "Position", 0));
    /* Rename the clip in the Project window: the Animation component follows. */
    ed.command("redo");
    ed.command("renameasset Assets/Animations/R83Box.anim R83Spin");
    ed.step_frame_headless();
    const Animation *an = ed.scene().find_by_name("R83Box")->get<Animation>();
    std::printf("    after the rename: '%s'\n", an ? an->clip.c_str() : "-");
    CHECK(an && an->clip == "Assets/Animations/R83Spin.anim" && clip_asset(an->clip));
  });

  test("animation: in Play the Animation component turns a bone, and the skin follows", [] {
    /* The component on its own (a fixed step), then in the editor's Play. */
    Scene s;
    GameObject *root = s.create("R83Arm");
    GameObject *bone = s.create("Bone", root);
    std::remove(resolve_asset_path("Assets/Animations/R83Arm.anim").c_str());
    AnimationClipPtr c = create_clip_asset("Assets/Animations/R83Arm.anim", "R83Arm");
    CHECK(c != nullptr);
    if (!c) return;
    c->track("Bone", "Rotation", 0).set_key(0, 0, AnimLinear);
    c->track("Bone", "Rotation", 0).set_key(60, 90);
    Animation *an = root->add<Animation>();
    an->clip = c->asset_path;
    PlayContext ctx;
    ctx.scene = &s;
    an->start(ctx);
    ctx.dt = 0.5f;  // 30 frames at 60 per second
    an->update(ctx);
    std::printf("    after half a second: the bone at %.2f degrees\n", bone->local().euler_hint.x);
    CHECK(std::fabs(bone->local().euler_hint.x - 45.0f) < 1e-3f);
    /* The editor: the Blender tube's Upper bone, keyed, moves its half of the mesh in Play. */
    Editor ed;
    ed.init_headless(1000, 700);
    ImportResult r;
    CHECK(import_model("tests/fixtures/rig_tube.glb", r));
    instantiate_import(ed.scene(), r, "Rig Import", nullptr);
    GameObject *tube = ed.scene().find_by_name("Tube");
    GameObject *arm = tube ? ed.scene().find_by_name(tube->get<SkinModifier>()->object) : nullptr;
    CHECK(arm != nullptr);
    if (!arm) return;
    std::remove(resolve_asset_path("Assets/Animations/R83Tube.anim").c_str());
    AnimationClipPtr bend = create_clip_asset("Assets/Animations/R83Tube.anim", "R83Tube");
    const std::string path = anim_path(*arm, *ed.scene().find_by_name("Upper"));
    bend->track(path, "Rotation", 0).set_key(0, 0, AnimLinear);
    bend->track(path, "Rotation", 0).set_key(6, 60, AnimConstant);
    arm->add<Animation>()->clip = bend->asset_path;
    ed.mark_changed_for_test("R83 tube");
    ed.step_frame_headless();
    const std::vector<Vec3> rest = tube->evaluated_mesh()->positions;
    ed.command("play");
    const double t0 = now_seconds();
    while ((ed.scene_camera_for_test().animating || now_seconds() - t0 < 0.6) && now_seconds() - t0 < 20.0)
      ed.step_frame_headless();  // (until the camera's turn ends: a sanitizer build's frames are slow)
    tube = ed.scene().find_by_name("Tube");
    float moved = 0.0f;
    for (size_t v = 0; v < rest.size(); v++) moved = std::max(moved, length(tube->evaluated_mesh()->positions[v] - rest[v]));
    std::printf("    in Play the tube's vertices moved up to %.3f\n", moved);
    CHECK(moved > 0.1f);
    ed.command("stop");
  });

  test("animation: the dopesheet - clicking the ruler moves the playhead, dragging a key moves it in time, Delete removes it", [=] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.record_buttons_for_test(true);
    box_with_lid(ed);
    ed.command("window Animation");
    ed.command("anim new");
    ed.command("anim key Lid Position 1 10 1");
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    Recti sheet, ruler;
    CHECK(ed.button_rect_for_test("anim_sheet", sheet) && ed.button_rect_for_test("anim_ruler", ruler));
    if (sheet.w <= 0) return;
    const float end = std::max(30.0f, ed.anim_clip_for_test()->end_frame()) * 1.1f;  // (the sheet's span: the clip and a tenth more)
    auto x_of = [&](float f) { return sheet.x + (int)std::lround(f / end * (float)sheet.w); };
    auto click = [&](int x, int y, bool drag_to = false, int x2 = 0) {
      platform::Event m, d, mv, u;
      m.type = platform::EventType::MouseMove, d.type = platform::EventType::MouseDown, mv.type = platform::EventType::MouseMove, u.type = platform::EventType::MouseUp;
      m.x = d.x = x, m.y = d.y = mv.y = u.y = y;
      mv.x = u.x = drag_to ? x2 : x;
      ed.step_frame_headless({m});
      ed.step_frame_headless({d});
      if (drag_to) ed.step_frame_headless({mv});
      ed.step_frame_headless({u});
      ed.step_frame_headless();
    };
    click(x_of(20), ruler.y + ruler.h / 2);
    std::printf("    the playhead after clicking the ruler at 20: %g\n", ed.anim_for_test().frame);
    CHECK(std::fabs(ed.anim_for_test().frame - 20.0f) <= 1.0f);
    const int row = sheet.y + ed.ui_for_test().row_h() / 2;  // the first (only) track
    click(x_of(10), row, true, x_of(16));
    AnimationClipPtr clip = ed.anim_clip_for_test();
    const AnimTrack *t = clip ? clip->find("Lid", "Position", 1) : nullptr;
    std::printf("    the key after dragging: frame %g\n", t && !t->keys.empty() ? t->keys[0].frame : -1.0f);
    CHECK(t && t->keys.size() == 1 && std::fabs(t->keys[0].frame - 16.0f) <= 1.0f);
    platform::Event del;
    del.type = platform::EventType::KeyDown, del.key = platform::KEY_DELETE, del.x = sheet.x + sheet.w / 2, del.y = row;
    ed.step_frame_headless({del});
    ed.step_frame_headless();
    clip = ed.anim_clip_for_test();
    CHECK(clip && clip->find("Lid", "Position", 1) && clip->find("Lid", "Position", 1)->keys.empty());
  });
  test("animation: names with spaces survive the file; Ping Pong, Once, Clamp and a negative Speed wrap as they should", [] {
    AnimationClip c;
    c.track("Lamp Post/Bulb", "Light.Spot Angle", 0).set_key(5, 40.0f);
    c.track("Lamp Post/Bulb", "Light.Spot Angle", 0).set_key(10, 60.0f, AnimConstant);
    AnimationClip back;
    CHECK(load_clip_text(save_clip_text(c), back));
    const AnimTrack *t = back.find("Lamp Post/Bulb", "Light.Spot Angle", 0);
    CHECK(back.tracks.size() == 1 && t && t->keys.size() == 2 && t->keys[1].value == 60.0f && t->keys[0].interp == AnimAuto);
    AnimationClip bad;
    CHECK(load_clip_text("blendity_anim 1\ntrack\t0\tPosition\tA\nkey 12 nan 0\nkey 3 1.5 1\n", bad));
    CHECK(bad.tracks.size() == 1 && bad.tracks[0].keys.size() == 1 && bad.tracks[0].keys[0].frame == 3.0f);  // the bad line: no key
    CHECK(wrap_frame(90, 60, 2) == 30.0f && wrap_frame(130, 60, 2) == 10.0f);  // ping pong: back down, then up again
    CHECK(wrap_frame(-10, 60, 0) == 50.0f && wrap_frame(-10, 60, 2) == 10.0f);  // backwards
    CHECK(wrap_frame(75, 60, 1) == 60.0f && wrap_frame(75, 60, 3) == 60.0f && wrap_frame(-5, 60, 3) == 0.0f);
  });

  test("animation: Record keys only real changes - a rename or a new child keys nothing - and Preview puts back only what it moved", [=] {
    Editor ed;
    ed.init_headless(1400, 900);
    box_with_lid(ed);
    GameObject *cube = ed.scene().create("R83Other");
    cube->set_local_position({5, 0, 0});
    ed.command("select R83Box");
    ed.mark_changed_for_test("R83 other");
    ed.step_frame_headless();
    ed.command("anim new");
    ed.command("anim key Lid Position 1 0 0");
    ed.command("anim key Lid Position 1 30 1");
    ed.command("anim record on");
    ed.command("anim frame 15");
    ed.step_frame_headless();
    /* A rename and a new child: no keys. */
    ed.scene().find_by_name("Lid")->name = "Lid2";
    ed.mark_changed_for_test("Rename");
    ed.step_frame_headless();
    ed.scene().create("Handle", ed.scene().find_by_name("R83Box"))->set_local_position({0, 3, 0});
    ed.mark_changed_for_test("Add child");
    ed.step_frame_headless();
    AnimationClipPtr clip = ed.anim_clip_for_test();
    size_t keys = 0;
    for (const AnimTrack &t : clip->tracks) keys += t.keys.size();
    std::printf("    after a rename and a new child: %zu keys in %zu tracks\n", keys, clip->tracks.size());
    CHECK(keys == 2 && clip->tracks.size() == 1);
    ed.scene().find_by_name("Lid2")->name = "Lid";
    ed.mark_changed_for_test("Rename back");
    ed.step_frame_headless();
    /* The unrelated cube moved while previewing stays moved; the lid goes back; an undo after that doesn't bring the
     * previewed pose back. */
    ed.command("anim record off");
    ed.scene().find_by_name("R83Other")->set_local_position({7, 0, 0});
    ed.mark_changed_for_test("Move other");
    ed.step_frame_headless();
    ed.command("anim preview off");
    CHECK(ed.scene().find_by_name("R83Other")->local().position.x == 7.0f && std::fabs(ed.scene().find_by_name("Lid")->local().position.y - 2.0f) < 1e-6f);
    ed.scene().find_by_name("R83Other")->set_local_position({9, 0, 0});
    ed.mark_changed_for_test("Move other again");
    ed.step_frame_headless();
    ed.command("undo");
    std::printf("    after the undo: other x = %g, lid y = %g\n", ed.scene().find_by_name("R83Other")->local().position.x, ed.scene().find_by_name("Lid")->local().position.y);
    CHECK(ed.scene().find_by_name("R83Other")->local().position.x == 7.0f && std::fabs(ed.scene().find_by_name("Lid")->local().position.y - 2.0f) < 1e-6f);
  });

  test("animation: Preview is refused in Play, a new scene clears the window, and an undo after renaming the clip still finds it", [=] {
    Editor ed;
    ed.init_headless(1400, 900);
    box_with_lid(ed);
    ed.command("anim new");
    ed.command("anim key Lid Position 1 10 1");
    ed.step_frame_headless();
    ed.command("play");
    ed.command("anim preview on");
    CHECK(!ed.anim_for_test().preview);
    ed.command("stop");
    /* Rename, then undo a step made before it: the Animation component (restored from before) still finds the clip. */
    ed.command("anim key Lid Position 1 20 2");
    ed.step_frame_headless();
    ed.command("renameasset Assets/Animations/R83Box.anim R83Spin");
    ed.step_frame_headless();
    ed.command("undo");  // the second key
    ed.step_frame_headless();
    AnimationClipPtr clip = ed.anim_clip_for_test();
    std::printf("    after rename + undo: clip %s, %zu key(s)\n", clip ? clip->asset_path.c_str() : "-", clip && !clip->tracks.empty() ? clip->tracks[0].keys.size() : 0);
    CHECK(clip && clip->asset_path == "Assets/Animations/R83Spin.anim" && clip->tracks.size() == 1 && clip->tracks[0].keys.size() == 1);
    ed.command("newscene");
    CHECK(!ed.anim_for_test().preview && ed.anim_for_test().selected.empty());
  });
  /* ---------------------------------------------------------------- round 85: the curve editor (task 0061) */
  test("curves: Free Smooth leaves a key at its slope both sides, Broken at each side's own, Flat level; files keep them", [] {
    auto slope_after = [](const AnimTrack &t, float f) { return (t.sample(f + 0.001f) - t.sample(f)) / 0.001f; };
    auto slope_before = [](const AnimTrack &t, float f) { return (t.sample(f) - t.sample(f - 0.001f)) / 0.001f; };
    AnimTrack t;
    t.path = "Lid", t.property = "Position", t.axis = 1;
    t.set_key(0, 0), t.set_key(10, 5), t.set_key(20, 0);
    t.keys[1].tangent = TanFree, t.keys[1].in_slope = t.keys[1].out_slope = 2.0f;
    std::printf("    Free Smooth 2: slope %.3f before, %.3f after\n", slope_before(t, 10), slope_after(t, 10));
    CHECK(std::fabs(slope_before(t, 10) - 2.0f) < 0.02f && std::fabs(slope_after(t, 10) - 2.0f) < 0.02f);
    t.keys[1].tangent = TanBroken, t.keys[1].in_slope = 1.0f, t.keys[1].out_slope = -3.0f;
    CHECK(std::fabs(slope_before(t, 10) - 1.0f) < 0.02f && std::fabs(slope_after(t, 10) + 3.0f) < 0.02f);
    t.keys[1].tangent = TanFlat;
    CHECK(std::fabs(slope_before(t, 10)) < 0.02f && std::fabs(slope_after(t, 10)) < 0.02f);
    t.keys[1].tangent = TanBroken;
    AnimationClip c;
    c.tracks.push_back(t);
    AnimationClip back;
    CHECK(load_clip_text(save_clip_text(c), back) && back.tracks.size() == 1);
    const AnimKey &k = back.tracks[0].keys[1];
    CHECK(k.tangent == TanBroken && k.in_slope == 1.0f && k.out_slope == -3.0f && back.tracks[0].keys[0].tangent == TanAuto);
    /* A task-0059 file (no tangents) loads with Clamped Auto keys. */
    AnimationClip old;
    CHECK(load_clip_text("blendity_anim 1\ntrack\t1\tPosition\tLid\nkey 0 0 0\nkey 30 1 0\n", old));
    CHECK(old.tracks.size() == 1 && old.tracks[0].keys.size() == 2 && old.tracks[0].keys[1].tangent == TanAuto);
  });

  test("curves: in the Curves view dragging a key up raises its value, dragging its handle sets the slope (Free Smooth); each one undo step", [=] {
    Editor ed;
    ed.init_headless(1400, 900);
    box_with_lid(ed);
    ed.command("window Animation");
    ed.command("anim new");
    ed.command("anim key Lid Position 1 0 0");
    ed.command("anim key Lid Position 1 20 1");
    ed.command("anim key Lid Position 1 40 0");
    ed.step_frame_headless();
    /* Into the Curves view: the toolbar's Curves button. */
    ed.record_buttons_for_test(true);
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    Recti cb;
    CHECK(ed.button_rect_for_test("Curves", cb));
    auto mouse = [&](int x, int y, int x2, int y2) {
      platform::Event m, d, mv, u;
      m.type = platform::EventType::MouseMove, d.type = platform::EventType::MouseDown, mv.type = platform::EventType::MouseMove, u.type = platform::EventType::MouseUp;
      m.x = d.x = x, m.y = d.y = y, mv.x = u.x = x2, mv.y = u.y = y2;
      ed.step_frame_headless({m});
      ed.step_frame_headless({d});
      ed.step_frame_headless({mv});
      ed.step_frame_headless({u});
      ed.step_frame_headless();
    };
    mouse(cb.x + cb.w / 2, cb.y + cb.h / 2, cb.x + cb.w / 2, cb.y + cb.h / 2);
    CHECK(ed.anim_for_test().curves);
    int kx = 0, ky = 0;
    CHECK(ed.anim_curve_point_for_test(0, 20, -1, kx, ky));
    const size_t steps = ed.undo_steps_for_test();
    mouse(kx, ky, kx, ky - 40);  // the middle key, 40 px up
    AnimationClipPtr clip = ed.anim_clip_for_test();
    const float raised = clip->tracks[0].keys[1].value;
    std::printf("    the key after dragging it up: frame %g, value %.3f (was 1)\n", clip->tracks[0].keys[1].frame, raised);
    CHECK(raised > 1.05f && clip->tracks[0].keys[1].frame == 20.0f);
    /* Its out handle (the key is selected now): dragged down to the right, a falling slope. */
    int hx = 0, hy = 0;
    ed.step_frame_headless();
    CHECK(ed.anim_curve_point_for_test(0, 20, 1, hx, hy));
    mouse(hx, hy, hx, hy + 60);
    clip = ed.anim_clip_for_test();
    const AnimKey &k = clip->tracks[0].keys[1];
    std::printf("    after the handle: tangent mode %d, slope %.4f per frame\n", k.tangent, k.in_slope);
    CHECK(k.tangent == TanFree && k.in_slope < -0.001f && k.in_slope == k.out_slope);
    CHECK(ed.undo_steps_for_test() == steps + 2);
    ed.command("undo");  // the handle
    clip = ed.anim_clip_for_test();
    CHECK(clip->tracks[0].keys[1].tangent == TanAuto && std::fabs(clip->tracks[0].keys[1].value - raised) < 1e-5f);
    ed.command("undo");  // the move
    clip = ed.anim_clip_for_test();
    CHECK(std::fabs(clip->tracks[0].keys[1].value - 1.0f) < 1e-5f);
    /* The console's tangents. */
    ed.command("anim tangent Lid Position 1 20 broken 0.5 -0.25");
    clip = ed.anim_clip_for_test();
    CHECK(clip->tracks[0].keys[1].tangent == TanBroken && clip->tracks[0].keys[1].in_slope == 0.5f && clip->tracks[0].keys[1].out_slope == -0.25f);
  });
  test("curves: moving two keys of one track onto each other's frames keeps both; zooming far in on a value of 100 doesn't hang; a clip switched mid-drag gets no edit", [=] {
    Editor ed;
    ed.init_headless(1400, 900);
    box_with_lid(ed);
    ed.command("window Animation");
    ed.command("anim new");
    ed.command("anim key Lid Position 1 0 0");
    ed.command("anim key Lid Position 1 20 1");
    ed.command("anim key Lid Position 1 40 2");
    ed.step_frame_headless();
    ed.record_buttons_for_test(true);
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    Recti cb;
    CHECK(ed.button_rect_for_test("Curves", cb));
    auto ev = [](platform::EventType type, int x, int y, int mods = 0) {
      platform::Event e;
      e.type = type, e.x = x, e.y = y, e.mods = mods;
      return e;
    };
    auto click = [&](int x, int y, int mods = 0) {
      ed.step_frame_headless({ev(platform::EventType::MouseMove, x, y, mods)});
      ed.step_frame_headless({ev(platform::EventType::MouseDown, x, y, mods)});
      ed.step_frame_headless({ev(platform::EventType::MouseUp, x, y, mods)});
      ed.step_frame_headless();
    };
    click(cb.x + cb.w / 2, cb.y + cb.h / 2);
    CHECK(ed.anim_for_test().curves);
    int x0 = 0, y0 = 0, x20 = 0, y20 = 0;
    CHECK(ed.anim_curve_point_for_test(0, 0, -1, x0, y0) && ed.anim_curve_point_for_test(0, 20, -1, x20, y20));
    click(x0, y0);
    click(x20, y20, platform::MOD_SHIFT);
    CHECK(ed.anim_for_test().selected.size() == 2);
    /* Both 20 frames later: the key from 0 lands where the one from 20 was, that one where 40 was. */
    ed.step_frame_headless({ev(platform::EventType::MouseMove, x0, y0)});
    ed.step_frame_headless({ev(platform::EventType::MouseDown, x0, y0)});
    ed.step_frame_headless({ev(platform::EventType::MouseMove, x20, y0)});
    ed.step_frame_headless({ev(platform::EventType::MouseUp, x20, y0)});
    ed.step_frame_headless();
    AnimationClipPtr clip = ed.anim_clip_for_test();
    const AnimTrack &t = clip->tracks[0];
    std::printf("    after the move: %zu keys:", t.keys.size());
    for (const AnimKey &k : t.keys) std::printf(" (%g, %g)", k.frame, k.value);
    std::printf("\n");
    CHECK(t.keys.size() == 2 && t.key_at(20) >= 0 && t.key_at(40) >= 0);
    CHECK(t.key_at(20) >= 0 && t.keys[(size_t)t.key_at(20)].value == 0.0f && t.keys[(size_t)t.key_at(40)].value == 1.0f);
    CHECK(ed.anim_for_test().selected.size() == 2);
    ed.command("undo");
    CHECK(ed.anim_clip_for_test()->tracks[0].keys.size() == 3);

    /* Zooming far in round a value of 100: the axis stops at a hair, and every frame still draws. */
    ed.command("anim key Lid Position 1 20 100");
    ed.step_frame_headless();
    int kx = 0, ky = 0;
    CHECK(ed.anim_curve_point_for_test(0, 20, -1, kx, ky));
    for (int i = 0; i < 300; i++) {
      platform::Event w = ev(platform::EventType::Wheel, kx, ky);
      w.wheel_y = 3;
      ed.step_frame_headless({w});
    }
    const float span = ed.anim_for_test().v_max - ed.anim_for_test().v_min;
    std::printf("    the value axis after 300 wheel steps in: %g\n", span);
    CHECK(span >= 1e-4f && std::isfinite(span));

    /* A drag under way when another clip comes in: the new clip isn't edited, the old one isn't either. */
    AnimationClipPtr before = ed.anim_clip_for_test();
    const std::string before_text = save_clip_text(*before);
    ed.step_frame_headless({ev(platform::EventType::MouseMove, kx, ky)});
    ed.step_frame_headless({ev(platform::EventType::MouseDown, kx, ky)});
    ed.step_frame_headless({ev(platform::EventType::MouseMove, kx, ky - 50)});
    ed.command("anim new");
    AnimationClipPtr other = ed.anim_clip_for_test();
    CHECK(other && other != before);
    const std::string other_text = save_clip_text(*other);
    ed.step_frame_headless({ev(platform::EventType::MouseMove, kx, ky - 60)});
    ed.step_frame_headless({ev(platform::EventType::MouseUp, kx, ky - 60)});
    ed.step_frame_headless();
    CHECK(save_clip_text(*ed.anim_clip_for_test()) == other_text);
    CHECK(save_clip_text(*before) == before_text);
    CHECK(ed.anim_for_test().drag == 0);
  });

  test("curves: anim tangent with slopes is one undo step; an unknown mode changes nothing", [=] {
    Editor ed;
    ed.init_headless(1400, 900);
    box_with_lid(ed);
    ed.command("window Animation");
    ed.command("anim new");
    ed.command("anim key Lid Position 1 0 0");
    ed.command("anim key Lid Position 1 20 1");
    ed.command("anim key Lid Position 1 40 0");
    ed.step_frame_headless();
    const size_t steps = ed.undo_steps_for_test();
    ed.command("anim tangent Lid Position 1 20 free 0.75");
    ed.step_frame_headless();
    AnimationClipPtr clip = ed.anim_clip_for_test();
    CHECK(clip->tracks[0].keys[1].tangent == TanFree && clip->tracks[0].keys[1].in_slope == 0.75f && clip->tracks[0].keys[1].out_slope == 0.75f);
    CHECK(ed.undo_steps_for_test() == steps + 1);
    ed.command("anim tangent Lid Position 1 20 wobbly");
    ed.step_frame_headless();
    CHECK(ed.undo_steps_for_test() == steps + 1 && ed.anim_clip_for_test()->tracks[0].keys[1].tangent == TanFree);
    ed.command("anim tangent Lid Position 1 20 broken 1e30 -1e30");
    ed.step_frame_headless();
    CHECK(ed.anim_clip_for_test()->tracks[0].keys[1].in_slope == 1e6f && ed.anim_clip_for_test()->tracks[0].keys[1].out_slope == -1e6f);
    ed.command("undo");
    ed.command("undo");
    CHECK(ed.anim_clip_for_test()->tracks[0].keys[1].tangent == TanAuto);
  });
}

static void tests_round86_to_round100() {
  /* ---------------------------------------------------------------- round 86: the clip sequencer (task 0062) */
  /* Two clips on a lid: Up raises it from 0 to 6 over 60 frames (linear), Spin turns it from 0 to 90 degrees. */
  auto seq_clips = [] {
    for (const char *f : {"Assets/Animations/R86Up.anim", "Assets/Animations/R86Spin.anim", "Assets/Animations/R86Hold.anim"})
      std::remove(resolve_asset_path(f).c_str());
    clear_clip_assets();
    AnimationClipPtr up = create_clip_asset("Assets/Animations/R86Up.anim", "R86Up");
    up->track("Lid", "Position", 1).set_key(0, 0, AnimLinear);
    up->track("Lid", "Position", 1).set_key(60, 6, AnimLinear);
    up->length = 60, up->touch();
    AnimationClipPtr spin = create_clip_asset("Assets/Animations/R86Spin.anim", "R86Spin");
    spin->track("Lid", "Rotation", 1).set_key(0, 0, AnimLinear);
    spin->track("Lid", "Rotation", 1).set_key(60, 90, AnimLinear);
    spin->track("Lid", "Position", 1).set_key(0, 10, AnimConstant);
    spin->length = 60, spin->touch();
    AnimationClipPtr hold = create_clip_asset("Assets/Animations/R86Hold.anim", "R86Hold");
    hold->track("Lid", "Position", 1).set_key(0, -1, AnimConstant);
    hold->length = 60, hold->touch();
    save_dirty_clip_assets();
  };

  test("sequencer: one clip gives its own values (trimmed, at its speed); an overlap crossfades; a higher track wins", [=] {
    seq_clips();
    Sequence s;
    s.tracks.resize(2);
    SeqInstance a;
    a.clip = "Assets/Animations/R86Up.anim", a.name = "Up", a.start = 0, a.trim_in = 30, a.speed = 2;  // frames 30-60 of Up in 15 frames
    s.tracks[0].items.push_back(a);
    AnimValues v;
    sample_sequence(s, 5, v);
    const float *y = v.get("Lid", "Position", 1);
    std::printf("    trimmed at 30, speed 2, sequence frame 5: %.3f (Up at frame 40 is 4)\n", y ? *y : -99.0f);
    CHECK(y && std::fabs(*y - 4.0f) < 1e-4f);
    CHECK(std::fabs(a.duration(clip_asset(a.clip).get()) - 15.0f) < 1e-4f);
    /* Up from 0 for 60 frames, Spin from 40: the overlap 40-60 crossfades from Up to Spin. */
    s.tracks[0].items[0] = a;
    s.tracks[0].items[0].trim_in = 0, s.tracks[0].items[0].speed = 1;
    SeqInstance b;
    b.clip = "Assets/Animations/R86Spin.anim", b.name = "Spin", b.start = 40;
    s.tracks[0].items.push_back(b);
    v.clear();
    sample_sequence(s, 50, v);
    y = v.get("Lid", "Position", 1);
    const float *ry = v.get("Lid", "Rotation", 1);
    /* Halfway through the overlap: Up's 5 and Spin's 10 half and half; the rotation only Spin has is Spin's 15. */
    std::printf("    at 50 (halfway through the overlap): y %.3f (7.5), rotation %.3f (15)\n", y ? *y : -99.0f, ry ? *ry : -99.0f);
    CHECK(y && std::fabs(*y - 7.5f) < 1e-3f && ry && std::fabs(*ry - 15.0f) < 1e-3f);
    v.clear();
    sample_sequence(s, 70, v);
    CHECK(v.get("Lid", "Position", 1) && *v.get("Lid", "Position", 1) == 10.0f);  // after the overlap: Spin alone
    /* Hold on the track above, at 45-105: its -1 wins over the crossfade. */
    SeqInstance h;
    h.clip = "Assets/Animations/R86Hold.anim", h.name = "Hold", h.start = 45;
    s.tracks[1].items.push_back(h);
    v.clear();
    sample_sequence(s, 50, v);
    CHECK(*v.get("Lid", "Position", 1) == -1.0f && std::fabs(*v.get("Lid", "Rotation", 1) - 15.0f) < 1e-3f);
    /* Rotations blend the short way round: 350 to 10 through 0, not 180. */
    CHECK(std::fabs(blend_anim_value("Rotation", 350.0f, 10.0f, 0.5f) - 360.0f) < 1e-3f && blend_anim_value("Position", 350.0f, 10.0f, 0.5f) == 180.0f);
  });

  test("sequencer: a .seq round-trips; moving a clip's file keeps the sequence on it; odd lines are refused", [=] {
    seq_clips();
    Sequence s;
    s.name = "R86";
    s.tracks.resize(1);
    s.tracks[0].name = "Body Moves";
    SeqInstance a;
    a.clip = "Assets/Animations/R86Up.anim", a.name = "Up and away", a.start = 12, a.trim_in = 3, a.trim_out = 50, a.speed = 1.5f;
    s.tracks[0].items.push_back(a);
    s.triggers.push_back({"J", "Up and away", 0.4f});
    Sequence back;
    CHECK(load_sequence_text(save_sequence_text(s), back));
    CHECK(back.tracks.size() == 1 && back.tracks[0].name == "Body Moves" && back.tracks[0].items.size() == 1);
    const SeqInstance &i = back.tracks[0].items[0];
    CHECK(i.clip == a.clip && i.name == a.name && i.start == 12 && i.trim_in == 3 && i.trim_out == 50 && i.speed == 1.5f);
    CHECK(back.triggers.size() == 1 && back.triggers[0].key == "J" && back.triggers[0].instance == "Up and away" && back.triggers[0].blend == 0.4f);
    Sequence odd;
    CHECK(load_sequence_text("blendity_seq 1\nrate -5\ntrack T\nclip nan 0 -1 1\tA\tx.anim\nclip 0 0 -1 0\tB\ty.anim\nclip 1 2\nclip 0 0 -1 1\tC\t\ntrigger inf\tJ\tB\n", odd));
    CHECK(odd.frame_rate == 60.0f && odd.tracks.size() == 1 && odd.tracks[0].items.size() == 1 && odd.tracks[0].items[0].speed == 1.0f && odd.triggers.empty());
    CHECK(!load_sequence_text("not a sequence", odd));
    CHECK(trigger_key_code("j") == 'J' && trigger_key_code("Space") == ' ' && trigger_key_code("F5") == platform::KEY_F5 && trigger_key_code("Enter!") == 0);
    /* In the editor: a sequence naming Up, then Up renamed in the Project window. */
    Editor ed;
    ed.init_headless(1400, 900);
    GameObject *box = ed.scene().create("R86Box");
    ed.scene().create("Lid", box);
    ed.command("select R86Box");
    ed.step_frame_headless();
    std::remove(resolve_asset_path("Assets/Animations/R86Box Sequence.seq").c_str());
    std::remove(resolve_asset_path("Assets/Animations/R86Rise.anim").c_str());
    ed.command("seq new");
    CHECK(!ed.scene().sequence.empty() && ed.seq_for_test_sequence());  // (the scene's sequence since task 0070)
    ed.command("seq add Assets/Animations/R86Up.anim 0 0");
    ed.step_frame_headless();
    ed.command("renameasset Assets/Animations/R86Up.anim R86Rise");
    ed.step_frame_headless();
    SequencePtr q = ed.seq_for_test_sequence();
    std::printf("    after renaming the clip: the sequence's instance names '%s'\n", q && !q->tracks[0].items.empty() ? q->tracks[0].items[0].clip.c_str() : "-");
    CHECK(q && q->tracks[0].items.size() == 1 && q->tracks[0].items[0].clip == "Assets/Animations/R86Rise.anim");
    std::string text;
    CHECK(fs::read_file(resolve_asset_path(q->asset_path), text) && text.find("R86Rise.anim") != std::string::npos);
    ed.command("renameasset Assets/Animations/R86Rise.anim R86Up");  // (the other tests' clip back)
    ed.step_frame_headless();
  });

  test("sequencer: in Play a trigger's key jumps to its clip, blending from the pose of the moment", [=] {
    seq_clips();
    Scene sc;
    GameObject *root = sc.create("R86Root");
    GameObject *lid = sc.create("Lid", root);
    std::remove(resolve_asset_path("Assets/Animations/R86Play.seq").c_str());
    clear_sequence_assets();
    SequencePtr s = create_sequence_asset("Assets/Animations/R86Play.seq", "R86Play");
    SeqInstance up, hold;
    up.clip = "Assets/Animations/R86Up.anim", up.name = "Up", up.start = 0;
    hold.clip = "Assets/Animations/R86Hold.anim", hold.name = "Hold", hold.start = 100;
    s->tracks[0].items = {up, hold};
    s->triggers.push_back({"J", "Hold", 0.5f});
    s->touch();
    Sequencer *q = root->add<Sequencer>();
    q->sequence = s->asset_path;
    bool j = false;
    PlayContext ctx;
    ctx.scene = &sc;
    ctx.dt = 1.0f / 60.0f;
    ctx.key_down = [&](int k) { return j && k == 'J'; };
    q->start(ctx);
    for (int i = 0; i < 30; i++) q->update(ctx);  // half a second of Up: y 3
    const float before = lid->local().position.y;
    std::printf("    after half a second: y %.3f (3)\n", before);
    CHECK(std::fabs(before - 3.0f) < 0.15f);
    j = true;
    q->update(ctx);  // J: to Hold (-1), over half a second
    const float first = lid->local().position.y;
    for (int i = 0; i < 14; i++) q->update(ctx);
    const float mid = lid->local().position.y;
    for (int i = 0; i < 30; i++) q->update(ctx);  // (J still held: no new jump)
    const float after = lid->local().position.y;
    std::printf("    J pressed: y %.3f, a quarter second on %.3f, after the blend %.3f (-1)\n", first, mid, after);
    CHECK(first < before && first > 2.0f);         // the blend starts from the pose of the moment
    CHECK(mid < first && mid > -1.0f && std::fabs(mid - 1.0f) < 0.2f);  // halfway: (3 + -1) / 2
    CHECK(after == -1.0f && q->frame() > 100.0f);  // Hold's own value, the timeline playing on from Hold
    /* The Animation component steps aside on an object with a Sequencer. */
    Animation *an = root->add<Animation>();
    an->clip = "Assets/Animations/R86Up.anim";
    an->start(ctx);
    an->update(ctx);
    CHECK(lid->local().position.y == -1.0f);
  });

  test("sequencer: in the window, dropping a clip on a track places it, dragging a bar moves it, its end trims it; each one undo step", [=] {
    seq_clips();
    Editor ed;
    ed.init_headless(1400, 900);
    GameObject *box = ed.scene().create("R86Win");
    ed.scene().create("Lid", box);
    ed.command("select R86Win");
    ed.step_frame_headless();
    std::remove(resolve_asset_path("Assets/Animations/R86Win Sequence.seq").c_str());
    ed.command("window Sequencer");
    ed.command("seq new");
    ed.record_buttons_for_test(true);
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    const SeqEditorState &st = ed.seq_for_test();
    CHECK(st.lanes.w > 0);
    /* Up dropped on track 2 at about frame 20 (as the Project window's drag would). */
    const int drop_x = ed.seq_for_test().lanes.x + (int)std::lround(20.0f / st.end * (float)st.lanes.w);
    CHECK(ed.seq_drop_clip_for_test(resolve_asset_path("Assets/Animations/R86Up.anim"), drop_x, st.lanes.y + st.lane_h + st.lane_h / 2));
    ed.step_frame_headless();
    SequencePtr q = ed.seq_for_test_sequence();
    CHECK(q && q->tracks.size() == 2 && q->tracks[1].items.size() == 1 && std::fabs(q->tracks[1].items[0].start - 20.0f) <= 1.0f);
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    Recti bar;
    CHECK(ed.button_rect_for_test("seq_bar:R86Up", bar));
    auto drag = [&](int x, int y, int x2, int y2) {
      platform::Event m, d, mv, u;
      m.type = mv.type = platform::EventType::MouseMove, d.type = platform::EventType::MouseDown, u.type = platform::EventType::MouseUp;
      m.x = d.x = x, m.y = d.y = y, mv.x = u.x = x2, mv.y = u.y = y2;
      ed.step_frame_headless({m});
      ed.step_frame_headless({d});
      ed.step_frame_headless({mv});
      ed.step_frame_headless({u});
      ed.step_frame_headless();
    };
    float per_frame = (float)st.lanes.w / st.end;  // (the ruler fits the sequence: measured again after it grows)
    const size_t steps = ed.undo_steps_for_test();
    /* The middle, 30 frames right and up onto track 1. */
    drag(bar.x + bar.w / 2, bar.y + bar.h / 2, bar.x + bar.w / 2 + (int)std::lround(30 * per_frame), bar.y + bar.h / 2 - st.lane_h);
    q = ed.seq_for_test_sequence();
    const float moved = q->tracks[0].items.empty() ? -1.0f : q->tracks[0].items[0].start;
    std::printf("    moved: track 0 starts at %g (about 50)\n", moved);
    CHECK(q->tracks[0].items.size() == 1 && q->tracks[1].items.empty() && std::fabs(moved - 50.0f) <= 1.0f);
    CHECK(ed.undo_steps_for_test() == steps + 1);
    /* The right end, 20 frames left: Up ends at its frame 40. */
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    CHECK(ed.button_rect_for_test("seq_bar:R86Up", bar));
    per_frame = (float)st.lanes.w / st.end;
    drag(bar.right() - 2, bar.y + bar.h / 2, bar.right() - 2 - (int)std::lround(20 * per_frame), bar.y + bar.h / 2);
    q = ed.seq_for_test_sequence();
    const SeqInstance &i = q->tracks[0].items[0];
    std::printf("    trimmed: out %g (about 40), %g frames long\n", i.trim_out, i.duration(clip_asset(i.clip).get()));
    CHECK(std::fabs(i.trim_out - 40.0f) <= 1.0f && std::fabs(i.start - moved) < 1e-4f);
    CHECK(ed.undo_steps_for_test() == steps + 2);
    ed.command("undo");
    q = ed.seq_for_test_sequence();
    CHECK(q->tracks[0].items[0].trim_out < 0.0f);
    ed.command("undo");
    q = ed.seq_for_test_sequence();
    CHECK(q->tracks[1].items.size() == 1 && q->tracks[0].items.empty());
    /* Preview: scrubbing poses the lid, turning it off puts it back, with no undo step. */
    const size_t before_preview = ed.undo_steps_for_test();
    ed.command("seq preview on");
    ed.command("seq frame 50");  // Up's frame 30: y 3
    ed.step_frame_headless();
    GameObject *lid = ed.scene().find_by_name("Lid");
    CHECK(std::fabs(lid->local().position.y - 3.0f) < 1e-3f);
    ed.command("seq preview off");
    ed.step_frame_headless();
    CHECK(ed.scene().find_by_name("Lid")->local().position.y == 0.0f && ed.undo_steps_for_test() == before_preview);
    /* Triggers from the console; renaming the clip carries its trigger. */
    ed.command("seq trigger K R86Up 0.3");
    ed.command("seq trigger K Nope");
    ed.command("seq rename R86Up Rise");
    ed.step_frame_headless();
    q = ed.seq_for_test_sequence();
    CHECK(q->triggers.size() == 1 && q->triggers[0].instance == "Rise" && q->triggers[0].blend == 0.3f && q->find("Rise"));
  });
  test("sequencer: a bar inside another fades in and out within itself; Once holds the last frame; an empty Sequencer leaves the Animation playing", [=] {
    seq_clips();
    Sequence s;
    s.tracks.resize(1);
    SeqInstance a, b;
    a.clip = "Assets/Animations/R86Up.anim", a.name = "Up", a.start = 0;
    b.clip = "Assets/Animations/R86Hold.anim", b.name = "Hold", b.start = 20, b.trim_out = 20;  // 20-40, inside Up's 0-60
    s.tracks[0].items = {a, b};
    AnimValues v;
    sample_sequence(s, 30, v);  // halfway through Hold: Up's 3 and Hold's -1 half and half
    std::printf("    a bar inside another, halfway through it: %.3f (1)\n", *v.get("Lid", "Position", 1));
    CHECK(std::fabs(*v.get("Lid", "Position", 1) - 1.0f) < 1e-3f);
    /* Once: two seconds of a one-second timeline end on Up's last value. */
    Scene sc;
    GameObject *root = sc.create("R86Once");
    GameObject *lid = sc.create("Lid", root);
    std::remove(resolve_asset_path("Assets/Animations/R86Once.seq").c_str());
    SequencePtr q = create_sequence_asset("Assets/Animations/R86Once.seq", "R86Once");
    q->tracks[0].items = {a};
    Sequencer *c = root->add<Sequencer>();
    c->sequence = q->asset_path, c->wrap = 1;
    PlayContext ctx;
    ctx.scene = &sc, ctx.dt = 1.0f / 60.0f;
    c->start(ctx);
    for (int i = 0; i < 120; i++) c->update(ctx);
    std::printf("    Once after two seconds: y %.4f (6)\n", lid->local().position.y);
    CHECK(std::fabs(lid->local().position.y - 6.0f) < 1e-2f);
    /* No sequence named: the Animation component on the same object plays. */
    c->sequence.clear();
    Animation *an = root->add<Animation>();
    an->clip = "Assets/Animations/R86Hold.anim";
    an->start(ctx);
    an->update(ctx);
    CHECK(lid->local().position.y == -1.0f);
  });

  test("sequencer: the two Previews take turns; a huge track number is refused; redo puts an edit back; a renamed folder carries sequences not loaded", [=] {
    seq_clips();
    Editor ed;
    ed.init_headless(1400, 900);
    GameObject *box = ed.scene().create("R86Turns");
    GameObject *lid = ed.scene().create("Lid", box);
    ed.command("select R86Turns");
    ed.step_frame_headless();
    std::remove(resolve_asset_path("Assets/Animations/R86Turns Sequence.seq").c_str());
    ed.command("seq new");
    ed.command("seq add Assets/Animations/R86Up.anim 0 0");
    ed.step_frame_headless();
    /* Sequencer Preview, then the Animation window's: the first is put away (the scene back) before the second. */
    ed.command("seq preview on");
    ed.command("seq frame 30");
    CHECK(std::fabs(lid->local().position.y - 3.0f) < 1e-3f);
    ed.command("anim preview on");
    CHECK(!ed.seq_for_test().preview && lid->local().position.y == 0.0f);
    ed.command("anim preview off");
    ed.step_frame_headless();
    CHECK(ed.scene().find_by_name("Lid")->local().position.y == 0.0f);
    /* Track numbers past the next one up, or not numbers, are refused. */
    const size_t tracks = ed.seq_for_test_sequence()->tracks.size();
    ed.command("seq add Assets/Animations/R86Up.anim 100000000 0");
    ed.command("seq add Assets/Animations/R86Up.anim 1e20 0");
    ed.command("seq add Assets/Animations/R86Up.anim nan 0");
    CHECK(ed.seq_for_test_sequence()->tracks.size() == tracks);
    /* Undo and redo of a sequence edit. */
    ed.command("seq speed R86Up 2");
    ed.step_frame_headless();
    ed.command("undo");
    CHECK(ed.seq_for_test_sequence()->find("R86Up")->speed == 1.0f);
    ed.command("redo");
    CHECK(ed.seq_for_test_sequence()->find("R86Up")->speed == 2.0f);
    /* A folder holding a clip and a sequence that isn't loaded, renamed: the sequence on disk follows the clip. */
    const std::string dir = "Assets/R86Dir", dir2 = "Assets/R86Dir2";
    std::error_code ec;  // (earlier runs' folders: renaming onto one that exists is refused, as Unity does)
    std::filesystem::remove_all(resolve_asset_path(dir), ec);
    std::filesystem::remove_all(resolve_asset_path(dir2), ec);
    fs::make_dirs(resolve_asset_path(dir));
    fs::write_file(resolve_asset_path(dir + "/C.anim"), "blendity_anim 1\ntrack\t1\tPosition\tLid\nkey 0 0 0\n");
    fs::write_file(resolve_asset_path(dir + "/S.seq"), "blendity_seq 1\ntrack T\nclip 0 0 -1 1\tC\t" + dir + "/C.anim\n");
    ed.command("renameasset " + dir + " R86Dir2");
    ed.step_frame_headless();
    std::string text;
    CHECK(fs::read_file(resolve_asset_path(dir2 + "/S.seq"), text));
    const size_t at = text.find("clip");
    std::printf("    the moved sequence names: %s", at == std::string::npos ? "-\n" : text.substr(at).c_str());
    CHECK(text.find(dir2 + "/C.anim") != std::string::npos);
  });
  /* ---------------------------------------------------------------- round 95: the scene's sequence (task 0070) */
  test("global sequencer: one scene sequence plays clips on two objects at once, each bar naming its object; it survives a save and plays in Play", [=] {
    seq_clips();
    Editor ed;
    ed.init_headless(1400, 900);
    GameObject *a = ed.scene().create("R95A");
    GameObject *la = ed.scene().create("Lid", a);
    GameObject *b = ed.scene().create("R95B");
    GameObject *lb = ed.scene().create("Lid", b);
    ed.step_frame_headless();
    std::remove(resolve_asset_path("Assets/Animations/R95 Sequence.seq").c_str());
    ed.command("seq new R95 Sequence");
    CHECK(!ed.scene().sequence.empty() && ed.seq_for_test_sequence());
    /* Up on A and Hold on B, at the same time on two tracks: each moves its own lid. */
    ed.command("seq add Assets/Animations/R86Up.anim 0 0 R95A");
    ed.command("seq add Assets/Animations/R86Hold.anim 1 0 R95B");
    ed.step_frame_headless();
    SequencePtr q = ed.seq_for_test_sequence();
    CHECK(q && q->tracks[0].items.size() == 1 && q->tracks[0].items[0].target == "R95A" && q->tracks[1].items[0].target == "R95B");
    ed.command("seq preview on");
    ed.command("seq frame 30");
    ed.step_frame_headless();
    std::printf("    at 30: A's lid y %.3f (3), B's lid y %.3f (-1)\n", la->local().position.y, lb->local().position.y);
    CHECK(std::fabs(la->local().position.y - 3.0f) < 1e-3f && std::fabs(lb->local().position.y + 1.0f) < 1e-3f);
    ed.command("seq preview off");
    ed.step_frame_headless();
    CHECK(la->local().position.y == 0.0f && lb->local().position.y == 0.0f);
    /* Two overlapping bars on one track for different objects don't blend: each plays fully. */
    ed.command("seq add Assets/Animations/R86Hold.anim 0 20 R95B");
    ed.step_frame_headless();
    ed.command("seq preview on");
    ed.command("seq frame 30");
    ed.step_frame_headless();
    CHECK(std::fabs(la->local().position.y - 3.0f) < 1e-3f && std::fabs(lb->local().position.y + 1.0f) < 1e-3f);
    ed.command("seq preview off");
    ed.step_frame_headless();
    /* The file keeps the objects; the scene keeps its sequence. */
    std::string text;
    CHECK(fs::read_file(resolve_asset_path(q->asset_path), text) && text.find("\tR95A") != std::string::npos);
    const std::string scene_file = fs::join(test_dir(), "r95.scene");
    CHECK(save_scene(ed.scene(), scene_file));
    Scene back;
    std::string err;
    CHECK(load_scene(scene_file, back, err) && back.sequence == ed.scene().sequence);
    std::remove(scene_file.c_str());
    /* Play: the scene's director moves both lids. */
    ed.command("play");
    for (int i = 0; i < 31; i++) ed.step_frame_headless();
    GameObject *pa = ed.scene().find_by_name("R95A");
    const float ya = pa && !pa->children.empty() ? pa->children[0]->local().position.y : -99.0f;
    std::printf("    in Play after half a second: A's lid y %.3f\n", ya);
    CHECK(ya > 0.5f);
    ed.command("stop");
  });
  test("global sequencer: three objects' bars overlapping on one track all play; an object given a bar during Preview is put back; renaming the .seq keeps the scene on it", [=] {
    seq_clips();
    Editor ed;
    ed.init_headless(1400, 900);
    GameObject *objs[3];
    GameObject *lids[3];
    for (int k = 0; k < 3; k++) {
      objs[k] = ed.scene().create(strprintf("R96O%d", k));
      lids[k] = ed.scene().create("Lid", objs[k]);
    }
    ed.step_frame_headless();
    std::remove(resolve_asset_path("Assets/Animations/R96Seq.seq").c_str());
    std::remove(resolve_asset_path("Assets/Animations/R96Renamed.seq").c_str());
    ed.command("seq new R96Seq");
    ed.command("seq add Assets/Animations/R86Up.anim 0 0 R96O0");
    ed.command("seq add Assets/Animations/R86Hold.anim 0 10 R96O1");
    ed.step_frame_headless();
    ed.command("seq preview on");
    /* An object given a bar while previewing: its lid moves, and is put back with the others. */
    ed.command("seq add Assets/Animations/R86Hold.anim 0 20 R96O2");
    ed.command("seq frame 30");
    ed.step_frame_headless();
    std::printf("    at 30: lids %.2f %.2f %.2f (3, -1, -1)\n", lids[0]->local().position.y, lids[1]->local().position.y, lids[2]->local().position.y);
    CHECK(std::fabs(lids[0]->local().position.y - 3.0f) < 1e-3f && lids[1]->local().position.y == -1.0f && lids[2]->local().position.y == -1.0f);
    ed.command("seq preview off");
    ed.step_frame_headless();
    CHECK(lids[0]->local().position.y == 0.0f && lids[1]->local().position.y == 0.0f && lids[2]->local().position.y == 0.0f);
    /* The .seq renamed in the Project window: the scene follows it. */
    ed.command("renameasset Assets/Animations/R96Seq.seq R96Renamed");
    ed.step_frame_headless();
    std::printf("    the scene's sequence after the rename: '%s'\n", ed.scene().sequence.c_str());
    CHECK(ed.scene().sequence == "Assets/Animations/R96Renamed.seq" && ed.seq_for_test_sequence());
  });
  /* ---------------------------------------------------------------- round 97: dragging stacks into order (task 0071) */
  test("stacks: a modifier's or a filter's header dragged below another moves it there in one undo step; a click still folds it", [] {
    Editor ed;
    ed.init_headless(1400, 1000);
    ed.command("layout Default");
    ed.record_buttons_for_test(true);
    using ET = platform::EventType;
    auto at = [](ET t, int x, int y) {
      platform::Event e;
      e.type = t, e.x = x, e.y = y;
      return e;
    };
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    /* Drag the header `from` to just below the header `below`'s middle. */
    auto drag = [&](const std::string &from, const std::string &below) {
      Recti a, b;
      if (!ed.button_rect_for_test(from, a) || !ed.button_rect_for_test(below, b)) {
        std::printf("    no header '%s' or '%s'\n", from.c_str(), below.c_str());
        return false;
      }
      const int x = a.x + a.w / 2, y0 = a.y + a.h / 2, y1 = b.y + b.h - 2;
      ed.step_frame_headless({at(ET::MouseMove, x, y0)});
      ed.step_frame_headless({at(ET::MouseDown, x, y0)});
      for (int k = 1; k <= 4; k++) ed.step_frame_headless({at(ET::MouseMove, x, y0 + (y1 - y0) * k / 4)});
      ed.step_frame_headless({at(ET::MouseUp, x, y1)});
      ed.step_frame_headless();
      return true;
    };
    ed.command("select Cube");
    ed.command("component BevelModifier");
    ed.command("component ArrayModifier");
    ed.command("component SmoothModifier");
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    auto order = [&] {
      std::string s;
      GameObject *g = ed.scene().find_by_name("Cube");
      for (const auto &c : g->components)
        if (c->is_modifier()) s += std::string(c->type_name()).substr(0, 3);
      return s;
    };
    std::printf("    modifiers: %s\n", order().c_str());
    CHECK(order() == "BevArrSmo");
    GameObject *cube = ed.scene().find_by_name("Cube");
    const bool folded0 = cube->get<BevelModifier>()->ui_expanded;
    CHECK(drag("mod_head0", "mod_head2"));  // Bevel to the bottom
    std::printf("    after the drag: %s\n", order().c_str());
    CHECK(order() == "ArrSmoBev");
    CHECK(cube->get<BevelModifier>()->ui_expanded == folded0);  // (the press on its name didn't fold it)
    ed.command("undo");
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    CHECK(order() == "BevArrSmo");  // one undo step
    /* A click on a name still folds it. */
    Recti h;
    CHECK(ed.button_rect_for_test("mod_head0", h));
    cube = ed.scene().find_by_name("Cube");
    const bool was = cube->get<BevelModifier>()->ui_expanded;
    ed.step_frame_headless({at(ET::MouseMove, h.x + h.w / 2, h.y + h.h / 2)});
    ed.step_frame_headless({at(ET::MouseDown, h.x + h.w / 2, h.y + h.h / 2)});
    ed.step_frame_headless({at(ET::MouseUp, h.x + h.w / 2, h.y + h.h / 2)});
    std::printf("    click at %d,%d (%dx%d): folded %d -> %d, %s\n", h.x, h.y, h.w, h.h, (int)was, (int)cube->get<BevelModifier>()->ui_expanded, order().c_str());
    CHECK(cube->get<BevelModifier>()->ui_expanded != was && order() == "BevArrSmo");
    /* A camera's filters. */
    ed.command("select Main Camera");
    ed.command("filter add Film Grain");
    ed.command("filter add Retro Console");
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    auto filters = [&] {
      std::string s;
      GameObject *cam = ed.scene().find_by_name("Main Camera");
      const auto *cf = cam ? cam->get<CameraFilters>() : nullptr;
      if (!cf) return std::string("none");
      for (const auto &e : cf->effects) s += std::string(e->type_name()).substr(0, 4) + " ";
      return s;
    };
    std::printf("    filters: %s\n", filters().c_str());
    const std::string before = filters();
    CHECK(drag("filter_head0", "filter_head1"));
    std::printf("    after the drag: %s\n", filters().c_str());
    CHECK(filters() != before && filters().substr(5) == before.substr(0, 5));  // the first is last
    /* Up: the second dragged above the first's middle puts them back. */
    Recti f0, f1;
    CHECK(ed.button_rect_for_test("filter_head0", f0) && ed.button_rect_for_test("filter_head1", f1));
    const int fx = f1.x + f1.w / 2, fy0 = f1.y + f1.h / 2, fy1 = f0.y + 2;
    ed.step_frame_headless({at(ET::MouseMove, fx, fy0)});
    ed.step_frame_headless({at(ET::MouseDown, fx, fy0)});
    for (int k = 1; k <= 4; k++) ed.step_frame_headless({at(ET::MouseMove, fx, fy0 + (fy1 - fy0) * k / 4)});
    ed.step_frame_headless({at(ET::MouseUp, fx, fy1)});
    ed.step_frame_headless();
    std::printf("    dragged up: %s\n", filters().c_str());
    CHECK(filters() == before);
    /* A drag whose list goes away (another object selected) is dropped: showing the list again doesn't move anything. */
    ed.step_frame_headless({at(ET::MouseMove, fx, fy0)});
    ed.step_frame_headless({at(ET::MouseDown, fx, fy0)});
    for (int k = 1; k <= 4; k++) ed.step_frame_headless({at(ET::MouseMove, fx, fy0 + (fy1 - fy0) * k / 4)});
    ed.command("select Cube");
    ed.step_frame_headless();
    ed.step_frame_headless({at(ET::MouseUp, fx, fy1)});
    ed.command("select Main Camera");
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    CHECK(filters() == before);
    /* Esc drops a drag. */
    ed.step_frame_headless({at(ET::MouseMove, fx, fy0)});
    ed.step_frame_headless({at(ET::MouseDown, fx, fy0)});
    for (int k = 1; k <= 4; k++) ed.step_frame_headless({at(ET::MouseMove, fx, fy0 + (fy1 - fy0) * k / 4)});
    platform::Event esc;
    esc.type = ET::KeyDown, esc.key = platform::KEY_ESCAPE;
    ed.step_frame_headless({esc});
    ed.step_frame_headless({at(ET::MouseUp, fx, fy1)});
    ed.step_frame_headless();
    std::printf("    after Esc: %s\n", filters().c_str());
    CHECK(filters() == before);
  });

  test("stacks: a Sequencer track's name dragged below another moves the track with its clips; seq trackmove does too", [=] {
    seq_clips();
    Editor ed;
    ed.init_headless(1400, 900);
    ed.command("window Sequencer");
    std::remove(resolve_asset_path("Assets/Animations/R97 Sequence.seq").c_str());
    ed.record_buttons_for_test(true);
    ed.scene().create("R97O");
    ed.command("seq new R97 Sequence");
    ed.command("seq add Assets/Animations/R86Up.anim 0 0 R97O");
    ed.command("seq track");
    ed.command("seq add Assets/Animations/R86Hold.anim 1 0 R97O");
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    auto first_clip = [&](int t) {
      SequencePtr s = ed.seq_for_test_sequence();
      return s && (int)s->tracks.size() > t && !s->tracks[(size_t)t].items.empty() ? s->tracks[(size_t)t].items[0].clip : std::string("-");
    };
    CHECK(first_clip(0) == "Assets/Animations/R86Up.anim");
    using ET = platform::EventType;
    auto at = [](ET t, int x, int y) {
      platform::Event e;
      e.type = t, e.x = x, e.y = y;
      return e;
    };
    Recti a, b;
    CHECK(ed.button_rect_for_test("seq_track0", a) && ed.button_rect_for_test("seq_track1", b));
    const int x = a.x + a.w / 2, y0 = a.y + a.h / 2, y1 = b.y + b.h - 2;
    ed.step_frame_headless({at(ET::MouseMove, x, y0)});
    ed.step_frame_headless({at(ET::MouseDown, x, y0)});
    for (int k = 1; k <= 4; k++) ed.step_frame_headless({at(ET::MouseMove, x, y0 + (y1 - y0) * k / 4)});
    ed.step_frame_headless({at(ET::MouseUp, x, y1)});
    ed.step_frame_headless();
    std::printf("    track 0 after the drag: %s\n", first_clip(0).c_str());
    CHECK(first_clip(0) == "Assets/Animations/R86Hold.anim" && first_clip(1) == "Assets/Animations/R86Up.anim");
    ed.command("seq trackmove 1 0");
    CHECK(first_clip(0) == "Assets/Animations/R86Up.anim");
    std::remove(resolve_asset_path("Assets/Animations/R97 Sequence.seq").c_str());
  });
  /* ---------------------------------------------------------------- round 100: render animation (task 0074) */
  test("render animation: the scene's sequence rendered frame by frame into numbered PNGs that differ, the pose put back; an MP4 when FFmpeg is there", [=] {
    seq_clips();
    Editor ed;
    ed.init_headless(1200, 800);
    ed.command("engine raster");
    ed.scene().render.width = 320, ed.scene().render.height = 180;
    GameObject *root = ed.scene().create("R100Root");
    ed.command("create Cube");
    GameObject *lid = ed.active_object_for_test();
    CHECK(lid != nullptr);
    if (!lid) return;
    lid->name = "Lid";
    ed.scene().set_parent(lid, root);
    const float y0 = lid->local().position.y;
    ed.step_frame_headless();
    std::remove(resolve_asset_path("Assets/Animations/R100 Sequence.seq").c_str());
    ed.command("seq new R100 Sequence");
    ed.command("seq add Assets/Animations/R86Up.anim 0 0 R100Root");
    ed.step_frame_headless();
    ed.command("renderanim 0 20 24");
    for (int i = 0; i < 200 && ed.render_animation_for_test().active; i++) ed.step_frame_headless();
    const auto &ra = ed.render_animation_for_test();
    std::printf("    %zu frame(s) in %s at %g fps\n", ra.written.size(), ra.dir.c_str(), ra.fps);
    CHECK(!ra.active && ra.written.size() == 21 && ra.fps == 24.0f);
    CHECK(ra.written.size() == 21 && fs::filename(ra.written[0]) == "0001.png" && fs::filename(ra.written[20]) == "0021.png");
    if (ra.written.size() != 21) return;
    Bitmap a, b;
    std::string err;
    CHECK(load_image(ra.written[0], a, err) && load_image(ra.written[20], b, err));
    CHECK(a.width == 320 && a.height == 180 && b.width == 320);
    CHECK(a.rgba8 != b.rgba8);  // (the lid rose 2 m between them)
    CHECK(std::fabs(lid->local().position.y - y0) < 1e-4f);  // (the pose went back)
    const bool video = video::available() || !ed.ffmpeg_program().empty();  // (built with FFmpeg, or one on the PATH)
    if (!video)
      for (const std::string &f : ra.written) std::remove(f.c_str());  // (the test project's Renders/ stays small)
    /* FFmpeg: its arguments; with one on the PATH (CI's Linux has it), the MP4 is made in the background. */
    const std::vector<std::string> args = ffmpeg_png_to_mp4_args("ffmpeg", "R/a b", 24.0f, "R/a b.mp4");
    CHECK(args.size() == 18 && args[2] == "-nostdin" && args[6] == "24" && args[10] == fs::join("R/a b", "%04d.png") && args.back() == "R/a b.mp4");
    CHECK(sys::windows_command_line({"C:\\Program Files\\ffmpeg.exe", "a b", "x\"y", "e\\", "p\\ q\\"}) ==
          "\"C:\\Program Files\\ffmpeg.exe\" \"a b\" \"x\\\"y\" e\\ \"p\\ q\\\\\"");
    if (!video) {
      std::printf("    no FFmpeg here: frames only\n");
      return;
    }
    for (int i = 0; i < 400 && ed.render_animation_for_test().encode; i++) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      ed.step_frame_headless();
    }
    bool made = false;
    for (const DirEntry &de : fs::list(fs::parent(ra.video))) made = made || (de.name == fs::filename(ra.video) && de.size > 0);
    std::printf("    MP4 %s: %d\n", ra.video.c_str(), (int)made);
    CHECK(made);
  });

  test("render animation: without a sequence or a clip it refuses; Stop puts the pose back", [=] {
    seq_clips();
    Editor ed;
    ed.init_headless(1000, 700);
    ed.command("engine raster");
    ed.scene().render.width = 160, ed.scene().render.height = 90;
    ed.command("select Cube");
    ed.step_frame_headless();
    ed.command("renderanim");
    CHECK(!ed.render_animation_for_test().active);  // (nothing to render)
    GameObject *root = ed.scene().create("R100B");
    GameObject *lid = ed.scene().create("Lid", root);
    ed.command("select R100B");
    ed.command("component Animation");
    CHECK(root->get<Animation>() != nullptr);
    if (!root->get<Animation>()) return;
    root->get<Animation>()->clip = "Assets/Animations/R86Up.anim";
    ed.step_frame_headless();
    ed.command("renderanim 30 59");
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    std::printf("    rendering the clip: active %d, lid at %.2f\n", (int)ed.render_animation_for_test().active, lid->local().position.y);
    CHECK(ed.render_animation_for_test().active && ed.render_animation_for_test().source == 2 && lid->local().position.y > 2.9f);
    ed.command("renderanim stop");
    CHECK(!ed.render_animation_for_test().active && lid->local().position.y == 0.0f);
    /* Another object selected mid-render, or a save (which ends the preview): it stops, the pose goes back. */
    ed.command("renderanim 30 59");
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    ed.command("select Cube");
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    std::printf("    the Cube selected mid-render: active %d, lid at %.2f\n", (int)ed.render_animation_for_test().active, lid->local().position.y);
    CHECK(!ed.render_animation_for_test().active && lid->local().position.y == 0.0f);
    ed.command("select R100B");
    ed.command("renderanim 30 59");
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    ed.command("anim preview off");  // (what a save, an undo or another scene does)
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    CHECK(!ed.render_animation_for_test().active);
    ed.command("renderanim 24");  // (one number: refused, not a render)
    CHECK(!ed.render_animation_for_test().active);
  });
}

static void tests_round89() {
  /* ---------------------------------------------------------------- round 89: armatures nested as Unity's humanoids (task 0065) */
  test("rig nesting: GameObject > Armature on a selected mesh puts mesh and Armature in '<mesh> Rig'; moving the root moves both; undo puts the mesh back", [] {
    Editor ed;
    ed.init_headless(1400, 900);
    GameObject *group = ed.scene().create("R89Group");
    ed.scene().create("R89First", group);
    GameObject *body = ed.scene().create("R89Body", group);
    MeshFilter *mf = body->add<MeshFilter>();
    mf->mesh = primitives::cylinder(0.3f, 2.0f, 12);
    body->add<MeshRenderer>();
    body->set_world_position({2, 0, 1});
    ed.select_for_test(body->id, false);
    ed.mark_changed_for_test("body");
    ed.step_frame_headless();
    const size_t index_before = 1;
    ed.command("create Armature");
    ed.step_frame_headless();
    GameObject *rig = ed.scene().find_by_name("R89Body Rig");
    GameObject *arm = ed.scene().find_by_name("Armature");
    std::printf("    rig %d, its parent %s, children: %zu\n", (int)(rig != nullptr), rig && rig->parent ? rig->parent->name.c_str() : "-", rig ? rig->children.size() : 0);
    CHECK(rig && rig->parent == group && group->children.size() == 2 && group->children[index_before] == rig);
    CHECK(rig && body->parent == rig && arm && arm->parent == rig);
    CHECK(length(body->world_position() - Vec3(2, 0, 1)) < 1e-4f && length(rig->world_position() - Vec3(2, 0, 1)) < 1e-4f);
    CHECK(body->get<SkinModifier>() && body->get<SkinModifier>()->object == arm->name);
    /* Moving the root moves mesh and skeleton together: the skinned mesh is the rest shape, moved. */
    const Vec3 v0 = body->world_matrix().point(body->evaluated_mesh()->positions[0]);
    rig->set_world_position({5, 0, 1});
    ed.mark_changed_for_test("move rig");
    ed.step_frame_headless();
    const Vec3 v1 = body->world_matrix().point(body->evaluated_mesh()->positions[0]);
    std::printf("    a vertex moved by (%.4f, %.4f, %.4f) for the root's (3, 0, 0)\n", v1.x - v0.x, v1.y - v0.y, v1.z - v0.z);
    CHECK(length((v1 - v0) - Vec3(3, 0, 0)) < 1e-3f);
    /* A second armature on the same mesh goes into the same root. */
    ed.select_for_test(body->id, false);
    ed.step_frame_headless();
    ed.command("create Armature");
    ed.step_frame_headless();
    GameObject *arm2 = ed.scene().find_by_name("Armature.001");
    CHECK(arm2 && arm2->parent == rig && !ed.scene().find_by_name("R89Body Rig.001"));
    /* Undo both armatures: the mesh is back in the group at its place, no root. */
    ed.command("undo");
    ed.command("undo");
    ed.command("undo");
    ed.step_frame_headless();
    GameObject *b = ed.scene().find_by_name("R89Body");
    GameObject *g = ed.scene().find_by_name("R89Group");
    CHECK(b && g && b->parent == g && g->children.size() == 2 && g->children[index_before] == b && !ed.scene().find_by_name("R89Body Rig"));
  });

  test("rig nesting: Add Armature (Skin) moves an unused armature into the mesh's rig root, but leaves one another mesh uses", [] {
    Editor ed;
    ed.init_headless(1400, 900);
    auto mesh = [&](const char *name) {
      GameObject *g = ed.scene().create(name);
      g->add<MeshFilter>()->mesh = primitives::cylinder(0.3f, 2.0f, 12);
      g->add<MeshRenderer>();
      return g;
    };
    GameObject *a = mesh("R89A"), *b = mesh("R89B");
    ed.command("create Armature");  // (nothing selected: at the scene root)
    GameObject *arm = ed.scene().find_by_name("Armature");
    CHECK(arm && !arm->parent);
    ed.step_frame_headless();
    /* The rigging window's button: through nest_armature, as the button does. */
    a->add<SkinModifier>()->object = arm->name;
    ed.nest_armature_for_test(arm, a);
    CHECK(arm->parent && arm->parent->name == "R89A Rig" && a->parent == arm->parent);
    /* B uses it too: a fresh unparented skeleton used by A stays put. */
    GameObject *arm2 = ed.scene().create("Skeleton2");
    arm2->add<Armature>();
    a->get<SkinModifier>()->object = "Skeleton2";
    b->add<SkinModifier>()->object = "Skeleton2";
    ed.nest_armature_for_test(arm2, b);
    CHECK(!arm2->parent && !b->parent);
  });
}

static void tests_round90() {
  /* ---------------------------------------------------------------- round 90: bone head/tail handles and extrude (task 0066) */
  test("bones: dragging a bone's tail handle re-aims and lengthens it, its child stays put and the result is the rest pose; the head handle keeps the tail", [] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.command("create Armature");
    GameObject *bone = ed.scene().find_by_name("Bone");
    CHECK(bone != nullptr);
    if (!bone) return;
    ed.command("bone add Bone 0.5");
    GameObject *child = ed.scene().find_by_name("Bone.001");
    CHECK(child != nullptr);
    if (!child) return;
    const Vec3 child_at = child->world_position();
    ed.select_for_test(bone->id, false);
    ed.record_buttons_for_test(true);
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    Recti tail;
    CHECK(ed.button_rect_for_test("bone_tail", tail));
    const Vec3 head = bone->world_position(), tail0 = ed.bone_tail_for_test(*bone);
    const size_t steps = ed.undo_steps_for_test();
    platform::Event m, d, mv, u;
    m.type = mv.type = platform::EventType::MouseMove, d.type = platform::EventType::MouseDown, u.type = platform::EventType::MouseUp;
    const int tx = tail.x + 4, ty = tail.y + 4;
    m.x = d.x = tx, m.y = d.y = ty, mv.x = u.x = tx + 80, mv.y = u.y = ty + 10;
    ed.step_frame_headless({m});
    ed.step_frame_headless({d});
    ed.step_frame_headless({mv});
    ed.step_frame_headless({u});
    ed.step_frame_headless();
    const Vec3 tail1 = ed.bone_tail_for_test(*bone);
    std::printf("    tail (%.2f %.2f %.2f) -> (%.2f %.2f %.2f); head moved %.5f; child moved %.5f; length %.3f\n", tail0.x, tail0.y, tail0.z, tail1.x,
                tail1.y, tail1.z, length(bone->world_position() - head), length(child->world_position() - child_at), bone->get<Bone>()->length);
    CHECK(length(tail1 - tail0) > 0.1f && length(bone->world_position() - head) < 1e-4f);
    CHECK(length(child->world_position() - child_at) < 1e-4f);
    CHECK(std::fabs(bone->get<Bone>()->length - length(tail1 - head)) < 1e-4f);
    CHECK(length(bone->get<Bone>()->rest_position - bone->local().position) < 1e-5f);  // the edit is the rest
    CHECK(ed.undo_steps_for_test() == steps + 1);
    /* The head, by the console: the tail stays. */
    ed.command(strprintf("bone head Bone %g %g %g", head.x, head.y + 0.25f, head.z));
    CHECK(length(ed.bone_tail_for_test(*bone) - tail1) < 1e-4f && length(bone->world_position() - (head + Vec3(0, 0.25f, 0))) < 1e-4f);
    ed.command("undo");
    ed.command("undo");
    ed.step_frame_headless();
    bone = ed.scene().find_by_name("Bone");
    CHECK(bone && length(ed.bone_tail_for_test(*bone) - tail0) < 1e-4f);
  });

  test("bones: Extrude (Ctrl+E in Unity's keys, E in Blender's) grows a bone from the tip whose tail follows the mouse; a click keeps it, Esc takes it away", [] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.command("create Armature");
    GameObject *bone = ed.scene().find_by_name("Bone");
    CHECK(bone != nullptr);
    if (!bone) return;
    ed.select_for_test(bone->id, false);
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    const Recti r = ed.scene_rect_for_test();
    const int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
    auto ev = [](platform::EventType t, int x, int y, int key = 0, int mods = 0) {
      platform::Event e;
      e.type = t, e.x = x, e.y = y, e.key = key, e.mods = mods;
      return e;
    };
    using ET = platform::EventType;
    const size_t before = ed.scene().object_count();
    ed.step_frame_headless({ev(ET::MouseMove, cx, cy)});
    ed.step_frame_headless({ev(ET::KeyDown, cx, cy, platform::KEY_E, platform::MOD_CTRL)});
    CHECK(ed.scene().object_count() == before + 1 && ed.bone_drag_for_test().modal);
    GameObject *grown = ed.active_object_for_test();
    CHECK(grown && grown->parent == bone);
    if (!grown) return;
    const Vec3 tip = ed.bone_tail_for_test(*bone);
    CHECK(length(grown->world_position() - tip) < 1e-4f);
    ed.step_frame_headless({ev(ET::MouseMove, cx + 120, cy - 60)});
    const Vec3 t1 = ed.bone_tail_for_test(*grown);
    ed.step_frame_headless({ev(ET::MouseDown, cx + 120, cy - 60)});
    ed.step_frame_headless({ev(ET::MouseUp, cx + 120, cy - 60)});
    ed.step_frame_headless();
    std::printf("    the new bone's tail followed the mouse to (%.2f %.2f %.2f); kept: %d\n", t1.x, t1.y, t1.z, (int)!ed.bone_drag_for_test().modal);
    CHECK(!ed.bone_drag_for_test().modal && ed.scene().object_count() == before + 1 && length(ed.bone_tail_for_test(*grown) - t1) < 1e-4f);
    /* Again, then Esc: no new bone. */
    ed.select_for_test(grown->id, false);
    ed.step_frame_headless();
    ed.step_frame_headless({ev(ET::KeyDown, cx + 120, cy - 60, platform::KEY_E, platform::MOD_CTRL)});
    CHECK(ed.scene().object_count() == before + 2);
    ed.step_frame_headless({ev(ET::KeyDown, cx + 120, cy - 60, platform::KEY_ESCAPE)});
    ed.step_frame_headless();
    CHECK(ed.scene().object_count() == before + 1 && !ed.bone_drag_for_test().bone);
  });
}

static void tests_round91() {
  /* ---------------------------------------------------------------- round 91: rig controls, IK and FK (task 0067) */
  auto quat_dot = [](Quat a, Quat b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; };
  test("rig controls: Add IK gives a box target and a diamond pole coloured by side; Remove IK takes the constraint, target and pole in one step", [] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.command("create Armature");
    ed.command("bone add Bone 1");
    GameObject *shin = ed.scene().find_by_name("Bone.001");
    CHECK(shin != nullptr);
    if (!shin) return;
    ed.scene().find_by_name("Bone")->name = "LeftThigh";
    shin->name = "LeftShin";
    ed.command("ik add LeftShin");
    ed.step_frame_headless();
    const IKConstraint *ik = shin->get<IKConstraint>();
    CHECK(ik != nullptr);
    if (!ik) return;
    GameObject *target = ed.scene().find_by_name(ik->target), *pole = ed.scene().find_by_name(ik->pole);
    CHECK(target && pole && target->get<RigControl>() && pole->get<RigControl>());
    if (!target || !pole) return;
    CHECK(target->get<RigControl>()->shape == RigControl::Box && target->get<RigControl>()->role == RigControl::IKTarget);
    CHECK(pole->get<RigControl>()->shape == RigControl::Diamond && pole->get<RigControl>()->role == RigControl::IKPole);
    std::printf("    target '%s' side %d, pole '%s' side %d (2 = Left)\n", target->name.c_str(), rig_control_side(*target, *target->get<RigControl>()),
                pole->name.c_str(), rig_control_side(*pole, *pole->get<RigControl>()));
    CHECK(rig_control_side(*target, *target->get<RigControl>()) == RigControl::Left);
    const size_t steps = ed.undo_steps_for_test(), objects = ed.scene().object_count();
    ed.command("ik remove LeftShin");
    ed.step_frame_headless();
    CHECK(!shin->get<IKConstraint>() && ed.scene().object_count() == objects - 2 && ed.undo_steps_for_test() == steps + 1);
    ed.command("undo");
    ed.step_frame_headless();
    GameObject *s2 = ed.scene().find_by_name("LeftShin");
    CHECK(s2 && s2->get<IKConstraint>() && ed.scene().object_count() == objects);
  });

  test("rig controls: an FK circle turns its bone as it turns, a click on its wire selects it, and Remove FK Controls leaves the bone as posed", [=] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.command("create Armature");
    GameObject *bone = ed.scene().find_by_name("Bone");
    CHECK(bone != nullptr);
    if (!bone) return;
    ed.command("fk add Bone");
    ed.step_frame_headless();
    GameObject *ctl = ed.scene().find_by_name("BoneFK");
    CHECK(ctl && ctl->get<RigControl>() && ctl->get<RigControl>()->role == RigControl::FK && ctl->get<RigControl>()->drives == "Bone");
    if (!ctl) return;
    CHECK(length(ctl->world_position() - bone->world_position()) < 1e-4f);
    ctl->set_local_rotation(ctl->local().rotation * Quat::axis_angle(Vec3(0, 0, 1), 0.5f));
    ed.mark_changed_for_test("turn FK");
    ed.step_frame_headless();
    std::printf("    the control and the bone after turning the control: dot %.6f\n", std::fabs(quat_dot(ctl->world_rotation(), bone->world_rotation())));
    CHECK(std::fabs(quat_dot(ctl->world_rotation(), bone->world_rotation())) > 0.9999f);
    /* A click on the circle's wire selects the control. */
    std::vector<std::pair<Vec3, Vec3>> segs;
    ed.rig_control_segments_for_test(*ctl, segs);
    int x = 0, y = 0;
    CHECK(!segs.empty() && ed.screen_of_for_test(segs[3].first, x, y));
    ed.command("select Main Camera");  // (not the bone: its gizmo would sit on the circle and take the click)
    ed.step_frame_headless();
    platform::Event m, d, u;
    m.type = platform::EventType::MouseMove, d.type = platform::EventType::MouseDown, u.type = platform::EventType::MouseUp;
    m.x = d.x = u.x = x, m.y = d.y = u.y = y;
    ed.step_frame_headless({m});
    ed.step_frame_headless({d});
    ed.step_frame_headless({u});
    ed.step_frame_headless();
    std::printf("    clicked the circle: active '%s'\n", ed.active_object_for_test() ? ed.active_object_for_test()->name.c_str() : "-");
    CHECK(ed.active_object_for_test() == ctl);
    const Quat posed = bone->world_rotation();
    ed.command("fk remove Bone");
    ed.step_frame_headless();
    CHECK(!ed.scene().find_by_name("BoneFK") && std::fabs(quat_dot(bone->world_rotation(), posed)) > 0.9999f);
  });
}

static void tests_round92() {
  /* ---------------------------------------------------------------- round 92: the auto-rig's colours and views (task 0068) */
  test("auto-rig: markers are coloured by body part (left blue, right red, middle yellow, tail purple) and the placing view switches", [] {
    Editor ed;
    ed.init_headless(1400, 900);
    auto rgb = [](uint32_t c) { return std::array<int, 3>{(int)((c >> 16) & 255), (int)((c >> 8) & 255), (int)(c & 255)}; };
    auto bluest = [&](uint32_t c) { auto v = rgb(c); return v[2] > v[0] && v[2] > v[1]; };
    auto reddest = [&](uint32_t c) { auto v = rgb(c); return v[0] > v[1] + 60 && v[0] > v[2] + 60; };
    auto yellow = [&](uint32_t c) { auto v = rgb(c); return v[0] > 200 && v[1] > 180 && v[2] < 120; };
    CHECK(bluest(ed.autorig_marker_color("LeftWrist", 1)) && bluest(ed.autorig_marker_color("FrontLeftPaw", 1)));
    CHECK(reddest(ed.autorig_marker_color("RightKnee", 1)) && reddest(ed.autorig_marker_color("HindRightHock", 1)));
    CHECK(yellow(ed.autorig_marker_color("Chin", 1)) && yellow(ed.autorig_marker_color("Withers", 1)));
    const auto tail = rgb(ed.autorig_marker_color("TailTip", 1));
    CHECK(tail[0] > tail[1] && tail[2] > tail[1]);  // purple
    /* A twin is lighter than the hand-placed marker, an unplaced one darker. */
    auto sum = [&](uint32_t c) { auto v = rgb(c); return v[0] + v[1] + v[2]; };
    CHECK(sum(ed.autorig_marker_color("LeftWrist", 2)) > sum(ed.autorig_marker_color("LeftWrist", 1)) &&
          sum(ed.autorig_marker_color("LeftWrist", 0)) < sum(ed.autorig_marker_color("LeftWrist", 1)));
    /* Views: start on a mesh, switch to the left side and the top. */
    ed.command("create Cylinder");
    ed.command("autorig humanoid");
    CHECK(ed.autorig_for_test().active && ed.autorig_for_test().view == "Front");
    ed.command("autorig view left");
    const double t0 = now_seconds();
    while ((ed.scene_camera_for_test().animating || now_seconds() - t0 < 0.5) && now_seconds() - t0 < 20.0)
      ed.step_frame_headless();  // (until the camera's turn ends: a sanitizer build's frames are slow)
    std::printf("    after 'autorig view left': view %s, yaw %.1f, ortho %d\n", ed.autorig_for_test().view.c_str(), ed.scene_camera_for_test().yaw,
                (int)ed.scene_camera_for_test().ortho);
    CHECK(ed.autorig_for_test().view == "Left" && std::fabs(std::remainder(ed.scene_camera_for_test().yaw - 90.0f, 360.0f)) < 1.0f && ed.scene_camera_for_test().ortho);
    ed.command("autorig view top");
    const double t1 = now_seconds();
    while ((ed.scene_camera_for_test().animating || now_seconds() - t1 < 0.5) && now_seconds() - t1 < 20.0)
      ed.step_frame_headless();  // (until the camera's turn ends: a sanitizer build's frames are slow)
    CHECK(ed.autorig_for_test().view == "Top" && ed.scene_camera_for_test().pitch > 80.0f);
    ed.command("autorig view sideways");  // (refused)
    CHECK(ed.autorig_for_test().view == "Top");
  });
  test("rigging: an extrude is one undo step on its click and none after Esc; a scaled armature's tail lands where it's put; a rig at the scene root keeps its place", [] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.command("create Armature");
    GameObject *bone = ed.scene().find_by_name("Bone");
    CHECK(bone != nullptr);
    if (!bone) return;
    ed.select_for_test(bone->id, false);
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    const Recti r = ed.scene_rect_for_test();
    const int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
    auto ev = [](platform::EventType t, int x, int y, int key = 0, int mods = 0) {
      platform::Event e;
      e.type = t, e.x = x, e.y = y, e.key = key, e.mods = mods;
      return e;
    };
    using ET = platform::EventType;
    const size_t steps = ed.undo_steps_for_test(), objects = ed.scene().object_count();
    ed.step_frame_headless({ev(ET::MouseMove, cx, cy)});
    ed.step_frame_headless({ev(ET::KeyDown, cx, cy, platform::KEY_E, platform::MOD_CTRL)});
    for (int k = 1; k <= 10; k++) ed.step_frame_headless({ev(ET::MouseMove, cx + 8 * k, cy - 4 * k)});
    ed.step_frame_headless({ev(ET::KeyDown, cx + 80, cy - 40, platform::KEY_ESCAPE)});
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    std::printf("    after Esc: %zu undo steps more, %zu objects more\n", ed.undo_steps_for_test() - steps, ed.scene().object_count() - objects);
    CHECK(ed.undo_steps_for_test() == steps && ed.scene().object_count() == objects);
    ed.select_for_test(bone->id, false);
    ed.step_frame_headless();
    ed.step_frame_headless({ev(ET::KeyDown, cx, cy, platform::KEY_E, platform::MOD_CTRL)});
    for (int k = 1; k <= 10; k++) ed.step_frame_headless({ev(ET::MouseMove, cx + 8 * k, cy - 4 * k)});
    ed.step_frame_headless({ev(ET::MouseDown, cx + 80, cy - 40)});
    ed.step_frame_headless({ev(ET::MouseUp, cx + 80, cy - 40)});
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    std::printf("    after a kept extrude: %zu undo steps more\n", ed.undo_steps_for_test() - steps);
    CHECK(ed.undo_steps_for_test() == steps + 1 && ed.scene().object_count() == objects + 1);
    ed.command("undo");
    ed.step_frame_headless();
    CHECK(ed.scene().object_count() == objects);
    /* A half-scale armature: the tail goes exactly where it's put. */
    GameObject *arm = ed.scene().find_by_name("Armature");
    CHECK(arm != nullptr);
    if (!arm) return;
    Transform t = arm->local();
    t.scale = Vec3(0.5f);
    arm->set_local(t);
    ed.mark_changed_for_test("scale");
    ed.step_frame_headless();
    bone = ed.scene().find_by_name("Bone");
    const Vec3 want = bone->world_position() + Vec3(0.3f, 0.6f, 0.0f);
    ed.command(strprintf("bone tail Bone %g %g %g", want.x, want.y, want.z));
    std::printf("    the tail on a half-scale armature is off by %.6f\n", length(ed.bone_tail_for_test(*bone) - want));
    CHECK(length(ed.bone_tail_for_test(*bone) - want) < 1e-4f);
    /* A mesh at the scene root, between two others: its rig takes its place there. */
    GameObject *a = ed.scene().create("R94A");
    GameObject *m = ed.scene().create("R94Mesh");
    m->add<MeshFilter>()->mesh = primitives::cylinder(0.3f, 2.0f, 12);
    m->add<MeshRenderer>();
    ed.scene().create("R94C");
    size_t at = 0;
    for (size_t i = 0; i < ed.scene().roots.size(); i++)
      if (ed.scene().roots[i] == m) at = i;
    ed.select_for_test(m->id, false);
    ed.step_frame_headless();
    ed.command("create Armature");
    ed.step_frame_headless();
    GameObject *rig = ed.scene().find_by_name("R94Mesh Rig");
    CHECK(rig && at < ed.scene().roots.size() && ed.scene().roots[at] == rig && ed.scene().roots[at - 1] == a);
  });

  test("rigging: Remove IK keeps a target of the user's own; FK isn't added to IK's bones; Reset Pose puts FK controls back; '.L' controls are blue", [] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.command("create Armature");
    ed.command("bone add Bone 1");
    GameObject *thigh = ed.scene().find_by_name("Bone"), *shin = ed.scene().find_by_name("Bone.001");
    CHECK(thigh && shin);
    if (!thigh || !shin) return;
    thigh->name = "Thigh.L", shin->name = "Shin.L";
    ed.command("ik add Shin.L");
    ed.step_frame_headless();
    GameObject *target = ed.scene().find_by_name("Shin.LIK");
    CHECK(target && target->get<RigControl>() && rig_control_side(*target, *target->get<RigControl>()) == RigControl::Left);
    /* A cup of the user's as the target instead: Remove IK leaves it. */
    GameObject *cup = ed.scene().create("Cup");
    shin->get<IKConstraint>()->target = "Cup";
    ed.mark_changed_for_test("aim at the cup");
    ed.step_frame_headless();
    ed.command("ik remove Shin.L");
    ed.step_frame_headless();
    CHECK(ed.scene().find_by_name("Cup") == cup && !shin->get<IKConstraint>() && !ed.scene().find_by_name("Shin.LPole"));
    /* IK again: FK on its bones is refused. */
    ed.command("ik add Shin.L");
    ed.step_frame_headless();
    ed.command("fk add Shin.L Thigh.L");
    CHECK(!ed.scene().find_by_name("Shin.LFK") && !ed.scene().find_by_name("Thigh.LFK"));
    ed.command("ik remove Shin.L");
    ed.step_frame_headless();
    /* FK on both, a turn on the thigh's control, then Reset Pose: the thigh and its control are back at rest and stay. */
    ed.command("armature reset Armature");  // (the IK turned the thigh: back to its rest before the controls go on)
    ed.step_frame_headless();
    ed.command("fk add Shin.L Thigh.L");
    ed.step_frame_headless();
    GameObject *ctl = ed.scene().find_by_name("Thigh.LFK");
    CHECK(ctl != nullptr);
    if (!ctl) return;
    const Quat rest = thigh->world_rotation();
    ctl->set_local_rotation(ctl->local().rotation * Quat::axis_angle(Vec3(1, 0, 0), 0.6f));
    ed.mark_changed_for_test("turn");
    ed.step_frame_headless();
    auto qd = [](Quat a, Quat b) { return std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w); };
    CHECK(qd(thigh->world_rotation(), rest) < 0.999f);
    ed.command("armature reset Armature");
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    std::printf("    after Reset Pose: the thigh is at rest %d, its control with it %d\n", (int)(qd(thigh->world_rotation(), rest) > 0.9999f),
                (int)(qd(ctl->world_rotation(), rest) > 0.9999f));
    CHECK(qd(thigh->world_rotation(), rest) > 0.9999f && qd(ctl->world_rotation(), rest) > 0.9999f);
    CHECK(!solve_constraints(ed.scene()));  // (nothing left to turn: the editor can go idle)
  });
}

static void tests_round93() {
  /* ---------------------------------------------------------------- round 93: the dopesheet's grouped rows and box select (task 0069) */
  test("keyframes: a Position's x/y/z are one dopesheet row that unfolds; its diamond selects and moves all three; a thin box selects every key it touches", [] {
    Editor ed;
    ed.init_headless(1400, 900);
    GameObject *box = ed.scene().create("R93Box");
    ed.scene().create("Lid", box);
    ed.command("select R93Box");
    ed.command("layout Animation");  // (the Animation window along the bottom: wide enough to tell frames apart)
    ed.command("anim new");
    ed.command("anim key Lid Position 0 10 0");
    ed.command("anim key Lid Position 1 10 1");
    ed.command("anim key Lid Position 2 10 2");
    ed.command("anim key Lid Position 1 30 3");
    ed.command("anim key Lid Scale 0 20 1");
    ed.record_buttons_for_test(true);
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    Recti grp;
    CHECK(ed.button_rect_for_test("anim_track:Lid : Position", grp));
    Recti x_row;
    CHECK(!ed.button_rect_for_test("anim_track:    .x", x_row));  // folded
    const AnimEditorState &st = ed.anim_for_test();
    auto key_x = [&](float f) { return st.sheet.x + (int)std::lround(f / st.sheet_end * (float)st.sheet.w); };
    auto ev = [](platform::EventType t, int x, int y, int mods = 0) {
      platform::Event e;
      e.type = t, e.x = x, e.y = y, e.mods = mods;
      return e;
    };
    using ET = platform::EventType;
    auto click = [&](int x, int y, int mods = 0) {
      ed.step_frame_headless({ev(ET::MouseMove, x, y, mods)});
      ed.step_frame_headless({ev(ET::MouseDown, x, y, mods)});
      ed.step_frame_headless({ev(ET::MouseUp, x, y, mods)});
      ed.step_frame_headless();
    };
    /* The group's diamond at frame 10: all three axes' keys. */
    const int gy = grp.y + grp.h / 2;
    click(key_x(10), gy);
    std::printf("    clicking the group's diamond at 10: %zu keys selected\n", st.selected.size());
    CHECK(st.selected.size() == 3);
    /* Dragged 10 frames right: all three move to 20, the y key at 30 stays. */
    ed.step_frame_headless({ev(ET::MouseMove, key_x(10), gy)});
    ed.step_frame_headless({ev(ET::MouseDown, key_x(10), gy)});
    ed.step_frame_headless({ev(ET::MouseMove, key_x(20), gy)});
    ed.step_frame_headless({ev(ET::MouseUp, key_x(20), gy)});
    ed.step_frame_headless();
    AnimationClipPtr clip = ed.anim_clip_for_test();
    if (clip)
      for (const AnimTrack &tr : clip->tracks) {
        std::printf("    %s:", tr.label().c_str());
        for (const AnimKey &k : tr.keys) std::printf(" %g", k.frame);
        std::printf("\n");
      }
    /* Where they landed (10 frames on, give or take the pixel rounding of a frame): all three together. */
    const float moved = clip && !clip->find("Lid", "Position", 0)->keys.empty() ? clip->find("Lid", "Position", 0)->keys[0].frame : -1.0f;
    CHECK(clip && std::fabs(moved - 20.0f) <= 1.0f && clip->find("Lid", "Position", 2)->key_at(moved) >= 0 &&
          clip->find("Lid", "Position", 1)->key_at(moved) >= 0 && clip->find("Lid", "Position", 1)->key_at(30) >= 0 &&
          clip->find("Lid", "Position", 0)->key_at(10) < 0);
    /* Unfold: the axes get rows of their own. */
    click(grp.x + 8, gy);
    for (int i = 0; i < 2; i++) ed.step_frame_headless();
    CHECK(ed.button_rect_for_test("anim_track:    .x", x_row) && ed.button_rect_for_test("anim_track:    .z", x_row));
    /* A thin box over the diamonds at 20 and 30 (not crossing any row's middle line), from empty space: it takes the
     * keys it touches - the group's and the axes' rows below. */
    Recti sheet;
    CHECK(ed.button_rect_for_test("anim_sheet", sheet));
    Recti zrow;
    CHECK(ed.button_rect_for_test("anim_track:    .z", zrow));
    click(sheet.right() - 5, sheet.bottom() - 5);  // (empty: clears the selection)
    CHECK(st.selected.empty());
    const int y0 = grp.y + 2, y1 = zrow.y + 3;  // (within the first row's top and the z row's top: no middle line of z)
    ed.step_frame_headless({ev(ET::MouseMove, key_x(17), y0)});
    ed.step_frame_headless({ev(ET::MouseDown, key_x(17), y0)});
    ed.step_frame_headless({ev(ET::MouseMove, key_x(33), y1)});
    ed.step_frame_headless({ev(ET::MouseUp, key_x(33), y1)});
    ed.step_frame_headless();
    std::printf("    the thin box selected %zu keys (x, y, z at 20 and y at 30: 4):", st.selected.size());
    for (const auto &k : st.selected) std::printf(" (%d, %g)", k.first, k.second);
    std::printf("; rows at y %d (group), %d (z); box %d..%d\n", grp.y, zrow.y, y0, y1);
    CHECK(st.selected.size() == 4);
    /* Ctrl+box takes them off again. */
    ed.step_frame_headless({ev(ET::MouseMove, key_x(15), y0, platform::MOD_CTRL)});  // (not where the last box began: that would be a double-click)
    ed.step_frame_headless({ev(ET::MouseDown, key_x(15), y0, platform::MOD_CTRL)});
    ed.step_frame_headless({ev(ET::MouseMove, key_x(33), y1, platform::MOD_CTRL)});
    ed.step_frame_headless({ev(ET::MouseUp, key_x(33), y1, platform::MOD_CTRL)});
    ed.step_frame_headless();
    CHECK(st.selected.empty());
  });
  test("auto-rig: a marker dragged in the side view keeps its depth through the body (it doesn't jump to the nearer leg)", [] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.command("create Cylinder");
    ed.command("autorig humanoid");
    ed.command("autorig symmetry off");
    ed.command("autorig mark LeftKnee 0.15 0.4 0.1");
    ed.command("autorig view left");
    const double t0 = now_seconds();
    while ((ed.scene_camera_for_test().animating || now_seconds() - t0 < 0.5) && now_seconds() - t0 < 20.0)
      ed.step_frame_headless();  // (until the camera's turn ends: a sanitizer build's frames are slow)
    const Vec3 before = ed.autorig_for_test().markers.at("LeftKnee");
    int x = 0, y = 0;
    CHECK(ed.screen_of_for_test(before, x, y));
    platform::Event m, d, mv, u;
    m.type = mv.type = platform::EventType::MouseMove, d.type = platform::EventType::MouseDown, u.type = platform::EventType::MouseUp;
    m.x = d.x = x, m.y = d.y = y, mv.x = u.x = x + 15, mv.y = u.y = y - 25;
    ed.step_frame_headless({m});
    ed.step_frame_headless({d});
    ed.step_frame_headless({mv});
    ed.step_frame_headless({u});
    ed.step_frame_headless();
    const Vec3 after = ed.autorig_for_test().markers.at("LeftKnee");
    std::printf("    the knee (%.3f %.3f %.3f) -> (%.3f %.3f %.3f) in the left view\n", before.x, before.y, before.z, after.x, after.y, after.z);
    CHECK(std::fabs(after.x - before.x) < 1e-3f && length(after - before) > 0.02f);
  });
}

static void tests_round141() {
  /* ---------------------------------------------------------------- round 141: AutoPosing (task 0122) */
  /* GameObject > Rig > Humanoid's person (1.7 m, IK on the hands and feet), weighed with capsules. */
  auto r141_pose_of = [](GameObject &arm) {
    std::vector<std::pair<GameObject *, Quat>> out;
    std::vector<GameObject *> st{&arm};
    while (!st.empty()) {
      GameObject *g = st.back();
      st.pop_back();
      for (GameObject *c : g->children) st.push_back(c);
      if (g->get<Bone>()) out.push_back({g, g->local().rotation});
    }
    return out;
  };
  auto r141_angle = [](Quat a, Quat b) {
    const Quat d = normalize(a * b.conjugate());
    return 2.0f * std::acos(std::min(1.0f, std::fabs(d.w))) * 57.2957795f;
  };
  test("autopose: a dragged hand reaches a target in reach, the pinned feet stay, every joint within its limit, the rest of the body barely moved", [&] {
    Scene s;
    GameObject *rig = create_rig(s, false, Vec3(0.0f));
    const body::Body b = body::build(s, *rig, 75.0f);
    GameObject *hand = s.find_by_name("LeftHand"), *lf = s.find_by_name("LeftFoot"), *rf = s.find_by_name("RightFoot");
    const auto before = r141_pose_of(*rig);
    const Vec3 lf0 = lf->world_position(), rf0 = rf->world_position(), to = hand->world_position() + Vec3(0.15f, 0.3f, 0.25f);
    const autopose::Result r = autopose::solve(*rig, {{hand, to, 1.0f}}, {lf, rf}, &b);
    float worst = 0, head = 0;
    for (const auto &p : before) {
      const float a = r141_angle(p.first->local().rotation, p.second);
      worst = std::max(worst, a);
      if (p.first->name == "Head") head = a;
    }
    std::printf("    %zu joints, %d iterations, %.2f ms: the hand %.2f mm off, the feet %.2f mm; the largest turn %.1f degrees, the head's %.1f; balance %.1f cm\n",
                r.bones, r.iterations, r.ms, r.error * 1000.0f, r.pin_error * 1000.0f, worst, head, r.balance * 100.0f);
    CHECK(r.error < 0.01f && length(lf->world_position() - lf0) < 0.005f && length(rf->world_position() - rf0) < 0.005f);
    CHECK(worst <= 75.0f + 0.5f && head < 10.0f && r.ms < 16.0f);  // (live while dragging: under a frame)
  });
  test("autopose: balance keeps the center of mass over the feet when the head is pushed forward; out of reach the arm points at the target, nothing breaks", [&] {
    float off[2] = {0, 0};
    for (int k = 0; k < 2; k++) {
      Scene s;
      GameObject *rig = create_rig(s, false, Vec3(0.0f));
      const body::Body b = body::build(s, *rig, 75.0f);
      GameObject *head = s.find_by_name("Head"), *lf = s.find_by_name("LeftFoot"), *rf = s.find_by_name("RightFoot");
      autopose::Settings st;
      st.balance = k == 0;
      const autopose::Result r = autopose::solve(*rig, {{head, head->world_position() + Vec3(0, -0.1f, 0.35f), 1.0f}}, {lf, rf}, &b, st);
      std::vector<ArmatureBone> now;
      armature_bones(*rig, now);
      const Vec3 c = body::state(b, now).com, mid = (lf->world_position() + rf->world_position()) * 0.5f;
      off[k] = std::sqrt((c.x - mid.x) * (c.x - mid.x) + (c.z - mid.z) * (c.z - mid.z));
      (void)r;
    }
    std::printf("    head forward: the center of mass %.1f cm from over the feet with balance, %.1f cm without\n", off[0] * 100.0f, off[1] * 100.0f);
    CHECK(off[0] < 0.6f * off[1]);
    Scene s;
    GameObject *rig = create_rig(s, false, Vec3(0.0f));
    const body::Body b = body::build(s, *rig, 75.0f);
    GameObject *hand = s.find_by_name("LeftHand"), *ua = s.find_by_name("LeftUpperArm"), *lf = s.find_by_name("LeftFoot"), *rf = s.find_by_name("RightFoot");
    const Vec3 lf0 = lf->world_position(), far = Vec3(-5.0f, 1.5f, 0.0f);
    const autopose::Result r = autopose::solve(*rig, {{hand, far, 1.0f}}, {lf, rf}, &b);
    const Vec3 arm = normalize(hand->world_position() - ua->world_position()), want = normalize(far - ua->world_position());
    bool finite = true;
    for (const auto &p : r141_pose_of(*rig)) finite = finite && std::isfinite(p.first->local().rotation.w);
    std::printf("    5 m away: %.2f m short (said), the arm %.1f degrees off the target's direction, the foot %.2f mm, all finite: %s\n", r.error,
                std::acos(std::min(1.0f, dot(arm, want))) * 57.2957795f, length(lf->world_position() - lf0) * 1000.0f, finite ? "yes" : "NO");
    /* (a 5 m pull leans the hips to their 30 cm limit and the legs to theirs: the feet within 2 cm then; a reach within
     * the body's holds them to a millimetre, above) */
    CHECK(finite && r.error > 3.0f && dot(arm, want) > 0.9f && length(lf->world_position() - lf0) < 0.02f);
  });
  test("autopose (review): the hips dragged down crouch the body - the feet stay down and flat, the knees bend forward, not sideways", [&] {
    Scene s;
    GameObject *rig = create_rig(s, false, Vec3(0.0f));
    const body::Body b = body::build(s, *rig, 75.0f);
    GameObject *hips = s.find_by_name("Hips"), *lf = s.find_by_name("LeftFoot"), *rf = s.find_by_name("RightFoot"), *knee = s.find_by_name("LeftLowerLeg");
    const Vec3 lf0 = lf->world_position(), rf0 = rf->world_position(), knee0 = knee->world_position();
    const Quat lq0 = lf->world_rotation();
    const autopose::Result r = autopose::solve(*rig, {{hips, hips->world_position() + Vec3(0, -0.25f, 0), 1.0f}}, {lf, rf}, &b);
    const Vec3 dk = knee->world_position() - knee0;
    const Quat dq = normalize(lf->world_rotation() * lq0.conjugate());
    const float foot_turn = 2.0f * std::acos(std::min(1.0f, std::fabs(dq.w))) * 57.2957795f;
    std::printf("    hips 25 cm down: %.1f mm off; the feet %.2f mm and %.2f degrees; the knee moved (%.3f, %.3f, %.3f)\n", r.error * 1000.0f,
                std::max(length(lf->world_position() - lf0), length(rf->world_position() - rf0)) * 1000.0f, foot_turn, dk.x, dk.y, dk.z);
    CHECK(r.error < 0.01f && length(lf->world_position() - lf0) < 0.005f && length(rf->world_position() - rf0) < 0.005f && foot_turn < 1.0f);
    CHECK(dk.z > 0.03f && std::fabs(dk.x) < 0.5f * dk.z);  // (forward, the way the knee already bends - the rig faces +Z)
  });
  test("autopose in the editor: a drag poses the body, keys it at the playhead (one undo step); IK targets follow; a locked hand stays", [&] {
    Editor ed;
    r34_open(ed);
    GameObject *rig = create_rig(ed.scene(), false, Vec3(0.0f));
    ed.mark_changed_for_test("Add the rig");
    ed.command("select " + rig->name);
    ed.command("anim new Pose");
    ed.step_frame_headless();
    ed.command("autopose on");
    GameObject *hand = ed.scene().find_by_name("LeftHand");
    /* (within the arm's reach: with the other hand locked the chest can't turn to help) */
    const Vec3 to = hand->world_position() + Vec3(0.15f, 0.2f, 0.1f), rh0 = ed.scene().find_by_name("RightHand")->world_position();
    ed.command("autopose lock RightHand");
    ed.command(strprintf("autopose drag LeftHand %f %f %f", to.x, to.y, to.z));
    const Vec3 elbow = ed.scene().find_by_name("LeftLowerArm")->world_position();
    auto dbg = [&](const char *when) {
      const Vec3 h = ed.scene().find_by_name("Hips")->world_position();
      std::printf("    DEBUG %s: hand %.3f off, hips (%.3f %.3f %.3f)\n", when, length(ed.scene().find_by_name("LeftHand")->world_position() - to), h.x, h.y, h.z);
    };
    dbg("after drag");
    ed.step_frame_headless();  // (IK solves on the pose: its target and pole moved with it, nothing pulls back)
    dbg("after IK");
    Scene &s = ed.scene();
    const float elbow_kept = length(s.find_by_name("LeftLowerArm")->world_position() - elbow);
    const float reached = length(s.find_by_name("LeftHand")->world_position() - to), kept = length(s.find_by_name("RightHand")->world_position() - rh0);
    AnimationClipPtr clip = ed.anim_clip_for_test();
    /* (IK turns the arm: its target is what's keyed, and IK poses the arm from it) */
    const std::string arm_path = clip ? anim_path(*rig, *s.find_by_name("LeftHandIK")) : "";
    const bool keyed = clip && clip->find(arm_path, "Position", 0) != nullptr;
    ed.command("undo");
    ed.step_frame_headless();
    AnimationClipPtr c2 = ed.anim_clip_for_test();
    const bool undone = c2 && c2->find(arm_path, "Position", 0) == nullptr;
    std::printf("    the hand %.1f mm from the target after IK, the elbow %.1f mm from where AutoPose put it; the locked hand moved %.1f mm; keyed: %s; "
                "undone in one step: %s\n", reached * 1000.0f, elbow_kept * 1000.0f, kept * 1000.0f, keyed ? "yes" : "no", undone ? "yes" : "no");
    CHECK(reached < 0.02f && elbow_kept < 0.02f && kept < 0.01f && keyed && undone);
  });
}

static void tests_round149_to_round151() {
  /* ---------------------------------------------------------------- round 149: the AI download (task 0129) */
  /* The model's skeleton (tools/inbetween/cmu.py's, from the CMU data) and a stand-in for the network: the interpolation
   * it is given, the hips lifted by `bump` metres mid-span (so the tests see the span made), counted. */
  struct R149Fake : ai::Runtime {
    float bump = 0.0f;
    int calls = 0;
    R149Fake() {
      const float o[22][3] = {{0.000f, 0.000f, 0.000f}, {0.090f, -0.096f, 0.047f}, {0.135f, -0.371f, 0.000f}, {0.144f, -0.396f, 0.000f}, {0.010f, -0.026f, 0.110f},
                              {-0.088f, -0.096f, 0.047f}, {-0.136f, -0.375f, 0.000f}, {-0.144f, -0.395f, 0.000f}, {-0.012f, -0.032f, 0.112f}, {0.000f, 0.000f, 0.000f},
                              {0.000f, 0.114f, -0.008f}, {0.001f, 0.114f, 0.001f}, {0.001f, 0.115f, 0.005f}, {0.001f, 0.188f, -0.004f}, {0.001f, 0.115f, 0.005f},
                              {0.181f, 0.067f, -0.006f}, {0.283f, 0.000f, 0.000f}, {0.189f, 0.000f, 0.000f}, {0.001f, 0.115f, 0.005f}, {-0.178f, 0.068f, -0.009f},
                              {-0.287f, 0.000f, 0.000f}, {-0.189f, 0.000f, 0.000f}};
      for (const auto &v : o) info.offsets.push_back(Vec3(v[0], v[1], v[2]));
      info.parents = {-1, 0, 1, 2, 3, 0, 5, 6, 7, 0, 9, 10, 11, 12, 11, 14, 15, 16, 11, 18, 19, 20};
      info.styles = {"walk", "run", "jump", "fall", "crawl", "combat", "acrobatic", "other"};
      info.hidden = 4;
    }
    bool run(const std::vector<float> &frames, const std::vector<float> &known, int, int W, std::vector<float> &out, std::string &,
             std::vector<float> *hidden) override {
      calls++;
      out = frames;
      int first = -1, last = -1;
      for (int t = 0; t < W; t++)
        if (known[(size_t)t] == 0.0f) last = t, first = first < 0 ? t : first;
      for (int t = first; first >= 0 && t <= last; t++) out[(size_t)t * ai::kFeatures + 1] += bump * std::sin(3.14159265f * (float)(t - first + 1) / (float)(last - first + 2));
      if (!hidden) return true;
      /* Its "last features": where each frame is in the span (enough for a fit to learn the lift away). */
      hidden->assign((size_t)W * info.hidden, 0.0f);
      for (int t = first; first >= 0 && t <= last; t++) {
        const float u = (float)(t - first + 1) / (float)(last - first + 2);
        float *h = &(*hidden)[(size_t)t * info.hidden];
        h[0] = std::sin(3.14159265f * u), h[1] = u, h[2] = u * u, h[3] = 1.0f;
      }
      return true;
    }
  };
  /* An archive of `files` (name, text) in `dir`, the platform's kind (zip on Windows), and its path. */
  auto r149_archive = [](const std::string &dir, const std::vector<std::pair<std::string, std::string>> &files) {
    const std::string src = fs::join(dir, "src");
    fs::make_dirs(src);
    for (const auto &f : files) fs::write_file(fs::join(src, f.first), f.second);
    const std::string path = fs::join(dir, ai::archive_name());
    std::vector<std::string> argv;
#ifdef _WIN32
    const char *root = std::getenv("SystemRoot");
    argv = {fs::join(fs::join(root ? root : "C:\\Windows", "System32"), "tar.exe"), "-a", "-cf", path, "-C", src};
#else
    argv = {"tar", "-czf", path, "-C", src};
#endif
    for (const auto &f : files) argv.push_back(f.first);
    sys::run_program(argv);
    return path;
  };
  test("ai download: refused when it doesn't match its checksum; an install that can't run is removed again; without it, AI Inbetween says how to get it", [&] {
    const std::string dir = fs::join(test_dir(), "r149");
    const std::string install = fs::join(dir, "install");
    set_env("BLENDITY_AI_DIR", install);
    std::string e;
    ai::remove(e);
    const std::string archive = r149_archive(dir, {{"inbetween.json", "{\"offsets\": [], \"parents\": [], \"styles\": [\"walk\"], \"ctx\": 10}"},
                                                   {"inbetween.onnx", "not a model"}});
    fs::write_file(archive + ".sha256", std::string(64, '0') + "  " + ai::archive_name() + "\n");
    Editor ed;
    r34_open(ed);
    ed.command("ai install " + archive);  // (the checksum beside it doesn't match: refused, nothing unpacked)
    for (int i = 0; i < 2000 && ed.ai_chore_busy_for_test(); i++) std::this_thread::sleep_for(std::chrono::milliseconds(2)), ed.step_frame_headless();
    const bool refused = !fs::exists(fs::join(install, "inbetween.onnx"));
    fs::remove_file(archive + ".sha256");
    ed.command("ai install " + archive);  // (no checksum: unpacked, but there is no ONNX Runtime in it - removed again)
    for (int i = 0; i < 2000 && ed.ai_chore_busy_for_test(); i++) std::this_thread::sleep_for(std::chrono::milliseconds(2)), ed.step_frame_headless();
    const bool removed = !fs::exists(fs::join(install, "inbetween.onnx")) && !ai::install_problem().empty();
    ed.command("assist on ai");
    GameObject *rig = create_rig(ed.scene(), false, Vec3(20.0f, 0.0f, 0.0f));
    ed.mark_changed_for_test("A rig");
    ed.command("select " + rig->name);
    ed.step_frame_headless();
    ed.command("ai span 0 10");
    const bool nothing = !ed.ai_busy_for_test() && !ed.ai_ghost_for_test();
    std::printf("    a wrong checksum refused: %s; an install that can't run removed: %s; without it nothing made: %s ('%s')\n", refused ? "yes" : "no",
                removed ? "yes" : "no", nothing ? "yes" : "no", ai::install_problem().c_str());
    CHECK(refused && removed && nothing);
    /* An archive holding anything but the download's own files: refused before anything is unpacked. */
    const std::string odd = r149_archive(fs::join(dir, "odd"), {{"inbetween.onnx", "x"}, {"evil.txt", "not ours"}});
    ed.command("ai install " + odd);
    for (int i = 0; i < 2000 && ed.ai_chore_busy_for_test(); i++) std::this_thread::sleep_for(std::chrono::milliseconds(2)), ed.step_frame_headless();
    const bool foreign = !fs::exists(fs::join(install, "evil.txt")) && !fs::exists(fs::join(install + ".new", "evil.txt"));
    std::printf("    an archive with a file of another kind refused, nothing unpacked: %s\n", foreign ? "yes" : "no");
    CHECK(foreign);
    set_env("BLENDITY_AI_DIR", fs::join(test_dir(), "no_ai"));
  });
  /* With a real install (BLENDITY_AI_TEST_INSTALL: an archive tools/inbetween/package.py built, unpacked): the
   * network loaded through ONNX Runtime, 60 frames timed. Skipped where there is none (CI). */
  test("ai download: a real install loads ONNX Runtime and makes 60 frames in under 100 ms on the CPU (skipped without one)", [&] {
    const char *real = std::getenv("BLENDITY_AI_TEST_INSTALL");
    if (!real || !*real) return (void)std::printf("    (no BLENDITY_AI_TEST_INSTALL: skipped)\n");
    set_env("BLENDITY_AI_DIR", real);
    std::string e;
    double ms = 0, best = 1e9;
    bool ok = true;
    for (int i = 0; i < 4 && ok; i++) ok = ai::probe(e, &ms), best = std::min(best, ms);  // (the first run warms up)
    std::printf("    ONNX Runtime: %s, 60 frames in %.1f ms\n", ok ? "loaded" : e.c_str(), best);
    CHECK(ok && best < 100.0);
    set_env("BLENDITY_AI_DIR", fs::join(test_dir(), "no_ai"));
  });
  /* ---------------------------------------------------------------- round 150: AI inbetweening in the Animation window (task 0130) */
  /* A person with a clip: keys on the hips' height and a thigh's turn at frames 0, 10, 40 and 50. */
  auto r150_person = [](Editor &ed) {
    r34_open(ed);
    GameObject *rig = create_rig(ed.scene(), false, Vec3(20.0f, 0.0f, 0.0f));
    fs::remove_file(fs::join(fs::join(std::getenv("BLENDITY_PROJECT"), "Assets/AI"), rig->name + ".aihead"));  // (no fit from an earlier run)
    ed.mark_changed_for_test("A person");
    ed.command("select " + rig->name);
    ed.step_frame_headless();
    ed.command("anim new Span");
    for (int f : {0, 10, 40, 50}) {
      ed.command(strprintf("anim key Hips Position 1 %d %g", f, 0.95 + 0.02 * (f % 20 == 0)));
      ed.command(strprintf("anim key LeftUpperLeg Rotation 0 %d %g", f, f == 10 ? 30.0 : f == 40 ? -20.0 : 0.0));
    }
    ed.step_frame_headless();
    return rig;
  };
  auto r150_wait = [](Editor &ed) {
    for (int i = 0; i < 400 && ed.ai_busy_for_test(); i++) {
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
      ed.step_frame_headless();
    }
    ed.step_frame_headless();
  };
  test("ai inbetween: only the span between the keys is made - its ends meet them, every other key untouched; the same seed the same keys; Apply undone whole", [&] {
    Editor ed;
    auto fake = std::make_shared<R149Fake>();
    fake->bump = 0.1f;
    ed.ai_set_runtime_for_test(fake);
    r150_person(ed);
    ed.command("ai span 10 40");  // (off: refused)
    const bool off = fake->calls == 0;
    ed.command("assist on ai");
    AnimationClipPtr clip = ed.anim_clip_for_test();
    AnimationClip before = *clip;
    ed.command("ai span 10 40");
    r150_wait(ed);
    AnimationClip made = ed.ai_result_for_test();
    const AnimTrack *hy = made.find("Hips", "Position", 1);
    const AnimTrack *hy0 = before.find("Hips", "Position", 1);
    /* Outside the span and its ends: the very same keys. */
    bool outside = hy && hy0;
    for (const AnimTrack &t0 : before.tracks) {
      const AnimTrack *t = const_cast<AnimationClip &>(made).find(t0.path, t0.property, t0.axis);
      for (const AnimKey &k : t0.keys)
        if (k.frame <= 10.01f || k.frame >= 39.99f) outside = outside && t && t->key_at(k.frame) >= 0 && std::fabs(t->keys[(size_t)t->key_at(k.frame)].value - k.value) < 1e-5f;
    }
    int inside = 0;
    for (int f = 11; hy && f < 40; f++) inside += hy->key_at((float)f) >= 0;
    const float lift = hy && hy0 ? hy->sample(25.0f) - hy0->sample(25.0f) : 0.0f;
    /* The same seed again: the same keys. */
    ed.command("ai span 10 40");
    r150_wait(ed);
    AnimationClip second = ed.ai_result_for_test();
    const AnimTrack *again = second.find("Hips", "Position", 1);
    bool same = again && hy && again->keys.size() == hy->keys.size();
    for (size_t i = 0; same && i < hy->keys.size(); i++) same = std::fabs(again->keys[i].value - hy->keys[i].value) < 1e-6f;
    /* Apply, then undo: the clip as it was. */
    ed.command("ai apply");
    ed.step_frame_headless();
    const bool applied = ed.anim_clip_for_test()->find("Hips", "Position", 1)->key_at(25.0f) >= 0;
    ed.command("undo");
    ed.step_frame_headless();
    const AnimTrack *u = ed.anim_clip_for_test()->find("Hips", "Position", 1);
    const bool undone = u && u->keys.size() == hy0->keys.size() && u->key_at(25.0f) < 0;
    /* (0.1 of the model's metres, scaled to this rig's legs: about 0.11 m) */
    std::printf("    off: refused %s; %d of 29 frames inside keyed, the hips %.3f m higher mid-span (the stand-in's lift); outside untouched: %s; "
                "same seed same keys: %s; applied %s, undone whole %s\n",
                off ? "yes" : "no", inside, lift, outside ? "yes" : "no", same ? "yes" : "no", applied ? "yes" : "no", undone ? "yes" : "no");
    CHECK(off && inside == 29 && lift > 0.07f && lift < 0.15f && outside && same && applied && undone);
  });
  /* With the real model (BLENDITY_AI_TEST_INSTALL): a walk's middle 29 frames taken out and made again come closer to
   * the walk than the keys' own interpolation does - the feet, hands and head. Skipped without one (CI). */
  test("ai inbetween with the real model: a walk's middle made again is closer to the walk than the keys' interpolation (skipped without one)", [&] {
    const char *real = std::getenv("BLENDITY_AI_TEST_INSTALL");
    if (!real || !*real) return (void)std::printf("    (no BLENDITY_AI_TEST_INSTALL: skipped)\n");
    set_env("BLENDITY_AI_DIR", real);
    Editor ed;
    r34_open(ed);
    GameObject *rig = create_rig(ed.scene(), false, Vec3(20.0f, 0.0f, 0.0f));
    ed.mark_changed_for_test("A person");
    ed.command("select " + rig->name);
    ed.step_frame_headless();
    ed.command("assist on walk");
    ed.command("assist on ai");
    ed.command("anim new Walk");
    ed.command("walk cycle walk 1.4 3 travel");
    ed.step_frame_headless();
    AnimationClip walk = *ed.anim_clip_for_test();
    const int from = 20, to = 50;
    /* The keys inside the span deleted: what the keys' interpolation gives there. */
    AnimationClip holed = walk;
    for (AnimTrack &t : holed.tracks)
      t.keys.erase(std::remove_if(t.keys.begin(), t.keys.end(), [&](const AnimKey &k) { return k.frame > from + 0.01f && k.frame < to - 0.01f; }), t.keys.end());
    *ed.anim_clip_for_test() = holed;
    ed.anim_clip_for_test()->touch();
    ed.command("ai style walk");
    ed.command(strprintf("ai span %d %d", from, to));
    r150_wait(ed);
    const gait::Rig g = gait::analyse(ed.scene(), *rig);
    std::vector<GameObject *> ends{g.legs[0].foot, g.legs[1].foot, g.arms[0].hand, g.arms[1].hand, g.head};
    auto places = [&](const AnimationClip &c) {
      std::vector<Vec3> p;
      body::for_each_frame(ed.scene(), *rig, c, from + 1, to - 1, [&](int) {
        for (GameObject *e : ends) p.push_back(e->world_position());
      });
      return p;
    };
    const std::vector<Vec3> truth = places(walk), lerp = places(holed), net = places(ed.ai_result_for_test());
    double el = 0, en = 0;
    for (size_t i = 0; i < truth.size() && i < net.size(); i++) el += length(lerp[i] - truth[i]), en += length(net[i] - truth[i]);
    el /= std::max<size_t>(1, truth.size()), en /= std::max<size_t>(1, truth.size());
    std::printf("    a walk's frames %d-%d made again: the feet, hands and head %.1f cm from the walk on average; the keys' interpolation %.1f cm\n",
                from + 1, to - 1, en * 100.0, el * 100.0);
    CHECK(ed.ai_ghost_for_test() && en < el);
    set_env("BLENDITY_AI_DIR", fs::join(test_dir(), "no_ai"));
  });
  test("ai inbetween: the curves outside the span keep their shape; a yaw keyed past 180 degrees is met without a spin; another character's clip never gets it", [&] {
    Editor ed;
    auto fake = std::make_shared<R149Fake>();
    ed.ai_set_runtime_for_test(fake);
    GameObject *rig = r150_person(ed);
    ed.command("assist on ai");
    /* Not a peak at the ends (an Auto slope there would follow the new keys inside), and the hips turning 200 degrees. */
    for (int f : {0, 10, 40, 50}) ed.command(strprintf("anim key Hips Position 1 %d %g", f, 0.90 + 0.05 * f / 10.0 * (f > 10 ? 0.5 : 1.0)));
    ed.command("anim key Hips Rotation 1 10 0");
    ed.command("anim key Hips Rotation 1 40 200");
    ed.step_frame_headless();
    AnimationClip before = *ed.anim_clip_for_test();
    ed.command("ai span 10 40");
    r150_wait(ed);
    AnimationClip made = ed.ai_result_for_test();
    const AnimTrack *b = before.find("Hips", "Position", 1), *a = made.find("Hips", "Position", 1);
    float outside = 0.0f;
    for (float f : {2.5f, 5.0f, 7.5f, 42.5f, 45.0f, 47.5f}) outside = std::max(outside, std::fabs(a->sample(f) - b->sample(f)));
    const AnimTrack *yaw = made.find("Hips", "Rotation", 1);
    float jump = 0.0f;
    for (float f = 10.0f; f < 40.0f; f += 1.0f) jump = std::max(jump, std::fabs(yaw->sample(f + 1.0f) - yaw->sample(f)));
    /* Another character selected: Apply refused, its clip untouched. */
    GameObject *other = create_rig(ed.scene(), false, Vec3(25.0f, 0.0f, 0.0f));
    ed.mark_changed_for_test("Another");
    ed.command("select " + other->name);
    ed.step_frame_headless();
    ed.command("anim new Other");
    ed.command("anim key Hips Position 1 0 0.9");
    ed.step_frame_headless();
    const size_t tracks = ed.anim_clip_for_test()->tracks.size();
    ed.command("ai apply");
    ed.step_frame_headless();
    const bool kept = ed.anim_clip_for_test()->tracks.size() == tracks && ed.anim_clip_for_test()->name != before.name;
    std::printf("    outside the span the hips' curve moved at most %.6f m; the yaw's largest step inside %.1f degrees (0 to 200 over 30 frames); "
                "another character's clip untouched by Apply: %s\n", outside, jump, kept ? "yes" : "no");
    CHECK(outside < 1e-4f && jump < 12.0f && kept);
    (void)rig;
  });
  test("ai inbetween: refused for four legs, a span past the clip's end, and an answer that isn't numbers", [&] {
    Editor ed;
    auto fake = std::make_shared<R149Fake>();
    ed.ai_set_runtime_for_test(fake);
    r150_person(ed);
    ed.command("assist on ai");
    ed.command("ai span 40 400");
    r150_wait(ed);
    const bool past = !ed.ai_ghost_for_test() && fake->calls == 0;
    fake->bump = std::numeric_limits<float>::quiet_NaN();
    ed.command("ai span 10 40");
    r150_wait(ed);
    const bool nan = !ed.ai_ghost_for_test() && fake->calls == 1;
    Editor dog_ed;
    dog_ed.ai_set_runtime_for_test(std::make_shared<R149Fake>());
    r34_open(dog_ed);
    GameObject *dog = create_rig(dog_ed.scene(), true, Vec3(20.0f, 0.0f, 0.0f));
    dog_ed.mark_changed_for_test("A dog");
    dog_ed.command("select " + dog->name);
    dog_ed.step_frame_headless();
    dog_ed.command("anim new Dog");
    dog_ed.command("anim key Hips Position 1 0 0.5");
    dog_ed.command("anim key Hips Position 1 20 0.5");
    dog_ed.command("assist on ai");
    dog_ed.command("ai span 0 20");
    r150_wait(dog_ed);
    const bool four = !dog_ed.ai_ghost_for_test() && !dog_ed.ai_busy_for_test();
    std::printf("    refused: a span past the end %s, a NaN answer %s, a four-legged rig %s\n", past ? "yes" : "no", nan ? "yes" : "no", four ? "yes" : "no");
    CHECK(past && nan && four);
  });
  test("ai inbetween: a person's pose goes to the model's skeleton and back unchanged (mirrored, faced, scaled, the rest poses aligned)", [&] {
    Editor ed;
    auto fake = std::make_shared<R149Fake>();
    ed.ai_set_runtime_for_test(fake);
    GameObject *rig = r150_person(ed);
    ed.command("assist on ai");
    /* Both ends one pose (frame 10's: the thigh turned 30 degrees): what the stand-in hands back (that pose
     * throughout) must come out as exactly that pose on every made frame. */
    ed.command("anim key LeftUpperLeg Rotation 0 40 30");
    ed.command("anim key Hips Position 1 40 0.95");
    ed.step_frame_headless();
    ed.command("ai span 10 40");
    r150_wait(ed);
    const gait::Rig g = gait::analyse(ed.scene(), *rig);
    std::vector<std::pair<GameObject *, Quat>> at10;
    AnimationClipPtr clip = ed.anim_clip_for_test();
    body::for_each_frame(ed.scene(), *rig, *clip, 10, 10, [&](int) {
      for (GameObject *b : {g.hips, g.legs[0].upper, g.legs[0].lower, g.legs[1].upper, g.spine.empty() ? g.hips : g.spine[0], g.arms[0].upper})
        at10.push_back({b, b->world_rotation()});
    });
    float worst = 0.0f;
    AnimationClip made = ed.ai_result_for_test();
    body::for_each_frame(ed.scene(), *rig, made, 25, 25, [&](int) {
      for (const auto &b : at10) {
        const Quat d = b.first->world_rotation() * b.second.conjugate();
        worst = std::max(worst, 2.0f * std::acos(std::min(1.0f, std::fabs(d.w))) * 57.29578f);
      }
    });
    std::printf("    the pose through the model's skeleton and back: at most %.3f degrees off\n", worst);
    CHECK(ed.ai_ghost_for_test() && worst < 0.5f);
  });
  /* ---------------------------------------------------------------- round 151: fine-tuning on your own clips (task 0131) */
  test("ai fine-tune: fitted to the character's clips it misses them less; saved beside the project; off, the shipped model again; Stop keeps the last fit", [&] {
    Editor ed;
    auto fake = std::make_shared<R149Fake>();
    fake->bump = 0.1f;  // (the stand-in's habit the clips don't have: a fit should learn it away)
    ed.ai_set_runtime_for_test(fake);
    GameObject *rig = r150_person(ed);
    ed.command("assist on ai");
    ed.command("assist on walk");
    ed.command("anim new Walk");
    ed.command("walk cycle walk 1.4 3 travel");  // (a second clip of this person: a walk)
    ed.step_frame_headless();
    auto four_keys = [&] {  // (r150_person's keys, in the current clip)
      for (int f : {0, 10, 40, 50}) {
        ed.command("anim key Hips Position 1 " + std::to_string(f) + (f % 20 == 0 ? " 0.97" : " 0.95"));
        ed.command("anim key LeftUpperLeg Rotation 0 " + std::to_string(f) + (f == 10 ? " 30" : f == 40 ? " -20" : " 0"));
      }
      ed.step_frame_headless();
    };
    auto wait_tune = [&] {
      for (int i = 0; i < 2000 && ed.ai_tuning_for_test(); i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        ed.step_frame_headless();
      }
    };
    /* A span made by the shipped model first (the stand-in's lift), then the fit, then the span again with it and
     * without: off, exactly the shipped model's answer. */
    auto lift = [&] {
      ed.command("ai span 10 40");
      r150_wait(ed);
      AnimationClip made = ed.ai_result_for_test();
      AnimationClipPtr clip = ed.anim_clip_for_test();
      const AnimTrack *a = made.find("Hips", "Position", 1), *b = clip ? clip->find("Hips", "Position", 1) : nullptr;
      return a && b ? a->sample(25.0f) - b->sample(25.0f) : 99.0f;
    };
    ed.command("anim new Check");
    four_keys();
    const float shipped = lift();
    ed.command("ai finetune");
    wait_tune();
    const ai::Head fitted = ed.ai_head_for_test();
    const std::string file = fs::join(fs::join(std::getenv("BLENDITY_PROJECT"), "Assets/AI"), rig->name + ".aihead");
    const bool saved = fs::exists(file);
    const float with = lift();
    ed.command("ai finetune off");
    const float without = lift();
    ed.command("ai finetune on");
    /* Stop at once: the fit already made stays. */
    std::string text0;
    fs::read_file(file, text0);
    ed.command("ai finetune");
    ed.command("ai finetune stop");
    wait_tune();
    std::string text1;
    fs::read_file(file, text1);
    std::printf("    fitted on %d windows: their RMS error %.5f before, %.5f after; saved: %s; a made span's lift %.4f m by the shipped model, %.4f m with "
                "the fit, %.4f m with it off; stopped at once, the last fit kept: %s\n",
                fitted.windows, fitted.before, fitted.after, saved ? "yes" : "no", shipped, with, without, text0 == text1 && ed.ai_head_for_test().valid() ? "yes" : "no");
    CHECK(fitted.valid() && fitted.windows > 20 && fitted.after < fitted.before && saved);
    CHECK(std::fabs(without - shipped) < 1e-6f && std::fabs(with - shipped) > 1e-4f && text0 == text1 && ed.ai_head_for_test().valid());
    fs::remove_file(file);  // (the next run starts without a fit)
  });
}

static void tests_round174() {
  /* ---------------------------------------------------------------- round 174: IK handles in metres, a size preference, rotatable targets (task 0156) */
  test("IK handles: a target's box is a fifth of the shin on a rig scaled 100 or 0.01 as on one at 1, Fit Control Sizes corrects a loaded rig, and the size preference scales the wire and the pick", [&] {
    /* The drawn box's edge against the shin's world length, for a rig at three scales. */
    auto box_edge = [&](Editor &ed, const char *target) {
      GameObject *t = ed.scene().find_by_name(target);
      std::vector<std::pair<Vec3, Vec3>> segs;
      ed.rig_control_segments(*t, *t->get<RigControl>(), segs);
      float edge = 0.0f;
      for (auto &sg : segs) edge = std::max(edge, length(sg.second - sg.first));
      return edge;
    };
    auto shin_len = [&](Editor &ed) {
      GameObject *s = ed.scene().find_by_name("RightLowerLeg");
      return length(s->world_matrix().point({0, s->get<Bone>()->length, 0}) - s->world_position());
    };
    float ratio[3] = {0, 0, 0};
    const float scales[3] = {1.0f, 100.0f, 0.01f};
    for (int k = 0; k < 3; k++) {
      Editor ed;
      ed.init_headless(800, 500);
      ed.command("create Armature");  // (a plain armature, scaled before the rig is built on it)
      ed.command("rig humanoid");
      GameObject *rig = ed.scene().find_by_name("Humanoid");
      rig->set_local_scale(Vec3(scales[k]));
      ed.step_frame_headless();
      ed.command("ik fitsizes Humanoid");  // (the rig's bones were made at scale 1: the handles are refitted to the scaled lengths)
      ed.step_frame_headless();
      ratio[k] = box_edge(ed, "RightFootIK") / shin_len(ed);
    }
    std::printf("    the target box's edge over the shin's world length: %.3f at scale 1, %.3f at 100, %.3f at 0.01 (0.4 = a fifth of the bone each side)\n", ratio[0], ratio[1],
                ratio[2]);
    for (int k = 0; k < 3; k++) CHECK(std::fabs(ratio[k] - 0.4f) < 0.004f);
    /* IK added to a rig already scaled by 100 (an FBX import's root): a fifth of the shin at once, no refit. */
    {
      Editor ed;
      ed.init_headless(800, 500);
      ed.command("rig humanoid");
      ed.command("ik remove RightLowerLeg");
      ed.scene().find_by_name("Humanoid")->set_local_scale(Vec3(100.0f));
      ed.step_frame_headless();
      ed.command("ik add RightLowerLeg");
      ed.step_frame_headless();
      const float at_creation = box_edge(ed, "RightLowerLegIK") / shin_len(ed);
      std::printf("    IK added on a rig already at scale 100: the box %.3f of the shin at once\n", at_creation);
      CHECK(std::fabs(at_creation - 0.4f) < 0.004f);
    }
    /* A loaded rig whose FK handles were sized the old way (the bone's own length, not metres) under a root scaled by
     * 100: a hundred times too small, until Fit Control Sizes. */
    Editor ed;
    ed.init_headless(800, 500);
    ed.command("rig humanoid");
    GameObject *rig = ed.scene().find_by_name("Humanoid");
    ed.command("fk add Spine");
    ed.step_frame_headless();
    GameObject *fk = ed.scene().find_by_name("SpineFK"), *spine = ed.scene().find_by_name("Spine"), *t = ed.scene().find_by_name("RightFootIK");
    CHECK(fk && spine && t);
    if (!fk || !spine || !t) return;
    auto fk_ratio = [&] {
      std::vector<std::pair<Vec3, Vec3>> segs;
      ed.rig_control_segments(*fk, *fk->get<RigControl>(), segs);
      float r = 0.0f;
      for (auto &sg : segs) r = std::max(r, length(sg.first - fk->world_position()));
      return r / length(spine->world_matrix().point({0, spine->get<Bone>()->length, 0}) - spine->world_position());
    };
    const float fresh = fk_ratio();
    rig->set_local_scale(Vec3(100.0f));
    fk->get<RigControl>()->size = spine->get<Bone>()->length * 0.3f;  // (as a scene made before this round would hold it)
    ed.step_frame_headless();
    const float wrong = fk_ratio();
    ed.command("ik fitsizes");
    ed.step_frame_headless();
    const float fitted = fk_ratio();
    std::printf("    the FK circle's radius over the spine's length: %.3f fresh, %.4f on an old scaled rig, %.3f after Fit Control Sizes (0.3)\n", fresh, wrong, fitted);
    CHECK(std::fabs(fresh - 0.3f) < 0.003f && wrong < 0.01f && std::fabs(fitted - 0.3f) < 0.003f);
    /* The preference doubles the wire and the pick distance. */
    ed.command("overlay controls size 2");
    ed.step_frame_headless();
    CHECK(std::fabs(box_edge(ed, "RightFootIK") / shin_len(ed) - 0.8f) < 0.008f);
    ed.command("overlay controls size 1");
    /* The control's own scale no longer resizes the wire. */
    t->set_local_scale(Vec3(3.0f));
    ed.step_frame_headless();
    CHECK(std::fabs(box_edge(ed, "RightFootIK") / shin_len(ed) - 0.4f) < 0.004f);
  });

  test("IK handles: Rotate End ticked later keeps the foot as it is, Weight 0 leaves it at rest, a twist bone beside the hand isn't the hand, an FK control on the foot is refused, and a file without the field loads with it off", [&] {
    Editor ed;
    ed.init_headless(800, 500);
    ed.command("rig humanoid");
    ed.step_frame_headless();
    GameObject *shin = ed.scene().find_by_name("RightLowerLeg"), *foot = ed.scene().find_by_name("RightFoot"), *t = ed.scene().find_by_name("RightFootIK");
    IKConstraint *ik = shin ? shin->get<IKConstraint>() : nullptr;
    CHECK(ik && foot && t);
    if (!ik || !foot || !t) return;
    auto fwd_of = [](GameObject *g) { return normalize(g->world_matrix().dir(Vec3(0, 1, 0))); };
    /* Rotate End off, the foot pitched 25 degrees and the handle turned: ticking Rotate End captures the offset on the
     * first solve, so the foot stays as it is instead of snapping to the handle. */
    ik->rotate_end = false, ik->end_set = false, ik->end_offset = Quat();
    foot->set_local_rotation(normalize(Quat::axis_angle(Vec3(1, 0, 0), 25.0f * kPi / 180.0f) * foot->get<Bone>()->rest_rotation));
    t->set_world_rotation(normalize(Quat::axis_angle(Vec3(0, 1, 0), 40.0f * kPi / 180.0f) * t->world_rotation()));
    ed.mark_changed_for_test("pitched, handle turned");
    ed.step_frame_headless();
    const Vec3 held = fwd_of(foot);
    ik->rotate_end = true;  // (the Inspector's tick)
    ed.mark_changed_for_test("tick");
    ed.step_frame_headless();
    std::printf("    Rotate End ticked with the foot pitched: the foot's direction moved by %.3f degrees\n", std::acos(std::max(-1.0f, std::min(1.0f, dot(held, fwd_of(foot))))) * 180.0f / kPi);
    CHECK(ik->end_set && dot(held, fwd_of(foot)) > 0.9999f);
    /* Weight 0: the leg and the foot at rest whatever the handle says. */
    ik->weight = 0.0f;
    t->set_world_rotation(normalize(Quat::axis_angle(Vec3(0, 1, 0), 60.0f * kPi / 180.0f) * t->world_rotation()));
    ed.mark_changed_for_test("weight 0");
    ed.step_frame_headless();
    const Quat fq = foot->local().rotation, pitched = normalize(Quat::axis_angle(Vec3(1, 0, 0), 25.0f * kPi / 180.0f) * foot->get<Bone>()->rest_rotation);
    CHECK(std::fabs(fq.x * pitched.x + fq.y * pitched.y + fq.z * pitched.z + fq.w * pitched.w) > 0.9999f);
    ik->weight = 1.0f;
    /* A twist bone beside the foot (its head up the shin, not at the tip): the foot is still the end bone. */
    GameObject *twist = ed.scene().create("RightShinTwist", shin);
    twist->set_local_position({0, shin->get<Bone>()->length * 0.5f, 0});
    twist->add<Bone>()->length = 0.1f;
    CHECK(ik_end_bone(*shin) == foot);
    /* An FK control on the foot is refused (IK turns it), and the solve settles (no re-solve every frame). */
    ed.command("fk add RightFoot");
    ed.step_frame_headless();
    CHECK(!ed.scene().find_by_name("RightFootFK"));
    ed.step_frame_headless();
    CHECK(!solve_constraints(ed.scene()));
    CHECK(ik_end_driven(ed.scene(), *foot) && !ik_end_driven(ed.scene(), *shin));
    /* A scene saved before Rotate End existed: the field missing, the IK loads with it off. */
    std::string text = save_scene_text(ed.scene()), stripped;
    std::istringstream is(text);
    for (std::string line; std::getline(is, line);)
      if (line.find("Rotate_End") == std::string::npos && line.find("End_Offset") == std::string::npos) stripped += line + "\n";
    Scene back;
    std::string err;
    CHECK(load_scene_text(stripped, back, err));
    const IKConstraint *bk = back.find_by_name("RightLowerLeg") ? back.find_by_name("RightLowerLeg")->get<IKConstraint>() : nullptr;
    CHECK(bk && !bk->rotate_end && !bk->end_set);
  });

}

void run_animation_tests() {
  round64_tests();
  round69_tests();
  tests_round81();
  tests_round82();
  tests_round83_to_round85();
  tests_round86_to_round100();
  tests_round89();
  tests_round90();
  tests_round91();
  tests_round92();
  tests_round93();
  tests_round141();
  tests_round149_to_round151();
  tests_round174();
}
