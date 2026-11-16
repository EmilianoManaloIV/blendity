## 2026-11-15 (round 537): LoopTools built in

**Asked:** Pass 27 (Blender 5.3's new features): LoopTools, which Blender 5.3 made native - Relax Edge Loops, Space,
Circle, Flatten, Curve Edge Loops Between Selected and Bridge - as Edit Mode operators with F9 numbers, a menu entry,
a console `editop` and a Learn lesson step each, robust on degenerate input.

**Added (task 0517)**
- Five Edit Mode operators in the Mesh menu's Deform group, ported from Blender 5.3's native LoopTools
  (`src/scene/mesh_tools_loop.cpp`): Relax Edge Loops (`relax_loops`), Space Edge Loops Evenly (`space_loops`), To
  Circle (`circularize`), Flatten (`flatten_loops`) and Curve Edge Loops Between Selected (`curve_loops`). A loop is
  the edges whose two ends are selected; Adjust Last Operation (F9) holds their numbers (factor, iterations,
  interpolation, fit, rotation, radius, flatten, plane, elevation clamp, extend, Space Evenly, Lock X Y Z). Keymap
  actions `mesh.relax_loops` ... `mesh.curve_loops` (no default keys); `editop <name>` runs them.
- Learn: the lesson "LoopTools: Loops to Circles, Curves and Planes".
- Bridge is not a new operator: Blender 5.3 did not port LoopTools' Bridge, and its native Bridge Edge Loops is what
  Blendity's Bridge already does.
- Robust: zero-length edges, collinear loops, non-manifold edges, junctions, 1e7 / 1e-7 scales and NaN positions
  leave a valid mesh - a loop that can't be solved is left as it was, and the toast says so when none could be.

**Fixed after review (code-reviewer):** edge and face mode now use the real edge / face selection (two neighbouring
rings in edge mode relax as two loops, a ring of faces rounds both borders); Relax and Space refuse selected faces, as
Blender's; a refusal (-1) is told apart from "ran, moved nothing" (Factor 0, an already flat face); a loop closing on a
junction no longer depends on edge order; Flatten's Best Fit is Blender's plane from the cloud's extreme points; To
Circle's surface projection splits quads on Blender's diagonal; `redo axis|individual|lock|size` reach every F9 field
from the console.

**Checked:** on `main` 068207d9, the round's groups: Windows with Blender's libraries `modeling` 5,706 checks and
`editor` 3,419 (four editor tests - reference images, the scatter / screenshot sweep rows, the Animation window scrub -
failed in the group run in a reused scratch project and passed alone; none touches this round's files); Linux
dependency-free `modeling` 5,648 and `editor` 3,394, 0 failed. The full suite is CI's. Stress: `looptools` (new: all
five operators on a 10,000-vertex closed ring and open chain and Flatten on a 100 x 100 patch, 4.6 - 41.6 ms an
operator) 0 problems; `pushpull` 26,620 operations, 0 problems, 394 results with faces newly on top of each other, no
change; `modifiers_ngon` 11,514 operations, 0 problems; `curved` 236 projected draws, 0 problems, 0 new overlaps;
`editor` ran clean.

<!-- README -->
replace: (extrude, inset, bevel, loop cut, bridge, knife, n-gons, F9; a
with: (extrude, inset, bevel, loop cut, bridge, knife, n-gons, LoopTools, F9; a

<!-- docs/USABILITY.md -->
after: | Adding a door or window meant the Create section's buttons
| Shaping loops (a round hole, a flat cap, an even ring) took the LoopTools add-on in Blender and was missing here. (round 537) | Relax, Space, To Circle, Flatten and Curve in Edit Mode's Deform group, with F9 numbers. |

<!-- docs/ARCHITECTURE.md -->
after: | Hard-surface tools (grid, pipe, array, taper, recess)
| LoopTools: Relax, Space, To Circle, Flatten, Curve (Blender 5.3 native, round 537) | `scene/mesh_tools_loop.cpp` | `looptools` |
