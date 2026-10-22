# 0168: strands as mass-weighted XPBD rods

- **Status:** done (round 188)
- **Requirements:** R-11 (secondary motion), Q-02 (what the user sees is what the physics means)
- **Decisions:** ADR 0049 amended (the strand solver: XPBD rods with mass, compliance, damping and air drag)
- **Model:** main session
- **PR:** #133 (stacked on #132)

## Goal
The user: hair physics feels "stiff and jello-like"; they want Blender-hair-dynamics / film feel, research based, for
hair and other dynamic parts.

## Why the old solver felt that way (measured)
The verlet chain blended each tip toward the animated pose by `1 - (1 - stiffness)^(60 h)` every substep: a stiff
spring of about 5 Hz to the pose at hair's 0.15. It pinned the strand to the pose (a 40 cm strand held horizontal
sagged 1.6 cm - the "stiff") and rang against it (14 wiggles of over 2 mm before settling, 15 after a head turn -
the "jello"). Damping was a per-substep blend of the whole velocity, gravity didn't care about mass, and nothing
dragged a strand in still air.

## Design (Macklin et al. 2016, Macklin 2019, Müller et al. 2007, Kugelstadt & Schömer 2016; Blender's hair dynamics)
- **Mass:** each tip a mass from its bone's length and the chain's Density (kg/m; hair 0.01, an ear 0.02, cloth
  0.05, a tail 0.1); the root fixed.
- **Substeps at 240 Hz**, one constraint iteration each (Macklin 2019: small steps beat iterations); velocities
  from positions (PBD).
- **The pull toward the pose and the bend as XPBD compliant constraints:** a slider s is a spring of
  20 Hz x s^1.5 for hair's mass (1 rigid); its share of the way per substep is w / (w + alpha / h^2), so the
  stiffness doesn't depend on the frame rate and a heavier strand is pulled less by the same slider.
- **Damping as a velocity decay relative to where each tip's animation is going** (a move or a turn alike; per
  bone from its effective spring - the fade along the chain and the mass - so free tips aren't overdamped; past
  critical at Damping 1), **air drag always on** (Air, 1/s for hair's density; a heavier chain feels less of it; a
  Wind pulls every chain toward its motion by the chain's Wind, as before).
- **The velocity rescaled** when a frame's substeps are another length than the last frame's (60 fps drifting
  between four and five substeps would otherwise kick the chain).
- **Lengths** as mass-weighted distance constraints, then the exact root-first pass and the body's capsules as
  before; the **bend** applied after the mass-weighted sweeps (they would let the lower bones' weight open the
  curl's upper joints). Strand separation as before.
- **Soft tissue keeps its own spring** (label Soft: the verlet code it was tuned with, its wind included).
- **Old scenes** keep their sliders (which now mean the new spring) and load their label's Density and Air.
- Label defaults: hair stiffness 0.3 and damping 0.5 (was 0.15 / 0.15) - near critical damping for the new spring.

## Acceptance criteria
1. A 40 cm strand of eight bones released horizontally settles within 1 cm in ≤ 1.5 s with at most one overshoot;
   a head turned 90 degrees in 0.3 s with the strand pointing back lags the turn (over 5 degrees) and brings the tip
   within 1 cm of its rest within 1 s of the turn with at most one overshoot.
2. The same motion at 30, 60 and 144 frames a second and with the frame time wavering between 1/60 and 1/59 s (the
   tip 0.25 s after release, mid-fall, within 1 cm); the existing swing tests' frame-rate agreement kept.
3. A curl with Bend 0.5 keeps a share of its curl hanging under its own weight (between 10 % and 97 %); Bend 1
   keeps the joints' angles.
4. Ten times hair's density sags further and settles more slowly; Density and Air are saved.
5. The existing spring-bone, strand, soft tissue and hair rig tests pass (the Bend test measures the joints'
   angles: with no pull and a free root the curl turns about its root until its weight hangs below, as a real one
   does); 100 chains of three bones under 0.5 ms a frame (stress).

## Results
Measured (the tests' prints; the stress rows on a quiet machine):

- A 40 cm strand of eight bones (hair's defaults) released horizontally: settled within 1 cm after 0.75 s with no
  overshoot, its tip hanging 0.177 m below the head; before: 0.80 s with 14 overshoots of over 2 mm, the tip held at
  0.016 m (pinned to the pose).
- A head turned 90 degrees in 0.3 s with the strand pointing back: the tip lagged the turn by up to 9.3 degrees and
  came within 1 cm of its rest 0.73 s after it, one overshoot. (Before, measured with the strand hanging down: 0.88 s,
  15 overshoots.)
- The tip 0.25 s after release, mid-fall: 0.0060 / 0.0006 / 0.0004 m from 60 fps's at 30 / 144 / a frame time
  wavering between 1/60 and 1/59 s; the hanging tip after 4 s: 0.0000 m apart at all three (before 0.0027 at 144).
- Ten times hair's density: the tip sags 0.386 m (0.177 at hair's) and settles after 0.98 s (0.75).
- A curl of 30 degrees a joint with Bend 0.5 hanging from its root for 4 s keeps 89 % of it; Bend 1 keeps its joints
  at 40.0 degrees.
- 100 spring chains of 3 bones, ms a frame (stress physics): 0.366 plain, 0.454 with collision, 0.559 with separation too; was 0.548, 0.485, 0.730.
- 100 spring chains of 10 bones, ms a frame (stress physics): 1.445 plain, 1.752 with collision, 2.276 with separation too; was 2.020, 2.175, 2.862.
- 100 spring chains of AutoPhysics clip (100 frames), ms a frame (stress physics): solved plain, passes with collision, steps with separation too; was solved, passes, steps.
- 100 spring chains of a jump, ms a frame (stress physics): yes plain, 5 with collision, 4230 with separation too; was yes, 5, 4230.
- 100 spring chains of a run, ms a frame (stress physics): yes plain, 5 with collision, 8040 with separation too; was yes, 5, 8040.

## Tests
- "strands feel like film hair (task 0168): ..." and "strands: the same hang at 30, 60 and 144 frames a second within
  1 cm ..." and "strands: a heavier chain ..." (tests/test_physics.cpp).
- The round 153 Bend test, its measure changed to the joints' angles (tests/test_physics.cpp).
