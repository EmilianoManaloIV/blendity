# 0480: The Blockout part of the Modeling panel - less to look at, drawing told apart from selecting (round 496)

- **Status:** done (round 496, 2026-11-13) - the owner's ask and choice, 2026-11-12, through the hub: "Lets do A and B together."
  (of the proposal's alternatives), plus the toolbar button.
- **Requirements:** U-01 (the tool does what the cursor says), U-03 (the interface is readable), C-02 (every tool from the
  console)
- **Decisions:** ADR 0085 (HCI rules) for the panel; **a new ADR for how a Blockout takes scale** (items 7 and 8: a
  change to a documented convention - a piece's size is its numbers, never its transform's scale)
- **Model:** main session or implementer once the choice is made, then test-engineer, then code-reviewer
- **PR:** branch `task/0480-blockout-panel-hci` (on top of #396)

## What the owner said
- "Improve HCI for the blockout pieces, really looks crowded to work with. And is confusing between selection and
  drawing."
- "For the blockout part of the modeling panel."
- "I feel that having the Draw: and Operation: tag with so many options is too much. There has to be a better way."
- "I also don't think it should be included in the main board on the top next to "center" "global" etc. I feel that its
  a niche tool that would only be used with the blocking out and general modeling tools."
- On scaling: "Scaling for the block out objects is weird, especially when I want to scale the object using the local
  axis." - "It sheared." - and, to the fix proposed (below, 7 and 8): "Go ahead with A + B and I'm unsure for the second
  question." (whether a parent in their scene was scaled; the modal S oddity was not answered and is left out).

## What is there today (the study: `pass23-hci-496-proposal.md` in the hub's notes folder, with four screenshots)
- The Blockout section of the Modeling Tools window: **24 controls in 16 rows**, about half the panel's height - 11
  one-shot "add a piece" buttons in a grid, the Draw Piece (Room) tool's switch, 11 option buttons (2 footprint ticks, 5
  "Draw:", 4 "Operation:") and one field (Room Wall Thickness). Thirteen rows are identical full-width buttons: a
  one-shot, a mode you stay in and that mode's options look the same.
- Two vocabularies for one piece: the grid's "Subtract Cylinder" against the tool's Draw: Solid + Circle Footprint +
  Operation: Subtract.
- The tool being on shows only as the relabelled button, the Scene view toolbar's cube, the status hint and a ground dot.
  While it is on every left press in the view draws; only Esc, Shift+R or the button leave it.

## Decided
1. **The Draw Piece tool's button leaves the Scene view's top toolbar** (the cube beside Center / Global). The tool lives
   in the Modeling Tools window's Blockout section, on Shift+R, in the menu and in the console (`tool room`) only.
   Lessons and hints that point at the toolbar button are corrected.
   - Test: the toolbar has no such button; the tool is still switched on from the panel, by Shift+R and by `tool room`.

2. **One Shape dropdown in place of the footprint ticks and the Draw: row** (the proposal's A): its nine entries are
   the nine things the tool draws - Room, Round Room, Outline Room; Box, Cylinder, Outline; Wedge, Stairs, Arch. A shape
   names its footprint (a Round Room and a Cylinder are the circle, the Outline ones the polyline), so the two ticks and
   their silent resetting of the Draw row go. The tool's switch reads "Draw Piece" with its shortcut and is lit while on.
3. **No Operation row** (A and B): Auto is the rule - pulled out adds, pushed in subtracts (in a Blockout with nothing
   to cut it adds, as round 495 has it). **Shift held while the height is pulled makes the other one.** The label beside
   the mouse says what the piece will be and what Shift would make ("height 0.50 m  Subtract  (Shift: Add)"). A room has
   no operation. Alt is not the key (Alt + click orbits); Ctrl stays "no snapping".
4. **Afterwards, F9 and the Inspector change the operation** (B): F9's Operation row for a Blockout piece shows words
   (Add, Subtract, Intersect), not 0 / 1 / 2. Intersect is reached there and from the console only.
5. **Room Wall Thickness** is shown only while the shape is one of the three rooms.
6. **A console operation that sticks is visible:** `blockout roomopt operation add|subtract|intersect` still fixes the
   operation until `auto`; while one is fixed the panel shows one dim line, "Operation: Intersect (console) - click to
   clear", and a click sets Auto again. The console's `blockout roomopt` words all stay (circle, polyline, solid,
   subtract, piece, operation, wall) and set the dropdown.

## Scale never shears a piece (the findings: `pass23-scale-496-findings.md` in the hub's notes folder)
The cause: a piece's own transform cannot hold a shear; a piece shears when something above it is scaled unevenly and
the piece is turned against that thing's axes - reproduced with the Blockout object scaled 2 1 1 and a box turned 30
degrees inside it. And a piece scaled by the gizmo keeps the scale in its transform, beside its size: two sources of
truth (Size 2 2 2 with Scale 2 1 1; a cylinder that is an ellipse and says "Radius 1").

7. **Scaling a piece changes its own numbers** (the gizmo's Scale handles, the modal S with or without an axis): a
   box's, wedge's and stairs' Width / Height / Depth, a cylinder's radius and height, a sphere's radius, an arch's
   radii and depth, an outline's points and height, an opening's width / head / sill - along the piece's own axes; its
   transform's scale stays 1 1 1. A cylinder or a sphere scaled along one of its round axes scales its radius: it stays
   round. A scale an older scene left on a piece is folded into its numbers the first time the piece is scaled (and
   by "Apply Scale to Size" in the piece's Inspector). A mesh piece keeps its transform's scale (its numbers are the
   mesh's). Several pieces scaled together scale about the pivot: their places move, their numbers scale. One undo step.
8. **Scaling the Blockout scales its pieces**, about the Blockout's pivot - their places and their numbers - and leaves
   the Blockout's own scale at 1 1 1: no piece can shear, a room's wall keeps its thickness, the Draw Piece tool never
   refuses a "scaled unevenly" Blockout. The same for **a folder or a parent piece between the Blockout and a piece**:
   scaling it scales what is under it and leaves its own scale at 1. A turned piece under a one-axis scale of its
   parent is scaled along its own axes by how much each of them grew (so it does not shear; said in the lesson).
   - **What can still shear, and what is done about it:** a scale that is ALREADY in a scene (a Blockout, a folder or a
     parent piece saved with an uneven scale), and an uneven scale on an object ABOVE the Blockout (not the Blockout's
     to change), and a scale typed into the Inspector's Scale field or set by the console's `scale` (they stay
     literal). For the first: the Blockout's status says "scaled unevenly: turned pieces are sheared" with a button
     **Apply Scale to Pieces** that does item 8's fold once. For the others: the same status line names the object.
9. `perf drag gizmo` learns the Scale handles (`sx`, `sy`, `sz`, `sall`) so the tests drag them with real events.

## The piece is seen while it is drawn (the owner, 2026-11-12: "Drawing an arch, stair, or other draw method needs a
preview. I can't really orientation to accurately gauge its placement.")
Today every shape shows its footprint's outline and then a plain box of lines over it; a wedge, stairs and an arch look
like a box until the click, and which way they face follows rules nothing shows (stairs climb along the rectangle's
second side as the surface lays it; a wedge is tall at the press end; an arch bulges along the second side).

10. **The preview is the piece.** While the footprint is dragged and while the height is pulled the view shows the piece
    itself as a wireframe - stairs with their steps, a wedge's slope, an arch's ring or opening, a room's two walls, a
    cylinder's rings - drawn by the code that draws a selected piece's outline, from the numbers the click will make
    (one function describes the piece from the drags; the preview and the making both use it, so they cannot
    disagree). During the footprint the height is provisional and the lines fainter; during the height pull it is the
    true height, in the operation's colour (Add orange, Subtract yellow, Intersect violet), with an arrow on the
    footprint for the way it rises.
11. **R turns it.** During the footprint or the height, R turns the piece's direction by 90 degrees round the surface's
    normal (four ways); the preview follows, the label beside the mouse says "R: turn", and the way chosen is kept for
    the next piece of that shape. (R is taken by the tool while a piece is in progress only.)
12. **The direction by itself** (the owner, 2026-11-12: "The default is fine, objects are parametric anyways."): a
    wedge and stairs both rise away from where the drag started, along the rectangle's LONGER side; an arch spans the
    longer side, its feet on the edge where the drag started. Pushed into the face they are turned over, as round 495
    has it.
13. **The direction is changed afterwards too** (the pieces are parametric): F9 after a drawn wedge, stairs or arch has
    a **Turn** row (a quarter turn round the surface's normal each step, the footprint kept: the piece's two footprint
    sizes swap as it turns), and the piece's Inspector has a **Turn 90** button doing the same on any such piece. The
    lesson says so.

## Not chosen, not built (the hub asks the owner separately)
- Modal S on a GLOBAL axis turning and swelling a turned object (any object, not only a Blockout's): not answered.
- The proposal's step 2: a chip in the view naming the tool and its way out ("Drawing: Box - Auto | Esc"), and the tool
  switching itself off after one piece unless "Keep drawing" is ticked.
- C: one merged grid where a click on a shape starts drawing it once.

## Done: deviations
What differs from the spec as it was built (rounds 496 and its review fixes):
- The operation label keeps `%.3f` metres in the height ("height 0.500 m  Subtract  (Shift: Add)"), not two places.
- **Shift** flips a console-fixed Add or Subtract; a console-fixed Intersect ignores it (it has no other).
- The dim line "Operation: ... (console) - click to clear" also shows for the old `blockout roomopt subtract on`.
- Choosing a **room shape forgets a fixed operation** (the old `subtract on` included: a room has no operation, `roomopt piece room` always did).
- `BlockoutDrawChoice` is per editor (the tool's fields in `Editor::RoomTool`), not one for the process.
- **Carriers keep the scale they had** (not forced to 1) until Apply Scale to Pieces: a Blockout or folder scaled by hand ends at 1 1 1 only if
  it was; a scale already on one keeps shearing until the fold.
- **Objects with their own geometry keep transform scale**: a mesh piece, a plain mesh object or a baked Blockout that holds pieces grows by its
  Scale like a Leaf (its pieces follow through their world axes); it is no carrier and is left out of "scaled unevenly" and of Apply Scale to Pieces.
- **Walls through a fold**: folding the Blockout's own scale (Apply Scale to Pieces) multiplies a room's Wall by the square root of its X and Z scale
  and Floor / Ceiling by its Y scale, and Texture Scale by the cube root of the three, so the world looks as it did; a folder's or a piece's own
  scale folded changes none of them (a wall is in the Blockout's space). A plain user scale keeps the wall (item 8). Field Cell is left as it is.
- **Square footprints keep the old direction rule** (a wedge, stairs or arch drawn on a square rectangle: item 12's "longer side" has no longer one).
- **The arch's turn**: an arch turned (R, Turn, Turn 90) spans the other way over the same centre - its box turns; only a wedge and stairs keep their
  rectangle (their two footprint sizes swap).
- The preview is pinned by line counts and numbers (the lines drawn, the piece made from the same function), not by pixels.
- AC 8's Global case runs the same code as Local: the Scale tool uses the object's own axes whichever way the toolbar says; the modal S X covers the
  world axes.
- `src/editor/panels.cpp` and `src/editor/perf_script.cpp` are touched (the panel's section, `perf drag gizmo sx|sy|sz|sall`).
- From the review fixes: a number made by a scale stops at 10000 (F9's limit) and a filled arch's inner radius stays 0; a child selected before its parent
  is folded after it; a zero scale axis stays zero; the modal S restores its set once when G or R takes over; `blockout applyscale` with a selection that
  holds no Blockout and no piece does nothing; Turn folds an old scale into the numbers first; the Scale tool's handle wins a press over a piece's own
  handle under it; `blockout room` ignores the remembered R turns; the footprint's hint names the shape; the keymap's label says Draw Piece.
- From the second review (round 496 fixes): a fold leaves a Blend radius alone (times the Blockout's own folded scale only), a hand scale ignores the Scale the
  piece carried; a Blockout inside a folded Blockout takes the product of the scales (walls, Texture Scale); a round piece under an uneven fold keeps the rest
  of the ratio on its own Scale (a hand scale still makes it round); a non-finite factor leaves places where they are; the Transform tool's scale handle wins
  a press too; Apply Scale to Size's tooltip says it folds what is below a parent piece. Known limits: the Scale handle's hover is last frame's and `gizmo_hot_`
  is shared by Scene views; Turn under an unevenly scaled parent does not keep the world box.

## Scope
- In: items 1 to 13; the Learn sections that name the old rows or the toolbar button corrected ("Dragging rooms out",
  "Ramps, stairs, arches and odd-shaped rooms", "Drawing pieces on faces"); the status hint for the tool.
- Out: the one-shot "add a piece" grid above the tool (it stays as it is); the Scene view's handles, outlines and gizmo
  (the study's short second part); the chip and "off after one piece".

## Likely files
`src/editor/blockout_tool.cpp` (`draw_room_tools`, `room_update`'s height stage reading Shift, `room_draw`'s label,
`blockout_draw_operation` taking the flip), `src/editor/blockout_pick.h` (or `Editor::RoomTool` in `editor.h`, free now:
round 495's `BlockoutDrawChoice` can move there in this round, as its own small commit), `src/editor/editor.cpp` (the
toolbar's cube, ~1524), `src/editor/adjust_last.cpp` (the Operation row as words), `src/editor/hints.cpp`,
`src/editor/lessons.cpp`, `tests/test_blockout.cpp`, a fragment `changelog.d/496-blockout-panel.md`. For the scale:
`src/scene/blockout_object.h/.cpp` (the piece's numbers scaled along its own axes), `src/editor/scene_view.cpp`
(`gizmo_update`'s scale, `perf_gizmo_handle`) and `src/editor/modal_transform.cpp` (`transform_apply`) - the two places a
scale is written -, `src/editor/blockout_editor.cpp` (the status line, Apply Scale), a new ADR.

## Acceptance criteria
1. The Scene view's top toolbar no longer shows the Draw Piece tool; the panel, Shift+R, the menu and the console still
   switch it on and off.
2. The Blockout section of the Modeling Tools window has, for the tool, one switch, one Shape dropdown and (for a
   room) one field - no footprint ticks, no Draw: buttons, no Operation: buttons; a test counts them.
3. Each of the nine shapes chosen in the dropdown and drawn with real mouse events makes that piece (shape, room or not,
   circle / outline footprint).
4. With real events: a box pulled out adds and pushed in subtracts; with Shift held at the click the other one; the
   label's text names both; a room ignores Shift.
5. F9 after a drawn solid shows Operation as a word and changes it; Intersect is reachable there.
6. `blockout roomopt operation intersect` shows the dim line and the next solid is Intersect; a click on the line (or
   `operation auto`) clears it. Every older `blockout roomopt` word still works and moves the dropdown.
7. Rounds 479, 480 and 495's tool tests pass - changed only where they clicked a row that no longer exists, each change
   said in the test.
8. **No shear** (real mouse events through `perf drag gizmo sx`): a box turned 30 degrees, its X scale handle dragged,
   with the toolbar on Local and on Global - the piece's transform scale is 1 1 1, its Width grew, every face normal of
   the Blockout's mesh lies on one of the piece's three axes, one undo step. The same by S then X, and S X X. A room
   scaled keeps its wall's thickness; a cylinder scaled along X stays round, its radius grown. A piece saved with scale
   2 1 1 is folded at its first scale: the same shape, scale 1.
9. **The Blockout scaled** 2 along X with a piece turned 30 degrees inside: the Blockout's scale is 1 1 1, the pieces
   moved and grew, no piece is sheared (the face normals again), a room's wall is as thick as before, the Draw Piece tool
   draws in it. The same for a folder between. A Blockout loaded with an uneven scale shows the status line, and Apply
   Scale to Pieces leaves it at 1 with the same outline of pieces along its axes.
10. **The preview** (real events): with Stairs, Wedge and Arch chosen, during the footprint drag and during the height
    pull the view has the piece's lines (the picture differs from the same frame with the tool's old box - counted in a
    rectangle round the piece), and the piece the click makes has the corners the preview's function gave. R during the
    draw turns the made piece a quarter turn each press (stairs: the top step's end moves round the four sides); the
    next stairs start that way. F9's Turn row and the Inspector's Turn 90 turn a made stairs a quarter turn in its
    footprint (the same box of the world before and after; the top step's end on the next side), one undo step each.
11. The round's groups and `editor` on Windows (both builds) and Linux, the audits (the coverage map: every keymap action
   and window still has a test); CI.
