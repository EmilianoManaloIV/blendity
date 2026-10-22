// SPDX-License-Identifier: GPL-2.0-or-later
// Curve objects: sampling a curve into a framed path, and the Curve modifier that bends a mesh along it
// (Blender: blenkernel/intern/curve.cc for the evaluation, modifiers/intern/MOD_curve.cc for the deform).
#include "curve.h"
#include "scene.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace bl {

namespace {

bool finite3(Vec3 v) { return finite_bits(v.x) && finite_bits(v.y) && finite_bits(v.z); }

Vec3 lerp3(Vec3 a, Vec3 b, float t) { return a + (b - a) * t; }

Vec3 bezier(Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3, float t) {
  const float u = 1.0f - t;
  return p0 * (u * u * u) + p1 * (3.0f * u * u * t) + p2 * (3.0f * u * t * t) + p3 * (t * t * t);
}

Vec3 catmull_rom(Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3, float t) {
  const float t2 = t * t, t3 = t2 * t;
  return (p1 * 2.0f + (p2 - p0) * t + (p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3) * t2 + (p1 * 3.0f - p0 - p2 * 3.0f + p3) * t3) * 0.5f;
}

/* A unit vector across t, leaning to `up` (or the next axis when up runs along t). */
Vec3 across(Vec3 t, Vec3 up) {
  Vec3 n = up - t * dot(t, up);
  if (length_sq(n) < 1e-8f) {
    const Vec3 alt = std::fabs(t.z) < 0.9f ? Vec3(0, 0, 1) : Vec3(1, 0, 0);
    n = alt - t * dot(t, alt);
  }
  return normalize(n);
}

/* The rotation taking unit a to unit b applied to v (shortest arc). */
Vec3 transport(Vec3 v, Vec3 a, Vec3 b) {
  const Vec3 axis = cross(a, b);
  const float s = length(axis), c = dot(a, b);
  if (s < 1e-7f) return v;  // parallel (or exactly reversed: no unique turn, keep it)
  return Quat::axis_angle(axis / s, std::atan2(s, c)).rotate(v);
}

}  // namespace

