// SPDX-License-Identifier: GPL-2.0-or-later
// Snapping between Blockouts and joining Blockouts that touch (round 501, task 0485, ADR 0129's amendment): a moved Blockout, a moved
// piece and a dragged face handle land on the other Blockouts' faces and corners; a Blockout whose result touches another's shows
// "touches 'Name'" and a Join Blockouts button; `blockout join` moves the other Blockouts' pieces under one, each in its place in the world.
#include "test_common.h"

#include "../src/scene/blockout_object.h"

#include <set>

using namespace bl;

namespace {

using R501ET = platform::EventType;

platform::Event r501_ev(R501ET t, int x, int y, int mods = 0) {
  platform::Event e;
  e.type = t, e.x = x, e.y = y, e.mods = mods;
  return e;
}

void r501_settle(Editor &ed, int frames = 2) {
  for (int i = 0; i < frames; i++) ed.step_frame_headless();
}

void r501_click(Editor &ed, int x, int y) {
  ed.step_frame_headless({r501_ev(R501ET::MouseMove, x, y)});
  ed.step_frame_headless({r501_ev(R501ET::MouseDown, x, y)});
  ed.step_frame_headless({r501_ev(R501ET::MouseUp, x, y)});
}

void r501_key(Editor &ed, int key) {
  platform::Event down, up;
  down.type = R501ET::KeyDown, down.key = key;
  up.type = R501ET::KeyUp, up.key = key;
  ed.step_frame_headless({down});
  ed.step_frame_headless({up});
  ed.step_frame_headless();
}

bool r501_click_key(Editor &ed, const std::string &key) {
  ed.record_buttons_for_test(true);
  r501_settle(ed, 2);
  Recti r;
  if (!ed.button_rect_for_test(key, r)) return false;
  r501_click(ed, r.x + r.w / 2, r.y + r.h / 2);
  r501_settle(ed, 3);
  return true;
}

size_t r501_mark() {
  std::vector<LogEntry> tmp;
  return Log::fetch((size_t)1 << 40, tmp);
}
std::string r501_log_since(size_t mark) {
  std::vector<LogEntry> v;
  Log::fetch(mark, v);
  std::string s;
  for (const LogEntry &e : v) s += e.text + "\n";
  return s;
}

void r501_empty(Editor &ed) {
  std::vector<GameObject *> roots;
  ed.scene().for_each([&](GameObject &g) {
    if (!g.parent) roots.push_back(&g);
  });
  ed.scene().destroy(roots);
  ed.clear_selection_for_test();
}

GameObject *r501_hand(Editor &ed, const char *name) {
  GameObject *g = ed.scene().create(name);
  g->add<Blockout>();
  g->add<MeshFilter>()->mesh = std::make_shared<Mesh>();
  g->add<MeshRenderer>();
  return g;
}

GameObject *r501_box(Editor &ed, GameObject &parent, const char *name, Vec3 at, Vec3 size, int op = 0) {
  GameObject *g = ed.scene().create(name, &parent);
  BlockoutPiece *p = g->add<BlockoutPiece>();
  p->shape = 0, p->operation = op, p->size = size;
  g->set_local_position(at);
  return g;
}

size_t r501_blockouts(Editor &ed) {
  size_t n = 0;
  ed.scene().for_each([&](GameObject &g) { n += g.get<Blockout>() != nullptr; });
  return n;
}

size_t r501_pieces(Editor &ed, const GameObject &bo) {
  std::vector<const GameObject *> v;
  blockout_piece_objects(bo, v);
  return v.size();
}

/* The eight corners of a box piece in the world. */
std::vector<Vec3> r501_corners(const GameObject &p) {
  const Vec3 h = p.get<BlockoutPiece>()->size * 0.5f;
  const Mat4 w = p.world_matrix();
  std::vector<Vec3> out;
  for (int k = 0; k < 8; k++) out.push_back(w.point(Vec3((k & 1) ? h.x : -h.x, (k & 2) ? h.y : -h.y, (k & 4) ? h.z : -h.z)));
  return out;
}

float r501_worst(const std::vector<Vec3> &a, const std::vector<Vec3> &b) {
  float worst = 0.0f;
  for (size_t i = 0; i < a.size() && i < b.size(); i++) worst = std::max(worst, length(a[i] - b[i]));
  return a.size() == b.size() ? worst : 1e9f;
}

/* Two Blockouts side by side: Left's slab has its +X face at x = 1.1, Right's slab (2 m) its -X face at 3.3. */
void r501_pair(Editor &ed, GameObject *&left, GameObject *&right, GameObject *&slab_a, GameObject *&slab_b, int w = 1600, int h = 1000) {
  ed.init_headless(w, h);
  r501_empty(ed);
  ed.command("camera 20 55 22 3 0 0");
  left = r501_hand(ed, "Left");
  slab_a = r501_box(ed, *left, "SlabA", {0, 0, 0}, {2.2f, 2, 2});
  right = r501_hand(ed, "Right");
  right->set_world_position({4.3f, 0, 0});
  slab_b = r501_box(ed, *right, "SlabB", {0, 0, 0}, {2, 2, 2});
  ed.mark_changed_for_test("test scene");
  r501_settle(ed, 3);
}

}  // namespace

void tests_round501() {
  test("blockout join: a Blockout dragged along X with the Move gizmo stops with its wall in the plane of another Blockout's wall, and with Ctrl held does not", [] {
    for (const bool ctrl : {false, true}) {
      Editor ed;
      GameObject *left, *right, *sa, *sb;
      r501_pair(ed, left, right, sa, sb);
      ed.select_for_test(right->id, false);
      r501_settle(ed, 3);
      int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
      CHECK(ed.project_to_window({4.3f, 0.0f, 0.0f}, x0, y0) && ed.project_to_window({2.15f, 0.0f, 0.0f}, x1, y1));  // (a few centimetres short of flush: the grid would not land on 1.1)
      ed.command("tool move");
      ed.command("perf drag gizmo x " + std::to_string(x1 - x0) + " frames 8" + (ctrl ? " mods ctrl" : ""));
      r501_settle(ed, 20);
      const float face = sb->world_matrix().point({-1.0f, 0.0f, 0.0f}).x;
      std::printf("    ctrl %d: Right's -X face ended at x = %.5f (Left's +X face is at 1.1)\n", ctrl, face);
      if (ctrl) CHECK(std::fabs(face - 1.1f) > 0.01f);
      else CHECK_NEAR(face, 1.1f, 1e-4f);
    }
  });

  test("blockout join: the modal G locked to X lands a Blockout's wall in the plane of another's", [] {
    Editor ed;
    GameObject *left, *right, *sa, *sb;
    r501_pair(ed, left, right, sa, sb);
    ed.select_for_test(right->id, false);
    r501_settle(ed, 3);
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    CHECK(ed.project_to_window({4.3f, 0.0f, 0.0f}, x0, y0) && ed.project_to_window({2.15f, 0.0f, 0.0f}, x1, y1));
    ed.step_frame_headless({r501_ev(R501ET::MouseMove, x0, y0)});
    r501_key(ed, platform::KEY_G);
    r501_key(ed, platform::KEY_X);
    ed.step_frame_headless({r501_ev(R501ET::MouseMove, x1, y1)});
    ed.step_frame_headless({r501_ev(R501ET::MouseMove, x1, y1)});
    r501_click(ed, x1, y1);
    r501_settle(ed, 3);
    const float face = sb->world_matrix().point({-1.0f, 0.0f, 0.0f}).x;
    std::printf("    modal G: Right's -X face ended at x = %.5f\n", face);
    CHECK_NEAR(face, 1.1f, 1e-4f);
  });

  test("blockout join: a piece of one Blockout moved along X with the gizmo lands its face in the plane of another Blockout's wall", [] {
    Editor ed;
    GameObject *left, *right, *sa, *sb;
    r501_pair(ed, left, right, sa, sb);
    ed.select_for_test(sb->id, false);  // (the piece, not its Blockout: the piece snap runs)
    r501_settle(ed, 3);
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    CHECK(ed.project_to_window({4.3f, 0.0f, 0.0f}, x0, y0) && ed.project_to_window({2.15f, 0.0f, 0.0f}, x1, y1));
    ed.command("tool move");
    ed.command("perf drag gizmo x " + std::to_string(x1 - x0) + " frames 8");
    r501_settle(ed, 20);
    const float face = sb->world_matrix().point({-1.0f, 0.0f, 0.0f}).x;
    std::printf("    piece: SlabB's -X face ended at x = %.5f\n", face);
    CHECK_NEAR(face, 1.1f, 1e-4f);
  });

  test("blockout join: the other Blockouts' planes and corners are targets of a face handle's line, after the Blockout's own", [] {
    Editor ed;
    GameObject *left, *right, *sa, *sb;
    r501_pair(ed, left, right, sa, sb);
    std::vector<float> ts;
    ed.blockout_cross_candidates(*right, Vec3(4.3f, 0, 0), Vec3(-1, 0, 0), ts);  // (metres from Right's middle, towards Left)
    bool plane = false;
    for (float t : ts) plane = plane || std::fabs(t - (4.3f - 1.1f)) < 1e-4f;  // (Left's +X face)
    CHECK(plane);
    CHECK(ts.size() == 8);  // (Left's slab's eight corners; Right's own are not targets)
  });

  test("blockout join: two touching Blockouts show \"touches 'Name'\" and the button, two apart show neither", [] {
    Editor ed;
    GameObject *left, *right, *sa, *sb;
    r501_pair(ed, left, right, sa, sb, 1500, 2200);
    ed.record_buttons_for_test(true);
    ed.select_for_test(left->id, false);
    r501_settle(ed, 3);
    Recti r;
    CHECK(!ed.button_rect_for_test("Join Blockouts", r));  // (a 2.2 m slab and a 2 m slab with their faces 2.2 m apart)
    CHECK(!ed.blockout_touches(*left, *right));
    right->set_world_position({2.1f, 0.0f, 0.0f});  // (Right's -X face at 1.1: the walls meet)
    r501_settle(ed, 3);
    CHECK(ed.blockout_touches(*left, *right) && ed.blockout_touches(*right, *left));
    CHECK(ed.button_rect_for_test("Join Blockouts", r));
    CHECK(ed.button_rect_for_test("shown:touches 'Right'", r));
    right->set_world_position({2.0f, 0.0f, 0.0f});  // (overlapping)
    r501_settle(ed, 3);
    CHECK(ed.blockout_touches(*left, *right));
    right->set_world_position({2.12f, 0.0f, 0.0f});  // (a centimetre of air)
    r501_settle(ed, 3);
    CHECK(!ed.blockout_touches(*left, *right));
  });

  test("blockout join: the button joins two touching Blockouts, every piece in its place in the world, the object gone, one undo step that brings it back with its id", [] {
    Editor ed;
    GameObject *left, *right, *sa, *sb;
    r501_pair(ed, left, right, sa, sb, 1500, 2200);
    right->set_world_position({2.1f, 0.0f, 0.0f});
    GameObject *folder = ed.scene().create("Folder", right);
    GameObject *deep = r501_box(ed, *folder, "Deep", {0, 0, 1.5f}, {1, 1, 1});
    const uint64_t right_id = right->id, sb_id = sb->id;
    const std::vector<Vec3> before_a = r501_corners(*sa), before_b = r501_corners(*sb), before_d = r501_corners(*deep);
    ed.select_for_test(left->id, false);
    ed.mark_changed_for_test("test scene");
    r501_settle(ed, 3);
    const size_t steps = ed.undo_depth_for_test();
    CHECK(r501_click_key(ed, "Join Blockouts"));
    r501_settle(ed, 3);
    CHECK(r501_blockouts(ed) == 1);
    CHECK(ed.scene().find_by_name("Right") == nullptr);
    CHECK(r501_pieces(ed, *left) == 3);
    CHECK(sb->parent == left && deep->parent == folder && folder->parent == left);
    CHECK(r501_worst(before_a, r501_corners(*sa)) < 1e-4f && r501_worst(before_b, r501_corners(*sb)) < 1e-4f && r501_worst(before_d, r501_corners(*deep)) < 1e-4f);
    CHECK(sb->local().scale.x == 1.0f && sb->local().scale.y == 1.0f && sb->local().scale.z == 1.0f);
    CHECK(ed.undo_depth_for_test() == steps + 1);
    ed.command("undo");
    r501_settle(ed, 3);
    GameObject *back = ed.scene().find_by_name("Right");
    CHECK(back && back->id == right_id && r501_blockouts(ed) == 2);
    GameObject *sb2 = ed.scene().find(sb_id);
    CHECK(sb2 && sb2->parent == back);
  });

  test("blockout join: a Blockout turned 30 degrees, one scaled 2 with a turned piece, and a third - joined by name; nothing is sheared and every world box stays", [] {
    Editor ed;
    ed.init_headless(1600, 1000);
    r501_empty(ed);
    const Quat turn = Quat::axis_angle(Vec3(0, 1, 0), 30.0f * kDeg2Rad);
    GameObject *a = r501_hand(ed, "Alpha");
    a->set_world_rotation(turn);
    GameObject *pa = r501_box(ed, *a, "PA", {0, 0, 0}, {2, 2, 2});
    GameObject *b = r501_hand(ed, "Beta");  // (against Alpha's +X face, in Alpha's turned frame; scaled 2 about its own origin)
    b->set_world_rotation(turn);
    b->set_world_position(turn.rotate(Vec3(3.0f, 0, 0)));
    b->set_local_scale(Vec3(2.0f));
    GameObject *pb = r501_box(ed, *b, "PB", {-0.5f, 0, 0}, {1, 1, 1});
    GameObject *pt = r501_box(ed, *b, "PT", {0.5f, 0, 0.5f}, {1, 1, 1});
    pt->set_local_rotation(Quat::axis_angle(Vec3(0, 1, 0), 30.0f * kDeg2Rad));
    GameObject *c = r501_hand(ed, "Gamma");
    c->set_world_position(Vec3(-2.0f, 0, 0));  // (against Alpha's other side, unturned: Alpha's wall is turned, so these overlap rather than lie flush)
    GameObject *pc = r501_box(ed, *c, "PC", {0, 0, 0}, {2, 2, 2});
    std::vector<GameObject *> pieces{pa, pb, pt, pc};
    std::vector<std::vector<Vec3>> before;
    for (GameObject *p : pieces) before.push_back(r501_corners(*p));
    ed.mark_changed_for_test("test scene");
    r501_settle(ed, 3);
    const size_t mark = r501_mark();
    ed.command("blockout join Alpha Beta Gamma");
    r501_settle(ed, 3);
    CHECK(r501_blockouts(ed) == 1 && r501_pieces(ed, *a) == 4);
    for (size_t i = 0; i < pieces.size(); i++) CHECK(r501_worst(before[i], r501_corners(*pieces[i])) < 1e-4f);
    for (GameObject *p : pieces) {
      const Vec3 s = p->local().scale;
      CHECK(s.x == 1.0f && s.y == 1.0f && s.z == 1.0f);
    }
    CHECK(r501_log_since(mark).find("Joined 2 Blockouts into 'Alpha'") != std::string::npos);
  });

  test("blockout join: where the class materials differ no piece changes its material; settings that differ take the active Blockout's", [] {
    Editor ed;
    ed.init_headless(1600, 1000);
    r501_empty(ed);
    GameObject *a = r501_hand(ed, "Alpha");
    r501_box(ed, *a, "PA", {0, 0, 0}, {2, 2, 2});
    GameObject *b = r501_hand(ed, "Beta");
    b->set_world_position({2.0f, 0, 0});
    GameObject *pb = r501_box(ed, *b, "PB", {0, 0, 0}, {2, 2, 2});
    GameObject *own = r501_box(ed, *b, "PO", {0, 2, 0}, {1, 1, 1});
    a->get<Blockout>()->material[kClassSolid] = "Concrete";
    b->get<Blockout>()->material[kClassSolid] = "Brick";
    b->get<Blockout>()->cell = 0.5f;
    own->get<BlockoutPiece>()->material[kClassSolid] = "Glass";
    ed.mark_changed_for_test("test scene");
    r501_settle(ed, 3);
    const size_t mark = r501_mark();
    ed.command("blockout join Alpha Beta");
    r501_settle(ed, 3);
    CHECK(r501_blockouts(ed) == 1);
    CHECK(pb->get<BlockoutPiece>()->material[kClassSolid] == "Brick");
    CHECK(own->get<BlockoutPiece>()->material[kClassSolid] == "Glass");
    CHECK(a->get<Blockout>()->cell == 0.1f && a->get<Blockout>()->material[kClassSolid] == "Concrete");
    const std::string log = r501_log_since(mark);
    CHECK(log.find("Field Cell") != std::string::npos && log.find("class materials") != std::string::npos);
  });

  test("blockout join: refusals say why and change nothing - a baked Blockout (Unbake first), one Blockout alone, a Blockout not touching anything, one inside the other", [] {
    Editor ed;
    ed.init_headless(1600, 1000);
    r501_empty(ed);
    GameObject *a = r501_hand(ed, "Alpha");
    r501_box(ed, *a, "PA", {0, 0, 0}, {2, 2, 2});
    GameObject *b = r501_hand(ed, "Beta");
    b->set_world_position({2.0f, 0, 0});
    r501_box(ed, *b, "PB", {0, 0, 0}, {2, 2, 2});
    GameObject *far = r501_hand(ed, "Far");
    far->set_world_position({50.0f, 0, 0});
    r501_box(ed, *far, "PF", {0, 0, 0}, {2, 2, 2});
    GameObject *baked = ed.scene().create("Baked");
    baked->add<BlockoutBaked>();
    ed.mark_changed_for_test("test scene");
    r501_settle(ed, 3);
    size_t mark = r501_mark();
    ed.command("blockout join Alpha Baked");
    CHECK(r501_log_since(mark).find("Unbake first") != std::string::npos);
    CHECK(r501_blockouts(ed) == 3);
    mark = r501_mark();
    ed.select_for_test(far->id, false);
    ed.command("blockout join");
    CHECK(r501_log_since(mark).find("touches no other") != std::string::npos);
    CHECK(r501_blockouts(ed) == 3);
    mark = r501_mark();
    ed.command("blockout join Far");  // (a Blockout alone)
    CHECK(r501_log_since(mark).find("two or more") != std::string::npos);
    CHECK(r501_blockouts(ed) == 3);
    /* A Blockout under the one it is joined with: the outer one's pieces cannot go under the inner one. */
    ed.scene().set_parent(b, a, -1, true);
    ed.mark_changed_for_test("nest");
    r501_settle(ed, 3);
    mark = r501_mark();
    ed.select_for_test(b->id, false);
    ed.command("blockout join Alpha");
    CHECK(r501_log_since(mark).find("Hierarchy") != std::string::npos);
    CHECK(r501_blockouts(ed) == 3);
    /* Named Blockouts that do not touch still join, and the log says they do not. */
    ed.scene().set_parent(b, nullptr, -1, true);
    r501_settle(ed, 3);
    mark = r501_mark();
    ed.select_for_test(a->id, false);
    ed.command("blockout join Alpha Far");
    CHECK(r501_log_since(mark).find("did not touch") != std::string::npos);
    CHECK(r501_blockouts(ed) == 2 && ed.scene().find_by_name("Far") == nullptr);
  });

  test("blockout join: one Blockout selected joins everything that touches it, and what touches that", [] {
    Editor ed;
    ed.init_headless(1600, 1000);
    r501_empty(ed);
    GameObject *a = r501_hand(ed, "Alpha");
    r501_box(ed, *a, "PA", {0, 0, 0}, {2, 2, 2});
    GameObject *b = r501_hand(ed, "Beta");
    b->set_world_position({2.0f, 0, 0});
    r501_box(ed, *b, "PB", {0, 0, 0}, {2, 2, 2});
    GameObject *c = r501_hand(ed, "Gamma");
    c->set_world_position({4.0f, 0, 0});  // (touches Beta only)
    r501_box(ed, *c, "PC", {0, 0, 0}, {2, 2, 2});
    GameObject *far = r501_hand(ed, "Far");
    far->set_world_position({50.0f, 0, 0});
    r501_box(ed, *far, "PF", {0, 0, 0}, {2, 2, 2});
    ed.mark_changed_for_test("test scene");
    ed.select_for_test(a->id, false);
    r501_settle(ed, 3);
    CHECK(ed.blockout_join_available());
    ed.command("blockout join");
    r501_settle(ed, 3);
    CHECK(r501_blockouts(ed) == 2 && r501_pieces(ed, *a) == 3 && ed.scene().find_by_name("Far") != nullptr);
  });
}

