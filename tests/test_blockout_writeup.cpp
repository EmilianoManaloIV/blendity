// SPDX-License-Identifier: GPL-2.0-or-later
// Pass 23b's write-up (round 503, task 0487): the bench interior (`benchscene blockout [400]`) with the pass's pieces - styled windows and doors, the architectural
// pieces, a piece with a modifier stack - and the lesson "Building rooms with Blockout" with its new steps. Split from test_blockout.cpp, whose object file reached the
// section limit (MSVC C1128); tests_round503 is called from run_blockout_tests so the tests stay in the blockout group.
#include "test_common.h"

#include "../src/scene/blockout.h"
#include "../src/scene/blockout_object.h"
#include "../src/scene/raycast.h"

#include <cstring>
#include <limits>
#include <set>
#include <sstream>

using namespace bl;

namespace {

/* Small helpers copied from test_blockout.cpp (its own are internal to that file). */
const Mesh *r503_mesh(const GameObject *g) {
  const MeshFilter *mf = g ? g->get<MeshFilter>() : nullptr;
  return mf && mf->mesh ? mf->mesh.get() : nullptr;
}

bool r503_same(const Mesh &a, const Mesh &b) {
  return a.positions.size() == b.positions.size() && a.face_offsets == b.face_offsets && a.corner_verts == b.corner_verts &&
         (a.positions.empty() || std::memcmp(a.positions.data(), b.positions.data(), a.positions.size() * sizeof(Vec3)) == 0);
}

void r503_settle(Editor &ed, int frames = 2) {
  for (int i = 0; i < frames; i++) ed.step_frame_headless();
}

void r503_built(Editor &ed) {
  r503_settle(ed, 3);
  ed.command("blockout wait");
  r503_settle(ed, 2);
}

void r503_empty_scene(Editor &ed) {
  std::vector<GameObject *> roots;
  ed.scene().for_each([&](GameObject &g) {
    if (!g.parent) roots.push_back(&g);
  });
  ed.scene().destroy(roots);
  ed.clear_selection_for_test();
}

size_t r503_mark() {
  std::vector<LogEntry> tmp;
  return Log::fetch((size_t)1 << 40, tmp);
}
size_t r503_warnings_since(size_t mark, std::string *text = nullptr) {
  std::vector<LogEntry> v;
  Log::fetch(mark, v);
  size_t warnings = 0;
  for (const LogEntry &e : v) {
    if (text) *text += e.text + "\n";
    warnings += e.level != LogLevel::Info;
  }
  return warnings;
}

/* A result a user could keep: valid, closed, no two faces on top of each other (the checks of round 476's, without its flat-face detail). */
bool r503_sound(const Mesh &m, const char *what) {
  std::string why;
  const bool valid = structurally_valid(m, &why), closed = closed_manifold(m);
  const size_t over = overlapping_face_pairs(m), fight = meshops::zfight_pairs(m).size();
  const bool out = m.face_count() == 0 || signed_volume(m) > 0.0f;
  if (!valid || !closed || over || fight || !out)
    std::printf("    %s: valid %d (%s) closed %d overlapping %zu zfight %zu outward %d\n", what, (int)valid, why.c_str(), (int)closed, over, fight, (int)out);
  return valid && closed && !over && !fight && out;
}

/* Every BlockoutPiece under the Blockout, at any depth. */
std::vector<const BlockoutPiece *> r503_pieces(const GameObject &bo) {
  std::vector<const BlockoutPiece *> out;
  std::vector<const GameObject *> stack{&bo};
  while (!stack.empty()) {
    const GameObject *g = stack.back();
    stack.pop_back();
    for (const GameObject *c : g->children) {
      if (const BlockoutPiece *p = c->get<BlockoutPiece>()) out.push_back(p);
      stack.push_back(c);
    }
  }
  return out;
}

/* What the interior has of the pass's kinds. */
struct R503Kinds {
  std::set<int> window_styles, door_styles, presets;
  size_t pieces = 0, modified = 0, windows = 0, doors = 0;
};
R503Kinds r503_kinds(Editor &ed, const GameObject &bo) {
  R503Kinds k;
  for (const BlockoutPiece *p : r503_pieces(bo)) {
    k.pieces++;
    if (p->opening == 2) k.windows++, k.window_styles.insert(p->style);
    if (p->opening == 1) k.doors++, k.door_styles.insert(p->style);
    if (p->is_preset()) k.presets.insert(p->preset);
  }
  ed.scene().for_each([&](GameObject &g) {
    if (g.get<BlockoutPiece>() && g.get<BevelModifier>()) k.modified++;
  });
  return k;
}

uint64_t r503_snapshot_hash(GameObject &bo) {
  std::vector<blockout::Piece> pieces;
  blockout::Settings st;
  blockout_snapshot(bo, pieces, st);
  return blockout_snapshot_hash(pieces, st);
}

/* The lesson's Try-it commands, in order. */
std::string r503_lesson_body(const char *title) {
  for (int i = 0; i < lesson_count(); i++)
    if (std::string(lesson(i).title) == title) return lesson(i).body;
  return "";
}
std::vector<std::string> r503_lesson_cmds(const char *title = "Building rooms with Blockout") {
  std::vector<std::string> cmds;
  std::istringstream is(r503_lesson_body(title));
  for (std::string line; std::getline(is, line);) {
    if (line.rfind("! cmd:", 0) != 0) continue;
    const std::string c = line.substr(6, line.find(" | ") - 6);
    for (size_t s = 0; s <= c.size();) {
      const size_t e = c.find(';', s);
      std::string one = c.substr(s, e == std::string::npos ? std::string::npos : e - s);
      while (!one.empty() && one.front() == ' ') one.erase(one.begin());
      if (!one.empty()) cmds.push_back(one);
      if (e == std::string::npos) break;
      s = e + 1;
    }
  }
  return cmds;
}

/* Real events for the F9 tests (copied from test_blockout_leaf.cpp / test_blockout.cpp). */
platform::Event r503_ev(platform::EventType t, int x, int y, int mods = 0) {
  platform::Event e;
  e.type = t, e.x = x, e.y = y, e.mods = mods;
  return e;
}
platform::Event r503_key(int key, int mods = 0) {
  platform::Event e;
  e.type = platform::EventType::KeyDown, e.key = key, e.mods = mods;
  return e;
}
bool r503_has(Editor &ed, const char *key) {
  Recti r;
  return ed.button_rect_for_test(key, r);
}
/* A number typed into one of the Adjust Last panel's fields. */
bool r503_type(Editor &ed, const std::string &key, const std::string &text) {
  Recti r;
  for (int i = 0; i < 4 && !ed.button_rect_for_test(key, r); i++) ed.step_frame_headless();
  if (!ed.button_rect_for_test(key, r)) return false;
  const int x = r.x + r.w / 2, y = r.y + r.h / 2;
  for (platform::EventType t : {platform::EventType::MouseMove, platform::EventType::MouseDown, platform::EventType::MouseUp, platform::EventType::MouseDown, platform::EventType::MouseUp})
    ed.step_frame_headless({r503_ev(t, x, y)});
  ed.step_frame_headless({r503_key(platform::KEY_A, platform::MOD_CTRL)});
  for (char c : text) {
    platform::Event e;
    e.type = platform::EventType::Text, e.codepoint = (uint32_t)c;
    ed.step_frame_headless({e});
  }
  ed.step_frame_headless({r503_key(platform::KEY_ENTER)});
  r503_settle(ed, 2);
  return true;
}
/* A scene with a Blockout and a 4 m room selected, buttons recorded, F9 pressed twice (it is open when a piece is made: closed, then open again, as a user would). */
GameObject *r503_room_scene(Editor &ed) {
  ed.init_headless(1600, 1000);
  r503_empty_scene(ed);
  ed.record_buttons_for_test(true);
  GameObject *bo = ed.scene().create("Blockout");
  bo->add<Blockout>();
  bo->add<MeshFilter>()->mesh = std::make_shared<Mesh>();
  bo->add<MeshRenderer>();
  ed.select_for_test(bo->id, false);
  ed.command("blockout add room box 4 3 4");
  r503_settle(ed, 3);
  return bo;
}
void r503_f9(Editor &ed) {
  ed.step_frame_headless({r503_key(platform::KEY_F9)});
  r503_settle(ed, 2);
}
/* A recorded control clicked by its key (false: not showing). */
bool r503_click_key(Editor &ed, const std::string &key) {
  ed.record_buttons_for_test(true);
  r503_settle(ed, 2);
  Recti r;
  if (!ed.button_rect_for_test(key, r)) return false;
  for (platform::EventType t : {platform::EventType::MouseMove, platform::EventType::MouseDown, platform::EventType::MouseUp})
    ed.step_frame_headless({r503_ev(t, r.x + r.w / 2, r.y + r.h / 2)});
  r503_settle(ed, 2);
  return true;
}
/* A dropdown opened and one of its items chosen, by clicking. */
bool r503_pick_combo(Editor &ed, const char *shown, const char *item) {
  ed.record_buttons_for_test(true);
  r503_settle(ed, 2);
  Recti c;
  for (const ui::Context::ControlRec &k : ed.ui_for_test().controls)  // (an Inspector dropdown is found by the option it shows now)
    if (std::string(k.kind) == "combo" && k.label == shown && k.r.w > 0) {
      c = k.r;
      break;
    }
  if (c.w <= 0) return false;
  for (platform::EventType t : {platform::EventType::MouseMove, platform::EventType::MouseDown, platform::EventType::MouseUp})
    ed.step_frame_headless({r503_ev(t, c.x + c.w / 2, c.y + c.h / 2)});
  r503_settle(ed, 2);
  return r503_click_key(ed, std::string("menu:") + item);
}

/* ---- overlapping_faces: the sweep against the all-pairs loop it replaced (round 503) ---- */

/* The all-pairs version as it was before the sweep (meshops::overlapping_faces, copied): the reference the sweep must equal pair for pair. */
size_t r503_overlapping_reference(const Mesh &m, float plane_dist, std::vector<std::pair<uint32_t, uint32_t>> *out) {
  const RenderMesh &rm = m.render_mesh(true);
  const size_t T = rm.tri_count();
  const float scale = std::max(1e-3f, length(m.bounds().extent()));
  const float eps_d = plane_dist > 0 ? plane_dist : 1e-4f * scale, eps_a = 1e-6f * scale * scale;
  std::vector<Vec3> n(T);
  std::vector<AABB> box(T);
  for (size_t t = 0; t < T; t++) {
    const Vec3 a = rm.positions[rm.indices[t * 3]], b = rm.positions[rm.indices[t * 3 + 1]], c = rm.positions[rm.indices[t * 3 + 2]];
    const Vec3 cr = cross(b - a, c - a);
    n[t] = length(cr) > 1e-20f ? normalize(cr) : Vec3(0.0f);
    box[t].add(a);
    box[t].add(b);
    box[t].add(c);
  }
  auto clip_area = [](std::vector<Vec2> poly, const Vec2 *tri) {
    for (int e = 0; e < 3 && !poly.empty(); e++) {
      const Vec2 a = tri[e], b = tri[(e + 1) % 3];
      auto side = [&](Vec2 p) { return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x); };
      std::vector<Vec2> o;
      for (size_t i = 0; i < poly.size(); i++) {
        const Vec2 p = poly[i], q = poly[(i + 1) % poly.size()];
        const float sp = side(p), sq = side(q);
        if (sp >= 0) o.push_back(p);
        if ((sp >= 0) != (sq >= 0)) o.push_back(p + (q - p) * (sp / (sp - sq)));
      }
      poly = o;
    }
    float a2 = 0;
    for (size_t i = 0; i < poly.size(); i++) a2 += poly[i].x * poly[(i + 1) % poly.size()].y - poly[(i + 1) % poly.size()].x * poly[i].y;
    return 0.5f * std::fabs(a2);
  };
  std::set<std::pair<uint32_t, uint32_t>> pairs;
  for (size_t i = 0; i < T; i++)
    for (size_t j = i + 1; j < T; j++) {
      const uint32_t fi = rm.tri_face[i], fj = rm.tri_face[j];
      if (fi == fj || std::fabs(dot(n[i], n[j])) < 0.999f || length(n[i]) < 0.5f) continue;
      if (box[i].max.x < box[j].min.x - eps_d || box[j].max.x < box[i].min.x - eps_d || box[i].max.y < box[j].min.y - eps_d ||
          box[j].max.y < box[i].min.y - eps_d || box[i].max.z < box[j].min.z - eps_d || box[j].max.z < box[i].min.z - eps_d)
        continue;
      const Vec3 a0 = rm.positions[rm.indices[i * 3]];
      const float within = dot(n[i], n[j]) > 0 ? eps_d : 1e-5f * scale;
      bool coplanar = true;
      for (int k = 0; k < 3; k++) coplanar = coplanar && std::fabs(dot(rm.positions[rm.indices[j * 3 + k]] - a0, n[i])) < within;
      if (!coplanar) continue;
      const Vec3 u = normalize(std::fabs(n[i].y) < 0.9f ? cross(Vec3(0, 1, 0), n[i]) : cross(Vec3(1, 0, 0), n[i])), v = cross(n[i], u);
      auto flat = [&](size_t t, int k) {
        const Vec3 p = rm.positions[rm.indices[t * 3 + k]];
        return Vec2(dot(p, u), dot(p, v));
      };
      Vec2 ti[3] = {flat(i, 0), flat(i, 1), flat(i, 2)};
      std::vector<Vec2> pj = {flat(j, 0), flat(j, 1), flat(j, 2)};
      if ((ti[1].x - ti[0].x) * (ti[2].y - ti[0].y) - (ti[1].y - ti[0].y) * (ti[2].x - ti[0].x) < 0) std::swap(ti[1], ti[2]);
      if (clip_area(pj, ti) > eps_a) pairs.insert({std::min(fi, fj), std::max(fi, fj)});
    }
  if (out) out->assign(pairs.begin(), pairs.end());
  return pairs.size();
}

