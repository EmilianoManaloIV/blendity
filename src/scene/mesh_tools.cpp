// SPDX-License-Identifier: GPL-2.0-or-later
// Edge and face tools of Blender's Edit Mode that rebuild topology:
//   Bevel                 blender/source/blender/bmesh/tools/bmesh_bevel.cc
//   Bridge Edge Loops     blender/source/blender/bmesh/operators/bmo_bridge.cc
//   Subdivide / Dissolve  bmo_subdivide.cc, bmo_dissolve.cc
//   Connect (J)           bmo_connect.cc
// plus two of Blendity's own: Push Through (a hole along the face normal,
// imprinted on whatever face it exits through, at any angle) and contact
// fusing (an extruded face that lands on another face merges into it).
#include "mesh.h"
#include "mesh_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace bl::meshops {

/* The edge selection the editor set for this thread (nullptr: derive edges
 * from the selected vertices). */
static thread_local const std::unordered_set<uint64_t> *t_edge_selection = nullptr;

EdgeSelectionScope::EdgeSelectionScope(const std::unordered_set<uint64_t> *edges) : previous(t_edge_selection) {
  t_edge_selection = edges;
}
EdgeSelectionScope::~EdgeSelectionScope() { t_edge_selection = previous; }

bool edge_selected(uint32_t a, uint32_t b, const std::vector<uint8_t> &vert_sel) {
  if (t_edge_selection) return t_edge_selection->count(Mesh::edge_key(a, b)) > 0;
  return a < vert_sel.size() && b < vert_sel.size() && vert_sel[a] && vert_sel[b];
}

namespace {

/* Unnormalised Newell normal of a vertex loop (length = 2 x area). */
Vec3 loop_newell(const Mesh &m, const std::vector<uint32_t> &loop) {
  Vec3 n(0.0f);
  for (size_t i = 0; i < loop.size(); i++) {
    const Vec3 &a = m.positions[loop[i]], &b = m.positions[loop[(i + 1) % loop.size()]];
    n.x += (a.y - b.y) * (a.z + b.z);
    n.y += (a.z - b.z) * (a.x + b.x);
    n.z += (a.x - b.x) * (a.y + b.y);
  }
  return n;
}

Vec3 loop_center(const Mesh &m, const std::vector<uint32_t> &loop) {
  Vec3 c(0.0f);
  for (uint32_t v : loop) c += m.positions[v];
  return loop.empty() ? c : c / (float)loop.size();
}

std::vector<uint32_t> face_loop(const Mesh &m, size_t f) { return std::vector<uint32_t>(m.face_verts(f), m.face_verts(f) + m.face_size(f)); }

/* A connected set of selected faces and its boundary loops, each running in
 * the faces' own winding (so its Newell normal is the region's normal). */
struct Region {
  std::vector<uint32_t> faces;
  std::vector<std::vector<uint32_t>> loops;
  Vec3 normal;  // unit, area weighted
  Vec3 center;
};

std::vector<Region> face_regions(const Mesh &m, const std::vector<uint8_t> &fsel) {
  const size_t nf = m.face_count();
  std::vector<uint32_t> parent(nf);
  for (size_t i = 0; i < nf; i++) parent[i] = (uint32_t)i;
  std::function<uint32_t(uint32_t)> find = [&](uint32_t x) { return parent[x] == x ? x : parent[x] = find(parent[x]); };
  EdgeFaces ef(m);
  for (size_t f = 0; f < nf; f++) {
    if (f >= fsel.size() || !fsel[f]) continue;
    const uint32_t *v = m.face_verts(f);
    uint32_t n = m.face_size(f);
    for (uint32_t i = 0; i < n; i++)
      for (uint32_t g : ef.at(v[i], v[(i + 1) % n]))
        if (g != f && g < fsel.size() && fsel[g]) parent[find((uint32_t)f)] = find(g);
  }
  std::map<uint32_t, Region> by_root;
  for (size_t f = 0; f < nf; f++)
    if (f < fsel.size() && fsel[f]) by_root[find((uint32_t)f)].faces.push_back((uint32_t)f);
  std::vector<Region> out;
  for (auto &[root, r] : by_root) {
    /* Directed boundary edges: edges whose reverse isn't in the region. */
    std::unordered_set<uint64_t> directed;
    auto dkey = [](uint32_t a, uint32_t b) { return ((uint64_t)a << 32) | b; };
    for (uint32_t f : r.faces) {
      const uint32_t *v = m.face_verts(f);
      uint32_t n = m.face_size(f);
      for (uint32_t i = 0; i < n; i++) directed.insert(dkey(v[i], v[(i + 1) % n]));
    }
    std::unordered_map<uint32_t, uint32_t> next;
    bool pinched = false;
    Vec3 nsum(0.0f), csum(0.0f);
    float asum = 0;
    for (uint32_t f : r.faces) {
      const uint32_t *v = m.face_verts(f);
      uint32_t n = m.face_size(f);
      Vec3 fn = loop_newell(m, face_loop(m, f));
      nsum += fn;
      float a = length(fn);
      csum += m.face_center(f) * a;
      asum += a;
      for (uint32_t i = 0; i < n; i++) {
        uint32_t a0 = v[i], b0 = v[(i + 1) % n];
        if (directed.count(dkey(b0, a0))) continue;
        if (next.count(a0)) pinched = true;
        next[a0] = b0;
      }
    }
    r.normal = length_sq(nsum) > 1e-20f ? normalize(nsum) : Vec3(0, 1, 0);
    r.center = asum > 0 ? csum / asum : m.face_center(r.faces[0]);
    if (!pinched) {
      std::unordered_set<uint32_t> used;
      for (auto &[start, _] : next) {
        if (used.count(start)) continue;
        std::vector<uint32_t> loop;
        uint32_t v = start;
        while (!used.count(v) && next.count(v)) {
          used.insert(v);
          loop.push_back(v);
          v = next[v];
        }
        if (v == start && loop.size() >= 3) r.loops.push_back(std::move(loop));
      }
    }
    out.push_back(std::move(r));
  }
  return out;
}

/* Inserts vertices on edges, in every face that uses the edge (so neighbours
 * stay watertight). splits[edge_key] = (t from the lower vertex index, vertex). */
using EdgeSplits = std::unordered_map<uint64_t, std::vector<std::pair<float, uint32_t>>>;

void apply_edge_splits(Mesh &m, EdgeSplits &splits) {
  if (splits.empty()) return;
  for (auto &[k, list] : splits) std::sort(list.begin(), list.end());
  FaceBuilder fb(m);
  std::vector<uint32_t> v;
  std::vector<Vec2> t;
  for (size_t f = 0; f < m.face_count(); f++) {
    v.clear();
    t.clear();
    const uint32_t b = m.face_offsets[f], n = m.face_size(f);
    for (uint32_t i = 0; i < n; i++) {
      uint32_t a = m.corner_verts[b + i], c = m.corner_verts[b + (i + 1) % n];
      v.push_back(a);
      if (fb.has_uv) t.push_back(m.uvs[b + i]);
      auto it = splits.find(Mesh::edge_key(a, c));
      if (it == splits.end()) continue;
      const auto &list = it->second;
      const bool forward = a < c;
      for (size_t k = 0; k < list.size(); k++) {
        const auto &s = forward ? list[k] : list[list.size() - 1 - k];
        v.push_back(s.second);
        if (fb.has_uv) {
          float lt = forward ? s.first : 1.0f - s.first;
          t.push_back(m.uvs[b + i] + (m.uvs[b + (i + 1) % n] - m.uvs[b + i]) * lt);
        }
      }
    }
    fb.add(v.data(), v.size(), fb.has_uv ? t.data() : nullptr, m.material_of(f), m.smooth_of(f));
  }
  fb.commit(m);
}

/* Adds `extra` vertices to a closed loop by splitting its longest edges, and
 * returns the loop with them in order. */
std::vector<uint32_t> densify_loop(Mesh &m, const std::vector<uint32_t> &loop, size_t extra) {
  const size_t n = loop.size();
  std::vector<float> len(n);
  for (size_t i = 0; i < n; i++) len[i] = length(m.positions[loop[(i + 1) % n]] - m.positions[loop[i]]);
  std::vector<int> cuts(n, 0);
  for (size_t e = 0; e < extra; e++) {
    size_t best = 0;
    for (size_t i = 1; i < n; i++)
      if (len[i] / (cuts[i] + 1) > len[best] / (cuts[best] + 1)) best = i;
    cuts[best]++;
  }
  EdgeSplits splits;
  std::vector<uint32_t> out;
  for (size_t i = 0; i < n; i++) {
    uint32_t a = loop[i], c = loop[(i + 1) % n];
    out.push_back(a);
    for (int k = 1; k <= cuts[i]; k++) {
      float t = k / (float)(cuts[i] + 1);
      uint32_t nv = m.add_vert(lerp(m.positions[a], m.positions[c], t));
      out.push_back(nv);
      splits[Mesh::edge_key(a, c)].push_back({a < c ? t : 1.0f - t, nv});
    }
  }
  apply_edge_splits(m, splits);
  return out;
}

/* Rotation taking unit vector a onto unit vector b. */
Mat4 rotation_between(Vec3 a, Vec3 b) {
  float c = clampf(dot(a, b), -1.0f, 1.0f);
  Vec3 axis = cross(a, b);
  if (length_sq(axis) < 1e-12f) {
    if (c > 0) return Mat4::identity();
    axis = std::fabs(a.x) < 0.9f ? cross(a, Vec3(1, 0, 0)) : cross(a, Vec3(0, 1, 0));
  }
  return Mat4::rotate(Quat::axis_angle(normalize(axis), std::acos(c)));
}

/* Faces between an outer loop and an inner loop lying inside it (a polygon
 * with a hole), on a surface with normal n. Walks both loops by angle around
 * the inner loop's centre and zips them together: quads where the two loops
 * advance in step, triangles where one has more vertices. */
void annulus(Mesh &m, std::vector<uint32_t> outer, std::vector<uint32_t> inner, Vec3 n, int mat, std::vector<uint32_t> &made) {
  if (dot(loop_newell(m, outer), n) < 0) std::reverse(outer.begin(), outer.end());
  if (dot(loop_newell(m, inner), n) < 0) std::reverse(inner.begin(), inner.end());
  const Vec3 c = loop_center(m, inner);
  Vec3 u = normalize(cross(std::fabs(n.y) < 0.9f ? Vec3(0, 1, 0) : Vec3(1, 0, 0), n)), w = cross(n, u);
  auto ang = [&](uint32_t v) {
    Vec3 d = m.positions[v] - c;
    return std::atan2(dot(d, w), dot(d, u));
  };
  const size_t no = outer.size(), ni = inner.size();
  const float a0 = ang(outer[0]);
  size_t j0 = 0;
  float bestd = 1e9f;
  for (size_t j = 0; j < ni; j++) {
    float d = std::fabs(std::remainder(ang(inner[j]) - a0, 2.0f * kPi));
    if (d < bestd) { bestd = d; j0 = j; }
  }
  std::rotate(inner.begin(), inner.begin() + j0, inner.end());
  auto unwrap = [&](const std::vector<uint32_t> &loop, float start) {
    std::vector<float> a(loop.size() + 1);
    float prev = start + std::remainder(ang(loop[0]) - start, 2.0f * kPi);
    a[0] = prev;
    for (size_t k = 1; k < loop.size(); k++) {
      float x = ang(loop[k]);
      while (x <= prev) x += 2.0f * kPi;
      while (x > prev + 2.0f * kPi) x -= 2.0f * kPi;
      a[k] = prev = x;
    }
    a[loop.size()] = a[0] + 2.0f * kPi;
    return a;
  };
  std::vector<float> ao = unwrap(outer, a0), ai = unwrap(inner, a0);
  size_t i = 0, j = 0;
  bool pending = false;  // an outer-step triangle waiting to become a quad
  uint32_t pend[3] = {};
  auto emit = [&](std::initializer_list<uint32_t> f) {
    m.add_face(f.begin(), f.size(), nullptr, mat);
    made.push_back((uint32_t)m.face_count() - 1);
  };
  auto flush = [&] {
    if (pending) emit({pend[0], pend[1], pend[2]});
    pending = false;
  };
  while (i < no || j < ni) {
    bool step_outer = j >= ni || (i < no && ao[i + 1] <= ai[j + 1]);
    uint32_t oi = outer[i % no], oi1 = outer[(i + 1) % no], ij = inner[j % ni], ij1 = inner[(j + 1) % ni];
    if (step_outer) {
      flush();
      pend[0] = oi; pend[1] = oi1; pend[2] = ij;
      pending = true;
      i++;
    }
    else {
      if (pending && pend[1] == oi && pend[2] == ij) {
        emit({pend[0], pend[1], ij1, ij});  // outer step + inner step = quad
        pending = false;
      }
      else {
        flush();
        emit({oi, ij1, ij});
      }
      j++;
    }
  }
  flush();
}

/* Even point-in-polygon test in the plane with normal n. */
bool inside_loop(const Mesh &m, const std::vector<uint32_t> &loop, Vec3 p, Vec3 n, float eps) {
  Vec3 u = normalize(cross(std::fabs(n.y) < 0.9f ? Vec3(0, 1, 0) : Vec3(1, 0, 0), n)), w = cross(n, u);
  auto to2 = [&](Vec3 q) { return Vec2(dot(q, u), dot(q, w)); };
  Vec2 pp = to2(p);
  bool in = false;
  for (size_t i = 0, k = loop.size() - 1; i < loop.size(); k = i++) {
    Vec2 a = to2(m.positions[loop[i]]), b = to2(m.positions[loop[k]]);
    /* On an edge counts as inside. */
    Vec2 e = b - a;
    float l2 = dot(e, e);
    float t = l2 > 0 ? clampf(dot(pp - a, e) / l2, 0.0f, 1.0f) : 0.0f;
    if (length(pp - (a + e * t)) <= eps) return true;
    if ((a.y > pp.y) != (b.y > pp.y) && pp.x < (b.x - a.x) * (pp.y - a.y) / (b.y - a.y) + a.x) in = !in;
  }
  return in;
}

void drop_faces(Mesh &m, const std::vector<uint8_t> &drop) {
  FaceBuilder fb(m);
  for (size_t f = 0; f < m.face_count(); f++)
    if (f >= drop.size() || !drop[f]) fb.add(m.face_verts(f), m.face_size(f), fb.has_uv ? &m.uvs[m.face_offsets[f]] : nullptr, m.material_of(f), m.smooth_of(f));
  fb.commit(m);
}

/* Drops faces, then marks the given faces (indices before dropping) in the result. */
std::vector<uint8_t> drop_and_select(Mesh &m, const std::vector<uint8_t> &drop, const std::vector<uint32_t> &select) {
  std::vector<uint32_t> remap(m.face_count(), UINT32_MAX);
  uint32_t k = 0;
  for (size_t f = 0; f < m.face_count(); f++)
    if (f >= drop.size() || !drop[f]) remap[f] = k++;
  drop_faces(m, drop);
  std::vector<uint8_t> sel(m.face_count(), 0);
  for (uint32_t f : select)
    if (f < remap.size() && remap[f] != UINT32_MAX) sel[remap[f]] = 1;
  return sel;
}

float mesh_scale(const Mesh &m) { return std::max(1e-3f, length(m.bounds().extent())); }

std::vector<std::pair<uint32_t, uint32_t>> selected_edges(const Mesh &m, const std::vector<uint8_t> &vsel) {
  std::vector<std::pair<uint32_t, uint32_t>> out;
  for (auto &e : m.edge_cache())
    if (edge_selected(e.first, e.second, vsel)) out.push_back(e);
  return out;
}

}  // namespace

