// SPDX-License-Identifier: GPL-2.0-or-later
// Stress: modifiers live on a Blockout (round 502, task 0486). Included by stress_main.cpp; `--only blockout_modifiers`.
// The same door drag as stress_blockout.inc's live row (`interior 100`: an Add and a Subtract box for each of 36 rooms and 28 door
// cuts, workers on, a door moved a little every frame at 16.7 ms), with a Bevel, a Mirror or a Subdivision Surface (1 level) on the
// Blockout. The frame is the main thread's whole step_frame_headless; the move-to-landing time is the pieces' build (as the live
// row), the move-to-evaluated-mesh time is until the stack's result over that build is what the Blockout shows. The stack must
// never run on the main thread during the drag (the counter of GameObject::evaluated_mesh calls past the key hit, this thread's).
#include "../src/scene/pose_counts.h"

namespace bkmod {

using bl::blockout::Op;
using bl::blockout::Piece;

struct Drag {
  std::vector<double> frame_ms, landing_ms, evaluated_ms;
  uint64_t main_evaluations = 0, bare_frames = 0, adopted = 0, landings = 0;
  double settle_ms = 0.0;
  size_t base_faces = 0, shown_faces = 0;
  std::string problem;
  bool same_by_hand = false;
};

/* The stack run by hand over the Blockout's own mesh. */
static Mesh by_hand(const GameObject &g) {
  Mesh m = *g.get<MeshFilter>()->mesh;
  for (const auto &c : g.components)
    if (c->is_modifier() && c->enabled) run_modifier(*c, m);
  return m;
}

static bool same_mesh(const Mesh &a, const Mesh &b) {
  return a.positions.size() == b.positions.size() && a.face_offsets == b.face_offsets && a.corner_verts == b.corner_verts &&
         (a.positions.empty() || std::memcmp(a.positions.data(), b.positions.data(), a.positions.size() * sizeof(Vec3)) == 0);
}

/* Waits (frames of the editor) until the build landed and the stack's result over it is adopted. */
static void settle(Editor &ed, GameObject &bo, double limit_s = 120.0) {
  ed.command("blockout wait");
  const double start = now_seconds();
  for (;;) {
    ed.step_frame_headless();
    (void)bo.evaluated_mesh();
    if (!ed.eval_jobs_running_for_test() && !ed.blockout_jobs_running_for_test() && !bo.evaluation_pending(0)) break;
    if (now_seconds() - start > limit_s) break;
  }
  ed.step_frame_headless();
}

static Drag run(const Options &o, const char *type) {
  Drag d;
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
  ed.command("blockout wait");
  for (int i = 0; i < 3; i++) ed.step_frame_headless();
  ed.select_object(bo->id);
  ed.command(std::string("component ") + type);
  settle(ed, *bo);  // (the first result - a modifier just added - is the rule's old one; every later one is the worker's)
  GameObject *door = objs[(size_t)6 * 6 * 2];
  const Vec3 home = door->local().position;
  const uint64_t landed0 = ed.blockout_landed_for_test();
  const PoseCounts counts0 = pose_counts_this_thread();
  const int frames = o.quick ? 30 : 120;
  uint64_t landed = landed0;
  const Mesh *shown = bo->evaluated_mesh();
  double oldest_build = -1.0, oldest_result = -1.0;
  const double start = now_seconds();
  for (int f = 0; f < frames; f++) {
    const double moved_at = now_seconds();
    if (oldest_build < 0.0) oldest_build = moved_at;
    if (oldest_result < 0.0) oldest_result = moved_at;
    door->set_local_position(home + Vec3(0.0f, 0.0f, 0.02f * (float)(f % 30)));
    ScopedTimer t;
    ed.step_frame_headless();
    d.frame_ms.push_back(t.ms());
    if (ed.blockout_landed_for_test() != landed) {
      landed = ed.blockout_landed_for_test();
      d.landing_ms.push_back((now_seconds() - oldest_build) * 1000.0);
      oldest_build = -1.0;
    }
    const Mesh *ev = bo->evaluated_mesh();
    d.bare_frames += ev == nullptr || ev == bo->get<MeshFilter>()->mesh.get();
    if (ev != shown) {  // (a result of the stack was adopted: it covers every move before the build it ran over; the oldest move since the last one is the wait)
      shown = ev;
      d.adopted++;
      d.evaluated_ms.push_back((now_seconds() - oldest_result) * 1000.0);
      oldest_result = -1.0;
    }
    const double due = start + 0.0167 * (double)(f + 1) - now_seconds();
    if (due > 0.0) std::this_thread::sleep_for(std::chrono::duration<double>(due));
  }
  d.main_evaluations = (pose_counts_this_thread() - counts0).evaluations;
  d.landings = landed - landed0;
  const double drag_end = now_seconds();
  settle(ed, *bo);
  d.settle_ms = (now_seconds() - drag_end) * 1000.0;
  const Mesh *ev = bo->evaluated_mesh();
  d.base_faces = bo->get<MeshFilter>()->mesh->face_count();
  d.shown_faces = ev ? ev->face_count() : 0;
  d.same_by_hand = ev && same_mesh(*ev, by_hand(*bo));
  d.problem = ev ? ppstress::invalid(*ev) : "no evaluated mesh";
  return d;
}

static void rows(Report &rep, const Options &o) {
  rep.table({"stack on the Blockout, `interior 100`, a door moved every frame for " + std::string(o.quick ? "30" : "120") + " frames (one every 16.7 ms)", "frame median ms", "frame worst ms",
             "move to landing median ms", "move to evaluated mesh median ms", "move to evaluated mesh worst ms", "results adopted during the drag", "wait after the last move ms",
             "main-thread evaluations during the drag (must be 0)", "frames of the bare mesh (must be 0)", "faces: base / shown", "verdict"});
  const char *const types[3] = {"BevelModifier", "MirrorModifier", "SubdivisionSurface"};
  const char *const names[3] = {"Bevel (defaults)", "Mirror (X)", "Subdivision Surface (1 level)"};
  std::vector<Drag> all;
  for (int k = 0; k < 3; k++) {
    const Drag d = run(o, types[k]);
    all.push_back(d);
    std::vector<double> frames = d.frame_ms;
    std::sort(frames.begin(), frames.end());
    const double worst = frames.empty() ? 0.0 : frames.back();
    const double ev_median = bkstress::median(d.evaluated_ms), ev_worst = d.evaluated_ms.empty() ? 0.0 : *std::max_element(d.evaluated_ms.begin(), d.evaluated_ms.end());
    /* The goal's verdict, as the live row's (a shared runner's frames say nothing: no verdict with --quick) - plus the counter. */
    const bool ok = worst <= 16.7 && !d.evaluated_ms.empty() && ev_median <= 200.0 && d.main_evaluations == 0 && d.bare_frames == 0;
    rep.row({names[k], f2(bkstress::median(d.frame_ms)), f2(worst), d.landing_ms.empty() ? "none" : f2(bkstress::median(d.landing_ms)), d.evaluated_ms.empty() ? "none" : f2(ev_median),
             d.evaluated_ms.empty() ? "none" : f2(ev_worst), std::to_string(d.adopted), f2(d.settle_ms), std::to_string(d.main_evaluations), std::to_string(d.bare_frames),
             strprintf("%zu / %zu", d.base_faces, d.shown_faces), o.quick ? "-" : ok ? "ok" : "OVER"});
  }
  /* Untimed: after the drag the Blockout shows exactly the stack run by hand over its pieces' mesh, and the result is a sound mesh. */
  rep.table({"soundness after the drag (untimed)", "evaluated mesh == the stack by hand", "problems in the result", "verdict"});
  for (int k = 0; k < 3; k++) {
    const Drag &d = all[(size_t)k];
    rep.row({names[k], d.same_by_hand ? "same" : "DIFFERS", d.problem.empty() ? "0" : "1: " + d.problem, d.same_by_hand && d.problem.empty() ? "ok" : "PROBLEM"});
  }
}

}  // namespace bkmod

static void test_blockout_modifiers(Report &rep, const Options &o) {
  rep.title("Modifiers on a Blockout (round 502, ADR 0129's amendment)", "A stack on a Blockout object is evaluated over the mesh its pieces build, on the evaluation worker after each landing.");
  bkmod::rows(rep, o);
}
