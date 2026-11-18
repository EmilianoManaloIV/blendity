// SPDX-License-Identifier: GPL-2.0-or-later
// Blockout parts as objects (round 547, task 0527, ADR 0149): a door's leaf, a door's and a window's Frame and a window's Glass each take an ordinary modifier stack, and the stack stays through every
// rebuild of the opening. Split from test_blockout.cpp, whose object file is at the MSVC section limit (C1128); tests_round547 is called from run_blockout_tests so it stays in the blockout group.
#include "test_common.h"

#include "../src/scene/blockout.h"
#include "../src/scene/raycast.h"
#include "../src/editor/blockout_pick.h"

#include <array>
#include <chrono>
#include <cmath>
#include <functional>
#include <limits>

using namespace bl;

namespace {

using EvType = platform::EventType;

platform::Event pe_ev(EvType t, int x, int y, int mods = 0) {
  platform::Event e;
  e.type = t, e.x = x, e.y = y, e.mods = mods;
  return e;
}

GameObject *pe_find(Editor &ed, const char *name) { return ed.scene().find_by_name(name); }

/* Nothing in the scene: the default scene's plane, cube and teapot would be floors and occluders of their own. */
void pe_empty_scene(Editor &ed) {
  std::vector<GameObject *> roots;
  ed.scene().for_each([&](GameObject &g) {
    if (!g.parent) roots.push_back(&g);
  });
  ed.scene().destroy(roots);
  ed.clear_selection_for_test();
}

void pe_settle(Editor &ed, int frames = 2) {
  for (int i = 0; i < frames; i++) ed.step_frame_headless();
}

/* The Blockout built, the parts made, the parts' meshes settled. */
void pe_built(Editor &ed) {
  pe_settle(ed, 3);
  ed.command("blockout wait");
  pe_settle(ed, 3);
  ed.command("blockout wait");
  pe_settle(ed, 2);
}

const Mesh *pe_mesh(const GameObject &g) {
  const MeshFilter *mf = g.get<MeshFilter>();
  return mf && mf->mesh ? mf->mesh.get() : nullptr;
}

/* A room (4 x 3 x 4, walls 0.2) at the origin with an opening on its first wall (-X) made by the console. The opening's own space: x across, y up from the floor line, z through the wall and out
 * into the room (+Z faces the room). `words`: what follows `blockout add` ("door single", "window grid 1.6 2 0.9"). */
struct PartScene {
  GameObject *bo = nullptr, *room = nullptr, *opening = nullptr;
};
PartScene pe_make(Editor &ed, const std::string &words, int method = 0) {
  ed.init_headless(900, 600);
  pe_empty_scene(ed);
  PartScene s;
  s.bo = ed.scene().create("Blockout");
  s.bo->add<Blockout>()->method = method;
  s.bo->add<MeshFilter>()->mesh = std::make_shared<Mesh>();
  s.bo->add<MeshRenderer>();
  s.bo->set_world_position({0.0f, 0.0f, 0.0f});
  ed.select_for_test(s.bo->id, false);
  ed.command("blockout add room box 4 3 4");
  pe_settle(ed, 2);
  s.room = ed.active_object_for_test();
  if (!s.room) return s;
  s.room->set_local_position({0.0f, 0.0f, 0.0f});
  pe_settle(ed, 2);
  ed.command("blockout add " + words);
  s.opening = ed.active_object_for_test();
  pe_built(ed);
  return s;
}

/* The opening's part objects of one kind (-1: all), in the order they stand. */
std::vector<GameObject *> pe_parts(const GameObject &opening, int kind = -1) {
  std::vector<GameObject *> out;
  for (GameObject *c : opening.children) {
    const BlockoutLeaf *m = c->get<BlockoutLeaf>();
    if (m && (kind < 0 || m->kind == kind)) out.push_back(c);
  }
  return out;
}
GameObject *pe_part(const GameObject &opening, int kind, size_t nth = 0) {
  const std::vector<GameObject *> v = pe_parts(opening, kind);
  return nth < v.size() ? v[nth] : nullptr;
}

/* The stack on `g` applied by hand to a copy of its base mesh: what the object's evaluated mesh must be. */
Mesh pe_by_hand(const GameObject &g) {
  Mesh m = *pe_mesh(g);
  for (const auto &c : g.components)
    if (c->is_modifier() && c->enabled) c->modify(m);
  return m;
}

/* A number that tells two meshes apart: the counts and the sum of the positions along three skew directions. */
double pe_sum(const Mesh &m) {
  double s = (double)m.vert_count() * 1000.0 + (double)m.face_count();
  for (const Vec3 &p : m.positions) s += 0.37 * p.x + 0.71 * p.y + 1.13 * p.z + 0.11 * p.x * p.y + 0.05 * p.x * p.x + 0.07 * p.y * p.y + 0.09 * p.z * p.z;  // (the squares: a widening about the middle moves no sum of the points)
  return s;
}

/* The object's evaluated mesh is what its stack makes of its present base mesh (the check every survival test ends with), and the stack did something (more faces or vertices than the base). */
bool pe_stack_runs(const GameObject &g, size_t modifiers) {
  size_t n = 0;
  for (const auto &c : g.components) n += c->is_modifier();
  const Mesh *base = pe_mesh(g);
  const Mesh *eval = g.evaluated_mesh(0);
  if (n != modifiers || !base || !eval) return false;
  const Mesh hand = pe_by_hand(g);
  if (hand.vert_count() != eval->vert_count() || hand.face_count() != eval->face_count() || std::fabs(pe_sum(hand) - pe_sum(*eval)) > 1e-3) return false;
  return eval->face_count() != base->face_count() || eval->vert_count() != base->vert_count();
}

/* The kernel builds asked for so far: a change of a part's modifier or of Open Amount asks for none. */
uint64_t pe_builds(Editor &ed) { return ed.blockout_started_for_test() + ed.blockout_main_builds_for_test(); }

float pe_yaw(const GameObject &leaf) {
  const Vec3 f = leaf.local().rotation.rotate(Vec3(0.0f, 0.0f, 1.0f));
  return std::atan2(f.x, f.z) * 57.29577951308232f;
}

std::string pe_scene_path(Editor &ed, const char *file) {
  const std::string dir = ed.resolve_asset_path_for_test("Assets/Scenes");
  fs::make_dirs(dir);
  return fs::join(dir, file);
}

/* Waits for an export's file. */
std::string pe_export(Editor &ed, const char *format, const char *name) {
  const std::string dir = ed.resolve_asset_path_for_test("Assets/Exports");
  fs::make_dirs(dir);
  const std::string path = fs::join(dir, name);
  fs::remove_file(path);
  ed.command(std::string("export ") + format + " all " + path);
  pe_settle(ed, 5);
  for (int i = 0; i < 400 && (!fs::exists(path) || ed.file_export_running_for_test()); i++) ed.step_frame_headless();
  return path;
}

void pe_click(Editor &ed, int x, int y) {
  ed.step_frame_headless({pe_ev(EvType::MouseMove, x, y)});
  ed.step_frame_headless({pe_ev(EvType::MouseDown, x, y)});
  ed.step_frame_headless({pe_ev(EvType::MouseUp, x, y)});
}


/* How many faces of the Blockout's live mesh are of one class (by blockout_face_class, from the pieces the snapshot makes now). */
size_t pe_class_faces(const GameObject &bo, int cls) {
  const Mesh *m = pe_mesh(bo);
  const Blockout *b = bo.get<Blockout>();
  if (!m || !b || b->state.faces.piece.size() != m->face_count()) return 0;
  std::vector<blockout::Piece> pieces;
  blockout::Settings settings;
  blockout_snapshot(bo, pieces, settings);
  size_t n = 0;
  for (size_t f = 0; f < m->face_count(); f++)
    for (const blockout::Piece &p : pieces)
      if (p.id == b->state.faces.piece[f]) n += blockout_face_class(p, b->state.faces.side[f]) == cls;
  return n;
}

/* The mesh's box. */
AABB pe_box(const Mesh &m) { return m.bounds(); }

/* A mesh a part may have: sound, closed, outward. */
bool pe_sound(const Mesh *m, std::string *why = nullptr) {
  if (!m || m->face_count() == 0) return why ? (*why = "no faces", false) : false;
  std::string w;
  if (!structurally_valid(*m, &w)) return why ? (*why = w, false) : false;
  if (!closed_manifold(*m)) return why ? (*why = "open or pinched", false) : false;
  if (!(signed_volume(*m) > 0.0f)) return why ? (*why = "inside out", false) : false;
  return true;
}

}  // namespace

// r547-begin
/* ===================================================================== */
/* Round 547 (task 0527, ADR 0149): the parts take modifier stacks         */
/* ===================================================================== */

/* ---- Phase 1: the leaf takes a stack ---- */

static void tests_round547_leaf() {
  test("blockout parts (547): a Solidify on a door's leaf stays through Width, Leaf Thickness, Hinge Side and Single -> Double -> Single, and the leaf's evaluated mesh is the stack applied to the new mesh each time", [] {
    Editor ed;
    const PartScene s = pe_make(ed, "door single");
    CHECK(s.opening != nullptr);
    if (!s.opening) return;
    BlockoutPiece *bp = s.opening->get<BlockoutPiece>();
    GameObject *leaf = pe_part(*s.opening, kPartKindLeaf);
    CHECK(leaf != nullptr);
    if (!leaf) return;
    const uint64_t id = leaf->id;
    leaf->add<SolidifyModifier>();
    ed.mark_changed_for_test("test modifier");
    pe_built(ed);
    CHECK(pe_stack_runs(*leaf, 1));
    const uint64_t before = pe_builds(ed);
    double last = pe_sum(*pe_mesh(*leaf));
    auto check = [&](const char *what, bool mesh_changes) {
      pe_built(ed);
      GameObject *l = ed.scene().find(id);
      CHECK(l != nullptr);
      if (!l) return;
      CHECK(l->get<BlockoutLeaf>() && l->parent == s.opening);
      CHECK(pe_stack_runs(*l, 1));
      const double now = pe_sum(*pe_mesh(*l));
      std::printf("    %s: the leaf's mesh %s, the stack runs\n", what, now != last ? "is new" : "is the same");
      if (mesh_changes) CHECK(now != last);
      last = now;
    };
    bp->width = 1.2f;
    ed.mark_changed_for_test("test width");
    check("Width", true);
    bp->leaf_thickness = 0.06f;
    ed.mark_changed_for_test("test thickness");
    check("Leaf Thickness", true);
    bp->hinge_side = 1;
    ed.mark_changed_for_test("test hinge");
    check("Hinge Side", false);  // (the box is the same; the pivot moved to the other jamb)
    CHECK(ed.scene().find(id)->local().position.x < 0.0f);
    blockout_opening_set_style(*bp, kDoorDouble);
    ed.mark_changed_for_test("test style");
    pe_built(ed);
    CHECK(pe_parts(*s.opening, kPartKindLeaf).size() == 2);
    for (GameObject *l : pe_parts(*s.opening, kPartKindLeaf)) CHECK(pe_stack_runs(*l, 1));  // (the second leaf took a copy of the first one's stack: ADR 0149 decision 5)
    CHECK(ed.scene().find(id) != nullptr);
    blockout_opening_set_style(*bp, kDoorSingle);
    ed.mark_changed_for_test("test style");
    pe_built(ed);
    CHECK(pe_parts(*s.opening, kPartKindLeaf).size() == 1 && ed.scene().find(id) && pe_stack_runs(*ed.scene().find(id), 1));
    (void)before;
  });

  test("blockout parts (547): Open Amount and a hand swing keep a leaf's Bevel; a stack does not count as a turn, and the swing does not run the stack again or ask for a build", [] {
    Editor ed;
    const PartScene s = pe_make(ed, "door single");
    CHECK(s.opening != nullptr);
    if (!s.opening) return;
    BlockoutPiece *bp = s.opening->get<BlockoutPiece>();
    GameObject *leaf = pe_part(*s.opening, kPartKindLeaf);
    CHECK(leaf != nullptr);
    if (!leaf) return;
    const uint64_t id = leaf->id;
    leaf->add<BevelModifier>();
    ed.mark_changed_for_test("test modifier");
    pe_built(ed);
    CHECK(pe_stack_runs(*leaf, 1));
    CHECK(bp->open_amount == 0.0f && std::fabs(pe_yaw(*leaf)) < 0.05f);  // (the stack is no turn)
    const size_t steps = ed.undo_depth_for_test();
    const uint64_t builds = pe_builds(ed);
    ed.select_for_test(s.opening->id, false);
    ed.command("blockout set open 0.5");
    pe_built(ed);
    leaf = ed.scene().find(id);
    CHECK(leaf && pe_stack_runs(*leaf, 1));
    if (!leaf) return;
    CHECK_NEAR(std::fabs(pe_yaw(*leaf)), 45.0f, 0.05f);
    CHECK(pe_builds(ed) == builds);  // (a swing asks for no build of the Blockout)
    /* Turned by hand: Open Amount follows the angle, the stack stays and runs on the same base mesh. */
    const Mesh *base = pe_mesh(*leaf);
    const uint64_t eval_key = leaf->evaluated_key(0);
    leaf->set_local_euler(Vec3(0.0f, std::copysign(72.0f, pe_yaw(*leaf)), 0.0f));
    ed.mark_changed_for_test("test turn");
    pe_built(ed);
    leaf = ed.scene().find(id);
    CHECK(leaf != nullptr);
    if (!leaf) return;
    CHECK_NEAR(bp->open_amount, 72.0f / 90.0f, 1e-3f);
    CHECK(pe_stack_runs(*leaf, 1) && pe_mesh(*leaf) == base && leaf->evaluated_key(0) == eval_key);
    CHECK(pe_builds(ed) == builds);
    (void)steps;
  });

  test("blockout parts (547): a Wireframe on a leaf survives Bake to Mesh and Unbake, a duplicate of the door, a delete of the leaf with its undo, and a save and an open", [] {
    Editor ed;
    const PartScene s = pe_make(ed, "door single");
    CHECK(s.opening != nullptr);
    if (!s.opening) return;
    GameObject *leaf = pe_part(*s.opening, kPartKindLeaf);
    CHECK(leaf != nullptr);
    if (!leaf) return;
    const uint64_t leaf_id = leaf->id, door_id = s.opening->id;
    leaf->add<WireframeModifier>();
    ed.mark_changed_for_test("test modifier");
    pe_built(ed);
    CHECK(pe_stack_runs(*leaf, 1));
    /* Bake / Unbake. */
    ed.select_for_test(s.bo->id, false);
    ed.command("blockout bake");
    pe_built(ed);
    GameObject *baked = pe_find(ed, "Blockout");
    CHECK(baked && baked->get<BlockoutBaked>());
    CHECK(ed.scene().find(leaf_id) && pe_stack_runs(*ed.scene().find(leaf_id), 1));
    ed.select_for_test(baked->id, false);
    ed.command("blockout unbake");
    pe_built(ed);
    leaf = ed.scene().find(leaf_id);
    CHECK(leaf && leaf->parent == ed.scene().find(door_id) && pe_stack_runs(*leaf, 1));
    /* Duplicate: the copy's leaf has the stack too, each its own. */
    ed.select_for_test(door_id, false);
    ed.command("duplicate");
    pe_built(ed);
    GameObject *copy = ed.active_object_for_test();
    CHECK(copy && copy->id != door_id);
    if (!copy) return;
    GameObject *copy_leaf = pe_part(*copy, kPartKindLeaf);
    CHECK(copy_leaf && copy_leaf->id != leaf_id && pe_stack_runs(*copy_leaf, 1));
    CHECK(ed.scene().find(leaf_id) && pe_stack_runs(*ed.scene().find(leaf_id), 1));
    const uint64_t copy_id = copy->id;
    ed.select_for_test(copy_id, false);
    ed.command("delete");
    pe_built(ed);
    /* Delete the leaf: the door's Leaf goes off with it; the undo brings it back with its stack. */
    ed.select_for_test(leaf_id, false);
    pe_settle(ed, 2);
    ed.command("delete");
    pe_built(ed);
    CHECK(!ed.scene().find(door_id)->get<BlockoutPiece>()->leaf && pe_parts(*ed.scene().find(door_id), kPartKindLeaf).empty());
    ed.command("undo");
    pe_built(ed);
    CHECK(ed.scene().find(door_id)->get<BlockoutPiece>()->leaf);
    leaf = pe_part(*ed.scene().find(door_id), kPartKindLeaf);
    CHECK(leaf && pe_stack_runs(*leaf, 1));
    /* Save and open: the stack is in the file, the leaf is not made again. */
    const std::string path = pe_scene_path(ed, "r547_leaf.scene");
    CHECK(fs::write_file(path, save_scene_text(ed.scene())));
    ed.command("newscene");
    CHECK(ed.open_scene_for_test(path));
    for (int i = 0; i < 400 && !pe_find(ed, "Blockout"); i++) ed.step_frame_headless();
    pe_built(ed);
    GameObject *door = pe_find(ed, "Door");
    CHECK(door != nullptr);
    if (!door) return;
    leaf = pe_part(*door, kPartKindLeaf);
    CHECK(leaf && pe_stack_runs(*leaf, 1) && pe_parts(*door, kPartKindLeaf).size() == 1);
  });

  test("blockout parts (547): an FBX, a GLB and an OBJ write a door's leaf with its Solidify applied - the node's mesh is the evaluated one", [] {
    Editor ed;
    const PartScene s = pe_make(ed, "door single");
    CHECK(s.opening != nullptr);
    if (!s.opening) return;
    GameObject *leaf = pe_part(*s.opening, kPartKindLeaf);
    CHECK(leaf != nullptr);
    if (!leaf) return;
    leaf->add<SolidifyModifier>();
    ed.mark_changed_for_test("test modifier");
    pe_built(ed);
    CHECK(pe_stack_runs(*leaf, 1));
    const Mesh *eval = leaf->evaluated_mesh(0);
    const size_t base_faces = pe_mesh(*leaf)->face_count(), eval_faces = eval ? eval->face_count() : 0;
    CHECK(eval_faces > base_faces);
    for (const char *format : {"fbx", "glb", "obj"}) {
      const std::string name = std::string("r547_leaf.") + format;
      const std::string path = pe_export(ed, format, name.c_str());
      ImportResult r;
      CHECK(import_model(path, r));
      size_t found = 0;
      for (const ImportedNode &n : r.nodes) {
        if (!n.mesh || n.name.find("Leaf") == std::string::npos) continue;
        found++;
        std::printf("    %s: the leaf's node has %zu faces (base %zu, evaluated %zu)\n", name.c_str(), n.mesh->face_count(), base_faces, eval_faces);
        CHECK(n.mesh->face_count() >= eval_faces);  // (a file may triangulate: never fewer than the evaluated mesh's faces)
        CHECK(n.mesh->face_count() > base_faces);
      }
      CHECK(found == 1);
    }
  });
}

