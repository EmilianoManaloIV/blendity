## 2026-11-10 (round 468): Posing, scrubbing and playback, measured

**Asked:** Pass 22's first round. The owner reported a low frame rate while using IK handles, Puppet Posing, playback
and scrubbing on their avatar, and wasn't sure the world around it matters. Nothing measured any of it. **This round
measures: nothing is faster and no picture changes. What changes in behaviour: a crash is fixed; and while a `perf drag`
script plays (a developer's console command) the system's mouse and keys are replaced by the script's, in the main window
alone, and a press is never a double click.**

**Fixed**
- **A Puppet drag after an undo could crash** (found by the new stress section, on the card): AutoPosing keeps the body
  model between drags, keyed by its values; an undo puts a copy of the scene in place - the same values, other objects -
  and the next drag's solver walked the kept model's bones, which the next edit had freed (an access violation in
  `body::hips_of`; without the crash, a pose solved from a stale body). The Centre of Mass's body had the same flaw and
  read its armature every frame. `body::rebind` points a kept body at the open scene's bones again; nothing is weighed
  again.

**Added (task 0444)**
- **The frame's parts while posing**: `PoseCounts` (`src/scene/pose_counts.h`; the skin's cache key and deform, shape keys,
  evaluations, outline shells, `solve_constraints` with its FK / IK / VRChat parts and name walks, `apply_clip`, the
  AutoPose solver, the springs) and new `FrameProfile` fields (Record, anim apply, trails, AutoPosing, the Puppet controls,
  the rig overlay, mesh uploads, `commit_undo` and its parts), shown in a "Posing" block of the Profiler and printed by
  `perf status`, `perf drag` and `--windowed-check`. `MeshCounts` also counts the in-place moves of a render mesh that were
  refused (and of them those refused because a vertex's corners no longer share a normal) beside the render meshes and
  tangents built whole.
- **`perf drag gizmo | puppet | key | ruler | at`, `perf clip`, `perf pick`**: a drag played as real mouse and key events
  through the editor's own input path, logged with the press, first-move, mean, longest and release frames and the parts of a
  drag frame. `perf frames N` now also logs the posing parts and counts over the frames it stepped. `perf pick` and
  `perf clip` find the rig above the active object, below it (an imported character's root) or anywhere in the scene.
- **`benchscene avatarlike | avatarworld`** (and the `quick` names; `src/editor/bench_avatar.cpp`): a stand-in avatar of
  Shinano's size built from Blendity's own shapes, deterministic - `create_rig`'s humanoid with its four IK limbs grown to
  292 bones (fingers, toes, eyes and jaw, twist and helper bones, hair, skirt, tail, ears, ribbons), 15 skinned meshes and
  1 plain one of 139,912 triangles (at most 4 weights a vertex; vertex groups only for the bones that move a mesh), 60
  spring chains each with a PhysBone's settings kept beside it, 28 VRChat rotation constraints that name their sources and
  move nothing at rest, 2 to 4 toon materials a mesh (a third with normal maps), outlines on 12 meshes, 70 shape keys and a
  clip that keys every bone; built in about 0.2 s. The quick one: 61 bones, 5 + 1 meshes, 19,440 triangles, 9 chains,
  4 constraints, 10 shape keys. `avatarworld` stands it in `worldlike`.
- **Stress section `posing`** (named-only: it runs minutes; `stress/stress_posing.inc`): the avatar alone, the world alone and
  the avatar in the world with its lighting baked, each with the views on the CPU and on the card (when there is one): idle,
  orbiting, the Rotate gizmo on a bone IK doesn't turn, the Move gizmo on an IK target and a pole, a Puppet handle, G and R,
  a scrub, the clip playing, Play with the springs; the press, first-move and release frames; cold first use; fourteen settings
  (Record, Preview off, the Game view, Other Views Follow Drags, trails / onion skins / zoetrope, Centre of Mass, the Avatar
  and Rigging windows, the Inspector, Realtime GI Automatic / Manual, Live Link with a client, surface snapping, F12 Live
  Preview, a path-traced view); the assistants as stall rows; "does an untouched window settle" (0 frames, 0 views, 0 constraint
  moves wanted), "is the pose a frame late" and "does the world matter". The last cell is `ok` / `OVER` / `-`, or `NO VIEW` for
  a drag, scrub or play row whose frames drew no view (a layout that hides the Scene view measures nothing).
- **`tools/bench_world.py --posing`** (real content, on a copy of the project it is given - the project itself is only read,
  and its files are compared before and after): the same drags, scrub, clip and Play on an imported avatar as it comes in
  and again with IK handles (`rig autoik`), headless (each row's press, first-move, mean, longest and release frames and the
  frame's parts, biggest first) and in a real window on the card (`perf drag ... loop` under `--windowed-check`).
  `--avatar-package` / `--avatar-prefab` / `--avatar-at` stand an avatar in an opened world, `--camera` sets the Scene view
  (the same view of the avatar alone and in the world), `--prop` moves an object of the world, `--settings` adds the settings
  matrix, `--frames`, `--no-pipeline`, `--self-test`. Before anything is timed the avatar's prefab is imported once so
  Unity numbers its parts. `tools/dev/chain.sh` runs the three setups with `POSING=1`.
- A Learn lesson, "Where a Posing Frame Goes".

**Changed**
- The skin pose change and IK timing rows moved from `vgroups` to `posing` (under a header with ms columns the comparison
  reads); `vgroups` keeps its robustness rows. The `animation` section's rows gained a `budget` column (`ok` / `OVER` by
  their own 0.2 / 0.5 / 0.2 ms targets: the 201-track row reads `OVER` at about 0.8 ms).
- `tools/perf_suite.py`: `posing` joins the default sections, an OVER finding names its first time column, a row already OVER in
  the baseline is "still over" and doesn't fail `--check`, and `--save-baseline --sections` keeps the other sections.

**Checked:** the unit suites - with Blender's libraries 24,259 checks (of them four card-against-CPU picture checks
of the `gpu` group fail in a worktree build of the libraries, the same four at the same lines on main's own build there;
a Live Link check that asserts a tick time failed once while two suites ran side by side and passed alone), dependency-free
23,345 and Linux 23,351, both with 0 failed; the audits (the coverage map, the sweep's command list, the HCI and Unity checks);
`tools/test_perf_suite.py` and `tools/bench_world.py --self-test`. Headless screenshots of the starter scene, the world
stand-in, a posed rig and the posed avatar, from main's build and from this round's, on the CPU and on the card: no pixel
differs outside what the clock draws (the Statistics overlay, the status bar, a toast, the Project window's file list).
`blendity_stress --only posing` (the card's rows need the build with Blender's libraries; the dependency-free build has
no card path and leaves them out with a note), `animation` and `vgroups`. The timed runs (the baseline, the check twice, the owner's avatar, world and both, a cold
run, main's build against this one) ran one at a time under a watcher for other builds and editors on the machine;
`docs/PERFORMANCE.md` ("Pass 22") says what it saw during each. The baseline is saved in `stress/results/baseline.md`.
**Not met:** `perf_suite.py --check` against that baseline fails on one or two rows (below); the settings rows on the
owner's avatar inside the owner's world were not measured (over two minutes a row; the stand-in's are).

**Found** (the numbers and the ranking: `docs/PERFORMANCE.md`, "Pass 22"; the owner's projects measured locally)
- No posing row reaches 16.7 ms. The stand-in: a gizmo, G / R or Puppet drag 48 to 63 ms a frame alone on the card, 65
  to 80 ms in the world; a scrub 55 to 123 ms; Play with the springs 87 to 108 ms. The owner's avatar in a window: 34 to
  68 ms alone, 58 to 91 ms in the owner's world, a scrub or a playing clip 76 to 192 ms.
- Where it goes, biggest first: render meshes rebuilt whole with their tangents because the in-place move is refused
  (up to 36 ms; smooth-by-angle splits that flip with the pose at a few sliver corners), every moved mesh sent to the
  card whole (6 to 17 ms, 4.8 to 5.9 MB a frame), skinning of all 13 meshes whatever moved (8.7 ms; a rigid move of the
  root included), normals (2 to 11 ms), skin keys (78 to 156 hashes a frame, 5 to 9 ms; 52 while only a prop moves),
  Record (11 ms). The world adds 23 to 35 ms to a posed frame. Constraints, the IK solver and shape keys are cheap.
- The pose is drawn a frame late: up to 16.5 mm on an IK-target drag.
- The owner's avatar has no arms in its humanoid map: its arm rows are refused.
- Two "the clip playing" rows have two states (22 or 32 ms: the view drawn on 12 or on 20 of 30 frames) on a quiet
  machine, which is what fails the check. Cause not found; round 472.
- The counters do not show: main's build against this one, 0 findings on `editor_render` and `world_like`; two
  single-frame rows of `responsiveness` moved (3.2 to 4.5 ms, 0.9 to 2.3 ms), far inside their budgets.
