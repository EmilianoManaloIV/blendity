# Blendity stress test report

Date: 2026-10-06 23:37  
Threads: 32  
Mode: full  
Frame budget: 16.7 ms

## Rasterizer: triangle throughput & limit

Grid of icospheres (1,280 tris each) at 1280x720. Optimised = all cores + back-face + frustum culling + exact row spans + fast setup (AVX2 rejection of 8 triangles at a time, small-triangle cull, lazy vertex shading). Each optimisation is then disabled in turn. Limit = largest triangle count under the budget.

| objects | triangles | optimised ms | 1 thread ms | no-cull ms | no-span ms | no fast setup ms | Mtris/s (opt) |
|---|---|---|---|---|---|---|---|
| 16 | 20.5k | 1.19 | 4.42 | 1.33 | 1.13 | 1.13 | 17.3 |
| 64 | 81.9k | 1.57 | 9.99 | 2.22 | 1.52 | 1.58 | 52.1 |
| 256 | 327.7k | 3.20 | 28.50 | 5.00 | 3.18 | 3.36 | 102.5 |
| 1024 | 1.31M | 7.19 | 81.62 | 13.93 | 8.34 | 10.63 | 182.3 |
| 2048 | 2.62M | 11.54 | 121.31 | 18.88 | 10.92 | 16.91 | 227.1 |
| 4096 | 5.24M | 15.03 | 169.70 | 25.73 | 15.18 | 27.53 | 348.9 |
| 8192 | 10.49M | 20.05 | 244.75 | 30.68 | 19.60 | 48.54 | 522.9 |
| 16384 | 20.97M | 26.35 | 316.57 | 37.30 | 26.65 | 83.11 | 795.9 |

> 60 FPS limit at 1280x720: ~5.24M triangles optimised vs ~81.9k single-threaded (32 threads available).
| objects | vertex ms | assembly + binning ms | raster ms | total ms | triangles kept |
|---|---|---|---|---|---|
| 16 | 0.01 | 0.17 | 0.25 | 0.43 | 45.9% |
| 64 | 0.02 | 0.44 | 0.54 | 0.99 | 45.4% |
| 256 | 0.04 | 1.27 | 1.25 | 2.56 | 45.4% |
| 1024 | 0.11 | 3.97 | 2.09 | 6.17 | 38.7% |
| 2048 | 0.17 | 6.45 | 4.10 | 10.72 | 29.5% |
| 4096 | 0.30 | 9.47 | 4.40 | 14.17 | 18.0% |
| 8192 | 0.51 | 13.13 | 4.52 | 18.17 | 10.1% |
| 16384 | 0.96 | 18.94 | 4.06 | 23.97 | 5.1% |

## Rasterizer: tile size sweep

Same scene (1,024 icospheres, 1.3M tris), varying the binning tile size.

| tile px | ms/frame |
|---|---|
| 16 | 7.63 |
| 32 | 7.55 |
| 64 | 7.84 |
| 128 | 7.59 |
| 256 | 10.31 |

> Fastest tile size on this machine: 32 px.

## Rasterizer: resolution scaling (fill rate)

256 icospheres, varying the viewport resolution.

| resolution | megapixels | ms/frame | Mpix/s |
|---|---|---|---|
| 640x360 | 0.23 | 1.92 | 119.9 |
| 1280x720 | 0.92 | 3.14 | 293.6 |
| 1920x1080 | 2.07 | 4.07 | 510.0 |
| 2560x1440 | 3.69 | 5.81 | 634.2 |
| 3840x2160 | 8.29 | 12.00 | 691.2 |

## Scene graph: world-matrix updates

Move the root, then read every world matrix. Cached = dirty-flag propagation (GameObject::world_matrix). Uncached = walk the parent chain for every object (world_matrix_uncached).

| shape | objects | cached ms | uncached ms | speed-up |
|---|---|---|---|---|
| flat (all roots) | 1000 | 0.02 | 0.03 | 1.4x |
| flat (all roots) | 10000 | 0.10 | 0.15 | 1.5x |
| flat (all roots) | 100000 | 2.46 | 4.46 | 1.8x |
| deep chain | 1000 | 0.04 | 10.63 | 242.9x |
| deep chain | 4000 | 0.18 | 171.14 | 942.7x |
| deep chain | 10000 | 0.46 | 1098.31 | 2413.0x |
| tree (fan-out 4) | 1000 | 0.04 | 0.11 | 3.0x |
| tree (fan-out 4) | 10000 | 0.49 | 1.53 | 3.1x |
| tree (fan-out 4) | 100000 | 9.49 | 22.29 | 2.3x |