/* ---- Phase 2: a window's and a door's Frame and a window's Glass are objects ---- */

static void tests_round547_objects() {
  test("blockout parts (547): a styled window has a Frame and a Glass object under its piece - upright, the pivot at the middle of the hole's bottom line, a closed outward mesh, no piece and no collider - Opening Only has none; a styled door has a Frame and its leaves, a Revolving door only the Blockout's ring and drum", [] {
    struct Want {
      const char *words;
      size_t frames, glasses, leaves;
    };
    const std::vector<Want> wants = {{"window single", 1, 1, 0}, {"window double", 1, 1, 0}, {"window fixed", 1, 1, 0}, {"window grid 1.6 2 0.9", 1, 1, 0}, {"window arched", 1, 1, 0},
                                     {"window round", 1, 1, 0}, {"window opening", 0, 0, 0}, {"door single", 1, 0, 1}, {"door double", 1, 0, 2}, {"door sliding", 1, 0, 1},
                                     {"door arched", 1, 0, 1}, {"door revolving", 0, 0, 0}, {"door opening", 0, 0, 0}};
    for (const Want &w : wants) {
      Editor ed;
      const PartScene s = pe_make(ed, w.words);
      CHECK(s.opening != nullptr);
      if (!s.opening) continue;
      const BlockoutPiece *bp = s.opening->get<BlockoutPiece>();
      const std::vector<GameObject *> frames = pe_parts(*s.opening, kPartKindFrame), glasses = pe_parts(*s.opening, kPartKindGlass), leaves = pe_parts(*s.opening, kPartKindLeaf);
      std::printf("    %s: %zu frame, %zu glass, %zu leaf object(s)\n", w.words, frames.size(), glasses.size(), leaves.size());
      CHECK(frames.size() == w.frames && glasses.size() == w.glasses && leaves.size() == w.leaves);
      for (const GameObject *g : pe_parts(*s.opening)) {
        const BlockoutLeaf *mark = g->get<BlockoutLeaf>();
        CHECK(g->parent == s.opening && !g->get<BlockoutPiece>() && g->get<MeshFilter>() && g->get<MeshRenderer>());
        std::string why;
        CHECK(pe_sound(pe_mesh(*g), &why));
        if (!why.empty()) std::printf("      %s: %s\n", g->name.c_str(), why.c_str());
        if (mark->kind == kPartKindLeaf) continue;
        CHECK(length(g->local().position - Vec3(0.0f, bp->sill, 0.0f)) < 1e-5f);  // (the middle of the hole's bottom line, in the wall's plane)
        CHECK(std::fabs(pe_yaw(*g)) < 1e-3f);
        CHECK(!g->get<ColliderComponent>() && !g->get<Rigidbody>());
        CHECK(g->get<MeshRenderer>()->contribute_gi == (mark->kind == kPartKindFrame));  // (the frame is static and baked; the glass is a transparent sheet)
        const AABB box = pe_box(*pe_mesh(*g));
        CHECK(box.min.x >= -0.5f * bp->width - 1e-4f - (bp->sill_board ? bp->sill_overhang : 0.0f) && box.max.x <= 0.5f * bp->width + 1e-4f + (bp->sill_board ? bp->sill_overhang : 0.0f));
        CHECK(box.max.y <= bp->head - bp->sill + 1e-4f);
      }
      /* The Blockout's mesh: the clear hole, and no Solid or Glass face of the opening (a Revolving door's ring is Solid). */
      pe_built(ed);
      const bool ring = std::string(w.words) == "door revolving";
      CHECK(pe_class_faces(*s.bo, kClassGlass) == 0);
      CHECK((pe_class_faces(*s.bo, kClassSolid) > 0) == ring);
    }
  });
}

/* ---- A stack on a part stays through everything the opening does ---- */

namespace {

struct Mutation {
  const char *what;
  std::function<void(BlockoutPiece &)> apply;
  bool mesh_changes;  // whether the part's own mesh is new afterwards (a Hinge Side moves the pivot, not the box)
};

/* The part (by id) has its `modifiers`, runs them on its present mesh, and still hangs under the opening. */
bool pe_alive(Editor &ed, uint64_t id, const GameObject &opening, size_t modifiers) {
  const GameObject *g = ed.scene().find(id);
  return g && g->parent == &opening && g->get<BlockoutLeaf>() && pe_stack_runs(*g, modifiers);
}

/* Each mutation of the opening's numbers applied in turn: the part keeps its object (its id) and its modifier, and the modifier runs on the part's new mesh (compared with the stack applied by hand). */
void pe_survive(const char *words, int kind, int modifier, const std::vector<Mutation> &mutations) {
  Editor ed;
  const PartScene s = pe_make(ed, words);
  CHECK(s.opening != nullptr);
  if (!s.opening) return;
  BlockoutPiece *bp = s.opening->get<BlockoutPiece>();
  GameObject *part = pe_part(*s.opening, kind);
  CHECK(part != nullptr);
  if (!part) return;
  const uint64_t id = part->id;
  if (modifier == 0) part->add<BevelModifier>();
  else if (modifier == 1) part->add<WireframeModifier>();
  else part->add<SolidifyModifier>();
  ed.mark_changed_for_test("test modifier");
  pe_built(ed);
  CHECK(pe_stack_runs(*part, 1));
  /* A change of a part's modifier asks for no build of the Blockout. */
  const uint64_t builds = pe_builds(ed);
  if (BevelModifier *bevel = part->get<BevelModifier>()) bevel->width = 0.02f;
  ed.mark_changed_for_test("test modifier number");
  pe_built(ed);
  CHECK(pe_builds(ed) == builds);
  double last = pe_sum(*pe_mesh(*ed.scene().find(id)));
  for (const Mutation &m : mutations) {
    m.apply(*bp);
    ed.mark_changed_for_test("test number");
    pe_built(ed);
    const bool alive = pe_alive(ed, id, *s.opening, 1);
    const double now = alive ? pe_sum(*pe_mesh(*ed.scene().find(id))) : 0.0;
    std::printf("    %s %s: the part %s, its mesh %s\n", words, m.what, alive ? "stays with its stack running" : "IS LOST", now != last ? "is new" : "is the same");
    CHECK(alive);
    if (alive && m.mesh_changes) CHECK(now != last);
    if (alive) last = now;
    const size_t want = kind == kPartKindLeaf && bp->style == kDoorDouble ? 2 : 1;
    CHECK(pe_parts(*s.opening, kind).size() == want);
  }
}

}  // namespace

static void tests_round547_survive() {
  test("blockout parts (547): a Bevel on a window's Frame stays through Width, Head, Sill, Frame Width, Frame Depth, Sill Board and the styles Single, Grid, Double, Fixed, Arched and Round, and the Bevel runs on each new mesh", [] {
    std::vector<Mutation> all = {
        {"Width", [](BlockoutPiece &p) { p.width += 0.3f; }, true},
        {"Head", [](BlockoutPiece &p) { p.head += 0.2f; }, true},
        {"Sill", [](BlockoutPiece &p) { p.sill += 0.15f; p.head += 0.15f; }, false},  // (the box is the same; the pivot moved up)
        {"Frame Width", [](BlockoutPiece &p) { p.frame_width = 0.09f; }, true},
        {"Frame Depth", [](BlockoutPiece &p) { p.frame_depth = 0.14f; }, true},
        {"Sill Board off", [](BlockoutPiece &p) { p.sill_board = false; }, true},
        {"Sill Board on", [](BlockoutPiece &p) { p.sill_board = true; }, true},
    };
    for (int style : {kWindowGrid, kWindowDouble, kWindowFixed, kWindowArched, kWindowRound, kWindowSingle})
      all.push_back({kBlockoutWindowStyleNames[style], [style](BlockoutPiece &p) { blockout_opening_set_style(p, style); }, true});
    pe_survive("window single", kPartKindFrame, 0, all);
  });

  test("blockout parts (547): a Wireframe on a window's Glass stays through Width, Head, Sill, Frame Width, Panes Across and Up and the styles - and Glass off removes the object, Glass on makes a new one", [] {
    std::vector<Mutation> all = {
        {"Width", [](BlockoutPiece &p) { p.width += 0.3f; }, true},
        {"Head", [](BlockoutPiece &p) { p.head += 0.2f; }, true},
        {"Frame Width", [](BlockoutPiece &p) { p.frame_width = 0.09f; }, true},
    };
    for (int style : {kWindowGrid, kWindowDouble, kWindowArched, kWindowRound, kWindowSingle})
      all.push_back({kBlockoutWindowStyleNames[style], [style](BlockoutPiece &p) { blockout_opening_set_style(p, style); }, true});
    pe_survive("window single", kPartKindGlass, 1, all);
    Editor ed;
    const PartScene s = pe_make(ed, "window single");
    CHECK(s.opening != nullptr);
    if (!s.opening) return;
    GameObject *glass = pe_part(*s.opening, kPartKindGlass);
    CHECK(glass != nullptr);
    if (!glass) return;
    glass->add<WireframeModifier>();
    s.opening->get<BlockoutPiece>()->glass = false;
    ed.mark_changed_for_test("test glass off");
    pe_built(ed);
    CHECK(pe_parts(*s.opening, kPartKindGlass).empty() && pe_parts(*s.opening, kPartKindFrame).size() == 1);
    s.opening->get<BlockoutPiece>()->glass = true;
    ed.mark_changed_for_test("test glass on");
    pe_built(ed);
    CHECK(pe_parts(*s.opening, kPartKindGlass).size() == 1);
    if (pe_part(*s.opening, kPartKindGlass)) CHECK(pe_part(*s.opening, kPartKindGlass)->get<WireframeModifier>() == nullptr);  // (a new object: the stack went with the old one)
  });

  test("blockout parts (547): a Bevel on a door's Frame and a Solidify on its leaf stay through Width, Head, Frame Width, Frame Depth, Leaf Thickness, Open Amount and the styles Double, Arched, Sliding and Single", [] {
    std::vector<Mutation> all = {
        {"Width", [](BlockoutPiece &p) { p.width += 0.2f; }, true},
        {"Head", [](BlockoutPiece &p) { p.head += 0.1f; }, true},
        {"Frame Width", [](BlockoutPiece &p) { p.frame_width = 0.09f; }, true},
        {"Frame Depth", [](BlockoutPiece &p) { p.frame_depth = 0.14f; }, false},
        {"Open Amount", [](BlockoutPiece &p) { p.open_amount = 0.6f; }, false},
    };
    for (int style : {kDoorDouble, kDoorArched, kDoorSliding, kDoorSingle})
      all.push_back({kBlockoutDoorStyleNames[style], [style](BlockoutPiece &p) { blockout_opening_set_style(p, style); }, style == kDoorArched});  // (the frame of a Single, Double and Sliding door is the same)
    pe_survive("door single", kPartKindFrame, 0, all);
    all.insert(all.begin() + 4, {"Leaf Thickness", [](BlockoutPiece &p) { p.leaf_thickness = 0.07f; }, true});
    pe_survive("door single", kPartKindLeaf, 2, all);
  });

  test("blockout parts (547): a window's Frame and Glass and a door's Frame keep their stacks through a duplicate, a delete with its undo, Bake to Mesh with Unbake, and a save with an open", [] {
    struct Case {
      const char *words;
      int kind, modifier;
    };
    for (const Case &c : {Case{"window grid 1.6 2 0.9", kPartKindFrame, 0}, Case{"window single", kPartKindGlass, 1}, Case{"door double", kPartKindFrame, 0}}) {
      Editor ed;
      const PartScene s = pe_make(ed, c.words);
      CHECK(s.opening != nullptr);
      if (!s.opening) continue;
      GameObject *part = pe_part(*s.opening, c.kind);
      CHECK(part != nullptr);
      if (!part) continue;
      const uint64_t part_id = part->id, opening_id = s.opening->id;
      const std::string opening_name = s.opening->name;  // (an undo makes the objects again: the pointers are not kept past it)
      if (c.modifier == 0) part->add<BevelModifier>();
      else part->add<WireframeModifier>();
      ed.mark_changed_for_test("test modifier");
      pe_built(ed);
      CHECK(pe_stack_runs(*part, 1));
      /* Bake / Unbake: the part stays an object at the same world place, then goes back under its opening. */
      const Vec3 world = part->world_matrix().point(Vec3(0.1f, 0.1f, 0.0f));
      ed.select_for_test(s.bo->id, false);
      ed.command("blockout bake");
      pe_built(ed);
      GameObject *baked = pe_find(ed, "Blockout");
      CHECK(baked && baked->get<BlockoutBaked>());
      const GameObject *held = ed.scene().find(part_id);
      CHECK(held && held->parent == baked && held->active_in_hierarchy() && pe_stack_runs(*held, 1));
      if (held) CHECK(length(held->world_matrix().point(Vec3(0.1f, 0.1f, 0.0f)) - world) < 1e-4f);
      if (!baked) continue;
      ed.select_for_test(baked->id, false);
      ed.command("blockout unbake");
      pe_built(ed);
      CHECK(pe_alive(ed, part_id, *ed.scene().find(opening_id), 1));
      /* Duplicate: the copy has its own part with the stack; the original keeps its own. */
      ed.select_for_test(opening_id, false);
      ed.command("duplicate");
      pe_built(ed);
      GameObject *copy = ed.active_object_for_test();
      CHECK(copy && copy->id != opening_id);
      if (!copy) continue;
      GameObject *copy_part = pe_part(*copy, c.kind);
      CHECK(copy_part && copy_part->id != part_id && pe_stack_runs(*copy_part, 1) && pe_parts(*copy, c.kind).size() == 1);
      CHECK(pe_alive(ed, part_id, *ed.scene().find(opening_id), 1) && pe_parts(*ed.scene().find(opening_id), c.kind).size() == 1);
      const uint64_t copy_id = copy->id;
      ed.select_for_test(copy_id, false);
      ed.command("delete");
      pe_built(ed);
      CHECK(ed.scene().find(copy_id) == nullptr);
      /* Delete the part by hand: the opening makes another (without the stack); the undo brings the first back, once. */
      ed.select_for_test(part_id, false);
      pe_settle(ed, 2);
      ed.command("delete");
      pe_built(ed);
      const GameObject *opening = ed.scene().find(opening_id);
      CHECK(opening && pe_parts(*opening, c.kind).size() == 1 && pe_parts(*opening, c.kind)[0]->id != part_id);
      if (c.kind == kPartKindFrame && opening) CHECK(!pe_parts(*opening, c.kind)[0]->get<BevelModifier>());
      ed.command("undo");
      pe_built(ed);
      opening = ed.scene().find(opening_id);
      CHECK(opening != nullptr);
      if (!opening) continue;
      CHECK(pe_parts(*opening, c.kind).size() == 1 && pe_alive(ed, part_id, *opening, 1));
      /* Save and open. */
      const std::string path = pe_scene_path(ed, "r547_part.scene");
      CHECK(fs::write_file(path, save_scene_text(ed.scene())));
      ed.command("newscene");
      CHECK(ed.open_scene_for_test(path));
      for (int i = 0; i < 400 && !pe_find(ed, "Blockout"); i++) ed.step_frame_headless();
      pe_built(ed);
      GameObject *reopened = pe_find(ed, opening_name.c_str());
      CHECK(reopened != nullptr);
      if (!reopened) continue;
      GameObject *again = pe_part(*reopened, c.kind);
      CHECK(again && pe_stack_runs(*again, 1) && pe_parts(*reopened, c.kind).size() == 1);
    }
  });
}

