## 2026-11-12 (round 495): a piece - a Subtract box and the rest - drawn on a face of a Blockout (task 0479)

**Asked** (the user, 2026-11-12): "Make it so that you can "draw" subtract box, etc. Onto faces other blockout
objects." And, asked whether the direction of the pull should choose the operation: "Yes, that's right."

**What was there:** the Room tool (Shift+R) could already drag a footprint on any face and pull it into a solid, with
a Subtract tick - but the piece went into the *selected* Blockout, the tool said "Room", it knew a box, a cylinder
and an outline only, and the console drew on the ground only.

**Added:**
- **A piece drawn on a face of a Blockout is a piece of that Blockout**, whatever is selected (on the ground or on a
  mesh that is no Blockout: the selected Blockout or a new one, as before).
- **Draw:** Room, Solid, **Wedge, Stairs, Arch** in the tool's options (Modeling Tools > Create > Blockout). A wedge is
  tall where the drag began, stairs climb the way the rectangle was dragged, an arch lies in the surface - pushed
  through a wall with Subtract it is an arched opening.
- **Operation:** **Auto** (the default, on the user's word: pushed into the face a solid subtracts, pulled out it
  adds), Add, Subtract, **Intersect**. The label beside the mouse says which it will be while the height is pulled.
- **Console:** `blockout draw room|box|cylinder|wedge|stairs|arch add|subtract|intersect|auto at <x y z> normal
  <x y z> size <du dz height>` - what the tool's drags make on that surface point; `blockout roomopt piece ...` and
  `blockout roomopt operation ...` set the tool's rows.
- The Learn section "Drawing pieces on faces".

**Changed** (what the Room tool did differently before):
- the tool's button reads "Draw Piece (Room) Tool"; the Solid Piece and Subtract ticks are the Draw and Operation
  rows;
- **a solid dug into a surface with no operation chosen is now a Subtract piece** (Auto) where it was an Add piece
  inside the surface - unless its Blockout has nothing in it yet (a new one too), where Auto adds, since there is
  nothing to cut. Choose Operation: Add for the old result;
- `blockout roomopt subtract off` now means Auto, not Add (`on` is Subtract as before; `solid`, `circle` and
  `polyline` mean what they did);
- **a room drawn on a face of a Blockout joins that Blockout** too, where it joined the selected one or a new one;
- a wedge or stairs pushed into a face is turned over - a ramp or a stairwell going down into the face, open at it.

**Found and fixed before publishing:** the test-engineer: Solid switched off and on kept the operation chosen
before. The code review (changes needed, all made but the two named under the limits): a pushed-in wedge or stairs
cut a hollow closed under the face; the Learn section's lines did not do what they said (the box of `blockout new`
stands where the camera looks, and the arch came out upside down) - they are five `blockout draw` lines now, run by
a test; a console command run while a footprint waited for its height took its Blockout away; the tool's choices
were reset by whoever read them - they are forgotten where Solid goes off; Auto into an empty Blockout made a lone
cutter; the face is now the one under the press itself, not found again by a short ray; the Operation rows are
greyed for a room. The second review, of those fixes (approve with changes, made): **F9 on a pocket dug with a
rectangle moved it sideways onto the corner the drag began at** (older than this round - rounds 479 and 480's solids
dug down did it too - and met at once now that pushing in cuts); the label beside the mouse said Subtract where Auto
would add; "nothing to cut" now looks for a piece that is on and adds; the Learn test runs the lesson's own lines;
**F9's Depth on an arch drawn on a face keeps the face** (`adjust_last.cpp`, freed by the hub).

**Known limits:** the footprint snaps to the grid, not to the face's edges and corners; an arch's rise is half its
width and the rectangle's second side only says which way it bulges (its other numbers are the Inspector's and
F9's); an arch ring thinner than about two grid cells can come out with faces on top of each other on the
grid path (seen with a ring 0.125 m thick at the 0.1 m cell; a drawn arch's ring is half its radius, as the
component's own default, which was clean); `blockout draw` from the console finds the
Blockout by a 5 cm ray at the point given, which is too long in a Blockout scaled to a hundredth and too short
300 km from the origin (the tool itself uses the press's own ray); a face of a Blockout nested in another gives the
piece to the outer one; the tool's Draw and Operation choices live outside the editor's own state this round
(`blockout_pick.h` says why) and are one set for the program; a sphere has no footprint and is not drawn
(`blockout add sphere`).

**Checked:** on main `f36d411d` (the round's groups and `editor`, ADR 0134; the full suite is CI's). Windows with Blender's libraries: `blockout` 5,582 checks, `editor` 2,346, 0 failed; Windows dependency-free: 4,081 and 2,331, 0 failed; Linux (GCC, dependency-free): 4,083 and 2,330, 0 failed. 24 tests for this round, the tool's with real mouse events. Pockets 0.25, 0.5 and 0.75 m deep in a box wall and one in a room's wall: 0 faces on top of each other, volumes exact, on both builds. Not run here: the ASan + UBSan group and the stress sections (no kernel change; CI's jobs run them). Audits: the coverage map (378 entries, 0 empty), `changelog.py check`, `docs_check.sh`. Test-engineer: 14 tests, one fault found; code review twice: changes needed, then approve with changes - made as listed above (the second review's changes were not reviewed a third time).

<!-- README -->
replace: (round 480), SketchUp-style drawing
with: (round 480), pieces drawn straight onto a Blockout's faces - a Subtract box pushed in, a ramp, stairs, an arch (round 495), SketchUp-style drawing

<!-- docs/USABILITY.md -->
after: | A door, a window or a piece that cuts could not be clicked
| A cutting box could not be drawn on a face of a Blockout unless that Blockout was selected, and only as a box or a cylinder behind the Room tool's Solid and Subtract ticks. (round 495, asked by the user) | The Draw Piece (Room) tool draws a box, cylinder, wedge, stairs or arch on any face; the piece joins the Blockout it was drawn on, and pushed in it cuts (Operation: Auto). |

<!-- docs/ARCHITECTURE.md -->
append:
## Blockout: pieces drawn on faces (round 495, task 0479)
`src/editor/blockout_tool.cpp` (the Draw Piece / Room tool): `blockout_at_surface(scene, point, normal)` - the
Blockout whose mesh has a face there (the console's `blockout draw` asks it). `BlockoutDrawChoice`
(`blockout_pick.h`): the tool's piece (wedge, stairs, arch) and operation (Auto, Add, Intersect) beside
`RoomTool`'s ticks, and the Blockout the footprint in progress was started on (set from the press's own ray;
`room_target` prefers it to the selection); `blockout_draw_operation(solid, subtract, height, nothing_to_cut)` is the
operation a pull of that height makes.
`Editor::room_make` places the piece (`blockout draw` and the tool both call it).
