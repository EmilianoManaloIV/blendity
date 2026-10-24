# ADR 0049: Realistic hair and soft-tissue physics on Spring Bones

- **Status:** accepted (2026-10-21, tasks 0153, rounds 171-172)
- **Deciders:** the user (asked), the main session (proposed)
- **Related:** ADR 0042 (body physics as keys), task 0120 (spring bones), ADR 0047 (imported characters)

## Context
Task 0120's spring bones move hair, ears, tails and cloth as verlet particles pulled toward the animated pose in
world space, kept at their lengths and outside capsules along the body's bones (a seventh of each bone's length in
radius). That gives lag, overshoot and settling, and it is enough for a tail. It is not enough for hair and soft
tissue, which the user asked for: a curl straightens (nothing holds a bone's angle to the one before it), every
strand of a ponytail moves alike (nothing keeps strands apart), the wind doesn't exist, a skirt passes through a
thigh whose capsule is thinner than the leg, and a breast or a belly - a single bone that should trail the body's
acceleration, stay inside a cone and keep its volume - has no model at all.

The research (Kugelstadt & Schömer 2016, position-based Cosserat rods; Macklin 2016 and 2019, XPBD and small
substeps; Daviet 2023, real-time hair with contacts; Akyürek & Sahillioğlu 2025, spring-decomposed skinning for
secondary motion) agrees on the frame: particles, substeps, constraints projected in turn. Full Cosserat rods carry
a quaternion per segment; what a game character needs from them - the bend holding its rest shape, the twist
following the root - is had more cheaply.

## Decision
- **The chain solver stays verlet with substeps**, and gains constraints projected after the pose pull each
  substep: a **bend** constraint (each tip pulled toward where its bone's rest angle to the bone before it puts it,
  by `bend`: a curl keeps its shape, a strand bends where it was bent), the pose pull **fading from root to tip**
  (`root_to_tip`: the tip free to swing, the root following the animation), **wind** (a `Wind` component: a
  direction or a point, a strength in m/s, turbulence and gusts from periodic noise of time and place; a chain's
  `wind` scales its response), **colliders fitted to the skin** (each body bone's capsule radius from the vertices
  its vertex group moves, cached per armature by the skinned mesh's version; a seventh of the length without a
  skin) and **strand separation** (every stepped chain's tips in a uniform grid, tips of different chains pushed
  apart to their radii, chains with `separate`). The twist follows the animation by construction: a bone's world
  rotation is its animated rotation turned by the smallest rotation from its animated to its simulated direction,
  which keeps the roll.
- **Substeps settle the frame rate**, as before (120 Hz, up to 16 a frame); the new forces and constraints are
  applied per substep, so 30, 60 and 144 fps give the same hang within the old tolerance.
- **Soft tissue is the fifth label, "Soft"** (round 172): a single bone (or a short chain) whose tip is a damped
  spring-mass driven by the bone's animated **acceleration** (it trails the body's acceleration, not its position:
  a run makes it bounce, a slow walk hardly), limited to a cone about the animated direction, with **volume** kept
  by a counter-scale on the bone as the tip stretches or squashes (on a chain's last bone: a non-uniform scale on
  a parent shears what hangs below it; a bake writes it as Scale keys). The spring is the chain's Stiffness and
  Damping; `lag` is the share of the acceleration trailed (the verlet tips trail the position on their own). Soft
  chains don't collide by default: the torso's fitted capsule would sit on them. Breast, thigh, hip, belly bones
  are found by name (VRM's `J_Sec_L_Bust`, MMD's 胸 / 尻 / 太もも after its tails and ribbons, Rigify's and game
  rigs' words) and get Soft defaults. A **Jiggle** deform modifier does the same for a mesh without such bones: a
  vertex group's centre as the spring, every vertex moved by the offset and the stack masking it by the group. It
  runs on the scene's simulated clock (Play, whether or not the Physics assistant is on) and is not baked into
  clips: a jiggle in a render comes from the Play-time step, not from keys.
- **Hair rigged from its mesh** (round 172): hair cards (each its own lock, up to 64; more are merged into sectors
  by their roots) or a shell cut into eight sectors, a chain per lock from the scalp to the tip, each vertex
  weighted to its own lock's two nearest joints, springs added - one undo step. A mesh skinned to a character
  hangs its rig under the head bone (found by the whole word "head" or 頭), in the rest pose only, and the hair's
  chains collide with the character's body (a chain's armature is the outermost above it).
