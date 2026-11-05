# ADR 0112: oneTBB's allocator for the Windows build with Blender's libraries, and Tracy for developers

- **Status:** accepted (round 419, task 0395; the user gave permission for the downloads on 2026-11-05)
- **Amends:** ADR 0001 ("no library outside what Blender bundles") for one developer-only library fetched at a pinned
  release on request (Tracy). ADR 0038's question (block-compressed textures) stays open: see point 5.
- **Requirements:** Q-06 (responsiveness), C-01 / C-02 (builds), R-05

## Context

Pass 16's plan (round 419) allowed new dependencies "where the suite shows the gain", on Blender's libraries' terms:
a GPL-3-compatible licence, its text and row in `licenses/`, a `BL_WITH_*` switch, Blendity's own code as the
standalone fallback, shipped only in Blendity-full, and adopted only when a suite row improves 1.3x or more on the
reference machine. The candidates in order: oneTBB's scalable allocator (`tbbmalloc_proxy`, already on disk in
Blender's packages and the allocator Blender links on Windows and Linux), Tracy (a developer's profiler), mimalloc
(MIT, not in Blender's packages), then KTX / Basis block compression and OpenImageIO only if rounds 414-418 left a
need. The user gave permission to download Tracy's client, mimalloc and the others (2026-11-05).

## Measured (this machine, AMD Ryzen / RTX 4070 SUPER, Windows 11; the build with Blender's libraries)

`blendity_stress --only parity` and `--only world_like --quick`, each build run twice in one session, the mean of
the two; the ratio is the system allocator's time over the candidate's (over 1 = faster). Full table: the spec.

| Row | System | oneTBB (tbbmalloc_proxy) | mimalloc 2.1.7 (not adopted) |
|---|---|---|---|
| Select linked from one face, 500k faces | 122.7 ms | 83.6 (1.47x) | 65.6 (1.87x) |
| Export the 50k sphere as STL | 13.3 | 9.1 (1.46x) | 8.4 (1.59x) |
| Undo of the delete, 10,000 objects | 46.8 | 33.9 (1.38x) | 26.4 (1.78x) |
| Export the 50k sphere as FBX | 29.1 | 21.0 (1.38x) | 17.0 (1.71x) |
| Duplicate 10,000 objects | 144.7 | 107.6 (1.34x) | 87.8 (1.65x) |
| Open a scene of 10,000 cubes | 76.5 | 61.6 (1.24x) | 50.5 (1.52x) |
| F12 at 1080p, 400 spheres | 99.5 | 77.9 (1.28x) | 80.5 (1.24x) |
| Exit Edit Mode, 500k faces | 3.2 | 9.6 (0.33x) | 2.8 (1.15x) |
| Modifier: Subdivision level 1, the 50k sphere | 21.3 | 28.1 (0.76x) | 20.2 (1.06x) |
| Memory: 10,000 cubes (MB) | 17.2 | 22.3 | 16.6 |
| Memory: a 500k-face mesh (MB) | 150.1 | 168.6 | 201.5 |

Over all 59 timed rows: oneTBB's geometric mean 1.15x (faster on 48, slower on 4, 10 rows at 1.3x or more, 2 at
1/1.3 or worse); mimalloc's 1.21x (faster on 48, slower on 2, 18 rows at 1.3x or more, none at 1/1.3 or worse). The
parity section's wall time fell 54.8 s -> 19.1 s with either - more than the rows' own times (the section's setup,
its scene builds and teardown allocate too).

## Decision

1. **The Windows build with Blender's libraries uses oneTBB's allocator; every other build keeps the system's.**
   build.bat's `BLENDITY_ALLOCATOR` is `tbbmalloc` by default (`auto` means it too): `tbbmalloc_proxy` linked with
   `/INCLUDE:__TBB_malloc_proxy` (it needs the DLL runtime this build already uses), so every allocation in the
   process - Blender's DLLs' included - goes to oneTBB's allocator, as in Blender's own Windows build. `system`
   (or `BLENDITY_NO_TBBMALLOC=1`) builds without it; `TBB_MALLOC_DISABLE_REPLACEMENT=1` turns the proxy off for a run
   (Windows only - it is oneTBB's Windows switch). Unknown values stop the build. It passes the 1.3x gate on ten rows;
   its costs: Exit Edit Mode on 500k faces 3 -> 10 ms (its large-block release), the Subdivision modifier on the 50k
   sphere 1.3x slower, 30 % more memory for 10,000 cubes.
2. **Linux and macOS keep the system allocator by default.** Nothing here measured them: the Linux build checked each
   round (WSL) and CI are dependency-free. build.sh's `BLENDITY_ALLOCATOR=tbbmalloc` links the proxy on Linux on
   request (`--push-state,--no-as-needed`); on macOS it stops the build - Blender doesn't use the proxy there. The
   CMake route (CMakeLists.txt) keeps the system allocator on every platform.
3. **The build says which allocator it has.** `allocator_name()` / `allocator_replaced()` (src/core/allocator.cpp)
   ask the proxy itself on Windows (`TBB_malloc_replacement_log`), so a proxy turned off or failed is reported as
   such; `libs` prints "Memory: ..."; the dependency list has an "oneTBB malloc" row.
4. **mimalloc is not adopted**, though it measured faster still: compiled in as a static library it replaces `new` /
   `delete` in Blendity's executable only, while Blender's C++ DLLs allocate and free through the C runtime's heap -
   a block one side allocates and the other frees crashed the full test suite (the assets and editor groups) though
   the stress sections had run. Process-wide it would need mimalloc's DLL and its redirect module; that is a later
   question. Its source isn't fetched and no build has it.
5. **Tracy, for developers only.** `BLENDITY_TRACY=1` compiles Tracy's client in (`TRACY_ENABLE`, `TRACY_ON_DEMAND`:
   it collects only while a viewer is connected) from `../blender/extra/tracy`; `src/core/profiling.h`'s `BL_ZONE` /
   `BL_FRAME_MARK` mark the frame, the views, the gather, the deferred render and the card's draw. Without the switch
   they are nothing, so no shipped build has it. `packaging/blender-libs.txt` pins `extra-tracy` (v0.11.1 and its
   commit); `packaging/fetch_blender_libs.sh ... --extras` fetches it and checks the commit - the release and CI
   fetches don't. Its licence is `licenses/tracy-BSD-3-Clause.txt`.
6. **Not adopted:** KTX / Basis block compression (ADR 0038 stays as it is) - rounds 414-416 brought SideRail's views
   to about 8 ms a frame on the card, so no row needs smaller textures on the card; OpenImageIO - round 413 builds a
   texture's mips on every core, and no import row is behind for texture decoding. xxHash, VMA and hash-map libraries
   stay out. Each can come back through this ADR's gate.

## Consequences

- The Windows build with Blender's libraries is faster across the suite (1.15x on the timed rows on average, up to
  1.47x; the parity section 2.9x in wall time) and uses more memory for many small objects.
- A Linux build with the libraries can opt in once someone runs the suite there; macOS follows Blender and doesn't.
- An allocator bug would be process-wide on Windows: `TBB_MALLOC_DISABLE_REPLACEMENT=1` for a run, the build switch,
  or Preferences' "Use Blender's libraries" off (the dependency-free Blendity, with the system allocator).
- `CLAUDE.md`'s convention "no new dependency unless Blender itself bundles it" now has this ADR's exception for a
  developer-only extra (Tracy); the convention file itself is the maintainer's to update.
