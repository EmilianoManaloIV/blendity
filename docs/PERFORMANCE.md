# Performance, limits and efficiencies

All numbers come from `blendity_stress` (full run, about 130 s with Blender's libraries, which add the library-vs-fallback comparisons) on the development machine: **AMD Ryzen 9 5900XT (16 cores / 32 threads), Windows 11, MSVC 14.51 `/O2`**. Re-run it on your own machine:

```
dist/windows/blendity_stress            # full run → stress/results/report_<date>.md + .csv
dist/windows/blendity_stress --quick    # ~3 s (CI)
dist/windows/blendity_stress --only raster
```

The raw reports are in [`stress/results/`](../stress/results/): the first pass in `report_20261006_001100.md`, the rendering/UV/modeling pass in `report_20261006_091049.md`, and the libraries + bare-metal pass in `report_20261006_233746.md` (`latest.md`, about 130 s).

## Pass 6: editor frames (task 0011)

`blendity_stress --only editor_render` draws the frames an editor spends its time on, in a working
scene:
- **Scene:** 200 icospheres, a floor, a sun with shadows, a 50k-face sphere.
- **Layout:** "2 by 3", so the Scene and Game views are both visible.
- **Camera Preview:** on (the Main Camera is selected).
- **Size:** 1600×1000, headless.

Before and after, Linux, dependency-free build, same machine:

| Frame | Before (ms) | After (ms) | What it renders now |
|---|---|---|---|
| Idle (nothing changed) | 47.3 | **2.1** | nothing: the three views are copied back |
| Mouse moving over the Hierarchy | 44.2 | **2.1** | nothing |
| Orbiting the Scene view | 43.6 | **9.9** | the Scene view; no shadow map |
| Moving an object | 45.1 | **24.7** | three views and one shadow map |
| Editing a material colour | 45.4 | **27.0** | three views and one shadow map |
| Playing | 32.5 | **14.8** | the Scene and Game views (the preview hides in Play) |

### What was found
- **The shadow map was never cached.** `docs/PERFORMANCE.md` and the stress note both said it was. It
  was rendered on every shaded render, up to three times a frame, at about 8 ms each. It is now kept
  per caster set, two entries (ADR 0009).
- **Any input re-rendered every visible view.** A mouse move over a panel, a caret blink or a tooltip
  timer was enough. A view now reuses its picture while a content hash of what it shows is unchanged
  (ADR 0009). A verify mode re-renders and compares, and the whole unit suite runs clean in it.
- **Clears were serial:** about 12 bytes a pixel on one thread. They now run as one parallel pass, and
  the visibility plane exists only for deferred shading.
- **Overlay lines and points were drawn one by one.** The grid alone is about 2,900 lines. They are
  now binned into 32-row bands drawn in parallel. Each pixel still sees them in order, so the picture
  is bit-identical. Overlays went from 1.15 to 0.77 ms in a normal frame, and from 17.5 to 12.4 ms with
  a 50k-face Edit Mode cage.
- **The idle cost with many objects is the scene hash:** 1.9 ms at 1,000 meshes and 5.1 ms at 10,000
  (`--only editor_ui`).
- **Edit Mode on a 50k-face mesh is still slow (about 280 ms a change).**
  - Rebuilding its render mesh costs 13–17 ms and its tangents 28–32 ms.
  - Most of the rest is the overlapping-vertex check. It runs when the mesh changes outside a drag (a
    real gizmo drag skips it).
  - A positions-only mesh update and a cheaper overlap check are the follow-up; they need an ADR
    because they change what a mesh version means (ADR 0003).

## Pass 5: Push/Pull on odd meshes

`blendity_stress --only pushpull` runs `meshops::push_pull` on 26 awkward meshes:
- **Shapes:** cylinders with 32- and 128-gon caps, a cone apex, UV and ico spheres, a torus, an L-shaped and a star-shaped (concave) prism, a non-planar face, a sheared box, a wedge, a subdivided cube, a cube with a tunnel, and a thin wall.
- **Broken or unusual topology:** an open quad and grid, a cube with inside-out normals, unwelded faces, collinear corners plus a sliver, and a non-manifold fin.
- **Scale and position:** cubes at 0.1 mm and 10 km, and one 200 km from the origin.

Every face is tried, plus random groups of 2 - 6 faces, the whole mesh and disconnected pairs. The distances run from 1e-9 to 1,000 x the mesh size, exactly at and around the hole and join limits, and include NaN and infinity, each with and without Ctrl (new face). Every result is checked for:
- bad indices, NaN and repeated corners;
- a closed mesh coming out open;
- volume moving the wrong way, or the solid turning inside out;
- new zero-area faces.

A second part drives the interactive operator in a headless editor: P, a mouse sweep through the snaps, then a click, Esc or a right-click. Undo or the cancel must restore the exact mesh.

| Round | Problems in ~27,000 operations | Fixed |
|---|---|---|
| First run | 7,088 | — |
| Non-finite distances refused | 3,344 fewer | NaN / inf filled the mesh with NaN |
| Neighbour-stretch cleanup kept shared corners | 347 open meshes → 0 | a T-junction where a stretched face dropped a corner its neighbour still used |
| Stop at the nearest geometry behind / in front | 704 inside-out → 64 | pushing a whole side past the far side inverted the box |
| Weld new vertices, cancel twin faces | 2,390 zero-area faces → 175 | hole cuts landing exactly on existing corners duplicated them |
| Ignore the region's own neighbours as "in front" | 48 collapses → 0 | a 5-face selection "joined" onto its own neighbour and vanished |
| Sweep the whole outline, not just corner rays | tunnel cases → 0 | a face over a tunnel was pushed straight through it |
| Corners slide along leaning neighbours | 80 inside-out → 0 | a wedge's slanted face pushed in sent walls out through the base |
| Precision-aware tolerances, relative Newell normals | 199 far-from-origin → 2 | 200 km from the origin floats step 2 cm; normals summed raw coordinates |
| Holes that leave slivers fall back to stopping | 46 → 0 | needle triangles from cuts on curved faces |
| **Now** | **2** | both on the cube 200 km from the origin (below) |

The interactive part: every Push/Pull on every mesh was undone or cancelled back to the exact mesh, at about 7 ms a frame (1000 x 700, headless).

### Honest negatives (pass 5)

- **Some selections are refused rather than guessed.** About 6% of the attempts are refused, almost all of them because the faces around the selection lean over it and no slide keeps every neighbour flat. Examples are a whole band of faces, or a group whose corners touch three or more unselected planes. Refusing beats a broken mesh, and the cursor shows the reason.
- **200 km from the origin** a 1 m cube is only about 50 float steps wide, and 2 of 26,620 results still came out open. Model near the origin, as in every float-based editor.
- `push_through` itself still leaves slivers on some curved meshes. Push/Pull doesn't show them, because it falls back to stopping at the far side, but Alt+P (Push Through on its own) can still produce them.

## Pass 4: GPU render devices (Vulkan)

`blendity_stress --only gpu` renders the same frame on every device: 400 textured spheres on a checker floor, with sun + sky, 4 bounces and 128 samples at 1280×720. The GPUs are an RTX 4070 SUPER and an RTX 3060 Ti in the same machine. A unit test checks that every device, and CPU + GPU together, matches the CPU render (mean within 3%, per-pixel difference under 6%). Two runs agreed within 1%.

| Device | samples/s | vs CPU |
|---|---|---|
| CPU (Embree, 32 threads) | 21.0 | 1.00× |
| RTX 4070 SUPER, compute shader (Blendity BVH) | 157 | 7.5× |
| RTX 4070 SUPER, ray tracing hardware | 1,150 | **55×** |
| RTX 3060 Ti, compute shader | 80.5 | 3.8× |
| RTX 3060 Ti, ray tracing hardware | 347 | 16.5× |
| Both GPUs | 1,268 | **60×** (samples split 96 : 32) |
| CPU + both GPUs | 1,070–1,160 | 51–55× (CPU takes 1–2 samples) |

### Efficiencies found and applied

- **Ray tracing hardware is 7.3× the software BVH** on the same GPU (`VK_KHR_ray_query`, one BLAS per unique mesh plus a TLAS). The software-BVH kernel stays for GPUs without RT cores.
- **Optimised SPIR-V, cached on disk.** An early build skipped shaderc's optimiser: the kernel compiled in 70 ms and seemed just as fast. That was only because the driver's shader cache was already warm. On a cold cache, NVIDIA's driver took **~105 s per GPU** to build the pipeline from unoptimised SPIR-V, so the first GPU render took 212 s. Optimised SPIR-V fixed two things:
  - The driver builds the pipeline in 2.7 s.
  - The software-BVH kernel runs **1.7× faster**, and the RT kernel 1.6×.

  shaderc's optimiser takes 6 s, so its output is cached in `~/.blendity/cache` (keyed by a hash of the source and options). The editor starts compiling in the background as soon as Device is set to GPU Compute, and each GPU builds its pipeline on its own thread. First GPU render on a new machine: **212 s → 9.3 s**. After an update: 3.4 s. After that: 0.2 s.
- **Multi-GPU went from slower than one GPU to faster** through four changes:
  - Guided self-scheduling batches replaced fixed batch sizes.
  - Buffers are merged only at the 100 ms display interval rather than after every batch.
  - Albedo and normal are read back only when the denoiser runs.
  - Readback moved into **host-cached** memory. Reading 15 MB from the default uncached host-visible memory took about 50 ms, a big share of a 100 ms frame.
- **The CPU stops claiming samples** once the GPUs would finish the remaining ones sooner, so a slow CPU sample no longer holds up the frame.

### Honest negatives (pass 4)

- **Adding the CPU to two RT GPUs is 9–16% slower** in this scene. One CPU sample takes 47 ms while a GPU sample takes under 1 ms, so the frame waits for the CPU's last sample. The CPU still helps alongside GPUs without ray tracing hardware, or a single slow GPU. As in Blender, it is a tick box, and off is the right choice with fast GPUs.
- **The second GPU adds only 10%**, not the 30% its standalone speed suggests. The whole 128-sample frame takes about 100 ms, so each batch's submit-and-wait and the final readback are a large fixed share. Overlapping batches across two command buffers is the next step.

## Pass 3: Blender's libraries and bare-metal optimisation

This pass linked 16 of Blender's prebuilt libraries (each optional, each compared with Blendity's own fallback in the stress suite). It then hand-optimised the hottest CPU paths with SIMD and by skipping work. Same-session A/B builds were used for the before/after numbers below, because background load on this machine drifts by ~20% between runs.

