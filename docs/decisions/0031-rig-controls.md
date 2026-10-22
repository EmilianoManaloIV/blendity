# 0031: Rig controls as a component; FK solved before IK

- **Status:** accepted (task 0067; the user's request of 2026-10-14)
- **Date:** 2026-10-14
- **Requirements:** R-06
- **Relates to:** ADR 0025 (bones and skins), task 0048 (IK)

## Context
IK targets and poles were bare empties, drawn nowhere, so a rig's handles read as part of the skeleton. Blender gives
bones custom shapes; Maya rigs use NURBS controller curves coloured by side. Animators pose a rig by its controls.

## Decision
- **RigControl** is a component on an ordinary object: shape (Circle, Square, Box, Diamond, Sphere, Arrows, Cross,
  Root), side (Auto from the name, Centre, Left, Right), role (Control, IK Target, IK Pole, FK), size, the bone it
  drives (FK, by name) and In Front. The editor draws its wire over the scene and picks it by its wire.
- **IK** controls are made by Add IK and the rig builders; Remove IK deletes only controls with an IK role.
- **FK**: a control with role FK, parented to its bone's parent, turns the bone to its own world rotation in
  `solve_constraints`, before IK; bones an IK chain turns are left to IK (the two never fight).
- **Rig roots** (task 0065): a character is one object holding its mesh and its Armature, as Unity imports humanoids;
  the Animation window and the Sequencer target it.
- **Controls are drawn in metres** (task 0156): the wire is `size` metres round the control's world position and
  rotation, with no scale from the control or its parents, times the Rig Control Size preference. A rig's root
  scale (a centimetre import) no longer scales its handles; Fit Control Sizes recomputes sizes from the bones.
- **The end bone follows the target's rotation** (task 0156): an IK constraint's `rotate_end` makes the bone below
  the chain's end (the foot, the hand) take the target's world rotation times `end_offset`, captured when the IK is
  made (the target starts world-aligned and the offset carries the end bone's rotation: a handle keyed near the
  gimbal of its Euler angles would lose a degree) or by `ik orient`. Blender's IK "Rotation", Maya's orient
  constraint on the handle. The end bone counts as driven by IK; the walk and footstep generators turn the handle
  to the foot's roll.

## Consequences
- Controls are objects: they keyframe, parent and select like anything else, and files need no new format.
- References stay by name (the IK's target and pole, the FK's bone), as the rest of the scene's references.
