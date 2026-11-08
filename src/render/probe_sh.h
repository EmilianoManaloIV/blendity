// SPDX-License-Identifier: GPL-2.0-or-later
// Round 442 (task 0418, ADR 0116): light probes as L2 spherical harmonics - nine coefficients per colour, as Unity's
// light probes keep them (SphericalHarmonicsL2). Stored already convolved with the cosine lobe (Ramamoorthi and
// Hanrahan 2001, "An Efficient Representation for Irradiance Environment Maps"), so eval(n) is what the ambient cube's
// eval gives: irradiance / pi from radiance, and a lamp's colour x N.L. Negative lobes (L2's ringing) are clamped at 0.
#pragma once

#include "../core/math.h"

#include <array>
#include <cmath>

namespace bl {

struct ProbeSH {
  std::array<Vec3, 9> c{};

  /* The real SH basis to band 2 at unit direction n, in Sloan's order (Unity's too): band 1 as y, z, x; band 2's zonal
   * term on z. Any fixed order works: the bake and both renderers evaluate in world axes alike. */
  static void basis(Vec3 n, float y[9]) {
    y[0] = 0.282095f;
    y[1] = 0.488603f * n.y, y[2] = 0.488603f * n.z, y[3] = 0.488603f * n.x;
    y[4] = 1.092548f * n.x * n.y, y[5] = 1.092548f * n.y * n.z, y[6] = 0.315392f * (3.0f * n.z * n.z - 1.0f);
    y[7] = 1.092548f * n.x * n.z, y[8] = 0.546274f * (n.x * n.x - n.y * n.y);
  }
  /* A_l / pi per coefficient: the cosine lobe's bands (pi, 2 pi / 3, pi / 4) over pi. */
  static float band(int k) { return k == 0 ? 1.0f : k < 4 ? 2.0f / 3.0f : 0.25f; }

