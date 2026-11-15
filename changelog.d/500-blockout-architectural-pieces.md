## 2026-11-14 (round 500): architectural pieces - columns, pilasters, beams, railings, roofs, stairs with a landing, spiral stairs and arcades, each one piece with numbers (task 0484, ADR 0128 and 0129 amended)

**Asked** (the owner, 2026-11-11, Pass 23b through the hub): "Yes, more architectural pieces is good." - kept
blockout-level, "so I can texture and add materials later".

**Added:** a Blockout piece can be one of nine **architectural pieces**, each **one piece with numbers** (no child
object, no object per solid), made through round 498's generator:
- **Column** (round or square shaft, Base and Capital Height and Width, **Taper**, Segments) and **Pilaster** (a half
  column standing against the wall under the mouse or the selected room's first wall).
- **Beam** (Length, Depth, Width).
- **Railing** along a line or an outline's sides: a post every Post Spacing, Post Size, Rail Height, Rail Size, a bottom
  rail on / off; at an outline's corner a newel post a little wider and taller than the rails.
- **Roof** over a rectangle or an outline: **Gable, Hip, Shed**, Pitch or Ridge Height, Overhang, Thickness (0: a solid
  roof). Its faces that look up are a ninth material class, **Roof** (a Blockout material and a piece override).
- **Stairs L** and **Stairs U** (two flights and a landing), **Spiral Stairs** (Radius, Inner Radius, Total Height,
  Steps, Turn in degrees).
- **Arcade** (Count arches on Count + 1 columns: Span, Column Width, Arch Rise, Depth).
Each is made by the Create section's **Architecture** dropdown and its **Add** button (one dropdown, not nine buttons),
GameObject > Blockout > Architecture, and the console (`blockout add column|pilaster|beam|railing|roof|stairs l|stairs
u|stairs spiral|arcade [numbers]`, `blockout set <word> <value>`); its numbers are in the Inspector and in F9 after it is
made, and the handles of round 479 change the number each is named for. A Learn section, "Architectural pieces", with a
Try-it for each.
- **A new kernel shape, Convex** - a solid given by its corners (4 to 64), its faces the convex hull's - for a tapered
  shaft, a hip roof's ends and a spiral step (ADR 0128 amended). The hull is kept per thread, so a selected tapered
  column's outline costs nothing after the first frame.

**Limits, said plainly:**
- Over an outline a roof is a roof over each rectangle the outline cuts into, the ridge along its long side; its
  handles and Ridge Height follow the widest rectangle, and only the top handle is offered. Outlined roofs whose
  rectangles overlap can leave a face on top of another where their bottoms meet (below).
- Counts (posts a side, steps, arches) are held to 256; a size over 200 m is left out; Turn 0, a spiral held at 1440
  degrees, an arch rise held at 170 degrees and a shaft whose corners make no solid are named in the status.
- Scale grows lengths, keeps counts and angles, keeps a zero a zero (Roof Thickness, Overhang, a plinth of 0); Post
  Spacing grows (the same posts, farther apart); a roof's Pitch follows the slope, tan(pitch) times the height's scale
  over the span's.
- On the grid path at the default Field Cell, posts and rails finer than a cell are held at one cell and the status
  names the cell that shows them.

**Numbers** (`blendity_stress --only blockout`, untimed counts at below-normal priority beside other work):
- **The new sweep, each kind alone and standing in a room** (500 arrangements a kind a path: sizes, places, turns; a
  third of the railings and roofs over an outline). Problems (a result with faces on top of each other) on the exact
  path / on the grid (cell 0.08): Column 0 / 9, Pilaster 0 / 12, Beam 0 / 0, Railing 26 / 10, Roof 1 / 9, Stairs L 3 / 3,
  Stairs U 1 / 4, Spiral Stairs 1 / 13, Arcade 3 / 8. The causes:
  - **Railing (exact 26):** in a room turned against the railing, the room's floor face, split round many small post
    footprints standing on it, lies on top of itself (seed 16: the floor's top face, 20 corners, over another piece of
    the same floor) - the kernel's coplanar contact (ADR 0128), not the railing's own solids. The newel posts took the
    corner cases away (28 -> 26 exact) at the cost of 2 on the grid (8 -> 10).
  - **Stairs, spiral steps, the roof:** two solids meeting in one plane - a flight on its landing, two spiral steps on
    their shared bottom plane, the bottoms of an outlined roof's overlapping rectangles. The fix pass moved what could
    overlap (L stairs 11 -> 3 exact, 18 -> 3 grid; U stairs 4 -> 1 exact, 15 -> 4 grid); a spiral's steps overlapping
    gave 36 instead of 1, so they meet.
  - **Arcade (exact 3, and 250 with small faces on top):** arcades standing in the room, whose columns reach through
    its walls.
  - **The grid path:** features finer than the 8 cm cell (posts, a pilaster's thin plinth) and turned faces.
- **Hostile numbers** (0 or 1,000 posts, a pitch of 0 or 90, a taper of 0, 500 steps, an arcade of 0, NaN, 1e9):
  exact 1,413 cases, 1,232 built, 181 refused, **0 problems**; grid 1,413, 1,233 built, 180 refused, **0 problems**.
- **The 100-piece bench interior with one of each piece added** (109 pieces): the mesh closed, no build on the main
  thread on either path. Its times were taken under load and say OVER; the timed row comes with round 503's window.
- **20 tapered columns selected** in the bench interior: frame median 1.78 ms with the libraries (under load).
- **Every sweep that existed, and rounds 498 and 499's rows: the same counts row for row** on both builds (only the
  live rows' build counts, which follow the machine's load, moved).

**Checked:** the groups blockout, editor, lighting, physics and hci, 0 failed on each: Windows without the libraries
(blockout 21,188 checks), Windows with them (28,407) and Linux (21,191). The Linux run found the Architecture dropdown
cut by its wider font ("Spiral Stairs") - the label now takes what it needs, and in a narrow pane the dropdown shares
the Add button's row. The test-engineer's 20 tests found a face-centre corner the hull refused, a railing cap of 257
posts, a solid roof that scaling made a 1 cm sheet - fixed - and two exact-path faces of a spiral the UV measure could
not lay flat (a 5e-6 m sliver where two steps share a plane; the measure now leaves out faces that are not flat or are
slivers, and prints how many). A code review: the hull rebuilt every frame (now kept), outlined handles that moved the
piece, silent omissions, scale rules - fixed. The coverage map: no command, action or window without a test. The full
suite, macOS and the sanitizers are CI's.

<!-- docs/USABILITY.md -->
after: | A Blockout knew boxes, cylinders and spheres
| A Blockout had rooms, boxes, cylinders, spheres, wedges, stairs, arches, doors and windows; a column, a railing, a roof or a staircase with a landing had to be put together from them. (round 500) | Nine **architectural pieces** - Column, Pilaster, Beam, Railing, Roof (Gable, Hip, Shed), Stairs L / U, Spiral Stairs, Arcade - each one piece with numbers, from one Architecture dropdown; a Roof material class. |

<!-- docs/ARCHITECTURE.md -->
append:
## Blockout: architectural pieces (round 500, task 0484)
A piece's `preset` (9 kinds, `kBlockoutPresetNames`) is generated in `src/scene/blockout_presets.cpp`
(`blockout_preset_pieces`, called by `blockout_snapshot` as round 498's windows are); generated ids name the piece
(`blockout_piece_owner`). The kernel's `Shape::Convex` (`Piece::points`, 4-64 corners) is built from
`detail::convex_hull` (kept per thread, 512 answers); `convex_up_faces` / `convex_edges` give the Roof class's count and
the wire. `kClassRoof` is the ninth face class.
