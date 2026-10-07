// SPDX-License-Identifier: GPL-2.0-or-later
// CPU feature detection for runtime SIMD dispatch. Blendity's baseline is
// x86-64-v2 (SSE4.2), the same as Blender's; wider kernels are chosen at run
// time (Cycles does the same with its SSE4.1 / AVX2 kernel variants:
// blender/intern/cycles/util/system.cpp).
#pragma once

#include <string>

namespace bl::cpu {

struct Features {
  bool sse41 = false, sse42 = false, avx = false, avx2 = false, fma = false, avx512f = false;
  bool neon = false;
};

const Features &features();
/* "AVX2 + FMA", "SSE4.2", "NEON"... for the Profiler and stress reports. */
std::string describe();

}  // namespace bl::cpu