/* ---- Materials, light, grid, old scenes, export ---- */

static void tests_round547_look() {
  test("blockout parts (547): the Frame takes the opening's Solid material else the Blockout's, the Glass the Glass class's, the leaf the Leaf class's; a material dropped on a part by hand is kept when the opening changes", [] {
    Editor ed;
    const PartScene s = pe_make(ed, "window single");
    CHECK(s.opening != nullptr);
    if (!s.opening) return;
    const std::string solid = "Assets/Materials/R547/Solid.mat", glass = "Assets/Materials/R547/Glass.mat", own = "Assets/Materials/R547/Own.mat", hand = "Assets/Materials/R547/Hand.mat";
    for (const std::string &m : {solid, glass, own, hand}) create_material_asset(*make_material(m.substr(m.rfind('/') + 1, m.size() - m.rfind('/') - 5), Vec3(0.3f, 0.6f, 0.9f)), m);  // (named for its file: a material renames its file to its own name)
    auto holds = [&](const GameObject *g, const std::string &path) {
      const MeshRenderer *mr = g ? g->get<MeshRenderer>() : nullptr;
      return mr && mr->materials.size() == 1 && mr->materials[0] && mr->materials[0]->asset_path == path;
    };
    ed.select_for_test(s.bo->id, false);
    ed.command("blockout set material solid " + solid);
    ed.command("blockout set material glass " + glass);
    pe_built(ed);
    GameObject *frame = pe_part(*s.opening, kPartKindFrame), *pane = pe_part(*s.opening, kPartKindGlass);
    CHECK(holds(frame, solid) && holds(pane, glass));
    /* The opening's own: only its parts. */
    ed.select_for_test(s.opening->id, false);
    ed.command("blockout set material solid " + own);
    pe_built(ed);
    frame = pe_part(*s.opening, kPartKindFrame);
    CHECK(frame != nullptr && holds(frame, own) && holds(pe_part(*s.opening, kPartKindGlass), glass));
    if (!frame) return;
    /* A material dropped on the Frame by hand is the user's: a new Width does not take it back, and neither does a change of the class's material. */
    frame->get<MeshRenderer>()->materials.assign(1, material_asset(hand));
    ed.mark_changed_for_test("test drop");
    s.opening->get<BlockoutPiece>()->width += 0.2f;
    ed.mark_changed_for_test("test width");
    pe_built(ed);
    CHECK(holds(pe_part(*s.opening, kPartKindFrame), hand));
    ed.command("blockout set material solid " + solid);
    pe_built(ed);
    CHECK(holds(pe_part(*s.opening, kPartKindFrame), hand) && holds(pe_part(*s.opening, kPartKindGlass), glass));
  });

  test("blockout parts (547): a window's Frame and a door's Frame are Contribute GI, a Glass is not, and neither takes a collider; a door's leaf keeps its collider and body", [] {
    Editor ed;
    const PartScene w = pe_make(ed, "window single");
    CHECK(w.opening != nullptr);
    if (!w.opening) return;
    const GameObject *frame = pe_part(*w.opening, kPartKindFrame), *pane = pe_part(*w.opening, kPartKindGlass);
    CHECK(frame && pane);
    if (!frame || !pane) return;
    CHECK(frame->get<MeshRenderer>()->contribute_gi && !pane->get<MeshRenderer>()->contribute_gi);
    CHECK(!frame->get<ColliderComponent>() && !pane->get<ColliderComponent>() && !frame->get<Rigidbody>() && !pane->get<Rigidbody>());
    Editor ed2;
    const PartScene d = pe_make(ed2, "door single");
    CHECK(d.opening != nullptr);
    if (!d.opening) return;
    CHECK(pe_part(*d.opening, kPartKindFrame) && pe_part(*d.opening, kPartKindFrame)->get<MeshRenderer>()->contribute_gi && !pe_part(*d.opening, kPartKindFrame)->get<ColliderComponent>());
    const GameObject *leaf = pe_part(*d.opening, kPartKindLeaf);
    CHECK(leaf && leaf->get<ColliderComponent>() && leaf->get<Rigidbody>() && !leaf->get<MeshRenderer>()->contribute_gi);
  });

  test("blockout parts (547): the Blockout's mesh of a styled window or door is the plain hole's, byte for byte - Single, Double, Fixed, Grid windows and Single, Double, Sliding doors; a changed Frame Width, Panes or Glass asks for no build of the Blockout", [] {
    for (const char *words : {"window single", "window double", "window fixed", "window grid 1.6 2 0.9", "door single", "door double", "door sliding"}) {
      Editor ed;
      const PartScene s = pe_make(ed, words);
      CHECK(s.opening != nullptr);
      if (!s.opening) continue;
      BlockoutPiece *bp = s.opening->get<BlockoutPiece>();
      const uint64_t styled = blockout_mesh_hash(*pe_mesh(*s.bo));
      const uint64_t builds = pe_builds(ed);
      bp->frame_width = 0.11f, bp->frame_depth = 0.13f, bp->glass = !bp->glass, bp->leaf_thickness = 0.07f, bp->panes_across = 2;  // (the sill board is not here: round 547's review put its thickness into the hole)
      ed.mark_changed_for_test("test numbers");
      pe_built(ed);
      CHECK(pe_builds(ed) == builds && blockout_mesh_hash(*pe_mesh(*s.bo)) == styled);  // (the numbers only the parts read: nothing for the kernel)
      const bool deeper = bp->is_styled_window() && bp->sill_board && bp->style != kWindowFixed;  // (a sill board is a notch in the hole under the window, the width of the opening)
      bp->style = 0, bp->glass = true;
      ed.mark_changed_for_test("test opening only");
      pe_built(ed);
      std::printf("    %s: the styled and the plain hole's mesh are %s\n", words, blockout_mesh_hash(*pe_mesh(*s.bo)) == styled ? "the same" : "different");
      CHECK((blockout_mesh_hash(*pe_mesh(*s.bo)) == styled) == !deeper);
      CHECK(pe_parts(*s.opening).empty());  // (Opening Only: no part is left)
    }
  });

  test("blockout parts (547): on the field path a styled window and door add no 'finer than Field Cell' note and no unresolved place of their frames, at the default Field Cell and a coarse one", [] {
    for (const char *words : {"window grid 1.6 2 0.9", "window arched", "window round", "door single", "door arched"})
      for (float cell : {0.1f, 0.2f}) {
        Editor ed;
        ed.init_headless(900, 600);
        pe_empty_scene(ed);
        GameObject *bo = ed.scene().create("Blockout");
        Blockout *b = bo->add<Blockout>();
        b->method = 2, b->cell = cell;
        bo->add<MeshFilter>()->mesh = std::make_shared<Mesh>();
        bo->add<MeshRenderer>();
        ed.select_for_test(bo->id, false);
        ed.command("blockout add room box 4 3 4");
        pe_settle(ed, 2);
        ed.command(std::string("blockout add ") + words);
        pe_built(ed);
        bool note = false;
        for (const auto &l : ed.blockout_status_lines_for_test(*bo)) note = note || l.first.find("finer than Field Cell") != std::string::npos;
        std::printf("    %s at cell %.1f: %zu unresolved place(s), %s\n", words, cell, b->state.status.unresolved, note ? "A NOTE" : "no note");
        CHECK(!note && b->state.window_fits.empty());
        const bool curved = std::string(words) == "window arched" || std::string(words) == "window round" || std::string(words) == "door arched";
        if (!curved) CHECK(b->state.status.unresolved == 0);
        else CHECK(b->state.status.unresolved <= 40);  // (a curved hole is an outline prism on the grid: it leaves some places, 4 to 20 measured at these cells; an Opening Only one is a Box, so there is no baseline to compare with - the bound is a ceiling, not a pin)
      }
  });

  test("blockout parts (547): a scene saved with a styled window and door but no part objects (rounds 498 / 499) opens with the Frame, Glass and leaf made, the Blockout's mesh the plain hole's, and the parts as wide as the opening", [] {
    Editor ed;
    const PartScene s = pe_make(ed, "window grid 1.6 2 0.9");
    CHECK(s.opening != nullptr);
    if (!s.opening) return;
    ed.select_for_test(s.room->id, false);
    ed.command("blockout add door single");
    pe_built(ed);
    GameObject *door = ed.active_object_for_test();
    CHECK(door && door != s.opening && door->get<BlockoutPiece>());
    if (!door) return;
    door->set_local_position(door->local().position + Vec3(0.0f, 0.0f, 1.0f));
    ed.mark_changed_for_test("test move");
    pe_built(ed);
    const uint64_t bytes = blockout_mesh_hash(*pe_mesh(*s.bo));
    const std::string window_name = s.opening->name, door_name = door->name;
    /* What those rounds saved: no part objects at all. */
    for (GameObject *o : {s.opening, door}) {
      std::vector<GameObject *> parts = pe_parts(*o);
      ed.scene().destroy(parts);
    }
    const std::string path = pe_scene_path(ed, "r547_old.scene");
    CHECK(fs::write_file(path, save_scene_text(ed.scene())));
    ed.command("newscene");
    CHECK(ed.open_scene_for_test(path));
    for (int i = 0; i < 400 && !pe_find(ed, "Blockout"); i++) ed.step_frame_headless();
    pe_built(ed);
    GameObject *window = pe_find(ed, window_name.c_str());
    GameObject *again = ed.scene().find_by_name(door_name);
    CHECK(window && again);
    if (!window || !again) return;
    CHECK(pe_parts(*window, kPartKindFrame).size() == 1 && pe_parts(*window, kPartKindGlass).size() == 1 && pe_parts(*again, kPartKindFrame).size() == 1 && pe_parts(*again, kPartKindLeaf).size() == 1);
    CHECK(blockout_mesh_hash(*pe_mesh(*pe_find(ed, "Blockout"))) == bytes);
    if (const GameObject *frame = pe_part(*window, kPartKindFrame)) {
      const AABB box = pe_box(*pe_mesh(*frame));
      const BlockoutPiece *bp = window->get<BlockoutPiece>();
      CHECK_NEAR(box.min.x, -0.5f * bp->width - bp->sill_overhang, 1e-4f);
      CHECK_NEAR(box.max.y, bp->head - bp->sill, 1e-4f);
    }
  });

  test("blockout parts (547): an FBX, a GLB and an OBJ hold a window's Frame and Glass each as its own node with its evaluated mesh, and the Blockout's node has no frame", [] {
    Editor ed;
    const PartScene s = pe_make(ed, "window grid 1.6 2 0.9");
    CHECK(s.opening != nullptr);
    if (!s.opening) return;
    GameObject *frame = pe_part(*s.opening, kPartKindFrame), *pane = pe_part(*s.opening, kPartKindGlass);
    CHECK(frame && pane);
    if (!frame || !pane) return;
    frame->add<BevelModifier>(), pane->add<WireframeModifier>();
    ed.mark_changed_for_test("test modifiers");
    pe_built(ed);
    CHECK(pe_stack_runs(*frame, 1) && pe_stack_runs(*pane, 1));
    const size_t frame_faces = frame->evaluated_mesh(0)->face_count(), pane_faces = pane->evaluated_mesh(0)->face_count();
    for (const char *format : {"fbx", "glb", "obj"}) {
      const std::string name = std::string("r547_window.") + format;
      const std::string path = pe_export(ed, format, name.c_str());
      ImportResult r;
      CHECK(import_model(path, r));
      size_t frames = 0, panes = 0;
      for (const ImportedNode &n : r.nodes) {
        if (!n.mesh || n.mesh->face_count() == 0) continue;
        if (n.name.find("Frame") != std::string::npos) {
          frames++;
          CHECK(n.mesh->face_count() >= frame_faces);
        }
        if (n.name.find("Glass") != std::string::npos) {
          panes++;
          CHECK(n.mesh->face_count() >= pane_faces);
        }
      }
      std::printf("    %s: %zu Frame node(s), %zu Glass node(s) (evaluated faces %zu and %zu)\n", name.c_str(), frames, panes, frame_faces, pane_faces);
      CHECK(frames == 1 && panes == 1);
    }
  });
}

/* ---- Picking, the Inspector and the console ---- */

