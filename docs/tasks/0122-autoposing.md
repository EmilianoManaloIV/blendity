# 0122: AutoPosing

- **Status:** done
- **Requirements:** R-13
- **Decisions:** ADR 0042 (the corrections as keys)
- **Model:** main session (Opus)
- **PR:** (with task 0121)

## Goal
Pose a character by one end, as Cascadeur's AutoPosing does: drag a hand, a foot, the head or the hips, and the spine
and limbs follow naturally with the feet kept down and the body balanced. The user chose "rule-based" (no learned
model).

## Scope
- In: `autopose::solve`: a full-body inverse kinematics by damped least squares (Buss 2004).
  - **Unknowns:** every body bone's turn about its head, plus the hips' place. Fingers, toes, eyes, the jaw, end bones
    and spring chains keep theirs.
  - **Goals:** a bone's head to a point. Each goal's error is clamped to 10 cm a step.
  - **Pins:** heads held where they are, far firmer than goals.
  - **Balance:** the center of mass's ground point kept over the pins' middle, with two or more pins.
  - **Limits:** each joint turns at most 75 degrees from the pose the drag started in (the spine and neck half of
    that), and is pulled back towards it, less with each iteration.
  - **The hips:** with pins, they move at most 30 cm (a lean, not a step).
  - **Settle passes:** with the goals held only lightly, they put back a pin that a hard reach left off.
- In: the editor.
  - With **AutoPosing** on (the Animation window's button, or `autopose on`), dragging one bone with the move gizmo
    poses the body: the drag's starting pose is restored and solved again each frame.
  - **Pins:** the contacts at the playhead, the bones the user locks (`autopose lock <bone>`), or else the feet.
  - IK targets are moved to where their chains end, so IK agrees with the pose instead of pulling it back.
  - **On release:** the moved bones and targets are keyed at the playhead if the character has a clip, in the same
    undo step as the drag.
  - Console: `autopose on|off | drag <bone> <x> <y> <z> | lock | unlock | locks`. A Learn section.
- Out: rotating with the gizmo (moves only); the support polygon of feet with area (the pins' middle stands in);
  learned priors.

## Acceptance criteria
1. A dragged hand reaches a reachable target; the pinned feet don't move; joints stay within their limits; the rest
   of the body barely moves; under a frame for a humanoid.
2. The center of mass stays over the feet when the head is pushed forward (balance on, against off).
3. Out of reach: the arm points at the target, the feet stay, nothing breaks.
4. In the editor: a drag poses and keys (one undo step); IK targets follow; a locked hand stays.

## As built
- Tests: round 141, on GameObject > Rig > Humanoid's person (17 joints).
  - A hand within reach: 0.6 mm off, the feet 0.1 mm, the largest turn 40 degrees, the head's 4, in 0.5 ms.
  - Head forward: the center of mass 3.4 cm from over the feet with balance, against 6.6 cm without.
  - A hand dragged 5 m away: the arm 2 degrees off the target's line, a foot 12 mm, with the hips at their 30 cm lean
    and the legs at their limits.
  - Before the clamp, the hips cap and the settle passes, the same reach walked a foot 2 m.
- The Windows build passes its sources through a response file: with these files the compiler's command line passed
  cmd's 8,191-character limit.
- After the review:
  - Dragging the hips keeps the feet pinned: a 25 cm crouch lands 5.6 mm off, the feet held and flat, the knees bending
    forward.
  - IK poles follow the solved bend.
  - Pinned and dragged ends keep their turn.
  - Limits are measured from the rest; a knee or elbow is a hinge, with one Jacobian column (its bend axis, up to 150
    degrees).
  - Balance works only through joints that move no goal or pin. A damped null-space projection leaked into the goals;
    an undamped one blew up.
  - Pins and IK chains are found once per drag, the body is kept between drags, and the settle passes stop when the
    pins are back.
  - Keys go only where something moved, and not on IK-driven bones.
