// SPDX-License-Identifier: GPL-2.0-or-later
// Modifiers on a Blockout piece (round 514, task 0496, ADR 0138): a piece's stack is ordinary modifier components on its object,
// shown in its Inspector, refused where a piece cannot take one, keyed by its numbers and stack but never its place.
#include "test_common.h"

#include "../src/scene/blockout_object.h"
#include "../src/scene/blockout_piece_stack.h"

#include <cstring>
#include <map>

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

const Mesh *r514_mesh(const GameObject &bo) {
  const MeshFilter *mf = bo.get<MeshFilter>();
  return mf && mf->mesh ? mf->mesh.get() : nullptr;
}

AABB r514_bounds(const GameObject &bo) {
  AABB b;
  if (const Mesh *m = r514_mesh(bo))
    for (const Vec3 &v : m->positions) b.add(v);
  return b;
}

/* The Blockout's mesh is what its pieces build as numbers alone (no stack): the path a piece without modifiers takes, bit for bit. */
bool r514_numbers_build(const GameObject &bo) {
  std::vector<blockout::Piece> pieces;
  blockout::Settings st;
  blockout_snapshot(bo, pieces, st);
  Mesh ref;
  const Mesh *m = r514_mesh(bo);
  return m && blockout::build(pieces, st, ref) && ref.positions.size() == m->positions.size() && ref.face_offsets == m->face_offsets &&
         ref.corner_verts == m->corner_verts && (ref.positions.empty() || std::memcmp(ref.positions.data(), m->positions.data(), ref.positions.size() * sizeof(Vec3)) == 0);
}

size_t r514_count(const GameObject &g, const char *type) {
  size_t n = 0;
  for (const auto &c : g.components) n += std::string(c->type_name()) == type;
  return n;
}

/* Edges with other than two faces (0: closed). */
size_t r514_open_edges(const Mesh &m) {
  std::map<std::pair<uint32_t, uint32_t>, int> n;
  for (size_t f = 0; f < m.face_count(); f++) {
    const uint32_t a0 = m.face_offsets[f], a1 = m.face_offsets[f + 1];
    for (uint32_t c = a0; c < a1; c++) {
      uint32_t u = m.corner_verts[c], v = m.corner_verts[c + 1 < a1 ? c + 1 : a0];
      if (u > v) std::swap(u, v);
      n[{u, v}]++;
    }
  }
  size_t bad = 0;
  for (const auto &e : n) bad += e.second != 2;
  return bad;
}

