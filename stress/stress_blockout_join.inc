// SPDX-License-Identifier: GPL-2.0-or-later
// Stress: snapping between Blockouts and joining Blockouts that touch (round 501, task 0485). Included by stress_main.cpp;
// `--only blockout_join` (`--quick` for a short pass). Three parts:
//  1. a live row - the snap's cost in a frame with 10 Blockouts of 100 pieces in the scene: a Blockout dragged with the Move
//     gizmo (real events through `perf drag gizmo`) with the snap on and with Ctrl held (no snap), the first frame's cost (the
//     targets are built once) and the snap call alone, warm; verdict against 16.7 ms (no verdict with --quick);
//  2. the Inspector's touch check with those 10 Blockouts: cold (the first ask after an edit - what a drag pays every frame) and
//     warm, a miss and a hit, and the frame with the Inspector showing it;
//  3. an untimed sweep of joins: two interiors at many offsets and turns (touching, overlapping, apart): the joined result is
//     sound, every piece's world box is kept, no face lies newly on top of another, the touching verdict is the one an independent
//     separating-axis test gives and the log says "did not touch" exactly where they did not; counts in a table.
// Times here are rows of a table, not measurements to assert on (ADR 0056): a timed window is arranged separately.
#include "../src/core/edit_epoch.h"

namespace bjstress {

using bl::blockout::Op;
using bl::blockout::Piece;

/* An interior of bkstress::interior(n, doors) as a Blockout object at `at`, turned `turn` degrees about Y; its piece objects appended to `pieces`. */
static GameObject *make_blockout(Editor &ed, const std::string &name, Vec3 at, float turn, int n, int doors, std::vector<GameObject *> *pieces = nullptr) {
  GameObject *bo = ed.scene().create(name);
  bo->add<Blockout>();
  bo->add<MeshFilter>()->mesh = std::make_shared<Mesh>();
  bo->add<MeshRenderer>();
  bo->set_world_position(at);
  if (turn != 0.0f) bo->set_world_rotation(Quat::axis_angle(Vec3(0, 1, 0), turn * kDeg2Rad));
  for (const Piece &p : bkstress::interior(n, doors)) {
    GameObject *g = ed.scene().create("Piece", bo);
    BlockoutPiece *bp = g->add<BlockoutPiece>();
    bp->operation = p.op == Op::Subtract ? 1 : 0;
    bp->size = p.size;
    g->set_local_position(p.to_blockout.translation());
    if (pieces) pieces->push_back(g);
  }
  return bo;
}

static void empty_scene(Editor &ed) {
  std::vector<GameObject *> roots;
  ed.scene().for_each([&](GameObject &g) {
    if (!g.parent) roots.push_back(&g);
  });
  ed.scene().destroy(roots);
  ed.clear_selection_for_test();
}

static size_t log_mark() {
  std::vector<LogEntry> tmp;
  return Log::fetch((size_t)1 << 40, tmp);
}
static std::string log_since(size_t mark) {
  std::vector<LogEntry> v;
  Log::fetch(mark, v);
  std::string s;
  for (const LogEntry &e : v) s += e.text + "\n";
  return s;
}

static size_t count_blockouts(Editor &ed) {
  size_t n = 0;
  ed.scene().for_each([&](GameObject &g) { n += g.get<Blockout>() != nullptr; });
  return n;
}

/* The eight world corners of a box piece. */
static std::vector<Vec3> corners(const GameObject &p) {
  const Vec3 h = p.get<BlockoutPiece>()->size * 0.5f;
  const Mat4 w = p.world_matrix();
  std::vector<Vec3> out;
  for (int k = 0; k < 8; k++) out.push_back(w.point(Vec3((k & 1) ? h.x : -h.x, (k & 2) ? h.y : -h.y, (k & 4) ? h.z : -h.z)));
  return out;
}

/* Whether two oriented boxes (a piece each) touch or overlap within `tol`: the textbook separating-axis test, written here without the editor's code. */
static bool obb_touch(const GameObject &a, const GameObject &b, float tol) {
  const Mat4 wa = a.world_matrix(), wb = b.world_matrix();
  const Vec3 ha = a.get<BlockoutPiece>()->size * 0.5f, hb = b.get<BlockoutPiece>()->size * 0.5f;
  Vec3 ax[3], bx[3];
  for (int k = 0; k < 3; k++) ax[k] = normalize(wa.dir(Vec3(k == 0, k == 1, k == 2))), bx[k] = normalize(wb.dir(Vec3(k == 0, k == 1, k == 2)));
  const Vec3 d = wb.point(Vec3(0.0f)) - wa.point(Vec3(0.0f));
  auto radius = [](const Vec3 *axes, const Vec3 &h, const Vec3 &dir) { return std::fabs(dot(axes[0], dir)) * h.x + std::fabs(dot(axes[1], dir)) * h.y + std::fabs(dot(axes[2], dir)) * h.z; };
  auto separated = [&](Vec3 dir) {
    const float len = length(dir);
    if (len < 1e-5f) return false;
    dir = dir * (1.0f / len);
    return std::fabs(dot(d, dir)) > radius(ax, ha, dir) + radius(bx, hb, dir) + tol;
  };
  for (int k = 0; k < 3; k++)
    if (separated(ax[k]) || separated(bx[k])) return false;
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++)
      if (separated(cross(ax[i], bx[j]))) return false;
  return true;
}
static bool shells_touch(const std::vector<GameObject *> &a, const std::vector<GameObject *> &b) {
  for (const GameObject *p : a)
    for (const GameObject *q : b)
      if (p->get<BlockoutPiece>()->operation == 0 && q->get<BlockoutPiece>()->operation == 0 && obb_touch(*p, *q, 1e-3f)) return true;
  return false;
}

