// SPDX-License-Identifier: GPL-2.0-or-later
// Moving around in VR (round 384, task 0361): see xr_nav.h.
#include "xr_nav.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace bl::xr {

namespace {

constexpr float kRad2Deg = 57.29577951308232f;

Quat yaw_turn(float yaw_deg) { return Quat::euler({0.0f, yaw_deg, 0.0f}); }

/* A horizontal direction's angle, degrees, in Quat::euler's sense (+Z is 0, +X is 90). */
float heading(Vec3 v) { return std::atan2(v.x, v.z) * kRad2Deg; }

float level_span(Vec3 v) { return std::hypot(v.x, v.z); }

/* The turn from v0 to v1 about the vertical; none when either is nearly vertical (one hand above the other: its
 * heading is noise - review 384). */
float turn_between(Vec3 v0, Vec3 v1) {
  if (level_span(v0) < kMinTwoHandSpan || level_span(v1) < kMinTwoHandSpan) return 0.0f;
  return heading(v1) - heading(v0);
}

bool can_turn(Vec3 a_xr, const Vec3 *b_xr) { return b_xr && level_span(to_blendity(*b_xr - a_xr)) >= kMinTwoHandSpan; }

Vec3 level(Vec3 v) {
  v.y = 0.0f;
  const float l = length(v);
  return l > 1e-6f ? v / l : Vec3(0, 0, 1);
}

/* The ground plane's crossing between a (above) and b (at or below). */
Vec3 ground_between(Vec3 a, Vec3 b) {
  const float t = a.y / (a.y - b.y);
  return a + (b - a) * t;
}

}  // namespace

/* A stage point in Blendity's axes, lifted. */
static Vec3 local(const Stage &s, Vec3 p_xr) { return to_blendity(p_xr) + Vec3(0.0f, std::isfinite(s.lift) ? s.lift : 0.0f, 0.0f); }

Mat4 Stage::matrix() const {
  const float s = std::isfinite(scale) && scale > 0.0f ? scale : 1.0f;
  const float up = std::isfinite(lift) ? lift * s : 0.0f;
  return Mat4::trs(origin + Vec3(0.0f, up, 0.0f), yaw_turn(yaw), Vec3(s, s, s));
}

Vec3 stage_point(const Stage &s, Vec3 p_xr) { return s.matrix().point(to_blendity(p_xr)); }
Vec3 stage_dir(const Stage &s, Vec3 d_xr) { return yaw_turn(s.yaw).rotate(to_blendity(d_xr)); }

Arc teleport_arc(const Stage &s, const Hand &h, const HitFn &hit, float speed, float gravity, int steps, float dt) {
  Arc arc;
  Vec3 p = stage_point(s, h.aim.position);
  Vec3 v = normalize(stage_dir(s, h.aim.orientation.rotate({0, 0, -1}))) * (speed * s.scale);
  arc.points.push_back(p);
  for (int i = 0; i < steps; i++) {
    const Vec3 next = p + v * dt;
    v.y -= gravity * s.scale * dt;
    Vec3 at, normal(0, 1, 0);
    if (hit && hit(p, next, at, normal)) {
      arc.points.push_back(at);
      arc.landed = normal.y >= kMinFloorNormalY, arc.at = at;
      return arc;
    }
    if (p.y > 0.0f && next.y <= 0.0f) {
      arc.at = ground_between(p, next);
      arc.points.push_back(arc.at);
      arc.landed = true;
      return arc;
    }
    arc.points.push_back(next);
    p = next;
  }
  return arc;
}

Stage teleport(const Stage &s, Vec3 head_xr, Vec3 target) {
  const Vec3 head = stage_point(s, head_xr);
  Stage out = s;
  out.origin = Vec3(s.origin.x + target.x - head.x, target.y, s.origin.z + target.z - head.z);
  return out;
}

Stage smooth_move(const Stage &s, Quat head_xr_turn, Vec2 stick, float speed, float dt) {
  const Vec3 fwd = level(stage_dir(s, head_xr_turn.rotate({0, 0, -1})));
  const Vec3 right(fwd.z, 0.0f, -fwd.x);
  Stage out = s;
  out.origin = s.origin + (fwd * stick.y + right * stick.x) * (speed * s.scale * dt);
  return out;
}

