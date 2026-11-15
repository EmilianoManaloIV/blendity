// SPDX-License-Identifier: GPL-2.0-or-later
// Blockout door leaves as objects (round 515, task 0497, ADR 0136): the implementer's tests and the test engineer's edges. Split from test_blockout.cpp, whose object file reached
// the section limit (MSVC C1128); the two entry points are called from run_blockout_tests so they stay in the blockout group, in the same order.
#include "test_common.h"

#include "../src/scene/blockout.h"
#include "../src/scene/raycast.h"

#include <array>
#include <limits>

using namespace bl;

namespace {

/* Small helpers copied from test_blockout.cpp (its own are internal to that file); the line each came from is named. */
/* test_blockout.cpp, round 477: events, finding, making objects, the log, settling, clicking */
using R477ET = platform::EventType;

platform::Event r477_ev(R477ET t, int x, int y, int mods = 0) {
  platform::Event e;
  e.type = t, e.x = x, e.y = y, e.mods = mods;
  return e;
}
platform::Event r477_key(int key, bool down = true) {
  platform::Event e;
  e.type = down ? R477ET::KeyDown : R477ET::KeyUp, e.key = key;
  return e;
}

GameObject *r477_find(Editor &ed, const char *name = "Blockout") { return ed.scene().find_by_name(name); }
/* A Blockout object made by hand (no editor command): the object, its mesh and renderer. */
GameObject *r477_blockout(Editor &ed, const char *name = "Blockout") {
  GameObject *g = ed.scene().create(name);
  g->add<Blockout>();
  g->add<MeshFilter>()->mesh = std::make_shared<Mesh>();
  g->add<MeshRenderer>();
  return g;
}
GameObject *r477_piece(Editor &ed, GameObject &parent, const char *name, int shape, int op, Vec3 at, Vec3 size = Vec3(1.0f), float radius = 0.5f, float height = 1.0f) {
  GameObject *g = ed.scene().create(name, &parent);
  BlockoutPiece *p = g->add<BlockoutPiece>();
  p->shape = shape, p->operation = op, p->size = size, p->radius = radius, p->height = height;
  g->set_local_position(at);
  return g;
}

/* The log lines written since `mark` (r477_mark's), joined; and how many of them warn. */
size_t r477_mark() {
  std::vector<LogEntry> tmp;
  return Log::fetch((size_t)1 << 40, tmp);
}
std::string r477_log_since(size_t mark, size_t *warnings = nullptr) {
  std::vector<LogEntry> v;
  Log::fetch(mark, v);
  std::string s;
  if (warnings) *warnings = 0;
  for (const LogEntry &e : v) {
    s += e.text + "\n";
    if (warnings && e.level != LogLevel::Info) (*warnings)++;
  }
  return s;
}

/* Nothing in the scene: the default scene's plane, cube and teapot would be floors and occluders of their own. */
void r477_empty_scene(Editor &ed) {
  std::vector<GameObject *> roots;
  ed.scene().for_each([&](GameObject &g) {
    if (!g.parent) roots.push_back(&g);
  });
  ed.scene().destroy(roots);
  ed.clear_selection_for_test();
}

void r477_settle(Editor &ed, int frames = 2) {
  for (int i = 0; i < frames; i++) ed.step_frame_headless();
}

void r477_click(Editor &ed, int x, int y, int mods = 0) {
  ed.step_frame_headless({r477_ev(R477ET::MouseMove, x, y, mods)});
  ed.step_frame_headless({r477_ev(R477ET::MouseDown, x, y, mods)});
  ed.step_frame_headless({r477_ev(R477ET::MouseUp, x, y, mods)});
}

/* test_blockout.cpp, the round 477 engineer: te_key, te_scene_path, te_warnings_since */
platform::Event te_key(int key, int mods, bool down = true) {
  platform::Event e;
  e.type = down ? R477ET::KeyDown : R477ET::KeyUp, e.key = key, e.mods = mods;
  return e;
}

std::string te_scene_path(Editor &ed, const char *file) {
  const std::string dir = ed.resolve_asset_path_for_test("Assets/Scenes");
  fs::make_dirs(dir);
  return fs::join(dir, file);
}

size_t te_warnings_since(size_t mark) {
  size_t w = 0;
  r477_log_since(mark, &w);
  return w;
}

/* rounds 478, 479, 495, 496, 482 */
/* A room as the console makes it, selected afterwards: the first piece. */
GameObject *r478_room_by_command(Editor &ed, GameObject &bo, const char *line = "blockout add room box 4 3 4") {
  ed.select_for_test(bo.id, false);
  ed.command(line);
  r477_settle(ed, 2);
  return ed.active_object_for_test();
}

/* A number typed into one of the Adjust Last panel's fields (the panel must be showing and the buttons recorded). */
bool r479_type(Editor &ed, const std::string &key, const std::string &text) {
  Recti r;
  for (int i = 0; i < 4 && !ed.button_rect_for_test(key, r); i++) ed.step_frame_headless();
  if (!ed.button_rect_for_test(key, r)) return false;
  const int x = r.x + r.w / 2, y = r.y + r.h / 2;
  for (R477ET t : {R477ET::MouseMove, R477ET::MouseDown, R477ET::MouseUp, R477ET::MouseDown, R477ET::MouseUp}) ed.step_frame_headless({r477_ev(t, x, y)});
  ed.step_frame_headless({te_key(platform::KEY_A, platform::MOD_CTRL)});
  for (char c : text) {
    platform::Event e;
    e.type = R477ET::Text, e.codepoint = (uint32_t)c;
    ed.step_frame_headless({e});
  }
  ed.step_frame_headless({r477_key(platform::KEY_ENTER)});
  r477_settle(ed, 2);
  return true;
}

/* Round 496: a recorded control of the panel in the view, clicked by its key (the buttons must be recorded; false when it is not showing). */
bool r496_click_key(Editor &ed, const std::string &key) {
  ed.record_buttons_for_test(true);
  r477_settle(ed, 2);
  Recti r;
  if (!ed.button_rect_for_test(key, r)) return false;
  r477_click(ed, r.x + r.w / 2, r.y + r.h / 2);
  r477_settle(ed, 2);
  return true;
}

/* A dropdown opened and an item of its list chosen, by clicking: `rect_key` is the control's recorded key, `item` the entry's text. */
bool r496_pick_combo(Editor &ed, const char *rect_key, const char *item) {
  ed.record_buttons_for_test(true);
  r477_settle(ed, 2);
  Recti c;
  if (!ed.button_rect_for_test(rect_key, c)) return false;
  r477_click(ed, c.x + c.w / 2, c.y + c.h / 2);
  r477_settle(ed, 2);
  return r496_click_key(ed, std::string("menu:") + item);
}

/* The Blockout a piece belongs to and its mesh. */
const Mesh *r495_mesh(const GameObject &bo) {
  const MeshFilter *mf = bo.get<MeshFilter>();
  return mf && mf->mesh ? mf->mesh.get() : nullptr;
}

void r495_built(Editor &ed) {
  r477_settle(ed, 3);
  ed.command("blockout wait");
  r477_settle(ed, 2);
}

/* A material asset in the scratch project; its project path. */
std::string r482_material(const char *name, Vec3 colour) {
  const std::string path = std::string("Assets/Materials/R482/") + name + ".mat";
  create_material_asset(*make_material(name, colour), path);
  return path;
}

/* Waits for an export's file; the file's text (binary too). */
std::string r482_export(Editor &ed, const char *format, const char *name) {
  const std::string dir = ed.resolve_asset_path_for_test("Assets/Exports");
  fs::make_dirs(dir);
  const std::string path = fs::join(dir, name);
  fs::remove_file(path);
  ed.command(std::string("export ") + format + " all " + path);
  r477_settle(ed, 5);
  for (int i = 0; i < 400 && (!fs::exists(path) || ed.file_export_running_for_test()); i++) ed.step_frame_headless();
  return path;
}

/* The Modeling Tools window showing (the Blockout section open), the buttons recorded. */
void r496_panel(Editor &ed) {
  ed.command("layout Default");
  ed.command("window Modeling Tools");
  ed.command("alltools on");
  ed.record_buttons_for_test(true);
  r477_settle(ed, 4);
}

bool r496_has(Editor &ed, const char *key) {
  Recti r;
  return ed.button_rect_for_test(key, r);
}

/* round 498 */
/* The paths this build has, as the Blockout's Method says them: Field (2) always, Exact (1) with Blender's libraries. */
std::vector<int> r498_methods() {
  std::vector<int> m{2};
  if (blockout::crisp_available()) m.insert(m.begin(), 1);
  return m;
}

/* The faces of one class (by blockout_face_class, from the pieces the snapshot made): their number and how far along the window's Z they reach. */
struct R498Class {
  size_t faces = 0;
  float z_lo = 1e30f, z_hi = -1e30f;
};
R498Class r498_class(const GameObject &bo, const GameObject &win, int cls) {
  R498Class out;
  const Mesh *m = r495_mesh(bo);
  const Blockout *b = bo.get<Blockout>();
  if (!m || !b || b->state.faces.piece.size() != m->face_count()) return out;
  std::vector<blockout::Piece> pieces;
  blockout::Settings settings;
  blockout_snapshot(bo, pieces, settings);
  const Mat4 to_win = win.world_matrix().inverse() * bo.world_matrix();
  for (size_t f = 0; f < m->face_count(); f++) {
    const blockout::Piece *piece = nullptr;
    for (const blockout::Piece &p : pieces)
      if (p.id == b->state.faces.piece[f]) piece = &p;
    if (!piece || blockout_face_class(*piece, b->state.faces.side[f]) != cls) continue;
    out.faces++;
    for (uint32_t i = 0; i < m->face_size(f); i++) {
      const float z = to_win.point(m->positions[m->face_verts(f)[i]]).z;
      out.z_lo = std::min(out.z_lo, z), out.z_hi = std::max(out.z_hi, z);
    }
  }
  return out;
}


/* test_blockout.cpp's r498_judge, without the kernel's build record (r480t_judge needs it): what the stress sweep calls a problem of the Blockout's live mesh; "" when sound. */
std::string r498_judge(const GameObject &bo, size_t *small = nullptr) {
  const Mesh *m = r495_mesh(bo);
  const Blockout *b = bo.get<Blockout>();
  if (small) *small = 0;
  if (!m || !b) return "no mesh";
  if (!b->state.status.refusal.empty()) return "refused: " + b->state.status.refusal;
  const blockout::Faces &faces = b->state.faces;
  if (faces.piece.size() != m->face_count()) return "the faces' origins are not the mesh's";
  if (m->face_count() == 0) return "";
  std::string why;
  if (!structurally_valid(*m, &why)) return why;
  if (!closed_manifold(*m)) return "open or pinched";
  if (signed_volume(*m) <= 0.0f) return "inside out";
  std::vector<std::pair<uint32_t, uint32_t>> pairs;
  const AABB box = m->bounds();
  meshops::overlapping_faces(*m, 1e-4f * length(box.max - box.min), &pairs);
  std::string bad;
  for (const auto &pr : pairs) {
    if (faces.flat[pr.first] && faces.flat[pr.second]) bad = "flat faces on top of each other";
    else if (small) (*small)++;
  }
  return bad;
}

/* round 499: the room with a door, the console's word of a style, the wall rig */

/* A room (4 x 3 x 4, walls 0.2) with a door on its first wall (-X) made by the console, built by `method` (0 Auto, 1 Exact, 2 Field). The door's own space: x across,
 * y up from the floor line, z through the wall and out into the room (+Z faces the room). */
struct R499Scene {
  GameObject *bo = nullptr, *room = nullptr, *door = nullptr;
};
R499Scene r499_make(Editor &ed, const std::string &door_words, int method = 0, float cell = 0.0f) {
  ed.init_headless(900, 600);
  r477_empty_scene(ed);
  R499Scene s;
  s.bo = r477_blockout(ed);
  s.bo->set_world_position({0.0f, 0.0f, 0.0f});
  s.bo->get<Blockout>()->method = method;
  if (cell > 0.0f) s.bo->get<Blockout>()->cell = cell;
  s.room = r478_room_by_command(ed, *s.bo);
  if (!s.room) return s;
  s.room->set_local_position({0.0f, 0.0f, 0.0f});
  r477_settle(ed, 2);
  ed.command("blockout add door " + door_words);
  s.door = ed.active_object_for_test();
  r495_built(ed);
  return s;
}

/* The style's console word. */
std::string r499_word(int style) { return style == kDoorOpeningOnly ? "opening" : to_lower(kBlockoutDoorStyleNames[style]); }

/* A wall 3 m wide, 2.4 m high and 0.2 m thick in the XY plane with a door piece in it made by hand: its floor line is the wall's bottom (y = -1.2), 0.9 m wide to a head
 * at 2.1 m, 0.4 m deep (the wall and 0.1 m each side). The door faces +Z. Field Cell 0.01 so that the field path resolves a 4 cm leaf and a 6 cm frame. */
struct R499Wall {
  GameObject *bo = nullptr, *door = nullptr;
};
R499Wall r499_wall(Editor &ed, int style, int method, float cell = 0.01f) {
  ed.init_headless(900, 600);
  r477_empty_scene(ed);
  R499Wall w;
  w.bo = r477_blockout(ed);
  Blockout *b = w.bo->get<Blockout>();
  b->method = method, b->cell = cell;
  r477_piece(ed, *w.bo, "Wall", 0, 0, {0, 0, 0}, {3.0f, 2.4f, 0.2f});
  w.door = ed.scene().create("Door", w.bo);
  BlockoutPiece *p = w.door->add<BlockoutPiece>();
  p->shape = 0, p->operation = 1, p->opening = 1, p->width = 0.9f, p->head = 2.1f, p->sill = 0.0f, p->depth = 0.4f, p->style = style;
  w.door->set_local_position({0.0f, -1.2f, 0.0f});
  r495_built(ed);
  return w;
}

/* ---- Round 515 (ADR 0136): a door's leaf is an object under it; what the tests of the round and the round 499 ones read of it ---- */

/* The door's leaf objects, in the order they stand. */
std::vector<GameObject *> r515_leaves(const GameObject &door) {
  std::vector<GameObject *> out;
  for (GameObject *c : door.children)
    if (c->get<BlockoutLeaf>()) out.push_back(c);
  return out;
}

/* How far the leaf is turned about its up axis, degrees (Unity's yaw: +Z toward +X). */
float r515_yaw(const GameObject &leaf) {
  const Vec3 f = leaf.local().rotation.rotate(Vec3(0.0f, 0.0f, 1.0f));
  return std::atan2(f.x, f.z) * 57.29577951308232f;
}

/* Builds and lets the editor keep the leaves in step: the walk that makes them runs on a frame. */
void r515_settle(Editor &ed) {
  r495_built(ed);
  r477_settle(ed, 3);
  r495_built(ed);
}

/* The kernel builds asked for so far (workers started and main-thread builds): an Open Amount that asks for none leaves both as they were. */
uint64_t r515_builds(Editor &ed) { return ed.blockout_started_for_test() + ed.blockout_main_builds_for_test(); }

/* A door of `style` made by the console in a room, settled. */
R499Scene r515_door(Editor &ed, int style, int method = 0) {
  R499Scene s = r499_make(ed, r499_word(style), method);
  r515_settle(ed);
  return s;
}

/* The Blockout's mesh bytes, as a hash. */
uint64_t r515_bytes(const GameObject &bo) {
  const Mesh *m = r495_mesh(bo);
  return m ? blockout_mesh_hash(*m) : 0;
}

/* A key press and release with the mouse in the Scene view (the Rotate modal wants the pointer there). */
void r515_key(Editor &ed, int key) {
  const Recti r = ed.scene_view_rect();
  const int x = r.x + r.w / 2 + 40, y = r.y + r.h / 2;
  platform::Event m, d, u;
  m.type = platform::EventType::MouseMove;
  m.x = d.x = u.x = x;
  m.y = d.y = u.y = y;
  d.type = platform::EventType::KeyDown;
  d.key = u.key = key;
  u.type = platform::EventType::KeyUp;
  ed.step_frame_headless({m, d, u});
}

void r515_type(Editor &ed, const char *text) {
  const Recti r = ed.scene_view_rect();
  for (const char *c = text; *c; c++) {
    platform::Event t;
    t.type = platform::EventType::Text;
    t.codepoint = (uint32_t)*c;
    t.x = r.x + r.w / 2 + 40;
    t.y = r.y + r.h / 2;
    ed.step_frame_headless({t});
  }
}

}  // namespace

