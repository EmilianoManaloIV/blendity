# Task 0395: Dependencies where the suite shows the gain (round 419)

- **Status:** built
- **Requirements:** Q-06 (responsiveness), C-01 / C-02 (builds)
- **Decisions:** ADR 0112 (oneTBB's allocator for the Windows build with Blender's libraries; Tracy for developers;
  amends ADR 0001)
- **Model:** main session (Fable, then Opus 5.5)
- **PR:** stacked on #316 (round 418)

## Asked
The plan's round 419: candidates in order - tbbmalloc_proxy, Tracy, mimalloc, then KTX / Basis and OpenImageIO only if
needed - each adopted only when a suite row improves 1.3x or more on the reference machine, GPL-3-compatible, behind a
`BL_WITH_*` switch with Blendity's code as the fallback, shipped only in Blendity-full. On 2026-11-05 the user gave
permission to download Tracy's client, mimalloc and the others.

## Built
- build.bat: `BLENDITY_ALLOCATOR=tbbmalloc|system` (default `tbbmalloc`; unknown values stop the build; the chosen one
  printed on the "Using Blender libraries" line): oneTBB's `tbbmalloc_proxy` linked with `/INCLUDE:__TBB_malloc_proxy`,
  its DLL deployed beside the others (the copy filter no longer drops it).
- build.sh: `BLENDITY_ALLOCATOR=system|tbbmalloc` (default `system`); `tbbmalloc` links the proxy on Linux
  (`--push-state,--no-as-needed`) and stops the build on macOS.
- `src/core/allocator.cpp` / `.h`: `allocator_name()`, `allocator_replaced()` (the proxy's `TBB_malloc_replacement_log`
  on Windows); `libs` prints "Memory: ..."; `deps.cpp` rows for oneTBB malloc and Tracy.
- `BLENDITY_TRACY=1`: Tracy's client compiled in (`TRACY_ENABLE`, `TRACY_ON_DEMAND`); `src/core/profiling.h`
  (`BL_ZONE`, `BL_FRAME_MARK`) at the frame, the Scene and Game views, the gather, the deferred render and the card's
  draw - nothing without the switch. `packaging/blender-libs.txt` pins `extra-tracy` v0.11.1;
  `packaging/fetch_blender_libs.sh ... --extras` fetches it (and refetches a clone at another commit).
- `tests_round419` (test_editor.cpp): on Windows with the proxy, the proxy's own log says it replaced the runtime; a
  million blocks of mixed sizes written on every core, read back and freed by other jobs.

## Acceptance criteria
1. Each adopted library improves at least one suite row 1.3x or more on this machine, measured against the system
   allocator in the same session (A/B binaries, two runs each).
2. The dependency-free build is unchanged (no new library, the system allocator); a default build needs no fetched
   extra.
3. A `BLENDITY_TRACY=1` build compiles and runs the suite's quick parity section.
4. Both suites, the audits.

## Measured
ADR 0112 has the headline rows. The full table (system / oneTBB / mimalloc, the mean of two runs each, ms unless the
row says otherwise; ** where a ratio passes 1.3x either way):