Stage snap_turn(const Stage &s, Vec3 head_xr, float degrees) {
  const Vec3 head = stage_point(s, head_xr);
  Stage out = s;
  out.yaw = s.yaw + degrees;
  out.origin = head - yaw_turn(out.yaw).rotate(local(s, head_xr) * s.scale);
  return out;
}

Grab grab_begin(const Stage &s, Vec3 a_xr, const Vec3 *b_xr) {
  Grab g;
  g.active = true, g.two = b_xr != nullptr, g.start = g.last = s, g.a0 = a_xr;
  g.turning = can_turn(a_xr, b_xr);
  if (b_xr) g.b0 = *b_xr;
  return g;
}

Stage grab_update(Grab &g, Vec3 a_xr, const Vec3 *b_xr) {
  if (g.active && g.two && b_xr && can_turn(a_xr, b_xr) != g.turning) g = grab_begin(g.last, a_xr, b_xr);
  g.last = grab_update(static_cast<const Grab &>(g), a_xr, b_xr);
  return g.last;
}

/* The stage that keeps the grabbed point where it was in the world: with the start S0 = T(o0) R(y0) S(s0) and the hands'
 * centre c0 then and c1 now (Blendity's axes), S1 = S0 T(c0) R(dy) S(k) T(-c1), so
 * o1 = o0 + R(y0) s0 c0 - R(y1) s1 c1 with y1 = y0 + dy and s1 = s0 k. */
Stage grab_update(const Grab &g, Vec3 a_xr, const Vec3 *b_xr) {
  if (!g.active) return g.start;
  const Vec3 v0 = to_blendity(g.b0 - g.a0);
  const bool two = g.two && b_xr && length(v0) >= kMinTwoHandSpan;  // (hands that started together grab as one - review 384)
  const Vec3 c0 = local(g.start, two ? (g.a0 + g.b0) * 0.5f : g.a0), c1 = local(g.start, two ? (a_xr + *b_xr) * 0.5f : a_xr);
  Stage out = g.start;
  if (two) {
    const Vec3 v1 = to_blendity(*b_xr - a_xr);
    const float d0 = length(v0), d1 = std::max(length(v1), kMinTwoHandSpan);
    out.scale = std::clamp(snap_scale(g.start.scale * d0 / d1), kMinScale, kMaxScale);
    out.yaw = g.start.yaw - turn_between(v0, v1);
  }
  out.origin = g.start.origin + yaw_turn(g.start.yaw).rotate(c0 * g.start.scale) - yaw_turn(out.yaw).rotate(c1 * out.scale);
  return out;
}

float snap_scale(float scale, float snap) { return std::fabs(scale - 1.0f) < snap ? 1.0f : scale; }

void SegmentBvh::build(const std::vector<Vec3> &positions, const std::vector<uint32_t> &indices) {
  pos = positions, tris = indices, nodes.clear();
  const uint32_t nt = (uint32_t)(tris.size() / 3);
  order.resize(nt);
  for (uint32_t i = 0; i < nt; i++) order[i] = i;
  if (!nt) return;
  std::vector<Vec3> centre(nt);
  for (uint32_t i = 0; i < nt; i++) centre[i] = (pos[tris[i * 3]] + pos[tris[i * 3 + 1]] + pos[tris[i * 3 + 2]]) * (1.0f / 3.0f);
  nodes.reserve((size_t)nt * 2);
  nodes.emplace_back();
  std::vector<std::array<uint32_t, 3>> todo{{0u, 0u, nt}};  // (node, first, count)
  while (!todo.empty()) {
    const auto [node, first, count] = todo.back();
    todo.pop_back();
    Vec3 lo(1e30f, 1e30f, 1e30f), hi(-1e30f, -1e30f, -1e30f), clo = lo, chi = hi;
    for (uint32_t k = first; k < first + count; k++) {
      for (int c = 0; c < 3; c++) lo = vmin(lo, pos[tris[order[k] * 3 + c]]), hi = vmax(hi, pos[tris[order[k] * 3 + c]]);
      clo = vmin(clo, centre[order[k]]), chi = vmax(chi, centre[order[k]]);
    }
    nodes[node].lo = lo, nodes[node].hi = hi;
    const Vec3 ext = chi - clo;
    const int axis = ext.x >= ext.y && ext.x >= ext.z ? 0 : ext.y >= ext.z ? 1 : 2;
    if (count <= 4 || !(ext[axis] > 0.0f)) {
      nodes[node].first = first, nodes[node].count = count;
      continue;
    }
    const uint32_t mid = first + count / 2;
    std::nth_element(order.begin() + first, order.begin() + mid, order.begin() + first + count, [&](uint32_t a, uint32_t b) { return centre[a][axis] < centre[b][axis]; });
    const uint32_t left = (uint32_t)nodes.size();
    nodes.emplace_back();
    nodes.emplace_back();
    nodes[node].first = left, nodes[node].right = left + 1, nodes[node].count = 0;
    todo.push_back({left, first, mid - first});
    todo.push_back({left + 1, mid, first + count - mid});
  }
}

