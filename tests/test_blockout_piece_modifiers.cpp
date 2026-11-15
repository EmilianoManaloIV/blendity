// SPDX-License-Identifier: GPL-2.0-or-later
// Modifiers on a Blockout piece (round 514, task 0496, ADR 0138): a piece's stack is ordinary modifier components on its object,
// shown in its Inspector, refused where a piece cannot take one, keyed by its numbers and stack but never its place.
#include "test_common.h"

#include "../src/scene/blockout_object.h"
#include "../src/scene/blockout_piece_stack.h"

using namespace bl;

namespace {

using R514ET = platform::EventType;

platform::Event r514_ev(R514ET t, int x, int y) {
  platform::Event e;
  e.type = t, e.x = x, e.y = y;
  return e;
}

platform::Event r514_key(int key) {
  platform::Event e;
  e.type = R514ET::KeyDown, e.key = key;
  return e;
}

void r514_settle(Editor &ed, int frames = 2) {
  for (int i = 0; i < frames; i++) ed.step_frame_headless();
}

void r514_click(Editor &ed, int x, int y) {
  ed.step_frame_headless({r514_ev(R514ET::MouseMove, x, y)});
  ed.step_frame_headless({r514_ev(R514ET::MouseDown, x, y)});
  ed.step_frame_headless({r514_ev(R514ET::MouseUp, x, y)});
}

/* A recorded control clicked by its key; false when it is not showing. */
bool r514_click_key(Editor &ed, const std::string &key) {
  ed.record_buttons_for_test(true);
  r514_settle(ed, 2);
  Recti r;
  if (!ed.button_rect_for_test(key, r)) return false;
  r514_click(ed, r.x + r.w / 2, r.y + r.h / 2);
  r514_settle(ed, 2);
  return true;
}

/* A new Blockout with its 2 m box selected (what `blockout new` makes). */
GameObject *r514_box(Editor &ed) {
  ed.command("blockout new");
  ed.command("blockout wait");
  GameObject *box = nullptr;
  ed.scene().for_each([&](GameObject &g) {
    if (g.get<BlockoutPiece>() && !box) box = &g;
  });
  return box;
}

size_t r514_count(const GameObject &g, const char *type) {
  size_t n = 0;
  for (const auto &c : g.components) n += std::string(c->type_name()) == type;
  return n;
}

void tests_round514() {
  test("blockout piece modifiers: a selected piece shows a Modifiers section; Add Modifier > Bevel puts a Bevel on the piece, Boolean is off for a piece", [] {
    Editor ed;
    ed.init_headless(1500, 1400);
    GameObject *box = r514_box(ed);
    CHECK(box != nullptr);
    if (!box) return;
    ed.command("select " + box->name);
    CHECK(r514_click_key(ed, "Add Modifier"));  // (the section is there for a piece)
    CHECK(r514_click_key(ed, "menu:Bevel"));
    CHECK(r514_count(*box, "BevelModifier") == 1);
    CHECK(blockout_piece_stack_live(*box));
    CHECK(r514_click_key(ed, "Add Modifier"));
    CHECK(r514_click_key(ed, "menu:Boolean"));  // (off: clicking it adds nothing - and the menu stays open, as for any item that is off)
    CHECK(r514_count(*box, "BooleanModifier") == 0);
    ed.step_frame_headless({r514_key(platform::KEY_ESCAPE)});
    ed.command("undo");
    r514_settle(ed, 2);
    GameObject *again = ed.scene().find_by_name(box->name);
    CHECK(again && r514_count(*again, "BevelModifier") == 0);
  });

  test("blockout piece modifiers: which modifiers a piece takes - Bevel, Array, Mirror, Subdivision yes; Boolean, Curve, Lattice, Armature, Smooth Normals no, each with a reason", [] {
    for (const char *t : {"BevelModifier", "ArrayModifier", "MirrorModifier", "SolidifyModifier", "SubdivisionSurface", "DisplaceModifier", "DecimateModifier",
                          "TriangulateModifier", "WeldModifier", "WireframeModifier", "ScrewModifier", "CastModifier", "SimpleDeformModifier", "SmoothModifier"})
      CHECK(blockout_piece_modifier_refusal(t).empty());
    for (const char *t : {"BooleanModifier", "CurveModifier", "LatticeModifier", "MeshDeformModifier", "SkinModifier", "NormalTransferModifier", "SmoothNormalsModifier"}) {
      const std::string why = blockout_piece_modifier_refusal(t);
      std::printf("    %s: %s\n", t, why.c_str());
      CHECK(!why.empty());
    }
  });

  test("blockout piece modifiers: a room, a door or window and an Intersect piece take no stack and say why; a Bevel put on one by the console stays on it, inert", [] {
    Editor ed;
    ed.init_headless(1500, 1400);
    GameObject *box = r514_box(ed);
    CHECK(box != nullptr);
    if (!box) return;
    BlockoutPiece *bp = box->get<BlockoutPiece>();
    CHECK(blockout_piece_stack_refusal(*bp).empty());
    struct Kind {
      const char *what;
      std::function<void(BlockoutPiece &)> set;
    };
    const std::vector<Kind> kinds = {{"room", [](BlockoutPiece &p) { p.room = true; }},
                                     {"door", [](BlockoutPiece &p) { p.opening = 1; }},
                                     {"window", [](BlockoutPiece &p) { p.opening = 2; }},
                                     {"intersect", [](BlockoutPiece &p) { p.operation = 2; }}};
    ed.command("select " + box->name);
    ed.command("component BevelModifier");
    CHECK(r514_count(*box, "BevelModifier") == 1);
    for (const Kind &k : kinds) {
      const BlockoutPiece keep = *bp;
      k.set(*bp);
      const std::string why = blockout_piece_stack_refusal(*bp);
      std::printf("    %s: %s\n", k.what, why.c_str());
      CHECK(!why.empty());
      CHECK(!blockout_piece_stack_live(*box));  // (the Bevel is there, inert)
      CHECK(blockout_piece_stack_hash(*box) == 0);
      *bp = keep;
    }
    CHECK(blockout_piece_stack_live(*box));
    CHECK(r514_click_key(ed, "Add Modifier"));  // (the section shows for every piece; a door's says why it takes none)
  });

  test("blockout piece modifiers: the stack's key follows its modifiers' fields and order, a switched-off modifier, and never the piece's place", [] {
    Editor ed;
    ed.init_headless(900, 600);
    GameObject *box = r514_box(ed);
    CHECK(box != nullptr);
    if (!box) return;
    CHECK(blockout_piece_stack_hash(*box) == 0);
    ed.command("select " + box->name);
    ed.command("component BevelModifier");
    const uint64_t one = blockout_piece_stack_hash(*box);
    CHECK(one != 0);
    box->set_local_position(box->local().position + Vec3(3.0f, 1.0f, -2.0f));
    box->set_local_rotation(Quat::euler(Vec3(0.0f, 30.0f, 0.0f)));
    CHECK(blockout_piece_stack_hash(*box) == one);  // (moved and turned: the same evaluated mesh)
    box->get<BevelModifier>()->width = 0.2f;
    const uint64_t wider = blockout_piece_stack_hash(*box);
    CHECK(wider != one);
    ed.command("component ArrayModifier");
    const uint64_t two = blockout_piece_stack_hash(*box);
    CHECK(two != wider);
    box->get<ArrayModifier>()->enabled = false;
    CHECK(blockout_piece_stack_hash(*box) == wider);  // (off: as if it were not there)
    ed.command("component BooleanModifier");          // (not taken: no part of the stack)
    CHECK(blockout_piece_stack_hash(*box) == wider);
  });

  test("blockout piece modifiers: Ctrl+D on a piece with a stack makes a piece with the same stack; save and open keep it", [] {
    Editor ed;
    ed.init_headless(900, 600);
    GameObject *box = r514_box(ed);
    CHECK(box != nullptr);
    if (!box) return;
    ed.command("select " + box->name);
    ed.command("component ArrayModifier");
    box->get<ArrayModifier>()->count = 4;
    const uint64_t key = blockout_piece_stack_hash(*box);
    ed.command("duplicate");
    r514_settle(ed, 2);
    size_t with = 0;
    ed.scene().for_each([&](GameObject &g) {
      if (g.get<BlockoutPiece>() && blockout_piece_stack_hash(g) == key) with++;
    });
    CHECK(with == 2);
    const std::string path = std::string(std::getenv("BLENDITY_PROJECT") ? std::getenv("BLENDITY_PROJECT") : ".") + "/r514_piece_stack.scene";
    ed.command("save " + path);
    Editor ed2;
    ed2.init_headless(900, 600);
    ed2.command("open " + path);
    size_t loaded = 0;
    ed2.scene().for_each([&](GameObject &g) {
      if (g.get<BlockoutPiece>() && blockout_piece_stack_hash(g) == key && g.get<ArrayModifier>() && g.get<ArrayModifier>()->count == 4) loaded++;
    });
    CHECK(loaded == 2);
  });
}

}  // namespace

void run_blockout_piece_modifiers_tests() {
  tests_round514();
}
