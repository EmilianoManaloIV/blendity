# Task 0421: The soak's memory (round 445)

- **Status:** in review
- **Requirements:** Q-04 (interactive editor), Q-06 (responsiveness)
- **Decisions:** ADR 0105 (the performance suite and the responsiveness budget)
- **Model:** main session (Opus 5.5)
- **PR:** stacked on the round 437 branch

## Asked
Pass 17's soak (round 433, task 0409) found the editor's working memory at 1.1-1.3 GB for the 26 flat minutes of a
30-minute fuzz, and nobody knew what held it. Round 445:
- Memory by subsystem in the soak's report: each cache reports its bytes (cheap accounting of what exists, not an
  allocator hook).
- Find the holder; release or bound it; show before and after.

## Found
- The soak (`BLENDITY_FUZZ_MINUTES=N blendity_stress --only editor_fuzz`) is deterministic by frame, so one seed finds the
  growth: `BLENDITY_FUZZ_SEED=4242` starts it at about frame 5,600 (seed 12345, the default, never grows past 120 MB in 5
  minutes; seed 777 reaches 173 MB).
- **Not** the scene (0.7 MB), the undo history (0.0), the image cache (1.3), the renderers' scratch (1-22), the views'
  planes (5-31) and pictures (2-8), the shadow maps (20), the lightmaps or the voxel GI (0).
- **A procedural surface.** The growth is a `Surface` job on a worker (the jobs list at the growing frames says so): the
  fuzz sets a surface to 4096 x 4096.
  - While it evaluates, its planes hold 44 bytes a texel: **738 MB** at 4096 x 4096 (height, roughness, mask, alpha and
    metal 4 bytes each; albedo and emission 12 each). The growth shows as 20-30 MB a frame, to 610 MB and past, and falls
    back when the job lands.
  - When it lands, its six images with their mips are stored in the procedural-texture cache: **448 MB**, held for the
    rest of the run. That cache (`g_cache` in `procedural_texture.cpp`) was not in `texture_cache_bytes()` and had no
    limit, so a second large surface only adds to it.

## Built
- **Accounts:** `Editor::memory_accounts()` (name, bytes) for the scene, the undo history, the image cache, the
  renderers' scratch, the views' planes and pictures, the shadow maps, the procedural-texture cache, a running surface
  job's planes, the voxel GI, the lightmaps and the path tracers - with `RenderTarget::plane_bytes`,
  `Renderer3D::scratch_bytes`, `PathTracer::memory_bytes`, `LiveTexture::memory_bytes` and
  `procedural_texture_cache_bytes`. `perf memory` prints them with the process's working set and the bytes in no account.
- **The soak's report:** every minute a `soak: minute M holds: ...` line; every frame that grows the process by 20 MB
  (was 100) prints the accounts, the jobs running and the frame's parts with its events; the report's last note holds
  the end state.
- **Bounded:** the procedural-texture cache has a budget (1/32 of the machine's memory, 128-512 MB; 512 on a 16 GB
  machine) for what can be dropped. After every store (a surface landing, a synchronous evaluation)
  `procedural_texture_cache_trim` drops the least recently used entries that only the cache holds (no material or view
  has their images); entries in use are pinned and not counted, so a few big surfaces in use don't empty the unused
  ones. A dropped surface is remembered with its hash: the surface poll leaves it alone until it is edited or something
  asks for it again, so it is not evaluated again in a loop. A dropped drag preview flags nothing; clearing or
  forgetting an asset clears its mark.
- **The renderers' scratch** (items, overlay commands, chunk tables, GI texels - kept between frames, cleared not freed):
  with the total over 96 MB, `trim_idle_memory` gives back a renderer that has flushed or batched nothing for 5 seconds
  (`Renderer3D::use_count`; between frames only). A renderer that is drawing - an orbit in Edit Mode - keeps its scratch,
  so it is not allocated again every few seconds.
- **Races and sizes:** the accounts leave the F12 path tracer out while a render job exists (its worker builds it); a
  running surface's planes are sized by the size it evaluates (a power of two) and, while it builds them, its images. The soak's runs never reached it (22 MB); Edit Mode on the 500k sphere keeps 152 MB (the test).

## Measured (this machine, seed 4242)

| | before | after |
|---|---|---|
| accounts at minute 1 | procedural textures not counted: **570-650 MB unaccounted** | procedural textures **448.0 MB**, unaccounted **51-59 MB** |
| the working set, minutes 1 -> 4 | 699 -> 646 MB (flat) | 560 -> 585 MB (flat) |
| the working set's peak while the surface evaluated | 610 MB at frame 13,510 (a 1.2-minute run), 1.1-1.3 GB in the 30-minute | not changed (the planes) |

The cache's 448 MB is one 4096 x 4096 surface and sits under the 512 MB budget: the bound shows on the second one, in
the test (a budget of 1 byte drops the unused one and keeps the one in use). The 100-130 MB the working set falls by is
the planes' allocator slack and the first run's stand-in textures.

## Left open
- **A running surface's planes** (738 MB at 4096 x 4096) are a worker's working memory: only an evaluation that writes the
  alpha, metal and emission planes when a layer writes them (not for every texel of every surface) would cut it by 40%.
  Left: every generator and filter reads them.
- **Six images for every surface**, built even when its Normal, Roughness or Height Map switches are off.
- The default seed's soak does not grow; the 30-minute soak with seed 4242 was not rerun (4-minute runs were).

## Tests (tests_round445, the editor group)
- The accounts name the subsystems, the bytes add up, `perf memory` says them.
- The renderers' scratch after Edit Mode on the 500k sphere: `trim_scratch` frees exactly what is kept, nothing under the
  limit, and the next frame draws.
- The idle trim on a fake clock: the scratch is kept through 6 s of orbiting frames and 2 s idle, and freed after 12 s.
- The surface cache's budget: the unused entry is dropped and marked, the one in use stays, asking again clears the mark;
  pinned entries aren't counted against the budget; a dropped preview flags nothing and clear forgets the marks.
- The poll: a dropped surface is not queued again; an edit queues it.