| Row | system | tbbmalloc | mimalloc | system/tbbmalloc | system/mimalloc |
|---|---|---|---|---|---|
| [stress] blender_parity done at | 54.80 | 19.10 | 18.95 | 2.87x | 2.89x | **
| Modifier: Array x10 on the 50k sphere | 9.55 | 8.31 | 5.00 | 1.15x | 1.91x | **
| Select linked from one face (500k faces) | 122.72 | 83.60 | 65.62 | 1.47x | 1.87x | **
| Undo of the delete, 10,000 objects | 46.84 | 33.89 | 26.38 | 1.38x | 1.78x | **
| Export the 50k sphere as FBX | 29.05 | 21.04 | 17.02 | 1.38x | 1.71x | **
| Exit Edit Mode (500k faces) | 3.18 | 9.64 | 2.77 | 0.33x | 1.15x | **
| Duplicate 10,000 objects | 144.72 | 107.64 | 87.84 | 1.34x | 1.65x | **
| Export the 50k sphere as STL | 13.32 | 9.12 | 8.39 | 1.46x | 1.59x | **
| Modifier: Bevel on the 50k sphere | 681.05 | 562.75 | 432.99 | 1.21x | 1.57x | **
| Select linked from one face (50k faces) | 12.71 | 9.88 | 8.12 | 1.29x | 1.57x | **
| Open it: 10,000 cubes | 76.45 | 61.56 | 50.45 | 1.24x | 1.52x | **
| Modifier: Mirror on the 50k sphere | 3.62 | 2.99 | 2.47 | 1.21x | 1.46x | **
| Modifier stack again after a vertex moved (Subdivision 1 + Mirror) | 24.10 | 16.88 | 20.31 | 1.43x | 1.19x | **
| A frame of 1,000 keyframed objects | 0.43 | 0.36 | 0.30 | 1.19x | 1.43x | **
| Import the 50k sphere from STL | 18.50 | 13.00 | 13.31 | 1.42x | 1.39x | **
| Modifier: Solidify on the 50k sphere | 9.59 | 6.85 | 7.12 | 1.40x | 1.35x | **
| Merge by distance, everything (500k faces) | 695.10 | 555.65 | 496.50 | 1.25x | 1.40x | **
| Inset three faces (50k faces) | 26.62 | 19.61 | 20.18 | 1.36x | 1.32x | **
| Export the 50k sphere as glTF (GLB) | 40.17 | 35.30 | 29.59 | 1.14x | 1.36x | **
| Save a scene: 10,000 cubes | 26.23 | 22.68 | 19.40 | 1.16x | 1.35x | **
| Import the 50k sphere from glTF (GLB) | 27.38 | 24.09 | 20.79 | 1.14x | 1.32x | **
| Subdivide every edge (50k faces) | 131.47 | 100.63 | 108.93 | 1.31x | 1.21x | **
| F12 at 1920x1080, 400 spheres (raster) | 99.47 | 77.88 | 80.51 | 1.28x | 1.24x |
| Select all (500k faces) | 30.81 | 24.15 | 29.01 | 1.28x | 1.06x |
| Exit Edit Mode (50k faces) | 1.36 | 1.31 | 1.07 | 1.04x | 1.28x |
| Export the 50k sphere as USD (USDA) | 117.87 | 104.91 | 93.32 | 1.12x | 1.26x |
| Loop cut (50k faces) | 33.09 | 26.33 | 29.55 | 1.26x | 1.12x |
| Extrude three faces (50k faces) | 25.81 | 20.54 | 21.11 | 1.26x | 1.22x |
| Extrude three faces (500k faces) | 221.94 | 176.81 | 200.88 | 1.26x | 1.10x |
| Memory: a 500k-face mesh | 150.05 | 168.64 | 201.46 | 0.89x | 0.74x | **
| Inset three faces (500k faces) | 237.32 | 189.55 | 222.49 | 1.25x | 1.07x |
| Triangulate every face (500k faces) | 451.08 | 361.39 | 400.17 | 1.25x | 1.13x |
| Modifier: Boolean union with a second 50k sphere on the 50k sphere | 171.10 | 137.50 | 171.31 | 1.24x | 1.00x |
| Modifier: Subdivision level 1 on the 50k sphere | 21.34 | 28.11 | 20.21 | 0.76x | 1.06x | **
| Memory: 10,000 cubes | 17.18 | 22.26 | 16.55 | 0.77x | 1.04x |
| Triangulate every face (50k faces) | 43.46 | 36.05 | 35.40 | 1.21x | 1.23x |
| Import the 50k sphere from OBJ | 27.03 | 22.06 | 22.30 | 1.23x | 1.21x |
| Bevel three faces' edges (50k faces) | 32.80 | 27.09 | 26.77 | 1.21x | 1.23x |
| Undo of the subdivide (50k faces) | 17.13 | 14.13 | 17.71 | 1.21x | 0.97x |
| Save a scene: a 500k-face mesh | 30.07 | 24.84 | 25.09 | 1.21x | 1.20x |
| Export the 50k sphere as OBJ | 36.64 | 30.30 | 30.97 | 1.21x | 1.18x |
| Select all, 10,000 objects | 2.40 | 2.00 | 3.03 | 1.20x | 0.79x |
| Select all (50k faces) | 4.08 | 3.42 | 4.22 | 1.19x | 0.96x |
| Undo of the triangulate (500k faces) | 34.44 | 42.47 | 40.31 | 0.81x | 0.85x |
| Open it: a 500k-face mesh | 33.91 | 30.14 | 28.58 | 1.13x | 1.19x |
| Merge by distance, everything (50k faces) | 56.27 | 49.80 | 47.78 | 1.13x | 1.18x |
| 1,000 rigid boxes falling: a frame | 0.57 | 0.49 | 0.52 | 1.17x | 1.11x |
| Import the 50k sphere from PLY | 11.25 | 9.70 | 9.79 | 1.16x | 1.15x |
| Delete 10,000 objects | 17.80 | 18.08 | 15.38 | 0.98x | 1.16x |
| Modifier: Decimate 0.5 on the 50k sphere | 61.38 | 53.11 | 55.85 | 1.16x | 1.10x |
| Enter Edit Mode (50k faces) | 13.95 | 12.10 | 14.43 | 1.15x | 0.97x |
| Import the 50k sphere from USD (USDA) | 54.27 | 48.27 | 47.14 | 1.12x | 1.15x |
| Export the 50k sphere as PLY | 13.40 | 11.66 | 11.82 | 1.15x | 1.13x |
| Undo of the triangulate (50k faces) | 6.17 | 5.66 | 5.66 | 1.09x | 1.09x |
| Modifier: Subdivision level 2 on the 50k sphere | 114.72 | 114.39 | 105.61 | 1.00x | 1.09x |
| Enter Edit Mode (500k faces) | 106.88 | 102.00 | 98.62 | 1.05x | 1.08x |
| Import the 50k sphere from FBX | 15.92 | 15.02 | 14.77 | 1.06x | 1.08x |
| Modifier: Subdivision level 1 on a 24,576-quad cube | 12.66 | 11.86 | 12.85 | 1.07x | 0.99x |
| -> perf status: frame | 8.35 | 8.90 | 8.35 | 0.94x | 1.00x |
| [stress] world_like done at | 2.40 | 2.30 | 2.35 | 1.04x | 1.02x |
| Unwrap the 50k sphere | 685.89 | 668.76 | 680.82 | 1.03x | 1.01x |
| Ten shape keys on the 50k sphere, evaluated | 0.60 | 0.59 | 0.59 | 1.01x | 1.02x |
| Blendity stress test - 32 hardware threads, budget | 16.70 | 16.70 | 16.70 | 1.00x | 1.00x |