/* ===================================================================== */
/* Bevel                                                                  */
/* ===================================================================== */

bool bevel_edges(Mesh &m, std::vector<uint8_t> &vert_sel, std::vector<uint8_t> &face_sel, float width, int segments, std::string *err,
                 bool clamp_overlap) {
  segments = std::max(1, std::min(64, segments));
  EdgeFaces ef(m);
  std::unordered_set<uint64_t> bev;
  std::vector<uint8_t> at_vert(m.vert_count(), 0);
  for (auto &e : selected_edges(m, vert_sel)) {
    FaceList fs = ef.at(e.first, e.second);
    if (fs.size() != 2 || fs[0] == fs[1]) continue;  // only edges between two faces get a bevel face
    bev.insert(Mesh::edge_key(e.first, e.second));
    at_vert[e.first] = at_vert[e.second] = 1;
  }
  if (bev.empty()) {
    if (err) *err = "Select edges between two faces (edge mode, press 2)";
    return false;
  }
  width = std::max(width, 1e-6f);
  auto is_bev = [&](uint32_t a, uint32_t b) { return bev.count(Mesh::edge_key(a, b)) > 0; };
  /* Clamp Overlap (Blender's default): the whole bevel narrows evenly to the
   * widest it can be without two new vertices passing each other on an edge -
   * half an edge whose both ends slide, nearly all of one where only one does.
   * Off: the exact width, which can fold over on short edges. */
  if (clamp_overlap) {
    float limit = width;
    for (size_t f = 0; f < m.face_count(); f++) {
      const uint32_t n = m.face_size(f), *fv = m.face_verts(f);
      for (uint32_t i = 0; i < n; i++) {
        const uint32_t v = fv[i], o = fv[(i + 1) % n];
        if (!at_vert[v] && !at_vert[o]) continue;
        const float len = length(m.positions[o] - m.positions[v]);
        /* Along a beveled edge only corners where two bevels meet slide, from both ends. */
        limit = std::min(limit, len * (is_bev(v, o) || (at_vert[v] && at_vert[o]) ? 0.49f : 0.98f));
      }
    }
    width = std::max(limit, 1e-6f);
  }
  /* A new vertex `s` along edge (v, o) from v. Every face that shares that
   * edge end asks for the same one, so the result stays connected. */
  std::unordered_map<uint64_t, uint32_t> slides;
  std::unordered_map<uint32_t, uint32_t> origin;  // new vertex -> beveled vertex it came from
  auto dist_along = [&](uint32_t v, uint32_t o) {
    const float len = length(m.positions[o] - m.positions[v]);
    return clamp_overlap ? width : std::min(width, 0.999f * len);  // clamped: already within every edge
  };
  auto slide = [&](uint32_t v, uint32_t o) {
    uint64_t k = ((uint64_t)v << 32) | o;
    auto it = slides.find(k);
    if (it != slides.end()) return it->second;
    Vec3 d = m.positions[o] - m.positions[v];
    float l = length(d);
    uint32_t nv = m.add_vert(m.positions[v] + (l > 0 ? d / l : Vec3(0.0f)) * dist_along(v, o));
    origin[nv] = v;
    return slides[k] = nv;
  };
  /* near[(face, v, other)] = the face's new vertex at v next to beveled edge (v, other). */
  std::map<std::tuple<uint32_t, uint32_t, uint32_t>, uint32_t> near;
  const size_t nf0 = m.face_count();
  std::vector<std::vector<uint32_t>> new_faces(nf0);
  for (size_t f = 0; f < nf0; f++) {
    std::vector<uint32_t> fv = face_loop(m, f);
    const size_t n = fv.size();
    auto &out = new_faces[f];
    for (size_t i = 0; i < n; i++) {
      uint32_t v = fv[i], P = fv[(i + n - 1) % n], N = fv[(i + 1) % n];
      if (!at_vert[v]) { out.push_back(v); continue; }
      bool bP = is_bev(P, v), bN = is_bev(v, N);
      if (!bP && !bN) {
        out.push_back(slide(v, P));
        out.push_back(slide(v, N));
      }
      else if (bN && !bP) {
        uint32_t s = slide(v, P);
        out.push_back(s);
        near[{(uint32_t)f, v, N}] = s;
      }
      else if (bP && !bN) {
        uint32_t s = slide(v, N);
        out.push_back(s);
        near[{(uint32_t)f, v, P}] = s;
      }
      else {
        /* Both edges beveled: a corner inside the face. */
        Vec3 p = m.positions[v], dp = m.positions[P] - p, dn = m.positions[N] - p;
        Vec3 q = p + normalize(dp) * dist_along(v, P) + normalize(dn) * dist_along(v, N);
        uint32_t s = m.add_vert(q);
        origin[s] = v;
        out.push_back(s);
        near[{(uint32_t)f, v, P}] = s;
        near[{(uint32_t)f, v, N}] = s;
      }
    }
  }
  /* Bevel strips: one per beveled edge, `segments` faces across, following a
   * quadratic arc through the original corner (Blender's profile = 0.5). */
  std::unordered_map<uint64_t, std::vector<uint32_t>> arcs;  // (f1 point, f2 point) -> arc
  auto arc = [&](uint32_t p1, uint32_t p2, uint32_t corner) {
    uint64_t k = ((uint64_t)p1 << 32) | p2;
    auto it = arcs.find(k);
    if (it != arcs.end()) return it->second;
    std::vector<uint32_t> pts{p1};
    Vec3 a = m.positions[p1], c = m.positions[corner], b = m.positions[p2];
    for (int s = 1; s < segments; s++) {
      float t = s / (float)segments;
      uint32_t nv = m.add_vert(a * ((1 - t) * (1 - t)) + c * (2 * t * (1 - t)) + b * (t * t));
      origin[nv] = corner;
      pts.push_back(nv);
    }
    pts.push_back(p2);
    return arcs[k] = pts;
  };
  struct Strip { std::vector<uint32_t> A, B; int mat; };
  std::vector<Strip> strips;
  for (uint64_t k : bev) {
    uint32_t a = (uint32_t)(k >> 32), b = (uint32_t)(k & 0xFFFFFFFF);
    FaceList fs = ef.at(a, b);
    uint32_t f1 = fs[0], f2 = fs[1];
    /* f1 must run a -> b. */
    int ca = -1;
    for (uint32_t i = 0; i < m.face_size(f1); i++)
      if (m.face_verts(f1)[i] == a) ca = (int)i;
    if (ca < 0 || m.face_verts(f1)[(ca + 1) % m.face_size(f1)] != b) std::swap(f1, f2);
    auto get = [&](uint32_t f, uint32_t v, uint32_t o) {
      auto it = near.find({f, v, o});
      return it == near.end() ? v : it->second;
    };
    strips.push_back({arc(get(f1, a, b), get(f2, a, b), a), arc(get(f1, b, a), get(f2, b, a), b), m.material_of(f1)});
  }
  /* A face that ends at a beveled corner (its two slide points are an arc's
   * ends) takes the arc's vertices too, as Blender does, so it stays one face. */
  auto expand = [&](const std::vector<uint32_t> &in, std::vector<std::pair<uint32_t, float>> &out) {
    out.clear();
    for (size_t i = 0; i < in.size(); i++) {
      uint32_t x = in[i], y = in[(i + 1) % in.size()];
      out.push_back({x, -1.0f});
      for (bool rev : {false, true}) {
        auto it = arcs.find(rev ? (((uint64_t)y << 32) | x) : (((uint64_t)x << 32) | y));
        if (it == arcs.end() || it->second.size() <= 2) continue;
        const auto &pts = it->second;
        for (size_t k = 1; k + 1 < pts.size(); k++)
          out.push_back({rev ? pts[pts.size() - 1 - k] : pts[k], k / (float)(pts.size() - 1)});
        break;
      }
    }
  };
  /* Rebuild the original faces with their new corners (UVs re-derived by
   * position inside the old face: the corners only slide along its edges). */
  FaceBuilder fb(m);
  std::vector<std::pair<uint32_t, float>> ex;
  for (size_t f = 0; f < nf0; f++) {
    expand(new_faces[f], ex);
    std::vector<uint32_t> verts;
    for (auto &e : ex) verts.push_back(e.first);
    std::vector<Vec2> uv;
    if (fb.has_uv) {
      const uint32_t b = m.face_offsets[f], n = m.face_size(f);
      const uint32_t *fv = m.face_verts(f);
      for (auto &e : ex) {
        uint32_t nvx = e.first;
        if (e.second >= 0) { uv.push_back(Vec2(0, 0)); continue; }  // filled in below by interpolation
        auto o = origin.find(nvx);
        uint32_t src = o == origin.end() ? nvx : o->second;
        uint32_t ci = 0;
        while (ci < n && fv[ci] != src) ci++;
        Vec2 base = ci < n ? m.uvs[b + ci] : Vec2(0, 0);
        if (o != origin.end() && ci < n) {
          /* Offset along the two face edges at this corner, in UV space. */
          uint32_t P = fv[(ci + n - 1) % n], N = fv[(ci + 1) % n];
          Vec3 d = m.positions[nvx] - m.positions[src];
          Vec3 eP = m.positions[P] - m.positions[src], eN = m.positions[N] - m.positions[src];
          /* Solve d = a eP + b eN in the face plane (least squares 2x2). */
          float pp = dot(eP, eP), pn = dot(eP, eN), nn = dot(eN, eN), dpv = dot(d, eP), dnv = dot(d, eN);
          float det = pp * nn - pn * pn;
          float a = det != 0 ? (dpv * nn - dnv * pn) / det : 0, bb = det != 0 ? (dnv * pp - dpv * pn) / det : 0;
          base = base + (m.uvs[b + (ci + n - 1) % n] - base) * a + (m.uvs[b + (ci + 1) % n] - base) * bb;
        }
        uv.push_back(base);
      }
      /* Arc vertices: along the straight line between their two ends. */
      for (size_t i = 0; i < ex.size(); i++) {
        if (ex[i].second < 0) continue;
        size_t s = i, e = i;
        while (ex[s].second >= 0) s = (s + ex.size() - 1) % ex.size();
        while (ex[e].second >= 0) e = (e + 1) % ex.size();
        uv[i] = uv[s] + (uv[e] - uv[s]) * ex[i].second;
      }
    }
    fb.add(verts.data(), verts.size(), fb.has_uv ? uv.data() : nullptr, m.material_of(f), m.smooth_of(f));
  }
  std::vector<uint32_t> bevel_faces;
  for (const Strip &st : strips)
    for (int s = 0; s < segments; s++) {
      uint32_t q[4] = {st.B[s], st.A[s], st.A[s + 1], st.B[s + 1]};
      fb.add(q, 4, nullptr, st.mat);
      bevel_faces.push_back((uint32_t)(fb.offs.size() - 2));
    }
  fb.commit(m);
  /* Corner patches where beveled edges meet: the holes left between new
   * vertices of one original vertex, found as open boundaries and filled. */
  {
    std::unordered_set<uint64_t> directed;
    auto dkey = [](uint32_t x, uint32_t y) { return ((uint64_t)x << 32) | y; };
    for (size_t f = 0; f < m.face_count(); f++) {
      const uint32_t *v = m.face_verts(f);
      uint32_t n = m.face_size(f);
      for (uint32_t i = 0; i < n; i++) directed.insert(dkey(v[i], v[(i + 1) % n]));
    }
    std::unordered_map<uint32_t, uint32_t> next;
    for (uint64_t d : directed) {
      uint32_t x = (uint32_t)(d >> 32), y = (uint32_t)(d & 0xFFFFFFFF);
      if (directed.count(dkey(y, x))) continue;
      auto ox = origin.find(x), oy = origin.find(y);
      if (ox == origin.end() || oy == origin.end() || ox->second != oy->second) continue;
      next[y] = x;  // the patch runs opposite to the open edge
    }
    std::unordered_set<uint32_t> used;
    for (auto &[start, _] : next) {
      if (used.count(start)) continue;
      std::vector<uint32_t> loop;
      uint32_t v = start;
      while (!used.count(v) && next.count(v)) {
        used.insert(v);
        loop.push_back(v);
        v = next[v];
      }
      if (v == start && loop.size() >= 3) {
        m.add_face(loop.data(), loop.size(), nullptr, 0);
        bevel_faces.push_back((uint32_t)m.face_count() - 1);
      }
    }
  }
  remove_loose_verts(m);
  face_sel.assign(m.face_count(), 0);
  for (uint32_t f : bevel_faces) face_sel[f] = 1;
  vert_sel.assign(m.vert_count(), 0);
  for (size_t f = 0; f < m.face_count(); f++)
    if (face_sel[f])
      for (uint32_t i = 0; i < m.face_size(f); i++) vert_sel[m.face_verts(f)[i]] = 1;
  m.touch();
  return true;
}

