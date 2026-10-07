# Architecture and Blender mapping

Blendity rebuilds the parts of Blender you use for modeling, UV mapping, texturing and rendering, and wraps them in Unity's editor workflow. It isn't a fork. Each subsystem is re-implemented in C++, following Blender's design, so the editor builds and runs with nothing but a compiler. When Blender's prebuilt libraries are present (`blender/lib/<platform>`), Blendity also links the same ones Blender uses (Embree, OpenImageDenoise, TBB, OpenColorIO and more, see below), each behind a `BL_WITH_*` switch with Blendity's own code as the fallback. The matching Blender source is listed for every module. Paths are relative to the `blender/` folder that sits next to this project.

```
src/
├── core/       math (Unity conventions), logging, filesystem, job system (TBB or own pool),
│               CPU feature detection for SIMD dispatch
├── platform/   one window + input: Win32 / X11 / Cocoa          (Blender: intern/ghost)
├── image/      PNG/JPEG/TGA/BMP/HDR decoding, JPEG/HDR encoding, mipmapped textures
├── render/     2D canvas, fonts, rasterizer (Gouraud / deferred PBR / depth), shading,
│               SIMD display encoding, OpenColorIO views,
│               shadow maps, Hosek-Wilkie sky, path tracer
├── scene/      Mesh (Blender layout) + operators, UV tools, materials, model import,
│               GameObject/Component scene, scene IO
├── editor/     IMGUI toolkit, docking, Scene/Game views, UV Editor, Render window,
│               asset handling, panels, Learn tab
├── research/   paper-derived features (registry)
├── deps/       registry of the optional Blender libraries (the Console's `libs` command)
└── app/        main()
extern/         ufbx, fast_float, MikkTSpace, Hosek-Wilkie sky (copied from Blender's tree)
stress/         blendity_stress  - limits & naive-vs-optimised comparisons
tests/          blendity_tests   - 554 checks with Blender's libraries
```

## Libraries borrowed from Blender's tree

