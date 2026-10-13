# 0052: The Rigging window: skeleton, skin, vertex groups, weight painting and IK in one place

- **Status:** done (as built below)
- **Requirements:** R-02, R-03
- **Decisions:** none needed (a new window kind, the existing pattern)
- **Model:** main session (Opus)
- **PR:** https://github.com/EmilianoManaloIV/blendity/pull/49

## Goal
The user asked (2026-10-12): "rigging and weight painting tools should be in their own tab. The mesh filter area is
getting a little crowded". Everything for bones, skins, vertex groups, weight painting and IK moves into a
**Rigging** window. The MeshFilter section of the Inspector keeps only modeling.

## Scope
- In:
  - **`WindowKind::Rigging`** (Window > Rigging): appended before `Count`, so saved layouts keep their numbers.
  - **Fold-out sections:**
    - **Skeleton**:
      - Add Armature.
      - **Add Bone**: a child at the selected bone's tip.
      - Rig > Humanoid / Quadruped.
      - **Reset Pose** and **Apply as Rest**.
      - Show Bones, In Front.
    - **Skin**:
      - Add Armature (Skin) to the selected mesh, naming the selected skeleton.
      - Bind: Automatic Weights.
      - Max Influences, Deform (Linear / Dual Quaternion).
    - **Vertex Groups**: the list, weight, Assign / Remove / Select / Deselect, as before.
    - **Weight Paint**: the toggle and the brush.
    - **IK**: Add IK to the selected bone (a target at its tip and a pole are made), Auto IK.
  - **Selecting a bone picks its group.** While painting, selecting a bone makes the group named after it the painted
    one (Blender's behaviour).
  - **The MeshFilter section** loses Vertex Groups and Weight Paint. One row, "Vertex Groups, Weight Paint: Rigging...", opens
    the window.
  - **Console** for the new buttons: `ik add <bone>` (making its target), and `bone add` already exists.
- Out: the auto-rig section (task 0058), inpainted weights (task 0057).

## Acceptance criteria
1. `window Rigging` opens it; a saved layout with the Rigging tab loads back.
2. Add Bone by a click adds a child at the selected bone's tip.
3. Reset Pose and Apply as Rest by clicks do what the console verbs do.
4. Weight Paint from the window paints like before; clicking a bone while painting switches the group to the bone's.
5. The MeshFilter section has no vertex groups or weight paint; its Rigging row opens the window.
6. Add IK by a click on a selected shin makes a target and a pole, and moving the target poses the leg.

## As built
- **The buttons act on the objects**, not by name, so two characters with the same bone names don't mix: Add Bone,
  Reset Pose, Apply as Rest, Add IK and Auto IK. The console verbs stay for scripts; `ik add` with no bone uses the
  selection.
- **The window works on the selection as a whole.** The mesh is the one being painted, else a selected mesh. The
  skeleton is a selected bone's or armature's, else the mesh's Skin's. Add Armature (Skin) wants both selected
  (Ctrl+click in the Hierarchy).
- **Its fields have their own ids**, apart from the Inspector's and from each other (the IK's Weight and the
  brush's Weight).
- **Add IK on a forearm** (found by its name) puts the pole behind; on anything else, in front.
- **Tests click the window's buttons** through a recording of each frame's button rectangles (`ui::Context::record_buttons`).

## Tests and checks to run
- Unit tests (round 74) with real clicks; full suite, verify mode, Linux, sanitizer.
