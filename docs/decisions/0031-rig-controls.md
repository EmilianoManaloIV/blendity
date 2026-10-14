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

## Consequences
- Controls are objects: they keyframe, parent and select like anything else, and files need no new format.
- References stay by name (the IK's target and pole, the FK's bone), as the rest of the scene's references.
