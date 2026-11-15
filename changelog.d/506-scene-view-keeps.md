## 2026-11-14 (round 506): the Scene view's per-frame work kept while the scene holds (task 0490)

**Asked:** "Do an HCI and Performance sweep across the program to test, verify, and optimize components." (Pass 25 Speed:
the sweep's top row - the Scene view of 50,000 cubes on the card, 33-34 ms a frame with only the camera moving, budget
16.7 ms - the CPU doing for every object, every frame, work whose result did not change.)

**Added (task 0490):**
- **`scene_views`, a named-only stress section** (`blendity_stress --only scene_views [--quick]`; also in `tools/perf_suite.py`'s
  default sections): 50,000 and 10,000 cubes on the card, six actions (the camera orbiting in Shaded and in Solid, a Scene view and
  a Game view both drawn every frame, one object moved every frame, the pointer over a panel with the view still, the Hierarchy
  shown with the pointer over it), each run off / on / off again in one process (a warm cache alone can't flatter "on"), with
  where a frame went (the editor's profile and the new timers) and what it did a frame (counts, and each keep's frames kept /
  missed), and a last table with each keep turned off alone. A machine with no card prints a note and no rows.
- **`src/core/view_perf.h`**: plain counters and `ScopedTimer`s at the places below, `view_perf::keeps` (the switch that turns
  every keep off at once; `BLENDITY_VIEW_KEEPS=0` starts a run with them off), `disabled` (one keep off alone) and
  `stale_for_test` (the unit tests skip a keep's invalidation to prove their pictures can tell a stale keep).
- **`src/editor/view_keeps.h`**: what the keeps hold between frames - the Editor's `keeps_` member, freed with it.

**Changed (each keep behind `view_perf::keeps`; ADR 0115's change counter, and Blender's own split of syncing objects from
redrawing a view - its draw manager fingerprints a sync with a counter, a redraw with a moved camera only updates the view):**
1. **The card's per-instance buffers** (`gpu_raster.cpp`): `set_instances` advances a generation; the instance rows and the toon /
   baked / lilToon tables are written once a generation, straight into the buffer, per buffer with the buffer's own id (it is
   shared by every view's slot); the lilToon tables are not written while no instance is lilToon; the validation loops and the
   "which passes does the array need" scans run when the array or a mesh / texture changes.
2. **The lights, probes and sun** `make_lighting` gathered (three scene walks): kept while the scene, its serial and what else they
   are made from hold - and while the change counter moved only by transforms (a cube dragged) and the lights' and probes' own
   world matrices did not; one entry for the views' lights, one for the path tracer's and the bakes'. The headlight, exposure and
   view transform are made every frame.
3. **`instances_key`**: not hashed again when the draw items are the kept list under the generation the slot's array was stored for.
4. **`collect_items`**: `collect_items_ref` returns the kept list itself, a reference into the cache (13 MB a copy for 50,000
   cubes); `collect_items` is a copy of it; a copy only where `render_deferred` writes realtime-GI lightmaps into the items.
5. **`gpu_view_unsupported`**: kept per list on the items' generation and the materials' textures' generation, without a heap
   allocation for each item; a Scene view and a Game view drawn together each keep their own.
6. **Solid shading's item list** (`solid_items_ref`): kept on the scene hash with its own generation, so the card's keeps work there
   too, and the lights its environment is made from with it; not kept in Edit Mode, Weight Paint or Sculpt (their items carry
   per-frame state).
7. **The Hierarchy's flattened rows** (`draw_hierarchy`): kept between frames and made again when the scene, its structure counter
   (an object added, deleted, re-parented, renamed, re-ordered, activated), the folds or the search text change, and on the check
   interval; under a search the names are part of the key (a name typed in the Inspector lists its row the same frame).

**Numbers (one timed run on a quiet machine, 2026-11-14 07:55; `docs/perf/sweep-506-scene-views.md` has every table).** 50,000
cubes on the card, keeps off -> on (off again):

| what the user does | before | after | | budget 16.7 ms |
|---|---|---|---|---|
| the windowed app, the camera orbiting, Shaded | 32.6 - 39.8 ms | 9.6 - 10.2 ms | 3.3x | ok (was OVER; Pass 19's target was 25) |
| the stress row: the camera orbiting, Shaded | 31.4 (35.7) | 13.3 | 2.7x | ok |
| the camera orbiting, Solid | 53.6 (52.7) | 18.0 | 3.0x | OVER by 1.3 ms |
| a Scene view and a Game view both drawn | 121.4 (121.3) | 77.2 | 1.6x | OVER |
| one object moved every frame | 243.3 (242.6) | 206.1 | 1.2x | OVER (not slower: the criterion) |
| the Hierarchy shown, the pointer over it | 4.5 (4.5) | 3.5 | 1.3x | ok |
| 10,000 cubes: orbit Shaded / Solid / both views / a drag | 6.9 / 9.0 / 25.1 / 46.2 | 3.7 / 3.4 / 18.4 / 39.6 | 1.9x / 2.6x / 1.5x / 1.2x | ok / ok / OVER / OVER |

Each keep turned off alone over the Shaded orbit at 50,000 cubes is worth: the lights 7.6 ms, the card's buffers 5.6, the refusal
check 3.4, the item copy 1.2, the instance key 1.0. A camera-only frame now writes 0 instance rows (50,000), 0 tables (3), makes 0
validations (1), 0 item hashes (1), 0 refusal walks (2), 0 item copies (1) and walks the lights once in 8 frames (every frame).

**Taken out again (the rule: a keep that does not move the frame by 0.5 ms does not stay):** the casters' bounds kept for
`update_shadow_map(frame_only)` (-0.1 ms: the card's path does not fit the shadow map in these frames at all) and the every-8th
walks each in a frame of their own (-0.4 ms median, the longest frame 29.9 ms with it against 31.0 without: noise).

**No regression:** `editor_render`, `gpu_viewport`, `world_like` in the same window - no row newly OVER and none slower than the baseline but one, which is faster than round
504's timed run (the working scene's drag in Solid on the card: 12.35 in the old baseline, 19.68 in round 504's run, 14.85
now). `posing --quick` with the keeps off and on reads the same row for row.

**Checked:** unit tests (`tests_round506*`, and the test engineer's `tests_round506_engineer*`: two editors in lockstep, one with
the keeps on and the scene checked every 8th frame as a window does, one with them off and checked every frame, compared pixel for
pixel after every frame - Scene view, Game view, Hierarchy): an object moved / added / deleted / duplicated / hidden / re-parented,
a material, a texture, a light's every setting, lights and probes dragged with the Move gizmo by real mouse events (released and
cancelled), a parent turned, a clip scrubbed, Play mode with a falling body, Save / New / Open, the world, the shading modes, the
Scene-lighting button, Lights Gamma, Edit / Sculpt / Paint entered and left, the Camera Preview, layouts, 5,000 objects and a
200-level nest in the Hierarchy, rows dragged and dropped, NaN and empty meshes, undo and redo of each; on the card with two views
and with 10,000 instances; a camera-only frame does none of the work (counts); each keep that can go stale is caught by a test
that skips its invalidation. `editor` / `render` / `gpu` groups: see the PR.

**Fixed after the test engineer's run:** with a Hierarchy search on, a name typed in the Inspector that starts to match was listed
up to 8 frames late (the name changes with every key, the structure counter only when the rename is committed): the names are now
folded into the kept rows' key while a search is on.

**Fixed in review (second round):**
- The instance key's record follows a kept array when it is confirmed for a list that was regathered with the same contents (a
  light edited, Solid and back): before, every later frame hashed all the items again. Tested: 0 item hashes after the first
  frame following a light moved, a light's intensity, Solid and Shaded again.
- Solid's kept lights are keyed on Unity's Lights Gamma too (it is not in the scene hash). Tested: `unity lightsgamma on / off` in
  Solid and Shaded, picture equal with the keeps off.
- The lights keep's "the change counter moved only by transforms" branch is tested on the CPU path: a light dragged by transform
  writes alone, its parent dragged, a probe dragged, a light's intensity by the console's `set`, a cube dragged (0 light walks), and
  with only the placement comparison skipped (`kStaleTrackedBit`) the dragged light is drawn where it was. A constraint moving a
  light is not tested (none was easy to set up).
- The Game view, the Camera Preview and every other game gather have a draw-item list of their own (3 variants x tangents; the
  keeps' list slots follow), so with a Scene view, a Game view and the selected camera's preview drawn every frame no list is
  gathered, no refusal check made and no item hashed after the first frame (tested; the instance key was kept 12
  times in 4 frames, missed 0).
- The Hierarchy's fold key is a sum of a mixed (splitmix64) id, so {5, 7} is not {4, 8}.

**Left, with its numbers:** two views drawn every frame still rewrite the shared card buffer (150,000 rows a frame with a Scene
view, a Game view and a Camera Preview at 50,000 cubes: 77 ms) - a buffer per view slot is the fix, about 43 MB a slot at 50,000;
a drag of one object among 50,000 makes the item list, the instance array and the static overlays whole (206 ms with three views
drawn; collect 80 ms, the instance gather 43 ms) - per-object updates are the fix; Solid at 50,000 is 1.3 ms over (its instance
array is gathered by rows each frame).
