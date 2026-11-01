// SPDX-License-Identifier: GPL-2.0-or-later
// Stroke objects (ADR 0084, task 0248): the mesh from the stroke list, the list as text.
#include "stroke_object.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <sstream>

namespace bl {

namespace {

constexpr float kTwoPi = 6.2831853f;

Vec3 safe_normalize(Vec3 v, Vec3 fallback) {
  const float l = length(v);
  return l > 1e-8f ? v / l : fallback;
}

/* A right vector for a quad facing `n` with `up` its vertical (made orthogonal to n). */
void card_frame(Vec3 n, Vec3 up_in, Vec3 &right, Vec3 &up) {
  up = up_in - n * dot(up_in, n);
  if (length(up) < 1e-6f) up = std::fabs(n.y) < 0.9f ? Vec3(0.0f, 1.0f, 0.0f) : Vec3(0.0f, 0.0f, 1.0f), up = up - n * dot(up, n);
  up = safe_normalize(up, Vec3(0.0f, 1.0f, 0.0f));
  right = safe_normalize(cross(up, n), Vec3(1.0f, 0.0f, 0.0f));
}

/* The path's direction at point i (its neighbours'). */
Vec3 tangent_at(const std::vector<Vec3> &p, size_t i) {
  const size_t n = p.size();
  return safe_normalize((i + 1 < n ? p[i + 1] : p[i]) - (i > 0 ? p[i - 1] : p[i]), Vec3(1.0f, 0.0f, 0.0f));
}

/* How far along the path each point is, and the whole length (never 0). */
std::vector<float> lengths_along(const std::vector<Vec3> &p, float &total) {
  std::vector<float> along(p.size(), 0.0f);
  for (size_t i = 1; i < p.size(); i++) along[i] = along[i - 1] + length(p[i] - p[i - 1]);
  total = std::max(1e-6f, along.empty() ? 0.0f : along.back());
  return along;
}

/* v made square to t, or none (zero) when it lies along t. */
Vec3 square_to(Vec3 v, Vec3 t) {
  const Vec3 s = v - t * dot(v, t);
  return length(s) > 0.3f * std::max(1e-6f, length(v)) ? normalize(s) : Vec3();
}

/* v turned by the smallest turn that takes t0 to t1 (Rodrigues): parallel transport, even round a sharp corner
 * (re-check 385: projecting had rolled tubes 30-57 degrees at a V). */
Vec3 transport(Vec3 v, Vec3 t0, Vec3 t1) {
  const Vec3 axis = cross(t0, t1);
  const float s = length(axis), c = dot(t0, t1);
  if (s < 1e-6f) return v;
  const Vec3 k = axis / s;
  return v * c + cross(k, v) * s + k * (dot(k, v) * (1.0f - c));
}

/* Any direction square to t. */
Vec3 any_square(Vec3 t) { return normalize(std::fabs(t.y) < 0.9f ? Vec3(0.0f, 1.0f, 0.0f) - t * t.y : Vec3(1.0f, 0.0f, 0.0f) - t * t.x); }

/* The normal a point along the path, square to it (review 385: each point's own up had flipped and twisted the
 * stroke). A ribbon follows the hand's up made square to the path, turned over when it would flip the strip, the last
 * normal carried where the up lies along the path; a tube carries its first normal along (parallel transport) - its
 * roll is the hand's at the start, never a twist. */
std::vector<Vec3> path_normals(const StrokeRibbon &rb) {
  const size_t n = rb.points.size();
  std::vector<Vec3> out(n);
  if (rb.ups.empty() && !rb.tube) return std::vector<Vec3>(n, safe_normalize(rb.facing, Vec3(0.0f, 0.0f, 1.0f)));  // (a desk ribbon: its facing, as before round 385)
  Vec3 prev = square_to(rb.up_at(0), tangent_at(rb.points, 0)), t_prev = tangent_at(rb.points, 0);
  if (length(prev) < 0.5f) prev = any_square(t_prev);
  for (size_t i = 0; i < n; i++) {
    const Vec3 t = tangent_at(rb.points, i), carried = square_to(transport(prev, t_prev, t), t);
    t_prev = t;
    Vec3 nrm = rb.tube ? carried : square_to(rb.up_at(i), t);
    if (length(nrm) < 0.5f) nrm = length(carried) > 0.5f ? carried : any_square(t);
    if (dot(nrm, prev) < 0.0f) nrm = nrm * -1.0f;
    out[i] = prev = nrm;
  }
  return out;
}

using ShadeFn = std::function<Vec3(Vec3 own, Vec3 surface, Vec3 at)>;

/* A ribbon as a strip: two vertices a point, across the path square to its normal there, its width there; each face
 * twice (the back its own face). */
void add_strip(Mesh &m, std::vector<Vec3> &custom, const StrokeRibbon &rb, const ShadeFn &shade) {
  const size_t n = rb.points.size();
  float total = 0.0f;
  const std::vector<float> along = lengths_along(rb.points, total);
  const std::vector<Vec3> normals = path_normals(rb);
  std::vector<uint32_t> left(n), right(n);
  for (size_t i = 0; i < n; i++) {
    const Vec3 side = safe_normalize(cross(normals[i], tangent_at(rb.points, i)), Vec3(0.0f, 1.0f, 0.0f)) * (std::max(1e-4f, rb.width_at(i)) * 0.5f);
    left[i] = m.add_vert(rb.points[i] - side), right[i] = m.add_vert(rb.points[i] + side);
    custom.push_back(shade(normals[i], rb.surface_normal, rb.points[i]));
    custom.push_back(shade(normals[i], rb.surface_normal, rb.points[i]));
  }
  for (size_t i = 1; i < n; i++) {
    const uint32_t vs[4] = {left[i - 1], right[i - 1], right[i], left[i]}, back[4] = {left[i], right[i], right[i - 1], left[i - 1]};
    const float u0 = along[i - 1] / total, u1 = along[i] / total;
    const Vec2 uv[4] = {{u0, 0.0f}, {u0, 1.0f}, {u1, 1.0f}, {u1, 0.0f}}, uvb[4] = {{u1, 0.0f}, {u1, 1.0f}, {u0, 1.0f}, {u0, 0.0f}};
    m.add_face(vs, 4, uv, rb.material);
    m.add_face(back, 4, uvb, rb.material);
  }
}

/* A tube's point k round its ring at point i: the direction out from the path (the ring turns from the normal a
 * toward b = t x a). */
Vec3 ring_out(Vec3 a, Vec3 t, int k) {
  const float ang = k * kTwoPi / kTubeSides;
  return a * std::cos(ang) + cross(t, a) * std::sin(ang);
}

/* A cap: the end ring again (its own vertices: their normal is along the path) round a centre, wound to face `out`. */
void add_cap(Mesh &m, std::vector<Vec3> &custom, const StrokeRibbon &rb, size_t i, Vec3 a, Vec3 out) {
  const Vec3 t = tangent_at(rb.points, i);
  const float r = std::max(1e-4f, rb.width_at(i)) * 0.5f;
  const uint32_t c = m.add_vert(rb.points[i]);
  custom.push_back(out);
  uint32_t ring[kTubeSides];
  for (int k = 0; k < kTubeSides; k++) ring[k] = m.add_vert(rb.points[i] + ring_out(a, t, k) * r), custom.push_back(out);
  const bool start = dot(out, t) < 0.0f;
  for (int k = 0; k < kTubeSides; k++) {
    const uint32_t k1 = ring[(k + 1) % kTubeSides];
    const uint32_t tri[3] = {c, start ? k1 : ring[k], start ? ring[k] : k1};
    m.add_face(tri, 3, nullptr, rb.material);
  }
}

/* A ribbon as a tube: a ring of kTubeSides a point, the width its diameter, its normals out from the path, its faces
 * wound outward (review 385: they had faced in); capped. */
void add_tube(Mesh &m, std::vector<Vec3> &custom, const StrokeRibbon &rb) {
  const size_t n = rb.points.size();
  float total = 0.0f;
  const std::vector<float> along = lengths_along(rb.points, total);
  const std::vector<Vec3> normals = path_normals(rb);
  std::vector<uint32_t> ring(n * (kTubeSides + 1));  // (a seam vertex a ring: v wraps 0..1)
  for (size_t i = 0; i < n; i++) {
    const float r = std::max(1e-4f, rb.width_at(i)) * 0.5f;
    for (int k = 0; k <= kTubeSides; k++) {
      const Vec3 out = ring_out(normals[i], tangent_at(rb.points, i), k);
      ring[i * (kTubeSides + 1) + k] = m.add_vert(rb.points[i] + out * r);
      custom.push_back(out);
    }
  }
  for (size_t i = 1; i < n; i++)
    for (int k = 0; k < kTubeSides; k++) {
      const uint32_t *p = &ring[(i - 1) * (kTubeSides + 1)], *q = &ring[i * (kTubeSides + 1)];
      const uint32_t vs[4] = {p[k], p[k + 1], q[k + 1], q[k]};
      const float u0 = along[i - 1] / total, u1 = along[i] / total, v0 = (float)k / kTubeSides, v1 = (float)(k + 1) / kTubeSides;
      const Vec2 uv[4] = {{u0, v0}, {u0, v1}, {u1, v1}, {u1, v0}};
      m.add_face(vs, 4, uv, rb.material);
    }
  add_cap(m, custom, rb, 0, normals[0], tangent_at(rb.points, 0) * -1.0f);
  add_cap(m, custom, rb, n - 1, normals[n - 1], tangent_at(rb.points, n - 1));
}

}  // namespace

void Strokes::reflect(Reflector &r) {
  static const char *modes[] = {"Own", "Surface", "Canopy"};
  r.enumeration("Normals", normals, modes, 3);
  r.help("How the cards shade: Own - each card's facing (a flat sprite); Surface - the normal of the surface it was\n"
         "painted on (leaves that take their branch's light); Canopy - away from the Canopy centre (a bush of flat cards\n"
         "shades as one round volume: the hand-painted foliage trick).");
  r.field("Canopy", canopy);
  if (r.serializing()) {
    std::string text = strokes_to_text(*this);
    r.text("Strokes", text);
    if (text != strokes_to_text(*this)) strokes_from_text(text, *this);  // (loading)
  }
}

/* Round 385: a ribbon's optional groups after its points - `W` a width a point, `U` an up a point, `T` a tube, `M` its
 * material slot. */
static std::string ribbon_extras_to_text(const StrokeRibbon &rb) {
  std::string out;
  char buf[96];
  if (!rb.widths.empty()) out += " W";
  for (float w : rb.widths) std::snprintf(buf, sizeof buf, " %g", w), out += buf;
  if (!rb.ups.empty()) out += " U";
  for (const Vec3 &u : rb.ups) std::snprintf(buf, sizeof buf, " %g %g %g", u.x, u.y, u.z), out += buf;
  if (rb.tube) out += " T";
  if (rb.material) std::snprintf(buf, sizeof buf, " M %d", rb.material), out += buf;
  return out;
}

/* A group's values, or the group dropped when the line runs out or breaks inside it (review 385: a cut-short group had
 * kept zeros). */
template <class T, class Read>
void read_group(std::istringstream &ls, std::vector<T> &out, size_t n, Read read) {
  out.resize(n);
  for (size_t i = 0; i < n && !ls.fail(); i++) read(out[i]);
  if (ls.fail()) out.clear();
}

static void ribbon_extras_from_text(std::istringstream &ls, StrokeRibbon &rb) {
  const size_t n = rb.points.size();
  std::string tag;
  while (ls >> tag) {
    if (tag == "W") read_group(ls, rb.widths, n, [&](float &w) { ls >> w; });
    if (tag == "U") read_group(ls, rb.ups, n, [&](Vec3 &u) { ls >> u.x >> u.y >> u.z; });
    if (tag == "T") rb.tube = true;
    if (tag == "M") ls >> rb.material;
    if (ls.fail()) break;
  }
  rb.material = std::clamp(rb.material, 0, kMaxStrokeSlot);  // (review 385: a slot from a file is bounded)
}

std::string strokes_to_text(const Strokes &s) {
  std::string out;
  char buf[256];
  for (const StrokeCard &c : s.cards) {
    std::snprintf(buf, sizeof buf, "C %g %g %g %g %g %g %g %g %g %g %g %g %g\n", c.center.x, c.center.y, c.center.z, c.facing.x, c.facing.y, c.facing.z, c.up.x, c.up.y, c.up.z, c.surface_normal.x, c.surface_normal.y,
                  c.surface_normal.z, c.size);
    out += buf;
  }
  for (const StrokeRibbon &rb : s.ribbons) {
    std::snprintf(buf, sizeof buf, "R %g %g %g %g %g %g %g %zu", rb.facing.x, rb.facing.y, rb.facing.z, rb.surface_normal.x, rb.surface_normal.y, rb.surface_normal.z, rb.width, rb.points.size());
    out += buf;
    for (const Vec3 &p : rb.points) {
      std::snprintf(buf, sizeof buf, " %g %g %g", p.x, p.y, p.z);
      out += buf;
    }
    out += ribbon_extras_to_text(rb);
    out += "\n";
  }
  return out;
}

bool strokes_from_text(const std::string &text, Strokes &s) {
  s.cards.clear(), s.ribbons.clear();
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    std::istringstream ls(line);
    char kind = 0;
    ls >> kind;
    if (kind == 'C') {
      StrokeCard c;
      ls >> c.center.x >> c.center.y >> c.center.z >> c.facing.x >> c.facing.y >> c.facing.z >> c.up.x >> c.up.y >> c.up.z >> c.surface_normal.x >> c.surface_normal.y >> c.surface_normal.z >> c.size;
      if (!ls.fail()) s.cards.push_back(c);
    }
    else if (kind == 'R') {
      StrokeRibbon rb;
      size_t n = 0;
      ls >> rb.facing.x >> rb.facing.y >> rb.facing.z >> rb.surface_normal.x >> rb.surface_normal.y >> rb.surface_normal.z >> rb.width >> n;
      if (ls.fail() || n > 100000) continue;
      for (size_t i = 0; i < n && !ls.fail(); i++) {
        Vec3 p;
        ls >> p.x >> p.y >> p.z;
        rb.points.push_back(p);
      }
      ribbon_extras_from_text(ls, rb);
      if (rb.points.size() >= 2) s.ribbons.push_back(std::move(rb));
    }
  }
  return true;
}