// r515-begin
/* ===================================================================== */
/* Round 515 (task 0497, ADR 0136): a door's leaf is an object            */
/* ===================================================================== */

static void tests_round515_leaves() {
  /* ---- Criterion 1 ---- */
  test("blockout door leaf (515): a new Single, Double, Sliding or Arched door has one, two, one and one leaf objects under its piece - each with its origin on its hinge (Sliding: its closed place), upright, a mesh, a renderer and no piece of its own - and an Opening Only or a Revolving door has none; Leaf off removes them and Leaf on makes them", [] {
    struct Want {
      int style;
      size_t leaves;
    };
    for (const Want &w : {Want{kDoorSingle, 1}, {kDoorDouble, 2}, {kDoorSliding, 1}, {kDoorArched, 1}, {kDoorRevolving, 0}, {kDoorOpeningOnly, 0}}) {
      Editor ed;
      const R499Scene s = r515_door(ed, w.style);
      CHECK(s.door != nullptr);
      if (!s.door) continue;
      const BlockoutPiece *bp = s.door->get<BlockoutPiece>();
      const std::vector<GameObject *> leaves = r515_leaves(*s.door);
      std::printf("    %s: %zu leaf object(s)\n", kBlockoutDoorStyleNames[w.style], leaves.size());
      CHECK(leaves.size() == w.leaves);
      const float clear = bp->width - 2.0f * bp->frame_width;
      for (const GameObject *leaf : leaves) {
        const Vec3 at = leaf->local().position, up = leaf->local().rotation.rotate(Vec3(0.0f, 1.0f, 0.0f));
        CHECK(leaf->get<MeshFilter>() && leaf->get<MeshFilter>()->mesh && leaf->get<MeshRenderer>() && !leaf->get<BlockoutPiece>() && up.y > 0.9999f);
        CHECK_NEAR(at.y, bp->sill, 1e-5f);
        CHECK(leaf->local().scale.x == 1.0f && leaf->local().scale.y == 1.0f && leaf->local().scale.z == 1.0f && r515_yaw(*leaf) == 0.0f);  // (closed)
        if (w.style == kDoorSliding) {  // (on the wall's face)
          CHECK_NEAR(at.x, 0.0f, 1e-5f);
          CHECK(at.z > 0.0f);
        }
        else {  // (on the jamb's inner edge, at the frame's middle depth)
          CHECK_NEAR(at.z, 0.0f, 1e-5f);
          CHECK(std::fabs(std::fabs(at.x) - 0.5f * clear) < 1e-4f);
        }
      }
      if (w.style == kDoorDouble) CHECK(leaves.size() == 2 && leaves[0]->local().position.x * leaves[1]->local().position.x < 0.0f && leaves[0]->name == "Leaf L" && leaves[1]->name == "Leaf R");
      if (w.style == kDoorSingle && leaves.size() == 1) {  // (Hinge Side moves the pivot to the other jamb)
        const float before = leaves[0]->local().position.x;
        ed.select_for_test(s.door->id, false);
        ed.command("blockout set hinge right");
        r515_settle(ed);
        const std::vector<GameObject *> again = r515_leaves(*s.door);
        CHECK(again.size() == 1 && again[0]->local().position.x * before < 0.0f);
      }
      if (w.style == kDoorOpeningOnly) continue;
      ed.select_for_test(s.door->id, false);
      ed.command("blockout set leaf off");
      r515_settle(ed);
      CHECK(r515_leaves(*s.door).empty());
      ed.command("blockout set leaf on");
      r515_settle(ed);
      CHECK(r515_leaves(*s.door).size() == (w.style == kDoorRevolving ? 1u : w.leaves));  // (a Revolving door's Leaf is its wings)
    }
    Editor ed;
    const R499Scene s = r515_door(ed, kDoorRevolving);
    CHECK(s.door && r515_leaves(*s.door).empty());
    if (!s.door) return;
    ed.select_for_test(s.door->id, false);
    ed.command("blockout set leaf on");
    r515_settle(ed);
    const std::vector<GameObject *> wings = r515_leaves(*s.door);
    CHECK(wings.size() == 1 && wings[0]->name == "Wings" && s.door->get<BlockoutPiece>()->wings);
    if (!wings.empty()) CHECK(wings[0]->get<MeshFilter>()->mesh->face_count() >= 30);  // (a post and four vanes: five boxes)
    ed.command("blockout set leaf off");
    r515_settle(ed);
    CHECK(r515_leaves(*s.door).empty());
  });

  test("blockout door leaf (515): the Door button with a style chosen in the Modeling Tools window makes a Single door with one leaf and a Double door with two, with real events", [] {
    Editor ed;
    const R499Scene s = r499_make(ed, "");
    CHECK(s.room != nullptr);
    if (!s.room) return;
    r496_panel(ed);
    for (const std::pair<int, size_t> &w : {std::pair<int, size_t>{kDoorSingle, 1}, {kDoorDouble, 2}, {kDoorOpeningOnly, 0}}) {
      ed.select_for_test(s.room->id, false);
      r477_settle(ed, 2);
      CHECK(r496_pick_combo(ed, "tools:door_style", kBlockoutDoorStyleNames[w.first]));
      CHECK(r496_click_key(ed, "Door"));
      r515_settle(ed);
      GameObject *door = ed.active_object_for_test();
      CHECK(door && door->get<BlockoutPiece>() && door->get<BlockoutPiece>()->style == w.first);
      if (door) CHECK(r515_leaves(*door).size() == w.second);
    }
  });

  /* ---- Criterion 2: Open Amount is a transform ---- */
  test("blockout door leaf (515): Open Amount from 0 to 1 in twenty steps asks for no Blockout build and leaves the mesh bytes as they were - a Single leaf turns 90 degrees about its hinge, a Double's two turn the opposite ways, a Sliding leaf moves its width, the Wings turn 90 degrees; a typed number in the Inspector does the same", [] {
    for (int style : {kDoorSingle, kDoorDouble, kDoorSliding, kDoorRevolving}) {
      Editor ed;
      const R499Scene s = r515_door(ed, style);
      CHECK(s.door != nullptr);
      if (!s.door) continue;
      ed.select_for_test(s.door->id, false);
      if (style == kDoorRevolving) {
        ed.command("blockout set leaf on");
        r515_settle(ed);
      }
      const std::vector<GameObject *> leaves = r515_leaves(*s.door);
      CHECK(!leaves.empty());
      if (leaves.empty()) continue;
      const BlockoutPiece *bp = s.door->get<BlockoutPiece>();
      const uint64_t bytes = r515_bytes(*s.bo), builds = r515_builds(ed);
      std::vector<Vec3> closed;
      for (const GameObject *l : leaves) closed.push_back(l->local().position);
      for (int step = 1; step <= 20; step++) {
        ed.command(strprintf("blockout set open %g", (double)step / 20.0));
        r477_settle(ed, 2);
        if (step % 5) continue;
        const float open = (float)step / 20.0f;
        for (size_t i = 0; i < leaves.size(); i++) {
          if (style == kDoorSliding) CHECK(std::fabs(leaves[i]->local().position.x - closed[i].x) > 0.0f && r515_yaw(*leaves[i]) == 0.0f);
          else CHECK_NEAR(std::fabs(r515_yaw(*leaves[i])), 90.0f * open, 0.01f);
          CHECK_NEAR(leaves[i]->local().position.y, closed[i].y, 1e-5f);
        }
      }
      CHECK_NEAR(bp->open_amount, 1.0f, 1e-6f);
      CHECK(r515_builds(ed) == builds && r515_bytes(*s.bo) == bytes);
      if (style == kDoorDouble) CHECK(r515_yaw(*leaves[0]) * r515_yaw(*leaves[1]) < 0.0f);  // (hung on the two jambs: one turns each way)
      if (style == kDoorSliding) {
        const float width = bp->width + 2.0f * bp->frame_width;
        CHECK_NEAR(std::fabs(leaves[0]->local().position.x - closed[0].x), width, 1e-4f);
      }
      /* Typed into the Inspector's Open Amount, the same: a transform, no build. */
      ed.command("window Inspector");
      ed.record_buttons_for_test(true);
      ed.select_for_test(s.door->id, false);
      r477_settle(ed, 3);
      CHECK(r479_type(ed, "field:Open Amount", "0.5"));
      CHECK_NEAR(bp->open_amount, 0.5f, 1e-6f);
      if (style != kDoorSliding) CHECK_NEAR(std::fabs(r515_yaw(*leaves[0])), 45.0f, 0.01f);
      CHECK(r515_builds(ed) == builds && r515_bytes(*s.bo) == bytes);
    }
  });

  /* ---- Criterion 3: turning a leaf writes Open Amount ---- */
  test("blockout door leaf (515): turning a leaf 45 degrees about its Y with the Rotate modal (real keys) sets its door's Open Amount to 0.5 in the same undo step, the Inspector shows it, and one undo returns both", [] {
    Editor ed;
    const R499Scene s = r515_door(ed, kDoorSingle);
    CHECK(s.door != nullptr);
    if (!s.door) return;
    const std::vector<GameObject *> leaves = r515_leaves(*s.door);
    CHECK(leaves.size() == 1);
    if (leaves.size() != 1) return;
    GameObject *leaf = leaves[0];
    const BlockoutPiece *bp = s.door->get<BlockoutPiece>();
    const uint64_t bytes = r515_bytes(*s.bo), builds = r515_builds(ed);
    ed.select_for_test(leaf->id, false);
    ed.step_frame_headless();
    ed.mark_changed_for_test("test scene");
    r477_settle(ed, 3);
    const size_t steps = ed.undo_depth_for_test();
    r515_key(ed, platform::KEY_G);
    CHECK(ed.transform_active_for_test());
    r515_key(ed, platform::KEY_R);
    r515_key(ed, platform::KEY_Y);
    r515_type(ed, "45");
    r515_key(ed, platform::KEY_ENTER);
    r477_settle(ed, 4);
    std::printf("    the leaf is at %.3f degrees, Open Amount %.4f, %zu undo step(s)\n", r515_yaw(*leaf), bp->open_amount, ed.undo_depth_for_test() - steps);
    CHECK(!ed.transform_active_for_test());
    CHECK_NEAR(std::fabs(r515_yaw(*leaf)), 45.0f, 0.05f);
    CHECK_NEAR(bp->open_amount, 0.5f, 1e-3f);
    CHECK(ed.undo_depth_for_test() == steps + 1);  // (the turn and the written number are one step)
    CHECK(r515_leaves(*s.door).size() == 1 && r515_leaves(*s.door)[0] == leaf);  // (turned by hand: the leaf stays, it is not made again)
    CHECK(r515_builds(ed) == builds && r515_bytes(*s.bo) == bytes);
    ed.command("window Inspector");
    ed.record_buttons_for_test(true);
    ed.select_for_test(s.door->id, false);
    r477_settle(ed, 3);
    CHECK(r496_has(ed, "field:Open Amount"));
    ed.command("undo");
    r515_settle(ed);
    GameObject *back = ed.scene().find(leaf->id);
    CHECK(back && std::fabs(r515_yaw(*back)) < 0.05f && ed.scene().find(s.door->id)->get<BlockoutPiece>()->open_amount == 0.0f);
  });

  test("blockout door leaf (515): a Double door with one leaf turned by hand takes its Open Amount from that leaf and the other leaf follows; a leaf tipped over by hand is only an object turned (Open Amount stays)", [] {
    Editor ed;
    const R499Scene s = r515_door(ed, kDoorDouble);
    CHECK(s.door != nullptr);
    if (!s.door) return;
    const std::vector<GameObject *> leaves = r515_leaves(*s.door);
    CHECK(leaves.size() == 2);
    if (leaves.size() != 2) return;
    const BlockoutPiece *bp = s.door->get<BlockoutPiece>();
    const float sign = leaves[1]->local().position.x > 0.0f ? 1.0f : -1.0f;  // (the +X jamb's leaf turns with +yaw)
    leaves[1]->set_local_euler(Vec3(0.0f, sign * 60.0f, 0.0f));
    ed.mark_changed_for_test("test turn");
    r515_settle(ed);
    CHECK_NEAR(bp->open_amount, 60.0f / 90.0f, 1e-3f);
    CHECK_NEAR(std::fabs(r515_yaw(*leaves[0])), 60.0f, 0.05f);  // (the other leaf followed)
    CHECK(r515_yaw(*leaves[0]) * r515_yaw(*leaves[1]) < 0.0f);
    const float open = bp->open_amount;
    leaves[0]->set_local_euler(Vec3(30.0f, 0.0f, 0.0f));  // (tipped over: not a swing)
    ed.mark_changed_for_test("test tip");
    r515_settle(ed);
    CHECK_NEAR(bp->open_amount, open, 1e-6f);
  });
}