CurvePath sample_curve(const Curve &c, const Mat4 &xf, Vec3 up) {
  CurvePath out;
  /* The usable points, through xf; consecutive duplicates dropped (no zero-length segments). */
  std::vector<Vec3> P;
  std::vector<float> tilt, rad;
  for (const CurvePoint &cp : c.points) {
    const Vec3 p = xf.point(cp.position);
    if (!finite3(p)) continue;
    if (!P.empty() && length_sq(p - P.back()) < 1e-12f) continue;
    P.push_back(p);
    tilt.push_back(finite_bits(cp.tilt) ? cp.tilt * kDeg2Rad : 0.0f);
    rad.push_back(finite_bits(cp.radius) ? std::max(0.0f, std::min(cp.radius, 1e6f)) : 1.0f);
  }
  bool closed = c.closed;
  if (closed && P.size() >= 2 && length_sq(P.front() - P.back()) < 1e-12f) P.pop_back(), tilt.pop_back(), rad.pop_back();
  const size_t n = P.size();
  if (n < 2) return out;
  if (n < 3) closed = false;  // a closed curve needs a loop
  out.closed = closed;
  const int res = c.type == 2 ? 1 : std::max(1, std::min(c.resolution, 256));
  const size_t segs = closed ? n : n - 1;
  auto at = [&](long i) -> Vec3 {  // a point by index, wrapped (closed) or extended straight (open)
    if (closed) return P[(size_t)((i % (long)n + (long)n) % (long)n)];
    if (i < 0) return P[0] * 2.0f - P[1];
    if (i >= (long)n) return P[n - 1] * 2.0f - P[n - 2];
    return P[(size_t)i];
  };
  /* Blender's Auto handles: along the line through the neighbours, a third of each side's length. */
  auto handle = [&](long i, bool right) -> Vec3 {
    const Vec3 p = at(i);
    const bool end_lo = !closed && i == 0, end_hi = !closed && i == (long)n - 1;
    Vec3 dir = end_lo ? at(1) - p : end_hi ? p - at(i - 1) : at(i + 1) - at(i - 1);
    const float l = length(dir);
    if (l < 1e-12f) return p;
    dir = dir / l;
    const float side = right ? length(at(i + 1) - p) : length(p - at(i - 1));
    return p + dir * (right ? side : -side) * (1.0f / 3.0f);
  };
  for (size_t s = 0; s < segs; s++) {
    const long i = (long)s;
    const Vec3 a = at(i), b = at(i + 1);
    const size_t j = (s + 1) % n;
    const Vec3 h0 = handle(i, true), h1 = handle(i + 1, false);
    for (int k = 0; k < res; k++) {
      const float t = k / (float)res;
      Vec3 p = c.type == 0 ? bezier(a, h0, h1, b, t) : c.type == 1 ? catmull_rom(at(i - 1), a, b, at(i + 2), t) : lerp3(a, b, t);
      if (!out.pos.empty() && length_sq(p - out.pos.back()) < 1e-14f) continue;
      out.pos.push_back(p);
      out.radius.push_back(rad[s] + (rad[j] - rad[s]) * t);
      out.normal.push_back(Vec3(tilt[s] + (tilt[j] - tilt[s]) * t, 0, 0));  // the tilt, until the frames are made
    }
  }
  if (!closed) {
    out.pos.push_back(P[n - 1]);
    out.radius.push_back(rad[n - 1]);
    out.normal.push_back(Vec3(tilt[n - 1], 0, 0));
  }
  const size_t m = out.pos.size();
  if (m < 2) return CurvePath{};
  /* Tangents by central differences; arc length. */
  out.tangent.resize(m);
  out.length.assign(m, 0.0f);
  for (size_t i = 0; i < m; i++) {
    const Vec3 prev = i > 0 ? out.pos[i - 1] : closed ? out.pos[m - 1] : out.pos[i];
    const Vec3 next = i + 1 < m ? out.pos[i + 1] : closed ? out.pos[0] : out.pos[i];
    Vec3 t = next - prev;
    out.tangent[i] = length_sq(t) > 1e-20f ? normalize(t) : (i ? out.tangent[i - 1] : Vec3(1, 0, 0));
    if (i) out.length[i] = out.length[i - 1] + length(out.pos[i] - out.pos[i - 1]);
  }
  if (closed) out.length.push_back(out.length.back() + length(out.pos[0] - out.pos[m - 1]));  // the closing span
  /* Normals by parallel transport (rotation minimising), the tilt turned in after. */
  std::vector<float> tilts(m);
  for (size_t i = 0; i < m; i++) tilts[i] = out.normal[i].x;
  Vec3 nrm = across(out.tangent[0], up);
  out.normal[0] = nrm;
  for (size_t i = 1; i < m; i++) {
    nrm = across(out.tangent[i], transport(nrm, out.tangent[i - 1], out.tangent[i]));
    out.normal[i] = nrm;
  }
  if (closed) {
    /* Carried once round, the frame comes back turned: spread that turn along the loop so it meets itself. */
    const Vec3 back = across(out.tangent[0], transport(out.normal[m - 1], out.tangent[m - 1], out.tangent[0]));
    const float twist = std::atan2(dot(cross(back, out.normal[0]), out.tangent[0]), dot(back, out.normal[0]));
    const float total = std::max(1e-12f, out.length[m]);
    for (size_t i = 1; i < m; i++) out.normal[i] = Quat::axis_angle(out.tangent[i], twist * out.length[i] / total).rotate(out.normal[i]);
  }
  for (size_t i = 0; i < m; i++)
    if (tilts[i] != 0.0f) out.normal[i] = Quat::axis_angle(out.tangent[i], tilts[i]).rotate(out.normal[i]);
  if (!(out.total() > 0.0f) || !finite_bits(out.total())) return CurvePath{};
  return out;
}

void CurvePath::at(float s, Vec3 &p, Vec3 &t, Vec3 &n, float &r) const {
  const size_t m = pos.size();
  if (m < 2 || !(total() > 0.0f)) {
    p = m ? pos[0] : Vec3(0.0f), t = Vec3(1, 0, 0), n = Vec3(0, 1, 0), r = 1.0f;
    return;
  }
  if (!finite_bits(s)) s = 0.0f;
  const float L = total();
  if (closed) {
    s = std::fmod(s, L);
    if (s < 0.0f) s += L;
  }
  else if (s <= 0.0f || s >= L) {  // straight on past the ends
    const size_t e = s <= 0.0f ? 0 : m - 1;
    t = tangent[e], n = normal[e], r = radius[e];
    p = pos[e] + t * (s <= 0.0f ? s : s - L);
    return;
  }
  /* The span holding s (length has m entries open, m + 1 closed: the last span closes the loop). */
  const size_t spans = closed ? m : m - 1;
  size_t lo = std::upper_bound(length.begin(), length.begin() + spans + 1, s) - length.begin();
  lo = lo == 0 ? 0 : std::min(lo - 1, spans - 1);
  const size_t hi = (lo + 1) % m;
  const float span = length[lo + 1] - length[lo];
  const float f = span > 0.0f ? std::max(0.0f, std::min(1.0f, (s - length[lo]) / span)) : 0.0f;
  p = lerp3(pos[lo], pos[hi], f);
  t = normalize(lerp3(tangent[lo], tangent[hi], f));
  if (!(length_sq(t) > 0.5f)) t = tangent[lo];
  n = across(t, lerp3(normal[lo], normal[hi], f));
  r = radius[lo] + (radius[hi] - radius[lo]) * f;
}