/* ===================================================================== */
/* Bridge, contact fusing and push-through                               */
/* ===================================================================== */

namespace {

/* Builds the tube between loops A and B, both given in "region winding" (the
 * direction of the faces they bound or replace). Handles different vertex
 * counts (the shorter loop gets vertices on its longest edges), any relative
 * orientation (B is rotated onto A's plane before matching vertices), and
 * curved paths through `segments` rings on a Hermite curve. */
void build_tube(Mesh &m, std::vector<uint32_t> A, std::vector<uint32_t> B, int segments, int twist, float smooth, int path, int mat,
                std::vector<uint32_t> &made) {
  if (A.size() < B.size()) A = densify_loop(m, A, B.size() - A.size());
  else if (B.size() < A.size()) B = densify_loop(m, B, A.size() - B.size());
  const size_t n = A.size();
  const Vec3 nA = normalize(loop_newell(m, A)), nB = normalize(loop_newell(m, B));
  const Vec3 cA = loop_center(m, A), cB = loop_center(m, B);
  std::reverse(B.begin(), B.end());
  /* Match vertices: rotate B's (reversed) plane onto A's, scale both to unit
   * size, then pick the cyclic offset with the smallest squared distance. */
  Mat4 R = rotation_between(-nB, nA);
  auto rms = [&](const std::vector<uint32_t> &L, Vec3 c) {
    float s = 0;
    for (uint32_t v : L) s += length_sq(m.positions[v] - c);
    return std::sqrt(std::max(s / L.size(), 1e-20f));
  };
  float sA = rms(A, cA), sB = rms(B, cB);
  size_t best = 0;
  float bestc = 1e30f;
  for (size_t k = 0; k < n; k++) {
    float cost = 0;
    for (size_t i = 0; i < n; i++)
      cost += length_sq((m.positions[A[i]] - cA) / sA - R.dir(m.positions[B[(i + k) % n]] - cB) / sB);
    if (cost < bestc) { bestc = cost; best = k; }
  }
  best = (size_t)(((int64_t)best + twist) % (int64_t)n + n) % n;
  std::rotate(B.begin(), B.begin() + best, B.end());
  /* Rings. Inside (a tunnel): leave A against its normal and come out of B
   * along its normal. Outside (a handle): leave A along its normal and come
   * back into B against its normal. Auto takes the tunnel only for roughly
   * opposite faces with each behind the other (front and back of a box). */
  segments = std::max(1, segments);
  Vec3 d = cB - cA;
  float k = smooth;  // tangent length per unit of each vertex pair's distance
  bool inside = path == 2 || (path == 0 && dot(nA, nB) < -0.5f && dot(d, nA) < 0 && dot(d, nB) > 0);
  Vec3 t0 = inside ? -nA : nA, t1 = inside ? nB : -nB;
  /* The more the path has to turn away from the straight line, the longer
   * the tangents, so the curve clears the surface it leaves (a 90 degree
   * handle needs about 3x the distance; a straight tunnel 1x). */
  if (length_sq(d) > 1e-12f) k *= 1.0f + 1.2f * (1.0f - dot(t0, normalize(d)));
  std::vector<std::vector<uint32_t>> rings{A};
  for (int s = 1; s < segments; s++) {
    float t = s / (float)segments, t2 = t * t, t3 = t2 * t;
    float h00 = 2 * t3 - 3 * t2 + 1, h10 = t3 - 2 * t2 + t, h01 = -2 * t3 + 3 * t2, h11 = t3 - t2;
    std::vector<uint32_t> ring(n);
    for (size_t i = 0; i < n; i++) {
      float L = length(m.positions[B[i]] - m.positions[A[i]]) * k;  // wider arcs on the outside of the bend
      ring[i] = m.add_vert(m.positions[A[i]] * h00 + t0 * (L * h10) + m.positions[B[i]] * h01 + t1 * (L * h11));
    }
    rings.push_back(ring);
  }
  rings.push_back(B);
  for (int s = 0; s < segments; s++)
    for (size_t i = 0; i < n; i++) {
      uint32_t q[4] = {rings[s][i], rings[s][(i + 1) % n], rings[s + 1][(i + 1) % n], rings[s + 1][i]};
      m.add_face(q, 4, nullptr, mat);
      made.push_back((uint32_t)m.face_count() - 1);
    }
}

/* If region R (one boundary loop) lies on another face T of the mesh, facing
 * it, merge them: the prism the extrusion made becomes part of the solid. */
bool try_fuse(Mesh &m, const std::vector<uint32_t> &region_faces, const std::vector<uint32_t> &loop, Vec3 nR,
              const std::vector<uint8_t> &excluded, std::vector<uint8_t> &drop, std::vector<uint32_t> &made) {
  const float eps = 1e-4f * mesh_scale(m);
  for (size_t t = 0; t < m.face_count(); t++) {
    if (excluded[t] || drop[t]) continue;
    Vec3 nT = m.face_normal(t);
    if (dot(nT, nR) > -0.98f) continue;
    Vec3 pT = m.positions[m.face_verts(t)[0]];
    bool coplanar = true;
    for (uint32_t v : loop) coplanar = coplanar && std::fabs(dot(m.positions[v] - pT, nT)) <= eps * 10;
    if (!coplanar) continue;
    std::vector<uint32_t> tl = face_loop(m, t);
    bool l_in_t = true, t_in_l = true;
    for (uint32_t v : loop) l_in_t = l_in_t && inside_loop(m, tl, m.positions[v], nT, eps * 10);
    for (uint32_t v : tl) t_in_l = t_in_l && inside_loop(m, loop, m.positions[v], nT, eps * 10);
    if (!l_in_t && !t_in_l) continue;
    for (uint32_t f : region_faces) drop[f] = 1;
    drop[t] = 1;
    if (l_in_t && t_in_l) {
      /* Same outline: weld the loop onto the face's vertices. */
      std::vector<uint32_t> target(m.vert_count());
      for (size_t i = 0; i < target.size(); i++) target[i] = (uint32_t)i;
      for (uint32_t v : loop) {
        uint32_t bestv = v;
        float bd = 1e30f;
        for (uint32_t w : tl) {
          float d = length_sq(m.positions[w] - m.positions[v]);
          if (d < bd) { bd = d; bestv = w; }
        }
        target[v] = bestv;
      }
      for (uint32_t &v : m.corner_verts) v = target[v];
    }
    else if (l_in_t) annulus(m, tl, loop, nT, m.material_of(t), made);   // a hole in T where the prism lands
    else annulus(m, loop, tl, nR, m.material_of(region_faces[0]), made);  // the cap shrinks to a ring around T
    return true;
  }
  return false;
}

}  // namespace

