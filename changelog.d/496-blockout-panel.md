## 2026-11-13 (round 496): the Blockout's Draw Piece tool - one Shape list, a preview that is the piece, scale that never shears (task 0480, ADR 0135)

**Asked** (the owner, 2026-11-12, through the hub): "Improve HCI for the blockout pieces, really looks crowded to work
with. And is confusing between selection and drawing." - "I feel that having the Draw: and Operation: tag with so many
options is too much." - "I also don't think it should be included in the main board on the top next to "center"
"global" etc." - "Scaling for the block out objects is weird, especially when I want to scale the object using the
local axis." - "It sheared." - "Drawing an arch, stair, or other draw method needs a preview." The owner chose "A and B
together" of the proposal, and for the direction: "The default is fine, objects are parametric anyways."

**Changed - the panel:**
- **The Draw Piece tool's button is off the Scene view's top toolbar.** The tool is in the Modeling Tools window's
  Blockout section, on Shift+R, in the menu and in the console (`tool room`).
- **One Shape dropdown** of the nine things the tool draws - Room, Round Room, Outline Room; Box, Cylinder, Outline;
  Wedge, Stairs, Arch - in place of the two footprint ticks and the Draw: row. The switch reads "Draw Piece (Shift+R)"
  and is lit while on. Room Wall Thickness shows only for a room shape.
- **No Operation row.** Pulled out adds, pushed in subtracts; **Shift held at the click makes the other one**, and the
  label beside the mouse says which ("height 0.500 m  Subtract  (Shift: Add)"). A room has no operation.
- **F9's Operation row shows words** (Add, Subtract, Intersect); Intersect is reached there and from the console.
- A console operation that sticks (`blockout roomopt operation ...`, the old `subtract on`) shows one dim line,
  "Operation: Intersect (console) - click to clear". Choosing a room shape forgets it. Every older `blockout roomopt`
  word still works and moves the dropdown.

**Changed - scale (ADR 0135):**
- **Scaling a piece changes its own numbers** along its own axes (the Scale handles, S with or without an axis): a
  box's Width / Height / Depth, a cylinder's radius and height, an outline's points... Its transform's Scale stays as
  it was (1 1 1 for a piece never scaled). A cylinder and a sphere stay round. A room keeps its wall's thickness.
- **Scaling a Blockout, a folder in it or a parent piece scales what is under it** - places and numbers - instead of
  its own Scale, so a turned piece inside is not sheared: it grows along its own axes by how much each of them grew.