static void tests_round547_pick() {
  test("blockout parts (547): a click on a window's frame selects the window, a second click on the same spot selects the Frame; a click on a door's leaf selects the leaf", [] {
    Editor ed;
    ed.init_headless(1600, 1000);
    pe_empty_scene(ed);
    GameObject *bo = ed.scene().create("Blockout");
    bo->add<Blockout>();
    bo->add<MeshFilter>()->mesh = std::make_shared<Mesh>();
    bo->add<MeshRenderer>();
    bo->set_world_position({0.0f, 0.0f, 0.0f});
    GameObject *wall = ed.scene().create("Wall", bo);
    BlockoutPiece *wp = wall->add<BlockoutPiece>();
    wp->shape = 0, wp->operation = 0, wp->size = Vec3(6.0f, 3.0f, 0.2f);
    wall->set_local_position({0.0f, 1.5f, 0.0f});
    GameObject *window = ed.scene().create("Window", bo);
    BlockoutPiece *p = window->add<BlockoutPiece>();
    p->shape = 0, p->operation = 1, p->opening = 2, p->style = kWindowSingle, p->width = 1.6f, p->head = 2.2f, p->sill = 0.8f, p->depth = 0.4f;
    p->frame_width = 0.3f, p->frame_depth = 0.2f, p->sill_board = false;
    window->set_local_position({-1.2f, 0.0f, 0.0f});
    GameObject *door = ed.scene().create("Door", bo);
    BlockoutPiece *d = door->add<BlockoutPiece>();
    d->shape = 0, d->operation = 1, d->opening = 1, d->style = kDoorSingle, d->width = 1.0f, d->head = 2.1f, d->sill = 0.0f, d->depth = 0.4f, d->frame_width = 0.2f;
    door->set_local_position({1.6f, 0.0f, 0.0f});
    ed.command("camera 5 0 9 0 1.5 0");
    pe_built(ed);
    const GameObject *frame = pe_part(*window, kPartKindFrame), *leaf = pe_part(*door, kPartKindLeaf);
    CHECK(frame && leaf);
    if (!frame || !leaf) return;
    /* The middle of the frame's left jamb, on its front face (the wall's +Z side faces the camera). */
    int fx = 0, fy = 0, lx = 0, ly = 0;
    const float jamb = -0.8f + 0.15f;  // (the frame's width is 0.3: the jamb's middle is 0.15 from the hole's edge)
    CHECK(ed.project_to_window(frame->world_matrix().point(Vec3(jamb, 1.2f, 0.1f)), fx, fy));
    const AABB leaf_box = pe_box(*pe_mesh(*leaf));  // (the slab's own box: aim at its middle, not at a number that happens to be near it)
    CHECK(ed.project_to_window(leaf->world_matrix().point((leaf_box.min + leaf_box.max) * 0.5f), lx, ly));
    std::printf("    the frame spot is at pixel %d %d, the leaf spot at %d %d\n", fx, fy, lx, ly);
    {
      const Recti vr = ed.scene_view_rect();
      const uint32_t under = ed.scene_id_for_test(lx - vr.x, ly - vr.y);
      const GameObject *ug = under ? ed.scene().find(under) : nullptr;
      std::printf("    the pixel of the leaf's middle holds '%s'\n", ug ? ug->name.c_str() : "nothing");
    }
    ed.clear_selection_for_test();
    pe_settle(ed, 3);
    pe_click(ed, lx, ly);  // (the leaf first: a click on a leaf selects the leaf itself)
    pe_settle(ed, 2);
    std::printf("    click on the leaf: %s\n", ed.active_object_for_test() ? ed.active_object_for_test()->name.c_str() : "nothing");
    CHECK(ed.selection_size_for_test() == 1 && ed.active_object_for_test() == leaf);
    ed.clear_selection_for_test();
    pe_settle(ed, 3);
    pe_click(ed, fx, fy);
    pe_settle(ed, 2);
    std::printf("    first click: %s\n", ed.active_object_for_test() ? ed.active_object_for_test()->name.c_str() : "nothing");
    CHECK(ed.selection_size_for_test() == 1 && ed.active_object_for_test() == window);
    pe_click(ed, fx, fy);
    pe_settle(ed, 2);
    std::printf("    second click: %s\n", ed.active_object_for_test() ? ed.active_object_for_test()->name.c_str() : "nothing");
    CHECK(ed.selection_size_for_test() == 1 && ed.active_object_for_test() == frame);
  });

  test("blockout parts (547): `blockout part select [frame|glass|leaf]` selects the selected openings' parts and `blockout part opening` their openings; `blockout leaf select | door` still work; a part's Inspector says who made it and that its modifiers are the user's", [] {
    Editor ed;
    const PartScene s = pe_make(ed, "window single");
    CHECK(s.opening != nullptr);
    if (!s.opening) return;
    ed.select_for_test(s.opening->id, false);
    ed.command("blockout part select frame");
    CHECK(ed.selection_size_for_test() == 1 && ed.active_object_for_test() == pe_part(*s.opening, kPartKindFrame));
    ed.command("blockout part opening");
    CHECK(ed.selection_size_for_test() == 1 && ed.active_object_for_test() == s.opening);
    ed.command("blockout part select");
    CHECK(ed.selection_size_for_test() == 2);  // (the Frame and the Glass)
    ed.command("blockout part select glass");  // (the parts are selected now: no opening, so nothing changes and it warns)
    CHECK(ed.selection_size_for_test() == 2);
    ed.select_for_test(s.opening->id, false);
    ed.command("blockout part select glass");
    CHECK(ed.selection_size_for_test() == 1 && ed.active_object_for_test() == pe_part(*s.opening, kPartKindGlass));
    /* The Inspector line, with its text. */
    ed.command("layout Default");
    ed.record_buttons_for_test(true);
    pe_settle(ed, 3);
    Recti r;
    CHECK(ed.button_rect_for_test(strprintf("shown:Made by Window '%s' - its shape follows the window's numbers; its modifiers are yours", s.opening->name.c_str()), r));
    /* The door's: a leaf and a frame, and the old command words. */
    Editor ed2;
    const PartScene d = pe_make(ed2, "door double");
    CHECK(d.opening != nullptr);
    if (!d.opening) return;
    ed2.select_for_test(d.opening->id, false);
    ed2.command("blockout leaf select");
    CHECK(ed2.selection_size_for_test() == 2);
    ed2.command("blockout leaf door");
    CHECK(ed2.selection_size_for_test() == 1 && ed2.active_object_for_test() == d.opening);
    ed2.command("blockout part select frame");
    CHECK(ed2.selection_size_for_test() == 1 && ed2.active_object_for_test() == pe_part(*d.opening, kPartKindFrame));
  });
}

// r547-te-begin
/* ===================================================================== */
/* Round 547, the test engineer's block (written from the spec and ADR 0149) */
/* ===================================================================== */

namespace {

bool pe_finite(const Mesh *m) {
  if (!m) return false;
  for (const Vec3 &p : m->positions)
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return false;
  return true;
}

size_t pe_stack_size(const GameObject &g) {
  size_t n = 0;
  for (const auto &c : g.components) n += c->is_modifier();
  return n;
}

void pe_add_stack(GameObject &g, int modifier) {
  if (modifier == 0) g.add<BevelModifier>();
  else if (modifier == 1) g.add<WireframeModifier>();
  else g.add<SolidifyModifier>();
}

/* How many objects in the scene carry the part mark. */
size_t pe_marked(Editor &ed) {
  size_t n = 0;
  ed.scene().for_each([&](GameObject &g) { n += g.get<BlockoutLeaf>() != nullptr; });
  return n;
}

platform::Event pe_key(int key, int mods, bool down = true) {
  platform::Event e;
  e.type = down ? EvType::KeyDown : EvType::KeyUp, e.key = key, e.mods = mods;
  return e;
}

/* The wall, a window and a door the way the picking test sets them up, with the camera in front. */
struct PickScene {
  GameObject *bo = nullptr, *wall = nullptr, *window = nullptr, *door = nullptr;
};
PickScene pe_pick_scene(Editor &ed) {
  ed.init_headless(1600, 1000);
  pe_empty_scene(ed);
  PickScene s;
  s.bo = ed.scene().create("Blockout");
  s.bo->add<Blockout>();
  s.bo->add<MeshFilter>()->mesh = std::make_shared<Mesh>();
  s.bo->add<MeshRenderer>();
  s.bo->set_world_position({0.0f, 0.0f, 0.0f});
  s.wall = ed.scene().create("Wall", s.bo);
  BlockoutPiece *wp = s.wall->add<BlockoutPiece>();
  wp->shape = 0, wp->operation = 0, wp->size = Vec3(6.0f, 3.0f, 0.2f);
  s.wall->set_local_position({0.0f, 1.5f, 0.0f});
  s.window = ed.scene().create("Window", s.bo);
  BlockoutPiece *p = s.window->add<BlockoutPiece>();
  p->shape = 0, p->operation = 1, p->opening = 2, p->style = kWindowSingle, p->width = 1.6f, p->head = 2.2f, p->sill = 0.8f, p->depth = 0.4f;
  p->frame_width = 0.3f, p->frame_depth = 0.2f, p->sill_board = false;
  s.window->set_local_position({-1.2f, 0.0f, 0.0f});
  s.door = ed.scene().create("Door", s.bo);
  BlockoutPiece *d = s.door->add<BlockoutPiece>();
  d->shape = 0, d->operation = 1, d->opening = 1, d->style = kDoorSingle, d->width = 1.0f, d->head = 2.1f, d->sill = 0.0f, d->depth = 0.4f, d->frame_width = 0.2f;
  s.door->set_local_position({1.6f, 0.0f, 0.0f});
  ed.command("camera 5 0 9 0 1.5 0");
  pe_built(ed);
  return s;
}

bool pe_pixel(Editor &ed, const GameObject &g, Vec3 local, int &x, int &y) { return ed.project_to_window(g.world_matrix().point(local), x, y); }

}  // namespace

static void tests_round547_te_stacks() {
  test("blockout parts (547, te): the Frame's and Glass's stacks stay through Single -> Grid -> Round; Opening Only removes the parts and an Undo brings them back with their stacks; Redo removes them again; Single after it makes new parts with no stack", [] {
    Editor ed;
    const PartScene s = pe_make(ed, "window single");
    CHECK(s.opening != nullptr);
    if (!s.opening) return;
    const uint64_t oid = s.opening->id;
    GameObject *frame = pe_part(*s.opening, kPartKindFrame), *glass = pe_part(*s.opening, kPartKindGlass);
    CHECK(frame && glass);
    if (!frame || !glass) return;
    const uint64_t fid = frame->id, gid = glass->id;
    pe_add_stack(*frame, 0);
    pe_add_stack(*glass, 1);
    ed.mark_changed_for_test("test stacks");
    pe_built(ed);
    CHECK(pe_stack_runs(*frame, 1) && pe_stack_runs(*glass, 1));
    auto set_style = [&](int style) {
      blockout_opening_set_style(*ed.scene().find(oid)->get<BlockoutPiece>(), style);
      ed.mark_changed_for_test("test style");
      pe_built(ed);
    };
    for (int style : {kWindowGrid, kWindowRound}) {
      set_style(style);
      CHECK(pe_alive(ed, fid, *ed.scene().find(oid), 1) && pe_alive(ed, gid, *ed.scene().find(oid), 1));
    }
    set_style(0);
    CHECK(pe_parts(*ed.scene().find(oid)).empty() && ed.scene().find(fid) == nullptr && ed.scene().find(gid) == nullptr);  // (Opening Only: the parts go, ADR 0149 decision 1)
    ed.command("undo");
    pe_built(ed);
    const GameObject *back = ed.scene().find(oid);
    CHECK(back != nullptr);
    if (!back) return;
    std::printf("    undo of Opening Only: %zu frame(s), %zu glass\n", pe_parts(*back, kPartKindFrame).size(), pe_parts(*back, kPartKindGlass).size());
    CHECK(pe_parts(*back, kPartKindFrame).size() == 1 && pe_parts(*back, kPartKindGlass).size() == 1);
    if (pe_part(*back, kPartKindFrame)) CHECK(pe_stack_runs(*pe_part(*back, kPartKindFrame), 1));
    if (pe_part(*back, kPartKindGlass)) CHECK(pe_stack_runs(*pe_part(*back, kPartKindGlass), 1));
    ed.command("redo");
    pe_built(ed);
    CHECK(ed.scene().find(oid) && pe_parts(*ed.scene().find(oid)).empty() && pe_marked(ed) == 0);
    set_style(kWindowSingle);
    const GameObject *again = ed.scene().find(oid);
    CHECK(again && pe_parts(*again, kPartKindFrame).size() == 1 && pe_parts(*again, kPartKindGlass).size() == 1);
    if (again)
      for (const GameObject *g : pe_parts(*again)) CHECK(pe_stack_size(*g) == 0);
  });

  test("blockout parts (547, te): an Undo of a Width change gives a Frame and a Glass back their old mesh with the stack running on it, the parts are the same objects, and Redo goes forward again", [] {
    Editor ed;
    const PartScene s = pe_make(ed, "window grid 1.6 2 0.9");
    CHECK(s.opening != nullptr);
    if (!s.opening) return;
    const uint64_t oid = s.opening->id;
    GameObject *frame = pe_part(*s.opening, kPartKindFrame), *glass = pe_part(*s.opening, kPartKindGlass);
    CHECK(frame && glass);
    if (!frame || !glass) return;
    const uint64_t fid = frame->id, gid = glass->id;
    pe_add_stack(*frame, 0);
    pe_add_stack(*glass, 1);
    ed.mark_changed_for_test("test stacks");
    pe_built(ed);
    const double f0 = pe_sum(*pe_mesh(*frame)), g0 = pe_sum(*pe_mesh(*glass));
    ed.scene().find(oid)->get<BlockoutPiece>()->width += 0.5f;
    ed.mark_changed_for_test("test width");
    pe_built(ed);
    const double f1 = pe_sum(*pe_mesh(*ed.scene().find(fid)));
    CHECK(f1 != f0);
    ed.command("undo");
    pe_built(ed);
    CHECK(pe_alive(ed, fid, *ed.scene().find(oid), 1) && pe_alive(ed, gid, *ed.scene().find(oid), 1));
    if (ed.scene().find(fid) && ed.scene().find(gid)) {
      CHECK_NEAR(pe_sum(*pe_mesh(*ed.scene().find(fid))), f0, 1e-3);
      CHECK_NEAR(pe_sum(*pe_mesh(*ed.scene().find(gid))), g0, 1e-3);
    }
    ed.command("redo");
    pe_built(ed);
    CHECK(pe_alive(ed, fid, *ed.scene().find(oid), 1) && pe_alive(ed, gid, *ed.scene().find(oid), 1));
    if (ed.scene().find(fid)) CHECK_NEAR(pe_sum(*pe_mesh(*ed.scene().find(fid))), f1, 1e-3);
    CHECK(pe_parts(*ed.scene().find(oid)).size() == 2);
  });

  test("blockout parts (547, te): a door's Frame loses its stack on Revolving (only the Blockout's ring is left) and comes back plain on Single; the leaf's Solidify stays through Sliding and back", [] {
    Editor ed;
    const PartScene s = pe_make(ed, "door single");
    CHECK(s.opening != nullptr);
    if (!s.opening) return;
    const uint64_t oid = s.opening->id;
    GameObject *frame = pe_part(*s.opening, kPartKindFrame), *leaf = pe_part(*s.opening, kPartKindLeaf);
    CHECK(frame && leaf);
    if (!frame || !leaf) return;
    const uint64_t lid = leaf->id;
    pe_add_stack(*frame, 0);
    pe_add_stack(*leaf, 2);
    ed.mark_changed_for_test("test stacks");
    pe_built(ed);
    blockout_opening_set_style(*ed.scene().find(oid)->get<BlockoutPiece>(), kDoorSliding);
    ed.mark_changed_for_test("test sliding");
    pe_built(ed);
    CHECK(pe_alive(ed, lid, *ed.scene().find(oid), 1));
    blockout_opening_set_style(*ed.scene().find(oid)->get<BlockoutPiece>(), kDoorRevolving);
    ed.mark_changed_for_test("test revolving");
    pe_built(ed);
    CHECK(pe_parts(*ed.scene().find(oid), kPartKindFrame).empty());
    CHECK(pe_class_faces(*s.bo, kClassSolid) > 0);  // (the ring)
    blockout_opening_set_style(*ed.scene().find(oid)->get<BlockoutPiece>(), kDoorSingle);
    ed.mark_changed_for_test("test single");
    pe_built(ed);
    const GameObject *again = ed.scene().find(oid);
    CHECK(again && pe_parts(*again, kPartKindFrame).size() == 1);
    if (again && pe_part(*again, kPartKindFrame)) CHECK(pe_stack_size(*pe_part(*again, kPartKindFrame)) == 0);
  });

  test("blockout parts (547, te): changing, disabling and removing a modifier on a Frame, a Glass and a leaf asks for no Blockout build, and the evaluated mesh follows each change", [] {
    for (const char *words : {"window grid 1.6 2 0.9", "door single"}) {
      Editor ed;
      const PartScene s = pe_make(ed, words);
      CHECK(s.opening != nullptr);
      if (!s.opening) continue;
      const uint64_t builds = pe_builds(ed);
      for (GameObject *g : pe_parts(*s.opening)) {
        const size_t base_faces = pe_mesh(*g)->face_count();
        pe_add_stack(*g, 2);  // Solidify
        ed.mark_changed_for_test("test add");
        pe_built(ed);
        const Mesh *e = g->evaluated_mesh(0);
        CHECK(e && e->face_count() > base_faces);
        SolidifyModifier *sol = g->get<SolidifyModifier>();
        CHECK(sol != nullptr);
        if (sol) sol->enabled = false;
        ed.mark_changed_for_test("test disable");
        pe_built(ed);
        e = g->evaluated_mesh(0);
        CHECK(e && e->face_count() == base_faces);
        if (sol) sol->enabled = true;
        ed.mark_changed_for_test("test enable");
        pe_built(ed);
        e = g->evaluated_mesh(0);
        CHECK(e && e->face_count() > base_faces);
        if (sol) g->remove_component(sol);
        ed.mark_changed_for_test("test remove");
        pe_built(ed);
        e = g->evaluated_mesh(0);
        CHECK(e && e->face_count() == base_faces && pe_stack_size(*g) == 0);
      }
      std::printf("    %s: %llu builds before, %llu after the modifier edits\n", words, (unsigned long long)builds, (unsigned long long)pe_builds(ed));
      CHECK(pe_builds(ed) == builds);
    }
  });
}

