// SPDX-License-Identifier: GPL-2.0-or-later
// Stress: modifiers on a Blockout piece (round 514, task 0496, ADR 0138). Included by stress_main.cpp; `--only blockout_piece_modifiers`.
// `interior 100` (the bench interior: 36 rooms as an Add and a Subtract box each, 28 door cuts, workers on) with one column inside a
// room carrying a Bevel, and with one carrying an Array of 10. Rows: the time from a change of the column (the Bevel's width, the Array's
// count) until the mesh can be drawn (budget 100 ms), a 120-frame drag of ANOTHER piece (a door: frame median / worst, budget 16.7 ms,
// stack evaluations during it - none wanted), a 120-frame drag of the COLUMN itself (no stack evaluation while it moves: the cache gives
// its mesh), and the first evaluation's cost. Then an untimed sweep of every modifier a piece takes on every shape that takes one, added
// and subtracted, in a small building on the exact and the grid path: problems must be 0, faces newly on top of each other are counted.
// The times are not measurements unless a timed window was arranged (the section prints them either way); --quick shortens the drags.
#include "../src/scene/blockout_piece_stack.h"

namespace bkpm {

using bl::blockout::Op;
using bl::blockout::Piece;
using bl::blockout::Shape;

template <class T, class F>
static std::shared_ptr<Component> mod(F set) {
  auto m = std::make_shared<T>();
  set(*m);
  return m;
}

struct Cost {
  double first_eval_ms = 0.0, add_ms = 0.0;
  std::vector<double> change_ms;
  uint64_t change_evals = 0, changes = 0;
  std::vector<double> other_frame, other_landing, column_frame, column_landing;
  uint64_t other_evals = 0, column_evals = 0, other_main = 0, column_main = 0;
  size_t faces_before = 0, faces_after = 0;
  std::string problem;
};

/* A change made, until the build it caused landed and the frame after it (the frame that draws the new mesh) was stepped. */
static double change_to_drawn(Editor &ed, const std::function<void()> &change, uint64_t &evals) {
  const uint64_t landed0 = ed.blockout_landed_for_test(), e0 = bl::blockout_piece_stack_evaluations();
  const double t0 = now_seconds();
  change();
  while (now_seconds() - t0 < 120.0) {
    ed.step_frame_headless();
    if (ed.blockout_landed_for_test() != landed0 && !ed.blockout_jobs_running_for_test()) break;
  }
  ed.step_frame_headless();
  evals = bl::blockout_piece_stack_evaluations() - e0;
  return (now_seconds() - t0) * 1000.0;
}

/* A piece moved a little every frame for `frames` frames, one every 16.7 ms as a window's; frames and landings timed as the live row does. */
static void drag(Editor &ed, GameObject &piece, int frames, std::vector<double> &frame_ms, std::vector<double> &landing_ms, uint64_t &evals, uint64_t &main_builds) {
  const Vec3 home = piece.local().position;
  const uint64_t landed0 = ed.blockout_landed_for_test(), main0 = ed.blockout_main_builds_for_test(), e0 = bl::blockout_piece_stack_evaluations();
  uint64_t landed = landed0;
  double oldest = -1.0;
  const double start = now_seconds();
  for (int f = 0; f < frames; f++) {
    const double moved_at = now_seconds();
    if (oldest < 0.0) oldest = moved_at;
    piece.set_local_position(home + Vec3(0.0f, 0.0f, 0.02f * (float)(f % 30)));
    ScopedTimer t;
    ed.step_frame_headless();
    frame_ms.push_back(t.ms());
    if (ed.blockout_landed_for_test() != landed) {
      landed = ed.blockout_landed_for_test();
      landing_ms.push_back((now_seconds() - oldest) * 1000.0);
      oldest = -1.0;
    }
    const double due = start + 0.0167 * (double)(f + 1) - now_seconds();
    if (due > 0.0) std::this_thread::sleep_for(std::chrono::duration<double>(due));
  }
  evals = bl::blockout_piece_stack_evaluations() - e0, main_builds = ed.blockout_main_builds_for_test() - main0;
  piece.set_local_position(home);
  ed.command("blockout wait");
}

static Cost run(const Options &o, bool array) {
  Cost c;
  bl::blockout_piece_stack_cache_clear();
  Editor ed;
  ed.init_headless(1280, 720);
  ed.set_async_jobs_for_test(true);
  std::vector<GameObject *> roots;
  ed.scene().for_each([&](GameObject &g) {
    if (!g.parent) roots.push_back(&g);
  });
  ed.scene().destroy(roots);
  GameObject *bo = ed.scene().create("Blockout");
  bo->add<Blockout>();
  bo->add<MeshFilter>()->mesh = std::make_shared<Mesh>();
  bo->add<MeshRenderer>();
  const std::vector<Piece> src = bkstress::interior(6, 28);
  std::vector<GameObject *> objs;
  for (const Piece &p : src) {
    GameObject *g = ed.scene().create("Piece", bo);
    BlockoutPiece *bp = g->add<BlockoutPiece>();
    bp->operation = p.op == Op::Subtract ? 1 : 0;
    bp->size = p.size;
    g->set_local_position(p.to_blockout.translation());
    objs.push_back(g);
  }
  /* The column stands against the building's west wall, overlapping it, so it joins the building's one cluster (inside a room's air it would
   * be cut away: the air is a Subtract box, and every Subtract piece is taken after every Add piece). An Array of 10 steps 1.5 of its own width
   * along z, along the wall. */
  GameObject *col = ed.scene().create("Column", bo);
  BlockoutPiece *cp = col->add<BlockoutPiece>();
  cp->shape = 1, cp->radius = array ? 0.1f : 0.3f, cp->height = 2.8f;
  col->set_local_position(array ? Vec3(-2.15f, 0.0f, -1.4f) : Vec3(-2.3f, 0.0f, 0.0f));
  ed.command("blockout wait");
  for (int i = 0; i < 3; i++) ed.step_frame_headless();
  c.faces_before = bo->get<MeshFilter>()->mesh->face_count();
  /* The first evaluation alone (cold, on this thread: the number a build on a worker pays once), then the whole first landing with the modifier added. */
  {
    Piece numbers = cp->to_piece(col->id, Mat4::identity());
    ScopedTimer t;
    Mesh shape;
    bl::blockout::piece_shape_mesh(numbers, shape);
    std::shared_ptr<Component> m = array ? mod<ArrayModifier>([](ArrayModifier &a) { a.count = 10, a.relative_offset = Vec3(0.0f, 0.0f, 1.5f), a.merge = false; })
                                         : mod<BevelModifier>([](BevelModifier &b) { b.width = 0.03f; });
    std::vector<const Component *> stack = {m.get()};
    bl::blockout_piece_stack_run(stack, shape);
    c.first_eval_ms = t.ms();
  }
  ed.select_object(col->id);
  uint64_t evals = 0;
  c.add_ms = change_to_drawn(ed, [&] {
    ed.command(array ? "component ArrayModifier" : "component BevelModifier");
    if (array) {
      ArrayModifier *a = col->get<ArrayModifier>();
      a->count = 10, a->relative_offset = Vec3(0.0f, 0.0f, 1.5f), a->constant_offset = Vec3(0.0f), a->merge = false;
    } else {
      col->get<BevelModifier>()->width = 0.03f;
    }
  }, evals);
  c.faces_after = bo->get<MeshFilter>()->mesh->face_count();
  ed.command("blockout wait");
  const int reps = o.quick ? 3 : 7;
  for (int i = 0; i < reps; i++) {
    c.change_ms.push_back(change_to_drawn(ed, [&] {
      if (array) col->get<ArrayModifier>()->count = 9 - i;  // (9, 8 ... 3: a new key each time)
      else col->get<BevelModifier>()->width = 0.04f + 0.005f * (float)i;  // (never the 0.03 it was added with: a key already in the cache would cost no evaluation)
    }, evals));
    c.change_evals += evals, c.changes++;
  }
  const int frames = o.quick ? 30 : 120;
  GameObject *door = objs[(size_t)6 * 6 * 2];
  drag(ed, *door, frames, c.other_frame, c.other_landing, c.other_evals, c.other_main);
  drag(ed, *col, frames, c.column_frame, c.column_landing, c.column_evals, c.column_main);
  const Mesh *m = bo->get<MeshFilter>()->mesh.get();
  c.problem = m ? bkstress::problem(*m) : "no mesh";
  return c;
}

static double worst_of(std::vector<double> v) {
  return v.empty() ? 0.0 : *std::max_element(v.begin(), v.end());
}

static void rows(Report &rep, const Options &o) {
  const Cost bevel = run(o, false), array = run(o, true);
  rep.table({"`interior 100` with a column carrying...", "first evaluation alone ms (cold, this thread)", "add the modifier to the drawn mesh ms", "change to drawn mesh median ms", "change to drawn mesh worst ms",
             "stack evaluations per change (1 wanted)", "faces before / after the modifier", "problems in the result", "verdict (change within 100 ms)"});
  const Cost *all[2] = {&bevel, &array};
  const char *names[2] = {"a Bevel (width changes)", "an Array of 10 (count changes)"};
  for (int k = 0; k < 2; k++) {
    const Cost &c = *all[k];
    const double med = bkstress::median(c.change_ms), worst = worst_of(c.change_ms);
    const bool ok = c.change_evals == c.changes && c.problem.empty() && med <= 100.0 && c.faces_after != c.faces_before;  // (the modifier must show in the mesh)
    rep.row({names[k], f2(c.first_eval_ms), f2(c.add_ms), f2(med), f2(worst), strprintf("%llu in %llu", (unsigned long long)c.change_evals, (unsigned long long)c.changes),
             strprintf("%zu / %zu", c.faces_before, c.faces_after), c.problem.empty() ? "0" : "1: " + c.problem, o.quick ? "-" : ok ? "ok" : "OVER"});
  }
  rep.table({"a drag of... (" + std::string(o.quick ? "30" : "120") + " frames, one every 16.7 ms)", "column carries", "frame median ms", "frame worst ms", "move to landing median ms", "landings",
             "stack evaluations during it (0 wanted)", "main-thread builds (0 wanted)", "verdict (worst frame 16.7 ms)"});
  for (int k = 0; k < 2; k++) {
    const Cost &c = *all[k];
    for (int what = 0; what < 2; what++) {
      const std::vector<double> &fr = what == 0 ? c.other_frame : c.column_frame;
      const std::vector<double> &land = what == 0 ? c.other_landing : c.column_landing;
      const uint64_t evals = what == 0 ? c.other_evals : c.column_evals, main = what == 0 ? c.other_main : c.column_main;
      const double worst = worst_of(fr);
      const bool ok = worst <= 16.7 && evals == 0 && main == 0;
      rep.row({what == 0 ? "another piece (a door)" : "the column itself", k == 0 ? "a Bevel" : "an Array of 10", f2(bkstress::median(fr)), f2(worst), land.empty() ? "none" : f2(bkstress::median(land)),
               std::to_string(land.size()), std::to_string(evals), std::to_string(main), o.quick ? (evals == 0 && main == 0 ? "-" : "COUNTER") : ok ? "ok" : "OVER"});
    }
  }
}

/* ---- The untimed sweep: each allowed modifier on each shape that takes one, added and subtracted, in a small building ---- */
struct ShapeCase {
  const char *name;
  std::function<void(Piece &)> set;
};
static std::vector<ShapeCase> shapes() {
  return {{"box", [](Piece &p) { p.shape = Shape::Box, p.size = Vec3(2.0f); }},
          {"cylinder", [](Piece &p) { p.shape = Shape::Cylinder, p.radius = 0.8f, p.height = 2.0f, p.segments = 16; }},
          {"sphere", [](Piece &p) { p.shape = Shape::Sphere, p.radius = 0.9f, p.segments = 16, p.rings = 8; }},
          {"wedge", [](Piece &p) { p.shape = Shape::Wedge, p.size = Vec3(2.0f, 1.5f, 2.0f); }},
          {"stairs", [](Piece &p) { p.shape = Shape::Stairs, p.size = Vec3(2.0f, 2.0f, 3.0f), p.steps = 4; }},
          {"arch", [](Piece &p) { p.shape = Shape::Arch, p.radius = 1.2f, p.inner_radius = 0.6f, p.angle = 180.0f, p.segments = 6, p.height = 0.6f; }},
          {"outline", [](Piece &p) {
             p.shape = Shape::Outline, p.height = 1.5f;
             p.outline = {Vec2(-1, -1), Vec2(1, -1), Vec2(1, 0), Vec2(0, 0), Vec2(0, 1), Vec2(-1, 1)};
           }},
          {"mesh piece", [](Piece &p) { p.shape = Shape::Mesh, p.mesh = primitives::cube(1.8f), p.mesh_key = 77; }}};
}
struct ModCase {
  const char *name;
  std::function<std::shared_ptr<Component>()> make;
};
static std::vector<ModCase> mods() {
  return {{"Bevel", [] { return mod<BevelModifier>([](BevelModifier &m) { m.width = 0.1f, m.limit_method = 0; }); }},
          {"Array", [] { return mod<ArrayModifier>([](ArrayModifier &m) { m.count = 3, m.relative_offset = Vec3(1.3f, 0.0f, 0.0f); }); }},
          {"Mirror", [] { return mod<MirrorModifier>([](MirrorModifier &) {}); }},
          {"Solidify", [] { return mod<SolidifyModifier>([](SolidifyModifier &m) { m.thickness = 0.1f; }); }},
          {"Subdivision 1", [] { return mod<SubdivisionSurface>([](SubdivisionSurface &m) { m.levels = 1; }); }},
          {"Displace", [] { return mod<DisplaceModifier>([](DisplaceModifier &m) { m.strength = 0.1f; }); }},
          {"Triangulate", [] { return mod<TriangulateModifier>([](TriangulateModifier &) {}); }},
          {"Weld", [] { return mod<WeldModifier>([](WeldModifier &m) { m.distance = 0.01f; }); }},
          {"Smooth", [] { return mod<SmoothModifier>([](SmoothModifier &m) { m.factor = 0.5f; m.iterations = 2; }); }},
          {"Cast", [] { return mod<CastModifier>([](CastModifier &m) { m.factor = 0.5f; }); }},
          {"Simple Deform", [] { return mod<SimpleDeformModifier>([](SimpleDeformModifier &m) { m.mode = 0; m.angle = 45.0f; }); }},
          {"Screw", [] { return mod<ScrewModifier>([](ScrewModifier &m) { m.angle = 360.0f; m.steps = 8; }); }},
          {"Wireframe", [] { return mod<WireframeModifier>([](WireframeModifier &m) { m.thickness = 0.05f; }); }},
          {"Decimate", [] { return mod<DecimateModifier>([](DecimateModifier &m) { m.ratio = 0.3f; }); }}};
}

static size_t overlaps_of(const Mesh &m, size_t &unchecked) {
  if (m.face_count() == 0) return 0;
  if (m.face_count() >= 20000) {
    unchecked++;
    return 0;
  }
  const AABB bb = m.bounds();
  std::vector<std::pair<uint32_t, uint32_t>> pairs;
  meshops::overlapping_faces(m, 1e-4f * length(bb.max - bb.min), &pairs);
  return pairs.size();
}

struct Tally {
  size_t cases = 0, built = 0, refused = 0, skipped = 0, problems = 0, newly = 0, unchecked = 0;
  std::string first;
};

/* The small building (2 x 2 rooms, one door) with the stacked piece in room 0's air (added) or through the wall to room 1 (subtracted). */
static void sweep_case(size_t s, size_t k, const ShapeCase &shape, const ModCase &mod_case, bool subtract, bool field, size_t baseline, Tally &t) {
  std::vector<Piece> pieces = bkstress::interior(2, 1);
  Piece p;
  p.id = 1000, p.op = subtract ? Op::Subtract : Op::Add;
  shape.set(p);
  p.to_blockout = Mat4::trs(Vec3(subtract ? 2.0f : 0.0f, subtract ? 0.0f : -0.4f, 0.0f), Quat(), Vec3(1.0f));
  pieces.push_back(p);
  BlockoutSurface surf;
  BlockoutSurface::Stack st;
  st.index = pieces.size() - 1, st.key = 0x514e8000ULL + s * 100 + k;
  st.modifiers.push_back(mod_case.make());
  surf.stacks.push_back(st);
  bl::blockout_piece_stacks_apply(pieces, surf);
  bl::blockout::Settings set;
  set.path = field ? bl::blockout::Path::Field : bl::blockout::Path::Auto;
  set.cell = field ? 0.2f : 0.1f;
  Mesh out;
  bl::blockout::Report rep;
  t.cases++;
  const bool ok = bl::blockout::build(pieces, set, out, nullptr, &rep);
  t.skipped += !rep.skipped.empty();
  std::string bad;
  if (!ok) {
    t.refused++;
    if (rep.why.empty()) bad = "refused without a reason";
  } else {
    t.built++;
    bad = bkstress::problem(out);
    const size_t over = overlaps_of(out, t.unchecked);
    t.newly += over > baseline ? over - baseline : 0;
  }
  if (bad.empty()) return;
  t.problems++;
  if (t.first.empty()) t.first = strprintf("%s + %s, %s, %s path: %s", shape.name, mod_case.name, subtract ? "subtracted" : "added", field ? "field" : "auto", bad.c_str());
  if (t.problems <= 8) std::printf("  PROBLEM: %s + %s, %s, %s path: %s\n", shape.name, mod_case.name, subtract ? "subtracted" : "added", field ? "field" : "auto", bad.c_str());
}

static void sweep(Report &rep, const Options &o) {
  const std::vector<ShapeCase> sh = shapes();
  const std::vector<ModCase> md = mods();
  size_t baseline[2] = {0, 0};
  for (int field = 0; field < 2; field++) {  // (the building alone: its own faces on top of each other, which the piece is not to add to)
    bl::blockout::Settings set;
    set.path = field ? bl::blockout::Path::Field : bl::blockout::Path::Auto;
    set.cell = field ? 0.2f : 0.1f;
    Mesh m;
    size_t unchecked = 0;
    if (bl::blockout::build(bkstress::interior(2, 1), set, m)) baseline[field] = overlaps_of(m, unchecked);
  }
  rep.table({"sweep: a modifier on every shape, added and subtracted, in a small building (untimed)", "cases", "built", "refused (with a reason)", "a piece skipped", "problems (0 wanted)",
             "faces newly on top of each other (the building's own " + std::to_string(baseline[0]) + " on the exact path, " + std::to_string(baseline[1]) + " on the grid, not counted)",
             "too large to check", "first problem"});
  Tally total;
  for (size_t k = 0; k < md.size(); k++) {
    if (o.quick && k % 3 != 0) continue;
    Tally t;
    for (size_t s = 0; s < sh.size(); s++)
      for (int sub = 0; sub < 2; sub++)
        for (int field = 0; field < 2; field++) sweep_case(s, k, sh[s], md[k], sub != 0, field != 0, baseline[field], t);
    rep.row({md[k].name, std::to_string(t.cases), std::to_string(t.built), std::to_string(t.refused), std::to_string(t.skipped), std::to_string(t.problems), std::to_string(t.newly),
             std::to_string(t.unchecked), t.first.empty() ? "-" : t.first});
    total.cases += t.cases, total.built += t.built, total.refused += t.refused, total.skipped += t.skipped, total.problems += t.problems, total.newly += t.newly, total.unchecked += t.unchecked;
  }
  rep.row({"all modifiers", std::to_string(total.cases), std::to_string(total.built), std::to_string(total.refused), std::to_string(total.skipped), std::to_string(total.problems),
           std::to_string(total.newly), std::to_string(total.unchecked), total.problems == 0 ? "ok" : "PROBLEMS"});
}

/* ---- Extremes: a stack that makes a very large mesh, built alone: valid or refused with a reason, and what it cost (untimed as a verdict) ---- */
static void extremes(Report &rep, const Options &o) {
  rep.table({"extremes: one piece alone (untimed)", "stack evaluation ms", "faces of the piece's mesh", "build ms", "built / refused", "problems in the result", "verdict"});
  struct Ex {
    const char *name;
    Shape shape;
    std::function<std::shared_ptr<Component>()> make;
  };
  const int count = o.quick ? 300 : 1500;
  const std::vector<Ex> ex = {
      {"Array of many small boxes", Shape::Box, [=] { return mod<ArrayModifier>([=](ArrayModifier &m) { m.count = count, m.relative_offset = Vec3(1.5f, 0.0f, 0.0f), m.merge = false; }); }},
      {"Subdivision 5 of a box", Shape::Box, [] { return mod<SubdivisionSurface>([](SubdivisionSurface &m) { m.levels = 5; }); }},
      {"Subdivision 4 of a sphere", Shape::Sphere, [] { return mod<SubdivisionSurface>([](SubdivisionSurface &m) { m.levels = 4; }); }},
      {"Bevel 8 segments of a cylinder", Shape::Cylinder, [] { return mod<BevelModifier>([](BevelModifier &m) { m.width = 0.1f, m.segments = 8, m.limit_method = 0; }); }}};
  for (const Ex &e : ex) {
    Piece p;
    p.id = 1, p.shape = e.shape, p.size = Vec3(0.2f), p.radius = 0.5f, p.height = 1.0f, p.to_blockout = Mat4::identity();
    BlockoutSurface surf;
    BlockoutSurface::Stack st;
    st.index = 0, st.key = 0x514e9000ULL + (uint64_t)(&e - ex.data());
    st.modifiers.push_back(e.make());
    surf.stacks.push_back(st);
    std::vector<Piece> pieces = {p};
    bl::blockout_piece_stack_cache_clear();
    ScopedTimer t;
    bl::blockout_piece_stacks_apply(pieces, surf);
    const double eval_ms = t.ms();
    const size_t faces = pieces[0].mesh ? pieces[0].mesh->face_count() : 0;
    Mesh out;
    bl::blockout::Report rep2;
    ScopedTimer tb;
    const bool ok = bl::blockout::build(pieces, bl::blockout::Settings(), out, nullptr, &rep2);
    const double build_ms = tb.ms();
    const std::string bad = ok ? bkstress::problem(out) : (rep2.why.empty() ? "refused without a reason" : "");
    rep.row({e.name, f2(eval_ms), std::to_string(faces), f2(build_ms), ok ? "built" : "refused: " + rep2.why, bad.empty() ? "0" : "1: " + bad, bad.empty() ? "ok" : "PROBLEM"});
  }
}

}  // namespace bkpm

static void test_blockout_piece_modifiers(Report &rep, const Options &o) {
  rep.title("Modifiers on a Blockout piece (round 514, ADR 0138)",
            "A piece with a modifier stack becomes a mesh piece: its shape, then the stack, evaluated on the build's worker and kept by key (numbers and stack, never the place). "
            "Rows: `interior 100` with a column carrying a Bevel or an Array of 10 - the change of the column to the drawn mesh (budget 100 ms), a drag of another piece and of the column "
            "itself (budget 16.7 ms per frame, no stack evaluation during either), and an untimed sweep of every allowed modifier on every shape, added and subtracted.");
  bkpm::rows(rep, o);
  bkpm::sweep(rep, o);
  bkpm::extremes(rep, o);
}
