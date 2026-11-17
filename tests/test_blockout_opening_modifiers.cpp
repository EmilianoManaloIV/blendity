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

const Mesh *r546_mesh(const GameObject &bo) {
  const MeshFilter *mf = bo.get<MeshFilter>();
  return mf && mf->mesh ? mf->mesh.get() : nullptr;
}

bool r546_same(const Mesh &a, const Mesh &b) {
  if (a.positions.size() != b.positions.size() || a.face_offsets != b.face_offsets || a.corner_verts != b.corner_verts) return false;
  for (size_t i = 0; i < a.positions.size(); i++)
    if (a.positions[i].x != b.positions[i].x || a.positions[i].y != b.positions[i].y || a.positions[i].z != b.positions[i].z) return false;
  return true;
}


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
  void land(bool mark = true) {  // (mark: the hand edits before it are an undo step - not where undo itself is tested)
    if (mark) ed.mark_changed_for_test("r546");
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

  test("opening modifiers: a styled window (Grid, no glass) with an Array of 2 is, face for face, two such windows placed by hand - frame, bars and pane cells in both", [] {
    auto grid = [](GameObject &g) {
      BlockoutPiece *bp = g.get<BlockoutPiece>();
      bp->glass = false;  // (no pane: a ray through a pane cell passes - and one on a bar does not)
    };
    R546Wall arrayed;
    GameObject *win = arrayed.window(-1.5f, 5);
    grid(*win);
    ArrayModifier *a = win->add<ArrayModifier>();
    a->count = 2, a->relative_offset = Vec3(3.0f, 0.0f, 0.0f);
    arrayed.land();
    R546Wall by_hand;
    grid(*by_hand.window(-1.5f, 5));
    grid(*by_hand.window(1.5f, 5));
    by_hand.land();
    const Mesh *ma = r546_mesh(*arrayed.bo), *mh = r546_mesh(*by_hand.bo);
    std::printf("    arrayed %zu faces, by hand %zu\n", ma ? ma->face_count() : 0, mh ? mh->face_count() : 0);
    CHECK(ma && mh && ma->face_count() == mh->face_count());
    /* Each copy as the window: the middle cell open in both, the wall between them solid, and each copy exactly where the hand-placed one is. */
    for (float x : {-1.5f, 1.5f}) {
      CHECK(arrayed.hole_at(x, 1.5f) == by_hand.hole_at(x, 1.5f));
      CHECK(arrayed.hole_at(x, 1.5f));
      CHECK(arrayed.hole_at(x + 0.17f, 1.5f) == by_hand.hole_at(x + 0.17f, 1.5f));  // (on or beside a bar: the same either way)
    }
    CHECK(!arrayed.hole_at(0.0f, 1.5f));  // (between the copies: wall)
  });

  test("opening modifiers: a plain door with an Array of 2 cuts two doorways down to the floor", [] {
    R546Wall w;
    GameObject *d = w.ed.scene().create("Door", w.bo);
    BlockoutPiece *bp = d->add<BlockoutPiece>();
    bp->opening = 1, bp->width = 1.0f, bp->sill = 0.0f, bp->head = 2.1f, bp->depth = 0.4f;
    d->set_local_position(Vec3(-2.0f, 0.0f, 0.0f));
    ArrayModifier *a = d->add<ArrayModifier>();
    a->count = 2, a->relative_offset = Vec3(3.0f, 0.0f, 0.0f);
    w.land();
    for (float x : {-2.0f, 1.0f}) {
      CHECK(w.hole_at(x, 0.2f));
      CHECK(w.hole_at(x, 1.9f));
      CHECK(!w.hole_at(x, 2.5f));  // (above the head: wall)
    }
    CHECK(!w.hole_at(-0.5f, 1.0f));
  });

  test("opening modifiers: a Bevel above an Array and an Array above a Bevel build the same mesh - the repeats run last whatever their order", [] {
    auto build = [](bool bevel_first) {
      R546Wall w;
      GameObject *win = w.window(-2.0f);
      auto add_array = [&] {
        ArrayModifier *a = win->add<ArrayModifier>();
        a->count = 3, a->relative_offset = Vec3(2.0f, 0.0f, 0.0f);
      };
      if (!bevel_first) add_array();
      win->add<BevelModifier>()->width = 0.05f;
      if (bevel_first) add_array();
      w.land();
      return r546_mesh(*w.bo) ? *r546_mesh(*w.bo) : Mesh();
    };
    const Mesh one = build(true), other = build(false);
    CHECK(one.face_count() > 0 && r546_same(one, other));
  });

  test("opening modifiers: a click on a copy's hole selects the window (the copy's faces are the window's)", [] {
    R546Wall w;
    GameObject *win = w.window(-2.0f);
    ArrayModifier *a = win->add<ArrayModifier>();
    a->count = 2, a->relative_offset = Vec3(4.0f, 0.0f, 0.0f);
    w.land();
    CHECK(blockout_piece_owner(blockout_copy_id(win->id, 1)) == win->id);
    CHECK(blockout_copy_of(blockout_copy_id(win->id, 1)) == 1);
    CHECK(blockout_copy_of(win->id) == 0);
    /* The middle of the copy's hole floor (at x = 2), from four sides: looking along the wall the ray meets solid wall first; from in
     * front and behind it goes through the hole onto its floor. */
    size_t named = 0;
    for (int yaw : {0, 90, 180, 270}) {
      w.ed.command(strprintf("camera %d 35 5 2 1.0 0", yaw));
      for (int i = 0; i < 3; i++) w.ed.step_frame_headless();
      int x = 0, y = 0;
      if (!w.ed.project_to_window(Vec3(2.0f, 1.0f, 0.0f), x, y)) continue;
      w.ed.clear_selection_for_test();
      for (platform::EventType t : {platform::EventType::MouseMove, platform::EventType::MouseDown, platform::EventType::MouseUp}) {
        platform::Event e;
        e.type = t, e.x = x, e.y = y;
        w.ed.step_frame_headless({e});
      }
      for (int i = 0; i < 2; i++) w.ed.step_frame_headless();
      const bool is_window = w.ed.selection_size_for_test() == 1 && w.ed.active_object_for_test() == win;
      std::printf("    yaw %d: %s\n", yaw, is_window ? "the window" : "something else");
      named += is_window;
    }
    CHECK(named >= 2);
  });
}

