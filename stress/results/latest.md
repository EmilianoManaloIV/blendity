# Blendity stress test report

Date: 2026-10-06 09:10  
Threads: 32  
Mode: full  
Frame budget: 16.7 ms

## Rasterizer: triangle throughput & limit

Grid of icospheres (1,280 tris each) at 1280x720. Optimised = all cores + back-face + frustum culling + exact row spans. Each optimisation is then disabled in turn. Limit = largest triangle count under the budget.

| objects | triangles | optimised ms | 1 thread ms | no-cull ms | no-span ms | Mtris/s (opt) |
|---|---|---|---|---|---|---|
| 16 | 20.5k | 1.00 | 4.04 | 1.16 | 1.01 | 20.4 |
| 64 | 81.9k | 1.41 | 10.47 | 1.88 | 1.50 | 58.3 |
| 256 | 327.7k | 2.79 | 29.43 | 4.02 | 3.00 | 117.6 |
| 1024 | 1.31M | 8.06 | 97.11 | 13.67 | 8.15 | 162.5 |
| 2048 | 2.62M | 15.71 | 179.40 | 26.34 | 15.95 | 166.9 |
| 4096 | 5.24M | 32.10 | 350.46 | 52.07 | 31.36 | 163.3 |
| 8192 | 10.49M | 60.95 | 684.71 | 102.08 | 61.62 | 172.1 |
| 16384 | 20.97M | 119.68 | 1361.89 | 200.23 | 121.38 | 175.2 |

> 60 FPS limit at 1280x720: ~2.62M triangles optimised vs ~81.9k single-threaded (32 threads available).

## Rasterizer: tile size sweep

Same scene (1,024 icospheres, 1.3M tris), varying the binning tile size.

| tile px | ms/frame |
|---|---|
| 16 | 11.02 |
| 32 | 8.97 |
| 64 | 8.34 |
| 128 | 8.88 |
| 256 | 12.31 |

> Fastest tile size on this machine: 64 px.

## Rasterizer: resolution scaling (fill rate)

256 icospheres, varying the viewport resolution.

| resolution | megapixels | ms/frame | Mpix/s |
|---|---|---|---|
| 640x360 | 0.23 | 2.52 | 91.3 |
| 1280x720 | 0.92 | 3.02 | 304.8 |
| 1920x1080 | 2.07 | 4.18 | 495.6 |
| 2560x1440 | 3.69 | 5.98 | 616.0 |
| 3840x2160 | 8.29 | 11.30 | 734.3 |

## Scene graph: world-matrix updates

Move the root, then read every world matrix. Cached = dirty-flag propagation (GameObject::world_matrix). Uncached = walk the parent chain for every object (world_matrix_uncached).

| shape | objects | cached ms | uncached ms | speed-up |
|---|---|---|---|---|
| flat (all roots) | 1000 | 0.01 | 0.02 | 1.8x |
| flat (all roots) | 10000 | 0.13 | 0.24 | 1.9x |
| flat (all roots) | 100000 | 1.76 | 3.18 | 1.8x |
| deep chain | 1000 | 0.05 | 13.29 | 275.9x |
| deep chain | 4000 | 0.17 | 225.07 | 1325.3x |
| deep chain | 10000 | 0.43 | 1448.71 | 3391.7x |
| tree (fan-out 4) | 1000 | 0.03 | 0.13 | 4.0x |
| tree (fan-out 4) | 10000 | 0.36 | 1.80 | 5.0x |
| tree (fan-out 4) | 100000 | 7.45 | 23.78 | 3.2x |

> Dirty flags turn the deep-chain case from O(n^2) into O(n): every object reuses its parent's cached matrix.

## Picking: ray casting vs id buffer

