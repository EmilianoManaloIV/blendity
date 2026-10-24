// SPDX-License-Identifier: GPL-2.0-or-later
// What uv.cpp's unwrapper, uv_abf.cpp and uv_parts.cpp share (task 0179): a chart's vertices as corner fans and its
// triangles in one order, so ABF++'s angles line up with LSCM's triangles.
#pragma once

#include "mesh.h"

#include <array>
#include <vector>

namespace bl::uvops::detail {

/* A set of faces as a chart: its corners (in face order), each corner's chart vertex (corners of one mesh vertex are
 * one chart vertex only when joined across non-seam edges inside the chart: a seam or the chart's edge splits them),
 * each chart vertex's mesh vertex, and the triangles - each face fanned from its first corner (k = 1 .. n - 2), in
 * face order - as chart vertices. */
struct ChartTopology {
  std::vector<uint32_t> corners;          // slot -> mesh corner
  std::vector<int> local;                 // slot -> chart vertex
  std::vector<uint32_t> verts;            // chart vertex -> mesh vertex
  std::vector<std::array<int, 3>> tris;   // chart vertices, fans in face order (degenerate ones included)
};
ChartTopology chart_topology(const Mesh &m, const std::vector<uint32_t> &faces);

/* LSCM on the faces (uv.cpp's island unwrap); `tri_angles` (3 per triangle of chart_topology's order, radians):
 * each triangle's shape from these angles instead of its 3D one (ABF++'s, Sheffer et al. 2005: LSCM then lays out the
 * angles it found). */
void lscm_chart(Mesh &m, const std::vector<uint32_t> &faces, const std::vector<float> *tri_angles = nullptr, bool parallel = true);

/* ABF++ (uv_abf.cpp): angles for the chart's triangles that are as close to the 3D ones as a flat layout allows.
 * False when it doesn't converge (the caller uses LSCM's own), or without Eigen (the dependency-free build). */
bool abf_angles(const Mesh &m, const ChartTopology &chart, std::vector<float> &tri_angles);

}  // namespace bl::uvops::detail
