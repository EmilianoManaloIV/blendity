# ADR 0149: A styled opening's frame and glass are objects, like its leaf, so each part takes a modifier stack

- **Status:** accepted 2026-11-15 (round 547, task 0527). The owner chose A ("Yes, just option A. for the modifiers":
  a stack on each part object); B, C and D were not weighed by the owner and are the recommended defaults (bars and sill
  board part of the Frame, no collider on the Frame, a click selects the opening and a second click the part) - the
  owner can change any of them.
- **Date:** 2026-11-15
- **Amends:** ADR 0129 (round 498's and 499's amendments: "a styled window is one piece that makes its own kernel pieces";
  the frame and the pane leave the kernel), ADR 0136 (the leaf's mechanism becomes the parts'); ADR 0138 unchanged

## Context
The owner asked why a door or a window takes no modifiers, and asked for both: the opening's own stack (round 546) and
its parts'. Round 515 made a door's leaf its own object (ADR 0136): a child of the door piece, pivot at the hinge, its
mesh made by the editor from the door's numbers and replaced - the object kept - when they change. The leaf can
therefore already carry an ordinary modifier stack. The frame, bars, sill board and pane cannot: round 498 makes them as
what a blank's carving leaves of the wall, so they are faces of the Blockout's mesh. A stack on the opening piece (round
546) runs on the cut; it cannot bevel the frame, which is wall that the cut left.

The grid path has a second reason to move them: a frame or a bar finer than the Field Cell is held at a cell or left
out, with a status note, and every styled opening leaves unresolved places (two corners in one cell) that stop faces
merging - round 503 had to style only twelve of the bench's 51 openings, because all 51 made its tests four times slower.

## Decision
1. **Parts are objects.** A styled opening has part objects, children of its piece, each marked with the part mark
   (round 515's `BlockoutLeaf`, given a `kind`: Leaf, Frame, Glass - a scene saved with round 515 reads its leaves as
   Leaf):
   - **Frame** (window and door): a window's frame, bars and sill board; a door's jambs and head (the arch of an Arched
     door). One object.
   - **Glass** (window): the panes, one thin slab a light. One object.
   - **Leaf** (door): as ADR 0136.
2. **The kernel makes the clear hole only.** A styled opening gives the kernel the hole through the wall at the frame's
   outline (an Arched or Round head included). A Revolving door's ring and drum stay in the kernel: they are the
   blocked-out space, not a part. The Solid and Glass faces of the opening leave the Blockout's mesh;
   `window_face_class` and `fit_to_grid` lose the frame, the bars and the pane.
3. **Pivots:** Frame and Glass at the opening's origin - the middle of the hole's bottom line, in the wall's plane, the
   opening's axes; the leaf at its hinge (ADR 0136).
4. **Made by the editor, kept by the object.** The part's mesh is made from the opening's numbers (`WindowShape`), in the
   part's own space, with the Blockout's texture scale; when the numbers its shape reads change, the mesh is replaced and
   the object kept - its components (modifiers, a material the user set, scripts) stay. A part's own numbers (Frame Width,
   Frame Depth, Bar Width, Glass Thickness, Leaf Thickness) are out of the Blockout's snapshot hash; only the hole's
   numbers are in it.
5. **Each part's stack is its own** (choice A1): an ordinary modifier stack on the part object, evaluated as on any
   object; export, the Unity bridge and Bake to Mesh read the evaluated mesh as for any object. When a style change makes
   a new leaf beside one that has a stack (Single -> Double), the new leaf takes a copy of it.
6. **The opening owns its parts:** they follow it (its wall, room, Blockout), are duplicated, deleted and undone with it,
   go with Bake (separate objects at the same world place) and come back with Unbake, as ADR 0136 decision 5 and round
   515's fixes (a part moved out or copied becomes an ordinary object; a part the opening did not make is never
   destroyed). A deleted Frame or Glass is made again (a styled opening always has a Frame: Frame Width is held at its least, 1 mm; a window's Glass off makes none).
7. **Materials:** a part takes its class's material - Frame the Solid class, Glass the Glass class, Leaf the Leaf class -
   the opening's override first, then the Blockout's; a material the user dropped on a part object is kept.
8. **Light and collision:** Frame static (Contribute GI on, its own lightmap UVs), no collider (choice C1: the wall's hole
   is what is walked through); Glass static, Contribute GI off; the leaf dynamic and kinematic (ADR 0136).
9. **Picking** (choice D1): a click on a Frame or Glass selects its opening; a second click on the same spot selects the
   part. A leaf selects itself.
10. **Old scenes:** a scene saved since round 498 / 499 opens with the parts made; those openings' faces move from the
    Blockout's mesh to the parts, the openings look the same. A scene from before round 498 has no styled openings and
    loads to the same bytes.

## Consequences
- The owner can Bevel a frame, Wireframe a pane into muntins or Solidify a leaf, and keep it through every edit; an
  engine gets each part as its own node.
- The grid path meshes a styled opening as a plain hole: no "finer than Field Cell" notes and no unresolved places from
  frames; the kernel has fewer pieces an opening (a window of 3 x 3 lights was 1 blank + 2 reveals + 9 lights + a board).
- The window and door stress sweeps of rounds 498 / 499 change meaning (the hole and the parts' meshes, not carved
  frames); their rows restart with this round.
- More objects in the Hierarchy (two or three an opening), folded under the opening.
- The frame and the wall touch in a plane as two objects (not one mesh): a seam a lightmap may show at a low resolution.

## Alternatives
- **Keep the frame in the kernel and run a stack on the opening's frame faces** - a stack would need a mesh of its own
  for the frame, which is exactly this decision done inside the kernel; and the grid would still mesh the frame.
- **One stack on the opening mirrored onto its parts** (choice A2) - one place to edit, but a new kind of component and a
  sync of components; Blender and Unity users expect the stack on the object they selected.
- **Bake to Mesh, then add modifiers** (today's workaround) - loses the numbers.

## Not decided here
- A window's sash that swings (a casement) - the leaf's mechanism would serve; not asked for.
- The opening's own stack on its cut (round 546, ADR of its own).
