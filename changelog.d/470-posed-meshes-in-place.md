## 2026-11-11 (round 470): A posed mesh follows in place - corners split there, ranges for a small move, settled at rest

**Asked:** Pass 22's third round (task 0446, ADR 0124 decisions 5 to 7), from round 468's table: while an avatar is
posed, most of its meshes were built again from nothing on every frame, with MikkTSpace, because a few corners on
sliver faces changed which neighbours they are smoothed with; every moved mesh with a normal map or several materials
went to the card whole; and a posed mesh kept approximate tangents for good.

**Changed (task 0446, ADR 0124)**
- **A new split is made in place.** A corner whose normal no longer matches the render vertex it shared gets one of
  its own (`Mesh::update_in_place`: `split_corner`, `split_slots`); the move is no longer refused and the mesh no
  longer built whole. At most one render vertex a corner; a settle or a build shares them again.
- **Tangents around what moved.** The quick tangents are worked out for the vertices of the moved faces only, each
  summed in the whole pass's order (the same bits); a small move no longer rewrites, or sends, every tangent.
- **Ranges for meshes with a normal map and for meshes with several materials.** The card takes the moved vertex
  ranges; a mesh of triangles sends no indices at all.
- **Posed meshes are made exact at rest.** Once nothing has changed for two frames, two posed meshes a frame are
  built again (vertices shared anew, MikkTSpace), as ADR 0034 settles an edited mesh - also meshes that only had
  corners split.
- **The smooth-by-angle normals pass runs on every thread** and reads the vertex-face table kept per topology (it
  built one on every call); the sums and their order are unchanged.
- The Profiler's Posing block: "corners split in place" (was "refused for a new split") and "meshes settled".

