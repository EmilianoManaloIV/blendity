// SPDX-License-Identifier: GPL-2.0-or-later
// Quad remeshing through QuadriFlow (ADR 0077, task 0241): a smooth 4-direction field on the surface, its
// singularities, a quad mesh integrated from the position field - Blender's Quad Remesh, ZRemesher's class of result
// without its guides. Behind BL_WITH_QUADRIFLOW (the bundled sources compile when Eigen is present).
#pragma once

#include "mesh.h"

#include <atomic>
#include <string>

namespace bl::meshops {

struct QuadRemeshParams {
  int target_faces = 2000;
  bool preserve_sharp = false;
  bool preserve_boundary = false;
  bool adaptive_scale = false;
  int seed = 0;
};

struct QuadRemeshStats {
  size_t verts_in = 0, faces_in = 0, verts_out = 0, faces_out = 0;
  double ms_total = 0.0;
};

/* Whether this build has QuadriFlow. */
bool quad_remesh_available();
/* The mesh remeshed to quads of about `target_faces`. `cancel`, when given, is read between QuadriFlow's stages: set,
 * the function returns false with why = "cancelled". The mask and the vertex groups are carried by position; groups,
 * UVs, seams, sharp edges and shape keys are not. False, with `why`, without the library, for a mesh with no faces,
 * or when QuadriFlow finds no quads. */
bool quad_remesh(const Mesh &in, const QuadRemeshParams &p, Mesh &out, QuadRemeshStats *stats = nullptr, std::string *why = nullptr,
                 const std::atomic<bool> *cancel = nullptr);

}  // namespace bl::meshops
