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
tests/          blendity_tests   - 613 checks with Blender's libraries
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
| `push_through`, `fuse_contacts` | Hole along a face's normal, imprinted on whatever face it exits through (an annulus of faces cut into it; the Boolean solver for several exit faces). Faces extruded or moved onto another face merge into it | `bmesh/operators/bmo_*` + Boolean (`geometry/intern/mesh_boolean_manifold.cc`) | — |
| `subdivide_edges`, `connect_vertices`, `dissolve_edges`, `collapse_edges` | Edge Subdivide, Connect (J), Dissolve, Collapse | `bmo_subdivide.cc`, `bmo_connect.cc`, `bmo_dissolve.cc` | ProBuilder edge actions |
| `scene/mesh_internal.h` | Shared topology helpers (FaceBuilder, EdgeFaces) | BMesh's radial loop cycles | — |
| `Editor::LastOp`, `run_last_op`, `draw_last_op_panel` | Adjust Last Operation: re-runs the operator from a copy of the mesh with new values and an X / Y / Z / normal move, replacing one undo step | `windowmanager/intern/wm_operators.cc` (`WM_operator_last_redo`), `editors/undo` | — |
| `ui::eval_number`, `FieldEdit` / `ApplyEditReflector` (`editor/panels.cpp`) | Expressions in number fields (`+=`, `*=`, functions, `L()`, `R()`) and multi-object editing through the same reflection that draws the Inspector | `editors/interface` (numeric input), RNA multi-editing (Alt+drag) | Inspector expressions, multi-object editing |
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
| `meshops::shell`, `draft`, `radial_array`, `fillet_polygon` (`mesh_tools4.cpp`) | Shell = remove faces + inward solidify with rims; Draft moves each vertex of a side face in by its height above the selection's base times tan(angle); Radial copies about an axis; fillets replace corners with arcs tangent to both sides, shrinking to fit | Plasticity Shell, Thicken, Draft Face, Radial Array, Fillet Curve |
| `editor/material_window.cpp` | A software-shaded preview sphere per material (Lambert + Blinn-Phong, its base texture wrapped on, alpha over a checker), cached by version; a grid to pick, drag or right-click, and the material's own fields underneath | Unity Project previews + material Inspector; Blender's Material Browser |
| `editor/tools_window.cpp`, `Editor::draw_edit_tools` | A dockable window of all modeling tools; Edit Mode's tools are one function shared with the Inspector | UModeler / ProBuilder tool windows |
| `editor/object_snap.cpp` (`raycast_surface`, `rest_on_surface`, `nearest_vertex_on_screen`) | Surface snap: the hit point and normal (excluding what moves); the object turns to the normal by the shortest arc if asked, then its lowest point along the normal touches the surface. Vertex snap: the selection's vertex nearest the press is the anchor, the nearest other vertex on screen (24 px) the target | Unity Ctrl+Shift surface snap and V vertex snap; Blender snapping |
| `toggle_pilot_camera`, `update_pilot_camera`, `draw_pilot_frame` | The Scene camera takes the camera's position, yaw / pitch and vertical FOV; afterwards the camera object follows the view each frame (FOV, or focal length on a physical camera); a passepartout with thirds guides | Blender Lock Camera to View; Unity Align With View |

## Key conventions

- **Coordinates:** Unity's left-handed, Y-up space. OBJ import/export mirror X and reverse the winding, as Unity's importer does.
- **Front faces:** `cross(b - a, c - a)` points outward. Faces wind clockwise on screen in a y-up view.
- **Pixels:** `0xAARRGGBB`, so Win32, X11 and CoreGraphics can all blit without conversion.
- **Idle CPU:** the editor redraws only on input, animation or timers (tooltip, caret). At idle it sleeps in the OS event wait.
- **Undo:** a scene-graph snapshot after each finished interaction. Meshes are shared and copied on first write.

## What isn't recreated

Rendering runs on the CPU (Blender's EEVEE uses the GPU; Cycles can use either), and materials are a fixed Principled BSDF rather than node graphs. Out of scope so far: sculpting, rigging/animation/NLA, the node systems (shader, geometry, compositor), grease pencil, the video sequencer, the Python API, `.blend` files and Alembic caches, volumes, hair, and caustics or subsurface scattering in the path tracer (glass refracts, but light through it reaches surfaces only along paths, as in Cycles without caustics). The Learn tab explains the concepts behind several of them where they relate to Unity.