### Blender's libraries vs Blendity's own code

| Library (Blender uses it for) | Blendity's fallback | Result |
|---|---|---|
| **Embree** (Cycles CPU BVH) | two-level SAH BVH | **2.0–2.9× more rays/s** (64–95 vs 22–47 Mrays/s); BVH builds 1.2–2.3× faster |
| **OpenImageDenoise** (Cycles denoiser) | À-Trous wavelet filter | RMSE vs a 512-spp reference at 4 spp: noisy 0.096, À-Trous 0.073, **OIDN 0.046**. 2–2.3× slower (250 ms at 720p), so the viewport uses À-Trous while sampling and OIDN once converged |
| **oneTBB** (BLI_task) | thread pool | **6.5× cheaper dispatch** for 10,000 small `parallel_for` calls; streaming and imbalanced loops are even |
| **Eigen** sparse Cholesky (UV unwrap) | block-Jacobi CG | LSCM 1.2–1.8× faster on islands of 4k+ faces (slower on tiny ones); a 262k-face single island becomes practical (11 s) |
| **OpenSubdiv** (Subdivision Surface) | direct Catmull-Clark | Identical meshes, but **3–6× slower** here: OSD's topology refinement pays off on animated, re-evaluated meshes, not one-off subdivisions. Kept off by default |
| **OpenColorIO** (colour management) | built-in curves | AgX / Filmic / PBR Neutral baked into 97³ LUTs: 6.8 ms per 1080p frame vs 17 ms for the exact OCIO processor, 99% of pixels within 1/255 |
| **Manifold** (Boolean, Manifold solver) | none | Boolean modifier (exact volumes in the unit tests) |
| **meshoptimizer** | none | Decimate modifier |
| **Jolt** | O(n²) sphere physics | **13.9× faster at 2,000 bodies**; 5,000 bodies in 2.2 ms. Slower below ~200 bodies (fixed per-step overhead) |
| **libjpeg-turbo / libpng** | Blendity's decoders | 4K decode **3.2× / 1.8× faster**; PNG output 22 → 10 MB with adaptive filters + zlib |
| **Zstandard** (.blend compression) | none | Text scenes **51× smaller**, still loading at ~60 MB/s |
| **Open PGL** (Cycles path guiding) | off | Learns correctly (10× more guided samples toward the light), but **0.45–0.49× efficiency** in the doorway test scene: lookups cost more than the noise removed. Opt-in, as in Cycles |