## The Opus review's fixes
The proxy kept to the platform it was measured on (build.sh: the system allocator by default on Linux, and never on
macOS, where Blender doesn't use it - a release from CI would have shipped both unmeasured); the mimalloc switch, its
pin and its licence row removed (its wiring crashed the suite); the extras fetched only with `--extras`, a clone at an
older pin refetched; `allocator_replaced()` asks the proxy's own log (an environment variable with any value had read
as "off"); the test checks the replacement, the bytes and frees on other threads, without opening the GPU through
`libs`; Tracy on demand; unknown allocator names refused and the choice printed; the Linux link's `--as-needed` state
restored with push / pop; the ADR's claims (which heap, which platforms, CMake, the wall time against the rows).

## Not done
- mimalloc: measured faster (1.21x) but its static `new` / `delete` override crashed the full suite against Blender's
  DLLs (ADR 0112's point 4); not fetched, not built.
- KTX / Basis and OpenImageIO: not downloaded - no suite row needs them (ADR 0112's point 6).
- Linux's and macOS's builds with Blender's libraries keep the system allocator; Linux can opt in once its suite runs
  with the libraries. CMake keeps the system allocator.
- `CLAUDE.md`'s dependency convention isn't edited here (the maintainer's file); ADR 0112 records the exception.