/* ===================================================================== */
/* Round 501, the test engineer: the acceptance tests of task 0485, by the  */
/* spec's criteria, with real events where it says so                        */
/* ===================================================================== */

namespace {

using R501Mods = int;

const Mesh *r501e_mesh(const GameObject &g) {
  const MeshFilter *mf = g.get<MeshFilter>();
  return mf && mf->mesh ? mf->mesh.get() : nullptr;
}

void r501e_wait(Editor &ed) {
  ed.command("blockout wait");
  r501_settle(ed, 3);
}

void r501e_click(Editor &ed, int x, int y, int mods = 0) {
  ed.step_frame_headless({r501_ev(R501ET::MouseMove, x, y, mods)});
  ed.step_frame_headless({r501_ev(R501ET::MouseDown, x, y, mods)});
  ed.step_frame_headless({r501_ev(R501ET::MouseUp, x, y, mods)});
}

/* A menu reached by real events (the keys: menubar entry, then the item; a submenu is hovered). */
bool r501e_menu(Editor &ed, const std::vector<std::string> &keys) {
  ed.record_buttons_for_test(true);
  for (size_t i = 0; i < keys.size(); i++) {
    Recti r;
    for (int k = 0; k < 4 && !ed.button_rect_for_test(keys[i], r); k++) ed.step_frame_headless();
    if (!ed.button_rect_for_test(keys[i], r)) return false;
    const int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
    for (R501ET t : {R501ET::MouseMove, R501ET::MouseDown, R501ET::MouseUp}) ed.step_frame_headless({r501_ev(t, cx, cy)});
    ed.step_frame_headless();
  }
  return true;
}

/* ---- geometry oracles ---- */

/* Whether a point (in the mesh's own space) is inside the solid: the parity of the faces a skewed ray crosses (copied from test_blockout.cpp's r483_solid). */
bool r501e_inside(const Mesh &m, Vec3 p) {
  const double dir[3] = {0.3217, 0.8531, 0.4112};
  int crossings = 0;
  for (size_t f = 0; f < m.face_count(); f++) {
    const uint32_t *v = m.face_verts(f);
    for (uint32_t i = 1; i + 1 < m.face_size(f); i++) {
      const Vec3 a = m.positions[v[0]], b = m.positions[v[i]], c = m.positions[v[i + 1]];
      const double e1[3] = {(double)b.x - a.x, (double)b.y - a.y, (double)b.z - a.z}, e2[3] = {(double)c.x - a.x, (double)c.y - a.y, (double)c.z - a.z};
      const double h[3] = {dir[1] * e2[2] - dir[2] * e2[1], dir[2] * e2[0] - dir[0] * e2[2], dir[0] * e2[1] - dir[1] * e2[0]};
      const double det = e1[0] * h[0] + e1[1] * h[1] + e1[2] * h[2];
      if (std::fabs(det) < 1e-12) continue;
      const double s[3] = {(double)p.x - a.x, (double)p.y - a.y, (double)p.z - a.z};
      const double u = (s[0] * h[0] + s[1] * h[1] + s[2] * h[2]) / det;
      if (u < 0.0 || u > 1.0) continue;
      const double q[3] = {s[1] * e1[2] - s[2] * e1[1], s[2] * e1[0] - s[0] * e1[2], s[0] * e1[1] - s[1] * e1[0]};
      const double w = (dir[0] * q[0] + dir[1] * q[1] + dir[2] * q[2]) / det;
      if (w < 0.0 || u + w > 1.0) continue;
      crossings += (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) / det > 0.0;
    }
  }
  return crossings % 2 == 1;
}

/* Whether a WORLD point is solid in a Blockout's (or any object's) mesh. */
bool r501e_solid(const GameObject &bo, Vec3 world) {
  const Mesh *m = r501e_mesh(bo);
  return m && r501e_inside(*m, bo.world_matrix().inverse().point(world));
}

/* What is wrong with a mesh, or nothing: structure, closed, outward. */
std::string r501e_problem(const Mesh &m) {
  std::string why;
  if (!structurally_valid(m, &why)) return "invalid: " + why;
  if (!closed_manifold(m)) return "open or pinched";
  if (m.face_count() && signed_volume(m) <= 0.0f) return "inside out";
  return "";
}

std::vector<Vec3> r501e_face_points(const Mesh &m, const Mat4 &w, size_t f) {
  std::vector<Vec3> out;
  for (uint32_t c = m.face_offsets[f]; c < m.face_offsets[f + 1]; c++) out.push_back(w.point(m.positions[m.corner_verts[c]]));
  return out;
}

Vec3 r501e_newell(const std::vector<Vec3> &p) {
  Vec3 n(0.0f);
  for (size_t i = 0; i < p.size(); i++) {
    const Vec3 a = p[i], b = p[(i + 1) % p.size()];
    n += Vec3((a.y - b.y) * (a.z + b.z), (a.z - b.z) * (a.x + b.x), (a.x - b.x) * (a.y + b.y));
  }
  return n;
}

/* The face of a mesh (in the world, through `w`) that contains the world point `p` in its plane with the normal `n` (within `tol` metres), or -1. */
int r501e_face_at(const Mesh &m, const Mat4 &w, Vec3 p, Vec3 n, float tol = 1e-3f) {
  for (size_t f = 0; f < m.face_count(); f++) {
    const std::vector<Vec3> pts = r501e_face_points(m, w, f);
    Vec3 nf = r501e_newell(pts);
    if (!(length(nf) > 1e-12f)) continue;
    nf = normalize(nf);
    if (dot(nf, n) < 0.99f || std::fabs(dot(p - pts[0], nf)) > tol) continue;
    int drop = 0;  // the axis the face is most square to: the 2D test runs on the other two
    for (int a = 1; a < 3; a++)
      if (std::fabs(nf[a]) > std::fabs(nf[drop])) drop = a;
    const int u = (drop + 1) % 3, v = (drop + 2) % 3;
    bool in = false;
    for (size_t i = 0, j = pts.size() - 1; i < pts.size(); j = i++) {
      const double ui = pts[i][u], vi = pts[i][v], uj = pts[j][u], vj = pts[j][v];
      if ((vi > p[v]) != (vj > p[v]) && (double)p[u] < (uj - ui) * ((double)p[v] - vi) / (vj - vi) + ui) in = !in;
    }
    if (in) return (int)f;
  }
  return -1;
}

/* A face's material as the asset path of the slot it uses ("" the default). */
std::string r501e_face_material(const GameObject &bo, const Mesh &m, size_t f) {
  const MeshRenderer *mr = bo.get<MeshRenderer>();
  const int slot = m.material_of(f);
  if (!mr || slot < 0 || (size_t)slot >= mr->materials.size() || !mr->materials[(size_t)slot]) return "";
  return mr->materials[(size_t)slot]->asset_path;
}

/* Points on every face of a Blockout's mesh (the face's centre and the centres of its fan triangles), in the world, with the face's normal and material: what the face is
 * called before a join, to be found again after it. */
struct R501Sample {
  std::vector<Vec3> pts;
  Vec3 n;
  std::string material;
};
std::vector<R501Sample> r501e_samples(const GameObject &bo) {
  std::vector<R501Sample> out;
  const Mesh *m = r501e_mesh(bo);
  if (!m) return out;
  const Mat4 w = bo.world_matrix();
  for (size_t f = 0; f < m->face_count(); f++) {
    const std::vector<Vec3> p = r501e_face_points(*m, w, f);
    Vec3 n = r501e_newell(p);
    if (!(length(n) > 1e-12f)) continue;
    R501Sample s;
    s.n = normalize(n);
    s.material = r501e_face_material(bo, *m, f);
    Vec3 c(0.0f);
    for (const Vec3 &q : p) c += q;
    s.pts.push_back(c * (1.0f / (float)p.size()));
    for (size_t i = 1; i + 1 < p.size() && s.pts.size() < 6; i++) s.pts.push_back((p[0] + p[i] + p[i + 1]) * (1.0f / 3.0f));
    out.push_back(s);
  }
  return out;
}

/* The samples found again in a mesh: how many, how many with the same material, the ones not found (their first point) and the ones that changed (words). */
struct R501Found {
  size_t total = 0, found = 0, same = 0;
  std::vector<Vec3> missing;
  std::vector<std::string> changed;
  std::vector<Vec3> changed_at;  // (a point on each changed face)
};
R501Found r501e_find(const std::vector<R501Sample> &samples, const GameObject &bo) {
  R501Found r;
  const Mesh *m = r501e_mesh(bo);
  const Mat4 w = bo.world_matrix();
  for (const R501Sample &s : samples) {
    r.total++;
    int face = -1;
    for (const Vec3 &p : s.pts) {
      face = m ? r501e_face_at(*m, w, p, s.n) : -1;
      if (face >= 0) break;
    }
    if (face < 0) {
      r.missing.push_back(s.pts[0]);
      continue;
    }
    r.found++;
    const std::string now = r501e_face_material(bo, *m, (size_t)face);
    if (now == s.material) r.same++;
    else {
      r.changed.push_back("'" + s.material + "' -> '" + now + "' at " + strprintf("%.2f %.2f %.2f", s.pts[0].x, s.pts[0].y, s.pts[0].z));
      r.changed_at.push_back(s.pts[0]);
    }
  }
  return r;
}

/* A material asset in the scratch project; its project path. */
std::string r501e_material(const char *name, Vec3 colour) {
  const std::string path = std::string("Assets/Materials/R501/") + name + ".mat";
  create_material_asset(*make_material(name, colour), path);
  return path;
}

/* Every piece object under a Blockout, enabled or not, in folders or not, not into another Blockout. */
void r501e_collect(GameObject &n, std::vector<GameObject *> &out) {
  for (GameObject *c : n.children) {
    if (c->get<Blockout>()) continue;
    if (c->get<BlockoutPiece>()) out.push_back(c);
    r501e_collect(*c, out);
  }
}
std::vector<GameObject *> r501e_pieces(GameObject &bo) {
  std::vector<GameObject *> v;
  r501e_collect(bo, v);
  return v;
}

/* The world corners of every piece, one list each, to compare after an edit. */
std::vector<std::vector<Vec3>> r501e_all_corners(const std::vector<GameObject *> &pieces) {
  std::vector<std::vector<Vec3>> v;
  for (const GameObject *p : pieces) v.push_back(r501_corners(*p));
  return v;
}
float r501e_worst_boxes(const std::vector<std::vector<Vec3>> &before, const std::vector<GameObject *> &pieces) {
  float worst = 0.0f;
  for (size_t i = 0; i < pieces.size() && i < before.size(); i++) worst = std::max(worst, r501_worst(before[i], r501_corners(*pieces[i])));
  return before.size() == pieces.size() ? worst : 1e9f;
}
bool r501e_unit_scales(const std::vector<GameObject *> &pieces) {
  for (const GameObject *p : pieces) {
    const Vec3 s = p->local().scale;
    if (!(s.x == 1.0f && s.y == 1.0f && s.z == 1.0f)) return false;
  }
  return true;
}

bool r501e_same_mesh(const Mesh &a, const Mesh &b) {
  return a.positions.size() == b.positions.size() && a.face_offsets == b.face_offsets && a.corner_verts == b.corner_verts &&
         (a.positions.empty() || std::memcmp(a.positions.data(), b.positions.data(), a.positions.size() * sizeof(Vec3)) == 0);
}

/* What blockout::build makes of the Blockout's snapshot with no cache: the mesh the editor's must equal. */
bool r501e_matches(const GameObject &g) {
  std::vector<blockout::Piece> pieces;
  blockout::Settings st;
  blockout_snapshot(g, pieces, st);
  Mesh ref;
  const Mesh *m = r501e_mesh(g);
  return m && blockout::build(pieces, st, ref) && r501e_same_mesh(ref, *m);
}

/* The area of the mesh's faces that lie wholly in the plane `axis = at` (any way they face): a wall's faces kept, and nothing of a face that should be gone. */
double r501e_area_in_plane(const GameObject &bo, int axis, float at) {
  const Mesh *m = r501e_mesh(bo);
  const Mat4 w = bo.world_matrix();
  double area = 0.0;
  for (size_t f = 0; m && f < m->face_count(); f++) {
    const std::vector<Vec3> p = r501e_face_points(*m, w, f);
    bool all = true;
    for (const Vec3 &q : p) all = all && std::fabs(q[axis] - at) < 1e-3f;
    if (all) area += 0.5 * length(r501e_newell(p));
  }
  return area;
}

/* ---- scenes ---- */

/* Two interiors as the stress section builds them, as two Blockouts: a 4.2 x 3.2 x 4.2 shell (Add), 3.8 x 2.8 x 3.8 of air (Subtract) and, when asked, a door cut
 * (Subtract) through the wall at +X. 4 m apart along X their shells share a wall 0.2 m thick. */
struct R501Int {
  GameObject *bo = nullptr, *shell = nullptr, *air = nullptr, *door = nullptr;
};
R501Int r501e_interior(Editor &ed, const std::string &name, Vec3 at, bool door, float turn_deg = 0.0f) {
  R501Int r;
  r.bo = r501_hand(ed, name.c_str());
  r.bo->set_world_position(at);
  if (turn_deg != 0.0f) r.bo->set_world_rotation(Quat::axis_angle(Vec3(0, 1, 0), turn_deg * kDeg2Rad));
  r.shell = r501_box(ed, *r.bo, (name + "_Shell").c_str(), {0, 0, 0}, {4.2f, 3.2f, 4.2f});  // (no spaces: the console splits names at them)
  r.air = r501_box(ed, *r.bo, (name + "_Air").c_str(), {0, 0, 0}, {3.8f, 2.8f, 3.8f}, 1);
  if (door) r.door = r501_box(ed, *r.bo, (name + "_Door").c_str(), {2.0f, -0.35f, 0}, {0.6f, 2.1f, 0.9f}, 1);
  return r;
}

/* An empty scene and a view that holds most of what these tests build. */
void r501e_open(Editor &ed, const char *camera = "camera 20 55 22 2 2 2") {
  ed.init_headless(1600, 1000);
  r501_empty(ed);
  ed.command(camera);
}

/* The screen pixels along an axis between two world points, signed by where the axis points: what `perf drag gizmo` takes. */
int r501e_pixels(Editor &ed, Vec3 from, Vec3 to, int axis) {
  Vec3 unit(0.0f);
  unit[axis] = 1.0f;
  int ax = 0, ay = 0, bx = 0, by = 0, cx = 0, cy = 0;
  if (!ed.project_to_window(from, ax, ay) || !ed.project_to_window(to, bx, by) || !ed.project_to_window(from + unit, cx, cy)) return 0;
  const float ux = (float)(cx - ax), uy = (float)(cy - ay), len = std::max(1e-3f, std::sqrt(ux * ux + uy * uy));
  return (int)std::lround(((float)(bx - ax) * ux + (float)(by - ay) * uy) / len);
}

/* The Move gizmo's arrow along `axis` dragged from where the lead stands to a world point (a real drag: press, moves, release). */
void r501e_gizmo(Editor &ed, Vec3 from, Vec3 to, int axis, int mods = 0, const char *handle = nullptr) {
  const char *const names[3] = {"x", "y", "z"};
  ed.command("tool move");
  ed.command(std::string("perf drag gizmo ") + (handle ? handle : names[axis]) + " " + std::to_string(r501e_pixels(ed, from, to, axis)) + " frames 8" + (mods & platform::MOD_CTRL ? " mods ctrl" : ""));
  r501_settle(ed, 30);
}

/* The modal G (locked to an axis when `axis` is 0..2, free when -1) from the pixel of one world point to the pixel of another, then a click; `typed` types a distance instead. */
void r501e_modal(Editor &ed, Vec3 from, Vec3 to, int axis, int mods = 0, const std::string &typed = "") {
  int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  ed.project_to_window(from, x0, y0);
  ed.project_to_window(to, x1, y1);
  ed.step_frame_headless({r501_ev(R501ET::MouseMove, x0, y0)});
  r501_key(ed, platform::KEY_G);
  if (axis >= 0) r501_key(ed, axis == 0 ? platform::KEY_X : axis == 1 ? platform::KEY_Y : platform::KEY_Z);
  if (!typed.empty()) {
    for (char c : typed) {
      platform::Event e;
      e.type = R501ET::Text, e.codepoint = (uint32_t)c;
      ed.step_frame_headless({e});
    }
    ed.step_frame_headless({r501_ev(R501ET::MouseMove, x1, y1)});
    platform::Event d, u;
    d.type = R501ET::KeyDown, d.key = platform::KEY_ENTER;
    u.type = R501ET::KeyUp, u.key = platform::KEY_ENTER;
    ed.step_frame_headless({d});
    ed.step_frame_headless({u});
  }
  else {
    ed.step_frame_headless({r501_ev(R501ET::MouseMove, x1, y1, mods)});
    ed.step_frame_headless({r501_ev(R501ET::MouseMove, x1, y1, mods)});
    r501e_click(ed, x1, y1, mods);
  }
  r501_settle(ed, 3);
}

/* Left is a box 2.2 long on `axis` and 2 across (so its face on that axis is at 1.1 and it spans -1..1 on the others); Right is 4.3 along `axis`. In the corner variant Right's piece is a
 * metre cube with its high faces at +1 on the other two axes, so it meets Left's corner, not just its wall. */
struct R501Flush {
  GameObject *left = nullptr, *right = nullptr, *piece = nullptr;
  Vec3 at;
};
R501Flush r501e_flush_scene(Editor &ed, int axis, bool corner) {
  /* The view looks across the axis that is dragged (a drag along the line of sight is foreshortened: the pixels of the mouse and the metres of the move stop being proportional, and
   * 'five centimetres short of flush' would be a guess). */
  r501e_open(ed, axis == 0 ? "camera 20 55 22 2 2 2" : axis == 1 ? "camera 20 5 22 2 2 2" : "camera 100 15 22 2 2 2");
  R501Flush s;
  Vec3 lsize(2.0f), rsize(2.0f);
  lsize[axis] = 2.2f;
  s.left = r501_hand(ed, "Left");
  r501_box(ed, *s.left, "SlabA", {0, 0, 0}, lsize);
  s.right = r501_hand(ed, "Right");
  s.at = Vec3(0.0f);
  if (corner) {
    rsize = Vec3(1.0f);
    for (int k = 0; k < 3; k++)
      if (k != axis) s.at[k] = 0.5f;
  }
  s.at[axis] = 1.1f + rsize[axis] * 0.5f + 1.1f;  // (its near face 1.1 m from Left's)
  s.right->set_world_position(s.at);
  s.piece = r501_box(ed, *s.right, "SlabB", {0, 0, 0}, rsize);
  ed.mark_changed_for_test("test scene");
  r501_settle(ed, 3);
  return s;
}
/* Where Right's origin must stand for its near face to lie in Left's face (1.1 on the axis). */
float r501e_flush_origin(const R501Flush &s, int axis) { return 1.1f + s.piece->get<BlockoutPiece>()->size[axis] * 0.5f; }
float r501e_near_face(const R501Flush &s, int axis) { return s.piece->world_matrix().point(Vec3(0.0f))[axis] - s.piece->get<BlockoutPiece>()->size[axis] * 0.5f; }

/* A Left that is something else, for the scenes where a Blockout must not be a target: kind 0 nothing, 1 a baked Blockout, 2 a plain mesh, 3 a Blockout switched off, 4 a Blockout without
 * pieces, 5 a Blockout whose only piece is switched off, 6 a live Blockout (the control: it IS a target). Right (4.3 along X, one 2 m piece) is the object that moves. */
struct R501Kind {
  GameObject *left = nullptr, *right = nullptr, *slab_b = nullptr;
};
const char *const kR501Kinds[7] = {"nothing", "baked", "plain mesh", "switched off", "no pieces", "pieces off", "live"};
R501Kind r501e_kind_scene(Editor &ed, int kind) {
  r501e_open(ed, "camera 20 55 22 3 0 0");
  R501Kind s;
  if (kind == 2) {
    s.left = ed.scene().create("Left");
    s.left->add<MeshFilter>()->mesh = primitives::cube(1.0f);
    s.left->add<MeshRenderer>();
    s.left->set_local_scale({2.2f, 2.0f, 2.0f});
  }
  else if (kind != 0) {
    s.left = r501_hand(ed, "Left");
    if (kind != 4) {
      GameObject *a = r501_box(ed, *s.left, "SlabA", {0, 0, 0}, {2.2f, 2, 2});
      if (kind == 5) a->get<BlockoutPiece>()->enabled = false;
    }
  }
  s.right = r501_hand(ed, "Right");
  s.right->set_world_position({4.3f, 0, 0});
  s.slab_b = r501_box(ed, *s.right, "SlabB", {0, 0, 0}, {2, 2, 2});
  ed.mark_changed_for_test("test scene");
  r501_settle(ed, 3);
  if (kind == 1) {
    r501e_wait(ed);
    ed.select_for_test(s.left->id, false);
    ed.command("blockout bake");
    r501_settle(ed, 3);
  }
  if (kind == 3) {
    s.left->active = false;
    ed.mark_changed_for_test("switch off");
    r501_settle(ed, 3);
  }
  return s;
}

bool r501e_bits(Vec3 a, Vec3 b) { return std::memcmp(&a, &b, sizeof(Vec3)) == 0; }

/* A handle taken, pulled to the pixel of a world point and let go (copied from test_blockout.cpp's r479_pull: false when the handle or the point is off the screen). */
bool r501e_pull(Editor &ed, const char *handle, Vec3 to, int mods = 0, bool *snapped = nullptr) {
  int hx = 0, hy = 0, tx = 0, ty = 0;
  if (!ed.blockout_handle_pixel_for_test(handle, hx, hy) || !ed.project_to_window(to, tx, ty)) return false;
  ed.step_frame_headless({r501_ev(R501ET::MouseMove, hx, hy, mods)});
  ed.step_frame_headless({r501_ev(R501ET::MouseDown, hx, hy, mods)});
  if (!ed.blockout_dragging_for_test()) return false;
  ed.step_frame_headless({r501_ev(R501ET::MouseMove, tx, ty, mods)});
  if (snapped) *snapped = ed.blockout_drag_snapped_for_test();
  ed.step_frame_headless({r501_ev(R501ET::MouseUp, tx, ty, mods)});
  r501_settle(ed, 3);
  return true;
}

}  // namespace

