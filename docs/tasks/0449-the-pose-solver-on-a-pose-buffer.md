# 0449: Puppet Posing's solver on a pose buffer

- **Status:** done, narrower than written: see the deviations at the end (approved by the user, 2026-11-10: "I approve all specs from all passes")
- **Requirements:** Q-04 (interactive editor), Q-06 (responsiveness)
- **Decisions:** ADR 0127
- **Model:** main session (the solver's numerics; goldens)
- **PR:** https://github.com/EmilianoManaloIV/blendity/pull/389
- **Round:** 473, Pass 22's sixth. Measured against the baseline as round 472 left it.

## Goal
Dragging a Puppet handle costs about what turning one bone costs. Today a drag frame restores the start pose into
the scene, rebuilds the joint list and iterates up to 92 times with two scene writes per bone per iteration. Round
468: PUPPET_MS ms a drag frame on the owner's avatar against FK_MS ms for an FK drag (windowed, card); the solver
SOLVER_MS ms over SOLVER_ITERS iterations, the controls around it CONTROLS_MS ms; the press frame PUPPET_PRESS ms and
the drag's longest frame PUPPET_LONGEST ms (the body model, the rig analysis, the pose prior).

## Scope
- In:
  1. `autopose::Rig` (`src/scene/autopose.h/.cpp`): the pose buffer of ADR 0127, built in `Editor::autopose_begin`
     and kept on `autopose_` with the rig registry's structure stamp and the Scene's `serial` (never across an undo:
     round 468's crash).
  2. `autopose::solve(Rig &, goals, pins, body, settings)`: forward kinematics, the Jacobian and the damped solve on
     the buffer; no allocation per iteration (the scratch vectors live in the `Rig`); bone classes read from the
     buffer; the early exit; today's `solve(GameObject &, ...)` kept as a thin wrapper (builds a `Rig`, solves,
     writes) for the other callers (`autopose fk/ik/match`, the tests).
  3. `Editor::autopose_to` (`src/editor/autopose_ui.cpp`): no restore of the start pose into the scene; one write per
     changed joint after the solve; then the FK controls and IK targets as today.
  4. `puppet::pose` (`src/scene/puppet.cpp`): the prior's input and output through the buffer.
  5. `body::hips_of` and the body's weights read once per drag (they don't change during it).
  6. The first frame of a drag: what round 468's cold rows show in "the first Puppet drag, its longest frame"
     (PUPPET_LONGEST ms): the body model on a job when it is over 50 ms (the drag starts with the capsule body and
     takes the weighed one when it lands), the rig analysis from the registry.
  7. Counters: `solver iterations` a frame, `solver exits early`, `scene writes by the solver`.
  8. **The owner's avatar gets its hand handles** (round 468's finding: `humanoid::map` finds its legs, hips, spine
     and head but no arms, so Puppet Posing has no hand handles there): `gait::analyse`'s walk from an upper arm to
     its lower arm takes the child that is named as the next part before the child that is furthest away, and
     support bones (`_sup`, "support") join the twist / roll / helper names it steps over (`next_down`, gait.cpp:19;
     `limb_of`, ik.cpp:616). Cause confirmed on a rig built like the avatar's before the fix is written.
- Out: the pose prior's own cost (an optional model; its ms is reported, not changed); new posing features.

## Likely files
- `src/scene/autopose.h`, `src/scene/autopose.cpp`, `src/scene/puppet.cpp`, `src/scene/body_physics.cpp` (`hips_of`)
- `src/editor/autopose_ui.cpp`, `src/editor/puppet_ui.cpp`
- `tests/test_animation.cpp` (`tests_round473`, the goldens), `stress/stress_posing.inc` (notes)

## Acceptance criteria
1. **Goldens**: for twenty recorded drags (each of the six handles on `create_rig`'s humanoid and on the stand-in;
   pins on and off; a limit hit; a target out of reach; the hips dragged with both feet pinned) the buffer solver
   with the early exit OFF leaves every joint within 1e-4 m and 0.01 degree of today's solver; with it ON within
   1 mm and 0.2 degree, every goal met as well as today (its error no larger), every pin within today's tolerance.
2. **No scene write inside the solve**: a drag frame counts at most one `set_local` per joint (counted through the
   moved list of ADR 0125) and 0 during the iterations; `below_version` of the rig's root moves once a frame.
