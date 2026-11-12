## 2026-11-12 (round 472): Record, scrubbing and the Animation window cost what the pose costs; the limb is drawn where the solver puts it

**Asked:** Pass 22's fifth round (task 0448, ADR 0126): posing with Record on, scrubbing and playing a clip should
cost what the pose costs, and a dragged IK target's limb should be drawn where the solver puts it for that frame.

**Measured first** (round 471's runs, the owner's avatar, 275 bones keyed): Record cost 10.5 ms a frame and
serialised the clip once a frame; on a scrub the clip's apply was 1.4 ms and the Animation window 1.5 to 1.9 ms;
the constraints 0.2 ms. On the stand-in the hand was drawn up to 16.5 mm behind its IK target on 30 of 35 frames
of a drag.

**Changed (task 0448, ADR 0126)**
- **Record** takes the clip's text once per undo step instead of on every frame, and reads only the objects that
  moved since it last looked (`transform_version`), where it read nine numbers of every object under the root
  through a map keyed by text. The keys written are the same.
- **A clip's tracks stay bound to their objects** (`apply_clip`): found once per clip and root, kept while the
  clip's tracks and the objects under the root (which, hung from what, called what) are the same - no map of paths
  built per call. Blend-shape tracks are bound by index. `AnimationClip::track` / `find` use an index from 24 tracks.
- **The Animation window** keeps its rows and each row's keys while only the playhead moves.
- **G / R / S** apply and redraw only when an input changed (the mouse, a typed number, an axis, a modifier, the
  camera): a held modal with a still mouse costs nothing.
- **The limb is drawn where the solver puts it**: the constraints are solved once more right after the Scene
  view's tools moved something, before the view gathers (not in Play).

