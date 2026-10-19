// SPDX-License-Identifier: GPL-2.0-or-later
// Shape keys (task 0135): a mesh's named vertex offsets (Blender's shape keys, Unity's and FBX's blend shapes, glTF's
// morph targets, MMD's vertex morphs) and a renderer's weights for them, added to the basis before any modifier - so a
// face's expression is skinned with the head.
#include "scene.h"

#include <cmath>

namespace bl {

int Mesh::find_shape(const std::string &name) const {
  for (size_t i = 0; i < shape_keys.size(); i++)
    if (shape_keys[i].name == name) return (int)i;
  return -1;
}

float MeshRenderer::shape_weight(const std::string &name) const {
  for (const auto &w : shape_weights)
    if (w.first == name) return w.second;
  return 0.0f;
}

void MeshRenderer::set_shape_weight(const std::string &name, float w) {
  if (!std::isfinite(w)) return;
  for (auto &x : shape_weights)
    if (x.first == name) return void(x.second = w);
  shape_weights.push_back({name, w});
}

uint64_t shape_key_hash(const Mesh &m, const MeshRenderer *mr) {
  if (!mr || m.shape_keys.empty()) return 0;
  uint64_t h = 1469598103934665603ull;
  bool any = false;
  for (const ShapeKey &k : m.shape_keys) {
    const float w = mr->shape_weight(k.name);
    if (w == 0.0f || k.delta.size() != m.positions.size()) continue;
    uint32_t bits;
    std::memcpy(&bits, &w, 4);
    for (char c : k.name) h = (h ^ (uint8_t)c) * 1099511628211ull;
    h = (h ^ bits) * 1099511628211ull;
    any = true;
  }
  return any ? h : 0;
}

void apply_shape_keys(const Mesh &m, const MeshRenderer *mr, std::vector<Vec3> &positions) {
  if (!mr) return;
  for (const ShapeKey &k : m.shape_keys) {
    const float w = mr->shape_weight(k.name);
    if (w == 0.0f || k.delta.size() != positions.size()) continue;
    for (size_t i = 0; i < positions.size(); i++) positions[i] = positions[i] + k.delta[i] * w;
  }
}

namespace meshops {

bool shape_keys_fit(const Mesh &m) {
  for (const ShapeKey &k : m.shape_keys)
    if (k.delta.size() != m.vert_count()) return false;
  return true;
}

void transfer_shape_keys(const Mesh &from, Mesh &to) {
  if (from.shape_keys.empty() || !shape_keys_fit(from)) {
    for (ShapeKey &k : to.shape_keys) k.delta.resize(to.vert_count(), Vec3(0.0f));  // nothing to carry: fit the sizes
    return;
  }
  /* The groups' map (by index, else by position on from's surface), read as offsets instead of weights. */
  GroupTransferMap map;
  group_transfer_map(from, to, map);
  const size_t nv = to.vert_count();
  std::vector<ShapeKey> out(from.shape_keys.size());
  for (size_t k = 0; k < out.size(); k++) {
    const std::vector<Vec3> &d = from.shape_keys[k].delta;
    out[k].name = from.shape_keys[k].name;
    out[k].delta.assign(nv, Vec3(0.0f));
    for (size_t v = 0; v < nv && v * 3 + 2 < map.src.size(); v++) {
      const uint32_t *s = &map.src[v * 3];
      if (s[0] >= d.size() || s[1] >= d.size() || s[2] >= d.size()) continue;
      const Vec3 b = map.bary[v];
      out[k].delta[v] = d[s[0]] * b.x + d[s[1]] * b.y + d[s[2]] * b.z;
    }
  }
  to.shape_keys = std::move(out);
}

}  // namespace meshops

}  // namespace bl