static void tests_round501_snap() {
  /* ---- criterion 1: a moved Blockout snaps to another (wall on wall, corner on corner), Ctrl off, on every axis ---- */
  test("blockout join (te): a Blockout dragged with the Move gizmo along X, Y and Z stops with its wall in the plane of the other's wall to the float; with Ctrl held it does not", [] {
    for (int axis = 0; axis < 3; axis++)
      for (const bool ctrl : {false, true}) {
        Editor ed;
        R501Flush s = r501e_flush_scene(ed, axis, false);
        ed.select_for_test(s.right->id, false);
        r501_settle(ed, 3);
        Vec3 to = s.at;
        to[axis] = r501e_flush_origin(s, axis) + 0.05f;  // (five centimetres short of flush: the grid would not land on 1.1)
        r501e_gizmo(ed, s.at, to, axis, ctrl ? platform::MOD_CTRL : 0);
        const float face = r501e_near_face(s, axis);
        std::printf("    axis %d ctrl %d: near face at %.5f (Left's face is at 1.1)\n", axis, (int)ctrl, face);
        if (ctrl) CHECK(std::fabs(face - 1.1f) > 0.01f);
        else CHECK_NEAR(face, 1.1f, 1e-4f);
        const Vec3 now = s.right->world_position();
        for (int k = 0; k < 3; k++)
          if (k != axis) CHECK(std::fabs(now[k] - s.at[k]) < 1e-5f);  // (a single-axis drag changes one coordinate)
        CHECK(s.right->local().scale.x == 1.0f && s.right->local().scale.y == 1.0f && s.right->local().scale.z == 1.0f);
      }
  });

  test("blockout join (te): a corner dragged onto a corner of another Blockout lands on it in all three coordinates (Ctrl: it does not)", [] {
    for (int axis = 0; axis < 3; axis++)
      for (const bool ctrl : {false, true}) {
        Editor ed;
        R501Flush s = r501e_flush_scene(ed, axis, true);
        ed.select_for_test(s.right->id, false);
        r501_settle(ed, 3);
        Vec3 to = s.at;
        to[axis] = r501e_flush_origin(s, axis) + 0.05f;
        r501e_gizmo(ed, s.at, to, axis, ctrl ? platform::MOD_CTRL : 0);
        /* Right's corner (low on `axis`, high on the others) and Left's (high on `axis`, high on the others). */
        Vec3 lo(0.0f), hi(0.0f);
        for (int k = 0; k < 3; k++) lo[k] = k == axis ? -0.5f : 0.5f, hi[k] = k == axis ? 1.1f : 1.0f;
        const Vec3 mine = s.piece->world_matrix().point(lo), theirs = hi;
        std::printf("    axis %d ctrl %d: distance corner to corner %.5f\n", axis, (int)ctrl, length(mine - theirs));
        if (ctrl) CHECK(length(mine - theirs) > 0.01f);
        else CHECK(length(mine - theirs) < 1e-4f);
      }
  });

  test("blockout join (te): the modal G locked to X, Y or Z lands a Blockout's wall in the other's plane; with Ctrl it is the grid's step instead; a typed distance is never snapped; G with no axis lock takes no snap", [] {
    for (int axis = 0; axis < 3; axis++)
      for (const bool ctrl : {false, true}) {
        Editor ed;
        R501Flush s = r501e_flush_scene(ed, axis, false);
        ed.select_for_test(s.right->id, false);
        r501_settle(ed, 3);
        Vec3 to = s.at;
        to[axis] = r501e_flush_origin(s, axis) + 0.05f;
        r501e_modal(ed, s.at, to, axis, ctrl ? platform::MOD_CTRL : 0);
        const float face = r501e_near_face(s, axis);
        std::printf("    modal axis %d ctrl %d: near face at %.5f\n", axis, (int)ctrl, face);
        if (ctrl) CHECK(std::fabs(face - 1.1f) > 0.01f);
        else CHECK_NEAR(face, 1.1f, 1e-4f);
      }
    {  // (a typed distance is the user's number)
      Editor ed;
      R501Flush s = r501e_flush_scene(ed, 0, false);
      ed.select_for_test(s.right->id, false);
      r501_settle(ed, 3);
      r501e_modal(ed, s.at, s.at, 0, 0, "-1.07");
      const float face = r501e_near_face(s, 0);
      std::printf("    typed -1.07: near face at %.5f (2.2 - 1.07 = 1.13)\n", face);
      CHECK_NEAR(face, 1.13f, 1e-4f);
    }
    {  // (no axis lock: the view plane, no Blockout snap - the face does not come to rest in 1.1)
      Editor ed;
      R501Flush s = r501e_flush_scene(ed, 0, false);
      ed.select_for_test(s.right->id, false);
      r501_settle(ed, 3);
      Vec3 to = s.at;
      to.x = r501e_flush_origin(s, 0) + 0.05f;
      r501e_modal(ed, s.at, to, -1);
      const float face = r501e_near_face(s, 0);
      std::printf("    modal, no axis: near face at %.5f\n", face);
      CHECK(std::fabs(face - 1.1f) > 1e-3f);
    }
  });

  test("blockout join (te): the Move gizmo's free handle takes no Blockout snap", [] {
    Editor ed;
    R501Flush s = r501e_flush_scene(ed, 0, false);
    ed.select_for_test(s.right->id, false);
    r501_settle(ed, 3);
    Vec3 to = s.at;
    to.x = r501e_flush_origin(s, 0) + 0.05f;
    r501e_gizmo(ed, s.at, to, 0, 0, "free");
    const float face = r501e_near_face(s, 0);
    std::printf("    free handle: near face at %.5f\n", face);
    CHECK(std::fabs(face - 1.1f) > 1e-3f);
  });

  test("blockout join (te): several Blockouts moved together snap as one set against the others; a Group holding a Blockout snaps its Blockout; members of the moving set are no targets of each other", [] {
    {
      Editor ed;
      R501Flush s = r501e_flush_scene(ed, 0, false);
      s.right->set_world_position({4.3f, 0, 0}), s.at = Vec3(4.3f, 0, 0);  // (the layout of this test: Right 2.15 m from flush, Right2 4.3 m beyond it)
      GameObject *right2 = r501_hand(ed, "Right2");
      right2->set_world_position({8.6f, 0, 0});
      GameObject *slab_c =r501_box(ed, *right2, "SlabC", {0, 0, 0}, {2, 2, 2});
      ed.mark_changed_for_test("test scene");
      r501_settle(ed, 3);
      ed.select_for_test(s.right->id, false);
      ed.select_for_test(right2->id, true);  // (the lead is the one selected last)
      r501_settle(ed, 3);
      const float gap0 = right2->world_position().x - s.right->world_position().x;
      r501e_gizmo(ed, right2->world_position(), Vec3(right2->world_position().x - (4.3f - 2.15f), 0, 0), 0);
      std::printf("    set: Right's near face at %.5f, Right2's at %.5f\n", r501e_near_face(s, 0), slab_c->world_position().x - 1.0f);
      CHECK_NEAR(r501e_near_face(s, 0), 1.1f, 1e-4f);
      CHECK_NEAR(right2->world_position().x - s.right->world_position().x, gap0, 1e-5f);  // (they moved by the same distance)
    }
    {  // (the Group)
      Editor ed;
      R501Flush s = r501e_flush_scene(ed, 0, false);
      s.right->set_world_position({4.3f, 0, 0}), s.at = Vec3(4.3f, 0, 0);
      GameObject *group = ed.scene().create("Group");
      group->set_world_position(s.at);
      ed.scene().set_parent(s.right, group, -1, true);
      ed.mark_changed_for_test("group");
      ed.select_for_test(group->id, false);
      r501_settle(ed, 3);
      r501e_gizmo(ed, s.at, Vec3(r501e_flush_origin(s, 0) + 0.05f, 0, 0), 0);
      CHECK_NEAR(r501e_near_face(s, 0), 1.1f, 1e-4f);
    }
    {  // (the moving set's own members are no targets: no snap although Right2's wall is where Right's would come to)
      Editor ed;
      R501Flush s = r501e_flush_scene(ed, 0, false);
      s.right->set_world_position({4.3f, 0, 0}), s.at = Vec3(4.3f, 0, 0);
      GameObject *right2 = r501_hand(ed, "Right2");
      right2->set_world_position({8.6f, 0, 0});
      r501_box(ed, *right2, "SlabC", {0, 0, 0}, {2, 2, 2});
      r501_settle(ed, 3);
      ed.mark_changed_for_test("test scene");
      r501_settle(ed, 3);
      /* Both move, Left is the only other Blockout. Moving by -2.28: Right's -X face (3.3) comes to 1.02, 0.08 from Left's wall at 1.1 (d = -2.2); Right2's -X face (7.6) comes to 5.32,
       * 0.02 from Right's own +X plane (5.3, d = -2.3). Were the set's members targets of each other the nearer, -2.3, would win. */
      float d = -2.28f;
      const bool both = ed.blockout_whole_snap(*right2, right2->world_position(), Vec3(1, 0, 0), {s.right->id, right2->id}, d);
      std::printf("    both moving, d = -2.28 -> snapped %d, d %.4f (Left's wall: -2.2; the set's own faces must not count)\n", (int)both, d);
      CHECK(both && std::fabs(d - -2.2f) < 1e-4f);
      /* Only Right moves: Right2 is a target now. */
      d = 4.28f;
      const bool alone = ed.blockout_whole_snap(*s.right, s.right->world_position(), Vec3(1, 0, 0), {s.right->id}, d);
      std::printf("    Right alone, d = 4.28 -> snapped %d, d %.4f\n", (int)alone, d);
      CHECK(alone && std::fabs(d - 4.3f) < 1e-4f);
    }
  });

  test("blockout join (te): a baked Blockout, a plain mesh, a Blockout switched off, one with no pieces and one whose pieces are off are no snap targets - the Blockout lands exactly where it does with nothing else in the scene", [] {
    Vec3 alone;
    for (int kind = 0; kind < 7; kind++) {
      Editor ed;
      R501Kind s = r501e_kind_scene(ed, kind);
      ed.select_for_test(s.right->id, false);
      r501_settle(ed, 3);
      r501e_gizmo(ed, {4.3f, 0, 0}, {2.15f, 0, 0}, 0);
      const Vec3 now = s.right->world_position();
      std::printf("    Left = %-12s: Right ended at x = %.6f\n", kR501Kinds[kind], now.x);
      if (kind == 0) alone = now;
      else if (kind == 6) CHECK_NEAR(now.x - 1.0f, 1.1f, 1e-4f);  // (the control: a live Blockout IS a target)
      else CHECK(r501e_bits(now, alone));
    }
  });

  test("blockout join (te): the Move gizmo for an object that is no Blockout ends where it ended before - bit for bit - with a Blockout beside it or not", [] {
    Vec3 ended[2];
    for (int with = 0; with < 2; with++) {
      Editor ed;
      r501e_open(ed, "camera 20 55 22 3 0 0");
      if (with) {
        GameObject *left = r501_hand(ed, "Left");
        r501_box(ed, *left, "SlabA", {0, 0, 0}, {2.2f, 2, 2});
      }
      GameObject *cube = ed.scene().create("Cube");
      cube->add<MeshFilter>()->mesh = primitives::cube(1.0f);
      cube->add<MeshRenderer>();
      cube->set_world_position({4.3f, 0, 0});
      cube->set_local_scale({2, 2, 2});
      ed.mark_changed_for_test("test scene");
      r501_settle(ed, 3);
      ed.select_for_test(cube->id, false);
      r501_settle(ed, 3);
      r501e_gizmo(ed, {4.3f, 0, 0}, {2.15f, 0, 0}, 0);
      ended[with] = cube->world_position();
      std::printf("    cube, %s a Blockout: ended at x = %.6f\n", with ? "with" : "without", ended[with].x);
    }
    CHECK(r501e_bits(ended[0], ended[1]));
    CHECK(std::fabs(ended[1].x - 2.15f) < 0.3f);  // (it did move: the drag is not what is being compared away)
  });

  /* ---- criterion 2: a piece snaps across Blockouts: the gizmo, and a face handle with the mouse ---- */
  test("blockout join (te): a piece of Blockout B dragged with the gizmo lands in A's plane, not with Ctrl, and not for targets that are no live Blockout", [] {
    Vec3 alone;
    for (int kind = 0; kind < 7; kind++)
      for (const bool ctrl : {false, true}) {
        if (ctrl && kind != 6) continue;
        Editor ed;
        R501Kind s = r501e_kind_scene(ed, kind);
        ed.select_for_test(s.slab_b->id, false);
        r501_settle(ed, 3);
        r501e_gizmo(ed, {4.3f, 0, 0}, {2.15f, 0, 0}, 0, ctrl ? platform::MOD_CTRL : 0);
        const Vec3 now = s.slab_b->world_position();
        std::printf("    Left = %-12s ctrl %d: SlabB ended at x = %.6f\n", kR501Kinds[kind], (int)ctrl, now.x);
        if (kind == 0) alone = now;
        else if (kind == 6 && ctrl) CHECK(std::fabs(now.x - 2.1f) > 0.01f);
        else if (kind == 6) CHECK_NEAR(now.x - 1.0f, 1.1f, 1e-4f);
        else CHECK(r501e_bits(now, alone));
      }
  });

  test("blockout join (te): a face handle pulled with the mouse across Blockouts lands in the other's wall plane (both directions), Ctrl does not, one undo step puts the piece back", [] {
    for (const bool ctrl : {false, true}) {
      Editor ed;
      GameObject *left, *right, *sa, *sb;
      r501_pair(ed, left, right, sa, sb);
      ed.select_for_test(sb->id, false);
      r501_settle(ed, 3);
      const size_t depth = ed.undo_depth_for_test();
      bool snapped = false;
      CHECK(r501e_pull(ed, "-x", {1.15f, 0.0f, 0.0f}, ctrl ? platform::MOD_CTRL : 0, &snapped));
      const BlockoutPiece *bp = sb->get<BlockoutPiece>();
      const float face = sb->world_matrix().point({-bp->size.x * 0.5f, 0.0f, 0.0f}).x;
      std::printf("    -x handle, ctrl %d: face ended at x = %.5f, snapped %d\n", (int)ctrl, face, (int)snapped);
      if (ctrl) CHECK(!snapped && std::fabs(face - 1.1f) > 1e-3f);
      else CHECK(snapped && std::fabs(face - 1.1f) < 1e-4f);
      CHECK_NEAR(sb->world_matrix().point({bp->size.x * 0.5f, 0, 0}).x, 5.3f, 1e-4f);  // (the far face did not move)
      CHECK(ed.undo_depth_for_test() == depth + 1);
      ed.command("undo");
      r501_settle(ed, 3);
      sb = ed.scene().find_by_name("SlabB");
      CHECK(sb && sb->get<BlockoutPiece>()->size.x == 2.0f && sb->local().position.x == 0.0f);
    }
    {  // (the other way: Left's slab pulled out towards Right's wall at 3.3)
      Editor ed;
      GameObject *left, *right, *sa, *sb;
      r501_pair(ed, left, right, sa, sb);
      ed.select_for_test(sa->id, false);
      r501_settle(ed, 3);
      bool snapped = false;
      CHECK(r501e_pull(ed, "+x", {3.35f, 0.0f, 0.0f}, 0, &snapped));
      const BlockoutPiece *bp = sa->get<BlockoutPiece>();
      const float face = sa->world_matrix().point({bp->size.x * 0.5f, 0.0f, 0.0f}).x;
      std::printf("    +x handle: face ended at x = %.5f, snapped %d\n", face, (int)snapped);
      CHECK(snapped && std::fabs(face - 3.3f) < 1e-4f);
      CHECK_NEAR(sa->world_matrix().point({-bp->size.x * 0.5f, 0, 0}).x, -1.1f, 1e-4f);
    }
  });

  test("blockout join (te): a face handle takes no snap from a baked Blockout, a plain mesh, a Blockout switched off or without live pieces - the same size as with nothing there", [] {
    float alone = 0.0f;
    for (int kind = 0; kind < 7; kind++) {
      Editor ed;
      R501Kind s = r501e_kind_scene(ed, kind);
      ed.select_for_test(s.slab_b->id, false);
      r501_settle(ed, 3);
      bool snapped = false;
      CHECK(r501e_pull(ed, "-x", {1.15f, 0.0f, 0.0f}, 0, &snapped));
      const float size = s.slab_b->get<BlockoutPiece>()->size.x;
      std::printf("    Left = %-12s: SlabB's width %.5f snapped %d\n", kR501Kinds[kind], size, (int)snapped);
      if (kind == 0) alone = size;
      else if (kind == 6) CHECK(snapped && std::fabs(size - 4.2f) < 1e-4f);
      else CHECK(!snapped && size == alone);
    }
  });

  test("blockout join (te): the other Blockouts' targets come after the Blockout's own in a piece's candidate list, are appended and not replaced, leave out the Blockout asked for, and are the world places of turned and scaled Blockouts and of a room's shell", [] {
    Editor ed;
    GameObject *left, *right, *sa, *sb;
    r501_pair(ed, left, right, sa, sb);
    std::vector<float> ts{42.0f};  // (a candidate of the piece's own Blockout, already there)
    ed.blockout_cross_candidates(*right, Vec3(4.3f, 0, 0), Vec3(-1, 0, 0), ts);
    CHECK(ts.size() == 9 && ts[0] == 42.0f);
    ts.clear();
    ed.blockout_cross_candidates(*left, Vec3(0, 0, 0), Vec3(1, 0, 0), ts);  // (Right's: x = 3.3 and 5.3, four corners each)
    size_t at33 = 0, at53 = 0;
    for (float t : ts) at33 += std::fabs(t - 3.3f) < 1e-4f, at53 += std::fabs(t - 5.3f) < 1e-4f;
    CHECK(ts.size() == 8 && at33 == 4 && at53 == 4);
    /* A Blockout turned 30 degrees and scaled 2 along X (as a loaded scene may have it), its piece turned too: the candidates are the pieces' world corners along the line. */
    left->set_world_rotation(Quat::axis_angle(Vec3(0, 1, 0), 30.0f * kDeg2Rad));
    left->set_local_scale({2.0f, 1.0f, 1.0f});
    sa->set_local_rotation(Quat::axis_angle(Vec3(0, 0, 1), 20.0f * kDeg2Rad));
    ed.mark_changed_for_test("turn");
    r501_settle(ed, 3);
    const Vec3 origin(4.3f, 0.2f, 0.1f), dir = normalize(Vec3(-1.0f, 0.2f, 0.3f));
    ts.clear();
    ed.blockout_cross_candidates(*right, origin, dir, ts);
    const std::vector<Vec3> corners = r501_corners(*sa);
    size_t matched = 0;
    for (const Vec3 &c : corners) {
      const float want = dot(c - origin, dir);
      for (float t : ts)
        if (std::fabs(t - want) < 1e-4f) {
          matched++;
          break;
        }
    }
    CHECK(ts.size() == 8 && matched == 8);
    /* A room: its air and its shell (air grown by the walls) both count. */
    GameObject *room_bo = r501_hand(ed, "Hall");
    room_bo->set_world_position({0, 0, 20});
    GameObject *room = r501_box(ed, *room_bo, "Room", {0, 0, 0}, {4, 2.6f, 4});
    room->get<BlockoutPiece>()->room = true;
    room->get<BlockoutPiece>()->wall = room->get<BlockoutPiece>()->floor = room->get<BlockoutPiece>()->ceiling = 0.2f;
    ed.mark_changed_for_test("room");
    r501_settle(ed, 3);
    ts.clear();
    ed.blockout_cross_candidates(*right, Vec3(0, 0, 20), Vec3(1, 0, 0), ts);
    size_t air = 0, shell = 0;
    for (float t : ts) air += std::fabs(std::fabs(t) - 2.0f) < 1e-4f, shell += std::fabs(std::fabs(t) - 2.2f) < 1e-4f;
    std::printf("    a room: %zu candidates, %zu on the air's walls, %zu on the shell's\n", ts.size(), air, shell);
    CHECK(air == 8 && shell == 8);  // (four corners on each side of the middle: |t| 2.0 for the air's walls, 2.2 for the shell's)
    /* A Blockout switched off is no target. */
    room_bo->active = false;
    ed.mark_changed_for_test("off");
    r501_settle(ed, 3);
    ts.clear();
    ed.blockout_cross_candidates(*right, Vec3(0, 0, 20), Vec3(1, 0, 0), ts);
    CHECK(ts.size() == 8);  // (Left's only)
  });
}

