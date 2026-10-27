// SPDX-License-Identifier: GPL-2.0-or-later
#include "sketch.h"

#include <algorithm>
#include <cmath>

namespace bl {

SketchLayer &Sketch::layer_at(int i) {
  if (layers.empty()) layers.push_back(SketchLayer{});
  return layers[(size_t)std::max(0, std::min((int)layers.size() - 1, i))];
}

namespace sketch {

static const float kPi = 3.14159265358979f;

std::vector<Vec2> resample(const std::vector<Vec2> &pts, float spacing) {
  std::vector<Vec2> out;
  if (pts.empty()) return out;
  out.push_back(pts[0]);
  float carry = 0.0f;  // (the distance already walked toward the next sample)
  for (size_t i = 1; i < pts.size(); i++) {
    Vec2 a = pts[i - 1];
    const Vec2 b = pts[i];
    float seg = length(b - a);
    while (seg > 0.0f && carry + seg >= spacing) {
      const float t = (spacing - carry) / seg;
      a = a + (b - a) * t;
      out.push_back(a);
      seg = length(b - a);
      carry = 0.0f;
    }
    carry += seg;
  }
  if (length(out.back() - pts.back()) > spacing * 0.25f) out.push_back(pts.back());
  else out.back() = pts.back();
  return out;
}

std::vector<size_t> corners(const std::vector<Vec2> &pts, int k, float turn_deg) {
  std::vector<size_t> out;
  const int n = (int)pts.size();
  if (n < 2 * k + 3) return out;
  std::vector<float> turn((size_t)n, 0.0f);
  for (int i = k; i + k < n; i++) {
    const Vec2 a = pts[(size_t)i] - pts[(size_t)(i - k)], b = pts[(size_t)(i + k)] - pts[(size_t)i];
    const float la = length(a), lb = length(b);
    if (la < 1e-6f || lb < 1e-6f) continue;
    const float c = std::max(-1.0f, std::min(1.0f, dot(a, b) / (la * lb)));
    turn[(size_t)i] = std::acos(c) * 180.0f / kPi;
  }
  for (int i = k; i + k < n; i++) {
    if (turn[(size_t)i] < turn_deg) continue;
    bool peak = true;
    for (int j = std::max(1, i - k); j <= std::min(n - 2, i + k) && peak; j++)
      if (j != i && (turn[(size_t)j] > turn[(size_t)i] || (turn[(size_t)j] == turn[(size_t)i] && j < i))) peak = false;
    if (peak) out.push_back((size_t)i);
  }
  return out;
}

/* The direction of most spread (the covariance's first axis) and the mean. */
static void principal(const std::vector<Vec2> &pts, Vec2 &mean, Vec2 &dir, float &var_along, float &var_across) {
  mean = Vec2{0, 0};
  for (const Vec2 &p : pts) mean += p;
  mean = mean / (float)std::max<size_t>(1, pts.size());
  float sxx = 0, sxy = 0, syy = 0;
  for (const Vec2 &p : pts) {
    const Vec2 d = p - mean;
    sxx += d.x * d.x, sxy += d.x * d.y, syy += d.y * d.y;
  }
  const float n = (float)std::max<size_t>(1, pts.size());
  sxx /= n, sxy /= n, syy /= n;
  const float theta = 0.5f * std::atan2(2.0f * sxy, sxx - syy);
  dir = Vec2{std::cos(theta), std::sin(theta)};
  const float c = dir.x, s = dir.y;
  var_along = c * c * sxx + 2 * c * s * sxy + s * s * syy;
  var_across = s * s * sxx - 2 * c * s * sxy + c * c * syy;
}

bool fit_line(const std::vector<Vec2> &pts, float tol, Fit &out) {
  if (pts.size() < 2) return false;
  Vec2 mean, dir;
  float va, vc;
  principal(pts, mean, dir, va, vc);
  float worst = 0.0f;
  for (const Vec2 &p : pts) {
    const Vec2 d = p - mean;
    worst = std::max(worst, std::fabs(d.x * dir.y - d.y * dir.x));
  }
  if (worst > tol) return false;
  out = Fit{};
  out.kind = StrokeKind::Line;
  out.pts = {mean + dir * dot(pts.front() - mean, dir), mean + dir * dot(pts.back() - mean, dir)};
  out.error = worst;
  return true;
}

/* Kasa's algebraic circle fit: x^2 + y^2 + D x + E y + F = 0 by least squares. */
static bool fit_circle(const std::vector<Vec2> &pts, Vec2 &c, float &r) {
  if (pts.size() < 3) return false;
  double sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0, sxz = 0, syz = 0, sz = 0;
  const Vec2 o = pts[0];  // (about the first point: the sums stay small)
  for (const Vec2 &q : pts) {
    const double x = q.x - o.x, y = q.y - o.y, z = x * x + y * y;
    sx += x, sy += y, sxx += x * x, syy += y * y, sxy += x * y, sxz += x * z, syz += y * z, sz += z;
  }
  const double n = (double)pts.size();
  /* Normal equations for (D, E, F). */
  double a[3][3] = {{sxx, sxy, sx}, {sxy, syy, sy}, {sx, sy, n}};
  double b[3] = {-sxz, -syz, -sz};
  for (int i = 0; i < 3; i++) {  // (Gaussian elimination with pivoting)
    int piv = i;
    for (int k = i + 1; k < 3; k++)
      if (std::fabs(a[k][i]) > std::fabs(a[piv][i])) piv = k;
    std::swap(a[i], a[piv]);
    std::swap(b[i], b[piv]);
    if (std::fabs(a[i][i]) < 1e-12) return false;
    for (int k = i + 1; k < 3; k++) {
      const double f = a[k][i] / a[i][i];
      for (int j = i; j < 3; j++) a[k][j] -= f * a[i][j];
      b[k] -= f * b[i];
    }
  }
  double x[3];
  for (int i = 2; i >= 0; i--) {
    double s = b[i];
    for (int j = i + 1; j < 3; j++) s -= a[i][j] * x[j];
    x[i] = s / a[i][i];
  }
  const double cx = -x[0] / 2, cy = -x[1] / 2, rr = cx * cx + cy * cy - x[2];
  if (!(rr > 0) || !std::isfinite(rr)) return false;
  c = Vec2{(float)cx + o.x, (float)cy + o.y};
  r = (float)std::sqrt(rr);
  return true;
}

/* The angle of p round c, and the signed sweep of a polyline round c (radians). */
static float angle_about(Vec2 c, Vec2 p) { return std::atan2(p.y - c.y, p.x - c.x); }
static float sweep_about(Vec2 c, const std::vector<Vec2> &pts) {
  float total = 0.0f, prev = angle_about(c, pts[0]);
  for (size_t i = 1; i < pts.size(); i++) {
    const float a = angle_about(c, pts[i]);
    float d = a - prev;
    while (d > kPi) d -= 2 * kPi;
    while (d < -kPi) d += 2 * kPi;
    total += d;
    prev = a;
  }
  return total;
}

bool fit_arc(const std::vector<Vec2> &pts, float tol, bool closed, Fit &out) {
  Vec2 c;
  float r;
  if (!fit_circle(pts, c, r)) return false;
  float worst = 0.0f;
  for (const Vec2 &p : pts) worst = std::max(worst, std::fabs(length(p - c) - r));
  if (worst > tol) return false;
  /* A nearly straight stroke fits a huge circle too: not an arc unless it bends (its sweep is a real turn). */
  const float sweep = sweep_about(c, pts);
  if (!closed && std::fabs(sweep) < 15.0f * kPi / 180.0f) return false;
  out = Fit{};
  out.kind = StrokeKind::Arc;
  out.closed = closed;
  out.center = c, out.rx = out.ry = r, out.angle = 0.0f;
  out.start = angle_about(c, pts.front());
  out.sweep = closed ? (sweep < 0 ? -2 * kPi : 2 * kPi) : sweep;
  out.pts = sample_arc(c, r, r, 0.0f, out.start, out.sweep, std::max(8, (int)(std::fabs(out.sweep) * r / 4.0f)));
  out.error = worst;
  return true;
}

bool fit_ellipse(const std::vector<Vec2> &pts, float tol, Fit &out) {
  if (pts.size() < 8) return false;
  Vec2 mean, dir;
  float va, vc;
  principal(pts, mean, dir, va, vc);
  /* The radii by least squares on x'^2 / rx^2 + y'^2 / ry^2 = 1 in the axes' frame (linear in 1 / rx^2 and
   * 1 / ry^2): exact for a true ellipse however the stroke is sampled. */
  const float angle = std::atan2(dir.y, dir.x);
  const float c = std::cos(angle), sn = std::sin(angle);
  double sxx = 0, sxy = 0, syy = 0, sx = 0, sy = 0;
  for (const Vec2 &p : pts) {
    const Vec2 d = p - mean;
    const double x2 = (double)(d.x * c + d.y * sn) * (d.x * c + d.y * sn), y2 = (double)(-d.x * sn + d.y * c) * (-d.x * sn + d.y * c);
    sxx += x2 * x2, sxy += x2 * y2, syy += y2 * y2, sx += x2, sy += y2;
  }
  const double det = sxx * syy - sxy * sxy;
  if (std::fabs(det) < 1e-12) return false;
  const double ia = (sx * syy - sy * sxy) / det, ib = (sy * sxx - sx * sxy) / det;  // (1 / rx^2, 1 / ry^2)
  if (!(ia > 0) || !(ib > 0)) return false;
  const float rx = (float)(1.0 / std::sqrt(ia)), ry = (float)(1.0 / std::sqrt(ib));
  (void)va, (void)vc;
  if (rx < 1e-3f || ry < 1e-3f) return false;
  float worst = 0.0f;
  for (const Vec2 &p : pts) worst = std::max(worst, length(p - snap_to_ellipse(p, mean, rx, ry, angle)));
  if (worst > tol) return false;
  out = Fit{};
  out.kind = StrokeKind::Ellipse;
  out.closed = true;
  out.center = mean, out.rx = rx, out.ry = ry, out.angle = angle;
  out.start = 0.0f, out.sweep = 2 * kPi;
  out.pts = sample_arc(mean, rx, ry, angle, 0.0f, 2 * kPi, std::max(24, (int)((rx + ry) * kPi / 4.0f)));
  out.error = worst;
  return true;
}

/* A Gaussian of `sigma` samples' width over a resampled polyline, the ends fixed. */
static std::vector<Vec2> smoothed(const std::vector<Vec2> &r, float sigma) {
  std::vector<Vec2> out = r;
  const int n = (int)r.size(), reach = (int)std::ceil(sigma * 2.0f);
  for (int i = 1; i + 1 < n; i++) {
    Vec2 sum{0, 0};
    float wsum = 0.0f;
    for (int k = -reach; k <= reach; k++) {
      const int j = std::max(0, std::min(n - 1, i + k));
      const float w = std::exp(-(float)(k * k) / (2.0f * sigma * sigma));
      sum += r[(size_t)j] * w;
      wsum += w;
    }
    out[(size_t)i] = sum / wsum;
  }
  return out;
}

Fit fit_curve(const std::vector<Vec2> &pts, float spacing) {
  Fit out;
  out.kind = StrokeKind::Curve;
  out.pts = smoothed(resample(pts, spacing), 2.0f);
  return out;
}

std::vector<Vec2> sample_arc(Vec2 center, float rx, float ry, float angle, float start, float sweep, int n) {
  std::vector<Vec2> out;
  const float c = std::cos(angle), s = std::sin(angle);
  n = std::max(2, n);
  for (int i = 0; i <= n; i++) {
    const float t = start + sweep * (float)i / (float)n;
    const float x = rx * std::cos(t), y = ry * std::sin(t);
    out.push_back({center.x + x * c - y * s, center.y + x * s + y * c});
  }
  return out;
}

std::vector<Fit> correct(const std::vector<Vec2> &pts, int level) {
  std::vector<Fit> out;
  if (pts.size() < 2) return out;
  const float spacing = 3.0f;
  const std::vector<Vec2> r = resample(pts, spacing);
  if (level <= 0 || r.size() < 4) {
    out.push_back(fit_curve(pts, spacing));
    out.back().kind = StrokeKind::Freehand;
    return out;
  }
  /* The corners are looked for on a lightly smoothed path with 18 px windows: a shaky hand's wobble swings a short
   * window's direction by tens of degrees, and a real corner survives the smoothing. */
  const float turn = level >= 2 ? 55.0f : 75.0f;
  std::vector<size_t> cut = corners(smoothed(r, 1.5f), 6, turn);
  float total = 0.0f;
  for (size_t i = 1; i < r.size(); i++) total += length(r[i] - r[i - 1]);
  const bool closed = length(r.front() - r.back()) < std::max(10.0f, total * 0.08f) && total > 40.0f;
  std::vector<Vec2> ring;  // (a closed stroke with corners, begun at its first corner: its start is a corner too)
  if (closed && !cut.empty()) {
    ring.assign(r.begin() + (ptrdiff_t)cut[0], r.end());
    ring.insert(ring.end(), r.begin() + 1, r.begin() + (ptrdiff_t)cut[0] + 1);
    cut = corners(smoothed(ring, 1.5f), 6, turn);
    if (cut.empty()) cut = corners(smoothed(r, 1.5f), 6, turn), ring.clear();
  }
  const std::vector<Vec2> &pl = ring.empty() ? r : ring;
  /* A scribble - pieces too short to mean anything, too many of them, or a path that turns sharply again and again
   * (a zigzag, a hatch) - stays as drawn: the sharp turns are counted on the path as the pen drew it. */
  bool scribble = cut.size() > 6 || corners(r, 3, 95.0f).size() > 6;
  for (size_t i = 0; i + 1 < cut.size() && !scribble; i++) scribble = (cut[i + 1] - cut[i]) * spacing < 12.0f;
  if (scribble) {
    out.push_back(fit_curve(pts, spacing));
    out.back().kind = StrokeKind::Freehand;
    return out;
  }
  std::vector<size_t> bounds{0};
  bounds.insert(bounds.end(), cut.begin(), cut.end());
  bounds.push_back(pl.size() - 1);
  for (size_t b = 0; b + 1 < bounds.size(); b++) {
    const std::vector<Vec2> piece(pl.begin() + (ptrdiff_t)bounds[b], pl.begin() + (ptrdiff_t)bounds[b + 1] + 1);
    float len = 0.0f;
    for (size_t i = 1; i < piece.size(); i++) len += length(piece[i] - piece[i - 1]);
    const float tol = std::max(4.0f, std::min(14.0f, len * 0.04f)) * (level >= 2 ? 1.0f : 0.5f);
    const bool piece_closed = closed && bounds.size() == 2;
    Fit f;
    if (fit_line(piece, tol, f)) out.push_back(f);
    else if (piece_closed && fit_ellipse(piece, tol, f)) out.push_back(f);
    else if (fit_arc(piece, tol, piece_closed, f)) out.push_back(f);
    else if (piece_closed && fit_ellipse(piece, tol * 1.5f, f)) out.push_back(f);
    else out.push_back(fit_curve(piece, spacing));
  }
  /* The pieces meet exactly at the corners: each corner is the mean of its two pieces' ends; a closed stroke's last
   * piece meets its first. */
  const size_t n = out.size();
  for (size_t i = 0; i + 1 < n + (closed && n > 1 ? 1 : 0); i++) {
    Fit &a = out[i], &b = out[(i + 1) % n];
    if (a.pts.empty() || b.pts.empty()) continue;
    const Vec2 m = (a.pts.back() + b.pts.front()) * 0.5f;
    a.pts.back() = m;
    b.pts.front() = m;
  }
  return out;
}

Vec2 snap_to_line(Vec2 p, Vec2 a, Vec2 b) {
  const Vec2 d = b - a;
  const float l2 = dot(d, d);
  if (l2 < 1e-12f) return a;
  return a + d * (dot(p - a, d) / l2);
}

Vec2 snap_to_ellipse(Vec2 p, Vec2 center, float rx, float ry, float angle) {
  /* In the ellipse's frame; the nearest point by Newton on the parameter from the angle of p. */
  const float c = std::cos(angle), s = std::sin(angle);
  const Vec2 d = p - center;
  const float x = d.x * c + d.y * s, y = -d.x * s + d.y * c;
  auto dist2 = [&](float t) {
    const float ex = rx * std::cos(t) - x, ey = ry * std::sin(t) - y;
    return ex * ex + ey * ey;
  };
  float t = 0.0f, best = dist2(0.0f);
  for (int i = 1; i < 64; i++) {  // (the coarse sweep: Newton alone from the angle of p can land on the farthest point)
    const float a = 2.0f * kPi * (float)i / 64.0f, d = dist2(a);
    if (d < best) best = d, t = a;
  }
  for (int i = 0; i < 8; i++) {
    const float ct = std::cos(t), st = std::sin(t);
    const float ex = rx * ct - x, ey = ry * st - y;
    const float f = -ex * rx * st + ey * ry * ct;  // d/dt of the half squared distance
    const float df = ex * rx * -ct + rx * rx * st * st + ey * ry * -st + ry * ry * ct * ct;
    if (std::fabs(df) < 1e-9f) break;
    const float nt = t - f / df, nd = dist2(nt);
    if (nd > best) break;  // (a step that doesn't come closer: the sweep's point stands)
    best = nd, t = nt;
  }
  const float ox = rx * std::cos(t), oy = ry * std::sin(t);
  return {center.x + ox * c - oy * s, center.y + ox * s + oy * c};
}

Vec2 snap_to_polyline(Vec2 p, const std::vector<Vec2> &pts) {
  if (pts.empty()) return p;
  Vec2 best = pts[0];
  float bd = length(p - pts[0]);
  for (size_t i = 1; i < pts.size(); i++) {
    const Vec2 a = pts[i - 1], b = pts[i], d = b - a;
    const float l2 = dot(d, d);
    const float t = l2 < 1e-12f ? 0.0f : std::max(0.0f, std::min(1.0f, dot(p - a, d) / l2));
    const Vec2 q = a + d * t;
    const float dist = length(p - q);
    if (dist < bd) bd = dist, best = q;
  }
  return best;
}

Vec2 mirror(Vec2 p, Vec2 a, Vec2 dir) {
  const float l = length(dir);
  if (l < 1e-9f) return p;
  const Vec2 d = dir / l;
  const Vec2 q = a + d * dot(p - a, d);
  return q + (q - p);
}

int nearest_vanishing(Vec2 from, Vec2 to, const Vec2 *vps, int n, float max_deg) {
  const Vec2 move = to - from;
  if (length(move) < 1e-6f) return -1;
  int best = -1;
  float best_deg = max_deg;
  for (int i = 0; i < n; i++) {
    const Vec2 toward = vps[i] - from;
    if (length(toward) < 1e-6f) continue;
    const float c = std::fabs(dot(move, toward)) / (length(move) * length(toward));  // (either way along the line)
    const float deg = std::acos(std::max(-1.0f, std::min(1.0f, c))) * 180.0f / kPi;
    if (deg < best_deg) best_deg = deg, best = i;
  }
  return best;
}

float distance_to_polyline(Vec2 p, const std::vector<Vec2> &pts) {
  if (pts.empty()) return 1e30f;
  return length(p - snap_to_polyline(p, pts));
}

std::vector<Vec2> french_curve(int which) {
  std::vector<Vec2> out;
  const int n = 40;
  for (int i = 0; i <= n; i++) {
    const float t = (float)i / (float)n;
    if (which <= 0) out.push_back({t - 0.5f, 0.18f * std::sin(t * 2.0f * kPi)});  // a gentle S
    else if (which == 1) {
      const float a = kPi * (0.15f + 0.7f * t);  // a C, three quarters of a circle's gentle side
      out.push_back({0.5f * std::cos(a) - 0.1f, 0.5f * std::sin(a) - 0.45f});
    }
    else {
      const float a = 2.2f * kPi * t, r = 0.5f * (1.0f - 0.75f * t);  // a tightening spiral
      out.push_back({r * std::cos(a), r * std::sin(a)});
    }
  }
  return out;
}

}  // namespace sketch
}  // namespace bl
