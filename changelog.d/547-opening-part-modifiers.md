## 2026-11-17 (round 547): A door's and a window's parts take modifiers - the leaf, the frame, the glass (task 0527, ADR 0149)

**Asked** (the owner, through the hub): "Why can't I add modifiers to windows and doors?", then "Lets do both" - the
opening's own stack (round 546) and its parts' (this round) - and "Yes, just option A. for the modifiers": a stack on
each part object. B, C and D are the spec's recommended defaults, not weighed by the owner: the bars and the sill board
are part of the Frame, the Frame has no collider, a click on a Frame or Glass selects its opening and a second click the
part.

**Changed (task 0527, ADR 0149; amends ADR 0129 and ADR 0136)**
- **A styled opening's Frame and Glass are objects**, as round 515's leaf is: children of the opening's piece, marked
  with the part mark (`BlockoutLeaf`, now with a kind - Leaf, Frame, Glass - a copy index and the material it was given;
  a scene of round 515 reads its leaves as Leaf). A window's Frame holds its frame, bars and sill board; a door's its
  jambs and head (the arch of an Arched door). A window's Glass holds its panes, one slab a light. Frame and Glass have
  their pivot at the opening's origin, the middle of the hole's bottom in the wall's plane.
- **The kernel makes only the clear hole** of a styled opening - one Subtract piece, a box or the arched / round outline
  pulled through the wall, down by the sill board's thickness when there is one (so the wall's strip below the hole is
  not coplanar with the board's top). A Revolving door keeps its ring and drum (the blocked-out space, not a part). The
  Solid and Glass faces of an opening leave the Blockout's mesh; the grid path meshes no frame, so a styled opening adds
  no "finer than Field Cell" note.
- **A part keeps its modifier stack** - its mesh is replaced, the object kept - through Width, Head, Sill, Frame Width,
  Style within the kind, Open Amount and a hand swing, duplicate, delete and undo, Bake and Unbake, save and open, and
  export (each part its own node with its evaluated mesh). A second leaf made by a style change (Single -> Double) takes
  a copy of the first leaf's stack. A part's own numbers (Frame Width and Depth, Bar Width, Glass Thickness, Leaf
  Thickness) are out of the Blockout's snapshot hash: changing them makes the part again and asks for no build. Enabled
  off, or numbers that make no plan, hide the Frame and Glass (inactive) instead of destroying them, so they come back
  with their stacks. A deleted Frame or Glass is made again (a Frame cannot be turned off: Frame Width is held at 1 mm
  at least).
- **Materials:** Frame takes the Solid class's material, Glass the Glass class's, a leaf the Leaf class's - the
  opening's override first, then the Blockout's; a material dropped on a part by hand is kept (compared by asset path).
- **Light and collision:** Frame static (Contribute GI on), no collider; Glass static, Contribute GI off; the leaf as in
  round 515.
- **Picking:** a click on a Frame or Glass selects its opening; a second click on it, with the opening the only
  selection, selects the part; a leaf selects itself. Box select maps a Frame or Glass to its opening. The Draw Piece
  and Room tools treat a part's face as its Blockout's surface. The part's Inspector line says it is made by its
  opening and that its modifiers are the user's.
- **Console:** `blockout part select [frame|glass|leaf]` selects the selected openings' parts, `blockout part opening`
  their openings.
- **The bench interior** (`benchscene blockout`) styles all its openings again in the 100-piece interior; the 400-piece
  one keeps six styled doors and six styled windows, because an arched or round hole still leaves unresolved places on
  the grid path.
- **Learn:** "Windows with a style" and "Doors with a style" gain a Try-it: Bevel the frame, Wireframe the glass,
  Solidify the leaf, change the Width - the modifiers stay.
