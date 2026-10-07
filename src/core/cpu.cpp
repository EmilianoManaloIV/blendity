// SPDX-License-Identifier: GPL-2.0-or-later
#include "cpu.h"

#if defined(_M_X64) || defined(__x86_64__)
#  define BL_X86 1
#  ifdef _MSC_VER
#    include <intrin.h>
#  else
#    include <cpuid.h>
#  endif
#endif

namespace bl::cpu {

#ifdef BL_X86
static void cpuid(int leaf, int sub, unsigned r[4]) {
#  ifdef _MSC_VER
  int out[4];
  __cpuidex(out, leaf, sub);
  for (int i = 0; i < 4; i++) r[i] = (unsigned)out[i];
#  else
  __cpuid_count(leaf, sub, r[0], r[1], r[2], r[3]);
#  endif
}

static unsigned long long xgetbv0() {
#  ifdef _MSC_VER
  return _xgetbv(0);
#  else
  unsigned lo, hi;
  __asm__ volatile("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0));
  return ((unsigned long long)hi << 32) | lo;
#  endif
}
#endif

const Features &features() {
  static const Features f = [] {
    Features r;
#ifdef BL_X86
    unsigned a[4];
    cpuid(0, 0, a);
    const unsigned max_leaf = a[0];
    cpuid(1, 0, a);
    r.sse41 = (a[2] >> 19) & 1;
    r.sse42 = (a[2] >> 20) & 1;
    r.fma = (a[2] >> 12) & 1;
    const bool osxsave = (a[2] >> 27) & 1, avx_bit = (a[2] >> 28) & 1;
    /* AVX needs the OS to save the YMM registers (XCR0 bits 1 and 2). */
    const bool ymm_saved = osxsave && (xgetbv0() & 0x6) == 0x6;
    r.avx = avx_bit && ymm_saved;
    r.fma = r.fma && r.avx;
    if (max_leaf >= 7) {
      cpuid(7, 0, a);
      r.avx2 = r.avx && ((a[1] >> 5) & 1);
      r.avx512f = r.avx && ((a[1] >> 16) & 1) && osxsave && (xgetbv0() & 0xE6) == 0xE6;
    }
#elif defined(__aarch64__) || defined(_M_ARM64)
    r.neon = true;
#endif
    return r;
  }();
  return f;
}

std::string describe() {
  const Features &f = features();
  if (f.neon) return "NEON";
  if (f.avx512f) return "AVX-512 + AVX2 + FMA";
  if (f.avx2) return f.fma ? "AVX2 + FMA" : "AVX2";
  if (f.avx) return "AVX";
  if (f.sse42) return "SSE4.2";
  if (f.sse41) return "SSE4.1";
  return "SSE2";
}

}  // namespace bl::cpu
