# 0043: AutoPhysics as a centroidal trajectory optimisation, solved by Blendity's own QP

- **Status:** accepted (task 0118's spike: GO; used by task 0119)
- **Date:** 2026-10-18
- **Requirements:** R-13

## Context
Task 0117 corrects a flight: a parabola for the center of mass and the take-off's angular momentum kept. Cascadeur's
AutoPhysics goes further and also corrects the frames in contact: a take-off that can't produce the jump, a landing
that stops too fast, a lean no friction could hold. That needs the contacts' forces, which nothing keys. The user
chose "the full AutoPhysics solve".

Blendity has no optimisation library. Blender bundles Eigen, but Eigen has no QP solver, and ADR 0001 rules out new
dependencies.

## Decision
- **The body is its center of mass and its angular momentum** (centroidal dynamics, Orin, Goswami and Lee 2013). The
  limbs aren't solved for: the corrected center and momentum are put back on the rig through the hips and the
  contacts' pinned IK targets, as task 0117 already does. That keeps the problem small, about 10 unknowns a frame.
- **One trajectory optimisation over the clip:**
  - **Unknowns, frame by frame:** the center, the momentum per kilogram, and each contact's force in body weights,
    all near 1 in size.
  - **Equalities:** Newton (the center's second difference against gravity and the forces) at every inner frame,
    and Euler (the momentum's step to the next frame against the forces' moments) at every frame. Euler is a step
    forward, not a central difference: a central one leaves even and odd frames as two chains, free to zig-zag (the
    review found this).
  - **Accepted by the laws:** a result counts when Newton's and Euler's errors, measured exactly, are both under 0.1%
    of the body's weight. A solve that stopped at its step limit can still pass; one whose linearisation never
    settled can't. Clips are limited to 2,000 frames.
  - **Inequalities:** each force inside its friction pyramid, pushing only, at most ten body weights.
  - **Objective:** near the clip's center (each frame's weight, the ends held) and momentum, a small third difference,
    the least force among equal answers (two feet share).
- **The moments are bilinear** (a force's arm depends on the center). They are linearised about the last solution
  and solved again: sequential quadratic programming. The early passes are solved loosely, the last finely, and the
  passes stop once the center moves less than 0.1 mm between them.
- **Blendity's own QP solver** (`core/qp`): ADMM as OSQP does it (Stellato et al. 2020), with an adaptive step and
  stiffer equalities. Its linear system is factorised by a banded Cholesky, because with the unknowns in frame order
  every row couples frames at most three apart. A factorisation costs n x band² (about 4 million multiplications for
  100 frames with four contacts). Its
  stopping test is absolute: the held ends' large weights would loosen a relative one.
- **Contacts are points** (an effector's tip, still through the contact). A foot's support polygon is left for later:
  two contacts per foot (heel and toe) give it when needed.

## The spike's measurements (task 0118, 100 frames at 30 fps, 75 kg, the development machine)

| Clip | Unknowns | Steps | Time | Newton / Euler residual | The center moved | Notes |
|---|---|---|---|---|---|---|
| A jump keyed as a sine arc | 1,080 | 4,230 | 283 ms | 1.4e-5 g / 8e-6 m | 0.25 m | the flight now falls at exactly g |
| A run, 3 m/s | 804 | 8,040 | 313 ms | 1.3e-4 g / 1.3e-4 m | 4.3 cm | nearly possible as keyed |
| A flip keyed slowing its spin | 1,020 | 3,540 | 221 ms | 2.0e-5 g / 1e-5 m | 0.36 m | the spin constant in the air (to 0.01 kg m^2/s); the feet must start it |
| A fall onto the hands | 1,470 | 3,960 | 493 ms | 7.0e-6 g / 1.2e-4 m | 0.16 m | four contacts |

Standing still comes back as it was (0.06 mm), each foot carrying half the weight. **GO:** every clip under 1 s, the
laws held to 0.013% of the body's weight, the moves those the physics asks for. (The spike's first numbers, with a
central-difference Euler: the slowest 606 ms, a flip's spin held within 1%.)

## Consequences
- AutoPhysics (task 0119) runs this on the clip's center and momentum (tasks 0115, 0117) and contacts (0116), then
  writes the result as keys. It shows the same ghost and frame colours, with green, blue or red judged from how far
  each frame moved.
- A solve takes a few hundred milliseconds, so it runs on a worker, never on the editor's thread.
- **Next, if it's needed:** scaling the problem (Ruiz equilibration) to cut the steps, and heel and toe contacts for
  a foot's support polygon.
- The QP solver is general: later tasks (AutoPosing's balance, 0122) can use it.

## Alternatives considered
- **Whole-body dynamics** (every joint's torque): far more unknowns, and nonlinear in the pose. Cascadeur itself
  works on a simplified body.
- **A general nonlinear solver** (IPOPT and the like): a new dependency, which ADR 0001 rules out.
- **Eigen's dense or sparse solvers inside ADMM:** the banded factorisation is already a few milliseconds. Eigen would add a
  `BL_WITH_EIGEN` switch for no measured gain.
