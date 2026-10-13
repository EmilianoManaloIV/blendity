// SPDX-License-Identifier: GPL-2.0-or-later
// The Mesh Deform modifier (task 0044, ADR 0023): a mesh bound to a closed cage follows the cage's vertices. Blender
// binds with harmonic coordinates (modifiers/intern/MOD_meshdeform.cc); this uses mean value coordinates, closed form:
// Ju, Schaefer and Warren, "Mean Value Coordinates for Closed Triangular Meshes" (SIGGRAPH 2005), the robust form of
// their pseudo-code (a point on a cage face takes that face's barycentric weights).
#include "scene.h"
#include "../core/core.h"
#include "../core/jobs.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <sstream>
#include <unordered_map>

namespace bl {

/* The cache of bound weights: one row of cage-vertex weights per mesh vertex. */
struct MeshDeformWeights {
  uint64_t key = 0;  // the binding's
  size_t verts = 0, cage = 0;
  std::vector<float> w;
  std::vector<Vec3> input;  // each row's input position (a row is redone when its vertex moves)
};

namespace {

uint64_t hash_bytes(uint64_t h, const void *p, size_t n) {
  const uint8_t *b = (const uint8_t *)p;
  for (size_t i = 0; i < n; i += 8) {
    uint64_t v = 0;
    std::memcpy(&v, b + i, std::min<size_t>(8, n - i));
    h = (h ^ v) * 1099511628211ull;
    h ^= h >> 29;
  }
  return h;
}

}  // namespace

/* One point's mean value coordinates over a closed triangle mesh (positions p, triangles t), into w (size p). */
void mean_value_coordinates(const Vec3 &x, const std::vector<Vec3> &p, const std::vector<uint32_t> &t, float *w) {
  const size_t nv = p.size();
  const double eps = 1e-8;        // a point on a cage vertex (relative to nothing: distances)
  const double eps_angle = 1e-6;  // on a triangle (h = pi) or in its plane (s = 0): rounding in the angles is ~1e-8
  std::vector<double> d(nv), acc(nv, 0.0);
  std::vector<double> u(nv * 3);
  for (size_t j = 0; j < nv; j++) {
    const double dx = (double)p[j].x - x.x, dy = (double)p[j].y - x.y, dz = (double)p[j].z - x.z;
    d[j] = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (d[j] < eps) {  // on a cage vertex: that vertex alone
      std::fill(w, w + nv, 0.0f);
      w[j] = 1.0f;
      return;
    }
    u[j * 3] = dx / d[j], u[j * 3 + 1] = dy / d[j], u[j * 3 + 2] = dz / d[j];
  }
  auto ud = [&](size_t a, size_t b) {  // |u_a - u_b|
    const double ex = u[a * 3] - u[b * 3], ey = u[a * 3 + 1] - u[b * 3 + 1], ez = u[a * 3 + 2] - u[b * 3 + 2];
    return std::sqrt(ex * ex + ey * ey + ez * ez);
  };
  for (size_t f = 0; f + 2 < t.size(); f += 3) {
    const size_t v[3] = {t[f], t[f + 1], t[f + 2]};
    double th[3], c[3], s[3];
    for (int i = 0; i < 3; i++) {
      const double l = ud(v[(i + 1) % 3], v[(i + 2) % 3]);
      th[i] = 2.0 * std::asin(std::min(1.0, l * 0.5));
    }
    const double h = (th[0] + th[1] + th[2]) * 0.5;
    if (kPi - h < eps_angle) {
      /* x lies on this triangle: its 2D barycentric weights. */
      std::fill(w, w + nv, 0.0f);
      double sum = 0.0, bw[3];
      for (int i = 0; i < 3; i++) sum += bw[i] = std::sin(th[i]) * d[v[(i + 2) % 3]] * d[v[(i + 1) % 3]];
      for (int i = 0; i < 3; i++) w[v[i]] += sum > 0.0 ? (float)(bw[i] / sum) : 1.0f / 3.0f;
      return;
    }
    const double *u0 = &u[v[0] * 3], *u1 = &u[v[1] * 3], *u2 = &u[v[2] * 3];
    const double det = u0[0] * (u1[1] * u2[2] - u1[2] * u2[1]) - u0[1] * (u1[0] * u2[2] - u1[2] * u2[0]) + u0[2] * (u1[0] * u2[1] - u1[1] * u2[0]);
    const double sign = det < 0.0 ? -1.0 : 1.0;
    bool skip = false;
    for (int i = 0; i < 3; i++) {
      const double si1 = std::sin(th[(i + 1) % 3]), si2 = std::sin(th[(i + 2) % 3]);
      c[i] = si1 * si2 > 0.0 ? (2.0 * std::sin(h) * std::sin(h - th[i])) / (si1 * si2) - 1.0 : 1.0;
      s[i] = sign * std::sqrt(std::max(0.0, 1.0 - c[i] * c[i]));
      skip = skip || std::fabs(s[i]) <= eps_angle;
    }
    if (skip) continue;  // x in the triangle's plane, outside it: no contribution
    for (int i = 0; i < 3; i++) {
      const int i1 = (i + 1) % 3, i2 = (i + 2) % 3;
      acc[v[i]] += (th[i] - c[i1] * th[i2] - c[i2] * th[i1]) / (d[v[i]] * std::sin(th[i1]) * s[i2]);
    }
  }
  double sum = 0.0;
  for (double a : acc) sum += a;
  if (!(std::fabs(sum) > 1e-30) || !std::isfinite(sum)) {
    std::fill(w, w + nv, 1.0f / (float)nv);  // nowhere sensible (far away, degenerate): the average
    return;
  }
  for (size_t j = 0; j < nv; j++) w[j] = (float)(acc[j] / sum);
}

static const GameObject *cage_target(const MeshDeformModifier &m, const Mesh **mesh) {
  thread_local int depth = 0;  // cages that cage each other (A by B by A) stop here
  *mesh = nullptr;
  if (!m.owner || !m.owner->scene || m.object.empty() || depth > 8) return nullptr;
  const GameObject *t = m.owner->scene->find_by_name(m.object);
  if (!t || t == m.owner) return nullptr;
  depth++;
  *mesh = t->evaluated_mesh();
  depth--;
  return *mesh ? t : nullptr;
}

uint64_t MeshDeformModifier::bind_key() const {
  uint64_t h = 1469598103934665603ull;
  const uint64_t n[2] = {bind_verts.size(), bind_tris.size()};
  h = hash_bytes(h, n, sizeof(n));
  if (!bind_verts.empty()) h = hash_bytes(h, bind_verts.data(), bind_verts.size() * sizeof(Vec3));
  if (!bind_tris.empty()) h = hash_bytes(h, bind_tris.data(), bind_tris.size() * sizeof(uint32_t));
  return h;
}

/* Why a cage can't be bound ("" when it can): finite, closed and manifold (every edge has one triangle each way round
 * it), and some volume. Used by Bind and by a binding read from a file. */
static std::string check_cage(const std::vector<Vec3> &verts, const std::vector<uint32_t> &tris) {
  if (tris.size() < 12 || tris.size() % 3) return "the cage needs a closed surface (at least a tetrahedron)";
  for (uint32_t i : tris)
    if (i >= verts.size()) return "the cage's triangles name vertices it doesn't have";
  for (const Vec3 &v : verts)
    if (!finite_bits(v.x) || !finite_bits(v.y) || !finite_bits(v.z)) return "the cage has a vertex at no finite place (a zero scale?)";
  std::unordered_map<uint64_t, std::pair<int, int>> edges;  // each undirected edge: triangles going a->b, b->a (a < b)
  for (size_t f = 0; f + 2 < tris.size(); f += 3)
    for (int i = 0; i < 3; i++) {
      const uint32_t a = tris[f + i], b = tris[f + (i + 1) % 3];
      auto &e = edges[Mesh::edge_key(a, b)];
      (a < b ? e.first : e.second)++;
    }
  for (auto &[k, n] : edges) {
    if (n.first + n.second == 1) return "the cage is open (it has a boundary edge): it must be closed";
    if (n.first != 1 || n.second != 1) return "the cage has edges shared by more than two faces, or faces facing opposite ways";
  }
  /* The volume, in double about the used vertices' centre (precise far from the origin too). */
  AABB b;
  for (uint32_t i : tris) b.add(verts[i]);
  const Vec3 c = b.center();
  double vol = 0.0;
  for (size_t f = 0; f + 2 < tris.size(); f += 3) {
    double p[3][3];
    for (int k = 0; k < 3; k++) {
      const Vec3 v = verts[tris[f + k]] - c;
      p[k][0] = v.x, p[k][1] = v.y, p[k][2] = v.z;
    }
    vol += (p[0][0] * (p[1][1] * p[2][2] - p[1][2] * p[2][1]) - p[0][1] * (p[1][0] * p[2][2] - p[1][2] * p[2][0]) +
            p[0][2] * (p[1][0] * p[2][1] - p[1][1] * p[2][0])) / 6.0;
  }
  const double diag = length(b.max - b.min);
  if (!(std::fabs(vol) > 1e-6 * diag * diag * diag)) return "the cage has no volume";
  return "";
}

bool MeshDeformModifier::bind(std::string *error) {
  auto fail = [&](const std::string &e) {
    if (error) *error = e;
    return false;
  };
  const Mesh *cm = nullptr;
  const GameObject *t = cage_target(*this, &cm);
  if (!t) return fail(object.empty() ? "no Cage object named" : "no mesh object named '" + object + "' (the cage)");
  const Mat4 rel = owner->world_matrix().inverse() * t->world_matrix();
  std::vector<Vec3> verts(cm->positions.size());
  for (size_t i = 0; i < verts.size(); i++) verts[i] = rel.point(cm->positions[i]);
  std::vector<uint32_t> tris, local;
  for (size_t f = 0; f < cm->face_count(); f++) {
    if (cm->face_size(f) < 3) continue;
    meshops::triangulate_face_local(*cm, f, local);
    const uint32_t b = cm->face_offsets[f];
    for (uint32_t k : local) tris.push_back(cm->corner_verts[b + k]);
  }
  const std::string why = check_cage(verts, tris);
  if (!why.empty()) return fail(why);
  bind_verts = std::move(verts);
  bind_tris = std::move(tris);
  weights_.reset();
  last_error.clear();
  return true;
}

void MeshDeformModifier::unbind() {
  bind_verts.clear();
  bind_tris.clear();
  weights_.reset();
  last_error.clear();
}

/* The cage at Bind as one text field: "<vertices> <triangles> x y z ... a b c ...", floats exact (%.9g). Shown only to
 * files and undo (the Inspector shows Bind / Unbind instead); the change hash sees a 64-bit key of it. */
void MeshDeformModifier::reflect(Reflector &r) {
  r.text("Object", object);
  r.help("Name of the cage: a closed mesh round this one (Blender: Mesh Deform > Object). Move its vertices in Edit Mode\n"
         "after Bind and this mesh follows. Put Mesh Deform first among the deforming modifiers.");
  reflect_group_limit(r, vertex_group, invert_group);
  if (!r.all_fields()) return;
  if (r.hashing()) {  // every frame, for every evaluation: the key, not ~40 bytes per cage vertex
    std::string k = strprintf("%016llx", (unsigned long long)bind_key());
    r.text("Bind", k);
    return;
  }
  const uint64_t key = bind_key();
  if (key != bind_text_key_) {
    std::string s = strprintf("%zu %zu", bind_verts.size(), bind_tris.size() / 3);
    for (const Vec3 &v : bind_verts) s += strprintf(" %.9g %.9g %.9g", v.x, v.y, v.z);
    for (uint32_t i : bind_tris) s += strprintf(" %u", i);
    bind_text_ = std::move(s);
    bind_text_key_ = key;
  }
  std::string text = bind_text_;
  r.text("Bind", text);
  if (text == bind_text_) return;
  /* A reader brought another binding: parse and check it like Bind does (anything malformed or not a closed cage
   * unbinds). */
  std::istringstream in(text);
  size_t nv = 0, nt = 0;
  std::vector<Vec3> verts;
  std::vector<uint32_t> tris;
  bool ok = (bool)(in >> nv >> nt) && nv <= 1000000 && nt <= 2000000;
  if (ok) {
    verts.resize(nv);
    for (Vec3 &v : verts) ok = ok && (bool)(in >> v.x >> v.y >> v.z);
    tris.resize(nt * 3);
    for (uint32_t &i : tris) ok = ok && (bool)(in >> i) && i < nv;
  }
  if (ok && nv && !check_cage(verts, tris).empty()) ok = false;
  if (!ok) verts.clear(), tris.clear();
  bind_verts = std::move(verts);
  bind_tris = std::move(tris);
  weights_.reset();
}

/* The cage's evaluated mesh as the Boolean hashes its cutter (the object, its version and deform version) and where it
 * sits: nothing here evaluates another object's keys, so cages naming each other can't recurse. */
uint64_t MeshDeformModifier::modifier_dependency_hash() const {
  const Mesh *cm = nullptr;
  const GameObject *t = cage_target(*this, &cm);
  if (!t) return 0;
  const Mat4 rel = owner->world_matrix().inverse() * t->world_matrix();
  uint64_t h = hash_bytes(1469598103934665603ull, rel.m, sizeof(rel.m));
  const uint64_t k[3] = {(uint64_t)(uintptr_t)cm, cm->version, cm->deform_version};
  return hash_bytes(h, k, sizeof(k));
}

void MeshDeformModifier::modify(Mesh &m) const {
  if (bind_verts.empty()) return;
  const Mesh *cm = nullptr;
  const GameObject *t = cage_target(*this, &cm);
  auto note = [&](const std::string &e) {
    if (e != last_error) Log::warn("Mesh Deform on '%s': %s", owner ? owner->name.c_str() : "?", e.c_str());
    last_error = e;
  };
  if (!t) return note("the cage '" + object + "' is gone");
  const size_t nc = bind_verts.size(), nv = m.positions.size();
  if (cm->positions.size() != nc) return note("the cage's vertices changed since Bind: Bind again");
  if ((double)nc * (double)nv > 64.0 * 1024 * 1024) return note("too many weights (mesh vertices x cage vertices over 64 million)");
  /* The weights for these input positions and this binding. Rows whose input vertex didn't move are kept, so editing
   * the bound mesh (a drag in Edit Mode) recomputes only the vertices it moves. A cache shared with a scene snapshot is
   * copied before it is changed. */
  const uint64_t bkey = bind_key();
  std::shared_ptr<MeshDeformWeights> wt = weights_;
  const bool reuse = wt && wt->key == bkey && wt->verts == nv && wt->cage == nc && wt->input.size() == nv;
  if (!reuse) {
    wt = std::make_shared<MeshDeformWeights>();
    wt->key = bkey, wt->verts = nv, wt->cage = nc;
    wt->w.assign(nv * nc, 0.0f);
    wt->input.assign(nv, Vec3(std::numeric_limits<float>::quiet_NaN()));  // nothing computed yet
  }
  std::vector<uint32_t> stale;
  for (size_t i = 0; i < nv; i++)
    if (std::memcmp(&wt->input[i], &m.positions[i], sizeof(Vec3)) != 0) stale.push_back((uint32_t)i);
  if (!stale.empty()) {
    if (reuse && wt.use_count() > 2) wt = std::make_shared<MeshDeformWeights>(*wt);  // others hold it: change a copy
    JobSystem::global().parallel_for((int64_t)stale.size(), 64, [&](int64_t b, int64_t e) {
      for (int64_t k = b; k < e; k++) {
        const size_t i = stale[(size_t)k];
        const Vec3 x = m.positions[i];
        float *row = wt->w.data() + i * nc;
        if (finite_bits(x.x) && finite_bits(x.y) && finite_bits(x.z)) mean_value_coordinates(x, bind_verts, bind_tris, row);
        else std::fill(row, row + nc, 0.0f);
        wt->input[i] = x;
      }
    });
    weights_ = wt;
  }
  last_error.clear();
  const Mat4 rel = owner->world_matrix().inverse() * t->world_matrix();
  std::vector<Vec3> cage(nc);
  for (size_t j = 0; j < nc; j++) cage[j] = rel.point(cm->positions[j]);
  JobSystem::global().parallel_for((int64_t)nv, 256, [&](int64_t b, int64_t e) {
    for (int64_t i = b; i < e; i++) {
      const Vec3 x = m.positions[(size_t)i];
      if (!finite_bits(x.x) || !finite_bits(x.y) || !finite_bits(x.z)) continue;
      const float *row = wt->w.data() + (size_t)i * nc;
      double px = 0, py = 0, pz = 0;
      for (size_t j = 0; j < nc; j++) px += (double)row[j] * cage[j].x, py += (double)row[j] * cage[j].y, pz += (double)row[j] * cage[j].z;
      const Vec3 p((float)px, (float)py, (float)pz);
      if (finite_bits(p.x) && finite_bits(p.y) && finite_bits(p.z)) m.positions[(size_t)i] = p;
    }
  });
  m.touch();
}

}  // namespace bl