### Bare-metal and algorithmic wins

| Bottleneck | Fix | Before → After (same session) |
|---|---|---|
| **Rasterizer setup**: every object's vertices were transformed *and lit* (Gouraud) before culling, and every triangle did 3 perspective divides | Small-triangle cull (a bounding box with no pixel centre is dropped before it's stored); vertices transformed where their triangles are assembled, divided once per vertex, and **lit only when a surviving triangle needs them**; an **AVX2 kernel rejects 8 triangles per step** (gathers + vector compares, no FMA so it rejects exactly what the scalar code would) | 21M tris (16k objects, 5% visible): **174 → 34 ms** (60 → 26 ms in the quiet full run). 1 thread: 1,953 → 408 ms. 60 FPS limit at 720p: **~2.6M → ~5.2M triangles** |
| **Display encode** (every viewport pixel, path tracer resolve, sky background): `pow()` per channel for sRGB | An exact table indexed by the float's exponent + 7 mantissa bits, storing each bucket's value and the one threshold where rounding steps up (correctly rounded for every float in [0, 1]), and **SSE4.1 (4 px) / AVX2 + FMA (8 px) tone-curve kernels** picked at run time from `cpu::features()` | 1080p Filmic, 1 thread: **97.8 → 6.3 ms (15.7×)**; all threads: 6.5 → 0.65 ms |
| **Deferred shading**: barycentrics, world position and normal matrix work recomputed for every pixel | Per-triangle setup once (screen-linear planes relative to vertex 0 for precision), reused while neighbouring pixels share the triangle; `pow()` removed from GGX Fresnel and the sky gradient | 4K shading pass: ~10% faster (34 → ~30 ms) |
| **À-Trous weights**: `pow(n·n', 64)` per tap | six squarings | (inside the denoiser timings above) |

