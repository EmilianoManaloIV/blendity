# Blendity

**Blender's modeling, UV, texturing and rendering, driven like Unity.** Blendity is a 3D editor written in C++17. Its windows, navigation, shortcuts, gizmos, Inspector and Play mode follow the Unity editor. Underneath, its meshes, Edit Mode tools, UV unwrapping, Principled materials and two render engines (an EEVEE-like rasterizer and a Cycles-like path tracer) follow Blender's design and source code. A built-in **Learn** tab teaches how each piece maps across Unity, Blender and the graphics theory in the reference books.

![Blendity editor](docs/images/editor.png)

| Path-traced viewport | UV Editor (LSCM unwrap) |
|---|---|
| ![Rendered](docs/images/rendered.png) | ![UV editor](docs/images/uv.png) |
| **Edit Mode: loop cuts** | **Learn tab** |
| ![Edit mode](docs/images/editmode.png) | ![Learn tab](docs/images/learn.png) |

---

## Run it

| Platform | Executable | Status |
|---|---|---|
| **Windows** 10/11 x64 | `dist/windows/Blendity.exe`, with Blender's library DLLs beside it (the dependency-free build is one static file). No installer | Built and tested here (real window + headless) |
| **Linux** x64 (X11 / XWayland) | `dist/linux/Blendity` + `dist/linux/lib/`: needs only `libX11` from the system | Built and tested here (WSL Ubuntu) |
| **macOS** (Apple Silicon / Intel) | `dist/macos/Blendity.app` via `./build.sh` or the CI artifact | ⚠️ Written but **not compiled or run here** (no Mac available). The GitHub Actions workflow builds and tests it on `macos-latest` |

Double-click the executable, or run it from a terminal:

```bash
Blendity --layout Learning      # start with the Learn tab beside the Scene view
Blendity --scene Assets/Scenes/SampleScene.scene
Blendity --scale 1.5            # force UI scale (default follows OS DPI)
Blendity --headless-screenshot out.png --cmd "select Cube" --cmd "shading rendered" --frames 30   # no window
```

New to the editor? Press **F1** (or the orange **Learn** button).

## Build it

You only need a compiler. Blender's own small bundled libraries are already in `extern/`.

**Optional: Blender's prebuilt libraries.** Put Blender's library folder for your platform at `../blender/lib/<platform>` (from `projects.blender.org/blender/lib-windows_x64`, `lib-linux_x64` or `lib-macos_arm64`). The build then picks up Embree, OpenImageDenoise, oneTBB, Eigen, OpenSubdiv, OpenEXR, OpenColorIO, Manifold, meshoptimizer, Open PGL, Jolt, libjpeg-turbo, libpng, zlib and Zstandard, and copies their runtime files next to the executables. Each one is optional and has a built-in fallback. `BLENDITY_NO_LIBS=1` (or `-DBLENDITY_NO_LIBS=ON`) forces the dependency-free build. Type `libs` in the Console to see what's active.

```bash
build.bat release all            # Windows: finds Visual Studio / Build Tools automatically
```
```bash
./build.sh release all           # Linux (g++ + libx11-dev) and macOS (Xcode command line tools)
```
```bash
cmake -B build && cmake --build build --parallel && ctest --test-dir build   # optional CMake route
```

Each build produces **`Blendity`** (the editor), **`blendity_tests`** (430 unit checks; 358 in the dependency-free build) and **`blendity_stress`** (the stress and efficiency suite). Pushing to GitHub runs `.github/workflows/build.yml`, which builds, tests and uploads binaries for all three operating systems.

## What you can do

**Model (Blender's Edit Mode, Unity's handles).** Primitives (cube, UV/ico sphere, cylinder, cone, torus, plane, quad). Vertex, edge and face selection, box select, edge loops, gizmo editing with **proportional editing**. Extrude, inset, **loop cut** (any number of cuts, slide), **fill**, **merge at center**, **recalculate normals**, delete, subdivide, smooth, triangulate, merge by distance. Non-destructive modifier components: **Mirror, Array, Solidify**, Subdivision Surface, Smooth, plus **Boolean** (Manifold) and **Decimate** (meshoptimizer) with Blender's libraries.

**UV map.** A UV Editor window (Ctrl+9) with move/rotate/scale, box select, linked select, snapping, a stretch overlay and background images. **Unwrap (LSCM)** with seams, **Smart UV Project**, cube / cylinder / sphere / view projections, reset, **pack islands** (with rotation) and average island scale.

