// SPDX-License-Identifier: GPL-2.0-or-later
#include "deps.h"

#include "../core/core.h"

#ifdef BL_WITH_TBB
#  include <oneapi/tbb/version.h>
#endif
#ifdef BL_WITH_EMBREE
#  include <embree4/rtcore_config.h>
#endif
#ifdef BL_WITH_OIDN
#  include <OpenImageDenoise/config.h>
#endif
#ifdef BL_WITH_EIGEN
#  include <Eigen/Core>
#endif
#ifdef BL_WITH_OPENSUBDIV
#  include <opensubdiv/version.h>
#endif
#ifdef BL_WITH_OPENEXR
#  include <OpenEXR/OpenEXRConfig.h>
#endif
#ifdef BL_WITH_OCIO
#  include <OpenColorIO/OpenColorABI.h>
#endif
#ifdef BL_WITH_MESHOPT
#  include "meshopt.h"
#endif
#ifdef BL_WITH_OPENPGL
#  include <openpgl/version.h>
#endif
#ifdef BL_WITH_MANIFOLD
#  include <manifold/version.h>
#endif
#ifdef BL_WITH_JOLT
#  include <Jolt/Jolt.h>
#endif
#ifdef BL_WITH_LIBJPEG
#  include <cstdio>
#  include <jpeglib.h>
#endif
#ifdef BL_WITH_LIBPNG
#  include <png.h>
#  include <zlib.h>
#endif
#ifdef BL_WITH_ZSTD
#  include <zstd.h>
#endif

namespace bl::deps {

#define BL_STR2(x) #x
#define BL_STR(x) BL_STR2(x)

const std::vector<Library> &libraries() {
  static const std::vector<Library> libs = {
#ifdef BL_WITH_TBB
      {"oneTBB", true, TBB_VERSION_STRING,
#else
      {"oneTBB", false, "",
#endif
       "Task scheduler behind parallel_for (work stealing)", "Blendity's own thread pool"},
#ifdef BL_WITH_EMBREE
      {"Embree", true, RTC_VERSION_STRING,
#else
      {"Embree", false, "",
#endif
       "Path tracer ray queries (Cycles' CPU BVH)", "Blendity's two-level SAH BVH"},
#ifdef BL_WITH_OIDN
      {"OpenImageDenoise", true, strprintf("%d.%d.%d", OIDN_VERSION_MAJOR, OIDN_VERSION_MINOR, OIDN_VERSION_PATCH),
#else
      {"OpenImageDenoise", false, "",
#endif
       "AI denoiser for path-traced renders (Cycles' default)", "A-Trous wavelet filter"},
#ifdef BL_WITH_EIGEN
      {"Eigen", true, strprintf("%d.%d.%d", EIGEN_WORLD_VERSION, EIGEN_MAJOR_VERSION, EIGEN_MINOR_VERSION),
#else
      {"Eigen", false, "",
#endif
       "Sparse direct solver for LSCM unwrapping (as in uv_parametrizer.cc)", "Conjugate gradients"},
#ifdef BL_WITH_OPENSUBDIV
      {"OpenSubdiv", true, BL_STR(OPENSUBDIV_VERSION),
#else
      {"OpenSubdiv", false, "",
#endif
       "Catmull-Clark with creases and limit surfaces (Subdivision Surface)", "Blendity's Catmull-Clark"},
#ifdef BL_WITH_OPENEXR
      {"OpenEXR", true, OPENEXR_VERSION_STRING,
#else
      {"OpenEXR", false, "",
#endif
       "Read/write .exr images (HDRIs, float renders)", "Radiance .hdr only"},
#ifdef BL_WITH_OCIO
      {"OpenColorIO", true, OCIO_VERSION,
#else
      {"OpenColorIO", false, "",
#endif
       "Blender's colour management config (AgX, Filmic, Standard...)", "Built-in Standard / Filmic / ACES curves"},
#ifdef BL_WITH_MANIFOLD
      {"Manifold", true, strprintf("%d.%d.%d", MANIFOLD_VERSION_MAJOR, MANIFOLD_VERSION_MINOR, MANIFOLD_VERSION_PATCH),
#else
      {"Manifold", false, "",
#endif
       "Boolean modifier (Blender's Manifold solver)", "Boolean unavailable"},
#ifdef BL_WITH_MESHOPT
      {"meshoptimizer", true, strprintf("%d.%d", MESHOPTIMIZER_VERSION / 1000, (MESHOPTIMIZER_VERSION % 1000) / 10),
#else
      {"meshoptimizer", false, "",
#endif
       "Decimate modifier and vertex-cache optimisation", "Decimate unavailable"},
#ifdef BL_WITH_OPENPGL
      {"OpenPGL", true, strprintf("%d.%d.%d", OPENPGL_VERSION_MAJOR, OPENPGL_VERSION_MINOR, OPENPGL_VERSION_PATCH),
#else
      {"OpenPGL", false, "",
#endif
       "Path guiding in the path tracer (Cycles 'Guiding')", "Unguided BSDF sampling"},
#ifdef BL_WITH_JOLT
      {"Jolt Physics", true, strprintf("%d.%d.%d", JPH_VERSION_MAJOR, JPH_VERSION_MINOR, JPH_VERSION_PATCH),
#else
      {"Jolt Physics", false, "",
#endif
       "Rigidbody simulation in Play mode", "Blendity's sphere physics"},
#ifdef BL_WITH_LIBJPEG
      {"libjpeg-turbo", true, strprintf("%d", JPEG_LIB_VERSION),
#else
      {"libjpeg-turbo", false, "",
#endif
       "SIMD JPEG decoding", "Blendity's baseline JPEG decoder"},
#ifdef BL_WITH_LIBPNG
      {"libpng + zlib", true, strprintf("%s / %s", PNG_LIBPNG_VER_STRING, ZLIB_VERSION),
#else
      {"libpng + zlib", false, "",
#endif
       "PNG decoding", "Blendity's inflate + PNG decoder"},
#ifdef BL_WITH_ZSTD
      {"Zstandard", true, ZSTD_VERSION_STRING,
#else
      {"Zstandard", false, "",
#endif
       "Compressed .scene files (like Blender's .blend compression)", "Uncompressed text scenes"},
  };
  return libs;
}

std::string summary() {
  std::string s;
  for (const Library &l : libraries())
    if (l.enabled) s += (s.empty() ? "" : ", ") + std::string(l.name) + " " + l.version;
  return s.empty() ? "none (dependency-free build)" : s;
}

}  // namespace bl::deps
