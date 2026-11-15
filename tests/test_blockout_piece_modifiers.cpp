// SPDX-License-Identifier: GPL-2.0-or-later
// Modifiers on a Blockout piece (round 514, task 0496, ADR 0138): a piece's stack is ordinary modifier components on its object,
// shown in its Inspector, refused where a piece cannot take one, keyed by its numbers and stack but never its place.
#include "test_common.h"

#include "../src/scene/blockout_object.h"
#include "../src/scene/blockout_piece_stack.h"

#include <cstring>
#include <functional>
#include <limits>
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


/* ================================================================================================================================
 * Round 514, the test engineer's part (spec 0496's acceptance criteria, each with real events where it says so). Helpers are copies
 * of the ones in test_blockout_modifiers.cpp and test_blockout_join.cpp (those live in anonymous namespaces), prefixed r514e_.
 * ================================================================================================================================ */

GameObject *r514e_hand(Editor &ed, const char *name = "Blockout") {
  GameObject *g = ed.scene().create(name);
  g->add<Blockout>();
  g->add<MeshFilter>()->mesh = std::make_shared<Mesh>();
  g->add<MeshRenderer>();
  return g;
}

GameObject *r514e_piece(Editor &ed, GameObject &parent, const char *name, int shape, int op, Vec3 at, Vec3 size = Vec3(1.0f), float radius = 0.5f, float height = 1.0f) {
  GameObject *g = ed.scene().create(name, &parent);
  BlockoutPiece *p = g->add<BlockoutPiece>();
  p->shape = shape, p->operation = op, p->size = size, p->radius = radius, p->height = height;
  g->set_local_position(at);
  return g;
}

void r514e_empty(Editor &ed) {
  std::vector<GameObject *> roots;
  ed.scene().for_each([&](GameObject &g) {
    if (!g.parent) roots.push_back(&g);
  });
  ed.scene().destroy(roots);
  ed.clear_selection_for_test();
}

/* The Blockout's build landed and the Blockout's own stack (round 502) is adopted, workers on or off. */
void r514e_land(Editor &ed, GameObject &bo) {
  ed.command("blockout wait");
  for (int f = 0; f < 3000; f++) {
    ed.step_frame_headless();
    (void)bo.evaluated_mesh();
    if (!ed.eval_jobs_running_for_test() && !ed.blockout_jobs_running_for_test() && !bo.evaluation_pending(0)) break;
  }
  ed.step_frame_headless();
}

size_t r514e_mark() {
  std::vector<LogEntry> tmp;
  return Log::fetch((size_t)1 << 40, tmp);
}
std::string r514e_log_since(size_t mark) {
  std::vector<LogEntry> v;
  Log::fetch(mark, v);
  std::string s;
  for (const LogEntry &e : v) s += e.text + "\n";
  return s;
}

/* A key with the mouse over the Scene view, as a user presses it. */
void r514e_press(Editor &ed, int key, int mods = 0) {
  const Recti r = ed.scene_view_rect();
  ed.step_frame_headless({r514_ev(R514ET::MouseMove, r.x + r.w / 2, r.y + r.h / 2)});
  platform::Event d;
  d.type = R514ET::KeyDown, d.key = key, d.mods = mods;
  ed.step_frame_headless({d});
  platform::Event u = d;
  u.type = R514ET::KeyUp;
  ed.step_frame_headless({u});
  ed.step_frame_headless();
}

/* A face handle taken, pulled to the pixel of a world point and let go. */
bool r514e_pull(Editor &ed, const char *handle, Vec3 to, int mods = 0) {
  int hx = 0, hy = 0, tx = 0, ty = 0;
  if (!ed.blockout_handle_pixel_for_test(handle, hx, hy) || !ed.project_to_window(to, tx, ty)) return false;
  platform::Event e = r514_ev(R514ET::MouseMove, hx, hy);
  e.mods = mods;
  ed.step_frame_headless({e});
  e.type = R514ET::MouseDown;
  ed.step_frame_headless({e});
  if (!ed.blockout_dragging_for_test()) return false;
  e = r514_ev(R514ET::MouseMove, tx, ty), e.mods = mods;
  ed.step_frame_headless({e});
  e.type = R514ET::MouseUp;
  ed.step_frame_headless({e});
  r514_settle(ed, 3);
  return true;
}

GameObject *r514e_click_world(Editor &ed, Vec3 world) {
  int x = 0, y = 0;
  if (!ed.project_to_window(world, x, y)) return nullptr;
  ed.clear_selection_for_test();
  r514_click(ed, x, y);
  r34_steps(ed, 2);
  return ed.selection_size_for_test() == 1 ? ed.active_object_for_test() : nullptr;
}

/* A face as its corners' coordinates, starting at its least corner: what "the same face" means whatever the point order of the mesh. */
using R514Poly = std::vector<float>;
std::vector<R514Poly> r514e_faces(const Mesh &m, const std::function<bool(Vec3)> &keep) {
  std::vector<R514Poly> out;
  for (size_t f = 0; f < m.face_count(); f++) {
    const uint32_t a = m.face_offsets[f], b = m.face_offsets[f + 1];
    if (b <= a) continue;
    float cx = 0, cy = 0, cz = 0;
    for (uint32_t i = a; i < b; i++) {
      const Vec3 &p = m.positions[m.corner_verts[i]];
      cx += p.x, cy += p.y, cz += p.z;
    }
    const float n = (float)(b - a);
    if (!keep(Vec3(cx / n, cy / n, cz / n))) continue;
    auto less = [](const Vec3 &p, const Vec3 &q) { return p.x != q.x ? p.x < q.x : p.y != q.y ? p.y < q.y : p.z < q.z; };
    uint32_t first = a;
    for (uint32_t i = a + 1; i < b; i++)
      if (less(m.positions[m.corner_verts[i]], m.positions[m.corner_verts[first]])) first = i;
    R514Poly poly;
    for (uint32_t k = 0; k < b - a; k++) {
      const Vec3 &p = m.positions[m.corner_verts[a + (first - a + k) % (b - a)]];
      poly.push_back(p.x), poly.push_back(p.y), poly.push_back(p.z);
    }
    out.push_back(poly);
  }
  std::sort(out.begin(), out.end());
  return out;
}
std::vector<R514Poly> r514e_all_faces(const Mesh &m) {
  return r514e_faces(m, [](Vec3) { return true; });
}

/* Pieces of the surface the faces `take` make, joined by shared points. */
size_t r514e_components(const Mesh &m, const std::function<bool(size_t)> &take) {
  std::vector<uint32_t> parent(m.positions.size());
  for (size_t i = 0; i < parent.size(); i++) parent[i] = (uint32_t)i;
  std::function<uint32_t(uint32_t)> find = [&](uint32_t v) {
    while (parent[v] != v) v = parent[v] = parent[parent[v]];
    return v;
  };
  std::vector<char> used(parent.size(), 0);
  for (size_t f = 0; f < m.face_count(); f++) {
    if (!take(f)) continue;
    const uint32_t a = m.face_offsets[f], b = m.face_offsets[f + 1];
    for (uint32_t i = a; i < b; i++) {
      used[m.corner_verts[i]] = 1;
      parent[find(m.corner_verts[i])] = find(m.corner_verts[a]);
    }
  }
  size_t roots = 0;
  for (size_t i = 0; i < parent.size(); i++) roots += used[i] && find((uint32_t)i) == (uint32_t)i;
  return roots;
}

/* The mesh's points in a piece's own space. */
AABB r514e_local_bounds(const Mesh &m, const Mat4 &to_piece) {
  AABB b;
  for (const Vec3 &v : m.positions) b.add(to_piece.point(v));
  return b;
}

/* Structurally valid and finite, closed, outward. */
bool r514e_valid(const Mesh &m) {
  if (m.face_offsets.empty()) return m.corner_verts.empty();
  if (m.face_offsets.front() != 0 || m.face_offsets.back() != m.corner_verts.size()) return false;
  for (size_t f = 0; f + 1 < m.face_offsets.size(); f++)
    if (m.face_offsets[f + 1] < m.face_offsets[f]) return false;
  for (uint32_t v : m.corner_verts)
    if (v >= m.positions.size()) return false;
  for (const Vec3 &p : m.positions)
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return false;
  return true;
}
double r514e_volume(const Mesh &m) {
  double v = 0;
  for (size_t f = 0; f < m.face_count(); f++) {
    const uint32_t a = m.face_offsets[f], b = m.face_offsets[f + 1];
    const Vec3 p0 = m.positions[m.corner_verts[a]];
    for (uint32_t i = a + 1; i + 1 < b; i++) {
      const Vec3 p1 = m.positions[m.corner_verts[i]], p2 = m.positions[m.corner_verts[i + 1]];
      v += (double)dot(p0, cross(p1, p2)) / 6.0;
    }
  }
  return v;
}

bool r514e_bits(const Mesh &a, const Mesh &b) {
  return a.positions.size() == b.positions.size() && a.face_offsets == b.face_offsets && a.corner_verts == b.corner_verts &&
         (a.positions.empty() || std::memcmp(a.positions.data(), b.positions.data(), a.positions.size() * sizeof(Vec3)) == 0);
}

/* What the Blockout's pieces build from scratch: the stacks evaluated again with an emptied cache (so not a mesh the cache kept). */
bool r514e_scratch(const GameObject &bo, Mesh &out) {
  blockout_piece_stack_cache_clear();
  std::vector<blockout::Piece> pieces;
  blockout::Settings st;
  BlockoutSnapshotNotes notes;
  blockout_snapshot(bo, pieces, st, &notes);
  blockout_piece_stacks_apply(pieces, notes.surface);
  return blockout::build(pieces, st, out);
}

size_t r514e_modifiers(const GameObject &g) {
  size_t n = 0;
  for (const auto &c : g.components) n += c->is_modifier();
  return n;
}

GameObject *r514e_find_piece(Editor &ed, const char *name) {
  GameObject *g = ed.scene().find_by_name(name);
  return g && g->get<BlockoutPiece>() ? g : nullptr;
}

/* A column and a far box: the scene of the first criterion. */
struct R514eColumn {
  GameObject *bo = nullptr, *col = nullptr, *far_box = nullptr;
};
R514eColumn r514e_column_scene(Editor &ed) {
  r514e_empty(ed);
  R514eColumn s;
  s.bo = r514e_hand(ed);
  s.col = r514e_piece(ed, *s.bo, "Column", 1, 0, {0.0f, 0.75f, 0.0f}, Vec3(1.0f), 0.5f, 1.5f);
  s.far_box = r514e_piece(ed, *s.bo, "Far", 0, 0, {20.0f, 0.5f, 0.0f}, {2.0f, 1.0f, 2.0f});
  ed.mark_changed_for_test("r514e setup");
  r514e_land(ed, *s.bo);
  return s;
}
const auto r514e_far = [](Vec3 c) { return c.x > 10.0f; };
const auto r514e_near = [](Vec3 c) { return c.x <= 10.0f; };