- Round 552 (#437) is merged in: the leaf sync, and the Frame / Glass ids this round adds to it, tell scenes apart by
  serial.

**Checked:** the round's groups in three builds, round 552 merged in, 0 failed except one timing check noted below.
Windows without libraries: blockout 16,015, editor 3,200, lighting 9,825 (1 failed), physics 190, hci 1,849,
blockout_modifiers 1,023, blockout_join 511. Windows with libraries: 20,809 / 3,231 / 9,881 / 208 / 1,849 / 1,029 / 512.
Linux: 16,018 / 3,205 / 9,827 / 190 / 1,847 / 1,023 / 511. The one failure was round 507's "opening the baked scene
again the moment the bake lands brings in every page it baked" (test_lighting.cpp:12135) in the build without
libraries. It passed three reruns, the build with libraries and Linux; this round doesn't touch the bake's pages, so it
is timing, not this change. The blockout group has more tests (751 -> 794) but fewer checks (22,860 -> 16,018 on Linux):
- The window and door sweeps check one hole per opening, not the frame's and glass's kernel pieces.
- The hostile-numbers sweep was trimmed (17.8 s -> 3.1 s).
- Four tests of round 498's grid frame ("a frame a cell wide on the grid", the "finer than Field Cell" note and its
  advised cell) are gone, because the grid path meshes no frame now (spec criterion 7).

**Stress** (`--only blockout`, against round 503's reports, both builds): problems are 0 everywhere, as before. "Faces
newly on top of each other" lines are the same as round 503's: the wedge + Solidify 2 without libraries; the sphere +
Bevel, outline + Wireframe and cylinder + Decimate 2 each with libraries.
- **Fewer faces on top of others, because the frame left the wall's mesh.**
  - Styled windows on the exact path: 65 -> 0, faces of no area 4 -> 1, unresolved places 123 -> 0.
  - Styled doors on the exact path: 70 -> 0, faces of no area 1 -> 0, unresolved 85 -> 0.
  - Styled windows on the grid (cell 0.08): 3 -> 0, faces of no area 15 -> 5, unresolved 8,289 -> 2,477.
  - Styled windows on the grid (cell 0.1): 120 of 210 built clean, was 113.
- **One count rose:** styled doors on the grid (cell 0.08), unresolved places 49,423 -> 50,008 (+1.2%). A door
  now cuts one clear hole through the wall instead of carrying the jamb and head pieces. That hole's outline (arched
  ones included) crosses the grid's cells in a few more places. These are cells the grid path leaves unresolved, not
  problems.
  Faces of no area there fell 18 -> 17.
- **Field cell 0.08:** any pieces, and hostile meshes, have one fewer face of no area each (59 -> 58, 31 -> 30).
- **The 109-piece interior:** 17,254 faces, was 17,590 (libraries).
- Times moved both ways within the run-to-run spread of a shared machine. The live drag rows stay OVER, as in round 503.

<!-- README -->
replace: door leaves that are objects and swing about their hinge without a rebuild (round 515),
with: door leaves that are objects and swing about their hinge without a rebuild (round 515), **a door's and a window's parts - leaf, frame, glass - each an object that takes its own modifiers** (round 547),

<!-- docs/USABILITY.md -->
after: | Opening a door rebuilt the wall at every angle, and an open leaf cut whatever stood in its swing. (round 515)
| A door's or window's frame and glass took no modifiers - they were faces of the Blockout's mesh - and "Why can't I add modifiers to windows and doors?" had no answer but Bake to Mesh. (round 547) | The Frame and the Glass are objects like the leaf: each takes its own stack, which stays through every change of the opening's numbers, save and export. |

<!-- docs/ARCHITECTURE.md -->
after: | Door leaves as objects
| Opening parts as objects | `BlockoutLeaf::kind`, `blockout_part_plans`, `blockout_opening_cut`, `Editor::blockout_sync_leaves` (ADR 0149) | A styled opening's Frame and Glass are objects made like its leaf (the mesh replaced, the object and its stack kept); the kernel makes only the opening's clear hole (round 547) |
