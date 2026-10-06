# Licenses

Blendity's own source code is **GPL-2.0-or-later**, the same as Blender (`../LICENSE`).

It also compiles in a few libraries that Blender itself bundles. They are copied unmodified from the Blender tree into `extern/`:

| Library | Used for | License | Blender location |
|---|---|---|---|
| [ufbx](https://github.com/ufbx/ufbx) v0.20.0 | FBX import | MIT (`ufbx-MIT.txt`) | `extern/ufbx` |
| [fast_float](https://github.com/fastfloat/fast_float) 5.0.0 | fast, exact float parsing in the scene loader | MIT (`fast_float-MIT.txt`) | `extern/fast_float` |
| MikkTSpace (Blender's C++ port) | tangents for normal maps | Apache-2.0 (`Apache-2.0.txt`) | `intern/mikktspace` |
| Hosek-Wilkie sky model | physical sky | BSD-3-Clause (`BSD-3-Clause.txt`) | `intern/sky` |

## What this means for the executables

Apache-2.0 code can be combined with GPL version 3 code, but not with GPL version 2 only. Because Blendity's code is "GPL-2.0 **or later**", the combined program is distributed under **GPL-3.0-or-later** (`GPL-3.0.txt`). This is the same situation as Blender's own binaries, which also bundle Apache-2.0 libraries.

In practice:

- You may use, study, change and share Blendity's source and executables.
- If you distribute a modified executable, you must also offer its source under GPL-3.0-or-later and keep these notices.
- The MIT, BSD-3-Clause and Apache-2.0 notices in this folder must ship with any binary distribution.

The scenes, meshes, textures and renders you make with Blendity are yours. The GPL covers the program, not its output.