> Dirty flags turn the deep-chain case from O(n^2) into O(n): every object reuses its parent's cached matrix.

## Picking: ray casting vs id buffer

Average cost of one mouse pick. Brute force tests every triangle of every object; AABB rejects objects first (FoCG 12.3); the id buffer reads one pixel written by the rasterizer (how Blendity and Blender's GPU selection work).

| objects | brute force us | AABB first us | id buffer us |
|---|---|---|---|
| 100 | 147.7 | 5.1 | 0.0036 |
| 1000 | 1404.4 | 42.5 | 0.0039 |
| 10000 | (skipped) | 421.0 | 0.0039 |

> The id buffer costs nothing extra per pick because the renderer already wrote it; ray casting scales with scene size.

## Catmull-Clark subdivision: limit

Repeatedly subdivide a cube until one level takes longer than 2 s or exceeds 25M faces.

| level | faces | vertices | ms | mesh memory |
|---|---|---|---|---|
| 1 | 24 | 26 | 0.0 | 2.1 KB |
| 2 | 96 | 98 | 0.0 | 6.6 KB |
| 3 | 384 | 386 | 0.0 | 24.6 KB |
| 4 | 1.5k | 1.5k | 0.1 | 96.6 KB |
| 5 | 6.1k | 6.1k | 0.3 | 384.6 KB |
| 6 | 24.6k | 24.6k | 1.3 | 1.5 MB |
| 7 | 98.3k | 98.3k | 5.0 | 6.0 MB |
| 8 | 393.2k | 393.2k | 17.9 | 24.0 MB |
| 9 | 1.57M | 1.57M | 77.5 | 96.0 MB |
| 10 | 6.29M | 6.29M | 402.9 | 384.0 MB |

> Stopped before level 11: would exceed 25M faces.
| mesh | levels | faces | Blendity ms | OpenSubdiv ms | OpenSubdiv speed-up |
|---|---|---|---|---|---|
| cube | 2 | 96 | 0.0 | 0.1 | 0.17x |
| cube | 4 | 1.5k | 0.2 | 0.4 | 0.35x |
| cube | 6 | 24.6k | 1.8 | 7.4 | 0.25x |
| torus (UVs) | 2 | 32.8k | 3.0 | 9.0 | 0.33x |
| torus (UVs) | 4 | 524.3k | 32.3 | 116.5 | 0.28x |
| torus (UVs) | 6 | 8.39M | 707.2 | 2295.3 | 0.31x |

## Topology: building the edge table

Subdivision, wireframes and smoothing all need unique edges. Compared: std::unordered_map keyed by vertex pair, the original sort-and-unique implementation, and the bucketed counting sort Blendity now uses (Mesh::edges, subdivide_impl).

| faces | hash map ms | global sort ms | bucket/CSR ms | bucket vs sort |
|---|---|---|---|---|
| 1.5k | 0.4 | 0.3 | 0.0 | 7.9x |
| 24.6k | 11.0 | 6.0 | 0.7 | 8.0x |
| 393.2k | 228.0 | 112.0 | 13.4 | 8.4x |
| 1.57M | 1133.9 | 442.9 | 53.3 | 8.3x |

> Bucketing edges by their lower vertex index (counting sort) is linear and cache-friendly; Mesh::edges now uses it.

## Merge by Distance: spatial hash vs O(n^2)

A grid where every vertex is duplicated (as after an import with split normals).

| vertices | naive ms | spatial hash ms | speed-up | merged |
|---|---|---|---|---|
| 9.6k | 7.2 | 1.2 | 6.1x | 7.9k |
| 38.4k | 113.0 | 4.7 | 23.8x | 31.8k |
| 153.6k | 1878.4 | 26.4 | 71.1x | 127.7k |
| 614.4k | (skipped: too slow) | 142.5 | - | 511.4k |
| 2.46M | (skipped: too slow) | 671.1 | - | 2.05M |

## Scene save / load (text format)

Many small objects, then one huge mesh.

| case | file size | save ms | load ms | load MB/s |
|---|---|---|---|---|
| 1000 objects (shared cube) | 199.0 KB | 26.2 | 3.2 | 64.4 |
| 10000 objects (shared cube) | 2.0 MB | 9.6 | 31.5 | 65.1 |
| 50000 objects (shared cube) | 9.8 MB | 54.4 | 160.1 | 64.5 |
| 1 mesh, 393.2k verts | 56.2 MB | 169.6 | 115.8 | 508.6 |

> Formatting 1M floats: printf %.9g 222.3 ms vs to_chars 32.4 ms (6.9x). The writer now uses to_chars.

## Undo snapshots: copy-on-write vs deep copy

Cost of one undo step for scenes holding heavy meshes. CoW = Scene::clone() sharing meshes (Blender's implicit sharing); deep = also duplicating every mesh.

| objects | total verts | CoW clone ms | deep copy ms | deep copy memory |
|---|---|---|---|---|
| 10 | 983.1k | 0.06 | 10.7 | 60.0 MB |
| 100 | 9.83M | 0.07 | 89.2 | 600.1 MB |
| 500 | 49.15M | 0.29 | 659.5 | 2.93 GB |

> Copy-on-write makes undo cost proportional to what changed, not to scene size.

## Job system: parallel scaling

Transform 8M vertices by a matrix with 1..N threads.

| threads | ms | speed-up | efficiency |
|---|---|---|---|
| 1 | 18.52 | 1.0x | 100% |
| 2 | 9.90 | 1.9x | 93% |
| 3 | 8.58 | 2.2x | 72% |
| 4 | 8.93 | 2.1x | 52% |
| 8 | 8.91 | 2.1x | 26% |
| 16 | 8.20 | 2.3x | 14% |
| 32 | 7.63 | 2.4x | 8% |

> Memory bandwidth, not core count, limits this kind of streaming kernel (GEA Vol. I 3.5 Memory Architectures).
| workload | built-in pool ms | oneTBB ms | TBB speed-up |
|---|---|---|---|
| stream 8M vertices | 7.67 | 7.49 | 1.02x |
| imbalanced compute (cost grows 64x) | 7.26 | 7.21 | 1.01x |
| 10,000 small parallel_for calls | 398.95 | 61.36 | 6.50x |
| nested: 32 x parallel_for(250k) | 5.05 | 4.72 | 1.07x |

> TBB's clear win is dispatch cost: many small loops (typical of editor operators) start much faster. Streaming and imbalanced work are a wash: memory bandwidth limits the first, and the built-in pool already hands out small chunks dynamically. Nested loops gain a little because TBB runs the inner loops in parallel too.

## Editor UI: Hierarchy virtualisation & frame cost

Full editor frames (headless, 1600x900) with N empty GameObjects expanded in the Hierarchy.

| objects | frame ms | objects/ms |
|---|---|---|
| 1000 | 10.63 | 94.1 |
| 10000 | 10.96 | 912.6 |
| 100000 | 21.87 | 4571.5 |

> Only visible rows are drawn, so frame cost grows with the tree walk, not with drawing.

## Editor robustness: random input fuzzing

Random clicks, drags, wheel and key presses across the whole window (monkey testing).

| frames | events | objects at end | avg frame ms | result |
|---|---|---|---|---|
| 6000 | 10621 | 6 | 4.13 | no crash |

## Memory footprint

Approximate bytes per element (Scene::memory_bytes / Mesh::memory_bytes).

| item | bytes |
|---|---|
| empty GameObject | 320.7 |
| GameObject + MeshFilter + MeshRenderer (shared mesh) | 512.9 |
| mesh vertex (incl. render cache) | 146.6 |

## Shading: Gouraud vs deferred PBR vs deferred + sun shadows

256 icospheres with a material at each resolution. Deferred shades each visible pixel once from the visibility buffer (EEVEE-like); shadows add a 2048^2 depth-only pass plus a 3x3 PCF lookup per pixel.

| resolution | Gouraud ms | deferred ms | deferred+shadow ms | shadow pass ms | shade ms |
|---|---|---|---|---|---|
| 640x360 | 2.05 | 2.59 | 2.82 | 10.03 | 1.06 |
| 1280x720 | 3.26 | 6.29 | 6.77 | 7.66 | 4.00 |
| 1920x1080 | 4.09 | 10.34 | 11.59 | 8.30 | 7.44 |
| 3840x2160 | 10.33 | 32.19 | 35.42 | 7.53 | 25.39 |

> The shadow map only needs re-rendering when lights or casters move (render_view.cpp caches it by scene hash).
| view transform (1920x1080) | ms / frame | Mpix/s |
|---|---|---|
| Filmic (built-in curve) | 5.44 | 381.4 |
| AgX (OCIO 97^3 LUT) | 6.79 | 305.2 |
| Filmic (OCIO 97^3 LUT) | 6.77 | 306.3 |
| Khronos PBR Neutral (OCIO 97^3 LUT) | 7.12 | 291.2 |
| AgX (OCIO CPU processor, exact) | 17.06 | 121.5 |
| Filmic encode, 1 thread | ms / 1080p frame | Mpix/s | vs pow() |
|---|---|---|---|
| scalar, pow() per channel | 97.84 | 21.2 | 1.00x |
| scalar, sRGB table | 36.28 | 57.1 | 2.70x |
| SSE4.1 (4 px), sRGB table | 7.76 | 267.2 | 12.61x |
| AVX2 + FMA (8 px), sRGB table | 6.25 | 331.8 | 15.65x |
| AVX2 + FMA (8 px), all threads | 0.65 | 3170.5 | 149.60x |

> CPU: AVX2 + FMA. The sRGB table is correctly rounded for every float in [0, 1] (checked in the unit tests).

## Path tracer: BVH build and ray throughput (Blendity BVH vs Embree)

Grids of icospheres traced at 320x180, 4 bounces (Cycles-like: NEE, MIS). Mrays/s counts every camera, bounce and shadow ray actually cast. Each scene runs on Blendity's two-level SAH BVH and, when built with Blender's libraries, on Embree (Cycles' CPU ray tracing kernels).

| objects | triangles | BVH build ms | Embree build ms | BVH Mrays/s | Embree Mrays/s | Embree speed-up |
|---|---|---|---|---|---|---|
| 16 | 20.5k | 1.2 | 0.7 | 47.4 | 95.3 | 2.01x |
| 256 | 327.7k | 1.7 | 0.8 | 28.6 | 70.2 | 2.46x |
| 1024 | 1.31M | 2.3 | 1.6 | 24.9 | 63.8 | 2.56x |
| 4096 | 5.24M | 4.7 | 3.8 | 23.4 | 65.3 | 2.79x |
| 16384 | 20.97M | 15.1 | 11.0 | 22.2 | 64.3 | 2.89x |

> Every object shares one mesh, so both backends build a single bottom-level tree (Cycles instancing).
| unique meshes | triangles | backend | first build ms | move 1 object ms | edit 1 mesh ms | rebuilt |
|---|---|---|---|---|---|---|
| 64 | 81.9k | Blendity BVH | 5.7 | 0.7 | 1.8 | 1 |
| 64 | 81.9k | Embree | 2.5 | 0.7 | 1.1 | 1 |
| 256 | 327.7k | Blendity BVH | 20.3 | 2.5 | 3.4 | 1 |
| 256 | 327.7k | Embree | 9.2 | 2.6 | 2.8 | 1 |
| 1024 | 1.31M | Blendity BVH | 78.8 | 10.1 | 10.7 | 1 |
| 1024 | 1.31M | Embree | 39.4 | 10.6 | 10.6 | 1 |

> Bottom-level trees are cached by mesh content hash in both backends: moving an object only rebuilds the top level; editing a mesh rebuilds just that mesh.
| samples | unguided RMSE | guided RMSE | unguided ms | guided ms | efficiency gain |
|---|---|---|---|---|---|
| 256 | 0.0116 | 0.0119 | 2071.9 | 4021.4 | 0.49x |
| 512 | 0.0079 | 0.0088 | 4113.8 | 7393.1 | 0.45x |
| 256, BSDF-only vs mesh-light NEE | 0.0502 | 0.0116 | 1423.9 | 2085.2 | 12.74x |

> Guiding trains on the first 128 samples, then steers diffuse bounces toward the light it learned (Cycles: Light Paths > Path Guiding). It is unbiased and learns (10x more guided samples point at the doorway than cosine sampling), but here the OpenPGL lookups cost more than the noise it removes - it stays opt-in, as in Cycles.
| resolution | A-Trous ms | OpenImageDenoise ms |
|---|---|---|
| 640x360 | 32.4 | 65.2 |
| 1280x720 | 119.3 | 249.2 |
| 1920x1080 | 248.2 | 581.8 |

## UV: LSCM unwrap and packing

LSCM on an n x n grid (one island): block-Jacobi conjugate gradients on the assembled normal equations (parallel sparse mat-vec) vs Eigen's sparse Cholesky, the direct-solver route Blender takes. Then Smart UV Project + pack on a subdivided torus.

| case | faces | islands | CG ms | Eigen ms | Eigen speed-up |
|---|---|---|---|---|---|
| LSCM grid 16x16 | 256 | 1 | 0.6 | 0.9 | 0.7x |
| LSCM grid 64x64 | 4.1k | 1 | 36.4 | 20.1 | 1.8x |
| LSCM grid 128x128 | 16.4k | 1 | 224.9 | 140.4 | 1.6x |
| LSCM grid 256x256 | 65.5k | 1 | 1150.8 | 943.4 | 1.2x |
| LSCM grid 512x512 | 262.1k | 1 | (skipped) | 11252.4 | - |
| Smart UV torus (subdiv 0) | 512 | 14 | 0.4 |
| Smart UV torus (subdiv 1) | 2.0k | 12 | 1.4 |
| Smart UV torus (subdiv 2) | 8.2k | 12 | 6.3 |
| Smart UV torus (subdiv 3) | 32.8k | 14 | 31.8 |

## Textures: mip build and sampling throughput

Generated UV Grid textures; random UVs, single thread. Trilinear = 8 texel fetches + sRGB LUT decode.

| size | mip build ms | memory | closest Ms/s | linear Ms/s | trilinear Ms/s |
|---|---|---|---|---|---|
| 512 | 5.9 | 1.3 MB | 66.7 | 25.8 | 14.7 |
| 1024 | 21.3 | 5.3 MB | 60.6 | 25.8 | 13.0 |
| 2048 | 85.4 | 21.3 MB | 34.8 | 15.8 | 11.5 |
| 4096 | 335.5 | 85.3 MB | 18.0 | 8.4 | 5.9 |

## Modeling tools at scale

Edit-mode operators on large meshes (an n x n grid / subdivided cube). Interactive tools should stay under ~100 ms.

| operator | faces | ms |
|---|---|---|
| edge loop (129 verts) | 16.4k | 0.3 |
| loop cut | 16.5k | 1.6 |
| solidify (+rim) | 33.5k | 2.3 |
| recalculate normals | 33.5k | 5.7 |
| mirror X (merge) | 33.0k | 0.9 |
| edge loop (513 verts) | 262.1k | 4.8 |
| loop cut | 262.7k | 30.5 |
| solidify (+rim) | 527.4k | 38.8 |
| recalculate normals | 527.4k | 111.7 |
| mirror X (merge) | 525.3k | 16.3 |
| edge loop (1025 verts) | 1.05M | 20.6 |
| loop cut | 1.05M | 121.5 |
| solidify (+rim) | 2.10M | 170.2 |
| recalculate normals | 2.10M | 460.5 |
| mirror X (merge) | 2.10M | 68.6 |

## Image codecs and scene compression

Decoding a 3840x2160 photo-like image (gradients + noise) with Blendity's own decoders vs Blender's libjpeg-turbo and libpng, then Zstandard on a text scene.

| format | file size | Blendity ms | library ms | library speed-up |
|---|---|---|---|---|
| PNG (libpng + zlib) | 10.0 MB | 158.9 | 89.6 | 1.77x |
| JPEG (libjpeg-turbo) | 3.0 MB | 215.1 | 67.5 | 3.19x |
| scene | text size | zstd size | ratio | save ms | load ms |
|---|---|---|---|---|---|
| 10000 objects | 2.0 MB | 39.7 KB | 51.1x | 11.5 | 32.9 |

## Physics: Blendity's sphere physics vs Jolt

N rigid boxes dropped in a pile onto a plane; average ms per 60 Hz frame over 2 simulated seconds. Blendity's fallback tests every pair (O(n^2)); Jolt uses a broad phase, sleeping and a multithreaded solver.

| bodies | Blendity ms/frame | Jolt ms/frame | Jolt speed-up |
|---|---|---|---|
| 100 | 0.07 | 0.16 | 0.4x |
| 500 | 1.01 | 0.35 | 2.9x |
| 2000 | 13.81 | 0.99 | 13.9x |
| 5000 | (skipped) | 2.18 | - |

---
Total run time: 130.0 s
