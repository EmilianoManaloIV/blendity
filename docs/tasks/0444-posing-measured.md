# 0444: Posing, scrubbing and playback, measured (round 468)

- **Status:** done, with criterion 9 met in part - see Deviations (approved by the owner on 2026-11-09, as written, the five points at the end included:
  "Approve as written", relayed by the hub session)
- **Requirements:** Q-04 (the editor stays interactive), Q-06 (responsiveness), R-13 (the animation tools), C-04 (the
  user's files untouched)
- **Decisions:** none needed (a measurement; Pass 22's plan, approved 2026-11-09). Five points are the owner's to say
  yes or no to: "For your decision" at the end.
- **Model:** implementer (Sonnet) for the code and the tools, then test-engineer, then code-reviewer; the worker session
  runs the timed runs on a quiet machine and writes the tables
- **PR:** (link when opened)

## Goal
The owner reported (2026-11-09) a low frame rate while using IK handles, Puppet Posing, playback and scrubbing, and
the animation assistants on their avatar, and was not sure whether the world around the avatar matters. Nothing
measures any of it today: no stress row and no `tools/bench_world.py` step drags a bone, an IK target or a Puppet
handle, scrubs or plays a clip on a skinned avatar, or puts an avatar in a world.

Afterwards one command says what a frame of posing costs and where it goes, on a committed stand-in and on the
owner's Shinano and SideRail, so every fix round of Pass 22 (469-475) is specified against measured numbers and shows
its gain in the same table. **This round changes no behaviour and makes nothing faster.**

## The plan's code references, checked against main (75c615b2)
Every file:line in the plan lands on the function it names on main (`render_view.cpp:433 / 956`, `gpu_views.cpp:1153 /
1673 / 1704`, `lighting.cpp:131`, `scene.cpp:1815`, `ik.cpp:52 / 361`, `animation_window.cpp:304`, `puppet.cpp:74`,
`editor.cpp:632 / 2066`, `skin.cpp:408`, `animation.cpp:121 / 615`, `modal_transform.cpp:241`, `jobs.h:34`,
`scene.h:1735`, `perf_suite.py:31`, `tests/test_common.h:659`, `tests/test_animation.cpp:5682`). What the plan says
about the code is wrong or incomplete in these places:

1. **Skinning and normals are already threaded.** The skin loop runs on `JobSystem::parallel_for` (`skin.cpp:293`,
   1,024 vertices a chunk) and the in-place normals do too, only around the moved faces (`mesh.cpp:793`). On one thread
   are: the cache key (`SkinModifier::modifier_dependency_hash`, `skin.cpp:230`: an allocation, a walk over every bone,
   two matrices and a name hashed per bone, on every `evaluated_mesh` call), the name map and matrices built before
   each deform (`skin.cpp:266-289`), `apply_shape_keys` (`shape_keys.cpp:69`), the position copy, `moved_faces`,
   `write_corners`, the in-place tangents (`mesh.cpp:833-865`) and the bounds. R2's "Threads" item may gain less than
   the plan assumes; this round times each part apart.
2. **A second reason a moved mesh goes to the card whole.** The plan names meshes with several materials
   (`gpu_views.cpp:1155`). A mesh whose tangents exist (a normal-mapped material) has every tangent written again on
   each move, which clears `moved_from` (`mesh.cpp:868`): it goes up whole too, one material or several.
3. **`BL_ZONE` gives no number in a normal build.** It is a Tracy macro, nothing without `BLENDITY_TRACY=1` (ADR
   0112). The frame's parts have to be `FrameProfile` fields timed by `ProfileScope`, and counters in the scene layer
   as `mesh_counts()` is (the skin deform and `solve_constraints` sit below the editor, where there is no
   `FrameProfile`). This round adds both, with a `BL_ZONE` beside each for Tracy.
4. **The keyframe row that misses its target is in `animation`, not `vgroups`** (`stress_main.cpp:4572`: 0.7951 ms
   against 0.2). The comparison can't flag it for two reasons: its last cell is the target, not `OVER`, and
   `first_regression` ignores any growth under 1 ms.
5. **`rig humanoid` is 17 bones and 4 IK chains** (`create_rig`, `ik.cpp:502`). A stand-in sized like Shinano needs a
   builder of its own.
6. **Puppet Posing's solve is up to 92 iterations a frame, not 32:** 32, plus up to 60 "settle" iterations whenever
   anything is pinned - and the feet are pinned by default (`autopose.cpp:166`).
7. **The constraint walks are behind live counters** (`IKConstraint::live`, `RigControl::live`,
   `VRCConstraintComponent::live`, `ik.cpp:416-430`): Shinano as imported has VRChat constraints only, so the IK and FK
   walks start when IK handles are added (`rig autoik`, Add IK) - the owner's "IK handles" case. Both are measured.
8. **Springs in Play are `SpringBone` components** (`spring::step`, `spring_bones.cpp:1035`); an import keeps each
   PhysBone's settings as one on its root (`unity_scene.cpp:816-825`). The Play rows say how many chains moved.
9. **The Avatar window re-checks the avatar twice a second, not every frame** (`avatar_ui.cpp:125`): it would show as
   a hitch every half second, so its row reports the longest frame too.
10. **Possibly a pose a frame late:** `solve_constraints` runs before the windows draw (`editor.cpp:631`), and the
    gizmo moves its object inside the Scene view's update, after it. Read from the code, an IK target dragged by the
    gizmo is solved in the next frame. A row measures it (below).
11. **Not checked line by line** (the plan's leads for rounds 472 and 474: the Animation window's rows and Summary
    row, Curves, the Sequencer, the `gait::analyse` callers, Centre of Mass): each window's own time is already
    recorded (`FrameProfile::ms_window`, round 436) and decides.
12. **`tools/bench_world.py` has no `--frames` and no `--windowed-check` option of its own** (the worker's brief said
    so): it passes `--windowed-check` to the exe with 120 frames. This round adds `--frames`.

## Scope
- In:
  1. **The frame's parts while posing** - times and counts, nested in the frame as the existing ones are:
     - scene layer, `PoseCounts pose_counts()` (atomics, as `MeshCounts`): the skin's cache key
       (`modifier_dependency_hash`), the skin deform and its setup before the loop, shape keys (`shape_key_hash`,
       `apply_shape_keys`), `evaluated_mesh` evaluations, outline shells built (`Outline::shell`), `solve_constraints`
       with its FK / IK / VRChat parts and the name lookups that walked (`resolve`), `apply_clip`, `autopose::solve`
       (iterations too), `spring::step`; `MeshCounts` gains the in-place move's normals and tangents apart;
     - editor, `FrameProfile` (`ProfileScope`): Record (`anim_record_changes`, and how many times it serialised the
       clip), `anim_apply`, the trails' refresh, AutoPosing's frame (`autopose_to`), the Puppet controls' drawing and
       hit test, the rig overlay, `commit_undo` with its parts (settling meshes, the scene's clone, the assets saved);
       the frame's `PoseCounts` kept per frame as `mesh_last_` is.
     - Already there and reused: scene hashes, draw items, the instance array, overlays, the lighting key, uploads to
       the card (whole and partial), each window's own time.
     - The Profiler window shows them in a "Posing" block; `perf status` and `--windowed-check` print them.
  2. **`perf drag`: a drag played as real mouse and key events** (`src/editor/perf_script.cpp`, new), so the gizmo,
     Puppet, G / R, Record and the Animation window's ruler run as they do under a hand:
     - `perf drag gizmo <x|y|z|rx|ry|rz|free> <pixels> [frames N] [part <part>] [mods <ctrl|shift|alt>+...]` - the active
       object's Move or Rotate gizmo handle, pressed where the gizmo's own hit test has it (no pixel constant in a
       script); `part` selects that humanoid part of the active rig first. A bone IK turns is refused, as it is by
       hand.
     - `perf drag puppet <lhand|rhand|lfoot|rfoot|head|hips> <dx> <dy> [frames N]`
     - `perf drag key <g|r> <dx> <dy> [frames N]` - G or R with the mouse over the active object, moved, then a click.
     - `perf drag ruler <from frame> <to frame> [frames N]` - a scrub on the Animation window's ruler.
     - `perf drag at <x0> <y0> <x1> <y1> [frames N]`, `perf drag stop`.
     - A headless editor plays it at once (a move to the press point, the press, N move frames - 30 unless given - the
       release, one frame after) and logs one line: the press frame, the first move, the drag frames' mean and longest,
       the release frame and the frame after, in ms, then a line of the drag frames' parts. In a window, or with the
       word `queue`, it is queued and played one step a frame; `loop` goes back and forth without releasing until
       `perf drag stop` (for `--windowed-check`, which measures a fixed number of frames). While a script plays, the
       system's own mouse and key events are dropped, so a hand on the mouse can't disturb a measurement; a real Esc
       stops it and releases the button.
     - `perf clip [frames N]`: a clip on the active rig for the scrub and play rows - every bone under its armature
       keyed with a small turn at three frames - so they don't depend on a rig's names or on the Walk Cycle assistant.
       `perf pick <part>`: selects a humanoid part of the active rig (the lookup Puppet Posing uses). A part is named
       as `humanoid::kPartNames` has it, spaces and case ignored, or by the handles' short words (`lhand`, `rhand`,
       `lfoot`, `rfoot`, `head`, `hips`).
  3. **A stand-in sized like Shinano** (`src/editor/bench_avatar.cpp`, new; built from Blendity's own primitives so it
     can be committed, as `benchscene worldlike` is): `benchscene avatarlike` - `create_rig`'s humanoid with its four
     IK limbs (targets and poles), grown to about 283 bones (fingers, toes, eyes and jaw, hair, skirt, tail, ears and
     accessory chains), 15 skinned meshes and 1 plain one of about 140,000 triangles in all, each skinned to the bones
     around it (at most 4 a vertex, its vertex groups the bones that move it), 2 to 4 toon materials a mesh, some with
     normal maps, outlines, shape keys (60 on the face, a few on), about 61 spring chains with a PhysBone's settings
     kept beside each, 28 VRChat rotation constraints that name their sources, and a clip that keys every bone.
     `benchscene avatarworld`: `worldlike` with that avatar standing in it. `avatarlikequick` / `avatarworldquick`: a
     small one of each for the unit suite and `--quick`. Deterministic: two builds give the same scene text.
  4. **Stress section `posing`** (`stress/stress_main.cpp`; named-only like `responsiveness` - minutes, and CI's quick
     run has 900 s for everything). Three setups: the avatar alone, the world alone, the avatar in the world with its
     lighting baked. Rows, by real events through `step_frame_headless` (the template is `editor_render`'s vertex
     drag), each with the views on the CPU and on the card when there is one:
     - base rows: idle; orbiting; the Rotate gizmo on a bone no IK turns (FK: the chest); the Move gizmo on an IK
       target, and on a pole; a Puppet handle; G on the IK target and R on the chest; a scrub; the clip playing; Play
       with the springs;
     - the press frame, the first move and the release frame of each drag, against ADR 0105 (100 ms for a one-frame
       operation, 50 ms while a job runs);
     - cold first use, in a process that has not posed yet: the first posed frame after the avatar is built, the first
       Puppet drag (its body model, rig analysis and pose prior), the first Preview on and Record on, entering and
       leaving Play;
     - the settings, one row each on the row they can change (the IK-target drag, or the clip playing): Record on;
       Preview off; the Game view beside the Scene view, and Other Views Follow Drags on; trails, onion skins and the
       zoetrope (with Record, and while scrubbing); Centre of Mass shown; the Avatar window open; the Inspector on the
       Armature and on a skinned mesh; the Rigging window's Walk section; Realtime GI with Lighting Updates Auto and
       Manual; Live Link with a client; surface snapping; an F12 Live Preview; a path-traced view open;
     - each assistant's command as a stall row (longest frame, mean, total, frames, the tray's label): Walk Cycle,
       retarget, the ragdoll, AutoPhysics, the FK/IK switch and Match;
     - "does an untouched window settle": over 120 frames with no input after a pose, the frames that asked for
       another, the views drawn, the constraint solves that reported a move - at the origin and with the rig 5,000 m
       from it (0 expected each);
     - "is the pose a frame late": after each frame of an IK-target drag, how far the chain's tip is from its target
       (0: the picture shows the solved pose in the same frame);
     - "does the world matter": the drag frame in the world, less the same drag alone, beside the world's own orbit
       frame.
     Columns: the row (its setup and device in its name, so no two rows share one), `ms / frame` first, the longest
     frame, then the parts, then `ok` / `OVER` / `-`. The budget: 16.7 ms mean for a drag, scrub or play row with the
     views on the card; 100 ms for a press, first-move or release frame; none for the CPU rows and the cold rows
     beyond ADR 0105's.
  5. **The suite reads it** (`tools/perf_suite.py`, `tools/test_perf_suite.py`):
     - `posing` joins `DEFAULT_SECTIONS`;
     - the `vgroups` skin and IK timing rows (a pose change on 50k vertices; a humanoid's four chains; the same among
       20k objects) are written by `posing` under a header with ms columns, where the comparison reads them; `vgroups`
       keeps its robustness rows and says where the others went;
     - the `animation` section's rows end in `ok` / `OVER` by their own targets (0.2 / 0.5 / 0.2 ms): the 201-track
       row reads `OVER` today;
     - an `OVER` finding names the row's first time column and its value (today it prints "longest frame" and whatever
       is in the second cell);
     - a row that is `OVER` in the baseline and still `OVER` is listed as "still over" and doesn't fail `--check`; a
       row newly `OVER`, or one that regressed, does;
     - `--save-baseline` with `--sections` replaces only those sections in the baseline file (today it would drop
       every section not run).
  6. **Real content** (`tools/bench_world.py`): `--posing` adds the steps (pose, scrub, play; `perf drag`'s lines and
     the parts read from the log) and the windowed rows (`perf drag ... loop`, `anim play`, Play); `--avatar-package`
     and `--avatar-prefab` are the avatar-in-world mode (the package imported into the project, the scene opened, the
     prefab placed); `--frames N`; `--self-test` checks the log parsing without running Blendity. With `--posing` or an
     avatar given, the tool copies `--project` into `<out>/project` first and runs there: the project it was given is
     never written. Three runs: Shinano alone, SideRail alone (the base cost: idle, orbiting, a prop moved), Shinano in
     SideRail. Shinano is measured as imported (FK on an upper arm; its VRChat constraints only) and again with IK
     handles added (`rig autoik`: the IK-target and pole rows); the settings rows that have a console switch run on
     Shinano in SideRail. `tools/dev/chain.sh` gets `POSING=1` (the posing sections through `perf_suite.py`, then the
     three runs; off unless asked).
  7. **The baseline and the write-up:** `stress/results/baseline.md` with the `posing` tables, saved with nothing else
     building or running; `docs/PERFORMANCE.md` "Pass 22" with the stand-in's table, the three real-content tables
     (marked "measured locally"), for each setup the parts ranked by their share of a drag frame and of a played frame,
     and the answer to "does the world matter".
  8. A short Learn section, "Where a posing frame goes": the Profiler's Posing block and `perf drag`, a Try-it on
     `benchscene avatarlikequick`, two questions. (Round 475 writes the full lesson.)
