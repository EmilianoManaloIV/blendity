# Performance, limits and efficiencies

All numbers come from `blendity_stress` (full run, about 55 s) on the development machine: **AMD Ryzen 9 5900XT (16 cores / 32 threads), Windows 11, MSVC 14.51 `/O2`**. Re-run it on your own machine:

```
dist/windows/blendity_stress            # full run → stress/results/report_<date>.md + .csv
dist/windows/blendity_stress --quick    # ~3 s (CI)
dist/windows/blendity_stress --only raster
```

The raw reports are in [`stress/results/`](../stress/results/): the first pass in `report_20261006_001100.md`, the rendering/UV/modeling pass in `report_20261006_091049.md` (`latest.md`).

## Pass 2: rendering, UVs, texturing and modeling tools

The second pass added five suites (`shading`, `pathtracer`, `uv`, `textures`, `modeling`) and three fixes came out of them.

### Limits found

| Subsystem | Result (on this machine) |
|---|---|
| Deferred PBR (visibility buffer + one shading pass) | 256 spheres: **6.0 ms at 720p, 11.8 ms at 1080p, 39.8 ms at 4K**. Shading costs ~4 ns/pixel; Gouraud is 1.5–4× cheaper |
| Sun shadow map, 2048² | ~7.7 ms to render (cached until lights or casters move); the 3×3 PCF lookup adds 0.4–2.7 ms per frame |
| Path tracer throughput | **231 Mrays/s** (20k tris) → **69 Mrays/s** (21M tris): ray cost grows ~logarithmically with scene size |
| Texture sampling (1 thread) | closest 22–73, bilinear 10–29, trilinear 10–15 Msamples/s; drops at 4K when the texture leaves the cache |
| LSCM unwrap | a single 65k-face island in **1.3 s**; typical seam-cut islands (a few thousand faces) in milliseconds |
| Edit-mode tools on 1M faces | edge loop 18 ms, loop cut 122 ms, mirror 61 ms, solidify 155 ms, recalculate normals 449 ms |

### Efficiencies found and applied

| Bottleneck | Fix | Before → After |
|---|---|---|
| **Path tracer BVH**: one flat BVH over every world-space triangle, rebuilt whenever anything moved | Two-level BVH like Cycles instancing: one object-space BVH per unique mesh, cached across rebuilds by a content hash and built in parallel, plus a small top-level BVH over instances (`PathTracer::build`) | 21M instanced tris: **34.3 s → 15 ms**. 1,024 unique meshes (1.3M tris): first build **1,853 → 106 ms**, moving one object **→ 9.9 ms**. Ray throughput also rose (54 → 69 Mrays/s at 21M tris) because the working set shrank |
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

## Next candidates (not done yet)

1. Reuse the path tracer's per-mesh BVH for Edit Mode face picking (currently linear in triangle count).
2. SIMD (SSE/NEON) edge functions in the rasterizer and 4-wide BVH traversal in the path tracer (GEA Vol. I 4.10).
3. Multigrid or sparse Cholesky for very large LSCM islands.
4. Parallel mip-map generation for 4K+ textures.
5. Replacing the per-component `std::map` in the scene loader with a flat vector.
6. A uniform-grid broad phase for `physics_step` (currently O(bodies²)).