/* ---------------------------------------------------------------------- */
/* 1 and 2: the live rows                                                  */
/* ---------------------------------------------------------------------- */

struct DragRun {
  double press = 0.0, first = 0.0, mean = 0.0, longest = 0.0;  // (the frames of `perf drag`'s own record: the command plays the whole drag)
  float moved = 0.0f;  // how far the Blockout ended from where it began, along X
  bool ok = false;
};

/* The Move gizmo's X arrow dragged from the Blockout's origin to a world point, as `perf drag gizmo` plays it (real mouse events: press on the handle, `frames` moves, release). */
static DragRun drag(Editor &ed, GameObject &bo, float to_x, int frames, bool ctrl) {
  DragRun r;
  const Vec3 home = bo.world_position();
  ed.select_for_test(bo.id, false);
  for (int i = 0; i < 4; i++) ed.step_frame_headless();
  int ax = 0, ay = 0, bx = 0, by = 0, cx = 0, cy = 0;
  if (!ed.project_to_window(home, ax, ay) || !ed.project_to_window(home + Vec3(to_x, 0, 0), bx, by) || !ed.project_to_window(home + Vec3(1, 0, 0), cx, cy)) return r;
  const float ux = (float)(cx - ax), uy = (float)(cy - ay), len = std::max(1e-3f, std::sqrt(ux * ux + uy * uy));
  const int pixels = (int)std::lround(((float)(bx - ax) * ux + (float)(by - ay) * uy) / len);
  ed.command("tool move");
  ed.command("perf drag gizmo x " + std::to_string(pixels) + " frames " + std::to_string(frames) + (ctrl ? " mods ctrl" : ""));
  for (int guard = 0; ed.perf_script_active_for_test() && guard < frames * 20 + 400; guard++) ed.step_frame_headless();
  for (int i = 0; i < 5; i++) ed.step_frame_headless();
  const PerfDragResult &pr = ed.perf_drag_result_for_test();
  r.ok = pr.ok;
  r.press = pr.ms_press, r.first = pr.ms_first, r.mean = pr.ms_mean, r.longest = pr.ms_longest;
  r.moved = bo.world_position().x - home.x;
  bo.set_world_position(home);  // (back for the next run)
  ed.mark_changed_for_test("back");
  for (int i = 0; i < 3; i++) ed.step_frame_headless();
  return r;
}

