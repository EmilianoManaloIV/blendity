// SPDX-License-Identifier: GPL-2.0-or-later
// Research feature registry.
//
// Workflow: drop a paper into research/papers/, ask Claude Code to implement a
// technique from it, and it gets added here as a Feature. Registered features
// automatically appear in:
//   * the Research window (with citation + status),
//   * Mesh > Research Features,
//   * the Inspector's MeshFilter tools.
// Track paper -> feature -> status in research/FEATURES.md.
#pragma once

#include "../scene/mesh.h"

#include <functional>
#include <string>
#include <vector>

namespace bl::research {

struct Feature {
  std::string id;           // stable identifier, e.g. "taubin_smoothing"
  std::string name;         // menu label
  std::string citation;     // paper reference
  std::string description;  // one or two sentences
  std::string status;       // "implemented", "experimental", ...
  std::string source_file;  // where the implementation lives
  std::function<bool(Mesh &)> apply;
};

const std::vector<Feature> &features();
void register_feature(Feature f);
/* Registers the built-in research features (called once at startup). */
void register_features();
bool apply(const std::string &id, Mesh &m);

/* ---- implementations (one function per paper technique) ---- */

/* Taubin 1995: alternating shrink (lambda > 0) / inflate (mu < -lambda) passes
 * form a low-pass filter that smooths without the shrinkage of plain
 * Laplacian smoothing. */
void taubin_smooth(Mesh &m, float lambda = 0.5f, float mu = -0.53f, int iterations = 10);

}  // namespace bl::research