namespace {

std::vector<GameObject *> r501e_by_ids(Editor &ed, const std::vector<uint64_t> &ids) {
  std::vector<GameObject *> v;
  for (uint64_t id : ids) v.push_back(ed.scene().find(id));
  return v;
}
std::vector<uint64_t> r501e_ids(const std::vector<GameObject *> &v) {
  std::vector<uint64_t> ids;
  for (const GameObject *g : v) ids.push_back(g->id);
  return ids;
}
/* The worst way a piece's world box moved, the pieces looked up by id again (an undo makes the objects anew); 1e9 when one is gone. */
float r501e_worst_by_ids(Editor &ed, const std::vector<uint64_t> &ids, const std::vector<std::vector<Vec3>> &before) {
  float worst = 0.0f;
  for (size_t i = 0; i < ids.size(); i++) {
    const GameObject *g = ed.scene().find(ids[i]);
    if (!g) return 1e9f;
    worst = std::max(worst, r501_worst(before[i], r501_corners(*g)));
  }
  return worst;
}

/* Worst 1 - |cos| of a face normal of the Blockout's mesh in the WORLD against the nearest axis of any of `frames` (0: every face is square to one of the frames' axes). */
float r501e_off_axes(const GameObject &bo, const std::vector<Quat> &frames) {
  const Mesh *m = r501e_mesh(bo);
  if (!m) return 1e9f;
  const Mat4 w = bo.world_matrix();
  float worst = 0.0f;
  for (size_t f = 0; f < m->face_count(); f++) {
    Vec3 n = r501e_newell(r501e_face_points(*m, w, f));
    if (!(length(n) > 1e-12f)) continue;
    n = normalize(n);
    float best = 0.0f;
    for (const Quat &q : frames)
      for (const Vec3 axis : {Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(0, 0, 1)}) best = std::max(best, std::fabs(dot(n, q.rotate(axis))));
    worst = std::max(worst, 1.0f - best);
  }
  return worst;
}

/* A status line the Inspector shows (the recorded "shown:" rows), by a part of its words. */
bool r501e_shows(Editor &ed, const std::string &part) {
  ed.record_buttons_for_test(true);
  r501_settle(ed, 3);
  for (const auto &kv : ed.ui_for_test().button_rects)
    if (kv.first.rfind("shown:", 0) == 0 && kv.first.find(part) != std::string::npos) return true;
  return false;
}

/* Grid points over the two interiors, offset by numbers that no face plane is a multiple of. */
std::vector<Vec3> r501e_probe_points() {
  std::vector<Vec3> v;
  for (int i = 0; i < 20; i++)
    for (int j = 0; j < 10; j++)
      for (int k = 0; k < 14; k++) v.push_back(Vec3(-3.1f + 0.531f * (float)i, -1.9f + 0.413f * (float)j, -3.0f + 0.557f * (float)k));
  return v;
}

}  // namespace