| Library | Blender location | Used in Blendity for | License |
|---|---|---|---|
| ufbx | `extern/ufbx` | FBX import (`scene/import.cpp`) | MIT |
| fast_float | `extern/fast_float` | exact float parsing in the scene loader | MIT |
| MikkTSpace (C++ port) | `intern/mikktspace` | tangents for normal maps (`Mesh::render_mesh_tangents`) | Apache-2.0 |
| Hosek-Wilkie sky | `intern/sky` | physical sky texture (`render/sky.cpp`, ported from Cycles' `kernel/svm/sky.h`) | BSD-3-Clause |

Everything else (image codecs, zlib inflate, JPEG Huffman coding, the rasterizer, the path tracer, LSCM, packing) is written here, so nothing else needs installing. See `licenses/README.md` for what the Apache-2.0 part means for the executables.

## Blender's prebuilt libraries (phase 3, optional)

Blender's full build uses about 40 external libraries, prebuilt per platform at `projects.blender.org/blender/lib-<platform>`. If that folder is at `blender/lib/<platform>`, `build.bat`, `build.sh` and `CMakeLists.txt` detect each library and define `BL_WITH_<NAME>`. They also link it, copy its runtime files next to the executables, and switch to the shared C++ runtime. With `BLENDITY_NO_LIBS=1`, or without the folder, you get the dependency-free build. Every feature below has a fallback or simply isn't offered. `src/deps/deps.cpp` lists what was compiled in, and the Console command `libs` prints it.

| Library | `BL_WITH_` | Blendity code | Fallback | Blender uses it in |
|---|---|---|---|---|
| Embree 4 | `EMBREE` | `render/pathtracer.cpp` (`build_embree`): instanced two-level scene, BLAS cached by mesh hash, built in parallel | own SAH BVH | `intern/cycles/bvh/embree.cpp` |
| OpenImageDenoise 2 | `OIDN` | `PathTracer::denoised_oidn` (beauty + albedo + normal) | À-Trous filter | `intern/cycles/integrator/denoiser_oidn.cpp` |
| Open PGL | `OPENPGL` | path guiding: field trained per render, product-MIS with the BSDF (opt-in) | off | `intern/cycles/integrator/guiding.h` |
| oneTBB | `TBB` | `core/jobs.cpp` backend (`tbb::parallel_for` in a `task_arena`) | own thread pool | `source/blender/blenlib/BLI_task.hh` |
| Eigen | `EIGEN` | `scene/uv.cpp` LSCM: `SimplicialLDLT` direct solve | block-Jacobi CG | `intern/slim`, `intern/itasc` |
| OpenSubdiv | `OPENSUBDIV` | `scene/subdiv_osd.cpp` (selectable, off by default) | own Catmull-Clark | `intern/opensubdiv` |
| OpenColorIO | `OCIO` | `render/colormanagement.cpp`: Blender's config, views baked into 97³ tetrahedral LUTs over a log2 shaper | Standard / Filmic / ACES curves | `source/blender/imbuf/intern/colormanagement.cc` |
| Manifold | `MANIFOLD` | `scene/boolean.cpp`, `BooleanModifier` (n-gons rebuilt by face id) | (no Boolean) | `source/blender/geometry/intern/mesh_boolean_manifold.cc` |
| meshoptimizer | `MESHOPT` | `scene/decimate.cpp`, `DecimateModifier` (attribute-aware, per material) | (no Decimate) | Blender's library set |
| Jolt Physics | `JOLT` | `scene/physics_jolt.cpp`: Play-mode rigid bodies (convex hulls, static mesh colliders) | O(n²) sphere physics | Blender's library set |
| OpenEXR + Imath | `OPENEXR` | `image/image_libs.cpp`: EXR read, EXR render output (half/float, ZIP) | (no EXR) | `source/blender/imbuf/intern/oiio` |
| libjpeg-turbo, libpng + zlib | `LIBJPEG`, `LIBPNG` | decoding; zlib for the PNG writer (adaptive filters) | own decoders / deflate | `source/blender/imbuf` |
| Zstandard | `ZSTD` | `scene/scene_io.cpp`: compressed `.scene` (detected by magic on load) | plain text | `.blend` compression |

**ABI settings.** The libraries were built with specific defines that Blendity must match (`blender/build_files/build_environment/cmake`, and each library's exported `*Config.cmake`): `IMATH_DLL`/`OPENEXR_DLL` on Windows, `MANIFOLD_PAR=1`, and Jolt's `JPH_DOUBLE_PRECISION`, `JPH_CROSS_PLATFORM_DETERMINISTIC`, `JPH_OBJECT_STREAM`, `JPH_USE_SSE4_x`. `JPH_FLOATING_POINT_EXCEPTIONS_ENABLED` is set **only on Windows**, because Jolt enables it only for MSVC builds. A mismatch makes Jolt's version check abort at start-up. Runtime-only dependencies are deployed too: OpenJPH (for OpenEXR) and Intel's SYCL runtime (for Embree).

## Hand-written SIMD (phase 3)

The baseline target is plain x86-64 (Linux builds with Blender's libraries use SSE4.2, as Blender does). Wider kernels are chosen at run time from `core/cpu.cpp` (CPUID + XGETBV), the way Cycles keeps SSE4.1 and AVX2 kernel variants (`intern/cycles/util/simd.h`). Each function is compiled for its own target (`__attribute__((target))` on GCC/Clang; MSVC needs no flag), so the executables still run on older CPUs. Other architectures use the scalar code.

| Kernel | What it does | Check |
|---|---|---|
| `render/display.cpp` | HDR → view transform → 8-bit sRGB, 4 px (SSE4.1) or 8 px (AVX2 + FMA) per step; the sRGB curve is an exact table (exponent + 7 mantissa bits → value + rounding threshold) | unit test: correctly rounded on 17M floats; kernels agree within one step |
| `render/raster.cpp` `needs_scalar8_avx2` | Rejects 8 triangles at a time in setup (back-face, off-screen, no pixel centre, beyond far plane) with gathers and vector compares; survivors go through the scalar path in order | unit test: identical colour, depth and id buffers with `RasterOptions::fast_setup` on and off |

## Module map

| Blendity | What it does | Blender source it mirrors | Unity equivalent |
|---|---|---|---|
| `platform/platform_win32.cpp`, `platform_x11.cpp`, `platform_cocoa.mm` | Window, input, drag & drop, clipboard, DPI | `intern/ghost/intern/GHOST_SystemWin32.cc`, `GHOST_SystemX11.cc`, `GHOST_SystemCocoa.mm` | Editor platform layer |
| `core/math.h` | Vectors, quaternions, column-major matrices | `source/blender/blenlib/BLI_math_matrix.hh` | `Vector3`, `Quaternion`, `Matrix4x4` (left-handed, Y-up, ZXY Euler) |
| `core/jobs.*` | `parallel_for`: oneTBB when available, else its own thread pool | `source/blender/blenlib/BLI_task.hh` (TBB) | Job System |
| `render/canvas.*` | UI drawing, anti-aliased TrueType text from OS fonts | `source/blender/blenfont/intern/blf_glyph.cc` (FreeType) | IMGUI drawing |
| `render/raster.*` | Tile-binned multithreaded rasterizer, z-buffer, ID buffer, outlines | `source/blender/draw/intern/draw_manager.cc`, `draw/engines/overlay`, `gpu/intern/gpu_select.cc` | Built-in render pipeline |
| `scene/mesh.*` | `positions` + `face_offsets` + `corner_verts` (n-gons), render cache | `makesdna/DNA_mesh_types.h`, `blenkernel/BKE_mesh.hh`, `blenkernel/intern/mesh_normals.cc` | `Mesh` (triangles only) |
| `mesh_make_mutable` | Copy-on-write sharing | `blenlib/BLI_implicit_sharing.hh` | — |
| `primitives::*` | Cube, spheres, cylinder, cone, torus, plane, quad | `bmesh/operators/bmo_primitive.cc` | GameObject > 3D Object |
| `meshops::subdivide_catmull_clark` | Subdivision | `blenkernel/intern/subdiv_mesh.cc` (OpenSubdiv) | — |
| `meshops::extrude_faces`, `inset_faces`, `delete_faces` | Edit Mode operators | `bmesh/operators/bmo_extrude.cc`, `editors/mesh/editmesh_extrude.cc` | ProBuilder |
| `meshops::merge_by_distance`, `triangulate`, `smooth_laplacian` | Cleanup and smoothing | `bmo_removedoubles.cc`, `bmo_triangulate.cc`, `modifiers/intern/MOD_smooth.cc` | — |
| `scene/scene.*` GameObject + Transform | Object, parenting, keep-transform reparent | `makesdna/DNA_object_types.h`, `blenkernel/intern/object.cc`, `editors/object/object_relations.cc` | GameObject, Transform |
| Components (`MeshFilter`, `MeshRenderer`, `Light`, `Camera`, `Rigidbody`, …) | Behaviour via composition | Object data-blocks, materials, `blenkernel/intern/rigidbody.cc` | Components |
| Modifier components (`SubdivisionSurface`, `SmoothModifier`) | Non-destructive stack, cached evaluation | `source/blender/modifiers` | — |
| `Reflector` | One field description drives the Inspector, file IO and change hashing | RNA (`source/blender/makesrna`) | `SerializedProperty` |
| `scene/scene_io.cpp` | Text scene format (tolerant of unknown fields), OBJ with axis conversion | `blenloader/intern/writefile.cc`, `io/wavefront_obj` | YAML scenes, model importer |
| `editor/ui.*` | Immediate-mode widgets, popups, menus, tooltips | `editors/interface` | IMGUI (`EditorGUILayout`) |
| `editor/editor.cpp` dock tree | Tabs, split/merge areas, layout presets | `editors/screen` (areas, workspaces) | Docking, Layouts |
| `editor/scene_view.cpp` | Navigation, gizmos, picking, Edit Mode | `editors/space_view3d/view3d_navigate_*.cc`, `view3d_select.cc`, `editors/transform`, `editors/gizmo_library` | Scene view, handles |
| `draw_hierarchy` | Tree, reparent by drag, rename, search | `editors/space_outliner` | Hierarchy |
| `draw_inspector` | Component editing | `editors/space_buttons` | Inspector |
| `draw_project` | Assets folder browser | `editors/space_file` | Project |
| `draw_console` | Log + command line | `editors/space_info`, `editors/space_console` | Console |
| `UndoState` snapshots | Global undo | `editors/undo` (memfile undo) | `Undo.RecordObject` |
| `editor/learn.cpp`, `lessons.cpp` | 25 lessons linking Unity ↔ Blender ↔ theory | — | Learn window |

## Modeling, UVs, texturing and rendering (phase 2)

| Blendity | What it does | Blender source it mirrors | Unity equivalent |
|---|---|---|---|
| `Mesh::uvs`, `face_material`, `seams` | Per-corner UV map, per-face material index, seam edges as named attributes | `blenkernel/intern/mesh_attributes.cc`, `.uv_seam`, `material_index` | `Mesh.uv`, sub-meshes |
| `meshops::edge_loop`, `loop_cut` | Edge-loop walker and Loop Cut (ring walk, n cuts, slide, caps patched) | `bmesh/intern/bmesh_walkers_impl.cc` (`BMW_EDGELOOP`, `BMW_EDGERING`), `editors/mesh/editmesh_loopcut.cc` | ProBuilder Insert Edge Loop |
| `meshops::fill`, `merge_at_center`, `recalc_normals_outside` | Fill, Merge > At Center, Recalculate Outside | `bmesh/operators/bmo_create.cc` (contextual create), `bmo_removedoubles.cc`, `bmo_normals.cc` | ProBuilder Fill Hole / Collapse / Conform Normals |
| Proportional editing (`compute_proportional_weights`) | Smooth / sphere / root / sharp / linear / constant falloff, grid-accelerated | `editors/transform` (proportional editing) | — |
| `MirrorModifier`, `ArrayModifier`, `SolidifyModifier` | Generative modifier components | `modifiers/intern/MOD_mirror.cc`, `MOD_array.cc`, `MOD_solidify_extrude.cc` | — |
| `scene/uv.*` (`uvops::`) | LSCM unwrap with seams, Smart UV Project, cube/cylinder/sphere/view projections, pack islands, average island scale | `geometry/intern/uv_parametrizer.cc`, `uv_pack.cc`, `editors/uvedit/uvedit_unwrap_ops.cc` | ProBuilder UV Editor |
| `editor/uv_editor.cpp` | UV Editor window: select, W/E/R transforms, snapping, stretch overlay, background image | `editors/space_image`, `editors/uvedit` | — |
| `scene/material.*` | Unity-style Material evaluating the Principled BSDF; texture slots, tiling, UV/box/generated mapping, procedural checker/noise, UV Grid / Color Grid | `nodes/shader/nodes/node_shader_bsdf_principled.cc`, `node_shader_tex_image.cc`, `intern/cycles/kernel/svm/checker.h`, `noise.h`, `imbuf/intern/` (generated images) | Material, Standard / Lit shader |
| `image/image.*` | PNG (inflate), baseline JPEG, TGA, BMP, Radiance HDR decoding; JPEG (optimal Huffman) and HDR encoding; mip chains with an sRGB LUT | `imbuf/intern/format_png.cc`, `format_jpeg.cc`, `format_hdr.cc`, `format_targa.cc`, `format_bmp.cc` (via OpenImageIO), `imbuf/intern/imageprocess.cc` | Texture importer |
| `scene/import.*` | OBJ + MTL and FBX (ufbx) import with materials and textures; files copied into Assets | `io/wavefront_obj/importer`, `io/fbx` | Model importer |
| `render/raster.*` (`ShadeMode::Deferred`) | Visibility buffer + one shading pass per pixel, sun shadow maps with PCF | `draw/engines/eevee` (deferred pipeline, `eevee_shadow.cc`) | URP/HDRP deferred, shadow maps |
| `render/shading.*` | GGX + Lambert BRDF, image-based lighting (SH9 irradiance, mip-filtered specular), Standard / Filmic / ACES view transforms | `draw/engines/eevee/shaders`, `imbuf/intern/colormanagement.cc` | Lit shader, Tonemapping override |
| `render/sky.*` | Hosek-Wilkie sky texture driven by the first Directional Light | `intern/cycles/kernel/svm/sky.h`, `intern/sky` | Procedural Skybox |
| `render/pathtracer.*` | Progressive path tracer: two-level binned-SAH BVH (instancing), NEE, GGX VNDF sampling, MIS, Russian roulette, clamping, À-Trous denoiser | `intern/cycles/bvh`, `intern/cycles/kernel/integrator`, `kernel/closure/bsdf_microfacet.h` | HDRP Path Tracing |
| `editor/render_view.cpp` | Shaded / Rendered viewport modes, F12 final render, Render window, saving PNG/JPEG/HDR | `editors/render/render_internal.cc`, `editors/space_image` (render result) | Game view / Recorder |
| `editor/assets.cpp` | Image asset scan, drag-and-drop import into Assets, texture assignment | `editors/space_file`, `io` operators | Project window import |

## Edge tools, Inspector editing, preview and surface types (phase 4)

| Blendity | What it does | Blender source it mirrors | Unity equivalent |
|---|---|---|---|
| `scene/mesh_tools.cpp` `bevel_edges` | Bevel with width and segments; neighbouring faces take the curve's vertices, corner patches where beveled edges meet | `bmesh/tools/bmesh_bevel.cc` | ProBuilder Bevel |
| `scene/mesh_tools.cpp` `bridge` | Joins two face regions or two holes with a tube. Matches vertices after rotating one outline onto the other's plane, adds vertices to the shorter outline, and curves the tube (Hermite) as a tunnel or a handle | `bmesh/operators/bmo_bridge.cc` | ProBuilder Bridge Edges |
| `push_through`, `fuse_contacts` | Hole along a face's normal, imprinted on whatever face it exits through (an annulus of faces cut into it; the Boolean solver for several exit faces). Faces extruded or moved onto another face merge into it | `bmesh/operators/bmo_*` + Boolean (`geometry/intern/mesh_boolean_manifold.cc`) | — |
| `subdivide_edges`, `connect_vertices`, `dissolve_edges`, `collapse_edges` | Edge Subdivide, Connect (J), Dissolve, Collapse | `bmo_subdivide.cc`, `bmo_connect.cc`, `bmo_dissolve.cc` | ProBuilder edge actions |
| `scene/mesh_internal.h` | Shared topology helpers (FaceBuilder, EdgeFaces) | BMesh's radial loop cycles | — |
| `Editor::LastOp`, `run_last_op`, `draw_last_op_panel` | Adjust Last Operation: re-runs the operator from a copy of the mesh with new values and an X / Y / Z / normal move, replacing one undo step | `windowmanager/intern/wm_operators.cc` (`WM_operator_last_redo`), `editors/undo` | — |
| `ui::eval_number`, `FieldEdit` / `ApplyEditReflector` (`editor/panels.cpp`) | Expressions in number fields (`+=`, `*=`, functions, `L()`, `R()`) and multi-object editing through the same reflection that draws the Inspector | `editors/interface` (numeric input), RNA multi-editing (Alt+drag) | Inspector expressions, multi-object editing |
| `Reflector::samples`, `RenderSettings::preview_*`, `Editor::draw_camera_preview` | Sample counts with halve / double and power-of-two presets; Preview and Live Preview renders; Camera Preview inset | Cycles Sampling panel, `editors/render` | Camera Preview |
| `MaterialSurface` (`scene/material.h`) | Opaque / Cutout / Transparent / Glass. Path tracer: stochastic alpha, transparent shadows, rough dielectric BSDF. Rasterizer: sorted forward pass after the opaque pass | `intern/cycles/kernel/closure/bsdf_microfacet.h` (glass), EEVEE blend modes | URP Surface Type, Alpha Clipping |

## Key conventions

- **Coordinates:** Unity's left-handed, Y-up space. OBJ import/export mirror X and reverse the winding, as Unity's importer does.
- **Front faces:** `cross(b - a, c - a)` points outward. Faces wind clockwise on screen in a y-up view.
- **Pixels:** `0xAARRGGBB`, so Win32, X11 and CoreGraphics can all blit without conversion.
- **Idle CPU:** the editor redraws only on input, animation or timers (tooltip, caret). At idle it sleeps in the OS event wait.
- **Undo:** a scene-graph snapshot after each finished interaction. Meshes are shared and copied on first write.

## What isn't recreated

Rendering runs on the CPU (Blender's EEVEE uses the GPU; Cycles can use either), and materials are a fixed Principled BSDF rather than node graphs. Out of scope so far: sculpting, rigging/animation/NLA, the node systems (shader, geometry, compositor), grease pencil, the video sequencer, the Python API, `.blend` and glTF IO, volumes, hair, and caustics or subsurface scattering in the path tracer (glass refracts, but light through it reaches surfaces only along paths, as in Cycles without caustics). The Learn tab explains the concepts behind several of them where they relate to Unity.