**Not in this round:** the rig registry (the constraints cost 0.2 ms a frame, under the spec's 0.3 ms line - with
the second solve a drag now pays 0.5 ms for them, so the registry is worth a look again in the write-up); the
second solve after an Inspector drag (the Scene view's tools only); what a scrub mostly costs - the meshes moved
in place, their normals and uploads, and the card.

**Checked:** the `animation` and `editor` groups on main e978001a with the round on it, 0 failed: Windows without the libraries 1,507 and 2,327 checks, with them 1,512 and 2,342, Linux 1,507 and 2,331; `round 472` alone 255 checks; the `gpu` group with the libraries 795 checks, the 4 picture checks that also fail on `main`. The whole suite is CI's (round 493's rule). Code review: one major finding fixed (two animated roots rebuilt each other's bindings on every frame); one nit left for the write-up (three caches compare a raw pointer and a counter).

**Measured:** measured locally, the owner's avatar in the owner's world (275 bones keyed; a window on the card): an IK target dragged with Record on 41.3 -> 25.3 ms a frame (the same drag with Record off: 23.7), Record's own part 10.5 -> 0.85 ms, clip serialisations 1 -> 0 a frame. On a scrub the clip's sampling 0.64 -> 0.05 ms, its apply 0.77 -> 0.17 ms, the Animation window 1.5 -> 0.4 ms (1.9 -> 0.7 with the Game view): about 2.3 ms of a 50 ms frame, so the scrub and play rows read the same (50.0 -> 49.4, 49.8 -> 50.0). The stand-in: the hand was drawn up to 16.5 mm behind its IK target on 30 of 35 frames of a drag, now 0.000 mm on all 35; `AnimationClip::find` on a 201-track clip 0.74 -> 0.009 ms (the `animation` section's row, over its budget before). The second solve doubles the constraints on a drag: 0.24 -> 0.48 ms, 46 -> 92 name walks. Tables: `docs/PERFORMANCE.md`, "Pass 22".

<!-- docs/USABILITY.md -->
after: | In Play, in a large world, one mesh of a moving avatar
| Dragging an IK target, the hand trailed its handle by a frame (up to 16.5 mm on the stand-in); with Record on every frame of a drag wrote the whole clip out as text. | Round 472 (ADR 0126): the constraints are solved again after the Scene view's tools; Record takes the clip's text once per undo step and reads only what moved. Numbers: `docs/PERFORMANCE.md`, "Pass 22". |

<!-- docs/ARCHITECTURE.md -->
after: | `edit_epoch::structure()` / `transforms()` / `Moved`
| `Editor::solve_after_tools` (`editor/scene_view.cpp`); `bind_clip` / `clip_bindings` / `names_below`, `AnimationClip::layout` / `touch_keys()` (`scene/animation.cpp`, `.h`); `Editor::anim_row_layout`, `anim_record_object` (`editor/animation_window.cpp`) | Round 472 (ADR 0126). The frame's order: the constraints are solved before the windows are drawn and once more after a Scene view's tools wrote a transform, before that view gathers. A clip's bindings to objects are kept on the clip per root (32 at most) while `layout`, the track count, the structure stamp less the applies' own bumps, and `names_below(root)` hold; code that changes a track's path, property or axis, or adds, removes or reorders tracks, must call `touch()` - `touch_keys()` is for key values only. The Animation window's rows are kept per clip version; Record's `anim_.shown_objects` holds each object's `transform_version` at the last look. |
<!-- docs/PERFORMANCE.md -->
append:
## Pass 22: Record, scrubbing and the Animation window (round 472, task 0448)

Measured first, from round 471's runs (the owner's avatar in the owner's world, 275 bones keyed): Record cost 10.5 ms
a frame and wrote the whole clip out as text once a frame; on a scrub the clip's sampling and apply were 1.4 ms and
the Animation window 1.5 to 1.9 ms; the constraints 0.2 ms a frame (46 to 92 walks by name), under the 0.3 ms line
the task set for building the rig registry - it is not built. On the stand-in the hand was drawn up to 16.5 mm behind
its IK target.

The owner's avatar in the owner's world, measured locally (a window on the card, ms a frame). Before: round 471's
run of 2026-11-11; after: round 472 on main e978001a, 2026-11-12, the machine quiet but for the bench's own Unity
import and Linux probe:

| part of a frame | round 471 | round 472 |
|---|---|---|
| Record, an IK target dragged: `record` | 10.54 | 0.85 |
| ... clip serialisations a frame | 1 | 0 |
| a scrub, Scene view only: `clip` (sampling) | 0.64 | 0.05 |
| ... `anim apply` | 0.77 | 0.17 |
| ... `window animation` | 1.49 | 0.41 |
| a scrub, Scene + Game view: `window animation` | 1.90 | 0.67 |
| the clip playing: `clip` + `anim apply` | 1.35 | 0.19 |
| an IK target dragged: `constraints` (the second solve) | 0.24 | 0.48 |
| ... walks by name a frame | 46 | 92 |

| row | round 471 | round 472 |
|---|---|---|
| IK: the target moved, Record on | 41.3 | 25.3 |
| IK: the left hand's target moved | 23.4 | 23.7 |
| IK: the left foot's target moved | 22.2 | 23.2 |
| scrubbing, Scene view only | 50.0 | 49.4 |
| scrubbing, Scene + Game view | 47.4 | 47.3 |
| the clip playing, Scene view only | 49.8 | 50.0 |
| the clip playing, Scene + Game view | 48.9 | 47.9 |
| FK: the chest turned | 38.3 | 41.8 |
| Puppet: the hips' handle | 55.3 | 56.2 |
| still: the Scene view | 5.7 | 6.6 |
| a prop of the world moved | 8.6 | 11.2 |

Read with care: the two runs are a day apart and `main` took Pass 23's and Pass 24's rounds in between, so the rows
this round does not touch (the last four) show what a day and those rounds move, not this round. Rows are good to
about a fifth.

**Not reached:** a scrub or a playing clip at what the pose costs. What this round removed is 2.3 ms of a 50 ms
frame; the rest is the meshes moved in place, their normals and uploads, and the card (rounds 469 to 471's ground,
and round 474's for Play).

The stand-in (`blendity_stress --only posing`, `--only animation`), main e978001a against round 472 one after the
other on a quiet machine:

| | main | round 472 |
|---|---|---|
| the hand behind its IK target during a drag (35 frames) | up to 16.512 mm, on 30 frames | 0.000 mm, on 0 frames |
| `AnimationClip::find`, a 201-track clip (ms) | 0.7436 (over its budget) | 0.0092 |
| a scrub, Scene + Game view, alone / in the world | 68.3 / 88.2 | 68.1 / 88.5 |
| a scrub, Scene view only, alone / in the world | 46.8 / 59.4 | 46.1 / 59.4 |
| the Move gizmo on the left hand's IK target, alone / in the world | 9.5 / 14.6 | 9.5 / 14.6 |
| G on the left hand's IK target, alone / in the world | 12.3 / 20.0 | 12.2 / 20.6 |
| a Puppet handle, alone / in the world | 50.3 / 60.5 | 54.6 / 62.4 |

The stand-in's rows for a playing clip are not steady enough to compare: "the clip playing, Scene + Game view, alone"
read 20.4, 16.7, 30.5 and 30.4 ms in four runs of this night, two builds, and the check of round 472 against its own
baseline flags four such rows. The saved baseline is round 472's.