bool fuse_contacts(Mesh &m, std::vector<uint8_t> &face_sel) {
  face_sel.resize(m.face_count(), 0);
  std::vector<Region> regions = face_regions(m, face_sel);
  std::vector<uint8_t> drop(m.face_count(), 0);
  std::vector<uint32_t> made;
  bool any = false;
  for (const Region &r : regions)
    if (r.loops.size() == 1) any |= try_fuse(m, r.faces, r.loops[0], r.normal, face_sel, drop, made);
  if (!any) return false;
  face_sel = drop_and_select(m, drop, {});
  cleanup_faces(m);
  remove_loose_verts(m);
  face_sel.assign(m.face_count(), 0);
  m.touch();
  return true;
}

bool bridge(Mesh &m, std::vector<uint8_t> &vert_sel, std::vector<uint8_t> &face_sel, int segments, int twist, float smooth, int path,
            std::string *err) {
  face_sel.resize(m.face_count(), 0);
  vert_sel.resize(m.vert_count(), 0);
  std::vector<uint8_t> drop(m.face_count(), 0);
  std::vector<std::vector<uint32_t>> loops;
  std::vector<Region> regions = face_regions(m, face_sel);
  int mat = 0;
  if (!regions.empty()) {
    if (regions.size() != 2 || regions[0].loops.size() != 1 || regions[1].loops.size() != 1) {
      if (err) *err = "Bridge needs two separate selected face regions, each with one outline";
      return false;
    }
    /* Two regions touching face to face: fuse instead of building a zero-length tube. */
    float gap = std::fabs(dot(regions[1].center - regions[0].center, regions[0].normal));
    if (gap < 1e-4f * mesh_scale(m) && dot(regions[0].normal, regions[1].normal) < -0.98f) {
      std::vector<uint8_t> sel0(m.face_count(), 0);
      for (uint32_t f : regions[0].faces) sel0[f] = 1;
      if (fuse_contacts(m, sel0)) {
        face_sel.assign(m.face_count(), 0);
        vert_sel.assign(m.vert_count(), 0);
        return true;
      }
    }
    for (const Region &r : regions) {
      loops.push_back(r.loops[0]);
      for (uint32_t f : r.faces) drop[f] = 1;
    }
    mat = m.material_of(regions[0].faces[0]);
  }
  else {
    /* Edge loops: open boundaries among the selected vertices. Each runs
     * opposite to the face it borders (the way a face filling it would). */
    EdgeFaces ef(m);
    std::unordered_map<uint32_t, uint32_t> next;
    for (size_t f = 0; f < m.face_count(); f++) {
      const uint32_t *v = m.face_verts(f);
      uint32_t n = m.face_size(f);
      for (uint32_t i = 0; i < n; i++) {
        uint32_t a = v[i], b = v[(i + 1) % n];
        if (edge_selected(a, b, vert_sel) && ef.at(a, b).size() == 1) next[b] = a;
      }
    }
    std::unordered_set<uint32_t> used;
    for (auto &[start, _] : next) {
      if (used.count(start)) continue;
      std::vector<uint32_t> loop;
      uint32_t v = start;
      while (!used.count(v) && next.count(v)) {
        used.insert(v);
        loop.push_back(v);
        v = next[v];
      }
      if (v == start && loop.size() >= 3) loops.push_back(loop);
    }
    if (loops.size() != 2) {
      if (err) *err = "Bridge needs two selected faces, or two open edge loops (holes)";
      return false;
    }
  }
  std::vector<uint32_t> made;
  build_tube(m, loops[0], loops[1], segments, twist, smooth, path, mat, made);
  drop.resize(m.face_count(), 0);
  face_sel = drop_and_select(m, drop, made);
  remove_loose_verts(m);
  vert_sel.assign(m.vert_count(), 0);
  for (size_t f = 0; f < m.face_count(); f++)
    if (face_sel[f])
      for (uint32_t i = 0; i < m.face_size(f); i++) vert_sel[m.face_verts(f)[i]] = 1;
  m.touch();
  return true;
}

bool push_through(Mesh &m, std::vector<uint8_t> &face_sel, int segments, std::string *err) {
  face_sel.resize(m.face_count(), 0);
  std::vector<Region> regions = face_regions(m, face_sel);
  if (regions.size() != 1 || regions[0].loops.size() != 1) {
    if (err) *err = "Push Through needs one selected face region (press 3, select faces)";
    return false;
  }
  const Region &r = regions[0];
  const std::vector<uint32_t> &L = r.loops[0];
  const Vec3 d = -r.normal;
  const float scale = mesh_scale(m);
  /* Triangles of every other face, for ray casts. */
  struct Tri { Vec3 a, b, c; uint32_t face; };
  std::vector<Tri> tris;
  std::vector<uint32_t> local;
  std::vector<uint8_t> in_region(m.face_count(), 0);
  for (uint32_t f : r.faces) in_region[f] = 1;
  for (size_t f = 0; f < m.face_count(); f++) {
    if (in_region[f]) continue;
    triangulate_face_local(m, f, local);
    const uint32_t *v = m.face_verts(f);
    for (size_t i = 0; i + 2 < local.size(); i += 3)
      tris.push_back({m.positions[v[local[i]]], m.positions[v[local[i + 1]]], m.positions[v[local[i + 2]]], (uint32_t)f});
  }
  auto cast = [&](Vec3 o, float &t_out, uint32_t &f_out) {
    Ray ray{o + d * (1e-4f * scale), d};
    t_out = 1e30f;
    f_out = UINT32_MAX;
    for (const Tri &t : tris) {
      float h = ray_triangle(ray, t.a, t.b, t.c);
      if (h <= 0) h = ray_triangle(ray, t.a, t.c, t.b);  // either winding
      if (h > 0 && h < t_out) { t_out = h; f_out = t.face; }
    }
    return f_out != UINT32_MAX;
  };
  std::vector<Vec3> hits(L.size());
  std::set<uint32_t> exit_faces;
  for (size_t i = 0; i < L.size(); i++) {
    float t;
    uint32_t f;
    if (!cast(m.positions[L[i]], t, f)) {
      if (err) *err = "Push Through: nothing behind the selection to come out of (is the mesh closed?)";
      return false;
    }
    if (dot(m.face_normal(f), d) <= 0) {
      if (err) *err = "Push Through: another surface is in the way before the far side";
      return false;
    }
    hits[i] = m.positions[L[i]] + d * (t + 1e-4f * scale);
    exit_faces.insert(f);
  }
  if (exit_faces.size() == 1) {
    /* One exit face, at any angle: project the outline onto it along the
     * normal, cut that shape out of it, and join both openings with a tube. */
    const uint32_t T = *exit_faces.begin();
    std::vector<uint32_t> L2(L.size());
    for (size_t i = 0; i < L.size(); i++) L2[i] = m.add_vert(hits[i]);
    std::vector<uint32_t> made;
    const int mat = m.material_of(r.faces[0]);
    segments = std::max(1, segments);
    std::vector<std::vector<uint32_t>> rings{L};
    for (int s = 1; s < segments; s++) {
      std::vector<uint32_t> ring(L.size());
      for (size_t i = 0; i < L.size(); i++) ring[i] = m.add_vert(lerp(m.positions[L[i]], hits[i], s / (float)segments));
      rings.push_back(ring);
    }
    rings.push_back(L2);
    const size_t n = L.size();
    for (int s = 0; s < segments; s++)
      for (size_t i = 0; i < n; i++) {
        uint32_t q[4] = {rings[s][i], rings[s][(i + 1) % n], rings[s + 1][(i + 1) % n], rings[s + 1][i]};
        m.add_face(q, 4, nullptr, mat);
        made.push_back((uint32_t)m.face_count() - 1);
      }
    annulus(m, face_loop(m, T), L2, m.face_normal(T), m.material_of(T), made);
    std::vector<uint8_t> drop(m.face_count(), 0);
    for (uint32_t f : r.faces) drop[f] = 1;
    drop[T] = 1;
    face_sel = drop_and_select(m, drop, made);
    remove_loose_verts(m);
    m.touch();
    return true;
  }
  /* The hole comes out through several faces: subtract a prism instead. */
  if (!boolean_available()) {
    if (err) *err = "Push Through: the hole exits through several faces; that needs the Boolean solver (Manifold, part of Blender's libraries)";
    return false;
  }
  Mesh prism;
  const float far_d = scale * 4.0f;
  for (uint32_t v : L) prism.add_vert(m.positions[v] - d * (1e-3f * scale));
  for (uint32_t v : L) prism.add_vert(m.positions[v] + d * far_d);
  const uint32_t n = (uint32_t)L.size();
  std::vector<uint32_t> cap(n), back(n);
  for (uint32_t i = 0; i < n; i++) { cap[i] = i; back[i] = 2 * n - 1 - i; }
  prism.add_face(cap.data(), n);
  prism.add_face(back.data(), n);
  for (uint32_t i = 0; i < n; i++) prism.add_face({(i + 1) % n, i, n + i, n + (i + 1) % n});
  std::string berr;
  if (!boolean_op(m, prism, Mat4::identity(), BooleanOp::Difference, &berr)) {
    if (err) *err = "Push Through (Boolean): " + berr;
    return false;
  }
  face_sel.assign(m.face_count(), 0);
  m.touch();
  return true;
}

/* ===================================================================== */
/* Push / Pull (SketchUp)                                                 */
/* ===================================================================== */