void tests_round546_more() {
  test("opening modifiers: a Bevel on a plain window reshapes its hole - more faces than the plain cut, a valid mesh, the hole still open", [] {
    R546Wall w;
    GameObject *win = w.window(0.0f);
    w.land();
    const size_t plain = r546_mesh(*w.bo) ? r546_mesh(*w.bo)->face_count() : 0;
    BevelModifier *b = win->add<BevelModifier>();
    b->width = 0.05f;
    w.land();
    const Mesh *m = r546_mesh(*w.bo);
    std::string why;
    std::printf("    plain cut %zu faces, bevelled %zu\n", plain, m ? m->face_count() : 0);
    CHECK(m && m->face_count() > plain);
    CHECK(m && structurally_valid(*m, &why));
    CHECK(w.hole_at(0.0f, 1.5f));
  });

  test("opening modifiers: moving a window with an Array and a Bevel evaluates nothing - the copies move with it", [] {
    R546Wall w;
    GameObject *win = w.window(-2.0f);
    win->add<BevelModifier>()->width = 0.03f;
    ArrayModifier *a = win->add<ArrayModifier>();
    a->count = 2, a->relative_offset = Vec3(2.0f, 0.0f, 0.0f);
    w.land();
    const uint64_t e0 = blockout_piece_stack_evaluations();
    for (int i = 1; i <= 3; i++) {
      win->set_local_position(Vec3(-2.0f + 0.25f * (float)i, 0.0f, 0.0f));
      w.land();
    }
    std::printf("    3 moves: %llu stack evaluations\n", (unsigned long long)(blockout_piece_stack_evaluations() - e0));
    CHECK(blockout_piece_stack_evaluations() == e0);  // (the hole's key is its numbers and stack, never its place: one evaluation for every copy)
    CHECK(w.hole_at(-1.25f, 1.5f) && w.hole_at(0.75f, 1.5f));  // (the window and its copy, 2 m on)
    CHECK(!w.hole_at(-2.25f, 1.5f));
  });

  test("opening modifiers: adding an Array to a window, then undo and redo, gives the mesh before and after bit for bit", [] {
    R546Wall w;
    GameObject *win = w.window(-2.0f);
    w.land();
    const Mesh before = r546_mesh(*w.bo) ? *r546_mesh(*w.bo) : Mesh();
    w.ed.select_for_test(win->id, false);
    w.ed.command("component ArrayModifier");
    w.ed.command("set ArrayModifier.Count 3");
    w.land(false);
    const Mesh after = r546_mesh(*w.bo) ? *r546_mesh(*w.bo) : Mesh();
    CHECK(!r546_same(before, after));
    /* Undo until the window has no Array (the steps a command makes are the editor's to count), redo until it has its 3 again.
     * Undo makes the scene's objects again: the Blockout and the window are found by name each time. */
    auto array_of = [&]() -> const ArrayModifier * {
      GameObject *g = w.ed.scene().find_by_name("Window");
      return g ? g->get<ArrayModifier>() : nullptr;
    };
    for (int i = 0; i < 4 && array_of(); i++) w.ed.command("undo");
    CHECK(!array_of());
    w.bo = w.ed.scene().find_by_name("Blockout");
    CHECK(w.bo != nullptr);
    if (!w.bo) return;
    w.land(false);
    CHECK(r546_mesh(*w.bo) && r546_same(*r546_mesh(*w.bo), before));
    for (int i = 0; i < 4 && !(array_of() && array_of()->count == 3); i++) w.ed.command("redo");
    CHECK(array_of() && array_of()->count == 3);
    w.bo = w.ed.scene().find_by_name("Blockout");
    CHECK(w.bo != nullptr);
    if (!w.bo) return;
    w.land(false);
    CHECK(r546_mesh(*w.bo) && r546_same(*r546_mesh(*w.bo), after));
  });

  test("opening modifiers: NaN and huge numbers in a window's Array or Bevel leave a valid mesh, or the window as its numbers, and say why", [] {
    R546Wall w;
    GameObject *win = w.window(0.0f);
    ArrayModifier *a = win->add<ArrayModifier>();
    BevelModifier *b = win->add<BevelModifier>();
    const float bad[] = {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), 1e30f, -1e30f};
    size_t cases = 0, broken = 0;
    for (float v : bad) {
      a->count = 3, a->relative_offset = Vec3(v, 0.0f, 0.0f), b->width = 0.02f;
      w.land();
      cases++;
      const Mesh *m = r546_mesh(*w.bo);
      broken += !(m && structurally_valid(*m));
      a->relative_offset = Vec3(1.5f, 0.0f, 0.0f), b->width = v;
      w.land();
      cases++;
      m = r546_mesh(*w.bo);
      broken += !(m && structurally_valid(*m));
    }
    std::printf("    %zu cases, %zu broken\n", cases, broken);
    CHECK(broken == 0);
    a->relative_offset = Vec3(std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f);
    std::vector<std::pair<std::string, bool>> lines;
    blockout_piece_stack_status(*w.bo, lines);
    bool said = false;
    for (const auto &l : lines) said = said || (l.second && l.first.find("not finite") != std::string::npos);
    CHECK(said);
  });

  test("opening modifiers: a scene saved with an Array on a window (inert before round 546) opens with its copies cut", [] {
    R546Wall w;
    GameObject *win = w.window(-2.0f);
    ArrayModifier *a = win->add<ArrayModifier>();
    a->count = 3, a->relative_offset = Vec3(2.0f, 0.0f, 0.0f);
    w.land();
    const std::string dir = w.ed.resolve_asset_path_for_test("Assets/Scenes");
    fs::make_dirs(dir);
    const std::string path = fs::join(dir, "r546_saved.scene");
    CHECK(fs::write_file(path, save_scene_text(w.ed.scene())));
    w.ed.command("newscene");
    CHECK(w.ed.open_scene_for_test(path));
    GameObject *bo = nullptr;
    for (int i = 0; i < 400 && !bo; i++) {
      w.ed.step_frame_headless();
      w.ed.scene().for_each([&](GameObject &g) {
        if (g.get<Blockout>() && !bo) bo = &g;
      });
    }
    fs::remove_file(path);  // (read: the scratch project keeps nothing of the test, whatever follows)
    CHECK(bo != nullptr);
    if (!bo) return;
    w.bo = bo;
    w.land();
    for (float x : {-2.0f, 0.0f, 2.0f}) CHECK(w.hole_at(x, 1.5f));
  });
}