**Texture.** Unity-style Materials evaluating Blender's **Principled BSDF**: base colour, metallic, roughness, specular, normal and emission maps with tiling/offset, UV / box / generated mapping, procedural checker, noise and Blender's UV Grid / Color Grid test images. Multiple material slots per mesh, assigned per face. PNG, JPEG, TGA, BMP, Radiance HDR and (with Blender's libraries) OpenEXR images, with mipmaps and trilinear filtering. Drag and drop images, **OBJ + MTL** and **FBX** files (via ufbx, which Blender bundles) and their textures come along.

**Render.** Four Scene view modes: Wireframe, Solid, **Shaded** (deferred PBR with sun shadow maps and image-based lighting) and **Rendered** (progressive path tracing). World: gradient, **Hosek-Wilkie physical sky**, **HDRI** or flat colour. View transforms: Standard, Filmic, ACES, plus exposure, and with OpenColorIO **Blender's own views (AgX, the default, Filmic, Khronos PBR Neutral…)**. **F12** renders the Main Camera with either engine (rasterized with supersampling, or path traced with denoising) to PNG, JPEG, HDR or EXR. With Blender's libraries the path tracer runs on **Embree**, denoises with **OpenImageDenoise**, samples emissive meshes as lights, and offers Cycles-style **path guiding** (Open PGL).

**Scene and play.** GameObjects with Transform hierarchies and Components (MeshFilter, MeshRenderer, Light, Camera, Rotator, Oscillator, PlayerController, Rigidbody). Play mode copies the scene and restores it on Stop, as in Unity; rigid bodies use **Jolt Physics** when available. Undo/redo, a text `.scene` format, OBJ export, PNG screenshots (Shift+F12).

## Unity controls, Blender tools

| | Blendity (Unity) | Blender |
|---|---|---|
| Orbit / Pan / Zoom | Alt+LMB / MMB / Wheel or Alt+RMB | MMB / Shift+MMB / Wheel |
| Fly | Hold RMB + WASD, Q/E, Shift | Shift+` |
| Frame selection | F | Numpad . |
| View / Move / Rotate / Scale / Transform | Q / W / E / R / Y | — / G / R / S / Transform tool |
| Duplicate / Delete / Rename | Ctrl+D / Del / F2 | Shift+D / X / F2 |
| Play / Pause / Step | Ctrl+P / Ctrl+Shift+P / Ctrl+Alt+P | — |
| Edit Mode / vertex / edge / face | Tab / 1 / 2 / 3 | Tab / 1 / 2 / 3 |
| Extrude / Inset | Ctrl+E / Ctrl+I | E / I |
| Select edge loop | double-click an edge | Alt+click |
| Loop cut | Ctrl+R over an edge | Ctrl+R |
| Fill / Merge at center / Recalculate normals | Alt+F / Alt+M / Shift+N | F / M / Shift+N |
| Proportional editing | O (scroll while dragging to resize) | O |
| UV Editor / Unwrap | Ctrl+9 / U | UV Editing workspace / U |
| Render image / Render window | F12 / F11 | F12 / F11 |
| Snap while dragging | hold Ctrl | hold Ctrl |
| Windows | Ctrl+1 Scene, 2 Game, 3 Inspector, 4 Hierarchy, 5 Project, 6 Console, 7 Profiler, 8 Research, 9 UV Editor | — |

**Windows:** Scene, Game, Hierarchy, Inspector, Project, Console (with a command line; type `help`), **UV Editor**, **Render**, **Learn**, **Research** and **Profiler**. Drag tabs to dock or split them; Window → Layouts has the Default, 2 by 3, Tall, Wide and Learning presets.

## The Learn tab

24 lessons, each linking the **Unity concept ↔ Blender concept and source file ↔ theory**, with chapter citations from the books in *Blender Documents*:

- **[FoCG]** Marschner & Shirley, *Fundamentals of Computer Graphics*, 5th ed.
- **[GEA1]** / **[GEA2]** Gregory, *Game Engine Architecture* Vol. I and II

They cover the editor layout, navigation, GameObjects vs Objects, scene graphs, transforms, handedness, mesh storage, Edit Mode, modifiers, cameras, the rasterizer, lighting, the game loop, files, undo, performance and research, **UV Mapping**, **Materials and Textures**, **Render Engines**, **Light, Sky and Tone Mapping**, **Advanced Modeling Tools**, and new in this release **Libraries and Performance** (Blender's external libraries, profiling, SIMD and multithreading). Lessons have "Try it" buttons that drive the editor, reveal-on-click self-checks and links into the matching folder of the Blender source tree. The text is my own wording; the books are cited, not reproduced.

## Your research papers

Drop papers onto the editor window or into **`research/papers/`**. They appear in the **Research** tab. Then ask Claude Code to implement a technique; features register in `src/research/` and show up in the Research tab, the Mesh menu and the Inspector. A worked example is included: **Taubin's λ|μ smoothing** (SIGGRAPH '95). See [`research/README.md`](research/README.md) and [`research/FEATURES.md`](research/FEATURES.md).

## Stress tests and efficiencies

`blendity_stress` pushes every subsystem until it breaks a time budget and compares naive and optimized implementations. Highlights from this machine (Ryzen 9 5900XT, 32 threads):

- **≈5.2M triangles at 60 FPS** in the software rasterizer (1280×720, measured on an idle machine; about half that under background load), after this pass's triangle-setup rework: an AVX2 kernel rejects 8 triangles per step, triangles that cover no pixel centre are dropped, and only vertices of surviving triangles are lit. 21M triangles went **174 → 34 ms** in a same-session A/B
- **Display encoding 15.7× faster** on one core (SSE4.1 / AVX2 kernels chosen at run time, plus an exact sRGB table instead of `pow()`)
- Path tracer: **22–47 Mrays/s** on Blendity's own BVH, **64–95 Mrays/s with Embree** (2–2.9×). Earlier releases quoted 70–240 Mrays/s from an approximate ray counter; rays are now counted exactly. A **two-level (instanced) BVH** made rebuilding a 21M-triangle scene **34 s → 16 ms**
- Blender's libraries vs Blendity's fallbacks: TBB dispatch 6.5×, Jolt 13.9× at 2,000 bodies, libjpeg-turbo 3.2×, OpenImageDenoise 37% lower error than À-Trous, zstd scenes 51× smaller. Sampling emissive meshes as lights made path tracing 12.7× more efficient in a lit-doorway scene. **OpenSubdiv and path guiding were slower** here, so both stay opt-in
- Edit-mode tools on a 1M-face mesh: **edge loop 46×**, loop cut 8×, recalculate normals 7× faster after replacing a hash map with a CSR adjacency table; LSCM unwrap 10× faster (and Eigen's direct solver another 1.2–1.8×)
- Earlier passes: edge table 8.5×, scene save 13×, mesh load 3.6×, Catmull-Clark 2.2×, copy-on-write undo 1,500×

Full results, including what didn't help, are in [`docs/PERFORMANCE.md`](docs/PERFORMANCE.md).

## Project layout

```
blendity/
├── src/            engine + editor (docs/ARCHITECTURE.md maps each part to Blender's source)
│   ├── image/      PNG/JPEG/TGA/BMP/HDR codecs (+ OpenEXR, libpng, libjpeg-turbo), mipmapped textures
│   ├── render/     rasterizer, deferred shading, shadows, sky, path tracer, SIMD display encoding, OCIO
│   ├── scene/      meshes + operators, UV tools, materials, import, scene IO, Boolean / Decimate, physics
│   ├── editor/     Unity-style editor, UV Editor, Render window, Learn tab
│   └── deps/       which of Blender's optional libraries were compiled in
├── extern/         small libraries Blender bundles (ufbx, fast_float, MikkTSpace, Hosek-Wilkie sky)
├── stress/         stress suite + results/
├── tests/          unit tests
├── Assets/         your project's scenes, meshes and textures (the Project window)
├── research/       papers/ drop folder, FEATURES.md tracker
├── docs/           ARCHITECTURE.md, PERFORMANCE.md, images/
├── licenses/       license texts and what they mean for the executables
└── build.bat · build.sh · CMakeLists.txt · .github/workflows/build.yml
```

## Why not a Blender fork?

Blender's full build needs about 40 external libraries (Python, OpenImageIO, Boost, OSL, USD…) and a large build system. Blendity re-implements the subsystems you asked for in plain C++ and documents which Blender source files each part corresponds to, so it builds with nothing but a compiler. Where Blender's prebuilt libraries are available, it links the 16 that matter for these features (ray tracing, denoising, threading, colour management, booleans, physics, image formats) instead of reimplementing them. Each comes with a measured comparison against Blendity's own fallback. Not recreated: sculpting, animation and rigging, node editors, geometry nodes, grease pencil, the compositor and sequencer, Python, and `.blend` files. See [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).

## License

Source: GPL-2.0-or-later, the same as Blender (`LICENSE`). The executables also contain MIT, BSD-3-Clause and Apache-2.0 code from Blender's bundled libraries, and, when built with Blender's prebuilt libraries, code under Apache-2.0, BSD, MIT, MPL-2.0, Zlib, libpng and TOST licenses. So **binaries are distributed under GPL-3.0-or-later**. The license texts are copied next to the executables. Details in [`licenses/README.md`](licenses/README.md).