namespace {

/* Rays from every vertex of a face region along its normal (pull) or
 * against it (push), against all other faces. */
struct RegionRays {
  std::vector<uint32_t> verts;  // region vertices (outline first)
  std::vector<float> t;          // hit distance per vertex (-1 = none)
  std::vector<uint32_t> face;    // hit face per vertex
  std::vector<uint8_t> facing;   // the hit face faces back toward the region (an entry face for pulls, exit for pushes)
};

RegionRays cast_region(const Mesh &m, const Region &r, Vec3 dir, bool want_exit) {
  RegionRays out;
  std::vector<uint8_t> in_region(m.face_count(), 0), seen(m.vert_count(), 0);
  for (uint32_t f : r.faces) in_region[f] = 1;
  for (uint32_t v : r.loops[0]) if (!seen[v]) { seen[v] = 1; out.verts.push_back(v); }
  for (uint32_t f : r.faces)
    for (uint32_t i = 0; i < m.face_size(f); i++)
      if (!seen[m.face_verts(f)[i]]) { seen[m.face_verts(f)[i]] = 1; out.verts.push_back(m.face_verts(f)[i]); }
  struct Tri { Vec3 a, b, c; uint32_t face; };
  std::vector<Tri> tris;
  std::vector<uint32_t> local;
  for (size_t f = 0; f < m.face_count(); f++) {
    if (in_region[f]) continue;
    triangulate_face_local(m, f, local);
    const uint32_t *v = m.face_verts(f);
    for (size_t i = 0; i + 2 < local.size(); i += 3)
      tris.push_back({m.positions[v[local[i]]], m.positions[v[local[i + 1]]], m.positions[v[local[i + 2]]], (uint32_t)f});
  }
  const float scale = mesh_scale(m);
  for (uint32_t v : out.verts) {
    /* Start a hair inside the region (rays from a corner run along edges)
     * and off the surface. */
    Vec3 o = m.positions[v] + (r.center - m.positions[v]) * 1e-3f + dir * (1e-4f * scale);
    Ray ray{o, dir};
    float best = 1e30f;
    uint32_t bf = UINT32_MAX;
    for (const Tri &tr : tris) {
      float h = ray_triangle(ray, tr.a, tr.b, tr.c);
      if (h > 0 && h < best) { best = h; bf = tr.face; }
    }
    out.t.push_back(bf == UINT32_MAX ? -1.0f : best + 1e-4f * scale);
    out.face.push_back(bf);
    bool fac = bf != UINT32_MAX && (want_exit ? dot(m.face_normal(bf), dir) > 0 : dot(m.face_normal(bf), dir) < 0);
    out.facing.push_back(fac);
  }
  return out;
}

/* How far the region can travel along dir before it meets other geometry:
 * the nearest point of any other face's edges inside the prism the outline
 * sweeps (vertices inside it, and edges crossing its sides). Rays from the
 * corners alone miss obstacles in the middle (a tunnel under a face).
 * Returns -1 when nothing is in the way. */
float sweep_limit(const Mesh &m, const Region &r, Vec3 dir, float tol) {
  const std::vector<uint32_t> &L = r.loops[0];
  if (L.size() < 3) return -1.0f;
  /* A basis across dir: the outline is tested in that plane. */
  const Vec3 u = normalize(cross(dir, std::fabs(dir.y) < 0.9f ? Vec3(0, 1, 0) : Vec3(1, 0, 0))), w = cross(dir, u);
  const Vec3 o = m.positions[L[0]];
  std::vector<Vec2> poly;
  poly.reserve(L.size());
  Vec2 lo{1e30f, 1e30f}, hi{-1e30f, -1e30f};
  for (uint32_t v : L) {
    Vec3 d = m.positions[v] - o;
    Vec2 p{dot(d, u), dot(d, w)};
    poly.push_back(p);
    lo = {std::min(lo.x, p.x), std::min(lo.y, p.y)};
    hi = {std::max(hi.x, p.x), std::max(hi.y, p.y)};
  }
  /* The region's own plane height along dir: obstacles must be beyond it. */
  float h0 = -1e30f;
  for (uint32_t v : L) h0 = std::max(h0, dot(m.positions[v] - o, dir));
  auto seg_dist = [](Vec2 p, Vec2 a, Vec2 b) {
    Vec2 ab = b - a, ap = p - a;
    float t = std::max(0.0f, std::min(1.0f, (ap.x * ab.x + ap.y * ab.y) / std::max(1e-30f, ab.x * ab.x + ab.y * ab.y)));
    Vec2 c{a.x + ab.x * t - p.x, a.y + ab.y * t - p.y};
    return std::sqrt(c.x * c.x + c.y * c.y);
  };
  auto inside = [&](Vec2 p) {
    if (p.x < lo.x - tol || p.y < lo.y - tol || p.x > hi.x + tol || p.y > hi.y + tol) return false;
    bool in = false;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
      const Vec2 a = poly[i], b = poly[j];
      if (seg_dist(p, a, b) <= tol) return true;  // on the outline counts
      if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) in = !in;
    }
    return in;
  };
  std::vector<uint8_t> in_region(m.face_count(), 0);
  for (uint32_t f : r.faces) in_region[f] = 1;
  float best = -1.0f;
  auto take = [&](float h) {
    h -= h0;
    if (h > tol && (best < 0 || h < best)) best = h;
  };
  std::unordered_set<uint64_t> done;
  for (size_t f = 0; f < m.face_count(); f++) {
    if (in_region[f]) continue;
    const uint32_t n = m.face_size(f), *fv = m.face_verts(f);
    for (uint32_t i = 0; i < n; i++) {
      const uint32_t a = fv[i], b = fv[(i + 1) % n];
      if (!done.insert(Mesh::edge_key(a, b)).second) continue;
      const Vec3 da = m.positions[a] - o, db = m.positions[b] - o;
      const Vec2 pa{dot(da, u), dot(da, w)}, pb{dot(db, u), dot(db, w)};
      if (std::max(pa.x, pb.x) < lo.x - tol || std::min(pa.x, pb.x) > hi.x + tol || std::max(pa.y, pb.y) < lo.y - tol ||
          std::min(pa.y, pb.y) > hi.y + tol)
        continue;
      const float ha = dot(da, dir), hb = dot(db, dir);
      if (inside(pa)) take(ha);
      if (inside(pb)) take(hb);
      /* Crossings with the outline's sides. */
      for (size_t k = 0, j = poly.size() - 1; k < poly.size(); j = k++) {
        const Vec2 c = poly[j], d = poly[k];
        const Vec2 e = pb - pa, g = d - c;
        const float den = e.x * g.y - e.y * g.x;
        if (std::fabs(den) < 1e-20f) continue;
        const float s = ((c.x - pa.x) * g.y - (c.y - pa.y) * g.x) / den, t = ((c.x - pa.x) * e.y - (c.y - pa.y) * e.x) / den;
        if (s > 0.0f && s < 1.0f && t >= 0.0f && t <= 1.0f) take(ha + (hb - ha) * s);
      }
    }
  }
  return best;
}


/* Faces with (next to) no area: what a clean result must not add. */
static size_t sliver_faces(const Mesh &m, float scale) {
  size_t n = 0;
  for (size_t f = 0; f < m.face_count(); f++) {
    const uint32_t *v = m.face_verts(f);
    const Vec3 o = m.positions[v[0]];
    Vec3 an(0.0f);
    for (uint32_t i = 0; i < m.face_size(f); i++) an += cross(m.positions[v[i]] - o, m.positions[v[(i + 1) % m.face_size(f)]] - o);
    if (length(an) * 0.5f < 1e-8f * scale * scale) n++;
  }
  return n;
}

/* Every edge used by exactly two faces, in opposite directions. */
static bool closed_manifold(const Mesh &m) {
  std::unordered_map<uint64_t, int> dir;
  for (size_t f = 0; f < m.face_count(); f++) {
    const uint32_t *v = m.face_verts(f);
    const uint32_t n = m.face_size(f);
    for (uint32_t i = 0; i < n; i++) {
      uint32_t a = v[i], b = v[(i + 1) % n];
      dir[Mesh::edge_key(a, b)] += a < b ? 1 : 16;
    }
  }
  for (auto &[k, c] : dir)
    if (c != 17) return false;
  return true;
}

/* Tidies what an operator just built. A cut or a moved face can land exactly
 * on an existing corner or face; then the new geometry
 *  - welds any new vertex onto a vertex within eps (old ones preferred),
 *  - drops corners that repeat and faces left with fewer than 3,
 *  - removes pairs of faces on the same corners facing opposite ways (a new
 *    wall lying on an existing face: the two cancel, as solids do).
 * Vertices whose positions were in `old` are never welded to each other, so
 * deliberately unwelded geometry elsewhere is left alone. Returns the face
 * remap (old face -> new face, UINT32_MAX = removed). */
static std::vector<uint32_t> tidy_new_geometry(Mesh &m, const std::vector<Vec3> &old, float eps) {
  struct Key {
    uint32_t x, y, z;
    bool operator==(const Key &o) const { return x == o.x && y == o.y && z == o.z; }
  };
  struct KeyHash {
    size_t operator()(const Key &k) const { return ((size_t)k.x * 73856093u) ^ ((size_t)k.y * 19349663u) ^ ((size_t)k.z * 83492791u); }
  };
  auto key = [](Vec3 p) {
    Key k;
    std::memcpy(&k.x, &p.x, 4);
    std::memcpy(&k.y, &p.y, 4);
    std::memcpy(&k.z, &p.z, 4);
    return k;
  };
  /* New vertices: at a position nothing had before, or more of them at one
   * than before (a cut landing exactly on an existing corner). */
  std::unordered_map<Key, int, KeyHash> had;
  had.reserve(old.size());
  for (Vec3 p : old) had[key(p)]++;
  const uint32_t nv = (uint32_t)m.vert_count();
  std::vector<uint8_t> is_new(nv, 0);
  bool any_new = false;
  for (uint32_t v = 0; v < nv; v++) {
    auto it = had.find(key(m.positions[v]));
    if (it != had.end() && it->second > 0) it->second--;
    else {
      is_new[v] = 1;
      any_new = true;
    }
  }
  std::vector<uint32_t> identity(m.face_count());
  for (size_t f = 0; f < identity.size(); f++) identity[f] = (uint32_t)f;
  if (!any_new || !(eps > 0)) return identity;
  /* Weld: a grid of eps-sized cells; each new vertex looks at its 27 neighbours. */
  std::unordered_map<uint64_t, std::vector<uint32_t>> grid;
  auto cell = [&](Vec3 p, int dx, int dy, int dz) {
    int64_t x = (int64_t)std::floor(p.x / eps) + dx, y = (int64_t)std::floor(p.y / eps) + dy, z = (int64_t)std::floor(p.z / eps) + dz;
    return ((uint64_t)(x & 0x1FFFFF) << 42) | ((uint64_t)(y & 0x1FFFFF) << 21) | (uint64_t)(z & 0x1FFFFF);
  };
  for (uint32_t v = 0; v < nv; v++) grid[cell(m.positions[v], 0, 0, 0)].push_back(v);
  std::vector<uint32_t> to(nv);
  for (uint32_t v = 0; v < nv; v++) to[v] = v;
  bool welded = false;
  for (uint32_t v = 0; v < nv; v++) {
    if (!is_new[v]) continue;
    uint32_t best = v;
    for (int dx = -1; dx <= 1; dx++)
      for (int dy = -1; dy <= 1; dy++)
        for (int dz = -1; dz <= 1; dz++) {
          auto it = grid.find(cell(m.positions[v], dx, dy, dz));
          if (it == grid.end()) continue;
          for (uint32_t u : it->second) {
            if (u == v || length(m.positions[u] - m.positions[v]) > eps) continue;
            /* Prefer an old vertex, then the lowest index (new ones settle in order). */
            bool better = best == v || (!is_new[u] && is_new[best]) || (is_new[u] == is_new[best] && u < best);
            if (better && (!is_new[u] || u < v)) best = u;
          }
        }
    if (best != v) {
      to[v] = to[best];
      welded = true;
    }
  }
  /* Rebuild the faces through the weld, dropping repeats and collapsed faces. */
  std::vector<std::vector<uint32_t>> loops(m.face_count());
  std::vector<std::vector<Vec2>> luvs(m.face_count());
  const bool has_uv = m.has_uvs();
  for (size_t f = 0; f < m.face_count(); f++) {
    const uint32_t b0 = m.face_offsets[f];
    for (uint32_t i = 0; i < m.face_size(f); i++) {
      uint32_t v = to[m.face_verts(f)[i]];
      if (!loops[f].empty() && loops[f].back() == v) continue;
      loops[f].push_back(v);
      if (has_uv) luvs[f].push_back(m.uvs[b0 + i]);
    }
    while (loops[f].size() > 1 && loops[f].front() == loops[f].back()) {
      loops[f].pop_back();
      if (has_uv) luvs[f].pop_back();
    }
  }
  /* Opposite twins: the same corners, reversed. */
  std::map<std::vector<uint32_t>, std::vector<uint32_t>> by_set;
  for (size_t f = 0; f < loops.size(); f++) {
    if (loops[f].size() < 3) continue;
    std::vector<uint32_t> s = loops[f];
    std::sort(s.begin(), s.end());
    by_set[s].push_back((uint32_t)f);
  }
  std::vector<uint8_t> drop(loops.size(), 0);
  for (auto &[s, faces] : by_set) {
    if (faces.size() < 2) continue;
    for (size_t i = 0; i < faces.size(); i++)
      for (size_t j = i + 1; j < faces.size(); j++) {
        uint32_t a = faces[i], b = faces[j];
        if (drop[a] || drop[b] || loops[a].size() != loops[b].size()) continue;
        /* Reversed cyclic order: b read backwards is a rotation of a. */
        const auto &la = loops[a], &lb = loops[b];
        const size_t n = la.size();
        size_t start = std::find(lb.begin(), lb.end(), la[0]) - lb.begin();
        bool reversed = start < n;
        for (size_t k = 0; reversed && k < n; k++) reversed = la[k] == lb[(start + n - k) % n];
        if (reversed) drop[a] = drop[b] = 1;
      }
  }
  bool changed = welded;
  for (size_t f = 0; f < loops.size(); f++) changed = changed || drop[f] || loops[f].size() != m.face_size(f);
  if (!changed) return identity;
  FaceBuilder fb(m);
  std::vector<uint32_t> remap(m.face_count(), UINT32_MAX);
  for (size_t f = 0; f < loops.size(); f++) {
    if (drop[f] || loops[f].size() < 3) continue;
    remap[f] = (uint32_t)(fb.offs.size() - 1);
    fb.add(loops[f].data(), loops[f].size(), fb.has_uv ? luvs[f].data() : nullptr, m.material_of(f), m.smooth_of(f));
  }
  fb.commit(m);
  remove_loose_verts(m);
  return remap;
}