static void tests_round515_doorway() {
  /* ---- Criterion 4: nothing in front of the wall is cut ---- */
  test("blockout door leaf (515): an open Single and Double door cuts nothing in front of the wall - a column standing in the swing keeps all its faces, and the Blockout's status has no swing cut to name", [] {
    for (int style : {kDoorSingle, kDoorDouble}) {
      Editor ed;
      const R499Scene s = r515_door(ed, style);
      CHECK(s.door != nullptr);
      if (!s.door) continue;
      /* A column 0.3 m wide in the swing: 0.5 m out into the room from the door, between the hinges. */
      const Vec3 at = s.door->world_matrix().point(Vec3(0.0f, 0.0f, 0.5f));
      GameObject *column = r477_piece(ed, *s.bo, "Column", 0, 0, at + Vec3(0.0f, 1.0f, 0.0f), Vec3(0.3f, 2.0f, 0.3f));
      CHECK(column != nullptr);
      if (!column) continue;
      ed.select_for_test(s.door->id, false);
      ed.command("blockout set open 1");
      r515_settle(ed);
      const Blockout *b = s.bo->get<Blockout>();
      const Mesh *m = r495_mesh(*s.bo);
      CHECK(m != nullptr);
      if (!m) continue;
      size_t faces = 0;
      for (size_t f = 0; f < m->face_count(); f++) faces += b->state.faces.piece[f] == column->id;
      std::printf("    %s: the column keeps %zu faces, %zu swing cut notes\n", kBlockoutDoorStyleNames[style], faces, b->state.swing_cuts.size());
      CHECK(faces >= 5 && b->state.swing_cuts.empty());  // (a box has six faces; the one on the floor is the floor's)
      /* The same Blockout with the door closed has the same bytes: Open Amount changed nothing in it. */
      const uint64_t open_bytes = r515_bytes(*s.bo);
      ed.command("blockout set open 0");
      r515_settle(ed);
      CHECK(r515_bytes(*s.bo) == open_bytes);
      for (const auto &line : ed.blockout_status_lines_for_test(*s.bo)) CHECK(line.first.find("cuts") == std::string::npos);
    }
  });

  test("blockout door leaf (515): the doorway of a styled door is the same whatever Open Amount, Hinge Side, Leaf and Leaf Thickness say - the frame and the clear opening are the Blockout's, the leaf is not", [] {
    for (int style : {kDoorSingle, kDoorDouble, kDoorSliding, kDoorArched}) {
      Editor ed;
      const R499Wall s = r499_wall(ed, style, r498_methods()[0], 0.01f);
      r515_settle(ed);
      CHECK(s.door != nullptr && r515_leaves(*s.door).size() == 1 + (style == kDoorDouble ? 1u : 0u));
      if (!s.door) continue;
      const uint64_t bytes = r515_bytes(*s.bo);
      CHECK(bytes != 0 && r498_judge(*s.bo).empty());
      BlockoutPiece *bp = s.door->get<BlockoutPiece>();
      bp->open_amount = 0.7f, bp->hinge_side = 1, bp->slide_direction = 1, bp->leaf_thickness = 0.07f;
      ed.mark_changed_for_test("test numbers");
      r515_settle(ed);
      CHECK(r515_bytes(*s.bo) == bytes);
      bp->leaf = false;
      ed.mark_changed_for_test("test leaf off");
      r515_settle(ed);
      CHECK(r515_bytes(*s.bo) == bytes && r515_leaves(*s.door).empty());
      CHECK(r498_class(*s.bo, *s.door, kClassLeaf).faces == 0);  // (no Leaf faces in the Blockout's mesh at all: the leaf is an object)
      CHECK(r498_class(*s.bo, *s.door, kClassSolid).faces >= 4 && r498_class(*s.bo, *s.door, kClassCut).faces >= 4);
    }
  });
}

static void tests_round515_follow() {
  /* ---- Criterion 5: the leaves follow their door ---- */
  test("blockout door leaf (515): moving the door, moving its room, turning the Blockout, duplicating and deleting the door - the leaves follow, are duplicated and go - and an undo brings them back; deleting a leaf by hand turns its door's Leaf off in one undo step", [] {
    Editor ed;
    const R499Scene s = r515_door(ed, kDoorDouble);
    CHECK(s.door != nullptr);
    if (!s.door) return;
    auto leaf_world = [&](const GameObject &leaf) { return leaf.world_matrix().point(Vec3(0.0f, 0.0f, 0.0f)); };
    auto hinge_world = [&](const GameObject &door, size_t i) { return door.world_matrix().point(r515_leaves(door)[i]->local().position); };
    const std::vector<GameObject *> leaves = r515_leaves(*s.door);
    CHECK(leaves.size() == 2);
    if (leaves.size() != 2) return;
    /* Moved along its wall, with the room, with the Blockout turned: each leaf stays on its hinge. */
    s.door->set_local_position(s.door->local().position + Vec3(0.0f, 0.0f, 0.4f));
    ed.mark_changed_for_test("test move");
    s.room->set_local_position(s.room->local().position + Vec3(1.0f, 0.0f, 0.5f));
    s.bo->set_local_euler(Vec3(0.0f, 30.0f, 0.0f));
    ed.mark_changed_for_test("test move");
    r515_settle(ed);
    for (size_t i = 0; i < 2; i++) CHECK(length(leaf_world(*leaves[i]) - hinge_world(*s.door, i)) < 1e-4f);
    CHECK(r515_leaves(*s.door).size() == 2);
    /* Duplicated: the copy has its own two leaves, the original keeps its two. */
    const size_t before = ed.scene().object_count();
    ed.select_for_test(s.door->id, false);
    ed.command("duplicate");
    r515_settle(ed);
    GameObject *copy = ed.active_object_for_test();
    CHECK(copy && copy != s.door && copy->get<BlockoutPiece>());
    if (!copy) return;
    const std::vector<GameObject *> mine = r515_leaves(*copy);
    CHECK(mine.size() == 2 && r515_leaves(*s.door).size() == 2 && ed.scene().object_count() == before + 3);
    for (GameObject *l : mine) CHECK(l != leaves[0] && l != leaves[1] && l->parent == copy);
    const uint64_t copy_id = copy->id, second = mine[1]->id;  // (an undo makes the objects again: the ids, not the pointers, are kept)
    /* A leaf deleted by hand turns the copy's Leaf off, in one undo step; the other leaf goes with it. */
    ed.select_for_test(mine[0]->id, false);
    r477_settle(ed, 2);
    const size_t steps = ed.undo_depth_for_test();
    ed.command("delete");
    r515_settle(ed);
    GameObject *again = ed.scene().find(copy->id);
    CHECK(again && !again->get<BlockoutPiece>()->leaf && r515_leaves(*again).empty());
    CHECK(ed.undo_depth_for_test() == steps + 1);
    ed.command("undo");
    r515_settle(ed);
    again = ed.scene().find(copy_id);
    CHECK(again && again->get<BlockoutPiece>()->leaf && r515_leaves(*again).size() == 2);
    /* The door deleted: its leaves go; an undo brings all three objects back. */
    const size_t objects = ed.scene().object_count();
    ed.select_for_test(copy_id, false);
    ed.command("delete");
    r515_settle(ed);
    CHECK(ed.scene().object_count() == objects - 3 && ed.scene().find(second) == nullptr);
    ed.command("undo");
    r515_settle(ed);
    const GameObject *door_back = ed.scene().find(copy_id);
    CHECK(ed.scene().object_count() == objects && door_back && r515_leaves(*door_back).size() == 2);
  });

  test("blockout door leaf (515): a leaf moved out of its door is an ordinary object and the door makes a new one; a Blockout's Apply Scale to Pieces keeps the leaf's object scale at 1 and its mesh follows the door's numbers", [] {
    Editor ed;
    const R499Scene s = r515_door(ed, kDoorSingle);
    CHECK(s.door != nullptr);
    if (!s.door) return;
    GameObject *leaf = r515_leaves(*s.door).empty() ? nullptr : r515_leaves(*s.door)[0];
    CHECK(leaf != nullptr);
    if (!leaf) return;
    const uint64_t id = leaf->id;
    ed.scene().set_parent(leaf, nullptr, -1, true);
    ed.mark_changed_for_test("test reparent");
    r515_settle(ed);
    const GameObject *out = ed.scene().find(id);
    CHECK(out && !out->get<BlockoutLeaf>());  // (an ordinary object now)
    CHECK(r515_leaves(*s.door).size() == 1 && r515_leaves(*s.door)[0]->id != id && s.door->get<BlockoutPiece>()->leaf);  // (the door made its own again)
    /* Scale on the Blockout goes into the numbers; the leaf follows them with scale 1. */
    s.bo->set_local_scale(Vec3(2.0f, 1.0f, 1.0f));
    ed.select_for_test(s.bo->id, false);
    ed.command("blockout applyscale");
    r515_settle(ed);
    const GameObject *l = r515_leaves(*s.door).empty() ? nullptr : r515_leaves(*s.door)[0];
    CHECK(l != nullptr);
    if (!l) return;
    CHECK(l->local().scale.x == 1.0f && l->local().scale.y == 1.0f && l->local().scale.z == 1.0f);
    const BlockoutPiece *bp = s.door->get<BlockoutPiece>();
    const Mesh *m = l->get<MeshFilter>()->mesh.get();
    CHECK(m != nullptr);
    if (!m) return;
    Vec3 lo(1e30f), hi(-1e30f);
    for (const Vec3 &p : m->positions)
      for (int a = 0; a < 3; a++) lo[a] = std::min(lo[a], p[a]), hi[a] = std::max(hi[a], p[a]);
    CHECK_NEAR(hi.x - lo.x, bp->width - 2.0f * bp->frame_width, 1e-3f);  // (the leaf is as wide as the clear opening the scaled numbers give)
  });

  /* ---- Criterion 6: Bake and Unbake ---- */
  test("blockout door leaf (515): Bake to Mesh keeps the leaves as separate enabled objects at the same world place, and Unbake returns them under their door", [] {
    Editor ed;
    const R499Scene s = r515_door(ed, kDoorDouble);
    CHECK(s.door != nullptr);
    if (!s.door) return;
    ed.select_for_test(s.door->id, false);
    ed.command("blockout set open 0.4");
    r515_settle(ed);
    std::vector<uint64_t> ids;
    std::vector<Vec3> world;
    for (const GameObject *l : r515_leaves(*s.door)) ids.push_back(l->id), world.push_back(l->world_matrix().point(Vec3(0.3f, 0.5f, 0.0f)));
    CHECK(ids.size() == 2);
    const uint64_t door_id = s.door->id;
    ed.select_for_test(s.bo->id, false);
    ed.command("blockout bake");
    r477_settle(ed, 3);
    GameObject *bo = r477_find(ed);
    CHECK(bo && bo->get<BlockoutBaked>());
    if (!bo) return;
    for (size_t i = 0; i < ids.size(); i++) {
      const GameObject *l = ed.scene().find(ids[i]);
      CHECK(l && l->active && l->active_in_hierarchy() && l->parent == bo && l->get<MeshFilter>());
      if (l) CHECK(length(l->world_matrix().point(Vec3(0.3f, 0.5f, 0.0f)) - world[i]) < 1e-4f);
    }
    r477_settle(ed, 3);
    CHECK(ed.scene().find(ids[0]) && ed.scene().find(ids[1]) && ed.scene().find(ids[0])->get<BlockoutLeaf>());  // (baking does not let go of them)
    ed.select_for_test(bo->id, false);
    ed.command("blockout unbake");
    r515_settle(ed);
    const GameObject *door = ed.scene().find(door_id);
    CHECK(door != nullptr && r515_leaves(*door).size() == 2);
    for (size_t i = 0; i < ids.size(); i++) {
      const GameObject *l = ed.scene().find(ids[i]);
      CHECK(l && door && l->parent == door);
      if (l) CHECK(length(l->world_matrix().point(Vec3(0.3f, 0.5f, 0.0f)) - world[i]) < 1e-4f);
    }
  });
}