void r514_shapes() {
  test("blockout piece modifiers: a piece's shape is one closed outward surface - a Bevel gives 26 faces as on the Cube primitive; an Array of touching boxes is one long box, with gaps three boxes; workers off and on alike", [] {
    {
      BevelModifier bev;
      MeshPtr cube = primitives::cube(2.0f);
      Mesh c = *cube;
      run_modifier(bev, c);
      blockout::Piece numbers;
      numbers.shape = blockout::Shape::Box, numbers.size = Vec3(2.0f);
      Mesh k;
      blockout::piece_shape_mesh(numbers, k);
      const size_t kv = k.vert_count(), kf = k.face_count();
      run_modifier(bev, k);
      std::printf("    primitives::cube %zu points -> bevel %zu faces, %zu open edges; the kernel's box %zu points %zu faces -> bevel %zu faces, %zu open edges\n", cube->vert_count(),
                  c.face_count(), r514_open_edges(c), kv, kf, k.face_count(), r514_open_edges(k));
      /* Found while building round 514: the kernel's local box is not wound one way before it is placed, and a Bevel over it left four
       * corners open (22 faces, 12 open edges) - the Blockout then meshed the piece on the grid. piece_shape_mesh winds it outward. */
      CHECK(r514_open_edges(k) == 0 && k.face_count() == c.face_count());
    }
    for (const char *type : {"BevelModifier", "ArrayModifier", "ArrayGap"})
      for (int workers = 0; workers < 2; workers++) {
        blockout_piece_stack_cache_clear();
        Editor ed;
        ed.init_headless(900, 600);
        ed.set_async_jobs_for_test(workers != 0);
        GameObject *box = r514_box(ed);
        if (!box) continue;
        GameObject *bo = blockout_of_piece(*box);
        ed.command("select " + box->name);
        const bool gap = std::string(type) == "ArrayGap";
        ed.command(std::string("component ") + (gap ? "ArrayModifier" : type));
        if (gap) box->get<ArrayModifier>()->relative_offset = Vec3(1.5f, 0.0f, 0.0f);
        ed.command("blockout wait");
        blockout::Piece numbers = box->get<BlockoutPiece>()->to_piece(box->id, Mat4::identity());
        Mesh shape;
        blockout::piece_shape_mesh(numbers, shape);
        std::vector<const Component *> stack;
        blockout_piece_stack(*box, stack);
        blockout_piece_stack_run(stack, shape);
        const Mesh *m = r514_mesh(*bo);
        const BlockoutStatus &st = bo->get<Blockout>()->state.status;
        std::printf("    %s workers %d: stack mesh %zu faces, %zu open edges; result %zu faces, %zu points; refusal '%s'\n", type, workers, shape.face_count(),
                    r514_open_edges(shape), m ? m->face_count() : 0, m ? m->vert_count() : 0, st.refusal.c_str());
        CHECK(r514_open_edges(shape) == 0 && st.refusal.empty());
        const size_t want = std::string(type) == "BevelModifier" ? 26 : gap ? 18 : 6;  // (touching boxes are one box; apart, three)
        CHECK(m && m->face_count() == want);
      }
  });
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

  test("blockout piece modifiers: a Bevel on a piece bevels that piece in the Blockout's mesh; an Array of 3 makes it three long; a piece with no modifier builds as before, bit for bit", [] {
    blockout_piece_stack_cache_clear();
    Editor ed;
    ed.init_headless(900, 600);
    GameObject *box = r514_box(ed);
    CHECK(box != nullptr);
    if (!box) return;
    GameObject *bo = blockout_of_piece(*box);
    CHECK(bo && r514_numbers_build(*bo));  // (no modifier: the numbers' build)
    ed.mark_changed_for_test("the box");  // (the step the undo below goes back to)
    r514_settle(ed, 2);
    const size_t faces0 = r514_mesh(*bo)->face_count();
    const AABB b0 = r514_bounds(*bo);
    ed.command("select " + box->name);
    ed.command("component BevelModifier");
    box->get<BevelModifier>()->width = 0.2f;
    box->get<BevelModifier>()->limit_method = 0;  // (every edge)
    ed.command("blockout wait");
    {
      blockout::Piece numbers = box->get<BlockoutPiece>()->to_piece(box->id, Mat4::identity());
      Mesh shape;
      CHECK(blockout::piece_shape_mesh(numbers, shape));
      std::vector<const Component *> stack;
      blockout_piece_stack(*box, stack);
      blockout_piece_stack_run(stack, shape);
      std::printf("    the box's shape: %zu faces; bevelled by its stack: %zu faces, %zu points\n", (size_t)6, shape.face_count(), shape.vert_count());
    }
    const size_t faces1 = r514_mesh(*bo)->face_count();
    const AABB b1 = r514_bounds(*bo);
    std::printf("    a 2 m box: %zu faces; bevelled 0.2 m: %zu faces\n", faces0, faces1);
    CHECK(faces1 > faces0);
    CHECK(std::fabs(b1.max.x - b0.max.x) < 1e-3f && std::fabs(b1.min.y - b0.min.y) < 1e-3f);  // (a bevel keeps the box's bounds)
    CHECK(!r514_numbers_build(*bo));
    const std::string bo_name = bo->name;  // (the undo replaces the scene's objects: names, not pointers)
    ed.command("undo");
    ed.command("blockout wait");
    bo = ed.scene().find_by_name(bo_name);
    CHECK(bo && r514_numbers_build(*bo));  // (undo: the numbers again)
    GameObject *box2 = nullptr;
    ed.scene().for_each([&](GameObject &g) {
      if (g.get<BlockoutPiece>() && !box2) box2 = &g;
    });
    CHECK(box2 != nullptr);
    if (!box2 || !bo) return;
    ed.command("select " + box2->name);
    ed.command("component ArrayModifier");
    box2->get<ArrayModifier>()->count = 3;
    ed.command("blockout wait");
    const AABB b3 = r514_bounds(*bo);
    std::printf("    an Array of 3: x %.3f .. %.3f\n", b3.min.x, b3.max.x);
    CHECK(std::fabs((b3.max.x - b3.min.x) - 3.0f * (b0.max.x - b0.min.x)) < 1e-3f);
  });

  test("blockout piece modifiers: moving and turning a piece with a stack takes its mesh from the cache; changing a modifier's number or the piece's size evaluates it once", [] {
    blockout_piece_stack_cache_clear();
    Editor ed;
    ed.init_headless(900, 600);
    GameObject *box = r514_box(ed);
    CHECK(box != nullptr);
    if (!box) return;
    ed.command("select " + box->name);
    ed.command("component BevelModifier");
    ed.command("blockout wait");
    const uint64_t e0 = blockout_piece_stack_evaluations();
    for (int k = 1; k <= 5; k++) {
      box->set_local_position(Vec3(0.5f * (float)k, 0.0f, 0.0f));
      box->set_local_rotation(Quat::euler(Vec3(0.0f, 10.0f * (float)k, 0.0f)));
      ed.command("blockout wait");
    }
    CHECK(blockout_piece_stack_evaluations() == e0);  // (five moves, no evaluation)
    box->get<BevelModifier>()->width = 0.15f;
    ed.command("blockout wait");
    CHECK(blockout_piece_stack_evaluations() == e0 + 1);
    box->get<BlockoutPiece>()->size.x = 3.0f;
    ed.command("blockout wait");
    CHECK(blockout_piece_stack_evaluations() == e0 + 2);
  });

  test("blockout piece modifiers: with workers on, a drag of a stacked piece evaluates nothing and the mesh follows; the Blockout's status names the piece", [] {
    blockout_piece_stack_cache_clear();
    Editor ed;
    ed.init_headless(900, 600);
    ed.set_async_jobs_for_test(true);
    GameObject *box = r514_box(ed);
    CHECK(box != nullptr);
    if (!box) return;
    GameObject *bo = blockout_of_piece(*box);
    ed.command("select " + box->name);
    ed.command("component ArrayModifier");
    ed.command("blockout wait");
    const uint64_t e0 = blockout_piece_stack_evaluations();
    for (int k = 1; k <= 30; k++) {
      box->set_local_position(Vec3(0.1f * (float)k, 0.0f, 0.0f));
      ed.step_frame_headless();
    }
    ed.command("blockout wait");
    CHECK(blockout_piece_stack_evaluations() == e0);
    if (bo) std::printf("    after the drag: x %.3f .. %.3f, the piece at x %.3f\n", r514_bounds(*bo).min.x, r514_bounds(*bo).max.x, box->local().position.x);
    CHECK(bo && std::fabs(r514_bounds(*bo).min.x - (3.0f - 1.0f)) < 1e-3f);  // (the array's first box where the piece is now: x 3 - 1)
    std::vector<std::pair<std::string, bool>> lines;
    if (bo) blockout_piece_stack_status(*bo, lines);
    bool named = false;
    for (const auto &l : lines) named = named || (l.first.find(box->name) != std::string::npos && l.first.find("built as a mesh") != std::string::npos);
    CHECK(named);
  });

  test("blockout piece modifiers: a thousand changed stacks keep the cache of evaluated meshes under its caps; New Scene empties it; the status says how full it is", [] {
    blockout_piece_stack_cache_clear();
    Editor ed;
    ed.init_headless(900, 600);
    GameObject *box = r514_box(ed);
    CHECK(box != nullptr);
    if (!box) return;
    GameObject *bo = blockout_of_piece(*box);
    ed.command("select " + box->name);
    ed.command("component BevelModifier");
    size_t most = 0, most_bytes = 0;
    for (int k = 0; k < 1000; k++) {
      box->get<BevelModifier>()->width = 0.01f + 0.0003f * (float)k;  // (a new key each time)
      ed.command("blockout wait");
      size_t entries = 0, bytes = 0;
      blockout_piece_stack_cache_size(entries, bytes);
      most = std::max(most, entries), most_bytes = std::max(most_bytes, bytes);
    }
    std::printf("    1000 changed stacks: at most %zu entries (cap %zu), %.2f MB (cap %.0f MB)\n", most, blockout_piece_stack_cache_limit(false), (double)most_bytes / 1048576.0,
                (double)blockout_piece_stack_cache_limit(true) / 1048576.0);
    CHECK(most <= blockout_piece_stack_cache_limit(false));
    CHECK(most_bytes <= blockout_piece_stack_cache_limit(true));
    CHECK(most > 0);
    std::vector<std::pair<std::string, bool>> lines;
    if (bo) blockout_piece_stack_status(*bo, lines);
    bool said = false;
    for (const auto &l : lines) said = said || l.first.find("Evaluated piece stacks kept") != std::string::npos;
    CHECK(said);
    ed.command("newscene");
    size_t entries = 1, bytes = 1;
    blockout_piece_stack_cache_size(entries, bytes);
    CHECK(entries == 0 && bytes == 0);
  });
}

}  // namespace

void run_blockout_piece_modifiers_tests() {
  r514_shapes();
  tests_round514();
}
