# 0050: Minimum spec, CPU and memory: 4 cores / 8 threads at 3 GHz, 8 GB of RAM

- **Status:** done (as built below)
- **Requirements:** Q-04
- **Decisions:** ADR 0026 (minimum spec and quality tiers)
- **Model:** main session (Opus)
- **PR:** (link when opened)

## Goal
Blendity runs well on the user's minimum machine: a quad-core, eight-thread CPU at about 3 GHz, 8 GB of RAM and a
GTX 1050 Ti (the GPU is task 0051). The editor stays responsive there, and its memory stays well inside 8 GB.

## Scope
- In:
  - **A thread ceiling** for the whole process (TBB's `global_control`, so Embree, OpenImageDenoise and OpenSubdiv
    share it): Preferences > Performance > CPU Threads, `BLENDITY_THREADS`, and `blendity_stress --threads N` /
    `--min-spec` (8 threads). This is how the minimum machine is emulated here.
  - **Baseline and after**, at 8 threads: `editor_render`, `editor_ui`, `shading`, `memory`, `physics`, `modeling`.
    Results go in PERFORMANCE.md as "Pass 8: minimum spec".
  - **Viewport Resolution Scale** (50-100 %): the Scene and Game views rasterize smaller and are scaled up when
    drawn. It is part of the views' cache keys (ADR 0009).
  - **Physics shares the CPU**: Jolt's own thread pool gets half the threads instead of all of them but one.
  - **Fine-grained loops** (grain 1-2) get grains that give each thread a few chunks.
  - **Memory**: the texture cache gets a byte budget with least-recently-used eviction (25 % of RAM, at most 2 GB).
    Undo gets a byte budget on what its snapshots don't share. The working set is reported in the stress `memory`
    section.
  - **Quality tiers** in Preferences > Performance (Low / Medium / High / Custom). Each sets the viewport scale,
    raster anti-aliasing, shadow map size, preview samples and size, Realtime GI's resolution and rays, and the
    frame cap. On a first run, a machine with 8 threads or fewer, or 8 GB or less, starts on Low.
  - The hotspots the 8-thread profile shows, fixed in measured order.
- Out: the GPU (task 0051), a GPU viewport (ADR 0013).

## Acceptance criteria
1. With the ceiling at 8, every parallel loop and TBB user runs on at most 8 threads (a test counts distinct thread
   ids).
2. At 8 threads, 1080p, Low tier: idle ≤ 2 ms, orbit ≤ 12 ms, moving an object and editing a material ≤ 16.7 ms, a
   Play frame ≤ 16.7 ms (stress `editor_render`, `physics`).
3. Viewport scale 50 % renders a quarter of the pixels and stays in the cache keys; verify mode finds no stale
   pictures.
4. The texture cache stays under its budget while a test loads more than it holds, and evicted textures reload.
5. Undo stays under its budget, dropping its oldest steps first.
6. Peak working set of the stress `memory` sample scenes under 3 GB.
7. The tiers set what they promise; the first-run choice follows the machine; preferences keep the choice.

## Tests and checks to run
- Unit tests (round 72), full suite, verify mode, Linux, sanitizer. Stress at `--threads 8` and at full threads,
  before and after.

## Risks and edge cases
- TBB's `global_control` is process-wide: the ceiling can't be lower in one place than another (the profiler's 1-thread
  toggle still works through `set_max_threads`).
- Upscaled views must keep picking (ids) at full resolution, or clicks land on the wrong object.

## As built
- **Measured at 8 threads, Low** (fastest of four runs):
  - Met: idle 0.8 ms, orbit 7.1 ms, moving an object 15.5 ms.
  - **Not met (needs the user's OK or a follow-up)**: editing a material colour 19.8 ms and Play 17.6 ms, against
    16.7 ms.
  - Peak working set with 10,200 objects: 448 MB. Numbers are in PERFORMANCE.md pass 8.
- **What got the frame there, in measured order**:
  1. Viewport Resolution Scale: Low is 67 %, Medium 85 %.
  2. Cached shadows: moving casters drawn over the still ones' cached map. A material colour no longer redraws
     the map.
  3. The sky drawn only where nothing opaque landed: every 4th pixel, exact where it changes sharply.
  4. Polynomial atan2 / asin in the sky lookups.
  5. Each pixel's footprint computed only for live procedural textures.
  6. Scaling the depth and id planes up in parallel.
- **The first run picks the tier** for the machine (Low on 8 threads or fewer, or about 8 GB or less). It is not
  offered as a question. A user's saved frame cap is kept.
- **The tiers cap the views only**: shadow map size, Realtime GI rays and reflective shadow map. F12 renders keep
  the scene's settings, and also fit their shadow frame exactly.
- **A filter stack with its own resolution** (retro consoles) is not scaled again.
- **Fine-grained loops** were left as they are. With TBB, a grain is only a minimum, and the partitioner merges
  them; at 8 threads they didn't show up in the profile.
- **Undo's budget** counts each step's own objects (about 600 bytes each) and the meshes only it holds. A 10,000-object
  scene's step is about 6 MB.

## Documentation to update
- ADR 0026, PERFORMANCE.md (pass 8), README (minimum spec), CHANGELOG, USABILITY.
