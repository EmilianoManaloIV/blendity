# 0137: MMD motions (VMD)

- **Status:** done
- **Requirements:** R-13
- **Decisions:** ADR 0047
- **Model:** main session
- **PR:** (stacked on task 0136)

## Goal
A `.vmd` dance or motion dropped onto an imported MMD character plays on it: its bone keys, its faces and its IK
switches, as an ordinary clip the Animation window edits.

## Scope
- In: an own reader for VMD (bone, morph, IK-show records; camera, light and shadow records counted and skipped).
- In: bones found by MMD's names through `mmd_bone_name`, so a motion fits any model with the standard bones.
- In: the conversion of MMD's keys (a turn in the bone's rest frame, no rest orientation) into Blendity's local
  rotations: `R_parent^-1 · C(q) · R_bone`, and positions `rest + R_parent^-1 · C(t) · 0.08`.
- In: MMD's Bezier timing curves as the keys' Hermite slopes; a run the curves can't follow within half a degree or a
  millimetre (a large turn, an Euler wrap) keyed every frame from the true slerp.
- In: morph keys as `MeshRenderer.BlendShape/<name>` tracks on every mesh of the character with that key.
- In: IK off and on as `IKConstraint.Weight` keys on the constraint aimed at that IK bone.
- In: the editor: `Assets/Animations/<character>/<motion>.anim`, the character's Animation component playing it;
  drop, File > Import and the Project window's double-click route `.vmd` the same way; the character is the
  selection's, else the scene's only one.
- Out: the motion's camera and lights; inherited-rotation bones (their keys land on the bone itself).

## Acceptance criteria
1. A synthetic motion on the synthetic MMD character: a 90-degree turn of the hips on a straight curve within 0.6
   degrees at every checked frame; a 170-degree ankle turn, eased, half done at its midpoint and exact at its end.
2. The centre's move comes out 0.2 m towards +Z; a morph reaches 0.5 halfway; the leg's IK weight 1 then 0.
3. A bone the character hasn't is reported, not dropped silently.
4. Dropped into the editor: the clip asset is made and played by the character.

## As built
- `scene/import_vmd.cpp`; `Editor::import_motion_file`, `motion_target` (`editor/assets.cpp`).
- Tests: round 157.
- After the review:
  - each track's keys are built once, in frame order, and assigned whole (a dance's thousands of keys a bone were
    inserted one by one); the key count is in the Console;
  - a densified run settles the key it ends on from its own chain of Euler angles (no flick into it);
  - a bone keyed at its rest gets no track (the local rest compared, not the root-relative one: a constant track on a
    hair bone fought its spring);
  - a control point on a curve's end leaves the slope to the other one;
  - the bone of that name is preferred to a mesh or an empty of it;
  - the `.vmd` is copied in beside its clip; the dialog is "Import Model or Motion";
  - the tests give the centre's X and Z different curves (a transposed read would show), check Play turns the hips,
    and take the editor's objects before Play (Play restores the scene);
  - ADR 0047 records the conversion.
