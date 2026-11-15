# 0496: Modifiers on a Blockout piece (round 514)

- **Status:** done (round 514, 2026-11-15, on main after rounds 498-502; presets added to the pieces that take no stack after
  the merge of round 500). Approved 2026-11-14 with ADR 0138 (the owner, through the hub: "Approve 514 and 515"; the three
  choices below as recommended).
- **Requirements:** R-02, R-03 (modeling, modifiers), Q-06 (responsiveness), U-01, C-02, R-09
- **Decisions:** ADR 0138 (new: modifiers on a Blockout piece); ADR 0129 and 0128 amended by it; round 502 (task 0486)
- **Model:** main session for the kernel and job parts, implementer for the Inspector and console, then test-engineer,
  then code-reviewer
- **PR:** its own, base `main`

## Goal
The owner (2026-11-14): "Yes, per piece modifiers would be a great implimentaiton to add." A selected Blockout piece has
a Modifiers section in its Inspector like an object's: Bevel just the arches, an Array of ten columns from one, a
Mirror on one wing - the stack runs on that piece's own shape before it is added to or cut from the rest, live, and the
rest of the Blockout keeps its exact numeric pieces.

## Scope
- In:
  1. A piece's object carries ordinary modifier components (ADR 0138 decision 1); the Inspector shows a selected piece's
     Modifiers section with Add Modifier, the header toggles, reorder, Duplicate, Copy to Selected, Remove - as an
     object's. The modifiers a piece does not take (ADR 0138 decision 4) are shown off in its Add Modifier list with the
     reason in the tooltip; a room, a door or window and an Intersect piece show the section with one line saying why
     it takes no stack (no hidden feature: the reason is on screen, not only in the console).
  2. The snapshot hands the build job each stacked piece's numbers and a clone of its modifiers; the job makes the
     piece's shape as a mesh (the kernel's own shape code), runs the stack, and passes it to the kernel as a mesh piece
     with the piece's operation; evaluated meshes are cached by a key of the numbers and the stack (not the place).
  3. The Blockout's stack (round 502) runs after, over the combined result.
  4. Handles and the wire on the piece's numbers; faces of the stacked mesh name the piece.
  5. Bake to Mesh / Unbake keep the piece stacks; export and Generate Lighting read the result.
  6. Console: `component <Type>` on a selected piece adds a modifier (as on any object); `set <Type>.<Field> <value>`
     sets a number; `blockout info` names the stacked pieces and what each costs. Every action of the section is
     reachable by keyboard per ADR 0145 (the HCI stream's keyboard-reach rule): Tab / arrows through the section,
     Enter on Add Modifier opens the menu, the menu by arrows.
  7. A Learn section "Modifiers on a piece" (Bevel the arches; Array a column ten times; Mirror one wing; why a piece
     with a stack is slower and less exact; three questions).
  8. Stress rows: `interior 100` with a column carrying Bevel, and with a column carrying an Array of 10 - the time
     from a change of the column until the mesh can be drawn (budget **100 ms**), a drag of another piece (frames,
     budget **16.7 ms**), a drag of the column itself (re-uses the evaluated mesh: no stack evaluation while it moves),
     a change of the Bevel's width (one evaluation); and a sweep of stacked pieces (each allowed modifier on each shape
     that takes one, added and subtracted, in a small building): problems 0, faces newly on top of each other counted.
- Out: stacks on rooms, doors / windows and Intersect pieces; Apply on a piece's modifier (refused with the reason:
  Bake to Mesh first); new modifiers; a stack that changes the piece's numbers.

## Acceptance criteria
1. With real events: select a column piece, Add Modifier > Bevel in its Inspector: the Blockout's mesh shows the column
   bevelled, the other pieces unchanged face for face; the same by `component BevelModifier`. Undo removes it in one
   step; redo brings it back; save / open keeps it; Ctrl+D on the piece duplicates it with its stack.
2. An Array of 10 on one column gives ten columns in the result, in the piece's own axes, added (or cut, for a
   Subtract piece); a Mirror on one wing's piece mirrors it across the piece's own plane.
3. Moving or turning a stacked piece does not evaluate its stack again (a counter); changing one of its numbers or a
   modifier's field evaluates it once; the stack runs on the build's worker only (no main-thread evaluation counted).