void tests_round514_engineer_a() {
  /* ---- Criterion 1 ---- */
  test("blockout piece modifiers: Add Modifier > Bevel clicked on a selected column bevels it in the Blockout and leaves the far box face for face; `component BevelModifier` gives the same mesh; undo removes it in one step, redo brings it back", [] {
    blockout_piece_stack_cache_clear();
    std::vector<R514Poly> via_ui;
    {
      Editor ed;
      ed.init_headless(1500, 1400);
      R514eColumn s = r514e_column_scene(ed);
      CHECK(s.bo && s.col && s.far_box);
      if (!s.bo) return;
      const std::vector<R514Poly> far0 = r514e_faces(*r514_mesh(*s.bo), r514e_far);
      const size_t near0 = r514e_faces(*r514_mesh(*s.bo), r514e_near).size();
      CHECK(!far0.empty() && near0 > 0);
      ed.select_for_test(s.col->id, false);
      const size_t depth = ed.undo_depth_for_test();
      CHECK(r514_click_key(ed, "Add Modifier"));
      CHECK(r514_click_key(ed, "menu:Bevel"));
      r514e_land(ed, *s.bo);
      CHECK(r514_count(*s.col, "BevelModifier") == 1);
      CHECK(ed.undo_depth_for_test() == depth + 1);  // (one click, one step)
      const size_t near1 = r514e_faces(*r514_mesh(*s.bo), r514e_near).size();
      std::printf("    the column: %zu faces, bevelled %zu faces; the far box %zu faces\n", near0, near1, far0.size());
      CHECK(near1 > near0);                                                             // (the column is bevelled)
      CHECK(r514e_faces(*r514_mesh(*s.bo), r514e_far) == far0);                          // (the far box: the same faces, bit for bit)
      CHECK(!r514_numbers_build(*s.bo));
      via_ui = r514e_all_faces(*r514_mesh(*s.bo));
      const std::vector<R514Poly> far1 = far0;
      ed.command("undo");
      r514_settle(ed, 2);
      GameObject *bo = ed.scene().find_by_name("Blockout");
      GameObject *col = r514e_find_piece(ed, "Column");
      CHECK(bo && col);
      if (!bo || !col) return;
      r514e_land(ed, *bo);
      CHECK(r514_count(*col, "BevelModifier") == 0 && ed.undo_depth_for_test() == depth);  // (one undo took the whole add away)
      CHECK(r514_numbers_build(*bo));
      CHECK(r514e_faces(*r514_mesh(*bo), r514e_near).size() == near0 && r514e_faces(*r514_mesh(*bo), r514e_far) == far1);
      ed.command("redo");
      r514_settle(ed, 2);
      bo = ed.scene().find_by_name("Blockout");
      col = r514e_find_piece(ed, "Column");
      CHECK(bo && col);
      if (!bo || !col) return;
      r514e_land(ed, *bo);
      CHECK(r514_count(*col, "BevelModifier") == 1);
      CHECK(r514e_all_faces(*r514_mesh(*bo)) == via_ui);  // (redo: the same bevelled mesh)
    }
    {
      Editor ed;
      ed.init_headless(900, 600);
      R514eColumn s = r514e_column_scene(ed);
      if (!s.bo) return;
      ed.select_for_test(s.col->id, false);
      ed.command("component BevelModifier");
      r514e_land(ed, *s.bo);
      CHECK(r514_count(*s.col, "BevelModifier") == 1);
      CHECK(r514e_all_faces(*r514_mesh(*s.bo)) == via_ui);  // (the console and the Inspector make the same result)
    }
  });

  test("blockout piece modifiers: Ctrl+D on a stacked piece makes a piece with its own copy of the stack; the saved scene opens and builds the same bevelled mesh, evaluating the stack again", [] {
    blockout_piece_stack_cache_clear();
    Editor ed;
    ed.init_headless(900, 600);
    R514eColumn s = r514e_column_scene(ed);
    if (!s.bo) return;
    ed.select_for_test(s.col->id, false);
    ed.command("component BevelModifier");
    r514e_land(ed, *s.bo);
    const std::vector<R514Poly> bevelled = r514e_all_faces(*r514_mesh(*s.bo));
    const Mesh one_mesh = *r514_mesh(*s.bo);
    const AABB one_bounds = r514_bounds(*s.bo);
    r514e_press(ed, platform::KEY_D, platform::MOD_CTRL);
    std::vector<GameObject *> stacked;
    ed.scene().for_each([&](GameObject &g) {
      if (g.get<BlockoutPiece>() && g.get<BevelModifier>()) stacked.push_back(&g);
    });
    CHECK(stacked.size() == 2);
    if (stacked.size() != 2) return;
    CHECK(blockout_piece_stack_hash(*stacked[0]) == blockout_piece_stack_hash(*stacked[1]) && blockout_piece_stack_hash(*stacked[0]) != 0);
    CHECK(stacked[0]->get<BevelModifier>() != stacked[1]->get<BevelModifier>());
    stacked[1]->get<BevelModifier>()->width = 0.2f;  // (its own: the other keeps its width)
    CHECK(stacked[0]->get<BevelModifier>()->width == 0.05f);
    CHECK(blockout_piece_stack_hash(*stacked[0]) != blockout_piece_stack_hash(*stacked[1]));
    stacked[1]->get<BevelModifier>()->width = 0.05f;
    r514e_land(ed, *s.bo);
    /* The copy sits exactly on the column: a union of two coincident mesh pieces. Its faces need not be the single column's, bit for bit
     * (that is a boolean of two identical meshes), so what is pinned is: the same box, closed, the same volume - and, below, that the opened
     * scene gives this very mesh. */
    const Mesh &two = *r514_mesh(*s.bo);
    const AABB tb = r514_bounds(*s.bo);
    std::printf("    one bevelled column: %zu faces, volume %.4f; two on one place: %zu faces, volume %.4f, %zu open edges\n", bevelled.size(), r514e_volume(Mesh(one_mesh)),
                two.face_count(), r514e_volume(two), r514_open_edges(two));
    CHECK(r514_open_edges(two) == 0 && r514e_valid(two));
    CHECK(std::fabs(r514e_volume(two) - r514e_volume(Mesh(one_mesh))) < 1e-3 * std::fabs(r514e_volume(Mesh(one_mesh))));
    CHECK(std::fabs(tb.max.x - one_bounds.max.x) < 1e-4f && std::fabs(tb.min.y - one_bounds.min.y) < 1e-4f);
    const std::vector<R514Poly> two_faces = r514e_all_faces(two);
    const std::string dir = ed.resolve_asset_path_for_test("Assets/Scenes");
    fs::make_dirs(dir);
    const std::string path = fs::join(dir, "r514e_stacked.scene");
    fs::remove_file(path);
    ed.command("save " + path);
    for (int i = 0; i < 400 && (!fs::exists(path) || ed.save_job_running_for_test()); i++) ed.step_frame_headless();
    ed.command("newscene");
    const uint64_t evals = blockout_piece_stack_evaluations();
    CHECK(ed.open_scene_for_test(path));
    GameObject *loaded = nullptr;
    for (int i = 0; i < 400 && !loaded; i++) {
      ed.step_frame_headless();
      ed.scene().for_each([&](GameObject &g) {
        if (g.get<Blockout>()) loaded = &g;
      });
    }
    CHECK(loaded != nullptr);
    if (!loaded) return;
    size_t with = 0;
    ed.scene().for_each([&](GameObject &g) { with += g.get<BlockoutPiece>() && g.get<BevelModifier>() && g.get<BevelModifier>()->width == 0.05f; });
    CHECK(with == 2);
    r514e_land(ed, *loaded);
    CHECK(r514e_all_faces(*r514_mesh(*loaded)) == two_faces);  // (the same two pieces, built again from the file: the same bits)
    CHECK(blockout_piece_stack_evaluations() > evals);  // (the cache was emptied by the open: the stack ran again)
    fs::remove_file(path);
  });

  /* ---- Criterion 2 ---- */
  test("blockout piece modifiers: an Array of 10 on a turned column piece gives ten columns along the piece's own turned X, as one added result", [] {
    for (int workers = 0; workers < 2; workers++) {
      blockout_piece_stack_cache_clear();
      Editor ed;
      ed.init_headless(900, 600);
      ed.set_async_jobs_for_test(workers != 0);
      r514e_empty(ed);
      GameObject *bo = r514e_hand(ed);
      GameObject *col = r514e_piece(ed, *bo, "Col", 0, 0, {3.0f, 0.5f, 2.0f}, Vec3(1.0f));
      col->set_local_rotation(Quat::euler(Vec3(0.0f, 30.0f, 0.0f)));
      ed.select_for_test(col->id, false);
      ed.command("component ArrayModifier");
      ArrayModifier *ar = col->get<ArrayModifier>();
      ar->count = 10, ar->relative_offset = Vec3(2.0f, 0.0f, 0.0f), ar->constant_offset = Vec3(0.0f), ar->merge = false;
      r514e_land(ed, *bo);
      const Mesh *m = r514_mesh(*bo);
      CHECK(m != nullptr && r514e_valid(*m));
      if (!m) continue;
      const AABB lb = r514e_local_bounds(*m, blockout_piece_matrix(*col, *bo).inverse());
      std::printf("    workers %d: %zu separate columns; in the piece's own axes x %.3f .. %.3f, y %.3f .. %.3f, z %.3f .. %.3f\n", workers, r514e_components(*m, [](size_t) { return true; }),
                  lb.min.x, lb.max.x, lb.min.y, lb.max.y, lb.min.z, lb.max.z);
      CHECK(r514e_components(*m, [](size_t) { return true; }) == 10);
      CHECK_NEAR(lb.min.x, -0.5, 1e-3);
      CHECK_NEAR(lb.max.x, 18.5, 1e-3);  // (ten 1 m columns 2 m apart along the TURNED x)
      CHECK_NEAR(lb.min.z, -0.5, 1e-3);
      CHECK_NEAR(lb.max.z, 0.5, 1e-3);
      CHECK_NEAR(lb.min.y, -0.5, 1e-3);
      CHECK_NEAR(lb.max.y, 0.5, 1e-3);
      const AABB wb = r514_bounds(*bo);
      CHECK(wb.max.z - wb.min.z > 5.0f);  // (turned: in the world's axes the row is not square to z)
    }
  });

  test("blockout piece modifiers: an Array of 10 on a Subtract column piece cuts ten holes in the slab it passes through", [] {
    for (int workers = 0; workers < 2; workers++) {
      blockout_piece_stack_cache_clear();
      Editor ed;
      ed.init_headless(900, 600);
      ed.set_async_jobs_for_test(workers != 0);
      r514e_empty(ed);
      GameObject *bo = r514e_hand(ed);
      r514e_piece(ed, *bo, "Slab", 0, 0, {5.4f, 0.0f, 0.0f}, {13.0f, 1.0f, 3.0f});
      GameObject *col = r514e_piece(ed, *bo, "Hole", 1, 1, {0.0f, 0.0f, 0.0f}, Vec3(1.0f), 0.3f, 3.0f);
      ed.select_for_test(col->id, false);
      ed.command("component ArrayModifier");
      ArrayModifier *ar = col->get<ArrayModifier>();
      ar->count = 10, ar->relative_offset = Vec3(2.0f, 0.0f, 0.0f), ar->constant_offset = Vec3(0.0f), ar->merge = false;
      r514e_land(ed, *bo);
      const Mesh *m = r514_mesh(*bo);
      const blockout::Faces &faces = bo->get<Blockout>()->state.faces;
      CHECK(m != nullptr && r514e_valid(*m) && r514_open_edges(*m) == 0);
      if (!m || faces.piece.size() != m->face_count()) {
        CHECK(false);
        continue;
      }
      const size_t holes = r514e_components(*m, [&](size_t f) { return faces.piece[f] == col->id; });
      std::printf("    workers %d: %zu faces, %zu hole walls apart (ten wanted)\n", workers, m->face_count(), holes);
      CHECK(holes == 10);
      CHECK(r514e_volume(*m) > 0.0 && r514e_volume(*m) < 13.0 * 3.0 - 10.0 * 3.1415926 * 0.09 + 0.2);  // (the slab less ten bores: 39 - 2.83 m3 at most)
    }
  });

  test("blockout piece modifiers: a Mirror on a turned wing piece (an outline off its origin) mirrors across the piece's own plane, not the world's", [] {
    blockout_piece_stack_cache_clear();
    Editor ed;
    ed.init_headless(900, 600);
    r514e_empty(ed);
    GameObject *bo = r514e_hand(ed);
    GameObject *wing = r514e_piece(ed, *bo, "Wing", 6, 0, {10.0f, 0.0f, 5.0f}, Vec3(1.0f), 0.5f, 1.0f);
    wing->get<BlockoutPiece>()->outline = {Vec2(1.0f, 0.0f), Vec2(3.0f, 0.0f), Vec2(3.0f, 1.0f), Vec2(1.0f, 1.0f)};
    wing->set_local_rotation(Quat::euler(Vec3(0.0f, 40.0f, 0.0f)));
    r514e_land(ed, *bo);
    const Mesh one = *r514_mesh(*bo);
    CHECK(one.face_count() > 0 && r514e_components(one, [](size_t) { return true; }) == 1);
    ed.select_for_test(wing->id, false);
    ed.command("component MirrorModifier");
    r514e_land(ed, *bo);
    const Mesh *m = r514_mesh(*bo);
    CHECK(m && r514e_valid(*m) && r514_open_edges(*m) == 0);
    if (!m) return;
    const Mat4 inv = blockout_piece_matrix(*wing, *bo).inverse();
    const AABB lb = r514e_local_bounds(*m, inv);
    std::printf("    wing alone %zu faces; mirrored %zu faces in %zu parts; piece-space x %.3f .. %.3f\n", one.face_count(), m->face_count(),
                r514e_components(*m, [](size_t) { return true; }), lb.min.x, lb.max.x);
    CHECK(r514e_components(*m, [](size_t) { return true; }) == 2);
    CHECK_NEAR(lb.min.x, -3.0, 1e-3);
    CHECK_NEAR(lb.max.x, 3.0, 1e-3);
    size_t unmatched = 0;
    for (const Vec3 &v : m->positions) {
      const Vec3 p = inv.point(v);
      bool found = false;
      for (const Vec3 &w : m->positions) {
        const Vec3 q = inv.point(w);
        found = found || (std::fabs(q.x + p.x) < 2e-3f && std::fabs(q.y - p.y) < 2e-3f && std::fabs(q.z - p.z) < 2e-3f);
      }
      unmatched += !found;
    }
    CHECK(unmatched == 0);  // (every point has its twin across the piece's own x = 0 plane)
  });
}

