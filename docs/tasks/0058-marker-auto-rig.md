# 0058: Marker auto-rig, humanoid and quadruped (Mixamo-style)

- **Status:** done (as built below)
- **Requirements:** R-06
- **Decisions:** ADR 0028 (part 2: the marker auto-rig)
- **Model:** main session (Opus)
- **PR:** (link when opened)

## Goal
The user asked (2026-10-12) for "a Mixamo-styled automatic weight paint" with "the markers and inpainted weights", and
"support for quadruped auto rigging as well". Mixamo asks for a few markers on the character (chin, wrists, elbows,
knees, groin), builds the skeleton from them and weights it automatically.

## Scope
- In:
  - **Rigging > Auto-Rig**: pick Humanoid or Quadruped with a mesh selected. The Scene view turns to an orthographic
    front view (a quadruped: its left side) framed on the mesh; a diagram in the Rigging window shows the markers and
    which one is next.
  - **Placing**: click on the mesh. The marker sits at the middle of the first part the click passes through (where
    the ray enters and leaves it), so joints land inside the body. Symmetry (on by default) mirrors left and right
    across the mesh's middle. A placed marker can be dragged again; Remove Last takes the last one back (markers are
    the tool's own state, not the scene's, so they aren't undo steps).
  - **Markers**: humanoid (Mixamo's): Chin, Left/Right Wrist, Left/Right Elbow, Left/Right Knee, Groin. Quadruped:
    Nose, Skull (the base of the skull), Withers, Hips, Tail Base, Tail Tip, four Paws, two front Knees, two hind Hocks.
  - **Build Rig**: the skeleton with `create_rig`'s bone names (so the presets, Auto IK and `limb_of` keep working),
    IK on each limb, a Skin modifier on the mesh, and task 0057's inpainted weights (in the background on a large
    mesh). One undo step for the rig and the bind (a large mesh's bind lands later, as its own step).
  - Console: `autorig humanoid|quadruped`, `autorig mark <marker> <x> <y> <z>`, `autorig build`, `autorig cancel`,
    `autorig symmetry on|off`.
  - A Learn section in the Bones and skins lesson.
- Out: fingers and face rigs; markers from a picture alone.

## Acceptance criteria
1. Markers on a code-built mannequin and a code-built four-legged body give the named bones at the expected places
   (within 2 % of the height).
2. Every vertex is weighted (each sums to 1).
3. Posing a forearm moves the hand, not the hip; posing a front leg moves its paw, not the head.
4. Clicking on the mesh in the front view puts a marker inside the body (mid-depth), and Symmetry mirrors it.
5. Undo removes the rig and the bind.

## As built
- **Poles from the markers' bend**: each limb's IK pole points the way its joint bends (an elbow in front of the line
  from the shoulder to the wrist, a dog's hock behind the hip-to-paw line), so Build Rig leaves every bone at its rest.
  A straight limb gets a 2 % bend towards create_rig's side (knees forward, elbows back, front knees back, hind hocks
  forward): IK reaches just short of a straight limb and would otherwise bend it by itself.
- **Left and right by the body**: a person faces +Z; an animal from its tail to its nose. Markers placed on the wrong
  side swap, so an animal modelled facing -Z still gets its left legs on its left.
- **Refused**: markers no body has (a groin above the chin, a paw above its knee) and names that aren't markers.
- **The hips** sit 80 % of the way from the middle to each knee, and the ankles on the hip-to-knee line just above the
  floor, so a wide stance keeps them inside the body.
- **The session**: Weight Paint and Edit Mode end when it starts; the move gizmo steps aside; Esc ends it (not while a
  G / R / S or a text field has the keyboard); New Scene, Open Scene and Play cancel it; a double-click doesn't frame
  the view. Starting again on a mesh just rigged brings its markers back.
- **Undo**: on a small mesh the rig and its bind are one step ("Auto-Rig"); a large mesh's bind lands later as its own.

## Tests and checks to run
- Unit tests (round 82); full suite, verify mode, Linux, sanitizer.