Vec3 Strokes::shade(Vec3 own, Vec3 surface, Vec3 at) const {
  if (normals == 1) return safe_normalize(surface, own);
  if (normals == 2) return safe_normalize(at - canopy, own);
  return own;
}

void Strokes::append_ribbon(Mesh &m, std::vector<Vec3> &custom, const StrokeRibbon &rb) const {
  if (rb.points.size() < 2) return;
  if (rb.tube) return add_tube(m, custom, rb);
  add_strip(m, custom, rb, [&](Vec3 own, Vec3 surface, Vec3 at) { return shade(own, surface, at); });
}

MeshPtr Strokes::with_last_ribbon(const Mesh &prev) const {
  if (ribbons.empty() || !prev.custom_normals_fit()) return build_mesh();
  MeshPtr m = mesh_copy_bare(prev);  // (re-check 385: not the old mesh's render caches - its version is new anyway)
  std::vector<Vec3> custom = std::move(m->custom_normals);
  append_ribbon(*m, custom, ribbons.back());
  m->sync_attributes();
  m->custom_normals = std::move(custom);
  m->touch();
  return m;
}

MeshPtr Strokes::build_mesh() const {
  auto m = std::make_shared<Mesh>();
  std::vector<Vec3> custom;
  auto shade_normal = [&](Vec3 own, Vec3 surface, Vec3 at) { return shade(own, surface, at); };
  for (const StrokeCard &c : cards) {
    const Vec3 n = safe_normalize(c.facing, Vec3(0.0f, 0.0f, 1.0f));
    Vec3 right, up;
    card_frame(n, c.up, right, up);
    const float h = std::max(1e-4f, c.size) * 0.5f;
    const uint32_t v0 = m->add_vert(c.center - right * h - up * h), v1 = m->add_vert(c.center + right * h - up * h), v2 = m->add_vert(c.center + right * h + up * h), v3 = m->add_vert(c.center - right * h + up * h);
    const uint32_t vs[4] = {v0, v1, v2, v3}, back[4] = {v3, v2, v1, v0};
    const Vec2 uv[4] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}}, uvb[4] = {{0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f}};
    m->add_face(vs, 4, uv);
    m->add_face(back, 4, uvb);  // (the back as a face of its own: a renderer's back-side flip never turns the shared custom normal)
    for (int k = 0; k < 4; k++) custom.push_back(shade_normal(n, c.surface_normal, m->positions[vs[k]]));
  }
  for (const StrokeRibbon &rb : ribbons) append_ribbon(*m, custom, rb);
  m->sync_attributes();
  m->custom_normals = std::move(custom);
  m->touch();
  return m;
}

}  // namespace bl