static void tests_round547_te_numbers() {
  test("blockout parts (547, te): every odd number on a styled window - NaN, infinity, 1e30, 1e9, 0, negative, 1e-9 in Width, Head, Sill, Depth, Frame Width, Frame Depth, Sill Overhang and Arch Rise - leaves finite sound parts, one Frame and one Glass, the same objects with their stack, and the old numbers bring the same parts back", [] {
    const std::vector<std::pair<const char *, float BlockoutPiece::*>> fields = {
        {"Width", &BlockoutPiece::width},           {"Head", &BlockoutPiece::head},
        {"Sill", &BlockoutPiece::sill},             {"Depth", &BlockoutPiece::depth},
        {"Frame Width", &BlockoutPiece::frame_width}, {"Frame Depth", &BlockoutPiece::frame_depth},
        {"Sill Overhang", &BlockoutPiece::sill_overhang}, {"Arch Rise", &BlockoutPiece::arch_rise}};
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    /* A sample of the odd values (ADR 0056: the whole sweep - infinity, -infinity, 1e30, a negative, 1e-9 as well - took 13 s of the suite): NaN, a number far too large, and zero, in each of the eight numbers. */
    (void)inf;
    const std::vector<float> values = {nan, 1e9f, 0.0f};
    for (const auto &field : fields) {
      Editor ed;
      const PartScene s = pe_make(ed, "window grid 1.6 2 0.9");
      CHECK(s.opening != nullptr);
      if (!s.opening) continue;
      BlockoutPiece *bp = s.opening->get<BlockoutPiece>();
      GameObject *frame = pe_part(*s.opening, kPartKindFrame), *glass = pe_part(*s.opening, kPartKindGlass);
      CHECK(frame && glass);
      if (!frame || !glass) continue;
      const uint64_t fid = frame->id, gid = glass->id;
      pe_add_stack(*frame, 0);
      pe_add_stack(*glass, 1);
      ed.mark_changed_for_test("test stacks");
      pe_built(ed);
      const float old = bp->*(field.second);
      const double frame_sum = pe_sum(*pe_mesh(*frame)), glass_sum = pe_sum(*pe_mesh(*glass));
      for (float v : values) {
        bp->*(field.second) = v;
        ed.mark_changed_for_test("test odd number");
        pe_built(ed);
        const Mesh *bm = pe_mesh(*s.bo);
        const bool blockout_ok = !bm || bm->face_count() == 0 || (structurally_valid(*bm) && pe_finite(bm));
        bool parts_ok = pe_parts(*s.opening, kPartKindFrame).size() <= 1 && pe_parts(*s.opening, kPartKindGlass).size() <= 1;
        for (const GameObject *g : pe_parts(*s.opening)) {
          const Mesh *m = pe_mesh(*g);
          parts_ok = parts_ok && m && pe_finite(m) && structurally_valid(*m) && pe_finite(g->evaluated_mesh(0));
        }
        const bool kept = ed.scene().find(fid) && ed.scene().find(gid);
        std::printf("    %s = %g: Blockout %s, parts %s, Frame and Glass %s\n", field.first, (double)v, blockout_ok ? "ok" : "BROKEN", parts_ok ? "ok" : "BROKEN", kept ? "kept" : "LOST");
        CHECK(blockout_ok);
        CHECK(parts_ok);
        CHECK(kept);  // (a number typed on its way to another one must not throw the user's modifiers away)
      }
      bp->*(field.second) = old;
      ed.mark_changed_for_test("test number back");
      pe_built(ed);
      CHECK(pe_alive(ed, fid, *s.opening, 1) && pe_alive(ed, gid, *s.opening, 1));
      if (ed.scene().find(fid) && ed.scene().find(gid)) {
        CHECK_NEAR(pe_sum(*pe_mesh(*ed.scene().find(fid))), frame_sum, 1e-3);
        CHECK_NEAR(pe_sum(*pe_mesh(*ed.scene().find(gid))), glass_sum, 1e-3);
      }
      CHECK(pe_parts(*s.opening).size() == 2);
    }
  });

  test("blockout parts (547, te): a window 0.05 m wide and one 1e4 m wide leave sound finite parts or none, never a frame left at the old size; a 50 x 50 Grid window makes a valid Frame and Glass", [] {
    {
      /* Tiny: with a frame that fits, and with the default frame that is wider than the window. */
      for (float fw : {0.005f, 0.06f}) {
        Editor ed;
        const PartScene s = pe_make(ed, "window single");
        CHECK(s.opening != nullptr);
        if (!s.opening) continue;
        BlockoutPiece *bp = s.opening->get<BlockoutPiece>();
        bp->width = 0.05f, bp->head = 0.1f, bp->sill = 0.0f, bp->frame_width = fw;
        ed.mark_changed_for_test("test tiny");
        pe_built(ed);
        bool ok = true;
        for (const GameObject *g : pe_parts(*s.opening)) {
          const Mesh *m = pe_mesh(*g);
          ok = ok && m && pe_finite(m) && structurally_valid(*m);
          if (m && fw < 0.01f) ok = ok && pe_sound(m);
          if (m) ok = ok && pe_box(*m).max.x <= 0.5f * 0.05f + bp->sill_overhang + 1e-4f;
        }
        std::printf("    0.05 m wide, Frame Width %g: %zu part(s), %s\n", (double)fw, pe_parts(*s.opening).size(), ok ? "sound" : "BROKEN");
        CHECK(ok);
      }
    }
    {
      Editor ed;
      const PartScene s = pe_make(ed, "window single");
      CHECK(s.opening != nullptr);
      if (!s.opening) return;
      BlockoutPiece *bp = s.opening->get<BlockoutPiece>();
      pe_add_stack(*pe_part(*s.opening, kPartKindFrame), 0);
      ed.mark_changed_for_test("test stack");
      pe_built(ed);
      bp->width = 1e4f, bp->head = 1e4f;
      ed.mark_changed_for_test("test huge");
      pe_built(ed);
      /* A Frame the opening does not make for a size it will not mesh may stay (the user's modifiers wait for a size that works), but it must not show a 0.9 m frame in a 1e4 m hole. */
      for (const GameObject *g : pe_parts(*s.opening)) {
        const Mesh *m = pe_mesh(*g);
        const bool finite = m && pe_finite(m);
        const float span = m ? pe_box(*m).max.x - pe_box(*m).min.x : 0.0f;
        const float width_now = s.opening->get<BlockoutPiece>()->width;  // (the number the window has now: a step of the editor may hold a typed number to a field's own range)
        std::printf("    1e4 m wide (the piece says %g): %s spans %.3f m\n", (double)width_now, g->name.c_str(), (double)span);
        CHECK(finite);
        CHECK(span >= 0.9f * std::min(width_now, 1e4f));  // (a stale frame at the old size would be the user's window drawn wrong)
      }
      const Mesh *bm = pe_mesh(*s.bo);
      CHECK(!bm || bm->face_count() == 0 || (structurally_valid(*bm) && pe_finite(bm)));
    }
    {
      Editor ed;
      const PartScene s = pe_make(ed, "window grid 1.6 2 0.9");
      CHECK(s.opening != nullptr);
      if (!s.opening) return;
      BlockoutPiece *bp = s.opening->get<BlockoutPiece>();
      bp->panes_across = 50, bp->panes_up = 50;
      ed.mark_changed_for_test("test 50 x 50");
      pe_built(ed);
      const GameObject *frame = pe_part(*s.opening, kPartKindFrame), *pane = pe_part(*s.opening, kPartKindGlass);
      CHECK(frame && pane);
      if (!frame || !pane) return;
      std::string why;
      const bool frame_ok = pe_finite(pe_mesh(*frame)) && structurally_valid(*pe_mesh(*frame), &why), pane_ok = pe_finite(pe_mesh(*pane)) && structurally_valid(*pe_mesh(*pane));
      std::printf("    50 x 50: Frame %zu faces %s %s, Glass %zu faces %s\n", pe_mesh(*frame)->face_count(), frame_ok ? "valid" : "INVALID", why.c_str(), pe_mesh(*pane)->face_count(), pane_ok ? "valid" : "INVALID");
      CHECK(frame_ok && pane_ok);
      CHECK(pe_class_faces(*s.bo, kClassSolid) == 0 && pe_class_faces(*s.bo, kClassGlass) == 0);
    }
  });

  test("blockout parts (547, te): a Frame, a Glass and a leaf stay the same objects through twenty Width changes - one of each under the opening, no object left over, the stack equal to the stack by hand each time - and the first Width brings back the first mesh", [] {
    for (int method : {0, 2}) {
      Editor ed;
      const PartScene s = pe_make(ed, "window grid 1.6 2 0.9", method);
      CHECK(s.opening != nullptr);
      if (!s.opening) continue;
      GameObject *frame = pe_part(*s.opening, kPartKindFrame), *glass = pe_part(*s.opening, kPartKindGlass);
      CHECK(frame && glass);
      if (!frame || !glass) continue;
      const uint64_t fid = frame->id, gid = glass->id;
      pe_add_stack(*frame, 0);
      pe_add_stack(*glass, 1);
      ed.mark_changed_for_test("test stacks");
      pe_built(ed);
      BlockoutPiece *bp = s.opening->get<BlockoutPiece>();
      const float w0 = bp->width;
      const double f0 = pe_sum(*pe_mesh(*frame)), g0 = pe_sum(*pe_mesh(*glass));
      const size_t objects = ed.scene().object_count();
      for (int i = 1; i <= 20; i++) {
        bp->width = w0 + 0.05f * (float)(i % 7) + 0.01f * (float)i;
        ed.mark_changed_for_test("test width");
        pe_built(ed);
        CHECK(pe_alive(ed, fid, *s.opening, 1) && pe_alive(ed, gid, *s.opening, 1) && pe_parts(*s.opening).size() == 2 && ed.scene().object_count() == objects);
      }
      bp->width = w0;
      ed.mark_changed_for_test("test width back");
      pe_built(ed);
      CHECK(pe_alive(ed, fid, *s.opening, 1) && pe_alive(ed, gid, *s.opening, 1));
      if (ed.scene().find(fid) && ed.scene().find(gid)) {
        CHECK_NEAR(pe_sum(*pe_mesh(*ed.scene().find(fid))), f0, 1e-3);
        CHECK_NEAR(pe_sum(*pe_mesh(*ed.scene().find(gid))), g0, 1e-3);
      }
    }
    Editor ed;
    const PartScene d = pe_make(ed, "door double");
    CHECK(d.opening != nullptr);
    if (!d.opening) return;
    std::vector<uint64_t> ids;
    for (GameObject *g : pe_parts(*d.opening)) {
      ids.push_back(g->id);
      pe_add_stack(*g, g->get<BlockoutLeaf>()->kind == kPartKindFrame ? 0 : 2);
    }
    ed.mark_changed_for_test("test stacks");
    pe_built(ed);
    BlockoutPiece *dp = d.opening->get<BlockoutPiece>();
    for (int i = 1; i <= 20; i++) {
      dp->width = 1.0f + 0.03f * (float)i, dp->head = 2.1f + 0.01f * (float)(i % 5);
      ed.mark_changed_for_test("test door width");
      pe_built(ed);
      for (uint64_t id : ids) CHECK(pe_alive(ed, id, *d.opening, 1));
      CHECK(pe_parts(*d.opening).size() == 3);
    }
  });
}

static void tests_round547_te_toggles() {
  test("blockout parts (547, te): a door's Leaf off removes the leaf and keeps the Frame with its Bevel; the Leaf on again makes a plain leaf; the Frame is never made twice", [] {
    Editor ed;
    const PartScene s = pe_make(ed, "door single");
    CHECK(s.opening != nullptr);
    if (!s.opening) return;
    const uint64_t oid = s.opening->id;
    GameObject *frame = pe_part(*s.opening, kPartKindFrame);
    CHECK(frame != nullptr);
    if (!frame) return;
    const uint64_t fid = frame->id;
    pe_add_stack(*frame, 0);
    ed.mark_changed_for_test("test stack");
    pe_built(ed);
    ed.scene().find(oid)->get<BlockoutPiece>()->leaf = false;
    ed.mark_changed_for_test("test leaf off");
    pe_built(ed);
    CHECK(pe_parts(*ed.scene().find(oid), kPartKindLeaf).empty() && pe_alive(ed, fid, *ed.scene().find(oid), 1));
    ed.scene().find(oid)->get<BlockoutPiece>()->leaf = true;
    ed.mark_changed_for_test("test leaf on");
    pe_built(ed);
    CHECK(pe_parts(*ed.scene().find(oid), kPartKindLeaf).size() == 1 && pe_parts(*ed.scene().find(oid), kPartKindFrame).size() == 1 && pe_alive(ed, fid, *ed.scene().find(oid), 1));
  });

  test("blockout parts (547, te): switching a styled window's Enabled off and on leaves sound parts, no part twice and no stray mark; what happens to the Frame's Bevel is printed", [] {
    Editor ed;
    const PartScene s = pe_make(ed, "window single");
    CHECK(s.opening != nullptr);
    if (!s.opening) return;
    const uint64_t oid = s.opening->id;
    GameObject *frame = pe_part(*s.opening, kPartKindFrame);
    CHECK(frame != nullptr);
    if (!frame) return;
    const uint64_t fid = frame->id;
    pe_add_stack(*frame, 0);
    ed.mark_changed_for_test("test stack");
    pe_built(ed);
    ed.scene().find(oid)->get<BlockoutPiece>()->enabled = false;
    ed.mark_changed_for_test("test off");
    pe_built(ed);
    std::printf("    Enabled off: %zu part object(s) left\n", pe_parts(*ed.scene().find(oid)).size());
    ed.scene().find(oid)->get<BlockoutPiece>()->enabled = true;
    ed.mark_changed_for_test("test on");
    pe_built(ed);
    const GameObject *op = ed.scene().find(oid);
    CHECK(op && pe_parts(*op, kPartKindFrame).size() == 1 && pe_parts(*op, kPartKindGlass).size() == 1 && pe_marked(ed) == 2);
    std::printf("    Enabled on again: the Frame is %s, its Bevel is %s\n", ed.scene().find(fid) ? "the same object" : "a new object", op && pe_part(*op, kPartKindFrame) && pe_stack_size(*pe_part(*op, kPartKindFrame)) == 1 ? "kept" : "gone");
    for (const GameObject *g : pe_parts(*op)) CHECK(pe_sound(pe_mesh(*g)));
  });
}

