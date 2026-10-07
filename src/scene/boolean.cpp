// SPDX-License-Identifier: GPL-2.0-or-later
// Mesh booleans with Manifold - the solver behind Blender's Boolean modifier
// "Manifold" mode (blender/source/blender/geometry/intern/
// mesh_boolean_manifold.cc). Faces are triangulated going in; triangles that
// still form one hole-free piece of an original face come back out as that
// n-gon (Blender rebuilds faces the same way, by face id).
#include "mesh.h"

#include <algorithm>
#include <map>
#include <unordered_map>

#ifdef BL_WITH_MANIFOLD
#  include <manifold/manifold.h>
#endif

namespace bl::meshops {

bool boolean_available() {
#ifdef BL_WITH_MANIFOLD
  return true;
#else
  return false;
#endif
}

#ifdef BL_WITH_MANIFOLD
namespace {

constexpr int kProps = 5;  // x y z u v

/* One input mesh as a MeshGL64: a property vertex per (mesh vertex, UV),
 * duplicates listed in the merge vectors so Manifold sees a closed solid. */
manifold::MeshGL64 to_meshgl(const Mesh &m, const Mat4 &xform, uint32_t face_id_base) {
  manifold::MeshGL64 g;
  g.numProp = kProps;
  const bool uv = m.has_uvs();
  std::vector<int64_t> first_prop(m.vert_count(), -1);
  std::map<std::tuple<uint32_t, float, float>, uint32_t> prop_of;
  auto prop_vertex = [&](uint32_t corner) -> uint32_t {
    uint32_t v = m.corner_verts[corner];
    Vec2 t = uv ? m.uvs[corner] : Vec2(0.0f, 0.0f);
    auto key = std::make_tuple(v, t.x, t.y);
    auto it = prop_of.find(key);
    if (it != prop_of.end()) return it->second;
    uint32_t id = (uint32_t)(g.vertProperties.size() / kProps);
    Vec3 p = xform.point(m.positions[v]);
    for (double x : {(double)p.x, (double)p.y, (double)p.z, (double)t.x, (double)t.y}) g.vertProperties.push_back(x);
    if (first_prop[v] < 0) first_prop[v] = id;
    else {
      g.mergeFromVert.push_back(id);
      g.mergeToVert.push_back((uint32_t)first_prop[v]);
    }
    prop_of.emplace(key, id);
    return id;
  };
  std::vector<uint32_t> local;
  for (size_t f = 0; f < m.face_count(); f++) {
    triangulate_face_local(m, f, local);
    const uint32_t base = m.face_offsets[f];
    for (size_t k = 0; k + 2 < local.size(); k += 3) {
      for (int j = 0; j < 3; j++) g.triVerts.push_back(prop_vertex(base + local[k + j]));
      g.faceID.push_back(face_id_base + (uint32_t)f);
    }
  }
  return g;
}

/* Output triangles back into a Mesh, rebuilding n-gons per source face. */
Mesh from_meshgl(const manifold::MeshGL64 &g, const std::vector<int> &face_material) {
  Mesh out;
  const size_t nprop = g.NumVert(), ntri = g.NumTri();
  /* Mesh vertex of each property vertex (merge duplicates). */
  std::vector<uint32_t> canon(nprop);
  for (size_t i = 0; i < nprop; i++) canon[i] = (uint32_t)i;
  for (size_t i = 0; i < g.mergeFromVert.size(); i++) canon[g.mergeFromVert[i]] = (uint32_t)g.mergeToVert[i];
  std::vector<uint32_t> vert_of(nprop, UINT32_MAX);
  for (size_t i = 0; i < nprop; i++) {
    uint32_t c = canon[i];
    while (canon[c] != c) c = canon[c];
    if (vert_of[c] == UINT32_MAX) {
      const double *p = &g.vertProperties[c * g.numProp];
      vert_of[c] = out.add_vert({(float)p[0], (float)p[1], (float)p[2]});
    }
    vert_of[i] = vert_of[c];
  }
  auto uv_of = [&](size_t prop) {
    const double *p = &g.vertProperties[prop * g.numProp];
    return Vec2((float)p[3], (float)p[4]);
  };
  /* Group triangles by source face. */
  std::unordered_map<uint64_t, std::vector<uint32_t>> groups;
  std::vector<uint64_t> order;
  for (size_t t = 0; t < ntri; t++) {
    uint64_t fid = t < g.faceID.size() ? g.faceID[t] : t;
    auto [it, inserted] = groups.try_emplace(fid);
    if (inserted) order.push_back(fid);
    it->second.push_back((uint32_t)t);
  }
  std::vector<uint32_t> fv;
  std::vector<Vec2> ft;
  for (uint64_t fid : order) {
    const auto &tris = groups[fid];
    const int mat = fid < face_material.size() ? face_material[fid] : 0;
    /* Boundary of the group: directed edges whose reverse isn't in it. */
    std::map<std::pair<uint32_t, uint32_t>, int> count;
    for (uint32_t t : tris)
      for (int j = 0; j < 3; j++) {
        uint32_t a = vert_of[g.triVerts[t * 3 + j]], b = vert_of[g.triVerts[t * 3 + (j + 1) % 3]];
        count[{a, b}]++;
      }
    bool simple = true;
    std::unordered_map<uint32_t, uint32_t> succ;
    std::unordered_map<uint32_t, uint32_t> prop_at;  // mesh vertex -> property vertex (for UVs)
    for (uint32_t t : tris)
      for (int j = 0; j < 3; j++) {
        uint32_t pa = (uint32_t)g.triVerts[t * 3 + j], pb = (uint32_t)g.triVerts[t * 3 + (j + 1) % 3];
        uint32_t a = vert_of[pa], b = vert_of[pb];
        prop_at[a] = pa;
        if (count.count({b, a})) continue;  // interior edge
        if (!succ.emplace(a, b).second) simple = false;  // vertex on the boundary twice
      }
    std::vector<uint32_t> loop;
    if (simple && !succ.empty()) {
      uint32_t start = succ.begin()->first, v = start;
      do {
        loop.push_back(v);
        auto it = succ.find(v);
        if (it == succ.end() || loop.size() > succ.size()) { simple = false; break; }
        v = it->second;
      } while (v != start);
      simple = simple && loop.size() == succ.size() && loop.size() >= 3;
    }
    if (simple) {
      fv.assign(loop.begin(), loop.end());
      ft.clear();
      for (uint32_t v : loop) ft.push_back(uv_of(prop_at[v]));
      out.add_face(fv.data(), fv.size(), ft.data(), mat);
    }
    else {
      for (uint32_t t : tris) {
        uint32_t tv[3];
        Vec2 tt[3];
        for (int j = 0; j < 3; j++) {
          tv[j] = vert_of[g.triVerts[t * 3 + j]];
          tt[j] = uv_of(g.triVerts[t * 3 + j]);
        }
        out.add_face(tv, 3, tt, mat);
      }
    }
  }
  return out;
}

}  // namespace
#endif

bool boolean_op(Mesh &a, const Mesh &b, const Mat4 &b_to_a, BooleanOp op, std::string *error) {
#ifdef BL_WITH_MANIFOLD
  try {
    /* Face ids: A's faces keep their index, B's follow after them, so each
     * output face knows its source and material (Blender: "Index Based"). */
    const uint32_t na = (uint32_t)a.face_count();
    std::vector<int> mats(na + b.face_count());
    for (uint32_t f = 0; f < na; f++) mats[f] = a.material_of(f);
    for (size_t f = 0; f < b.face_count(); f++) mats[na + f] = b.material_of(f);
    manifold::Manifold ma(to_meshgl(a, Mat4::identity(), 0)), mb(to_meshgl(b, b_to_a, na));
    if (ma.Status() != manifold::Manifold::Error::NoError || mb.Status() != manifold::Manifold::Error::NoError) {
      if (error) *error = "Boolean needs closed, manifold meshes (no holes or loose faces)";
      return false;
    }
    manifold::Manifold r = op == BooleanOp::Union ? ma + mb : op == BooleanOp::Intersect ? (ma ^ mb) : ma - mb;
    Mesh out = from_meshgl(r.GetMeshGL64(), mats);
    out.name = a.name;
    out.smooth = a.smooth;
    out.smooth_angle = a.smooth_angle;
    if (!a.has_uvs() && !b.has_uvs()) out.uvs.clear();
    a = std::move(out);
    a.touch();
    return true;
  }
  catch (const std::exception &e) {
    if (error) *error = e.what();
    return false;
  }
#else
  (void)a;
  (void)b;
  (void)b_to_a;
  (void)op;
  if (error) *error = "Boolean needs Blender's libraries (Manifold)";
  return false;
#endif
}

}  // namespace bl::meshops