static void live_rows(Report &rep, const Options &o) {
  Editor ed;
  ed.init_headless(1280, 720);
  ed.set_async_jobs_for_test(true);
  empty_scene(ed);
  std::vector<GameObject *> bos;
  const double build_start = now_seconds();
  for (int k = 0; k < 10; k++) bos.push_back(make_blockout(ed, "Blockout " + std::to_string(k), Vec3(30.0f * (float)k, 0, 0), 0.0f, 6, 28));
  ed.mark_changed_for_test("ten Blockouts");
  ed.command("blockout wait");
  for (int i = 0; i < 4; i++) ed.step_frame_headless();
  std::printf("  ten Blockouts of 100 pieces built in %.1f s\n", now_seconds() - build_start);
  ed.command("camera 20 55 70 15 0 12");
  for (int i = 0; i < 4; i++) ed.step_frame_headless();
  GameObject &lead = *bos[0];
  const int frames = o.quick ? 30 : 120;
  /* Blockout 0 spans x -2.1..21.9, Blockout 1 starts at 27.9: its shell meets Blockout 1's after 6.0 m. Drag 6.05 m: five centimetres short. */
  const float gap = 6.0f;

  /* The snap call alone: the first (the targets are built: 900 pieces' boxes) and then warm. */
  const Vec3 home = lead.world_position();
  std::vector<double> warm;
  float d = gap + 0.05f;
  ScopedTimer cold_timer;
  const bool snapped = ed.blockout_whole_snap(lead, home, Vec3(1, 0, 0), {lead.id}, d);
  const double cold_ms = cold_timer.ms();
  const float landed = d;
  for (int i = 0; i < (o.quick ? 50 : 400); i++) {
    d = gap + 0.05f;
    ScopedTimer t;
    (void)ed.blockout_whole_snap(lead, home, Vec3(1, 0, 0), {lead.id}, d);
    warm.push_back(t.ms());
  }

  /* Where a drag may stop with the snap on: some x-plane of a piece of Blockout 0 (moved) in the plane of one of Blockout 1's. With Ctrl it is a multiple of the grid's step. */
  std::vector<float> planes0, planes1;
  for (const Piece &p : bkstress::interior(6, 28)) {
    const Vec3 c = p.to_blockout.translation();
    for (const float s : {-0.5f, 0.5f}) planes0.push_back(c.x + s * p.size.x), planes1.push_back(30.0f + c.x + s * p.size.x);
  }
  auto in_a_plane = [&](float moved) {
    for (float a : planes0)
      for (float b : planes1)
        if (std::fabs((b - a) - moved) < 1e-4f) return true;
    return false;
  };
  rep.table({"live: 10 Blockouts of 100 pieces, one dragged " + std::to_string(frames) + " frames with the Move gizmo's X arrow (real mouse events; `perf drag`'s frames, which draw the Scene view)",
             "press frame ms", "first move frame ms", "mean drag frame ms", "longest drag frame ms", "dragged to (m)", "where it stopped", "verdict"});
  (void)drag(ed, lead, gap + 0.05f, frames, true);  // (a first drag warms the draw paths: the rows below are the second and third)
  bool landed_right = true;
  DragRun pairs[2][2];
  for (int round = 0; round < 2; round++)
    for (int ctrl = 0; ctrl < 2; ctrl++) {
      const DragRun r = drag(ed, lead, gap + 0.05f, frames, ctrl != 0);
      pairs[round][ctrl] = r;
      const bool grid = std::fabs(r.moved / 0.25f - std::round(r.moved / 0.25f)) < 1e-3f;
      const bool plane = in_a_plane(r.moved);
      const bool right = r.ok && (ctrl ? grid : plane);
      landed_right = landed_right && right;
      rep.row({ctrl ? "Ctrl held (no snap: the grid)" : "snap on", f2(r.press), f2(r.first), f2(r.mean), f2(r.longest), strprintf("%.4f", r.moved),
               ctrl ? (grid ? "on the grid" : "NOT on the grid") : plane ? "a corner of Blockout 0 in a plane of Blockout 1" : "NOT in a plane of Blockout 1", right ? "ok" : "PROBLEM"});
    }
  rep.table({"the snap call alone (blockout_whole_snap, 900 other pieces)", "first call ms (targets built)", "warm median ms", "warm worst ms", "snapped on the first call", "verdict"});
  std::sort(warm.begin(), warm.end());
  const bool cold_ok = cold_ms <= 16.7 && warm.back() <= 16.7;
  rep.row({"X axis, " + std::string(snapped ? "landed " : "did not snap, ") + strprintf("%.4f m", landed), f2(cold_ms), strprintf("%.4f", bkstress::median(warm)), strprintf("%.4f", warm.back()), snapped ? "yes" : "no",
           o.quick ? "-" : (cold_ok && snapped && landed_right) ? "ok" : "OVER"});
  rep.note(strprintf("The drag frames are the main thread's whole step_frame_headless: the Scene view draws ten Blockouts (CPU raster, 1280 x 720) and the Inspector shows the dragged one, so their mean (%.1f ms with the "
                     "snap, %.1f with Ctrl in the second round) is the renderer's, against the budget of 16.7 ms; the snap's own part is the second table. Not paced at 16.7 ms.",
                     pairs[1][0].mean, pairs[1][1].mean));

  /* The Inspector's touch check. A miss: Blockout 0 touches nothing (6 m of air to Blockout 1). A hit: Blockout 1 moved flush against it. */
  rep.table({"the Inspector's touch check, 10 Blockouts of 100 pieces", "cold ms (first ask after an edit: a drag pays it every frame)", "warm median ms", "frames with the row showing: median ms",
             "frames: worst ms", "answer", "verdict"});
  for (const bool hit : {false, true}) {
    bos[1]->set_world_position(Vec3(hit ? 24.0f : 30.0f, 0, 0));
    ed.mark_changed_for_test("move");
    ed.select_for_test(bos[0]->id, false);
    ed.record_buttons_for_test(true);
    for (int i = 0; i < 4; i++) ed.step_frame_headless();
    std::vector<GameObject *> v;
    double cold = 0.0;
    {
      edit_epoch::bump();
      ScopedTimer t;
      ed.blockout_touching(*bos[0], v);
      cold = t.ms();
    }
    const size_t found = v.size();
    std::vector<double> warm_ms, frame_ms;
    for (int i = 0; i < (o.quick ? 50 : 400); i++) {
      std::vector<GameObject *> w;
      ScopedTimer t;
      ed.blockout_touching(*bos[0], w);
      warm_ms.push_back(t.ms());
    }
    for (int i = 0; i < (o.quick ? 20 : 60); i++) {
      ScopedTimer t;
      ed.step_frame_headless();
      frame_ms.push_back(t.ms());
    }
    std::sort(frame_ms.begin(), frame_ms.end());
    Recti r;
    const bool shown = ed.button_rect_for_test("Join Blockouts", r);
    const bool right = shown == hit && found == (hit ? 1u : 0u);
    rep.row({hit ? "a hit (Blockout 1 flush against Blockout 0)" : "a miss (nothing touches Blockout 0)", f2(cold), strprintf("%.4f", bkstress::median(warm_ms)), f2(bkstress::median(frame_ms)),
             f2(frame_ms.back()), strprintf("%zu touching, button %s", found, shown ? "shown" : "not shown"), o.quick ? "-" : (right && cold <= 16.7 && frame_ms.back() <= 16.7) ? "ok" : "OVER"});
  }
  ed.record_buttons_for_test(false);
}

