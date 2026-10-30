// SPDX-License-Identifier: GPL-2.0-or-later
// DynaMesh (ADR 0076, task 0240): a uniform retopology of a closed (or nearly closed) mesh - inside and outside by
// the winding number along grid lines, three votes a grid vertex, then naive surface nets with the lines' exact
// crossings, then tangential relaxation. Self-intersections merge; the result is a closed shell of quads.
#pragma once

#include "mesh.h"

namespace bl::meshops {

struct DynaMeshStats {
  int cells[3] = {0, 0, 0};  // (the grid's cells per axis)
  float cell = 0.0f;         // (the cell size, world units of the mesh's space)
  size_t verts_in = 0, faces_in = 0, verts_out = 0, faces_out = 0, crossings = 0;
  size_t filled_cells = 0;  // (cells filled in because their corners fell into two pieces: parts closer than a cell merged)
  double ms_columns = 0.0, ms_nets = 0.0, ms_smooth = 0.0, ms_total = 0.0;
};

/* `resolution` cells along the longest side (8-256), `smooth` relaxation passes (0-10). The mask and the vertex
 * groups are carried by position; groups, UVs, seams, sharp edges and shape keys are not. False, with `why`, for a
 * mesh with no faces or no extent. */
bool dynamesh(const Mesh &in, int resolution, int smooth, Mesh &out, DynaMeshStats *stats = nullptr, std::string *why = nullptr);

}  // namespace bl::meshops