static void tests_round501_join() {
  /* ---- criterion 3: the join of two touching interiors ---- */
  test("blockout join (te): two interiors that share a wall join into one Blockout - every piece where it was, none scaled, ONE wall between the rooms, the doorway open, closed and valid; one undo step brings both back with their ids, redo joins again", [] {
    Editor ed;
    r501e_open(ed);
    R501Int a = r501e_interior(ed, "Alpha", {0, 0, 0}, true);
    R501Int b = r501e_interior(ed, "Beta", {4, 0, 0}, false);
    ed.mark_changed_for_test("test scene");
    r501e_wait(ed);
    std::vector<GameObject *> pieces = r501e_pieces(*a.bo);
    for (GameObject *p : r501e_pieces(*b.bo)) pieces.push_back(p);
    const std::vector<uint64_t> ids = r501e_ids(pieces);
    const std::vector<std::vector<Vec3>> before = r501e_all_corners(pieces);
    const uint64_t alpha_id = a.bo->id, beta_id = b.bo->id, beta_shell = b.shell->id;
    const Mesh beta_mesh = *r501e_mesh(*b.bo);
    CHECK(ed.blockout_touches(*a.bo, *b.bo));
    CHECK(r501e_solid(*b.bo, {2.0f, 0.0f, 0.0f}));  // (before: the door is in Alpha, Beta's wall is whole)
    CHECK(r501e_problem(*r501e_mesh(*a.bo)).empty() && r501e_problem(beta_mesh).empty());
    ed.select_for_test(a.bo->id, false);
    r501_settle(ed, 2);
    const size_t steps = ed.undo_depth_for_test();
    ed.command("blockout join Beta");
    r501e_wait(ed);
    GameObject *alpha = ed.scene().find(alpha_id);
    CHECK(alpha && r501_blockouts(ed) == 1 && ed.scene().find_by_name("Beta") == nullptr);
    if (!alpha) return;
    CHECK(r501e_pieces(*alpha).size() == 5);
    CHECK(r501e_worst_by_ids(ed, ids, before) < 1e-4f);
    CHECK(r501e_unit_scales(r501e_by_ids(ed, ids)));
    const Mesh *m = r501e_mesh(*alpha);
    CHECK(m != nullptr);
    if (!m) return;
    const std::string problem = r501e_problem(*m);
    if (!problem.empty()) std::printf("    joined mesh: %s\n", problem.c_str());
    CHECK(problem.empty());
    CHECK(r501e_matches(*alpha));
    CHECK(overlapping_face_pairs(*m) == 0);
    /* 2 shells of 56.448 sharing 0.2 x 3.2 x 4.2, minus two rooms of 40.432, minus the door through the wall (0.2 x 2.1 x 0.9). */
    const double volume = signed_volume(*m);
    std::printf("    joined volume %.4f (expected 28.966)\n", volume);
    CHECK(std::fabs(volume - 28.966) < 0.02);
    CHECK(r501e_solid(*alpha, {2.0f, 0.0f, 1.5f}));   // (the middle of the shared wall's thickness)
    CHECK(r501e_solid(*alpha, {2.0f, -1.5f, 0.0f}));  // (... under the door: the sill of the floor)
    CHECK(r501e_solid(*alpha, {2.0f, 1.5f, 0.0f}));   // (... over the door: the ceiling)
    CHECK(!r501e_solid(*alpha, {2.0f, 0.0f, 0.0f}));  // (the doorway: air, where Beta's wall was before)
    CHECK(!r501e_solid(*alpha, {0.0f, 0.0f, 0.0f}) && !r501e_solid(*alpha, {4.0f, 0.0f, 0.0f}));
    /* One wall: its two faces (Alpha's air at 1.9, Beta's at 2.1) and nothing of the outer shells inside it. */
    const Mat4 w = alpha->world_matrix();
    CHECK(r501e_face_at(*m, w, {1.9f, 0.0f, 1.2f}, {-1, 0, 0}) >= 0 && r501e_face_at(*m, w, {2.1f, 0.0f, 1.2f}, {1, 0, 0}) >= 0);
    for (const float x : {1.9f, 2.1f})
      for (const float n : {-1.0f, 1.0f}) CHECK(r501e_face_at(*m, w, {x, 1.5f, 0.0f}, {n, 0, 0}) < 0);  // (a point in the ceiling slab on those planes: no face)
    /* Each room's wall face is 3.8 x 2.8 less the doorway's notch (0.9 x 2.1): any copy of an outer shell's face left in the wall would add to it. */
    const double a19 = r501e_area_in_plane(*alpha, 0, 1.9f), a21 = r501e_area_in_plane(*alpha, 0, 2.1f);
    std::printf("    area of the faces in the plane x = 1.9: %.4f, x = 2.1: %.4f (expected 8.75 each)\n", a19, a21);
    CHECK(std::fabs(a19 - 8.75) < 0.01 && std::fabs(a21 - 8.75) < 0.01);
    /* One undo step: both objects back with their ids and meshes. */
    CHECK(ed.undo_depth_for_test() == steps + 1);
    const Mesh joined = *m;
    ed.command("undo");
    r501e_wait(ed);
    GameObject *back = ed.scene().find_by_name("Beta");
    CHECK(back && back->id == beta_id && r501_blockouts(ed) == 2);
    const GameObject *shell2 = ed.scene().find(beta_shell);
    CHECK(shell2 && back && shell2->parent == back);
    CHECK(back && r501e_mesh(*back) && r501e_same_mesh(beta_mesh, *r501e_mesh(*back)));
    CHECK(r501e_worst_by_ids(ed, ids, before) < 1e-4f);
    CHECK(ed.scene().find(alpha_id) && r501e_pieces(*ed.scene().find(alpha_id)).size() == 3);
    ed.command("redo");
    r501e_wait(ed);
    alpha = ed.scene().find(alpha_id);
    CHECK(alpha && r501_blockouts(ed) == 1 && ed.scene().find_by_name("Beta") == nullptr && r501e_pieces(*alpha).size() == 5);
    CHECK(alpha && r501e_mesh(*alpha) && r501e_same_mesh(joined, *r501e_mesh(*alpha)));
    CHECK(r501e_worst_by_ids(ed, ids, before) < 1e-4f);
    ed.command("undo");
    r501e_wait(ed);
    CHECK(r501_blockouts(ed) == 2 && ed.scene().find(beta_id) != nullptr);  // (and again: ids stay)
  });

  test("blockout join (te): two interiors at many offsets and turns - touching, overlapping walls, shifted, turned - join into a sound mesh that is solid exactly where either was, every piece's world box kept to 1e-4", [] {
    struct Variant {
      const char *what;
      Vec3 at;
      float turn_a, turn_b;
      bool oracle;  // (the union of the two meshes is what the join must make: only where neither interior reaches into the other's air)
    };
    const Quat t30 = Quat::axis_angle(Vec3(0, 1, 0), 30.0f * kDeg2Rad);
    const std::vector<Variant> variants = {
        {"walls share 0.2 m", {4.0f, 0, 0}, 0, 0, true},
        {"shells just touching", {4.2f, 0, 0}, 0, 0, true},
        {"shifted 1.2 m along Z", {4.0f, 0, 1.2f}, 0, 0, true},
        {"shifted 0.4 m up", {4.0f, 0.4f, 0}, 0, 0, true},
        {"Beta turned 90 degrees", {4.0f, 0, 0}, 0, 90, true},
        {"both turned 30 degrees in one frame", t30.rotate(Vec3(4.0f, 0, 0)), 30, 30, true},
        {"Beta turned 30 degrees, Alpha not", {4.0f, 0, 0}, 0, 30, false},
        {"Alpha turned 30 degrees, Beta not", {4.0f, 0, 0}, 30, 0, false},
    };
    const std::vector<Vec3> probes = r501e_probe_points();
    for (const Variant &v : variants) {
      Editor ed;
      r501e_open(ed);
      R501Int a = r501e_interior(ed, "Alpha", {0, 0, 0}, false, v.turn_a);
      R501Int b = r501e_interior(ed, "Beta", v.at, false, v.turn_b);
      ed.mark_changed_for_test("test scene");
      r501e_wait(ed);
      std::vector<GameObject *> pieces = r501e_pieces(*a.bo);
      for (GameObject *p : r501e_pieces(*b.bo)) pieces.push_back(p);
      const std::vector<std::vector<Vec3>> before = r501e_all_corners(pieces);
      std::vector<char> expect;
      for (const Vec3 &p : probes) expect.push_back(r501e_solid(*a.bo, p) || r501e_solid(*b.bo, p));
      CHECK(ed.blockout_touches(*a.bo, *b.bo));
      ed.select_for_test(a.bo->id, false);
      r501_settle(ed, 2);
      ed.command("blockout join Beta");
      r501e_wait(ed);
      CHECK(r501_blockouts(ed) == 1 && r501e_pieces(*a.bo).size() == 4);
      const float moved = r501e_worst_boxes(before, pieces);
      const Mesh *m = r501e_mesh(*a.bo);
      const std::string problem = m ? r501e_problem(*m) : "no mesh";
      size_t wrong = 0;
      if (v.oracle && m)
        for (size_t i = 0; i < probes.size(); i++) wrong += (r501e_solid(*a.bo, probes[i]) != (expect[i] != 0));
      std::printf("    %-38s boxes moved %.2e, problem '%s', %zu of %zu probes differ from the union%s\n", v.what, moved, problem.c_str(), wrong, probes.size(), v.oracle ? "" : " (not checked)");
      CHECK(moved < 1e-4f);
      CHECK(problem.empty());
      CHECK(r501e_unit_scales(pieces));
      CHECK(wrong == 0);
      CHECK(m && r501e_matches(*a.bo));
    }
  });

  /* ---- criterion 4: turns, scales, materials, three Blockouts, nested folders ---- */
  test("blockout join (te): a Blockout scaled 2 1 1 with a turned piece joins an unscaled one - its scale goes into the pieces' numbers, no piece is scaled or sheared, the Inspector's shear line is gone; the log says the scale was folded", [] {
    Editor ed;
    r501e_open(ed);
    GameObject *a = r501_hand(ed, "Alpha");
    GameObject *pa = r501_box(ed, *a, "PA", {0, 0, 0}, {2, 2, 2});
    GameObject *b = r501_hand(ed, "Beta");
    b->set_world_position({3.0f, 0, 0});
    b->set_local_scale({2.0f, 1.0f, 1.0f});  // (a scene loaded with a scale: the world shapes below are what it shows now)
    GameObject *pb = r501_box(ed, *b, "PB", {-0.5f, 0, 0}, {1, 1, 1});  // (2 m long in the world: x from 1 to 3, meeting PA)
    GameObject *pt = r501_box(ed, *b, "PT", {0.5f, 0, 0.5f}, {1, 1, 1});
    pt->set_local_rotation(Quat::axis_angle(Vec3(0, 1, 0), 30.0f * kDeg2Rad));
    ed.mark_changed_for_test("test scene");
    r501e_wait(ed);
    ed.select_for_test(b->id, false);
    CHECK(r501e_shows(ed, "scaled unevenly"));  // (round 496: Beta shears its turned piece now)
    const std::vector<GameObject *> straight{pa, pb};
    const std::vector<std::vector<Vec3>> before = r501e_all_corners(straight);
    const Vec3 centre_before = pt->world_position();
    ed.select_for_test(a->id, false);
    const size_t mark = r501_mark();
    ed.command("blockout join Beta");
    r501e_wait(ed);
    const std::string said = r501_log_since(mark);
    std::printf("    log: %s", said.c_str());
    CHECK(r501_blockouts(ed) == 1 && r501e_pieces(*a).size() == 3);
    CHECK(r501e_worst_boxes(before, straight) < 1e-4f);  // (the straight pieces are where they were, now by their own numbers)
    CHECK(r501e_unit_scales({pa, pb, pt}));
    CHECK(length(pt->world_position() - centre_before) < 1e-4f);
    CHECK(r501e_off_axes(*a, {Quat(), Quat::axis_angle(Vec3(0, 1, 0), 30.0f * kDeg2Rad)}) < 1e-3f);  // (no face sheared)
    CHECK(said.find("Joined 1 Blockout into 'Alpha'") != std::string::npos && said.find("scaled one went into its pieces' numbers") != std::string::npos);
    CHECK(said.find("scaled unevenly") == std::string::npos);
    ed.select_for_test(a->id, false);
    CHECK(!r501e_shows(ed, "scaled unevenly"));
  });

  test("blockout join (te): into a Blockout scaled 2 1 1 a turned piece comes in sheared as that Blockout's own are, and the join says so; the pieces that are not turned keep their world boxes", [] {
    Editor ed;
    r501e_open(ed);
    GameObject *a = r501_hand(ed, "Alpha");
    a->set_local_scale({2.0f, 1.0f, 1.0f});
    GameObject *pa = r501_box(ed, *a, "PA", {0, 0, 0}, {1, 2, 2});
    GameObject *own = r501_box(ed, *a, "Own turned", {0, 0, 2.0f}, {1, 1, 1});
    own->set_local_rotation(Quat::axis_angle(Vec3(0, 1, 0), 30.0f * kDeg2Rad));
    GameObject *b = r501_hand(ed, "Beta");
    b->set_world_position({2.0f, 0, 0});
    GameObject *pb = r501_box(ed, *b, "PB", {0, 0, 0}, {2, 2, 2});
    GameObject *pt = r501_box(ed, *b, "PT", {0, 0, 2.0f}, {1, 1, 1});
    pt->set_local_rotation(Quat::axis_angle(Vec3(0, 1, 0), 30.0f * kDeg2Rad));
    ed.mark_changed_for_test("test scene");
    r501e_wait(ed);
    const std::vector<GameObject *> straight{pa, pb};
    const std::vector<std::vector<Vec3>> before = r501e_all_corners(straight);
    ed.select_for_test(a->id, false);
    const size_t mark = r501_mark();
    ed.command("blockout join Beta");
    r501e_wait(ed);
    const std::string said = r501_log_since(mark);
    std::printf("    log: %s", said.c_str());
    std::printf("    PB's Scale after the join: %.3f %.3f %.3f (it was 1 1 1; the world box is %s)\n", pb->local().scale.x, pb->local().scale.y, pb->local().scale.z,
                r501e_worst_boxes(before, straight) < 1e-4f ? "kept" : "NOT kept");
    CHECK(r501_blockouts(ed) == 1);
    CHECK(r501e_worst_boxes(before, straight) < 1e-4f);
    CHECK(said.find("is scaled unevenly: turned pieces are sheared") != std::string::npos);
    CHECK(r501e_shows(ed, "scaled unevenly"));  // (round 496's status on the Blockout itself)
    const Mesh *m = r501e_mesh(*a);
    CHECK(m && r501e_problem(*m).empty());
  });

  test("blockout join (te): where the class materials differ no face changes its material - a piece without its own gets its old Blockout's, a piece's own stays, the active Blockout's settings win and the log names them", [] {
    Editor ed;
    r501e_open(ed);
    const char *const names[6] = {"Floor", "Walls", "Ceiling", "Outside", "Cut", "Solid"};
    std::string pa[6], pb[6];
    for (int k = 0; k < 6; k++) {
      pa[k] = r501e_material((std::string("A") + names[k]).c_str(), Vec3(0.9f, 0.1f + 0.1f * (float)k, 0.1f));
      pb[k] = r501e_material((std::string("B") + names[k]).c_str(), Vec3(0.1f, 0.2f + 0.1f * (float)k, 0.9f));
    }
    const std::string glass = r501e_material("BGlass", Vec3(0.5f, 0.9f, 0.9f));
    auto room = [&](GameObject &bo, const char *name) {
      GameObject *r = r501_box(ed, bo, name, {0, 1.3f, 0}, {4, 2.6f, 4});
      BlockoutPiece *p = r->get<BlockoutPiece>();
      p->room = true, p->wall = p->floor = p->ceiling = 0.2f;
      return r;
    };
    GameObject *a = r501_hand(ed, "Alpha");
    room(*a, "Room A");
    GameObject *b = r501_hand(ed, "Beta");
    b->set_world_position({4.4f, 0, 0});  // (the shells meet in the plane x = 2.2)
    room(*b, "Room B");
    GameObject *pillar = r501_box(ed, *b, "Pillar", {0.6f, 1.3f, 0.0f}, {1, 2.6f, 1});
    GameObject *glass_box = r501_box(ed, *b, "Glass box", {-1.0f, 0.5f, 1.2f}, {0.5f, 0.5f, 0.5f});
    glass_box->get<BlockoutPiece>()->material[kClassSolid] = glass;
    for (int k = 0; k < 6; k++) a->get<Blockout>()->material[k] = pa[k], b->get<Blockout>()->material[k] = pb[k];
    /* A Field Cell that differs but is fine enough for 0.2 m walls (0.05, not the 0.5 this test had first): on the grid path - every build without
     * Blender's libraries - Beta's mesh before the join is built by ITS cell, and at 0.5 its thin walls were not where the joined mesh (the active
     * Blockout's 0.1) has them, so 95 of 125 face samples could not be found again. The cell that wins is still checked below. */
    b->get<Blockout>()->cell = 0.05f;
    b->get<Blockout>()->texture_scale = a->get<Blockout>()->texture_scale + 2.0f;
    const float scale0 = a->get<Blockout>()->texture_scale;
    ed.mark_changed_for_test("test scene");
    r501e_wait(ed);
    ed.select_for_test(a->id, false);
    std::vector<R501Sample> samples = r501e_samples(*a), sb = r501e_samples(*b);
    samples.insert(samples.end(), sb.begin(), sb.end());
    std::set<std::string> used;
    for (const R501Sample &s : samples) used.insert(s.material);
    CHECK(used.size() >= 5);  // (the check below has something to tell apart)
    const size_t mark = r501_mark();
    ed.command("blockout join Beta");
    r501e_wait(ed);
    const std::string said = r501_log_since(mark);
    std::printf("    log: %s", said.c_str());
    const R501Found f = r501e_find(samples, *a);
    std::printf("    %zu face samples: %zu found, %zu with the same material, %zu not found (the shared wall's), %zu changed; %zu different materials in use\n", f.total, f.found, f.same, f.missing.size(),
                f.changed.size(), used.size());
    for (size_t i = 0; i < f.changed.size() && i < 5; i++) std::printf("      changed: %s\n", f.changed[i].c_str());
    /* Where did the faces that changed lie? The shells are 4.4 m wide (x -2.2..2.2 and 2.2..6.6) and 4.4 deep, 2.8 high from -0.2: the ones in the planes both Blockouts' outer faces
     * share (the top y = 2.8, the bottom y = -0.2, the sides z = +-2.2) are one flat face of the joined mesh, which has one material for the two Blockouts' faces it covers. */
    size_t in_shared_plane = 0;
    for (const Vec3 &p : f.changed_at) in_shared_plane += std::fabs(p.y - 2.8f) < 1e-3f || std::fabs(p.y + 0.2f) < 1e-3f || std::fabs(std::fabs(p.z) - 2.2f) < 1e-3f;
    std::printf("    %zu of the %zu changed faces lie in a plane the two Blockouts' outer faces share (merged into one face of the joined mesh)\n", in_shared_plane, f.changed.size());
    CHECK(in_shared_plane == f.changed.size());  // (nothing else changes: every room's own floor, walls and ceiling keep what they had)
    /* And only in one direction (round 501's second review: the shared planes hold nearly every outer face of both, so the check above alone would
     * also pass Alpha's faces turning to Beta's): every face that changed was Beta's - on Beta's side of the plane where the shells met. */
    for (const Vec3 &p : f.changed_at) CHECK(p.x > 2.2f - 1e-3f);
    /* Spec criterion 4 ("no face changes its material") is NOT met where the two Blockouts' outer faces lie in one plane: the joined mesh has ONE face there, which can carry one
     * material (round 482's rule: a face made of several pieces' faces in one plane takes the first piece's), so Beta's outside faces in the plane of Alpha's take Alpha's. The kernel
     * is not changed; the limit is reported to the owner (round 501) and stated in the changelog, the lesson and the ADR. So: every changed face lies in such a shared plane, and
     * every other face keeps its material (f.found == f.same + f.changed.size()). */
    CHECK(f.found == f.same + f.changed.size());
    CHECK(f.found > f.total * 8 / 10);
    for (const Vec3 &p : f.missing) CHECK(std::fabs(p.x - 2.2f) < 1e-3f);  // (only the faces that were the wall between: on the plane where the shells met)
    CHECK(glass_box->get<BlockoutPiece>()->material[kClassSolid] == glass && pillar->get<BlockoutPiece>()->material[kClassSolid] == pb[kClassSolid]);
    CHECK(a->get<Blockout>()->cell == 0.1f && a->get<Blockout>()->texture_scale == scale0 && a->get<Blockout>()->material[kClassWalls] == pa[kClassWalls]);
    CHECK(said.find("Field Cell") != std::string::npos && said.find("Texture Scale") != std::string::npos && said.find("class materials") != std::string::npos);
    CHECK(r501e_problem(*r501e_mesh(*a)).empty());
  });

  test("blockout join (te): the documented material limit - where the old Blockout's class material was none and the active one's is set those faces take the active one's, and the log says so; the other way round nothing changes", [] {
    for (int reversed = 0; reversed < 2; reversed++) {
      Editor ed;
      r501e_open(ed);
      const std::string walls = r501e_material("LimitWalls", Vec3(0.2f, 0.8f, 0.2f));
      GameObject *a = r501_hand(ed, "Alpha");
      GameObject *ra = r501_box(ed, *a, "Room A", {0, 1.3f, 0}, {4, 2.6f, 4});
      GameObject *b = r501_hand(ed, "Beta");
      b->set_world_position({4.4f, 0, 0});
      GameObject *rb = r501_box(ed, *b, "Room B", {0, 1.3f, 0}, {4, 2.6f, 4});
      for (GameObject *r : {ra, rb}) {
        BlockoutPiece *p = r->get<BlockoutPiece>();
        p->room = true, p->wall = p->floor = p->ceiling = 0.2f;
      }
      (reversed ? b : a)->get<Blockout>()->material[kClassWalls] = walls;  // (the other keeps none)
      ed.mark_changed_for_test("test scene");
      r501e_wait(ed);
      ed.select_for_test(a->id, false);
      std::vector<R501Sample> samples = r501e_samples(*a), sb = r501e_samples(*b);
      samples.insert(samples.end(), sb.begin(), sb.end());
      const size_t mark = r501_mark();
      ed.command("blockout join Beta");
      r501e_wait(ed);
      const std::string said = r501_log_since(mark);
      const R501Found f = r501e_find(samples, *a);
      std::printf("    %s: %zu of %zu found, %zu changed\n", reversed ? "Beta has the material, Alpha none" : "Alpha has the material, Beta none", f.found, f.total, f.changed.size());
      for (size_t i = 0; i < f.changed.size() && i < 3; i++) std::printf("      changed: %s\n", f.changed[i].c_str());
      if (reversed) {
        CHECK(f.changed.empty());  // (Beta's walls got Beta's material as an override: they keep it; Alpha's none stays none)
        CHECK(said.find("default material through their old Blockout") == std::string::npos);
      }
      else {
        CHECK(!f.changed.empty());  // (the limit: Beta's wall faces, default before, now Alpha's)
        for (const std::string &c : f.changed) CHECK(c.find("'' -> '" + walls + "'") == 0);
        CHECK(said.find("default material through their old Blockout") != std::string::npos);
      }
    }
  });

  test("blockout join (te): three Blockouts in a row join by name into one - all pieces, boxes kept, closed and valid, the volume of the three interiors less their two shared walls; one undo step brings all three back", [] {
    Editor ed;
    r501e_open(ed);
    R501Int a = r501e_interior(ed, "Alpha", {0, 0, 0}, false);
    R501Int b = r501e_interior(ed, "Beta", {4, 0, 0}, false);
    R501Int c = r501e_interior(ed, "Gamma", {-4, 0, 0}, false);
    ed.mark_changed_for_test("test scene");
    r501e_wait(ed);
    std::vector<GameObject *> pieces = r501e_pieces(*a.bo);
    for (GameObject *bo : {b.bo, c.bo})
      for (GameObject *p : r501e_pieces(*bo)) pieces.push_back(p);
    const std::vector<uint64_t> ids = r501e_ids(pieces), bos{a.bo->id, b.bo->id, c.bo->id};
    const std::vector<std::vector<Vec3>> before = r501e_all_corners(pieces);
    ed.select_for_test(a.bo->id, false);
    ed.command("blockout join Beta Gamma");
    r501e_wait(ed);
    GameObject *alpha = ed.scene().find(bos[0]);
    CHECK(alpha && r501_blockouts(ed) == 1 && r501e_pieces(*alpha).size() == 6);
    CHECK(r501e_worst_by_ids(ed, ids, before) < 1e-4f && r501e_unit_scales(r501e_by_ids(ed, ids)));
    const Mesh *m = alpha ? r501e_mesh(*alpha) : nullptr;
    CHECK(m && r501e_problem(*m).empty());
    if (m) {
      std::printf("    three interiors: volume %.4f (expected 42.672)\n", signed_volume(*m));
      CHECK(std::fabs(signed_volume(*m) - 42.672) < 0.03);
    }
    ed.command("undo");
    r501e_wait(ed);
    CHECK(r501_blockouts(ed) == 3 && ed.scene().find(bos[1]) && ed.scene().find(bos[2]));
    CHECK(r501e_worst_by_ids(ed, ids, before) < 1e-4f);
    for (const uint64_t id : bos) CHECK(ed.scene().find(id) && r501e_mesh(*ed.scene().find(id)) && r501e_problem(*r501e_mesh(*ed.scene().find(id))).empty());
  });

  test("blockout join (te): pieces under folders at any depth, with folders turned and moved, keep their world boxes and their folders; a Blockout nested in the active one joins into it", [] {
    Editor ed;
    r501e_open(ed);
    GameObject *a = r501_hand(ed, "Alpha");
    r501_box(ed, *a, "PA", {0, 0, 0}, {2, 2, 2});
    GameObject *b = r501_hand(ed, "Beta");
    b->set_world_position({2.0f, 0, 0});
    b->set_world_rotation(Quat::axis_angle(Vec3(0, 1, 0), 45.0f * kDeg2Rad));
    GameObject *folder = ed.scene().create("Folder", b);
    folder->set_local_position({0.5f, 0.2f, 0.5f});
    folder->set_local_rotation(Quat::axis_angle(Vec3(0, 1, 0), 90.0f * kDeg2Rad));
    GameObject *sub = ed.scene().create("Sub", folder);
    sub->set_local_position({0.0f, 0.0f, 1.0f});
    GameObject *deep = r501_box(ed, *sub, "Deep", {0, 0.5f, 1.5f}, {1, 1, 1});
    GameObject *flat = r501_box(ed, *b, "Flat", {0, 0, 0}, {2, 2, 2});
    GameObject *inner = r501_hand(ed, "Inner");  // (a Blockout in Alpha)
    ed.scene().set_parent(inner, a, -1, true);
    GameObject *pi = r501_box(ed, *inner, "PI", {0, 3.0f, 0}, {1, 1, 1});
    ed.mark_changed_for_test("test scene");
    r501e_wait(ed);
    const std::vector<GameObject *> moving{deep, flat, pi};
    const std::vector<std::vector<Vec3>> before = r501e_all_corners(moving);
    const uint64_t deep_id = deep->id;
    ed.select_for_test(a->id, false);
    ed.command("blockout join Beta Inner");
    r501e_wait(ed);
    CHECK(r501_blockouts(ed) == 1);
    CHECK(folder->parent == a && sub->parent == folder && deep->parent == sub && flat->parent == a && pi->parent == a);
    CHECK(r501e_worst_boxes(before, moving) < 1e-4f);
    CHECK(r501e_unit_scales({deep, flat, pi, folder, sub}));
    const Mesh *m = r501e_mesh(*a);
    CHECK(m && r501e_problem(*m).empty() && r501e_solid(*a, pi->world_position()));
    CHECK(ed.scene().find(deep_id) == deep);
  });

  /* ---- criterion 4: what is refused, and what is not ---- */
  test("blockout join (te): a baked Blockout in the selection is refused with 'Unbake first' and nothing changes - no object, no undo step; Play mode refuses too; names that are no Blockout say so", [] {
    Editor ed;
    r501e_open(ed);
    R501Int a = r501e_interior(ed, "Alpha", {0, 0, 0}, false);
    R501Int b = r501e_interior(ed, "Beta", {4, 0, 0}, false);
    R501Int c = r501e_interior(ed, "Gamma", {-4, 0, 0}, false);
    GameObject *cube = ed.scene().create("Cube");
    cube->add<MeshFilter>()->mesh = primitives::cube(1.0f);
    cube->add<MeshRenderer>();
    cube->set_world_position({0, 10, 0});
    ed.mark_changed_for_test("test scene");
    r501e_wait(ed);
    ed.select_for_test(c.bo->id, false);
    ed.command("blockout bake");
    r501_settle(ed, 3);
    CHECK(ed.scene().find(c.bo->id) && ed.scene().find(c.bo->id)->get<BlockoutBaked>());
    auto unchanged = [&](const char *what) {
      CHECK(r501_blockouts(ed) == 2 && ed.scene().find(a.bo->id) && ed.scene().find(b.bo->id) && r501e_pieces(*ed.scene().find(a.bo->id)).size() == 2 &&
            r501e_pieces(*ed.scene().find(b.bo->id)).size() == 2);
      if (r501_blockouts(ed) != 2) std::printf("    (%s changed the scene)\n", what);
    };
    const size_t depth = ed.undo_depth_for_test();
    /* The selection holds a baked object. */
    ed.select_for_test(a.bo->id, false);
    ed.select_for_test(c.bo->id, true);
    size_t mark = r501_mark();
    ed.command("blockout join");
    CHECK(r501_log_since(mark).find("Unbake first") != std::string::npos);
    unchanged("baked in the selection");
    /* ... and by name. */
    ed.select_for_test(a.bo->id, false);
    mark = r501_mark();
    ed.command("blockout join Beta Gamma");
    CHECK(r501_log_since(mark).find("Unbake first") != std::string::npos);
    unchanged("baked by name");
    CHECK(ed.undo_depth_for_test() == depth);
    /* Names that are not Blockouts: nothing is joined, the scene is as it was. */
    for (const char *line : {"blockout join Beta Nowhere", "blockout join Beta Cube"}) {
      mark = r501_mark();
      ed.command(line);
      const std::string said = r501_log_since(mark);
      std::printf("    '%s' -> %s", line, said.c_str());
      CHECK(said.find("there is no Blockout named") != std::string::npos);
      unchanged(line);
    }
    /* Nothing selected, or a plain mesh: asks for Blockouts. */
    ed.clear_selection_for_test();
    mark = r501_mark();
    ed.command("blockout join");
    CHECK(r501_log_since(mark).find("select the Blockouts to join") != std::string::npos);
    ed.select_for_test(cube->id, false);
    mark = r501_mark();
    ed.command("blockout join");
    CHECK(r501_log_since(mark).find("select the Blockouts to join") != std::string::npos);
    unchanged("nothing selected");
    CHECK(ed.undo_depth_for_test() == depth);
    /* Play mode. */
    ed.select_for_test(a.bo->id, false);
    ed.command("play");
    r501_settle(ed, 3);
    CHECK(ed.playing_for_test());
    mark = r501_mark();
    ed.command("blockout join Beta");
    CHECK(r501_log_since(mark).find("leave Play mode first") != std::string::npos);
    CHECK(r501_blockouts(ed) == 2);
    CHECK(!ed.blockout_join_available());
    ed.command("stop");
    r501_settle(ed, 3);
    CHECK(!ed.playing_for_test() && r501_blockouts(ed) == 2);
  });

  test("blockout join (te): the active object not a Blockout - named Blockouts still join into the first one named; names of pieces count as their Blockout; a piece selected then another's makes the last one's Blockout the active", [] {
    {
      Editor ed;
      r501e_open(ed);
      R501Int a = r501e_interior(ed, "Alpha", {0, 0, 0}, false);
      R501Int b = r501e_interior(ed, "Beta", {4, 0, 0}, false);
      GameObject *cube = ed.scene().create("Cube");
      cube->add<MeshFilter>()->mesh = primitives::cube(1.0f);
      cube->add<MeshRenderer>();
      ed.mark_changed_for_test("test scene");
      r501e_wait(ed);
      ed.select_for_test(cube->id, false);
      const size_t depth = ed.undo_depth_for_test();
      ed.command("blockout join Alpha Beta");
      r501e_wait(ed);
      CHECK(r501_blockouts(ed) == 1 && ed.scene().find_by_name("Alpha") && ed.scene().find_by_name("Beta") == nullptr && ed.scene().find_by_name("Cube"));
      CHECK(ed.undo_depth_for_test() == depth + 1);
    }
    {
      Editor ed;
      r501e_open(ed);
      R501Int a = r501e_interior(ed, "Alpha", {0, 0, 0}, false);
      R501Int b = r501e_interior(ed, "Beta", {4, 0, 0}, false);
      ed.mark_changed_for_test("test scene");
      r501e_wait(ed);
      ed.select_for_test(a.bo->id, false);
      ed.command("blockout join Beta_Shell");  // (Beta's piece named: its Blockout)
      r501e_wait(ed);
      CHECK(r501_blockouts(ed) == 1 && ed.scene().find_by_name("Beta") == nullptr);
    }
    {
      Editor ed;
      r501e_open(ed);
      R501Int a = r501e_interior(ed, "Alpha", {0, 0, 0}, false);
      R501Int b = r501e_interior(ed, "Beta", {4, 0, 0}, false);
      ed.mark_changed_for_test("test scene");
      r501e_wait(ed);
      ed.select_for_test(a.shell->id, false);
      ed.select_for_test(b.shell->id, true);  // (Beta's piece last: Beta is the active, Alpha joins it)
      ed.command("blockout join");
      r501e_wait(ed);
      CHECK(r501_blockouts(ed) == 1 && ed.scene().find_by_name("Beta") != nullptr && ed.scene().find_by_name("Alpha") == nullptr);
      CHECK(ed.scene().find_by_name("Beta") && r501e_pieces(*ed.scene().find_by_name("Beta")).size() == 4);
      CHECK(ed.active_object_for_test() && ed.active_object_for_test()->name == "Beta");  // (the result is selected)
    }
    {  // (the same name twice, and the active one named: each joined once)
      Editor ed;
      r501e_open(ed);
      R501Int a = r501e_interior(ed, "Alpha", {0, 0, 0}, false);
      R501Int b = r501e_interior(ed, "Beta", {4, 0, 0}, false);
      ed.mark_changed_for_test("test scene");
      r501e_wait(ed);
      ed.select_for_test(a.bo->id, false);
      const size_t mark = r501_mark();
      ed.command("blockout join Beta Beta Alpha");
      r501e_wait(ed);
      CHECK(r501_blockouts(ed) == 1 && r501e_pieces(*ed.scene().find_by_name("Alpha")).size() == 4);
      CHECK(r501_log_since(mark).find("Joined 1 Blockout into 'Alpha'") != std::string::npos);
    }
  });
}

