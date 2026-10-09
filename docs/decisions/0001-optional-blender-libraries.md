# 0001: Blender's libraries are optional, with dependency-free fallbacks

- **Status:** accepted (recorded 2026-10-09; decided in phases 2-3, 2026-10-06)
- **Requirements:** C-01, C-02, R-05

## Context
Blendity re-implements Blender's subsystems in plain C++ so it builds with nothing but a compiler. For
speed and fidelity the user approved using the libraries Blender itself ships prebuilt (Embree, OIDN,
TBB, Eigen, OpenSubdiv, OpenEXR, OpenColorIO, Manifold, meshoptimizer, OpenPGL, Jolt, libjpeg, libpng,
zlib, zstd, Vulkan / shaderc) from `blender/lib/<platform>`.

## Decision
- Each library is detected by `build.bat`, `build.sh` and `CMakeLists.txt` and enabled with a
  `BL_WITH_<NAME>` define. Code using it sits behind that define, with Blendity's own implementation (or
  the feature simply not offered) otherwise.
- `BLENDITY_NO_LIBS=1` forces the dependency-free build. CI builds that variant on all three platforms.
- No library outside what Blender bundles. Small code copied from Blender's tree lives in `extern/`.

## Consequences
- Anyone can build and run Blendity with only a compiler; CI needs no downloads.
- Two code paths per accelerated feature: both must be tested, and results may differ slightly (tests
  compare with tolerances).
- Platform-specific ABI details (e.g. Jolt's defines differ per platform) live in the build scripts.
- Where a fallback is cheap, both builds take it so they behave the same: Push Through leaving through
  several faces in one plane cuts the outline into them itself (task 0003); only exits across
  non-coplanar faces still need Manifold.
