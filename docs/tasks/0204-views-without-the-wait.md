# 0204: the views without the wait - frames left running on the card, picking's planes when asked

- **Status:** done (round 224)
- **Requirements:** R-07, Q-04; task 0203's rank 1
- **Decisions:** ADR 0061 amended (frames left running; the Scene view's planes copied back on demand)
- **Model:** main session
- **PR:** #169 (stacked on #168)

## What was there
Task 0203 found every window row spending most of the views' GPU path waiting. Each frame:
- the Scene view asked for its depth and ids back, for picking that only a click, a box select, the knife or a
  material drop needs;
- every view's submission was waited for before the editor went on.

The card's own passes took 0.3-2.0 ms of a 3.2-5.4 ms path.

## What changed
**On the card** (`gpu::Raster`):
- `render_async`: a view's frame submitted and left running.
- Every entry point that touches the card's resources waits for it first (`settle`): the command buffer, the
  frame's uniforms and the staging it reads stay untouched until the card is done.
- A wait that fails marks the card lost for good: every later use fails, and the editor draws on the CPU, as a
  failed wait always did.
- The present doesn't wait: the passes' dependencies order it after the views' frame on the queue.
- The finished frames' own times reach the Profiler through `take_async_times`.

**In the editor:**
- When the card shows the Scene view and draws its overlays and cage, outside verify mode, its depth and ids stay on
  the card.
- `ensure_scene_planes` copies them back when a reader asks: a click, a box select, Edit Mode's picks, the knife,
  a material drop, or verify mode's check.
- They are copied only while they are still the Scene view's, at its size. Any frame drawn some other way forgets
  them: on the CPU, scaled, piloted, after a device switch or a dropped card, or when another view takes the slot.
- With nothing to copy back, every view's frame is left running.

## Measured (RTX 4070 SUPER, a 2560 x 1369 window, `tools/bench_ui.py`; a frame)

| Row | Before | After | Blender 5.2.2 |
|---|---|---|---|
| The 50k sphere, Solid | 6.0 ms | 3.0 ms | 4.6 ms |
| The 500k sphere, Solid | 8.2 ms | 3.9 ms | 5.0 ms |
| A million triangles, Solid | 6.3 ms | 3.5 ms | 4.2 ms |
| A million triangles, Shaded / Material Preview | 6.4 ms | 3.3 ms | 5.5 ms |
| 10,000 cubes, Solid | 20.1 ms | 13.9 ms | 24.2 ms |
| Edit Mode, 50k | 6.2 ms | 3.6 ms | 5.1 ms |
| Edit Mode, 500k | 16.6 ms | 11.7 ms | 5.5 ms |
| Playing 1,000 keyed objects | 17.4 ms | 11.4 ms | 11.3 ms |

- Blendity is now ahead on the viewport rows, 1.2-1.7x.
- Edit Mode on 500k faces is left for task 0205, which takes the cage: the card's passes take 6 ms there.
- Playback is level; task 0206 takes it.

The first version still copied the planes back before every frame (in the cache check rather than the GPU path),
where the breakdown didn't show it: 6.0 -> 4.9 ms on the 50k sphere. With the call where it belongs, 2.8.

## Checked
- tests/test_gpu.cpp `tests_round224`:
  - a frame drawn again is one submission with its planes left on the card;
  - a click picks the Cube;
  - with the Cube moved away, a click where it was picks what is there now, on the card and after a switch to the
    CPU;
  - verify mode reads the planes every frame.
- The gpu, editor and render groups pass.
- An Opus review found, all fixed:
  - the planes' slot not forgotten when the next frame was drawn some other way, or when another view took it: a
    click could read old planes, or write past a target that shrank;
  - the CPU's cage tested against old depth when the card couldn't draw it;
  - a failed wait ignored (now the card is marked lost and the editor draws on the CPU);
  - the Profiler's times landing on the wrong frame;
  - a test that could pass with a stale pick.
