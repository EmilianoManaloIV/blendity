# 0259: rigging and weight paint - the pen's eraser and Ctrl subtract, a skeleton field for the skin

- **Status:** done (round 279)
- **Requirements:** R-14
- **Decisions:** ADR 0085 (the area rounds)
- **Model:** main session
- **PR:** #224

## Asked
The HCI pass (ADR 0085), the rigging round: Weight Paint's Add brush ignored the pen's eraser end and Ctrl (every other
brush in the editor subtracts with them), so taking weight away meant switching brushes; binding a mesh to a skeleton
needed a two-object selection made by Ctrl+click in the Hierarchy, which the Rigging window could only describe; the skin
solve's progress had a bar of its own (round 273's tray took it). Nielsen's H4 (consistency), H6 (recognition over
recall), the pen.

## What changed
- **Weight Paint subtracts with Ctrl and with the pen's eraser** while the Add brush is active (`wp_effective_mode`,
  decided once when the stroke begins, so a stroke never jumps between adding and subtracting): the eraser as Paint's,
  Ctrl as the Subtract brush; the hint and the brush help say so.
- **A Skeleton field for the skin**: with a mesh selected and no skeleton in the selection, the Rigging window's Skin
  section offers a dropdown of the scene's skeletons (every object carrying an Armature - what a Skin modifier names,
  wherever it sits) and enables Add Armature (Skin) for the one chosen; the pick belongs to the mesh it was made for;
  the Ctrl+click way still works and wins when a bone is selected.

## Tests (`tests_round279`)
- `wp_effective_mode`: the Add brush with Ctrl held, or with the pen's eraser, paints as Subtract; Subtract, Draw and
  Blur are left as they are.
- The Cube rigged, the Sphere selected alone: the Skin section's Skeleton dropdown is drawn and, the Armature picked
  through it, a real click on Add Armature (Skin) gives the Sphere a Skin modifier naming the Armature, vertex groups
  from the bind, and moves nothing (the Armature's and the Sphere's parents unchanged, the Cube still skinned).

## Not done
- The Rigging window's grid buttons give no reason when disabled (the grid helper predates `button_enabled`): the row
  under Add Armature (Skin) says what to do instead.
- The skin job already shows in the tray (round 273); its own bar in the Rigging window stays as a second view.
- A Record / Play state saved in the clip when Play cancels a solve.
- Auto-Rig and the body solver's cancels on Play keep their log lines (toasts since round 272).