namespace {

/* Whether a + d t, t in [0, tmax], meets the box (slabs). */
bool segment_box(Vec3 a, Vec3 d, float tmax, Vec3 lo, Vec3 hi) {
  float t0 = 0.0f, t1 = tmax;
  for (int k = 0; k < 3; k++) {
    if (std::fabs(d[k]) < 1e-12f) {
      if (a[k] < lo[k] || a[k] > hi[k]) return false;
      continue;
    }
    float ta = (lo[k] - a[k]) / d[k], tb = (hi[k] - a[k]) / d[k];
    if (ta > tb) std::swap(ta, tb);
    t0 = std::max(t0, ta), t1 = std::min(t1, tb);
    if (t0 > t1) return false;
  }
  return true;
}

/* Moller-Trumbore: the fraction along a + d where it meets the triangle, or -1. */
float segment_triangle(Vec3 a, Vec3 d, Vec3 p0, Vec3 p1, Vec3 p2) {
  const Vec3 e1 = p1 - p0, e2 = p2 - p0, h = cross(d, e2);
  const float det = dot(e1, h);
  if (std::fabs(det) < 1e-12f) return -1.0f;
  const float f = 1.0f / det;
  const Vec3 s = a - p0;
  const float u = f * dot(s, h);
  if (u < 0.0f || u > 1.0f) return -1.0f;
  const Vec3 q = cross(s, e1);
  const float v = f * dot(d, q);
  if (v < 0.0f || u + v > 1.0f) return -1.0f;
  const float t = f * dot(e2, q);
  return t >= 0.0f && t <= 1.0f ? t : -1.0f;
}

}  // namespace

bool SegmentBvh::first_hit(Vec3 a, Vec3 b, float &t, Vec3 &normal) const {
  if (nodes.empty()) return false;
  const Vec3 d = b - a;
  float best = 2.0f;
  uint32_t best_tri = 0;
  std::vector<uint32_t> stack{0};
  while (!stack.empty()) {
    const Node &n = nodes[stack.back()];
    stack.pop_back();
    if (!segment_box(a, d, std::min(best, 1.0f), n.lo, n.hi)) continue;
    if (n.count == 0) {
      stack.push_back(n.first), stack.push_back(n.right);
      continue;
    }
    for (uint32_t k = n.first; k < n.first + n.count; k++) {
      const uint32_t tri = order[k];
      const float f = segment_triangle(a, d, pos[tris[tri * 3]], pos[tris[tri * 3 + 1]], pos[tris[tri * 3 + 2]]);
      if (f >= 0.0f && f < best) best = f, best_tri = tri;
    }
  }
  if (best > 1.0f) return false;
  t = best;
  normal = normalize(cross(pos[tris[best_tri * 3 + 1]] - pos[tris[best_tri * 3]], pos[tris[best_tri * 3 + 2]] - pos[tris[best_tri * 3]]));
  if (dot(normal, d) > 0.0f) normal = normal * -1.0f;
  return true;
}

float measure(const Stage &s, Vec3 a_xr, Vec3 b_xr) { return length(stage_point(s, a_xr) - stage_point(s, b_xr)); }

}  // namespace bl::xr
