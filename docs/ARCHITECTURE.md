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
├── research/   paper-derived mesh features (registry; cited in Learn > References)
├── deps/       registry of the optional Blender libraries (the Console's `libs` command)
└── app/        main()
extern/         ufbx, fast_float, MikkTSpace, Hosek-Wilkie sky (copied from Blender's tree)
stress/         blendity_stress  - limits & naive-vs-optimised comparisons
tests/          blendity_tests   - unit checks (5,710 on Windows with Blender's libraries)
```

## System overview

Blendity is one process with no plugins and no scripting runtime. Three executables share one static
library built from `src/`: `Blendity` (the editor), `blendity_tests` and `blendity_stress`.

| Layer | Owns | Depends on |
|---|---|---|
| `core` | math (Unity's left-handed, Y-up conventions), logging, filesystem, jobs (TBB or own pool), CPU features | - |
| `platform` | one window, input events, clipboard, file dialogs (Win32 / X11 / Cocoa) | core |
| `image` | codecs, mipmapped textures | core |
| `scene` | `Mesh` (Blender's corner-based layout) and `meshops` operators, UVs, materials, modifiers, `GameObject` / components, scene and model IO | core, image |
| `render` | 2D canvas and fonts, rasterizer, path tracer, GPU devices, colour management | core, image, scene |
| `editor` | IMGUI toolkit (`ui::Context`), docking, the windows, tools (draw, knife, push/pull, modal transforms), keymaps, undo | everything above |

`scene` never includes `editor` or `render`; mesh operators (`meshops::*`, `src/scene/mesh_tools*.cpp`)
are pure functions on a `Mesh` plus selection vectors, which is what lets the tests and the stress suite
call them directly.

### One frame
`platform` events → `Editor::frame` → windows draw and handle input in docking order (the Scene view runs
the active tool: `draw_update`, `pushpull_update`, `modal_update`, the gizmo) → `handle_shortcuts` (the
keymap; tools consume keys they own first) → the frame is rasterized into the editor's framebuffer →
`platform` presents it. Headless runs (`init_headless` + `step_frame_headless`) execute the same frame
without a window; tests drive the UI this way with synthetic events.

### One edit
A tool or menu calls `Editor::edit_tool(op)` (or the console's `editop`). Operators that take numbers
become a `LastOp`: `run_last_op` copies `LastOp::before`, applies the `meshops` function with the current
values, records the result's pointer and version, and the F9 panel or drag helper re-runs it when a
value changes. `mark_changed` turns the edit into one undo step (a scene clone sharing unchanged meshes).

## Key invariants
- **Copy-on-write meshes** (ADR 0003): `mesh_make_mutable` before writing; re-read `MeshPtr` after an
  edit; every change `touch()`es the mesh so version-keyed caches refresh.
- **Edit Mode selections** are vectors indexed like the mesh (`vert_sel_`, `face_sel_`) plus a set of edge
  keys (`edge_sel_`); any operator that renumbers faces or vertices must rebuild them.
- **Adjustable operations** re-run from `before`; anything that changes the result after it was recorded
  must update `LastOp::result_version` (ADR 0005).
- **Geometry robustness** (Q-01): operators leave a structurally valid mesh or refuse with a message;
  closed meshes stay closed.
- **Isolation of user files** (C-04): tests and stress run in a scratch project and trash.

## Module ownership
| Area | Files | Tests / stress |
|---|---|---|
| Mesh data, primitives, extrude, individual inset, loop cut | `scene/mesh.h`, `mesh.cpp` | unit groups by round |
| Push/Pull, bevel, bridge, push-through, ring repair, dissolve edges | `scene/mesh_tools.cpp` | `pushpull`, `curved` |
| Poke, triangulate, duplicate, split (mesh_tools2); dissolve limited, seams, face paths (mesh_tools3) | `scene/mesh_tools2.cpp`, `mesh_tools3.cpp` | `modifiers_ngon` |
| Slice, follow, spin, region inset, smart fill, overlaps, imprints across faces | `scene/mesh_tools4.cpp` | `curved`, `modifiers_ngon` |
| Hard-surface tools (grid, pipe, array, taper, recess) | `scene/mesh_tools5.cpp` | `curved` |
| Drawing, guides, snaps | `editor/draw_tool.cpp` | unit (real clicks) |
| Edit Mode tools, F9, modal helpers, gizmo | `editor/scene_view.cpp`, `modal_transform.cpp` | unit, `editor` fuzz |
| Keymaps and shortcuts | `editor/keymap.cpp` | unit |

# History of subsystems

What each development phase added, with the Blender (and SketchUp / UModeler / Plasticity) equivalents.
These sections are kept as written; decisions that shape later work are recorded in `docs/decisions/`.

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
| Vulkan headers + shaderc | `VULKAN` | `render/gpu_device.cpp`: GPU path tracing (compute shader, hardware ray queries); the Vulkan loader is opened at run time | CPU only | `source/blender/gpu/vulkan`, `intern/cycles/device` (Cycles uses CUDA / OptiX / HIP / oneAPI / Metal) |

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
| `push_through`, `fuse_contacts` | Hole along a face's normal, imprinted on whatever face it exits through (an annulus of faces cut into it; several exit faces in one plane: the outline cut across them and the front outline given matching corners; the Boolean solver only when they are not coplanar). Faces extruded or moved onto another face merge into it | `bmesh/operators/bmo_*` + Boolean (`geometry/intern/mesh_boolean_manifold.cc`) | — |
| `subdivide_edges`, `connect_vertices`, `dissolve_edges`, `collapse_edges` | Edge Subdivide, Connect (J), Dissolve, Collapse | `bmo_subdivide.cc`, `bmo_connect.cc`, `bmo_dissolve.cc` | ProBuilder edge actions |
| `scene/mesh_internal.h` | Shared topology helpers (FaceBuilder, EdgeFaces) | BMesh's radial loop cycles | — |
| `Editor::LastOp`, `run_last_op`, `draw_last_op_panel` | Adjust Last Operation: re-runs the operator from a copy of the mesh with new values and an X / Y / Z / normal move, replacing one undo step | `windowmanager/intern/wm_operators.cc` (`WM_operator_last_redo`), `editors/undo` | — |
| `ui::eval_number`, `FieldEdit` / `ApplyEditReflector` (`editor/panels.cpp`) | Expressions in number fields (`+=`, `*=`, functions, `L()`, `R()`) and multi-object editing through the same reflection that draws the Inspector | `editors/interface` (numeric input), RNA multi-editing (Alt+drag) | Inspector expressions, multi-object editing |
| `skinweights::inpaint`, `transfer_skin_weights`, `automatic_skin_weights` (`scene/skin_weights.cpp`); `Editor::run_skin_job`, `SkinWeightJob` (`editor/skin_tools.cpp`); `JobSystem::run_background` | Skin weights by inpainting: the cotangent Laplacian and Voronoi mass, Q = -L + L M^-1 L, the constrained solve (Eigen LDLT or conjugate gradients); matching for transfer; the trusted-vertex rules for automatic weights; the background job and its own job arena | Data Transfer, With Automatic Weights (bone heat) | Epic's weight transfer (ADR 0028) |
| `Reflector::object`, `Editor::hierarchy_drag_object`, `finish_hierarchy_drag` | A field that names a GameObject: text to files, undo and `set`; in the Inspector also a drop target for a row dragged from the Hierarchy. A pressed unselected row is selected when the button comes up, unless a field took it | Outliner drag and drop, the eyedropper | `EditorGUI.ObjectField` |
| `Reflector::samples`, `RenderSettings::preview_*`, `Editor::draw_camera_preview` | Sample counts with halve / double and power-of-two presets; Preview and Live Preview renders; Camera Preview inset | Cycles Sampling panel, `editors/render` | Camera Preview |
| `MaterialSurface` (`scene/material.h`) | Opaque / Cutout / Transparent / Glass. Path tracer: stochastic alpha, transparent shadows, rough dielectric BSDF. Rasterizer: sorted forward pass after the opaque pass | `intern/cycles/kernel/closure/bsdf_microfacet.h` (glass), EEVEE blend modes | URP Surface Type, Alpha Clipping |

## GPU render devices (phase 5)

Cycles has one device backend per vendor (`intern/cycles/device/cuda`, `optix`, `hip`, `oneapi`, `metal`). Blendity has a single one, **Vulkan**, which runs on NVIDIA, AMD and Intel GPUs on Windows and Linux, and on Apple GPUs through MoltenVK.

| Blendity | What it does | Blender / Cycles equivalent |
|---|---|---|
| `render/gpu_kernel.h` | The path-tracing megakernel in GLSL. It is a line-for-line port of `PathTracer::trace()`, covering materials, textures, normal maps, GGX, glass, Cutout / Transparent alpha, sun / point / mesh-light NEE with MIS, Russian roulette, sky and HDRI. It uses the same PCG seeds, so GPU and CPU samples are interchangeable. There are two variants: a software two-level BVH walk, and `GL_EXT_ray_query` | `intern/cycles/kernel/integrator/*`, `kernel/device/optix` |
| `render/gpu_device.cpp` | Loads `vulkan-1.dll` / `libvulkan.so.1` at run time (`VK_NO_PROTOTYPES`), enumerates GPUs and detects ray-tracing support, and compiles the kernel with shaderc. The optimised SPIR-V is cached in `~/.blendity/cache`, and `gpu::prewarm()` compiles it off the UI thread once GPU Compute is chosen. It uploads the flattened scene to device-local buffers, with one BLAS per unique mesh and a TLAS over objects. It renders sample batches and reads results back through host-cached memory | `device/device.h`, `device/optix/device_impl.cpp` (BVH build) |
| `PathTracer` (`render/pathtracer.cpp`) | Combined rendering. Every device (CPU threads and each GPU) claims whole sample indices from one atomic counter. GPUs take guided self-scheduling batches. The CPU stops claiming once the GPUs would finish the remaining samples sooner. Device buffers are merged only when the display needs it (every 100 ms and at the end), and albedo / normal only for the denoiser | `integrator/path_trace.cpp`, `work_balancer.cpp` (multi-device) |
| `RenderSettings::device`, `gpu_with_cpu`, `hardware_rt`; Render window *Render Devices* | Device choice is stored in the scene. Per-machine device ticks live in the editor prefs (`render_devices_off=`), and the Console has a `device cpu\|gpu [+cpu] [nort]` command | Render Properties > Device; Preferences > System > Cycles Render Devices |

## Push/Pull as an operator, per-mode tools, Set Origin and physical cameras (phase 6)

| Blendity | What it does | Blender / Unity equivalent |
|---|---|---|
| `Editor::pushpull_begin / _update / _finish / _cancel` (`editor/scene_view.cpp`) | Push/Pull as a modal face operator. P starts it on the selected faces, the mouse (projected onto the region's normal) or a typed expression sets the distance, and a click or Enter confirms (one undo step, then Adjust Last Operation). Esc or a right-click restores the mesh | `editors/transform` modal operators (G / E with numeric input) |
| `meshops::push_pull` hardening (`scene/mesh_tools.cpp`) | The changes the stress test led to: <br>- **Limits:** non-finite distances are refused; `push_pull_limits` adds `behind` / `ahead`, the nearest geometry in front or behind, from corner rays plus `sweep_limit` (other faces' vertices and edges inside the prism the outline sweeps), and Push/Pull stops there.<br>- **Leaning neighbours:** corners slide within the neighbours' planes (Blender's face move).<br>- **Cleanup:** the collinear-corner cleanup keeps vertices that other faces still use.<br>- **Tidying:** `tidy_new_geometry` welds new vertices to existing ones, drops collapsed faces and cancels opposite twin faces.<br>- **Holes** that would open the mesh or leave slivers fall back to stopping at the far side.<br>- **Precision:** tolerances scale with float precision far from the origin | `bmesh/operators`, `editors/mesh/editmesh_extrude.cc` |
| `EditOpInfo`, `edit_op_table()`, `Editor::edit_tool` | One table of Edit Mode operators tagged vertex / edge / face; the Mesh menu, the Inspector and the shortcuts read it, and a shortcut from another mode explains which mode it needs | Blender's Vertex / Edge / Face menus (Ctrl+V / Ctrl+E / Ctrl+F); ProBuilder's per-mode actions |
| `meshops::origin_point`, `Editor::set_origin` | Bounds, median, surface and volume centre of mass (signed tetrahedra relative to the first vertex), bottom centre, a point, the edit selection, the view pivot, or Geometry to Origin. The mesh moves one way, the object the other (through its rotation and scale), and children compensate | `editors/object/object_transform.cc` (`object_origin_set_exec`), `BKE_mesh_center_of_volume` |
| `Camera` (`scene/scene.h`): `physical`, `focal_length`, sensor presets, `sensor_fit`, `shift_x/y`, `aspect_mode`, `dof`, `f_stop`, `focus_distance`, `blades`, `physical_exposure` | The projection comes from the lens (`vertical_fov_deg`, off-axis shift in `projection()`). `aperture_radius` = focal length / (2 f-stop), and `exposure_stops` is relative to ISO 100, 1/60 s, f/2.8 | Unity Physical Camera; Blender `DNA_camera_types.h`, `intern/cycles/blender/camera.cpp` |
| `PTLens`, thin-lens primary rays (`render/pathtracer.cpp`, `gpu_kernel.h`) | The same aperture sample (disk or regular polygon) on the CPU and the GPU, aimed at the focus plane | `intern/cycles/kernel/camera/camera.h`, `kernel/sample/mapping.h` |
| `Editor::draw_camera_preview` | Shaded (rasterized) or Rendered: a progressive path tracer at the inset's size, rebuilt when the scene or the camera changes, denoised when it reaches the preview sample count | Unity Camera Preview; Blender's camera view in Rendered shading |
| `set <Component>.<Field> <value>` console command | Sets any reflected field by name (unique prefix, enums by text) | `bpy` property access |

## Blender's modal editing, per-face shading, Unity colours (phase 7)

| Blendity | What it does | Blender / Unity equivalent |
|---|---|---|
| `modal_transform.cpp` (`transform_begin / _update / _finish`, `extrude_and_move`) | G / R / S on objects or selected vertices. The mouse moves the selection on the view plane or along an axis, rotates it around the pivot, or scales it by distance. X / Y / Z lock an axis (twice: local), Shift+axis locks a plane. It takes typed values, Ctrl snap and Shift fine control. Ctrl+E extrudes, then moves along the normal as one undo step | `editors/transform` (`transform_mode_translate.cc`, `_rotate.cc`, `_resize.cc`, constraints) |
| `Editor::modal_begin / modal_update` | Inset (I) and Bevel (Ctrl+B) follow the mouse's distance from the selection's centre. Bevel takes the wheel for segments. Esc removes the operator's undo step | `editmesh_inset.cc`, `editmesh_bevel.cc` (modal) |
| `edge_sel_`, `meshops::EdgeSelectionScope` | Edge mode keeps the selected edges themselves. While a scope is alive, every edge tool reads that set instead of "both ends selected" | BMesh's per-edge select flags |
| `Mesh::face_smooth`, `sharp_edges`, `seams_sharp` | Smooth shading per face. Normals average within fans that are joined across smooth edges (union-find over corners), and stop at flat faces, sharp edges, the angle limit and, optionally, seams | `sharp_face` / `sharp_edge` attributes, `mesh_normals.cc` |
| `mesh_tools2.cpp` | Poke, triangulate and tris-to-quads on a selection, flip, duplicate, split, dissolve faces and vertices, extrude individual, shrink/fatten, to sphere, randomize, edge split; select linked, more / less, non-manifold and edge ring | `bmesh/operators/bmo_*.cc`, `editmesh_select.cc` |
| `bevel_edges(..., clamp_overlap)` | Clamp Overlap: one even width, at most half of any edge that slides from both ends | `bmesh_bevel.cc` (`clamp_overlap`) |
| `ui::Context::color_field(..., alpha)`, `Reflector::color_alpha` | Unity's colour field (alpha bar) and Color window (SV square, hue strip, RGB 0-255 / 0-1 / HSV, alpha, hex RRGGBBAA, swatches). Files keep the colour and its alpha as two fields | Unity `EditorGUI.ColorField`, `ColorPicker` |

## Lights, export, selection and viewport polish (phase 8)

| Blendity | What it does | Blender / Unity equivalent |
|---|---|---|
| `Light` (spot, area, `use_temperature`), `RenderLight`, `light_falloff`, `to_render_light` | Spot cones with a soft edge and area rectangles or discs, in the rasterizer, the CPU path tracer and the GPU kernel. Area lights are sampled over their surface; every light type draws 2 random numbers, so CPU and GPU stay in step. `kelvin_to_rgb` follows the Planckian locus (Kang et al. 2002) to linear RGB | Blender Spot / Area lamps, the Blackbody node; Unity Light Type, HDRP Color Temperature |
| `scene/export.cpp` (`export_obj_mtl`, `export_fbx`) | OBJ + MTL (UVs, smooth / flat normals, `usemtl` groups, Phong + PBR terms, texture maps) and ASCII FBX 7.4 (geometry, normals, UVs, per-face materials, one model per object), mirrored in X to right-handed. FBX import mirrors X the same way (Unity's convention) instead of ufbx's left-handed target | `io/wavefront_obj/exporter`, `io_scene_fbx`; Unity's FBX Exporter |
| `Editor::alt_click_release`, `edit_select_ring`, face loops in `edit_select_loop` | Alt+click a loop and Ctrl+Alt+click a ring; in face mode these give face loops. They only count when the mouse didn't move, so Alt+drag still orbits and pans | `editmesh_select.cc` (loop / ring select) |
| `view_gizmo_rect`, `draw_view_gizmo` | The scene gizmo keeps its clicks: axes give the named views, and the label or centre switches between perspective and isometric | Unity Scene Gizmo; Blender's navigation gizmo |
| `draw_origins`, `origin_target` | An origin dot on selected objects, and a preview of where Set Origin will move it | Blender's object origins |
| `cam_preview_lock_`, `focus_pick_update` / `raycast_scene` | A Camera Preview that stays while you edit, and a focus-distance eyedropper | Unity Camera Preview; Blender's Focus Distance eyedropper |

## File formats, object tools, origins, the modifier stack, shapes and n-gons (phase 9)

| Blendity | What it does | Blender / Unity equivalent |
|---|---|---|
| `scene/export.cpp` (`ExportOptions`, `export_file`, `prepare`, `split_normals`) | One preparation step for every format: world transform, scale and axes (mirror X; Z-up files also turn Y into Z like Blender's axis conversion), triangulation, winding, and per-corner normals split at flat faces, sharp edges and the smooth angle. Writers: OBJ + MTL, FBX 7.4 as a record tree with a binary encoder (Blender's `encode_bin.py`, header, null records and footer) and an ASCII one, glTF 2.0 (.glb chunks, or .gltf + .bin; per-material primitives, PBR materials, `KHR_materials_emissive_strength`, embedded or copied PNG / JPEG textures), STL (binary / ASCII), PLY (binary / ASCII, vertices split where normals or UVs differ) and USD text (faceVarying normals and `primvars:st`, UsdPreviewSurface materials, GeomSubsets per material) | `io/wavefront_obj`, `io_scene_fbx/export_fbx_bin.py`, `io_scene_gltf2`, `io/stl`, `io/ply`, `io/usd` |
| `scene/import_formats.cpp` | glTF (a small JSON parser, accessors of every component type, strips and fans, embedded images extracted to the cache), STL, PLY (ASCII and both binary byte orders) and USD text (a USDA reader: prims, attributes, metadata, indexed primvars, GeomSubsets, `upAxis` / `metersPerUnit`, transforms) | Blender's importers for the same formats |
| `platform::save_file_dialog` / `open_file_dialog`, `Editor::pick_save_path`, `defer` | Native dialogs: `GetSaveFileNameW` (default extension, filter index, no repaint or stale capture during its loop), `NSSavePanel` / `NSOpenPanel`, zenity / kdialog. They run between frames (`deferred_`), so a screenshot never contains the menu that asked for it | Blender's file browser; Unity's `EditorUtility.SaveFilePanel` |
| `Mesh::loose_edges` | Edges without faces (sorted keys). `edges()` includes them, vertex renumbering remaps them, `remove_loose_verts` keeps their vertices, scenes save them (`w a b`) | Blender's loose / wire edges |
| `meshops::extrude_verts_edges` | An edge grows a quad wound against its face's direction (so they agree), a lone vertex grows a wire edge; the selection moves to the new ends. The editor uses a region extrude when whole faces are selected | `bmo_extrude.cc` (`extrude_edge_only`, `extrude_vert_indiv`) |
| `editor/object_ops.cpp` (`join_selected`, `boolean_selected`, `separate`), `meshops::append_mesh`, `extract_faces`, `loose_parts` | Join with material slots merged and mirrored objects re-wound; Boolean applied (cutters removed) or as modifiers with the cutter drawn as wire and left out of renders; Separate by selection or loose parts | `object_join.cc`, `MOD_boolean.cc` and the Bool Tool add-on, `mesh_separate_*` |
| `MeshRenderer::display_as`, `show_in_renders`, `Editor::pick_wire_object` | Solid / Wire / Bounds drawing; wire objects are picked by their edges since they leave no pixels in the id buffer | Object > Viewport Display > Display As, Visibility > Renders |
| `origin_edit_`, `compensate_origin_drag`, `origin_snap_target` | The gizmo moves or turns objects and the meshes (and children) are moved back in the world each frame; a click snaps the origin to a vertex, an edge midpoint or a face centre | Options > Affect Only > Origins; snapping |
| `Component::show_in_editmode` / `show_in_render`, `GameObject::evaluated_mesh(kind)` | Three cached evaluations: viewport (enabled), render (show in renders) and Edit Mode (enabled and show in edit mode, drawn over the cage) | Modifier header toggles |
| Inspector modifier stack (`panels.cpp`) | Modifiers drawn together with Blender's header (fold, name, toggles, menu, delete) and Add Modifier by category; Apply works on one modifier, ignoring those above it, as Blender does | Modifier Properties |
| `scene/modifiers.cpp` | Bevel (by angle or all edges, through `bevel_edges`), Triangulate (minimum vertices), Weld, Wireframe (rims inside every face, welded, then solidified), Displace (value-noise fBm), Simple Deform (a port of `MOD_simpledeform.cc`: the axis remap, limits and the clamped remainder), Cast (sphere, cylinder, cuboid), Screw (every edge swept round an axis, axis vertices welded). Non-finite settings leave the mesh alone | `MOD_bevel.cc`, `MOD_triangulate.cc`, `MOD_weld.cc`, `MOD_wireframe.cc`, `MOD_displace.cc`, `MOD_simpledeform.cc`, `MOD_cast.cc`, `MOD_screw.cc` |
| `ProceduralShape` (`scene/procedural.cpp`), `Editor::update_procedural_shapes` | Thirteen shapes built from their settings (grid boxes welded at the seams, lathed profiles, ring solids for pipes and arches, extruded side profiles for stairs and wedges), made safe first (finite, clamped). The editor rebuilds when the settings' hash changes and drops the component once the mesh's version moves on (an edit; copy-on-write clones keep the version) | ProBuilder Shapes; Add Mesh + Adjust Last Operation; Extra Objects |
| `meshops::split_edge`, `split_face`, `coplanar_region`, `coplanar_edges`, `dissolve_limited`; `editor/knife.cpp` | N-gon Mode hides edges between coplanar faces and selects whole flat regions; Merge Coplanar dissolves those edges and the corners left on straight runs; the Knife splits a face between two points on its outline with endpoint / midpoint / on-edge inference | Limited Dissolve (`bmo_dissolve.cc`), the Knife (`editmesh_knife.cc`); SketchUp's faces and Line tool |

## Material assets, drawing, UModeler tools, the pivot (phase 10)

| Blendity | What it does | Blender / Unity equivalent |
|---|---|---|
| `Material::asset_path`, `material_asset`, `create_material_asset`, `save_dirty_material_assets`, `relink_material_assets` (`scene_io.cpp`) | `.mat` files hold a material's reflected fields. One shared instance per file; edits are written back after each finished change; scenes name the file (values kept as a fallback). Undo snapshots copy materials, so after undo / redo the restored values go into the shared instance and the slots point back at it; after loading, the file wins | Unity Material assets; Blender material datablocks |
| `Editor::update_asset_drag`, `drop_asset`, `drop_slots_` / `drop_textures_` / `drop_rows_` | Dragging a `.mat` or an image out of the Project window. Inspector slots, texture fields and Hierarchy rows register their rectangles as they draw; the drop resolves after the frame. In the Scene view the object under the mouse (id buffer) and its face (ray cast) choose the slot | Unity drag and drop; Blender's Assign |
| `remove_material_slot`, `remove_unused_material_slots` | Faces on a removed slot move to the one before; later slots shift down | Blender's Remove Material Slot / Remove Unused Slots |
| `editor/draw_tool.cpp`, `meshops::imprint_loop`, `split_face_path` | Polyline, Rectangle, Circle, Arc (through three points), Polygon. The first click fixes the plane (the face's, else the ground's). Closed shapes inside a face are imprinted with the `annulus` ring builder; open lines cut faces between points on their outlines (edges split where needed), else become wire edges | UModeler / SketchUp drawing tools; Blender's Knife |
| `meshops::follow`, `spin`, `slice` (`mesh_tools4.cpp`) | Follow: the face's corners carried along the path by parallel transport, mitred at bends by 1 / cos(half the turn). Spin: copies round an axis, quads between them, 360 degrees closes. Slice: vertices where edges cross the plane, faces cut between them, an optional side removed | UModeler Follow / Lathe / Slice; SketchUp Follow Me; Blender Spin and Bisect |
| `meshops::sharp_edges_by_angle`, `seams_from_sharp` | Edges between faces meeting at more than the angle, edges marked sharp, and edges on three or more faces | Select Sharp Edges + Mark Seam |
| `Editor::aim_light` | Shortest turn from +Z to the target, then a twist that levels the X axis; the range grows to reach | Track To / Point At; Unity `Transform.LookAt` |
| Shift + gizmo drag (`gizmo_update`) | Extrude (faces, or edges / vertices) before the drag, so the handle moves, turns or scales the new geometry | Blender's E then G / S; ProBuilder / UModeler Shift-drag |
| `pivot_center_` | Pivot by default (saved); Center now scales about the bounds centre as it rotates; G / R / S use the same point | Unity's Pivot / Center; Blender's Pivot Point |

## Snapping, the Materials and Modeling Tools windows, camera piloting, Plasticity tools (phase 11)

| Blendity | What it does | Blender / Unity / SketchUp equivalent |
|---|---|---|
| `draw_hit` inference, `DrawTool::axis_u` | The plane's axes come from the face's longest edge or the object's rotation. Candidate directions from the last point (the axes, in-plane edge directions and their perpendiculars, square to the last segment) snap when within 8 px on screen; Shift takes the nearest | SketchUp inference (red / green axes, parallel and perpendicular) |
| `meshops::attach_face`, `point_in_face` | A drawn path between two corners of a face that runs outside it becomes a face closed by the face's own outline (shorter way round), sharing those edges the other way; only where they are open | SketchUp: drawing on a face's edge |
| `push_pull` wrapper (free-standing faces) | When every edge of the selection's outline has no other face, the original faces come back as the floor (pull) or the moved faces flip into it (push), so the result is closed; zero-area faces are skipped | SketchUp Push/Pull on a lone face |
| `annulus` tie tolerance | Equal angles step the outer loop first, so aligned shapes ring with quads | - |
| `Curve`, `CurvePath`, `sample_curve` (`scene/curve.{h,cpp}`) | A Curve object's points (Bezier with Auto handles, Catmull-Rom, Poly; closed; tilt, radius) sampled into positions with rotation-minimising frames (parallel transport; a closed loop's leftover twist spread round it) and an arc-length table; `at(s)` goes on straight past an open curve's ends and wraps a closed one | Blender `curve.cc`, Bishop frames |
| `CurveModifier`, `ArrayModifier::fit_type` | Deform-only: each vertex's coordinate along the deform axis becomes a distance along the curve (in the owner's space through the relative matrix), the other two an offset in its frame times the radius. Array's Fit Length / Fit Curve count the copies that fit. Both name the curve like the Boolean's cutter; their dependency hash is the curve's fields plus the relative matrix | Blender `MOD_curve.cc`, `MOD_array.cc` |
| `Lattice`, `LatticeModifier` (`scene/lattice.cpp`) | A box of u x v x w points (offsets from rest places on a unit cube); `displacement(q)` blends the offsets round q with linear or uniform cubic B-spline weights (border points repeated, outside clamped to the box); `resample` keeps the shape when the resolution changes. The modifier maps each vertex into the lattice's space and back (deform-only, parallel) | Blender `lattice_deform.cc` |
| `Mesh::vertex_groups` (`VertexGroup`), `meshops::transfer_vertex_groups` (`scene/vertex_groups.cpp`) | Named dense per-vertex weights. remove_loose_verts remaps them, Mirror / Array / Join copy them; any other topology change is mended afterwards by position (by index, exact match, else the nearest point of the old surface, barycentric) | ADR 0024 |
| `Bone`, `Armature`, `SkinModifier`, `armature_bones`, `skin_bind_automatic` (`scene/skin.cpp`) | Bones are GameObjects (rest TRS on the Bone); a skin's matrix per bone is owner^-1 x bone world x (root's parent x rest)^-1 x owner, blended linearly or by dual quaternions (rigid bones), 4 influences from the groups named after bones; automatic weights by distance to the bone segments | ADR 0025 |
| `transform_from_matrix` (`scene/scene.cpp`), `ImportedNode::bind_world`, `mirror_x` (`scene/import.h`) | A matrix as position / rotation / scale; an imported joint's bind pose (glTF inverse bind matrices, FBX cluster bind), which `instantiate_import` makes each Bone's rest | ADR 0025 |
| `Editor::Workspace`, `kWorkspaces`, `apply_workspace`, `shows(groups)`, `create_reference_image` (`editor/workspaces.cpp`) | Layouts for each stage of the work: a dock string plus a focus (tool groups); `shows` gates the Tools window's sections, the GameObject menu and Add Component; user layouts in preferences | ADR 0027 |
| `Editor::draw_rigging_window`, `rig_root`, `rig_mesh`, `add_bone`, `add_ik`; `draw_reflected`, `draw_weight_paint_controls` (`editor/rigging_window.cpp`, `panels.cpp`) | The Rigging window: skeleton, skin, vertex groups, Weight Paint, IK, on the selection's objects (not names); any reflected fields drawn with the Inspector's widgets elsewhere | task 0052 |
| `JobSystem::set_thread_limit`, `sys::physical_memory_bytes / working_set_bytes` (`core/`) | A ceiling on every thread the process works on (TBB `global_control` + the built-in pool); the machine's memory and the process's working set | ADR 0026 |
| `Editor::render_scaled`, `set_quality_tier`, `shadow_resolution_for_view`; `upscale_bilinear` | Viewport Resolution Scale (3D drawn smaller, colour bilinear and depth / ids nearest back to full size) and the quality tiers' caps, all in `raster_opt_key` | ADR 0026 |
| `update_shadow_map` cached layers; `render_shadow_map(..., base)` | Casters still for a second keep a cached map (one per view slot); moving ones are drawn over a copy, bit for bit a full map; the frame is snapped for the views | ADR 0026 |
| `Renderer3D::fill_pending_sky` | `clear_environment` defers the sky to `flush()`, after the opaque surfaces, where depth is still clear; smooth runs every 4th pixel | ADR 0026 |
| `texture_cache_set_budget`, `Editor::undo_budget` | Byte budgets: least recently used unheld textures, oldest undo steps | ADR 0026 |
| `IKConstraint`, `solve_constraints`, `ik_drives`, `create_rig`, `add_humanoid_ik` (`scene/ik.cpp`) | IK on a chain's end bone: from the rest pose, two bones by the law of cosines in the pole's plane, longer chains by CCD, blended by Weight; skipped while a hash of its inputs is unchanged; run by `Editor::frame` after the Play update and before drawing (no undo steps). Presets build named skeletons with targets and poles; auto IK maps limbs by bone names | ADR 0025 |
| `ImportedNode::is_bone`, `ImportedSkin` (`scene/import.h`) | glTF (JOINTS_0 / WEIGHTS_0) and FBX (ufbx clusters) skins: groups named after joints, joints become Bones resting at the file's transforms, an Armature on the skeleton's root, a Skin modifier on the mesh | ADR 0025 |
| Weight Paint (`editor/weight_paint.cpp`): `set_weight_paint`, `wp_update`, `wp_dab`, `wp_colors`; `DrawItem::vertex_colors` | A brush over the active mesh's chosen group: every vertex projected in parallel, Add / Subtract / Draw / Blur with a falloff, Front Faces Only by vertex normal, Mirror X; one undo step per stroke. The view draws the base mesh in Solid with a weight colour per render vertex (Gouraud's `shade_vertex` takes `vertex_colors` over albedo); `wp_version_` is in the Scene view's key | ADR 0024 |
| `Editor::carry_vertex_groups`, `run_modifier` | Where the mending happens: at each undo commit against the snapshot before the edit, and for each generative modifier in a stack; `run_modifier` also scales a deformer's move by its Vertex Group | ADR 0024 |
| `MeshDeformModifier`, `mean_value_coordinates` (`scene/mesh_deform.cpp`) | Bind keeps a closed cage's rest shape (vertices, triangles, in the owner's space; saved as text); the per-vertex mean value weights are a cache keyed on the input positions and the binding, made in parallel; each deform is a dense weights x current-cage product | ADR 0023 |
| `Component::control_point_count / control_point / set_control_point`, `GameObject::point_source` | Points the Scene view edits (a Curve's, a Lattice's), in the object's space | - |
| `Editor::draw_curve`, `draw_lattice`, `draw_control_points`, `point_pick`, `point_target` | Curves and lattices drawn over the Scene view each frame (the active one's points too); a click on a point selects it, and `gizmo_update` moves / turns / scales the selected points like Edit Mode vertices; `points` (or `curve`) `add|move|delete|select` in the console | - |
| `meshops::shell`, `draft`, `radial_array`, `fillet_polygon` (`mesh_tools4.cpp`) | Shell = remove faces + inward solidify with rims; Draft moves each vertex of a side face in by its height above the selection's base times tan(angle); Radial copies about an axis; fillets replace corners with arcs tangent to both sides, shrinking to fit | Plasticity Shell, Thicken, Draft Face, Radial Array, Fillet Curve |
| `editor/material_window.cpp` | A software-shaded preview sphere per material (Lambert + Blinn-Phong, its base texture wrapped on, alpha over a checker), cached by version; a grid to pick, drag or right-click, and the material's own fields underneath | Unity Project previews + material Inspector; Blender's Material Browser |
| `editor/tools_window.cpp`, `Editor::draw_edit_tools` | A dockable window of all modeling tools; Edit Mode's tools are one function shared with the Inspector | UModeler / ProBuilder tool windows |
| `editor/object_snap.cpp` (`raycast_surface`, `rest_on_surface`, `nearest_vertex_on_screen`) | Surface snap: the hit point and normal (excluding what moves); the object turns to the normal by the shortest arc if asked, then its lowest point along the normal touches the surface. Vertex snap: the selection's vertex nearest the press is the anchor, the nearest other vertex on screen (24 px) the target | Unity Ctrl+Shift surface snap and V vertex snap; Blender snapping |
| `toggle_pilot_camera`, `update_pilot_camera`, `draw_pilot_frame` | The Scene camera takes the camera's position, yaw / pitch and vertical FOV; afterwards the camera object follows the view each frame (FOV, or focal length on a physical camera); a passepartout with thirds guides | Blender Lock Camera to View; Unity Align With View |

## Drawing across faces, asset folders, face materials, drawing modes, guides, frame rate (phase 12)

| Blendity | What it does | Blender / Unity / Plasticity equivalent |
|---|---|---|
| `meshops::imprint_loop_across` (`mesh_tools4.cpp`) | The shape's segments are intersected (in 2D, in its plane) with every edge of the faces lying in that plane; crossings and points on edges become vertices (`split_edge`). Each piece of the shape between two such vertices splits the face that contains it (`split_face_path`); pieces off every face become wire edges. The enclosed faces are found by point-in-polygon on a point inside each face | SketchUp drawing over edges; Blender Knife Project |
| `pilot_frame_rect`, `pilot_view_from_camera`, `update_pilot_camera` | The frame is the camera's image aspect fitted in the view; the camera's vertical FOV is `2 atan(tan(view/2) * frame.h / view.h)`, and the inverse when piloting starts. A physical camera's focal length inverts `Camera::vertical_fov_deg` (Sensor Fit). The camera's last written transform and lens are remembered, so any other change moves the view to the camera | Blender Lock Camera to View |
| `editor/project_window.cpp` | The folder tree, folder drop targets (`drop_folders_`), F2 rename, Delete with confirmation; `move_project_entry` moves on disk then `retarget_asset_references`: the asset library (`retarget_material_assets`), texture paths, and `.scene` files (`retarget_scene_files`, compressed or not). Moves and deletes are recorded (`asset_redirects`), so an undo that brings back an old path follows the file instead of recreating it | Unity Project window, AssetDatabase.MoveAsset / DeleteAsset |
| `fs::move`, `fs::move_to_trash` | Rename (copy + remove across drives); the Recycle Bin through `SHFileOperationW` with `FOF_ALLOWUNDO`, the freedesktop.org Trash (`files/` + `info/*.trashinfo`) on Linux, `~/.Trash` on macOS. `BLENDITY_TRASH` points tests elsewhere | Unity: Delete moves to the OS trash |
| `sync_material_asset_names` | When no field is being typed in, a material asset whose name differs from its file's is renamed on disk (a unique name if taken). A loaded file's name always wins over the name stored inside it | Unity: asset name = file name |
| `material_picker_popup`, `assign_material_to_selected_faces` | A preview-sphere grid (the Materials window's renderer) of the object's slots, every `.mat` under Assets and the scene's materials; picking reuses or adds the slot | Unity object picker; Blender's material slot + Assign |
| `draw_outline` modes (`draw_rect_mode_`, `draw_circle_mode_`, `draw_uniform_`), `circle_through` | Corner / centre / 3-point rectangles (the third point's distance across the first side), square; circles from the centre, a diameter, or the circumcircle of 3 points | Plasticity Rectangle and Circle modes |
| `Scene::guides`, `draw_hit` guide snapping, `draw_guide_line` | Infinite lines saved with the scene. Snaps: guide-guide crossings (closest points of two lines meeting) and a guide piercing the drawing plane first; then the mesh's own snaps; then the point of a guide nearest the mouse ray; guide directions join the inference directions. Drawn dashed, clipped to the near plane and the view | Plasticity lines; SketchUp Tape Measure guides |
| `frame_wait_seconds`, `Editor::run` | Frames start at most `1 / max_fps` apart; input arriving sooner is gathered into the next frame; Redraw Always keeps the cap while idle. `draw_performance_settings` shows the measured average / worst frame cost against the budget | Unity Interaction Mode; Blender's viewport frame rate |

## Push/Pull clean-up, Delete Loose, region inset, face centres, rasterized depth of field (phase 13)

| Blendity | What it does | Blender / SketchUp / Unity equivalent |
|---|---|---|
| `meshops::overlapping_faces` | Triangles of different faces lying in one plane (same-facing within a distance, opposite-facing coincident) whose 2D clip (Sutherland-Hodgman) has area: z-fighting pairs. The stress suite counts new ones per Push/Pull; Edit Mode's Select Overlapping uses it | (none; SketchUp merges such faces) |
| `push_pull_impl` flush steps | Side walls at the outline (along a side or at a corner) that run the way the region moves end at some distance; the move goes exactly there (welding the walls away) and continues from there as a second step, rather than stopping 2 x snap short | SketchUp Push/Pull flush |
| `sweep_limit` outline rule | A point on the swept outline counts only if its edge heads inward: neighbouring walls the outline slides along are not obstacles | - |
| neighbour consistency, slide | A neighbour that stretches along one side stretches along every side in its plane; when every face around the outline contains the direction, the region's corners just move | Blender: move along normal |
| `dissolve_line_faces`, spike removal in `tidy_new_geometry` | A changed face whose corners lie on one line goes, its corners threaded into the face across its longest edge; welded loops lose a b a spikes | Blender: Dissolve Degenerate |
| `meshops::delete_loose` | Unused vertices and wire edges (optionally faces sharing no edge), within a vertex mask | Blender: Clean Up > Delete Loose |
| `meshops::inset_region` | Outline edges (one selected face) move in along the face-plane inward direction, mitred at corners; a quad ring joins old and new outline | Blender: Inset Faces, Individual off |
| `meshops::face_area_center`, draw_hit's Face Center | The fan-triangle area centroid (signed, right for concave faces); snaps within 10 px, or takes a centre-based shape's first click with Start at Face Center | Plasticity: face centre snap |
| `render/dof.cpp` `apply_depth_of_field` | Linear depth from the depth buffer; CoC `R abs(d - s) / d / (s tan(fov/2)) * H/2` (the path tracer's thin lens); a Vogel-disc gather where a sample counts if its CoC reaches the pixel, a sample behind the pixel only by the pixel's own CoC; in linear light | EEVEE / HDRP scatter-as-gather DOF |
| `Camera::focus_track`, `update_camera_focus` | Pick Focus Point stores the world point; each frame the focus distance is its depth along the camera's forward | Blender: Depth of Field > Focus Object |

## Holes of any shape, Smart Fill, Auto Smooth, camera sequences (phase 14)

| Blendity | What it does | Blender / SketchUp equivalent |
|---|---|---|
| `annulus` check + `bridged_ring` | The angle zipper's faces must each have positive area along n, be simple, and add up to the ring's area (outer minus inner); otherwise two bridges from outer to inner vertices (crossing no side of either loop, nor each other) split the ring into two simple faces | SketchUp's face with a hole; Blender: Fill with holes |
| `push_through` wrapper | `push_through_raw`, then `tidy_new_geometry`'s weld and a b a spike removal (not Dissolve Degenerate, which made curved exits worse) | - |
| `meshops::smart_fill`, `merge_verts` | Open sides (used once, chained against their face's direction) and wire edges form loops; a loop with coincident neighbours or pairs is welded (repeat), one with no area and no pairs is left, one face's own outline is skipped; a flat loop becomes an n-gon, a bent one (> 0.1% off its plane) a fan from its centroid; finally coincident touched corners are welded | Blender: Fill / Fill Holes / Merge by Distance |
| `meshops::shallow_edges`, `shade_auto_smooth`, `Editor::auto_smooth_after` | An operator that increases the number of edges between faces meeting at 1-30 degrees on a mesh with `smooth` off and no per-face shading sets `smooth` and `smooth_angle` (render normals already split by angle) | Blender: Shade Auto Smooth |
| `Editor::start_render_sequence`, `step_render_sequence`, `render_camera_override_` | Cameras with `in_sequence`, stable-sorted by `sequence_order`; each frame: start the next camera's render (main_camera returns the override), and when it is no longer rendering write it with `write_render_file` | Blender: rendering several cameras via markers / scripts |

## Keymaps, Z-fighting check, UV editing, tool groups (phase 15)

| Blendity | What it does | Blender / Unity equivalent |
|---|---|---|
| `editor/keymap.cpp` | `shortcut_actions()` (id, label, category, context); `keymap_preset(name)` builds bindings for Unity / Blender / Maya / 3ds Max / SketchUp; `keymap_` = preset + `keymap_overrides_`; `handle_shortcuts` matches each pressed key + modifiers against the most specific active context first (Scene + Edit, Edit, Scene, anywhere) and calls `run_action`; `keymap_conflicts` checks overlapping contexts | Blender keymap editor and presets; Unity Shortcuts Manager |
| `meshops::zfight_pairs`, `editor/zfight.cpp` | Triangles of faces in one plane (sorted and swept along x), clipped in 2D for overlap area; per pair: coverage of each face, same direction, same polygon; suggestions (duplicate: one, back-to-back identical: both, fully covered: it). The editor runs it on the selection or the whole scene in world space and skips back-to-back contact between different objects | - |
| `Editor::uv_select`, `uv_transform`, `uv_sync_to_faces` | Selection by vertex / face (point in UV polygon) / island (`uvops::compute_islands`); transforms about the selection's bounds centre; Sync sets Edit Mode's faces from fully selected UV faces | Blender UV editor selection modes, UV Sync Selection, Align / Rotate / Flip |
| `edit_op_group`, `edit_op_pairs`, grouped `draw_edit_tools` | Every operator in a group (Select first); paired operators drawn as one split button; Extrude's Individual switch maps `extrude` to `extrude_individual` in `edit_op` | ProBuilder / Blender menus |
| `scene_render_hash` | Hashes each light with `hash_component` (every reflected field) | - |

## Open-space drawing, the UV gizmo, selective Merge by Distance (phase 16)

| Blendity | What it does | Blender / Plasticity / UModeler equivalent |
|---|---|---|
| `Editor::draw_space_plane`, `draw_hit` | Off the mesh (or always with `draw_space_only_`) the ray meets the chosen plane (ground / XY / YZ / facing the view through the orbit pivot / the last face drawn on), offset along its normal; in open-space-only mode mesh snaps are projected onto the plane | Plasticity construction planes; UModeler drawing off a face |
| UV gizmo in `draw_uv_editor` | Handles at the selection's bounds centre in screen space; a press on one starts the Move / Rotate / Scale drag with `uv_axis_` (0 free / uniform, 1 U, 2 V) | The Scene view gizmo; Blender's UV editor transform |
| `meshops::merge_by_distance_selected` | A spatial hash of candidates (unselected vertices first when `unselected`), each selected vertex welding to the first candidate within the distance; `finish_merge` rebuilds faces | Blender: Merge > By Distance (Unselected) |
| `Editor::zfight_prune`, `zfight_outlines_visible` | Issues keep their meshes' versions; changed or deleted objects drop theirs; outlines show in Edit Mode of an involved object or with the panel open after a panel check | - |

## Axis planes, ring repair, pinched regions, the teapot (phase 17)

| Blendity | What it does | Blender / SketchUp / UModeler equivalent |
|---|---|---|
| `Editor::draw_set_axis_plane` | X / Y / Z while drawing: the plane across that axis through the first point (before it) or the last point (mid-polyline); `DrawTool::bent` marks a 3D path, kept as wire edges | Axis locks on Extrude; drawing a Follow Me path |
| `meshops::repair_rings` | After moving vertices: flat(ish) regions of faces with moved and unmoved corners, bounded by exactly two loops, that fold over are zipped again with `annulus` (by angle, else bridged) | - (UModeler keeps a drawn shape's ring clean) |
| `meshops::region_wedges` | Corners of a selection grouped per vertex through edges inside it; Extrude and Inset Region make one new vertex per wedge, so a pinched region stays edge-manifold | Blender's bmesh extrude (which leaves the pinch non-manifold) |
| `primitives::teapot`, `build_starter_scene` | Newell's 32 bicubic patches (306 control points), z-up to y-up, welded through a hash of rounded positions; the starting scene of new projects | Blender's start-up file (cube, camera, light) |
| `Editor::delete_material` | Empties every slot using it (default material), trashes an asset's .mat, one undo step | Blender: material unlink / Unity: deleting a material asset |

## Guides, perpendicular snaps, selection frames, Fluent-style tools (phase 18)

| Blendity | What it does | Blender / Plasticity / UModeler / Fluent equivalent |
|---|---|---|
| `Editor::draw_axis_guide`, `draw_edge_guide` | X / Y / Z lay a `GuideLine` along the (local or global) axis through the last or hovered point; Ctrl+click lays one along the nearest edge on screen; polyline points on a guide may leave the plane (`DrawTool::bent`, the plane re-faces the view) | Plasticity construction lines |
| Perpendicular snap in `draw_hit` | The foot of the last point on each edge in the plane, when the mouse is near it | Plasticity's perpendicular snap |
| `Editor::edit_selection_frame` | Y = summed normal of the selected faces, X = their longest edge in that plane; used by the gizmo's Local / Scale and G/R/S axis locks in Edit Mode; a new Drawing object is re-based onto its first shape | Blender's Normal orientation |
| `meshops::repair_rings` (grown) | The ring region grows by flat neighbouring faces until the moved outline lies inside its outer loop | - |
| `triangulate_face` (quads) | The diagonal whose two triangles face the face's normal | Blender's beauty split (with a validity check) |
| `meshops::overlapping_vertices` / `overlapping_edges`, `Editor::overlap_*` | A hash grid of vertices within eps; a grid of edge boxes, pairs on one line sharing more than eps; cached per mesh version and drawn in Edit Mode | Blender: Mesh Analysis / Merge by Distance preview |
| `meshops::follow_path` | Follow along an ordered chain of any edges, started from the end that leaves the face along its normal (mirrored through its start if both run into it) | UModeler: Follow with picked edges |
| `meshops::grid_faces`, `pipe`, `array_faces`, `extrude_taper`, `recess` (mesh_tools5.cpp) | Bilinear quad grids with shared side splits; a circle swept by Follow; duplicate + offset; extrude + scale about the cap's centre; region inset + extrude | Fluent 4: Grid, Pipe, Array, Taper, Inset |
| Make Face (`fill`) | Smart Fill on the selection's open loops, else Fill through the selected vertices | Blender: F / Fill Holes |

## Follow Me along edges, Push/Pull on several faces (phase 19)

| Blendity | What it does | SketchUp / Blender equivalent |
|---|---|---|
| `meshops::follow_path` | The sweep along the picked chain where it is, from its end nearest the face (rings at each path point + the transported offset of the profile from the path's start) | SketchUp: Follow Me |
| `meshops::push_pull_multi` | Groups (connected faces, or single faces when individual), found again by centre + normal after each step since faces renumber; each pushed with `push_pull`; ones that refuse are skipped | Blender: Extrude Individual / SketchUp: one face at a time |
| `Editor::draw_space_plane` | Always the ground (the Open Space Plane is gone; guides replace it) | Plasticity's default construction plane |

## Bevel profile, cutting across faces, a continuous inset limit (phase 20)

| Blendity | What it does | Blender equivalent |
|---|---|---|
| `bevel_edges(..., profile)` | Arc points o + U x + V y on the superellipse x^r + y^r = 1, o = a + b - corner, r = 4p (p <= 0.5) or 1 / (1 - p) | Bevel > Profile (superellipse) |
| Open-path commit in `draw_commit` | Crossings with edges of faces in the drawing plane become path points; each piece is cut through its one face; the crossed edges' stretches inside the path + chord are dissolved | Knife across faces |
| `dissolve_edges` | A collinear corner is dropped only when no face outside the merged group uses it | Dissolve Edges (Dissolve Vertices) |
| `inset_region` | Bisection for the largest thickness where no new face turns over, the middle keeps 2% of its area and no ring quad is a bow-tie | Inset (with an overlap clamp) |

## Tool helpers, non-manifold Push/Pull (phase 21)

| Blendity | What it does | Blender equivalent |
|---|---|---|
| `modal_begin` / `modal_update` for grid, pipe, array, taper, recess | The Bevel / Inset drag helper generalised: the mouse drives `LastOp::amount`, the wheel the op's count or second value, X / Y / Z Array's axis; then F9 | Modal operators + Adjust Last Operation |
| `Editor::remember_last_op_settings` | F9 changes become the tool's defaults (the Inspector no longer holds tool numbers) | Blender remembers operator settings |
| Lean check in `push_pull_impl` | Per outline edge, only faces running it opposite to the region's face (carrying the surface on) count; it leans only when all of those do | - |
| `Editor::overlap_shorter` | Of an overlapping edge pair, the shorter is marked / selected | Mesh Analysis |

## Selection-limited Merge Coplanar, Array onto the surface (phase 22)

| Blendity | What it does | Blender equivalent |
|---|---|---|
| `dissolve_limited(m, angle, face_mask)` | Coplanar edges with a masked face on both sides; collinear corners only when every face using them is masked (found again by position after step 1) | Limited Dissolve on a selection |
| `array_faces` | Each copy's outline imprinted with `imprint_loop_across` (pieces dissolved into one); partly off the surface it is clipped (Sutherland-Hodgman) to the surface's convex outer outline first; wholly off, a duplicate; selection by area centres | Array + Knife Project |

## Key conventions

- **Coordinates:** Unity's left-handed, Y-up space. OBJ import/export mirror X and reverse the winding, as Unity's importer does.
- **Front faces:** `cross(b - a, c - a)` points outward. Faces wind clockwise on screen in a y-up view.
- **Pixels:** `0xAARRGGBB`, so Win32, X11 and CoreGraphics can all blit without conversion.
- **Idle CPU:** the editor redraws only on input, animation or timers (tooltip, caret). At idle it sleeps in the OS event wait.
- **Undo:** a scene-graph snapshot after each finished interaction. Meshes are shared and copied on first write.

## Camera filters (round 27, task 0004, ADR 0007)

| Piece | What it does | Reference |
|---|---|---|
| `CameraFilter` (`scene/scene.h`), `RetroConsoleFilter` | Components on a camera's GameObject (several allowed, in order). Each adds its raster settings, resolution and image passes to a `FilterStack`. The Console dropdown fills in the fields when it changes (it is reflected first, so a loaded file's values win) | Unity post-processing effects (one component per effect) |
| `render/camera_filter.h` `FilterStack`, `filter_layout`, `upscale_nearest` | The internal resolution: Fill keeps `height` rows at the view's aspect (square pixels); Letterbox renders `width x height` and fits it with bars. The image is scaled up nearest-neighbour; never larger than the view | - |
| `RasterOptions::vertex_snap`, `affine_uv`, `tex` (`TexOverride`) | Screen x / y rounded to the snap grid in the vertex stage, the near-clip path and the see-through pass. Affine UVs come from screen-space barycentrics (no 1/w); lighting keeps the perspective-correct position. Texture rules in `tex_sample`: filter override, the first mip level within the size cap as the base, no mipmaps = that one level | psx-spx "GPU Rendering" (integer vertices, affine texture mapping) |
| `apply_retro_image`, `retro_dither_offset` | Fog by linear depth (geometry only; skipped without depth), then the 4x4 dither offset (PS1 matrix -4..+3, or Bayer) and truncation to 5 bits per channel | psx-spx "Dither Matrix" |
| `Editor::render_camera`, `camera_filters` | The one camera render path: raster stage, internal-resolution target, DoF, passes, upscale; depth and ids scaled up too, and the renderer rebound (`Renderer3D::rebind`, stats kept) to the full view for overlays. The Scene view always uses Fill, so overlays match. The Game view, Camera Preview, F12 (raster), camera sequences, and the Scene view with `filters on` (the main camera's stack) all use it | - |
| `resolve_final_render`, the Rendered Camera Preview | A path-traced render through filters traces at the internal resolution, applies the passes (no fog) and scales up; float formats fall back to PNG | - |
| `nan_bits` / `finite_bits` (`core/math.h`) | NaN and finiteness from the bits, because the Windows build's `/fp:fast` may fold `std::isnan` / `std::isfinite` and NaN-rejecting compares | - |
| `Material::textures` warm-up | Resolving replaces shared pointers, so it must not happen under shading threads (an ASan use-after-free from the new tests). The rasterizer resolves every drawn material (and the default one) at the start of `flush`, in every shade mode; the path tracer at `build` and at the start of each `render` call (progressive renders span frames). A re-entrant lock covers any other first caller | - |

## The Camera Filters stack (round 29, task 0005, ADR 0008)

| Piece | What it does | Reference |
|---|---|---|
| `FilterEffect`, `FilterEffectBase<T>`, `filter_effect_infos` (`scene/scene.h/.cpp`) | One effect in a stack: type name, category, help, `reflect`, `contribute`. The registry lists them in menu order by category. A new effect is a subclass plus one entry | Unity post-processing effects |
| `CameraFilters` | The component: an ordered `FilterEffectList`, deep-copied on clone (undo, Duplicate, Play), contributing its enabled effects to the `FilterStack` in order | Unity Post-process Layer + profile |
| `Reflector::filter_effects` (default) | The type names as one `;`-separated text field, then each effect's fields through a `PrefixReflector` ("E0 Width"). Files, undo, `hash_component` and `set CameraFilters.E0Width` all work through it. A reader that brings other names rebuilds the list | Blender RNA collections |
| `InspectorReflector::filter_effects`, `Editor::filter_stack_op` | The stack UI: a foldout per effect (toggle, menu: Move Up / Down, Reset, Remove) and Add Filter grouped by category. Menu items call `filter_stack_op` by object id (they run at the end of the frame); so does the `filter` console command | Unity's Add Effect menu |
| Legacy load in `load_scene_text`; `component Retro Console Filter`; `set RetroConsole.<Field>` | Task 0004's component loads into a stack holding a Retro Console effect; the old console name adds one; `set` reaches the first effect of a type | - |
| `RetroConsoleFilter` presets | PS1; N64 (3-point, 64 px textures, mipmaps, dither, fog); Saturn (320x224, snap, affine, screen-door, 15-bit); DOS (320x200, affine, 256 colours with Bayer dither) | psx-spx; N64 RDP docs (3-point filter, 4 KB TMEM); Sega VDP1 manual (mesh); VGA mode 13h |
| `TexFilter::ThreePoint` (`Texture::sample_level`) | The texel square split on its diagonal: a plane through the three nearest corners | N64 RDP texture filter |
| `RasterOptions::screen_door` | Transparent surfaces draw every other pixel opaque ((x + y) odd skipped), no blending; alpha < 0.1 vanishes | Saturn VDP1 mesh |
| `retro_palette`, `retro_palette_nearest`, `Palette256` | A 6x6x6 cube plus 40 greys; the nearest entry computed exactly: each channel rounded to the cube, the nearest grey to the mean, the nearer of the two; the dither spans one cube step | VGA 256-colour palettes |

## Piloting is the game camera (round 30, task 0009)

| Piece | What it does | Reference |
|---|---|---|
| `render_scene_view` piloting branch, `pilot_rt_` | The frame (`pilot_frame_rect`) is drawn by `render_camera` with the camera's own view, projection, exposure, DoF and filters; a dark passepartout around it. Ids are copied into `scene_rt_` (click-picking reads the pixels shown, so it always works) and depth is re-projected into the editor's projection (its near / far differ). For a centred perspective camera the editor's projection over the view equals the camera's over the frame (`scale_fov`), so the remaining tools line up; with an orthographic camera, lens shift or a letterboxing filter they don't, which is acceptable because the gizmos are hidden and Edit Mode ends piloting | Unity Game view; Blender camera view |
| `piloted_camera()`; the overlay skip in the Scene view's draw | While piloting: no grid, icons, origins, guides, overlap / Z-fighting overlays, transform or navigation gizmo, statistics. `draw_pilot_frame` keeps its banner and a 1 px outline outside the frame | - |

## Color, Lens and Stylize effects (round 31, task 0006)

| Piece | What it does | Reference |
|---|---|---|
| The `ColorGradingEffect` ... `SharpenEffect` classes (`scene/scene.h/.cpp`) | Twelve `FilterEffect`s registered under Color, Lens and Stylize. Each pushes one image pass with its settings captured by value | Unity post-processing effects |
| `apply_color_grading` | sRGB decoded through a 256-entry table to linear light. In order: white balance gains, exposure (2^stops), a contrast power curve through 0.18, saturation around Rec. 709 luma, then ASC CDL-style lift / gain / gamma. Encoded with the exact `linear_to_srgb8`. Neutral settings return untouched | ACES / Unity grading; ASC CDL |
| `apply_posterize`, `apply_grayscale`, `apply_invert` | Per channel, on display values: posterize rounds to N levels; Rec. 709 luma or the classic sepia matrix, mixed by amount; invert mixed by amount | - |
| `apply_vignette`, `apply_chromatic_aberration`, `apply_lens_distortion` | Radial from the centre. The vignette uses a smoothstep from (1 - smoothness) to the corners, with roundness blending frame and circle. Aberration samples red outward and blue inward. Distortion uses r' = r / ((1 + k r^2) s): k < 0 barrel, k > 0 pincushion, black outside | Brown-Conrady radial model |
| `apply_film_grain`, `apply_crt`, `FilterFrame::time / animate` | Grain: hashed noise per cell, strongest in the midtones, re-seeded 24 times a second only while animating. CRT: barrel curvature to black corners, darker odd rows, an RGB aperture grille, and a 30 Hz flicker only while animating. The editor animates in Play mode only | - |
| `apply_pixelate`, `apply_edge_outline`, `apply_sharpen` | Pixelate takes each cell's centre pixel. Outline marks a pixel where, `thickness` pixels away, the id differs or the distance along the view jumps by more than `depth_sensitivity` (relative). It needs depth, so it skips path-traced output. Sharpen is a 4-neighbour unsharp mask | Sobel-free edge detection as in toon post-processing |
| `Source` (camera_filter.cpp) | A copy of the colour that neighbour-reading passes read from while they rewrite the target; clamped and bilinear reads | - |

## Bloom and the HDR plane (round 32, task 0007)

| Piece | What it does | Reference |
|---|---|---|
| `RenderTarget::want_hdr / hdr / hdr_view_transform / hdr_exposure` | Linear light before exposure and tone mapping, allocated in `Renderer3D::begin` only when asked (Deferred mode). `shade_deferred` writes it from its row buffer; `clear_environment` writes the HDRI / physical sky radiance; a plain sky and pixels drawn over by `draw_see_through` hold x = -1. `render_camera` asks when the stack `needs_hdr`. `fill_hdr_from_linear` fills it from the path tracer's `linear_rgb()` for filtered F12 renders and the Rendered Camera Preview | EEVEE / Unity HDR render targets |
| `apply_bloom`, `BloomEffect` | A bright pass with Unity's soft knee and a firefly clamp. The bright pixels are area-averaged to a fixed 540-row base (so the glow covers the same part of the frame at any resolution), then 13-tap downsamples to about 2 px, then back up with a 4-tap tent mixed by Scatter. A pixel whose colour still encodes its HDR value is re-encoded with the glow added in linear light; one an earlier pass changed (posterize, dither, depth of field) gets the glow added in display light on top, so the stack order holds. Pixels it doesn't reach keep their exact value. `clear_environment` writes the sky's radiance into the plane (no seam at silhouettes). The buffers are reused between frames and shrunk when over twice the need | Jimenez 2014, "Next Generation Post Processing in Call of Duty: Advanced Warfare"; Unity URP bloom |

## Probe volumes (round 37, task 0014)

| Piece | What it does | Reference |
|---|---|---|
| `probe_place`, `ProbeVolumeData` (`render/probe_volume.h/.cpp`) | Top cells of 3 x max spacing split where triangles come near, down to 3 x min spacing; each leaf a brick of 4 x 4 x 4 probes; an octree per top cell finds a point's brick | Unity 6 Adaptive Probe Volumes (bricks, min / max spacing) |
| `ProbeCube` | Per probe, six directions: light from geometry (irradiance / pi) and the sky share each axis sees; evaluated with n squared weights; the sky's light applied when shading (relights with the world) | Valve's ambient cube (McTaggart 2004); APV sky occlusion |
| `probe_validate`, `probe_bake` | Back-face rays find probes inside geometry: moved past the nearest back face (virtual offset) or marked invalid and dilated; the bake gathers path-traced light from geometry, the sky share and Baked lights' direct light, in slices (`start`, `count`), and the last slice dilates (`probe_dilate`) | APV validity and virtual offset |
| `lightmap_layout`, `LightmapUvCache` (`render/lightmapper.h`) | The atlas step of a bake on its own: charts sized from world area x texels per unit, shelf-packed pages, entries and covered texels; unwrapped UVs kept per object between calls | Task 0012's baker, task 0016 |
| `RealtimeLightmaps`, `realtime_lightmap_layout` / `_update` (`render/realtime_lightmap.*`) | Contribute GI objects' realtime lightmaps: texels gather through voxel GI in slices, a finished pass is blurred within charts, dilated and published; `DrawItem::rt_bounce / rt_sky / rt_uv` make the per-pixel pass read them instead of tracing | Unity's realtime GI (Enlighten), ADR 0011 amendment |
| `Editor::lighting_frozen`, `lighting_update`, `preview_step` (`editor/lighting.cpp`) | Task 0020: Lighting Updates Manual freezes live GI (the last realtime lightmaps and probes, and stale lightmaps, stay shown); Update Lighting runs it once; Preview Lighting bakes a rough Lightmapper pass (quarter resolution, 16 / 64 / 256 samples) after changes settle, shown through `shown_lighting()` and never saved | Bakery's Realtime Preview |
| `HalftoneEffect`, `HatchingEffect`, `LineArtEffect`, `PaperEffect`, `ColorRampEffect` (`scene/scene.*`), `apply_halftone` / `apply_hatching` / `apply_line_art` / `apply_paper` / `apply_color_ramp` (`render/camera_filter.*`), `filter_looks` / `apply_filter_look`, `PathTracer::first_hit_planes` | Anime and manga filters (task 0039): Line Art rebuilds surface points and normals from the depth plane (creases by angle); the path tracers give the filters depth and ids from one unjittered ray per pixel centre (cached per build and camera), so lines match the rasterizer's; looks append ready-made stacks | Goo Engine's line art and halftone; Blender's Color Ramp |
| `Material::shading_model` + `toon_*`, `toon_level` / `shade_toon` / `toon_rim` (`render/shading.*`), `SurfaceSample::toon`, `Outline` (`scene/scene.*`), `DrawItem::always_cull`, `Material::outline_hull` | Toon shading and outlines (task 0038, ADR 0020): one toon response for the rasterizer, the CPU path tracer and the GPU kernel at the camera's hits (Lambert elsewhere); the inverted hull as its own draw item, seen by camera rays only in the tracers; mirrored items cull by their own winding | Goo Engine; Blender's Shader to RGB, Solidify (Flip Normals) outlines |
| `gpu::take_renderer` / `open_renderer` / `return_renderer`, `renderers_generation`, `set_background_open_default`, `PathTracer::adopt_ready_gpus` / `gpu_starting` / `gpu_gather_waiting`, the pipeline cache (`render/gpu_device.*`) | GPU devices opened in the background (task 0037, ADR 0021): a pool of ready renderers with a spare per device; tracers in the interactive editor trace on the CPU while theirs opens and adopt it when ready; bakes do a slice on the CPU instead of waiting; a VkPipelineCache per GPU on disk with a checked trailer | Cycles' device threads; Unity's async shader compilation |
| `gpu::Compute` (`render/gpu_device.*`) | A general Vulkan compute kernel: GLSL compiled once (cached on disk), 16 storage buffers, dispatch, read-back; its own device | Task 0018, ADR 0014 |
| `VoxelGiGpu` (`render/voxel_gi_gpu.*`) | voxel_gi_gather as a compute kernel (level-0 column walk, RSM back-projection, sky weights) for batches of points; grid / RSM / world uploaded on change; used by realtime lightmaps and live probes (GI Device) | Task 0018 |
| `PathTracer::gpu_gather`, kernel bake mode (`GParams::env_i` z / w) | The path tracer's kernel tracing from points instead of a camera (cosine around a normal, or a direction); Generate Lighting's texels and probe rays on the GPU | Task 0018 |
| `Editor::live_gi_needed` (`editor/lighting.cpp`) | Whether Realtime GI has live work: not when a current bake holds every light that bounces; gates the grid, RSM, per-pixel pass, live probes and realtime lightmaps; the Lighting window shows why | Task 0017 |
| `probe_live_update`, `Editor::update_probes_live` | With Realtime GI, slices of 4,096 probes per frame gather again through the voxel grid (six hemispheres) when the grid, sun, world or settings change; live-only placement without a bake | Task 0013's voxel GI |
| `ProbeFrame`, sampling in `shade_deferred` | Per pixel at the point pushed off along the normal and toward the eye, trilinear over the brick's valid probes with normal-based leak reduction; `light_surface` order: lightmap, probes, voxel GI, sky | APV normal / view bias |

## Voxel GI (round 36, task 0013)

| Piece | What it does | Reference |
|---|---|---|
| `VoxelGrid`, `voxel_grid_fit`, `voxelize_mesh`, `voxel_grid_build` (`render/voxel_gi.h/.cpp`) | Binary boundary voxelization: columns along Y, 128 bits deep, n x n (64 / 128 / 256); power-of-two voxels, origin snapped to 8 voxels. An exact conservative span voxelizer (triangle clipped to each column). Levels OR 2 x 2 columns, full depth kept | Thiedemann et al. 2011, sections 3 and 4 (atlas voxelization replaced) |
| `voxel_trace` (+ `voxel_trace_columns`, `voxel_trace_dda` references) | The hierarchical ray test: skip empty columns at coarse levels, descend on a hit, first set bit along the ray; starts at the level as wide as the ray is long | Section 4.1 / 4.2; Amanatides & Woo 1987 for the reference |
| `Rsm`, `render_rsm`, `Renderer3D::set_rsm_output` | The sun's reflective shadow map: deferred shading's side output of position, normal and reflected light, with the shadow map's fit | Dachsbacher & Stamminger 2005; section 5.1 |
| `voxel_gi_gather`, the GI pass in `shade_deferred` | Per receiver (1/2 or 1/4 resolution, 16 interleaved deterministic Hammersley ray sets): bounce = RSM light at hits the sun sees (eps = max(sqrt(3) v, s / cos)), sky = escaping share of the sky's radiance. 4 x 4 geometry-aware blur, upsampled per pixel; `light_surface` picks baked map (+ bounce the bake lacks) > voxel GI > sky | Section 5.1, figure 7 and 12 |
| `Editor::update_voxel_gi`, `auto_generate_step` | Per-object span cache (render-mesh serial, matrix, grid key), RSM cache (casters, materials, sun); Auto Generate re-bakes a second after the lighting stops changing | ADR 0011 |

## Baked lighting (round 35, task 0012)

| Piece | What it does | Reference |
|---|---|---|
| `Lightmapper` (`render/lightmapper.h/.cpp`) | Lays out a square chart per Contribute GI object (side = sqrt(world area) x texels per unit x Scale In Lightmap), shelf-packed into the smallest power-of-two page that holds them (else max-size pages). It rasterizes each chart's triangles into texels (world position and normal), then bakes per texel: cosine-weighted `PathTracer::incoming_radiance` rays (static objects, Baked and Mixed lights, the world) plus Baked lights' direct light with shadow rays. A filter within one object's chart and facing, and dilation into the padding. Progressive (`step(budget)`), cancellable, on its own copies of the meshes | Unity's Progressive Lightmapper; Cycles' baking |
| `lightmap_uvs`, `RenderMesh::tri_corner` | Lightmap UVs per mesh corner (Smart UV Project + Pack Islands at the chart's resolution, or the mesh's own UVs), stored in the bake per render-mesh triangle corner, so drawing needs no extra vertex split | Unity's Generate Lightmap UVs; Blender's Lightmap Pack |
| `LightingData`, `Editor::attach_lightmap`, `lightmap_content_hash` | Pages (`Lightmap`, sampled bilinear), per-object entries (content hash, page, UVs), the baked light modes and a scene key; saved as `Lightmap-N.hdr` + `LightingData.bin` in `<scene folder>/<scene name>/` (ADR 0010). A drawn object gets its map only while its hash matches; otherwise realtime ambient and "Lighting out of date" | Unity's Lighting Data Asset |
| `Renderer3D::light_surface` | A lightmapped surface: diffuse ambient = albedo x lightmap (irradiance / pi, replacing the sky's SH), Baked lights skipped (in the map), Mixed lights realtime (Baked Indirect) | Unity's Mixed Lighting: Baked Indirect |
| `LightingSettings`, Lighting window, `bake start/cancel/clear` | Scene settings (resolution, max size, padding, samples, bounces, denoise, indirect intensity, auto generate), the Generate Lighting / Clear Baked Data buttons and progress | Unity's Lighting window |

## Editor render caches (round 34, task 0011)

| Piece | What it does | Reference |
|---|---|---|
| `Editor::scene_view_key`, `frame_scene_hash`, `ViewCache` (`render_view.cpp`) | A view reuses its picture while a content hash of what it shows is unchanged (ADR 0009). The hash covers the scene (`scene_render_hash`: evaluated meshes, matrices, materials, lights, world), the MeshRenderer flags, and the render and world settings. The Scene view adds its camera, toggles, selection and Edit Mode state; the Game view and Camera Preview use `camera_render_hash`. On a hit the image is copied back, and the Scene view's depth and ids stay for picking. Never cached: Rendered shading, piloting, Play while running. `BLENDITY_RENDER_CACHE_VERIFY=1` renders anyway and compares | Blender's draw manager redraws only tagged regions; Unity's Scene view repaints on change |
| `Editor::update_shadow_map`, `ShadowEntry` | The sun's shadow map, kept per caster set: render-mesh serials, matrices, materials, light direction, resolution. Two entries, least recently used out (the Scene and Game views collect different meshes) | EEVEE's shadow update tagging |
| `Component::render_role`, `GameObject::find_role` / `mesh_renderer()` / `light()` / `camera()`, `GameObject::transform_version`, `render_memo`, `evaluated_keys`; `Editor::object_render_hash` (`scene.h`, `render_view.cpp`) | The scene hash's per-object memo (ADR 0015): a component says what the views read of it (one virtual call instead of a `dynamic_cast` per type); an object remembers the hash of its world matrix, flags and materials, keyed by a transform version (bumped wherever `mark_dirty` runs, before its early return) and the material pointers and versions; lights, probe volumes and the modifiers (through the three evaluated-mesh keys, computed in one pass) are hashed every time. `scene_render_hash(false)` hashes from scratch for the tests | Blender's depsgraph tags; Unity's `Transform.hasChanged` |
| `Editor::camera_filters` (cached), `FilterTarget` (`scene_ft_` ...), `FrameProfile::filter_stack_builds` / `filter_target_resizes`, `evaluate_material(const Material &, const Material::Resolved &, const SurfacePoint &)`, `Renderer3D::visible_` / `big_` / `see_through_` / `vjobs_` | Render-frame waste removed in task 0024: a camera's filter stack is built once and kept by the owner's id until its filter components' hash changes; each view that can be filtered owns the internal image it draws at the stack's resolution; the rasterizer resolves a material's textures once per flush and carries the pointer per triangle; the per-flush scratch is kept between flushes; the sun's shadow casters are copied only when the map is rendered | - |
| `Scene::active_camera`, `Editor::set_active_camera`, `main_camera` (override → active → "Main Camera" → first), `PathTracer::first_hit_planes`, `PTSettings::solid_background`, `Editor::trace_filter_frame` (`render_view.cpp`) | The explicit Active Camera (ADR 0016) and the path tracer's depth / id planes: a traced picture (filtered F12, the Rendered Camera Preview) gets a `FilterFrame` with the rasterizer's z / w and object ids, so outlines and fog run on it; a camera's Solid Color reaches the tracers for camera rays only; the Scene view's filters look through the active camera for exposure, clear colour and far clip | Blender's scene camera, Cycles' depth / object-index passes; Unity's MainCamera tag |
| `FilterFrame::pixel_scale`, `scaled_px` (`camera_filter.cpp`), `filter_layout` (renders at the stack's resolution, box-averaged into a smaller view), `RenderSettings::reflect_raster` | Filters look the same at every picture size (task 0026): pixel-sized parameters scale with height / 1080; a view smaller than its stack's resolution still renders at it; the views' cache keys hash only the raster-relevant render settings | Unity's post-processing uses normalised screen units; Blender's compositor works at render size |
| `src/texture/`: `ProceduralTexture`, `TextureLayer` (+ registry `texture_layer_infos`), `TexPlanes`, `procedural_texture_output`; `Reflector::layer_list`; `texture_asset` / `create_texture_asset` / `save_dirty_texture_assets` (`scene_io.cpp`); `material_textures_generation()` | Procedural texture assets (ADR 0017): a `.ptex` holds a layer stack evaluated in parallel to height / albedo / roughness / mask planes and then to albedo, normal (Sobel of the height), roughness and height images, cached per asset path and hash; materials reference an output as `X.ptex#albedo`; the views' render hash includes the material-texture generation so a texture edit redraws them; exports write the outputs as PNGs | Blender's texture nodes (brick, noise, voronoi, wave); Substance Designer's layer stacks |
| `Editor::draw_textures_window` (`texture_window.cpp`), `draw_texture_fields`, `texture_stack_op`, `InspectorReflector::layer_list` (`panels.cpp`), `WindowKind::Textures` | The Textures window (task 0028): thumbnails, presets, the stack drawn by the Inspector's reflector (like the filter stack), a preview at 256 while dragging and full size on release; stack edits through `texture_stack_op` (shared with `texlayer`), each one undo step through the texture texts in `UndoState` | Blender's modifier stack; Substance's layer panel |
| `Material::axis_*`, `Reflector::material_ref`, `Material::Resolved::axis` / `axis_tx`, `evaluate_material`'s axis branch over `evaluate_surface(..., force_box)` (`shading.cpp`), `GMaterial::axis` / `axis_i` and the kernel's `evaluate_material` / `evaluate_plain` | Axis materials (ADR 0018): parts by asset path resolved with the parent before shading, weights by the slope of the World or Local normal, each part triplanar, blended; the GPU appends the parts to its material table; the views' hash mixes the parts' versions | Unity's triplanar terrain shaders; Blender's Geometry Normal + Map Range node setups |
| `area_light_cosine`, `area_light_specular_dir`, `area_light_point`, `light_range_falloff` (`raster.h/.cpp`), `brdf_eval_parts` (`shading.cpp`) | An area light by its whole surface (task 0030): the unshadowed mean of n.L times its facing, integrated over the part in front of the surface's horizon (tan-substituted Gauss-Legendre; rays from the centre for an ellipse); the rasterizer's diffuse, the probes' axes and the lightmapper's unshadowed term (its shadow rays only weigh the visible share) | Cycles' area light sampling; Karis 2013 (representative point); Heitz et al. 2018 (ratio estimator) |
| `LiveTexture`, `TextureLayer::apply_point` / `make_aux` / `remap`, `LayerAux`, `procedural_texture_live`, `procedural_texture_set_preview` (`src/texture/`), `Material::Resolved::live`, `SurfacePoint::live_textures` / `footprint` | Live procedural textures (task 0031, ADR 0017 amended): an immutable snapshot of a texture's stack and of the planes its neighbourhood filters need, taken with its images and shared by the materials; the rasterizer evaluates it per pixel where the image would be magnified; a drag evaluates the images at 256 | Blender's shader nodes evaluated per sample; Substance's baked outputs |
| `PTSettings::Retro`, `PathTracer::retro_screen` / `retro_affine_uv`, the kernel's `retro_screen` / `retro_affine_uv`, `retro_for_trace` (`render_view.cpp`) | A camera filter's rasterizer stages in the path tracers (task 0032, ADR 0008 amended): snapped mesh copies for vertex snap; affine UVs and the screen door at camera-ray hits from the hit's place on the filter's image; texture rules on every hit (GPU: capped level, override filter) | The PS1 / Saturn hardware the Retro filter imitates |
| `Camera::show_guides` / `guide_*`, `Editor::draw_camera_guides` (`object_snap.cpp`), `Reflector::hashing` | Composition guides (task 0033) drawn over a camera's frame in the Game view, the Camera Preview and the piloted frame; reflected for files and undo but skipped by the change-detection hash, so toggling them never re-renders | Blender's camera Composition Guides and Safe Areas |
| `bake_textures`, `write_baked_textures`, `baked_material` (`src/render/texture_bake.h`), `Editor::bake_object_textures` / `bake_textures_for_export`, `ExportOptions::bake_textures`, `encode_png`, `texture_cache_forget` | Texture bake (task 0036, ADR 0019): the lightmapper's UVs (or the mesh's own), each texel's owning triangle, the material evaluated at 2 x 2 points of it on the original surface (UV derivatives and live-texture footprints per triangle), the normal written in the tangent frame of the mesh rebuilt on the baked UVs, padding by dilation; in the scene a new `<Object> Baked` asset, on export copies; glTF packs roughness and metallic into one embedded PNG, FBX links Texture / Video objects by property | Blender's Cycles Bake; Substance's mesh baker |
| `RenderSettings::transparent`, `PathTracer::coverage`, `Editor::render_with_alpha` / `apply_trace_alpha`, `write_png(..., alpha)`, `write_exr(..., alpha)` | Transparent renders (task 0035): the tracers' camera misses add nothing and their hits are counted per pixel (CPU arrays, a third region of the GPU's auxiliary buffer); the rasterizer renders over black and white and takes alpha from the difference; RGBA PNG and EXR | Blender's Film > Transparent |
| `ui::Context::draw_icon` masks, `Editor::project_dir_cache_`, `loaded_material_assets()` signature, the dock's fills (`ui.cpp`, `project_window.cpp`, `scene_io.cpp`, `editor.cpp`) | Idle-frame costs found in pass 7 (`docs/PERFORMANCE.md`): an icon is rasterized from its primitives once per size and blended through a coverage mask; the Project window's folder tree lists directories once per refresh, not per frame; the material library's sorted listing is rebuilt only when its entries change; the frame fills only the border strips and the dock paints no panel behind the Scene and Game views (they paint every pixel) | - |
| `RenderMesh::serial` | New for every render-mesh build and every in-place deform, never reused (unlike pointer + version after a free); 0 = not cacheable. What caches of drawn geometry key on | - |
| `RenderMesh::build_serial`, `Mesh::deform_version`, `Component::is_deform_only` | A stack's deforming tail (after its last topology-changing modifier) re-runs on a kept mesh and bumps `deform_version`; the render mesh then moves its positions, normals and tangents in place (`build_serial` stays), or rebuilds when the split pattern no longer holds | ADR 0022 |
| `FrameProfile`, `Editor::frame_profile()` / `frame_profile_totals()` | Per frame: time in the Scene view (and its overlays), Game view, preview, shadow map, UI and present; how many shadow renders, view renders and cache hits. Shown by stress and tests | Unity's Profiler |
| `Renderer3D::clear_planes`, `begin_overlay_batch` / `end_overlay_batch` | Clears in one parallel pass; the visibility plane only for deferred shading. Overlay lines and points recorded, binned into 32-row bands and drawn in parallel, in submission order per pixel (bit-identical to one by one) | - |

## What isn't recreated

Rendering runs on the CPU (Blender's EEVEE uses the GPU; Cycles can use either), and materials are a fixed Principled BSDF rather than node graphs. Out of scope so far: sculpting, rigging/animation/NLA, the node systems (shader, geometry, compositor), grease pencil, the video sequencer, the Python API, `.blend` files and Alembic caches, volumes, hair, and caustics or subsurface scattering in the path tracer (glass refracts, but light through it reaches surfaces only along paths, as in Cycles without caustics). The Learn tab explains the concepts behind several of them where they relate to Unity.