- Out (not in this task): any change to how a pose is solved, skinned, drawn, hashed, kept or recorded (rounds
  469-474); skinning on the GPU (not in this pass); pictures or files of the owner's two projects in the repository.

## Likely files
- `src/scene/scene.h`, `src/scene/pose_counts.cpp` (new), `skin.cpp`, `shape_keys.cpp`, `scene.cpp` (`evaluated_mesh`,
  `Outline::shell`), `ik.cpp`, `animation.cpp`, `autopose.cpp`, `spring_bones.cpp`, `mesh.h` / `mesh.cpp`
- `src/editor/editor.h` (`FrameProfile`), `editor.cpp` (`frame`, `frame_body`, `commit_undo`, `perf_status_text`,
  `windowed_check`, `bench_scene`), `perf_script.cpp` (new), `bench_avatar.cpp` (new), `editor_commands.cpp` (`perf`,
  `benchscene`), `scene_view.cpp` (a handle's press point, taken out of `gizmo_update`'s hit test unchanged),
  `animation_window.cpp`, `motion_trail.cpp`, `autopose_ui.cpp`, `puppet_ui.cpp`, `scene_overlays.cpp`, `panels.cpp`
  (the Profiler), `lessons.cpp`
- `stress/stress_main.cpp` (`test_posing`, `test_animation_stress`, `test_vgroups_stress`, the named-only list),
  `stress/sweep_commands.inc` (if a test's example changes it)
- `tools/perf_suite.py`, `tools/test_perf_suite.py`, `tools/bench_world.py`, `tools/dev/chain.sh`, `tools/dev/README.md`
- `tests/test_animation.cpp` and `tests/test_editor.cpp` (`tests_round468`)

## Acceptance criteria
1. `benchscene avatarlike` builds what the scope says: 270 to 300 bones, 15 skinned meshes and 1 plain one, 133,000 to
   147,000 triangles, 4 IK chains whose targets and poles exist, at least 60 spring chains, 28 VRChat constraints whose
   sources all resolve, outlines, shape keys, a clip with a track for every bone; turning an upper arm moves vertices
   of the meshes skinned to it and none of a mesh that isn't. Two builds give the same scene text. `avatarlikequick`
   and `avatarworldquick` build in the unit suite; `avatarworld` holds `worldlike`'s 600 parts and the avatar.
2. `perf drag gizmo rz 60 part chest` with the Rotate tool turns that bone as the same mouse events sent one by one do
   (the same rotation to 1e-5), in one undo step; `perf drag gizmo x 80` on an IK target moves it and the hand follows;
   `perf drag puppet lhand 24 36` moves the hand and keys the pose, as the test at `tests/test_animation.cpp:5682`
   does by hand; `perf drag key g ...` and `key r ...` leave the object moved or turned and no modal running;
   `perf drag ruler 0 20` leaves the playhead on frame 20 with Preview on.
3. Refusals leave nothing pressed: no active object, a handle that isn't on screen, a bone IK turns, no Animation
   window for `ruler`, an unknown part - each says why, and the next real click works. `queue` plays one step a frame
   and the gizmo is dragging between; real mouse events sent meanwhile change nothing; Esc stops it with the button
   released; `loop` runs until `perf drag stop`.
4. The logged line holds the press, first-move, mean, longest, release and after-frame times and the frame count, and
   the parts line follows it; `tools/bench_world.py --self-test` reads both.
5. The counts mean what they say: across a frame of a scene with no rig no posing count moves; one bone turned and one
   frame drawn counts at least one deform, with its time, for each mesh skinned to that armature and none for a mesh
   skinned to another; `solve_constraints` counts one solve a call and one lookup walk for each name it had to search;
   a Record drag frame counts its clip serialisations; a release counts one `commit_undo` with its parts. The
   Profiler's Posing block shows the rows while a rig is posed. No test asserts a time (ADR 0056).
6. Nothing else changes: the existing unit tests pass unchanged; headless screenshots of the starter scene, of
   `benchscene worldlikequick` and of a posed humanoid rig (the Statistics overlay off: it prints frame times) are the
   same pixels on main's build and on this round's; with no script playing, an event reaches the editor as before.
7. `blendity_stress --only posing` writes every row of the scope for the three setups (the card's rows when there is
   a card, and a note when there isn't), each named for its setup and device, `ms / frame` first, a verdict last;
   `blendity_stress --quick` without `--only` doesn't run it.
8. `tools/test_perf_suite.py` covers: a `posing` row's first time column compared; "still over" against newly `OVER`
   under `--check`; the finding's wording; `--save-baseline --sections posing` keeping the other sections. The
   `animation` section's 201-track row ends in `OVER`; the `vgroups` skin and IK rows are under a header whose time
   column the comparison reads.
9. `stress/results/baseline.md` holds the `posing` tables from a run with nothing else building or running (the PR
   says when), and `python tools/perf_suite.py --sections posing,animation --check` right after it exits 0.
10. `tools/bench_world.py` runs the three setups from scratch copies; the two projects it was given are byte for byte
    as before (file count, sizes and modification times compared before and after). `docs/PERFORMANCE.md` "Pass 22"
    holds the stand-in's table, the three real-content tables marked "measured locally", the ranked parts for each
    setup and the answer to "does the world matter".
11. `python tools/test_coverage_map.py --check` and `python tools/sweep_commands.py --check` pass; the full unit suite
    passes on Windows (both builds) and Linux; CI's four jobs pass.

## Tests and checks to run
- Unit: `BLENDITY_TEST_FILTER="perf drag"`, `--group animation`, `--group editor` in both Windows builds first, then
  the full suite (Windows both builds and Linux). `python tools/test_perf_suite.py`, `python tools/bench_world.py
  --self-test`.
- Stress: `--only posing`, `--only animation`, `--only vgroups`; `--only editor_render`, `world_like` and
  `responsiveness` on main's build and this round's in one session (the timers must not show: within the
  comparison's 20 %).
- Timed runs (the worker, after the review and the chain): the other worker asked to hold, no build, test or stress
  process of another tree running; then the baseline, the three `bench_world.py` runs, and one run from a copy of
  the exe under a new name with the shader cache emptied (the driver keeps its pipeline cache per exe path).

## Responsiveness and cold first use (ADR 0105)
- This round adds timers, counters and tools; no frame may get slower. The timers sit around calls that run a few
  hundred times a frame at most, never inside a per-vertex or per-object loop; the A/B above is the check.
- Cold first use is this round's subject, not a cost it adds: the rows above measure the first posed frame after an
  import, the first Puppet drag, the first Preview and Record and entering Play, in a process that hasn't posed
  before, and once with the card's pipeline cache cold. Each is reported against 50 / 100 ms.
- The stand-in's build (`benchscene avatarlike`) is a bench command and may take over 100 ms; it says so in its log
  line with its time.

## Risks and edge cases
- **Headless is not a window.** With the views on the card, a headless frame copies the picture back, which a window
  doesn't. The stress verdicts are therefore stricter than the goal; the goal (16.7 ms) is judged on the windowed rows
  of `bench_world.py`. Both are reported, with the copy-back in its own column.
- **A scripted drag takes the mouse.** A real Esc always stops it; it never starts from a file or a scene, only from
  the console; `--windowed-check` ends it when the check ends.
- **The pose prior** is an optional download: the Puppet rows say whether it ran and its ms; without it they measure
  IK alone.
- **Playback reads the real clock**, so a played frame's pose depends on how long the last one took: the play rows
  report frames and ms, not a pose; tests that need a pose set the clock (`set_clock_for_test`).
- **Machine drift** is about 20 % between sessions: before / after numbers of later rounds are A/B binaries in one
  session; the committed baseline is for regressions.
- **Counters from worker threads** (evaluations on workers, ADR 0107; the path tracer's snapshot): relaxed atomics, a
  count may land a frame late, never torn.
- **The stand-in is not Shinano:** its weights are smooth blends around tubes, its materials Blendity's toon, not
  lilToon. It is the regression row; the real-content tables are the judgement.
- The dependency-free build, GCC's explicit lambda types around `get<T>()`, shallow nesting (guard clauses), an SPDX
  header in each new file.

## Documentation to update
- CHANGELOG (round 468; a fragment in `changelog.d/` if task 0443 is on main by then), README (the check count),
  ARCHITECTURE rows (`pose_counts`, `perf drag` and the script's place in `frame`, `benchscene avatarlike`, stress
  `posing`), PERFORMANCE ("Pass 22: the baseline"), `tools/dev/README.md` (`POSING=1`), `tools/README.md`
  (`bench_world.py`'s new options), the Learn section, this spec marked done with its deviations.

## For your decision (say no to any; the rest stands)
1. **The Profiler gains a "Posing" block.** The plan said nothing you see changes in this round; this is the one
   visible addition, so you can read in the app what the tables report.
2. **`perf_suite.py --check` stops failing on a row that was already `OVER` in the baseline** ("still over" is
   listed; a new `OVER` or a regression still fails). Otherwise `--check` is red for the whole pass, from this round
   until the last fix, and can't tell a new regression from the known ones. (With a no, criterion 9 reads: it lists
   the `OVER` rows and nothing else.)
3. **The skin and IK timing rows move from `vgroups` to `posing`** (their numbers stay comparable; `vgroups` keeps its
   robustness rows).
4. **The stress verdicts judge headless frames** (stricter than a window); the 60 fps goal is judged on the windowed
   rows on Shinano and SideRail.
5. **Three developer sub-verbs on `perf`** (`drag`, `clip`, `pick`) rather than new top-level commands; while a
   scripted drag plays, the real mouse is ignored and Esc stops it.

## Deviations (round 468, as built)
- **Criterion 9, in part.** The baseline was taken on a machine verified quiet (a watcher for other builds, editors
  and the Linux VM; `docs/PERFORMANCE.md`, "Pass 22", lists each timed piece and what was seen). The check against it
  does **not** exit 0: two "the clip playing" rows read 22 ms or 32 ms from run to run on a quiet machine (the Scene
  view drawn on 12 or on 20 of 30 frames). The rows were not loosened; the cause goes to round 472.
- **The settings rows on the owner's avatar in the owner's world were not measured** (over two minutes a row in a
  window; the first run was cut at 54 minutes). The stand-in's settings table stands for them.
- **`tools/bench_world.py` gained `--avatar-at`, `--camera`, `--prop`, `--prepare-only` and `--reuse-copy`**: the
  scratch copies are prepared once outside the timed window (the import starts Unity to number the avatar's parts),
  then measured on, and reused only for the same avatar, scene and build. `--out` and the project must be wholly
  apart (the review found a project under `--out` could be deleted). "Unchanged" (criterion 10) is checked by file
  count, total size and newest change time, not file by file. Opening the owner's scene still starts Unity once;
  that piece says so.
- **Counts added beyond the scope's list.** The first runs on the owner's avatar showed that a posed frame's biggest
  single cost was not in the list: an in-place move of a render mesh is refused for some meshes on every frame, the
  render mesh is then built whole and its tangents with MikkTSpace. `MeshCounts` gained `in_place_refused` and
  `in_place_split`, and the posing lists the items `render meshes built whole`, `tangents built whole`, `mesh copies`
  (ms) and `render meshes moved in place`, `moves in place refused`, `refused for a new split` (counts).
- **`pose_counts_this_thread()`**: the calling thread's own tallies, for the unit tests that call the scene layer
  directly (the process-wide exact counts were flaky when a worker of an earlier test's editor counted at the same
  moment). The editor's frame, the stress section and the bench read the process-wide ones.
- **`perf pick` and `perf clip` look further than the rig above the active object**: then below it (an imported
  character's root sits two levels above its armature), then anywhere in the scene, whatever its humanoid map says;
  `perf clip` selects the rig's root (the Animation window works on the clip of the object at or above the active one)
  and warns when the clip got no keys.
- **`perf drag key r` needs the Blender keymap** (in the default Unity keymap R is the Scale tool; the script refuses
  and says so). `perf drag ruler` turns Preview on. `loop` without `queue` is queued even headless.
- **Scrub and play rows run in a layout that shows the Scene view**: the built-in Animation workspace (Scene + Game view
  above the Animation window) and a dock string with the Scene view alone; in the default layout `window Animation`
  opens as a tab over the Scene view and nothing is drawn. A drag, scrub or play row whose frames drew no view reads
  `NO VIEW` instead of a verdict. Play and clip rows pace their frames to 16.7 ms: a clip plays by the real clock.
- **The card's rows need the build with Blender's libraries** (the dependency-free build has no card for the views):
  the baseline is that build's. The ragdoll's stall row needs it too (Jolt).
- **The pole row** bends the arm first and moves the pole up: a pole turns nothing while its limb is straight.
  **"Is the pose a frame late"** solves the constraints once more after each frame and reports how far the hand then
  moves (the distance to the target alone could not tell lateness from an unreachable target).
- **`tools/bench_world.py`**: `--no-pipeline` added (the posing runs don't need the import / bake / export pipeline);
  the rows are the ones the owner's avatar allows - its humanoid map has no arms, so the upper-arm and left-hand rows
  are refused there and say so; the Puppet rows use the hips', head's and a foot's handles with `tool view` (a gizmo
  on the selected bone takes the press). The report ranks the leaf parts only: where a part is paid depends on who
  asks first (a posed mesh is deformed inside whichever of the overlays, the hashes or the gather first reads it).
- **Criterion 6's screenshots** are compared outside what the clock drives: the Statistics overlay, the status bar, a
  toast, the Project window's file list. The posed avatar is drawn at rest and wholly on the card before it is posed in
  both builds' runs: a skinned mesh moved in place keeps approximate tangents (ADR 0034) and is never settled, so its
  picture on the card depends on whether it went up posed or was moved after - an existing behaviour, found here.
- **The stand-in** has 292 bones, 15 + 1 meshes, 139,912 triangles, 60 spring chains, 28 constraints, 70 shape keys,
  outlines on 12 meshes; `create_rig` has no shoulder bones, so the Shoulder parts don't resolve on it.
- **One fix to the product, found by the section itself.** The stress section's card rows crashed in the build with
  Blender's libraries (an access violation in `body::hips_of`, under a Puppet drag): AutoPosing keeps the body model
  between drags keyed by value, an undo swaps the scene for a copy with the same values, and the next drag's solver
  walked the kept model's bones - objects of the swapped-out scene, freed by the edit in between. Not this round's
  code (main does the same: a Puppet drag, Ctrl+Z, any edit, a Puppet drag), but the baseline could not be taken
  without it. `body::rebind` points a kept body at the open scene's bones (AutoPosing's, and the Centre of Mass's,
  which read its `root` every frame); nothing is weighed again. A unit test pins it.
- **A script's press is never a double click** (the test-engineer's finding): the same drag played again within the
  double-click interval pressed the same pixel, the Scene view framed the selection and the camera flew through the
  measurement. While a script plays, `process_events` doesn't pair presses; a real click right after one doesn't pair
  with it either.
- **`perf drag key g | r` is refused when the active object's origin is outside the Scene view** (in front of the camera
  but over another window): before, only an origin behind the camera was.
- **A row "drew a view" when it rendered one or showed a kept picture** (the stress verdict's `NO VIEW`): with the
  views on the CPU a played clip renders a new picture on some frames only.
- **From the code review.** A script plays in the main window alone: `perf drag` is refused while another editor
  window is open, and one opened under a playing script stops it as a real Esc does (the script's events go to the
  main window, and what it aims at - the gizmo's probe, the Puppet controls' hover, the Animation window's ruler - is
  kept once for the editor, so a press could land at another window's coordinates). The ruler has an aim check like
  the gizmo's and the Puppet controls': drawn in the frame before the press, the mouse on it, nothing over it. A
  key's release passes through a playing script (a key down when it began must not stay down), a modifier held then
  is let go, and a lost focus is held back until the script has ended. "Refused for a new split" counts only a
  corner whose normal disagrees, not an index that doesn't fit.
- **The cold rows run first in the section** (before the three setups): "the first posed frame after the avatar is
  built" is then a process's first posed frame, which is what a first drag after starting the editor feels; the
  later blocks are an editor's first Puppet drag, Preview, Record and Play. Each Puppet drag also has a "longest
  frame" row: the drag itself begins once the mouse has moved a few pixels, so the body model, the rig analysis and
  the pose prior land in the first move frame of a short drag and in a later frame of a long one.
