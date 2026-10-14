# 0067: Rig controls - IK and FK gizmos like Blender's and Maya's, and removing them

- **Status:** done
- **Decisions:** ADR 0031
- **Requirements:** R-06 (rigging and animation)
- **Model:** main session (Opus)
- **PR:** (link when opened)

## Goal
The user (2026-10-14): "Give the inverse kinematics a special gizmo to indicate it's part of a rig controller and not
the armature itself (also add the option to remove FK/IK systems); make the gizmo reflect many IK/FK/Rig gizmos such as
those used in blender and maya."

## Scope
- In: a Rig Control component (shape, side, role, size, drives, in front) drawn as wire over the scene and picked by
  its wire; IK targets as boxes and poles as diamonds coloured by side (left blue, right red, middle yellow) with a
  dashed line from the pole to its joint; IK-driven bones tinted; FK controls (circles that turn their bones);
  Remove IK (constraint, target and pole controls), Add / Remove FK Controls; console `ik remove`, `fk add|remove`.
- Out: IK/FK blending switches, custom control meshes.

## Acceptance criteria
1. Add IK gives a box target and a diamond pole, coloured by side; Remove IK takes them away in one step.
2. Turning an FK circle turns its bone; a click on the circle's wire selects it; Remove FK Controls leaves the pose.

## As built
- Remove IK deletes only its target and pole controls (Rig Control with an IK role) that no other IK uses; an object
  of the user's used as a target stays. The Inspector's Remove Component on an IK constraint does the same.
- FK controls aren't added to bones IK turns (they would fight each frame); FK leaves IK's bones alone.
- Reset Pose turns the FK controls back with their bones.
- Sides come from Side or the name (Left / .L / _L / " L", the controls' IK / Pole / FK suffix ignored).
- Controls near a bone win a click (a control's distance counts half).
- Tests: rounds 91 and 94.