static void remap_selection(std::vector<uint8_t> &sel, const std::vector<uint32_t> &remap, size_t faces) {
  std::vector<uint8_t> out(faces, 0);
  for (size_t f = 0; f < sel.size() && f < remap.size(); f++)
    if (sel[f] && remap[f] != UINT32_MAX && remap[f] < faces) out[remap[f]] = 1;
  sel = std::move(out);
}

}  // namespace

PushPullLimits push_pull_limits(const Mesh &m, const std::vector<uint8_t> &face_sel) {
  PushPullLimits lim;
  std::vector<Region> regions = face_regions(m, face_sel);
  if (regions.size() != 1 || regions[0].loops.size() != 1) return lim;
  const Region &r = regions[0];
  /* The nearest surface behind and in front of the region: Push/Pull stops
   * there rather than passing through the mesh (which would turn it inside
   * out or make it intersect itself). */
  RegionRays down = cast_region(m, r, -r.normal, true);
  RegionRays up = cast_region(m, r, r.normal, false);
  /* Hits right at the start are the region's own neighbours (a corner on the
   * face next to it), not something in the way. */
  const float near = 1e-3f * mesh_scale(m);
  for (float t : down.t)
    if (t > near && (lim.behind < 0 || t < lim.behind)) lim.behind = t;
  for (float t : up.t)
    if (t > near && (lim.ahead < 0 || t < lim.ahead)) lim.ahead = t;
  {
    const float tol = 1e-4f * mesh_scale(m);
    const float sb = sweep_limit(m, r, -r.normal, tol), sa = sweep_limit(m, r, r.normal, tol);
    lim.behind_sweep = sb;
    if (sb > 0 && (lim.behind < 0 || sb < lim.behind)) lim.behind = sb;
    if (sa > 0 && (lim.ahead < 0 || sa < lim.ahead)) lim.ahead = sa;
  }
  /* A face whose every side continues into a coplanar wall (the whole top of a
   * box) just moves; holes and joins are for faces inside a larger surface. */
  EdgeFaces ef(m);
  bool inside_surface = false;
  const std::vector<uint32_t> &L = r.loops[0];
  std::vector<uint8_t> in_region(m.face_count(), 0);
  for (uint32_t f : r.faces) in_region[f] = 1;
  for (size_t i = 0; i < L.size(); i++)
    for (uint32_t g : ef.at(L[i], L[(i + 1) % L.size()]))
      if (!in_region[g] && std::fabs(dot(m.face_normal(g), r.normal)) > 0.01f) inside_surface = true;
  if (!inside_surface) return lim;
  /* Push: the far side, where every ray leaves the object. */
  bool ok = !down.verts.empty();
  float tmin = 1e30f;
  for (size_t i = 0; i < down.verts.size(); i++) {
    ok = ok && down.t[i] > near && down.facing[i];
    tmin = std::min(tmin, down.t[i]);
  }
  if (ok) lim.through = tmin;
  /* Pull: a face of the mesh in front, facing back (all rays on one face). */
  ok = !up.verts.empty();
  tmin = 1e30f;
  for (size_t i = 0; i < up.verts.size(); i++) {
    ok = ok && up.t[i] > near && up.facing[i] && up.face[i] == up.face[0];
    tmin = std::min(tmin, up.t[i]);
  }
  if (ok) {
    lim.contact = tmin;
    lim.contact_face = up.face[0];
  }
  return lim;
}