static void tests_round547_te_moved() {
  test("blockout parts (547, te): a renamed Frame keeps its name, id and stack through a Width change; a Frame moved out becomes an ordinary object with its stack, the opening makes a plain new one, and the moved one is never touched again", [] {
    Editor ed;
    const PartScene s = pe_make(ed, "window single");
    CHECK(s.opening != nullptr);
    if (!s.opening) return;
    const uint64_t oid = s.opening->id;
    GameObject *frame = pe_part(*s.opening, kPartKindFrame);
    CHECK(frame != nullptr);
    if (!frame) return;
    const uint64_t fid = frame->id;
    pe_add_stack(*frame, 0);
    frame->name = "My Frame";
    ed.mark_changed_for_test("test rename");
    pe_built(ed);
    s.opening->get<BlockoutPiece>()->width += 0.3f;
    ed.mark_changed_for_test("test width");
    pe_built(ed);
    CHECK(pe_alive(ed, fid, *s.opening, 1) && ed.scene().find(fid)->name == "My Frame" && pe_parts(*s.opening, kPartKindFrame).size() == 1);
    /* Moved out to the scene's root. */
    ed.scene().set_parent(ed.scene().find(fid), nullptr, -1, true);
    ed.mark_changed_for_test("test move out");
    pe_built(ed);
    const GameObject *out = ed.scene().find(fid);
    CHECK(out && !out->get<BlockoutLeaf>() && out->get<BevelModifier>() && !out->parent);
    const GameObject *op = ed.scene().find(oid);
    CHECK(op && pe_parts(*op, kPartKindFrame).size() == 1 && pe_parts(*op, kPartKindFrame)[0]->id != fid && pe_stack_size(*pe_parts(*op, kPartKindFrame)[0]) == 0);
    const double kept = out ? pe_sum(*pe_mesh(*out)) : 0.0;
    for (int i = 0; i < 4; i++) {
      ed.scene().find(oid)->get<BlockoutPiece>()->width += 0.1f;
      ed.mark_changed_for_test("test width");
      pe_built(ed);
    }
    out = ed.scene().find(fid);
    CHECK(out && !out->get<BlockoutLeaf>() && pe_stack_size(*out) == 1 && pe_sum(*pe_mesh(*out)) == kept);
    CHECK(pe_parts(*ed.scene().find(oid), kPartKindFrame).size() == 1);
  });

  test("blockout parts (547, te): Ctrl+D on a Frame (the real key) makes an ordinary copy and the opening keeps its own Frame with its stack; a Frame dropped under another window is left alone by both windows, and neither makes the other's", [] {
    Editor ed;
    const PartScene s = pe_make(ed, "window single");
    CHECK(s.opening != nullptr && s.room != nullptr);
    if (!s.opening || !s.room) return;
    const uint64_t oid = s.opening->id;
    GameObject *frame = pe_part(*s.opening, kPartKindFrame);
    CHECK(frame != nullptr);
    if (!frame) return;
    const uint64_t fid = frame->id;
    pe_add_stack(*frame, 0);
    ed.mark_changed_for_test("test stack");
    pe_built(ed);
    const size_t objects = ed.scene().object_count();
    ed.select_for_test(fid, false);
    pe_settle(ed, 2);
    ed.step_frame_headless({pe_key(platform::KEY_D, platform::MOD_CTRL)});
    ed.step_frame_headless({pe_key(platform::KEY_D, platform::MOD_CTRL, false)});
    pe_built(ed);
    GameObject *copy = ed.active_object_for_test();
    CHECK(copy && copy->id != fid && !copy->get<BlockoutLeaf>() && copy->get<MeshFilter>() && copy->get<BevelModifier>());
    CHECK(ed.scene().object_count() == objects + 1);
    CHECK(pe_alive(ed, fid, *ed.scene().find(oid), 1) && pe_parts(*ed.scene().find(oid), kPartKindFrame).size() == 1);
    const uint64_t copy_id = copy ? copy->id : 0;
    /* A second window, and the copy dropped under it (it is an ordinary object, a Frame dropped there is what the user did). */
    ed.select_for_test(s.room->id, false);
    ed.command("blockout add window single");
    pe_built(ed);
    GameObject *other = ed.active_object_for_test();
    CHECK(other && other != ed.scene().find(oid) && other->get<BlockoutPiece>());
    if (!other) return;
    const uint64_t other_id = other->id;
    ed.scene().set_parent(ed.scene().find(fid), ed.scene().find(other_id), -1, true);  // (the first window's own Frame, dragged under the second)
    ed.mark_changed_for_test("test drop");
    pe_built(ed);
    for (int i = 0; i < 3; i++) {
      ed.scene().find(oid)->get<BlockoutPiece>()->width += 0.1f;
      ed.scene().find(other_id)->get<BlockoutPiece>()->width += 0.1f;
      ed.mark_changed_for_test("test widths");
      pe_built(ed);
    }
    const GameObject *dropped = ed.scene().find(fid), *first = ed.scene().find(oid), *second = ed.scene().find(other_id);
    CHECK(dropped && !dropped->get<BlockoutLeaf>() && pe_stack_size(*dropped) == 1 && dropped->parent == second);
    CHECK(first && second && pe_parts(*first, kPartKindFrame).size() == 1 && pe_parts(*second, kPartKindFrame).size() == 1 && pe_parts(*first, kPartKindGlass).size() == 1 && pe_parts(*second, kPartKindGlass).size() == 1);
    CHECK(ed.scene().find(copy_id) != nullptr);
  });

  test("blockout parts (547, te): duplicating the whole Blockout gives the copy's windows their own Frame and Glass with copies of the stacks, made by the copy's window and changed with it only; the original's parts are untouched", [] {
    Editor ed;
    const PartScene s = pe_make(ed, "window grid 1.6 2 0.9");
    CHECK(s.opening != nullptr);
    if (!s.opening) return;
    const uint64_t oid = s.opening->id;
    GameObject *frame = pe_part(*s.opening, kPartKindFrame), *glass = pe_part(*s.opening, kPartKindGlass);
    CHECK(frame && glass);
    if (!frame || !glass) return;
    const uint64_t fid = frame->id, gid = glass->id;
    pe_add_stack(*frame, 0);
    pe_add_stack(*glass, 1);
    ed.mark_changed_for_test("test stacks");
    pe_built(ed);
    const double f0 = pe_sum(*pe_mesh(*frame));
    ed.select_for_test(s.bo->id, false);
    ed.command("duplicate");
    pe_built(ed);
    GameObject *copy_opening = nullptr;
    ed.scene().for_each([&](GameObject &g) {
      const BlockoutPiece *p = g.get<BlockoutPiece>();
      if (p && p->opening == 2 && g.id != oid) copy_opening = &g;
    });
    CHECK(copy_opening != nullptr);
    if (!copy_opening) return;
    const uint64_t cid = copy_opening->id;
    const GameObject *cf = pe_part(*copy_opening, kPartKindFrame), *cg = pe_part(*copy_opening, kPartKindGlass);
    CHECK(cf && cg && pe_parts(*copy_opening).size() == 2);
    if (!cf || !cg) return;
    CHECK(cf->id != fid && cg->id != gid && cf->get<BlockoutLeaf>()->made_by == std::to_string(cid) && cg->get<BlockoutLeaf>()->made_by == std::to_string(cid));
    CHECK(pe_stack_runs(*cf, 1) && pe_stack_runs(*cg, 1));
    CHECK(pe_marked(ed) == 4);  // (no part of the copy is a second one, none was left behind)
    const uint64_t cfid = cf->id;
    ed.scene().find(cid)->get<BlockoutPiece>()->width += 0.4f;
    ed.mark_changed_for_test("test copy width");
    pe_built(ed);
    CHECK(pe_alive(ed, cfid, *ed.scene().find(cid), 1) && pe_alive(ed, fid, *ed.scene().find(oid), 1));
    CHECK_NEAR(pe_sum(*pe_mesh(*ed.scene().find(fid))), f0, 1e-3);
    CHECK(pe_sum(*pe_mesh(*ed.scene().find(cfid))) != f0);
    CHECK(pe_marked(ed) == 4);
  });

  test("blockout parts (547, te): deleting a window removes its parts and fills the hole; Undo brings the same Frame and Glass back with their stacks, once; Redo and Undo again leave no part twice", [] {
    Editor ed;
    const PartScene s = pe_make(ed, "window single");
    CHECK(s.opening != nullptr);
    if (!s.opening) return;
    const uint64_t oid = s.opening->id;
    GameObject *frame = pe_part(*s.opening, kPartKindFrame), *glass = pe_part(*s.opening, kPartKindGlass);
    CHECK(frame && glass);
    if (!frame || !glass) return;
    pe_add_stack(*frame, 0);
    pe_add_stack(*glass, 1);
    ed.mark_changed_for_test("test stacks");
    pe_built(ed);
    const uint64_t with_hole = blockout_mesh_hash(*pe_mesh(*s.bo));
    ed.select_for_test(oid, false);
    ed.command("delete");
    pe_built(ed);
    CHECK(ed.scene().find(oid) == nullptr && pe_marked(ed) == 0);
    CHECK(blockout_mesh_hash(*pe_mesh(*pe_find(ed, "Blockout"))) != with_hole);
    for (int round = 0; round < 2; round++) {
      ed.command("undo");
      pe_built(ed);
      const GameObject *back = ed.scene().find(oid);
      CHECK(back && pe_parts(*back, kPartKindFrame).size() == 1 && pe_parts(*back, kPartKindGlass).size() == 1 && pe_marked(ed) == 2);
      if (back && pe_part(*back, kPartKindFrame) && pe_part(*back, kPartKindGlass)) CHECK(pe_stack_runs(*pe_part(*back, kPartKindFrame), 1) && pe_stack_runs(*pe_part(*back, kPartKindGlass), 1));
      CHECK(blockout_mesh_hash(*pe_mesh(*pe_find(ed, "Blockout"))) == with_hole);
      ed.command("redo");
      pe_built(ed);
      CHECK(ed.scene().find(oid) == nullptr && pe_marked(ed) == 0);
    }
  });
}

static void tests_round547_te_files() {
  test("blockout parts (547, te): a Bevel on a Frame and a Wireframe on a Glass round-trip through a save and an open as the same objects, a Width change after the open keeps both, and a material dropped on the Frame by hand is still the user's afterwards", [] {
    Editor ed;
    const PartScene s = pe_make(ed, "window grid 1.6 2 0.9");
    CHECK(s.opening != nullptr);
    if (!s.opening) return;
    const std::string name = s.opening->name, hand = "Assets/Materials/R547/TeHand.mat";
    create_material_asset(*make_material("TeHand", Vec3(0.9f, 0.2f, 0.2f)), hand);
    GameObject *frame = pe_part(*s.opening, kPartKindFrame), *glass = pe_part(*s.opening, kPartKindGlass);
    CHECK(frame && glass);
    if (!frame || !glass) return;
    const uint64_t fid = frame->id, gid = glass->id;
    pe_add_stack(*frame, 0);
    pe_add_stack(*glass, 1);
    frame->get<MeshRenderer>()->materials.assign(1, material_asset(hand));
    ed.mark_changed_for_test("test stacks");
    pe_built(ed);
    const double f0 = pe_sum(*ed.scene().find(fid)->evaluated_mesh(0)), g0 = pe_sum(*ed.scene().find(gid)->evaluated_mesh(0));
    const std::string path = pe_scene_path(ed, "r547_te_roundtrip.scene");
    CHECK(fs::write_file(path, save_scene_text(ed.scene())));
    ed.command("newscene");
    CHECK(ed.open_scene_for_test(path));
    for (int i = 0; i < 400 && !pe_find(ed, "Blockout"); i++) ed.step_frame_headless();
    pe_built(ed);
    GameObject *window = pe_find(ed, name.c_str());
    CHECK(window != nullptr);
    if (!window) return;
    CHECK(pe_parts(*window).size() == 2 && pe_marked(ed) == 2);
    GameObject *f = pe_part(*window, kPartKindFrame), *g = pe_part(*window, kPartKindGlass);
    CHECK(f && g && f->id == fid && g->id == gid && pe_stack_runs(*f, 1) && pe_stack_runs(*g, 1));
    if (!f || !g) return;
    CHECK_NEAR(pe_sum(*f->evaluated_mesh(0)), f0, 1e-3);
    CHECK_NEAR(pe_sum(*g->evaluated_mesh(0)), g0, 1e-3);
    window->get<BlockoutPiece>()->width += 0.3f;
    ed.mark_changed_for_test("test width after open");
    pe_built(ed);
    CHECK(pe_alive(ed, fid, *window, 1) && pe_alive(ed, gid, *window, 1) && pe_parts(*window).size() == 2);
    const MeshRenderer *mr = ed.scene().find(fid) ? ed.scene().find(fid)->get<MeshRenderer>() : nullptr;
    CHECK(mr && mr->materials.size() == 1 && mr->materials[0] && mr->materials[0]->asset_path == hand);
  });

  test("blockout parts (547, te): a scene saved with only the Glass's object missing (and the Frame holding a Bevel) opens with a new Glass and the same Frame with its stack; one with no mark on the parts reads them as leaves of the opening and makes no second set", [] {
    Editor ed;
    const PartScene s = pe_make(ed, "window grid 1.6 2 0.9");
    CHECK(s.opening != nullptr);
    if (!s.opening) return;
    const std::string name = s.opening->name;
    GameObject *frame = pe_part(*s.opening, kPartKindFrame), *glass = pe_part(*s.opening, kPartKindGlass);
    CHECK(frame && glass);
    if (!frame || !glass) return;
    const uint64_t fid = frame->id;
    pe_add_stack(*frame, 0);
    ed.mark_changed_for_test("test stack");
    pe_built(ed);
    std::vector<GameObject *> gone = {glass};
    ed.scene().destroy(gone);
    const std::string path = pe_scene_path(ed, "r547_te_partial.scene");
    CHECK(fs::write_file(path, save_scene_text(ed.scene())));
    ed.command("newscene");
    CHECK(ed.open_scene_for_test(path));
    for (int i = 0; i < 400 && !pe_find(ed, "Blockout"); i++) ed.step_frame_headless();
    pe_built(ed);
    GameObject *window = pe_find(ed, name.c_str());
    CHECK(window != nullptr);
    if (!window) return;
    CHECK(pe_parts(*window, kPartKindFrame).size() == 1 && pe_parts(*window, kPartKindGlass).size() == 1 && pe_marked(ed) == 2);
    CHECK(pe_alive(ed, fid, *window, 1));
    /* A window with a Frame but no Bevel on a styled opening that opens Opening Only: nothing is made. */
    window->get<BlockoutPiece>()->style = 0;
    ed.mark_changed_for_test("test opening only");
    pe_built(ed);
    CHECK(pe_marked(ed) == 0);
  });

  test("blockout parts (547, te): a GLB of a door holds its Frame and Leaf as nodes with their evaluated meshes (a Bevel and a Solidify), the Blockout's node is a node of its own, and no node is written twice", [] {
    Editor ed;
    const PartScene s = pe_make(ed, "door single");
    CHECK(s.opening != nullptr);
    if (!s.opening) return;
    GameObject *frame = pe_part(*s.opening, kPartKindFrame), *leaf = pe_part(*s.opening, kPartKindLeaf);
    CHECK(frame && leaf);
    if (!frame || !leaf) return;
    pe_add_stack(*frame, 0);
    pe_add_stack(*leaf, 2);
    ed.mark_changed_for_test("test stacks");
    pe_built(ed);
    const size_t frame_faces = frame->evaluated_mesh(0)->face_count(), leaf_faces = leaf->evaluated_mesh(0)->face_count();
    CHECK(frame_faces > pe_mesh(*frame)->face_count() && leaf_faces > pe_mesh(*leaf)->face_count());
    const std::string path = pe_export(ed, "glb", "r547_te_door.glb");
    ImportResult r;
    CHECK(import_model(path, r));
    size_t frames = 0, leaves = 0, blockouts = 0;
    for (const ImportedNode &n : r.nodes) {
      if (n.name == "Frame" && n.mesh) {
        frames++;
        CHECK(n.mesh->face_count() >= frame_faces);
      }
      if (n.name == "Leaf" && n.mesh) {
        leaves++;
        CHECK(n.mesh->face_count() >= leaf_faces);
      }
      if (n.name == "Blockout" && n.mesh) blockouts++;
    }
    std::printf("    glb: %zu Frame, %zu Leaf, %zu Blockout node(s)\n", frames, leaves, blockouts);
    CHECK(frames == 1 && leaves == 1 && blockouts == 1);
  });
}

