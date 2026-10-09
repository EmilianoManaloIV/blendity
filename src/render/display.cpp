// SPDX-License-Identifier: GPL-2.0-or-later
#include "display.h"

#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>

#include "colormanagement.h"
#include "../core/cpu.h"
#include "../image/image.h"

#if defined(_M_X64) || defined(__x86_64__)
#  define BL_DISPLAY_X86 1
#  include <immintrin.h>
#  ifdef _MSC_VER
#    define BL_TARGET_SSE41
#    define BL_TARGET_AVX2
#  else
#    define BL_TARGET_SSE41 __attribute__((target("sse4.1")))
#    define BL_TARGET_AVX2 __attribute__((target("avx2,fma")))
#  endif
#endif

namespace bl::display {

/* ------------------------------------------------------------ sRGB table */

/* Buckets cover [2^-24, 1]: index = (float bits >> 16) - (103 << 7), i.e. the
 * exponent and the top 7 mantissa bits. Within a bucket the exact curve moves
 * by less than one 8-bit step (at most ~0.9 near 1.0), so it crosses at most
 * one rounding boundary: value = base + (v >= threshold). Below 2^-24 the
 * result is 0 (12.92 * 255 * 2^-24 < 0.5). */
constexpr int kFirstExp = 103;                // 2^-24
constexpr int kBuckets = (127 - kFirstExp) * 128 + 1;  // + the bucket holding exactly 1.0

struct Srgb8Table {
  alignas(64) float thresh[kBuckets];
  alignas(64) int32_t base[kBuckets];
};

static inline uint32_t float_bits(float f) {
  uint32_t u;
  std::memcpy(&u, &f, 4);
  return u;
}
static inline float bits_float(uint32_t u) {
  float f;
  std::memcpy(&f, &u, 4);
  return f;
}

/* The definition the table reproduces: the sRGB curve in double precision,
 * rounded to nearest. (Single-precision powf is not quite monotonic, so a table
 * built from it inherits its glitches; powf itself differs on ~1 in 2.5M floats.) */
int srgb8_reference(float v) {
  double d = v > 0.0f ? (v < 1.0f ? v : 1.0) : 0.0;
  d = d <= 0.0031308 ? d * 12.92 : 1.055 * std::pow(d, 1.0 / 2.4) - 0.055;
  int i = (int)(d * 255.0 + 0.5);
  return i < 0 ? 0 : (i > 255 ? 255 : i);
}

static const Srgb8Table &table() {
  static const Srgb8Table *t = [] {
    auto *r = new Srgb8Table;
    const float inf = std::numeric_limits<float>::infinity();
    for (int b = 0; b < kBuckets; b++) {
      uint32_t lo = (uint32_t)(b + (kFirstExp << 7)) << 16, hi = lo + 0xFFFFu;
      int base = srgb8_reference(bits_float(lo));
      r->base[b] = base;
      r->thresh[b] = inf;
      if (b == kBuckets - 1 || srgb8_reference(bits_float(hi)) == base) continue;
      /* First float in the bucket that rounds one step higher. */
      while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (srgb8_reference(bits_float(mid)) > base) hi = mid;
        else lo = mid + 1;
      }
      r->thresh[b] = bits_float(lo);
    }
    return r;
  }();
  return *t;
}

static inline int bucket_of(float v) {
  int b = (int)(float_bits(v) >> 16) - (kFirstExp << 7);
  return b < 0 ? 0 : b;
}

uint8_t linear_to_srgb8(float v) {
  if (nan_bits(v)) return 0;  // not a compare: /fp:fast may assume no NaN
  v = v > 0.0f ? (v < 1.0f ? v : 1.0f) : 0.0f;
  const Srgb8Table &t = table();
  int b = bucket_of(v);
  return (uint8_t)(t.base[b] + (v >= t.thresh[b] ? 1 : 0));
}

/* ------------------------------------------------------------ tone curves */

