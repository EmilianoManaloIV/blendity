# Task 0322: Puppet Posing's controls you can click

- **Status:** done (round 346, 2026-10-31)
- **Asked:** "I'm also having trouble just clicking the control points without clicking or transforming the bones. What
  would you recommend as a suitable approach?" (and the plan's controls, as the animation programs draw them)

## What was wrong
The handles were 8 px dots on the joints themselves - where the bone is clicked and where its move gizmo sits - shown
only while a bone of the rig was selected (so a bone, and its gizmo, came first); a plain click on one pinned it.

## What
- **Controls, not dots on joints:** a ring round each wrist, a box under each foot, a ring above the head and a large ring
  round the hips, drawn over the model; left blue, right red, the middle yellow; a twelfth of the rig's height times the
  Armature's **Control Size**. A pinned one has a red centre.
- **Shown with the mesh selected:** the rig is found from a skinned mesh's Skin modifier or a character root's child, so
  no bone needs selecting first.
- **Hit anywhere in the ring or box** on screen (at least 12 px; 16 in Tablet Mode). A drag poses from the control's own
  point (the bone moves by as much); a click selects it; a **double-click pins it** (a click had - the commonest slip).
- **While AutoPosing, a click that lands on a bone of the rig keeps what was selected** (no gizmo appears on the bone);
  **Ctrl+click** picks it.

## The review's fixes
- A double-click to pin also framed the selection (the camera jumped): the pin takes the double-click.
- Ctrl+click added the bone to the mesh's selection (the gizmo then moved both, without AutoPose): it selects the bone
  alone.
- A control's click was only drawn: it selects its bone, so trails, onion skin, contacts and Lock the Selected Bone
  follow it, with no gizmo or bone handles over the control; the trails find the rig from the mesh as the controls do.
- The controls were hit over their whole disc (the gizmo, a finger's orbit and objects inside lost their presses): on
  the ring's line as seen (a level ring from the front is a flat ellipse), the box and the centre dot.
- The size follows the bones' lengths, not the pose (a crouch shrank every control); a blocked bone click leaves the
  Hierarchy as it was; the selected control in the views' key and forgotten with another rig; the tests on the editor's
  clock; the field named Puppet Control Size (beside the Preferences' Rig Control Size).

## Not done (round 347-348)
The chain starts and the knees and elbows as controls, controls of your own, and the per-limb FK/IK switch.

## Acceptance (`tests_round346`, round 281's test)
- The controls show with only the mesh selected; the foot's below its joint, the head's above; Puppet Control Size scales them.
- A click on the right hand's control selects it, a double-click pins it, another frees it; a drag poses.
- A click on the upper arm while AutoPosing keeps the mesh selected; Ctrl+click picks the bone.
