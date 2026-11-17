/* SPDX-FileCopyrightText: 2020 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */
// Automasking for Weight Paint and Vertex Paint (task 0519, Blender 5.3; shared by the two modes since task 0525):
// Topology, Mesh Boundary, Cavity (and Inverted), View Normal and Area Normal, and the stroke engine both modes dab
// with. Ported, not copied, from Blender's editors/sculpt_paint/mesh/sculpt_automasking.cc (normal_calc,
// calc_cavity_factor, calc_blurred_cavity_mesh, fill_topology_automasking_factors_mesh, init_boundary_masking_mesh),
// which 5.3 runs for weight and vertex paint as it does for sculpt.
#include "editor.h"
#include "../core/core.h"
#include "../core/jobs.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <unordered_map>

namespace bl {

const char *const kWeightCavityModes[3] = {"Off", "Cavity", "Inverted"};

/* The proportional-editing curves (1 at the centre, 0 at the radius). */
float paint_falloff(int kind, float t) {
  t = std::max(0.0f, std::min(1.0f, 1.0f - t));
  switch (kind) {
    case 1: return std::sqrt(std::max(0.0f, 2.0f * t - t * t));
    case 2: return std::sqrt(t);
    case 3: return t * t;
    case 4: return t;
    case 5: return 1.0f;
    default: return t * t * (3.0f - 2.0f * t);
  }
}

/* 1 when the angle between two normals is under `lower`, 0 over `upper`, a smoothstep between (Blender: normal_calc). */
static float normal_factor(Vec3 a, Vec3 b, float lower, float upper) {
  const float c = dot(a, b);
  if (!finite_bits(c)) return 1.0f;  // (no normal: a loose vertex, a NaN position - not held back by its direction)
  const float angle = std::acos(std::max(-1.0f, std::min(c, 1.0f)));
  if (angle > lower && angle < upper) {
    float t = 1.0f - (angle - lower) / (upper - lower);
    return t * t * (3.0f - 2.0f * t);
  }
  return angle > upper ? 0.0f : 1.0f;
}

static Vec3 unit_or_zero(Vec3 v) {
  const float l = length(v);
  return finite_bits(l) && l > 1e-12f ? v * (1.0f / l) : Vec3(0, 0, 0);
}

/* Vertex v's neighbours in the CSR lists (none past their end). */
static void nb_range(const std::vector<uint32_t> &nb_off, size_t v, uint32_t &k0, uint32_t &k1) {
  k0 = v < nb_off.size() ? nb_off[v] : 0;
  k1 = v + 1 < nb_off.size() ? nb_off[v + 1] : k0;
}

/* ---- the per-mesh helpers ---- */

/* Normals, neighbours and the X-mirror map, kept while the mesh is unchanged. */
void PaintMeshHelpers::prepare(const Mesh &m, bool mirror_x) {
  if (mesh == &m && version == m.version && normals.size() == m.vert_count() && mirror_done == mirror_x) return;
  normals = meshops::vertex_normals(m);
  meshops::vertex_neighbors(m, nb_off, nb);
  mirror.assign(m.vert_count(), UINT32_MAX);
  if (mirror_x && m.vert_count()) {
    /* Twins across the object's X = 0 plane, within a hundredth of a typical edge (not of the whole size: one stray
     * far vertex must not put every vertex in one cell). */
    std::vector<float> lens;
    for (size_t f = 0; f < m.face_count() && lens.size() < 4096; f++)
      for (uint32_t k = 0, n = m.face_size(f); k < n; k++) {
        const float l = length(m.positions[m.face_verts(f)[k]] - m.positions[m.face_verts(f)[(k + 1) % n]]);
        if (finite_bits(l) && l > 0.0f) lens.push_back(l);
      }
    float typical = 1.0f;
    if (!lens.empty()) {
      std::nth_element(lens.begin(), lens.begin() + lens.size() / 2, lens.end());
      typical = lens[lens.size() / 2];
    }
    const float eps = std::max(1e-7f, typical * 0.01f);
    std::unordered_map<uint64_t, std::vector<uint32_t>> grid;
    grid.reserve(m.vert_count());
    auto key = [&](Vec3 p) { return meshops::grid_key(meshops::grid_cell(p.x / eps), meshops::grid_cell(p.y / eps), meshops::grid_cell(p.z / eps)); };
    for (uint32_t i = 0; i < (uint32_t)m.vert_count(); i++) grid[key(m.positions[i])].push_back(i);
    for (uint32_t i = 0; i < (uint32_t)m.vert_count(); i++) {
      const Vec3 q(-m.positions[i].x, m.positions[i].y, m.positions[i].z);
      const int64_t cx = meshops::grid_cell(q.x / eps), cy = meshops::grid_cell(q.y / eps), cz = meshops::grid_cell(q.z / eps);
      float best = eps * eps;
      for (int dz = -1; dz <= 1; dz++)
        for (int dy = -1; dy <= 1; dy++)
          for (int dx = -1; dx <= 1; dx++) {
            auto it = grid.find(meshops::grid_key(cx + dx, cy + dy, cz + dz));
            if (it == grid.end()) continue;
            for (uint32_t j : it->second)
              if (length_sq(m.positions[j] - q) <= best) best = length_sq(m.positions[j] - q), mirror[i] = j;
          }
    }
  }
  mesh = &m;
  version = m.version;
  mirror_done = mirror_x;
}

/* ---- the automask ---- */

/* Topology: the islands of the seeds (Blender: fill_topology_automasking_factors_mesh's flood fill). */
static void flood_islands(const std::vector<uint32_t> &nb_off, const std::vector<uint32_t> &nb, std::vector<uint32_t> queue, std::vector<uint8_t> &island) {
  for (uint32_t s : queue) island[s] = 1;
  for (size_t q = 0; q < queue.size(); q++) {
    uint32_t k0, k1;
    nb_range(nb_off, queue[q], k0, k1);
    for (uint32_t k = k0; k < k1; k++) {
      const uint32_t w = nb[k];
      if (w >= island.size() || island[w]) continue;
      island[w] = 1;
      queue.push_back(w);
    }
  }
}

/* Mesh Boundary: each vertex's edge steps from an open edge, up to `steps`, -1 beyond (Blender:
 * init_boundary_masking_mesh). An open edge has one face; a vertex in no face is all edge; a non-manifold edge (3
 * faces) is not open. Made again every stroke: one sort of the edges, and a painted stroke changes the mesh anyway. */
static void boundary_distances(const Mesh &m, const std::vector<uint32_t> &nb_off, const std::vector<uint32_t> &nb, int steps, std::vector<int> &dist) {
  const size_t n = m.vert_count();
  dist.assign(n, -1);
  std::vector<uint64_t> keys;
  std::vector<uint8_t> has_face(n, 0);
  for (size_t f = 0; f < m.face_count(); f++)
    for (uint32_t k = 0, fs = m.face_size(f); k < fs; k++) {
      const uint32_t a = m.face_verts(f)[k], b = m.face_verts(f)[(k + 1) % fs];
      if (a >= n || b >= n || a == b) continue;
      has_face[a] = 1;
      keys.push_back(((uint64_t)std::min(a, b) << 32) | std::max(a, b));
    }
  std::sort(keys.begin(), keys.end());
  for (size_t i = 0; i < keys.size();) {
    size_t j = i;
    while (j < keys.size() && keys[j] == keys[i]) j++;
    if (j - i == 1) dist[(size_t)(keys[i] >> 32)] = dist[(size_t)(keys[i] & 0xFFFFFFFFu)] = 0;
    i = j;
  }
  for (size_t v = 0; v < n; v++)
    if (!has_face[v]) dist[v] = 0;
  for (int it = 0; it < steps; it++)
    for (size_t v = 0; v < n; v++) {
      if (dist[v] != -1) continue;
      uint32_t k0, k1;
      nb_range(nb_off, v, k0, k1);
      for (uint32_t k = k0; k < k1; k++)
        if (nb[k] < n && dist[nb[k]] == it) dist[v] = it + 1;
    }
}

/* A vertex's cavity: its wider neighbourhood's centre against the centre of the rings inside it, along their normal,
 * over the mean distance (Blender: calc_blurred_cavity_mesh, calc_cavity_factor). Positive in a hollow. The walk's
 * queue and visited marks are kept per thread (Blender: an inline set), not allocated for every vertex. */
static float cavity_of(const Mesh &m, const std::vector<Vec3> &normals, const std::vector<uint32_t> &nb_off, const std::vector<uint32_t> &nb,
                       size_t vert, int blur, float cavity_factor, bool inverted) {
  thread_local std::vector<uint32_t> stamp;
  thread_local uint32_t generation = 0;
  thread_local std::vector<std::pair<uint32_t, int>> queue;
  const size_t n = m.vert_count();
  if (stamp.size() < n) stamp.assign(n, 0), generation = 0;
  if (++generation == 0) std::fill(stamp.begin(), stamp.end(), 0u), generation = 1;  // (wrapped: every mark stale)
  const int num_steps = blur + 1;
  queue.clear();
  queue.push_back({(uint32_t)vert, 0});
  stamp[vert] = generation;
  const Vec3 start = m.positions[vert];
  Vec3 all_pos(0, 0, 0), in_pos(0, 0, 0), in_nrm(0, 0, 0);
  float all_dist = 0.0f;
  int all_count = 0, in_count = 0;
  for (size_t q = 0; q < queue.size(); q++) {
    const uint32_t v = queue[q].first;
    const int depth = queue[q].second;
    all_pos = all_pos + m.positions[v];
    all_dist += length(m.positions[v] - start);
    all_count++;
    if (depth >= num_steps) continue;
    in_pos = in_pos + m.positions[v];
    in_nrm = in_nrm + normals[v];
    in_count++;
    uint32_t k0, k1;
    nb_range(nb_off, v, k0, k1);
    for (uint32_t k = k0; k < k1; k++) {
      const uint32_t w = nb[k];
      if (w >= n || stamp[w] == generation) continue;
      stamp[w] = generation;
      queue.push_back({w, depth + 1});
    }
  }
  all_pos = all_pos * (1.0f / (float)all_count);
  all_dist /= (float)all_count;
  in_pos = in_pos * (1.0f / (float)std::max(1, in_count));
  Vec3 nrm = unit_or_zero(in_nrm);
  if (length_sq(nrm) == 0.0f) nrm = normals[vert];
  const float sum = all_dist != 0.0f ? dot(all_pos - in_pos, nrm) / all_dist : 0.0f;
  float f = std::fabs(sum) * cavity_factor * 50.0f;
  f = (sum < 0.0f ? -f : f) * 0.5f + 0.5f;
  f = finite_bits(f) ? std::max(0.0f, std::min(f, 1.0f)) : 0.5f;
  return inverted ? 1.0f - f : f;
}

void PaintAutomask::begin(const PaintAutomaskFields &b, const PaintMeshHelpers &h, const Mesh &m, size_t start, Vec3 area, Vec3 start_normal,
                          Vec3 view, bool mirror_x) {
  const size_t n = m.vert_count();
  started = true;
  view_dir = unit_or_zero(view);
  /* A sum that nearly cancels (the brush centred on a thin fold) has no direction: the start vertex's normal then. */
  area_normal = length(area) > 1e-3f ? unit_or_zero(area) : unit_or_zero(start_normal);
  cavity.assign(n, -1.0f);
  island.assign(n, 0);
  if (b.am_topology && start < n) {
    flood_islands(h.nb_off, h.nb, {(uint32_t)start}, island);
    /* Mirror X: the twins of the island's vertices start the mirrored island (Blender: the symmetric active vertices),
     * so a start vertex without an exact twin still finds it. */
    if (mirror_x) {
      std::vector<uint32_t> twins;
      for (size_t v = 0; v < n && v < h.mirror.size(); v++)
        if (island[v] && h.mirror[v] != UINT32_MAX && h.mirror[v] < n && !island[h.mirror[v]]) twins.push_back(h.mirror[v]);
      if (!twins.empty()) flood_islands(h.nb_off, h.nb, twins, island);
    }
  }
  edge_steps = std::max(1, std::min(b.am_boundary_steps, 20));
  if (b.am_boundary) boundary_distances(m, h.nb_off, h.nb, edge_steps, edge_dist);
}

float PaintAutomask::factor(const PaintAutomaskFields &b, const PaintMeshHelpers &h, const Mesh &m, const Mat4 &N, size_t v) {
  float f = 1.0f;
  if (b.am_topology && !(v < island.size() && island[v])) return 0.0f;
  if (b.am_boundary && v < edge_dist.size() && edge_dist[v] != -1) {
    const float p = 1.0f - (float)edge_dist[v] / (float)std::max(1, edge_steps);
    f *= 1.0f - p * p;
  }
  if (b.am_cavity != 0 && v < cavity.size()) {
    if (cavity[v] < 0.0f)
      cavity[v] = cavity_of(m, h.normals, h.nb_off, h.nb, v, std::max(0, std::min(b.am_cavity_blur, 25)),
                            finite_bits(b.am_cavity_factor) ? std::max(0.0f, b.am_cavity_factor) : 0.5f, b.am_cavity == 2);
    f *= cavity[v];
  }
  if ((b.am_view_normal || b.am_area_normal) && v < h.normals.size()) {
    const Vec3 nw = unit_or_zero(N.dir(h.normals[v]));
    if (length_sq(nw) > 0.0f) {
      if (b.am_view_normal) {
        const float lim = b.am_view_limit * (kPi / 180.0f), fall = b.am_view_falloff * kPi;
        f *= normal_factor(nw, view_dir, lim, lim + fall);
      }
      if (b.am_area_normal) {
        const float lim = b.am_area_limit * (kPi / 180.0f), fall = b.am_area_falloff * kPi;
        f *= normal_factor(nw, area_normal, lim - fall * 0.5f, lim + fall * 0.5f);
      }
    }
  }
  return finite_bits(f) ? std::max(0.0f, std::min(f, 1.0f)) : 0.0f;
}

/* <prefix>automask <topology|boundary|cavity|inverted|view|area> [on|off]; ... none; ... <boundarysteps|cavityfactor|
 * cavityblur|viewlimit|viewfalloff|arealimit|areafalloff> <number>. */
bool paint_automask_command(PaintAutomaskFields &b, const std::vector<std::string> &t, const char *mode_name) {
  std::string what = t.size() > 1 ? to_lower(t[1]) : "";
  const std::string val = t.size() > 2 ? to_lower(t[2]) : "on";
  const bool is_switch = val == "on" || val == "off" || val == "1" || val == "0";
  const bool on = val == "on" || val == "1";
  /* A number only when the whole word is one and finite: "abc" or "3x" refuse rather than read as 0. */
  char *end = nullptr;
  const float num = t.size() > 2 ? std::strtof(t[2].c_str(), &end) : 0.0f;
  const bool has_num = t.size() > 2 && end && end != t[2].c_str() && *end == 0 && finite_bits(num);
  const bool toggle = what == "topology" || what == "boundary" || what == "cavity" || what == "inverted" || what == "view" || what == "area";
  if (toggle && !is_switch) what.clear();  // ("banana" is neither on nor off: refused below)
  if (what == "none") b.am_topology = b.am_boundary = b.am_view_normal = b.am_area_normal = false, b.am_cavity = 0;
  else if (what == "topology") b.am_topology = on;
  else if (what == "boundary") b.am_boundary = on;
  else if (what == "cavity") b.am_cavity = on ? 1 : 0;
  else if (what == "inverted") b.am_cavity = on ? 2 : b.am_cavity == 2 ? 1 : b.am_cavity;  // (off leaves Cavity on, as Blender's own checkbox)
  else if (what == "view") b.am_view_normal = on;
  else if (what == "area") b.am_area_normal = on;
  else if (what == "boundarysteps" && has_num) b.am_boundary_steps = (int)std::max(1.0f, std::min(num, 20.0f));  // (clamped as a float: 1e10 must not overflow the int)
  else if (what == "cavityfactor" && has_num) b.am_cavity_factor = std::max(0.0f, std::min(num, 5.0f));
  else if (what == "cavityblur" && has_num) b.am_cavity_blur = (int)std::max(0.0f, std::min(num, 25.0f));
  else if (what == "viewlimit" && has_num) b.am_view_limit = std::max(0.0f, std::min(num, 180.0f));
  else if (what == "viewfalloff" && has_num) b.am_view_falloff = std::max(0.0001f, std::min(num, 1.0f));
  else if (what == "arealimit" && has_num) b.am_area_limit = std::max(0.0f, std::min(num, 180.0f));
  else if (what == "areafalloff" && has_num) b.am_area_falloff = std::max(0.0001f, std::min(num, 1.0f));
  else {
    Log::warn("%s topology|boundary|cavity|inverted|view|area [on|off], none, or boundarysteps|cavityfactor|cavityblur|"
              "viewlimit|viewfalloff|arealimit|areafalloff <number>",
              t.empty() ? "automask" : t[0].c_str());
    return false;
  }
  Log::info("%s automasking: topology %s, boundary %s (%d), cavity %s, view normal %s, area normal %s", mode_name, b.am_topology ? "on" : "off",
            b.am_boundary ? "on" : "off", b.am_boundary_steps, kWeightCavityModes[std::max(0, std::min(b.am_cavity, 2))], b.am_view_normal ? "on" : "off",
            b.am_area_normal ? "on" : "off");
  return true;
}

/* ---- the editor's side: where a stroke starts, and a dab's amounts ---- */

/* Where a stroke starts (Blender: the surface under the cursor). With a hit on the painted object in the view's id and
 * depth planes: the vertex nearest the hit, and the area normal of the vertices within the brush's world radius of it,
 * so neither a piece hidden behind nor a closed mesh's far side counts. Without one (the planes not drawn yet, the
 * brush's centre off the object): the front-facing vertex nearest the brush's centre on screen. */
bool Editor::paint_pick_start(const PaintMeshHelpers &h, const Mesh &m, const Mat4 &W, const Mat4 &N, float x, float y, float R, uint64_t obj,
                              size_t &start, Vec3 &area) {
  const size_t n = std::min(m.vert_count(), h.normals.size());
  const Vec3 eye = cam_.position(), fwd = cam_.forward();
  start = SIZE_MAX;
  area = Vec3(0, 0, 0);
  Vec3 hit;
  float dist = 0.0f;
  if (sculpt_hit(scene_rect_, x, y, hit, dist, obj)) {
    const float rw = sculpt_world_radius(R, dist);
    float best = FLT_MAX;
    for (size_t v = 0; v < n; v++) {
      const Vec3 p = W.point(m.positions[v]);
      const float d = length(p - hit);
      if (!finite_bits(d)) continue;
      if (d < best) best = d, start = v;
      if (d < rw) area = area + unit_or_zero(N.dir(h.normals[v])) * (1.0f - d / rw);
    }
    return start != SIZE_MAX;
  }
  float best = R;
  for (size_t v = 0; v < n; v++) {
    const Vec3 p = W.point(m.positions[v]);
    Vec2 sp;
    float z;
    if (!scene_r3d_.project(p, sp, z)) continue;
    const float d = length(Vec2(sp.x - x, sp.y - y));
    if (!(d < R)) continue;
    const Vec3 nw = unit_or_zero(N.dir(h.normals[v]));
    const Vec3 to_cam = cam_.ortho ? -fwd : eye - p;
    if (!(dot(nw, to_cam) > 0.0f)) continue;  // (no depth to go by: the faces turned away are behind)
    area = area + nw * (1.0f - d / R);
    if (d < best) best = d, start = v;
  }
  return start != SIZE_MAX;
}

/* One dab's amount per vertex (0: outside the brush) and, with automasking on, each vertex's automask factor (already
 * in the amount; the caller's blend may take it again, as Blender's does). Mirror X: a twin takes the larger of its own
 * and its partner's amount, with the partner's automask (each pair painted once). False when the automask has no
 * vertex to start from yet - the dab then paints nothing. */
bool Editor::paint_dab_amounts(const PaintDab &d, PaintMeshHelpers &h, PaintAutomask &am, std::vector<float> &amount, std::vector<float> &mask) {
  const Mesh &m = *d.mesh;
  const size_t n = m.vert_count();
  if (h.normals.size() != n) return false;
  const Mat4 &W = *d.world;
  const Mat4 N = W.inverse().transposed();
  const Vec3 eye = cam_.position(), fwd = cam_.forward();
  const bool masked = d.automask->am_any();
  if (masked && !am.started) {
    size_t start = SIZE_MAX;
    Vec3 area;
    if (!paint_pick_start(h, m, W, N, d.x, d.y, d.radius, d.obj, start, area)) return false;
    am.begin(*d.automask, h, m, start, area, N.dir(h.normals[start]), -fwd, d.mirror_x);
  }
  amount.assign(n, 0.0f);
  mask.assign(masked ? n : 0, 1.0f);
  JobSystem::global().parallel_for((int64_t)n, 2048, [&](int64_t b, int64_t e) {
    for (int64_t v = b; v < e; v++) {
      const Vec3 p = W.point(m.positions[(size_t)v]);
      Vec2 sp;
      float z;
      if (!scene_r3d_.project(p, sp, z)) continue;
      const float dist = length(Vec2(sp.x - d.x, sp.y - d.y));
      if (!(dist < d.radius)) continue;
      if (d.front_only) {  // facing the camera (Blender: Front Faces Only)
        const Vec3 nw = N.dir(h.normals[(size_t)v]);
        const Vec3 to_cam = cam_.ortho ? -fwd : eye - p;
        if (!(dot(nw, to_cam) > 0.0f)) continue;
      }
      const float k = masked ? am.factor(*d.automask, h, m, N, (size_t)v) : 1.0f;
      if (masked) mask[(size_t)v] = k;
      amount[(size_t)v] = d.strength * paint_falloff(d.falloff, dist / d.radius) * k;
    }
  });
  if (!d.mirror_x) return true;
  for (size_t v = 0; v < n; v++) {
    if (!(amount[v] > 0.0f) || v >= h.mirror.size() || h.mirror[v] == UINT32_MAX || h.mirror[v] >= n) continue;
    const uint32_t t = h.mirror[v];
    if (amount[v] <= amount[t]) continue;
    amount[t] = amount[v];
    if (masked) mask[t] = mask[v];
  }
  return true;
}

}  // namespace bl
