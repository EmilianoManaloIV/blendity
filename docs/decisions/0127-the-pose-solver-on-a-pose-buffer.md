# ADR 0127: The full-body pose solver works on a pose buffer, not on the scene

- **Status:** accepted; decisions 1, 2, 4 and 5 built in round 473 (task 0449), decision 3 (the early exit) measured and not built - see "As built" (approved by the user 2026-11-10: "I approve all specs from all passes")
- **Builds on:** task 0122 (AutoPosing), task 0280-0281 (Puppet Posing and its handles), ADR 0126 (the rig registry)
- **Requirements:** Q-04 (interactive editor), Q-06 (responsiveness)

## Context
A Puppet handle's drag solves the whole body every frame (`Editor::autopose_to` -> `puppet::pose` ->
`autopose::solve`). Round 468 (docs/PERFORMANCE.md, "Pass 22"): PUPPET_MS ms a drag frame on the owner's avatar
(windowed, card) against FK_MS ms for an FK drag of one bone; of it the solver SOLVER_MS ms over SOLVER_ITERS
iterations and the controls around it CONTROLS_MS ms. Read from the code, each frame of the drag:

- puts every object under the armature back to its start pose (`scene_->find` and `set_local` for each: every one
  marks its subtree dirty and bumps the scene's counters);
- rebuilds the joint list (a walk; `kept()` lower-cases each bone's name and asks the springs), sorts it by depth,
  finds each joint's parent by a quadratic search;
- iterates 32 times, and up to 60 more "settle" iterations when anything is pinned (the feet are, by default); an
  iteration allocates its vectors, fills the Jacobian (3 x goals by 3 x joints), solves it densely, lower-cases a
  name per bone (`spine_like`), and writes two transforms per bone INTO THE SCENE (`set_world_rotation`,
  `set_local_rotation`: each marks the subtree dirty, so each later read recomputes world matrices down the chain);
- runs the pose prior first when it is installed.

So one drag frame makes thousands of scene writes that nothing draws, each invalidating what ADR 0124 and ADR 0125
key on.

## Decision
1. **A pose buffer built once per drag** (`autopose::Rig`, at `autopose_begin`): the joints parents-first with their
   parents' indices, rest and start rotations, limits, hinge axes, lengths, each bone's class (kept / spine-like /
   hinge-like, from its name, once), the pins and the root's world matrix. Rebuilt only when the rig registry's
   structure stamp changes (ADR 0126).
2. **The solver reads and writes the buffer only**: world transforms by forward kinematics over the buffer; an
   iteration allocates nothing, touches no string and writes no scene object.
3. **An early exit**: the iterations stop once every goal and pin is met within tolerance and the last step moved
   less than a threshold; the settle iterations run only while a pin is off its place.
4. **The scene is written once a frame**: after the solve, each joint whose rotation changed gets one `set_local`.
   The start pose is not written back first: the solver starts from the buffer's start rotations.
5. **Goldens**: twenty recorded drags on the test rig and the stand-in (each handle, pins on and off, limits hit, a
   target out of reach); the buffer solver's joints stay within 1e-4 m and 0.01 degree of today's for the same
   iteration count, and within a stated tolerance with the early exit on.

## Consequences
- A Puppet drag frame costs the solve's arithmetic and one write per changed joint (PUPPET_MS -> PUPPET_TARGET ms).
- The pose a drag reaches can differ from today's in the last decimals when the early exit ends the iterations
  sooner (the goldens bound it); with the exit off it is today's within float noise.
- The pose prior, FK controls following their bones and IK targets following the pose (`autopose_to`'s tail) stay
  where they are, reading the scene after the one write.

## As built (round 473)
- **Decisions 1, 2 and 4** are `autopose::Rig` with `build_rig`, `solve(Rig &)` and `write`. The buffer holds every
  object under the armature (a goal, a pin or a weighed bone may hang under a joint without being one); a solve walks
  only the nodes it reads. It is built in `autopose_begin` and again when the structure stamp or the scene's identity
  changed - there is no rig registry (ADR 0126's was not built).
- **Decision 3, the early exit, is not built.** The solver's pull back to the start pose fades over the main
  iterations and the body follows it, so the pose reached depends on running all of them: 24 instead of 32 moves bones
  by up to 122 mm and 15 degrees, 16 by up to 323 mm, 8 by up to 515 mm (ten drags on the test rig). An exit that kept
  the pose would need another schedule for the pull, which changes what every drag reaches: a decision for the
  animator, not for a round about cost. The settle iterations' own exit (the pins within 0.5 mm) stays as it was.
- **Decision 5, the goldens:** the old solver stays in the code as `autopose::solve_reference` and the tests solve
  each drag both ways from the same start: every bone within 0.0013 mm and 0.0000 degree (the bound asked: 0.1 mm and
  0.01 degree), the same iteration count, the same errors.
- **With the pose prior** the frame still writes the start pose and the prior's pose into the scene (the prior's
  code reads and writes scene bones) and the buffer is read from the scene before the solve.
- **What it bought (measured):** the solver's part of a Puppet drag frame 1.95 -> 1.11 ms on the stand-in, and 44.5
  scene writes a frame by the solver. On the owner's avatar (about 200 joints) the solver's part is unchanged
  (4.65 -> 4.91 ms): there an iteration is the arithmetic per joint. The Context's "thousands of scene writes" were
  real but cheap next to the frame's skinning, normals and uploads: the Consequences' "a Puppet drag frame costs the
  solve's arithmetic and one write per changed joint" holds for the solver's part only.