namespace {
struct Curve {
  ViewTransform vt;
  float scale;  // exposure (and Filmic's 2x pre-scale)
  float wscale;
};
constexpr float kA = 0.15f, kB = 0.50f, kC = 0.10f, kD = 0.20f, kE = 0.02f, kF = 0.30f;

inline float hable(float x) { return ((x * (kA * x + kC * kB) + kD * kE) / (x * (kA * x + kB) + kD * kF)) - kE / kF; }

Curve make_curve(ViewTransform vt, float exposure) {
  Curve c{vt, std::exp2(exposure), 1.0f};
  if (vt == ViewTransform::Filmic) {
    c.scale *= 2.0f;
    c.wscale = 1.0f / hable(11.2f);
  } else if (vt == ViewTransform::ACES) {
    c.scale *= 0.6f;
  }
  return c;
}

inline float curve_scalar(const Curve &c, float x) {
  x *= c.scale;
  switch (c.vt) {
    case ViewTransform::Filmic: return hable(x) * c.wscale;
    case ViewTransform::ACES: return (x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f);
    default: return x;
  }
}

void encode_scalar(const float *rgb, uint32_t *out, size_t n, const Curve &c) {
  for (size_t i = 0; i < n; i++, rgb += 3)
    out[i] = 0xFF000000u | ((uint32_t)linear_to_srgb8(curve_scalar(c, rgb[0])) << 16) |
             ((uint32_t)linear_to_srgb8(curve_scalar(c, rgb[1])) << 8) | linear_to_srgb8(curve_scalar(c, rgb[2]));
}

#ifdef BL_DISPLAY_X86
/* 4 interleaved RGB pixels (12 floats) -> R, G, B lanes. */
BL_TARGET_SSE41 inline void load_rgb4(const float *p, __m128 &x, __m128 &y, __m128 &z) {
  __m128 a = _mm_loadu_ps(p), b = _mm_loadu_ps(p + 4), c = _mm_loadu_ps(p + 8);
  // a = x0 y0 z0 x1 | b = y1 z1 x2 y2 | c = z2 x3 y3 z3
  x = _mm_shuffle_ps(a, _mm_shuffle_ps(b, c, _MM_SHUFFLE(1, 1, 2, 2)), _MM_SHUFFLE(2, 0, 3, 0));
  y = _mm_shuffle_ps(_mm_shuffle_ps(a, b, _MM_SHUFFLE(0, 0, 1, 1)), _mm_shuffle_ps(b, c, _MM_SHUFFLE(2, 2, 3, 3)),
                     _MM_SHUFFLE(2, 0, 2, 0));
  z = _mm_shuffle_ps(_mm_shuffle_ps(a, b, _MM_SHUFFLE(1, 1, 2, 2)), _mm_shuffle_ps(c, c, _MM_SHUFFLE(3, 3, 0, 0)),
                     _MM_SHUFFLE(2, 0, 2, 0));
}

BL_TARGET_SSE41 inline __m128 curve_sse(const Curve &c, __m128 x) {
  x = _mm_mul_ps(x, _mm_set1_ps(c.scale));
  if (c.vt == ViewTransform::Filmic) {
    __m128 num = _mm_add_ps(_mm_mul_ps(x, _mm_add_ps(_mm_mul_ps(_mm_set1_ps(kA), x), _mm_set1_ps(kC * kB))), _mm_set1_ps(kD * kE));
    __m128 den = _mm_add_ps(_mm_mul_ps(x, _mm_add_ps(_mm_mul_ps(_mm_set1_ps(kA), x), _mm_set1_ps(kB))), _mm_set1_ps(kD * kF));
    return _mm_mul_ps(_mm_sub_ps(_mm_div_ps(num, den), _mm_set1_ps(kE / kF)), _mm_set1_ps(c.wscale));
  }
  if (c.vt == ViewTransform::ACES) {
    __m128 num = _mm_mul_ps(x, _mm_add_ps(_mm_mul_ps(_mm_set1_ps(2.51f), x), _mm_set1_ps(0.03f)));
    __m128 den = _mm_add_ps(_mm_mul_ps(x, _mm_add_ps(_mm_mul_ps(_mm_set1_ps(2.43f), x), _mm_set1_ps(0.59f))), _mm_set1_ps(0.14f));
    return _mm_div_ps(num, den);
  }
  return x;
}

/* Saturate (NaN -> 0), then base[b] + (v >= thresh[b]) with 4 table reads. */
BL_TARGET_SSE41 inline __m128i srgb8_sse(__m128 v, const Srgb8Table &t) {
  v = _mm_min_ps(_mm_max_ps(v, _mm_setzero_ps()), _mm_set1_ps(1.0f));
  __m128i b = _mm_max_epi32(_mm_sub_epi32(_mm_srli_epi32(_mm_castps_si128(v), 16), _mm_set1_epi32(kFirstExp << 7)), _mm_setzero_si128());
  alignas(16) int32_t idx[4];
  _mm_store_si128((__m128i *)idx, b);
  __m128 th = _mm_setr_ps(t.thresh[idx[0]], t.thresh[idx[1]], t.thresh[idx[2]], t.thresh[idx[3]]);
  __m128i base = _mm_setr_epi32(t.base[idx[0]], t.base[idx[1]], t.base[idx[2]], t.base[idx[3]]);
  return _mm_sub_epi32(base, _mm_castps_si128(_mm_cmpge_ps(v, th)));  // mask is -1 where stepping up
}

BL_TARGET_SSE41 void encode_sse41(const float *rgb, uint32_t *out, size_t n, const Curve &c) {
  const Srgb8Table &t = table();
  size_t i = 0;
  for (; i + 4 <= n; i += 4, rgb += 12) {
    __m128 r, g, b;
    load_rgb4(rgb, r, g, b);
    __m128i R = srgb8_sse(curve_sse(c, r), t), G = srgb8_sse(curve_sse(c, g), t), B = srgb8_sse(curve_sse(c, b), t);
    __m128i px = _mm_or_si128(_mm_or_si128(_mm_slli_epi32(R, 16), _mm_slli_epi32(G, 8)), _mm_or_si128(B, _mm_set1_epi32((int)0xFF000000u)));
    _mm_storeu_si128((__m128i *)(out + i), px);
  }
  encode_scalar(rgb, out + i, n - i, c);
}

BL_TARGET_AVX2 inline __m256 curve_avx2(const Curve &c, __m256 x) {
  x = _mm256_mul_ps(x, _mm256_set1_ps(c.scale));
  if (c.vt == ViewTransform::Filmic) {
    __m256 a = _mm256_set1_ps(kA);
    __m256 num = _mm256_fmadd_ps(x, _mm256_fmadd_ps(a, x, _mm256_set1_ps(kC * kB)), _mm256_set1_ps(kD * kE));
    __m256 den = _mm256_fmadd_ps(x, _mm256_fmadd_ps(a, x, _mm256_set1_ps(kB)), _mm256_set1_ps(kD * kF));
    return _mm256_mul_ps(_mm256_sub_ps(_mm256_div_ps(num, den), _mm256_set1_ps(kE / kF)), _mm256_set1_ps(c.wscale));
  }
  if (c.vt == ViewTransform::ACES) {
    __m256 num = _mm256_mul_ps(x, _mm256_fmadd_ps(_mm256_set1_ps(2.51f), x, _mm256_set1_ps(0.03f)));
    __m256 den = _mm256_fmadd_ps(x, _mm256_fmadd_ps(_mm256_set1_ps(2.43f), x, _mm256_set1_ps(0.59f)), _mm256_set1_ps(0.14f));
    return _mm256_div_ps(num, den);
  }
  return x;
}

BL_TARGET_AVX2 inline __m256i srgb8_avx2(__m256 v, const Srgb8Table &t) {
  v = _mm256_min_ps(_mm256_max_ps(v, _mm256_setzero_ps()), _mm256_set1_ps(1.0f));
  __m256i b = _mm256_max_epi32(_mm256_sub_epi32(_mm256_srli_epi32(_mm256_castps_si256(v), 16), _mm256_set1_epi32(kFirstExp << 7)),
                               _mm256_setzero_si256());
  __m256 th = _mm256_i32gather_ps(t.thresh, b, 4);
  __m256i base = _mm256_i32gather_epi32(t.base, b, 4);
  return _mm256_sub_epi32(base, _mm256_castps_si256(_mm256_cmp_ps(v, th, _CMP_GE_OQ)));
}

BL_TARGET_AVX2 void encode_avx2(const float *rgb, uint32_t *out, size_t n, const Curve &c) {
  const Srgb8Table &t = table();
  size_t i = 0;
  for (; i + 8 <= n; i += 8, rgb += 24) {
    __m128 r0, g0, b0, r1, g1, b1;
    load_rgb4(rgb, r0, g0, b0);
    load_rgb4(rgb + 12, r1, g1, b1);
    __m256 r = _mm256_set_m128(r1, r0), g = _mm256_set_m128(g1, g0), b = _mm256_set_m128(b1, b0);
    __m256i R = srgb8_avx2(curve_avx2(c, r), t), G = srgb8_avx2(curve_avx2(c, g), t), B = srgb8_avx2(curve_avx2(c, b), t);
    __m256i px = _mm256_or_si256(_mm256_or_si256(_mm256_slli_epi32(R, 16), _mm256_slli_epi32(G, 8)),
                                 _mm256_or_si256(B, _mm256_set1_epi32((int)0xFF000000u)));
    _mm256_storeu_si256((__m256i *)(out + i), px);
  }
  encode_scalar(rgb, out + i, n - i, c);
}
#endif

std::atomic<int> g_forced{(int)Kernel::Auto};

Kernel best_kernel() {
#ifdef BL_DISPLAY_X86
  const cpu::Features &f = cpu::features();
  if (f.avx2 && f.fma) return Kernel::AVX2;
  if (f.sse41) return Kernel::SSE41;
#endif
  return Kernel::Scalar;
}
}  // namespace