namespace {

std::string r501e_tooltip(Editor &ed, const std::string &key) {
  Recti r;
  if (!ed.button_rect_for_test(key, r)) return "";
  const auto it = ed.ui_for_test().tooltip_texts.find(std::to_string(r.x) + "," + std::to_string(r.y) + "," + std::to_string(r.w) + "," + std::to_string(r.h));
  return it == ed.ui_for_test().tooltip_texts.end() ? "" : it->second;
}

void r501e_escape(Editor &ed) {
  platform::Event d, u;
  d.type = R501ET::KeyDown, d.key = platform::KEY_ESCAPE;
  u.type = R501ET::KeyUp, u.key = platform::KEY_ESCAPE;
  ed.step_frame_headless({d});
  ed.step_frame_headless({u});
  r501_settle(ed, 2);
}

bool r501e_has_button(Editor &ed, const std::string &key) {
  ed.record_buttons_for_test(true);
  r501_settle(ed, 3);
  Recti r;
  return ed.button_rect_for_test(key, r);
}

/* A Blockout of one Add box (2 m), moved to `at`. */
GameObject *r501e_slab(Editor &ed, const char *name, Vec3 at, Vec3 size = Vec3(2.0f)) {
  GameObject *g = r501_hand(ed, name);
  g->set_world_position(at);
  r501_box(ed, *g, (std::string(name) + "_Slab").c_str(), {0, 0, 0}, size);
  return g;
}

size_t r501e_total_pieces(Editor &ed) {
  size_t n = 0;
  ed.scene().for_each([&](GameObject &g) { n += g.get<BlockoutPiece>() != nullptr; });
  return n;
}

}  // namespace