Average cost of one mouse pick. Brute force tests every triangle of every object; AABB rejects objects first (FoCG 12.3); the id buffer reads one pixel written by the rasterizer (how Blendity and Blender's GPU selection work).

| objects | brute force us | AABB first us | id buffer us |
|---|---|---|---|
| 100 | 146.0 | 5.0 | 0.0028 |
| 1000 | 1382.7 | 41.9 | 0.0033 |
| 10000 | (skipped) | 413.2 | 0.0032 |

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
| 8 | 393.2k | 393.2k | 18.5 | 24.0 MB |
| 9 | 1.57M | 1.57M | 75.8 | 96.0 MB |
| 10 | 6.29M | 6.29M | 395.0 | 384.0 MB |

> Stopped before level 11: would exceed 25M faces.

## Topology: building the edge table

Subdivision, wireframes and smoothing all need unique edges. Compared: std::unordered_map keyed by vertex pair, the original sort-and-unique implementation, and the bucketed counting sort Blendity now uses (Mesh::edges, subdivide_impl).

| faces | hash map ms | global sort ms | bucket/CSR ms | bucket vs sort |
|---|---|---|---|---|
| 1.5k | 0.5 | 0.4 | 0.0 | 8.5x |
| 24.6k | 7.0 | 7.4 | 0.7 | 10.3x |
| 393.2k | 196.2 | 120.2 | 11.8 | 10.2x |
| 1.57M | 1004.0 | 445.2 | 52.0 | 8.6x |

> Bucketing edges by their lower vertex index (counting sort) is linear and cache-friendly; Mesh::edges now uses it.

## Merge by Distance: spatial hash vs O(n^2)

A grid where every vertex is duplicated (as after an import with split normals).

| vertices | naive ms | spatial hash ms | speed-up | merged |
|---|---|---|---|---|
| 9.6k | 7.1 | 1.2 | 6.0x | 7.9k |
| 38.4k | 106.2 | 4.7 | 22.4x | 31.8k |
| 153.6k | 1780.1 | 20.3 | 87.6x | 127.7k |
| 614.4k | (skipped: too slow) | 134.6 | - | 511.4k |
| 2.46M | (skipped: too slow) | 595.1 | - | 2.05M |

## Scene save / load (text format)

Many small objects, then one huge mesh.

| case | file size | save ms | load ms | load MB/s |
|---|---|---|---|---|
| 1000 objects (shared cube) | 198.9 KB | 0.9 | 3.1 | 64.8 |
| 10000 objects (shared cube) | 2.0 MB | 9.3 | 30.5 | 67.0 |
| 50000 objects (shared cube) | 9.8 MB | 49.3 | 157.4 | 65.6 |
| 1 mesh, 393.2k verts | 56.2 MB | 159.7 | 116.7 | 504.9 |

> Formatting 1M floats: printf %.9g 256.7 ms vs to_chars 29.3 ms (8.7x). The writer now uses to_chars.

## Undo snapshots: copy-on-write vs deep copy

Cost of one undo step for scenes holding heavy meshes. CoW = Scene::clone() sharing meshes (Blender's implicit sharing); deep = also duplicating every mesh.

| objects | total verts | CoW clone ms | deep copy ms | deep copy memory |
|---|---|---|---|---|
| 10 | 983.1k | 0.03 | 9.0 | 60.0 MB |
| 100 | 9.83M | 0.05 | 87.1 | 600.1 MB |
| 500 | 49.15M | 0.38 | 703.2 | 2.93 GB |

> Copy-on-write makes undo cost proportional to what changed, not to scene size.

## Job system: parallel scaling

Transform 8M vertices by a matrix with 1..N threads.

| threads | ms | speed-up | efficiency |
|---|---|---|---|
| 1 | 17.07 | 1.0x | 100% |
| 2 | 11.19 | 1.5x | 76% |
| 3 | 8.86 | 1.9x | 64% |
| 4 | 7.67 | 2.2x | 56% |
| 8 | 7.31 | 2.3x | 29% |
| 16 | 7.13 | 2.4x | 15% |
| 32 | 7.06 | 2.4x | 8% |

> Memory bandwidth, not core count, limits this kind of streaming kernel (GEA Vol. I 3.5 Memory Architectures).

## Editor UI: Hierarchy virtualisation & frame cost

Full editor frames (headless, 1600x900) with N empty GameObjects expanded in the Hierarchy.

| objects | frame ms | objects/ms |
|---|---|---|
| 1000 | 10.71 | 93.4 |
| 10000 | 12.36 | 809.3 |
| 100000 | 25.26 | 3958.4 |

> Only visible rows are drawn, so frame cost grows with the tree walk, not with drawing.

## Editor robustness: random input fuzzing

Random clicks, drags, wheel and key presses across the whole window (monkey testing).

| frames | events | objects at end | avg frame ms | result |
|---|---|---|---|---|
| 6000 | 10621 | 6 | 3.60 | no crash |

## Memory footprint

Approximate bytes per element (Scene::memory_bytes / Mesh::memory_bytes).

| item | bytes |
|---|---|
| empty GameObject | 312.7 |
| GameObject + MeshFilter + MeshRenderer (shared mesh) | 504.9 |
| mesh vertex (incl. render cache) | 146.6 |

## Shading: Gouraud vs deferred PBR vs deferred + sun shadows

256 icospheres with a material at each resolution. Deferred shades each visible pixel once from the visibility buffer (EEVEE-like); shadows add a 2048^2 depth-only pass plus a 3x3 PCF lookup per pixel.

| resolution | Gouraud ms | deferred ms | deferred+shadow ms | shadow pass ms | shade ms |
|---|---|---|---|---|---|
| 640x360 | 2.45 | 3.36 | 3.44 | 12.36 | 1.22 |
| 1280x720 | 2.91 | 6.02 | 6.63 | 7.83 | 3.96 |
| 1920x1080 | 3.96 | 11.76 | 12.17 | 7.62 | 8.34 |
| 3840x2160 | 10.39 | 39.75 | 42.43 | 7.75 | 32.79 |

> The shadow map only needs re-rendering when lights or casters move (render_view.cpp caches it by scene hash).

## Path tracer: BVH build and ray throughput

Grids of icospheres traced at 320x180, 1 sample per pass, 4 bounces (Cycles-like: binned-SAH BVH, NEE, MIS). Mrays/s counts every camera, bounce and shadow ray.

| objects | triangles | BVH build ms | BVH nodes | ms / sample | Mrays/s |
|---|---|---|---|---|---|
| 16 | 20.5k | 1.1 | 1.5k | 1.5 | 231.4 |
| 256 | 327.7k | 1.5 | 1.8k | 3.4 | 102.2 |
| 1024 | 1.31M | 2.1 | 2.6k | 4.1 | 84.5 |
| 4096 | 5.24M | 4.6 | 5.6k | 4.6 | 75.8 |
| 16384 | 20.97M | 15.2 | 17.9k | 5.0 | 69.3 |

> Ray cost grows ~logarithmically with triangle count thanks to the BVH. All objects share one mesh here, so the two-level BVH builds a single bottom-level tree (Cycles instancing).
| unique meshes | triangles | first build ms | move 1 object ms | edit 1 mesh ms | rebuilt |
|---|---|---|---|---|---|
| 64 | 81.9k | 6.1 | 0.7 | 2.1 | 1 |
| 256 | 327.7k | 21.8 | 2.6 | 3.9 | 1 |
| 1024 | 1.31M | 105.9 | 9.9 | 11.4 | 1 |

> Bottom-level BVHs are cached by mesh content hash: moving an object only rebuilds the small top level; editing a mesh rebuilds just that mesh. The first version rebuilt one flat BVH over all triangles on any change (1.85 s at 1.3M triangles).

## UV: LSCM unwrap and packing

LSCM (conjugate gradients on the normal equations) on an n x n grid - one island - and Smart UV Project + pack on a subdivided torus. Blender solves LSCM with a sparse direct solver; CG needs O(n) iterations.

| case | faces | islands | ms |
|---|---|---|---|
| LSCM grid 16x16 | 256 | 1 | 1.5 |
| LSCM grid 64x64 | 4.1k | 1 | 62.2 |
| LSCM grid 128x128 | 16.4k | 1 | 264.6 |
| LSCM grid 256x256 | 65.5k | 1 | 1277.6 |
| Smart UV torus (subdiv 0) | 512 | 14 | 0.6 |
| Smart UV torus (subdiv 1) | 2.0k | 12 | 2.2 |
| Smart UV torus (subdiv 2) | 8.2k | 12 | 8.9 |
| Smart UV torus (subdiv 3) | 32.8k | 14 | 38.2 |

## Textures: mip build and sampling throughput

Generated UV Grid textures; random UVs, single thread. Trilinear = 8 texel fetches + sRGB LUT decode.

| size | mip build ms | memory | closest Ms/s | linear Ms/s | trilinear Ms/s |
|---|---|---|---|---|---|
| 512 | 5.3 | 1.3 MB | 72.9 | 28.7 | 14.6 |
| 1024 | 21.2 | 5.3 MB | 59.3 | 26.0 | 13.0 |
| 2048 | 84.2 | 21.3 MB | 52.1 | 23.6 | 12.2 |
| 4096 | 330.8 | 85.3 MB | 21.9 | 9.9 | 10.0 |

## Modeling tools at scale

Edit-mode operators on large meshes (an n x n grid / subdivided cube). Interactive tools should stay under ~100 ms.

| operator | faces | ms |
|---|---|---|
| edge loop (129 verts) | 16.4k | 0.3 |
| loop cut | 16.5k | 1.8 |
| solidify (+rim) | 33.5k | 2.4 |
| recalculate normals | 33.5k | 5.4 |
| mirror X (merge) | 33.0k | 0.9 |
| edge loop (513 verts) | 262.1k | 4.8 |
| loop cut | 262.7k | 29.0 |
| solidify (+rim) | 527.4k | 38.5 |
| recalculate normals | 527.4k | 95.9 |
| mirror X (merge) | 525.3k | 15.2 |
| edge loop (1025 verts) | 1.05M | 18.1 |
| loop cut | 1.05M | 122.1 |
| solidify (+rim) | 2.10M | 154.8 |
| recalculate normals | 2.10M | 448.5 |
| mirror X (merge) | 2.10M | 61.3 |

---
Total run time: 54.2 s