/* A stack whose result is not a closed surface, found among candidates by running it over a box: the modifier, the box's own mesh. */
struct R514eOpen {
  const char *name;
  std::function<std::shared_ptr<Component>()> make;
};

template <class T, class F>
std::shared_ptr<Component> r514e_mod(F set) {
  auto m = std::make_shared<T>();
  set(*m);
  return m;
}

std::vector<R514eOpen> r514e_open_candidates() {
  return {{"Screw 90 degrees, 4 steps, no merge", [] { return r514e_mod<ScrewModifier>([](ScrewModifier &m) { m.angle = 90.0f, m.steps = 4, m.merge = false; }); }},
          {"Solidify without its rim", [] { return r514e_mod<SolidifyModifier>([](SolidifyModifier &m) { m.fill_rim = false, m.thickness = 0.2f; }); }},
          {"Decimate to 2 percent", [] { return r514e_mod<DecimateModifier>([](DecimateModifier &m) { m.ratio = 0.02f; }); }},
          {"Decimate to nothing", [] { return r514e_mod<DecimateModifier>([](DecimateModifier &m) { m.ratio = 0.0f; }); }},
          {"Wireframe keeping the original", [] { return r514e_mod<WireframeModifier>([](WireframeModifier &m) { m.replace_original = false; }); }},
          {"Wireframe", [] { return r514e_mod<WireframeModifier>([](WireframeModifier &) {}); }},
          {"Weld at 2 m", [] { return r514e_mod<WeldModifier>([](WeldModifier &m) { m.distance = 2.0f; }); }},
          {"Mirror on a centred box", [] { return r514e_mod<MirrorModifier>([](MirrorModifier &) {}); }}};
}

