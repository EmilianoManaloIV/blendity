# 0118: Spike: the full AutoPhysics solve (GO / NO-GO)

- **Status:** done (GO)
- **Requirements:** R-13
- **Decisions:** ADR 0043 (new)
- **Model:** main session (Opus)
- **PR:** (with task 0119)

## Goal
Find out whether Cascadeur's AutoPhysics can be done in Blendity without a new dependency, fast enough to use: a
whole clip made physically possible, the frames in contact included, not only its flights (task 0117).

## Scope
- In: `qp::solve` (`core/qp`), a convex QP solver: ADMM as OSQP does it, with a banded Cholesky factorisation, warm
  starts, an adaptive step, and bad input refused.
- In: `body::solve_centroidal` (`scene/autophysics`): the centroidal trajectory optimisation of ADR 0043, with its
  moments linearised and solved again until settled; `centroidal_residuals` measures the result with the exact
  moments.
- In: four clips as an animator might key them (a jump, a run, a flip, a fall onto the hands), solved and measured.
- Out: putting the result on the rig, settings, and the editor: task 0119.

## Acceptance criteria (GO)
1. Every clip of 100 frames solves in under about 1 s.
2. Newton's and Euler's laws hold to a fraction of a percent of the body's weight.
3. Each clip moves only as far as its physics asks: a flight falls at g, a flight's spin stays constant, and a clip
   that is already possible comes back as it was.

## As built
- **GO:** the slowest clip (a fall onto the hands, four contacts) takes 493 ms. The laws hold to 7e-6 to 1.3e-4.
  Flights fall at exactly g, and a flip's spin is constant in the air. Standing still moves 0.06 mm. Every clip is in
  ADR 0043's table.
- What the spike changed on the way:
  - **Absolute stopping:** a relative test was loosened by the held ends' weights, and standing drifted by 4 mm.
  - **Settling:** loose early passes and a fine last pass, stopping once the center moves less than 0.1 mm.
  - **Regularisation:** the least force among equals is now weighted 1e-6. At 1e-4 it made a standing body sag.
- After the review: Euler as a step forward (a central difference let even and odd frames zig-zag); results accepted
  by the laws' measured errors rather than the solver's status; clips up to 2,000 frames; cancel between passes.
- Tests: round 138.
