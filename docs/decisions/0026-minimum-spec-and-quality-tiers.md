# 0026: A minimum machine, quality tiers that cap the editor's views, cached shadows

- **Status:** accepted (task 0050; the roadmap of 2026-10-12, approved by the user: "run on at minimum a GTX 1050 Ti,
  a quad-core processor with eight threads at a modest 3 GHz and 8 GB of RAM")
- **Date:** 2026-10-12
- **Requirements:** Q-04
- **Relates to:** ADR 0009 (the render cache), ADR 0013 (no GPU viewport), ADR 0015 (per-object hash memo)

## Context
Every measurement so far came from a 32-thread machine. The editor draws its views with a CPU rasterizer, so its
frame cost scales with cores and pixels. At 8 threads, moving an object took 47 ms a frame. Most of that went to
the Scene and Game views' per-pixel shading, a sun shadow map redrawn for every change, and a sky evaluated for
every pixel before the surfaces covered most of it.

## Decision
- **The minimum machine is emulated** with a process-wide thread ceiling: `JobSystem::set_thread_limit`, a TBB
  `global_control` that Embree, OpenImageDenoise and OpenSubdiv share. It is set from Preferences > Performance >
  CPU Threads, the `BLENDITY_THREADS` environment variable, or `blendity_stress --threads 8` / `--min-spec`. The
  editor targets are measured there.
- **Quality tiers** (Preferences > Performance > Quality tier; console `quality low|medium|high|custom`) hold
  **per machine**, in preferences, never in the scene. They cap **the editor's views only**, so an F12 render
  always uses the scene's settings.

  | Tier | Views' scale | Shadow map | Realtime GI | Frame cap |
  |---|---|---|---|---|
  | Low | 67 % | 1024 | 4 rays, 128 reflective shadow map | 60 fps |
  | Medium | 85 % | 2048 | 8 rays, 256 reflective shadow map | 120 fps |
  | High | 100 % | no cap | no cap | 144 fps |

  Changing a capped setting by hand makes the tier Custom. The first run picks Low on a machine with 8 threads or
  fewer, or about 8 GB of memory or less, and High otherwise. The caps are part of every view's picture key
  (`raster_opt_key`).
- **Viewport Resolution Scale.** The views draw their 3D smaller and scale it up: colour bilinear, depth and ids
  nearest. Overlays, gizmos and text are drawn afterwards at full size, and picking reads full-size planes. A
  camera filter stack with its own internal resolution (a retro console's) is left alone.
- **Cached shadows** (Unity's cached shadow maps):
  - **The frame:** the sun's shadow frame is snapped. Its radius is rounded to steps of 10 %, its centre to 1/16
    of that. F12 renders still fit the frame exactly.
  - **The still layer:** casters whose placement hasn't changed for a second get their own cached map, one per view
    slot.
  - **Moving casters** are drawn over a copy of that map. The result is the same map, bit for bit: a depth buffer
    keeps the nearest surface in any order.
  - **The shadow key** holds a material's surface type and Cast Shadows, not its version, so a colour edit doesn't
    redraw the map.
- **The sky is drawn last.** `clear_environment` only clears the planes. `flush()` draws the sky after the opaque
  surfaces, only where none landed: every fourth pixel, interpolated where the ends agree within 0.4 %, and per
  pixel where they don't (the sun's disc, an HDRI's detail). Sky directions use polynomial atan2 and asin, accurate
  to 2e-6 rad.
- **Memory budgets:**
  - The texture cache drops its least recently used unheld textures past a quarter of RAM, at most 2 GB.
  - Undo drops its oldest steps past an eighth of RAM, at most 2 GB, counting what each step alone holds.
- **Jolt** takes half of Blendity's threads instead of all of them but one.

- **The GPU** (task 0051). A GTX 1050 Ti traces with the compute kernel:
  - **Submissions** are cut to about 100 ms each, under Windows' 2 s driver timeout.
  - **Memory.** A scene goes to the GPUs with every texture a mip level smaller at a time until it fits in 80 % of
    the smallest card's memory. If it can't, the CPU renders it.
  - **Render > Device** gains "GPU + CPU (Auto)", which shares samples by measured speed.
  - **The viewport stays on the CPU** for this machine. ADR 0013 was re-measured at 8 threads: a GPU viewport only
    pays on ray-tracing cards.

## Consequences
- Low looks softer (bilinear-scaled 3D). Shadow texels can be up to about 1.2 times larger in the views.
- Wall-clock time decides which casters are moving. Pixels never depend on it, because the composed map equals a
  full one; only the work does. Verify mode stays exact.
- A user's own frame cap survives an update; a first run on a small machine starts on Low.