/* The parts (round 547's leaf, frame and glass objects) under an opening, by kind and copy. */
std::vector<const GameObject *> r546_parts(const GameObject &opening, int kind) {
  std::vector<const GameObject *> out;
  for (const GameObject *c : opening.children)
    if (const BlockoutLeaf *mark = c->get<BlockoutLeaf>(); mark && mark->kind == kind) out.push_back(c);
  return out;
}

void tests_round546_parts() {
  test("opening modifiers: a Single door with an Array of 2 has a leaf and a frame for each copy, each copy's in its own doorway", [] {
    R546Wall w;
    GameObject *d = w.ed.scene().create("Door", w.bo);
    BlockoutPiece *bp = d->add<BlockoutPiece>();
    bp->opening = 1, bp->style = 1, bp->width = 1.0f, bp->sill = 0.0f, bp->head = 2.1f, bp->depth = 0.4f;
    d->set_local_position(Vec3(-2.0f, 0.0f, 0.0f));
    w.land();
    const size_t leaves1 = r546_parts(*d, kPartKindLeaf).size();
    ArrayModifier *a = d->add<ArrayModifier>();
    a->count = 2, a->relative_offset = Vec3(3.0f, 0.0f, 0.0f);
    w.land();
    const std::vector<const GameObject *> leaves = r546_parts(*d, kPartKindLeaf);
    std::printf("    leaves: %zu without the Array, %zu with it\n", leaves1, leaves.size());
    CHECK(leaves1 >= 1 && leaves.size() == 2 * leaves1);
    bool in_copy = false;
    for (const GameObject *l : leaves)
      if (l->get<BlockoutLeaf>()->copy == 1) in_copy = in_copy || std::fabs(l->world_position().x - 1.0f) <= 0.55f;  // (copy 1 stands 3 m on, its doorway x 0.5 .. 1.5: the leaf hinges on one of its jambs)
    CHECK(in_copy);
  });

  test("opening modifiers: a mirrored copy's leaf hinges on the other jamb - the plans' sides are opposite", [] {
    R546Wall w;
    GameObject *d = w.ed.scene().create("Door", w.bo);
    BlockoutPiece *bp = d->add<BlockoutPiece>();
    bp->opening = 1, bp->style = 1, bp->width = 1.0f, bp->sill = 0.0f, bp->head = 2.1f, bp->depth = 0.4f;
    d->set_local_position(Vec3(0.0f, 0.0f, 0.0f));
    ArrayModifier *a = d->add<ArrayModifier>();
    a->count = 2, a->relative_offset = Vec3(2.5f, 0.0f, 0.0f);
    d->add<MirrorModifier>();  // (X: the copy at +2.5 m and its image at -2.5 m)
    w.land();
    float sign_plus = 0.0f, sign_minus = 0.0f;
    for (const GameObject *l : r546_parts(*d, kPartKindLeaf)) {
      const float x = l->local().position.x;
      const BlockoutLeaf *mark = l->get<BlockoutLeaf>();
      std::printf("    leaf of copy %d at x %.2f\n", mark->copy, x);
      if (x > 1.0f) sign_plus = 1.0f;
      if (x < -1.0f) sign_minus = 1.0f;
    }
    CHECK(sign_plus > 0.0f && sign_minus > 0.0f);  // (a leaf at each side)
    std::vector<BlockoutLeafPlan> one;
    blockout_part_plans(*bp, one);
    CHECK(!one.empty());
    /* The hinges: the leaf at +2.5 and the leaf at -2.5 stand mirrored - their local X positions mirror about the opening's middle. */
    std::vector<float> xs;
    for (const GameObject *l : r546_parts(*d, kPartKindLeaf)) xs.push_back(l->local().position.x);
    std::sort(xs.begin(), xs.end());
    CHECK(xs.size() == 3 && std::fabs(xs.front() + xs.back()) < 1e-3f);
  });

  test("opening modifiers: a Bevel reshapes a styled window's one hole (round 547's) - more faces than without it, a valid mesh, every copy bevelled", [] {
    R546Wall w;
    GameObject *win = w.window(-2.0f, 1);
    ArrayModifier *a = win->add<ArrayModifier>();
    a->count = 2, a->relative_offset = Vec3(4.0f, 0.0f, 0.0f);
    w.land();
    const size_t plain = r546_mesh(*w.bo) ? r546_mesh(*w.bo)->face_count() : 0;
    win->add<BevelModifier>()->width = 0.04f;
    w.land();
    const Mesh *m = r546_mesh(*w.bo);
    std::printf("    two Single windows: %zu faces, bevelled %zu\n", plain, m ? m->face_count() : 0);
    CHECK(m && m->face_count() > plain && structurally_valid(*m));
    CHECK(w.hole_at(-2.0f, 1.5f) == w.hole_at(2.0f, 1.5f));
  });

  test("opening modifiers: a Mirror Y would stand a copy on its head - it is left out, and the status says why", [] {
    R546Wall w;
    GameObject *win = w.window(0.0f);
    MirrorModifier *mm = win->add<MirrorModifier>();
    mm->x = false, mm->y = true;
    std::string why;
    CHECK(blockout_opening_placements(*win, &why).size() == 1);
    CHECK(why.find("upright") != std::string::npos);
    w.land();
    std::vector<std::pair<std::string, bool>> lines;
    blockout_piece_stack_status(*w.bo, lines);
    bool said = false;
    for (const auto &l : lines) said = said || l.first.find("upright") != std::string::npos;
    CHECK(said);
  });
}

