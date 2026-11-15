## 2026-11-14 (round 514): modifiers on a Blockout piece (task 0496, ADR 0138)

**Asked** (the owner, 2026-11-14, through the hub): "Yes, per piece modifiers would be a great implimentaiton to add." -
approved with "Approve 514 and 515" and the three recommended choices.

**Added**
- **A Blockout piece carries a modifier stack of its own** - ordinary modifier components on the piece's object, so
  saving, undo, Ctrl+D and Copy to Selected work as for any object. The stack runs on the piece's own shape, in its own
  axes, before the piece is added or cut: Bevel just the arches, an Array of ten columns from one, a Mirror on one wing.
  The rest of the Blockout keeps its exact numeric pieces.
- **Where it runs:** on the Blockout build's worker (and in the synchronous build of tests and scripts). The snapshot
  notes each stacked piece with clones of its modifiers; the build turns it into a mesh piece - its shape from its
  numbers (`blockout::piece_shape_mesh`, new), then the stack. Evaluated meshes are kept by a key of the piece's numbers
  and its stack, never its place: moving or turning a piece runs nothing; changing a number or a modifier runs it once.
  The cache is bounded (512 entries and 64 MB, the least recently used half dropped past either), guarded by one mutex,
  emptied by a new or opened scene; `blockout info` says how full it is.
- **Order:** piece stacks, then the pieces combined, then the Blockout's own stack (round 502).
- **The Inspector:** a selected piece has the Modifiers section an object has. Modifiers a piece does not take are off
  in its Add Modifier list with the reason in the tooltip (Boolean - use a Subtract piece; Curve, Lattice, Mesh Deform,
  Armature, Normal Transfer read another object; Smooth Normals only changes shading). A room, a door or window and an
  Intersect piece, and a preset (round 500's column, beam, railing, roof or stair), take no stack yet: their section says why, and a stack put on one by the console does nothing (the
  Blockout's status names the piece). Apply on a piece is refused with Bake to Mesh offered.
- **The status** names the stacked pieces ("built as a mesh: slower and less exact than its numbers").
- A Learn section "Modifiers on a piece".

**Fixed on the way:** the kernel's local box, cylinder, sphere and wedge are not wound one way before they are placed;
handed to a modifier as they were, a Bevel left four corners of a box open and the piece was meshed on the grid (1,249
faces for a bevelled 2 m box). `piece_shape_mesh` winds every face outward: the bevelled box is 26 faces, as on the
Cube primitive.

**Changed for old scenes:** a modifier already saved on a piece (inert until now) works when the scene is opened.

**After review:** a stack's result is tidied (copies of an Array that touch lose the faces between them; a face twice
is kept once) and refused - the piece built from its numbers, the status saying why - when it is not one closed solid,
is flat or has fewer than four faces (a Decimate to two flat faces made the piece vanish silently before); refusals are
kept with the cache so a refused stack does not run again at every build; Copy to Selected copies a modifier between
pieces (and names a piece that refuses it); an Array that fits a curve is said to do nothing on a piece; a modifier a
piece does not take, put there by the console, is named in the status; a stacked piece's faces shade smooth by angle;
the picking names a piece by the meshes the build used.

**Limits:** a piece with a stack builds as a mesh - on the exact path through Manifold, on the grid path as coarse as
the cell; its Blend radius is not read, nor that of the pieces in its cluster (round 480's rule for mesh pieces); stairs, arches and outlines are united by a build of the piece alone before the
stack (on builds without Blender's libraries, by the grid). No stacks on rooms, doors / windows, presets and Intersect pieces.

**Checked:** the round's groups (`blockout_piece_modifiers` new, `blockout_modifiers`, `blockout_join`, `blockout`,
`modeling`, `editor`), not the full suite: Windows with Blender's libraries 38,547 checks, dependency-free Windows
31,201 and Linux 31,207, 0 failed (the editor test "Browse... without a file dialog: said so" failed once under load -
its toast expires by the clock - and passes alone; not this round's code). Three code reviews, the last two's findings
fixed (a false warning on every plain piece's Modifiers section among them). Tests that need the exact path or Decimate
(meshoptimizer's) say so where a build has neither. `blendity_stress --only blockout_piece_modifiers` in a timed window,
exact path: a stacked column in `interior 100` changed to the drawn mesh in 19.6 ms median (Bevel) and 24.6 ms (Array of
10), budget 100 ms, one evaluation a change; a drag of the column or of a door, 120 frames: worst frame 5.2 - 6.5 ms,
0 stack evaluations, 0 main-thread builds. Untimed sweep, every allowed modifier on every shape, added and subtracted,
in a small building: 448 cases, 448 built, 0 problems on each path; faces newly on top of each other 6 on the exact
path (2 each with Bevel, Wireframe and Decimate) and 2 on the grid (Solidify) - a new section, so no earlier count;
not yet traced to their cases (an open item). Extremes (an Array of 6,000 faces, Subdivision 4 of a sphere, 70,656
faces) build, 0 problems. `--only blockout`: every count the same as round 500 recorded.

<!-- docs/ARCHITECTURE.md -->
after: | `Editor::blockout_hit`, `blockout_wall_under_mouse`
| `src/scene/blockout_piece_stack.h/.cpp` | Round 514 (ADR 0138): a Blockout piece's modifier stack - which modifiers and pieces take one, the stack's key (numbers and modifiers, never the place), the snapshot's note, `blockout_piece_stacks_apply` (on the build's worker: the piece becomes a mesh piece from `blockout::piece_shape_mesh` and its stack, through a bounded cache), the status lines. |

<!-- docs/USABILITY.md -->
after: | A Blockout's rooms and pieces were sized by typing numbers
| One column or arch of a Blockout could not be bevelled or repeated without turning the whole Blockout into a mesh | fixed (round 514): a piece carries its own modifiers - Bevel, Array, Mirror and the rest - live, and the other pieces stay exact |
