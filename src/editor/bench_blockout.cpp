// SPDX-License-Identifier: GPL-2.0-or-later
// `benchscene blockout | blockout 400` (round 483, task 0459): an interior of about 100 pieces (400 for the bigger one) in ONE
// Blockout, the size of real work - two storeys of box rooms that share their walls, with a round room, an L-shaped room and
// a room that keeps its own walls built on; doors between the rooms and windows in the outer walls, a staircase with the well
// above it, two arched openings, a gate arch, a colonnade and one blended column on a plinth; floor, wall, ceiling and outside
// materials; two lamps. What it measures: dragging a piece of a Blockout this size (the mesh follows within 100 ms), baking,
// lighting and exporting it. Deterministic: no clock, no random number, no address - every run gives the same pieces in the
// same order, so the same mesh bytes. The mesh is not built here: the Blockout walk asks for it through the normal background
// job, so the scene appears first and the mesh lands.
#include "editor.h"

#include "../core/core.h"

#include <algorithm>
#include <cmath>

namespace bl {

namespace {

constexpr float kAir = 4.0f, kHeight = 2.6f, kWall = 0.2f;
constexpr float kPitch = kAir + kWall;           // rooms side by side share one wall: the pitch is the air and one wall
constexpr float kStorey = kHeight + kWall;       // a ceiling is the next storey's floor

/* The interior's grid and the pieces made under its Blockout, so the parts of the build are small functions of their own. */
struct Interior {
  Scene &scene;
  GameObject &bo;
  int cols, rows;
  const int storeys = 2;
  const Quat turned = Quat::euler({0.0f, 90.0f, 0.0f});  // (an opening in a wall that faces along X)

  float cx(int i) const { return ((float)i - 0.5f * (float)(cols - 1)) * kPitch; }
  float cz(int j) const { return ((float)j - 0.5f * (float)(rows - 1)) * kPitch; }
  float x_west() const { return cx(0); }
  float x_east() const { return cx(cols - 1); }
  float z_front() const { return cz(0); }
  float z_back() const { return cz(rows - 1); }
  float z_gate() const { return z_front() - kPitch * 0.5f - 3.0f; }

  /* A piece under the Blockout; `shape` and `op` as BlockoutPiece's (0 box, 1 cylinder, 4 stairs, 5 arch, 6 outline; 0 add, 1 subtract). */
  BlockoutPiece *piece(const std::string &name, int shape, int op, Vec3 at, GameObject **made = nullptr) {
    GameObject *g = scene.create(name, &bo);
    g->set_local_position(at);
    BlockoutPiece *bp = g->add<BlockoutPiece>();
    bp->shape = shape, bp->operation = op;
    if (made) *made = g;
    return bp;
  }
  BlockoutPiece *room_at(const std::string &name, int shape, Vec3 at) {
    BlockoutPiece *bp = piece(name, shape, 0, at);
    bp->room = true, bp->wall = bp->floor = bp->ceiling = kWall;
    return bp;
  }
  BlockoutPiece *opening(const std::string &name, int kind, Vec3 at, bool along_x, float width, float head, float sill, float depth = 0.5f) {
    GameObject *g = nullptr;
    BlockoutPiece *bp = piece(name, 0, 1, at, &g);
    bp->opening = kind, bp->width = width, bp->head = head, bp->sill = sill, bp->depth = depth;
    if (along_x) g->set_local_rotation(turned);
    return bp;
  }
  BlockoutPiece *door(const std::string &name, Vec3 at, bool along_x, float depth = 0.5f) { return opening(name, 1, at, along_x, 0.9f, 2.1f, 0.0f, depth); }
  BlockoutPiece *window(const std::string &name, Vec3 at, bool along_x) { return opening(name, 2, at, along_x, 1.2f, 2.0f, 0.9f); }
  /* The round head of an arched door: a half-disc Subtract piece over the opening. */
  void arch_head(const std::string &name, Vec3 at, bool along_x) {
    GameObject *g = nullptr;
    BlockoutPiece *a = piece(name, 5, 1, at + Vec3(0.0f, 1.6f, 0.0f), &g);
    a->radius = 0.7f, a->inner_radius = 0.0f, a->height = 0.5f, a->angle = 180.0f, a->segments = 12;
    if (along_x) g->set_local_rotation(turned);
  }

