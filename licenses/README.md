# Licenses

Blendity's own source code is **GPL-2.0-or-later**, the same as Blender (`../LICENSE`).

It also compiles in a few libraries that Blender itself bundles. They are copied unmodified from the Blender tree into `extern/`:

| Library | Used for | License | Blender location |
|---|---|---|---|
| [ufbx](https://github.com/ufbx/ufbx) v0.20.0 | FBX import | MIT (`ufbx-MIT.txt`) | `extern/ufbx` |
| [fast_float](https://github.com/fastfloat/fast_float) 5.0.0 | fast, exact float parsing in the scene loader | MIT (`fast_float-MIT.txt`) | `extern/fast_float` |
| MikkTSpace (Blender's C++ port) | tangents for normal maps | Apache-2.0 (`Apache-2.0.txt`) | `intern/mikktspace` |
| Hosek-Wilkie sky model | physical sky | BSD-3-Clause (`BSD-3-Clause.txt`) | `intern/sky` |

## Blender's prebuilt libraries (optional)

When `blender/lib/<platform>` exists next to this project, the build scripts link Blender's own prebuilt libraries and copy their runtime files next to the executables. Those are the DLLs in `dist/windows`, `dist/linux/lib/*.so*` and the OpenColorIO config in `datafiles/`. Builds without that folder (or with `BLENDITY_NO_LIBS=1`) contain none of this code. Versions and licenses come from Blender's `build_files/build_environment/cmake/versions.cmake`. The license texts are copied from Blender's `release/license/`.

| Library | Version | Used for | License |
|---|---|---|---|
| [Embree](https://github.com/RenderKit/embree) | 4.4.1 | path tracer ray casting | Apache-2.0 (`Apache-2.0.txt`) |
| [OpenImageDenoise](https://www.openimagedenoise.org/) | 2.5.0 | path tracer denoising | Apache-2.0 |
| [oneTBB](https://github.com/uxlfoundation/oneTBB) | 2022.3.0 | job system | Apache-2.0 |
| [Eigen](http://eigen.tuxfamily.org) | (commit 8a1083e) | LSCM sparse solver (header-only) | MPL-2.0 (`MPL-2.0.txt`) |
| [OpenSubdiv](https://graphics.pixar.com/opensubdiv) | 3.7.0 | optional Catmull-Clark backend | TOST-1.0, Apache-2.0 based (`TOST-1.0.txt`) |
| [OpenEXR](https://github.com/AcademySoftwareFoundation/openexr) + [Imath](https://github.com/AcademySoftwareFoundation/Imath) | 3.4.10 / 3.2.2 | EXR read/write | BSD-3-Clause (`BSD-3-Clause.txt`) |
| [OpenJPH](https://github.com/aous72/OpenJPH) | 0.25.2 | runtime dependency of OpenEXR | BSD-2-Clause (`BSD-2-Clause.txt`) |
| [OpenColorIO](https://github.com/AcademySoftwareFoundation/OpenColorIO) | 2.5.0 | AgX / Filmic / other view transforms | BSD-3-Clause |
| [Manifold](https://github.com/elalish/manifold) | 3.5.2 | Boolean modifier | Apache-2.0 |
| [meshoptimizer](https://meshoptimizer.org) | 1.1 | Decimate modifier | MIT (`MIT.txt`) |
| [Open PGL](http://www.openpgl.org/) | 0.7.1 | path guiding | Apache-2.0 |
| [Jolt Physics](https://github.com/jrouwe/JoltPhysics) | 5.6.0 | Play-mode rigid bodies | MIT |
| [libjpeg-turbo](https://github.com/libjpeg-turbo/libjpeg-turbo/) | 2.1.3 | JPEG decoding | BSD-3-Clause (and the IJG terms in its notices) |
| [libpng](http://www.libpng.org/pub/png/libpng.html) | 1.6.58 | PNG decoding | libpng-2.0 (`libpng-2.0.txt`) |
| [zlib](https://zlib.net) | 1.3.1 | PNG compression | Zlib (`Zlib.txt`) |
| [Zstandard](https://github.com/facebook/zstd) | 1.5.7 | compressed `.scene` files | BSD-3-Clause |
| [DPC++ runtime](https://github.com/intel/llvm) | 7.1.0 | runtime dependency of Embree's build (SYCL) | Apache-2.0 with LLVM exception (`LLVM-exception.txt`) |

All of these are permissive or weak-copyleft licenses that are compatible with GPL-3.0. MPL-2.0 (Eigen) only asks that changes to Eigen's own files stay available; Blendity doesn't modify them. A binary build that includes them is therefore also distributed under GPL-3.0-or-later, as Blender's is.

## What this means for the executables

Apache-2.0 code can be combined with GPL version 3 code, but not with GPL version 2 only. Because Blendity's code is "GPL-2.0 **or later**", the combined program is distributed under **GPL-3.0-or-later** (`GPL-3.0.txt`). This is the same situation as Blender's own binaries, which also bundle Apache-2.0 libraries.

In practice:

- You may use, study, change and share Blendity's source and executables.
- If you distribute a modified executable, you must also offer its source under GPL-3.0-or-later and keep these notices.
- The license texts and notices in this folder must ship with any binary distribution.

The scenes, meshes, textures and renders you make with Blendity are yours. The GPL covers the program, not its output.
