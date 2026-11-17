// SPDX-License-Identifier: GPL-2.0-or-later
// Stress: modifiers on a door or window (round 546, task 0526, ADR 0150). Included by stress_main.cpp after round 514's section (it
// uses its helpers); `--only blockout_opening_modifiers`. A wall 40 m long with a row of ten windows made two ways - one window and an
// Array of 10, and ten windows placed by hand - plain and Grid style: the time from the change until the mesh can be drawn (budget
// 100 ms) and the faces each way (the same wanted: a copy is the window as its numbers make it). A 120-frame drag of the arrayed
// window with a Bevel on its hole (budget 16.7 ms a frame, no stack evaluated while it moves). Then an untimed sweep of every modifier
// a piece takes on a plain window, a Grid window and a Single door in the wall, alone and under an Array of 3: problems must be 0,
// faces newly on top of each other are counted against the opening without modifiers. The times are measurements only in a timed window.
namespace bkom {

using bl::blockout::Piece;

struct Wall {
  Editor ed;
  GameObject *bo = nullptr;
  explicit Wall(float length) {
    ed.init_headless(1280, 720);
    ed.set_async_jobs_for_test(true);
    std::vector<GameObject *> roots;
    ed.scene().for_each([&](GameObject &g) {
      if (!g.parent) roots.push_back(&g);
    });
    ed.scene().destroy(roots);
    bo = ed.scene().create("Blockout");
    bo->add<Blockout>();
    bo->add<MeshFilter>()->mesh = std::make_shared<Mesh>();
    bo->add<MeshRenderer>();
    GameObject *w = ed.scene().create("Wall", bo);
    BlockoutPiece *p = w->add<BlockoutPiece>();
    p->shape = 0, p->size = Vec3(length, 3.0f, 0.2f);
    w->set_local_position(Vec3(0.0f, 1.5f, 0.0f));
    ed.command("blockout wait");
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
  }
  GameObject *opening(int kind, int style, float x) {
    GameObject *g = ed.scene().create(kind == 1 ? "Door" : "Window", bo);
    BlockoutPiece *bp = g->add<BlockoutPiece>();
    bp->opening = kind, bp->style = style, bp->width = 1.0f, bp->sill = kind == 1 ? 0.0f : 1.0f, bp->head = kind == 1 ? 2.1f : 2.0f, bp->depth = 0.4f;
    g->set_local_position(Vec3(x, 0.0f, 0.0f));
    return g;
  }
  const Mesh *mesh() const { return bo->get<MeshFilter>()->mesh.get(); }
};

struct Row {
  double hand_ms = 0.0, array_ms = 0.0;
  size_t hand_faces = 0, array_faces = 0;
  std::string problem;
};

/* Ten windows 2 m apart, by hand and as an Array of 10 of the first. */
static Row row_of(int style) {
  Row r;
  uint64_t evals = 0;
  {
    Wall w(40.0f);
    r.hand_ms = bkpm::change_to_drawn(w.ed, [&] {
      for (int k = 0; k < 10; k++) w.opening(2, style, -9.0f + 2.0f * (float)k);
      w.ed.mark_changed_for_test("ten windows");
    }, evals);
    r.hand_faces = w.mesh() ? w.mesh()->face_count() : 0;
  }
  {
    Wall w(40.0f);
    GameObject *win = w.opening(2, style, -9.0f);
    w.ed.command("blockout wait");
    r.array_ms = bkpm::change_to_drawn(w.ed, [&] {
      ArrayModifier *a = win->add<ArrayModifier>();
      a->count = 10, a->relative_offset = Vec3(2.0f, 0.0f, 0.0f);
      w.ed.mark_changed_for_test("an Array of 10");
    }, evals);
    r.array_faces = w.mesh() ? w.mesh()->face_count() : 0;
    r.problem = w.mesh() ? bkstress::problem(*w.mesh()) : "no mesh";
  }
  return r;
}

static void rows(Report &rep, const Options &o) {
  rep.table({"a 40 m wall with ten windows 2 m apart", "placed by hand: to the drawn mesh ms", "one window + an Array of 10 ms", "faces by hand / by the Array (the same wanted)",
             "problems", "verdict (100 ms, faces equal)"});
  const char *names[2] = {"plain (Opening Only)", "Grid style"};
  for (int s = 0; s < 2; s++) {
    const Row r = row_of(s == 0 ? 0 : 5);  // (5: Grid)
    const bool ok = r.problem.empty() && r.hand_faces == r.array_faces && r.array_ms <= 100.0;
    rep.row({names[s], f2(r.hand_ms), f2(r.array_ms), strprintf("%zu / %zu", r.hand_faces, r.array_faces), r.problem.empty() ? "0" : r.problem,
             o.quick ? (r.problem.empty() && r.hand_faces == r.array_faces ? "-" : "COUNTER") : ok ? "ok" : "OVER"});
  }
  /* The arrayed window with a Bevel on its hole, dragged along the wall. */
  Wall w(40.0f);
  GameObject *win = w.opening(2, 0, -9.0f);
  ArrayModifier *a = win->add<ArrayModifier>();
  a->count = 10, a->relative_offset = Vec3(2.0f, 0.0f, 0.0f);
  win->add<BevelModifier>()->width = 0.03f;
  w.ed.mark_changed_for_test("an Array and a Bevel");
  w.ed.command("blockout wait");
  for (int i = 0; i < 3; i++) w.ed.step_frame_headless();
  std::vector<double> frames, landings;
  uint64_t evals = 0, main_builds = 0;
  bkpm::drag(w.ed, *win, o.quick ? 30 : 120, frames, landings, evals, main_builds);
  const double worst = frames.empty() ? 0.0 : *std::max_element(frames.begin(), frames.end());
  rep.table({"a drag of the window with an Array of 10 and a Bevel (" + std::string(o.quick ? "30" : "120") + " frames)", "frame median ms", "frame worst ms",
             "move to landing median ms", "stack evaluations (0 wanted)", "main-thread builds (0 wanted)", "verdict"});
  rep.row({"10 copies, bevelled holes", f2(bkstress::median(frames)), f2(worst), landings.empty() ? "none" : f2(bkstress::median(landings)), std::to_string(evals),
           std::to_string(main_builds), o.quick ? (evals == 0 && main_builds == 0 ? "-" : "COUNTER") : worst <= 16.7 && evals == 0 && main_builds == 0 ? "ok" : "OVER"});
}

/* ---- The untimed sweep ---- */
struct Tally {
  size_t cases = 0, built = 0, problems = 0, newly = 0, unchecked = 0;
  std::string first;
};

static void sweep(Report &rep, const Options &o) {
  struct Kind {
    const char *name;
    int kind, style;
  };
  const Kind kinds[3] = {{"plain window", 2, 0}, {"Grid window", 2, 5}, {"Single door", 1, 1}};
  const std::vector<bkpm::ModCase> md = bkpm::mods();
  rep.table({"sweep: each modifier on an opening in a wall, alone and under an Array of 3 (untimed)", "cases", "built", "problems (0 wanted)",
             "faces newly on top of each other (against the opening without modifiers)", "too large to check", "first problem"});
  Tally all;
  for (const Kind &k : kinds) {
    Tally t;
    size_t baseline = 0;
    {
      Wall w(12.0f);
      w.opening(k.kind, k.style, 0.0f);
      w.ed.mark_changed_for_test("base");
      w.ed.command("blockout wait");
      if (w.mesh()) baseline = bkpm::overlaps_of(*w.mesh(), t.unchecked);
    }
    for (size_t m = 0; m < md.size(); m++) {
      if (o.quick && m % 3 != 0) continue;
      for (int arrayed = 0; arrayed < 2; arrayed++) {
        Wall w(12.0f);
        GameObject *g = w.opening(k.kind, k.style, -2.5f);
        if (arrayed) {
          ArrayModifier *a = g->add<ArrayModifier>();
          a->count = 3, a->relative_offset = Vec3(2.0f, 0.0f, 0.0f);
        }
        g->add_component(md[m].make()->clone());
        w.ed.mark_changed_for_test("sweep");
        w.ed.command("blockout wait");
        t.cases++;
        const Mesh *mesh = w.mesh();
        if (!mesh) continue;
        t.built++;
        const std::string bad = bkstress::problem(*mesh);
        if (!bad.empty()) {
          t.problems++;
          if (t.first.empty()) t.first = strprintf("%s + %s%s: %s", k.name, md[m].name, arrayed ? " (arrayed)" : "", bad.c_str());
        }
        const size_t over = bkpm::overlaps_of(*mesh, t.unchecked);
        if (over > baseline) {
          t.newly += over - baseline;
          std::printf("  faces newly on top: %s + %s%s: %zu (the opening alone %zu)\n", k.name, md[m].name, arrayed ? " (arrayed)" : "", over, baseline);
        }
      }
    }
    rep.row({k.name, std::to_string(t.cases), std::to_string(t.built), std::to_string(t.problems), std::to_string(t.newly), std::to_string(t.unchecked), t.first.empty() ? "-" : t.first});
    all.cases += t.cases, all.built += t.built, all.problems += t.problems, all.newly += t.newly, all.unchecked += t.unchecked;
    if (all.first.empty()) all.first = t.first;
  }
  rep.row({"all", std::to_string(all.cases), std::to_string(all.built), std::to_string(all.problems), std::to_string(all.newly), std::to_string(all.unchecked), all.problems ? "PROBLEMS" : "ok"});
}

}  // namespace bkom

static void test_blockout_opening_modifiers(Report &rep, const Options &o) {
  rep.title("Modifiers on a door or window (round 546, ADR 0150)",
            "An opening's Array and Mirror repeat the whole opening (its hole and parts, exact, from its numbers); the other modifiers reshape its hole. Rows: a 40 m wall with "
            "ten windows by hand and as one window with an Array of 10 (time to the drawn mesh, budget 100 ms; the faces must match), a drag of the arrayed window with a "
            "bevelled hole (budget 16.7 ms a frame, no stack evaluated while it moves), and an untimed sweep of every modifier on a plain window, a Grid window and a door.");
  bkom::rows(rep, o);
  bkom::sweep(rep, o);
}