/* The sweep and the reference on one mesh: the same pair lists, both sorted (std::set order), so compared as they are. Returns how many pairs. */
size_t r503_same_pairs(const Mesh &m, const char *what, float plane_dist = 0.0f) {
  std::vector<std::pair<uint32_t, uint32_t>> a, b;
  const size_t na = meshops::overlapping_faces(m, plane_dist, &a), nb = r503_overlapping_reference(m, plane_dist, &b);
  const bool same = na == nb && a == b;
  if (!same) std::printf("    %s: sweep %zu pairs, all-pairs %zu\n", what, na, nb);
  CHECK(same);
  return nb;
}

/* A box of six quads (outward windings). */
void r503_box(Mesh &m, Vec3 lo, Vec3 hi) {
  const uint32_t v[8] = {m.add_vert({lo.x, lo.y, lo.z}), m.add_vert({hi.x, lo.y, lo.z}), m.add_vert({hi.x, hi.y, lo.z}), m.add_vert({lo.x, hi.y, lo.z}),
                         m.add_vert({lo.x, lo.y, hi.z}), m.add_vert({hi.x, lo.y, hi.z}), m.add_vert({hi.x, hi.y, hi.z}), m.add_vert({lo.x, hi.y, hi.z})};
  m.add_face({v[0], v[3], v[2], v[1]}), m.add_face({v[4], v[5], v[6], v[7]}), m.add_face({v[0], v[1], v[5], v[4]});
  m.add_face({v[2], v[3], v[7], v[6]}), m.add_face({v[1], v[2], v[6], v[5]}), m.add_face({v[0], v[4], v[7], v[3]});
}
void r503_quad(Mesh &m, Vec3 a, Vec3 b, Vec3 c, Vec3 d) {
  const uint32_t i = m.add_vert(a), j = m.add_vert(b), k = m.add_vert(c), l = m.add_vert(d);
  m.add_face({i, j, k, l});
}

}  // namespace