**Not in this round:** outline shells moved in place; the settle on a worker (two meshes a frame on the main thread
instead - written in the spec's deviations).

**Checked:** 25,777 unit checks without the libraries and 25,783 on Linux, 0 failed; 27,483 with the libraries, where the 4 `gpu` picture checks that fail on `main` fail the same way (`test_gpu.cpp:887` twice, `:2226`, round 455's Lanczos picture). The round's own tests: 648 random-move steps on smooth-by-angle, sharp, seamed, n-gon, concave, custom-normal, zero-area and fan-flipping meshes draw what a fresh build draws to the bit (913 corners split in place, 0 refused, 0 built whole); partial quick tangents equal the whole quick pass to the bit over 192 steps; 1,000 swings add no render vertex after the first; on the card a finger turned on a three-material mesh of triangles sends under 1 KB of 892 KB a frame (quads: 142 KB of 573 KB, their indices grouped again). `perf_suite.py --check` against the round's own baseline: 2 findings, both rows whose card time swings between runs of one build with the same counts (written in PERFORMANCE); against `main`'s: 3 of the same kind. The coverage map, `changelog.py check` and `docs_check.sh` pass. The code-reviewer approved (its findings - the settle bump made a move, a held button is not a rest - are in the commit).

**Measured:** the owner's avatar (measured locally, on the card, against round 469's run): the chest turned 38.5 -> 28.5 ms a frame alone and 61.9 -> 47.2 ms in the owner's world; a scrub 65.0 -> 32.9 and 87.1 -> 53.3; the clip playing in both views 73.2 -> 35.6 and 104.2 -> 51.0; a foot's IK target 25.7 -> 12.0 and 44.4 -> 26.1; Play with the springs 43.5 -> 26.9 and 72.0 -> 46.4. Its meshes built whole a frame: 3 to 17 -> 0 on every posed row. **Not reached:** the uploads did not halve (the chest turned 6.6 -> 6.7 ms, a scrub 13.7 -> 10.8 ms): these rows move most of every mesh they touch, and a mesh that moved by more than a quarter still goes to the card whole; only the IK rows send ranges (the hand's target: 2,923 -> 1,604 KB a frame). **The stand-in did not get faster** (every row within the run-to-run spread of `main`'s build): its posed frames are its nine outline shells, built whole and sent whole (7.8 ms and most of 7.4 MB on the chest row) - not done in this round. No posed row in the world reaches 16.7 ms. Tables: `docs/PERFORMANCE.md`, "Pass 22: a posed mesh follows in place".

<!-- docs/USABILITY.md -->
after: | In Play, in a large world, one mesh of a moving avatar
| While an avatar was posed, scrubbed or played, most of its meshes were built again from nothing on every frame and sent to the card whole; and a normal-mapped mesh that had been posed kept slightly different shading (approximate tangents) until something else rebuilt it. | Round 470 (ADR 0124): a posed mesh follows in place, a small move sends only its ranges (a move of most of a mesh still sends it whole), and when the pose rests the meshes are made exact again (two a frame). Numbers: `docs/PERFORMANCE.md`, "Pass 22". |

<!-- docs/ARCHITECTURE.md -->
after: | `edit_epoch::structure()` / `transforms()` / `Moved`
| `Mesh::update_in_place`: `split_corner` / `split_slots`, `quick_tangents_at`, `Mesh::needs_settle()`, `mesh_partial_share()` (`scene/mesh.cpp`), `GameObject::settle_evaluated`, `Editor::settle_evaluated_step` | Round 470 (ADR 0124, decisions 5 to 7; it extends ADR 0034): a corner whose normal leaves its shared render vertex gets one of its own in place (appended; `moved_from = 0` and a new `build_serial` on that frame) instead of a refusal and a whole build; the quick tangents are worked out around the moved faces only and their vertices join `moved_vertices`; `GpuMesh` parts (several materials) take the vertex ranges. Once the edit epoch has stood for two frames with no step pending and no button held, two evaluated meshes a frame are settled (built whole at the next draw; the bump is made as a move, not a structure change). The share of moved faces up to which normals are worked out around them alone stays 25 % (measured). |

<!-- docs/PERFORMANCE.md -->
append:
## Pass 22: a posed mesh follows in place (round 470, task 0446)

Round 469 left most of a posed avatar's meshes built again from nothing on every frame: a few corners changed which
neighbours they are smoothed with, the move in place was refused, and the whole build ran MikkTSpace. Round 470 gives
such a corner a render vertex of its own in place, works the quick tangents out around the moved faces only, sends
vertex ranges for meshes with several materials too, and makes the posed meshes exact again once the pose rests.

How these were taken: one machine, other sessions holding; each piece started after two minutes without foreign
work, with a watcher sampling every few seconds. The four stand-in pieces ran with no foreign sample. The owner's
avatar ran with the editor's own Linux VM probe at 26 to 35 % of one core on two samples, and the avatar in the world
with the bench's own Unity (it prepares the comparison) for the first minute - both let pass and listed, as in
rounds 468 and 469. Two earlier tries of these pieces were ended by a Blendity started from elsewhere and are not
used.

### The owner's avatar and world (measured locally; `tools/bench_world.py --posing`)

| row (a window on the card, ms a frame) | alone, round 469 | alone, round 470 | in the world, round 469 | in the world, round 470 |
|---|---|---|---|---|
| still: the Scene view | 3.9 | 3.8 | 8.5 | 8.7 |
| orbiting the Scene view | 3.9 | 4.2 | 7.9 | 9.9 |
| the whole avatar moved (its root selected) | 5.5 | 6.0 | 17.1 | 15.9 |
| FK: the chest turned | 38.5 | 28.5 | 61.9 | 47.2 |
| FK: an upper leg turned | 14.4 | 12.3 | 30.5 | 25.3 |
| FK: an upper arm turned | 4.1 | 4.0 | 9.4 | 8.3 |
| Puppet: the hips' handle | 46.8 | 34.2 | 70.7 | 56.0 |
| Puppet: the head's handle | 56.7 | 34.5 | 76.6 | 55.3 |
| Puppet: the left hand's handle | 4.0 | 4.0 | 9.4 | 8.0 |
| scrubbing, Scene view only | 65.0 | 32.9 | 87.1 | 53.3 |
| the clip playing, Scene view only | 69.2 | 33.0 | 94.4 | 52.2 |
| scrubbing, Scene + Game view | 66.9 | 35.0 | 95.0 | 52.3 |
| the clip playing, Scene + Game view | 73.2 | 35.6 | 104.2 | 51.0 |
| Play with the springs | 43.5 | 26.9 | 72.0 | 46.4 |
| IK: the left hand's target moved | 4.1 | 4.2 | 34.8 | 26.6 |
| IK: the left foot's target moved | 25.7 | 12.0 | 44.4 | 26.1 |
| IK: the target moved, Record on | 4.2 | 4.0 | 57.0 | 43.9 |
| IK: the clip playing, Scene view only | 71.2 | 33.6 | 97.8 | 53.1 |

What a frame did (the avatar alone, a frame's mean):

| row | meshes built whole, 469 -> 470 | corners split in place | upload KB, 469 -> 470 | meshes sent in ranges, 469 -> 470 |
|---|---|---|---|---|
| FK: the chest turned | 3.0 -> 0 | 21.6 | 5,099 -> 5,356 | 1.0 -> 1.0 of 11 |
| FK: an upper leg turned | 1.6 -> 0 | 5.3 | 2,676 -> 1,433 | 0.2 -> 1.3 of 4 |
| scrubbing, Scene view only | 6.0 -> 0 | 48.5 | 5,652 -> 5,990 | 0.1 -> 0 of 12.7 |
| the clip playing, Scene + Game view | 7.8 -> 0 | 1.7 | 5,884 -> 6,125 | 0 -> 0 of 12.9 |
| Play with the springs | 16.7 -> 0.1 | 125.8 | 1,804 -> 6,603 | 0 -> 0 of 13 |
| IK: the left hand's target moved | 1.1 -> 0 | 6.6 | 2,923 -> 1,604 | 0.4 -> 2.8 of 3.9 |
| IK: the left foot's target moved | 1.8 -> 0 | 5.9 | 2,654 -> 1,469 | 0.1 -> 1.2 of 3.9 |

- **Reached:** the chest turned at most 30 ms (28.5) and a scrub at most 40 ms (32.9); no mesh built whole and no
  MikkTSpace on a posed frame.
- **Not reached: the uploads at half.** The chest turned 6.6 -> 6.7 ms of uploads, a scrub 13.7 -> 10.8 ms. A chest
  turn, a scrub and a played clip move most of every mesh they touch; above a quarter of a mesh's faces the whole
  normals and tangents passes run (measured: they are not slower than the kept-state ones there) and the mesh goes to
  the card whole. Ranges are sent where a small part of a large mesh moves: the IK rows here, and a finger in the
  `gpu` test (under 1 KB of 892 KB). The bench has no finger row; which meshes of the chest row are over the quarter
  was read from the counts, not checked mesh by mesh.
- **Round 469's Play row was too kind.** Its run on the avatar alone was taken before that round's own fix to the
  upload budget: in Play 3 of 13 moved meshes reached the card a frame (1,804 KB), the others were not drawn. Round
  470's 26.9 ms draws all 13 (6,603 KB), so the gain on that row is larger than 43.5 -> 26.9 reads. The same holds
  for 469's "IK: the clip playing" row (9 of 12.9 sent).
- No posed row in the owner's world reaches 16.7 ms. What is left there is the world's own frame (round 471) and,
  on the avatar, normals, uploads and the shape-key hashes (rounds 472 to 474).

### The stand-in (`blendity_stress --only posing`; in `stress/results/baseline.md`)

Against `main`'s build (round 469 with its upload fix), taken in the same hour. Round 469's saved baseline is not
the comparison: it was taken before the upload fix, when the stand-in's outline shells - a new mesh on every posed
frame - were put off by the budget and not drawn (the chest row sent 15.2 meshes a frame then, 19 now).

| row (a window on the card; ms a frame, the longest frame in brackets) | alone, main | alone, round 470 | in the world, main | in the world, round 470 |
|---|---|---|---|---|
| idle | 1.8 (2) | 1.7 (2) | 3.5 (18) | 3.9 (18) |
| orbiting the Scene view | 3.3 (4) | 3.3 (4) | 10.7 (12) | 10.9 (13) |
| the Rotate gizmo on the chest (FK) | 35.0 (38) | 35.4 (37) | 42.2 (44) | 43.2 (48) |
| the Move gizmo on the left hand's IK target | 9.4 (11) | 9.8 (10) | 16.2 (20) | 20.1 (24) |
| the Move gizmo on the left elbow's pole (the arm bent first) | 10.1 (12) | 9.3 (13) | 20.8 (22) | 23.2 (26) |
| a Puppet handle (the left hand, AutoPosing on) | 50.8 (57) | 50.4 (58) | 64.7 (81) | 69.5 (84) |
| G on the left hand's IK target | 12.2 (14) | 12.3 (14) | 29.0 (31) | 29.4 (32) |
| R on the chest (the Blender keymap) | 41.3 (44) | 41.7 (45) | 60.3 (64) | 60.6 (65) |
| a scrub on the Animation window's ruler, Scene + Game view | 68.7 (89) | 67.8 (89) | 94.5 (128) | 93.8 (124) |
| the clip playing, Scene + Game view | 31.0 (89) | 17.7 (89) | 42.0 (119) | 42.1 (119) |
| a scrub on the Animation window's ruler, Scene view only | 46.6 (60) | 47.2 (63) | 62.5 (92) | 61.7 (88) |
| the clip playing, Scene view only | 17.8 (58) | 18.4 (62) | 18.9 (78) | 18.8 (77) |
| Play with the springs | 82.5 (91) | 82.1 (89) | 106.1 (113) | 106.3 (112) |
| a prop moved with the Move gizmo | - | - | 10.0 (13) | 10.9 (17) |

- **No gain on the stand-in.** The counts of `main` and the round are the same on every row (the chest row: 10
  meshes moved in place, 9 outline shells built whole, 19 meshes and 7,422 KB sent, 0 corners split): the stand-in's
  body meshes were already moved in place, and what it builds whole on a posed frame is its outline shells (7.8 ms on
  the chest row, 19.0 ms in Play). Outline shells moved in place are not done; the owner's avatar has none, so they
  rank by the stand-in alone. Round 474 lists them.
- **Rows that swing.** "The Move gizmo on the left hand's IK target" in the world read 16.2 ms on `main`'s build and
  21.1, 20.1 and (the check) 17.2 ms on the round's, with the same counts and the same upload bytes each time; the
  difference is all in the card's own path (7.7 to 12.3 ms). Round 469's baseline had it at 21.8. "The clip playing,
  Scene + Game view" alone read 31.0 on `main` and 17.7 on the round the same way. The `--check` findings of this
  round are world rows that read 1.2 to 1.3 times another run of the same build; the counts were compared on the
  IK-target row only.

### At rest

Once nothing has changed for two frames, with no step pending and no button held, two posed meshes a frame are
built whole again (vertices shared anew, MikkTSpace). Counted in the tests: the first settle on the second frame at
rest, at most two a frame, tangents and vertex counts a fresh build's afterwards. The cost of those frames was not
timed apart on the avatar: its 13 meshes take seven frames, each with two whole builds in it.