4. Refusals: each modifier not taken is off in the piece's Add Modifier list with its reason; a stack on a room, door,
   window or Intersect piece (from the console) stays inert and the Blockout's status names the piece and why; a stack
   whose result is not closed leaves the piece skipped and the status says so.
5. A piece with no modifier builds bit for bit as before (the whole `blockout` group unchanged); a scene from before this
   round opens and builds the same, except pieces that already carried a (previously inert) modifier - named in the
   changelog.
6. The Blockout's stack (round 502) runs after the piece stacks (a Mirror on the Blockout mirrors the bevelled column).
7. Handles drag the stacked piece's numbers; a click on a face made by the piece's stack selects the piece.
8. Bake to Mesh then Unbake keeps every piece stack; OBJ / FBX / GLB hold the stacked result.
9. Stress: the rows of scope 8 within their budgets (a miss is reported, not hidden); the sweep's problems 0.
10. The round's groups and `editor` on the three builds; the coverage map; `changelog.py check`; `test_shards.py --check`.

## Likely files (to sequence with Pass 23's worker - the kernel and Blockout files are theirs)
- **Pass 23's files, regions needed:**
  - `src/scene/blockout.h` / `blockout.cpp`: one public function, `bool piece_shape_mesh(const Piece &, Mesh &)` - the
    solid `local_box` / `local_cylinder` / ... already make, as a closed mesh in the piece's space (about 30 lines).
  - `src/scene/blockout_object.cpp`: `BlockoutPiece::to_piece` (~lines 400-450) - a stacked piece becomes a mesh piece
    whose mesh the job fills; `blockout_snapshot` / `BlockoutSnapshotNotes` - the stacked pieces' modifiers cloned into
    the notes; `blockout_snapshot_hash` - the stack's hash in the key.
  - `src/editor/blockout_editor.cpp`: `blockout_start`'s worker lambda - one call that evaluates the job's piece stacks
    before `blockout::build` (the cache returned with the job, as `cache` is); `blockout_status_lines` - one call line
    for the stacked pieces' notes and refusals.
  - `tests/test_blockout.cpp`: none; `stress/stress_blockout.inc`: none.
- **New files (mine):** `src/editor/blockout_piece_stack.cpp` (the job's evaluation, the cache, the refusals, the
  Inspector's piece section body), `tests/test_blockout_piece_modifiers.cpp` (group `blockout_piece_modifiers`, in the
  last CI shard), `stress/stress_blockout_piece_modifiers.inc`, `changelog.d/514-blockout-piece-modifiers.md`.
- **Shared, small:** `src/editor/panels.cpp` (the modifier section shown for a piece and its Add Modifier list
  filtered: two call lines in the section round 502 changed), `src/editor/editor.h` (one block), `lessons.cpp` (one
  lesson), `tests/test_main.cpp` / `stress/stress_main.cpp` (2 lines each), `tools/dev/test_shards.py` (one name).

## Choices for the owner (answered 2026-11-14: all three as recommended)
1. A piece's stack lives on the piece's object as ordinary modifiers (recommended: everything an object's stack has comes
   with it) - or a separate list inside the piece.
2. At first no stacks on rooms, doors / windows and Intersect pieces (recommended: each needs kernel work of its own) -
   or include rooms now (their shell would become a mesh: slower, and their walls no longer merge exactly).
3. A modifier already saved on a piece (inert until now) starts working when the scene is opened (recommended, named in
   the changelog) - or is ignored until the user touches it.

## Risks and edge cases
- A stack that opens the surface (Decimate, Wireframe on an open result) - refused by the kernel's mesh-piece rules,
  the piece named. NaN / huge numbers in a modifier: a refused piece, never a broken mesh.
- Cost: a high Subdivision level or an Array of thousands - the stress rows and a status line with the piece's face
  count; the kernel's existing limits for mesh pieces hold.
- Undo during a job evaluating a stack; a piece deleted while its stack is evaluated (the job's result dropped as any
  stale build's - round 502's fix keeps builds landing).
- Texture coordinates: a stacked piece's faces take the same planar projection as other faces; a curved result's
  faces shade smooth by the Auto Smooth angle.

## Documentation to update
`changelog.d/514-blockout-piece-modifiers.md` (README feature line, ARCHITECTURE row for `blockout_piece_stack.cpp`,
USABILITY row); ADR 0138 accepted; ADR 0129 and 0128 amended (a line each pointing to 0138); this spec marked done.
