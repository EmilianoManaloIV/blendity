/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

// LoopTools built in (round 537, task 0517): Blender 5.3's five native LoopTools operators on Blendity's Mesh.
// Ported (not copied, no BMesh) from bmesh/operators/bmo_relax_edge_loops.cc, bmo_space_edge_loops_evenly.cc,
// bmo_circularize.cc, bmo_flatten.cc and bmo_curve_edge_loops_between_selected.cc with their editors/mesh/editmesh_*.cc.
//
// Every function reads the vertex selection (`vert_sel`, one byte a vertex), moves vertices of `m` in place and returns
// how many vertices ended at a different position (0 is allowed: Flatten on a flat face, Factor 0), or -1 when it refused
// (no usable chain / plane / selection, a face selected for Relax and Space, a bad number) and left the mesh untouched.
// Positions are never written as NaN / Inf; a chain with a bad coordinate or no fitting plane / circle is skipped.
// An edge is selected as `edge_selected` says (Edge mode's own edges while an EdgeSelectionScope is alive, else both ends
// selected); a chain is a run of selected edges, and a vertex with more than two of them ends chains.
// `face_sel` (optional, one byte a face) is the editor's real face selection in Face mode: the selected edges are then those
// of the selected faces, and Relax / Space refuse when any face is selected. Null: a face counts as selected when all its
// edges are (for Circularize and Flatten), and Relax / Space do not look at faces.
// `lock` points to three bools (X, Y, Z of the mesh's local space; null = none); a locked axis keeps its coordinate.
#pragma once

#include "mesh.h"

#include <cstdint>
#include <vector>

namespace bl::meshops {

/* Relax Edge Loops: two-phase spline relax of each chain; open chains keep their ends, chains under 3 vertices are skipped.
 * iterations 1..25; interpolation 0 cubic / 1 linear; even_spacing puts the relaxed vertices midway between their knots. */
int relax_loops(Mesh &m, const std::vector<uint8_t> &vert_sel, int iterations = 1, int interpolation = 0, bool even_spacing = true,
                const std::vector<uint8_t> *face_sel = nullptr);

/* Space Edge Loops Evenly: equal arc length along each chain's own curve (interpolation 0 cubic / 1 linear), blended by
 * factor 0..1; open chains keep their ends. */
int space_loops(Mesh &m, const std::vector<uint8_t> &vert_sel, float factor = 1.0f, int interpolation = 0, const bool *lock = nullptr,
                const std::vector<uint8_t> *face_sel = nullptr);

/* To Circle: closed chains (and a selected face region's boundary, and isolated wire loops) onto a circle.
 * fit 0 best fit / 1 interior (smallest circle that stays inside); angle in radians rotates the circle; use_custom_radius
 * takes `radius`; regular spaces the vertices at equal angles (open chains: over the arc they span); flatten 0..1 blends
 * between projecting the circle onto the adjoining faces (0) and leaving it flat (1). */
int circularize(Mesh &m, const std::vector<uint8_t> &vert_sel, float factor = 1.0f, int fit = 0, float angle = 0.0f,
                bool use_custom_radius = false, float radius = 1.0f, bool regular = true, float flatten = 1.0f,
                const bool *lock = nullptr, const std::vector<uint8_t> *face_sel = nullptr);

/* Flatten: each selected face region (faces joined by shared edges) and each selected loose edge group goes onto its own
 * plane. method 0 best fit / 1 average face normal (best fit for groups without faces) / 2 `view_dir` (mesh space). */
int flatten_loops(Mesh &m, const std::vector<uint8_t> &vert_sel, float factor = 1.0f, int method = 0,
                  Vec3 view_dir = Vec3(0.0f, 0.0f, 1.0f), const bool *lock = nullptr,
                  const std::vector<uint8_t> *face_sel = nullptr);

/* Curve Edge Loops Between Selected: the unselected vertices of the edge loops through selected ones follow a curve
 * through the selected vertices (a fully selected loop makes the loops across it the ones that move). interpolation
 * 0 cubic / 1 linear; clamp 0 none / 1 only raise / 2 only lower (along the vertex normal); extend_loop works past the
 * selected ends; regular spaces the moved vertices evenly. */
int curve_loops(Mesh &m, const std::vector<uint8_t> &vert_sel, float factor = 1.0f, int interpolation = 0, int clamp = 0,
                bool extend_loop = false, bool regular = true, const bool *lock = nullptr);

}  // namespace bl::meshops