  void rooms();
  void joined_rooms();
  void doors();
  void doors_of_storey(int s);
  void arched_or_plain_door(int s, int i, int j, bool along_x, Vec3 at, int &arches);
  void windows();
  void window_of(int s, int i, int j);
  void stairs();
  void gate();
  void colonnade(int target);
};

void Interior::rooms() {
  for (int s = 0; s < storeys; s++)
    for (int j = 0; j < rows; j++)
      for (int i = 0; i < cols; i++)
        room_at(strprintf("Room %d.%d.%d", s, i, j), 0, Vec3(cx(i), kHeight * 0.5f + (float)s * kStorey, cz(j)))->size = {kAir, kHeight, kAir};
}

/* A round room built on the front-east room's EAST wall, an L-shaped one on the front-west room's WEST wall (each overlaps the room it joins
 * by half a metre, the L Room by 0.6 m, so its air opens into it; the L Room stands a tenth further in because at 0.5 m the Blockout's exact path
 * (Blender's libraries) leaves six small faces on top of each other on the notch wall of an L-shaped room at this place), and a room that keeps its own walls BEHIND the back middle room: its front wall stands right
 * against that room's back wall (a double wall, 0.4 m), and a door deep enough to cut both leads in. */
void Interior::joined_rooms() {
  BlockoutPiece *round_room = room_at("Round Room", 1, Vec3(x_east() + 3.5f, kHeight * 0.5f, z_front()));
  round_room->radius = 2.0f, round_room->height = kHeight, round_room->segments = 32;
  BlockoutPiece *l_room = room_at("L Room", 6, Vec3(x_west() - 3.4f, kHeight * 0.5f, z_front()));
  l_room->height = kHeight, l_room->outline = {Vec2(-2.0f, -2.0f), Vec2(2.0f, -2.0f), Vec2(2.0f, 0.0f), Vec2(0.0f, 0.0f), Vec2(0.0f, 2.0f), Vec2(-2.0f, 2.0f)};
  const float wall_outer = z_back() + kAir * 0.5f + kWall;  // the back room's outer face
  BlockoutPiece *keep_room = room_at("Keep Room", 0, Vec3(cx(cols / 2), kHeight * 0.5f, wall_outer + kWall + kAir * 0.5f));
  keep_room->size = {kAir, kHeight, kAir};
  keep_room->walls = 1;
  door("Keep Door", Vec3(cx(cols / 2), 0.0f, z_back() + kAir * 0.5f + kWall), false, 4.0f * kWall);  // (its depth spans both walls, 0.4 m)
}

void Interior::arched_or_plain_door(int s, int i, int j, bool along_x, Vec3 at, int &arches) {
  const bool arched = s == 0 && (along_x ? (i == 1 && j == 1) : (i == 2 && j == 0));  // two of the ground floor's are arched instead
  const char axis = along_x ? 'X' : 'Z';
  if (!arched) return (void)door(strprintf("Door %c %d.%d.%d", axis, s, i, j), at, along_x);
  arches++;
  opening(strprintf("Arched Door %c %d", axis, arches), 1, at, along_x, 1.4f, 1.6f, 0.0f);
  arch_head(strprintf("Arch Head %c %d", axis, arches), at, along_x);
}

/* Doors through the shared walls, along X and along Z. */
void Interior::doors_of_storey(int s) {
  const float floor_y = (float)s * kStorey;
  int arches = 0;
  for (int j = 0; j < rows; j++)
    for (int i = 0; i + 1 < cols; i++) arched_or_plain_door(s, i, j, true, Vec3(cx(i) + 0.5f * kPitch, floor_y, cz(j)), arches);
  for (int j = 0; j + 1 < rows; j++)
    for (int i = 0; i < cols; i++) arched_or_plain_door(s, i, j, false, Vec3(cx(i), floor_y, cz(j) + 0.5f * kPitch), arches);
}

void Interior::doors() {
  for (int s = 0; s < storeys; s++) doors_of_storey(s);
}

/* A window in an outer wall of a room on the edge: the front and back walls first, the ends for the rooms between. The room built onto a wall
 * leaves no outer wall there: the back middle room has none in its back wall (the Keep Room is behind it), the corner rooms keep their front
 * windows (the Round Room covers the east wall of one, the L Room the west wall of the other, and a room has one window). */
void Interior::window_of(int s, int i, int j) {
  const float floor_y = (float)s * kStorey;
  const std::string name = strprintf("Window %d.%d.%d", s, i, j);
  const bool keep_behind = s == 0 && i == cols / 2;
  if (j == 0) window(name, Vec3(cx(i), floor_y, z_front() - 0.5f * kPitch), false);
  else if (j == rows - 1) {
    if (!keep_behind) window(name, Vec3(cx(i), floor_y, z_back() + 0.5f * kPitch), false);
  }
  else if (i == 0) window(name, Vec3(x_west() - 0.5f * kPitch, floor_y, cz(j)), true);
  else if (i == cols - 1) window(name, Vec3(x_east() + 0.5f * kPitch, floor_y, cz(j)), true);
}

void Interior::windows() {
  for (int s = 0; s < storeys; s++)
    for (int j = 0; j < rows; j++)
      for (int i = 0; i < cols; i++) window_of(s, i, j);
}

/* A flight of stairs in the ground room at (1, 1) climbing towards +Z, and the well in the next storey's floor over its TOP end (the head room
 * of the last steps; over the foot it would be the solid floor the top step runs into). */
void Interior::stairs() {
  const float sx = cx(1), sz = cz(rows > 1 ? 1 : 0), length = 3.0f;
  BlockoutPiece *flight = piece("Staircase", 4, 0, Vec3(sx, kHeight * 0.5f, sz));
  flight->size = {1.6f, kHeight, length}, flight->steps = 14;
  const float well_z0 = sz + 0.3f, well_z1 = sz + 0.5f * length + 0.4f;  // (from the upper part of the flight to just past its last step)
  /* Its bottom is 0.1 m under the flight's top, so the last step is cut flat and no sliver is left of the one below it (its top is 0.19 m lower). */
  const float well_y0 = kHeight - 0.1f, well_y1 = kHeight + kWall + 0.2f;
  BlockoutPiece *well = piece("Stair Well", 0, 1, Vec3(sx, 0.5f * (well_y0 + well_y1), 0.5f * (well_z0 + well_z1)));
  well->size = {1.6f, well_y1 - well_y0, well_z1 - well_z0};
}

/* The gate in front: an arch, then a colonnade, and one column on a plinth that is blended into it. */
void Interior::gate() {
  BlockoutPiece *arch = piece("Gate Arch", 5, 0, Vec3(cx(0), 0.0f, z_gate()));
  arch->radius = 1.5f, arch->inner_radius = 1.0f, arch->height = 0.5f, arch->angle = 180.0f, arch->segments = 16;
  piece("Plinth", 0, 0, Vec3(cx(0) + 4.0f, 0.15f, z_gate()))->size = {1.2f, 0.3f, 1.2f};
  BlockoutPiece *blended = piece("Blended Column", 1, 0, Vec3(cx(0) + 4.0f, 0.3f + 1.2f, z_gate()));
  blended->radius = 0.3f, blended->height = 2.4f, blended->segments = 24, blended->blend = 0.12f;
}

/* Columns along the colonnade up to the piece count asked for (none when the interior already has that many). */
void Interior::colonnade(int target) {
  size_t made = bo.children.size();  // (every child of the Blockout is a piece)
  const float row_x0 = cx(0) + 6.0f, col_step = 1.6f;
  for (int k = 0; (int)made < target; k++, made++) {
    BlockoutPiece *c = piece(strprintf("Column %d", k), 1, 0, Vec3(row_x0 + (float)k * col_step, kHeight * 0.5f, z_gate()));
    c->radius = 0.25f, c->height = kHeight, c->segments = 16;
  }
}

/* A written material for a class of face, under the project like worldlike's textures. A file that is there is the user's (a second run must
 * not discard what they changed in it): it is written only when missing. */
std::string bench_material(const char *name, Vec3 colour, float roughness) {
  const std::string path = std::string("Assets/Materials/BenchBlockout/") + name + ".mat";
  if (fs::exists(resolve_asset_path(path))) return path;
  MaterialPtr m = make_material(name, colour);
  m->roughness = roughness;
  fs::make_dirs(fs::parent(resolve_asset_path(path)));
  if (!create_material_asset(*m, path)) Log::warn("benchscene blockout: can't write the material %s (the walls keep the default look)", path.c_str());
  return path;
}

}  // namespace

void Editor::build_blockout_bench(int pieces) {
  const bool big = pieces >= 300;
  GameObject *bo = scene_->create("Interior", nullptr);
  Blockout *b = bo->add<Blockout>();
  bo->add<MeshFilter>()->mesh = std::make_shared<Mesh>();
  bo->add<MeshRenderer>()->contribute_gi = true;  // (as a new Blockout has it)
  b->material[kClassFloor] = bench_material("Floor", Vec3(0.55f, 0.38f, 0.24f), 0.6f);
  b->material[kClassWalls] = bench_material("Walls", Vec3(0.86f, 0.82f, 0.72f), 0.9f);
  b->material[kClassCeiling] = bench_material("Ceiling", Vec3(0.95f, 0.95f, 0.95f), 0.9f);
  b->material[kClassOutside] = bench_material("Outside", Vec3(0.55f, 0.57f, 0.6f), 0.8f);

  Interior in{*scene_, *bo, big ? 10 : 4, big ? 6 : 3};
  in.rooms();
  in.joined_rooms();
  in.doors();
  in.windows();
  in.stairs();
  in.gate();
  in.colonnade(pieces);

  /* Two lamps, Mixed so the scene is lit before any bake (a bake adds the indirect light): one in the stair room's ground storey, beside the
   * flight, and one in the far corner room upstairs. */
  const Vec3 lamp_at[2] = {Vec3(in.cx(1) + 1.4f, 2.0f, in.cz(in.rows > 1 ? 1 : 0)), Vec3(in.cx(in.cols - 1), kStorey + 2.0f, in.cz(in.rows - 1))};
  for (int k = 0; k < 2; k++) {
    GameObject *lamp = scene_->create(k == 0 ? "Lamp Ground" : "Lamp Upstairs");
    Light *l = lamp->add<Light>();
    l->type = 1, l->mode = 1, l->intensity = 3.0f, l->range = 30.0f;
    lamp->set_local_position(lamp_at[k]);
  }

  /* The starter scene's Main Camera stood at (0, 2, -5): inside the front rooms' shared wall. It stands outside, in front of the gate and
   * looking at the building, so the Game view, Play mode and F12 see the interior from outside. */
  if (GameObject *cam = scene_->find_by_name("Main Camera")) {
    cam->set_local_position(Vec3(0.0f, 3.0f, in.z_gate() - 9.0f));
    cam->set_local_rotation(Quat::euler({8.0f, 0.0f, 0.0f}));
  }

  /* Looked at from the front and above, the whole building in view. */
  const float w = (float)in.cols * kPitch + 12.0f, d = (float)in.rows * kPitch + 8.0f;
  command(strprintf("camera 25 30 %g %g %g %g", std::max(w, d) * 1.1f, 0.0f, kStorey, -3.0f));
  selection_ = {bo->id};
  active_ = bo->id;
  expanded_.insert(bo->id);
}

}  // namespace bl
