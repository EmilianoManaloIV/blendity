// SPDX-License-Identifier: GPL-2.0-or-later
#include "colormanagement.h"

#include "../core/core.h"
#include "../core/jobs.h"
#include "../image/image.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <mutex>

#ifdef BL_WITH_OCIO
#  include <OpenColorIO/OpenColorIO.h>
namespace OCIO = OCIO_NAMESPACE;
#endif

namespace bl::colormanagement {

namespace {

/* Log2 shaper range. The low end is Blender's Filmic/AgX log minimum; the
 * top stops at 2^6.5 (~90x scene white), where every view has already
 * reached display white, so the grid nodes go where the curves bend. */
constexpr float kLogMin = -12.473931188f, kLogMax = 6.5f;
constexpr int kLutSize = 97;  // 65 left AgX at 1.65/255 (99th percentile) on saturated colours

struct ViewLut {
  std::once_flag built;
  std::vector<Vec3> data;  // kLutSize^3, display-encoded
};

struct State {
  bool ok = false;
  std::string path;
  std::vector<std::string> views;
  std::vector<std::unique_ptr<ViewLut>> luts;
#ifdef BL_WITH_OCIO
  OCIO::ConstConfigRcPtr config;
  std::string display = "sRGB";
#endif
};

std::string find_config() {
  for (const char *env : {"BLENDITY_OCIO", "OCIO"})
    if (const char *p = std::getenv(env))
      if (fs::exists(p)) return p;
  const std::string exe = fs::executable_dir();
  /* Shipped next to the executables, else Blender's source tree beside the
   * project (development layout: blendity/dist/<platform>/ and blender/). */
  for (const std::string &p :
       {fs::join(exe, "datafiles/colormanagement/config.ocio"),
        fs::join(exe, "../../../blender/release/datafiles/colormanagement/config.ocio"),
        fs::join(exe, "../../../../blender/release/datafiles/colormanagement/config.ocio"),
        fs::join(fs::current_dir(), "../blender/release/datafiles/colormanagement/config.ocio")})
    if (fs::exists(p)) return fs::normalize(p);
  return "";
}

State &state() {
  static State s;
  static std::once_flag init;
  std::call_once(init, [] {
#ifdef BL_WITH_OCIO
    s.path = find_config();
    if (s.path.empty()) return;
    try {
      s.config = OCIO::Config::CreateFromFile(s.path.c_str());
      if (s.config->getNumViews(s.display.c_str()) == 0) s.display = s.config->getDefaultDisplay();
      for (int i = 0; i < s.config->getNumViews(s.display.c_str()); i++) {
        s.views.push_back(s.config->getView(s.display.c_str(), i));
        s.luts.push_back(std::make_unique<ViewLut>());
      }
      s.ok = !s.views.empty();
    }
    catch (const std::exception &e) {
      Log::warn("OpenColorIO: cannot load %s: %s", s.path.c_str(), e.what());
    }
#endif
  });
  return s;
}

#ifdef BL_WITH_OCIO
OCIO::ConstCPUProcessorRcPtr cpu_processor(State &s, int view) {
  auto t = OCIO::DisplayViewTransform::Create();
  t->setSrc(OCIO::ROLE_SCENE_LINEAR);
  t->setDisplay(s.display.c_str());
  t->setView(s.views[(size_t)view].c_str());
  return s.config->getProcessor(t)->getDefaultCPUProcessor();
}
#endif

inline float shaper(float x) {
  if (!(x > 0.0f)) return 0.0f;
  return std::clamp((std::log2(x) - kLogMin) * (1.0f / (kLogMax - kLogMin)), 0.0f, 1.0f);
}

const ViewLut *lut(int view) {
  State &s = state();
  if (!s.ok || view < 0 || view >= (int)s.luts.size()) return nullptr;
  ViewLut &L = *s.luts[(size_t)view];
  std::call_once(L.built, [&] {
#ifdef BL_WITH_OCIO
    ScopedTimer t;
    const int N = kLutSize;
    std::vector<float> grid((size_t)N * N * N * 3);
    for (int b = 0; b < N; b++)
      for (int g = 0; g < N; g++)
        for (int r = 0; r < N; r++) {
          float *p = &grid[(((size_t)b * N + g) * N + r) * 3];
          const int idx[3] = {r, g, b};
          for (int k = 0; k < 3; k++) {
            /* Grid node 0 stands for black: everything below the shaper range. */
            p[k] = idx[k] == 0 ? 0.0f : std::exp2(kLogMin + (kLogMax - kLogMin) * idx[k] / (float)(N - 1));
          }
        }
    try {
      OCIO::ConstCPUProcessorRcPtr cpu = cpu_processor(s, view);
      /* Slabs of the grid in parallel: the CPU processor is thread-safe. */
      JobSystem::global().parallel_for(N, 1, [&](int64_t b0, int64_t b1) {
        OCIO::PackedImageDesc img(&grid[(size_t)b0 * N * N * 3], (long)(N * N * (b1 - b0)), 1, 3);
        cpu->apply(img);
      });
      L.data.resize((size_t)N * N * N);
      for (size_t i = 0; i < L.data.size(); i++) L.data[i] = {grid[i * 3], grid[i * 3 + 1], grid[i * 3 + 2]};
      Log::info("OpenColorIO: baked view '%s' into a %d^3 LUT in %.0f ms", s.views[(size_t)view].c_str(), N, t.ms());
    }
    catch (const std::exception &e) {
      Log::warn("OpenColorIO: view '%s' failed: %s", s.views[(size_t)view].c_str(), e.what());
    }
#endif
  });
  return L.data.empty() ? nullptr : &L;
}

}  // namespace

bool available() { return state().ok; }
std::string config_path() { return state().path; }
const std::vector<std::string> &views() { return state().views; }

int view_index(const std::string &name) {
  const auto &v = views();
  for (size_t i = 0; i < v.size(); i++)
    if (v[i] == name) return (int)i;
  return -1;
}

Vec3 display_rgb(int view, Vec3 c) {
  const ViewLut *L = lut(view);
  if (!L) return Vec3(saturate(linear_to_srgb(c.x)), saturate(linear_to_srgb(c.y)), saturate(linear_to_srgb(c.z)));
  /* Shaper, then tetrahedral interpolation in the 97^3 grid (what OpenColorIO's
   * own Lut3D uses: 4 lookups, and better than trilinear on views that mix
   * channels, like AgX). */
  const int N = kLutSize;
  float f[3] = {shaper(c.x) * (N - 1), shaper(c.y) * (N - 1), shaper(c.z) * (N - 1)};
  int i0[3];
  float t[3];
  for (int k = 0; k < 3; k++) {
    i0[k] = std::min((int)f[k], N - 2);
    t[k] = f[k] - i0[k];
  }
  auto at = [&](int r, int g, int b) { return L->data[((size_t)(i0[2] + b) * N + (i0[1] + g)) * N + (i0[0] + r)]; };
  const float fr = t[0], fg = t[1], fb = t[2];
  const Vec3 c000 = at(0, 0, 0), c111 = at(1, 1, 1);
  if (fr > fg) {
    if (fg > fb) return c000 + (at(1, 0, 0) - c000) * fr + (at(1, 1, 0) - at(1, 0, 0)) * fg + (c111 - at(1, 1, 0)) * fb;
    if (fr > fb) return c000 + (at(1, 0, 0) - c000) * fr + (c111 - at(1, 0, 1)) * fg + (at(1, 0, 1) - at(1, 0, 0)) * fb;
    return c000 + (at(1, 0, 1) - at(0, 0, 1)) * fr + (c111 - at(1, 0, 1)) * fg + (at(0, 0, 1) - c000) * fb;
  }
  if (fb > fg) return c000 + (c111 - at(0, 1, 1)) * fr + (at(0, 1, 1) - at(0, 0, 1)) * fg + (at(0, 0, 1) - c000) * fb;
  if (fb > fr) return c000 + (c111 - at(0, 1, 1)) * fr + (at(0, 1, 0) - c000) * fg + (at(0, 1, 1) - at(0, 1, 0)) * fb;
  return c000 + (at(1, 1, 0) - at(0, 1, 0)) * fr + (at(0, 1, 0) - c000) * fg + (c111 - at(1, 1, 0)) * fb;
}

uint32_t display_pixel(int view, Vec3 c) {
  Vec3 d = display_rgb(view, c);
  auto q = [](float v) {
    int i = (int)(saturate(v) * 255.0f + 0.5f);
    return (uint32_t)i;
  };
  return 0xFF000000u | (q(d.x) << 16) | (q(d.y) << 8) | q(d.z);
}

bool reference(int view, const float *in, float *out, size_t count) {
#ifdef BL_WITH_OCIO
  State &s = state();
  if (!s.ok || view < 0 || view >= (int)s.views.size()) return false;
  std::copy(in, in + count * 3, out);
  OCIO::PackedImageDesc img(out, (long)count, 1, 3);
  cpu_processor(s, view)->apply(img);
  return true;
#else
  (void)view;
  (void)in;
  (void)out;
  (void)count;
  return false;
#endif
}

}  // namespace bl::colormanagement