void tests_round503() {
  test("benchscene blockout: the interior has windows of every style, doors of three styles, one of each architectural piece and a piece with a Bevel - and still exactly 100 pieces", [] {
    Editor ed;
    ed.init_headless(900, 600);
    ed.command("benchscene blockout");
    GameObject *bo = ed.scene().find_by_name("Interior");
    CHECK(bo && bo->children.size() == 100);
    if (!bo) return;
    const R503Kinds k = r503_kinds(ed, *bo);
    std::printf("    %zu pieces: %zu windows in %zu styles, %zu doors in %zu styles, %zu presets, %zu with a Bevel\n", k.pieces, k.windows, k.window_styles.size(), k.doors,
                k.door_styles.size(), k.presets.size(), k.modified);
    CHECK(k.pieces == 100);
    for (int s = kWindowSingle; s < kBlockoutWindowStyles; s++) CHECK(k.window_styles.count(s) == 1);
    for (int s : {(int)kDoorSingle, (int)kDoorDouble, (int)kDoorSliding}) CHECK(k.door_styles.count(s) == 1);
    for (int p = kPresetColumn; p < kBlockoutPresets; p++) CHECK(k.presets.count(p) == 1);
    CHECK(k.modified == 1);
    /* The styled doors' leaves are objects of their own: they are no piece and no child of the Blockout, so the count above is still pieces. */
    r503_built(ed);
    size_t leaves = 0, leaf_pieces = 0;
    ed.scene().for_each([&](GameObject &g) {
      leaves += (g.get<BlockoutLeaf>() && g.get<BlockoutLeaf>()->kind == kPartKindLeaf);
      leaf_pieces += g.get<BlockoutLeaf>() && g.get<BlockoutPiece>();
    });
    std::printf("    %zu leaf objects\n", leaves);
    CHECK(leaves >= 6 && leaf_pieces == 0);
    bo = ed.scene().find_by_name("Interior");
    CHECK(bo && bo->children.size() == 100 && r503_pieces(*bo).size() == 100);
  });

  test("benchscene blockout: the same mesh on two runs, sound on the automatic path, the exact one and the field one", [] {
    Mesh first;
    for (int run = 0; run < 2; run++) {
      Editor ed;
      ed.init_headless(900, 600);
      ed.command("benchscene blockout");
      GameObject *bo = ed.scene().find_by_name("Interior");
      CHECK(bo && bo->get<Blockout>());
      if (!bo) return;
      ed.command("blockout wait");
      const Mesh *m = r503_mesh(bo);
      CHECK(m && m->face_count() > 1000);
      if (!m) return;
      CHECK(r503_sound(*m, "interior (auto)"));
      CHECK(bo->get<Blockout>()->state.status.refusal.empty());
      if (run == 0) first = *m;
      else CHECK(r503_same(first, *m));
      if (run == 1) continue;
      for (int method : {1, 2}) {  // Exact, Field (the exact path needs Blender's libraries: a build without them refuses it, as the other tests know)
        if (method == 1 && !blockout::crisp_available()) continue;
        bo->get<Blockout>()->method = method;
        ed.mark_changed_for_test("r503 path");
        r503_built(ed);
        bo = ed.scene().find_by_name("Interior");
        const Mesh *pm = r503_mesh(bo);
        CHECK(pm && pm->face_count() > 1000);
        if (pm) CHECK(r503_sound(*pm, method == 1 ? "interior (exact)" : "interior (field)"));
        if (bo && !bo->get<Blockout>()->state.status.refusal.empty())
          std::printf("    %s refused: %s\n", method == 1 ? "exact" : "field", bo->get<Blockout>()->state.status.refusal.c_str());
        CHECK(bo && bo->get<Blockout>()->state.status.refusal.empty());
        if (!bo) return;
      }
    }
  });

  test("benchscene blockout 400: 400 pieces with the same kinds, the same snapshot on two runs", [] {
    uint64_t hash[2] = {0, 0};
    for (int run = 0; run < 2; run++) {
      Editor ed;
      ed.init_headless(900, 600);
      ed.command("benchscene blockout 400");
      GameObject *bo = ed.scene().find_by_name("Interior");
      CHECK(bo && bo->children.size() == 400);
      if (!bo) return;
      const R503Kinds k = r503_kinds(ed, *bo);
      CHECK(k.pieces == 400 && k.window_styles.size() == 7 && k.door_styles.size() >= 4 && k.presets.size() == 9 && k.modified == 1);
      hash[run] = r503_snapshot_hash(*bo);
    }
    CHECK(hash[0] == hash[1] && hash[0] != 0);
  });

  test("learn: \"Building rooms with Blockout\" - a styled window, a styled door with its leaf swung and a column, and the pass's sections named", [] {
    const std::string body = r503_lesson_body("Building rooms with Blockout");
    CHECK(!body.empty());
    for (const char *section : {"Windows with a style", "Doors with a style", "Architectural pieces", "Modifiers on a piece"}) {
      CHECK(body.find(section) != std::string::npos);
      CHECK(!r503_lesson_body(section).empty());  // (the names are real lessons')
    }
    Editor ed;
    ed.init_headless(900, 600);
    r503_empty_scene(ed);
    GameObject *door = nullptr, *window = nullptr, *column = nullptr;
    for (const std::string &c : r503_lesson_cmds()) {
      ed.command(c);
      r503_built(ed);
      if (c.rfind("blockout add door", 0) == 0) door = ed.active_object_for_test();
      if (c.rfind("blockout add window", 0) == 0) window = ed.active_object_for_test();
      if (c.rfind("blockout add column", 0) == 0) column = ed.active_object_for_test();
    }
    const BlockoutPiece *d = door ? door->get<BlockoutPiece>() : nullptr, *w = window ? window->get<BlockoutPiece>() : nullptr, *c = column ? column->get<BlockoutPiece>() : nullptr;
    CHECK(d && d->style == kDoorSingle && std::fabs(d->open_amount - 0.5f) < 1e-4f);
    CHECK(w && w->style == kWindowDouble);
    CHECK(c && c->preset == kPresetColumn);
    size_t leaves = 0;
    ed.scene().for_each([&](GameObject &g) { leaves += (g.get<BlockoutLeaf>() && g.get<BlockoutLeaf>()->kind == kPartKindLeaf); });
    CHECK(leaves == 1);
  });

  test("learn: \"Building rooms with Blockout\" - its Try-its run twice in a row, from an empty scene and from the default scene, with no warning and valid meshes", [] {
    const std::vector<std::string> cmds = r503_lesson_cmds();
    CHECK(cmds.size() >= 12);
    for (int from_default = 0; from_default < 2; from_default++) {
      Editor ed;
      ed.init_headless(900, 600);
      if (!from_default) r503_empty_scene(ed);
      for (int round = 0; round < 2; round++) {
        const size_t mark = r503_mark();
        for (const std::string &c : cmds) {
          ed.command(c);
          r503_built(ed);
        }
        std::string log;
        const size_t warnings = r503_warnings_since(mark, &log);
        if (warnings) std::printf("    [%s, run %d]\n%s", from_default ? "default scene" : "empty scene", round + 1, log.c_str());
        CHECK(warnings == 0);
        ed.scene().for_each([&](GameObject &g) {
          if (g.get<MeshFilter>() && (g.get<Blockout>() || g.get<BlockoutBaked>()) && r503_mesh(&g)) CHECK(structurally_valid(*r503_mesh(&g)));
        });
      }
    }
  });

  test("learn: \"Doors with a style\" - its Try-its run twice in a row end with the drawn door its last step describes, on the room's east wall, facing out, 1 m by 2.1 m", [] {
    const std::string body = r503_lesson_body("Doors with a style");
    CHECK(body.find("leaf object") != std::string::npos && body.find("Draw Piece") != std::string::npos);
    const std::vector<std::string> cmds = r503_lesson_cmds("Doors with a style");
    CHECK(cmds.size() >= 14);
    Editor ed;
    ed.init_headless(900, 600);
    for (int round = 0; round < 2; round++) {
      /* The lesson's first step puts its room into a Blockout that is already in the scene (the lesson's introduction says it starts best from File > New Scene): the
       * second run begins from an empty scene as a user who starts again would, or its drawn door would land on the first run's duplicate room (x 2.59, not the wall). */
      r503_empty_scene(ed);
      const size_t mark = r503_mark();
      for (const std::string &c : cmds) {
        ed.command(c);
        r503_built(ed);
      }
      std::string log;
      const size_t warnings = r503_warnings_since(mark, &log);
      if (warnings) std::printf("    [run %d]\n%s", round + 1, log.c_str());
      CHECK(warnings == 0);
      /* The last step's door: the selected piece, a door 1 m wide and 2.1 m high from the floor line, in the east wall of the 4 m room, which spans 0..4 (the wall face is at x 4.2, the door
       * is centred on the wall), facing +X - out of the room. */
      GameObject *g = ed.active_object_for_test();
      const BlockoutPiece *d = g ? g->get<BlockoutPiece>() : nullptr;
      CHECK(d && d->opening == 1);
      if (!d) continue;
      CHECK(std::fabs(d->width - 1.0f) < 1e-3f && std::fabs(d->head - 2.1f) < 1e-3f && std::fabs(d->sill) < 1e-3f);
      const Vec3 at = g->world_position();
      std::printf("    the drawn door is at %.3f %.3f %.3f\n", at.x, at.y, at.z);
      CHECK(std::fabs(at.x - 4.1f) < 0.15f && std::fabs(at.y) < 1e-2f && at.z > 2.0f && at.z < 3.5f);
      CHECK(dot(g->world_matrix().dir({0.0f, 0.0f, 1.0f}), Vec3(1.0f, 0.0f, 0.0f)) > 0.99f);
      const Mesh *m = r503_mesh(g->parent);
      CHECK(m && structurally_valid(*m));
    }
  });

  test("Adjust Last (F9): after `blockout set style arched` on the window F9 is about it shows the new Style and its rows, and a later F9 edit keeps the console's style", [] {
    Editor ed;
    r503_room_scene(ed);
    ed.command("blockout add window grid 1.4 2.1 0.8");
    r503_settle(ed, 3);
    GameObject *win = ed.active_object_for_test();
    BlockoutPiece *bp = win ? win->get<BlockoutPiece>() : nullptr;
    CHECK(bp && ed.obj_last_valid() && ed.obj_last_for_test().kind == "blockout" && ed.obj_last_for_test().style == kWindowGrid);
    if (!bp) return;
    r503_f9(ed);  // closed
    r503_f9(ed);  // open again, a real key each time
    CHECK(r503_has(ed, "adjust:Panes Across") && !r503_has(ed, "adjust:Arch Rise"));
    ed.command("blockout set style arched");
    r503_settle(ed, 3);
    CHECK(bp->style == kWindowArched);
    CHECK(ed.obj_last_valid() && ed.obj_last_for_test().style == kWindowArched);  // (the record the panel's rows are bound to)
    CHECK(r503_has(ed, "adjust:Arch Rise") && !r503_has(ed, "adjust:Panes Across"));
    CHECK(r503_type(ed, "adjust:Width", "1.1"));
    CHECK(bp->style == kWindowArched && std::fabs(bp->width - 1.1f) < 1e-4f);  // (not written back to Grid)
  });

  test("Adjust Last (F9): after `blockout set open 0.5` on the door F9 is about it shows Open Amount 0.5, and a later F9 edit keeps it and the style", [] {
    Editor ed;
    r503_room_scene(ed);
    ed.command("blockout add door single 1 2.1");
    r503_settle(ed, 3);
    GameObject *door = ed.active_object_for_test();
    BlockoutPiece *bp = door ? door->get<BlockoutPiece>() : nullptr;
    CHECK(bp && ed.obj_last_valid() && ed.obj_last_for_test().opening == 1 && ed.obj_last_for_test().open_amount == 0.0f);
    if (!bp) return;
    r503_f9(ed);
    r503_f9(ed);
    ed.command("blockout set open 0.5");
    r503_settle(ed, 3);
    CHECK(std::fabs(bp->open_amount - 0.5f) < 1e-4f);
    CHECK(ed.obj_last_valid() && std::fabs(ed.obj_last_for_test().open_amount - 0.5f) < 1e-4f);
    CHECK(r503_type(ed, "adjust:Width", "1.2"));
    CHECK(std::fabs(bp->open_amount - 0.5f) < 1e-4f && bp->style == kDoorSingle && std::fabs(bp->width - 1.2f) < 1e-4f);
  });

  test("Adjust Last (F9): a number typed into the Inspector's Open Amount of the door F9 is about shows in F9 at once, and a later F9 edit keeps it", [] {
    Editor ed;
    r503_room_scene(ed);
    ed.command("blockout add door single 1 2.1");
    r503_settle(ed, 3);
    GameObject *door = ed.active_object_for_test();
    BlockoutPiece *bp = door ? door->get<BlockoutPiece>() : nullptr;
    CHECK(bp && ed.obj_last_valid() && ed.obj_last_for_test().opening == 1);
    if (!bp) return;
    ed.command("window Inspector");
    r503_settle(ed, 3);
    CHECK(r503_type(ed, "field:Open Amount", "0.75"));  // (a real click and typing in the Inspector's row)
    CHECK(std::fabs(bp->open_amount - 0.75f) < 1e-4f);
    CHECK(ed.obj_last_valid() && std::fabs(ed.obj_last_for_test().open_amount - 0.75f) < 1e-4f);
    CHECK(r503_type(ed, "adjust:Width", "1.2"));
    CHECK(std::fabs(bp->open_amount - 0.75f) < 1e-4f && std::fabs(bp->width - 1.2f) < 1e-4f);
  });

  test("overlapping faces: the sweep gives exactly the pairs the all-pairs loop gave (as sorted lists) on random box soups with coplanar touching faces", [] {
    uint32_t seed = 12345u;
    auto next = [&]() {
      seed = seed * 1664525u + 1013904223u;
      return (seed >> 8) & 0xFFFFu;
    };
    size_t total = 0;
    for (int soup = 0; soup < 6; soup++) {
      Mesh m;
      for (int b = 0; b < 30; b++) {
        const Vec3 lo(0.25f * (float)(next() % 8), 0.25f * (float)(next() % 8), 0.25f * (float)(next() % 8));  // (a grid: faces land in the same planes)
        const Vec3 size(0.25f * (float)(1 + next() % 4), 0.25f * (float)(1 + next() % 4), 0.25f * (float)(1 + next() % 4));
        r503_box(m, lo, lo + size);
      }
      total += r503_same_pairs(m, "random box soup");
    }
    CHECK(total > 0);  // (the soups do have faces on top of each other, so the comparison has something to find)
  });

  test("overlapping faces: the sweep and the all-pairs loop agree on faces 0.5 and 1.5 times the tolerance apart, same way and opposite ways", [] {
    for (const bool flipped : {false, true}) {
      for (const float f : {0.5f, 1.5f}) {
        Mesh m;
        r503_quad(m, {0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0});
        /* The tolerance of the pair: z-fighting faces are tested against `plane_dist` (given: 0.01 m, so the offsets are known exactly), the two sides of a slab against
         * 1e-5 of the mesh's size, whatever plane_dist is. */
        const float scale = length(Vec3(1.0f, 1.0f, 0.0f)), eps = flipped ? 1e-5f * scale : 0.01f;
        const float z = f * eps;
        if (flipped) r503_quad(m, {0, 1, z}, {1, 1, z}, {1, 0, z}, {0, 0, z});
        else r503_quad(m, {0, 0, z}, {1, 0, z}, {1, 1, z}, {0, 1, z});
        const size_t pairs = r503_same_pairs(m, flipped ? "opposite, near tolerance" : "same way, near tolerance", 0.01f);
        r503_same_pairs(m, "near tolerance, the default one");  // (the default tolerance, 1e-4 of the size: the two must agree there as well)
        std::printf("    %s, %.1f x tolerance: %zu pair(s)\n", flipped ? "opposite" : "same way", f, pairs);
        if (!flipped) CHECK(pairs == (f < 1.0f ? 1u : 0u));  // (opposite faces gave no pair at either offset in both versions: the agreement above is what is pinned there)
      }
    }
  });

  test("overlapping faces: the sweep and the all-pairs loop agree with degenerate faces, a face with a NaN corner, duplicate faces and a pair on the same plane", [] {
    Mesh m;
    r503_quad(m, {0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0});
    r503_quad(m, {0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0});                        // (a duplicate)
    r503_quad(m, {0.5f, 0.5f, 0}, {1.5f, 0.5f, 0}, {1.5f, 1.5f, 0}, {0.5f, 1.5f, 0});  // (overlapping a corner)
    r503_quad(m, {0, 0, 1}, {1, 0, 1}, {2, 0, 1}, {3, 0, 1});                         // (no area: all four on a line)
    r503_quad(m, {0, 0, 2}, {0, 0, 2}, {0, 0, 2}, {0, 0, 2});                         // (a point)
    const float nan = std::numeric_limits<float>::quiet_NaN();
    r503_quad(m, {0, 0, 3}, {1, 0, nan}, {1, 1, 3}, {0, 1, 3});                       // (a NaN corner)
    r503_quad(m, {0, 0, 0}, {0, 1, 0}, {1, 1, 0}, {1, 0, 0});                         // (the first one the other way)
    const size_t pairs = r503_same_pairs(m, "degenerate faces");
    CHECK(pairs >= 3);
    /* A corner at infinity along each axis in turn (the mesh's size is then infinite, and so is the tolerance): the two must still agree, whatever they find. */
    const float inf = std::numeric_limits<float>::infinity();
    for (int axis = 0; axis < 3; axis++) {
      for (const float sign : {1.0f, -1.0f}) {
        Mesh w;
        r503_quad(w, {0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0});
        r503_quad(w, {0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0});
        Vec3 corner(1.0f, 0.0f, 0.5f);
        (axis == 0 ? corner.x : axis == 1 ? corner.y : corner.z) = sign * inf;
        r503_quad(w, {0, 0, 0.5f}, corner, {1, 1, 0.5f}, {0, 1, 0.5f});
        r503_same_pairs(w, "an infinite corner");
      }
    }
  });

  test("overlapping faces: the sweep and the all-pairs loop agree when many triangles share the same x (the sweep's sort key), and with a larger tolerance given", [] {
    uint32_t seed = 777u;
    auto next = [&]() {
      seed = seed * 1664525u + 1013904223u;
      return (seed >> 8) & 0xFFFFu;
    };
    Mesh m;
    for (int k = 0; k < 60; k++) {  // quads in the plane x = 1, two ways round
      const float y = 0.25f * (float)(next() % 8), z = 0.25f * (float)(next() % 8);
      if (k % 3) r503_quad(m, {1, y, z}, {1, y + 0.5f, z}, {1, y + 0.5f, z + 0.5f}, {1, y, z + 0.5f});
      else r503_quad(m, {1, y, z}, {1, y, z + 0.5f}, {1, y + 0.5f, z + 0.5f}, {1, y + 0.5f, z});
    }
    r503_box(m, {1.0f, 0.0f, 0.0f}, {2.0f, 1.0f, 1.0f});  // (a box whose side lies in that plane too)
    CHECK(r503_same_pairs(m, "shared x") > 0);
    r503_same_pairs(m, "shared x, tolerance 0.01", 0.01f);
  });

  test("overlapping faces: the sweep and the all-pairs loop agree on a small interior of Blockout pieces - a room, styled window and door, columns, a roof, stairs and an arcade", [] {
    Editor ed;
    r503_room_scene(ed);
    for (const char *line : {"blockout add window grid 1.4 2.1 0.8", "blockout add door double", "blockout add column 0.2 2.6", "blockout add roof 6 4 35", "blockout add stairs l 1 3 2 8",
                             "blockout add arcade 3 2 3", "blockout add pilaster 0.2 2.6"}) {
      ed.command(line);
      r503_built(ed);
    }
    GameObject *bo = ed.scene().find_by_name("Blockout");
    const Mesh *m = r503_mesh(bo);
    CHECK(m && m->face_count() > 100);
    if (m) r503_same_pairs(*m, "small interior");
  });

  test("Adjust Last (F9): two doors selected, F9 about the first and the second active - an Open Amount typed into the Inspector reaches both, F9 shows it, and a later F9 edit keeps it", [] {
    Editor ed;
    r503_room_scene(ed);
    ed.command("blockout add door single 1 2.1");
    r503_settle(ed, 3);
    GameObject *b = ed.active_object_for_test();
    ed.command("blockout add door single 1 2.1");  // (the one F9 is about: the newest)
    r503_settle(ed, 3);
    GameObject *a = ed.active_object_for_test();
    BlockoutPiece *pa = a ? a->get<BlockoutPiece>() : nullptr, *pb = b ? b->get<BlockoutPiece>() : nullptr;
    CHECK(pa && pb && a != b && ed.obj_last_valid() && ed.obj_last_for_test().obj == a->id);
    if (!pa || !pb) return;
    ed.select_for_test(a->id, false);
    ed.select_for_test(b->id, true);  // (b active, a selected too)
    ed.command("window Inspector");
    r503_settle(ed, 3);
    CHECK(ed.active_object_for_test() == b && ed.obj_last_valid());
    CHECK(r503_type(ed, "field:Open Amount", "0.75"));
    CHECK(std::fabs(pb->open_amount - 0.75f) < 1e-4f && std::fabs(pa->open_amount - 0.75f) < 1e-4f);  // (the multi-object edit went to both)
    CHECK(ed.obj_last_valid() && std::fabs(ed.obj_last_for_test().open_amount - 0.75f) < 1e-4f);  // (F9 took the value A has, not the one it had)
    CHECK(r503_type(ed, "adjust:Width", "1.2"));
    CHECK(std::fabs(pa->open_amount - 0.75f) < 1e-4f && std::fabs(pa->width - 1.2f) < 1e-4f);  // (not written back to 0)
  });

  test("Adjust Last (F9): a `blockout set style` that changes only another selected piece leaves F9's piece alone and the panel ends, as before", [] {
    Editor ed;
    r503_room_scene(ed);
    ed.command("blockout add window grid 1.4 2.1 0.8");
    r503_settle(ed, 3);
    GameObject *win = ed.active_object_for_test();
    ed.command("blockout add box 1 1 1");  // (F9 is about the box now)
    r503_settle(ed, 3);
    GameObject *box = ed.active_object_for_test();
    BlockoutPiece *pw = win ? win->get<BlockoutPiece>() : nullptr;
    CHECK(pw && box && box != win && ed.obj_last_valid() && ed.obj_last_for_test().obj == box->id);
    if (!pw || !box) return;
    ed.select_for_test(win->id, true);  // (the box stays selected beside the window)
    ed.command("blockout set style double");
    r503_settle(ed, 3);
    CHECK(pw->style == kWindowDouble);
    CHECK(!ed.obj_last_valid());  // (the window's edit is a new step; F9 is not tied to it)
  });

  test("Adjust Last (F9): F9 on a window, a door active beside it - a Style picked in the Inspector for the door leaves the window as it was, and the panel ends", [] {
    Editor ed;
    r503_room_scene(ed);
    ed.command("blockout add door single 1 2.1");
    r503_settle(ed, 3);
    GameObject *door = ed.active_object_for_test();
    ed.command("blockout add window grid 1.4 2.1 0.8");  // (F9 is about the window: the newest)
    r503_settle(ed, 3);
    GameObject *win = ed.active_object_for_test();
    BlockoutPiece *pd = door ? door->get<BlockoutPiece>() : nullptr, *pw = win ? win->get<BlockoutPiece>() : nullptr;
    CHECK(pd && pw && door != win && ed.obj_last_valid() && ed.obj_last_for_test().obj == win->id);
    if (!pd || !pw) return;
    ed.select_for_test(win->id, false);
    ed.select_for_test(door->id, true);  // (the door active, the window selected too)
    ed.command("window Inspector");
    r503_settle(ed, 3);
    CHECK(r503_pick_combo(ed, "Single", "Double"));
    CHECK(pd->style == kDoorDouble);
    CHECK(pw->style == kWindowGrid);      // (a door's Style does not go to a window)
    CHECK(!ed.obj_last_valid());          // (F9 is not tied to the door's step)
  });

  /* The HCI worker's headless repro: draw a room, add a door, swing it by the console, wait, select its leaf. F9 showed Open Amount 0 while the leaf stood at 45 degrees.
   * Per adjust_last.cpp's rule the panel lasts while its step is the newest, whatever is selected: moving the selection to the leaf does not end it, so F9 keeps showing the
   * door's numbers, and they are the door's current ones. */
  test("Adjust Last (F9): add door, `blockout set open 0.5`, wait, `blockout leaf select` - F9 shows Open Amount 0.5 and the leaf stands at 45 degrees; a window set to arched shows Arched", [] {
    Editor ed;
    ed.init_headless(1600, 1000);
    r503_empty_scene(ed);
    ed.record_buttons_for_test(true);
    ed.command("blockout draw room auto at 0 0 0 normal 0 1 0 size 4 4 2.6");
    r503_settle(ed, 3);
    ed.command("blockout add door single 1 2.1");
    r503_settle(ed, 3);
    GameObject *door = ed.active_object_for_test();
    BlockoutPiece *bp = door ? door->get<BlockoutPiece>() : nullptr;
    CHECK(bp && ed.obj_last_valid() && ed.obj_last_for_test().opening == 1);
    if (!bp) return;
    ed.command("blockout set open 0.5");
    ed.command("blockout wait");
    ed.command("blockout leaf select");
    r503_settle(ed, 3);
    GameObject *leaf = ed.active_object_for_test();
    CHECK(leaf && leaf->get<BlockoutLeaf>() && leaf != door);
    if (!leaf) return;
    CHECK(ed.obj_last_valid() && ed.obj_last_for_test().obj == door->id);  // (the selection moved to the leaf; F9's step is still the newest)
    CHECK(std::fabs(ed.obj_last_for_test().open_amount - 0.5f) < 1e-4f);  // (what it showed was 0)
    const float yaw = std::acos(std::max(-1.0f, std::min(1.0f, dot(normalize(leaf->world_matrix().dir({0.0f, 0.0f, 1.0f})), normalize(door->world_matrix().dir({0.0f, 0.0f, 1.0f}))))));
    CHECK(std::fabs(yaw * 180.0f / 3.14159265f - 45.0f) < 0.5f);  // (the leaf's Rotation Y, 45 degrees about the door's up)
    /* The window: set arched, F9's Style shows Arched. */
    ed.command("blockout add window single 1.2 2 0.9");
    r503_settle(ed, 3);
    GameObject *win = ed.active_object_for_test();
    CHECK(win && win->get<BlockoutPiece>() && ed.obj_last_valid() && ed.obj_last_for_test().opening == 2);
    ed.command("blockout set style arched");
    ed.command("blockout wait");
    r503_settle(ed, 3);
    CHECK(win && win->get<BlockoutPiece>()->style == kWindowArched);
    CHECK(ed.obj_last_valid() && ed.obj_last_for_test().style == kWindowArched);
  });
}
