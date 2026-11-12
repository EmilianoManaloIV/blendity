## 2026-11-12 (round 474): Play with the springs stops building meshes whole; pins and trails do less each frame

**Asked:** Pass 22's seventh round (task 0450): what sits around a posed frame - trails, the assistants' views,
AutoPosing's pins, Play with an avatar's springs - should stop costing more than the pose.

**Measured first** (the stand-in, round 473's baseline; a part is built only when it is over 0.3 ms a frame or over
its budget): Play with the springs 82 ms a frame with 20 render meshes built whole every frame (a playing clip
builds none); trails, onion skins and the zoetrope add about 6 ms to a drag frame (the trail 2.6 ms, the constraints
1.7 ms instead of 0.4); the Puppet controls 2.9 ms a frame. Under the line and left alone: Centre of Mass (0.24
ms), the Avatar window (0.17 ms), the Rigging window (0.27 ms); the first Preview, the first Record and entering
Play (92, 71 and 96 ms: under their 100).

**Changed (task 0450)**
- **Play with the springs**: the scene's own physics step looked up every mesh's bounds at the start of each Play
  frame, even with no rigid body in the scene. That deformed every skinned mesh before the springs had moved the
  pose, and the Game view then built its meshes whole. With no body the step now does nothing; with one it is as it
  was.
- **AutoPosing's pins** are kept until what they are read from changes (the objects, the clip's contacts at the
  playhead, the locks), where every frame walked the scene up to three times.
- **Motion trails**: a key edit samples again only the frames around the edited keys (two keys either side), where
  it sampled the whole trail again - Record keys on every frame of a drag. Turning trails off releases what they kept.

**Not in this round**
- **Trails still pose the live scene** when they sample (the spec's private copy of the rig would have to repeat
  the constraint solves exactly: too large here). What changed is how many frames an edit samples again.
- **With a rigid body in the scene's own physics** (every build without the libraries; with them only a body added
  during Play) the bounds are looked up as before, and the meshes are built whole again.
- **The springs' own step** (4.9 ms a frame on the stand-in: three scans of the scene and a body fit hashed every
  frame) is read and listed, not changed. The release frame, the analyses on a timer, the first Puppet drag's
  longest frame (110 ms) and AutoPhysics (58 ms against 50) are not touched.

**Checked:** the `animation`, `editor` and `physics` groups, 0 failed: Windows without the libraries 2,229, 2,331 and 190 checks, with them 2,230, 2,349 and 208, Linux 2,229, 2,331 and 190; `round 474` alone 342 checks (338 with the libraries, where one test steps aside: under Jolt a mesh without a collider is no floor); the `gpu` group with the libraries 886 checks, the 4 picture checks that also fail on `main`. The whole suite is CI's (round 493's rule). Code review: no blocking finding. Its notes, kept here: the pins come from a cache, so a frame that commits an undo step walks once; an enabled Rigidbody in the scene's own physics loses the Play saving (follow-up: leave skinned objects out of the statics, or use last frame's bounds); with Record on, a keyed frame samples about 13 trail frames again on the test rig and compares every key of the clip (work that grows with the clip's keys); the trail keeps one copy of the clip's tracks; sampling part of a trail again could differ from sampling all of it where a constraint starts from its last answer (not seen in 37 kinds of edit on the test rig, not tried on long IK chains).

**Measured:** the stand-in, round 473's build against this one on a quiet machine (a window on the card). **Play with the springs: 82.3 -> 44.5 ms a frame alone, 101.5 -> 69.8 ms in its world** (longest frame 93 -> 61 and 108 -> 81); evaluations a frame 27 -> 10.9, tangents built whole 5.3 -> 0 ms, mesh uploads 28.1 -> 17.0 ms, the interface's part (where the physics step sat) 18.9 -> 5.0 ms; entering Play 83.5 -> 70.6 ms. The row is still over its budget: what is left is the springs' own step (3.8 ms), the outline shells built whole and the uploads. **Pins and trails: nothing a row shows.** The drag with trails, onion skins and the zoetrope reads 31.8 -> 28.7 ms (within what a row is good for), its trail part 2.16 -> 2.10 ms: the bench's drag keys on every frame, where the trail was already sampled a few frames at a time. The Puppet handle's row is unchanged (52.4 -> 52.6 ms alone, 61.5 -> 62.0 in its world; controls 1.36 -> 1.39 ms). One check finding, leaving Play 5.3 -> 8.2 ms, did not repeat (5.1 ms in the round's own baseline). The owner's avatar in the owner's world, measured locally, round 472's run against this round's (Unity signed in for both; rounds 473 and 474 together): Play with the springs reads the same in a window (45.5 -> 45.0 ms) and 67.7 -> 55.0 ms headless (normals 11.5 -> 6.6 ms, meshes moved in place 10.3 -> 5.7 ms); the Puppet handles 56.2 -> 52.5 ms (hips) and 55.5 -> 52.2 ms (head); the other rows within a tenth. Tables: `docs/PERFORMANCE.md`, "Pass 22".

