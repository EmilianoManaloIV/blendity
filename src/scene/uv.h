// SPDX-License-Identifier: GPL-2.0-or-later
// UV mapping operators (Blender: UV menu in the 3D viewport / UV Editor;
// source: blender/source/blender/geometry/intern/uv_parametrizer.cc,
// uv_pack.cc and blender/source/blender/editors/uvedit). Unity has no built-in
// UV tools - meshes arrive with UVs from a DCC tool or ProBuilder.
// All operators work on the face mask (null = every face) and keep the
// rest of the UV map untouched.
// Theory: FoCG ch. 11.2 "Texture Coordinate Functions".
#pragma once

#include "mesh.h"

#include <vector>

namespace bl::uvops {

using Mask = std::vector<uint8_t>;

/* Faces connected through non-seam edges (by_seams) or through shared UVs. */
int compute_islands(const Mesh &m, const Mask *mask, bool by_seams, std::vector<int> &face_island);

/* Unwrap (Least Squares Conformal Maps, Levy et al. 2002 - Blender's
 * "Conformal" method) per seam-bounded island, then pack. Returns islands. */
int unwrap_lscm(Mesh &m, const Mask *mask, float margin = 0.02f);
/* Smart UV Project: cluster faces by normal, planar-project, pack. */
int smart_project(Mesh &m, const Mask *mask, float angle_limit_deg = 66.0f, float margin = 0.02f);
void project_cube(Mesh &m, const Mask *mask, float cube_size = 1.0f);
void project_cylinder(Mesh &m, const Mask *mask);
void project_sphere(Mesh &m, const Mask *mask);
/* Project From View (Bounds): object_to_clip maps object space to clip space. */
void project_view(Mesh &m, const Mask *mask, const Mat4 &object_to_clip);
/* Reset: every face covers the whole 0-1 square (Blender: UV > Reset). */
void reset(Mesh &m, const Mask *mask);
/* Pack islands into 0-1 (Blender: Pack Islands, shelf + rotation). */
void pack_islands(Mesh &m, const Mask *mask, float margin = 0.02f, bool rotate = true);
/* Scale islands so texel density matches across the mesh. */
void average_island_scale(Mesh &m, const Mask *mask);
/* Mark or clear seams on edges whose two vertices are selected. */
int set_seams_from_vertices(Mesh &m, const std::vector<uint8_t> &vert_sel, bool mark);

/* Per-face area distortion: (uv area / 3D area) / mesh average. 1 = no stretch. */
std::vector<float> face_area_stretch(const Mesh &m);

}  // namespace bl::uvops