static void tests_round515_object() {
  /* ---- Criterion 7: a dynamic object ---- */
  test("blockout door leaf (515): a leaf is not Contribute GI, has a Box collider fitted to its slab and takes the Blockout's Leaf material", [] {
    Editor ed;
    const R499Scene s = r515_door(ed, kDoorSingle);
    CHECK(s.door != nullptr);
    if (!s.door) return;
    const std::vector<GameObject *> leaves = r515_leaves(*s.door);
    CHECK(leaves.size() == 1);
    if (leaves.empty()) return;
    const GameObject &leaf = *leaves[0];
    const MeshRenderer *mr = leaf.get<MeshRenderer>();
    const ColliderComponent *col = leaf.get<ColliderComponent>();
    CHECK(mr != nullptr);
    if (mr) CHECK(!mr->contribute_gi);
    CHECK(col != nullptr);
    if (col) CHECK(col->shape == 0);  // (a Box; its size is the slab's, below - the Fit to Mesh tick is spent once the collider is fitted)
    const Rigidbody *body = leaf.get<Rigidbody>();  // (kinematic in Play mode: a body that follows the transform and is not pulled by gravity)
    CHECK(body != nullptr);
    if (body) CHECK(body->is_kinematic && !body->use_gravity);
    const BlockoutPiece *bp = s.door->get<BlockoutPiece>();
    if (col) {
      CHECK_NEAR(col->size.x, bp->width - 2.0f * bp->frame_width, 1e-4f);
      CHECK_NEAR(col->size.z, std::min(bp->leaf_thickness, 0.6f * bp->frame_depth), 1e-4f);
    }
    /* The lightmap bake gathers the renderers that contribute GI: the leaf's does not (it moves: the wall is baked with the doorway open, probes light the leaf). */
    const std::string leaf_mat = r482_material("R515Leaf", Vec3(0.2f, 0.5f, 0.9f));
    ed.select_for_test(s.bo->id, false);
    ed.command("blockout set material leaf " + leaf_mat);
    r515_settle(ed);
    const std::vector<GameObject *> again = r515_leaves(*s.door);
    CHECK(again.size() == 1 && again[0]->get<MeshRenderer>() && again[0]->get<MeshRenderer>()->materials.size() == 1 && again[0]->get<MeshRenderer>()->materials[0] &&
          again[0]->get<MeshRenderer>()->materials[0]->asset_path == leaf_mat);
  });

  /* ---- Criterion 8: export ---- */
  test("blockout door leaf (515): an FBX, GLB and OBJ of a Blockout with a door hold the leaf as its own mesh node, and a GLB with Keep Transforms puts that node's pivot on the hinge, turned as far as Open Amount says (the exporter keeps no parents: each object is a top-level node)", [] {
    Editor ed;
    const R499Scene s = r515_door(ed, kDoorSingle);
    CHECK(s.door != nullptr);
    if (!s.door) return;
    ed.select_for_test(s.door->id, false);
    ed.command("blockout set open 0.5");
    r515_settle(ed);
    CHECK(r515_leaves(*s.door).size() == 1);
    for (const char *format : {"fbx", "glb", "obj"}) {
      const std::string name = std::string("r515_door.") + format;
      const std::string path = r482_export(ed, format, name.c_str());
      ImportResult r;
      CHECK(import_model(path, r));
      size_t meshes = 0, leaf_nodes = 0;
      for (const ImportedNode &n : r.nodes) {
        const bool has_mesh = n.mesh && n.mesh->face_count() > 0;
        meshes += has_mesh;
        leaf_nodes += has_mesh && n.name.find("Leaf") != std::string::npos;
        CHECK(n.name.find("Blockout Pieces") == std::string::npos);
      }
      std::printf("    %s: %zu mesh node(s), %zu named Leaf\n", name.c_str(), meshes, leaf_nodes);
      CHECK(meshes >= 1);
      if (std::string(format) == "obj") continue;  // (an OBJ is flat: a mesh per object, no node's place to read back)
      CHECK(meshes == 2 && leaf_nodes == 1);
    }
    /* Keep Transforms (glTF): the leaf's node is where its pivot is - the hinge - and turned as the leaf is, its vertices around the pivot. */
    const std::string dir = ed.resolve_asset_path_for_test("Assets/Exports");
    fs::make_dirs(dir);
    const std::string path = fs::join(dir, "r515_door_keep.glb");
    fs::remove_file(path);
    ed.command("export glb all transforms " + path);
    r477_settle(ed, 5);
    for (int i = 0; i < 400 && (!fs::exists(path) || ed.file_export_running_for_test()); i++) ed.step_frame_headless();
    ImportResult r;
    CHECK(import_model(path, r));
    const GameObject &leaf = *r515_leaves(*s.door)[0];
    const Vec3 hinge = leaf.world_matrix().point(Vec3(0.0f, 0.0f, 0.0f)), forward = normalize(leaf.world_matrix().dir(Vec3(0.0f, 0.0f, 1.0f)));
    size_t found = 0;
    for (const ImportedNode &n : r.nodes) {
      if (n.name.find("Leaf") == std::string::npos || !n.mesh) continue;
      found++;
      const Vec3 f = n.rotation.rotate(Vec3(0.0f, 0.0f, 1.0f));
      std::printf("    keep transforms: the leaf node at (%.3f %.3f %.3f), the hinge at (%.3f %.3f %.3f), turned %.4f\n", n.position.x, n.position.y, n.position.z, hinge.x, hinge.y, hinge.z, dot(f, forward));
      CHECK(length(n.position - hinge) < 1e-3f);
      CHECK(dot(f, forward) > 0.999f);
    }
    CHECK(found == 1);
  });
}

static void tests_round515_old() {
  /* ---- Criterion 9: scenes of an earlier build ---- */
  test("blockout door leaf (515): a scene with a styled door and no leaf objects (round 499's, whose leaf was in the mesh) opens with the leaf objects made and the doorway of the same mesh bytes", [] {
    Editor ed;
    const R499Scene s = r515_door(ed, kDoorSingle);
    CHECK(s.door != nullptr);
    if (!s.door) return;
    const uint64_t bytes = r515_bytes(*s.bo);
    /* The leaf objects left out of the saved text: what round 499 wrote (it had none). */
    std::vector<GameObject *> leaves = r515_leaves(*s.door);
    CHECK(leaves.size() == 1);
    ed.scene().destroy(leaves);
    const std::string path = te_scene_path(ed, "r515_old.scene");
    CHECK(fs::write_file(path, save_scene_text(ed.scene())));
    ed.command("newscene");
    CHECK(ed.open_scene_for_test(path));
    for (int i = 0; i < 400 && !r477_find(ed); i++) ed.step_frame_headless();
    r515_settle(ed);
    GameObject *door = ed.scene().find_by_name("Door");
    CHECK(door && door->get<BlockoutPiece>() && door->get<BlockoutPiece>()->leaf);
    if (!door) return;
    CHECK(r515_leaves(*door).size() == 1);
    GameObject *bo = r477_find(ed);
    CHECK(bo && r515_bytes(*bo) == bytes);
    /* A scene saved with the leaf in it has it back on open, the same object, not a second. */
    const std::string again = te_scene_path(ed, "r515_new.scene");
    CHECK(fs::write_file(again, save_scene_text(ed.scene())));
    ed.command("newscene");
    CHECK(ed.open_scene_for_test(again));
    for (int i = 0; i < 400 && !r477_find(ed); i++) ed.step_frame_headless();
    r515_settle(ed);
    door = ed.scene().find_by_name("Door");
    CHECK(door && r515_leaves(*door).size() == 1);
    fs::remove_file(path);
    fs::remove_file(again);
  });

  /* ---- The Inspector and the console ---- */
  test("blockout door leaf (515): a selected leaf shows its door's Open Amount and the line 'Made by Door ...'; editing it edits the door; `blockout leaf select` selects the selected door's leaves and `blockout leaf door` goes back", [] {
    Editor ed;
    const R499Scene s = r515_door(ed, kDoorSingle);
    CHECK(s.door != nullptr);
    if (!s.door) return;
    ed.select_for_test(s.door->id, false);
    ed.command("blockout leaf select");
    r477_settle(ed, 2);
    const std::vector<GameObject *> leaves = r515_leaves(*s.door);
    CHECK(leaves.size() == 1 && ed.active_object_for_test() == leaves[0]);
    if (leaves.empty()) return;
    ed.command("window Inspector");
    ed.record_buttons_for_test(true);
    r477_settle(ed, 3);
    CHECK(r496_has(ed, "field:Open Amount") && r496_has(ed, strprintf("shown:Made by Door '%s' - its shape follows the door's numbers", s.door->name.c_str()).c_str()));
    CHECK(r479_type(ed, "field:Open Amount", "0.6"));
    CHECK_NEAR(s.door->get<BlockoutPiece>()->open_amount, 0.6f, 1e-6f);
    r515_settle(ed);
    CHECK_NEAR(std::fabs(r515_yaw(*leaves[0])), 54.0f, 0.01f);
    ed.command("blockout leaf door");
    r477_settle(ed, 2);
    CHECK(ed.active_object_for_test() == s.door);
    const size_t depth = ed.undo_depth_for_test();
    const size_t mark = r477_mark();
    ed.select_for_test(leaves[0]->id, false);
    ed.command("blockout leaf nonsense");
    CHECK(te_warnings_since(mark) >= 1 && ed.undo_depth_for_test() == depth);
  });
}