void tests_round514_engineer_b() {
  /* ---- Criterion 3 ---- */
  test("blockout piece modifiers: turning or moving a stacked piece evaluates nothing; a modifier's field set by the console, or a number of the piece, evaluates it once", [] {
    blockout_piece_stack_cache_clear();
    Editor ed;
    ed.init_headless(900, 600);
    R514eColumn s = r514e_column_scene(ed);
    if (!s.bo) return;
    ed.select_for_test(s.col->id, false);
    ed.command("component BevelModifier");
    r514e_land(ed, *s.bo);
    const uint64_t e0 = blockout_piece_stack_evaluations();
    for (int k = 1; k <= 6; k++) {  // (turned and moved, each a new build)
      s.col->set_local_rotation(Quat::euler(Vec3(0.0f, 15.0f * (float)k, 0.0f)));
      s.col->set_local_position(Vec3(0.3f * (float)k, 0.75f, 0.1f * (float)k));
      r514e_land(ed, *s.bo);
    }
    CHECK(blockout_piece_stack_evaluations() == e0);
    s.far_box->set_local_position(Vec3(22.0f, 0.5f, 1.0f));  // (another piece moving: the column's stack is not asked either)
    r514e_land(ed, *s.bo);
    CHECK(blockout_piece_stack_evaluations() == e0);
    ed.command("set BevelModifier.Width 0.11");
    r514e_land(ed, *s.bo);
    CHECK(s.col->get<BevelModifier>()->width == 0.11f);
    CHECK(blockout_piece_stack_evaluations() == e0 + 1);
    ed.command("set BevelModifier.Segments 2");
    r514e_land(ed, *s.bo);
    CHECK(blockout_piece_stack_evaluations() == e0 + 2);
    s.col->get<BlockoutPiece>()->radius = 0.4f;
    r514e_land(ed, *s.bo);
    CHECK(blockout_piece_stack_evaluations() == e0 + 3);
    s.col->get<BevelModifier>()->enabled = false;  // (off: no stack, the numbers' build; nothing evaluated)
    r514e_land(ed, *s.bo);
    CHECK(blockout_piece_stack_evaluations() == e0 + 3 && r514_numbers_build(*s.bo));
    s.col->get<BevelModifier>()->enabled = true;  // (back on: the same key as before, so the cache gives it)
    r514e_land(ed, *s.bo);
    CHECK(blockout_piece_stack_evaluations() == e0 + 3 && !r514_numbers_build(*s.bo));
  });

  test("blockout piece modifiers: with workers on, dragging the stacked piece and dragging another piece evaluate nothing and build nothing on the main thread; a changed number evaluates once, on a worker; the mesh ends equal to a from-scratch build", [] {
    blockout_piece_stack_cache_clear();
    Editor ed;
    ed.init_headless(900, 600);
    ed.set_async_jobs_for_test(true);
    R514eColumn s = r514e_column_scene(ed);
    if (!s.bo) return;
    ed.select_for_test(s.col->id, false);
    ed.command("component BevelModifier");
    r514e_land(ed, *s.bo);
    const uint64_t e0 = blockout_piece_stack_evaluations(), main0 = ed.blockout_main_builds_for_test();
    for (int k = 1; k <= 40; k++) {  // (a drag of the column, one frame per move)
      s.col->set_local_position(Vec3(0.1f * (float)k, 0.75f, 0.0f));
      ed.step_frame_headless();
    }
    r514e_land(ed, *s.bo);
    for (int k = 1; k <= 40; k++) {  // (a drag of the far box)
      s.far_box->set_local_position(Vec3(20.0f + 0.1f * (float)k, 0.5f, 0.0f));
      ed.step_frame_headless();
    }
    r514e_land(ed, *s.bo);
    CHECK(blockout_piece_stack_evaluations() == e0);
    CHECK(ed.blockout_main_builds_for_test() == main0);
    s.col->get<BevelModifier>()->width = 0.09f;
    ed.step_frame_headless();
    r514e_land(ed, *s.bo);
    CHECK(blockout_piece_stack_evaluations() == e0 + 1);
    CHECK(ed.blockout_main_builds_for_test() == main0);  // (the evaluation was the worker's)
    Mesh ref;
    const bool ok = r514e_scratch(*s.bo, ref);
    CHECK(ok && r514e_bits(*r514_mesh(*s.bo), ref));
  });

  test("blockout piece modifiers: two Blockouts with the same stacked piece share one evaluation (the key is the numbers and the stack, never the Blockout or the place)", [] {
    blockout_piece_stack_cache_clear();
    Editor ed;
    ed.init_headless(900, 600);
    r514e_empty(ed);
    GameObject *a = r514e_hand(ed, "A"), *b = r514e_hand(ed, "B");
    GameObject *pa = r514e_piece(ed, *a, "Pa", 0, 0, {0.0f, 0.5f, 0.0f}, {2.0f, 1.0f, 2.0f});
    GameObject *pb = r514e_piece(ed, *b, "Pb", 0, 0, {7.0f, 0.5f, 3.0f}, {2.0f, 1.0f, 2.0f});
    pb->set_local_rotation(Quat::euler(Vec3(0.0f, 20.0f, 0.0f)));
    for (GameObject *p : {pa, pb}) {
      ed.select_for_test(p->id, false);
      ed.command("component BevelModifier");
    }
    const uint64_t e0 = blockout_piece_stack_evaluations();
    r514e_land(ed, *a);
    r514e_land(ed, *b);
    CHECK(blockout_piece_stack_evaluations() == e0 + 1);
    size_t entries = 0, bytes = 0;
    blockout_piece_stack_cache_size(entries, bytes);
    CHECK(entries == 1 && bytes > 0);
    Mesh ra, rb;
    CHECK(r514e_scratch(*a, ra) && r514e_scratch(*b, rb));
    CHECK(r514e_bits(*r514_mesh(*a), ra) && r514e_bits(*r514_mesh(*b), rb));
  });

  /* ---- Criterion 4: refusals ---- */
  test("blockout piece modifiers: every modifier a piece does not take is off in its Add Modifier list - clicking it adds nothing - while the ones it takes add (tooltips are not readable from a test)", [] {
    Editor ed;
    ed.init_headless(1500, 1400);
    GameObject *box = r514_box(ed);
    CHECK(box != nullptr);
    if (!box) return;
    ed.select_for_test(box->id, false);
    const char *const refused[][2] = {{"Boolean", "BooleanModifier"},   {"Curve", "CurveModifier"},          {"Lattice", "LatticeModifier"},
                                      {"Mesh Deform", "MeshDeformModifier"}, {"Armature (Skin)", "SkinModifier"}, {"Smooth Normals", "SmoothNormalsModifier"},
                                      {"Normal Transfer", "NormalTransferModifier"}};
    for (const auto &r : refused) {
      CHECK(r514_click_key(ed, "Add Modifier"));
      CHECK(r514_click_key(ed, std::string("menu:") + r[0]));
      ed.step_frame_headless({r514_key(platform::KEY_ESCAPE)});
      r514_settle(ed, 2);
      std::printf("    %s: %zu modifiers on the piece after the click; the reason the code gives: %s\n", r[0], r514e_modifiers(*box), blockout_piece_modifier_refusal(r[1]).c_str());
      CHECK(r514e_modifiers(*box) == 0);
      CHECK(!blockout_piece_modifier_refusal(r[1]).empty());
    }
    size_t want = 0;
    for (const char *label : {"Weld", "Array", "Mirror", "Decimate"}) {
      CHECK(r514_click_key(ed, "Add Modifier"));
      CHECK(r514_click_key(ed, std::string("menu:") + label));
      CHECK(r514e_modifiers(*box) == ++want);
    }
  });

  test("blockout piece modifiers: a stack put by the console on a room, a door, a window or an Intersect piece does nothing, the mesh is the numbers' build, and `blockout info` names the piece and says why", [] {
    Editor ed;
    ed.init_headless(900, 600);
    GameObject *box = r514_box(ed);
    CHECK(box != nullptr);
    if (!box) return;
    GameObject *bo = blockout_of_piece(*box);
    BlockoutPiece *bp = box->get<BlockoutPiece>();
    ed.select_for_test(box->id, false);
    ed.command("component BevelModifier");
    ed.command("component ArrayModifier");
    struct Kind {
      const char *what, *why;
      std::function<void(BlockoutPiece &)> set;
    };
    const std::vector<Kind> kinds = {{"room", "a room's walls", [](BlockoutPiece &p) { p.room = true; }},
                                     {"door", "a door or window", [](BlockoutPiece &p) { p.opening = 1, p.operation = 1; }},
                                     {"window", "a door or window", [](BlockoutPiece &p) { p.opening = 2, p.operation = 1; }},
                                     {"intersect", "Intersect piece", [](BlockoutPiece &p) { p.operation = 2; }}};
    for (const Kind &k : kinds) {
      const BlockoutPiece keep = *bp;
      k.set(*bp);
      ed.command("blockout wait");
      CHECK(r514_numbers_build(*bo));  // (inert: built from its numbers, bit for bit)
      const size_t mark = r514e_mark();
      ed.command("blockout info");
      const std::string log = r514e_log_since(mark);
      std::printf("    %s -> %s", k.what, log.find("do nothing") != std::string::npos ? log.substr(log.find("'")).c_str() : "(no line)\n");
      CHECK(log.find("'" + box->name + "': its modifiers do nothing") != std::string::npos && log.find(k.why) != std::string::npos);
      CHECK(log.find("is built as a mesh") == std::string::npos);
      *bp = keep;
    }
    ed.command("blockout wait");
    CHECK(!r514_numbers_build(*bo));  // (an ordinary Add box again: the stack is live)
  });

  test("blockout piece modifiers: a stack whose result is not a closed surface - found by running candidates over a box - is built as the kernel's mesh-piece rules decide: valid result on every path, and on the exact path the piece is skipped and the status names it", [] {
    const std::vector<R514eOpen> cands = r514e_open_candidates();
    size_t open_found = 0;
    for (const R514eOpen &c : cands) {
      blockout::Piece numbers;
      numbers.shape = blockout::Shape::Box, numbers.size = Vec3(2.0f);
      Mesh shape;
      blockout::piece_shape_mesh(numbers, shape);
      std::vector<std::shared_ptr<Component>> hold = {c.make()};
      std::vector<const Component *> stack = {hold[0].get()};
      blockout_piece_stack_run(stack, shape);
      const size_t open = r514_open_edges(shape);
      std::printf("    %s: %zu faces, %zu edges not on two faces\n", c.name, shape.face_count(), open);
      if (!open || shape.face_count() == 0) continue;
      open_found++;
      for (int method = 0; method < 3; method++) {  // (Auto, Exact, Field)
        if (method == 1 && !blockout::crisp_available()) continue;
        blockout_piece_stack_cache_clear();
        Editor ed;
        ed.init_headless(900, 600);
        r514e_empty(ed);
        GameObject *bo = r514e_hand(ed);
        GameObject *p = r514e_piece(ed, *bo, "Piece", 0, 0, {0.0f, 1.0f, 0.0f}, Vec3(2.0f));
        bo->get<Blockout>()->method = method;
        ed.select_for_test(p->id, false);
        p->add_component(hold[0]->clone());
        r514e_land(ed, *bo);
        const Mesh *m = r514_mesh(*bo);
        const BlockoutStatus &st = bo->get<Blockout>()->state.status;
        const size_t mark = r514e_mark();
        ed.command("blockout info");
        const std::string log = r514e_log_since(mark);
        std::printf("      method %d: %zu faces, %zu open edges, %zu skipped, refusal '%s'\n", method, m ? m->face_count() : 0, m ? r514_open_edges(*m) : 0, st.skipped.size(), st.refusal.c_str());
        CHECK(m && r514e_valid(*m));
        if (m) CHECK(r514_open_edges(*m) == 0);  // (whatever the path decided, the Blockout's own mesh is closed)
        if (method == 1) {  // (the exact path: an open mesh piece is skipped, and the status says which piece and why)
          CHECK(!st.skipped.empty() || !st.refusal.empty());
          CHECK(log.find("Skipped '" + p->name + "'") != std::string::npos || log.find("Refused:") != std::string::npos);
        }
      }
    }
    CHECK(open_found > 0);  // (if no candidate opens the surface, the spec's example is not reachable and the test needs a new one)
  });

  test("blockout piece modifiers: NaN and infinity in a modifier's fields leave a valid, closed, finite Blockout (the piece stays as its numbers or the stack's finite result)", [] {
    blockout_piece_stack_cache_clear();
    const float nan = std::nanf(""), inf = std::numeric_limits<float>::infinity();
    struct Case {
      const char *what;
      std::function<std::shared_ptr<Component>()> make;
    };
    const std::vector<Case> cases = {
        {"Bevel width NaN", [&] { return r514e_mod<BevelModifier>([&](BevelModifier &m) { m.width = nan; }); }},
        {"Bevel width inf", [&] { return r514e_mod<BevelModifier>([&](BevelModifier &m) { m.width = inf; }); }},
        {"Array offset NaN", [&] { return r514e_mod<ArrayModifier>([&](ArrayModifier &m) { m.relative_offset = Vec3(nan, 0.0f, 0.0f); }); }},
        {"Array constant offset inf", [&] { return r514e_mod<ArrayModifier>([&](ArrayModifier &m) { m.constant_offset = Vec3(inf, 0.0f, 0.0f); }); }},
        {"Solidify thickness inf", [&] { return r514e_mod<SolidifyModifier>([&](SolidifyModifier &m) { m.thickness = inf; }); }},
        {"Displace strength NaN", [&] { return r514e_mod<DisplaceModifier>([&](DisplaceModifier &m) { m.strength = nan; }); }},
        {"Decimate ratio NaN", [&] { return r514e_mod<DecimateModifier>([&](DecimateModifier &m) { m.ratio = nan; }); }},
        {"Weld distance NaN", [&] { return r514e_mod<WeldModifier>([&](WeldModifier &m) { m.distance = nan; }); }},
        {"Wireframe thickness NaN", [&] { return r514e_mod<WireframeModifier>([&](WireframeModifier &m) { m.thickness = nan; }); }},
        {"Screw angle NaN", [&] { return r514e_mod<ScrewModifier>([&](ScrewModifier &m) { m.angle = nan; }); }},
        {"Cast factor inf", [&] { return r514e_mod<CastModifier>([&](CastModifier &m) { m.factor = inf; }); }},
        {"Simple Deform factor NaN", [&] { return r514e_mod<SimpleDeformModifier>([&](SimpleDeformModifier &m) { m.mode = 2, m.factor = nan; }); }},
        {"Smooth factor inf", [&] { return r514e_mod<SmoothModifier>([&](SmoothModifier &m) { m.factor = inf; }); }},
        {"Mirror merge distance NaN", [&] { return r514e_mod<MirrorModifier>([&](MirrorModifier &m) { m.merge_distance = nan; }); }},
        {"Displace strength inf, noise scale NaN", [&] { return r514e_mod<DisplaceModifier>([&](DisplaceModifier &m) { m.strength = inf, m.noise_scale = nan; }); }}};
    for (int workers = 0; workers < 2; workers++) {
      Editor ed;
      ed.init_headless(900, 600);
      ed.set_async_jobs_for_test(workers != 0);
      r514e_empty(ed);
      GameObject *bo = r514e_hand(ed);
      GameObject *p = r514e_piece(ed, *bo, "Piece", 0, 0, {0.0f, 1.0f, 0.0f}, Vec3(2.0f));
      GameObject *p2 = r514e_piece(ed, *bo, "Ball", 2, 1, {1.0f, 1.0f, 0.0f}, Vec3(1.0f), 0.5f);
      (void)p2;
      for (const Case &c : cases) {
        p->add_component(c.make()->clone());
        r514e_land(ed, *bo);
        const Mesh *m = r514_mesh(*bo);
        const bool ok = m && r514e_valid(*m) && r514_open_edges(*m) == 0;
        if (!ok) std::printf("    workers %d, %s: INVALID (faces %zu)\n", workers, c.what, m ? m->face_count() : 0);
        CHECK(ok);
        for (auto it = p->components.begin(); it != p->components.end(); ++it)
          if ((*it)->is_modifier()) {
            p->components.erase(it);
            break;
          }
      }
    }
  });

  /* ---- Criterion 5 ---- */
  test("blockout piece modifiers: rooms with doors, stairs, an arch, an outline and Blend radii build bit for bit as the numbers do, with and without an unrelated stacked piece far away - the unstacked clusters are the same, face for face", [] {
    for (int workers = 0; workers < 2; workers++) {
      blockout_piece_stack_cache_clear();
      Editor ed;
      ed.init_headless(900, 600);
      ed.set_async_jobs_for_test(workers != 0);
      r514e_empty(ed);
      GameObject *bo = r514e_hand(ed);
      GameObject *room = r514e_piece(ed, *bo, "Room", 0, 0, {0.0f, 1.5f, 0.0f}, {4.0f, 3.0f, 4.0f});
      room->get<BlockoutPiece>()->room = true;
      GameObject *door = r514e_piece(ed, *bo, "Door", 0, 1, {2.0f, 0.0f, 0.0f}, Vec3(1.0f));
      door->get<BlockoutPiece>()->opening = 1;
      GameObject *stairs = r514e_piece(ed, *bo, "Stairs", 4, 0, {10.0f, 1.0f, 0.0f}, {2.0f, 2.0f, 3.0f});
      stairs->get<BlockoutPiece>()->steps = 4;
      GameObject *arch = r514e_piece(ed, *bo, "Arch", 5, 0, {15.0f, 0.0f, 0.0f});
      arch->get<BlockoutPiece>()->radius = 1.2f, arch->get<BlockoutPiece>()->inner_radius = 0.6f, arch->get<BlockoutPiece>()->segments = 8, arch->get<BlockoutPiece>()->height = 0.5f;
      GameObject *outline = r514e_piece(ed, *bo, "Outline", 6, 0, {20.0f, 0.0f, 0.0f}, Vec3(1.0f), 0.5f, 1.5f);
      outline->get<BlockoutPiece>()->outline = {Vec2(-1, -1), Vec2(1, -1), Vec2(1, 0), Vec2(0, 0), Vec2(0, 1), Vec2(-1, 1)};
      GameObject *ba = r514e_piece(ed, *bo, "BlendA", 0, 0, {25.0f, 1.0f, 0.0f}, Vec3(2.0f));
      GameObject *bb = r514e_piece(ed, *bo, "BlendB", 2, 0, {26.4f, 1.5f, 0.0f}, Vec3(1.0f), 0.8f);
      ba->get<BlockoutPiece>()->blend = 0.15f, bb->get<BlockoutPiece>()->blend = 0.15f;
      ed.mark_changed_for_test("r514e unstacked scene");
      r514e_land(ed, *bo);
      CHECK(r514_numbers_build(*bo));
      const auto left = [](Vec3 c) { return c.x < 40.0f; };
      const std::vector<R514Poly> before = r514e_faces(*r514_mesh(*bo), left);
      const size_t all0 = r514_mesh(*bo)->face_count();
      CHECK(before.size() == all0 && all0 > 20);
      GameObject *col = r514e_piece(ed, *bo, "FarColumn", 1, 0, {60.0f, 0.75f, 0.0f}, Vec3(1.0f), 0.5f, 1.5f);
      r514e_land(ed, *bo);
      CHECK(r514e_faces(*r514_mesh(*bo), left) == before);  // (an unstacked piece added far away: the others as they were)
      ed.select_for_test(col->id, false);
      ed.command("component BevelModifier");
      r514e_land(ed, *bo);
      const size_t far_faces = r514e_faces(*r514_mesh(*bo), [](Vec3 c) { return c.x >= 40.0f; }).size();
      std::printf("    workers %d: %zu faces in the building, the far column bevelled has %zu\n", workers, all0, far_faces);
      CHECK(r514e_faces(*r514_mesh(*bo), left) == before);  // (and with a stack on it)
      CHECK(far_faces > 26);                                // (a bevelled cylinder of 24 sides has more than its 26 faces)
      col->get<BevelModifier>()->enabled = false;
      r514e_land(ed, *bo);
      CHECK(r514_numbers_build(*bo));  // (the stack off: bit for bit the numbers again)
    }
  });

  /* ---- Criterion 6 ---- */
  test("blockout piece modifiers: the Blockout's own Mirror runs after the piece stacks - it mirrors the bevelled column, and the result is the Blockout's stack run by hand over the bevelled build", [] {
    for (int workers = 0; workers < 2; workers++) {
      blockout_piece_stack_cache_clear();
      Editor ed;
      ed.init_headless(900, 600);
      ed.set_async_jobs_for_test(workers != 0);
      r514e_empty(ed);
      GameObject *bo = r514e_hand(ed);
      GameObject *col = r514e_piece(ed, *bo, "Column", 1, 0, {3.0f, 0.75f, 0.0f}, Vec3(1.0f), 0.5f, 1.5f);
      ed.select_for_test(col->id, false);
      ed.command("component BevelModifier");
      r514e_land(ed, *bo);
      const size_t plain = [&] {
        Mesh m;
        r514e_scratch(*bo, m);
        return m.face_count();
      }();
      ed.select_for_test(bo->id, false);
      ed.command("component MirrorModifier");
      r514e_land(ed, *bo);
      const Mesh *base = bo->get<MeshFilter>()->mesh.get();
      const Mesh *ev = bo->evaluated_mesh();
      CHECK(base && ev && ev != base);
      if (!base || !ev) continue;
      CHECK(base->face_count() == plain && !r514_numbers_build(*bo));  // (the base is the bevelled column)
      CHECK(ev->face_count() == 2 * base->face_count());              // (the bevelled column twice, across the Blockout's plane)
      Mesh by_hand = *base;
      for (const auto &c : bo->components)
        if (c->is_modifier() && c->enabled) run_modifier(*c, by_hand);
      CHECK(r514e_bits(*ev, by_hand));
      const AABB b = ev->render_mesh().bounds;
      CHECK(b.min.x < -3.4f && b.max.x > 3.4f);
      CHECK(r514_count(*col, "BevelModifier") == 1 && r514_count(*bo, "MirrorModifier") == 1 && r514e_modifiers(*col) == 1);  // (each stack on its own object)
    }
  });
}

