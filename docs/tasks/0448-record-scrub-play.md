# 0448: Record, scrubbing, playback and the Animation window; the pose solved for the frame it is drawn in

- **Status:** done, narrower than written: see the deviations at the end (approved by the user, 2026-11-10: "I approve all specs from all passes")
- **Requirements:** Q-04 (interactive editor), Q-06 (responsiveness)
- **Decisions:** ADR 0126
- **Model:** implementer (Sonnet) for Record, the bindings, the window's rows and the modal; the worker for the second solve and the review's fixes
- **PR:** https://github.com/EmilianoManaloIV/blendity/pull/388
- **Round:** 472, Pass 22's fifth. Measured against the baseline as round 471 left it.

## Goal
Scrubbing and playing a clip, and posing with Record on, cost what the pose costs: the clip is not serialised, searched
or re-indexed on every frame, the Animation window does not redo its rows while only the playhead moves, and a dragged
IK target's limb is drawn where the solver puts it for that frame. Round 468's numbers (the owner's avatar, windowed on
the card; the stand-in in brackets): scrubbing SCRUB_MS ms a frame, of it the clip CLIP_MS ms and the Animation window
ANIMWIN_MS ms; the clip playing PLAY_MS ms; an IK drag with Record on RECORD_MS ms against DRAG_MS ms without
(RECORD_SER clip serialisations a frame); the pose a frame late by up to LATE_MM mm.

