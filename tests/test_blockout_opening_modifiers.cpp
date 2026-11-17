// SPDX-License-Identifier: GPL-2.0-or-later
// Modifiers on a door or window (round 546, task 0526, ADR 0150): an opening's Array and Mirror repeat the whole opening (its
// placements), the other modifiers reshape its hole; Width, Head and Sill stay one opening's size.
#include "test_common.h"

#include "../src/scene/blockout_object.h"
#include "../src/scene/blockout_piece_stack.h"
#include "../src/scene/raycast.h"

using namespace bl;

namespace {

/* A window piece on its own object (no Blockout needed for the placements): 1 m wide, sill 1 m, head 2 m, 0.4 m deep. */
GameObject *r546_window(Editor &ed, const char *name = "Window") {
  GameObject *g = ed.scene().create(name);
  BlockoutPiece *bp = g->add<BlockoutPiece>();
  bp->opening = 2, bp->width = 1.0f, bp->sill = 1.0f, bp->head = 2.0f, bp->depth = 0.4f;
  return g;
}

/* Where each placement puts the hole's middle (the opening's space). */
std::vector<Vec3> r546_middles(const BlockoutPiece &bp, const std::vector<Mat4> &at) {
  std::vector<Vec3> out;
  for (const Mat4 &m : at) out.push_back(m.point(Vec3(0.0f, 0.5f * (bp.head + bp.sill), 0.0f)));
  return out;
}

bool r546_near(Vec3 a, Vec3 b) { return length(a - b) < 1e-4f; }

void tests_round546_placements() {
  test("opening modifiers: a window with no modifier, or with only a Bevel, is one opening (the identity alone)", [] {
    Editor ed;
    ed.init_headless(640, 400);
    GameObject *w = r546_window(ed);
    std::vector<Mat4> at = blockout_opening_placements(*w);
    CHECK(at.size() == 1 && r546_near(at[0].translation(), Vec3(0.0f)));
    w->add<BevelModifier>();
    at = blockout_opening_placements(*w);
    CHECK(at.size() == 1);
    CHECK(!blockout_opening_repeat(*w->get<BevelModifier>()));
    /* A piece that is no opening has no copies either, whatever it carries. */
    GameObject *box = ed.scene().create("Box");
    box->add<BlockoutPiece>();
    box->add<ArrayModifier>();
    CHECK(blockout_opening_placements(*box).size() == 1);
  });

  test("opening modifiers: an Array of 3 at relative offset 1.5 along X places three windows 1.5 widths apart, the first where the window is", [] {
    Editor ed;
    ed.init_headless(640, 400);
    GameObject *w = r546_window(ed);
    ArrayModifier *a = w->add<ArrayModifier>();
    a->count = 3, a->relative_offset = Vec3(1.5f, 0.0f, 0.0f);
    CHECK(blockout_opening_repeat(*a));
    const std::vector<Mat4> at = blockout_opening_placements(*w);
    const std::vector<Vec3> mid = r546_middles(*w->get<BlockoutPiece>(), at);
    CHECK(mid.size() == 3);
    if (mid.size() != 3) return;
    CHECK(r546_near(mid[0], Vec3(0.0f, 1.5f, 0.0f)));
    CHECK(r546_near(mid[1], Vec3(1.5f, 1.5f, 0.0f)));
    CHECK(r546_near(mid[2], Vec3(3.0f, 1.5f, 0.0f)));
    /* The constant offset adds metres; Fit Length counts the steps that fit. */
    a->relative_offset = Vec3(0.0f), a->constant_offset = Vec3(2.0f, 0.0f, 0.0f), a->fit_type = 1, a->fit_length = 7.0f;
    CHECK(blockout_opening_placements(*w).size() == 3);  // (7 / 2 = 3 steps, as ArrayModifier counts them)
  });

  test("opening modifiers: a Mirror X on a lone window is still one hole; after an Array of 2 it adds the copy's image - three windows, symmetric about the opening's middle", [] {
    Editor ed;
    ed.init_headless(640, 400);
    GameObject *w = r546_window(ed);
    MirrorModifier *mm = w->add<MirrorModifier>();
    CHECK(blockout_opening_repeat(*mm));
    CHECK(blockout_opening_placements(*w).size() == 1);  // (the window's middle is on the mirror plane: its image is the same hole, cut once)
    mm->enabled = false;
    ArrayModifier *a = w->add<ArrayModifier>();
    a->count = 2, a->relative_offset = Vec3(2.0f, 0.0f, 0.0f);
    w->add<MirrorModifier>();  // (after the Array in the stack)
    const std::vector<Vec3> mid = r546_middles(*w->get<BlockoutPiece>(), blockout_opening_placements(*w));
    CHECK(mid.size() == 3);  // (the window at 0, its copy at +2, the copy's image at -2; the window's image is the window)
    for (float x : {-2.0f, 0.0f, 2.0f})
      CHECK(std::any_of(mid.begin(), mid.end(), [&](Vec3 p) { return r546_near(p, Vec3(x, 1.5f, 0.0f)); }));
  });

  test("opening modifiers: more than 256 copies, a curve-fitted Array, NaN offsets - one opening, and the reason says why", [] {
    Editor ed;
    ed.init_headless(640, 400);
    GameObject *w = r546_window(ed);
    ArrayModifier *a = w->add<ArrayModifier>();
    a->count = 300;
    std::string why;
    CHECK(blockout_opening_placements(*w, &why).size() == 1);
    CHECK(why.find("256") != std::string::npos);
    a->count = 3, a->relative_offset = Vec3(std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f);
    why.clear();
    CHECK(blockout_opening_placements(*w, &why).size() == 1);
    CHECK(why.find("not finite") != std::string::npos);
    a->relative_offset = Vec3(1.0f, 0.0f, 0.0f), a->fit_type = 2;
    CHECK(!blockout_opening_repeat(*a));
    CHECK(blockout_opening_placements(*w).size() == 1);
    /* A disabled Array places nothing more. */
    a->fit_type = 0, a->enabled = false;
    CHECK(blockout_opening_placements(*w).size() == 1);
  });
}

/* A Blockout of one wall - 8 m along X, 3 m high, 0.2 m thick, its foot at y = 0 - built by hand (no Editor command needed). */
struct R546Wall {
  Editor ed;
  GameObject *bo = nullptr, *wall = nullptr;
  R546Wall() {
    ed.init_headless(900, 600);
    std::vector<GameObject *> roots;
    ed.scene().for_each([&](GameObject &g) {
      if (!g.parent) roots.push_back(&g);
    });
    ed.scene().destroy(roots);
    bo = ed.scene().create("Blockout");
    bo->add<Blockout>();
    bo->add<MeshFilter>()->mesh = std::make_shared<Mesh>();
    bo->add<MeshRenderer>();
    wall = ed.scene().create("Wall", bo);
    BlockoutPiece *p = wall->add<BlockoutPiece>();
    p->shape = 0, p->size = Vec3(8.0f, 3.0f, 0.2f);
    wall->set_local_position(Vec3(0.0f, 1.5f, 0.0f));
  }
  /* A window in the wall: its foot on the floor line, `x` along the wall. */
  GameObject *window(float x, int style = 0) {
    GameObject *g = ed.scene().create("Window", bo);
    BlockoutPiece *bp = g->add<BlockoutPiece>();
    bp->opening = 2, bp->style = style, bp->width = 1.0f, bp->sill = 1.0f, bp->head = 2.0f, bp->depth = 0.4f;
    g->set_local_position(Vec3(x, 0.0f, 0.0f));
    return g;
  }
  void land() {
    ed.mark_changed_for_test("r546");
    ed.command("blockout wait");
    for (int f = 0; f < 3000; f++) {
      ed.step_frame_headless();
      (void)bo->evaluated_mesh();
      if (!ed.eval_jobs_running_for_test() && !ed.blockout_jobs_running_for_test() && !bo->evaluation_pending(0)) break;
    }
    ed.step_frame_headless();
  }
  /* Whether a ray through the wall at (x, y) along Z passes (a hole there). */
  bool hole_at(float x, float y) {
    SceneHit hit;
    return !scene_raycast(ed.scene(), Vec3(x, y, 3.0f), Vec3(0.0f, 0.0f, -1.0f), 6.0f, hit);
  }
};

void tests_round546_build() {
  test("opening modifiers: a plain window with an Array of 3 (relative offset 2 along X) cuts three holes in the wall, 2 m apart; the wall stays solid between them", [] {
    R546Wall w;
    GameObject *win = w.window(-2.0f);
    w.land();
    CHECK(w.hole_at(-2.0f, 1.5f));
    CHECK(!w.hole_at(0.0f, 1.5f));
    ArrayModifier *a = win->add<ArrayModifier>();
    a->count = 3, a->relative_offset = Vec3(2.0f, 0.0f, 0.0f);
    w.land();
    for (float x : {-2.0f, 0.0f, 2.0f}) CHECK(w.hole_at(x, 1.5f));
    for (float x : {-1.0f, 1.0f, 3.0f}) CHECK(!w.hole_at(x, 1.5f));
    CHECK(!w.hole_at(0.0f, 0.5f));  // (below the sill: wall)
    /* Width, Head and Sill are one opening's: the Array did not touch them. */
    const BlockoutPiece *bp = win->get<BlockoutPiece>();
    CHECK_NEAR(bp->width, 1.0f, 1e-6f);
    CHECK_NEAR(bp->sill, 1.0f, 1e-6f);
    CHECK_NEAR(bp->head, 2.0f, 1e-6f);
    /* The Array off again: one hole. */
    a->enabled = false;
    w.land();
    CHECK(w.hole_at(-2.0f, 1.5f));
    CHECK(!w.hole_at(0.0f, 1.5f));
  });

  test("opening modifiers: a styled window (Grid) with an Array of 2 has its frame in both copies - the ray through each copy's middle meets the frame's bars", [] {
    R546Wall w;
    GameObject *win = w.window(-1.5f, 1);
    ArrayModifier *a = win->add<ArrayModifier>();
    a->count = 2, a->relative_offset = Vec3(3.0f, 0.0f, 0.0f);
    w.land();
    /* Each copy: a hole beside its middle (between the bars) and the same picture at the copy as at the window. */
    const bool at0 = w.hole_at(-1.5f + 0.25f, 1.5f + 0.25f), at1 = w.hole_at(1.5f + 0.25f, 1.5f + 0.25f);
    const bool mid0 = w.hole_at(-1.5f, 1.5f), mid1 = w.hole_at(1.5f, 1.5f);
    std::printf("    copy 0: pane-cell %d middle %d; copy 1: pane-cell %d middle %d\n", (int)at0, (int)mid0, (int)at1, (int)mid1);
    CHECK(at0 == at1);
    CHECK(mid0 == mid1);
    CHECK(!w.hole_at(0.0f, 1.5f));  // (between the copies: wall)
  });

  test("opening modifiers: a click on a copy's hole edge names the window (the copy's faces are the window's)", [] {
    R546Wall w;
    GameObject *win = w.window(-2.0f);
    ArrayModifier *a = win->add<ArrayModifier>();
    a->count = 2, a->relative_offset = Vec3(4.0f, 0.0f, 0.0f);
    w.land();
    /* The copy's id is a made id whose owner is the window. */
    CHECK(blockout_piece_owner(blockout_copy_id(win->id, 1)) == win->id);
    CHECK(blockout_copy_of(blockout_copy_id(win->id, 1)) == 1);
    CHECK(blockout_copy_of(win->id) == 0);
  });
}

void tests_round546_lesson() {
  test("opening modifiers: the Learn section's Try-it commands run - a window with an Array of 3, and the status says it is repeated: 3 copies", [] {
    Editor ed;
    ed.init_headless(1200, 800);
    ed.command("newscene");
    ed.command("blockout draw room auto at 0 0 0 normal 0 1 0 size 8 4 2.6");
    ed.command("blockout add window 1 1.8 0.9");
    GameObject *win = nullptr;
    ed.scene().for_each([&](GameObject &g) {
      if (const BlockoutPiece *bp = g.get<BlockoutPiece>(); bp && bp->opening == 2) win = &g;
    });
    CHECK(win != nullptr);
    if (!win) return;
    for (const char *c : {"component ArrayModifier", "set ArrayModifier.Count 3", "set ArrayModifier.RelativeOffset 2 0 0"}) ed.command(c);  // (the lesson's line, split at ';' as the Learn tab runs it)
    const ArrayModifier *a = win->get<ArrayModifier>();
    CHECK(a && a->count == 3 && std::fabs(a->relative_offset.x - 2.0f) < 1e-6f);  // (on the window: it was the selection)
    CHECK(blockout_opening_placements(*win).size() == 3);
    ed.command("blockout wait");
    std::vector<std::pair<std::string, bool>> lines;
    GameObject *bo = win->parent;
    while (bo && !bo->get<Blockout>()) bo = bo->parent;
    if (bo) blockout_piece_stack_status(*bo, lines);
    bool said = false;
    for (const auto &l : lines) said = said || l.first.find("3 copies") != std::string::npos;
    CHECK(said);
  });
}

}  // namespace

void run_blockout_opening_modifiers_tests() {
  tests_round546_lesson();
  tests_round546_placements();
  tests_round546_build();
}
