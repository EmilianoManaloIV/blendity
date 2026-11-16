# 0527: Modifiers on a door's and a window's parts - the leaf, the frame, the glass (round 547, Pass 27)

- **Status:** approved 2026-11-15 (the owner, through the hub: "Yes, just option A. for the modifiers"); B, C and D are
  the recommended defaults (the owner did not weigh them and may change them)
- **Requirements:** R-02 (modeling), U-01, C-02, R-09 (export to engines)
- **Decisions:** ADR 0149 (new: a styled opening's frame and glass are objects, like its leaf); amends ADR 0129 and
  ADR 0136
- **Model:** main session (cross-cutting: kernel pieces, sync, picking, materials, export) with the implementer for the
  sync's generalisation and the tests; test-engineer; code-reviewer; the HCI worker reviews picking and the Inspector
- **PR:** its own, base `main` (stacked on round 546's if that is open when this one publishes)

## Goal
The owner asked: "Why can't I add modifiers to windows and doors?", then "Lets do both" - the opening's own stack (round
546, the modifiers worker) and its parts' (this round). After it, a door's leaf, a door's or window's frame and a
window's glass are each an object that takes an ordinary modifier stack - Bevel the frame, Wireframe the glass into
muntins, Solidify or Bevel a leaf - and the stack stays through every rebuild (Width, Head, Style...), Open Amount and the
swing, Bake / Unbake, save and open, and export.

## What is there today (main 068207d9)
- **The leaf is already its own object** (round 515, ADR 0136): a child of the door piece with its pivot at the hinge.
  When its shape's numbers change, the object is kept and only its MeshFilter's mesh is replaced
  (`Editor::blockout_leaf_made`, `blockout_editor.cpp` ~2586; the key check at ~2674) - so components the user adds stay.
  A leaf is not a Blockout piece, so the Inspector already lets it take modifiers, and export already writes each
  object's evaluated mesh (with its stack: `editor.cpp` ~3558, `evaluated_mesh_shared`). Nothing pins any of this yet.
- **The frame, bars, sill board and pane are faces of the Blockout's mesh** (round 498, `blockout_object.cpp` ~742):
  a window is a blank (Add) that Subtract pieces carve - the reveals and one light a cell - so what is left in the wall is
  the frame, the bars and the pane. A door's frame is the same. They cannot take a stack: they are not objects, and the
  opening piece refuses one ("a door or window is a cut sized by its Width, Head and Sill",
  `blockout_piece_stack.cpp:316`) - the refusal the owner met, which round 546 lifts for the opening itself.

## Scope
- In:
  1. **The leaf takes a stack, pinned.** Tests (real events where the user would act) that a Bevel / Solidify / Wireframe
     on a leaf survives the leaf being made again (Width, Leaf Thickness, Hinge Side, Style Single -> Double -> Single),
     Open Amount and a hand swing (the stack does not count as a turn, the swing does not re-run the stack), Bake /
     Unbake, duplicate, save / open, undo, export (glTF, FBX, OBJ: the node's mesh is the evaluated one). Fix what the
     tests find (e.g. the collider fit, the leaf key, the hand-turn reader).
  2. **The frame and the glass become objects** (ADR 0149), made, kept and named as the leaf is:
     - **Frame** - a window's frame, bars and sill board; a door's jambs and head (Arched: the arched head). One object,
       its pivot at the opening's origin (the middle of the hole's bottom, in the wall's plane), its mesh made from the
       opening's numbers by the editor (not the kernel), in the Solid class's material.
     - **Glass** - a window's panes (one quad slab a light), its own object at the same pivot, in the Glass class's
       material.
     - The kernel then makes only the opening's **clear hole** through the wall (the frame's outline; a Revolving door
       keeps its ring and drum - the blocked-out space, not a part).
     - The parts follow the opening's numbers (made again when they change, object and components kept), move with it,
       are duplicated, deleted and undone with it, go with Bake / come back with Unbake, and are converted on open for
       scenes saved since rounds 498 / 499 (the mesh of those openings changes, the opening looks the same).
  3. **A part's stack survives the part being made again** - the leaf's mechanism (replace the mesh, keep the object),
     for Frame and Glass too. The acceptance test the hub asked for: a Bevel on a window's Frame survives Width changing.
  4. **Picking and the Inspector (HCI):** a click on a Frame or Glass selects its **opening** (what the user edits most:
     Width, Style); a second click on the same spot selects the part itself (Unity's prefab convention); the leaf keeps
     selecting itself (it is swung by hand). A part's Inspector line says "Made by Window 'X' - its shape follows the
     window's numbers; its modifiers are yours". Console: `blockout part select [frame|glass|leaf]` selects the selected
     openings' parts, `blockout part opening` their openings.
  5. **Materials:** the Blockout's Solid / Glass / Leaf materials and an opening's own overrides go on the parts' objects
     (as round 515 does for the Leaf); a material dropped on a part by hand is the user's and is kept.
  6. **Light and collision:** Frame is static (Contribute GI on, a Box collider - or none, choice C); Glass is static,
     Contribute GI off (it is a transparent sheet); the leaf as now (dynamic, kinematic).
  7. **Learn:** "Windows with a style" and "Doors with a style" gain a Try-it: Bevel the frame, Wireframe the glass,
     Solidify the leaf, then change the Width - the modifiers stay.
- Out: the opening's own stack (round 546); casement windows that swing (a window's sash as a leaf - the same mechanism,
  not asked); a modifier stack applied to the wall around the opening; per-part UV editing.

## Choices for the owner (with this spec; recommended first)
- **A. Where the parts' stacks live:** (1, recommended) on each part object - ordinary Unity / Blender behaviour, export
  and the bridge need nothing new; a Double door's two leaves each have their own (the second leaf of a style change
  copies the first leaf's stack). (2) one stack on the opening for each part kind, mirrored onto its parts - one place to
  edit, but a new kind of component and a sync of components.
- **B. The window's bars and sill board:** (1, recommended) part of Frame - one Bevel bevels the whole frame. (2) their
  own objects (Bars, Sill) - finer control, more objects.
- **C. Frame collision:** (1, recommended) none - the wall's hole is what a player walks through and the frame sits in
  it; (2) a Box collider fitted to the frame (it would block the doorway's sides only by its thickness).
- **D. A click on a frame or glass:** (1, recommended) selects the opening, a second click the part; (2) selects the part
  at once (as the leaf does) - quicker to the stack, slower to the Width.

## Likely files
- `src/scene/blockout_object.h/.cpp`: the part mark (round 515's `BlockoutLeaf` with a `kind`: Leaf / Frame / Glass; old
  scenes read as Leaf), part plans and meshes for Frame and Glass (from `WindowShape`), `blockout_window_pieces` /
  `WindowMaker` down to the clear hole for styled openings (the Revolving ring and drum stay), `window_face_class` and
  `fit_to_grid` simplified (no frame or pane in the kernel), the snapshot hash (part-only numbers out of it), old-scene
  conversion.
- `src/editor/blockout_editor.cpp`: `blockout_sync_door` -> one sync for every styled opening's parts; Bake / Unbake; the
  status lines (the "frame finer than Field Cell" notes go); the console.
- `src/editor/scene_pick.cpp` (a part's click -> its opening, a second click -> the part), `src/editor/panels.cpp` (the
  part's Inspector line), `src/editor/editor.h`, `src/editor/lessons.cpp`, `src/scene/scene.cpp` (registration if the
  mark is renamed).
- `tests/test_blockout_parts.cpp` (new; `test_blockout.cpp` is at MSVC's section limit); edits in place to the window and
  door tests of rounds 498 / 499 / 515 / 503 that pin the frame or pane in the Blockout's mesh;
  `stress/stress_blockout.inc` (the styled window / door sweeps measure the clear hole and the parts' meshes).
- Docs: ADR 0149, ADR 0129 and 0136 pointers, this spec, `changelog.d/547-opening-part-modifiers.md`.
- **Not touched:** `src/scene/blockout_piece_stack.*` (round 546's); `adjust_last.cpp` beyond a line if a part's F9 needs one.

## Acceptance criteria
1. A styled window has a Frame and a Glass object (Opening Only: none); a styled door has a Frame and its leaves; each
   upright, its pivot where ADR 0149 says, in the class's material; the Blockout's mesh has the clear hole and no Solid
   or Glass faces of the opening (a Revolving door keeps its ring).
2. A Bevel on a window's Frame, a Wireframe on its Glass and a Solidify on a door's leaf survive: Width, Head, Sill,
   Frame Width, Style (within the kind), Open Amount, a hand swing, duplicate, delete + undo, Bake + Unbake, save + open;
   each check compares the part's evaluated mesh with the stack applied by hand to the new base mesh.
3. Changing a part's modifier asks for no Blockout build; changing the opening's numbers makes the parts again within
   the same frame's sync and the Blockout's build only for the hole.
4. Export (glTF, FBX, OBJ; the Unity bridge where the suite covers it): each part is its own node with its evaluated
   mesh, pivot and material; the Blockout's node has the hole.
5. Picking: a click on a frame selects the window; a second click the Frame; a click on a leaf the leaf (real events).
6. An old scene with round 498 / 499 openings opens with parts made and the openings looking the same (pixel test or
   bounds); a scene from before round 498 opens to the same mesh bytes.
7. The grid path: a styled opening adds no "finer than Field Cell" note and no unresolved places from its frame (the
   frame is no longer meshed on the grid) - the bench interior can style all its openings again (round 503's item 4).
8. The round's groups (blockout, editor, hci, lighting, physics, blockout_modifiers, blockout_join) on the three builds;
   the coverage map; the stress section - the styled window and door rows change (stated with the reason), the others
   the same row for row.

## Tests and checks to run
- Unit: the new file's tests, then the round's groups on Windows (both builds) and Linux.
- Stress: `--only blockout` - the window / door sweeps' new numbers explained; every other row the same.

## Risks and edge cases
- **The seam between the wall's hole and the frame:** two objects touching in a plane - not a problem for rendering or
  export; the lightmap bake treats the frame as its own object (its own lightmap UVs).
- **Old scenes:** the openings' meshes change (faces move from the Blockout's mesh into the parts), the scene looks the
  same; a user's material on the Blockout's Solid / Glass class follows to the parts.
- **Many objects:** a building of 50 windows gets 100 more objects (folded under their openings); the sync's cost per
  walk (round 515's early return for openings with nothing to do).
- **A part renamed, moved out, copied, deleted by hand:** as round 515's leaf (ADR 0136 decision 5 and its fixes): moved
  out or copied - an ordinary object; deleted - the opening makes it again (a Frame cannot be "turned off": Frame Width 0
  makes none).
- **Round 546's opening stack** works on the opening's cut; this round makes the cut a plain hole - the two compose (546's
  stack on the hole, this round's on the parts). Sequencing: whichever lands second merges the other.
- **Scale** (ADR 0135): parts follow the numbers, their object scale stays 1.

## Documentation to update
`changelog.d/547-opening-part-modifiers.md` (README line; USABILITY: "a door's or window's frame and glass took no
modifiers" -> fixed; ARCHITECTURE: the part objects; PERFORMANCE: the grid path's styled openings), ADR 0149, ADR 0129 /
0136 pointers, this spec marked done.