/* ---------------------------------------------------------------------- */
/* 3: the sweep                                                            */
/* ---------------------------------------------------------------------- */

struct Tally {
  size_t cases = 0, joined = 0, problems = 0, moved = 0, overlaps = 0, touch_wrong = 0, said_wrong = 0, scaled = 0;
  void add(const Tally &t) {
    cases += t.cases, joined += t.joined, problems += t.problems, moved += t.moved, overlaps += t.overlaps, touch_wrong += t.touch_wrong, said_wrong += t.said_wrong, scaled += t.scaled;
  }
};

static size_t overlapping(const Mesh &m) {
  if (m.face_count() == 0 || m.face_count() >= 20000) return 0;
  std::vector<std::pair<uint32_t, uint32_t>> pairs;
  const AABB box = m.bounds();
  return meshops::overlapping_faces(m, 1e-4f * length(box.max - box.min), &pairs);
}

static void sweep(Report &rep, const Options &o) {
  Editor ed;
  ed.init_headless(900, 600);
  const float dxs[] = {6.0f, 7.0f, 8.0f, 8.15f, 8.2f, 8.25f, 8.3f, 9.0f, 12.0f};
  const float dzs[] = {0.0f, 3.5f, 8.0f, 9.0f};
  const float dys[] = {0.0f, 0.6f};
  const float turns[] = {0.0f, 30.0f, 90.0f};
  Tally touching, apart;
  size_t index = 0;
  for (float dx : dxs)
    for (float dz : dzs)
      for (float dy : dys)
        for (float turn : turns) {
          if (o.quick && (index++ % 6) != 0) continue;
          empty_scene(ed);
          std::vector<GameObject *> pa, pb;
          GameObject *a = make_blockout(ed, "Alpha", Vec3(0.0f), 0.0f, 2, 1, &pa);
          GameObject *b = make_blockout(ed, "Beta", Vec3(dx, dy, dz), turn, 2, 1, &pb);
          ed.mark_changed_for_test("sweep");
          ed.command("blockout wait");
          for (int i = 0; i < 2; i++) ed.step_frame_headless();
          std::vector<GameObject *> all = pa;
          all.insert(all.end(), pb.begin(), pb.end());
          std::vector<std::vector<Vec3>> before;
          for (const GameObject *p : all) before.push_back(corners(*p));
          const Mesh *ma = a->get<MeshFilter>()->mesh.get(), *mb = b->get<MeshFilter>()->mesh.get();
          const size_t overlap_before = (ma ? overlapping(*ma) : 0) + (mb ? overlapping(*mb) : 0);
          const bool expected = shells_touch(pa, pb), says = ed.blockout_touches(*a, *b);
          Tally t;
          t.cases = 1;
          t.touch_wrong = expected != says;
          ed.select_for_test(a->id, false);
          const size_t mark = log_mark();
          ed.command("blockout join Beta");
          ed.command("blockout wait");
          for (int i = 0; i < 2; i++) ed.step_frame_headless();
          const std::string said = log_since(mark);
          const bool did_not_touch = said.find("did not touch") != std::string::npos;
          t.said_wrong = did_not_touch == says;  // (it must say so exactly when they do not touch)
          t.joined = count_blockouts(ed) == 1 && ed.scene().find_by_name("Alpha") && all.size() == 18;
          float worst = 0.0f;
          bool scaled = false;
          for (size_t i = 0; i < all.size(); i++) {
            const std::vector<Vec3> now = corners(*all[i]);
            for (size_t k = 0; k < now.size(); k++) worst = std::max(worst, length(now[k] - before[i][k]));
            const Vec3 s = all[i]->local().scale;
            scaled = scaled || s.x != 1.0f || s.y != 1.0f || s.z != 1.0f;
          }
          t.moved = worst > 1e-4f;
          t.scaled = scaled;
          const Mesh *m = ed.scene().find_by_name("Alpha") ? ed.scene().find_by_name("Alpha")->get<MeshFilter>()->mesh.get() : nullptr;
          std::string problem = m ? bkstress::problem(*m) : "no mesh";
          t.problems = !problem.empty();
          t.overlaps = m && overlapping(*m) > overlap_before;
          if (!problem.empty() || t.moved || t.touch_wrong || t.said_wrong || !t.joined)
            std::printf("  case dx %.2f dy %.1f dz %.1f turn %.0f: touches %d (expected %d), problem '%s', boxes moved %.2e, joined %d, said-so %s\n", dx, dy, dz, turn, (int)says, (int)expected,
                        problem.c_str(), worst, (int)t.joined, t.said_wrong ? "WRONG" : "ok");
          (expected ? touching : apart).add(t);
        }
  rep.table({"join of two interiors (Alpha at the origin, Beta at dx dy dz, turned 0 / 30 / 90 degrees), untimed", "cases", "joined (one Blockout, all 18 pieces)", "problems in the result (open, pinched, inside out, NaN)",
             "pieces whose world box moved > 1e-4", "pieces with a Scale", "faces newly on top of each other", "touching verdict differs from an independent test", "log says 'did not touch' wrongly", "verdict"});
  Tally all;
  all.add(touching), all.add(apart);
  for (const auto &row : {std::make_pair("touching or overlapping", touching), std::make_pair("apart (the join still works and says so)", apart), std::make_pair("all", all)}) {
    const Tally &t = row.second;
    const bool ok = t.joined == t.cases && t.problems == 0 && t.moved == 0 && t.scaled == 0 && t.overlaps == 0 && t.touch_wrong == 0 && t.said_wrong == 0;
    rep.row({row.first, std::to_string(t.cases), std::to_string(t.joined), std::to_string(t.problems), std::to_string(t.moved), std::to_string(t.scaled), std::to_string(t.overlaps),
             std::to_string(t.touch_wrong), std::to_string(t.said_wrong), ok ? "ok" : "PROBLEM"});
  }
}

}  // namespace bjstress

static void test_blockout_join(Report &rep, const Options &o) {
  rep.title("Snapping and joining Blockouts (round 501, ADR 0129's amendment)",
            "A moved Blockout, piece or face handle snaps to the other Blockouts' faces and corners; Join Blockouts moves touching Blockouts' pieces under one. Rows: the snap's cost in a frame with 10 "
            "Blockouts of 100 pieces and the Inspector's touch check (live rows, verdict against 16.7 ms); then an untimed sweep of joins of two interiors at many offsets and turns.");
  bjstress::live_rows(rep, o);
  bjstress::sweep(rep, o);
}