bool push_pull(Mesh &m, std::vector<uint8_t> &face_sel, float distance, bool merge_coplanar, PushPullResult *result, std::string *err) {
  if (result) *result = PushPullResult::Moved;
  face_sel.resize(m.face_count(), 0);
  std::vector<Region> regions = face_regions(m, face_sel);
  if (regions.size() != 1 || regions[0].loops.size() != 1) {
    if (err) *err = "Push/Pull works on one face (or one connected group of faces)";
    return false;
  }
  if (!std::isfinite(distance)) {
    if (err) *err = "Push/Pull: the distance must be a finite number";
    return false;
  }
  if (std::fabs(distance) < 1e-7f) return true;
  const Region r = regions[0];
  const float scale = mesh_scale(m);
  /* Far from the origin a float can't tell nearby positions apart (2 cm steps
   * at 200 km): tolerances grow with that, and a move smaller than a few
   * steps can't be represented, so it does nothing. */
  float extent = 0.0f;
  for (Vec3 p : m.positions) extent = std::max({extent, std::fabs(p.x), std::fabs(p.y), std::fabs(p.z)});
  const float ulp = extent * 1.2e-7f;
  if (std::fabs(distance) < 8.0f * ulp) return true;
  PushPullLimits lim = push_pull_limits(m, face_sel);
  const float snap = std::max(1e-3f * scale, 8.0f * ulp);
  /* What the result is checked against: new geometry is welded to what was
   * there, and a closed solid must stay closed. */
  const std::vector<Vec3> old_positions = m.positions;
  const bool was_closed = closed_manifold(m);
  const size_t slivers_before = sliver_faces(m, scale);
  const float weld = std::max(5e-4f * scale, 4.0f * ulp);  // cuts on curved faces land a hair off existing corners
  /* Pushed to (or past) the far side: a hole, cut into the exit face at its
   * own angle (SketchUp makes a hole when the push meets the back face). */
  if (distance < 0 && lim.through > 0 && -distance >= lim.through - snap) {
    Mesh before = m;
    std::vector<uint8_t> sel_before = face_sel;
    if (push_through(m, face_sel, 1, err)) {
      remap_selection(face_sel, tidy_new_geometry(m, old_positions, weld), m.face_count());
      if ((!was_closed || closed_manifold(m)) && sliver_faces(m, scale) <= slivers_before) {
        if (result) *result = PushPullResult::Hole;
        m.touch();
        return true;
      }
    }
    m = std::move(before);  // no clean hole there: stop at the far side instead
    face_sel = std::move(sel_before);
    if (err) err->clear();
  }
  /* New positions: along the normal, or, when pulled onto a face in front,
   * each vertex lands on that face where its ray meets it (any angle). */
  std::unordered_map<uint32_t, Vec3> target;
  bool joining = distance > 0 && lim.contact > 0 && distance >= lim.contact - snap && (lim.ahead < 0 || lim.ahead >= lim.contact - snap);
  EdgeFaces ef(m);
  std::vector<uint8_t> in_region(m.face_count(), 0);
  for (uint32_t f : r.faces) in_region[f] = 1;
  /* Neighbours that lean over the region (a wedge's slanted face, a sheared
   * box): a wall pushed in along the normal would come out through them and
   * turn the solid inside out, so the corners slide instead (below). */
  bool exits = false;
  if (distance < 0 && !joining) {
    const std::vector<uint32_t> &L = r.loops[0];
    for (size_t i = 0; i < L.size() && !exits; i++)
      for (uint32_t g : ef.at(L[i], L[(i + 1) % L.size()]))
        if (!in_region[g] && dot(-r.normal, m.face_normal(g)) > 0.02f) exits = true;
  }
  /* Otherwise never through the mesh: a push stops just short of the surface
   * behind (the far side), a pull just short of one in front - passing them
   * would turn the solid inside out or make it cut through itself. */
  if (distance < 0 && !exits && lim.behind > 0) distance = std::max(distance, -std::max(0.0f, lim.behind - 2.0f * snap));
  if (distance > 0 && !joining && lim.ahead > 0) distance = std::min(distance, std::max(0.0f, lim.ahead - 2.0f * snap));
  if (std::fabs(distance) < 1e-7f) return true;
  if (joining) {
    RegionRays up = cast_region(m, r, r.normal, false);
    const Vec3 nT = m.face_normal(lim.contact_face), pT = m.positions[m.face_verts(lim.contact_face)[0]];
    for (uint32_t v : up.verts) {
      /* Exact intersection with the target plane (the ray cast started off the vertex). */
      float denom = dot(r.normal, nT);
      float tt = std::fabs(denom) > 1e-8f ? dot(pT - m.positions[v], nT) / denom : 0.0f;
      target[v] = m.positions[v] + r.normal * tt;
    }
  }
  const Vec3 dvec = r.normal * distance;
  auto moved = [&](uint32_t v) {
    auto it = target.find(v);
    return it != target.end() ? it->second : m.positions[v] + dvec;
  };
  /* Leaning neighbours: the region moves the way Blender's face move does -
   * inner corners along the normal, each outline corner sliding within the
   * planes of the faces around it, so every neighbour keeps its plane. */
  if (exits) {
    const std::vector<uint32_t> &L = r.loops[0];
    {
      std::unordered_set<uint64_t> region_edges;
      std::unordered_set<uint32_t> region_verts;
      for (uint32_t f : r.faces)
        for (uint32_t i = 0; i < m.face_size(f); i++) {
          region_edges.insert(Mesh::edge_key(m.face_verts(f)[i], m.face_verts(f)[(i + 1) % m.face_size(f)]));
          region_verts.insert(m.face_verts(f)[i]);
        }
      std::unordered_set<uint32_t> outline(L.begin(), L.end());
      /* Each outline corner keeps to the planes of the unselected faces
       * around it: along the one plane, or the line where two meet (the edge
       * between them). `step` is its move per unit of push along the normal. */
      std::unordered_map<uint32_t, std::vector<Vec3>> planes;
      for (size_t f = 0; f < m.face_count(); f++) {
        if (in_region[f]) continue;
        for (uint32_t i = 0; i < m.face_size(f); i++) {
          const uint32_t v = m.face_verts(f)[i];
          if (!outline.count(v)) continue;
          const Vec3 nf = m.face_normal(f);
          auto &ps = planes[v];
          bool dup = false;
          for (Vec3 q : ps) dup = dup || std::fabs(dot(q, nf)) > 0.9999f;
          if (!dup) ps.push_back(nf);
        }
      }
      bool ok = true;
      std::unordered_map<uint32_t, Vec3> step;
      for (uint32_t v : L) {
        const auto &ps = planes[v];
        Vec3 t;
        if (ps.size() == 1) t = r.normal - ps[0] * dot(r.normal, ps[0]);
        else if (ps.size() >= 2) t = cross(ps[0], ps[1]);
        else { ok = false; break; }
        const float tn = dot(t, r.normal);
        if (std::fabs(tn) < 1e-3f * length(t) || length(t) < 1e-6f) { ok = false; break; }
        t = t / tn;  // one unit along the normal
        if (length(t) > 20.0f) { ok = false; break; }  // a plane almost along the normal: corners would fly sideways
        for (size_t k = 2; k < ps.size() && ok; k++) ok = std::fabs(dot(t, ps[k])) < 1e-3f * length(t);
        step[v] = t;
      }
      /* How far before a corner would slide past the end of an edge leaving it. */
      float reach = 1e30f;
      if (ok)
        for (const auto &e : m.edge_cache()) {
          if (region_edges.count(Mesh::edge_key(e.first, e.second))) continue;
          for (int k = 0; k < 2; k++) {
            const uint32_t v = k ? e.second : e.first, w = k ? e.first : e.second;
            if (!outline.count(v)) continue;
            const float along = dot(m.positions[w] - m.positions[v], r.normal);
            if (along < 0.0f) reach = std::min(reach, -along);
          }
        }
      if (!ok) {
        if (err) *err = "Push/Pull: the faces around the selection lean over it, so pushing it in would turn the solid inside out";
        return false;
      }
      /* Only what lies inside the swept outline stops it: the corner rays hit
       * the leaning neighbours at once, but those slide along. */
      const float d = std::max(distance, -std::max(0.0f, std::min(reach, lim.behind_sweep > 0 ? lim.behind_sweep : 1e30f) - 2.0f * snap));
      if (std::fabs(d) < 1e-7f) return true;
      std::vector<Vec3> to;
      std::vector<uint32_t> which;
      for (uint32_t v : region_verts) {
        Vec3 p = m.positions[v];
        p = outline.count(v) ? p + step[v] * d : p + r.normal * d;
        which.push_back(v);
        to.push_back(p);
      }
      for (size_t i = 0; i < which.size(); i++) m.positions[which[i]] = to[i];
      if (result) *result = PushPullResult::Moved;
      m.touch();
      return true;
    }
  }
  std::unordered_map<uint32_t, uint32_t> newv;
  for (uint32_t f : r.faces)
    for (uint32_t i = 0; i < m.face_size(f); i++) {
      uint32_t v = m.face_verts(f)[i];
      if (!newv.count(v)) newv[v] = UINT32_MAX;
    }
  for (auto &[v, nv] : newv) nv = m.add_vert(moved(v));
  /* Each side of the outline: a new wall, or - when the face beyond that side
   * lies in the wall's plane - the side's moved copy is inserted into that
   * face, which stretches or shrinks (SketchUp merges coplanar faces). */
  const std::vector<uint32_t> &L = r.loops[0];
  std::unordered_map<uint64_t, std::pair<uint32_t, uint32_t>> insert;  // directed edge in the neighbour -> (b', a')
  struct Wall { uint32_t v[4]; int mat; };
  std::vector<Wall> walls;
  std::unordered_set<uint32_t> touched;
  for (size_t i = 0; i < L.size(); i++) {
    uint32_t a = L[i], b = L[(i + 1) % L.size()];
    uint32_t N = UINT32_MAX;
    for (uint32_t g : ef.at(a, b))
      if (!in_region[g]) N = g;
    /* A side along the push direction would get a wall with no area: the side
     * just slides along itself, so its neighbour stretches even with Ctrl. */
    const Vec3 pa = m.positions[a], pb = m.positions[b], qa = m.positions[newv[a]], qb = m.positions[newv[b]];
    const float side = length(pb - pa), travel = std::max(length(qa - pa), length(qb - pb));
    const bool flat_wall = length(cross(qa - pb, qb - pa)) * 0.5f < 1e-4f * side * travel;
    if (flat_wall && N == UINT32_MAX) continue;  // an open edge: no wall needed
    bool coplanar = false;
    if ((merge_coplanar || flat_wall) && N != UINT32_MAX) {
      Vec3 nN = m.face_normal(N), pN = m.positions[a];
      coplanar = std::fabs(dot(m.positions[newv[a]] - pN, nN)) < snap && std::fabs(dot(m.positions[newv[b]] - pN, nN)) < snap;
    }
    if (coplanar) {
      insert[((uint64_t)b << 32) | a] = {newv[b], newv[a]};
      touched.insert(a);
      touched.insert(b);
      touched.insert(newv[a]);
      touched.insert(newv[b]);
    }
    else {
      int mat = m.material_of(N != UINT32_MAX ? N : r.faces[0]);
      walls.push_back({{a, b, newv[b], newv[a]}, mat});
    }
  }
  FaceBuilder fb(m);
  std::vector<uint32_t> verts;
  std::vector<Vec2> uv;
  std::vector<uint8_t> was_touched;
  for (size_t f = 0; f < m.face_count(); f++) {
    const uint32_t *v = m.face_verts(f);
    const uint32_t n = m.face_size(f), b0 = m.face_offsets[f];
    verts.clear();
    uv.clear();
    bool t = false;
    for (uint32_t i = 0; i < n; i++) {
      uint32_t a = v[i], c = v[(i + 1) % n];
      verts.push_back(in_region[f] ? newv[a] : a);
      if (fb.has_uv) uv.push_back(m.uvs[b0 + i]);
      if (in_region[f]) continue;
      auto it = insert.find(((uint64_t)a << 32) | c);
      if (it == insert.end()) continue;
      verts.push_back(it->second.first);
      verts.push_back(it->second.second);
      if (fb.has_uv) {
        uv.push_back(m.uvs[b0 + i]);
        uv.push_back(m.uvs[b0 + (i + 1) % n]);
      }
      t = true;
    }
    fb.add(verts.data(), verts.size(), fb.has_uv ? uv.data() : nullptr, m.material_of(f), m.smooth_of(f));
    was_touched.push_back(t);
  }
  for (const Wall &w : walls) {
    fb.add(w.v, 4, nullptr, w.mat);
    was_touched.push_back(0);
  }
  fb.commit(m);
  std::vector<uint32_t> remap;
  /* Stretched faces: drop corners that now sit on a straight line or fold
   * back on themselves (a box top pushed down leaves both kinds). A corner
   * that some other face still uses stays, or the two would no longer share
   * an edge (a T-junction opens the mesh). */
  {
    std::vector<std::vector<uint32_t>> loops(m.face_count());
    std::vector<std::vector<Vec2>> luvs(m.face_count());
    const bool has_uv = m.has_uvs();
    std::unordered_set<uint32_t> pinned;
    for (int round = 0; round < 8; round++) {
      for (size_t f = 0; f < m.face_count(); f++) {
        const uint32_t b0 = m.face_offsets[f];
        std::vector<uint32_t> &loop = loops[f];
        loop.assign(m.face_verts(f), m.face_verts(f) + m.face_size(f));
        if (has_uv) luvs[f].assign(m.uvs.begin() + b0, m.uvs.begin() + b0 + loop.size());
        if (f >= was_touched.size() || !was_touched[f]) continue;
        for (bool changed = true; changed && loop.size() > 2;) {
          changed = false;
          for (size_t i = 0; i < loop.size() && loop.size() > 2; i++) {
            uint32_t p = loop[(i + loop.size() - 1) % loop.size()], v = loop[i], q = loop[(i + 1) % loop.size()];
            if (!touched.count(v) || pinned.count(v)) continue;
            Vec3 e1 = m.positions[v] - m.positions[p], e2 = m.positions[q] - m.positions[v];
            float l1 = length(e1), l2 = length(e2);
            bool degenerate = l1 < snap || l2 < snap || length(cross(e1, e2)) < 1e-4f * l1 * l2;
            if (!degenerate) continue;
            loop.erase(loop.begin() + i);
            if (has_uv) luvs[f].erase(luvs[f].begin() + i);
            changed = true;
            break;
          }
        }
      }
      /* A corner dropped from one face but kept by another must stay everywhere. */
      std::unordered_set<uint32_t> used;
      for (const auto &loop : loops)
        if (loop.size() >= 3) used.insert(loop.begin(), loop.end());
      size_t before = pinned.size();
      for (size_t f = 0; f < m.face_count(); f++) {
        if (loops[f].size() == m.face_size(f)) continue;
        for (uint32_t i = 0; i < m.face_size(f); i++) {
          uint32_t v = m.face_verts(f)[i];
          if (used.count(v) && std::find(loops[f].begin(), loops[f].end(), v) == loops[f].end()) pinned.insert(v);
        }
      }
      if (pinned.size() == before) break;
    }
    FaceBuilder clean(m);
    remap.assign(m.face_count(), UINT32_MAX);
    for (size_t f = 0; f < m.face_count(); f++) {
      if (loops[f].size() < 3) continue;  // squashed flat
      remap[f] = (uint32_t)(clean.offs.size() - 1);
      clean.add(loops[f].data(), loops[f].size(), clean.has_uv ? luvs[f].data() : nullptr, m.material_of(f), m.smooth_of(f));
    }
    clean.commit(m);
  }
  remove_loose_verts(m);
  face_sel.assign(m.face_count(), 0);
  for (uint32_t f : r.faces)
    if (f < remap.size() && remap[f] != UINT32_MAX) face_sel[remap[f]] = 1;
  remap_selection(face_sel, tidy_new_geometry(m, old_positions, weld), m.face_count());
  if (result) *result = walls.empty() ? PushPullResult::Moved : PushPullResult::Extruded;
  if (joining) {
    /* The cap now lies on the face in front: join them (an opening there). */
    if (fuse_contacts(m, face_sel) && result) *result = PushPullResult::Joined;
  }
  m.touch();
  return true;
}

/* ===================================================================== */
/* Subdivide, dissolve, connect, collapse                                 */
/* ===================================================================== */

size_t subdivide_edges(Mesh &m, std::vector<uint8_t> &vert_sel, int cuts) {
  cuts = std::max(1, std::min(100, cuts));
  EdgeSplits splits;
  size_t n = 0;
  for (auto &e : selected_edges(m, vert_sel)) {
    for (int k = 1; k <= cuts; k++) {
      float t = k / (float)(cuts + 1);
      uint32_t nv = m.add_vert(lerp(m.positions[e.first], m.positions[e.second], t));
      splits[Mesh::edge_key(e.first, e.second)].push_back({e.first < e.second ? t : 1.0f - t, nv});
    }
    n++;
  }
  apply_edge_splits(m, splits);
  vert_sel.resize(m.vert_count(), 1);  // new vertices join the selection
  m.touch();
  return n;
}

