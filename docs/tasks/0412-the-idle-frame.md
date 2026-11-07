# Task 0412: The idle frame (round 436)

- **Status:** built
- **Requirements:** Q-04 (interactive editor), Q-06 (responsiveness)
- **Decisions:** ADR 0115 (the scene's change counter; amended), ADR 0105 (the suite)
- **Model:** main session (Opus 5.5)
- **PR:** stacked on #333 (round 435)

## Asked
Pass 18's plan, round 436: on the change counter, keep what the views re-derive every frame - the draw items, the
card's instances and see-through order, the overlay commands - and skip the UI pass on frames that change nothing.
Targets: Shinano's Scene view within 1.5x of Unity's (<= 3.3 ms); 50,000 cubes' gathering 23 -> < 3 ms, overlays and
UI down from 12.9 / 14.2 ms.

## Found
Per-window timing (new, below) and frame-part timers showed where an idle frame at 50,000 objects went (22 ms with
the views unchanged): three per-frame walks of every object with component lookups - procedural shapes (7 ms),
colliders' Fit to Mesh (7 ms), a camera's focus tracking (4 ms) - and the Scene view's icon pass (5 ms), all for state
that changes only when an edit moves the counter.

## Built
- **The views' draw items kept** (`Editor::collect_items`): per view kind, keyed by the scene's hash (cheap since
  round 435) and what the gather reads beside it (`items_key`: Edit Mode, the lighting shown, the views' outline / LOD
  / flatten switches); with an item that depends on the eye (an outline's width, a camera-shaped mesh) the eye too
  (`items_view_key`). Gathered every time: a scene with Camera Flatten or View Angle Shapes in a camera's view, an LOD
  Group's level, a render job's gather (it holds the outline shells). Verify mode gathers anyway and compares.
- **Walks that skip while nothing changed** (`WalkMark`, `walk_due`, `WalkScope`): procedural shapes, Fit to Mesh,
  focus tracking and the icon pass's list of objects to draw for - walked when the counter or the scene moves, while
  the scene changes behind the counter, and every 8th frame (every frame headless) as the scene hash is checked.
- **Per-window timing:** the Profiler's frame profile holds each docked window's own time; the windowed check prints
  the slowest ("the windows' own time").

## Not built (left open)
- **The card's instances** stay rebuilt every frame: they hold the card's mesh and texture ids, which the card's
  caches can reassign, so keeping them needs those caches to say when they change. At 50,000 cubes they are 12 ms, and
  the render call 12 ms: the cubes are one mesh drawn 50,000 times - instancing them is the fix.
- **The overlay commands** stay re-recorded when the view redraws (13 ms at 50,000 cubes, 1.1 at Shinano).
- **The UI pass on frames that change nothing:** the idle frame at 50,000 is now 5.3 ms; Shinano's UI 2.1 ms.

## Measured
`views435.py` and `--windowed-check`, the views on the card, this machine:

| view | before round 435 | after round 435 | after round 436 | Unity |
|---|---|---|---|---|
| Shinano, Scene view | 8.44 ms (gathering 1.83, hashing 0.98) | 6.52 | **4.69** (gathering 0, hashing 0.12) | 2.2 |
| Shinano, Scene + Game view | 6.17 | 5.09 | **2.93** (gathering 0) | 2.5 (Game) |
| 50,000 cubes, Scene view | 96.4 (gathering 21.9) | 92.1 | **61.7** (gathering 0) | - |
| 50,000 cubes, idle frame (views unchanged) | not measured | 14.5 | **5.3** | - |

Shinano's Scene + Game view is within 1.2x of Unity's Game view; its Scene view alone is 2.1x Unity's (target 1.5x
not met: the UI 2.1 ms and the overlays 1.1 ms are what is left). The fuzz: 6,000 frames, no stale picture, no change
the counter missed.

## Review (Opus)
Changes needed, fixed:
- **The Game view could take a render's draw items:** F12 and MoonRay gather into the same slot with the views'
  outline, LOD and flatten switches off. The switches are now in the key.
- **A render job's gather taken from the kept list held no outline shells:** a render job now always gathers.
- **The `focus` command could wait for the walk's next due frame:** it now applies at once (`apply_camera_focus`).
- **LOD Groups' levels live in a cache that drops old meshes:** an item drawn at a level is never kept.
- **The walks recorded the counter after the walk:** they now read it before, so a bump made during the walk walks
  again; one `WalkScope` replaces the copied clean-up structs.

Left as noted: a write that replaces a mesh past every funnel can leave a kept item pointing at a freed mesh until the
next check (7 frames in a window); the fuzz and the soak found none.

## Tests (tests_round436, tests_round436_review)
- Orbiting an unchanged scene answers every gather from the kept items; a `set` gathers again; verify mode finds no
  stale list.
- Idle frames skip the walks (at least two in each of 7 frames of 8); a Fit to Mesh ticked by `set` is fitted the next
  frame.
- A render's gather followed by the Game view's: no stale list in verify mode.
