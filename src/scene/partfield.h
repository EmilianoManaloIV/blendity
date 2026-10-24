// SPDX-License-Identifier: GPL-2.0-or-later
// The optional learned part tree for Part Unwrap (task 0180, ADR 0057 amended): PartField's per-face features (NVIDIA,
// "PartField: Learning 3D Feature Fields for Part Segmentation and Beyond", 2025) in place of the geometric ones, as
// PartUV uses them. PartField's weights are NVIDIA's under a non-commercial licence: Blendity neither ships nor
// downloads them. The user converts the model they obtained themselves (tools/partfield/) into
// ~/.blendity/ai/partfield/partfield.onnx; it runs in the ONNX Runtime of the AI install (ADR 0046). Off by default.
//
// The model's contract (what tools/partfield/export_onnx.py writes):
//   input  "points"   float [1, N, 6]: each face's centroid and normal, the mesh scaled into the unit cube at the origin
//   output "features" float [1, N, D]: a feature per point (D is whatever the model gives: PartField's are hundreds wide)
#pragma once

#include "mesh.h"

#include <memory>
#include <string>
#include <vector>

namespace bl::partfield {

constexpr const char *kModelFile = "partfield.onnx";
constexpr size_t kMaxPoints = 200000;  // (more faces than this: the model isn't asked; the geometric tree is used)

/* ~/.blendity/ai/partfield (or BLENDITY_PARTFIELD_DIR). */
std::string install_dir();
/* "" when the model and ONNX Runtime are there, else what is missing (for the UI). */
std::string install_problem();

class Model {
 public:
  virtual ~Model() = default;
  /* `points` n x 6; `out` n x dims. */
  virtual bool run(const std::vector<float> &points, size_t n, int &dims, std::vector<float> &out, std::string &error) = 0;
};

/* The installed model in ONNX Runtime: null with `error`. */
std::unique_ptr<Model> load(std::string &error);

/* The model's features for every face of `m` (n_faces x dims, each row scaled to unit length): false with `error`
 * when the mesh is too big or the model fails. */
bool face_features(Model &model, const Mesh &m, int &dims, std::vector<float> &out, std::string &error);

/* The points the model is given for `m` (n_faces x 6): centroids and normals in the unit cube (the tests read it). */
std::vector<float> face_points(const Mesh &m);

}  // namespace bl::partfield
