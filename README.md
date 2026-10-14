# Blendity

**Blender's modeling, UVs, materials, rendering, rigging and animation, driven like Unity.** Blendity is a 3D editor
written in C++17. Its windows, navigation, shortcuts, gizmos, Inspector and Play mode follow the Unity editor;
underneath, its meshes, Edit Mode tools, UV unwrapping, Principled materials, render engines (an EEVEE-like
rasterizer and a Cycles-like path tracer on the CPU and any Vulkan GPU), skins and IK follow Blender's design and
source code. A built-in **Learn** tab teaches each piece interactively.

![Blendity editor](docs/images/editor.png)

| Path-traced viewport | UV Editor (LSCM unwrap) |
|---|---|
| ![Rendered](docs/images/rendered.png) | ![UV editor](docs/images/uv.png) |
| **Edit Mode: loop cuts** | **Learn tab** |
| ![Edit mode](docs/images/editmode.png) | ![Learn tab](docs/images/learn.png) |

**→ The [manual](docs/MANUAL.md)** walks through the work stage by stage, with examples.

## Quick start

1. Build it (below) or download the build for your system from the GitHub Actions artifacts, then run
   `dist/windows/Blendity.exe` (Windows: no installer), `dist/linux/Blendity` (Linux: needs only `libX11`, with
   `dist/linux/lib/` beside it) or `dist/macos/Blendity.app`.
2. Press **F1** for the Learn tab, or pick a stage from the **Layout** dropdown (top right).
3. Alt+drag orbits, the wheel zooms, W / E / R move, rotate and scale, Tab enters Edit Mode, Ctrl+P plays.

```bash
Blendity --layout Learning      # start with the Learn tab beside the Scene view
Blendity --scene Assets/Scenes/SampleScene.scene
Blendity --scale 1.5            # force the UI scale (default follows the OS DPI)
Blendity --headless-screenshot out.png --cmd "select Cube" --cmd "shading rendered" --frames 30   # no window
```

## What it does, stage by stage

Each stage is a **layout** that arranges the windows and shows only that stage's tools (Show All Tools shows
everything):

- **Reference & Planning** - reference images, physical cameras with depth of field, composition guides, Pilot Camera.
- **Blocking Out** - primitives and parametric shapes, SketchUp-style drawing and **Push/Pull**, Booleans, snapping.
- **Modeling & Topology** - Blender's Edit Mode (extrude, inset, bevel, loop cut, bridge, knife, n-gons, F9), a
  modifier stack (Array, Bevel, Boolean, Mirror, Subdivision, Lattice, Curve, Mesh Deform...), curves and lattices
  edited with the same keys.
- **UV Unwrapping** - a UV Editor with LSCM unwrap, Smart UV Project, projections and island packing.
- **Texturing & Materials** - Principled materials and material assets, layered **procedural textures**, axis
  materials, toon shading, texture baking.
- **Lighting & Rendering** - point, spot, area and sun lights; baked lightmaps, probe volumes and voxel realtime GI;
  F12 rasterized or path-traced renders (Embree, OpenImageDenoise, GPU through Vulkan); camera filters from colour
  grading to PlayStation and manga looks.
- **Rigging**, and **Skinning & Weight Painting** - bones, skins, weight paint and IK in a Rigging window; **automatic
  weights and weight transfer by inpainting** (Epic, 2023); a **Mixamo-style marker auto-rig** for people and
  four-legged animals; rigged FBX / glTF import with bind poses.
- **Animation** - a **Unity-style dopesheet** and **Curves view** (key tangents: Clamped Auto, Free Smooth, Flat,
  Broken) with Record and Preview, the Animation component for Play, and a **Premiere-style Sequencer**: clips laid out
  on tracks crossfade where they overlap, and keys jump between them in Play.

Files: import and export OBJ, FBX, glTF, STL, PLY and USD, checked both ways against Blender 5.2. The keymap starts
from Unity, Unreal, Blender, Maya, 3ds Max or SketchUp, and **Edit > Editor Feel** switches the keys and the Scene view's
mouse navigation together to Unity's, Unreal's, Blender's or Maya's.

## Machines

| | Minimum | Recommended |
|---|---|---|
| CPU | 4 cores / 8 threads, about 3 GHz | 8+ cores |
| RAM | 8 GB | 16 GB+ |
| GPU | GTX 1050 Ti class (Vulkan 1.2; for path tracing, baking and realtime GI) | a GPU with ray tracing hardware (RTX, RX 6000+, Arc) |

The minimum machine starts on the **Low** quality tier (Edit > Preferences..., Quality): the views draw at 67 % and scale
up, with a 1024 shadow map and 60 fps. Renders always use the scene's own settings.

## Build it

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

**Optional: Blender's prebuilt libraries** (from `projects.blender.org/blender/lib-windows_x64`, `lib-linux_x64` or
`lib-macos_arm64`) at `../blender/lib/<platform>` add Embree, OpenImageDenoise, oneTBB, Eigen,
OpenSubdiv, OpenEXR, OpenColorIO, Manifold, Open PGL, Jolt, Vulkan + shaderc and others; each has a built-in fallback,
and `BLENDITY_NO_LIBS=1` (CMake: `-DBLENDITY_NO_LIBS=ON`) forces the dependency-free build. Type `libs` in the Console to see what's active.

Each build produces **`Blendity`**, **`blendity_tests`** (9795 unit checks in 643 tests with Blender's libraries; the
dependency-free build skips the library ones) and **`blendity_stress`** (the stress and efficiency suite). GitHub
Actions builds and tests Windows, Linux and macOS, and runs the tests under AddressSanitizer / UBSan. macOS is built
and tested only on CI (no Mac here).

## Documentation

- **[docs/MANUAL.md](docs/MANUAL.md)** - the user manual, by workflow, with examples and use cases.
- **The Learn tab** (F1) - interactive lessons with Try-it buttons, linking Unity, Blender's source and the graphics
  books (*Fundamentals of Computer Graphics*, *Game Engine Architecture*), and the papers behind the features.
- **For developers:** [ARCHITECTURE.md](docs/ARCHITECTURE.md) (each part mapped to Blender's source),
  [decisions/](docs/decisions/) (ADRs), [tasks/](docs/tasks/) (specs), [PERFORMANCE.md](docs/PERFORMANCE.md),
  [USABILITY.md](docs/USABILITY.md), [requirements.md](docs/requirements.md), [CHANGELOG.md](CHANGELOG.md), and
  [CLAUDE.md](CLAUDE.md) (engineering
  standards). Work starts as a task spec, is reviewed and tested, and lands through a pull request that CI must pass.

## Why not a Blender fork?

Blender's full build needs about 40 external libraries and a large build system. Blendity re-implements the parts it
needs in plain C++, documents which Blender source files each corresponds to, and links Blender's prebuilt libraries
where they matter, each measured against Blendity's own fallback. Not recreated: sculpting, node editors, geometry
nodes, grease pencil, the compositor and sequencer, Python, and `.blend` files.

## License

Source: GPL-2.0-or-later, the same as Blender (`LICENSE`). The executables also contain MIT, BSD-3-Clause and
Apache-2.0 code from Blender's bundled libraries and, when built with Blender's prebuilt libraries, code under
Apache-2.0, BSD, MIT, MPL-2.0, Zlib, libpng and TOST licenses, so **binaries are distributed under GPL-3.0-or-later**.
Details in [licenses/README.md](licenses/README.md).
