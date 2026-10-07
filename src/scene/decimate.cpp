// SPDX-License-Identifier: GPL-2.0-or-later
// Decimate (Blender: Decimate modifier, Collapse - blender/source/blender/
// modifiers/intern/MOD_decimate.cc) through meshoptimizer's quadric-error edge
// collapse. UVs are attributes the simplifier tries to preserve; each
// material is simplified separately with its border locked, so material
// regions keep their outlines.
#include "mesh.h"

#include "../deps/meshopt.h"

#include <algorithm>
#include <map>

namespace bl::meshops {

bool decimate_available() {
#ifdef BL_WITH_MESHOPT
  return true;
#else
  return false;
#endif
}

bool decimate(Mesh &m, float ratio) {
#ifdef BL_WITH_MESHOPT
  ratio = std::clamp(ratio, 0.0f, 1.0f);
  if (ratio >= 1.0f || m.face_count() == 0) return true;
  const bool has_uv = m.has_uvs();
  /* Vertex buffer: one entry per (mesh vertex, UV), as a GPU mesh would have. */
  std::map<std::tuple<uint32_t, float, float>, uint32_t> vid_of;
  std::vector<float> pos, uv;
  std::vector<uint32_t> orig_vert;
  auto vid = [&](uint32_t corner) {
    uint32_t v = m.corner_verts[corner];
    Vec2 t = has_uv ? m.uvs[corner] : Vec2(0.0f, 0.0f);
    auto [it, inserted] = vid_of.try_emplace(std::make_tuple(v, t.x, t.y), (uint32_t)orig_vert.size());
    if (inserted) {
      Vec3 p = m.positions[v];
      pos.insert(pos.end(), {p.x, p.y, p.z});
      uv.insert(uv.end(), {t.x, t.y});
      orig_vert.push_back(v);
    }
    return it->second;
  };
  /* Triangles grouped by material slot. */
  std::map<int, std::vector<uint32_t>> groups;
  std::vector<uint32_t> local;
  for (size_t f = 0; f < m.face_count(); f++) {
    triangulate_face_local(m, f, local);
    auto &idx = groups[m.material_of(f)];
    for (uint32_t c : local) idx.push_back(vid(m.face_offsets[f] + c));
  }
  const unsigned options = groups.size() > 1 ? meshopt_SimplifyLockBorder : 0;
  const float weights[2] = {0.5f, 0.5f};
  Mesh out;
  out.name = m.name;
  out.smooth = m.smooth;
  out.smooth_angle = m.smooth_angle;
  std::vector<uint32_t> new_vert(m.vert_count(), UINT32_MAX);
  for (auto &[mat, idx] : groups) {
    std::vector<uint32_t> dst(idx.size());
    size_t target = (size_t)(idx.size() / 3 * ratio) * 3;
    float err = 0;
    /* target_error 1 = the whole mesh extent: the ratio decides, like Blender's. */
    size_t n = has_uv ? meshopt_simplifyWithAttributes(dst.data(), idx.data(), idx.size(), pos.data(), orig_vert.size(), 12, uv.data(), 8,
                                                       weights, 2, nullptr, target, 1.0f, options, &err)
                      : meshopt_simplify(dst.data(), idx.data(), idx.size(), pos.data(), orig_vert.size(), 12, target, 1.0f, options, &err);
    for (size_t t = 0; t + 2 < n; t += 3) {
      uint32_t fv[3];
      Vec2 ft[3];
      for (int j = 0; j < 3; j++) {
        uint32_t v = orig_vert[dst[t + j]];
        if (new_vert[v] == UINT32_MAX) new_vert[v] = out.add_vert(m.positions[v]);
        fv[j] = new_vert[v];
        ft[j] = {uv[dst[t + j] * 2], uv[dst[t + j] * 2 + 1]};
      }
      out.add_face(fv, 3, has_uv ? ft : nullptr, mat);
    }
  }
  m = std::move(out);
  m.touch();
  return true;
#else
  (void)m;
  (void)ratio;
  return false;
#endif
}

}  // namespace bl::meshops