## Scope
- In (each only where round 468's parts show it over 0.3 ms a frame - the table decides which are dropped):
  1. **The solve follows the tools** (ADR 0126 decision 1): `solve_constraints` again after a view's tools ran
     (`scene_tool_updates`, the modal transform, the Inspector's drags) and before the view gathers, when a
     transform under a rig moved since the frame's solve.
  2. **The rig registry** (ADR 0126 decisions 2-3) for `solve_constraints`, `ik_drives`, `ik_driven_bones`,
     `pole_joint`, `fk_control_of`, `humanoid::fk_control` (`autopose_pins` and Play's scans are task 0450's).
  3. **Record**: the clip's `before` text taken once per undo step, not on every `mark_changed`
     (`anim_record_changes`, animation_window.cpp:304; `note_clip_edit` already does it for edits); keys written only
     for the objects that moved since the last record (from the scene's moved list, ADR 0125, or `transform_version`s
     kept per recorded object); `AnimationClip::track()` through an index (path, property, axis) -> track instead of
     the linear `find`; `anim_snapshot_shown` for the moved objects only.
  4. **Playback and scrubbing** (`apply_clip`, animation.cpp:615): the bindings from tracks to objects and properties
     kept per (clip version, the scene's structure stamp) - no path map rebuilt per call; blend-shape tracks bound by
     index; one `set_local` per object as now.
  5. **The Animation window**: its rows (the track tree, each row's keys laid out) kept until the clip's version, the
     window's size / scroll / zoom or the selection change; the playhead and the scrubbed frame drawn over them.
  6. **G / R / S idle when the mouse is still** (`transform_update`, modal_transform.cpp:241): no re-apply and no
     redraw request on a frame without a mouse move or a typed number.
- Out: the Curve editor and the Sequencer beyond what shares `apply_clip` (their rows are measured in round 468's
  `animation` section and listed if OVER); trails and onion skins (task 0450).

## Likely files
- `src/editor/editor.cpp` (`frame_body`), `src/editor/scene_view.cpp` (`draw_scene_view`, `scene_tool_updates`),
  `src/editor/modal_transform.cpp`, `src/scene/ik.cpp` (`solve_constraints`, `resolve`, `ik_drives`),
  `src/scene/scene.h/.cpp` (`Scene::rigs()`, the structure stamp), `src/editor/rig_controls.cpp`,
  `src/scene/humanoid.cpp`
- `src/editor/animation_window.cpp` (`anim_record_changes`, `anim_apply`, the rows), `src/scene/animation.h/.cpp`
  (`AnimationClip::track`, `find`, `apply_clip`)
- `tests/test_animation.cpp`, `tests/test_editor.cpp` (`tests_round472`), `stress/stress_posing.inc` ("is the pose a
  frame late" becomes a verdict), `stress/stress_main.cpp` (`animation`: the 201-track row)

## Acceptance criteria
1. **Not late.** During an IK-target drag (the gizmo, G, a Puppet handle that moves a target) solving the constraints
   once more after any frame moves no bone (under 0.01 mm): stress "is the pose a frame late" reads 0 on every frame;
   a headless screenshot mid-drag shows the hand at the target. One undo step, the same final pose as today.
2. **The registry**: `solve_constraints` on the stand-in counts 0 name walks after the first solve and no `for_each`;
   each lookup of item 2 returns what today's walk returns on rigs with duplicate names, nested rigs, a control
   outside its rig, a missing target, a renamed bone (a test compares both implementations object for object).
   Invalidation: rename, delete, add, duplicate, reparent, undo / redo, a scene opened, Play entered and left.
3. **Record**: a 60-frame IK drag with Record on counts 1 clip serialisation (the undo step's) instead of
   RECORD_SER a frame; the keys written are today's (the clip's text after the drag is byte for byte the same as
   before this round on the test rig and the stand-in); undo restores the clip.
4. **Playback / scrub**: `apply_clip` counts 0 path-map builds while the clip and the structure hold; the posed
   values are today's to the bit for the stand-in's clip (every bone, blend shapes, a light's intensity, a material
   field); a track naming a missing object, two objects with one name, a renamed object: as today.
5. **The Animation window**: while only the playhead moves, its row layout is built 0 times (counted); its pixels
   are the same as before for a scrub and a played clip (headless screenshots).
6. **G / R / S**: a held modal with a still mouse asks for no redraw and deforms nothing (counted over 60 frames).
7. **The numbers** (against the baseline round 471 saved): the `animation` section's 201-track row from OVER to
   `ok` (0.8 -> at most 0.2 ms); on the stand-in and the owner's avatar: the Record drag within 5 % of the plain
   drag; scrubbing from SCRUB_MS to at most SCRUB_TARGET ms; the clip playing from PLAY_MS to at most PLAY_TARGET
   ms a frame (windowed, card). `perf_suite.py --check` passes; the baseline is saved again.
8. The full suite on Windows (both builds) and Linux.

## Tests and checks to run
- Unit: `BLENDITY_TEST_FILTER="round 472"`, groups `animation`, `editor`, then the full suite.
- Stress: `posing`, `animation`, `vgroups`, `editor` (the fuzz). Timed: as the rounds before.

## Responsiveness and cold first use (ADR 0105)
- The registry's first build and the clip's first binding are one-off walks: measured as cold rows ("the first
  Preview on", "the first Record on", "entering Play" - OVER 100 ms in round 468's baseline: COLD_PREVIEW /
  COLD_RECORD / COLD_PLAY ms); they must not grow, and the round says what is in them.

## Risks and edge cases
- A second solve must not run when a tool is mid-way through a multi-step write (the modal transform writes all
  selected objects, then the solve); and never during Play's fixed steps (scripts own the order there).
- Record's "what moved" must include objects moved BY the solve (IK-driven bones are not keyed - `ik_drives` - but
  FK controls and targets are).
- The clip edited elsewhere (the Curve editor, a paste, an undo) while the window keeps its rows: the clip's version
  covers it; a clip swapped under the same name too.

## Documentation to update
- CHANGELOG, README count, ARCHITECTURE rows (`Scene::rigs()`, the frame's order), USABILITY row (the limb no longer
  trails the handle), ADR 0126, PERFORMANCE "Pass 22".

## Deviations (round 472, as built)
- **The placeholders in the Goal** were never filled from round 468; round 471's runs give them (the owner's avatar
  in the owner's world, 275 bones keyed): Record 10.5 ms a frame with 1 clip serialisation a frame; on a scrub the
  clip 0.64 ms and its apply 0.77 ms, the Animation window 1.5 to 1.9 ms; the constraints 0.2 ms. The late pose is the stand-in's
  number: up to 16.5 mm on 30 of 35 frames of a drag.
- **Scope 2, the rig registry: not built.** The spec's own rule drops it: the constraints cost 0.2 ms a frame
  (46 to 92 name walks), under 0.3 ms. Criterion 2 falls with it. ADR 0126's decisions 2 to 4 stay written, not built.
- **Scope 1, the second solve: built for the Scene view's tools only** (`Editor::solve_after_tools`, right after
  `scene_tool_updates`): the gizmo, G / R / S and the Puppet handles run there. An Inspector drag is drawn in
  another window and still shows a frame late. No screenshot test mid-drag: the test solves once more after every
  frame of a drag and compares every bone.
- **Scope 3, Record:** as written, except: shape weights and tracks that are not transforms are still looked at
  whole on every call (their keys are few); `ik_drives` is still a walk, now asked only for a bone that moved.
  "Byte for byte as before this round" is tested against a reference worked out by hand and against the same edits
  recorded with a whole look before each one - the old code cannot run beside the new.
- **Scope 4, the bindings:** kept on the clip per root (32 roots at most, the least recently used goes), valid
  while the clip's `layout`, its track count, the structure stamp (less the applies' own bumps: the review found two
  animated roots rebuilding each other's on every frame) and a signature of every object under the root hold (the
  tests found a name written with no stamp bump was missed when only the bound objects were checked).
- **Scope 5, the window:** the rows and each row's keys are kept; the x positions are still worked out per draw.
- **Criterion 7's `animation` 201-track row and the scrub / play targets:** see the fragment's Measured line.
