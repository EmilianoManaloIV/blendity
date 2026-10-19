# 0117: Ballistic flight and angular momentum

- **Status:** done
- **Requirements:** R-13
- **Decisions:** ADR 0042
- **Model:** main session (Opus)
- **PR:** (with tasks 0115, 0116)

## Goal
The core of Cascadeur's AutoPhysics: in the air, the center of mass follows a parabola under gravity and the body keeps
its angular momentum. The correction is shown as a ghost with frames green, blue or red, and applied when the animator
says.

## Scope
- In: `body::correct_flight`, for each stretch without contacts between a take-off and a landing:
  - the center of mass moved onto the parabola through the take-off and landing centers, at their times (so the jump
    keeps its length and its duration);
  - the body turned about its center so the angular momentum stays the one of the first frame in the air;
  - by default the turn left at landing is spread back, so the landing pose is the animator's.
- In: the hips' Position and Rotation keyed on every frame of a flight (and its take-off and landing), and the IK
  targets outside the hips with them. Euler angles are kept continuous.
- In: marks per frame: ground, fine (green), changed (blue), impossible (red: a take-off over 15 m/s, or a correction
  over 1 m; nothing written then).
- In: `body::angular_momentum` (per frame, from the bones' motion) and `quat_of`.
- In: the Animation window's Flight button (the ghost of the corrected body, the frames' colours under the ruler) and
  Apply (one undo step); the console's `body flight show | apply | off`.
- Out: forces at contacts (the full solve: tasks 0118, 0119).

## Acceptance criteria
1. A straight-line jump comes out a parabola with g = 9.81 m/s^2; the keyed clip realises it exactly; ground frames
   are untouched.
2. A tuck's angular momentum is kept within 3%; the landing pose is kept when asked.
3. An impossible jump is marked red and not written; a clip without contacts says so.
4. In the editor: shown without writing; Apply is one undo step.

## As built
- Tests: round 137.
