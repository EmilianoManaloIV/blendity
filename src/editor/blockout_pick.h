// SPDX-License-Identifier: GPL-2.0-or-later
// Blockout: a piece picked by its outline (round 494, task 0478). A door, a window or a piece that cuts is air where it is seen
// head-on; its outline is what can be clicked.
#pragma once

#include "../render/raster.h"
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

/* The Draw Piece (Room) tool's choices that `Editor::RoomTool` has no field for (round 495, task 0479: editor.h was another
 * pass's that round - they move into RoomTool when it is free). One set for the program, not one an editor: the program has one
 * editor, and its Scene views share one tool. They hold only while the tool's Solid is on - `blockout_draw_forget` is called
 * where Solid goes off and where the tool is switched on with Solid off - so a second editor in one program (the tests) starts
 * from the defaults unless it turns Solid on before the tool. Subtract itself is RoomTool's `subtract`. */
struct BlockoutDrawChoice {
  int piece = 0;         // 0 a box, cylinder or outline as the footprint says; 3 a wedge, 4 stairs, 5 an arch (blockout::Shape)
  int operation = -1;    // -1 by the direction unless RoomTool::subtract (pushed in subtracts, pulled out adds); 0 Add; 2 Intersect
  uint64_t surface = 0;  // the Blockout whose face the footprint in progress was started on (0: none) - the piece goes into it
};
BlockoutDrawChoice &blockout_draw_choice();
void blockout_draw_forget();  // (the piece and the operation back to their defaults; the surface is the footprint's and stays)

/* The operation of the solid the tool makes at this height: 0 Add, 1 Subtract, 2 Intersect (a room: 0). Auto goes by the
 * direction, and adds when the Blockout has nothing in it to cut (`nothing_to_cut`). */
int blockout_draw_operation(bool solid, bool subtract, float height, bool nothing_to_cut);

/* The Blockout whose mesh has a face at a surface point (`normal` the face's, toward the eye); 0: none there. */
uint64_t blockout_at_surface(const Scene &scene, Vec3 point, Vec3 normal);

}  // namespace bl