/* ---- Round 515 review: one undo step, kinematic in Play, no write-back in a preview, a leaf the door did not make, names ---- */
static void tests_round515_review() {
  test("blockout door leaf (515, review): a real Delete key on a selected leaf is ONE undo step that turns its door's Leaf off, and one undo brings the leaf and Leaf on back", [] {
    Editor ed;
    const R499Scene s = r515_door(ed, kDoorSingle);
    CHECK(s.door != nullptr);
    if (!s.door) return;
    const uint64_t door_id = s.door->id, leaf_id = r515_leaves(*s.door)[0]->id;
    ed.select_for_test(leaf_id, false);
    ed.step_frame_headless();
    ed.mark_changed_for_test("test scene");
    r477_settle(ed, 3);
    const size_t steps = ed.undo_depth_for_test();
    r515_key(ed, platform::KEY_DELETE);
    r477_settle(ed, 4);
    const GameObject *door = ed.scene().find(door_id);
    CHECK(door && door->get<BlockoutPiece>() && !door->get<BlockoutPiece>()->leaf && r515_leaves(*door).empty() && ed.scene().find(leaf_id) == nullptr);
    std::printf("    undo steps after the Delete key: %zu\n", ed.undo_depth_for_test() - steps);
    CHECK(ed.undo_depth_for_test() == steps + 1);
    ed.command("undo");
    r515_settle(ed);
    door = ed.scene().find(door_id);
    CHECK(door && door->get<BlockoutPiece>() && door->get<BlockoutPiece>()->leaf && r515_leaves(*door).size() == 1);
  });

  test("blockout door leaf (515, review): turning a leaf with the Rotate gizmo's mouse drag writes Open Amount into the same undo step - one step for the turn, one undo returns both", [] {
    for (int pixels : {60, -60}) {
      Editor ed;
      const R499Scene s = r515_door(ed, kDoorSingle);
      CHECK(s.door != nullptr);
      if (!s.door) continue;
      GameObject *leaf = r515_leaves(*s.door)[0];
      const uint64_t door_id = s.door->id, leaf_id = leaf->id;
      const float side = leaf->local().position.x > 0.0f ? 1.0f : -1.0f;
      ed.select_for_test(leaf_id, false);
      ed.command("tool rotate");
      ed.step_frame_headless();
      ed.mark_changed_for_test("test scene");
      r477_settle(ed, 3);
      const size_t steps = ed.undo_depth_for_test();
      ed.command(strprintf("perf drag gizmo ry %d", pixels));
      for (int i = 0; i < 400 && ed.perf_script_active_for_test(); i++) ed.step_frame_headless();
      r477_settle(ed, 4);
      const GameObject *turned = ed.scene().find(leaf_id);
      const GameObject *door = ed.scene().find(door_id);
      CHECK(turned && door && door->get<BlockoutPiece>());
      if (!turned || !door || !door->get<BlockoutPiece>()) continue;
      const float yaw = r515_yaw(*turned), want = std::min(1.0f, std::max(0.0f, side * yaw / 90.0f));
      std::printf("    drag %d px: the leaf at %.2f degrees, Open Amount %.4f, %zu undo step(s)\n", pixels, yaw, door->get<BlockoutPiece>()->open_amount, ed.undo_depth_for_test() - steps);
      CHECK(std::fabs(yaw) > 1.0f);
      CHECK_NEAR(door->get<BlockoutPiece>()->open_amount, want, 2e-3f);
      CHECK(ed.undo_depth_for_test() == steps + 1);
      ed.command("undo");
      r515_settle(ed);
      door = ed.scene().find(door_id);
      CHECK(door && door->get<BlockoutPiece>()->open_amount == 0.0f && r515_leaves(*door).size() == 1 && std::fabs(r515_yaw(*r515_leaves(*door)[0])) < 0.05f);
    }
  });

  test("blockout door leaf (515, review): a leaf has a kinematic Rigidbody without gravity, and in Play mode a leaf moved by a script moves its body - a box dropped on it lands on it where it is now", [] {
    Editor ed;
    const R499Scene s = r515_door(ed, kDoorSingle);
    CHECK(s.door != nullptr);
    if (!s.door) return;
    const GameObject *l0 = r515_leaves(*s.door)[0];
    CHECK(l0->get<Rigidbody>() && l0->get<Rigidbody>()->is_kinematic && !l0->get<Rigidbody>()->use_gravity);
    const uint64_t door_id = s.door->id;
    ed.command("play");
    CHECK(ed.playing_for_test());
    GameObject *pd = ed.scene().find(door_id);
    if (!pd || r515_leaves(*pd).size() != 1) {
      CHECK(false);
      ed.command("stop");
      return;
    }
    GameObject *leaf = r515_leaves(*pd)[0];
    /* Laid flat and lifted 3 m by a script: a slab 0.78 m by 2 m, 4 cm thick. The body must follow, or the box falls past where the slab was made. */
    leaf->set_local_euler(Vec3(90.0f, 0.0f, 0.0f));
    leaf->set_local_position(leaf->local().position + Vec3(0.0f, 3.0f, 0.0f));
    ed.step_frame_headless();  // (the body takes the new pose; the world matrices are read after it)
    const ColliderComponent *col = leaf->get<ColliderComponent>();
    const Vec3 centre = leaf->world_matrix().point(col ? col->center : Vec3(0.0f));
    std::printf("    the slab's middle in Play: y %.3f\n", centre.y);
    GameObject *box = ed.scene().create("Box");
    box->add<MeshFilter>()->mesh = primitives::cube();
    box->add<MeshRenderer>();
    box->set_local_scale(Vec3(0.2f));
    box->set_world_position(centre + Vec3(0.0f, 2.0f, 0.0f));
    box->add<Rigidbody>()->bounciness = 0.0f;
    for (int i = 0; i < 300; i++) ed.step_frame_headless();
    const GameObject *landed = ed.scene().find_by_name("Box");
    std::printf("    the box ends at y %.3f, the slab's middle was at y %.3f\n", landed ? landed->world_position().y : -99.0f, centre.y);
    /* The slab was made standing, its top at about 2 m: a body left where it was made would hold the box below that. Lifted 3 m and laid flat, it holds it above 3 m. */
    CHECK(landed && landed->world_position().y > 3.0f && landed->world_position().y < 4.0f);
    ed.command("stop");
  });

  test("blockout door leaf (515, review): while the Animation window's Preview poses the leaf, the pose is not written back to Open Amount and no undo step is amended; Preview off puts the leaf back", [] {
    Editor ed;
    const R499Scene s = r515_door(ed, kDoorSingle);
    CHECK(s.door != nullptr);
    if (!s.door) return;
    const uint64_t door_id = s.door->id;
    ed.select_for_test(door_id, false);
    ed.command("anim new");
    ed.command("anim key Leaf Rotation 1 0 0");
    ed.command("anim key Leaf Rotation 1 30 60");
    r477_settle(ed, 3);
    const size_t steps = ed.undo_steps_for_test();
    ed.command("anim preview on");
    if (!ed.anim_preview_for_test()) {
      CHECK(false);  // (the Animation window did not start: the rig of this test is wrong)
      return;
    }
    ed.command("anim frame 30");
    for (int i = 0; i < 8; i++) ed.step_frame_headless();
    const GameObject *door = ed.scene().find(door_id);
    CHECK(door && r515_leaves(*door).size() == 1);
    if (!door || r515_leaves(*door).size() != 1) return;
    std::printf("    in the preview: the leaf at %.2f degrees, Open Amount %.4f, undo steps %zu -> %zu\n", r515_yaw(*r515_leaves(*door)[0]), door->get<BlockoutPiece>()->open_amount, steps, ed.undo_steps_for_test());
    CHECK_NEAR(std::fabs(r515_yaw(*r515_leaves(*door)[0])), 60.0f, 0.5f);
    CHECK(door->get<BlockoutPiece>()->open_amount == 0.0f);
    CHECK(ed.undo_steps_for_test() == steps);
    ed.command("anim preview off");
    for (int i = 0; i < 4; i++) ed.step_frame_headless();
    door = ed.scene().find(door_id);
    CHECK(door && r515_leaves(*door).size() == 1 && door->get<BlockoutPiece>()->open_amount == 0.0f);
    CHECK(ed.undo_steps_for_test() == steps);  // (nothing was written back after the preview either)
    if (door && r515_leaves(*door).size() == 1) CHECK(std::fabs(r515_yaw(*r515_leaves(*door)[0])) < 0.5f);
  });

  test("blockout door leaf (515, review): Ctrl+D on a leaf keeps the copy as an ordinary object (no BlockoutLeaf) and the door keeps its own leaf; a leaf dragged under another door stays an ordinary object and neither door destroys it", [] {
    Editor ed;
    const R499Scene s = r515_door(ed, kDoorSingle);
    CHECK(s.door != nullptr);
    if (!s.door) return;
    const uint64_t door_id = s.door->id, leaf_id = r515_leaves(*s.door)[0]->id;
    const size_t before = ed.scene().object_count();
    ed.select_for_test(leaf_id, false);
    ed.command("duplicate");
    r515_settle(ed);
    GameObject *copy = ed.active_object_for_test();
    CHECK(copy && copy->id != leaf_id && !copy->get<BlockoutLeaf>() && copy->get<MeshFilter>());
    const GameObject *door = ed.scene().find(door_id);
    CHECK(door && r515_leaves(*door).size() == 1 && r515_leaves(*door)[0]->id == leaf_id && ed.scene().object_count() == before + 1);
    /* Under a second door: the first makes itself a new leaf, the second keeps its own and the stray is left alone. */
    ed.select_for_test(door_id, false);
    ed.command("duplicate");
    r515_settle(ed);
    GameObject *second = ed.active_object_for_test();
    CHECK(second && second->id != door_id && second->get<BlockoutPiece>() && r515_leaves(*second).size() == 1);
    if (!second) return;
    const uint64_t second_id = second->id, copy_id = copy ? copy->id : 0;
    if (copy) ed.scene().set_parent(copy, ed.scene().find(second_id), -1, true);
    ed.mark_changed_for_test("test reparent");
    r515_settle(ed);
    const GameObject *moved = ed.scene().find(copy_id);
    CHECK(moved && !moved->get<BlockoutLeaf>() && moved->parent && moved->parent->id == second_id);
    CHECK(ed.scene().find(second_id) && r515_leaves(*ed.scene().find(second_id)).size() == 1 && ed.scene().find(door_id) && r515_leaves(*ed.scene().find(door_id)).size() == 1);
  });

  test("blockout door leaf (515, review): a leaf marked as another door's and pasted under a door with Leaf off, or under a room piece, survives as an ordinary object - never destroyed, never adopted", [] {
    for (int under_room = 0; under_room < 2; under_room++) {
      Editor ed;
      const R499Scene s = r515_door(ed, kDoorSingle);
      CHECK(s.door != nullptr && s.room != nullptr);
      if (!s.door || !s.room) return;
      uint64_t parent_id = s.room->id;
      if (!under_room) {
        ed.select_for_test(s.room->id, false);
        ed.command("blockout add door single");
        GameObject *off = ed.active_object_for_test();
        CHECK(off && off != s.door && off->get<BlockoutPiece>());
        if (!off) return;
        off->get<BlockoutPiece>()->leaf = false;
        ed.mark_changed_for_test("test leaf off");
        r515_settle(ed);
        CHECK(r515_leaves(*off).empty());
        parent_id = off->id;
      }
      GameObject *pasted = ed.scene().create("Pasted Leaf", ed.scene().find(parent_id));
      pasted->add<MeshFilter>()->mesh = primitives::cube();
      BlockoutLeaf *mark = pasted->add<BlockoutLeaf>();
      mark->index = 0, mark->made_by = std::to_string(s.door->id);  // (the first door still has its own leaf of that index: a twin)
      const uint64_t pasted_id = pasted->id;
      ed.mark_changed_for_test("test paste");
      r515_settle(ed);
      const GameObject *kept = ed.scene().find(pasted_id);
      CHECK(kept && !kept->get<BlockoutLeaf>() && kept->get<MeshFilter>() && kept->parent && kept->parent->id == parent_id);
      CHECK(ed.scene().find(s.door->id) && r515_leaves(*ed.scene().find(s.door->id)).size() == 1);
      if (!under_room) CHECK(r515_leaves(*ed.scene().find(parent_id)).empty());
    }
  });

  test("blockout door leaf (515, review): a leaf kept when the style changes takes the name of the style - Leaf for a Single, Leaf L and Leaf R for a Double", [] {
    Editor ed;
    const R499Scene s = r515_door(ed, kDoorDouble);
    CHECK(s.door != nullptr);
    if (!s.door) return;
    ed.select_for_test(s.door->id, false);
    for (const char *word : {"single", "double", "single"}) {
      ed.command(std::string("blockout set style ") + word);
      r515_settle(ed);
      const std::vector<GameObject *> leaves = r515_leaves(*s.door);
      const bool two = std::string(word) == "double";
      CHECK(leaves.size() == (two ? 2u : 1u));
      if (leaves.size() == 1) CHECK(leaves[0]->name == "Leaf");
      if (leaves.size() == 2) CHECK(leaves[0]->name == "Leaf L" && leaves[1]->name == "Leaf R");
    }
  });

  test("blockout door leaf (515, review): a NaN Leaf Thickness and a NaN Open Amount leave the doorway's bytes as they were and the state still over many frames", [] {
    Editor ed;
    const R499Wall w = r499_wall(ed, kDoorSingle, r498_methods()[0], 0.01f);
    r515_settle(ed);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const uint64_t bytes = r515_bytes(*w.bo), builds = r515_builds(ed);
    BlockoutPiece *bp = w.door->get<BlockoutPiece>();
    bp->leaf_thickness = nan;
    ed.mark_changed_for_test("test nan");
    r515_settle(ed);
    CHECK(r515_bytes(*w.bo) == bytes && r498_judge(*w.bo).empty());
    bp->open_amount = nan;
    ed.mark_changed_for_test("test nan open");
    r515_settle(ed);
    const size_t steps = ed.undo_depth_for_test();
    for (int i = 0; i < 20; i++) ed.step_frame_headless();
    CHECK(r515_bytes(*w.bo) == bytes && r515_builds(ed) == builds && ed.undo_depth_for_test() == steps);
    CHECK(r515_leaves(*w.door).size() == 1);
  });
}

void tests_round515() {
  tests_round515_leaves();
  tests_round515_doorway();
  tests_round515_follow();
  tests_round515_object();
  tests_round515_old();
  tests_round515_review();
}
// r515-end

// r515te-begin
/* ===================================================================== */
/* Round 515, the test engineer's edges (task 0497, ADR 0136)             */
/* ===================================================================== */