void tests_round546_lesson() {
  test("opening modifiers: the Learn section's Try-it commands run - a window with an Array of 3, and the status says it is repeated: 3 copies", [] {
    Editor ed;
    ed.init_headless(1200, 800);
    ed.command("newscene");
    ed.command("blockout draw room auto at 0 0 0 normal 0 1 0 size 4 8 2.6");
    ed.command("blockout add window 1 1.8 0.9");
    GameObject *win = nullptr;
    ed.scene().for_each([&](GameObject &g) {
      if (const BlockoutPiece *bp = g.get<BlockoutPiece>(); bp && bp->opening == 2) win = &g;
    });
    CHECK(win != nullptr);
    if (!win) return;
    for (const char *c : {"component ArrayModifier", "set ArrayModifier.Count 3", "set ArrayModifier.RelativeOffset 1.5 0 0"}) ed.command(c);  // (the lesson's line, split at ';' as the Learn tab runs it)
    const ArrayModifier *a = win->get<ArrayModifier>();
    CHECK(a && a->count == 3 && std::fabs(a->relative_offset.x - 1.5f) < 1e-6f);  // (on the window: it was the selection)
    CHECK(blockout_opening_placements(*win).size() == 3);
    ed.command("blockout wait");
    std::vector<std::pair<std::string, bool>> lines;
    GameObject *bo = win->parent;
    while (bo && !bo->get<Blockout>()) bo = bo->parent;
    if (bo) blockout_piece_stack_status(*bo, lines);
    bool said = false;
    for (const auto &l : lines) said = said || l.first.find("3 copies") != std::string::npos;
    CHECK(said);
    ed.command("blockout info");  // (step 4 runs)
    /* Three holes in the wall: a ray in through each copy's middle, along the window's own Z, runs on into the room. */
    const Mat4 wm = win->world_matrix();
    const Vec3 n = normalize(wm.dir(Vec3(0, 0, 1)));
    const BlockoutPiece *bp = win->get<BlockoutPiece>();
    size_t through = 0;
    for (const Mat4 &f : blockout_opening_frames(*win, wm)) {
      const Vec3 mid = f.point(Vec3(0.0f, 0.5f * (bp->sill + bp->head), 0.0f));
      for (float side : {1.0f, -1.0f}) {
        SceneHit hit;
        const bool far = !scene_raycast(ed.scene(), mid + n * side, n * -side, 100.0f, hit) || hit.distance > 1.5f;
        if (far) {
          through++;
          break;
        }
      }
    }
    std::printf("    %zu of 3 copies cut through the wall\n", through);
    CHECK(through == 3);
  });
}

}  // namespace

void run_blockout_opening_modifiers_tests() {
  tests_round546_lesson();
  tests_round546_more();
  tests_round546_parts();
  tests_round546_placements();
  tests_round546_build();
}