### Honest negatives (pass 3)

- **Packing objects into fewer setup chunks did nothing on its own.** One chunk per object looked expensive (16k tile tables of ~4 KB, walked by every tile). Fixing it moved no time, because the real cost was the per-vertex lighting found by timing each stage. The packing stays, since the later fixes build on it.
- **Deferred shading only got ~10% faster.** After the per-triangle setup, the cost is spread evenly over material evaluation, lights and environment (~250 cycles each per pixel). Going further needs an 8-pixels-at-a-time (ISPC-style) shader, which wasn't done.
- **Single-precision `powf` isn't monotonic** at 8-bit rounding boundaries, so a table built from it inherits its glitches. The table is built from the double-precision curve instead. Plain `powf` itself misrounds about 1 float in 2.5 million.
- **Rays per second were overstated in pass 2.** The old counter estimated rays per path instead of counting them; it's now exact. The real built-in BVH rate is **22–47 Mrays/s**, not 69–231. The pass 2 table below keeps the original figures, marked as such.
- **The machine's load matters as much as many optimisations.** Unrelated runs differed by ~20%, so every before/after above is an A/B of two binaries in the same session.

## Pass 2: rendering, UVs, texturing and modeling tools

The second pass added five suites (`shading`, `pathtracer`, `uv`, `textures`, `modeling`) and three fixes came out of them.

### Limits found

| Subsystem | Result (on this machine) |
|---|---|
| Deferred PBR (visibility buffer + one shading pass) | 256 spheres: **6.0 ms at 720p, 11.8 ms at 1080p, 39.8 ms at 4K**. Shading costs ~4 ns/pixel; Gouraud is 1.5–4× cheaper |
| Sun shadow map, 2048² | ~7.7 ms to render; the 3×3 PCF lookup adds 0.4–2.7 ms per frame. (This said "cached until lights or casters move"; it was not cached until pass 6, task 0011.) |
| Path tracer throughput | *As first reported:* 231 Mrays/s (20k tris) → 69 Mrays/s (21M tris). **Corrected in pass 3 (exact ray counting): 47 → 22 Mrays/s** with the built-in BVH, 95 → 64 with Embree. Ray cost still grows ~logarithmically with scene size |
| Texture sampling (1 thread) | closest 22–73, bilinear 10–29, trilinear 10–15 Msamples/s; drops at 4K when the texture leaves the cache |
| LSCM unwrap | a single 65k-face island in **1.3 s**; typical seam-cut islands (a few thousand faces) in milliseconds |
| Edit-mode tools on 1M faces | edge loop 18 ms, loop cut 122 ms, mirror 61 ms, solidify 155 ms, recalculate normals 449 ms |

### Efficiencies found and applied

