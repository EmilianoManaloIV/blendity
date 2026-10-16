# 0039: A view's picture may be cheaper only while you interact

- **Status:** accepted (task 0104; the user chose "Yes, cheaper while moving" in the Blender-ideas plan)
- **Date:** 2026-10-16
- **Requirements:** R-05, Q-01

## Context
The views' pictures have been exact: any frame showed what a fresh render of the scene draws (the picture caches are
checked against that in verify mode). On a computer without a GPU (macOS, Windows on ARM, no Vulkan), a drag or an
orbit redraws the Scene view on the CPU every frame at full quality, and its shadow map is the largest single cost of a
changed frame.

Blender lowers its viewport's quality while you navigate or transform (`is_navigating` / `is_transforming`: EEVEE
takes fewer samples, Cycles' viewport draws at a lower resolution) and draws exactly once you stop.

## Decision
- **While `interacting()`** (a mouse button down, a transform, Push/Pull, a modal tool), the views drawn on the CPU
  may draw more cheaply than exact. Today that is one thing: the sun's shadow map at half its resolution (at least
  256), looked up with the 2 x 2 bilinear filter (the Low tier's).
- **The first frame after the interaction is exact.** The views' picture keys carry whether the frame was cheap, so a
  cheap picture is never reused once you stop.
- Preferences > Simpler While Moving, on by default; the console's `views simpler on|off`.
- **Not:** F12 renders, the views on the GPU (their shadow map costs little), Play's Game view when nothing is held.
- Verify mode compares a reused picture against an exact one, as before: a cheap frame is never reused, so it never
  counts as stale.

## Consequences
- A drag on the CPU path costs less; shadows look a little softer and blockier while it lasts, and snap back on
  release.
- The cache keys gain one bit. Anything else made cheaper during interaction later goes behind the same switch and the
  same rule: exact once you stop.