- A Scale an older scene left on a piece is folded into its numbers at its first scale, or by **Apply Scale to Size**
  in its Inspector. A Blockout or folder saved with an uneven Scale shows "scaled unevenly: turned pieces are sheared"
  and a button, **Apply Scale to Pieces** (`blockout applyscale`), that folds it once. A fold keeps the shape in the
  world: rooms' walls, floors and ceilings, Blend radii and the Texture Scale take the Blockout's own factor; a round
  piece under an uneven scale keeps what is left of it on its own Scale (a piece's own Scale never shears).
- Numbers a scale makes stop at 10,000 m (F9's limit). With the Scale or Transform tool, the gizmo's scale handle wins
  the press over a piece's own handle under it.
- `perf drag gizmo` knows the Scale handles: `sx`, `sy`, `sz`, `sall`.
- **What can still shear:** an uneven Scale on an object ABOVE the Blockout (the status line names it; not the
  Blockout's to change); a Scale typed into the Inspector's Scale field or set by the console's `scale` (literal - the
  status line then offers the button); a Scale already in a scene until the button is pressed. A mesh piece, and any
  object with a mesh of its own, keeps scaling by its transform as before.

**Added - the preview:**
- **The preview is the piece.** While the footprint is dragged and the height pulled the view shows the piece itself
  as a wireframe - stairs with their steps, a wedge's slope, an arch's ring - from the same function that makes the
  piece at the click, so the two cannot disagree; fainter with a provisional height during the footprint, in the
  operation's colour (Add orange, Subtract yellow, Intersect violet) with an arrow for its direction during the height.
- **R turns it** a quarter turn round the surface's normal while a piece is in progress; the way is kept for the next
  piece of that shape.
- **The direction by itself:** a wedge and stairs rise away from where the drag started along the rectangle's LONGER
  side; an arch spans the longer side with its feet on the edge where the drag started. (A square keeps the old rule.)
- **Afterwards:** F9 has a **Turn** row after a drawn wedge, stairs or arch, the Inspector a **Turn 90** button,
  the console `blockout turn [quarters]`. A wedge and stairs stay in their rectangle, the two sizes swapping; an arch
  spans the other way over the same centre.

**Changed in older tests** (each said in the test): round 479's reachability test clicks the panel's switch, not the
toolbar; the F9 tests pick Subtract from the dropdown instead of typing 1; round 495's panel test picks shapes in the
dropdown; four of round 495's wedge checks are swapped because a wedge now rises away from the press; the "Dragging
rooms out" lesson test counts 4 questions and 5 commands (the Scaling paragraph's Try-it line and question).

**Known limits:** what can still shear is listed above. Folding an unevenly scaled Blockout uses one factor for a
room's wall (the square root of the two horizontal factors), so walls along its two axes cannot both be exactly what
they were; Field Cell is not scaled by a fold. A round piece scaled by hand along one round axis grows its radius (it
stays round); after an uneven FOLD it keeps a Scale that is not 1 1 1. Turn under an unevenly scaled parent does not
keep the piece's box in the world; an arch turned spans the other way over the same centre. The scale handle that
takes the press is the one under the mouse on the frame before. A turned box beyond about 100 km stalls the kernel
itself (found here: over 150 s at 1,000 km, built directly; not this round's code - a follow-up), which is why
scale-made numbers stop at 10,000 m. The preview is pinned by its lines and numbers, not by pixels. The move of round
495's `BlockoutDrawChoice` into `Editor::RoomTool` (one set of choices an editor, not one for the program) is in this
round's commit, not one of its own: its lines are shared with the panel's.

**Checked:** the groups blockout, editor, lighting, physics and hci, 0 failed on each: Windows without the libraries
(blockout 6,400, editor 2,329), Windows with them (7,903, 2,346) and Linux (6,402, 2,331);
`tools/test_coverage_map.py --check` 378 entries, 0 empty. Two code reviews (both "changes needed", every finding
made) and the test-engineer's 33 tests (3 found faults, fixed); the changes made after the second review were not
reviewed again. No stress section was run: the kernel is not changed. The full suite, macOS and the sanitizers are
CI's.

<!-- README -->
replace: (round 482), with **wedges
with: (round 482), **a Draw Piece tool that shows the piece while it is drawn, scale that never shears a piece** (round 496), with **wedges

<!-- docs/USABILITY.md -->
after: | A Blockout had one material and no UVs
| The Blockout section of the Modeling Tools window had 24 controls in 16 rows, drawing a wedge, stairs or an arch showed a plain box, and scaling a Blockout with a turned piece sheared it. (round 496) | One **Shape** dropdown, Shift for the other operation, the piece itself as the preview with **R** to turn it, and scale goes into the pieces' own numbers. |

<!-- docs/ARCHITECTURE.md -->
append:
## Blockout: scale in the numbers, the drawn piece (round 496, task 0480, ADR 0135)
`blockout_piece_scale(piece, factors)` (`src/scene/blockout_object.cpp`) scales a piece's numbers along its own axes;
`BlockoutScaleSet` (`blockout_scale_collect` / `_apply` / `_restore`) is what the Scale gizmo (`scene_view.cpp`) and the
modal S (`modal_transform.cpp`) use for a Blockout, a folder in it or a piece - every other object scales as before.
`blockout_drawn_piece(...)` (`src/editor/blockout_pick.h`) describes the piece a Draw Piece drag will make; the
preview (`room_draw_preview`) and the making (`room_make`) both call it. The tool's choices live in `Editor::RoomTool`.