size_t r514e_tris(const Mesh &m) {
  size_t n = 0;
  for (size_t f = 0; f < m.face_count(); f++) n += m.face_size(f) >= 3 ? m.face_size(f) - 2 : 0;
  return n;
}

/* ---- the shapes and modifiers of criterion 9, at the kernel's door: the stack as `blockout_piece_stacks_apply` runs it ---- */
struct R514eShape {
  const char *name;
  std::function<void(blockout::Piece &)> set;
};
std::vector<R514eShape> r514e_shapes() {
  return {{"box", [](blockout::Piece &p) { p.shape = blockout::Shape::Box, p.size = Vec3(2.0f); }},
          {"cylinder", [](blockout::Piece &p) { p.shape = blockout::Shape::Cylinder, p.radius = 0.8f, p.height = 2.0f, p.segments = 16; }},
          {"sphere", [](blockout::Piece &p) { p.shape = blockout::Shape::Sphere, p.radius = 0.9f, p.segments = 16, p.rings = 8; }},
          {"wedge", [](blockout::Piece &p) { p.shape = blockout::Shape::Wedge, p.size = Vec3(2.0f, 1.5f, 2.0f); }},
          {"stairs", [](blockout::Piece &p) { p.shape = blockout::Shape::Stairs, p.size = Vec3(2.0f, 2.0f, 3.0f), p.steps = 4; }},
          {"arch", [](blockout::Piece &p) { p.shape = blockout::Shape::Arch, p.radius = 1.2f, p.inner_radius = 0.6f, p.angle = 180.0f, p.segments = 6, p.height = 0.6f; }},
          {"outline", [](blockout::Piece &p) {
             p.shape = blockout::Shape::Outline, p.height = 1.5f;
             p.outline = {Vec2(-1, -1), Vec2(1, -1), Vec2(1, 0), Vec2(0, 0), Vec2(0, 1), Vec2(-1, 1)};
           }},
          {"mesh piece", [](blockout::Piece &p) { p.shape = blockout::Shape::Mesh, p.mesh = primitives::cube(1.8f), p.mesh_key = 77; }}};
}
struct R514eMod {
  const char *name;
  std::function<std::shared_ptr<Component>()> make;
};
std::vector<R514eMod> r514e_mods() {
  return {{"Bevel", [] { return r514e_mod<BevelModifier>([](BevelModifier &m) { m.width = 0.1f, m.limit_method = 0; }); }},
          {"Array", [] { return r514e_mod<ArrayModifier>([](ArrayModifier &m) { m.count = 3, m.relative_offset = Vec3(1.3f, 0.0f, 0.0f); }); }},
          {"Mirror", [] { return r514e_mod<MirrorModifier>([](MirrorModifier &) {}); }},
          {"Solidify", [] { return r514e_mod<SolidifyModifier>([](SolidifyModifier &m) { m.thickness = 0.1f; }); }},
          {"Subdivision 1", [] { return r514e_mod<SubdivisionSurface>([](SubdivisionSurface &m) { m.levels = 1; }); }},
          {"Displace", [] { return r514e_mod<DisplaceModifier>([](DisplaceModifier &m) { m.strength = 0.1f; }); }},
          {"Triangulate", [] { return r514e_mod<TriangulateModifier>([](TriangulateModifier &) {}); }},
          {"Weld", [] { return r514e_mod<WeldModifier>([](WeldModifier &m) { m.distance = 0.01f; }); }},
          {"Smooth", [] { return r514e_mod<SmoothModifier>([](SmoothModifier &m) { m.factor = 0.5f, m.iterations = 2; }); }},
          {"Cast", [] { return r514e_mod<CastModifier>([](CastModifier &m) { m.factor = 0.5f; }); }},
          {"Simple Deform", [] { return r514e_mod<SimpleDeformModifier>([](SimpleDeformModifier &m) { m.mode = 0, m.angle = 45.0f; }); }},
          {"Screw", [] { return r514e_mod<ScrewModifier>([](ScrewModifier &m) { m.angle = 360.0f, m.steps = 8; }); }},
          {"Wireframe", [] { return r514e_mod<WireframeModifier>([](WireframeModifier &m) { m.thickness = 0.05f; }); }},
          {"Decimate", [] { return r514e_mod<DecimateModifier>([](DecimateModifier &m) { m.ratio = 0.3f; }); }}};
}

struct R514eSweep {
  size_t cases = 0, built = 0, refused = 0, skipped = 0, problems = 0;
  std::string first;
};

/* One shape with one modifier, added (alone) or subtracted (from a slab it pokes through); false when the result is a problem. */
bool r514e_thinner_than_cell(const Component &c, float cell) {
  if (const auto *w = dynamic_cast<const WireframeModifier *>(&c)) return std::fabs(w->thickness) < cell;
  if (const auto *sh = dynamic_cast<const SolidifyModifier *>(&c)) return std::fabs(sh->thickness) < cell;
  return false;
}

