# Task 0325: FK / IK per limb, matched both ways

- **Status:** done (round 348, 2026-10-31)
- **Asked:** "If possible, will it be useful to combine the FK/IK system with this as well?" (answered yes: Puppet
  Posing is IK - best for placing the ends; FK is better for arcs, swings and overlap; every animation package switches
  per limb and matches the pose so nothing pops)

## Today (before)
An IKConstraint's Weight (keyable since round 335) blended between the **rest pose** and the solve: Weight 0 put the
limb at rest whatever its bones had been turned to, so FK on an IK limb was impossible, and turning the Weight down
made the arm snap. Nothing moved the handles to a pose made by turning bones.

## What
- **Weight 0 is FK** (`ik_off`): the solve leaves the chain alone, so the bones keep their own pose - turned by the
  gizmo (no longer refused, `ik_drives`) or keyed; Rotate End leaves the hand or foot too. Weights between 0 and 1
  blend from the rest pose as before. (An MMD motion's IK switches, task 0137, now let its FK keys show.)
- **Switching without a jump** (`Editor::ik_switch`):
  - **to FK** - the bones hold the pose the IK left;
  - **to IK** - the target goes to the chain's tip (turned with the hand or foot when Rotate End is on) and the pole
    off the middle joint along its bend (`ik_match_to_pose`), so the solve gives the pose there is;
  - **Match** - the handles only, the Weight as it is.
  - With Record on, keyed at the playhead: to FK the Weight and the bones' rotations, to IK the Weight and the handles.
- **Where:** the IK constraint's Inspector (Switch to FK / Switch to IK / Match Handles); Animation > Puppet > FK / IK
  (each limb's IK tick, and Match); `autopose fk|ik|match larm|rarm|lleg|rleg` on the selected character.

## The review's fixes
- With Record on, the keys had been made one at a time, each applying the clip again - a clip already keying the arm put
  its old pose back before the bones were keyed (FK, IK, FK again at 30 jumped to frame 12's pose). The values are read
  first and keyed in one edit, at the drawn frame (on 2s, the drawn one), as one undo step named for the switch.
- The Weight key is a step (a smooth one blended frames between two switches through the rest pose); when nothing keyed
  it earlier, the state before is kept on the frame before. Each switch keys both the bones and the handles, so the
  stretch on each side is pinned.
- Weight 0 had kept "whatever the last frame left" for bones no key poses: a clip that turns an IK off puts those bones at
  rest (`apply_clip`), whichever frame was shown before - so an MMD motion's unkeyed knees show rest, as MMD does.
- Going to IK can change a bone's twist (and, past two bones, the middle joints): IK solves from the rest pose, so it is
  said in the Console rather than hidden.
- An FK limb is the bones': the walk and footsteps pose and key its bones (`find_ik`), FK controls turn them, they aren't
  tinted as IK-driven, and AutoPose drags its bones rather than a handle with no effect.
- The handles are found by id (`ik_target_of`, `ik_pole_of`); the Puppet menu analyses the rig once a draw; a four-legged
  rig is said (use its IK bone's buttons).

## Not done
- A forearm's twist kept by IK (a swing-twist solve).
- FK rotation rings on each joint in Pose Controls (the gizmo turns an FK bone).
- A blend from the FK pose (between 0 and 1 the blend is from the rest pose, as before).
- An FK limb carried rigidly by an AutoPose hips drag.

## Acceptance (`tests_round348`)
- To FK the hand and elbow stay (1e-4), the bones turn by hand and the handle moves nothing; to IK the hand, the elbow
  and the hand's turn stay (1e-3) with the handle at the hand; undo.
- Match moves only the handles; the Inspector's buttons clicked; the legs by name; a word that isn't a limb, or no rig
  selected, changes nothing.
- Record keys the switch; the lesson's Try-it.
