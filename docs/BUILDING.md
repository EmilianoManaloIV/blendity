# Building Blendity from source

You only need a compiler; Blender's small bundled libraries are in `extern/`.

```bash
build.bat release all            # Windows: finds Visual Studio / Build Tools automatically
```
```bash
./build.sh release all           # Linux (g++ + libx11-dev) and macOS (Xcode command line tools)
```
```bash
cmake -B build && cmake --build build --parallel && ctest --test-dir build   # optional CMake route
```

**Blender's prebuilt libraries** (the installers include them) come with one command, which fetches only the folders
Blendity uses, at the commits pinned in `packaging/blender-libs.txt` (about 600 MB, needs git-lfs):

```bash
bash packaging/fetch_blender_libs.sh windows_x64     # or linux_x64, macos_arm64; then build as above
```

They land in `../blender/lib/<platform>` (Blender's own layout, from `projects.blender.org/blender/lib-<platform>`) and
add Embree, OpenImageDenoise, oneTBB, Eigen,
OpenSubdiv, OpenEXR, OpenColorIO, Manifold, Open PGL, Jolt, Vulkan + shaderc, FFmpeg and others; each has a built-in fallback,
and `BLENDITY_NO_LIBS=1` (CMake: `-DBLENDITY_NO_LIBS=ON`) forces the dependency-free build. Type `libs` in the Console to see what's active.
On Windows that build also uses oneTBB's memory allocator (ADR 0112; `BLENDITY_ALLOCATOR=system` builds without it;
Linux opts in with `BLENDITY_ALLOCATOR=tbbmalloc`). Developers can add Tracy's profiler: fetch it with
`packaging/fetch_blender_libs.sh <platform> --extras`, then build with `BLENDITY_TRACY=1`.

Each build produces **`Blendity`**, **`blendity_tests`** (the unit checks: the [README](../README.md#build-from-source)
holds their count; the dependency-free build skips the library ones) and **`blendity_stress`** (the stress and
efficiency suite).
`py tools/run_suites.py <blendity_tests> [<another build's>]` runs every test group as its own process (both
Windows builds in about a minute and a half). GitHub
Actions builds and tests Windows, Linux and macOS, and runs the tests under AddressSanitizer / UBSan (in six shards at once; a pull request that changes only specs, ADRs or changelog fragments builds nothing - round 492). macOS is built
and tested only on CI (no Mac here).
A build runs from where it lands: `dist/windows/Blendity.exe`, `dist/linux/Blendity` (with `dist/linux/lib/` beside
it) or `dist/macos/Blendity.app`.

## Incremental Windows developer builds

For repeated local release builds, use an x64 Visual Studio installation with MSVC 14.51 and Python 3.11 or later. Set the following variables in a local command prompt, then build the editor. An external worktree also needs `BLENDITY_LIBDIR` set to the same Blender library directory as the clean reference.

```bat
set BLENDITY_VCVARS_VER=14.51
set BLENDITY_VCVARS_STRICT=1
set BLENDITY_INCREMENTAL=1
build.bat release app
```

Use `tests` for the unit-test program or `app-tests` to stage both programs together. The last `[blendity] Package:` line names the directory containing the requested executables, runtime libraries, licenses and applicable shaders. Launch the executable in that directory. Build failures leave the previous successful package available.

The cache defaults to `build\incremental` inside the worktree. Set `BLENDITY_INCREMENTAL_DIR` to a dedicated scratch directory for a different location. Reuse the same worktree and cache across revisions. Builds serialize writes to the cache. Old executable packages can stay open while a new package is prepared. After all programs using a retired cache have closed, its owner can remove that dedicated cache directory. Clear `BLENDITY_INCREMENTAL` with `set BLENDITY_INCREMENTAL=` to use the stock build.

## Why not a Blender fork?

Blender's full build needs about 40 external libraries and a large build system. Blendity re-implements the parts it
needs in plain C++, documents which Blender source files each corresponds to, and links Blender's prebuilt libraries
where they matter, each measured against Blendity's own fallback.