size_t dissolve_edges(Mesh &m, std::vector<uint8_t> &vert_sel) {
  EdgeFaces ef(m);
  const size_t nf = m.face_count();
  std::vector<uint32_t> parent(nf);
  for (size_t i = 0; i < nf; i++) parent[i] = (uint32_t)i;
  std::function<uint32_t(uint32_t)> find = [&](uint32_t x) { return parent[x] == x ? x : parent[x] = find(parent[x]); };
  std::unordered_set<uint32_t> touched;
  size_t dissolved = 0;
  for (auto &e : selected_edges(m, vert_sel)) {
    FaceList fs = ef.at(e.first, e.second);
    if (fs.size() != 2 || fs[0] == fs[1]) continue;
    parent[find(fs[0])] = find(fs[1]);
    touched.insert(e.first);
    touched.insert(e.second);
    dissolved++;
  }
  if (!dissolved) return 0;
  std::map<uint32_t, std::vector<uint32_t>> groups;
  for (size_t f = 0; f < nf; f++) groups[find((uint32_t)f)].push_back((uint32_t)f);
  FaceBuilder fb(m);
  for (auto &[root, faces] : groups) {
    if (faces.size() == 1) {
      uint32_t f = faces[0];
      fb.add(m.face_verts(f), m.face_size(f), fb.has_uv ? &m.uvs[m.face_offsets[f]] : nullptr, m.material_of(f), m.smooth_of(f));
      continue;
    }
    /* Outline of the merged faces: directed edges whose reverse is not in the group. */
    std::unordered_set<uint64_t> directed;
    auto dkey = [](uint32_t a, uint32_t b) { return ((uint64_t)a << 32) | b; };
    for (uint32_t f : faces)
      for (uint32_t i = 0; i < m.face_size(f); i++) directed.insert(dkey(m.face_verts(f)[i], m.face_verts(f)[(i + 1) % m.face_size(f)]));
    std::unordered_map<uint32_t, uint32_t> next;
    std::unordered_map<uint32_t, Vec2> uv_at;
    bool ok = true;
    for (uint32_t f : faces)
      for (uint32_t i = 0; i < m.face_size(f); i++) {
        uint32_t a = m.face_verts(f)[i], b = m.face_verts(f)[(i + 1) % m.face_size(f)];
        if (fb.has_uv) uv_at[a] = m.uvs[m.face_offsets[f] + i];
        if (directed.count(dkey(b, a))) continue;
        if (next.count(a)) ok = false;
        next[a] = b;
      }
    std::vector<uint32_t> loop;
    if (ok && !next.empty()) {
      uint32_t start = next.begin()->first, v = start;
      do {
        loop.push_back(v);
        v = next[v];
      } while (v != start && loop.size() <= next.size());
      ok = v == start && loop.size() == next.size();
    }
    if (!ok || loop.size() < 3) {  // the faces enclose a hole: keep them
      for (uint32_t f : faces) fb.add(m.face_verts(f), m.face_size(f), fb.has_uv ? &m.uvs[m.face_offsets[f]] : nullptr, m.material_of(f), m.smooth_of(f));
      continue;
    }
    /* Blender's "Dissolve Vertices": drop corners left between two edges on a line. */
    std::vector<uint32_t> kept;
    for (size_t i = 0; i < loop.size(); i++) {
      uint32_t p = loop[(i + loop.size() - 1) % loop.size()], v = loop[i], q = loop[(i + 1) % loop.size()];
      if (touched.count(v) && loop.size() - (i - kept.size()) > 3) {
        Vec3 a = normalize(m.positions[v] - m.positions[p]), b = normalize(m.positions[q] - m.positions[v]);
        if (dot(a, b) > 0.9999f) continue;
      }
      kept.push_back(v);
    }
    std::vector<Vec2> uv;
    for (uint32_t v : kept) uv.push_back(uv_at[v]);
    fb.add(kept.data(), kept.size(), fb.has_uv ? uv.data() : nullptr, m.material_of(faces[0]), m.smooth_of(faces[0]));
  }
  fb.commit(m);
  remove_loose_verts(m);
  vert_sel.assign(m.vert_count(), 0);
  m.touch();
  return dissolved;
}

size_t connect_vertices(Mesh &m, std::vector<uint8_t> &vert_sel) {
  FaceBuilder fb(m);
  size_t made = 0;
  for (size_t f = 0; f < m.face_count(); f++) {
    const uint32_t *v = m.face_verts(f);
    const uint32_t n = m.face_size(f), b = m.face_offsets[f];
    int s0 = -1, s1 = -1, count = 0;
    for (uint32_t i = 0; i < n; i++)
      if (v[i] < vert_sel.size() && vert_sel[v[i]]) {
        count++;
        if (s0 < 0) s0 = (int)i; else s1 = (int)i;
      }
    bool adjacent = count == 2 && ((s1 - s0) == 1 || (s0 == 0 && s1 == (int)n - 1));
    if (count != 2 || adjacent) {
      fb.add(v, n, fb.has_uv ? &m.uvs[b] : nullptr, m.material_of(f), m.smooth_of(f));
      continue;
    }
    /* Split along s0 - s1 (Blender: J, Connect Vertex Path). */
    for (int part = 0; part < 2; part++) {
      std::vector<uint32_t> pv;
      std::vector<Vec2> pt;
      int from = part == 0 ? s0 : s1, to = part == 0 ? s1 : s0;
      for (int i = from;; i = (i + 1) % (int)n) {
        pv.push_back(v[i]);
        if (fb.has_uv) pt.push_back(m.uvs[b + i]);
        if (i == to) break;
      }
      fb.add(pv.data(), pv.size(), fb.has_uv ? pt.data() : nullptr, m.material_of(f), m.smooth_of(f));
    }
    made++;
  }
  fb.commit(m);
  m.touch();
  return made;
}

size_t collapse_edges(Mesh &m, std::vector<uint8_t> &vert_sel) {
  std::vector<uint32_t> parent(m.vert_count());
  for (size_t i = 0; i < parent.size(); i++) parent[i] = (uint32_t)i;
  std::function<uint32_t(uint32_t)> find = [&](uint32_t x) { return parent[x] == x ? x : parent[x] = find(parent[x]); };
  size_t n = 0;
  for (auto &e : selected_edges(m, vert_sel)) {
    parent[find(e.first)] = find(e.second);
    n++;
  }
  if (!n) return 0;
  std::unordered_map<uint32_t, std::pair<Vec3, int>> sum;
  for (uint32_t v = 0; v < m.vert_count(); v++) {
    if (find(v) == v && !(v < vert_sel.size() && vert_sel[v])) continue;
    auto &s = sum[find(v)];
    s.first += m.positions[v];
    s.second++;
  }
  std::vector<uint32_t> target(m.vert_count());
  for (uint32_t v = 0; v < m.vert_count(); v++) target[v] = find(v);
  for (auto &[root, s] : sum) m.positions[root] = s.first / (float)s.second;
  for (uint32_t &v : m.corner_verts) v = target[v];
  cleanup_faces(m);
  remove_loose_verts(m);
  vert_sel.assign(m.vert_count(), 0);
  m.touch();
  return n;
}


/* ===================================================================== */
/* Set Origin (Blender: object/object_transform.cc, ED_object_origin_set)  */
/* ===================================================================== */

Vec3 origin_point(const Mesh &m, OriginPoint mode) {
  if (m.positions.empty()) return Vec3(0.0f);
  if (mode == OriginPoint::BoundsCenter || mode == OriginPoint::BoundsBottom) {
    AABB b;
    for (Vec3 p : m.positions) b.add(p);
    Vec3 c = b.center();
    if (mode == OriginPoint::BoundsBottom) c.y = b.min.y;  // Unity's "pivot at the base"
    return c;
  }
  /* Accumulated in double: large meshes far from the origin lose float precision. */
  double cx = 0, cy = 0, cz = 0, wsum = 0;
  /* Tetrahedra apex and centroids relative to the first vertex: far from the
   * origin the products would cancel. */
  const Vec3 o = m.positions[0];
  if (mode == OriginPoint::VolumeCenter) {
    /* Each triangle with the origin forms a tetrahedron; the signed volumes
     * weight the tetrahedra's centroids (Blender: BKE_mesh_center_of_volume). */
    for (size_t f = 0; f < m.face_count(); f++) {
      const uint32_t *v = m.face_verts(f);
      const Vec3 a = m.positions[v[0]] - o;
      for (uint32_t i = 1; i + 1 < m.face_size(f); i++) {
        const Vec3 b = m.positions[v[i]] - o, c = m.positions[v[i + 1]] - o;
        double vol = (double)dot(a, cross(b, c)) / 6.0;
        Vec3 g = (a + b + c) * 0.25f;
        cx += g.x * vol;
        cy += g.y * vol;
        cz += g.z * vol;
        wsum += vol;
      }
    }
    AABB b;
    for (Vec3 p : m.positions) b.add(p);
    Vec3 e = b.max - b.min;
    double box = (double)std::max(e.x, 1e-6f) * std::max(e.y, 1e-6f) * std::max(e.z, 1e-6f);
    if (std::fabs(wsum) > 1e-6 * box) return o + Vec3((float)(cx / wsum), (float)(cy / wsum), (float)(cz / wsum));
    mode = OriginPoint::SurfaceCenter;  // open or flat: no volume to speak of
    cx = cy = cz = wsum = 0;
  }
  if (mode == OriginPoint::SurfaceCenter) {
    for (size_t f = 0; f < m.face_count(); f++) {
      const uint32_t *v = m.face_verts(f);
      const Vec3 a = m.positions[v[0]];
      for (uint32_t i = 1; i + 1 < m.face_size(f); i++) {
        const Vec3 b = m.positions[v[i]], c = m.positions[v[i + 1]];
        double area = 0.5 * (double)length(cross(b - a, c - a));
        Vec3 g = (a + b + c) * (1.0f / 3.0f);
        cx += g.x * area;
        cy += g.y * area;
        cz += g.z * area;
        wsum += area;
      }
    }
    if (wsum > 0) return Vec3((float)(cx / wsum), (float)(cy / wsum), (float)(cz / wsum));
    cx = cy = cz = 0;
  }
  for (Vec3 p : m.positions) {
    cx += p.x;
    cy += p.y;
    cz += p.z;
  }
  const double n = (double)m.positions.size();
  return Vec3((float)(cx / n), (float)(cy / n), (float)(cz / n));
}

void translate(Mesh &m, Vec3 offset) {
  for (Vec3 &p : m.positions) p += offset;
  m.touch();
}

/* Drawing a closed shape onto a face (SketchUp's Rectangle / Circle on a face,
 * UModeler's drawing tools): the face gets a ring of faces around a new inner
 * face, which can then be pushed or pulled. Points are in mesh space. */
long imprint_loop(Mesh &m, size_t face, const std::vector<Vec3> &pts_in, std::string *error) {
  auto fail = [&](const char *e) {
    if (error) *error = e;
    return -1L;
  };
  if (face >= m.face_count() || pts_in.size() < 3) return fail("a closed shape needs 3 or more points on a face");
  const Vec3 n = normalize(m.face_normal(face));
  const Vec3 p0 = m.positions[m.face_verts(face)[0]];
  const std::vector<uint32_t> outer = face_loop(m, face);
  const float eps = 1e-4f * std::max(1.0f, mesh_scale(m));
  std::vector<Vec3> pts;
  for (Vec3 p : pts_in) {
    p -= n * dot(p - p0, n);  // onto the face's plane
    if (!inside_loop(m, outer, p, n, eps)) return fail("the shape must lie inside the face it is drawn on");
    pts.push_back(p);
  }
  const int mat = m.material_of(face);
  const bool smooth = m.smooth_of(face);
  std::vector<uint32_t> inner;
  for (const Vec3 &p : pts) inner.push_back(m.add_vert(p));
  std::vector<uint32_t> made;
  annulus(m, outer, inner, n, mat, made);
  if (dot(loop_newell(m, inner), n) < 0) std::reverse(inner.begin(), inner.end());
  m.add_face(inner.data(), inner.size(), nullptr, mat);
  if (!m.face_smooth.empty())
    for (size_t f = m.face_count() - made.size() - 1; f < m.face_count(); f++) m.face_smooth[f] = smooth ? 1 : 0;
  /* The old face goes; everything after it moves down one. */
  std::vector<uint8_t> drop(m.face_count(), 0);
  drop[face] = 1;
  delete_faces(m, drop);
  m.touch();
  return (long)m.face_count() - 1;  // the new inner face
}

}  // namespace bl::meshops