namespace {

/* The first door piece at or under `root`. */
GameObject *r515te_door_in(GameObject &root) {
  BlockoutPiece *bp = root.get<BlockoutPiece>();
  if (bp && bp->opening == 1) return &root;
  for (GameObject *c : root.children)
    if (GameObject *d = r515te_door_in(*c)) return d;
  return nullptr;
}

/* Every leaf object in the scene, wherever it stands (an orphan or a stray one counts). */
size_t r515te_all_leaves(Editor &ed) {
  size_t n = 0;
  ed.scene().for_each([&](GameObject &g) { n += g.get<BlockoutLeaf>() != nullptr; });
  return n;
}

/* How many leaf objects the door with this id has now (the object may have been made again by an undo: ids, not pointers). */
size_t r515te_leaf_count(Editor &ed, uint64_t door_id) {
  const GameObject *d = ed.scene().find(door_id);
  return d ? r515_leaves(*d).size() : 9999;
}

bool r515te_finite_leaf(const GameObject &leaf) {
  const Transform &t = leaf.local();
  if (!std::isfinite(t.position.x) || !std::isfinite(t.position.y) || !std::isfinite(t.position.z)) return false;
  if (!std::isfinite(t.rotation.x) || !std::isfinite(t.rotation.y) || !std::isfinite(t.rotation.z) || !std::isfinite(t.rotation.w)) return false;
  const MeshFilter *mf = leaf.get<MeshFilter>();
  if (!mf || !mf->mesh || mf->mesh->face_count() == 0) return false;
  for (const Vec3 &p : mf->mesh->positions)
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return false;
  return true;
}

struct R515State {
  uint64_t builds = 0, bytes = 0;
  size_t undo = 0, objects = 0;
  float open = 0.0f;
};

R515State r515te_state(Editor &ed, const GameObject &bo, uint64_t door_id) {
  R515State s;
  s.builds = r515_builds(ed), s.bytes = r515_bytes(bo), s.undo = ed.undo_depth_for_test(), s.objects = ed.scene().object_count();
  const GameObject *d = ed.scene().find(door_id);
  s.open = d && d->get<BlockoutPiece>() ? d->get<BlockoutPiece>()->open_amount : -1.0f;
  return s;
}

/* Frames with nothing asked for: the state must not move (builds, mesh bytes, undo steps, objects). */
bool r515te_still(Editor &ed, const GameObject &bo, uint64_t door_id, int frames = 8) {
  const R515State a = r515te_state(ed, bo, door_id);
  for (int i = 0; i < frames; i++) ed.step_frame_headless();
  r495_built(ed);
  const R515State b = r515te_state(ed, bo, door_id);
  const bool same = a.builds == b.builds && a.bytes == b.bytes && a.undo == b.undo && a.objects == b.objects && (a.open == b.open || (std::isnan(a.open) && std::isnan(b.open)));
  if (!same) std::printf("    NOT STILL: builds %llu -> %llu, undo %zu -> %zu, objects %zu -> %zu, open %g -> %g\n", (unsigned long long)a.builds, (unsigned long long)b.builds, a.undo, b.undo, a.objects, b.objects, a.open, b.open);
  return same;
}

/* A leaf of `door` that hangs on its +X side (its +yaw opens it): Double's second or only. */
GameObject *r515te_plus_leaf(const GameObject &door) {
  for (GameObject *l : r515_leaves(door))
    if (l->local().position.x > 0.0f) return l;
  return nullptr;
}

/* Another door in the same room, moved `shift` metres along the wall; the first is moved the other way. */
GameObject *r515te_second_door(Editor &ed, const R499Scene &s, const char *words, float shift) {
  const Vec3 along = s.door->local().rotation.rotate(Vec3(1.0f, 0.0f, 0.0f));
  s.door->set_local_position(s.door->local().position - along * shift);
  ed.mark_changed_for_test("test move");
  r515_settle(ed);
  ed.select_for_test(s.bo->id, false);
  r477_settle(ed, 2);
  ed.command(std::string("blockout add door ") + words);
  GameObject *d2 = ed.active_object_for_test();
  if (!d2 || !d2->get<BlockoutPiece>() || d2 == s.door) return nullptr;
  d2->set_local_position(d2->local().position + along * shift);
  ed.mark_changed_for_test("test move");
  r515_settle(ed);
  return d2;
}

}  // namespace

static void tests_round515_te_undo() {
  test("blockout door leaf (515, te): undo and redo of Style changes Single -> Double -> Opening Only -> Revolving (Leaf on) walk the leaf objects 1 -> 2 -> 0 -> 1 and back, never leaving a stray leaf, and the state is still after each", [] {
    Editor ed;
    const R499Scene s = r515_door(ed, kDoorSingle);
    CHECK(s.door != nullptr);
    if (!s.door) return;
    const uint64_t door = s.door->id, bo = s.bo->id;
    std::vector<size_t> counts{r515te_leaf_count(ed, door)};
    auto act = [&](const char *cmd) {
      ed.select_for_test(door, false);
      r477_settle(ed, 2);
      ed.command(cmd);
      r515_settle(ed);
      counts.push_back(r515te_leaf_count(ed, door));
      CHECK(r515te_all_leaves(ed) == counts.back());  // (no leaf of a door that is not there)
    };
    act("blockout set style double");
    act("blockout set style opening");
    act("blockout set style revolving");
    act("blockout set leaf on");
    std::printf("    leaf counts after each step:");
    for (size_t c : counts) std::printf(" %zu", c);
    std::printf("\n");
    CHECK(counts == (std::vector<size_t>{1, 2, 0, 0, 1}));
    CHECK(r515te_still(ed, *ed.scene().find(bo), door));
    for (int i = (int)counts.size() - 2; i >= 0; i--) {  // (undo back down)
      ed.command("undo");
      r515_settle(ed);
      const size_t got = r515te_leaf_count(ed, door);
      std::printf("    undo -> %zu leaf object(s), expected %zu\n", got, counts[(size_t)i]);
      CHECK(got == counts[(size_t)i] && r515te_all_leaves(ed) == got);
    }
    CHECK(r515te_still(ed, *ed.scene().find(bo), door));
    for (size_t i = 1; i < counts.size(); i++) {  // (and redo up again)
      ed.command("redo");
      r515_settle(ed);
      const size_t got = r515te_leaf_count(ed, door);
      std::printf("    redo -> %zu leaf object(s), expected %zu\n", got, counts[i]);
      CHECK(got == counts[i] && r515te_all_leaves(ed) == got);
    }
    CHECK(r515te_still(ed, *ed.scene().find(bo), door));
  });

  test("blockout door leaf (515, te): undoing a hand-turned leaf and redoing it keeps the leaf and its door in step (the pose and Open Amount come back together), with no extra undo step made by the sync", [] {
    Editor ed;
    const R499Scene s = r515_door(ed, kDoorSingle);
    CHECK(s.door != nullptr);
    if (!s.door) return;
    const uint64_t door = s.door->id, bo = s.bo->id;
    GameObject *leaf = r515_leaves(*s.door)[0];
    const uint64_t leaf_id = leaf->id;
    const size_t steps = ed.undo_depth_for_test();
    leaf->set_local_euler(Vec3(0.0f, 36.0f * (leaf->local().position.x > 0.0f ? 1.0f : -1.0f), 0.0f));
    ed.mark_changed_for_test("test turn");
    r515_settle(ed);
    CHECK_NEAR(ed.scene().find(door)->get<BlockoutPiece>()->open_amount, 0.4f, 2e-3f);
    CHECK(ed.undo_depth_for_test() == steps + 1);
    ed.command("undo");
    r515_settle(ed);
    CHECK(ed.scene().find(door)->get<BlockoutPiece>()->open_amount == 0.0f && std::fabs(r515_yaw(*ed.scene().find(leaf_id))) < 0.05f);
    ed.command("redo");
    r515_settle(ed);
    CHECK_NEAR(ed.scene().find(door)->get<BlockoutPiece>()->open_amount, 0.4f, 2e-3f);
    CHECK_NEAR(std::fabs(r515_yaw(*ed.scene().find(leaf_id))), 36.0f, 0.1f);
    CHECK(r515te_leaf_count(ed, door) == 1 && r515te_still(ed, *ed.scene().find(bo), door));
  });
}

static void tests_round515_te_loops() {
  test("blockout door leaf (515, te): a hand-turned leaf asks for no Blockout build and the state stays still over many frames - the door's Open Amount follows once, the Blockout bytes and the undo depth do not move again", [] {
    for (int style : {kDoorSingle, kDoorDouble}) {
      Editor ed;
      const R499Scene s = r515_door(ed, style);
      CHECK(s.door != nullptr);
      if (!s.door) continue;
      const uint64_t door = s.door->id;
      const uint64_t builds = r515_builds(ed), bytes = r515_bytes(*s.bo);
      GameObject *leaf = r515te_plus_leaf(*s.door);
      CHECK(leaf != nullptr);
      if (!leaf) continue;
      leaf->set_local_euler(Vec3(0.0f, 63.0f, 0.0f));
      ed.mark_changed_for_test("test turn");
      r515_settle(ed);
      CHECK_NEAR(s.door->get<BlockoutPiece>()->open_amount, 0.7f, 2e-3f);
      CHECK(r515_builds(ed) == builds && r515_bytes(*s.bo) == bytes);
      CHECK(r515te_still(ed, *s.bo, door, 12));
      /* Both leaves of a Double turned by hand to different angles in one go: the walk settles on one number, and stays. */
      if (style == kDoorDouble) {
        const std::vector<GameObject *> both = r515_leaves(*s.door);
        both[0]->set_local_euler(Vec3(0.0f, -20.0f, 0.0f));
        both[1]->set_local_euler(Vec3(0.0f, 80.0f, 0.0f));
        ed.mark_changed_for_test("test turn both");
        r515_settle(ed);
        std::printf("    both turned: Open Amount %.4f, yaws %.2f %.2f\n", s.door->get<BlockoutPiece>()->open_amount, r515_yaw(*both[0]), r515_yaw(*both[1]));
        CHECK(std::fabs(r515_yaw(*both[0]) + r515_yaw(*both[1])) < 0.2f);  // (mirrored again: one number)
        CHECK(r515_builds(ed) == builds && r515_bytes(*s.bo) == bytes);
        CHECK(r515te_still(ed, *s.bo, door, 12));
      }
    }
  });

  test("blockout door leaf (515, te): Open Amount of -2, 5, infinity and NaN holds the leaf between closed and 90 degrees with finite numbers, asks for no build and does not loop (NaN is the closed door)", [] {
    const float values[] = {-2.0f, 5.0f, std::numeric_limits<float>::infinity(), std::nanf("")};
    for (float v : values) {
      Editor ed;
      const R499Scene s = r515_door(ed, kDoorDouble);
      CHECK(s.door != nullptr);
      if (!s.door) continue;
      const uint64_t door = s.door->id;
      const uint64_t builds = r515_builds(ed), bytes = r515_bytes(*s.bo);
      s.door->get<BlockoutPiece>()->open_amount = v;
      ed.mark_changed_for_test("test open");
      r515_settle(ed);
      const std::vector<GameObject *> leaves = r515_leaves(*s.door);
      CHECK(leaves.size() == 2);
      for (const GameObject *l : leaves) {
        CHECK(r515te_finite_leaf(*l));
        const float yaw = std::fabs(r515_yaw(*l));
        std::printf("    Open Amount %g: yaw %.2f, builds %llu -> %llu\n", (double)v, (double)yaw, (unsigned long long)builds, (unsigned long long)r515_builds(ed));
        CHECK(yaw >= -0.01f && yaw <= 90.01f);
        CHECK(std::isnan(v) ? yaw < 0.01f : (v < 0.0f ? yaw < 0.01f : std::fabs(yaw - 90.0f) < 0.01f));
      }
      /* Opening numbers of a NaN differ from themselves (NaN != NaN): this is where a rebuild loop would hide. */
      CHECK(r515_bytes(*s.bo) == bytes);
      CHECK(r515te_still(ed, *s.bo, door, 12));
      CHECK(r515_builds(ed) <= builds + 1);  // (at most the one build the hash mark may owe a number it cannot compare)
    }
  });

  test("blockout door leaf (515, te): Open Amount typed as NaN or out of range in the console leaves the door and its leaves usable - a later Open Amount of 0.5 turns the leaf 45 degrees", [] {
    Editor ed;
    const R499Scene s = r515_door(ed, kDoorSingle);
    CHECK(s.door != nullptr);
    if (!s.door) return;
    ed.select_for_test(s.door->id, false);
    for (const char *w : {"nan", "-3", "7", "inf", "abc"}) {
      ed.command(std::string("blockout set open ") + w);
      r515_settle(ed);
      CHECK(r515_leaves(*s.door).size() == 1 && r515te_finite_leaf(*r515_leaves(*s.door)[0]));
    }
    ed.command("blockout set open 0.5");
    r515_settle(ed);
    CHECK(r515_leaves(*s.door).size() == 1);
    if (!r515_leaves(*s.door).empty()) CHECK_NEAR(std::fabs(r515_yaw(*r515_leaves(*s.door)[0])), 45.0f, 0.05f);
    CHECK(r515te_still(ed, *s.bo, s.door->id));
  });
}