- Out: a true Cosserat twist constraint (the roll follows the root instead), self-collision inside one strand,
  a ground plane for strands (the body's capsules suffice for a character), a "Show Colliders" overlay (the
  Scene view's bone icons stay; a follow-up if asked).

## References
- T. Jakobsen, "Advanced Character Physics", GDC 2001 (verlet, constraints by projection).
- T. Kugelstadt, E. Schoemer, "Position and Orientation Based Cosserat Rods", SCA 2016 (bend-twist with a rest
  shape: the bend constraint here is its position-only simplification).
- M. Macklin et al., "XPBD: Position-Based Simulation of Compliant Constrained Dynamics", 2016; "Small Steps in
  Physics Simulation", SCA 2019 (substeps over iterations).
- G. Daviet, "Interactive Hair Simulation on the GPU Using ADMM", SIGGRAPH 2023 (strand contacts in a grid).
- O. Akyuerek, Y. Sahillioglu, "Real-Time Secondary Animation with Spring Decomposed Skinning", CGF 2025 (the
  Jiggle modifier is its one-handle case).
- VRM 1.0, VRMC_springBone (stiffness, drag, gravity, hit spheres); games' jiggle bones (acceleration-driven lag,
  a cone, a counter-scale for volume).

## Consequences
- Old chains behave as before: `bend` 0, `root_to_tip` 0 and `wind` 1 with no Wind component give the old pull;
  the label defaults set the new values only when a label is applied again. Bakes stay deterministic (no clock in
  the step: the wind's time is the simulated time).
- Fitted colliders can be wider than the old ones: a skirt that rested against a thigh's thin capsule sits further
  out - by the thigh's real width, which is what was asked.
- Strand separation is O(n) with a grid; a hundred chains of ten bones cost well under a millisecond a frame.
- Watch: a bend of 1 with a stiff pose pull fights the pull (both want the tip somewhere); the label defaults keep
  them moderate. A Wind with gusts changes a bake when the clip's length changes (the gust noise is in simulated
  seconds): bake once the clip is cut.

## Amendment (task 0168): strands as mass-weighted XPBD rods
The user found hair "stiff and jello-like". Measured: the per-substep blend toward the pose was a spring of about
5 Hz at hair's stiffness that pinned the strand (a horizontal 40 cm strand sagged 1.6 cm) and rang against it (14
wiggles of over 2 mm while settling). Every label but Soft now steps as an XPBD rod (Macklin et al. 2016; small
steps, Macklin 2019):
- **Mass** per tip from its bone's length and the chain's Density (kg/m; hair 0.01, an ear 0.02, cloth 0.05, a
  tail 0.1); the root fixed.
- **Substeps at 240 Hz**, one iteration each, velocities from positions; the multipliers start at zero each
  substep (with one iteration they need no carrying over).
- **Compliant constraints** for the pull toward the pose and the bend: a slider s is a spring of 20 Hz x s^1.5 for
  hair's mass, applied as w / (w + alpha / h^2) - the frame rate drops out, and a heavier strand is pulled less by
  the same slider (k is in hair's mass, the inverse mass is the strand's).
- **Damping** decays the motion relative to the root's (Damping x (4 omega + 20) per second: about critical for
  hair's defaults), **air drag** always on toward the air's velocity (Air for hair's density, divided by the mass
  ratio; a Wind adds its 4 x Wind and its velocity).
- **Lengths** as mass-weighted distance constraints (three sweeps), then the bend toward its goal, then the exact
  root-first pass and the body's capsules - the bend after the sweeps, so the lower bones' weight doesn't open a
  curl's upper joints.
- **Soft tissue keeps its verlet spring** (the acceleration-driven model above was tuned with it).
The old scenes' sliders keep their values and mean the new spring; Density and Air load at hair's values. With no
pull and a free root a curl now turns about its root until its weight hangs below (Bend keeps its joints' angles):
the round 153 test measures the angles.