bool r514e_sweep_case(size_t s, size_t k, const R514eShape &shape, const R514eMod &mod, bool subtract, bool field, R514eSweep &t) {
  std::vector<blockout::Piece> pieces;
  if (subtract) {
    blockout::Piece slab;
    slab.id = 1, slab.shape = blockout::Shape::Box, slab.size = Vec3(6.0f, 3.0f, 6.0f), slab.to_blockout = Mat4::trs(Vec3(0.0f, 1.5f, 0.0f), Quat(), Vec3(1.0f));
    pieces.push_back(slab);
  }
  blockout::Piece p;
  p.id = 2, p.op = subtract ? blockout::Op::Subtract : blockout::Op::Add;
  shape.set(p);
  p.to_blockout = Mat4::trs(Vec3(subtract ? 2.5f : 0.0f, 1.5f, 0.0f), Quat(), Vec3(1.0f));
  pieces.push_back(p);
  BlockoutSurface surf;
  BlockoutSurface::Stack st;
  st.index = pieces.size() - 1, st.key = 0x514e0000ULL + s * 100 + k + 1;
  st.modifiers.push_back(mod.make());
  surf.stacks.push_back(st);
  blockout_piece_stacks_apply(pieces, surf);
  blockout::Settings set;
  set.path = field ? blockout::Path::Field : blockout::Path::Auto;
  Mesh out;
  blockout::Report rep;
  t.cases++;
  const bool ok = blockout::build(pieces, set, out, nullptr, &rep);
  t.skipped += !rep.skipped.empty();
  std::string bad;
  if (!ok) {
    t.refused++;
    if (rep.why.empty()) bad = "refused without a reason";
  } else {
    t.built++;
    if (!r514e_valid(out)) bad = "structurally invalid or not finite";
    else if (r514_open_edges(out) != 0) bad = "not closed";
    else if (out.face_count() && r514e_volume(out) <= 0.0) bad = "inside out";
    /* (On the grid a wire or shell thinner than the cell can be lost: ADR 0138 decision 5 accepts it, and the Blockout's status warns of it -
     * "...thinner than the Field Cell..." - which the editor test below reads. Only that case is let through here.) */
    else if (!subtract && out.face_count() == 0 && rep.skipped.empty() && rep.notes.empty() && !(field && r514e_thinner_than_cell(*st.modifiers[0], set.cell)))
      bad = "empty with nothing skipped or noted";  // (the status shows skipped pieces and notes; an Add piece that makes nothing with neither is a silent loss)
  }
  if (!bad.empty()) {
    t.problems++;
    if (t.first.empty() || t.problems <= 6) {
      const Mesh *pm = pieces.back().mesh.get();
      std::printf("      PROBLEM: %s + %s, %s, %s path: %s (the stacked piece's mesh: %zu faces, %zu open edges, volume %.4f; report: %zu skipped, %zu notes, why '%s')\n", shape.name, mod.name,
                  subtract ? "subtracted" : "added", field ? "field" : "auto", bad.c_str(), pm ? pm->face_count() : 0, pm ? r514_open_edges(*pm) : 0, pm ? r514e_volume(*pm) : 0.0,
                  rep.skipped.size(), rep.notes.size(), rep.why.c_str());
    }
    if (t.first.empty()) t.first = std::string(shape.name) + " + " + mod.name + ": " + bad;
  }
  return bad.empty();
}

