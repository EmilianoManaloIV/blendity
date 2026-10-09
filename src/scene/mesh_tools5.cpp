// SPDX-License-Identifier: GPL-2.0-or-later
// Hard-surface tools: Grid, Pipe, Array, Taper and Recess.
// They work on the selection like the other Edit Mode tools and pair with drawing shapes and
// Push/Pull: draw a shape, Recess it into a panel, Array it along the surface, run a Pipe
// along an edge.
#include "mesh.h"
#include "mesh_internal.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace bl::meshops {

/* Grid: each selected quad cut into cols x rows quads. Sides shared with other faces
 * are split there too (no T-junctions); a side shared by two selected quads is cut once. */
size_t grid_faces(Mesh &m, std::vector<uint8_t> &face_sel, int cols, int rows) {
  cols = std::max(1, std::min(64, cols));
  rows = std::max(1, std::min(64, rows));
  face_sel.resize(m.face_count(), 0);
  struct Quad {
    size_t face;
    uint32_t c[4];
    Vec2 uv[4];
    int mat;
    bool smooth;
  };
  std::vector<Quad> quads;
  const bool has_uv = m.has_uvs();
  for (size_t f = 0; f < m.face_count(); f++) {
    if (!face_sel[f] || m.face_size(f) != 4) continue;
    Quad q{f, {}, {}, m.material_of(f), m.smooth_of(f)};
    for (int k = 0; k < 4; k++) {
      q.c[k] = m.face_verts(f)[k];
      if (has_uv) q.uv[k] = m.uvs[m.face_offsets[f] + k];
    }
    quads.push_back(q);
  }
  if (quads.empty() || (cols == 1 && rows == 1)) return 0;
  /* Each side's cut vertices, from its lower-numbered end. */
  std::unordered_map<uint64_t, std::vector<uint32_t>> cuts;
  auto side = [&](uint32_t a, uint32_t b, int n) {
    const uint64_t k = Mesh::edge_key(a, b);
    auto it = cuts.find(k);
    if (it == cuts.end() || (int)it->second.size() != n - 1) {
      if (it != cuts.end()) return std::vector<uint32_t>();  // already cut another way: this quad is left alone
      const uint32_t lo = std::min(a, b), hi = std::max(a, b);
      std::vector<uint32_t> vs;
      uint32_t from = lo;
      for (int i = 1; i < n; i++) {
        const uint32_t v = split_edge(m, from, hi, 1.0f / (float)(n - i + 1));
        vs.push_back(v);
        from = v;
      }
      it = cuts.emplace(k, vs).first;
    }
    std::vector<uint32_t> out = it->second;
    if (a > b) std::reverse(out.begin(), out.end());
    out.insert(out.begin(), a);
    out.push_back(b);
    return out;
  };
  std::vector<uint8_t> drop(m.face_count(), 0);
  std::vector<std::vector<uint32_t>> new_faces;
  std::vector<std::vector<Vec2>> new_uvs;
  std::vector<std::pair<int, bool>> new_attr;
  size_t done = 0;
  for (const Quad &q : quads) {
    const std::vector<uint32_t> s0 = side(q.c[0], q.c[1], cols), s1 = side(q.c[1], q.c[2], rows), s2 = side(q.c[3], q.c[2], cols),
                                s3 = side(q.c[0], q.c[3], rows);
    if (s0.empty() || s1.empty() || s2.empty() || s3.empty()) continue;
    /* G(i, j): i along c0 -> c1, j along c0 -> c3. */
    std::vector<uint32_t> G((size_t)(cols + 1) * (rows + 1));
    auto at = [&](int i, int j) -> uint32_t & { return G[(size_t)j * (cols + 1) + i]; };
    for (int i = 0; i <= cols; i++) at(i, 0) = s0[i], at(i, rows) = s2[i];
    for (int j = 0; j <= rows; j++) at(0, j) = s3[j], at(cols, j) = s1[j];
    const Vec3 p0 = m.positions[q.c[0]], p1 = m.positions[q.c[1]], p2 = m.positions[q.c[2]], p3 = m.positions[q.c[3]];
    for (int j = 1; j < rows; j++)
      for (int i = 1; i < cols; i++) {
        const float u = (float)i / cols, v = (float)j / rows;
        at(i, j) = m.add_vert(p0 * ((1 - u) * (1 - v)) + p1 * (u * (1 - v)) + p2 * (u * v) + p3 * ((1 - u) * v));
      }
    for (int j = 0; j < rows; j++)
      for (int i = 0; i < cols; i++) {
        new_faces.push_back({at(i, j), at(i + 1, j), at(i + 1, j + 1), at(i, j + 1)});
        std::vector<Vec2> t;
        if (has_uv)
          for (auto [di, dj] : {std::pair<int, int>{0, 0}, {1, 0}, {1, 1}, {0, 1}}) {
            const float u = (float)(i + di) / cols, v = (float)(j + dj) / rows;
            t.push_back(q.uv[0] * ((1 - u) * (1 - v)) + q.uv[1] * (u * (1 - v)) + q.uv[2] * (u * v) + q.uv[3] * ((1 - u) * v));
          }
        new_uvs.push_back(t);
        new_attr.push_back({q.mat, q.smooth});
      }
    drop[q.face] = 1;
    done++;
  }
  if (!done) return 0;
  drop.resize(m.face_count(), 0);
  const size_t kept = m.face_count() - (size_t)std::count(drop.begin(), drop.end(), 1);
  for (size_t k = 0; k < new_faces.size(); k++)
    m.add_face(new_faces[k].data(), new_faces[k].size(), new_uvs[k].empty() ? nullptr : new_uvs[k].data(), new_attr[k].first);
  drop.resize(m.face_count(), 0);
  delete_faces(m, drop);
  face_sel.assign(m.face_count(), 0);
  for (size_t f = kept; f < m.face_count(); f++) face_sel[f] = 1;
  m.touch();
  return done;
}