void set_kernel(Kernel k) { g_forced = (int)k; }

Kernel active_kernel() {
  Kernel best = best_kernel(), k = (Kernel)g_forced.load(std::memory_order_relaxed);
  if (k == Kernel::Auto || (int)k > (int)best) return best;  // never pick an unsupported kernel
  return k;
}

const char *kernel_name(Kernel k) {
  switch (k) {
    case Kernel::Scalar: return "scalar";
    case Kernel::SSE41: return "SSE4.1 (4 px)";
    case Kernel::AVX2: return "AVX2 + FMA (8 px)";
    default: return "auto";
  }
}

void encode_span(const float *rgb, uint32_t *out, size_t n, ViewTransform vt, float exposure_stops) {
  if ((int)vt >= (int)ViewTransform::OcioView) {
    for (size_t i = 0; i < n; i++, rgb += 3) out[i] = to_display_pixel({rgb[0], rgb[1], rgb[2]}, vt, exposure_stops);
    return;
  }
  Curve c = make_curve(vt, exposure_stops);
  switch (active_kernel()) {
#ifdef BL_DISPLAY_X86
    case Kernel::AVX2: encode_avx2(rgb, out, n, c); break;
    case Kernel::SSE41: encode_sse41(rgb, out, n, c); break;
#endif
    default: encode_scalar(rgb, out, n, c); break;
  }
}

}  // namespace bl::display
