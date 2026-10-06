// SPDX-License-Identifier: GPL-2.0-or-later
#include "research.h"

namespace bl::research {

static std::vector<Feature> &registry() {
  static std::vector<Feature> r;
  return r;
}

const std::vector<Feature> &features() { return registry(); }

void register_feature(Feature f) {
  for (auto &e : registry())
    if (e.id == f.id) { e = std::move(f); return; }
  registry().push_back(std::move(f));
}

bool apply(const std::string &id, Mesh &m) {
  for (auto &f : registry())
    if (f.id == id && f.apply) return f.apply(m);
  return false;
}

void taubin_smooth(Mesh &m, float lambda, float mu, int iterations) {
  std::vector<uint32_t> off, nb;
  meshops::vertex_neighbors(m, off, nb);
  std::vector<Vec3> tmp(m.vert_count());
  auto pass = [&](float factor) {
    for (size_t v = 0; v < m.vert_count(); v++) {
      uint32_t b = off[v], e = off[v + 1];
      if (b == e) { tmp[v] = m.positions[v]; continue; }
      Vec3 avg(0.0f);
      for (uint32_t k = b; k < e; k++) avg += m.positions[nb[k]];
      avg = avg / (float)(e - b);
      /* Umbrella operator: delta = average of neighbours - p (Taubin eq. 3, uniform weights). */
      tmp[v] = m.positions[v] + (avg - m.positions[v]) * factor;
    }
    m.positions.swap(tmp);
  };
  for (int i = 0; i < iterations; i++) {
    pass(lambda);
    pass(mu);
  }
  m.touch();
}

void register_features() {
  static bool done = false;
  if (done) return;
  done = true;
  register_feature({"taubin_smoothing", "Taubin lambda|mu Smoothing",
                    "Taubin, G. (1995). A signal processing approach to fair surface design. SIGGRAPH '95, 351-358.",
                    "Removes noise without the volume loss of Laplacian smoothing by alternating a shrinking (lambda) "
                    "and an inflating (mu) pass. 10 iterations, lambda 0.5, mu -0.53.",
                    "implemented (example)", "src/research/research.cpp",
                    [](Mesh &m) {
                      taubin_smooth(m);
                      return true;
                    }});
  register_feature({"surface_noise", "Add Surface Noise (test input)",
                    "Utility - produces noisy input to compare smoothing methods.",
                    "Displaces every vertex along its normal by up to 3% of the mesh size. Use it before Taubin or "
                    "Laplacian smoothing to see the difference.",
                    "utility", "src/research/research.cpp",
                    [](Mesh &m) {
                      AABB b = m.bounds();
                      meshops::randomize(m, length(b.extent()) * 0.03f, (uint32_t)m.version * 2654435761u);
                      return true;
                    }});
}

}  // namespace bl::research