/* Pipe: a round tube of `sides` along the path (an ordered chain of vertices: wire edges
 * drawn with the Polyline tool, or edges of the mesh), capped at both ends. Wire edges it runs
 * along are used up; edges of the mesh stay. */
bool pipe(Mesh &m, const std::vector<uint32_t> &path, float radius, int sides, std::string *error) {
  auto fail = [&](const char *e) {
    if (error) *error = e;
    return false;
  };
  if (path.size() < 2) return fail("select a path of edges (a chain), or draw one with the Polyline tool");
  if (!(radius > 0) || !std::isfinite(radius)) return fail("the radius must be more than 0");
  sides = std::max(3, std::min(128, sides));
  for (uint32_t v : path)
    if (v >= m.vert_count()) return fail("the path has a vertex that isn't there");
  const Vec3 p0 = m.positions[path[0]];
  Vec3 t = m.positions[path[1]] - p0;
  if (length(t) < 1e-9f) return fail("the path's first edge has no length");
  t = normalize(t);
  const Vec3 a = normalize(cross(t, std::fabs(t.y) < 0.9f ? Vec3(0, 1, 0) : Vec3(1, 0, 0))), b = cross(t, a);
  std::vector<uint32_t> ring;
  for (int i = 0; i < sides; i++) {
    const float ang = 2.0f * kPi * i / sides;
    ring.push_back(m.add_vert(p0 + (a * std::cos(ang) + b * std::sin(ang)) * radius));
  }
  m.add_face(ring.data(), ring.size());
  const size_t face = m.face_count() - 1;
  /* All wire edges: the tube replaces them; otherwise they are the mesh's and stay. */
  bool wires = true;
  for (size_t i = 0; i + 1 < path.size() && wires; i++)
    wires = std::find(m.loose_edges.begin(), m.loose_edges.end(), Mesh::edge_key(path[i], path[i + 1])) != m.loose_edges.end();
  /* follow() reads wire paths from the face; a given path goes through follow_path (kept, starting
   * at the circle's centre - which is the path's first point). */
  std::string err;
  bool ok;
  if (wires) {
    std::vector<uint64_t> keep = m.loose_edges;  // only this path's wires may be used up
    std::vector<uint64_t> mine;
    for (size_t i = 0; i + 1 < path.size(); i++) mine.push_back(Mesh::edge_key(path[i], path[i + 1]));
    m.loose_edges = mine;
    ok = follow(m, face, &err);
    if (ok) {
      for (uint64_t k : keep)
        if (std::find(mine.begin(), mine.end(), k) == mine.end()) m.loose_edges.push_back(k);
    }
    else m.loose_edges = keep;
  }
  else ok = follow_path(m, face, path, &err);
  if (!ok) {
    std::vector<uint8_t> drop(m.face_count(), 0);
    drop[face] = 1;
    delete_faces(m, drop);
    return fail(err.empty() ? "could not sweep the tube" : err.c_str());
  }
  m.touch();
  return true;
}