static void tests_round501_ui_and_edges() {
  /* ---- criterion 5: the Inspector's row, the button, the Mesh menu ---- */
  test("blockout join (te): the Inspector says \"touches 'Name'\" and shows the button for flush, overlapping and a 0.5 mm gap, and shows neither for a 1.5 mm gap, 2 cm or 1 m", [] {
    Editor ed;
    GameObject *left, *right, *sa, *sb;
    r501_pair(ed, left, right, sa, sb, 1500, 2200);
    struct Gap {
      const char *what;
      float gap;
      bool touches;
    };
    const Gap gaps[] = {{"flush", 0.0f, true},         {"overlapping by 30 cm", -0.3f, true}, {"0.5 mm of air", 0.0005f, true},
                        {"1.5 mm of air", 0.0015f, false}, {"2 cm of air", 0.02f, false},         {"1 m of air", 1.0f, false}};
    for (const Gap &g : gaps) {
      right->set_world_position({2.1f + g.gap, 0.0f, 0.0f});
      ed.mark_changed_for_test("move");
      ed.select_for_test(left->id, false);
      const bool touches = ed.blockout_touches(*left, *right), back = ed.blockout_touches(*right, *left);
      const bool button = r501e_has_button(ed, "Join Blockouts"), text = r501e_has_button(ed, "shown:touches 'Right'");
      std::printf("    %-22s touches %d / %d, button %d, text %d\n", g.what, (int)touches, (int)back, (int)button, (int)text);
      CHECK(touches == g.touches && back == g.touches && button == g.touches && text == g.touches);
      ed.select_for_test(right->id, false);
      CHECK(r501e_has_button(ed, "shown:touches 'Left'") == g.touches);
    }
  });

  test("blockout join (te): what counts as touching - a cutter alone does not, a door alone does not, a room counts with its shell, a piece switched off or a Blockout switched off does not", [] {
    Editor ed;
    GameObject *left, *right, *sa, *sb;
    r501_pair(ed, left, right, sa, sb, 1500, 2200);
    BlockoutPiece *bp = sb->get<BlockoutPiece>();
    auto settle = [&] {
      ed.mark_changed_for_test("edit");
      r501_settle(ed, 3);
    };
    right->set_world_position({2.0f, 0.0f, 0.0f});  // (30 cm deep into Left)
    settle();
    CHECK(ed.blockout_touches(*left, *right));
    bp->operation = 1;  // (a cutter only: it overlaps Left but makes no matter of its own)
    settle();
    const bool cutter = ed.blockout_touches(*left, *right) || ed.blockout_touches(*right, *left);
    std::printf("    a Subtract piece alone, overlapping Left by 30 cm: touches %d (a joined Blockout WOULD be cut by it)\n", (int)cutter);
    CHECK(!cutter);
    bp->operation = 0, bp->opening = 1;  // (a door cut)
    settle();
    CHECK(!ed.blockout_touches(*left, *right));
    bp->opening = 0;
    settle();
    CHECK(ed.blockout_touches(*left, *right));
    /* A room of 2 m of air has a shell 0.2 m thick: at x = 2.3 its shell meets Left's wall (1.1), its air does not; at 2.31 neither. */
    bp->room = true, bp->wall = bp->floor = bp->ceiling = 0.2f;
    right->set_world_position({2.3f, 0.0f, 0.0f});
    settle();
    CHECK(ed.blockout_touches(*left, *right));
    right->set_world_position({2.31f, 0.0f, 0.0f});
    settle();
    CHECK(!ed.blockout_touches(*left, *right));
    bp->room = false;
    right->set_world_position({2.1f, 0.0f, 0.0f});
    settle();
    CHECK(ed.blockout_touches(*left, *right));
    bp->enabled = false;
    settle();
    CHECK(!ed.blockout_touches(*left, *right));
    bp->enabled = true;
    right->active = false;
    settle();
    CHECK(!ed.blockout_touches(*left, *right));
    right->active = true;
    settle();
    CHECK(ed.blockout_touches(*left, *right));
  });

  test("blockout join (te): the row names the first two Blockouts and 'and N more'", [] {
    Editor ed;
    GameObject *left, *right, *sa, *sb;
    r501_pair(ed, left, right, sa, sb, 1500, 2200);
    right->set_world_position({2.1f, 0, 0});
    GameObject *gamma = r501e_slab(ed, "Gamma", {-2.1f, 0, 0});
    GameObject *delta = r501e_slab(ed, "Delta", {0, 0, 2.0f});
    ed.mark_changed_for_test("test scene");
    ed.select_for_test(left->id, false);
    r501_settle(ed, 3);
    CHECK(r501e_has_button(ed, "shown:touches 'Right', 'Gamma' and 1 more"));
    delta->set_world_position({0, 0, 20.0f});
    ed.mark_changed_for_test("move");
    CHECK(r501e_has_button(ed, "shown:touches 'Right', 'Gamma'"));
    gamma->set_world_position({-20.0f, 0, 0});
    ed.mark_changed_for_test("move");
    CHECK(r501e_has_button(ed, "shown:touches 'Right'"));
  });

  test("blockout join (te): clicking the Join Blockouts button joins the selected Blockout with what touches it as one undo step, the row is gone after, and a selected piece shows no button but joins by the command", [] {
    Editor ed;
    GameObject *left, *right, *sa, *sb;
    r501_pair(ed, left, right, sa, sb, 1500, 2200);
    right->set_world_position({2.1f, 0, 0});
    ed.mark_changed_for_test("test scene");
    ed.select_for_test(sa->id, false);  // (a piece: the Blockout's Inspector is not showing)
    CHECK(!r501e_has_button(ed, "Join Blockouts"));
    CHECK(ed.blockout_join_available());
    ed.select_for_test(right->id, false);
    r501_settle(ed, 3);
    const size_t steps = ed.undo_depth_for_test();
    CHECK(r501_click_key(ed, "Join Blockouts"));
    r501e_wait(ed);
    GameObject *now = ed.scene().find_by_name("Right");
    CHECK(now && r501_blockouts(ed) == 1 && ed.scene().find_by_name("Left") == nullptr && r501e_pieces(*now).size() == 2);  // (the selected one takes the other)
    CHECK(ed.undo_depth_for_test() == steps + 1);
    CHECK(!r501e_has_button(ed, "Join Blockouts") && !r501e_has_button(ed, "shown:touches 'Left'"));
    CHECK(now && r501e_mesh(*now) && r501e_problem(*r501e_mesh(*now)).empty());
    ed.command("undo");
    r501e_wait(ed);
    CHECK(r501_blockouts(ed) == 2 && ed.scene().find_by_name("Left") && ed.scene().find_by_name("Right"));
    /* A piece selected: the command joins its Blockout with what touches it. */
    ed.select_for_test(ed.scene().find_by_name("SlabA")->id, false);
    ed.command("blockout join");
    r501e_wait(ed);
    CHECK(r501_blockouts(ed) == 1);
  });

  test("blockout join (te): the Mesh menu's Join Blockouts is on for two Blockouts or one that touches another, off for one alone and in Play, says why, and a click joins", [] {
    Editor ed;
    GameObject *left, *right, *sa, *sb;
    r501_pair(ed, left, right, sa, sb, 1600, 1000);
    right->set_world_position({12.0f, 0, 0});  // (apart)
    ed.mark_changed_for_test("test scene");
    r501e_wait(ed);
    ed.select_for_test(left->id, false);
    r501_settle(ed, 3);
    CHECK(!ed.blockout_join_available());
    size_t depth = ed.undo_depth_for_test();
    CHECK(r501e_menu(ed, {"menubar:Mesh"}));
    r501_settle(ed, 2);
    const std::string off = r501e_tooltip(ed, "menu:Join Blockouts");
    Recti row;
    CHECK(ed.button_rect_for_test("menu:Join Blockouts", row));
    std::printf("    off, tooltip: '%.60s'\n", off.c_str());
    CHECK(off.rfind("Off: select two or more Blockouts", 0) == 0);  // (the Draw submenu's tooltip is filed under the previous row's rect: see test_blockout.cpp's finding for Unbake)
    r501e_click(ed, row.x + row.w / 2, row.y + row.h / 2);
    CHECK(ed.undo_depth_for_test() == depth && r501_blockouts(ed) == 2);
    r501e_escape(ed);
    /* Two selected, apart: on, and it joins (the log says they did not touch). */
    ed.select_for_test(left->id, false);
    ed.select_for_test(right->id, true);
    r501_settle(ed, 3);
    CHECK(ed.blockout_join_available());
    CHECK(r501e_menu(ed, {"menubar:Mesh"}));
    r501_settle(ed, 2);
    const std::string on = r501e_tooltip(ed, "menu:Join Blockouts");
    std::printf("    on, tooltip: '%.60s'\n", on.c_str());
    CHECK(on.rfind("The selected Blockouts", 0) == 0);
    const size_t mark = r501_mark();
    CHECK(ed.button_rect_for_test("menu:Join Blockouts", row));
    r501e_click(ed, row.x + row.w / 2, row.y + row.h / 2);
    r501e_wait(ed);
    CHECK(r501_blockouts(ed) == 1 && ed.undo_depth_for_test() == depth + 1);
    CHECK(r501_log_since(mark).find("did not touch") != std::string::npos);
    /* One that touches another: on. */
    ed.command("undo");
    r501e_wait(ed);
    ed.scene().find_by_name("Right")->set_world_position({2.1f, 0, 0});
    ed.mark_changed_for_test("move");
    ed.select_for_test(ed.scene().find_by_name("Left")->id, false);
    r501_settle(ed, 3);
    CHECK(ed.blockout_join_available());
    depth = ed.undo_depth_for_test();
    CHECK(r501e_menu(ed, {"menubar:Mesh", "menu:Join Blockouts"}));
    r501e_wait(ed);
    CHECK(r501_blockouts(ed) == 1 && ed.undo_depth_for_test() == depth + 1);
    /* Play: off. */
    ed.command("undo");
    r501e_wait(ed);
    ed.select_for_test(ed.scene().find_by_name("Left")->id, false);
    ed.select_for_test(ed.scene().find_by_name("Right")->id, true);
    CHECK(ed.blockout_join_available());
    ed.command("play");
    r501_settle(ed, 3);
    CHECK(ed.playing_for_test() && !ed.blockout_join_available());
    ed.command("stop");
    r501_settle(ed, 3);
  });

  /* ---- edge cases ---- */
  test("blockout join (te): a Blockout with no pieces joins either way - nothing lost, nothing added", [] {
    {
      Editor ed;
      r501e_open(ed);
      R501Int a = r501e_interior(ed, "Alpha", {0, 0, 0}, true);
      GameObject *empty = r501_hand(ed, "Empty");
      empty->set_world_position({4, 0, 0});
      ed.mark_changed_for_test("test scene");
      r501e_wait(ed);
      const Mesh before = *r501e_mesh(*a.bo);
      ed.select_for_test(a.bo->id, false);
      const size_t mark = r501_mark();
      ed.command("blockout join Empty");
      r501e_wait(ed);
      CHECK(r501_blockouts(ed) == 1 && ed.scene().find_by_name("Empty") == nullptr && r501e_pieces(*a.bo).size() == 3);
      CHECK(r501e_mesh(*a.bo) && r501e_same_mesh(before, *r501e_mesh(*a.bo)));
      CHECK(r501_log_since(mark).find("Joined 1 Blockout into 'Alpha': 0 pieces moved") != std::string::npos);
      ed.command("undo");
      r501e_wait(ed);
      CHECK(r501_blockouts(ed) == 2 && ed.scene().find_by_name("Empty"));
    }
    {  // (the active one empty)
      Editor ed;
      r501e_open(ed);
      GameObject *empty = r501_hand(ed, "Alpha");
      R501Int b = r501e_interior(ed, "Beta", {4, 0, 0}, false);
      ed.mark_changed_for_test("test scene");
      r501e_wait(ed);
      ed.select_for_test(empty->id, false);
      ed.command("blockout join Beta");
      r501e_wait(ed);
      GameObject *alpha = ed.scene().find_by_name("Alpha");
      CHECK(alpha && r501_blockouts(ed) == 1 && r501e_pieces(*alpha).size() == 2);
      const Mesh *m = alpha ? r501e_mesh(*alpha) : nullptr;
      CHECK(m && r501e_problem(*m).empty() && r501e_solid(*alpha, {6.0f, 0, 0}) && !r501e_solid(*alpha, {4.0f, 0, 0}));
    }
  });

  test("blockout join (te): pieces switched off (the piece's Enabled, or the object) keep their state through a join and come back where they were when switched on", [] {
    Editor ed;
    r501e_open(ed);
    R501Int a = r501e_interior(ed, "Alpha", {0, 0, 0}, false);
    R501Int b = r501e_interior(ed, "Beta", {4, 0, 0}, false);
    GameObject *off = r501_box(ed, *b.bo, "Off_Box", {0, 3.0f, 0}, {1, 1, 1});
    off->get<BlockoutPiece>()->enabled = false;
    GameObject *hidden = r501_box(ed, *b.bo, "Hidden_Box", {0, 5.0f, 0}, {1, 1, 1});
    hidden->active = false;
    ed.mark_changed_for_test("test scene");
    r501e_wait(ed);
    std::vector<GameObject *> pieces = r501e_pieces(*a.bo);
    for (GameObject *p : r501e_pieces(*b.bo)) pieces.push_back(p);
    const std::vector<std::vector<Vec3>> before = r501e_all_corners(pieces);
    ed.select_for_test(a.bo->id, false);
    ed.command("blockout join Beta");
    r501e_wait(ed);
    CHECK(r501_blockouts(ed) == 1 && r501e_pieces(*a.bo).size() == 6);
    CHECK(off->parent == a.bo && hidden->parent == a.bo && !off->get<BlockoutPiece>()->enabled && !hidden->active);
    CHECK(r501e_worst_boxes(before, pieces) < 1e-4f);
    CHECK(r501e_matches(*a.bo) && !r501e_solid(*a.bo, {4.0f, 3.0f, 0}) && !r501e_solid(*a.bo, {4.0f, 5.0f, 0}));
    off->get<BlockoutPiece>()->enabled = true;
    ed.mark_changed_for_test("enable");
    r501e_wait(ed);
    CHECK(r501e_solid(*a.bo, {4.0f, 3.0f, 0}) && !r501e_solid(*a.bo, {4.0f, 5.0f, 0}));
    hidden->active = true;
    ed.mark_changed_for_test("show");
    r501e_wait(ed);
    CHECK(r501e_solid(*a.bo, {4.0f, 5.0f, 0}) && r501e_problem(*r501e_mesh(*a.bo)).empty());
  });

  test("blockout join (te): a mesh piece comes along - its source object stays where it is and the joined Blockout is as solid as the two were", [] {
    Editor ed;
    r501e_open(ed);
    GameObject *source = ed.scene().create("Source");
    source->add<MeshFilter>()->mesh = primitives::cube(1.0f);
    source->add<MeshRenderer>();
    source->set_world_position({0, 40.0f, 0});
    GameObject *a = r501e_slab(ed, "Alpha", {0, 0, 0});
    GameObject *b = r501_hand(ed, "Beta");
    b->set_world_position({10.0f, 0, 0});
    GameObject *mp = r501_box(ed, *b, "Mesh_Piece", {0, 0, 0}, {1, 1, 1});
    mp->get<BlockoutPiece>()->shape = (int)blockout::Shape::Mesh;
    mp->get<BlockoutPiece>()->mesh_object = "Source";
    ed.mark_changed_for_test("test scene");
    r501e_wait(ed);
    const size_t faces_before = r501e_mesh(*b) ? r501e_mesh(*b)->face_count() : 0;
    std::printf("    Beta's mesh piece built %zu faces before the join\n", faces_before);
    std::vector<Vec3> probes;
    std::vector<char> expect;
    for (int i = 0; i < 9; i++)
      for (int j = 0; j < 5; j++)
        for (int k = 0; k < 5; k++) {
          const Vec3 p(8.0f + 0.5f * (float)i + 0.03f, -1.0f + 0.5f * (float)j + 0.07f, -1.0f + 0.5f * (float)k + 0.05f);
          probes.push_back(p);
          expect.push_back(r501e_solid(*b, p));
        }
    ed.select_for_test(a->id, false);
    ed.command("blockout join Beta");
    r501e_wait(ed);
    CHECK(r501_blockouts(ed) == 1 && mp->parent == a && source->parent == nullptr && source->world_position().y == 40.0f);
    size_t wrong = 0;
    for (size_t i = 0; i < probes.size(); i++) wrong += r501e_solid(*a, probes[i]) != (expect[i] != 0);
    std::printf("    %zu of %zu probes differ from Beta's own mesh\n", wrong, probes.size());
    CHECK(wrong == 0);
    CHECK(r501e_mesh(*a) && r501e_problem(*r501e_mesh(*a)).empty() && r501e_matches(*a));
  });

  test("blockout join (te): huge and tiny scales - 1 km boxes and 2 cm boxes meeting flush join, keep their boxes and make a valid mesh", [] {
    for (const float s : {1000.0f, 0.02f}) {
      Editor ed;
      r501e_open(ed);
      GameObject *a = r501e_slab(ed, "Alpha", {0, 0, 0}, Vec3(s));
      GameObject *b = r501e_slab(ed, "Beta", {s, 0, 0}, Vec3(s));
      ed.mark_changed_for_test("test scene");
      r501e_wait(ed);
      std::vector<GameObject *> pieces = r501e_pieces(*a);
      for (GameObject *p : r501e_pieces(*b)) pieces.push_back(p);
      const std::vector<std::vector<Vec3>> before = r501e_all_corners(pieces);
      CHECK(ed.blockout_touches(*a, *b));
      ed.select_for_test(a->id, false);
      ed.command("blockout join Beta");
      r501e_wait(ed);
      const float moved = r501e_worst_boxes(before, pieces);
      const Mesh *m = r501e_mesh(*a);
      const std::string problem = m ? r501e_problem(*m) : "no mesh";
      std::printf("    boxes of %g m: boxes moved %.2e, mesh '%s', %zu faces\n", s, moved, problem.c_str(), m ? m->face_count() : (size_t)0);
      CHECK(r501_blockouts(ed) == 1 && r501e_pieces(*a).size() == 2);
      CHECK(moved < std::max(1e-4f, s * 1e-6f));  // (a float has 7 digits: at 1500 m a corner is good to about 1e-4)
      CHECK(problem.empty());
      CHECK(r501e_unit_scales(pieces));
      if (s > 1.0f && blockout::crisp_available()) CHECK(r501e_solid(*a, {0, 0, 0}) && r501e_solid(*a, {s, 0, 0}) && !r501e_solid(*a, {0, 0, s * 2.0f}));
    }
  });

  test("blockout join (te): NaN in a piece's position or in a Blockout's position leaves a valid scene - nothing lost, a finite mesh, and an undo brings the Blockouts back", [] {
    for (int what = 0; what < 2; what++) {
      Editor ed;
      r501e_open(ed);
      R501Int a = r501e_interior(ed, "Alpha", {0, 0, 0}, false);
      R501Int b = r501e_interior(ed, "Beta", {4, 0, 0}, false);
      ed.mark_changed_for_test("test scene");
      r501e_wait(ed);
      const uint64_t alpha_id = a.bo->id, beta_id = b.bo->id;
      if (what == 0) b.air->set_local_position({kNaN, 0.0f, 0.0f});
      else b.bo->set_world_position({kNaN, 0.0f, 0.0f});
      ed.mark_changed_for_test("NaN");
      r501_settle(ed, 3);
      ed.select_for_test(a.bo->id, false);
      const size_t mark = r501_mark();
      ed.command("blockout join Beta");
      r501e_wait(ed);
      const std::string said = r501_log_since(mark);
      GameObject *alpha = ed.scene().find(alpha_id);
      CHECK(alpha != nullptr);
      if (!alpha) continue;
      const Mesh *m = r501e_mesh(*alpha);
      const std::string problem = m ? r501e_problem(*m) : "no mesh";
      std::printf("    NaN in %s: %zu Blockouts, %zu pieces, Alpha's mesh '%s'\n", what == 0 ? "a piece" : "the Blockout", r501_blockouts(ed), r501e_total_pieces(ed), problem.c_str());
      CHECK(problem.empty());
      CHECK(r501e_total_pieces(ed) == 4);  // (no piece lost or made)
      CHECK(r501_blockouts(ed) == 1 || said.find("Join Blockouts") != std::string::npos);  // (joined, or refused with a word)
      ed.command("undo");
      r501e_wait(ed);
      CHECK(r501_blockouts(ed) == 2 && ed.scene().find(beta_id) && ed.scene().find(alpha_id));
    }
  });

  test("blockout join (te): the active Blockout's modifier stack stays and the other's is lost with its object, said in the log; undo brings both stacks back", [] {
    Editor ed;
    r501e_open(ed);
    R501Int a = r501e_interior(ed, "Alpha", {0, 0, 0}, false);
    R501Int b = r501e_interior(ed, "Beta", {4, 0, 0}, false);
    ed.mark_changed_for_test("test scene");
    r501e_wait(ed);
    ed.select_for_test(a.bo->id, false);
    ed.command("component MirrorModifier");
    ed.select_for_test(b.bo->id, false);
    ed.command("component BevelModifier");
    r501e_wait(ed);
    r501_settle(ed, 3);
    CHECK(a.bo->get<MirrorModifier>() && b.bo->get<BevelModifier>());
    const uint64_t alpha_id = a.bo->id, beta_id = b.bo->id;
    ed.select_for_test(a.bo->id, false);
    const size_t mark = r501_mark();
    ed.command("blockout join Beta");
    r501e_wait(ed);
    r501_settle(ed, 3);
    GameObject *alpha = ed.scene().find(alpha_id);
    CHECK(alpha && r501_blockouts(ed) == 1 && alpha->get<MirrorModifier>() && !alpha->get<BevelModifier>());
    const std::string said = r501_log_since(mark);
    std::printf("    log: %s", said.c_str());
    CHECK(said.find("1 modifier(s) on the removed Blockouts are gone") != std::string::npos);
    const Mesh *shown = alpha ? alpha->evaluated_mesh() : nullptr;
    CHECK(shown != nullptr);
    if (shown) {
      std::string why;
      CHECK(structurally_valid(*shown, &why));
    }
    ed.command("undo");
    r501e_wait(ed);
    r501_settle(ed, 3);
    GameObject *beta = ed.scene().find(beta_id);
    alpha = ed.scene().find(alpha_id);
    CHECK(beta && alpha && r501_blockouts(ed) == 2 && beta->get<BevelModifier>() && alpha->get<MirrorModifier>() && !alpha->get<BevelModifier>());
  });

  test("blockout join (te): an undo right after a join, while the builds run on workers, brings both Blockouts back with the meshes they had; redo and undo again, with no wait between, end in a consistent state", [] {
    Editor ed;
    r501e_open(ed);
    ed.set_async_jobs_for_test(true);
    R501Int a = r501e_interior(ed, "Alpha", {0, 0, 0}, true);
    R501Int b = r501e_interior(ed, "Beta", {4, 0, 0}, false);
    ed.mark_changed_for_test("test scene");
    r501e_wait(ed);
    const uint64_t alpha_id = a.bo->id, beta_id = b.bo->id;
    const Mesh alpha_mesh = *r501e_mesh(*a.bo), beta_mesh = *r501e_mesh(*b.bo);
    std::vector<GameObject *> pieces = r501e_pieces(*a.bo);
    for (GameObject *p : r501e_pieces(*b.bo)) pieces.push_back(p);
    const std::vector<uint64_t> ids = r501e_ids(pieces);
    const std::vector<std::vector<Vec3>> before = r501e_all_corners(pieces);
    ed.select_for_test(a.bo->id, false);
    ed.command("blockout join Beta");
    ed.command("undo");  // (no wait: the build the join started is running or has landed)
    r501e_wait(ed);
    GameObject *alpha = ed.scene().find(alpha_id), *beta = ed.scene().find(beta_id);
    CHECK(alpha && beta && r501_blockouts(ed) == 2);
    CHECK(alpha && r501e_mesh(*alpha) && r501e_same_mesh(alpha_mesh, *r501e_mesh(*alpha)));
    CHECK(beta && r501e_mesh(*beta) && r501e_same_mesh(beta_mesh, *r501e_mesh(*beta)));
    CHECK(r501e_worst_by_ids(ed, ids, before) < 1e-4f);
    for (int round = 0; round < 3; round++) {
      ed.command("redo");
      ed.command("undo");
      ed.command("redo");
    }
    r501e_wait(ed);
    alpha = ed.scene().find(alpha_id);
    CHECK(alpha && r501_blockouts(ed) == 1 && r501e_pieces(*alpha).size() == 5);
    CHECK(alpha && r501e_mesh(*alpha) && r501e_matches(*alpha) && r501e_problem(*r501e_mesh(*alpha)).empty());
    CHECK(r501e_worst_by_ids(ed, ids, before) < 1e-4f);
    ed.command("undo");
    r501e_wait(ed);
    CHECK(r501_blockouts(ed) == 2 && ed.scene().find(beta_id) && r501e_matches(*ed.scene().find(beta_id)) && r501e_matches(*ed.scene().find(alpha_id)));
  });
}

