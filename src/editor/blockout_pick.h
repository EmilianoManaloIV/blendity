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

}  // namespace bl
