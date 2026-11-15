## 2026-11-15 (round 515): a door's leaf is its own object - it swings about its hinge without rebuilding the wall (task 0497, ADR 0136, ADR 0129 amended)

**Asked** (the owner, 2026-11-14, Pass 23b through the hub): "Doors should also come in parts, so I can "swing" the door
open without modifying the mesh so much." - then "Approve 514 and 515", with the three recommended choices: the leaf is
dynamic (the lightmap is baked with the doorway open), a Revolving door's wings only with Leaf on (off by default),
turning a leaf by hand writes Open Amount back.

**Changed:** a styled door's leaf is no longer faces of the Blockout's mesh. A Single door has one **leaf object** under
its piece, a Double two (Leaf L, Leaf R), a Sliding door one, an Arched door one (square at the springing line, the arch
above stays open), a Revolving door one **Wings** object (four wings in a cross) when its Leaf is on; Opening Only has
none. Each leaf's origin is its hinge (a Sliding leaf's: its closed place; the wings': the drum's axis), so:
- **Open Amount turns the leaf** (90 degrees at 1; a Sliding leaf moves its width) - a transform write, **no Blockout
  build**, the Blockout's mesh bytes the same at every Open Amount.
- **Turning a leaf by hand** with the Rotate tool (gizmo or R Y) **writes Open Amount back** in the same undo step; one
  undo returns both. A Double follows the last leaf turned; a leaf turned past open is put back at the end of the range.
- **The doorway is clear:** the kernel makes the frame, the reveals and the opening only, so an open door cuts nothing
  that stands in its swing (round 499's swing cut and its status notes are gone).
- The door owns its leaves: they follow the door, its wall, its room and the Blockout; duplicate, delete and undo carry
  them; deleting a leaf by hand (Delete key, Hierarchy, Edit menu) turns the door's Leaf off in one undo step. A leaf
  copied with Ctrl+D, moved out of its door or under another piece becomes an ordinary object (its mark dropped, never
  deleted), and the door makes a new leaf if it still wants one. A name the user gave a leaf is kept.
- A leaf is lit by light probes (Contribute GI off, the wall's lightmap baked with the doorway open), has a Box collider
  and a **kinematic Rigidbody** without gravity, so in Play mode its body moves with it.
- **Bake to Mesh** keeps the leaves as separate objects at the same world place; Unbake returns them under the door.
- **Export** (OBJ, FBX, GLB): each leaf is its own node beside the Blockout's (the exporters keep no parents), its pivot
  at the hinge and, with Keep Transforms, turned as far as Open Amount says.
- The leaf's Inspector shows its door's Open Amount and Hinge Side and "Made by Door 'X' - its shape follows the door's
  numbers"; `blockout leaf select` selects the selected door's leaves, `blockout leaf door` the leaf's door.
- The Leaf material colours the leaf objects; changing it no longer rebuilds the Blockout.
- Scenes saved with round 499's styled doors open with their leaf objects made, the doors looking the same.
- Learn: "Doors with a style" swings a leaf with Open Amount, then with the Rotate tool.

**Fixed on the way:** a NaN Open Amount made the door look changed every frame (a float compare); a NaN Leaf Thickness
turned the door into a plain hole.

**Limits, said plainly:** a leaf turned during an animation or Sequencer preview, or while playing, is not written back
(the preview's pose is not the user's); after the preview the leaf goes back to Open Amount. A leaf's shape follows the
door's numbers: edits to its mesh are lost when those numbers change. A removed Rigidbody comes back when the leaf is
remade. The Unity bridge is not covered by a test.

**Numbers** (`blendity_stress --only blockout`, untimed counts at below-normal priority beside other work), against
round 500:
- **The 2,000-door sweep** on the grid: 2,000 built, 0 problems, 18 with faces of no area - the same as round 500.
- **Hostile numbers on styled doors** (180 cases on the grid): 127 built, 53 refused (round 500: 123 / 57), 0 problems -
  the four more built are doors with a NaN Leaf Thickness, which no longer refuses the doorway.
- Every other count row the same; the build and landing counts of the live rows differ by timing under load.

**Checked:** CHECKED_LINE

<!-- README -->
replace: a **Room tool** that draws a room on the ground or a face (round 479)
with: a **Room tool** that draws a room on the ground or a face (round 479), door leaves that swing about their hinge without a rebuild (round 515)

<!-- docs/USABILITY.md -->
after: | A Blockout knew boxes, cylinders and spheres
| Opening a door rebuilt the wall at every angle, and an open leaf cut whatever stood in its swing. (round 515) | A door's leaf is its own object with its pivot at the hinge: Open Amount or the Rotate tool turns it with no rebuild, and the doorway cuts nothing. |

<!-- docs/ARCHITECTURE.md -->
append:
## Blockout: door leaves as objects (round 515, task 0497, ADR 0136)
`BlockoutLeaf` (`blockout_object.h`) marks a leaf object (index, `made_by` door id, `open_seen`); `blockout_leaf_plans`
gives a door's leaves (hinge pose, slab size) and `blockout_leaf_key` their shape. `Editor::blockout_sync_leaves`
(at the start of `update_blockouts` and just before `commit_undo`) makes, re-makes, places and lets go of leaves and
writes a hand turn back to Open Amount through `blockout_join_step`. Open Amount, Hinge Side, Leaf and Leaf Thickness
are not read by the kernel pieces and are not in the snapshot hash.
