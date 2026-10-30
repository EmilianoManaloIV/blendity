// SPDX-License-Identifier: GPL-2.0-or-later
// Mesh booleans with Manifold - the solver behind Blender's Boolean modifier
// "Manifold" mode (blender/source/blender/geometry/intern/
// mesh_boolean_manifold.cc). Faces are triangulated going in; triangles that
// still form one hole-free piece of an original face come back out as that
// n-gon (Blender rebuilds faces the same way, by face id).
#include "mesh.h"

#include <algorithm>

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

/* One input mesh as a MeshGL: a property vertex per (mesh vertex, UV),
 * duplicates listed in the merge vectors so Manifold sees a closed solid.
 * Task 0211: 32-bit, as Blender passes it (the positions are floats either
 * way, and Manifold computes in doubles from them), and each vertex's
 * property vertices found through a short chain instead of a map per corner. */
manifold::MeshGL to_meshgl(const Mesh &m, const Mat4 &xform, uint32_t face_id_base) {
  manifold::MeshGL g;
  g.numProp = kProps;
  const bool uv = m.has_uvs();
  std::vector<int64_t> first_prop(m.vert_count(), -1);
  std::vector<uint32_t> next_prop;  // property vertex -> another of its mesh vertex (UINT32_MAX: none)
  g.vertProperties.reserve(m.vert_count() * kProps);
  g.triVerts.reserve(m.corner_verts.size() * 3);
  auto prop_vertex = [&](uint32_t corner) -> uint32_t {
    const uint32_t v = m.corner_verts[corner];
    const Vec2 t = uv ? m.uvs[corner] : Vec2(0.0f, 0.0f);
    for (int64_t id = first_prop[v]; id >= 0; id = next_prop[id] == UINT32_MAX ? -1 : (int64_t)next_prop[id]) {
      const float *q = &g.vertProperties[(size_t)id * kProps];
      if (q[3] == t.x && q[4] == t.y) return (uint32_t)id;
    }
    const uint32_t id = (uint32_t)(g.vertProperties.size() / kProps);
    const Vec3 p = xform.point(m.positions[v]);
    for (float x : {p.x, p.y, p.z, t.x, t.y}) g.vertProperties.push_back(x);
    next_prop.push_back(UINT32_MAX);
    if (first_prop[v] < 0) {
      first_prop[v] = id;
      return id;
    }
    const uint32_t first = (uint32_t)first_prop[v];
    next_prop[id] = next_prop[first];
    next_prop[first] = id;
    g.mergeFromVert.push_back(id);
    g.mergeToVert.push_back(first);
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

/* One source face's triangles as one n-gon: its boundary (directed edges
 * whose reverse isn't in the group) walked from its smallest vertex. False
 * when the boundary isn't one simple loop. */
bool group_loop(const manifold::MeshGL &g, const std::vector<uint32_t> &vert_of, const uint32_t *tris, size_t count,
                std::vector<std::pair<uint32_t, uint32_t>> &edges, std::vector<std::pair<uint32_t, uint32_t>> &sorted,
                std::vector<std::pair<uint32_t, uint32_t>> &succ, std::vector<uint32_t> &loop) {
  edges.clear();
  succ.clear();
  loop.clear();
  for (size_t i = 0; i < count; i++)
    for (int j = 0; j < 3; j++) edges.push_back({vert_of[g.triVerts[tris[i] * 3 + j]], vert_of[g.triVerts[tris[i] * 3 + (j + 1) % 3]]});
  sorted.assign(edges.begin(), edges.end());
  std::sort(sorted.begin(), sorted.end());
  for (const auto &[a, b] : edges)
    if (!std::binary_search(sorted.begin(), sorted.end(), std::make_pair(b, a))) succ.push_back({a, b});
  std::sort(succ.begin(), succ.end());
  for (size_t i = 1; i < succ.size(); i++)
    if (succ[i].first == succ[i - 1].first) return false;  // a vertex on the boundary twice
  if (succ.size() < 3) return false;
  const uint32_t start = succ[0].first;
  uint32_t v = start;
  do {
    loop.push_back(v);
    auto it = std::lower_bound(succ.begin(), succ.end(), std::make_pair(v, 0u));
    if (it == succ.end() || it->first != v || loop.size() > succ.size()) return false;
    v = it->second;
  } while (v != start);
  return loop.size() == succ.size();
}

/* Output triangles back into a Mesh, rebuilding n-gons per source face. */
Mesh from_meshgl(const manifold::MeshGL &g, const std::vector<int> &face_material) {
  Mesh out;
  const size_t nprop = g.NumVert(), ntri = g.NumTri();
  /* Mesh vertex of each property vertex (merge duplicates). */
  std::vector<uint32_t> canon(nprop);
  for (size_t i = 0; i < nprop; i++) canon[i] = (uint32_t)i;
  for (size_t i = 0; i < g.mergeFromVert.size(); i++) canon[g.mergeFromVert[i]] = g.mergeToVert[i];
  std::vector<uint32_t> vert_of(nprop, UINT32_MAX);
  out.positions.reserve(nprop);
  for (size_t i = 0; i < nprop; i++) {
    uint32_t c = canon[i];
    while (canon[c] != c) c = canon[c];
    if (vert_of[c] == UINT32_MAX) {
      const float *p = &g.vertProperties[c * g.numProp];
      vert_of[c] = out.add_vert({p[0], p[1], p[2]});
    }
    vert_of[i] = vert_of[c];
  }
  auto uv_of = [&](size_t prop) {
    const float *p = &g.vertProperties[prop * g.numProp];
    return Vec2(p[3], p[4]);
  };
  /* Triangles grouped by source face, the groups in the order their first
   * triangle comes (ids are face indices: a table, not a hash map). */
  auto fid_of = [&](size_t t) -> size_t { return t < g.faceID.size() ? (size_t)g.faceID[t] : t; };
  size_t nfid = 0;
  for (size_t t = 0; t < ntri; t++) nfid = std::max(nfid, fid_of(t) + 1);
  std::vector<uint32_t> group_of(nfid, UINT32_MAX), group_fid, group_start;
  for (size_t t = 0; t < ntri; t++) {
    uint32_t &gi = group_of[fid_of(t)];
    if (gi == UINT32_MAX) {
      gi = (uint32_t)group_fid.size();
      group_fid.push_back((uint32_t)fid_of(t));
      group_start.push_back(0);
    }
    group_start[gi]++;
  }
  group_start.push_back(0);
  for (size_t i = 0, sum = 0; i < group_start.size(); i++) {
    const size_t n = group_start[i];
    group_start[i] = (uint32_t)sum;
    sum += n;
  }
  std::vector<uint32_t> tris(ntri), fill(group_start.begin(), group_start.end() - 1);
  for (size_t t = 0; t < ntri; t++) tris[fill[group_of[fid_of(t)]]++] = (uint32_t)t;
  out.corner_verts.reserve(ntri * 3);
  std::vector<uint32_t> fv;
  std::vector<Vec2> ft;
  std::vector<std::pair<uint32_t, uint32_t>> edges, sorted, succ, prop_at;
  for (size_t gi = 0; gi < group_fid.size(); gi++) {
    const uint32_t *gt = tris.data() + group_start[gi];
    const size_t count = group_start[gi + 1] - group_start[gi];
    const uint32_t fid = group_fid[gi];
    const int mat = fid < face_material.size() ? face_material[fid] : 0;
    if (group_loop(g, vert_of, gt, count, edges, sorted, succ, fv)) {
      /* Each loop vertex's UV from the last of its corners in the group. */
      prop_at.clear();
      for (size_t i = 0; i < count; i++)
        for (int j = 0; j < 3; j++) prop_at.push_back({vert_of[g.triVerts[gt[i] * 3 + j]], g.triVerts[gt[i] * 3 + j]});
      std::stable_sort(prop_at.begin(), prop_at.end(), [](const auto &x, const auto &y) { return x.first < y.first; });
      ft.clear();
      for (uint32_t v : fv) {
        auto it = std::upper_bound(prop_at.begin(), prop_at.end(), v, [](uint32_t x, const auto &e) { return x < e.first; });
        ft.push_back(uv_of(std::prev(it)->second));
      }
      out.add_face(fv.data(), fv.size(), ft.data(), mat);
      continue;
    }
    for (size_t i = 0; i < count; i++) {
      uint32_t tv[3];
      Vec2 tt[3];
      for (int j = 0; j < 3; j++) {
        tv[j] = vert_of[g.triVerts[gt[i] * 3 + j]];
        tt[j] = uv_of(g.triVerts[gt[i] * 3 + j]);
      }
      out.add_face(tv, 3, tt, mat);
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
    Mesh out = from_meshgl(r.GetMeshGL(), mats);
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
