# 0156: IK handles sized in metres, a control-size preference, rotatable IK targets

- **Status:** done (round 174)
- **Requirements:** R-06
- **Decisions:** ADR 0031 (amended: controls drawn in metres; the end bone follows the target's rotation)
- **Model:** main session
- **PR:** #119 (stacked on #118)

## Goal
The user (2026-10-21): "In the current project I'm in, the IK handles are way too big. [...] On the case of Inverse
Kinematics I should be able to rotate the handles."

## What was wrong
- `add_limb_ik` sized a target's box from the lower bone's world length (metres) but the wire was drawn through the
  control's world matrix, which carries the rig root's scale: a centimetre rig (root scale 100, as FBX / Mixamo
  imports arrive) drew every handle a hundred times too big, a 0.01 rig a hundred times too small. FK circles were
  sized from the bone's own length (local units), wrong the other way round.
- The solver read only the target's position; the foot or hand below the chain kept its own pose and swung with
  the shin or forearm. Turning a handle did nothing.

## Scope
- In: `rig_control_segments` builds the wire from the control's world position and rotation only, at `size`
  metres times the new preference Rig Control Size (Preferences; `overlay controls size <x>`; saved as
  `rig_control_scale`); FK sizes from the bone's world length; Rigging > Fit Control Sizes (`ik fitsizes [root]`)
  recomputes every control's size from its bone (IK target 0.2 of the lower bone, pole 0.12, FK 0.3); loaded
  scenes keep their sizes (a rig whose handles were in another unit comes right with Fit Control Sizes).
  `IKConstraint::rotate_end` and `end_offset`: the bone below the chain's end takes the target's world rotation
  times the offset after each solve; new IK (Add IK, the rig presets, Auto IK) spawns a world-aligned target with
  Rotate End on and the offset carrying the end bone's rotation; `ik orient <bone>` turns it on for an existing IK with the
  offset that keeps the bone as it is; the hash of the solve's inputs includes the target's rotation; `ik_drives`
  covers the end bone, so the gizmo and the animation tools treat it as driven.
- Out: a twist / roll option for the limb from the target's rotation; FK / IK blending; screen-constant handles.

## Acceptance criteria
1. A humanoid rig at scale 1, 100 and 10 draws the foot target's box with an edge of 0.4 of the shin's world length
   (within 1 %) after Fit Control Sizes.
2. An FK circle is 0.3 of its bone's world length; an old-style FK size (the bone's local length) on a rig scaled
   by 100 shows as 0.003 of the bone until Fit Control Sizes makes it 0.3.
3. The preference 2 doubles the wire; a control's own scale doesn't change the wire.
4. A fresh rig's target starts world-aligned with an offset that reproduces the foot; turning the handle 30 degrees about the world's up turns
   the foot 30 degrees and leaves the knee in place; Rotate End off leaves the foot at its rest; `ik orient`
   captures a pitched foot and keeps it through a turn; the foot refuses the gizmo (`ik_drives`); the fields save
   and load.

## Tests and checks
- `IK handles: a target's box is a fifth of the shin ...` and `IK handles: a new target starts with the foot's
  rotation ...` (tests/test_main.cpp, round 174); the IK and rig-control groups unchanged.

## After the review
- One source for "the bones IK turns" (`ik_turned_bones`: the chain and, with Rotate End, the end bone): the FK
  solve, Add FK, the tint and `ik_drives` all use it, so an FK control on the foot is refused instead of fighting
  the orient every frame.
- Rotate End ticked in the Inspector captures its offset on the first solve (`end_set`), so the foot stays as it
  is instead of snapping to the handle; Weight blends the foot too; a NaN handle rotation turns nothing.
- The end bone is the child whose head sits at the chain's tip (a twist bone beside the hand isn't the hand).
- Fit Control Sizes matches a control's IK on the same top object (two copies of a character) and the FK bone by
  the id the solve found; a mistyped rig name warns instead of resizing the scene; the floors are a millimetre
  (a rig at 0.01 keeps a fifth of its shin); `fitted_control_size` and `ik_owner_of_control` extracted; the
  wire's frame is the world matrix's axes made unit and orthogonal with the mirroring kept.
- The gizmo's refusal says "turn its target instead" for the end bone; `overlay` lists `controls size <x>`.
- Tests added: IK added on a rig already at scale 100 (no refit), the 0.01 rig, Rotate End ticked later, Weight
  0, a twist bone, FK on the foot refused and the solve settling, a file without the field loading with it off.
- Not tested directly: the pick distance (it reads the same segments the wire is drawn from).
- The chain found two more: the Preferences row sat above the keymap preset (its list had to open upward: moved
  after the Import row, as task 0152's); the walk and footstep generators keyed the foot's roll on the foot, which
  Rotate End then overrode from the handle (43 mm of sliding): `gait::apply` turns the handle to the roll too. A
  handle spawned with the foot's rotation sat near the gimbal of its Euler keys (a foot points 90 degrees about X)
  and planted feet still slid 1.7 mm, so the target now starts world-aligned and the offset carries the foot's
  rotation (0.001 mm).