void tests_round501_engineer() {
  tests_round501_snap();
  tests_round501_join();
  tests_round501_ui_and_edges();
}

/* ===================================================================== */
/* Second pass (round 501): the review's findings                         */
/* ===================================================================== */

namespace {

/* A piece's world matrix compared number by number, to compare before and after. */
float r501_matrix_diff(const Mat4 &a, const Mat4 &b) {
  float worst = 0.0f;
  for (int i = 0; i < 16; i++) worst = std::max(worst, std::fabs(a.m[i] - b.m[i]));
  return worst;
}

BlockoutPiece *r501_bp(GameObject *g) { return g->get<BlockoutPiece>(); }

bool r501_scale_is(const GameObject &g, Vec3 want) { return g.local().scale.x == want.x && g.local().scale.y == want.y && g.local().scale.z == want.z; }

}  // namespace

static void tests_round501_second() {
  test("blockout join (2nd): a mirrored wedge and a mirrored stairs piece keep their world matrix - the mirror is not flipped to another axis", [] {
    const Vec3 mirrors[3] = {Vec3(1, -1, 1), Vec3(-1, -1, 1), Vec3(-1, 1, 1)};
    for (const Vec3 &mirror : mirrors)
      for (const int shape : {3, 4}) {  // (a wedge, stairs)
        Editor ed;
        ed.init_headless(1000, 700);
        r501_empty(ed);
        GameObject *a = r501_hand(ed, "Alpha");
        r501_box(ed, *a, "PA", {0, 0, 0}, {2, 2, 2});
        GameObject *b = r501_hand(ed, "Beta");
        b->set_world_position({2.0f, 0, 0});
        GameObject *w = r501_box(ed, *b, "Mirrored", {0.3f, 0.2f, 0.4f}, {1, 2, 1.5f});
        r501_bp(w)->shape = shape;
        w->set_local_rotation(Quat::axis_angle(Vec3(0, 1, 0), 40.0f * kDeg2Rad));
        w->set_local_scale(mirror);
        ed.mark_changed_for_test("test scene");
        r501_settle(ed, 3);
        const Mat4 before = w->world_matrix();
        const std::vector<Vec3> corners = r501_corners(*w);
        ed.command("blockout join Alpha, Beta");
        r501_settle(ed, 3);
        CHECK(r501_blockouts(ed) == 1 && w->parent == a);
        CHECK(r501_matrix_diff(before, w->world_matrix()) < 1e-4f);  // (the same solid: the same matrix, so every probe point inside it is inside it still)
        CHECK(r501_worst(corners, r501_corners(*w)) < 1e-4f);
      }
  });

  test("blockout join (2nd): the active Blockout scaled 2 2 2, or under a parent at 0.01 or 100 - the pieces that come in keep their world boxes and wall thickness, Scale 1 1 1, the size in their numbers", [] {
    struct Case { float own, parent; };
    for (const Case c : {Case{2.0f, 1.0f}, Case{1.0f, 0.01f}, Case{1.0f, 100.0f}}) {
      Editor ed;
      ed.init_headless(1000, 700);
      r501_empty(ed);
      GameObject *root = ed.scene().create("Root");
      root->set_local_scale(Vec3(c.parent));
      GameObject *a = r501_hand(ed, "Alpha");
      ed.scene().set_parent(a, root, -1, false);
      a->set_local_scale(Vec3(c.own));
      const float k = c.own * c.parent;  // (the active Blockout's world scale: one number is k metres)
      r501_box(ed, *a, "PA", {0, 0, 0}, Vec3(2.0f / k));
      GameObject *b = r501_hand(ed, "Beta");  // (at the root: world scale 1)
      b->set_world_position({2.0f, 0, 0});
      GameObject *pb = r501_box(ed, *b, "PB", {0, 0, 0}, {2, 2, 2});
      GameObject *room = r501_box(ed, *b, "RoomB", {0, 3.0f, 0}, {2, 2, 2});
      BlockoutPiece *rp = r501_bp(room);
      rp->room = true, rp->wall = 0.2f, rp->floor = 0.3f, rp->ceiling = 0.1f;
      GameObject *ball = r501_box(ed, *b, "Ball", {0, -3.0f, 0}, {1, 1, 1});
      r501_bp(ball)->blend = 0.1f;
      ed.mark_changed_for_test("test scene");
      r501_settle(ed, 3);
      std::vector<GameObject *> ps{pb, room, ball};
      std::vector<std::vector<Vec3>> before;
      for (GameObject *p : ps) before.push_back(r501_corners(*p));
      ed.command("blockout join Alpha, Beta");
      r501_settle(ed, 3);
      CHECK(r501_blockouts(ed) == 1);
      for (size_t i = 0; i < ps.size(); i++) CHECK(r501_worst(before[i], r501_corners(*ps[i])) < 1e-4f * std::max(1.0f, c.parent));
      for (GameObject *p : ps) CHECK(r501_scale_is(*p, Vec3(1.0f)));
      CHECK_NEAR(r501_bp(pb)->size.x * k, 2.0f, 1e-4f * std::max(1.0f, k));
      CHECK_NEAR(rp->wall * k, 0.2f, 1e-5f * std::max(1.0f, k));  // (the world thickness is what it was)
      CHECK_NEAR(rp->floor * k, 0.3f, 1e-5f * std::max(1.0f, k));
      CHECK_NEAR(rp->ceiling * k, 0.1f, 1e-5f * std::max(1.0f, k));
      CHECK_NEAR(r501_bp(ball)->blend * k, 0.1f, 1e-5f * std::max(1.0f, k));
      CHECK(a->local().scale.x == c.own);  // (the active Blockout's own Scale is not touched)
    }
  });

  test("blockout join (2nd): the active Blockout scaled 2 1 1 - a piece square to its axes keeps its box and Scale 1 1 1; a piece turned against it is sheared as its own are, and the log says so", [] {
    Editor ed;
    ed.init_headless(1000, 700);
    r501_empty(ed);
    GameObject *a = r501_hand(ed, "Alpha");
    a->set_local_scale(Vec3(2.0f, 1.0f, 1.0f));
    r501_box(ed, *a, "PA", {0, 0, 0}, {1, 2, 2});
    GameObject *b = r501_hand(ed, "Beta");
    b->set_world_position({2.0f, 0, 0});
    GameObject *square = r501_box(ed, *b, "Square", {0, 0, 0}, {2, 2, 2});
    GameObject *turned = r501_box(ed, *b, "Turned", {0, 3.0f, 0}, {1, 1, 1});
    turned->set_local_rotation(Quat::axis_angle(Vec3(0, 1, 0), 30.0f * kDeg2Rad));
    ed.mark_changed_for_test("test scene");
    r501_settle(ed, 3);
    const std::vector<Vec3> before = r501_corners(*square);
    const size_t mark = r501_mark();
    ed.command("blockout join Alpha, Beta");
    r501_settle(ed, 3);
    CHECK(r501_blockouts(ed) == 1);
    CHECK(r501_worst(before, r501_corners(*square)) < 1e-4f && r501_scale_is(*square, Vec3(1.0f)));
    CHECK(r501_log_since(mark).find("is scaled unevenly: turned pieces are sheared") != std::string::npos);
    CHECK(a->local().scale.x == 2.0f && a->local().scale.y == 1.0f);
  });

  test("blockout join (2nd): a Blockout with a zero scale axis is refused; a piece at NaN does not crash, spreads no NaN and the log says so", [] {
    Editor ed;
    ed.init_headless(1000, 700);
    r501_empty(ed);
    GameObject *a = r501_hand(ed, "Alpha");
    r501_box(ed, *a, "PA", {0, 0, 0}, {2, 2, 2});
    GameObject *b = r501_hand(ed, "Beta");
    b->set_world_position({2.0f, 0, 0});
    GameObject *good = r501_box(ed, *b, "Good", {0, 0, 0}, {2, 2, 2});
    GameObject *bad = r501_box(ed, *b, "Bad", {0, 3.0f, 0}, {1, 1, 1});
    ed.mark_changed_for_test("test scene");
    r501_settle(ed, 3);
    a->set_local_scale(Vec3(0.0f, 1.0f, 1.0f));
    size_t mark = r501_mark();
    ed.command("blockout join Alpha, Beta");
    CHECK(r501_log_since(mark).find("zero or not finite") != std::string::npos);
    CHECK(r501_blockouts(ed) == 2);
    a->set_local_scale(Vec3(1.0f));
    const std::vector<Vec3> before = r501_corners(*good);
    bad->set_local_position(Vec3(std::nanf(""), 0.0f, 0.0f));
    mark = r501_mark();
    ed.command("blockout join Alpha, Beta");
    r501_settle(ed, 3);
    CHECK(r501_blockouts(ed) == 1 && good->parent == a && bad->parent == a);
    CHECK(r501_worst(before, r501_corners(*good)) < 1e-4f);
    CHECK(r501_log_since(mark).find("not finite") != std::string::npos);
  });

  test("blockout join (2nd): refused in Edit Mode; the log names the other components that go with the removed Blockout", [] {
    Editor ed;
    ed.init_headless(1000, 700);
    r501_empty(ed);
    GameObject *a = r501_hand(ed, "Alpha");
    r501_box(ed, *a, "PA", {0, 0, 0}, {2, 2, 2});
    GameObject *b = r501_hand(ed, "Beta");
    b->set_world_position({2.0f, 0, 0});
    r501_box(ed, *b, "PB", {0, 0, 0}, {2, 2, 2});
    b->add<ColliderComponent>();
    ed.mark_changed_for_test("test scene");
    r501_settle(ed, 3);
    ed.command("create Cube");
    ed.command("edit face");
    size_t mark = r501_mark();
    ed.command("blockout join Alpha, Beta");
    CHECK(r501_log_since(mark).find("Edit Mode") != std::string::npos);
    CHECK(r501_blockouts(ed) == 2);
    ed.command("edit off");
    mark = r501_mark();
    ed.command("blockout join Alpha, Beta");
    r501_settle(ed, 3);
    CHECK(r501_blockouts(ed) == 1);
    CHECK(r501_log_since(mark).find("Collider") != std::string::npos);
  });

  test("blockout join (2nd): names with spaces and commas - the default second Blockout's name \"Blockout (1)\"; a space-separated list still works", [] {
    Editor ed;
    ed.init_headless(1000, 700);
    r501_empty(ed);
    GameObject *a = r501_hand(ed, "Blockout");
    r501_box(ed, *a, "PA", {0, 0, 0}, {2, 2, 2});
    GameObject *b = r501_hand(ed, "Blockout (1)");
    b->set_world_position({2.0f, 0, 0});
    r501_box(ed, *b, "PB", {0, 0, 0}, {2, 2, 2});
    GameObject *c = r501_hand(ed, "Gamma");
    c->set_world_position({4.0f, 0, 0});
    r501_box(ed, *c, "PC", {0, 0, 0}, {2, 2, 2});
    ed.mark_changed_for_test("test scene");
    r501_settle(ed, 3);
    ed.command("blockout join Blockout, Blockout (1)");
    r501_settle(ed, 3);
    CHECK(r501_blockouts(ed) == 2 && ed.scene().find_by_name("Blockout (1)") == nullptr);
    ed.command("blockout join Blockout Gamma");  // (the old, space-separated form)
    r501_settle(ed, 3);
    CHECK(r501_blockouts(ed) == 1);
  });

  test("blockout join (2nd): a Blockout standing in a room's air is no touch; on its floor or against its wall is; a Blockout of cutters alone is no touch", [] {
    Editor ed;
    ed.init_headless(1000, 700);
    r501_empty(ed);
    GameObject *room = r501_hand(ed, "Room");
    GameObject *rp = r501_box(ed, *room, "Air", {0, 0, 0}, {4, 3, 4});
    r501_bp(rp)->room = true, r501_bp(rp)->wall = r501_bp(rp)->floor = r501_bp(rp)->ceiling = 0.2f;
    GameObject *table = r501_hand(ed, "Table");
    r501_box(ed, *table, "Top", {0, 0, 0}, {1, 1, 1});
    GameObject *cutter = r501_hand(ed, "Cutter");
    cutter->set_world_position({30.0f, 0, 0});
    r501_box(ed, *cutter, "Hole", {0, 0, 0}, {1, 1, 1}, 1);
    ed.mark_changed_for_test("test scene");
    auto at = [&](Vec3 p) {
      table->set_world_position(p);
      r501_settle(ed, 2);
      return ed.blockout_touches(*room, *table) && ed.blockout_touches(*table, *room);
    };
    CHECK(!at({0.0f, 0.0f, 0.0f}));  // (floating in the air)
    CHECK(at({0.0f, -1.0f, 0.0f}));  // (its bottom at y = -1.5: the floor's top)
    CHECK(at({1.5f, 0.0f, 0.0f}));   // (its +X face at x = 2: the wall's inner face)
    CHECK(at({2.5f, 0.0f, 0.0f}));   // (inside the wall's thickness)
    CHECK(!at({3.0f, 0.0f, 0.0f}));  // (outside, 0.5 m from the wall)
    cutter->set_world_position({2.0f, 0.0f, 0.0f});  // (a Subtract-only Blockout overlapping the room: it has no matter, so no touch)
    r501_settle(ed, 2);
    CHECK(!ed.blockout_touches(*room, *cutter) && !ed.blockout_touches(*cutter, *room));
    table->set_world_position({0.0f, 0.0f, 0.0f});
    cutter->set_world_position({30.0f, 0, 0});
    r501_settle(ed, 2);
    ed.select_for_test(room->id, false);
    const size_t mark = r501_mark();
    ed.command("blockout join");
    CHECK(r501_log_since(mark).find("touches no other") != std::string::npos);  // (the one-selected join does not swallow what stands inside)
    CHECK(r501_blockouts(ed) == 3);
  });

  test("blockout join (2nd): two flat pieces of no thickness in one plane, turned against each other, touch when they overlap and not when they are apart", [] {
    Editor ed;
    ed.init_headless(1000, 700);
    r501_empty(ed);
    GameObject *a = r501_hand(ed, "Alpha");
    r501_box(ed, *a, "FlatA", {0, 0, 0}, {2, 0, 2});
    GameObject *b = r501_hand(ed, "Beta");
    GameObject *fb = r501_box(ed, *b, "FlatB", {0, 0, 0}, {2, 0, 2});
    fb->set_local_rotation(Quat::axis_angle(Vec3(0, 1, 0), 45.0f * kDeg2Rad));
    ed.mark_changed_for_test("test scene");
    auto touch = [&](Vec3 p) {
      b->set_world_position(p);
      r501_settle(ed, 2);
      return ed.blockout_touches(*a, *b);
    };
    CHECK(touch({0.5f, 0.0f, 0.0f}));
    CHECK(touch({1.9f, 0.0f, 0.0f}));   // (A reaches x = 1; B's turned square reaches 1.9 - 1.41 = 0.49)
    CHECK(!touch({3.0f, 0.0f, 0.0f}));  // (B's near corner at 3 - 1.41 = 1.59: apart)
    CHECK(!touch({0.5f, 0.0f, 3.0f}));
  });

  test("blockout join (2nd): the snap of a moved Blockout reports the corner that landed (the gizmo's mark), a piece moved by G and an axis key snaps to another Blockout, a moved piece's own Blockout is no target", [] {
    Editor ed;
    GameObject *left, *right, *sa, *sb;
    r501_pair(ed, left, right, sa, sb);
    ed.select_for_test(right->id, false);
    r501_settle(ed, 3);
    float d = -2.14f;
    Vec3 mark(0.0f);
    CHECK(ed.blockout_whole_snap(*right, Vec3(4.3f, 0, 0), Vec3(1, 0, 0), {right->id}, d, &mark));
    CHECK_NEAR(d, -2.2f, 1e-4f);
    CHECK_NEAR(mark.x, 1.1f, 1e-4f);
    float d2 = -2.14f;  // (Left's slab is moved with Right and so is no target: nothing left to land on)
    CHECK(!ed.blockout_whole_snap(*right, Vec3(4.3f, 0, 0), Vec3(1, 0, 0), {right->id, left->id}, d2) && d2 == -2.14f);
    float d3 = -2.14f;  // (a piece of Left moved with Right: Left is no target either)
    CHECK(!ed.blockout_whole_snap(*right, Vec3(4.3f, 0, 0), Vec3(1, 0, 0), {right->id, sa->id}, d3) && d3 == -2.14f);
    ed.select_for_test(sb->id, false);
    r501_settle(ed, 3);
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    CHECK(ed.project_to_window({4.3f, 0.0f, 0.0f}, x0, y0) && ed.project_to_window({2.15f, 0.0f, 0.0f}, x1, y1));
    ed.step_frame_headless({r501_ev(R501ET::MouseMove, x0, y0)});
    r501_key(ed, platform::KEY_G);
    r501_key(ed, platform::KEY_X);
    ed.step_frame_headless({r501_ev(R501ET::MouseMove, x1, y1)});
    ed.step_frame_headless({r501_ev(R501ET::MouseMove, x1, y1)});
    r501_click(ed, x1, y1);
    r501_settle(ed, 3);
    CHECK_NEAR(sb->world_matrix().point({-1.0f, 0.0f, 0.0f}).x, 1.1f, 1e-4f);
  });

  test("blockout join (2nd): the Learn section's own Try-it commands run - two Blockouts that touch, one left after the join", [] {
    Editor ed;
    ed.init_headless(1000, 700);
    r501_empty(ed);
    std::vector<std::string> cmds;
    for (int i = 0; i < lesson_count(); i++) {
      if (std::string(lesson(i).title) != "Snapping and joining Blockouts") continue;
      std::istringstream is(lesson(i).body);
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
    }
    CHECK(cmds.size() == 4);
    bool touching_before_join = false;
    for (const std::string &cmd : cmds) {
      if (cmd == "blockout join") {
        std::vector<GameObject *> all;
        ed.scene().for_each([&](GameObject &g) {
          if (g.get<Blockout>()) all.push_back(&g);
        });
        CHECK(all.size() == 2);
        touching_before_join = all.size() == 2 && ed.blockout_touches(*all[0], *all[1]);
      }
      ed.command(cmd);
      ed.command("blockout wait");
      r501_settle(ed, 2);
    }
    CHECK(touching_before_join);
    CHECK(r501_blockouts(ed) == 1);
  });
}

void run_blockout_join_tests() {
  tests_round501();
  tests_round501_engineer();
  tests_round501_second();
}