void tests_round514_engineer_c() {
  /* ---- Criterion 7 ---- */
  test("blockout piece modifiers: a face handle dragged with the mouse changes the stacked piece's numbers (one undo step) and the Blockout follows with the stack evaluated again, equal to a from-scratch build", [] {
    blockout_piece_stack_cache_clear();
    Editor ed;
    ed.init_headless(1600, 1000);
    r514e_empty(ed);
    ed.command("camera 30 40 12 0 0 0");
    GameObject *bo = r514e_hand(ed);
    GameObject *box = r514e_piece(ed, *bo, "Box", 0, 0, {2.0f, 0.0f, 0.0f}, {2.0f, 2.0f, 2.0f});
    ed.select_for_test(box->id, false);
    ed.command("component BevelModifier");
    ed.select_for_test(box->id, false);
    ed.mark_changed_for_test("r514e handles");
    r514e_land(ed, *bo);
    r514_settle(ed, 3);
    const size_t depth = ed.undo_depth_for_test();
    const uint64_t e0 = blockout_piece_stack_evaluations();
    CHECK(r514e_pull(ed, "+x", {4.0f, 0.0f, 0.0f}, platform::MOD_CTRL));
    const BlockoutPiece *bp = box->get<BlockoutPiece>();
    CHECK_NEAR(bp->size.x, 3.0, 0.05);
    CHECK(ed.undo_depth_for_test() == depth + 1);
    r514e_land(ed, *bo);
    CHECK(blockout_piece_stack_evaluations() > e0);
    CHECK(box->get<BevelModifier>() != nullptr);
    const Mesh *m = r514_mesh(*bo);
    CHECK(m && m->render_mesh().bounds.max.x > 3.9f && !r514_numbers_build(*bo));
    Mesh ref;
    CHECK(r514e_scratch(*bo, ref) && m && r514e_bits(*m, ref));
  });

  test("blockout piece modifiers: a click on a face the stack made (the second copy of an Array) selects the piece that carries the stack", [] {
    blockout_piece_stack_cache_clear();
    Editor ed;
    ed.init_headless(1600, 1000);
    r514e_empty(ed);
    ed.command("camera 25 60 9 0 0 0");
    GameObject *bo = r514e_hand(ed);
    GameObject *left = r514e_piece(ed, *bo, "Left", 0, 0, {-1.0f, 0.0f, 0.0f}, {2.0f, 1.0f, 2.0f});
    GameObject *right = r514e_piece(ed, *bo, "Right", 0, 0, {1.0f, 0.0f, 0.0f}, {2.0f, 1.0f, 2.0f});
    ed.select_for_test(left->id, false);
    ed.command("component ArrayModifier");
    ArrayModifier *ar = left->get<ArrayModifier>();
    ar->count = 3, ar->relative_offset = Vec3(0.0f, 0.0f, 1.0f), ar->constant_offset = Vec3(0.0f);
    r34_steps(ed, 4);
    r514e_land(ed, *bo);
    r514_settle(ed, 3);
    const AABB b = r514_bounds(*bo);
    CHECK(b.max.z > 4.9f);  // (the copies are there, 2 and 4 m along z)
    CHECK(r514e_click_world(ed, {-1.0f, 0.5f, 0.0f}) == left);  // (the original's top)
    CHECK(r514e_click_world(ed, {-1.0f, 0.5f, 2.0f}) == left);  // (the first copy's top: a face only the stack made)
    CHECK(r514e_click_world(ed, {1.0f, 0.5f, 0.0f}) == right);  // (the unstacked piece beside it)
  });

  /* ---- Criterion 8 ---- */
  test("blockout piece modifiers: Bake to Mesh keeps the bevelled mesh and each piece's Bevel under the pieces' folder, one undo step; Unbake brings the stack back live and the mesh the same", [] {
    blockout_piece_stack_cache_clear();
    Editor ed;
    ed.init_headless(900, 600);
    R514eColumn s = r514e_column_scene(ed);
    if (!s.bo) return;
    const uint64_t id = s.bo->id;
    ed.select_for_test(s.col->id, false);
    ed.command("component BevelModifier");
    ed.command("component TriangulateModifier");
    s.col->get<BevelModifier>()->width = 0.02f;
    r514e_land(ed, *s.bo);
    ed.mark_changed_for_test("r514e before bake");
    r514_settle(ed, 2);
    const std::vector<R514Poly> faces = r514e_all_faces(*r514_mesh(*s.bo));
    const size_t depth = ed.undo_depth_for_test();
    ed.select_for_test(id, false);
    ed.command("blockout bake");
    r514_settle(ed, 3);
    GameObject *bo = ed.scene().find(id);
    CHECK(bo && bo->get<BlockoutBaked>() && !bo->get<Blockout>() && ed.undo_depth_for_test() == depth + 1);
    if (!bo) return;
    CHECK(r514e_all_faces(*r514_mesh(*bo)) == faces);
    GameObject *holder = blockout_pieces_holder(*bo);
    CHECK(holder != nullptr);
    size_t kept = 0;
    std::string order;
    if (holder)
      for (GameObject *c : holder->children)
        if (c->get<BlockoutPiece>() && c->get<BevelModifier>()) {
          kept++;
          for (const auto &m : c->components)
            if (m->is_modifier()) order += std::string(m->type_name()) + ",";
          CHECK(c->get<BevelModifier>()->width == 0.02f);
        }
    CHECK(kept == 1 && order == "BevelModifier,TriangulateModifier,");
    ed.select_for_test(id, false);
    ed.command("blockout unbake");
    r514_settle(ed, 3);
    bo = ed.scene().find(id);
    CHECK(bo && bo->get<Blockout>() && ed.undo_depth_for_test() == depth + 2);
    if (!bo) return;
    r514e_land(ed, *bo);
    GameObject *col = r514e_find_piece(ed, "Column");
    CHECK(col && col->get<BevelModifier>() && col->get<TriangulateModifier>() && col->get<BevelModifier>()->width == 0.02f);
    CHECK(col && blockout_piece_stack_live(*col));
    CHECK(r514e_all_faces(*r514_mesh(*bo)) == faces);
    const uint64_t evals_before = blockout_piece_stack_evaluations();
    const uint64_t key_before = blockout_piece_stack_hash(*col);
    col->get<BevelModifier>()->width = 0.04f;  // (live again: an edit changes the mesh)
    r514e_land(ed, *bo);
    size_t named_column = 0;
    ed.scene().for_each([&](GameObject &g) { named_column += g.name == "Column"; });
    std::printf("    after the width edit: the piece's Blockout is %s, stack key %s, %llu evaluations, mesh %zu faces (before %zu), %zu 'Column' objects\n",
                blockout_of_piece(*col) == bo ? "the unbaked one" : "ANOTHER object", key_before != blockout_piece_stack_hash(*col) ? "changed" : "the same",
                (unsigned long long)(blockout_piece_stack_evaluations() - evals_before), r514_mesh(*bo)->face_count(), faces.size(), named_column);
    CHECK(r514e_all_faces(*r514_mesh(*bo)) != faces);
  });

  test("blockout piece modifiers: OBJ, FBX and GLB exports hold the stacked result (the Array's ten columns, not the piece's one)", [] {
    blockout_piece_stack_cache_clear();
    Editor ed;
    ed.init_headless(900, 600);
    r514e_empty(ed);
    GameObject *bo = r514e_hand(ed, "Hall");
    GameObject *col = r514e_piece(ed, *bo, "Col", 1, 0, {0.0f, 0.5f, 0.0f}, Vec3(1.0f), 0.3f, 1.0f);
    ed.select_for_test(col->id, false);
    ed.command("component ArrayModifier");
    ArrayModifier *ar = col->get<ArrayModifier>();
    ar->count = 10, ar->relative_offset = Vec3(2.0f, 0.0f, 0.0f), ar->constant_offset = Vec3(0.0f), ar->merge = false;
    r514e_land(ed, *bo);
    const Mesh &ev = *r514_mesh(*bo);
    const size_t want = r514e_tris(ev);
    CHECK(r514e_components(ev, [](size_t) { return true; }) == 10);
    for (const char *format : {"obj", "fbx", "glb"}) {
      const std::string dir = ed.resolve_asset_path_for_test("Assets/Exports");
      fs::make_dirs(dir);
      const std::string path = fs::join(dir, std::string("r514e_array.") + format);
      fs::remove_file(path);
      ed.command(std::string("export ") + format + " all " + path);
      r514_settle(ed, 5);
      for (int i = 0; i < 400 && (!fs::exists(path) || ed.file_export_running_for_test()); i++) ed.step_frame_headless();
      ImportResult r;
      CHECK(import_model(path, r));
      size_t tris = 0, nodes = 0;
      float max_x = -1e30f;
      for (const ImportedNode &n : r.nodes) {
        if (!n.mesh || n.mesh->face_count() == 0) continue;
        nodes++;
        tris += r514e_tris(*n.mesh);
        max_x = std::max(max_x, n.mesh->render_mesh().bounds.max.x);
      }
      std::printf("    %s: %zu mesh node(s), %zu triangles (the Blockout shows %zu), max x %.2f\n", format, nodes, tris, want, (double)max_x);
      CHECK(nodes == 1 && tris == want);
      CHECK(max_x > 11.0f && max_x < 11.2f);  // (ten columns 0.6 m wide, 1.2 m apart - an Array's relative offset is in multiples of the mesh's own width: the tenth ends at 9 * 1.2 + 0.3; the piece's own numbers end at 0.3)
      fs::remove_file(path);
    }
  });

  test("blockout piece modifiers: Generate Lighting lays out and bakes the stacked result (the lightmap has the bevelled column's triangles)", [] {
    blockout_piece_stack_cache_clear();
    Editor ed;
    r35_open(ed);
    LightingSettings &ls = ed.scene().lighting;
    ls.texels_per_unit = 1.0f, ls.max_size = 256, ls.indirect_samples = 2, ls.direct_samples = 2;
    GameObject *bo = r514e_hand(ed);
    MeshRenderer *mr = bo->get<MeshRenderer>();
    mr->contribute_gi = true, mr->generate_lightmap_uvs = true;
    GameObject *col = r514e_piece(ed, *bo, "Column", 1, 0, {3.0f, 0.5f, 0.0f}, Vec3(1.0f), 0.5f, 1.0f);
    ed.select_for_test(col->id, false);
    ed.command("component BevelModifier");
    r514e_land(ed, *bo);
    const uint64_t id = bo->id;
    const size_t tris = r514_mesh(*bo)->render_mesh().tri_count();
    std::vector<blockout::Piece> pieces;
    blockout::Settings st;
    blockout_snapshot(*bo, pieces, st);
    Mesh numbers;
    blockout::build(pieces, st, numbers);
    CHECK(tris > numbers.render_mesh().tri_count());  // (the bevel's triangles are in what is lit)
    ed.commit_change("r514e bevelled column");
    r35_bake(ed);
    const auto it = ed.lighting_data_for_test().entries.find(id);
    CHECK(it != ed.lighting_data_for_test().entries.end());
    if (it != ed.lighting_data_for_test().entries.end()) {
      std::printf("    lightmap triangles %zu (the stacked mesh has %zu, the numbers' %zu)\n", it->second.tri_uv.size() / 3, tris, numbers.render_mesh().tri_count());
      CHECK(it->second.tri_uv.size() == 3 * tris);
    }
  });

  /* ---- Criterion 9 ---- */
  test("blockout piece modifiers: every shape that takes a stack with every allowed modifier, added and subtracted, builds a closed outward result or is refused with a reason (the exact path where there is one: sample of the whole sweep, which is in stress)", [] {
    const std::vector<R514eShape> shapes = r514e_shapes();
    const std::vector<R514eMod> mods = r514e_mods();
    R514eSweep t;
    for (size_t k = 0; k < mods.size(); k++) CHECK(blockout_piece_modifier_refusal(mods[k].make()->type_name()).empty());
    for (size_t s = 0; s < shapes.size(); s++)
      for (size_t k = 0; k < mods.size(); k++)
        for (int sub = 0; sub < 2; sub++) r514e_sweep_case(s, k, shapes[s], mods[k], sub != 0, false, t);
    std::printf("    auto path (%s): %zu cases, %zu built, %zu refused, %zu with a piece skipped, %zu problems\n", blockout::crisp_available() ? "exact" : "field", t.cases, t.built,
                t.refused, t.skipped, t.problems);
    CHECK(t.problems == 0);
    if (!t.first.empty()) std::printf("    first problem: %s\n", t.first.c_str());
  });

  test("blockout piece modifiers: the grid path (Blockout method Field) takes the same sweep's sample - Bevel, Array, Solidify, Subdivision, Wireframe, Decimate on each shape - closed or refused with a reason", [] {
    const std::vector<R514eShape> shapes = r514e_shapes();
    const std::vector<R514eMod> mods = r514e_mods();
    R514eSweep t;
    for (size_t s = 0; s < shapes.size(); s++)
      for (size_t k : {(size_t)0, (size_t)1, (size_t)3, (size_t)4, (size_t)12, (size_t)13})
        for (int sub = 0; sub < 2; sub++) r514e_sweep_case(s, k, shapes[s], mods[k], sub != 0, true, t);
    std::printf("    field path: %zu cases, %zu built, %zu refused, %zu with a piece skipped, %zu problems\n", t.cases, t.built, t.refused, t.skipped, t.problems);
    CHECK(t.problems == 0);
  });

  test("blockout piece modifiers: in the editor, a Bevel on each shape, added and subtracted, on the Blockout's exact and field methods, leaves a closed finite mesh and no refusal", [] {
    blockout_piece_stack_cache_clear();
    Editor ed;
    ed.init_headless(900, 600);
    r514e_empty(ed);
    GameObject *src = ed.scene().create("Source");
    src->add<MeshFilter>()->mesh = primitives::cube(1.8f);
    src->set_local_position(Vec3(40.0f, 0.0f, 0.0f));
    GameObject *bo = r514e_hand(ed);
    r514e_piece(ed, *bo, "Slab", 0, 0, {0.0f, 1.5f, 0.0f}, {6.0f, 3.0f, 6.0f});
    GameObject *piece = r514e_piece(ed, *bo, "Piece", 0, 0, {0.0f, 1.5f, 0.0f});
    ed.select_for_test(piece->id, false);
    ed.command("component BevelModifier");
    piece->get<BevelModifier>()->limit_method = 0;
    piece->get<BevelModifier>()->width = 0.1f;
    const std::vector<R514eShape> shapes = r514e_shapes();
    size_t cases = 0, bad = 0;
    for (size_t s = 0; s < shapes.size(); s++)
      for (int op = 0; op < 2; op++)
        for (int method : {1, 2}) {
          if (method == 1 && !blockout::crisp_available()) continue;
          BlockoutPiece *bp = piece->get<BlockoutPiece>();
          blockout::Piece tmp;
          shapes[s].set(tmp);
          bp->shape = (int)tmp.shape, bp->size = tmp.shape == blockout::Shape::Mesh ? Vec3(1.0f) : tmp.size;
          bp->radius = tmp.radius, bp->height = tmp.height, bp->segments = tmp.segments, bp->rings = tmp.rings, bp->steps = tmp.steps;
          bp->inner_radius = tmp.inner_radius, bp->angle = tmp.angle, bp->outline = tmp.outline;
          bp->mesh_object = tmp.shape == blockout::Shape::Mesh ? "Source" : "";
          bp->operation = op;
          piece->set_local_position(Vec3(op ? 2.5f : 0.0f, 1.5f, 0.0f));
          bo->get<Blockout>()->method = method;
          r514e_land(ed, *bo);
          const Mesh *m = r514_mesh(*bo);
          const BlockoutStatus &st = bo->get<Blockout>()->state.status;
          const bool ok = m && r514e_valid(*m) && r514_open_edges(*m) == 0 && st.refusal.empty() && (m->face_count() == 0 || r514e_volume(*m) > 0.0);
          cases++;
          bad += !ok;
          if (!ok) std::printf("    PROBLEM: %s, %s, method %s: %zu faces, refusal '%s'\n", shapes[s].name, op ? "subtracted" : "added", method == 1 ? "exact" : "field", m ? m->face_count() : 0,
                               st.refusal.c_str());
          CHECK(ok);
        }
    std::printf("    %zu editor cases, %zu bad\n", cases, bad);
  });

  test("blockout piece modifiers: tiny and huge pieces with a Bevel or an Array leave a valid closed mesh (or the piece as its numbers) - never a broken one", [] {
    blockout_piece_stack_cache_clear();
    Editor ed;
    ed.init_headless(900, 600);
    r514e_empty(ed);
    GameObject *bo = r514e_hand(ed);
    GameObject *piece = r514e_piece(ed, *bo, "Piece", 0, 0, {0.0f, 0.0f, 0.0f}, Vec3(1.0f));
    ed.select_for_test(piece->id, false);
    for (const char *type : {"BevelModifier", "ArrayModifier", "SubdivisionSurface"}) {
      ed.command(std::string("component ") + type);
      for (float scale : {0.002f, 1.0f, 5000.0f}) {
        piece->get<BlockoutPiece>()->size = Vec3(scale);
        piece->set_local_position(Vec3(0.0f, scale * 0.5f, 0.0f));
        r514e_land(ed, *bo);
        const Mesh *m = r514_mesh(*bo);
        const bool ok = m && r514e_valid(*m) && r514_open_edges(*m) == 0;
        if (!ok) std::printf("    PROBLEM: %s at scale %g: %zu faces\n", type, (double)scale, m ? m->face_count() : 0);
        CHECK(ok);
      }
      for (auto it = piece->components.begin(); it != piece->components.end(); ++it)
        if ((*it)->is_modifier()) {
          piece->components.erase(it);
          break;
        }
    }
  });

  /* ---- A build overtaken by an edit or an undo ---- */
  test("blockout piece modifiers: a piece deleted, and the delete undone, while its stack is being evaluated on a worker, leaves the final mesh equal to a from-scratch build with no refusal", [] {
    blockout_piece_stack_cache_clear();
    Editor ed;
    ed.init_headless(900, 600);
    ed.set_async_jobs_for_test(true);
    R514eColumn s = r514e_column_scene(ed);
    if (!s.bo) return;
    ed.select_for_test(s.col->id, false);
    ed.command("component BevelModifier");
    r514e_land(ed, *s.bo);
    ed.mark_changed_for_test("r514e before the edit");
    ed.set_blockout_delay_ms_for_test(150);
    s.col->get<BevelModifier>()->width = 0.07f;  // (a build starts: it waits 150 ms on its worker before the stack runs)
    ed.step_frame_headless();
    ed.select_for_test(s.col->id, false);
    ed.command("delete");
    r514_settle(ed, 2);
    r514e_land(ed, *ed.scene().find_by_name("Blockout"));
    GameObject *bo = ed.scene().find_by_name("Blockout");
    CHECK(bo && r514e_find_piece(ed, "Column") == nullptr);
    if (!bo) return;
    CHECK(r514_mesh(*bo) && r514_bounds(*bo).max.x > 10.0f && r514_bounds(*bo).min.x > 10.0f);  // (only the far box is left)
    CHECK(bo->get<Blockout>()->state.status.refusal.empty());
    ed.set_blockout_delay_ms_for_test(150);
    ed.command("undo");
    r514_settle(ed, 1);
    bo = ed.scene().find_by_name("Blockout");
    CHECK(bo != nullptr);
    if (!bo) return;
    ed.set_blockout_delay_ms_for_test(0);
    r514e_land(ed, *bo);
    GameObject *col = r514e_find_piece(ed, "Column");
    CHECK(col && col->get<BevelModifier>());
    Mesh ref;
    CHECK(r514e_scratch(*bo, ref) && r514e_bits(*r514_mesh(*bo), ref) && bo->get<Blockout>()->state.status.refusal.empty());
    CHECK(r514_bounds(*bo).min.x < 0.0f);  // (the column is back)
  });

  /* ---- Criterion 10: the cache ---- */
  test("blockout piece modifiers: the cache's byte cap holds with large meshes (Subdivision 3 of many spheres), keeps the newest, and every mesh it returns is the one a from-scratch evaluation makes", [] {
    blockout_piece_stack_cache_clear();
    const size_t cap = blockout_piece_stack_cache_limit(true);
    size_t most = 0, most_bytes = 0;
    const int n = 300;
    std::shared_ptr<const Mesh> last;
    for (int k = 0; k < n; k++) {
      std::vector<blockout::Piece> pieces(1);
      pieces[0].id = 1, pieces[0].shape = blockout::Shape::Sphere, pieces[0].radius = 0.5f + 0.001f * (float)k, pieces[0].segments = 24, pieces[0].rings = 12;
      BlockoutSurface surf;
      BlockoutSurface::Stack st;
      st.index = 0, st.key = 0x514e5000ULL + (uint64_t)k;
      st.modifiers.push_back(r514e_mod<SubdivisionSurface>([](SubdivisionSurface &m) { m.levels = 3; }));
      surf.stacks.push_back(st);
      blockout_piece_stacks_apply(pieces, surf);
      CHECK(pieces[0].shape == blockout::Shape::Mesh && pieces[0].mesh && pieces[0].mesh->face_count() > 1000);
      last = pieces[0].mesh;
      size_t entries = 0, bytes = 0;
      blockout_piece_stack_cache_size(entries, bytes);
      most = std::max(most, entries), most_bytes = std::max(most_bytes, bytes);
    }
    size_t entries = 0, bytes = 0;
    blockout_piece_stack_cache_size(entries, bytes);
    std::printf("    %d evaluated meshes of %.2f MB each: at most %zu entries (cap %zu), %.1f MB (cap %.0f MB); %zu kept at the end\n", n,
                last ? (double)(last->positions.size() * sizeof(Vec3) + (last->corner_verts.size() + last->face_offsets.size()) * 4) / 1048576.0 : 0.0, most,
                blockout_piece_stack_cache_limit(false), (double)most_bytes / 1048576.0, (double)cap / 1048576.0, entries);
    CHECK(most_bytes <= cap);
    CHECK(most <= blockout_piece_stack_cache_limit(false));
    CHECK(entries > 0 && entries < (size_t)n);  // (the byte cap, not the entry cap, was what bounded it: it dropped some)
    /* The newest key is in the cache and is the scratch mesh; an old one was dropped and is evaluated again. */
    for (int k : {n - 1, 0}) {
      std::vector<blockout::Piece> pieces(1);
      pieces[0].id = 1, pieces[0].shape = blockout::Shape::Sphere, pieces[0].radius = 0.5f + 0.001f * (float)k, pieces[0].segments = 24, pieces[0].rings = 12;
      BlockoutSurface surf;
      BlockoutSurface::Stack st;
      st.index = 0, st.key = 0x514e5000ULL + (uint64_t)k;
      st.modifiers.push_back(r514e_mod<SubdivisionSurface>([](SubdivisionSurface &m) { m.levels = 3; }));
      surf.stacks.push_back(st);
      const uint64_t e0 = blockout_piece_stack_evaluations();
      blockout_piece_stacks_apply(pieces, surf);
      std::printf("    key %d: %s\n", k, blockout_piece_stack_evaluations() == e0 ? "from the cache" : "evaluated again");
      if (k == n - 1) CHECK(blockout_piece_stack_evaluations() == e0);
      blockout::Piece numbers;
      numbers.id = 1, numbers.shape = blockout::Shape::Sphere, numbers.radius = 0.5f + 0.001f * (float)k, numbers.segments = 24, numbers.rings = 12;
      Mesh scratch;
      blockout::piece_shape_mesh(numbers, scratch);
      SubdivisionSurface sd;
      sd.levels = 3;
      run_modifier(sd, scratch);
      CHECK(pieces[0].mesh && r514e_bits(*pieces[0].mesh, scratch));
    }
  });

  test("blockout piece modifiers: opening a scene empties the cache of evaluated meshes, as New Scene does", [] {
    blockout_piece_stack_cache_clear();
    Editor ed;
    ed.init_headless(900, 600);
    R514eColumn s = r514e_column_scene(ed);
    if (!s.bo) return;
    const std::string dir = ed.resolve_asset_path_for_test("Assets/Scenes");
    fs::make_dirs(dir);
    const std::string path = fs::join(dir, "r514e_plain.scene");
    fs::remove_file(path);
    ed.command("save " + path);  // (a scene with no stack at all)
    for (int i = 0; i < 400 && (!fs::exists(path) || ed.save_job_running_for_test()); i++) ed.step_frame_headless();
    ed.select_for_test(s.col->id, false);
    ed.command("component BevelModifier");
    r514e_land(ed, *s.bo);
    size_t entries = 0, bytes = 0;
    blockout_piece_stack_cache_size(entries, bytes);
    CHECK(entries == 1 && bytes > 0);
    CHECK(ed.open_scene_for_test(path));
    for (int i = 0; i < 200; i++) ed.step_frame_headless();
    blockout_piece_stack_cache_size(entries, bytes);
    CHECK(entries == 0 && bytes == 0);
    fs::remove_file(path);
  });

  test("blockout piece modifiers: eight threads taking stacked pieces' meshes from the cache while it is emptied and refilled all get the exact from-scratch mesh, no crash", [] {
    blockout_piece_stack_cache_clear();
    constexpr int kKeys = 6;
    std::vector<Mesh> ref(kKeys);
    for (int k = 0; k < kKeys; k++) {
      blockout::Piece p;
      p.shape = blockout::Shape::Box, p.size = Vec3(2.0f);
      blockout::piece_shape_mesh(p, ref[(size_t)k]);
      BevelModifier bev;
      bev.width = 0.02f * (float)(k + 1), bev.limit_method = 0;
      run_modifier(bev, ref[(size_t)k]);
    }
    std::atomic<int> bad{0}, done{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < 8; t++)
      threads.emplace_back([&, t] {
        for (int round = 0; round < 40; round++) {
          std::vector<blockout::Piece> pieces(kKeys);
          BlockoutSurface surf;
          for (int k = 0; k < kKeys; k++) {
            pieces[(size_t)k].id = (uint64_t)k + 1, pieces[(size_t)k].shape = blockout::Shape::Box, pieces[(size_t)k].size = Vec3(2.0f);
            BlockoutSurface::Stack st;
            st.index = (size_t)((k + t) % kKeys), st.key = 0x514e7000ULL + (uint64_t)((k + t) % kKeys);
            st.modifiers.push_back(r514e_mod<BevelModifier>([&](BevelModifier &m) { m.width = 0.02f * (float)(((k + t) % kKeys) + 1), m.limit_method = 0; }));
            surf.stacks.push_back(st);
          }
          blockout_piece_stacks_apply(pieces, surf);
          for (int k = 0; k < kKeys; k++) {
            const size_t slot = (size_t)((k + t) % kKeys);
            if (!pieces[slot].mesh || !r514e_bits(*pieces[slot].mesh, ref[slot])) bad++;
          }
        }
        done++;
      });
    for (int i = 0; i < 300 && done.load() < 8; i++) {  // (the main thread empties it and reads its size while they work)
      blockout_piece_stack_cache_clear();
      size_t entries = 0, bytes = 0;
      blockout_piece_stack_cache_size(entries, bytes);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    for (auto &th : threads) th.join();
    CHECK(bad.load() == 0 && done.load() == 8);
  });

  test("blockout piece modifiers: twelve Blockouts with workers on, their stacked pieces changed again and again, all end equal to a from-scratch build with no refusal", [] {
    blockout_piece_stack_cache_clear();
    Editor ed;
    ed.init_headless(900, 600);
    ed.set_async_jobs_for_test(true);
    r514e_empty(ed);
    std::vector<GameObject *> bos, pieces;
    for (int i = 0; i < 12; i++) {
      GameObject *bo = r514e_hand(ed, ("Blockout" + std::to_string(i)).c_str());
      GameObject *p = r514e_piece(ed, *bo, ("Piece" + std::to_string(i)).c_str(), i % 3, 0, {0.0f, 0.75f, 0.0f}, Vec3(1.5f), 0.6f, 1.5f);
      bo->set_local_position(Vec3(10.0f * (float)i, 0.0f, 0.0f));
      ed.select_for_test(p->id, false);
      ed.command(i % 2 ? "component BevelModifier" : "component ArrayModifier");
      bos.push_back(bo), pieces.push_back(p);
    }
    for (int round = 0; round < 6; round++) {
      for (int i = 0; i < 12; i++) {
        if (BevelModifier *b = pieces[(size_t)i]->get<BevelModifier>()) b->width = 0.02f + 0.01f * (float)((round + i) % 5);
        if (ArrayModifier *a = pieces[(size_t)i]->get<ArrayModifier>()) a->count = 2 + (round + i) % 3;
        if (i % 4 == 0) pieces[(size_t)i]->get<BlockoutPiece>()->radius = 0.5f + 0.02f * (float)round;
      }
      ed.step_frame_headless();
      ed.step_frame_headless();
    }
    for (GameObject *bo : bos) r514e_land(ed, *bo);
    size_t differ = 0, refused = 0;
    for (GameObject *bo : bos) {
      Mesh ref;
      const bool ok = r514e_scratch(*bo, ref);
      differ += !(ok && r514_mesh(*bo) && r514e_bits(*r514_mesh(*bo), ref));
      refused += !bo->get<Blockout>()->state.status.refusal.empty();
    }
    std::printf("    12 Blockouts: %zu differ from a from-scratch build, %zu refused\n", differ, refused);
    CHECK(differ == 0 && refused == 0);
  });
}

/* Round 514, the test engineer's tests: spec 0496's acceptance criteria 1 to 10. */
void tests_round514_engineer() {
  tests_round514_engineer_a();
  tests_round514_engineer_b();
  tests_round514_engineer_c();

  test("blockout piece modifiers: a stack that leaves no solid (Decimate to two flat faces) leaves the piece as its numbers and the status says why; on the grid a Wireframe thinner than the cell is warned of", [] {
    blockout_piece_stack_cache_clear();
    Editor ed;
    ed.init_headless(900, 600);
    GameObject *box = r514_box(ed);
    CHECK(box != nullptr);
    if (!box) return;
    GameObject *bo = blockout_of_piece(*box);
    ed.command("select " + box->name);
    ed.command("component DecimateModifier");
    box->get<DecimateModifier>()->ratio = 0.3f;
    ed.command("blockout wait");
    CHECK(bo && r514_numbers_build(*bo));  // (the box from its numbers, not nothing)
    std::vector<std::pair<std::string, bool>> lines;
    if (bo) blockout_piece_stack_status(*bo, lines);
    bool said = false;
    for (const auto &l : lines) said = said || (l.second && l.first.find(box->name) != std::string::npos && l.first.find("built from its numbers") != std::string::npos);
    for (const auto &l : lines) std::printf("    status: %s\n", l.first.c_str());
    CHECK(said);
    box->get<DecimateModifier>()->enabled = false;
    ed.command("component WireframeModifier");
    box->get<WireframeModifier>()->thickness = 0.05f;
    if (bo) bo->get<Blockout>()->method = 2;  // (Field: the grid)
    ed.command("blockout wait");
    lines.clear();
    if (bo) blockout_piece_stack_status(*bo, lines);
    bool warned = false;
    for (const auto &l : lines) warned = warned || (l.second && l.first.find("thinner than the Field Cell") != std::string::npos);
    CHECK(warned);
  });
}

}  // namespace

void run_blockout_piece_modifiers_tests() {
  r514_shapes();
  tests_round514();
  tests_round514_engineer();
}