| Bottleneck | Fix | Before → After |
|---|---|---|
| **Path tracer BVH**: one flat BVH over every world-space triangle, rebuilt whenever anything moved | Two-level BVH like Cycles instancing: one object-space BVH per unique mesh, cached across rebuilds by a content hash and built in parallel, plus a small top-level BVH over instances (`PathTracer::build`) | 21M instanced tris: **34.3 s → 15 ms**. 1,024 unique meshes (1.3M tris): first build **1,853 → 106 ms**, moving one object **→ 9.9 ms**. Ray throughput also rose by ~28% at 21M tris (reported then as 54 → 69 Mrays/s, an overcount; see pass 3) because the working set shrank |
| **LSCM unwrap**: every CG step multiplied by A and then Aᵀ, single-threaded, and stopped relative to the initial residual (a good initial guess made it work *harder*) | Assemble the normal equations once as 2×2 block-sparse rows, block-Jacobi preconditioner, rows split across threads for large islands, tolerance relative to ‖Aᵀb‖ (`uvops::lscm_island`) | 65k-face island: **11.8 s → 1.1–1.3 s (≈10×)**; 16k faces: 1.83 s → 0.22–0.26 s |
| **Edit-mode topology queries**: every tool built an `unordered_map<edge, vector<face>>` over the whole mesh | Vertex→face table built with a counting sort (CSR); an edge's faces are the intersection of two short lists (`EdgeFaces` in `mesh.cpp`) | 1M faces: edge loop **897 → 18 ms (50×)**, loop cut 951 → 122 ms (8×), solidify 1,028 → 155 ms (6.6×), recalculate normals 3,112 → 449 ms (6.9×) |

### Robustness

The input fuzzer **found a real crash** after the UV Editor and Render tabs were added: middle-clicking a tab closed it *during* the recursive dock drawing, which freed dock nodes still on the call stack (a use-after-free that had been latent since the first version). Tab closes are now queued and applied after drawing. To make the next one faster to find, `blendity_stress` now prints a symbolised stack trace on a crash (DbgHelp, Windows), `BLENDITY_FUZZ_TRACE=1` logs every frame's events, `BLENDITY_FUZZ_SEED=n` changes the sequence, and the fuzzer presses the new shortcuts too. Five seeds × 6,000 frames pass in a debug build with checked iterators.

### Honest negatives (pass 2)

- **Very large single UV islands are still CG-bound.** Iterations grow with the island's diameter, so a 65k-face island takes ~1.2 s where Blender's direct solver is faster. Multigrid or a sparse Cholesky would fix it; in practice, seams keep islands small.
- **Mip-map generation is single-threaded**: 330 ms for a 4K texture on load. It runs once per image, so it was left alone.
- **The first shadow-map render in a run is slower** (12 ms at 640×360 vs ~7.7 ms after): first-touch allocation of the 16 MB depth buffer.

## Pass 1: editor, scene graph and meshes

### Limits found

| Subsystem | Limit (on this machine) |
|---|---|
| Software rasterizer, 1280×720 @ 60 FPS | **~2.6M triangles/frame** with all cores (82k single-threaded). Peak throughput ≈ 205M tris/s at 21M tris |
| Fill rate | 935 Mpix/s at 3840×2160 (8.9 ms for 256 spheres in 4K) |
| Catmull-Clark subdivision | Level 10 of a cube = **6.3M faces in 0.34 s**, 192 MB. Level 11 (25M faces, ~770 MB) is stopped by the test's 25M-face safety cap |
| Scene graph | 100,000 objects: moving a root and re-reading every world matrix takes 1.8 ms (flat) / 6.9 ms (tree) |
| Hierarchy window | 100,000 rows expanded: **7.7 ms** per editor frame (virtualised drawing) |
| Merge by Distance | 2.46M vertices in 0.59 s |
| Undo | One step on a scene holding 49M vertices: **0.27 ms** (copy-on-write) |
| Robustness | 6,000 frames of random clicks/drags/keys (10,621 events): no crash |

### Efficiencies found and applied

Each row was a measurable bottleneck in the first stress run. The old implementation is kept inside the stress test so the comparison can be reproduced.

