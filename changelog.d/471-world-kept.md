## 2026-11-11 (round 471): The world stays kept while a character moves - the card's instances and see-through lists by object

**Asked:** Pass 22's fourth round (task 0447, ADR 0125): posing an avatar that stands in a world should cost what
posing it alone costs, plus drawing the world. The spec read the cause as the scene hash, the draw items and the
instance array made again for every object.

**Measured first:** the card's own parts were added to the posing rows (`card shadow maps`, `card g-buffer`, `card
lighting`, `card copies and wait`, `card render call`, `baked corners`, `see-through triangles`, `presenting`). In
the owner's world most of the world's share of a posed frame is the card drawing the world (6 to 10 ms of a 10 to
15 ms card path); the instance array made again is 2 to 3 ms and the see-through triangles about 2 ms; draw items
and scene hashes are under 1.5 ms together. So the round is narrower than the spec (its deviations and ADR 0125 say
what was left out and why).

**Changed (task 0447, ADR 0125)**
- **The card's instance array is kept by object** (`Editor::InstanceRows`): an object whose placement, mesh,
  materials and lightmap pages are what they were takes last frame's instances; only what changed is made again -
  in baked and see-through scenes too, where nothing was kept before.
- **See-through triangles are kept by object**, and while no list changed and the camera rests the sorted corners
  are last frame's. A camera move sorts them again, as before.
- A change still gathers once and then settles into the whole-array keep of round 458.
- Verify mode gathers such a frame whole as well and compares the two.
- The Profiler's posing parts name the card's passes; counts "instance rows kept / built" and "see-through lists
  kept".

**Not in this round:** the list of what moved, the scene hash and the draw items by object, the lighting key for a
move outside the bake, clips not "volatile" (measured small or belonging to round 472); the card's own passes for
the world.

**Checked:** 27,003 unit checks without the libraries and 27,011 on Linux, 0 failed; 29,080 with the libraries, where the 4 `gpu` picture checks that fail on `main` fail the same way (`test_gpu.cpp:887` twice, `:2226`, `:4886`); the audits pass. The round's own tests (`tests_round471_gpu`, 8 on the card): the picture drawn by rows equals a whole gather after 15 kinds of change and after a lightmap bake; verify mode finds 0 stale rows over 60 frames; see-through lists are copied while the camera rests; ids ageing off the card drop the rows; a whole gather at least every 64 calls; 5,000 objects with one moving make at most 2 rows a frame. The test-engineer found one fault (a mesh sent again dropped every kept row, so a posed avatar gathered whole on most frames) - fixed; the code review approved, its findings fixed. Not covered by a test: a texture or corner block freed under a row still being copied; the reuse of the last frame's sorted corners is checked by picture comparison only. An earlier run of the suites beside other passes' builds showed Live Link, `hci` and one `gpu` check failing; none repeated on the quiet machine.

**Measured:** on a quiet machine, round 470's build against this round's, one after the other. The owner's avatar in the owner's world (measured locally, a window on the card, ms a frame): the Scene view still 8.7 -> 5.7; the whole avatar moved 15.9 -> 11.1; the chest turned 47.2 -> 38.3; an upper leg 25.3 -> 23.3; a hand's IK target 26.6 -> 23.4; a foot's 26.1 -> 22.2; a prop of the world moved 10.0 -> 8.6; scrubbing 53.3 -> 50.0; the clip playing 52.2 -> 49.8; Play with the springs 46.4 -> 44.3; the Puppet handles within 1 ms. The stand-in in its world (`blendity_stress --only perf`, the baseline taken again by both builds): the Move gizmo on a hand's IK target 21.0 -> 14.1, on the elbow's pole 23.5 -> 16.8, G on the target 30.3 -> 21.8, R on the chest 61.5 -> 49.9, a prop moved 10.7 -> 7.7; alone, where there is no world to keep, every row within 1 ms except the clip playing in both views (17.7 -> 27.5, a row whose longest frame is 88 ms in both). **Not reached:** the spec's "what posing alone costs plus drawing the world" - the chest turned is 28.5 ms alone and 38.3 in the world; what is left is the card drawing the world and presenting, which this round only names. **How far to trust a row:** the same build checked against its own baseline minutes later flagged 5 rows 20 to 35 % slower, and the check against round 470's baseline flagged 2 (leaving Play 5.2 -> 7.5 ms, the clip playing with a Live Link client 23.0 -> 29.9, a row the self-check flagged too); the bench's headless rows moved 6 to 8 ms either way between the two runs' settings (orbiting 8.6 -> 15.1 as imported, 16.6 -> 8.4 with IK handles), so the tables use the windowed rows, as round 470's did. `stress/results/baseline.md` is this round's.

<!-- docs/USABILITY.md -->
after: | In Play, in a large world, one mesh of a moving avatar
| Posing an avatar inside a baked world made the card's instances for every object of the world again on every frame, and sorted every see-through triangle again though the camera had not moved. | Round 471 (ADR 0125): the instances and the see-through lists are kept by object; only what changed is made again. Numbers: `docs/PERFORMANCE.md`, "Pass 22". |

<!-- docs/ARCHITECTURE.md -->
after: | `edit_epoch::structure()` / `transforms()` / `Moved`
| `Editor::InstanceRows`, `instance_item_sum`, `instances_global_key`, `card_ids_gen_` (`editor/gpu_views.cpp`, `editor.h`) | Round 471 (ADR 0125; beside round 458's `InstanceKeep`): a view slot's instance array kept by drawn item. `gpu_view_draw`'s gather goes by rows when the last frame's stand (same card, shade mode, textures' generation, no texture let go since, a whole gather within 64 calls): an item whose `instance_item_sum` and mesh id are its row's takes a copy of the row's instances and, while its see-through job's key holds, of its see-through triangles; when no list was made again and the layout is the same the sorted `SeeCorner`s are the last frame's. A field `raster_instance` or the baked path reads must be in `instance_item_sum`; verify mode gathers whole as well and compares. |

<!-- docs/PERFORMANCE.md -->
append:
## Pass 22: the world stays kept while a character moves (round 471, task 0447)

Measured first: the card's own parts now have names in the posing rows. In the owner's world a posed frame's card
path is 10 to 15 ms: the render call 6 to 10 (g-buffer 2.5 to 3.7, lighting 1.2 to 2, copies and wait 1 to 3.6), the
instance array made again 2 to 3, the see-through triangles about 2; the scene hash and the draw items are under
1.5 ms together. So round 471 keeps the instance array and the see-through lists by object and leaves the hash and
the draw items as they are (ADR 0125).

Round 470's build against round 471's, one after the other on a quiet machine. The owner's avatar in the owner's
world is measured locally (a window on the card, ms a frame):

| row | round 470 | round 471 |
|---|---|---|
| still: the Scene view | 8.7 | 5.7 |
| orbiting the Scene view | 9.9 | 9.5 |
| a prop of the world moved | 10.0 | 8.6 |
| the whole avatar moved | 15.9 | 11.1 |
| FK: the chest turned | 47.2 | 38.3 |
| FK: an upper leg turned | 25.3 | 23.3 |
| IK: the left hand's target moved | 26.6 | 23.4 |
| IK: the left foot's target moved | 26.1 | 22.2 |
| scrubbing, Scene view only | 53.3 | 50.0 |
| the clip playing, Scene + Game view | 51.0 | 48.9 |
| Play with the springs | 46.4 | 44.3 |

The stand-in in its world (`blendity_stress --only perf`, ms a frame, the longest frame in brackets):

| row | round 470 | round 471 |
|---|---|---|
| the Move gizmo on the left hand's IK target | 21.0 (23) | 14.1 (19) |
| the Move gizmo on the left elbow's pole | 23.5 (26) | 16.8 (19) |
| G on the left hand's IK target | 30.3 (34) | 21.8 (25) |
| R on the chest | 61.5 (67) | 49.9 (55) |
| the Rotate gizmo on the chest (FK) | 43.5 (52) | 41.9 (49) |
| a Puppet handle | 70.6 (88) | 61.6 (77) |
| a prop moved with the Move gizmo | 10.7 (13) | 7.7 (8) |
| idle | 3.5 (18) | 4.7 (20) |

Not reached: posing in the world at what posing alone costs (the chest: 28.5 ms alone in round 470, 38.3 in the
world now). What is left of the world's share is the card drawing it and presenting. A row is good to about a
fifth: the same build against its own baseline minutes later flagged 5 rows 20 to 35 % slower, and the bench's
headless rows moved 6 to 8 ms either way between its two settings, which is why the windowed rows are the ones
tabled.