uint64_t Curve::content_key() const {
  uint64_t h = kFnvBasis;
  auto mix = [&](const void *p, size_t n) { h = mix_bytes(h, p, n); };  // 8 bytes a step
  const int head[3] = {type, closed ? 1 : 0, resolution};
  mix(head, sizeof(head));
  const uint64_t n = points.size();
  mix(&n, sizeof(n));
  if (!points.empty()) mix(points.data(), points.size() * sizeof(CurvePoint));
  return h ? h : 1;
}

const CurvePath &Curve::local_path() const {
  const uint64_t key = content_key();
  if (!path_ || path_key_ != key) {
    path_ = std::make_shared<const CurvePath>(sample_curve(*this, Mat4::identity()));
    path_key_ = key;
  }
  return *path_;
}

/* ------------------------------------------------------------ Curve modifier */

/* The named Curve object (never the owner itself). */
static const GameObject *curve_target(const Component &c, const std::string &name, const Curve **curve) {
  *curve = nullptr;
  if (!c.owner || !c.owner->scene || name.empty()) return nullptr;
  const GameObject *t = c.owner->scene->find_by_name(name);
  if (!t || t == c.owner) return nullptr;
  *curve = t->get<Curve>();
  return *curve ? t : nullptr;
}

/* What a modifier reading a curve depends on: its points and settings, and where it sits from the owner. A length
 * alone (Array's Fit Curve) only depends on the relative matrix's scale and shear (M^T M of its 3x3): moving or
 * turning the curve can't change the count, and a drag of it then stays on the deform-only path (ADR 0022). */
static uint64_t curve_dependency(const Component &c, const std::string &name, bool length_only) {
  const Curve *cv = nullptr;
  const GameObject *t = curve_target(c, name, &cv);
  if (!t) return 0;
  uint64_t h = kFnvBasis;
  auto mix = [&](const void *p, size_t n) { h = fnv1a(p, n, h); };
  const Mat4 rel = c.owner->world_matrix().inverse() * t->world_matrix();
  if (length_only) {
    float g[9];
    for (int i = 0; i < 3; i++)
      for (int j = 0; j < 3; j++) g[i * 3 + j] = dot(rel.column(i), rel.column(j));
    mix(g, sizeof(g));
  }
  else
    mix(rel.m, sizeof(rel.m));
  const uint64_t ch = cv->content_key();
  mix(&ch, sizeof(ch));
  return h;
}

uint64_t CurveModifier::modifier_dependency_hash() const { return curve_dependency(*this, object, false); }

void CurveModifier::modify(Mesh &m) const {
  const Curve *cv = nullptr;
  const GameObject *t = curve_target(*this, object, &cv);
  if (!t) return;
  const int k = std::max(0, std::min(deform_axis, 5)), ax = k % 3;
  const float sign = k >= 3 ? -1.0f : 1.0f;
  /* The mesh's coordinate along `up_axis` goes along the curve's normal, `side_axis` along tangent x normal, so a
   * straight curve along the deform axis leaves the mesh as it is. */
  const int up_axis = ax == 1 ? 2 : 1, side_axis = 3 - ax - up_axis;
  const float side_sign = (ax == 2 ? -1.0f : 1.0f) * sign;
  Vec3 up(0.0f);
  up[up_axis] = 1.0f;
  const CurvePath path = sample_curve(*cv, owner->world_matrix().inverse() * t->world_matrix(), up);
  if (!path.valid()) return;
  for (Vec3 &v : m.positions) {
    if (!finite_bits(v.x) || !finite_bits(v.y) || !finite_bits(v.z)) continue;
    Vec3 p, tg, n;
    float r;
    path.at(sign * v[ax], p, tg, n, r);
    const Vec3 b = cross(tg, n);
    v = p + (n * v[up_axis] + b * (side_sign * v[side_axis])) * r;
  }
  m.touch();
}

/* --------------------------------------------------------- Array: Fit Curve */

float array_curve_length(const ArrayModifier &a) {
  const Curve *cv = nullptr;
  const GameObject *t = curve_target(a, a.curve, &cv);
  if (!t) return 0.0f;
  return sample_curve(*cv, a.owner->world_matrix().inverse() * t->world_matrix()).total();
}

uint64_t ArrayModifier::modifier_dependency_hash() const { return fit_type == 2 && mode == 0 ? curve_dependency(*this, curve, true) : 0; }

}  // namespace bl
