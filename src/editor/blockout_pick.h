// SPDX-License-Identifier: GPL-2.0-or-later
// Blockout: a piece picked by its outline (round 494, task 0478). A door, a window or a piece that cuts is air where it is seen
// head-on; its outline is what can be clicked.
#pragma once

#include "../render/raster.h"
#include "../scene/blockout_object.h"
#include "../scene/scene.h"

#include <cstdint>
#include <functional>

namespace bl {

/* Whether a Blockout's pieces show their outlines: the Blockout or any of its pieces is selected. */
bool blockout_outlines_shown(const GameObject &blockout, const std::function<bool(uint64_t)> &selected);

/* The piece whose drawn outline is within `reach` pixels of a view pixel: only pieces that have no surface of their own to click -
 * openings, Subtract and Intersect pieces - of Blockouts that show their outlines, and only where the outline is not behind the
 * picture (`rt`'s depth). The nearest outline wins. 0: none. */
uint64_t blockout_pick_outline(Scene &scene, const Renderer3D &r3d, const RenderTarget &rt, const std::function<bool(uint64_t)> &selected, int view_x,
                               int view_y, float reach);

/* The operation of the solid the tool makes at this height: 0 Add, 1 Subtract, 2 Intersect (a room: 0). `chosen` is RoomTool's
 * `operation`: 0 and 2 fix it; otherwise Auto goes by the direction, and adds when the Blockout has nothing in it to cut
 * (`nothing_to_cut`). `flip` (Shift held at the click, round 496) makes the other of Add and Subtract; Intersect has no other. */
int blockout_draw_operation(bool solid, int chosen, bool subtract, bool flip, float height, bool nothing_to_cut);

/* The Draw Piece tool's Shape dropdown entry for its fields (round 496): 0 Room, 1 Round Room, 2 Outline Room, 3 Box, 4 Cylinder, 5 Outline,
 * 6 Wedge, 7 Stairs, 8 Arch. `piece` is RoomTool::piece (3 wedge, 4 stairs, 5 arch). */
int blockout_draw_shape(bool solid, bool circle, bool polyline, int piece);

/* Whether a Blockout has no matter a cutter could cut: no piece that is on and adds (a room adds its shell). */
bool blockout_nothing_to_cut(const GameObject &blockout);

/* The Blockout whose mesh has a face at a surface point (`normal` the face's, toward the eye); 0: none there. */
uint64_t blockout_at_surface(const Scene &scene, Vec3 point, Vec3 normal);

/* The piece the Draw Piece tool's drags describe (round 496): its numbers and where it stands. ONE function makes it - the Scene view's preview
 * and `room_make` both call it, so what is shown is what the click makes. */
struct DrawnPiece {
  int shape = 0;           // blockout::Shape: 0 box, 1 cylinder, 3 wedge, 4 stairs, 5 arch
  bool room = false;
  int op = 0;              // 0 Add, 1 Subtract, 2 Intersect
  float height = 0.0f;     // along the normal, signed (a room's is made positive)
  Vec3 size{1, 1, 1};      // a box, wedge, stairs
  float radius = 0.5f, inner_radius = 0.25f, depth = 1.0f;  // a cylinder, an arch; depth: the cylinder's height, the arch's depth along its Z
  Quat rotation;           // the piece's world turn
  Vec3 centre;             // its world position
  bool directional = false;  // a wedge, stairs or an arch: it has a way it faces
  Vec3 rise;               // that way, along the surface (a wedge or stairs rise toward it, an arch bulges toward it)
  Vec3 foot;               // the surface point the arrow starts at
  float reach = 0.0f;      // how long the arrow is
};

/* `kind`: RoomTool::piece (0 a box or cylinder as `circle` says, 3 wedge, 4 stairs, 5 arch), `op` the operation already chosen, `u` and `normal` the
 * surface's axes, `du` and `dz` the footprint's signed sides (a circle: du its radius), `scale` the Blockout's even scale (the numbers are in its
 * units), `turn` quarter turns round the normal (a wedge, stairs or arch; the footprint's two sizes swap as it turns). */
DrawnPiece blockout_drawn_piece(bool solid, bool circle, int kind, int op, Vec3 origin, Vec3 u, Vec3 normal, float du, float dz, float height, float scale, int turn);

/* A BlockoutPiece (for the preview's wire) from a DrawnPiece; `wall` a room's thickness. */
BlockoutPiece blockout_piece_from_drawn(const DrawnPiece &d, float wall);

/* The wire of a piece standing at `world`, as a selected piece's outline is drawn: `seg` gets each line. With `shell`: a room's grown shell
 * (its walls, floor and ceiling) instead, nothing for a piece that is no room. */
void blockout_piece_wire(const BlockoutPiece &bp, const Mat4 &world, bool shell, const std::function<void(Vec3, Vec3)> &seg);

/* The colour an operation's outline has: Add orange, Subtract yellow, Intersect violet (a room: Add's). */
uint32_t blockout_operation_colour(int op, bool room);

/* Whether a piece can be turned in its footprint (a wedge, stairs or an arch that is no opening). */
bool blockout_turnable(const BlockoutPiece &bp);

}  // namespace bl
