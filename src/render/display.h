// SPDX-License-Identifier: GPL-2.0-or-later
// Display encoding: HDR linear RGB -> view transform -> 8-bit sRGB pixels.
//
// This runs once per pixel per frame (viewport shading, path tracer resolve,
// environment background), so it has hand-written SIMD kernels:
//  - the sRGB curve is an exact table (no pow): the float's exponent and top 7
//    mantissa bits pick a bucket, and each bucket stores its 8-bit value plus
//    the one threshold where the rounded result steps up. Results are
//    correctly rounded (the curve in double precision) for every float in [0, 1].
//  - the Filmic / ACES curves run 4 (SSE4.1) or 8 (AVX2 + FMA) pixels at a time,
//    with the kernel chosen at run time from cpu::features().
#pragma once

#include <cstddef>
#include <cstdint>

#include "shading.h"

namespace bl::display {

static_assert(sizeof(Vec3) == 12, "encode_span reads Vec3 arrays as packed RGB floats");

/* round(sRGB(saturate(v)) * 255), correctly rounded, without pow(). NaN -> 0. */
uint8_t linear_to_srgb8(float v);
/* The same value from the double-precision curve (slow; for tests). */
int srgb8_reference(float v);

/* n pixels of interleaved linear RGB (3 floats each) -> 0xAARRGGBB. */
void encode_span(const float *rgb, uint32_t *out, size_t n, ViewTransform vt, float exposure_stops);

enum class Kernel { Auto, Scalar, SSE41, AVX2 };
/* For benchmarks and tests: force a kernel (falls back when the CPU lacks it). */
void set_kernel(Kernel k);
Kernel active_kernel();
const char *kernel_name(Kernel k);

}  // namespace bl::display