| Bottleneck | Fix | Before → After |
|---|---|---|
| **Edge table** (wireframes, smoothing, subdivision): global sort and an `unordered_map` both cost ~50 ns per edge | Counting-sort corners into buckets by their lower vertex index (CSR), then dedupe each small bucket with insertion sort. Linear time, cache friendly (`Mesh::edges`) | 1.57M faces: **437 ms → 51 ms (8.5×)**; hash map was 985 ms |
| **Scene save**: `ostringstream` + `printf("%.9g")` for every float | String builder + `std::to_chars` (shortest exact round-trip; `snprintf` fallback) | 1.2 MB mesh: **55 ms → 4.2 ms (13×)**; 1M floats: 248 ms → 30 ms |
| **Scene load**: `istringstream` + `getline` copies every line; `strtof` | Zero-copy line reader over the buffer + `std::from_chars` | Mesh data: **86 → 313 MB/s (3.6×)**; object-heavy files 43 → 59 MB/s |
| **Catmull-Clark**: global `std::sort` of 64-bit corner keys, serial assembly with `push_back` | Bucketed edges that also yield the corners per edge (no scatter), parallel face points, edge points, vertex points and output into presized arrays | Level 9 (1.6M faces): **133 → 62 ms (2.2×)**; level 10: 606 → 344 ms |
| Save/load round-trip drift | The loader no longer re-normalises unit quaternions (changed the last float bits) | save → load → save is now byte-identical (unit test) |

### Design choices the numbers confirm

| Technique | Where | Measured effect |
|---|---|---|
| Dirty-flag world matrices | `GameObject::world_matrix` | Deep chain of 10,000: **0.52 ms vs 1,438 ms (2,783×)**. Turns O(n²) into O(n) |
| Object-ID buffer picking | `Renderer3D` / `RenderTarget::ids` | 0.004 µs per pick vs 415 µs (AABB ray cast, 10k objects) vs 1.4 ms (brute force, 1k objects) |
| AABB rejection before triangle tests | picking | 28–33× over brute force |
| Spatial hash merge | `meshops::merge_by_distance` | 153k verts: **19 ms vs 1.79 s (93×)** |
| Copy-on-write meshes (Blender's implicit sharing) | `Scene::clone`, `mesh_make_mutable` | 500 heavy objects: **0.27 ms vs 417 ms and 1.46 GB** for deep copies |
| Tile-parallel raster | `Renderer3D::flush` | 12.2× over single-threaded at 21M tris |
| Back-face culling | raster setup | 1.65–1.7× fewer pixels/tris processed (no-cull is 70% slower) |
| 64 px tiles | `RasterOptions::tile_size` | Best of 16/32/64/128/256 (7.15 ms vs 9.6 ms at 16 px and 10.4 ms at 256 px) |

### Honest negatives (what didn't help much)

- **Exact row spans in the rasterizer** (`span_rows`) gain only **~1–5%** on icospheres, because their triangles are small and compact. The trick pays off on long, thin triangles, so it stays on, but it's not a big win here.
- **Streaming kernels don't scale with cores.** Transforming 8M vertices (≈190 MB of traffic) stops at **~2.2× from 3 threads onward**: it's memory-bandwidth bound. The 2M-vertex version that fits in cache reached 9.2× in the quick run. Lesson (GEA Vol. I 3.5): keep hot data small and touch it once.
- **Flat hierarchies** gain little from dirty flags (1.6–1.8×). The benefit grows with depth.
- Object-heavy scene loading is now limited by per-object work (component creation, a `std::map` per component), not parsing: ~3.7 µs per object.

## Next candidates

- **Positions-only Edit Mode updates** (pass 6). Keep the triangulation, edges and n-gon regions while a
  drag only moves vertices, and check overlaps incrementally.

Done since the last list: SIMD in the rasterizer (pass 3, in triangle setup rather than the per-pixel edge functions, which the timings showed aren't the bottleneck), a direct sparse solver for large LSCM islands (Eigen), and a real broad phase for physics (Jolt).

1. An 8-pixels-at-a-time deferred shader (ISPC style), now the largest remaining cost of the Shaded view.
2. SIMD vertex transform in the rasterizer's fused vertex stage (8 vertices per step).
3. 4-wide BVH traversal in the built-in path tracer, for builds without Embree (GEA Vol. I 4.10).
4. Reuse the path tracer's per-mesh BVH for Edit Mode face picking (currently linear in triangle count).
5. Parallel mip-map generation for 4K+ textures.
6. Replacing the per-component `std::map` in the scene loader with a flat vector.
7. A uniform-grid broad phase for the fallback `physics_step` (O(bodies²) without Jolt).