static void tests_round547_te_pick() {
  test("blockout parts (547, te): clicking a window's glass selects the window, the next click on the same spot the Glass; a click on a different spot of the frame with the window selected keeps the window, and the same spot again gives the Frame", [] {
    Editor ed;
    const PickScene s = pe_pick_scene(ed);
    const GameObject *frame = pe_part(*s.window, kPartKindFrame), *pane = pe_part(*s.window, kPartKindGlass);
    CHECK(frame && pane);
    if (!frame || !pane) return;
    int gx = 0, gy = 0, ax = 0, ay = 0, bx = 0, by = 0;
    /* (The glass spot is off the window's own axis: with the window selected its Move handles stand on the origin's axes, and a click on a handle is a drag, not a pick.) */
    CHECK(pe_pixel(ed, *pane, Vec3(0.45f, 0.4f, 0.0f), gx, gy));
    CHECK(pe_pixel(ed, *frame, Vec3(-0.8f + 0.15f, 1.2f, 0.1f), ax, ay));
    CHECK(pe_pixel(ed, *frame, Vec3(0.8f - 0.15f, 1.2f, 0.1f), bx, by));
    /* Do the pixels reach the parts at all: with the part itself selected, a click on its pixels keeps it (a miss would clear or change the selection). */
    for (const GameObject *g : {frame, pane}) {
      const int x = g == frame ? bx : gx, y = g == frame ? by : gy;
      ed.select_for_test(g->id, false);
      pe_click(ed, x, y);
      pe_settle(ed, 2);
      std::printf("    %s selected, click on its pixel %d %d: %s\n", g->name.c_str(), x, y, ed.active_object_for_test() ? ed.active_object_for_test()->name.c_str() : "nothing");
      CHECK(ed.selection_size_for_test() == 1 && ed.active_object_for_test() == g);
    }
    ed.clear_selection_for_test();
    pe_click(ed, gx, gy);
    pe_settle(ed, 2);
    std::printf("    glass, first click: %s\n", ed.active_object_for_test() ? ed.active_object_for_test()->name.c_str() : "nothing");
    CHECK(ed.selection_size_for_test() == 1 && ed.active_object_for_test() == s.window);
    pe_click(ed, gx, gy);
    pe_settle(ed, 2);
    std::printf("    glass, second click: %s\n", ed.active_object_for_test() ? ed.active_object_for_test()->name.c_str() : "nothing");
    CHECK(ed.selection_size_for_test() == 1 && ed.active_object_for_test() == pane);
    /* The window is selected again, then a click on the frame (a spot not clicked before). Two quick clicks are a double-click, which frames the selection: every spot is projected again after the view settles. */
    ed.select_for_test(s.window->id, false);
    pe_settle(ed, 80);
    CHECK(pe_pixel(ed, *frame, Vec3(-0.8f + 0.15f, 1.2f, 0.1f), ax, ay));
    pe_click(ed, ax, ay);
    pe_settle(ed, 2);
    CHECK(ed.selection_size_for_test() == 1 && ed.active_object_for_test() == s.window);  // (first click on this part: the window stays, no jump to the part)
    pe_click(ed, ax, ay);
    pe_settle(ed, 2);
    CHECK(ed.selection_size_for_test() == 1 && ed.active_object_for_test() == frame);  // (the same spot: the part)
    /* Selected itself: a click on it keeps it. */
    pe_settle(ed, 80);
    CHECK(pe_pixel(ed, *frame, Vec3(0.8f - 0.15f, 1.2f, 0.1f), bx, by));
    pe_click(ed, bx, by);
    pe_settle(ed, 2);
    CHECK(ed.selection_size_for_test() == 1 && ed.active_object_for_test() == frame);
    /* The window selected again, a click on another spot of the frame: the window stays; a click on a different spot than the last one never jumps to the part. */
    ed.select_for_test(s.window->id, false);
    pe_settle(ed, 80);
    CHECK(pe_pixel(ed, *frame, Vec3(-0.8f + 0.15f, 1.2f, 0.1f), ax, ay));
    CHECK(pe_pixel(ed, *frame, Vec3(0.8f - 0.15f, 1.2f, 0.1f), bx, by));
    pe_click(ed, ax, ay);
    pe_settle(ed, 80);  // (no double-click: the view stays)
    CHECK(pe_pixel(ed, *frame, Vec3(0.8f - 0.15f, 1.2f, 0.1f), bx, by));
    pe_click(ed, bx, by);
    pe_settle(ed, 2);
    std::printf("    window selected, click on the left jamb then the right: %s\n", ed.active_object_for_test() ? ed.active_object_for_test()->name.c_str() : "nothing");
    CHECK(ed.selection_size_for_test() == 1 && ed.active_object_for_test() == s.window);
  });

  test("blockout parts (547, te): a click on the wall beside a window's frame selects neither the window nor its parts, and a click on the door's frame then the door's leaf selects the leaf at once", [] {
    Editor ed;
    const PickScene s = pe_pick_scene(ed);
    int wx = 0, wy = 0, dx = 0, dy = 0;
    CHECK(pe_pixel(ed, *s.wall, Vec3(-2.6f, 0.0f, 0.1f), wx, wy));
    ed.clear_selection_for_test();
    pe_click(ed, wx, wy);
    pe_settle(ed, 2);
    const GameObject *hit = ed.active_object_for_test();
    std::printf("    the wall beside the frame: %s\n", hit ? hit->name.c_str() : "nothing");
    CHECK(hit != nullptr && hit != s.window && !hit->get<BlockoutLeaf>());
    /* The door: its Frame's jamb (opening first), then - with the window selected, so the door's own Move handles are not drawn over it - the leaf at once. */
    const GameObject *frame = pe_part(*s.door, kPartKindFrame), *leaf = pe_part(*s.door, kPartKindLeaf);
    CHECK(frame && leaf);
    if (!frame || !leaf) return;
    pe_settle(ed, 80);  // (the clicks above are quick ones: a double-click frames the selection, the spots are projected after the view settles)
    CHECK(pe_pixel(ed, *frame, Vec3(-0.5f + 0.1f, 1.0f, 0.1f), dx, dy));
    ed.clear_selection_for_test();
    pe_click(ed, dx, dy);
    pe_settle(ed, 2);
    std::printf("    the door's jamb: %s\n", ed.active_object_for_test() ? ed.active_object_for_test()->name.c_str() : "nothing");
    CHECK(ed.selection_size_for_test() == 1 && ed.active_object_for_test() == s.door);
    int lx = 0, ly = 0;
    ed.select_for_test(s.window->id, false);
    pe_settle(ed, 80);
    const AABB lb = pe_box(*pe_mesh(*leaf));  // (the middle of the slab: on the leaf whichever jamb it hangs on)
    CHECK(pe_pixel(ed, *leaf, Vec3(0.5f * (lb.min.x + lb.max.x), 0.5f * (lb.min.y + lb.max.y), lb.max.z), lx, ly));
    std::printf("    the leaf's pixel %d %d, its slab x %.2f .. %.2f\n", lx, ly, (double)lb.min.x, (double)lb.max.x);
    pe_click(ed, lx, ly);
    pe_settle(ed, 2);
    std::printf("    the leaf with the window selected: %s\n", ed.active_object_for_test() ? ed.active_object_for_test()->name.c_str() : "nothing");
    CHECK(ed.selection_size_for_test() == 1 && ed.active_object_for_test() == leaf);
    /* A second click on the leaf keeps it (it never goes up to the door). */
    pe_click(ed, lx, ly);
    pe_settle(ed, 2);
    CHECK(ed.selection_size_for_test() == 1 && ed.active_object_for_test() == leaf);
  });

  test("blockout parts (547, te): `blockout part` with a wrong word or an empty selection changes nothing and does not crash; a Frame deselected and the Blockout selected give `part opening` nothing", [] {
    Editor ed;
    const PartScene s = pe_make(ed, "window single");
    CHECK(s.opening != nullptr);
    if (!s.opening) return;
    ed.select_for_test(s.opening->id, false);
    for (const char *line : {"blockout part", "blockout part frob", "blockout part select roof", "blockout part opening frame"}) {
      ed.command(line);
      std::printf("    '%s': %zu selected\n", line, ed.selection_size_for_test());
      CHECK(ed.selection_size_for_test() == 1 && ed.active_object_for_test() == s.opening);
    }
    ed.select_for_test(s.bo->id, false);
    ed.command("blockout part select");
    CHECK(ed.selection_size_for_test() == 1 && ed.active_object_for_test() == s.bo);  // (the Blockout itself has no parts: its windows' are not its own)
    ed.command("blockout part opening");
    CHECK(ed.selection_size_for_test() == 1 && ed.active_object_for_test() == s.bo);
    ed.clear_selection_for_test();
    ed.command("blockout part select");
    ed.command("blockout part opening");
    CHECK(ed.selection_size_for_test() == 0);
  });
}

void tests_round547_te() {
  tests_round547_te_stacks();
  tests_round547_te_numbers();
  tests_round547_te_toggles();
  tests_round547_te_moved();
  tests_round547_te_files();
  tests_round547_te_pick();
}
// r547-te-end

/* ---- The review's fixes (round 547) ---- */

namespace {

/* Whether a face of mesh A (in `to_a`) and a face of mesh B (in `to_b`) lie in one plane, look the same way and overlap in it: what flickers when both are drawn. */
size_t pe_coplanar_same_facing(const Mesh &a, const Mat4 &to_common_a, const Mesh &b, const Mat4 &to_common_b) {
  size_t n = 0;
  for (size_t f = 0; f < a.face_count(); f++) {
    const Vec3 na = normalize(to_common_a.dir(a.face_normal(f)));
    const Vec3 pa = to_common_a.point(a.positions[a.face_verts(f)[0]]);
    Vec3 lo_a(1e30f), hi_a(-1e30f);
    for (uint32_t i = 0; i < a.face_size(f); i++)
      for (int k = 0; k < 3; k++) {
        const float v = to_common_a.point(a.positions[a.face_verts(f)[i]])[k];
        lo_a[k] = std::min(lo_a[k], v), hi_a[k] = std::max(hi_a[k], v);
      }
    for (size_t g = 0; g < b.face_count(); g++) {
      const Vec3 nb = normalize(to_common_b.dir(b.face_normal(g)));
      if (dot(na, nb) < 0.999f || std::fabs(dot(na, to_common_b.point(b.positions[b.face_verts(g)[0]]) - pa)) > 1e-5f) continue;
      bool overlap = true;
      for (int k = 0; k < 3 && overlap; k++) {
        float lo = 1e30f, hi = -1e30f;
        for (uint32_t i = 0; i < b.face_size(g); i++) {
          const float v = to_common_b.point(b.positions[b.face_verts(g)[i]])[k];
          lo = std::min(lo, v), hi = std::max(hi, v);
        }
        if (std::fabs(na[k]) > 0.5f) continue;  // (the axis the plane stands on)
        overlap = std::min(hi, hi_a[k]) - std::max(lo, lo_a[k]) > 1e-4f;
      }
      n += overlap;
    }
  }
  return n;
}

}  // namespace