  /* Radiance L arriving from `dir` over `solid_angle` steradians (a sample's share of the sphere). */
  void add_radiance(Vec3 dir, Vec3 L, float solid_angle) {
    float y[9];
    basis(dir, y);
    for (int k = 0; k < 9; k++) c[(size_t)k] += L * (y[k] * band(k) * solid_angle);
  }
  /* A lamp seen from `dir` lighting a surface facing it with `col` (the units the ambient cube's lamps use): its
   * irradiance col x max(0, N.L), as far as L2 holds it (1.06 x at the peak, a little ringing behind). */
  void add_direct(Vec3 dir, Vec3 col) {
    float y[9];
    basis(dir, y);
    for (int k = 0; k < 9; k++) c[(size_t)k] += col * (y[k] * band(k) * kPi);
  }
  /* The band of coefficient k (0, 1 or 2): which of a window's three factors scales it. */
  static int band_of(int k) { return k == 0 ? 0 : k < 4 ? 1 : 2; }
  /* The sum before the clamp, bands 1 and 2 scaled by `win` (round 455, ADR 0119: window_factors; 1, 1, 1 is none). */
  Vec3 eval_raw(Vec3 n, const std::array<float, 3> &win = {1.0f, 1.0f, 1.0f}) const {
    float y[9];
    basis(n, y);
    Vec3 o(0.0f);
    for (int k = 0; k < 9; k++) o += c[(size_t)k] * (y[k] * win[(size_t)band_of(k)]);
    return o;
  }
  Vec3 eval(Vec3 n, const std::array<float, 3> &win = {1.0f, 1.0f, 1.0f}) const {
    const Vec3 o = eval_raw(n, win);
    return Vec3(std::max(0.0f, o.x), std::max(0.0f, o.y), std::max(0.0f, o.z));
  }
  ProbeSH &operator+=(const ProbeSH &o) {
    for (int k = 0; k < 9; k++) c[(size_t)k] += o.c[(size_t)k];
    return *this;
  }
  ProbeSH operator*(float s) const {
    ProbeSH r;
    for (int k = 0; k < 9; k++) r.c[(size_t)k] = c[(size_t)k] * s;
    return r;
  }
};

/* Round 455 (ADR 0119): the window that tames L2's ringing (Sloan, "Stupid Spherical Harmonics Tricks"): band 0 kept, bands 1
 * and 2 times h1, h2. kind 1 Hanning h_l = (1 + cos(pi l / w)) / 2, kind 2 Lanczos h_l = sin(pi l / w) / (pi l / w); w is the
 * window's width in bands (3 to 16; w = 4: 1, 0.854, 0.5 and 1, 0.900, 0.637). Any other kind, or a width that is not a
 * number, gives no window. */
inline std::array<float, 3> window_factors(int kind, float w) {
  std::array<float, 3> h = {1.0f, 1.0f, 1.0f};
  if ((kind != 1 && kind != 2) || !(w >= 1.0f && w <= 64.0f)) return h;
  for (int l = 1; l <= 2; l++) {
    const float a = kPi * (float)l / w;
    h[(size_t)l] = kind == 1 ? (1.0f + std::cos(a)) * 0.5f : std::sin(a) / a;
  }
  return h;
}

/* Round 442: the 26 directions Unity's probes are read along (the cube's 6 faces, 12 edges, 8 corners, in this order -
 * the editor script writes them the same way): enough to fit the nine coefficients of each colour exactly. */
inline void probe_fit_directions(Vec3 out[26]) {
  const Vec3 axes[6] = {Vec3(1, 0, 0), Vec3(-1, 0, 0), Vec3(0, 1, 0), Vec3(0, -1, 0), Vec3(0, 0, 1), Vec3(0, 0, -1)};
  int k = 0;
  for (Vec3 a : axes) out[k++] = a;
  const float s[2] = {1.0f, -1.0f};
  for (int e = 0; e < 3; e++)
    for (float a : s)
      for (float b : s) out[k++] = normalize(e == 0 ? Vec3(a, b, 0) : e == 1 ? Vec3(a, 0, b) : Vec3(0, a, b));
  for (float a : s)
    for (float b : s)
      for (float d : s) out[k++] = normalize(Vec3(a, b, d));
}

/* Least squares from values seen along fixed directions to L2 coefficients: the pseudo-inverse of the 26 x 9 basis
 * matrix, built once (the directions turned by a scene's placement are still 26 spread directions). */
struct ProbeShFit {
  float pinv[9][26] = {};
  bool ok = false;
  void init(const Vec3 dirs[26]) {
    double A[26][9], N[9][18] = {};
    for (int j = 0; j < 26; j++) {
      float y[9];
      ProbeSH::basis(dirs[j], y);
      for (int k = 0; k < 9; k++) A[j][k] = y[k];
    }
    for (int r = 0; r < 9; r++) {  // [A^T A | I], then Gauss-Jordan
      for (int k = 0; k < 9; k++)
        for (int j = 0; j < 26; j++) N[r][k] += A[j][r] * A[j][k];
      N[r][9 + r] = 1.0;
    }
    ok = true;
    for (int col = 0; col < 9 && ok; col++) {
      int piv = col;
      for (int r = col + 1; r < 9; r++) piv = std::fabs(N[r][col]) > std::fabs(N[piv][col]) ? r : piv;
      if (std::fabs(N[piv][col]) < 1e-12) ok = false;
      for (int k = 0; ok && k < 18; k++) std::swap(N[col][k], N[piv][k]);
      const double inv = ok ? 1.0 / N[col][col] : 0.0;
      for (int k = 0; ok && k < 18; k++) N[col][k] *= inv;
      for (int r = 0; ok && r < 9; r++) {
        if (r == col) continue;
        const double f = N[r][col];
        for (int k = 0; k < 18; k++) N[r][k] -= f * N[col][k];
      }
    }
    for (int r = 0; r < 9; r++)  // pinv = (A^T A)^-1 A^T
      for (int j = 0; j < 26; j++) {
        double v = 0.0;
        for (int k = 0; k < 9; k++) v += N[r][9 + k] * A[j][k];
        pinv[r][j] = (float)v;
      }
  }
  /* Coefficients whose eval (before the clamp) matches `seen` along the directions, in least squares. */
  ProbeSH fit(const Vec3 seen[26]) const {
    ProbeSH s;
    for (int k = 0; k < 9; k++)
      for (int j = 0; j < 26; j++) s.c[(size_t)k] += seen[j] * pinv[k][j];
    return s;
  }
};

}  // namespace bl