static void tests_round515_te_numbers() {
  test("blockout door leaf (515, te): a 5 cm door and a 10 km door keep structurally valid, finite leaf objects (or none), and the Blockout still builds and settles", [] {
    for (float size : {0.05f, 10000.0f}) {
      Editor ed;
      const R499Scene s = r515_door(ed, kDoorDouble);
      CHECK(s.door != nullptr);
      if (!s.door) continue;
      BlockoutPiece *bp = s.door->get<BlockoutPiece>();
      bp->width = size;
      bp->head = std::min(size, 10000.0f);
      ed.mark_changed_for_test("test size");
      r515_settle(ed);
      const std::vector<GameObject *> leaves = r515_leaves(*s.door);
      std::printf("    door %g m: %zu leaf object(s)\n", (double)size, leaves.size());
      for (const GameObject *l : leaves) {
        CHECK(r515te_finite_leaf(*l));
        const Mesh *m = l->get<MeshFilter>()->mesh.get();
        CHECK(m && structurally_valid(*m));
        const Vec3 w = l->world_matrix().point(Vec3(0.0f));
        CHECK(std::isfinite(w.x) && std::isfinite(w.y) && std::isfinite(w.z));
        if (m) {
          Vec3 lo(1e30f), hi(-1e30f);
          for (const Vec3 &p : m->positions)
            for (int a = 0; a < 3; a++) lo[a] = std::min(lo[a], p[a]), hi[a] = std::max(hi[a], p[a]);
          CHECK(hi.x - lo.x > 0.0f && hi.y - lo.y > 0.0f && hi.z - lo.z > 0.0f);  // (no flat leaf)
          CHECK(hi.x - lo.x <= size + 1e-3f);                                       // (no wider than the door)
        }
      }
      const Mesh *bm = r495_mesh(*s.bo);
      CHECK(bm == nullptr || structurally_valid(*bm));
      CHECK(r515te_still(ed, *s.bo, s.door->id));
      ed.select_for_test(s.door->id, false);
      ed.command("blockout set open 1");  // (and it still swings)
      r515_settle(ed);
      for (const GameObject *l : r515_leaves(*s.door)) CHECK(r515te_finite_leaf(*l));
    }
  });

  test("blockout door leaf (515, te): Leaf Thickness 0, a negative one and NaN make a valid slab (thickness held to the 1 cm floor) - never a flat or NaN leaf", [] {
    const float values[] = {0.0f, -0.2f, std::nanf(""), 1e9f};
    for (float v : values) {
      Editor ed;
      const R499Scene s = r515_door(ed, kDoorSingle);
      CHECK(s.door != nullptr);
      if (!s.door) continue;
      s.door->get<BlockoutPiece>()->leaf_thickness = v;
      ed.mark_changed_for_test("test thickness");
      r515_settle(ed);
      const std::vector<GameObject *> leaves = r515_leaves(*s.door);
      CHECK(leaves.size() == 1);
      if (leaves.size() != 1) continue;
      CHECK(r515te_finite_leaf(*leaves[0]));
      const Mesh *m = leaves[0]->get<MeshFilter>()->mesh.get();
      CHECK(m && structurally_valid(*m));
      if (m) {
        Vec3 lo(1e30f), hi(-1e30f);
        for (const Vec3 &p : m->positions)
          for (int a = 0; a < 3; a++) lo[a] = std::min(lo[a], p[a]), hi[a] = std::max(hi[a], p[a]);
        std::printf("    Leaf Thickness %g: slab %.4f m thick\n", (double)v, (double)(hi.z - lo.z));
        CHECK(hi.z - lo.z >= kBlockoutMinSize - 1e-6f && hi.z - lo.z <= s.door->get<BlockoutPiece>()->frame_depth + 1e-3f);
      }
      const ColliderComponent *col = leaves[0]->get<ColliderComponent>();
      CHECK(col && std::isfinite(col->size.z) && col->size.z > 0.0f);
      CHECK(r515te_still(ed, *s.bo, s.door->id));
    }
  });

  test("blockout door leaf (515, te): a door on a turned and non-uniformly scaled Blockout keeps its leaf's object scale at 1 and its hinge on the jamb in world space; Apply Scale then keeps both and makes the world scale 1", [] {
    Editor ed;
    const R499Scene s = r515_door(ed, kDoorDouble);
    CHECK(s.door != nullptr);
    if (!s.door) return;
    s.bo->set_local_euler(Vec3(0.0f, 37.0f, 0.0f));
    s.bo->set_local_scale(Vec3(2.0f, 1.0f, 1.5f));
    ed.mark_changed_for_test("test turn and scale");
    r515_settle(ed);
    auto check_hinges = [&](const char *when) {
      const BlockoutPiece *bp = s.door->get<BlockoutPiece>();
      const float clear = bp->width - 2.0f * bp->frame_width;
      const std::vector<GameObject *> leaves = r515_leaves(*s.door);
      CHECK(leaves.size() == 2);
      for (const GameObject *l : leaves) {
        CHECK(l->local().scale.x == 1.0f && l->local().scale.y == 1.0f && l->local().scale.z == 1.0f);
        const float side = l->local().position.x > 0.0f ? 1.0f : -1.0f;
        const Vec3 jamb = s.door->world_matrix().point(Vec3(side * 0.5f * clear, bp->sill, 0.0f));
        const Vec3 hinge = l->world_matrix().point(Vec3(0.0f));
        std::printf("    %s: hinge off the jamb by %.5f m\n", when, (double)length(hinge - jamb));
        CHECK(length(hinge - jamb) < 1e-3f);
        const Vec3 up = normalize(l->world_matrix().dir(Vec3(0.0f, 1.0f, 0.0f)));
        CHECK(up.y > 0.9999f);  // (upright in the world: the Blockout is turned about Y only)
      }
    };
    check_hinges("turned and scaled");
    ed.select_for_test(s.door->id, false);
    ed.command("blockout set open 0.5");
    r515_settle(ed);
    check_hinges("half open");
    ed.select_for_test(s.bo->id, false);
    ed.command("blockout applyscale");
    r515_settle(ed);
    check_hinges("after Apply Scale");
    const GameObject *l = r515_leaves(*s.door).empty() ? nullptr : r515_leaves(*s.door)[0];
    if (l) {
      CHECK_NEAR(length(l->world_matrix().dir(Vec3(1.0f, 0.0f, 0.0f))), 1.0f, 1e-3f);
      CHECK_NEAR(length(l->world_matrix().dir(Vec3(0.0f, 0.0f, 1.0f))), 1.0f, 1e-3f);
    }
    CHECK_NEAR(s.door->get<BlockoutPiece>()->open_amount, 0.5f, 1e-3f);
    CHECK(r515te_still(ed, *s.bo, s.door->id));
  });
}

static void tests_round515_te_reparent() {
  test("blockout door leaf (515, te): a renamed leaf stays the door's leaf and still turns with Open Amount; a leaf moved under an ordinary object stops being a leaf and the door makes a new one", [] {
    Editor ed;
    const R499Scene s = r515_door(ed, kDoorSingle);
    CHECK(s.door != nullptr);
    if (!s.door) return;
    GameObject *leaf = r515_leaves(*s.door)[0];
    const uint64_t id = leaf->id;
    leaf->name = "Front Door Panel";
    ed.mark_changed_for_test("test rename");
    r515_settle(ed);
    CHECK(r515_leaves(*s.door).size() == 1 && r515_leaves(*s.door)[0]->id == id && r515_leaves(*s.door)[0]->name == "Front Door Panel");
    ed.select_for_test(s.door->id, false);
    ed.command("blockout set open 1");
    r515_settle(ed);
    CHECK_NEAR(std::fabs(r515_yaw(*ed.scene().find(id))), 90.0f, 0.05f);
    /* Under an empty object elsewhere (not the door's child any more). */
    GameObject *box = ed.scene().create("Holder");
    ed.scene().set_parent(ed.scene().find(id), box, -1, true);
    ed.mark_changed_for_test("test reparent");
    r515_settle(ed);
    const GameObject *out = ed.scene().find(id);
    CHECK(out && !out->get<BlockoutLeaf>() && out->parent == box);
    CHECK(r515_leaves(*s.door).size() == 1 && r515_leaves(*s.door)[0]->id != id);
    CHECK(r515te_all_leaves(ed) == 1);
    CHECK(r515te_still(ed, *s.bo, s.door->id));
  });

  test("blockout door leaf (515, te): a leaf dropped under another door is not destroyed or adopted - both doors keep exactly their own leaf, in either order of making", [] {
    for (int towards_first = 0; towards_first < 2; towards_first++) {
      Editor ed;
      const R499Scene s = r515_door(ed, kDoorSingle);
      CHECK(s.door != nullptr);
      if (!s.door) continue;
      GameObject *d2 = r515te_second_door(ed, s, "single", 1.1f);
      CHECK(d2 != nullptr);
      if (!d2) continue;
      CHECK(r515_leaves(*s.door).size() == 1 && r515_leaves(*d2).size() == 1);
      GameObject *from = towards_first ? d2 : s.door, *to = towards_first ? s.door : d2;
      const uint64_t moved = r515_leaves(*from)[0]->id, from_id = from->id, to_id = to->id;
      ed.scene().set_parent(ed.scene().find(moved), to, -1, true);
      ed.mark_changed_for_test("test reparent");
      r515_settle(ed);
      const GameObject *m = ed.scene().find(moved);
      std::printf("    leaf moved from door %llu under door %llu: the object %s, %zu / %zu leaves\n", (unsigned long long)from_id, (unsigned long long)to_id, m ? "survives" : "is GONE",
                  r515te_leaf_count(ed, from_id), r515te_leaf_count(ed, to_id));
      CHECK(m != nullptr);  // (the user's object is not silently destroyed: the ADR says it becomes an ordinary object)
      CHECK(r515te_leaf_count(ed, from_id) == 1 && r515te_leaf_count(ed, to_id) == 1);
      CHECK(m == nullptr || !m->get<BlockoutLeaf>());
      CHECK(r515te_all_leaves(ed) == 2);
      CHECK(r515te_still(ed, *s.bo, s.door->id));
    }
  });
}

static void tests_round515_te_multi() {
  test("blockout door leaf (515, te): duplicating the whole Blockout gives the copy leaves of its own (new objects under the copy's door); turning the copy's door never moves the original's leaf, and the original keeps its leaf", [] {
    Editor ed;
    const R499Scene s = r515_door(ed, kDoorDouble);
    CHECK(s.door != nullptr);
    if (!s.door) return;
    std::set<uint64_t> original;
    for (const GameObject *l : r515_leaves(*s.door)) original.insert(l->id);
    const uint64_t bo_id = s.bo->id, door_id = s.door->id;
    ed.select_for_test(bo_id, false);
    ed.command("duplicate");
    r515_settle(ed);
    GameObject *copy = ed.active_object_for_test();
    CHECK(copy && copy->id != bo_id && copy->get<Blockout>());
    if (!copy) return;
    copy->set_local_position(copy->local().position + Vec3(20.0f, 0.0f, 0.0f));
    ed.mark_changed_for_test("test move copy");
    r515_settle(ed);
    GameObject *cd = r515te_door_in(*copy);
    CHECK(cd && cd->id != door_id);
    if (!cd) return;
    const std::vector<GameObject *> mine = r515_leaves(*cd);
    CHECK(mine.size() == 2 && r515te_leaf_count(ed, door_id) == 2 && r515te_all_leaves(ed) == 4);
    for (const GameObject *l : mine) CHECK(!original.count(l->id) && l->parent == cd);
    for (uint64_t id : original) CHECK(ed.scene().find(id) && ed.scene().find(id)->parent == ed.scene().find(door_id));
    ed.select_for_test(cd->id, false);
    ed.command("blockout set open 1");
    r515_settle(ed);
    CHECK_NEAR(std::fabs(r515_yaw(*mine[0])), 90.0f, 0.05f);
    for (uint64_t id : original) CHECK(std::fabs(r515_yaw(*ed.scene().find(id))) < 0.05f);
    CHECK(ed.scene().find(door_id)->get<BlockoutPiece>()->open_amount == 0.0f);
    /* And the other way: turning the original's leaf by hand moves the original's door only. */
    GameObject *ol = r515te_plus_leaf(*ed.scene().find(door_id));
    CHECK(ol != nullptr);
    if (ol) ol->set_local_euler(Vec3(0.0f, 45.0f, 0.0f));
    ed.mark_changed_for_test("test turn original");
    r515_settle(ed);
    CHECK_NEAR(ed.scene().find(door_id)->get<BlockoutPiece>()->open_amount, 0.5f, 2e-3f);
    CHECK_NEAR(r515te_door_in(*ed.scene().find(copy->id))->get<BlockoutPiece>()->open_amount, 1.0f, 1e-6f);
    CHECK(r515te_still(ed, *ed.scene().find(bo_id), door_id));
  });

  test("blockout door leaf (515, te): deleting the whole Blockout takes every leaf with it (none is left in the scene, no frame makes one again) and one undo brings the Blockout, its door and its leaves back", [] {
    Editor ed;
    const R499Scene s = r515_door(ed, kDoorDouble);
    CHECK(s.door != nullptr);
    if (!s.door) return;
    const uint64_t bo_id = s.bo->id, door_id = s.door->id;
    const size_t objects = ed.scene().object_count();
    ed.select_for_test(bo_id, false);
    ed.command("delete");
    r515_settle(ed);
    CHECK(ed.scene().find(bo_id) == nullptr && r515te_all_leaves(ed) == 0);
    for (int i = 0; i < 6; i++) ed.step_frame_headless();
    CHECK(r515te_all_leaves(ed) == 0 && ed.scene().object_count() < objects);
    ed.command("undo");
    r515_settle(ed);
    CHECK(ed.scene().find(bo_id) != nullptr && r515te_leaf_count(ed, door_id) == 2 && r515te_all_leaves(ed) == 2 && ed.scene().object_count() == objects);
    CHECK(r515te_still(ed, *ed.scene().find(bo_id), door_id));
  });

  test("blockout door leaf (515, te): Bake to Mesh, a hand turn and a deleted leaf while baked, then Unbake leave the door with leaves that match its Leaf flag and its Open Amount, and no stray leaf anywhere", [] {
    for (int what = 0; what < 3; what++) {  // 0: turned while baked, 1: deleted while baked, 2: the door's Open Amount typed while baked
      Editor ed;
      const R499Scene s = r515_door(ed, kDoorDouble);
      CHECK(s.door != nullptr);
      if (!s.door) continue;
      const uint64_t bo_id = s.bo->id, door_id = s.door->id;
      std::vector<uint64_t> ids;
      for (const GameObject *l : r515_leaves(*s.door)) ids.push_back(l->id);
      ed.select_for_test(bo_id, false);
      ed.command("blockout bake");
      r477_settle(ed, 3);
      GameObject *bo = ed.scene().find(bo_id);
      CHECK(bo && bo->get<BlockoutBaked>());
      if (!bo) continue;
      if (what == 0) {
        GameObject *l = ed.scene().find(ids[1]);
        CHECK(l != nullptr);
        if (l) l->set_local_euler(Vec3(0.0f, l->local().position.x > 0.0f ? 40.0f : -40.0f, 0.0f));
        ed.mark_changed_for_test("test turn baked");
        r477_settle(ed, 3);
      }
      else if (what == 1) {
        ed.select_for_test(ids[0], false);
        r477_settle(ed, 2);
        ed.command("delete");
        r477_settle(ed, 3);
      }
      else if (GameObject *d = ed.scene().find(door_id)) {
        d->get<BlockoutPiece>()->open_amount = 0.8f;
        ed.mark_changed_for_test("test open baked");
        r477_settle(ed, 3);
      }
      ed.select_for_test(bo_id, false);
      ed.command("blockout unbake");
      r515_settle(ed);
      const GameObject *door = ed.scene().find(door_id);
      CHECK(door && door->get<BlockoutPiece>());
      if (!door) continue;
      const BlockoutPiece *bp = door->get<BlockoutPiece>();
      const size_t want = bp->leaf ? 2u : 0u;
      std::printf("    case %d: Leaf %d, %zu leaf object(s) under the door, %zu in the scene, Open Amount %.3f\n", what, (int)bp->leaf, r515_leaves(*door).size(), r515te_all_leaves(ed), (double)bp->open_amount);
      CHECK(r515_leaves(*door).size() == want && r515te_all_leaves(ed) == want);
      for (const GameObject *l : r515_leaves(*door)) CHECK_NEAR(std::fabs(r515_yaw(*l)), 90.0f * blockout_door_open(*bp), 0.1f);  // (the leaf shows the number the Inspector shows)
      CHECK(r515te_still(ed, *ed.scene().find(bo_id), door_id));
    }
  });
}

