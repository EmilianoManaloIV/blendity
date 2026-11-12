# 0450: Trails, the assistants, the release frame and Play with the springs

- **Status:** done, much narrower than written: see the deviations at the end (approved by the user, 2026-11-10: "I approve all specs from all passes")
- **Requirements:** Q-04 (interactive editor), Q-06 (responsiveness)
- **Decisions:** none needed (ADR 0105's budgets; ADR 0126's registry is used)
- **Model:** implementer (Sonnet), part by part
- **PR:** https://github.com/EmilianoManaloIV/blendity/pull/393
- **Round:** 474, Pass 22's seventh. Measured against the baseline as round 473 left it.

## Goal
What sits around a posed frame stops costing more than the pose: motion trails and onion skins, the analyses the
Avatar, Rigging and Centre of Mass views redo, AutoPosing's pin lookup, the frame a drag is released on, and Play
with an avatar's springs. Round 468 (the owner's avatar in SideRail unless said; windowed, card):
- an IK drag with Record and trails TRAILS_MS ms a frame against RECORD_MS ms without; onion skins ONION_MS ms;
- the Avatar window open AVATAR_MS ms (its longest frame AVATAR_LONGEST ms: it re-checks twice a second);
- Centre of Mass shown COM_MS ms; the Rigging window's Walk section RIGGING_MS ms;
- AutoPosing on, a Puppet drag: the controls' part CONTROLS_MS ms (three scene walks for the pins every frame);
- the release frame of a drag RELEASE_MS ms (the undo step: settle SETTLE_MS, the scene's clone CLONE_MS, assets
  ASSETS_MS) - budget 100 ms, 50 ms with a job running;
- Play with the springs PLAYMODE_MS ms a frame (SPRING_CHAINS chains a frame; the Game view docks itself);
- the assistants as stalls: Walk Cycle WALK_MS ms, AutoPhysics AUTOPHYSICS_MS ms (budget 50 with its job), the first
  Preview on COLD_PREVIEW ms, the first Record on COLD_RECORD ms, entering Play COLD_PLAY ms (budget 100).

