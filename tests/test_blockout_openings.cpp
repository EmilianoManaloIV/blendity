// SPDX-License-Identifier: GPL-2.0-or-later
// Draw Piece draws a door or a window (round 513, task 0495): a rectangle dragged on a wall of a Blockout stands in that wall as one
// opening piece, upright by the Blockout's up, facing the wall's normal, a door's bottom on the floor and a window's sill at the drag's
// lower edge. These tests are the round's HCI conditions (the grouped Shape list, the keyboard, the preview, the refusals' words) and its
// behaviour, driven with real events (step_frame_headless) and the console words. They run from run_hci_tests (a new group needs the
// shard table).
#include "test_common.h"

#include "../src/editor/blockout_pick.h"
#include "../src/scene/raycast.h"

#include <cmath>
#include <functional>

using namespace bl;

namespace {

using ET = platform::EventType;

platform::Event oo_ev(ET t, int x, int y, int mods = 0) {
  platform::Event e;
  e.type = t, e.x = x, e.y = y, e.mods = mods;
  return e;
}
platform::Event oo_key(int key, bool down = true) {
  platform::Event e;
  e.type = down ? ET::KeyDown : ET::KeyUp, e.key = key;
  return e;
}
void oo_settle(Editor &ed, int n = 2) {
  for (int i = 0; i < n; i++) ed.step_frame_headless();
}
void oo_click(Editor &ed, int x, int y) {
  for (ET t : {ET::MouseMove, ET::MouseDown, ET::MouseUp}) ed.step_frame_headless({oo_ev(t, x, y)});
}
void oo_empty(Editor &ed) {
  std::vector<GameObject *> roots;
  ed.scene().for_each([&](GameObject &g) {
    if (!g.parent) roots.push_back(&g);
  });
  ed.scene().destroy(roots);
  ed.clear_selection_for_test();
}

size_t oo_mark() {
  std::vector<LogEntry> tmp;
  return Log::fetch((size_t)1 << 40, tmp);
}
std::string oo_log_since(size_t mark) {
  std::vector<LogEntry> v;
  Log::fetch(mark, v);
  std::string s;
  for (const LogEntry &e : v) s += e.text + "\n";
  return s;
}

Quat oo_turn(float degrees) {
  const float a = degrees * 3.14159265f / 180.0f, c = std::cos(a), s = std::sin(a);
  return quat_from_axes(Vec3(c, 0, -s), Vec3(0, 1, 0), Vec3(s, 0, c));
}

GameObject *oo_blockout(Editor &ed, float turn, Vec3 at) {
  GameObject *bo = ed.scene().create("Blockout");
  bo->add<Blockout>();
  bo->add<MeshFilter>()->mesh = std::make_shared<Mesh>();
  bo->add<MeshRenderer>();
  bo->set_local_position(at);
  bo->set_local_rotation(oo_turn(turn));
  return bo;
}
/* A room 6 x 3 x 6 m whose air's floor is `floor` above the Blockout's origin, turned `turn` degrees in it; walls 0.2 m. */
GameObject *oo_room(Editor &ed, GameObject &bo, float turn, float floor = 0.0f, const char *name = "Room") {
  GameObject *g = ed.scene().create(name, &bo);
  BlockoutPiece *p = g->add<BlockoutPiece>();
  p->shape = 0, p->operation = 0, p->room = true, p->wall = p->floor = p->ceiling = 0.2f;
  p->size = {6.0f, 3.0f, 6.0f};
  g->set_local_position({0.0f, floor + 1.5f, 0.0f});
  g->set_local_rotation(oo_turn(turn));
  return g;
}
void oo_built(Editor &ed, GameObject *bo) {
  ed.mark_changed_for_test("test scene");
  for (int i = 0; i < 400; i++) {
    const MeshFilter *mf = bo->get<MeshFilter>();
    if (mf && mf->mesh && mf->mesh->face_count() > 0 && bo->get<Blockout>()->state.hash_valid) break;
    ed.step_frame_headless();
  }
  oo_settle(ed, 3);
}

std::vector<GameObject *> oo_pieces(GameObject &bo) {
  std::vector<GameObject *> out;
  for (GameObject *c : bo.children)
    if (c->get<BlockoutPiece>()) out.push_back(c);
  return out;
}
size_t oo_openings(GameObject &bo) {
  size_t n = 0;
  for (GameObject *c : oo_pieces(bo)) n += c->get<BlockoutPiece>()->opening != 0;
  return n;
}
GameObject *oo_opening(GameObject &bo) {
  for (GameObject *c : oo_pieces(bo))
    if (c->get<BlockoutPiece>()->opening) return c;
  return nullptr;
}
void oo_remove_openings(Editor &ed, GameObject &bo) {
  std::vector<GameObject *> gone;
  for (GameObject *c : oo_pieces(bo))
    if (c->get<BlockoutPiece>()->opening) gone.push_back(c);
  ed.scene().destroy(gone);
  ed.clear_selection_for_test();
  oo_built(ed, &bo);
}

/* The view put in front of a wall: the camera on the wall's outer side (found from the camera's own position: the yaw's direction is measured, not assumed). */
void oo_look_at(Editor &ed, Vec3 centre, Vec3 normal) {
  auto angle_at = [&](float yaw) {
    ed.command(strprintf("camera %g 10 9 %g %g %g", (double)yaw, (double)centre.x, (double)centre.y, (double)centre.z));
    ed.step_frame_headless();
    const Ray r = ed.screen_ray_for_test(Vec2(300.0f, 300.0f));
    return std::atan2(r.origin.z - centre.z, r.origin.x - centre.x);
  };
  const float a0 = angle_at(0.0f), a90 = angle_at(90.0f);
  float step = a90 - a0;
  while (step > 3.14159265f) step -= 6.2831853f;
  while (step < -3.14159265f) step += 6.2831853f;
  const float per_degree = step / 90.0f, target = std::atan2(normal.z, normal.x);
  float yaw = (target - a0) / per_degree;
  yaw = std::fmod(yaw, 360.0f);
  angle_at(yaw);
  oo_settle(ed, 2);
}

/* A drag from one world point to another with the tool, as a mouse does it; false when a point is off the view. */
bool oo_drag(Editor &ed, Vec3 a, Vec3 b, int mods = 0) {
  int ax = 0, ay = 0, bx = 0, by = 0;
  if (!ed.project_to_window(a, ax, ay) || !ed.project_to_window(b, bx, by)) return false;
  ed.step_frame_headless({oo_ev(ET::MouseMove, ax, ay, mods)});
  ed.step_frame_headless({oo_ev(ET::MouseDown, ax, ay, mods)});
  ed.step_frame_headless({oo_ev(ET::MouseMove, bx, by, mods)});
  ed.step_frame_headless({oo_ev(ET::MouseUp, bx, by, mods)});
  oo_settle(ed, 4);
  return true;
}

struct Wall {
  const char *name;
  Vec3 dir;  // the wall's normal in the room's own space
};
const Wall kWalls[4] = {{"+X", {1, 0, 0}}, {"-X", {-1, 0, 0}}, {"+Z", {0, 0, 1}}, {"-Z", {0, 0, -1}}};

Vec3 oo_unit(Vec3 v) { return normalize(v); }

/* The wall's outer face of the 6 m room, as world points: `across` metres along it to the right of a viewer facing it, `up` metres above the floor. */
struct WallFrame {
  Vec3 normal, right;  // world
  Vec3 at(float across, float up) const { return origin + right * across + Vec3(0, 0, 0) + upv * up; }
  Vec3 origin, upv;
};
WallFrame oo_wall_frame(const GameObject &bo, const GameObject &room, const Wall &w, float floor_y_local) {
  WallFrame f;
  const Mat4 rm = room.world_matrix();
  f.normal = oo_unit(rm.dir(w.dir));
  f.upv = oo_unit(bo.world_matrix().dir(Vec3(0, 1, 0)));
  f.right = oo_unit(cross(f.upv, f.normal));
  const Vec3 on_wall = rm.point(w.dir * 3.2f);  // (the room's middle height; moved to the floor below)
  const Vec3 floor_pt = bo.world_matrix().point(Vec3(0, floor_y_local, 0));
  f.origin = on_wall + f.upv * (dot(floor_pt, f.upv) - dot(on_wall, f.upv));
  return f;
}

struct Fixture {
  Editor ed;
  GameObject *bo = nullptr, *room = nullptr;
  void make(float room_turn, float bo_turn, Vec3 bo_at) {
    ed.init_headless(1600, 1000);
    oo_empty(ed);
    bo = oo_blockout(ed, bo_turn, bo_at);
    room = oo_room(ed, *bo, room_turn);
    oo_built(ed, bo);
    ed.command("tool room");
  }
};

/* One drag of `kind` ("door" / "window") on each wall; the checks of acceptance criterion 1. Sizes: the grid's step is 0.25 m, so a drag on it is exact. */
void oo_walls_check(Fixture &fx, const char *kind, float room_turn, float bo_turn, Vec3 bo_at, float lower, float upper, float width) {
  fx.make(room_turn, bo_turn, bo_at);
  fx.ed.command(std::string("blockout roomopt piece ") + kind);
  const bool door = std::string(kind) == "door";
  for (const Wall &w : kWalls) {
    const WallFrame f = oo_wall_frame(*fx.bo, *fx.room, w, 0.0f);
    oo_look_at(fx.ed, f.at(0.0f, 1.0f), f.normal);
    const size_t before = oo_openings(*fx.bo);
    const bool dragged = oo_drag(fx.ed, f.at(-0.5f * width, lower), f.at(0.5f * width, upper));
    CHECK(dragged);
    CHECK(oo_openings(*fx.bo) == before + 1);
    GameObject *g = oo_opening(*fx.bo);
    const BlockoutPiece *bp = g ? g->get<BlockoutPiece>() : nullptr;
    if (!bp || oo_openings(*fx.bo) != before + 1) return;
    CHECK(bp->opening == (door ? 1 : 2));
    CHECK_NEAR(bp->width, width, 1e-3f);
    CHECK_NEAR(bp->head, upper, 1e-3f);
    CHECK_NEAR(bp->sill, door ? 0.0f : lower, 1e-3f);
    const Mat4 gm = g->world_matrix();
    CHECK(dot(oo_unit(gm.dir(Vec3(0, 1, 0))), f.upv) > 0.9999f);  // (the Blockout's up)
    CHECK(dot(oo_unit(gm.dir(Vec3(0, 0, 1))), f.normal) > 0.999f);  // (facing along the wall's normal)
    CHECK_NEAR(g->local().position.y, 0.0f, 1e-3f);  // (the floor's height)
    /* A hole through the wall: a ray in at the opening's middle runs on past the wall to the room's far side. */
    const Vec3 mid = gm.point(Vec3(0, 0.5f * (bp->sill + bp->head), 0));
    SceneHit hit;
    const bool hit_far = scene_raycast(fx.ed.scene(), mid + f.normal * 1.0f, f.normal * -1.0f, 100.0f, hit) && hit.distance > 3.0f;
    CHECK(hit_far);
    CHECK(fx.ed.undo_depth_for_test() > 0);
    oo_remove_openings(fx.ed, *fx.bo);
  }
}

/* A click on empty ground with the tool on: nothing is made and the tool is waiting. */
bool oo_click_ground_makes_nothing(Fixture &fx) {
  const size_t pieces = oo_pieces(*fx.bo).size();
  int cx = 0, cy = 0;
  if (!fx.ed.project_to_window(Vec3(41.0f, 0.0f, 41.0f), cx, cy)) return false;
  oo_click(fx.ed, cx, cy);
  oo_settle(fx.ed, 3);
  return oo_pieces(*fx.bo).size() == pieces && fx.ed.room_tool_stage_for_test() == 0;
}

}  // namespace