/* Array: the selected faces repeated `count` times in all, each copy `offset` further on, cut into
 * the surface under it like a drawn shape. Everything ends selected. */
size_t array_faces(Mesh &m, std::vector<uint8_t> &face_sel, int count, Vec3 offset) {
  face_sel.resize(m.face_count(), 0);
  count = std::max(1, std::min(256, count));
  if (std::count(face_sel.begin(), face_sel.end(), 1) == 0 || count == 1 || !std::isfinite(offset.x + offset.y + offset.z)) return 0;
  /* The selected faces' outlines, as they are now (the copies are placed from these). */
  struct Shape {
    std::vector<Vec3> loop;
    Vec3 normal, centre;
  };
  std::vector<Shape> shapes;
  for (size_t f = 0; f < m.face_count(); f++)
    if (face_sel[f]) {
      Shape sh;
      for (uint32_t k = 0; k < m.face_size(f); k++) sh.loop.push_back(m.positions[m.face_verts(f)[k]]);
      sh.normal = normalize(m.face_normal(f));
      sh.centre = face_area_center(m, f);
      shapes.push_back(std::move(sh));
    }
  const std::vector<uint8_t> original = face_sel;
  std::vector<Vec3> wanted;  // where the selected faces end up (area centres), to select them at the end
  for (const Shape &sh : shapes) wanted.push_back(sh.centre);
  size_t made = 0;
  /* The flat surface a shape lies on: the outer outline of the faces in its plane around it
   * (Sutherland-Hodgman clips a copy hanging past its edge to it - when that outline is convex). */
  auto surface_outline = [&](Vec3 n, Vec3 p0, std::vector<Vec3> &outline) {
    const float eps = 1e-4f * std::max(1.0f, length(m.bounds().extent()));
    std::unordered_map<uint64_t, int> uses;
    for (size_t f = 0; f < m.face_count(); f++) {
      if (dot(normalize(m.face_normal(f)), n) < 0.9999f) continue;
      bool flat = true;
      for (uint32_t k = 0; k < m.face_size(f) && flat; k++) flat = std::fabs(dot(m.positions[m.face_verts(f)[k]] - p0, n)) < eps;
      if (!flat) continue;
      for (uint32_t k = 0; k < m.face_size(f); k++) {
        const uint32_t x = m.face_verts(f)[k], y = m.face_verts(f)[(k + 1) % m.face_size(f)];
        uses[((uint64_t)x << 32) | y]++;
      }
    }
    /* Directed sides with no reverse: the outlines, walked in the faces' own direction. */
    std::unordered_map<uint32_t, uint32_t> next;
    for (auto &kv : uses) {
      const uint32_t x = (uint32_t)(kv.first >> 32), y = (uint32_t)(kv.first & 0xFFFFFFFF);
      if (!uses.count(((uint64_t)y << 32) | x)) next[x] = y;
    }
    float best = 0.0f;
    std::unordered_set<uint32_t> seen;
    for (auto &kv : next) {
      if (seen.count(kv.first)) continue;
      std::vector<Vec3> loop;
      uint32_t v = kv.first;
      for (size_t guard = 0; guard <= next.size() && !seen.count(v); guard++) {
        seen.insert(v);
        loop.push_back(m.positions[v]);
        auto it = next.find(v);
        if (it == next.end()) break;
        v = it->second;
      }
      Vec3 nw(0.0f);
      for (size_t i = 0; i < loop.size(); i++) nw += cross(loop[i] - loop[0], loop[(i + 1) % loop.size()] - loop[0]);
      if (length(nw) > best) best = length(nw), outline = loop;
    }
    /* Corners on a straight side don't count; then every turn must go the same way. */
    std::vector<Vec3> c;
    for (size_t i = 0; i < outline.size(); i++) {
      const Vec3 a2 = outline[(i + outline.size() - 1) % outline.size()], b2 = outline[i], d2 = outline[(i + 1) % outline.size()];
      if (length(cross(b2 - a2, d2 - b2)) > 1e-7f * std::max(1.0f, length(b2 - a2) * length(d2 - b2))) c.push_back(b2);
    }
    outline = c;
    if (outline.size() < 3) return false;
    int sign = 0;
    for (size_t i = 0; i < outline.size(); i++) {
      const float t = dot(cross(outline[(i + 1) % outline.size()] - outline[i], outline[(i + 2) % outline.size()] - outline[(i + 1) % outline.size()]), n);
      const int sg = t > 0 ? 1 : -1;
      if (sign && sg != sign) return false;  // concave: not clipped
      sign = sg;
    }
    if (sign < 0) std::reverse(outline.begin(), outline.end());  // counter-clockwise about n
    return true;
  };
  auto clip = [](std::vector<Vec3> poly, const std::vector<Vec3> &win, Vec3 n) {
    for (size_t i = 0; i < win.size() && poly.size() >= 3; i++) {
      const Vec3 a2 = win[i], b2 = win[(i + 1) % win.size()];
      const Vec3 inward = cross(n, b2 - a2);  // left of a counter-clockwise side: inside
      auto side = [&](Vec3 p) { return dot(p - a2, inward); };
      std::vector<Vec3> out;
      for (size_t j = 0; j < poly.size(); j++) {
        const Vec3 p = poly[j], q = poly[(j + 1) % poly.size()];
        const float sp = side(p), sq = side(q);
        if (sp >= 0) out.push_back(p);
        if ((sp >= 0) != (sq >= 0)) out.push_back(p + (q - p) * (sp / (sp - sq)));
      }
      poly = std::move(out);
    }
    return poly;
  };
  for (int k = 1; k < count; k++) {
    const Vec3 off = offset * (float)k;
    /* Each copy is drawn onto the surface it lands on, as the original was: cut into the faces
     * there (no face lying on top of another). Hanging past the surface's edge, the part on it is
     * cut in. With nothing flat under it at all, it is a separate copy. */
    const Mesh before = m;
    const float eps = 1e-4f * std::max(1.0f, length(m.bounds().extent()));
    bool any_on = false, failed = false;
    for (const Shape &sh : shapes) {
      std::vector<Vec3> loop;
      for (const Vec3 &p : sh.loop) loop.push_back(p + off);
      size_t on = 0;
      for (const Vec3 &p : loop) {
        bool here = false;
        for (size_t f = 0; f < m.face_count() && !here; f++)
          here = dot(normalize(m.face_normal(f)), sh.normal) > 0.9999f && point_in_face(m, f, p, eps);
        on += here;
      }
      if (on == 0) continue;  // nothing of it on the surface
      any_on = true;
      if (on < loop.size()) {
        std::vector<Vec3> outline;
        if (!surface_outline(sh.normal, loop[0], outline)) continue;  // an odd-shaped surface: left out
        loop = clip(loop, outline, sh.normal);
        Vec3 nw(0.0f);
        for (size_t i = 0; i < loop.size(); i++) nw += cross(loop[i] - loop[0], loop[(i + 1) % loop.size()] - loop[0]);
        if (loop.size() < 3 || length(nw) < 1e-8f) continue;
      }
      std::vector<size_t> inner;
      std::string err;
      if (imprint_loop_across(m, loop, sh.normal, &inner, &err) < 0 || inner.empty()) {
        failed = true;
        break;
      }
      if (inner.size() > 1) {
        /* Cut across several faces (the ones round an earlier drawing): its pieces become one face. */
        std::unordered_map<uint64_t, int> uses;
        for (size_t f : inner)
          for (uint32_t i = 0; i < m.face_size(f); i++) uses[Mesh::edge_key(m.face_verts(f)[i], m.face_verts(f)[(i + 1) % m.face_size(f)])]++;
        std::unordered_set<uint64_t> join;
        for (auto &kv : uses)
          if (kv.second >= 2) join.insert(kv.first);
        if (!join.empty()) {
          std::vector<uint8_t> vs(m.vert_count(), 1);
          EdgeSelectionScope scope(&join);
          dissolve_edges(m, vs);
        }
      }
      Vec3 c(0.0f);
      float ar = 0.0f;
      for (size_t i = 1; i + 1 < loop.size(); i++) {
        const float t = 0.5f * dot(cross(loop[i] - loop[0], loop[i + 1] - loop[0]), sh.normal);  // signed: concave outlines too
        c += (loop[0] + loop[i] + loop[i + 1]) * (t / 3.0f);
        ar += t;
      }
      wanted.push_back(std::fabs(ar) > 1e-12f ? c / ar : loop[0]);
    }
    if (failed) {
      m = before;  // a cut that didn't take: leave this copy out rather than half of it
      continue;
    }
    if (!any_on) {
      std::vector<uint8_t> s2 = original;
      s2.resize(m.face_count(), 0);
      duplicate_faces(m, s2);  // s2: the copy
      std::unordered_set<uint32_t> verts;
      for (size_t f = 0; f < m.face_count(); f++)
        if (f < s2.size() && s2[f])
          for (uint32_t i = 0; i < m.face_size(f); i++) verts.insert(m.face_verts(f)[i]);
      for (uint32_t v : verts) m.positions[v] += off;
      for (const Shape &sh : shapes) wanted.push_back(sh.centre + off);
    }
    made++;
  }
  /* The originals and every copy selected (found by where their centres are). */
  face_sel.assign(m.face_count(), 0);
  const float eps = 1e-4f * std::max(1.0f, length(m.bounds().extent()));
  for (size_t f = 0; f < m.face_count(); f++) {
    const Vec3 c = face_area_center(m, f);
    for (const Vec3 &w : wanted)
      if (length(c - w) < eps) {
        face_sel[f] = 1;
        break;
      }
  }
  m.touch();
  return made;
}

