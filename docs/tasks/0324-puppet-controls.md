# Task 0324: Puppet Posing's control points you choose

- **Status:** done (round 347, 2026-10-31)
- **Asked:** "For the auto posing system, where can we specify control point bones so that we can adjust certain points
  of the bone is incorrect." - "Yes add that to the plan."

## Today (before)
Six handles (the hands, the feet, the head, the hips) could be put on bones by hand: Animation > Puppet > Handle Bones,
`autopose handle`, or the Armature's Puppet Handles text ("Left Hand=Bone.003; Head=head"). Only the handles followed
them - AutoPose's prior, the walk and footsteps kept the analysis' bones - and a chain start (an upper arm, a thigh)
couldn't be set at all.

## What
- **The Armature's Puppet Controls** (a fold in the Inspector): a bone field each - Hips, Head, the hands, the feet, the
  upper and lower arms, the thighs and the shins - a bone dropped from the Hierarchy, picked (the rig's bones first) or
  typed; empty, the analysis decides. `set Armature.LeftThigh <bone>` sets one from the console.
- **The analysis takes them** (`gait::analyse`): every tool that reads the rig - AutoPose and its prior, the walk,
  footsteps, the controls - agrees with the bones set.
- **Extra Controls:** bones by name, comma separated - a tail's tip, a sword - each a purple ring to drag, the bones
  above bending (AutoPose).
- **Problems said:** a line under the Armature's fields when the analysis has the left hand or foot on the right, or the
  head below the hips.
- Files keep the one text (Puppet Handles), its entries kept whichever is set (`Armature::puppet_bone`,
  `set_puppet_bone`); round 324's text loads.

## The review's fixes
- A part set on a side the analysis found no limb on did nothing (an imported rig named Bone.001 has no arms by name):
  the arms - and, when none are found, the legs - are made from the parts set.
- Each part was set alone, unchecked: a hand set now pulls its forearm and upper arm (its parents) unless they are set
  too, every limb must be a chain (each bone below the one before) and not another limb's; one that isn't keeps the
  analysis' bones and the Inspector says why. A head not below the hips is refused likewise.
- Animation > Puppet > Handle Bones' Found Automatically shows the analysis' own bone (`analyse(.., with_parts)`); the
  Inspector's line looks again when the rig's bones change; an armature never posed shows no line; a ';' or '=' in a
  name is cleaned; one parser of the list (`Armature::puppet_bone`); the help says quadrupeds ignore the limb fields.

## Not done
- A control's right-click "Put on Bone..." in the Scene view (the Inspector's fields and Handle Bones do it).
- A Reach per Extra Control (AutoPose decides how far up it bends).
- Knee and elbow pole controls (round 348's FK/IK).

## Acceptance (`tests_round347`)
- A left hand and a left thigh set by hand are the analysis'; `set Armature.RightFoot` sets one; other entries kept;
  saved and loaded.
- An Extra Control on the Chest is drawn and drags it; swapped hands warned in the Inspector; the lesson's Try-it.
