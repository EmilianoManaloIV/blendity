# 0279: one humanoid map for every rig

- **Status:** done (round 299)
- **Requirements:** R-08
- **Decisions:** ADR 0087 (Puppet Posing)
- **Model:** main session
- **PR:** #230

## Asked
The user (2026-10-29, /plan): "How to implement Animatica Marionette" - a pose model that moves the whole body from a
dragged hand, foot, hips or head, on Mixamo, UE5 mannequin, Character Creator, Daz, VRoid, 3ds Max Biped and Rigify
rigs, in a T or an A pose, without renaming bones. Their choices: a learned pose prior plus IK, after the bridges.
Blendity's feature is called Puppet Posing; nothing of Animatica's is used.

## What changed
- **`limb_of` by words** (`src/scene/ik.cpp`): a bone's name is split into words at separators and where a lower-case
  letter meets an upper-case one (`lThighBend` -> l, thigh, bend), so a side marker anywhere counts: Biped's
  `Bip01 L Thigh`, Character Creator's `CC_Base_L_Thigh`, VRoid's `J_Bip_L_UpperLeg`, Daz's `lThighBend` and
  `lShldrBend`, the UE5 mannequin's `thigh_l`, as Mixamo's and Rigify's before. Twist, roll and helper bones are not
  limbs. Daz's `Collar` is a shoulder, as `Clavicle` and `Shoulder` were. `gait::analyse`, and with it the walk, the
  footsteps and AI inbetweening, read these rigs by name now instead of by shape.
- **`humanoid::map`** (`src/scene/humanoid.h/.cpp`): the 21 parts Puppet Posing works with - hips, spine, chest, neck,
  head, shoulders, upper and lower arms, hands, upper and lower legs, feet, toes - on any rig, and each upper arm's drop
  below the horizontal as the rig stands (about 0 in a T pose, 30-50 in an A pose).
- **`humanoid::fk_control`**: what to key for a bone - its FK control (a Rig Control driving it) when the rig has one,
  else the bone itself.

## Tests (`tests_round299`, physics group)
31 names read right (the seven schemes, twist bones skipped); five rigs named the Biped, Character Creator, VRoid, Daz
and UE5 ways, each in a T and an A pose: all 21 parts found by their own bone, the arms' drop measured (0 and 40
degrees) and the pose told apart; an FK control returned for its bone.

## Not done
- Fingers: Puppet Posing poses the body; a hand's fingers keep their keys.