void run_blockout_openings_tests() {
  test("draw piece door: a 1 x 2 m drag on each wall of a room makes one upright door of that size facing the wall, its bottom on the floor, a hole through the wall", [] {
    Fixture fx;
    oo_walls_check(fx, "door", 0.0f, 0.0f, Vec3(0.0f), 0.0f, 2.0f, 1.0f);
  });

  test("draw piece door: the same on a room turned 30 degrees in its Blockout and in a Blockout turned 30 degrees", [] {
    Fixture a, b;
    oo_walls_check(a, "door", 30.0f, 0.0f, Vec3(0.0f), 0.0f, 2.0f, 1.0f);
    oo_walls_check(b, "door", 0.0f, 30.0f, Vec3(3.0f, 0.0f, 2.0f), 0.0f, 2.0f, 1.0f);
  });

  test("draw piece window: a 1 x 1.5 m drag from 0.75 m up on each wall makes one upright window, its sill at the drag's lower edge, in the Blockout's up, turned rooms included", [] {
    Fixture a, b, c;
    oo_walls_check(a, "window", 0.0f, 0.0f, Vec3(0.0f), 0.75f, 2.25f, 1.0f);
    oo_walls_check(b, "window", 30.0f, 0.0f, Vec3(0.0f), 0.75f, 2.25f, 1.0f);
    oo_walls_check(c, "window", 0.0f, 30.0f, Vec3(3.0f, 0.0f, 2.0f), 0.75f, 2.25f, 1.0f);
  });

  test("draw piece door: with Ctrl (no grid) a 0.9 x 2.1 m drag is that size to a few centimetres, and Window / Door Style rows' choices are applied to the drawn piece", [] {
    Fixture fx;
    fx.make(0.0f, 0.0f, Vec3(0.0f));
    fx.ed.command("blockout roomopt piece door");
    const WallFrame f = oo_wall_frame(*fx.bo, *fx.room, kWalls[0], 0.0f);
    oo_look_at(fx.ed, f.at(0.0f, 1.0f), f.normal);
    CHECK(oo_drag(fx.ed, f.at(-0.45f, 0.0f), f.at(0.45f, 2.1f), platform::MOD_CTRL));
    const BlockoutPiece *bp = oo_opening(*fx.bo) ? oo_opening(*fx.bo)->get<BlockoutPiece>() : nullptr;
    CHECK(bp != nullptr);
    if (!bp) return;
    CHECK_NEAR(bp->width, 0.9f, 0.06f);
    CHECK_NEAR(bp->head, 2.1f, 0.06f);
    CHECK_NEAR(bp->sill, 0.0f, 1e-4f);
  });

  test("draw piece door: the console draws it exactly - blockout draw door|window auto at <point> normal <n> size <width height 0> - on four walls, a turned room and a turned Blockout, with the current style", [] {
    for (const bool turned : {false, true}) {
      Fixture fx;
      fx.make(turned ? 30.0f : 0.0f, turned ? 30.0f : 0.0f, turned ? Vec3(3.0f, 0.0f, 2.0f) : Vec3(0.0f));
      for (const char *kind : {"door", "window"}) {
        const bool door = std::string(kind) == "door";
        for (const Wall &w : kWalls) {
          const WallFrame f = oo_wall_frame(*fx.bo, *fx.room, w, 0.0f);
          const Vec3 p = f.at(-0.45f, door ? 0.0f : 0.9f);
          fx.ed.command(strprintf("blockout draw %s auto at %.6f %.6f %.6f normal %.6f %.6f %.6f size 0.9 %g 0", kind, (double)p.x, (double)p.y, (double)p.z, (double)f.normal.x,
                                  (double)f.normal.y, (double)f.normal.z, door ? 2.1 : 1.1));
          oo_settle(fx.ed, 3);
          GameObject *g = oo_opening(*fx.bo);
          const BlockoutPiece *bp = g ? g->get<BlockoutPiece>() : nullptr;
          CHECK(bp && oo_openings(*fx.bo) == 1);
          if (!bp) continue;
          CHECK(bp->opening == (door ? 1 : 2));
          CHECK_NEAR(bp->width, 0.9f, 1e-4f);
          CHECK_NEAR(bp->head, door ? 2.1f : 2.0f, 1e-4f);
          CHECK_NEAR(bp->sill, door ? 0.0f : 0.9f, 1e-4f);
          CHECK(dot(oo_unit(g->world_matrix().dir(Vec3(0, 1, 0))), f.upv) > 0.9999f);
          CHECK(dot(oo_unit(g->world_matrix().dir(Vec3(0, 0, 1))), f.normal) > 0.999f);
          oo_remove_openings(fx.ed, *fx.bo);
        }
      }
    }
  });

  test("draw piece door: a door drawn on an upper room's wall stands on that room's floor, not on y = 0; on an Add piece's side it stands on the piece's foot", [] {
    Editor ed;
    ed.init_headless(1600, 1000);
    oo_empty(ed);
    GameObject *bo = oo_blockout(ed, 0.0f, Vec3(0.0f));
    oo_room(ed, *bo, 0.0f, 4.0f, "Upper");
    GameObject *box = ed.scene().create("Pillar", bo);
    BlockoutPiece *bp = box->add<BlockoutPiece>();
    bp->shape = 0, bp->operation = 0, bp->size = {2.0f, 3.0f, 2.0f};
    box->set_local_position({12.0f, 1.5f, 0.0f});
    oo_built(ed, bo);
    ed.command("blockout draw door auto at 3.2 4.0 0.5 normal 1 0 0 size -0.9 2.1 0");
    oo_settle(ed, 3);
    GameObject *g = oo_opening(*bo);
    CHECK(g != nullptr && oo_openings(*bo) == 1);
    if (g) CHECK_NEAR(g->local().position.y, 4.0f, 1e-3f);
    oo_remove_openings(ed, *bo);
    ed.command("blockout draw window auto at 13.0 0.5 0.4 normal 1 0 0 size 0.6 1 0");
    oo_settle(ed, 3);
    g = oo_opening(*bo);
    CHECK(g != nullptr && g->get<BlockoutPiece>()->opening == 2);
    if (g) {
      CHECK_NEAR(g->local().position.y, 0.0f, 1e-3f);
      CHECK_NEAR(g->get<BlockoutPiece>()->sill, 0.5f, 1e-3f);
    }
  });

  test("draw piece door: a drag past the wall's edge is held to the wall and the status says so; the opening is not wider than the wall", [] {
    Fixture fx;
    fx.make(0.0f, 0.0f, Vec3(0.0f));
    const size_t mark = oo_mark();
    const WallFrame f = oo_wall_frame(*fx.bo, *fx.room, kWalls[0], 0.0f);
    const Vec3 p = f.at(2.5f, 0.0f);
    fx.ed.command(strprintf("blockout draw door auto at %.6f %.6f %.6f normal %.6f %.6f %.6f size 3 2.1 0", (double)p.x, (double)p.y, (double)p.z, (double)f.normal.x, (double)f.normal.y,
                            (double)f.normal.z));
    oo_settle(fx.ed, 3);
    const BlockoutPiece *bp = oo_opening(*fx.bo) ? oo_opening(*fx.bo)->get<BlockoutPiece>() : nullptr;
    CHECK(bp != nullptr);
    /* The wall's face ends 3.2 m from its middle; the wall that meets it is 0.2 m thick, and the door stops that far short of the end: 3.0 m. From 2.5 m: 0.5 m wide. */
    if (bp) CHECK_NEAR(bp->width, 0.5f, 0.01f);
    CHECK(oo_log_since(mark).find("held to the wall") != std::string::npos);
    oo_remove_openings(fx.ed, *fx.bo);
    /* Across the whole wall: its span, less the thickness of the two walls that meet it - 6.0 m - and those walls' ends stay whole. */
    const Vec3 q = f.at(-3.1f, 0.0f);
    fx.ed.command(strprintf("blockout draw door auto at %.6f %.6f %.6f normal %.6f %.6f %.6f size 10 2.1 0", (double)q.x, (double)q.y, (double)q.z, (double)f.normal.x, (double)f.normal.y,
                            (double)f.normal.z));
    oo_settle(fx.ed, 4);
    const BlockoutPiece *wide = oo_opening(*fx.bo) ? oo_opening(*fx.bo)->get<BlockoutPiece>() : nullptr;
    CHECK(wide != nullptr);
    if (wide) CHECK_NEAR(wide->width, 6.0f, 0.01f);
    for (const float across : {-3.08f, 3.08f}) {  // (a ray in at the end of the meeting wall, level with the door: it must hit that wall's end 0.8 m in)
      SceneHit hit;
      const Vec3 on = f.at(across, 1.0f);
      CHECK(scene_raycast(fx.ed.scene(), on + f.normal * 0.8f, f.normal * -1.0f, 5.0f, hit) && std::fabs(hit.distance - 0.8f) < 0.05f);
    }
  });

  test("draw piece door: a drag on a floor, the ground, empty air, a sloped face, a ceiling, too small, or a door that cannot reach the floor makes nothing and the status says why and what to do", [] {
    Fixture fx;
    fx.make(0.0f, 0.0f, Vec3(0.0f));
    GameObject *wedge = fx.ed.scene().create("Ramp", fx.bo);
    BlockoutPiece *wp = wedge->add<BlockoutPiece>();
    wp->shape = 3, wp->operation = 0, wp->size = {2.0f, 2.0f, 2.0f};
    wedge->set_local_position({12.0f, 1.0f, 0.0f});
    oo_built(fx.ed, fx.bo);
    fx.ed.command("blockout roomopt piece door");
    auto refused = [&](const std::string &command, const char *words) {
      const size_t mark = oo_mark();
      const size_t pieces = oo_pieces(*fx.bo).size();
      fx.ed.command(command);
      oo_settle(fx.ed, 2);
      const std::string log = oo_log_since(mark);
      CHECK(oo_pieces(*fx.bo).size() == pieces);
      if (log.find(words) == std::string::npos) std::printf("    no \"%s\" in the status after: %s -> %s\n", words, command.c_str(), log.c_str());
      CHECK(log.find(words) != std::string::npos);
    };
    refused("blockout draw door auto at 0.3 0 0.1 normal 0 1 0 size 1 2 0", "this face is a floor");
    refused("blockout draw door auto at 0.3 3.0 0.1 normal 0 -1 0 size 1 2 0", "this face is a ceiling");
    refused("blockout draw door auto at 30 1 0 normal 1 0 0 size 1 2 0", "goes in a wall of a Blockout");
    refused("blockout draw door auto at 12 0 0 normal 0 0.7071 0.7071 size 1 1 0", "face");  // (a sloped face of the ramp: a floor or a lean)
    refused("blockout draw door auto at 3.2 0 0 normal 1 0 0 size 0.1 0.1 0", "Too small");
    refused("blockout draw door auto at 3.2 1.2 0 normal 1 0 0 size 1 1 0", "starts 1.20 m above");
    refused("blockout draw door auto at 3.2 -0.19 0 normal 1 0 0 size 1 0.1 0", "reach up from the floor");
    /* With real events: the ground and a room's roof. */
    const size_t mark = oo_mark();
    const size_t pieces = oo_pieces(*fx.bo).size();
    fx.ed.command("camera 10 70 14 0 0 0");
    oo_settle(fx.ed, 2);
    CHECK(oo_drag(fx.ed, Vec3(0.0f, 3.2f, -1.0f), Vec3(1.0f, 3.2f, 1.0f)));
    CHECK(oo_pieces(*fx.bo).size() == pieces);
    CHECK(oo_log_since(mark).find("this face is a floor") != std::string::npos);
    fx.ed.command("camera 10 70 14 40 0 40");
    oo_settle(fx.ed, 2);
    const size_t mark2 = oo_mark();
    CHECK(oo_drag(fx.ed, Vec3(40.0f, 0.0f, 40.0f), Vec3(41.0f, 0.0f, 41.0f)));
    CHECK(oo_log_since(mark2).find("goes in a wall of a Blockout") != std::string::npos);
    CHECK(oo_pieces(*fx.bo).size() == pieces);
  });

  test("draw piece door: R does nothing while Door or Window is the shape, and the status says why; the door keeps the wall's facing", [] {
    Fixture fx;
    fx.make(0.0f, 0.0f, Vec3(0.0f));
    fx.ed.command("blockout roomopt piece door");
    const WallFrame f = oo_wall_frame(*fx.bo, *fx.room, kWalls[0], 0.0f);
    oo_look_at(fx.ed, f.at(0.0f, 1.0f), f.normal);
    int ax = 0, ay = 0, bx = 0, by = 0;
    CHECK(fx.ed.project_to_window(f.at(-0.5f, 0.0f), ax, ay) && fx.ed.project_to_window(f.at(0.5f, 2.0f), bx, by));
    fx.ed.step_frame_headless({oo_ev(ET::MouseMove, ax, ay)});
    fx.ed.step_frame_headless({oo_ev(ET::MouseDown, ax, ay)});
    fx.ed.step_frame_headless({oo_ev(ET::MouseMove, bx, by)});
    CHECK(fx.ed.room_tool_stage_for_test() == 1);
    const size_t mark = oo_mark();
    fx.ed.step_frame_headless({oo_key(platform::KEY_R)});
    fx.ed.step_frame_headless({oo_key(platform::KEY_R, false)});
    CHECK(fx.ed.room_tool_stage_for_test() == 1);
    CHECK(oo_log_since(mark).find("turns with its wall") != std::string::npos);
    CHECK(fx.ed.room_tool_preview_lines_for_test() == 4);  // (the rectangle's four edges, in a Subtract's colour: blockout_operation_colour(1))
    CHECK((blockout_operation_colour(1, false) & 0x00FFFFFFu) != (blockout_operation_colour(0, false) & 0x00FFFFFFu));  // (Subtract's colour is not Add's)
    CHECK(fx.ed.room_tool_label_for_test().find("Door") != std::string::npos);
    const std::string bar = fx.ed.statusbar_hint_for_test();  // (the status bar names the door, not "Box", and says why R does nothing)
    CHECK(bar.find("Door: release") != std::string::npos && bar.find("Box") == std::string::npos);
    CHECK(bar.find("R does nothing") != std::string::npos && bar.find("R: turn it") == std::string::npos);
    if (bar.find("Door: release") == std::string::npos) std::printf("    status bar: %s\n", bar.c_str());
    fx.ed.step_frame_headless({oo_ev(ET::MouseUp, bx, by)});
    oo_settle(fx.ed, 4);
    GameObject *g = oo_opening(*fx.bo);
    CHECK(g && dot(oo_unit(g->world_matrix().dir(Vec3(0, 0, 1))), f.normal) > 0.999f);
  });

  test("draw piece door: F9 changes Width and Head of the drawn door, and one undo takes it away", [] {
    Fixture fx;
    fx.make(0.0f, 0.0f, Vec3(0.0f));
    fx.ed.command("blockout roomopt piece door");
    const WallFrame f = oo_wall_frame(*fx.bo, *fx.room, kWalls[0], 0.0f);
    oo_look_at(fx.ed, f.at(0.0f, 1.0f), f.normal);
    const size_t depth = fx.ed.undo_depth_for_test();
    CHECK(oo_drag(fx.ed, f.at(-0.5f, 0.0f), f.at(0.5f, 2.0f)));
    CHECK(fx.ed.undo_depth_for_test() == depth + 1);
    GameObject *g = oo_opening(*fx.bo);
    CHECK(g != nullptr && fx.ed.obj_last_valid());
    if (!g) return;
    fx.ed.record_buttons_for_test(true);
    oo_settle(fx.ed, 3);
    auto type = [&](const char *key, const char *text) {
      Recti r;
      for (int i = 0; i < 4 && !fx.ed.button_rect_for_test(key, r); i++) fx.ed.step_frame_headless();
      if (!fx.ed.button_rect_for_test(key, r)) return false;
      const int x = r.x + r.w / 2, y = r.y + r.h / 2;
      for (ET t : {ET::MouseMove, ET::MouseDown, ET::MouseUp, ET::MouseDown, ET::MouseUp}) fx.ed.step_frame_headless({oo_ev(t, x, y)});
      platform::Event all = oo_key(platform::KEY_A);
      all.mods = platform::MOD_CTRL;
      fx.ed.step_frame_headless({all});
      for (const char *c = text; *c; c++) {
        platform::Event e;
        e.type = ET::Text, e.codepoint = (uint32_t)*c;
        fx.ed.step_frame_headless({e});
      }
      fx.ed.step_frame_headless({oo_key(platform::KEY_ENTER)});
      oo_settle(fx.ed, 2);
      return true;
    };
    CHECK(type("adjust:Width", "1.4"));
    CHECK_NEAR(g->get<BlockoutPiece>()->width, 1.4f, 1e-4f);
    CHECK(type("adjust:Head", "2.2"));
    CHECK_NEAR(g->get<BlockoutPiece>()->head, 2.2f, 1e-4f);
    CHECK(fx.ed.undo_depth_for_test() == depth + 1);  // (the same step, amended)
    fx.ed.command("undo");
    oo_settle(fx.ed, 3);
    GameObject *after = fx.ed.scene().find_by_name("Blockout");  // (an undo swaps the scene: the old pointers are gone)
    CHECK(after && oo_openings(*after) == 0);
    CHECK(fx.ed.undo_depth_for_test() == depth);
  });

  test("draw piece door: the console sets the shape (blockout roomopt piece door|window), the nine shapes keep their numbers, and a footprint command still makes a plain solid", [] {
    Editor ed;
    ed.init_headless(1600, 1000);
    oo_empty(ed);
    const char *words[] = {"room", "wedge", "stairs", "arch", "door", "window"};
    const int rows[] = {0, 6, 7, 8, 10, 11};
    for (int i = 0; i < 6; i++) {
      ed.command(std::string("blockout roomopt piece ") + words[i]);
      CHECK(ed.room_tool_shape_for_test() == rows[i]);
    }
    ed.command("blockout roomopt piece door");
    ed.command("blockout room 0 0 4 3 2.5");  // (a door is no piece room_make draws: the footprint makes the plain solid)
    oo_settle(ed, 3);
    const GameObject *g = ed.active_object_for_test();
    CHECK(g && g->get<BlockoutPiece>() && !g->get<BlockoutPiece>()->opening);
    CHECK(ed.room_tool_shape_for_test() == 10);  // (and the tool's own choice is kept)
    ed.command("blockout roomopt circle on");
    CHECK(ed.room_tool_shape_for_test() == 4);
  });

  test("draw piece door: the Shape list groups the shapes - a heading Openings, then Door and Window - every row's text within its row; Door and Window are chosen by keyboard (focus the dropdown, Space, Down past the heading, Enter)", [] {
    Editor ed;
    ed.init_headless(1366, 768);
    ed.set_guides_for_test(false);
    ed.record_buttons_for_test(true);
    ed.command("layout Default");
    ed.command("window Modeling Tools");
    ed.command("alltools on");
    ed.command("blockout roomopt piece arch");
    oo_settle(ed, 4);
    Recti combo;
    CHECK(ed.button_rect_for_test("tools:shape", combo));
    if (combo.empty()) return;
    /* The list, opened by a click. */
    oo_click(ed, combo.x + combo.w / 2, combo.y + combo.h / 2);
    oo_settle(ed, 2);
    bool heading = false;
    int rows = 0, clipped = 0;
    for (const auto &c : ed.ui_for_test().controls) {
      const std::string kind = c.kind;
      if (kind != "heading" && kind != "menu") continue;
      if (c.label != "Door" && c.label != "Window" && c.label != "Openings" && c.label != "Arch" && c.label != "Round Room" && c.label != "Outline Room") continue;
      rows++;
      heading = heading || (kind == "heading" && c.label == "Openings");
      if (ed.text_width_for_test(c.label) > c.r.w) clipped++;
    }
    CHECK(heading);
    CHECK(rows >= 5);
    CHECK(clipped == 0);
    Recti r;
    CHECK(ed.button_rect_for_test("menu:Door", r) && ed.button_rect_for_test("menu:Window", r));
    CHECK(!ed.button_rect_for_test("menu:Openings", r));  // (a heading is no choice)
    ed.step_frame_headless({oo_key(platform::KEY_ESCAPE)});
    ed.step_frame_headless({oo_key(platform::KEY_ESCAPE, false)});
    oo_settle(ed, 2);
    /* The keyboard: F6 into the window, Tab to the dropdown, Space opens it on Arch, Down passes the heading to Door, Enter picks. */
    const Recti window = ed.window_rect_for_test(WindowKind::Tools);
    ed.step_frame_headless({oo_ev(ET::MouseMove, window.x + window.w / 2, window.y + window.h / 2)});
    auto press = [&](int k, int mods = 0) {
      platform::Event e = oo_key(k);
      e.mods = mods;
      ed.step_frame_headless({e});
      ed.step_frame_headless();
    };
    press(platform::KEY_F6);
    bool focused = false;
    for (int i = 0; i < 200 && !focused; i++) {
      ed.button_rect_for_test("tools:shape", combo);
      const Recti f = ed.ui_for_test().focus_rect_for_test();
      focused = !f.empty() && f.x == combo.x && f.y == combo.y && f.w == combo.w && f.h == combo.h;
      if (!focused) press(platform::KEY_TAB);
    }
    CHECK(focused);
    if (!focused) return;
    press(platform::KEY_SPACE);
    press(platform::KEY_DOWN);
    press(platform::KEY_ENTER);
    CHECK(ed.room_tool_shape_for_test() == 10);
    press(platform::KEY_SPACE);
    press(platform::KEY_DOWN);
    press(platform::KEY_ENTER);
    CHECK(ed.room_tool_shape_for_test() == 11);
    press(platform::KEY_SPACE);
    press(platform::KEY_UP);
    press(platform::KEY_UP);
    press(platform::KEY_ENTER);
    CHECK(ed.room_tool_shape_for_test() == 8);  // (Up from Door skips the heading to Arch)
  });

  /* ---- Independent edge cases (test-engineer, round 513): what the spec implies and the first tests do not pin down. ---- */

  test("draw piece door (edge): a press and release on the same pixel, and a drag of no height, make nothing, add no undo step, say what a drag is, and leave the tool waiting", [] {
    Fixture fx;
    fx.make(0.0f, 0.0f, Vec3(0.0f));
    fx.ed.command("blockout roomopt piece window");
    const WallFrame f = oo_wall_frame(*fx.bo, *fx.room, kWalls[0], 0.0f);
    oo_look_at(fx.ed, f.at(0.0f, 1.0f), f.normal);
    int ax = 0, ay = 0;
    CHECK(fx.ed.project_to_window(f.at(0.0f, 1.0f), ax, ay));
    const size_t depth = fx.ed.undo_depth_for_test();
    const size_t mark = oo_mark();
    fx.ed.step_frame_headless({oo_ev(ET::MouseMove, ax, ay)});
    fx.ed.step_frame_headless({oo_ev(ET::MouseDown, ax, ay)});
    fx.ed.step_frame_headless({oo_ev(ET::MouseUp, ax, ay)});
    oo_settle(fx.ed, 3);
    CHECK(oo_openings(*fx.bo) == 0);
    CHECK(fx.ed.undo_depth_for_test() == depth);
    CHECK(fx.ed.room_tool_stage_for_test() == 0);
    CHECK(!oo_log_since(mark).empty());
    /* A drag wide enough but of no height (a flat horizontal line). */
    int bx = 0, by = 0;
    CHECK(fx.ed.project_to_window(f.at(1.0f, 1.0f), bx, by));
    fx.ed.step_frame_headless({oo_ev(ET::MouseMove, ax, ay)});
    fx.ed.step_frame_headless({oo_ev(ET::MouseDown, ax, ay)});
    fx.ed.step_frame_headless({oo_ev(ET::MouseMove, bx, ay)});
    fx.ed.step_frame_headless({oo_ev(ET::MouseUp, bx, ay)});
    oo_settle(fx.ed, 3);
    CHECK(oo_openings(*fx.bo) == 0);
    CHECK(fx.ed.undo_depth_for_test() == depth);
    CHECK(fx.ed.room_tool_stage_for_test() == 0);
  });

  test("draw piece door (edge): right-click while dragging drops the rectangle, Esc leaves the tool, and neither makes a piece or an undo step", [] {
    Fixture fx;
    fx.make(0.0f, 0.0f, Vec3(0.0f));
    fx.ed.command("blockout roomopt piece door");
    const WallFrame f = oo_wall_frame(*fx.bo, *fx.room, kWalls[0], 0.0f);
    oo_look_at(fx.ed, f.at(0.0f, 1.0f), f.normal);
    int ax = 0, ay = 0, bx = 0, by = 0;
    CHECK(fx.ed.project_to_window(f.at(-0.5f, 0.0f), ax, ay) && fx.ed.project_to_window(f.at(0.5f, 2.0f), bx, by));
    const size_t depth = fx.ed.undo_depth_for_test();
    auto start = [&] {
      fx.ed.step_frame_headless({oo_ev(ET::MouseMove, ax, ay)});
      fx.ed.step_frame_headless({oo_ev(ET::MouseDown, ax, ay)});
      fx.ed.step_frame_headless({oo_ev(ET::MouseMove, bx, by)});
    };
    start();
    CHECK(fx.ed.room_tool_stage_for_test() == 1);
    platform::Event rd = oo_ev(ET::MouseDown, bx, by), ru = oo_ev(ET::MouseUp, bx, by);
    rd.button = ru.button = 1;
    fx.ed.step_frame_headless({rd});
    fx.ed.step_frame_headless({ru});
    fx.ed.step_frame_headless({oo_ev(ET::MouseUp, bx, by)});  // (the left button let go afterwards: no door)
    oo_settle(fx.ed, 3);
    CHECK(fx.ed.room_tool_stage_for_test() == 0);
    CHECK(oo_openings(*fx.bo) == 0);
    CHECK(fx.ed.undo_depth_for_test() == depth);
    CHECK(fx.ed.room_tool_active_for_test());  // (a right-click drops the rectangle, the tool stays)
    start();
    fx.ed.step_frame_headless({oo_key(platform::KEY_ESCAPE)});
    fx.ed.step_frame_headless({oo_key(platform::KEY_ESCAPE, false)});
    fx.ed.step_frame_headless({oo_ev(ET::MouseUp, bx, by)});
    oo_settle(fx.ed, 3);
    CHECK(oo_openings(*fx.bo) == 0);
    CHECK(fx.ed.undo_depth_for_test() == depth);
    CHECK(!fx.ed.room_tool_active_for_test());
    CHECK(fx.ed.room_tool_stage_for_test() == 0);
  });

  test("draw piece door (edge): a drawn door is one undo step; undo takes it away, redo brings the same door back, undo again leaves the wall whole", [] {
    Fixture fx;
    fx.make(0.0f, 0.0f, Vec3(0.0f));
    fx.ed.command("blockout roomopt piece door");
    const WallFrame f = oo_wall_frame(*fx.bo, *fx.room, kWalls[2], 0.0f);
    oo_look_at(fx.ed, f.at(0.0f, 1.0f), f.normal);
    const size_t depth = fx.ed.undo_depth_for_test();
    CHECK(oo_drag(fx.ed, f.at(-0.5f, 0.0f), f.at(0.5f, 2.0f)));
    CHECK(fx.ed.undo_depth_for_test() == depth + 1);
    fx.ed.command("undo");
    oo_settle(fx.ed, 3);
    GameObject *b = fx.ed.scene().find_by_name("Blockout");
    CHECK(b && oo_openings(*b) == 0);
    fx.ed.command("redo");
    oo_settle(fx.ed, 3);
    b = fx.ed.scene().find_by_name("Blockout");
    CHECK(b && oo_openings(*b) == 1);
    GameObject *g = b ? oo_opening(*b) : nullptr;
    if (g) {
      CHECK_NEAR(g->get<BlockoutPiece>()->width, 1.0f, 1e-3f);
      CHECK_NEAR(g->get<BlockoutPiece>()->head, 2.0f, 1e-3f);
    }
    CHECK(fx.ed.undo_depth_for_test() == depth + 1);
    fx.ed.command("undo");
    oo_settle(fx.ed, 3);
    b = fx.ed.scene().find_by_name("Blockout");
    CHECK(b && oo_openings(*b) == 0);
    CHECK(fx.ed.undo_depth_for_test() == depth);
  });

  test("draw piece window (edge): a window drawn taller than its wall is held to the wall and says so; one drawn above the wall's top is refused", [] {
    Fixture fx;
    fx.make(0.0f, 0.0f, Vec3(0.0f));
    const WallFrame f = oo_wall_frame(*fx.bo, *fx.room, kWalls[0], 0.0f);
    const Vec3 p = f.at(-0.5f, 0.9f);
    size_t mark = oo_mark();
    fx.ed.command(strprintf("blockout draw window auto at %.6f %.6f %.6f normal %.6f %.6f %.6f size 1 10 0", (double)p.x, (double)p.y, (double)p.z, (double)f.normal.x, (double)f.normal.y,
                            (double)f.normal.z));
    oo_settle(fx.ed, 3);
    GameObject *g = oo_opening(*fx.bo);
    CHECK(g != nullptr && oo_openings(*fx.bo) == 1);
    if (g) {
      const BlockoutPiece *bp = g->get<BlockoutPiece>();
      CHECK(bp->head > bp->sill + 0.2f);
      CHECK(bp->head <= 3.4f + 1e-3f);  // (the room's outer wall face ends at 3.2 m + the roof's 0.2 m)
      CHECK(std::isfinite(bp->head) && std::isfinite(bp->sill) && std::isfinite(bp->width));
    }
    CHECK(oo_log_since(mark).find("held to the wall") != std::string::npos);
    oo_remove_openings(fx.ed, *fx.bo);
    const Vec3 q = f.at(-0.5f, 6.0f);  // (above the wall: not on any face)
    mark = oo_mark();
    fx.ed.command(strprintf("blockout draw window auto at %.6f %.6f %.6f normal %.6f %.6f %.6f size 1 1 0", (double)q.x, (double)q.y, (double)q.z, (double)f.normal.x, (double)f.normal.y,
                            (double)f.normal.z));
    oo_settle(fx.ed, 3);
    CHECK(oo_openings(*fx.bo) == 0);
    CHECK(!oo_log_since(mark).empty());
  });

  test("draw piece door (edge): a drawing that starts inside an existing doorway is refused; one dragged across a doorway makes nothing broken", [] {
    Fixture fx;
    fx.make(0.0f, 0.0f, Vec3(0.0f));
    const WallFrame f = oo_wall_frame(*fx.bo, *fx.room, kWalls[0], 0.0f);
    auto draw = [&](const char *kind, float across, float up, float w, float h) {
      const Vec3 p = f.at(across, up);
      fx.ed.command(strprintf("blockout draw %s auto at %.6f %.6f %.6f normal %.6f %.6f %.6f size %g %g 0", kind, (double)p.x, (double)p.y, (double)p.z, (double)f.normal.x, (double)f.normal.y,
                              (double)f.normal.z, (double)w, (double)h));
      oo_settle(fx.ed, 3);
    };
    draw("door", -0.5f, 0.0f, 1.0f, 2.0f);
    CHECK(oo_openings(*fx.bo) == 1);
    size_t mark = oo_mark();
    draw("window", -0.2f, 1.0f, 0.4f, 0.4f);  // (the doorway's air)
    CHECK(oo_openings(*fx.bo) == 1);
    CHECK(!oo_log_since(mark).empty());
    mark = oo_mark();
    draw("window", -2.0f, 1.0f, 4.0f, 1.0f);  // (from the wall beside the doorway across it)
    CHECK(oo_openings(*fx.bo) == 2);  // (a window across a doorway is a second opening, wide, over the first: the wall's face is one face round the hole)
    CHECK(oo_log_since(mark).find("drawn on the wall") != std::string::npos);
    const MeshFilter *mf = fx.bo->get<MeshFilter>();
    CHECK(mf && mf->mesh && mf->mesh->face_count() > 0);
    CHECK(fx.ed.scene().find_by_name("Blockout") != nullptr);
  });

  test("draw piece door (edge): the console refuses NaN, infinite, absurd and zero numbers cleanly - no piece, no crash, a status line each - and keeps the tool's own choice", [] {
    Fixture fx;
    fx.make(0.0f, 0.0f, Vec3(0.0f));
    fx.ed.command("blockout roomopt piece window");
    const WallFrame f = oo_wall_frame(*fx.bo, *fx.room, kWalls[0], 0.0f);
    const Vec3 p = f.at(0.0f, 1.0f);
    auto fmt = [&](const char *at_y, const char *nx, const char *size) {
      return strprintf("blockout draw door auto at %.6f %s %.6f normal %s 0 0 size %s 0", (double)p.x, at_y, (double)p.z, nx, size);
    };
    const std::string bad[] = {fmt("nan", "1", "1 2"),     fmt("inf", "1", "1 2"),   fmt("1", "nan", "1 2"), fmt("1", "0", "1 2"), fmt("1", "1", "nan 2"),
                               fmt("1", "1", "1 inf"),     fmt("1", "1", "0 0"),     fmt("1", "1", "0 2"),   fmt("1e39", "1", "1 2"), fmt("1", "1", "1e39 2")};
    for (const std::string &c : bad) {
      const size_t mark = oo_mark();
      const size_t pieces = oo_pieces(*fx.bo).size();
      fx.ed.command(c);
      oo_settle(fx.ed, 2);
      CHECK(oo_pieces(*fx.bo).size() == pieces);
      const bool said = !oo_log_since(mark).empty();
      CHECK(said);
      if (!said || oo_pieces(*fx.bo).size() != pieces) std::printf("    not refused cleanly: %s\n", c.c_str());
    }
    CHECK(fx.ed.room_tool_shape_for_test() == 11);
    /* A huge but finite size on a good press: held to the wall, finite numbers. */
    fx.ed.command(strprintf("blockout draw door auto at %.6f %.6f %.6f normal %.6f 0 %.6f size 1e30 1e30 0", (double)f.at(0, 0).x, (double)f.at(0, 0).y, (double)f.at(0, 0).z, (double)f.normal.x, (double)f.normal.z));
    oo_settle(fx.ed, 3);
    if (GameObject *g = oo_opening(*fx.bo)) {
      const BlockoutPiece *bp = g->get<BlockoutPiece>();
      CHECK(std::isfinite(bp->width) && std::isfinite(bp->head) && bp->width < 7.0f && bp->head < 4.0f);
    }
    CHECK(fx.ed.scene().find_by_name("Blockout") != nullptr);
  });

  test("draw piece door (edge): a Blockout scaled 4x and 0.7x, or unevenly (2,1,1), takes a door upright facing the wall at the size asked in metres - or refuses with a status line", [] {
    const Vec3 scales[] = {Vec3(4.0f), Vec3(0.7f), Vec3(2.0f, 1.0f, 1.0f), Vec3(1.0f, 1.0f, 3.0f)};
    for (const Vec3 &s : scales) {
      Fixture fx;
      fx.make(0.0f, 0.0f, Vec3(0.0f));
      fx.bo->set_local_scale(s);
      oo_built(fx.ed, fx.bo);
      for (const Wall &w : {kWalls[0], kWalls[2]}) {
        const WallFrame f = oo_wall_frame(*fx.bo, *fx.room, w, 0.0f);
        const Vec3 p = f.at(-0.45f, 0.0f);
        const size_t mark = oo_mark();
        fx.ed.command(strprintf("blockout draw door auto at %.6f %.6f %.6f normal %.6f %.6f %.6f size 0.9 2.1 0", (double)p.x, (double)p.y, (double)p.z, (double)f.normal.x,
                                (double)f.normal.y, (double)f.normal.z));
        oo_settle(fx.ed, 3);
        GameObject *g = oo_opening(*fx.bo);
        const bool uniform = s.x == s.y && s.y == s.z;
        if (!g) {
          std::printf("    scale (%g %g %g) wall %s: refused: %s", (double)s.x, (double)s.y, (double)s.z, w.name, oo_log_since(mark).c_str());
          CHECK(!uniform);  // (a uniform scale must take the door; an uneven one may refuse, saying why)
          CHECK(!oo_log_since(mark).empty());
          continue;
        }
        const BlockoutPiece *bp = g->get<BlockoutPiece>();
        const Mat4 gm = g->world_matrix();
        CHECK(dot(oo_unit(gm.dir(Vec3(0, 1, 0))), f.upv) > 0.999f);
        CHECK(dot(oo_unit(gm.dir(Vec3(0, 0, 1))), f.normal) > 0.999f);
        CHECK(std::isfinite(bp->width) && std::isfinite(bp->head) && bp->width > 0.0f && bp->head > 0.0f);
        /* The size the user asked for, in metres: the piece's numbers are in the Blockout's units. */
        const float across_scale = length(fx.bo->world_matrix().dir(w.dir.x != 0.0f ? Vec3(0, 0, 1) : Vec3(1, 0, 0)));
        const float up_scale = length(fx.bo->world_matrix().dir(Vec3(0, 1, 0)));
        const bool metres = std::fabs(bp->width * across_scale - 0.9f) < 0.02f && std::fabs(bp->head * up_scale - 2.1f) < 0.02f;
        const bool units = std::fabs(bp->width - 0.9f) < 0.02f && std::fabs(bp->head - 2.1f) < 0.02f;
        if (!metres) std::printf("    scale (%g %g %g) wall %s: width %.4f head %.4f (as units: %d)\n", (double)s.x, (double)s.y, (double)s.z, w.name, (double)bp->width, (double)bp->head, units);
        CHECK(metres);
        oo_remove_openings(fx.ed, *fx.bo);
      }
    }
  });

  test("draw piece door (edge): a Blockout scaled 0.01x is too small for any door - the drag is refused with 'Too small' and nothing is made", [] {
    Fixture fx;
    fx.make(0.0f, 0.0f, Vec3(0.0f));
    fx.bo->set_local_scale(Vec3(0.01f));
    oo_built(fx.ed, fx.bo);
    const WallFrame f = oo_wall_frame(*fx.bo, *fx.room, kWalls[0], 0.0f);
    const Vec3 p = f.at(-0.01f, 0.0f);
    const size_t mark = oo_mark();
    fx.ed.command(strprintf("blockout draw door auto at %.6f %.6f %.6f normal %.6f %.6f %.6f size 0.02 0.03 0", (double)p.x, (double)p.y, (double)p.z, (double)f.normal.x, (double)f.normal.y,
                            (double)f.normal.z));
    oo_settle(fx.ed, 3);
    CHECK(oo_openings(*fx.bo) == 0);
    CHECK(!oo_log_since(mark).empty());
  });

  test("draw piece door (edge): after Door, choosing a box shape again makes a box - the drag on the ground goes to the height stage and no opening appears", [] {
    Fixture fx;
    fx.make(0.0f, 0.0f, Vec3(0.0f));
    fx.ed.command("blockout roomopt piece door");
    CHECK(fx.ed.room_tool_shape_for_test() == 10);
    fx.ed.command("blockout roomopt piece solid");
    CHECK(fx.ed.room_tool_shape_for_test() != 10 && fx.ed.room_tool_shape_for_test() != 11);
    fx.ed.command("camera 10 70 14 40 0 40");
    oo_settle(fx.ed, 2);
    const size_t pieces = oo_pieces(*fx.bo).size();
    CHECK(oo_drag(fx.ed, Vec3(40.0f, 0.0f, 40.0f), Vec3(42.0f, 0.0f, 42.0f)));
    CHECK(fx.ed.room_tool_stage_for_test() == 2);  // (a box asks for its height; a door never does)
    CHECK(oo_openings(*fx.bo) == 0);
    CHECK(oo_pieces(*fx.bo).size() == pieces);
    CHECK(fx.ed.statusbar_hint_for_test().find("Door") == std::string::npos);
    fx.ed.command("blockout roomopt piece window");
    CHECK(fx.ed.room_tool_shape_for_test() == 11);
    fx.ed.command("blockout roomopt piece wedge");
    CHECK(fx.ed.room_tool_shape_for_test() == 6);  // (the wedge row is not shifted by the negative door / window values)
  });

  test("draw piece window (edge): the status bar names Window while it is dragged and before, says R does nothing, and a box shape's bar has neither", [] {
    Fixture fx;
    fx.make(0.0f, 0.0f, Vec3(0.0f));
    fx.ed.command("blockout roomopt piece window");
    oo_settle(fx.ed, 2);
    std::string bar = fx.ed.statusbar_hint_for_test();
    CHECK(bar.find("Window: drag a rectangle on a wall") != std::string::npos);
    CHECK(bar.find("Door") == std::string::npos && bar.find("Box") == std::string::npos);
    const WallFrame f = oo_wall_frame(*fx.bo, *fx.room, kWalls[1], 0.0f);
    oo_look_at(fx.ed, f.at(0.0f, 1.0f), f.normal);
    int ax = 0, ay = 0, bx = 0, by = 0;
    CHECK(fx.ed.project_to_window(f.at(-0.5f, 0.75f), ax, ay) && fx.ed.project_to_window(f.at(0.5f, 2.0f), bx, by));
    fx.ed.step_frame_headless({oo_ev(ET::MouseMove, ax, ay)});
    fx.ed.step_frame_headless({oo_ev(ET::MouseDown, ax, ay)});
    fx.ed.step_frame_headless({oo_ev(ET::MouseMove, bx, by)});
    CHECK(fx.ed.room_tool_stage_for_test() == 1);
    bar = fx.ed.statusbar_hint_for_test();
    CHECK(bar.find("Window: release to cut the opening") != std::string::npos);
    CHECK(bar.find("R does nothing") != std::string::npos);
    CHECK(bar.find("right-click: drop it") != std::string::npos);
    CHECK(fx.ed.room_tool_label_for_test().find("Window") != std::string::npos);
    CHECK(fx.ed.room_tool_preview_lines_for_test() == 4);
    fx.ed.step_frame_headless({oo_ev(ET::MouseUp, bx, by)});
    oo_settle(fx.ed, 3);
    CHECK(oo_openings(*fx.bo) == 1);
    fx.ed.command("blockout roomopt piece stairs");
    bar = fx.ed.statusbar_hint_for_test();
    CHECK(bar.find("Window") == std::string::npos && bar.find("R does nothing") == std::string::npos);
  });

  test("draw piece door (edge): at 1024x640 with the UI at 1.5x the Shape list lies inside the window, every row whole; Home / End and a click on the heading never pick it; the closed list never shows 'Openings'", [] {
    Editor ed;
    ed.init_headless(1024, 640);
    ed.set_ui_scale_for_test(1.5f);
    ed.set_guides_for_test(false);
    ed.record_buttons_for_test(true);
    ed.command("layout Default");
    ed.command("window Modeling Tools");
    ed.command("alltools on");
    ed.command("blockout roomopt piece door");
    oo_settle(ed, 4);
    Recti combo;
    CHECK(ed.button_rect_for_test("tools:shape", combo));
    if (combo.empty()) return;
    /* The Tools window is longer than the screen here (the combo is below the fold): the keyboard reaches it, as ADR 0145 asks, and opens the list. */
    const Recti window = ed.window_rect_for_test(WindowKind::Tools);
    ed.step_frame_headless({oo_ev(ET::MouseMove, window.x + window.w / 2, window.y + window.h / 2)});
    auto press = [&](int k) {
      ed.step_frame_headless({oo_key(k)});
      ed.step_frame_headless({oo_key(k, false)});
      ed.step_frame_headless();
    };
    press(platform::KEY_F6);
    bool focused = false;
    for (int i = 0; i < 300 && !focused; i++) {
      ed.button_rect_for_test("tools:shape", combo);
      const Recti f = ed.ui_for_test().focus_rect_for_test();
      focused = !f.empty() && f.x == combo.x && f.y == combo.y && f.w == combo.w && f.h == combo.h;
      if (!focused) press(platform::KEY_TAB);
    }
    CHECK(focused);
    if (!focused) return;
    press(platform::KEY_SPACE);
    const Recti pop = ed.top_popup_rect_for_test();
    std::printf("    combo %d,%d %dx%d  popups %zu  popup %d,%d %dx%d\n", combo.x, combo.y, combo.w, combo.h, ed.popup_count_for_test(), pop.x, pop.y, pop.w, pop.h);
    CHECK(!pop.empty());
    CHECK(pop.x >= 0 && pop.y >= 0 && pop.x + pop.w <= 1024 && pop.y + pop.h <= 640);
    int rows = 0, outside = 0, clipped = 0;
    Recti heading_rect;
    for (const auto &c : ed.ui_for_test().controls) {
      const std::string kind = c.kind;
      if (kind != "heading" && kind != "menu") continue;
      if (c.r.y < pop.y - 1 || c.r.y + c.r.h > pop.y + pop.h + 1 || c.r.x < pop.x - 1 || c.r.x + c.r.w > pop.x + pop.w + 1) continue;  // (another menu's row)
      rows++;
      if (c.r.x < 0 || c.r.y < 0 || c.r.x + c.r.w > 1024 || c.r.y + c.r.h > 640) outside++;
      if (ed.text_width_for_test(c.label) > c.r.w) clipped++;
      if (kind == "heading" && c.label == "Openings") heading_rect = c.r;
    }
    CHECK(rows >= 11);  // (nine shapes + Door + Window, headings besides)
    CHECK(outside == 0 && clipped == 0);
    CHECK(!heading_rect.empty());
    /* A click on the heading: the list stays, the shape is unchanged. */
    oo_click(ed, heading_rect.x + heading_rect.w / 2, heading_rect.y + heading_rect.h / 2);
    oo_settle(ed, 2);
    CHECK(ed.room_tool_shape_for_test() == 10);
    CHECK(ed.top_popup_rect_for_test().empty() == false);
    /* Home and End and Enter: a choice, never the heading (row 9). */
    for (int k : {platform::KEY_HOME, platform::KEY_END, platform::KEY_HOME}) {
      ed.step_frame_headless({oo_key(k)});
      ed.step_frame_headless({oo_key(k, false)});
      ed.step_frame_headless();
      const int shape = ed.room_tool_shape_for_test();
      CHECK(shape != 9);
    }
    ed.step_frame_headless({oo_key(platform::KEY_ENTER)});
    ed.step_frame_headless({oo_key(platform::KEY_ENTER, false)});
    oo_settle(ed, 2);
    CHECK(ed.room_tool_shape_for_test() != 9);
    CHECK(ed.room_tool_shape_for_test() >= 0 && ed.room_tool_shape_for_test() <= 11);
    /* The closed dropdown shows the choice's name, never a heading. */
    ed.step_frame_headless({oo_key(platform::KEY_ESCAPE)});
    ed.step_frame_headless({oo_key(platform::KEY_ESCAPE, false)});
    oo_settle(ed, 3);
    for (const auto &c : ed.ui_for_test().controls) CHECK(c.label != "Openings");
  });

  test("draw piece door: switching to Door or Window (or out of them) drops a footprint in progress - by the console and by the dropdown - and a click then makes no piece and leaves stage 0", [] {
    Fixture fx;
    fx.make(0.0f, 0.0f, Vec3(0.0f));
    fx.ed.command("blockout roomopt piece solid");
    fx.ed.command("camera 10 70 14 40 0 40");
    oo_settle(fx.ed, 2);
    const size_t pieces = oo_pieces(*fx.bo).size();
    CHECK(oo_drag(fx.ed, Vec3(40.0f, 0.0f, 40.0f), Vec3(42.0f, 0.0f, 42.0f)));
    CHECK(fx.ed.room_tool_stage_for_test() == 2);
    fx.ed.command("blockout roomopt piece door");
    CHECK(fx.ed.room_tool_stage_for_test() == 0 && fx.ed.room_tool_shape_for_test() == 10);
    int cx = 0, cy = 0;
    CHECK(fx.ed.project_to_window(Vec3(41.0f, 0.0f, 41.0f), cx, cy));
    oo_click(fx.ed, cx, cy);
    oo_settle(fx.ed, 3);
    CHECK(oo_pieces(*fx.bo).size() == pieces && fx.ed.room_tool_stage_for_test() == 0);
    /* Out of an opening mid-drag is the same: Door chosen, a wall press begun, then a box chosen. */
    fx.ed.command("blockout roomopt piece solid");
    CHECK(oo_drag(fx.ed, Vec3(40.0f, 0.0f, 40.0f), Vec3(42.0f, 0.0f, 42.0f)));
    CHECK(fx.ed.room_tool_stage_for_test() == 2);
    fx.ed.command("blockout roomopt piece window");
    CHECK(fx.ed.room_tool_stage_for_test() == 0);
    /* The dropdown, by real events. */
    fx.ed.command("blockout roomopt piece solid");
    fx.ed.command("window Modeling Tools");
    fx.ed.command("alltools on");
    fx.ed.record_buttons_for_test(true);
    oo_settle(fx.ed, 4);
    CHECK(oo_drag(fx.ed, Vec3(40.0f, 0.0f, 40.0f), Vec3(42.0f, 0.0f, 42.0f)));
    CHECK(fx.ed.room_tool_stage_for_test() == 2);
    Recti combo, row;
    CHECK(fx.ed.button_rect_for_test("tools:shape", combo));
    oo_click(fx.ed, combo.x + combo.w / 2, combo.y + combo.h / 2);
    oo_settle(fx.ed, 2);
    CHECK(fx.ed.button_rect_for_test("menu:Door", row));
    oo_click(fx.ed, row.x + row.w / 2, row.y + row.h / 2);
    oo_settle(fx.ed, 3);
    CHECK(fx.ed.room_tool_shape_for_test() == 10 && fx.ed.room_tool_stage_for_test() == 0);
    CHECK(oo_click_ground_makes_nothing(fx));
  });

  test("draw piece window: a wall leaning 5 degrees is refused saying 'leans 5.0'; one leaning 1.5 degrees takes the window", [] {
    for (const float lean : {5.0f, 1.5f}) {
      Editor ed;
      ed.init_headless(1600, 1000);
      oo_empty(ed);
      GameObject *bo = oo_blockout(ed, 0.0f, Vec3(0.0f));
      GameObject *room = oo_room(ed, *bo, 0.0f);
      const float a = lean * 3.14159265f / 180.0f, c = std::cos(a), s = std::sin(a);
      room->set_local_rotation(quat_from_axes(Vec3(c, s, 0), Vec3(-s, c, 0), Vec3(0, 0, 1)));  // (about Z: the +X and -X walls lean)
      oo_built(ed, bo);
      const Mat4 rm = room->world_matrix();
      const Vec3 p = rm.point(Vec3(3.2f, 0.0f, 0.0f)), n = oo_unit(rm.dir(Vec3(1, 0, 0)));
      const size_t mark = oo_mark();
      ed.command(strprintf("blockout draw window auto at %.6f %.6f %.6f normal %.6f %.6f %.6f size 1 1 0", (double)p.x, (double)p.y, (double)p.z, (double)n.x, (double)n.y, (double)n.z));
      oo_settle(ed, 3);
      const std::string log = oo_log_since(mark);
      if (lean > 2.0f) {
        CHECK(oo_openings(*bo) == 0);
        CHECK(log.find("leans 5.0") != std::string::npos);
      }
      else {
        if (oo_openings(*bo) != 1) std::printf("    1.5 degrees: %s", log.c_str());
        CHECK(oo_openings(*bo) == 1);
      }
    }
  });

  test("draw piece door: on a room turned 90 degrees about X (a room drawn on a wall, lying on its side) a door dragged from the room's real floor up 2.1 m is made standing on that floor, and a window's sill is the drag's lower edge", [] {
    Editor ed;
    ed.init_headless(1600, 1000);
    oo_empty(ed);
    GameObject *bo = oo_blockout(ed, 0.0f, Vec3(0.0f));
    GameObject *room = oo_room(ed, *bo, 0.0f);
    room->set_local_rotation(quat_from_axes(Vec3(1, 0, 0), Vec3(0, 0, 1), Vec3(0, -1, 0)));  // (90 degrees about X: its local up is the Blockout's +Z)
    oo_built(ed, bo);
    /* Its local Z walls are now the Blockout's floor (the air's bottom at y = 1.5 - 3 = -1.5) and roof; the +X wall is upright at x = 3.2. */
    ed.command("blockout draw door auto at 3.2 -1.5 0.5 normal 1 0 0 size 1 2.1 0");
    oo_settle(ed, 3);
    GameObject *g = oo_opening(*bo);
    const BlockoutPiece *bp = g ? g->get<BlockoutPiece>() : nullptr;
    CHECK(bp != nullptr);
    if (bp) {
      CHECK_NEAR(bp->head, 2.1f, 1e-3f);
      CHECK_NEAR(bp->sill, 0.0f, 1e-4f);
      CHECK_NEAR(g->local().position.y, -1.5f, 0.02f);  // (the floor it stands on)
      CHECK(dot(oo_unit(g->world_matrix().dir(Vec3(0, 1, 0))), Vec3(0, 1, 0)) > 0.9999f);
      CHECK(dot(oo_unit(g->world_matrix().dir(Vec3(0, 0, 1))), Vec3(1, 0, 0)) > 0.999f);
    }
    oo_remove_openings(ed, *bo);
    ed.command("blockout draw window auto at 3.2 -0.5 0.5 normal 1 0 0 size 1 1.2 0");
    oo_settle(ed, 3);
    g = oo_opening(*bo);
    bp = g ? g->get<BlockoutPiece>() : nullptr;
    CHECK(bp != nullptr);
    if (bp) CHECK_NEAR(g->local().position.y + bp->sill, -0.5f, 0.02f);  // (the sill is at the drag's lower edge)
  });

  test("draw piece window: a window dragged from far under the floor is refused saying where the floor is; a few centimetres under are raised to the floor and the status says so; Door with another operation word is refused with the usage", [] {
    Fixture fx;
    fx.make(0.0f, 0.0f, Vec3(0.0f));
    const WallFrame f = oo_wall_frame(*fx.bo, *fx.room, kWalls[0], 0.0f);
    auto window_at = [&](float up) {
      const Vec3 p = f.at(-0.5f, up);
      fx.ed.command(strprintf("blockout draw window auto at %.6f %.6f %.6f normal %.6f %.6f %.6f size 1 1.5 0", (double)p.x, (double)p.y, (double)p.z, (double)f.normal.x, (double)f.normal.y,
                              (double)f.normal.z));
      oo_settle(fx.ed, 3);
    };
    size_t mark = oo_mark();
    window_at(-0.15f);
    CHECK(oo_openings(*fx.bo) == 0);
    CHECK(oo_log_since(mark).find("above the lower edge") != std::string::npos);
    mark = oo_mark();
    window_at(-0.03f);
    const BlockoutPiece *bp = oo_opening(*fx.bo) ? oo_opening(*fx.bo)->get<BlockoutPiece>() : nullptr;
    CHECK(bp != nullptr);
    if (bp) CHECK_NEAR(bp->sill, 0.0f, 1e-4f);
    CHECK(oo_log_since(mark).find("raised 3 cm") != std::string::npos);
    oo_remove_openings(fx.ed, *fx.bo);
    mark = oo_mark();
    const Vec3 p = f.at(-0.5f, 0.0f);
    fx.ed.command(strprintf("blockout draw door add at %.6f %.6f %.6f normal %.6f %.6f %.6f size 1 2 0", (double)p.x, (double)p.y, (double)p.z, (double)f.normal.x, (double)f.normal.y,
                            (double)f.normal.z));
    oo_settle(fx.ed, 3);
    CHECK(oo_openings(*fx.bo) == 0);
    CHECK(oo_log_since(mark).find("blockout draw door|window auto") != std::string::npos);
  });

  test("draw piece door: the same console words after an undo and an unrelated edit make the door again (no stale wall is read); the wall kept by a drag is dropped when it ends", [] {
    Fixture fx;
    fx.make(0.0f, 0.0f, Vec3(0.0f));
    const WallFrame f = oo_wall_frame(*fx.bo, *fx.room, kWalls[0], 0.0f);
    const Vec3 p = f.at(-0.5f, 0.0f);
    const std::string cmd = strprintf("blockout draw door auto at %.6f %.6f %.6f normal %.6f %.6f %.6f size 1 2.1 0", (double)p.x, (double)p.y, (double)p.z, (double)f.normal.x,
                                      (double)f.normal.y, (double)f.normal.z);
    fx.ed.command(cmd);
    oo_settle(fx.ed, 3);
    CHECK(oo_openings(*fx.bo) == 1);
    fx.ed.command("undo");
    oo_settle(fx.ed, 3);
    fx.ed.command("create Cube");  // (an edit that clears the redo steps and frees the scene the undo left)
    oo_settle(fx.ed, 3);
    fx.ed.command(cmd);
    oo_settle(fx.ed, 3);
    GameObject *bo = fx.ed.scene().find_by_name("Blockout");
    CHECK(bo != nullptr && oo_openings(*bo) == 1);
    GameObject *door = bo ? oo_opening(*bo) : nullptr;
    const BlockoutPiece *bp = door ? door->get<BlockoutPiece>() : nullptr;
    if (!bp) return;  // (the check above reports it; a crash would hide the rest of the suite)
    CHECK_NEAR(bp->width, 1.0f, 1e-3f);
    CHECK_NEAR(bp->head, 2.1f, 1e-3f);
  });

  test("draw piece door: on the room's inner face a drag may reach the air's corner and is not held (no 'held to the wall' in the status); only the outer face keeps off the walls that meet it", [] {
    Fixture fx;
    fx.make(0.0f, 0.0f, Vec3(0.0f));
    const size_t mark = oo_mark();
    /* The +X wall seen from inside: x = 3.0, its normal -X, the right of a viewer facing it is +Z; the air's corner is at z = 3.0. From z = 2.2 to 2.9... and then to the corner. */
    fx.ed.command("blockout draw door auto at 3.0 0 2.2 normal -1 0 0 size 0.7 2.1 0");
    oo_settle(fx.ed, 3);
    GameObject *g = oo_opening(*fx.bo);
    const BlockoutPiece *bp = g ? g->get<BlockoutPiece>() : nullptr;
    CHECK(bp != nullptr);
    if (bp) CHECK_NEAR(bp->width, 0.7f, 1e-3f);
    CHECK(oo_log_since(mark).find("held to the wall") == std::string::npos);
    oo_remove_openings(fx.ed, *fx.bo);
    fx.ed.command("blockout draw door auto at 3.0 0 2.2 normal -1 0 0 size 2 2.1 0");  // (past the corner: held to the air, which is 0.8 m from the start)
    oo_settle(fx.ed, 3);
    g = oo_opening(*fx.bo);
    bp = g ? g->get<BlockoutPiece>() : nullptr;
    CHECK(bp != nullptr);
    if (bp) CHECK_NEAR(bp->width, 0.8f, 0.01f);
  });
}