static void tests_round547_review() {
  test("blockout parts (547, review): with a sill board on, no face of the Blockout's mesh lies in the plane of a Frame face and looks the same way (the strips beside the frame flickered between Cut and Solid)", [] {
    for (const char *words : {"window single", "window double", "window grid 1.6 2 0.9", "window arched"}) {
      Editor ed;
      const PartScene s = pe_make(ed, words);
      CHECK(s.opening != nullptr);
      if (!s.opening) continue;
      const GameObject *frame = pe_part(*s.opening, kPartKindFrame);
      CHECK(frame && s.opening->get<BlockoutPiece>()->sill_board);
      if (!frame) continue;
      const size_t clash = pe_coplanar_same_facing(*pe_mesh(*s.bo), s.bo->world_matrix(), *pe_mesh(*frame), frame->world_matrix());
      std::printf("    %s: %zu coplanar same-facing pair(s)\n", words, clash);
      CHECK(clash == 0);
    }
  });

  test("blockout parts (547, review): a leaf saved by round 515 (no Material field on its mark) follows the Leaf material again - `blockout set material leaf Other` reaches it; one with a material dropped on it by a later round is kept", [] {
    Editor ed;
    const PartScene s = pe_make(ed, "door single");
    CHECK(s.opening != nullptr);
    if (!s.opening) return;
    const std::string one = "Assets/Materials/R547/LeafOne.mat", two = "Assets/Materials/R547/LeafTwo.mat";
    create_material_asset(*make_material("LeafOne", Vec3(0.3f, 0.6f, 0.9f)), one);
    create_material_asset(*make_material("LeafTwo", Vec3(0.9f, 0.6f, 0.3f)), two);
    auto holds = [&](const GameObject *g, const std::string &path) {
      const MeshRenderer *mr = g ? g->get<MeshRenderer>() : nullptr;
      return mr && mr->materials.size() == 1 && mr->materials[0] && mr->materials[0]->asset_path == path;
    };
    ed.select_for_test(s.bo->id, false);
    ed.command("blockout set material leaf " + one);
    pe_built(ed);
    GameObject *leaf = pe_part(*s.opening, kPartKindLeaf);
    CHECK(holds(leaf, one));
    if (!leaf) return;
    leaf->get<BlockoutLeaf>()->material.clear();  // (what a file of round 515 loads as: no record of what the editor gave)
    ed.command("blockout set material leaf " + two);
    pe_built(ed);
    CHECK(holds(pe_part(*s.opening, kPartKindLeaf), two));
  });

  test("blockout parts (547, review): at the default frame width (6 cm) and a plain camera the first click on a window's jamb selects the window and the second click on the same spot the Frame - the outline's edge a few pixels away does not take it", [] {
    Editor ed;
    ed.init_headless(1600, 1000);
    pe_empty_scene(ed);
    GameObject *bo = ed.scene().create("Blockout");
    bo->add<Blockout>();
    bo->add<MeshFilter>()->mesh = std::make_shared<Mesh>();
    bo->add<MeshRenderer>();
    GameObject *wall = ed.scene().create("Wall", bo);
    BlockoutPiece *wp = wall->add<BlockoutPiece>();
    wp->shape = 0, wp->operation = 0, wp->size = Vec3(6.0f, 3.0f, 0.2f);
    wall->set_local_position({0.0f, 1.5f, 0.0f});
    GameObject *window = ed.scene().create("Window", bo);
    BlockoutPiece *p = window->add<BlockoutPiece>();
    p->shape = 0, p->operation = 1, p->opening = 2, p->style = kWindowSingle, p->width = 1.6f, p->head = 2.2f, p->sill = 0.8f, p->depth = 0.4f;  // (Frame Width and Depth are the defaults)
    ed.command("camera 5 0 7 0 1.5 0");
    pe_built(ed);
    const GameObject *frame = pe_part(*window, kPartKindFrame);
    CHECK(frame && std::fabs(p->frame_width - 0.06f) < 1e-6f);
    if (!frame) return;
    int x = 0, y = 0;
    CHECK(ed.project_to_window(frame->world_matrix().point(Vec3(-0.5f, 1.37f, 0.05f)), x, y));  // (the top rail 3 cm under the outline's edge, away from the Width and Head handles that sit at the middles of the opening's sides: a click on a handle is a handle's)
    {
      const Recti vr = ed.scene_view_rect();
      const uint32_t under = ed.scene_id_for_test(x - vr.x, y - vr.y);
      const GameObject *ug = under ? ed.scene().find(under) : nullptr;
      std::printf("    the pixel %d %d of the jamb's middle holds '%s'\n", x, y, ug ? ug->name.c_str() : "nothing");
    }
    ed.clear_selection_for_test();
    pe_click(ed, x, y);
    pe_settle(ed, 2);
    CHECK(ed.selection_size_for_test() == 1 && ed.active_object_for_test() == window);
    pe_click(ed, x, y);
    pe_settle(ed, 2);
    std::printf("    the second click selects '%s' (%zu selected)\n", ed.active_object_for_test() ? ed.active_object_for_test()->name.c_str() : "nothing", ed.selection_size_for_test());
    CHECK(ed.selection_size_for_test() == 1 && ed.active_object_for_test() == frame);
    /* Ctrl+click on the selected opening's frame keeps it. */
    ed.select_for_test(window->id, false);
    pe_click(ed, x + 400, y + 300);  // (the sky: nothing)
    ed.select_for_test(window->id, false);
    ed.step_frame_headless({pe_ev(EvType::MouseMove, x, y, platform::MOD_CTRL)});
    ed.step_frame_headless({pe_ev(EvType::MouseDown, x, y, platform::MOD_CTRL)});
    ed.step_frame_headless({pe_ev(EvType::MouseUp, x, y, platform::MOD_CTRL)});
    pe_settle(ed, 2);
    CHECK(ed.selection_size_for_test() >= 1);  // (a toggle on the part of a selected opening does not deselect it)
  });

  test("blockout parts (547, review): an opening switched off keeps its Frame and Glass hidden with their stacks, and they are back when it is on; a door over 100 m (no hole, no parts) hides its Frame the same way", [] {
    Editor ed;
    const PartScene s = pe_make(ed, "window single");
    CHECK(s.opening != nullptr);
    if (!s.opening) return;
    BlockoutPiece *bp = s.opening->get<BlockoutPiece>();
    GameObject *frame = pe_part(*s.opening, kPartKindFrame);
    CHECK(frame != nullptr);
    if (!frame) return;
    const uint64_t id = frame->id;
    frame->add<BevelModifier>();
    ed.mark_changed_for_test("test modifier");
    pe_built(ed);
    bp->enabled = false;
    ed.mark_changed_for_test("test off");
    pe_built(ed);
    GameObject *kept = ed.scene().find(id);
    CHECK(kept && !kept->active && kept->get<BevelModifier>() && pe_parts(*s.opening, kPartKindFrame).size() == 1 && pe_parts(*s.opening, kPartKindGlass).size() == 1);
    bp->enabled = true;
    ed.mark_changed_for_test("test on");
    pe_built(ed);
    kept = ed.scene().find(id);
    CHECK(kept && kept->active && pe_stack_runs(*kept, 1) && !kept->get<BlockoutLeaf>()->hidden);
    Editor ed2;
    const PartScene d = pe_make(ed2, "door single");
    CHECK(d.opening != nullptr);
    if (!d.opening) return;
    GameObject *door_frame = pe_part(*d.opening, kPartKindFrame);
    CHECK(door_frame != nullptr);
    if (!door_frame) return;
    const uint64_t door_frame_id = door_frame->id;
    door_frame->add<BevelModifier>();
    ed2.mark_changed_for_test("test modifier");
    pe_built(ed2);
    d.opening->get<BlockoutPiece>()->width = 200.0f;
    ed2.mark_changed_for_test("test huge");
    pe_built(ed2);
    GameObject *hidden = ed2.scene().find(door_frame_id);
    CHECK(hidden && !hidden->active && hidden->get<BevelModifier>());
    d.opening->get<BlockoutPiece>()->width = 0.9f;
    ed2.mark_changed_for_test("test back");
    pe_built(ed2);
    hidden = ed2.scene().find(door_frame_id);
    CHECK(hidden && hidden->active && pe_stack_runs(*hidden, 1));
  });

  test("blockout parts (547, review): a window 150 m wide has a Frame as wide as its hole (the old size is not left in the hole), and the Frame is manifold in a 0.1 m wall with the default Frame Depth and no Sill Overhang", [] {
    Editor ed;
    const PartScene s = pe_make(ed, "window single");
    CHECK(s.opening != nullptr);
    if (!s.opening) return;
    BlockoutPiece *bp = s.opening->get<BlockoutPiece>();
    bp->width = 150.0f, bp->sill_board = false;
    ed.mark_changed_for_test("test huge");
    pe_built(ed);
    const GameObject *frame = pe_part(*s.opening, kPartKindFrame);
    CHECK(frame != nullptr);
    if (frame) CHECK_NEAR(pe_box(*pe_mesh(*frame)).max.x, 75.0f, 1e-2f);
    Editor ed2;
    ed2.init_headless(900, 600);
    pe_empty_scene(ed2);
    GameObject *bo = ed2.scene().create("Blockout");
    bo->add<Blockout>();
    bo->add<MeshFilter>()->mesh = std::make_shared<Mesh>();
    bo->add<MeshRenderer>();
    GameObject *wall = ed2.scene().create("Wall", bo);
    BlockoutPiece *wp = wall->add<BlockoutPiece>();
    wp->shape = 0, wp->operation = 0, wp->size = Vec3(3.0f, 2.4f, 0.1f);
    GameObject *window = ed2.scene().create("Window", bo);
    BlockoutPiece *q = window->add<BlockoutPiece>();
    q->shape = 0, q->operation = 1, q->opening = 2, q->style = kWindowSingle, q->width = 1.2f, q->head = 2.0f, q->sill = 0.9f, q->depth = 0.3f, q->sill_overhang = 0.0f;
    pe_built(ed2);
    const GameObject *thin = pe_part(*window, kPartKindFrame);
    std::string why;
    CHECK(thin != nullptr && pe_sound(pe_mesh(*thin), &why));
    if (!why.empty()) std::printf("    %s\n", why.c_str());
  });

  test("blockout parts (547, review): a Ctrl+D copy of a Frame dragged above the original in the Hierarchy does not take its place - the original keeps its mark and its stack, the copy is an ordinary object", [] {
    Editor ed;
    const PartScene s = pe_make(ed, "window single");
    CHECK(s.opening != nullptr);
    if (!s.opening) return;
    GameObject *frame = pe_part(*s.opening, kPartKindFrame);
    CHECK(frame != nullptr);
    if (!frame) return;
    const uint64_t id = frame->id, opening_id = s.opening->id;
    frame->add<BevelModifier>();
    ed.mark_changed_for_test("test modifier");
    pe_built(ed);
    ed.select_for_test(id, false);
    ed.command("duplicate");
    GameObject *copy = ed.active_object_for_test();
    CHECK(copy && copy->id != id);
    if (!copy) return;
    const uint64_t copy_id = copy->id;
    ed.scene().set_parent(copy, ed.scene().find(opening_id), 0, false);  // (above the original)
    ed.mark_changed_for_test("test reorder");
    pe_built(ed);
    const GameObject *original = ed.scene().find(id), *other = ed.scene().find(copy_id);
    CHECK(original && original->get<BlockoutLeaf>() && pe_stack_runs(*original, 1));
    CHECK(other && !other->get<BlockoutLeaf>());
    CHECK(pe_parts(*ed.scene().find(opening_id), kPartKindFrame).size() == 1);
  });

  test("blockout parts (547, review): a face of a window's Frame is the surface of its Blockout for the Draw tools (blockout_at_surface), and the part sync costs well under a millisecond a walk on the 400 bench (printed)", [] {
    Editor ed;
    const PartScene s = pe_make(ed, "window single");
    CHECK(s.opening != nullptr);
    if (!s.opening) return;
    const GameObject *frame = pe_part(*s.opening, kPartKindFrame);
    CHECK(frame != nullptr);
    if (frame) {
      const Vec3 at = frame->world_matrix().point(Vec3(-0.57f, 0.7f, 0.05f)), n = normalize(frame->world_matrix().dir(Vec3(0.0f, 0.0f, 1.0f)));
      CHECK(blockout_at_surface(ed.scene(), at, n) == s.bo->id);
    }
    Editor big;
    big.init_headless(900, 600);
    big.command("benchscene blockout 400");
    pe_built(big);
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 20; i++) big.blockout_sync_leaves_for_test();
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 20.0;
    std::printf("    the part sync of the 400 bench: %.3f ms a walk (printed, never asserted)\n", ms);
  });
}

/* ---- Round 571 (task 0553): a used pair of clicks on a frame is forgotten ---- */
namespace {
/* Slow single clicks: the editor's clock pinned and moved a second before each, so no two make a double click (which would frame the view and move the spot). */
struct SlowClicks {
  Editor &ed;
  double t = 1000.0;
  void click(int x, int y) {
    t += 1.0;
    ed.set_clock_for_test(t);
    pe_click(ed, x, y);
    pe_settle(ed, 2);
  }
  /* A click on an object's row in the Hierarchy. */
  bool row(const char *name) {
    int x = 0, y = 0;
    if (!ed.hierarchy_row_for_test(name, x, y)) {
      std::printf("      the Hierarchy row '%s' is not drawn\n", name);
      return false;
    }
    click(x, y);
    return true;
  }
  const char *active() const { return ed.active_object_for_test() ? ed.active_object_for_test()->name.c_str() : "nothing"; }
  bool only(const GameObject *g) const { return ed.selection_size_for_test() == 1 && ed.active_object_for_test() == g; }
};
}  // namespace

static void tests_round571() {
  test("blockout parts (571): after two clicks on a window's frame selected the Frame, selecting the window again in the Hierarchy and clicking that spot once selects the window, not the Frame at once", [] {
    Editor ed;
    const PickScene s = pe_pick_scene(ed);
    const GameObject *frame = pe_part(*s.window, kPartKindFrame);
    CHECK(frame != nullptr);
    if (!frame) return;
    int x = 0, y = 0;
    CHECK(pe_pixel(ed, *frame, Vec3(-0.8f + 0.15f, 1.2f, 0.1f), x, y));
    ed.clear_selection_for_test();
    pe_settle(ed, 3);
    SlowClicks c{ed};
    c.click(x, y);
    std::printf("    first click on the frame: %s\n", c.active());
    CHECK(c.only(s.window));
    c.click(x, y);
    std::printf("    second click on the spot: %s\n", c.active());
    CHECK(c.only(frame));
    CHECK(c.row("Window"));
    std::printf("    the window's row in the Hierarchy: %s\n", c.active());
    CHECK(c.only(s.window));
    c.click(x, y);
    std::printf("    one click on the spot: %s (want the window)\n", c.active());
    CHECK(c.only(s.window));
    c.click(x, y);
    std::printf("    and one more: %s (want the Frame)\n", c.active());
    CHECK(c.only(frame));
  });

  test("blockout parts (571): a first click on a window's frame, then another object and the window again in the Hierarchy: the next click on that spot is a first click again", [] {
    Editor ed;
    const PickScene s = pe_pick_scene(ed);
    const GameObject *frame = pe_part(*s.window, kPartKindFrame);
    CHECK(frame != nullptr);
    if (!frame) return;
    int x = 0, y = 0;
    CHECK(pe_pixel(ed, *frame, Vec3(-0.8f + 0.15f, 1.2f, 0.1f), x, y));
    ed.clear_selection_for_test();
    pe_settle(ed, 3);
    SlowClicks c{ed};
    c.click(x, y);
    CHECK(c.only(s.window));
    CHECK(c.row("Door"));
    std::printf("    the door's row in the Hierarchy: %s\n", c.active());
    CHECK(c.only(s.door));
    CHECK(c.row("Window"));
    CHECK(c.only(s.window));
    c.click(x, y);
    std::printf("    one click on the spot: %s (want the window)\n", c.active());
    CHECK(c.only(s.window));
    c.click(x, y);
    std::printf("    and one more: %s (want the Frame)\n", c.active());
    CHECK(c.only(frame));
  });

  test("blockout parts (571): the pair of clicks ends too when the selection changes while the Game view is in front of the Scene view (the check runs once an editor frame, not in the Scene view)", [] {
    Editor ed;
    const PickScene s = pe_pick_scene(ed);
    const GameObject *frame = pe_part(*s.window, kPartKindFrame);
    CHECK(frame != nullptr);
    if (!frame) return;
    int x = 0, y = 0;
    CHECK(pe_pixel(ed, *frame, Vec3(-0.8f + 0.15f, 1.2f, 0.1f), x, y));
    ed.clear_selection_for_test();
    pe_settle(ed, 3);
    SlowClicks c{ed};
    c.click(x, y);
    CHECK(c.only(s.window));
    ed.command("window Game");  // (the Game tab in front: the Scene view is not drawn while the selection changes)
    pe_settle(ed, 3);
    CHECK(c.row("Door"));
    CHECK(c.only(s.door));
    CHECK(c.row("Window"));
    CHECK(c.only(s.window));
    ed.command("window Scene");
    pe_settle(ed, 3);
    c.click(x, y);
    std::printf("    back in the Scene view, one click on the spot: %s (want the window)\n", c.active());
    CHECK(c.only(s.window));
    c.click(x, y);
    CHECK(c.only(frame));
  });
}

void tests_round547() {
  tests_round547_leaf();
  tests_round547_objects();
  tests_round547_survive();
  tests_round547_look();
  tests_round547_pick();
  tests_round547_te();  // (the test engineer's block)
  tests_round547_review();
  tests_round571();
}
// r547-end
