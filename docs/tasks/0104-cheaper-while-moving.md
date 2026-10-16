# 0104: Cheaper frames while moving, exact when you stop (CPU views)

- **Status:** done
- **Requirements:** R-05, Q-01
- **Decisions:** ADR 0039 (new): a view's picture may differ from the exact one only while you interact
- **Model:** main session (Opus)
- **PR:** (link when opened)

## Goal
On a computer without a GPU the views draw on the CPU, and a drag or an orbit redraws the Scene view every frame at full
quality. Blender draws more cheaply while the view moves or something is transformed (`is_navigating` /
`is_transforming`: EEVEE's reduced samples, Cycles' viewport resolution divider) and exactly once you stop. The CPU
views should do the same with their most expensive per-frame setting, the shadow map.

## Scope
- In: while `interacting()`, the CPU views render the sun's shadow map at half its resolution and look it up with
  the 2 x 2 bilinear filter (the Low tier's). The first frame after the interaction renders exactly again.
- In: Preferences > Simpler While Moving (on by default), saved; the console's `views simpler on|off`.
- In: the view's picture key carries the simpler state, so the settled frame is drawn again, never reused.
- In: a Learn section.
- Out: the views on the GPU (their shadow map costs little); F12 renders; the Game view in Play.

## Acceptance criteria
1. During a drag on the CPU path, the shadow map is rendered at half size with the 2 x 2 filter (counted); the frame
   after release equals a view drawn exactly (pixel for pixel).
2. With Simpler While Moving off, every frame is exact.
3. The frame cost of a drag on the CPU path drops (stress `editor_render`).
4. Verify mode finds no stale picture.

## Documentation to update
- CHANGELOG, README count, PERFORMANCE pass 11, ADR 0039, USABILITY row, Learn section.

## As built
- Shipped with task 0103 (round 129 tests).
- 6 drag frames took 6 half-size shadow maps; the frame after release equals the exact picture pixel for pixel; with
  the switch off, every frame is exact; verify mode finds no stale picture.
- The 50k-face Edit Mode drag on the CPU path, laptop setting: 28.3 -> 21.5 ms with task 0103 (the shadow map 3.5 ->
  2.3 ms). A click in the Hierarchy makes nothing cheaper and draws no view.
