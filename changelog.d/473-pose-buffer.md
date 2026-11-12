## 2026-11-12 (round 473): Puppet Posing's solver works on a pose buffer, not on the scene

**Asked:** Pass 22's sixth round (task 0449, ADR 0127): dragging a Puppet handle should cost about what turning one
bone costs.

**Read first:** every frame of a Puppet or AutoPosing drag put the whole start pose back into the scene, built the
joint list again, and wrote two transforms per bone into the scene on each of 32 to 92 iterations - thousands of
scene writes that nothing draws, each one marking a subtree dirty.

**Changed (task 0449, ADR 0127)**
- **A pose buffer** (`autopose::Rig`): everything under the armature as plain numbers, parents first, built once per
  drag. The solver reads and writes only the buffer; an iteration allocates nothing and touches no scene object.
- **The scene is written once a frame**: after the solve each joint whose turn changed gets one write (the hips'
  place and a joint's scale too, when they differ). The start pose
  is no longer written back first (it is, as before, when the pose prior is used: the prior reads the scene).
- **The pose is the same**: the buffer solver leaves every bone within 0.002 mm of the old solver's on 25 recorded
  drags (each handle, pins on and off, a limit hit, a target out of reach, the hips with both feet pinned). The old
  solver stays in the code as the tests' reference (`autopose::solve_reference`).
- **Arms with support bones**: an avatar whose arms carry `_sup` bones placed further out than the next limb bone
  now maps its arms, so Puppet Posing shows its hand handles there.

**Not in this round**
- **The early exit** of ADR 0127 (decision 3). Measured: the pose a drag reaches depends on the whole schedule of
  iterations - ending the 32 main iterations at 24 moves bones by up to 122 mm and 15 degrees, at 16 by 323 mm - so
  no exit can keep the pose within the task's 1 mm. The solve still runs 32 iterations, or up to 92 with pins.
- **The body model on a job** when it takes over 50 ms (scope 6).

**Checked:** the `animation` and `editor` groups, 0 failed: Windows without the libraries 1,887 and 2,326 checks, with them 1,892 and 2,340, Linux 1,887 and 2,330; `round 473` alone 380 checks; the `gpu` group with the libraries 886 checks, the 4 picture checks that also fail on `main`. The whole suite is CI's (round 493's rule). Code review: the buffer solver read against the old one line by line, no difference found; one major finding fixed (a bone destroyed mid-drag left the body model and the prior's rig pointing at freed objects when the buffer was built again), with the armature's parent read every frame, a joint's scale put back, and tests that fail if a frame writes the start pose again.

**Measured:** the stand-in, round 472's build against this one on a quiet machine (a Puppet handle dragged, a window on the card): the solver's own part 1.95 -> 1.11 ms a frame, the controls around it 3.56 -> 2.87 ms, 44.5 scene writes a frame by the solver where it made two a bone on each of 31 iterations; the frame itself reads the same (alone 50.3 -> 55.5 ms, in its world 63.4 -> 57.9: within what a row is good for). The solve was 2 ms of a 50 ms frame: a Puppet drag costs what moving the whole body costs (skinning, normals, uploads), not what the solver costs. The owner's avatar, measured locally: its left hand's handle exists now (a drag frame 49.7 ms headless; the solver 2.6 ms, 219 scene writes a frame); the solver's part for the hips' handle is unchanged there (4.65 -> 4.91 ms: on 200 joints it is the arithmetic per joint, not the scene writes). The avatar-in-the-world rows could not be compared with round 472's: Unity had no licence that morning, so the world came in without Unity's lightmaps and the avatar without its part numbers. Tables: `docs/PERFORMANCE.md`, "Pass 22".

<!-- docs/USABILITY.md -->
after: | Dragging an IK target, the hand trailed its handle by a frame
| A Puppet handle's drag wrote the whole body into the scene thousands of times a frame; an avatar whose arms carry support bones had no hand handles. | Round 473 (ADR 0127): the solver works on a pose buffer and the scene is written once a frame, the pose unchanged; the arm walk steps over support bones. Numbers: `docs/PERFORMANCE.md`, "Pass 22". |

<!-- docs/ARCHITECTURE.md -->
after: | `Editor::solve_after_tools` (`editor/scene_view.cpp`)
| `autopose::Rig`, `build_rig` / `read` / `read_pose` / `read_root` / `to_start` / `solve(Rig &)` / `write` / `stale`, `solve_reference` (`scene/autopose.h`, `.cpp`) | Round 473 (ADR 0127). The full-body solver's pose buffer: every object under an armature with its local transform, the joints by depth. `solve(Rig &)` touches no scene object; `write` puts each changed joint into the scene once. It holds raw object pointers: build it again when `stale` (the structure stamp moved) and never keep it across an undo. `solve(GameObject &)` builds, solves and writes for callers without a drag. `solve_reference` is the solver as it was, for the goldens only. The pose depends on the full iteration schedule: do not add an early exit without new goldens. |
<!-- docs/PERFORMANCE.md -->
append:
## Pass 22: the pose solver on a pose buffer (round 473, task 0449)

What a Puppet drag frame did before: the whole start pose into the scene, the joint list built again, then 32 to 92
iterations that each wrote two transforms per bone into the scene. Round 473 solves on a buffer and writes each
changed joint once.

The stand-in (`blendity_stress --only posing`), round 472's build against round 473's, one after the other on a
quiet machine (a window on the card, ms a frame):

| a Puppet handle dragged (the left hand, AutoPosing on) | round 472 | round 473 |
|---|---|---|
| the solver | 1.95 | 1.11 |
| the solve with what is around it (`autoposing`) | 2.07 | 1.28 |
| the Puppet controls' part of the frame | 3.56 | 2.87 |
| scene writes by the solver a frame | two a bone an iteration, 31 iterations | 44.5 |
| the frame, alone | 50.3 | 55.5 |
| the frame, in its world | 63.4 | 57.9 |
| the first Puppet drag, its longest frame | 102.8 | 110.1 |

**Not reached:** "dragging a Puppet handle costs about what turning one bone costs". The solve was 2 ms of a 50 ms
frame; the frame is the whole body moving - skinning, normals, uploads - as when the chest is turned (34.9 ms alone).
The frame rows moved by less than a row is good for (about a fifth), in both directions. The first drag's longest
frame is still over 100 ms: the body model on a job was not built.

**No early exit.** The pose a drag reaches depends on the whole schedule of iterations: ended at 24 of 32, bones are
up to 122 mm and 15 degrees from where they end today; at 16, up to 323 mm; at 8, up to 515 mm (ten drags on the test
rig). So the iteration count stays (30.9 a frame on the stand-in, as before).

The owner's avatar, measured locally. Its arms carry support bones and were not found before; now Puppet Posing has
its hand handles: the left hand's handle dragged costs 49.7 ms a frame headless (the solver 2.6 ms, 30.9 iterations,
219 scene writes a frame). The solver's part for the hips' handle is unchanged (4.65 -> 4.91 ms): on about 200
joints an iteration is the arithmetic per joint, which the buffer does not change.

**Not measured:** the owner's avatar in the owner's world against round 472. On 2026-11-12 Unity's batch runs ended
with exit 198 (no licence: "Access token is unavailable"), so the world was imported without Unity's lightmaps and
the avatar without Unity's part numbers - another scene than round 472's run drew. In that state neither build
steps a spring chain in Play (round 472's build was run again to check: 0 chains as well), so the rows that differ
are the import's, not this round's. The piece is to be run again once Unity has its licence.