static void tests_round515_te_files() {
  test("blockout door leaf (515, te): a scene with a hand-turned Double door and a leaf tipped by hand saves and opens with the same poses, the same Open Amount and the same two leaves (no second pair), and the open does not ask for a change", [] {
    Editor ed;
    const R499Scene s = r515_door(ed, kDoorDouble);
    CHECK(s.door != nullptr);
    if (!s.door) return;
    GameObject *a = r515te_plus_leaf(*s.door);
    CHECK(a != nullptr);
    if (!a) return;
    a->set_local_euler(Vec3(0.0f, 54.0f, 0.0f));
    ed.mark_changed_for_test("test turn");
    r515_settle(ed);
    const float open = s.door->get<BlockoutPiece>()->open_amount;
    CHECK_NEAR(open, 0.6f, 2e-3f);
    std::vector<std::pair<std::string, Transform>> before;
    for (const GameObject *l : r515_leaves(*s.door)) before.push_back({l->name, l->local()});
    const uint64_t bytes = r515_bytes(*s.bo);
    const std::string path = te_scene_path(ed, "r515_te_turned.scene");
    CHECK(fs::write_file(path, save_scene_text(ed.scene())));
    ed.command("newscene");
    CHECK(ed.open_scene_for_test(path));
    for (int i = 0; i < 400 && !r477_find(ed); i++) ed.step_frame_headless();
    r515_settle(ed);
    GameObject *door = ed.scene().find_by_name("Door");
    CHECK(door && door->get<BlockoutPiece>());
    if (door) {
      CHECK_NEAR(door->get<BlockoutPiece>()->open_amount, open, 1e-5f);
      const std::vector<GameObject *> leaves = r515_leaves(*door);
      CHECK(leaves.size() == 2 && r515te_all_leaves(ed) == 2);
      for (size_t i = 0; i < leaves.size() && i < before.size(); i++) {
        CHECK(leaves[i]->name == before[i].first);
        CHECK(length(leaves[i]->local().position - before[i].second.position) < 1e-4f);
        CHECK_NEAR(std::fabs(r515_yaw(*leaves[i])), 54.0f, 0.05f);
      }
      CHECK(r515te_still(ed, *r477_find(ed), door->id));
    }
    GameObject *bo = r477_find(ed);
    CHECK(bo && r515_bytes(*bo) == bytes);
    fs::remove_file(path);
  });

  test("blockout door leaf (515, te): a leaf tipped over by hand (not a swing) keeps its tip through save and open, and through the walks after it - Open Amount is not typed again, so nothing may reset it", [] {
    Editor ed;
    const R499Scene s = r515_door(ed, kDoorSingle);
    CHECK(s.door != nullptr);
    if (!s.door) return;
    ed.select_for_test(s.door->id, false);
    ed.command("blockout set open 0.5");
    r515_settle(ed);
    GameObject *leaf = r515_leaves(*s.door)[0];
    const Quat tipped = Quat::euler(Vec3(25.0f, leaf->local().rotation.rotate(Vec3(0.0f, 0.0f, 1.0f)).x > 0.0f ? 45.0f : -45.0f, 0.0f));
    leaf->set_local_rotation(tipped);
    ed.mark_changed_for_test("test tip");
    r515_settle(ed);
    const Vec3 f0 = leaf->local().rotation.rotate(Vec3(0.0f, 1.0f, 0.0f));
    CHECK(f0.y < 0.99f);  // (really tipped)
    const std::string path = te_scene_path(ed, "r515_te_tipped.scene");
    CHECK(fs::write_file(path, save_scene_text(ed.scene())));
    ed.command("newscene");
    CHECK(ed.open_scene_for_test(path));
    for (int i = 0; i < 400 && !r477_find(ed); i++) ed.step_frame_headless();
    r515_settle(ed);
    GameObject *door = ed.scene().find_by_name("Door");
    CHECK(door && r515_leaves(*door).size() == 1);
    if (door && r515_leaves(*door).size() == 1) {
      const Vec3 up = r515_leaves(*door)[0]->local().rotation.rotate(Vec3(0.0f, 1.0f, 0.0f));
      std::printf("    the tipped leaf's up after open: %.4f (before %.4f)\n", (double)up.y, (double)f0.y);
      CHECK(std::fabs(up.y - f0.y) < 1e-3f);  // (a user who tipped the leaf finds it as left)
    }
    fs::remove_file(path);
  });

  test("blockout door leaf (515, te): in Play mode a leaf turned by a script stays turned while playing and Stop puts the leaf, its door's Open Amount and the leaf count back as they were", [] {
    Editor ed;
    const R499Scene s = r515_door(ed, kDoorSingle);
    CHECK(s.door != nullptr);
    if (!s.door) return;
    const uint64_t door_id = s.door->id, bo_id = s.bo->id;
    const uint64_t leaf_id = r515_leaves(*s.door)[0]->id;
    const Transform start = r515_leaves(*s.door)[0]->local();
    const float side = start.position.x > 0.0f ? 1.0f : -1.0f;
    ed.command("play");
    CHECK(ed.playing_for_test());
    GameObject *pd = ed.scene().find(door_id);
    CHECK(pd && r515_leaves(*pd).size() == 1);
    if (!pd || r515_leaves(*pd).empty()) {
      ed.command("stop");
      return;
    }
    GameObject *pl = r515_leaves(*pd)[0];
    pl->set_local_euler(Vec3(0.0f, side * 72.0f, 0.0f));  // (what a script that swings the door does)
    for (int i = 0; i < 8; i++) ed.step_frame_headless();
    pd = ed.scene().find(door_id);
    CHECK(pd && r515_leaves(*pd).size() == 1);
    if (pd && r515_leaves(*pd).size() == 1) CHECK_NEAR(std::fabs(r515_yaw(*r515_leaves(*pd)[0])), 72.0f, 0.1f);  // (the editor did not undo the script's turn)
    ed.command("stop");
    r515_settle(ed);
    CHECK(!ed.playing_for_test());
    const GameObject *door = ed.scene().find(door_id);
    CHECK(door && door->get<BlockoutPiece>() && door->get<BlockoutPiece>()->open_amount == 0.0f);
    CHECK(door && r515_leaves(*door).size() == 1 && r515te_all_leaves(ed) == 1);
    if (door && r515_leaves(*door).size() == 1) {
      const GameObject *l = r515_leaves(*door)[0];
      CHECK(l->id == leaf_id && std::fabs(r515_yaw(*l)) < 0.05f && length(l->local().position - start.position) < 1e-5f);
    }
    CHECK(ed.scene().find(bo_id) && r515te_still(ed, *ed.scene().find(bo_id), door_id));
  });
}

static void tests_round515_te_events() {
  test("blockout door leaf (515, te): with two Double doors side by side, turning one leaf of the second by the real Rotate keys sets only the second door's Open Amount and moves only its two leaves; one undo returns them", [] {
    Editor ed;
    const R499Scene s = r515_door(ed, kDoorDouble);
    CHECK(s.door != nullptr);
    if (!s.door) return;
    GameObject *d2 = r515te_second_door(ed, s, "double", 1.3f);
    CHECK(d2 != nullptr);
    if (!d2) return;
    CHECK(r515_leaves(*s.door).size() == 2 && r515_leaves(*d2).size() == 2);
    const uint64_t d1_id = s.door->id, d2_id = d2->id;
    GameObject *leaf = r515te_plus_leaf(*d2);
    CHECK(leaf != nullptr);
    if (!leaf) return;
    const uint64_t builds = r515_builds(ed), bytes = r515_bytes(*s.bo);
    ed.select_for_test(leaf->id, false);
    ed.step_frame_headless();
    ed.mark_changed_for_test("test scene");
    r477_settle(ed, 3);
    const size_t steps = ed.undo_depth_for_test();
    r515_key(ed, platform::KEY_G);
    CHECK(ed.transform_active_for_test());
    r515_key(ed, platform::KEY_R);
    r515_key(ed, platform::KEY_Y);
    r515_type(ed, "45");
    r515_key(ed, platform::KEY_ENTER);
    r477_settle(ed, 4);
    const GameObject *a = ed.scene().find(d1_id), *b = ed.scene().find(d2_id);
    std::printf("    door 1 Open Amount %.4f, door 2 Open Amount %.4f, %zu undo step(s)\n", (double)a->get<BlockoutPiece>()->open_amount, (double)b->get<BlockoutPiece>()->open_amount, ed.undo_depth_for_test() - steps);
    CHECK(!ed.transform_active_for_test());
    CHECK_NEAR(b->get<BlockoutPiece>()->open_amount, 0.5f, 2e-3f);
    CHECK(a->get<BlockoutPiece>()->open_amount == 0.0f);
    for (const GameObject *l : r515_leaves(*b)) CHECK_NEAR(std::fabs(r515_yaw(*l)), 45.0f, 0.1f);
    for (const GameObject *l : r515_leaves(*a)) CHECK(std::fabs(r515_yaw(*l)) < 0.05f);
    CHECK(ed.undo_depth_for_test() == steps + 1);
    CHECK(r515_builds(ed) == builds && r515_bytes(*s.bo) == bytes);
    CHECK(r515te_still(ed, *s.bo, d2_id));
    ed.command("undo");
    r515_settle(ed);
    a = ed.scene().find(d1_id), b = ed.scene().find(d2_id);
    CHECK(a && b && a->get<BlockoutPiece>()->open_amount == 0.0f && b->get<BlockoutPiece>()->open_amount == 0.0f);
    if (b)
      for (const GameObject *l : r515_leaves(*b)) CHECK(std::fabs(r515_yaw(*l)) < 0.05f);
  });

  test("blockout door leaf (515, te): cancelling a Rotate of a leaf with Escape leaves the leaf, its door and the undo stack as they were", [] {
    Editor ed;
    const R499Scene s = r515_door(ed, kDoorSingle);
    CHECK(s.door != nullptr);
    if (!s.door) return;
    GameObject *leaf = r515_leaves(*s.door)[0];
    ed.select_for_test(leaf->id, false);
    ed.step_frame_headless();
    ed.mark_changed_for_test("test scene");
    r477_settle(ed, 3);
    const size_t steps = ed.undo_depth_for_test();
    const Transform start = leaf->local();
    r515_key(ed, platform::KEY_G);
    r515_key(ed, platform::KEY_R);
    r515_key(ed, platform::KEY_Y);
    r515_type(ed, "60");
    for (int i = 0; i < 4; i++) ed.step_frame_headless();  // (mid-drag: the sync must not write the half-turned number)
    r515_key(ed, platform::KEY_ESCAPE);
    r477_settle(ed, 4);
    CHECK(!ed.transform_active_for_test());
    CHECK(ed.undo_depth_for_test() == steps);
    CHECK(s.door->get<BlockoutPiece>()->open_amount == 0.0f);
    const GameObject *back = ed.scene().find(leaf->id);
    CHECK(back && length(back->local().position - start.position) < 1e-5f && std::fabs(r515_yaw(*back)) < 0.05f);
    CHECK(r515te_still(ed, *s.bo, s.door->id));
  });
}

void tests_round515_te() {
  tests_round515_te_undo();
  tests_round515_te_loops();
  tests_round515_te_numbers();
  tests_round515_te_reparent();
  tests_round515_te_multi();
  tests_round515_te_files();
  tests_round515_te_events();
}
// r515te-end
