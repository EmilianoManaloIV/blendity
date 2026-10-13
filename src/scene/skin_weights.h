// SPDX-License-Identifier: GPL-2.0-or-later
// Skin weights by inpainting (task 0057, ADR 0028): Abdrashitov, Raichstat, Monteiro, Stoll, Dvoroznak, Jacobson,
// "Robust Skin Weights Transfer via Weight Inpainting" (Epic Games, SIGGRAPH Asia 2023). Trusted vertices keep their
// weights; the others get the smoothest weights that agree with them: minimise trace(W^T Q W), Q = -L + L M^-1 L
// (L the cotangent Laplacian, M the lumped Voronoi mass), with the trusted rows fixed.
#pragma once

#include "mesh.h"

#include <atomic>
#include <string>
#include <vector>

namespace bl {

/* The vertex group whose vertices (weight over 0.5) are always inpainted, never copied (the paper's section 3.4). */
constexpr const char *kInpaintMask = "InpaintMask";

struct InpaintOptions {
  float distance = 0.05f;  // transfer: a match lies within this fraction of the target's bounding-box diagonal
  float angle = 30.0f;     // transfer: and its normal within this many degrees of the vertex's
  bool flipped = true;     // transfer: a normal facing the other way matches too (a garment's inner layer)
  float ratio = 0.8f;      // automatic: the nearest bone at most this fraction of the second nearest's distance
  int smooth = 0;          // Laplacian smoothing passes over the inpainted vertices afterwards
  std::atomic<float> *progress = nullptr;      // 0..1, written as it goes (may be null)
  const std::atomic<bool> *cancel = nullptr;  // stops early, returning false (may be null)
};

struct InpaintReport {
  size_t known = 0;      // vertices whose weights were trusted
  size_t inpainted = 0;  // vertices solved for
  size_t fallback = 0;   // vertices the solve can't reach (a loose part with nothing trusted): their first guess
  size_t groups = 0;     // groups solved
  const char *solver = "";
  bool converged = true;  // false: conjugate gradients stopped at their iteration limit (the weights are approximate)
  double ms = 0.0;
};

/* A deforming bone at rest, in the mesh's space. */
struct BoneSegment {
  std::string name;
  Vec3 head, tail;
};

namespace skinweights {

/* The core solve. weights[k][v]: group k at vertex v. Rows with known[v] stay; the others hold a first guess (the
 * distance weights, the closest point's) and are replaced by the inpainted ones (clamped to [0, 1]; renormalised over
 * all groups when `normalize`). An unknown vertex the Laplacian doesn't connect to a known one - a loose part with
 * nothing trusted, a vertex on wire edges or only on degenerate faces - keeps its guess. False when cancelled. */
bool inpaint(const Mesh &m, const std::vector<uint8_t> &known, std::vector<std::vector<float>> &weights, bool normalize, const InpaintOptions &o,
             InpaintReport *report, std::string *error);

/* The pieces of Q, exposed for tests: the cotangent stiffness K = -L (CSR rows, symmetric, rows sum to 0) and the
 * Voronoi mass per vertex. */
void cotangent_laplacian(const Mesh &m, std::vector<std::vector<std::pair<uint32_t, double>>> &rows, std::vector<double> &mass);

/* The distance weights (Blender's envelope-like falloff): each bone by (nearest distance / distance)^4, the 4 largest
 * kept, summing to 1. `out` has one weight per bone. */
void envelope_weights(Vec3 p, const std::vector<BoneSegment> &bones, std::vector<float> &out);

}  // namespace skinweights

/* Weights from `source` (already in the target's space, with its vertex groups) onto `target` (paper, section 3):
 * close matches with agreeing normals copy the source's weights, the rest are inpainted. The source's groups
 * replace the target's groups of the same names; others (InpaintMask) stay. */
bool transfer_skin_weights(const Mesh &source, Mesh &target, const InpaintOptions &o, InpaintReport *report, std::string *error);

/* Automatic weights by inpainting: a vertex whose nearest bone is clearly the nearest and which that bone can see
 * (nothing of the mesh in between) takes it whole; the rest are inpainted. Groups named after the bones are made
 * or replaced. */
bool automatic_skin_weights(Mesh &m, const std::vector<BoneSegment> &bones, const InpaintOptions &o, InpaintReport *report, std::string *error);

}  // namespace bl