## Scope
- In (each only where the table shows it over its budget or over 0.3 ms a frame - the rest are dropped and listed):
  1. **Trails and onion skins** (`trail_refresh`, motion_trail.cpp:28): sampled on a private copy of the rig's
     transforms (the pose buffer's forward kinematics where only bones are trailed), not by posing the live scene six
     times a frame - no scene counter moves for a trail; spread over frames with a budget; not restarted by a key
     edit that changes one track (only the frames around the edited key).
  2. **Analyses on change, not on a timer**: the Avatar window's `avatar_check` (every 0.5 s today) keyed on what it
     reads (the scene's structure stamp, the meshes' versions, the materials' looks) and run on a job when over 50
     ms; the Centre of Mass's path keyed as `body_refresh` is, its per-frame marker from the kept body; the Rigging
     window's Walk section keyed on the clip's version.
  3. **AutoPosing's pins** (`autopose_pins`, autopose_ui.cpp:130; called every frame by `draw_puppet_handles`): from
     the rig registry, once per structure change.
  4. **The release frame** (`commit_undo`, editor.cpp:2066): `settle_meshes` only for meshes that moved in place
     since their last settle (a list, not every object's `settle()`); the scene's clone - measured first: the copy of
     what the step changed if the clone is over the budget (meshes are shared copy-on-write already: the cost is
     objects and components); `save_dirty_clip_assets` and the other asset writes on a job (the text made on the
     frame, the file written off it - round 413's save job).
  5. **Play with the springs** (`spring::step`, spring_bones.cpp; Play's per-frame scans): the scans for springs,
     locomotion and skins from the registry; the springs' step threaded per chain when over 0.3 ms; the Game view's
     deform shared with the Scene view's (ADR 0124) - what is left is reported.
  6. **Cold first use over budget**: the first Preview on, the first Record on and entering Play are OVER 100 ms in
     round 468's baseline: what is in each (the clip's bindings, the scene's snapshot for Play, the first deform)
     taken off the frame or made smaller until they are under, or their remaining part named.
- Out: new assistant features; the ragdoll's and AutoPhysics's solvers themselves (jobs already).

## Likely files
- `src/editor/motion_trail.cpp`, `src/editor/avatar_ui.cpp`, `src/editor/body_ui.cpp`, `src/editor/autopose_ui.cpp`,
  `src/editor/puppet_ui.cpp`, `src/editor/editor.cpp` (`commit_undo`, `settle_meshes`, Play's enter / leave),
  `src/scene/spring_bones.cpp`, `src/editor/animation_window.cpp`
- `tests/test_animation.cpp`, `tests/test_editor.cpp` (`tests_round474`), `stress/stress_posing.inc`

## Acceptance criteria
1. **Trails**: with trails on, a posed frame counts 0 extra `apply_clip` + solve passes on the live scene (today 6 a
   frame) and the scene's counters don't move for a trail; the trail's points equal today's within 1e-5 m for the
   test rig's clip; a key edited re-samples only the frames its track's neighbours cover.
2. **Analyses**: with the Avatar window open and nothing changing, `avatar_check` runs 0 times over 120 frames (today
   every 0.5 s); it runs once after a change it reads; its findings are today's. The same for the Centre of Mass's
   path and the Walk section.
3. **Pins**: `autopose_pins` walks the scene 0 times a frame while the structure holds; the pins are today's on the
   test rig and the stand-in (hands, feet, locked controls).
4. **Release**: the release frame of every drag row is under 100 ms on the owner's avatar in SideRail and under 50 ms
   while a job runs; undo and redo restore exactly what they do today (the existing undo tests; a clip's file on
   disk after the job lands is byte for byte today's).
5. **Play**: entering Play under 100 ms; Play with the springs from PLAYMODE_MS to at most PLAYMODE_TARGET ms a
   frame on the stand-in (card), the chains' result the same to 1e-5 m after 120 fixed steps.
6. **The numbers** (against the baseline round 473 saved): each setting row within 5 % of its base row, or its
   remaining cost named; no `OVER` among the cold rows; `perf_suite.py --check` passes; the baseline is saved again.
7. The full suite on Windows (both builds) and Linux; ThreadSanitizer clean on the new jobs.

## Tests and checks to run
- Unit: `BLENDITY_TEST_FILTER="round 474"`, groups `animation`, `editor`, then the full suite.
- Stress: `posing` (the settings, the cold rows, the assistants), `responsiveness`, `operations`, `editor`.

## Responsiveness and cold first use (ADR 0105)
- This round's subject. Every job added has a tray label and is cancellable; a frame that starts one stays under
  50 ms.

## Risks and edge cases
- A trail sampled off the live scene must still see what the clip animates besides bones when those are trailed
  (an object's own track): fall back to the live sampling for non-bone trails.
- An asset written on a job while the user quits, opens another scene or undoes again: round 413's rules (the save
  job is waited for; a newer text supersedes an older one).
- An analysis keyed on too little shows stale findings: each key lists what its analysis reads, with a test that
  changes each input.

## Documentation to update
- CHANGELOG, README count, ARCHITECTURE rows, USABILITY rows, PERFORMANCE "Pass 22".

## Deviations (round 474, as built)
- **Measured first, on the stand-in (round 473's baseline)**, as the scope's own rule asks. Over the line and built:
  Play with the springs (82 ms a frame, 20 render meshes built whole a frame), trails (about 6 ms on a drag frame),
  the Puppet controls (2.9 ms). Under the line and dropped: Centre of Mass 0.24 ms, the Avatar window 0.17 ms, the
  Rigging window 0.27 ms (scope 2); the first Preview, the first Record and entering Play at 92, 71 and 96 ms (scope
  6). Not looked at: the release frame (scope 4).
- **Scope 5, Play:** the cause was not in the springs. The scene's own physics step asked every mesh for its world
  bounds each frame with no rigid body present, which evaluated the skinned meshes before the pose was final; it now
  returns when there is no body. The registry scans and the threaded spring step are not built; what the springs'
  4.9 ms is made of is listed in the fragment. Criterion 5's numbers: see the fragment's Measured line.
- **Scope 1, trails:** no private copy of the rig; the trail still poses the live scene to sample (criterion 1's "0
  extra passes on the live scene" is not met). Built: a key edit samples again only from two keys before the first
  changed key to two keys after the last (criterion 1's last clause), compared against a fresh sample for 37 kinds of
  edit.
- **Scope 3, pins:** from a cache keyed on what the walk reads, not from a rig registry (none exists). Criterion 3
  holds while nothing changes; a frame that commits an undo step walks once.
- **Criterion 7's ThreadSanitizer:** no job was added.
- **Measured after** (the stand-in, round 473's build against this one): Play with the springs 82.3 -> 44.5 ms a
  frame alone and 101.5 -> 69.8 ms in its world - still over the row's budget (criterion 5 not met in full). Pins and
  trails: no row shows a change (the trail drag 31.8 -> 28.7 ms, within what a row is good for). The owner's avatar in
  the owner's world, measured locally: Play in a window unchanged (45.5 -> 45.0 ms), headless 67.7 -> 55.0 ms.
- **Left for later** (listed in the pass's write-up): an enabled Rigidbody in the scene's own physics loses the Play
  saving; the springs' own step; trails on a private copy; the release frame; the first Puppet drag's longest frame
  (103 ms); AutoPhysics (58 ms against 50); `rig_ends()` keyed on the scene's serial only.