/* taper: the selected faces extruded `distance` along their normal with the end scaled by
 * `scale` around its centre (a frustum: 1 is a straight extrusion, 0 a point). */
void extrude_taper(Mesh &m, std::vector<uint8_t> &face_sel, float distance, float scale) {
  if (!std::isfinite(distance) || !std::isfinite(scale)) return;
  extrude_faces(m, face_sel, distance);
  std::unordered_set<uint32_t> cap;
  Vec3 n(0.0f), c(0.0f);
  for (size_t f = 0; f < m.face_count() && f < face_sel.size(); f++)
    if (face_sel[f]) {
      n += m.face_normal(f);
      for (uint32_t i = 0; i < m.face_size(f); i++) cap.insert(m.face_verts(f)[i]);
    }
  if (cap.empty()) return;
  for (uint32_t v : cap) c += m.positions[v];
  c = c / (float)cap.size();
  n = length(n) > 1e-12f ? normalize(n) : Vec3(0.0f);
  scale = std::max(0.0f, scale);
  for (uint32_t v : cap) {
    const Vec3 d = m.positions[v] - c, along = n * dot(d, n);
    m.positions[v] = c + along + (d - along) * scale;
  }
  m.touch();
}

/* inset (a panel): the selection inset as one region by `border`, then its middle pushed
 * in by `depth` (positive: a recess) or pulled out (negative: a raised plate). */
void recess(Mesh &m, std::vector<uint8_t> &face_sel, float border, float depth) {
  if (!std::isfinite(border) || !std::isfinite(depth)) return;
  inset_region(m, face_sel, std::max(0.0f, border));
  if (std::fabs(depth) > 0) extrude_faces(m, face_sel, -depth);
  m.touch();
}

}  // namespace bl::meshops
