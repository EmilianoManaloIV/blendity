# 0085: Per-view waste - the voxel GI grid's columns, the shadow map's ids plane

- **Status:** done
- **Requirements:** Q-04
- **Decisions:** none
- **Model:** main session (Opus)
- **PR:** https://github.com/EmilianoManaloIV/blendity/pull/76

## Goal
Remove work every view repeats each frame without need (the performance pass, plan item 0085).

## Measured first
On a moved-object frame the views' own setup around the rasterizer is small: gathering draw items 0.07 ms, the scene
hashes 0.03 ms (three a frame). So the plan's other candidates here - component lookups by role instead of
`get<T>()`, a selection bitmap for `is_selected` - would save microseconds in these scenes, and were left out (they
matter with thousands of objects, which this pass doesn't target). Shading is 5.1 of the Scene view's 7.2 ms: tasks
0088-0089.

## Scope
- In:
  - `voxel_grid_fit(..., allocate = false)` + `voxel_grid_allocate`: a view checking whether its grid still fits no
    longer allocates and zeroes a fresh column pyramid (about 87,000 columns at 256) every frame to find out.
  - Shadow maps (`make_depth_only`) keep no ids plane: not cleared, not written.
  - The GI blur writes every texel instead of copying the unblurred plane first.
- Out: the scene hash / modifier keys shared across views (render_view.cpp explains why not); `is_selected`.

## Acceptance criteria
1. Moving an object inside the GI grid makes no new pyramid; leaving it makes one.
2. A grid fitted without columns has the same placement and key.
3. A shadow map has no ids plane and the same depth as a target with one.

## As built
- Tests: round 111.
