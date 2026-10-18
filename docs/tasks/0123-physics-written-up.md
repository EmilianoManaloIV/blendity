# 0123: Physics for animation written up

- **Status:** done
- **Requirements:** R-13
- **Decisions:** ADRs 0042, 0043, 0044
- **Model:** main session (Opus)
- **PR:** (with tasks 0121, 0122)

## Goal
The physics plan's close: the whole set (tasks 0115-0122) explained in one place, with a Try-it a newcomer can follow.

## Scope
- In: a Learn section, "Physics for animation: a jump from start to finish".
  - **The order:** weigh, contacts, flight or AutoPhysics, then secondary motion, the ragdoll and AutoPosing.
  - **The common ghost and colours.**
  - **A Try-it:** a humanoid jumping in straight lines, its contacts found and its flight shown corrected.
- In: README's animation paragraph (each feature named), ARCHITECTURE's rows, USABILITY's rows (written with each task).
- In: a test that runs the Try-it as the Learn tab does.

## Acceptance criteria
1. The Try-it, run as the Learn tab runs it, finds the jump's contacts and shows one flight corrected.

## As built
- Tests: round 142. The Try-it finds 4 contacts (two feet at each end) and shows one flight, the body moved up to
  0.84 m: the keyed 1 m peak is higher than its 0.67 s in the air allows.
- The plan's tasks 0115-0123 are done:
  - masses, contacts and flight (#101);
  - AutoPhysics and spring bones (#102);
  - the active ragdoll, AutoPosing and this write-up (this PR).