3. **No allocation per iteration** (a counting allocator in the test around the iterations: 0 after the first).
4. **Behaviour kept**: `perf drag puppet lhand 24 36` moves the hand and keys the pose as before (round 468's test);
   pins, locks, "double-click pins a control", the FK controls following, the IK targets and poles following, undo
   in one step, Record on, the pose prior used when installed (the log line says so) - the existing Puppet and
   AutoPosing tests pass unchanged.
5. **A drag after an undo, after Play, after a bone is renamed or deleted mid-session**: the buffer is rebuilt; no
   stale pointer (the test of round 468's crash, extended to the buffer).
6. **The numbers** (against the baseline round 472 saved): the Puppet rows' drag frame from PUPPET_MS to at most
   PUPPET_TARGET ms on the owner's avatar and from PUPPET_STANDIN to PUPPET_STANDIN_TARGET on the stand-in (card);
   solver iterations a frame from SOLVER_ITERS to at most 40 in the common case; no press or first frame over 100
   ms, none over 50 ms while the body's job runs. `perf_suite.py --check` passes; the baseline is saved again.
7. Robust: a rig with no legs, one bone, 2,000 bones, zero-length bones, NaN goals (refused), a goal on a bone
   outside the armature (refused as today). The full suite on Windows (both builds) and Linux.
8. **Arms with support bones**: on a test rig whose upper and lower arms each carry a `_sup` bone placed further out
   than the next limb bone, `humanoid::map` holds both shoulders, upper arms, lower arms and hands, and the hands'
   Puppet handles show and drag; rigs the analysis already reads (`create_rig`, the stand-in, twist-bone rigs) map as
   before (their maps compared bone for bone). Measured locally: the owner's avatar maps all 17 parts.

## Tests and checks to run
- Unit: `BLENDITY_TEST_FILTER="round 473"`, `"Puppet"`, `"AutoPos"`, group `animation`, then the full suite.
- Stress: `posing` (the Puppet rows, the cold rows, the assistants' FK / IK switch rows), `editor` (the fuzz).

## Responsiveness and cold first use (ADR 0105)
- The body model's build is the one candidate for a job (item 6): its tray label, cancellable by the release; the
  drag never waits for it.

## Risks and edge cases
- Float order: forward kinematics on the buffer multiplies in a different order than `world_rotation()`'s chain -
  the goldens' 1e-4 m covers it; the early exit changes where the iterations stop.
- The solver's damping and step limits were tuned on the scene version: keep the constants; change one thing at a
  time (buffer first with the exit off, goldens green; then the exit).
- The hips' translation (move_hips) and the root's own motion during the drag (a parent animated) - the buffer holds
  the root's matrix at the drag's start; a parent moving mid-drag refreshes it.

## Documentation to update
- CHANGELOG, README count, ARCHITECTURE row (`autopose::Rig`), USABILITY row, ADR 0127, PERFORMANCE "Pass 22".

## Deviations (round 473, as built)
- **No early exit (scope 2, criteria 1 and 6).** Measured before it was kept: the pose depends on the whole schedule
  of iterations (the pull back to the start pose fades over the 32 main iterations and the body follows it): with 24
  main iterations instead of 32 bones end up to 122 mm and 15 degrees away, with 16 up to 323 mm. No exit keeps the
  task's 1 mm, so none is built; "solver iterations a frame at most 40" is not reached (32, or up to 92 with pins).
  Criterion 1's second half (the exit on) falls with it; the first half holds at 0.001 mm.
- **The rig registry does not exist** (round 472 did not build it): the buffer is built in `autopose_begin`, once per
  drag, and again when `autopose::stale` (the structure stamp) or the scene's identity says so.
- **Scope 4, the prior through the buffer:** the prior's own code reads and writes scene bones; with the prior in
  use a frame still puts the start pose into the scene first, then the prior's pose, and the buffer is read from the
  scene before the solve. Without the prior (and for an end the prior does not answer for) no start pose is written.
- **Scope 6, the body model on a job: not built.**
- **Criterion 3, "a counting allocator":** the test compares the bytes the buffer's scratch holds across solves
  (`Rig::scratch_bytes`) instead of counting allocations; the solve itself declares no container.
- **Criterion 1, "twenty recorded drags ... and on the stand-in":** 25 drags on `create_rig`'s humanoid (ten plain,
  fourteen odd ones, one with a null goal), compared against the old solver kept in the code (`solve_reference`)
  rather than against recorded numbers; the worst gap is 0.0013 mm. The stand-in is not compared (no stress note).
- **Two behaviours differ from the old solver, on purpose:** a goal whose target is not a finite number refuses the
  whole solve (nothing moves); a goal with no bone is left out, where the old code still counted it and so cut the
  first pin's weight in the settle iterations.
- **Mid-drag changes:** when the buffer is built again mid-drag (an object made, destroyed or renamed anywhere, an
  undo), the body model is bound to the scene again and dropped if its bones no longer match - the balance is then
  off until the next drag.
- **Criterion 6, the numbers:** the solver's part 1.95 -> 1.11 ms a frame on the stand-in; the frame rows read the
  same; no press or first frame under 100 ms was reached (the first drag's longest frame 110 ms). The owner's avatar
  in the owner's world was not comparable (Unity without a licence on the day): see the fragment's Measured line.
