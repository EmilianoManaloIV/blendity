# 0066: Move a bone's head and tail; extrude bones like vertices

- **Status:** done
- **Requirements:** R-06 (rigging and animation)
- **Model:** main session (Opus)
- **PR:** (link when opened)

## Goal
The user (2026-10-14): "Add the ability to move the base and tip of a bone and not just move and scale the bone itself.
Also add the ability to add bones using extrude like vertices."

## Scope
- In: handles on the selected bone's head and tail in the Scene view (Blender's armature Edit Mode, without the mode);
  dragging the tail aims and stretches the bone, dragging the head moves its start with the tail kept; the children
  keep their places and the result is the rest pose; Extrude (the keymap's Extrude: Ctrl+E Unity, E Blender) on a bone
  grows a child from its tip whose tail follows the mouse until a click (Esc cancels); console `bone head|tail|extrude`.
- Out: mirror extrude (Shift+E), bone roll handles.

## Acceptance criteria
1. Dragging the tail changes the bone's direction and length; head and children stay; one undo step.
2. Extrude adds a bone at the tip whose tail follows the mouse; a click keeps it (one step); Esc leaves no bone and no
   step.

## As built
- The head handle needs Shift (a plain press on the head is the gizmo's: Move's free move, Scale's uniform scale).
- The length is kept in the bone's own units, so a scaled armature's tail lands where it's put.
- A drag turns the bone from where it started (no roll drift) and keeps the grab offset; Ctrl snaps to the grid on a
  held drag.
- While an extrude runs, other shortcuts and the right-click fly wait; the step commits on its click.
- Not built: mirrored extrude (Shift+E).
- Tests: rounds 90 and 94.