<!-- docs/USABILITY.md -->
after: | A Puppet handle's drag wrote the whole body into the scene
| In Play, an avatar with spring bones built every one of its meshes whole on every frame; with motion trails on, every frame of a recorded drag sampled the whole trail again. | Round 474: the scene's own physics step no longer looks up every mesh's bounds when no rigid body exists; a key edit samples only the trail's frames around it; AutoPosing's pins are kept. Numbers: `docs/PERFORMANCE.md`, "Pass 22". |

<!-- docs/ARCHITECTURE.md -->
after: | `autopose::Rig`, `build_rig`
| `physics_step` (`scene/scene.cpp`); `Editor::autopose_pins` / `PinCache` (`editor/autopose_ui.cpp`); `trail_follow_edit` (`editor/motion_trail.cpp`) | Round 474. The scene's own physics step (no Jolt world) gathers its bodies first and returns without one: asking a skinned mesh for its bounds evaluates it, and doing that before the frame's pose is final makes the views build it twice - nothing that runs before the constraints and the springs in Play may ask for evaluated bounds. The pins are kept per armature and dragged bone, keyed on the scene's serial, the structure stamp, the clip, a hash of its contacts, the contacts active at the playhead and the locks. The trail keeps a copy of the clip's tracks and compares it after an edit; a change of clip, length, rate, loop or track set starts the trail over. |
<!-- docs/PERFORMANCE.md -->
append:
## Pass 22: Play with the springs, pins and trails (round 474, task 0450)

Measured first, on the stand-in with round 473's build: a part was built only when it cost over 0.3 ms a frame or sat
over its budget. Play with the springs built 20 render meshes whole on every frame; a playing clip builds none. The
cause was the scene's own physics step, which asked every mesh for its bounds before the springs had moved the pose.

The stand-in (`blendity_stress --only posing`), round 473's build against round 474's, one after the other on a
quiet machine (a window on the card; ms a frame, the longest frame in brackets):

| row | alone, 473 | alone, 474 | in the world, 473 | in the world, 474 |
|---|---|---|---|---|
| Play with the springs | 82.3 (93) | 44.5 (61) | 101.5 (108) | 69.8 (81) |
| the clip playing, Scene + Game view | 35.9 (88) | 36.2 (91) | 39.5 (114) | 39.4 (114) |
| a Puppet handle (the left hand, AutoPosing on) | 52.4 (59) | 52.6 (63) | 61.5 (79) | 62.0 (84) |
| the Move gizmo on the left hand's IK target | 10.1 (11) | 10.1 (13) | 16.6 (24) | 17.9 (22) |

| Play with the springs, alone: where the frame went (ms a frame) | round 473 | round 474 |
|---|---|---|
| evaluations a frame (a count) | 27.0 | 10.9 |
| meshes moved in place (normals and tangents in it) | 13.2 | 8.2 |
| tangents built whole | 5.3 | 0.0 |
| mesh uploads | 28.1 | 17.0 |
| the interface's part, which held the physics step | 18.9 | 5.0 |
| the springs' own step | 4.7 | 3.8 |

| once-only costs (ms; budget 100) | round 473 | round 474 |
|---|---|---|
| entering Play | 83.5 | 70.6 |
| the first Preview on | 79.0 | 76.2 |
| the first Record on | 69.0 | 66.6 |
| the first Puppet drag, its longest frame | 104.4 | 103.2 |
| AutoPhysics (budget 50) | 58.9 | 58.5 |

The drag with trails, onion skins and the zoetrope on (Record on, in the world): 31.8 -> 28.7 ms a frame, its trail
part 2.16 -> 2.10 ms. That is no change a row can show: the bench keys on every frame of the drag, and the saving is
for an edit of one key in a long clip.

Not reached: Play with the springs is still over its budget; with a rigid body in the scene's own physics the meshes
are built whole as before; trails still pose the live scene to sample.

The owner's avatar in the owner's world (measured locally), round 472's run against round 474's - rounds 473 and 474
together, both with Unity signed in (round 473's own run had no Unity licence and is not comparable):

| row (a window on the card, ms a frame) | round 472 | round 474 |
|---|---|---|
| Play with the springs | 45.5 | 45.0 |
| Puppet: the hips' handle | 56.2 | 52.5 |
| Puppet: the head's handle | 55.5 | 52.2 |
| FK: the chest turned | 41.8 | 40.9 |
| the clip playing, Scene view only | 50.0 | 50.3 |
| IK: the target moved, Record on | 25.3 | 25.0 |

Headless, Play with the springs reads 67.7 -> 55.0 ms there. The avatar's Play frame in a window did not change: its
frame is the card's work and presenting (g-buffer, lighting and presenting 33 of 45 ms).
