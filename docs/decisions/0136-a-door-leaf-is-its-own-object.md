# ADR 0136: A door's leaf is its own object, not part of the Blockout's mesh

- **Status:** accepted 2026-11-14 (round 515, task 0497; the owner, through the hub: "Approve 514 and 515", with the
  spec's three recommended choices)
- **Date:** 2026-11-14
- **Amends:** ADR 0129 (decision 3, "a piece has no mesh of its own", and round 499's amendment, "a hinged leaf is not an
  Add solid"); ADR 0128 is unchanged

## Context
Round 499 made a door's leaf part of the Blockout's mesh: a hinged leaf is what the doorway's cut leaves of the wall, so
opening it (Open Amount) rebuilds the whole Blockout, and the cut that frees a swung leaf also clears its swing in front of
the wall - a column or a near wall standing there is cut (#405 put that to the owner). An added leaf instead left 414
problems in 2,000 arrangements, because the kernel adds before it subtracts. The owner's answer (2026-11-14): "Doors
should also come in parts, so I can "swing" the door open without modifying the mesh so much."

A leaf that moves is not blockout matter: it is a part, like a Unity door prefab's child that an Animator turns about its
hinge. Keeping it in the boolean costs a rebuild per change of angle, the swing cut, and a mesh nobody can animate.

## Decision
1. **A door's leaves are objects of their own.** A styled door with Leaf on gets one child object per leaf under its
   piece object: "Leaf" (Double: "Leaf L" and "Leaf R"; Revolving: "Wings", one object of four wings). Each leaf object
   has a `MeshFilter`, a `MeshRenderer` and a new component **`BlockoutLeaf`** (`src/scene/blockout_object.h`) that
   marks it as made by the door: its index and the numbers its mesh was made from. It has no `BlockoutPiece`, so the
   kernel never sees it (ADR 0129 decision 2 - only pieces are built).
2. **The kernel builds the doorway without the leaf.** The door's cut is the whole clear opening inside the frame; the
   frame stays Blockout matter (Solid). There is no swing cut any more: `state.swing_cuts`, `note_swing_cuts` and the
   status line "its open leaf cuts ..." go. A leaf that swings into a column overlaps it, as two objects can.
3. **The leaf's pivot is its hinge.** A hinged leaf's object origin is on the hinge line (the jamb's inner edge, at the
   floor, at the frame's middle depth), its local Y the Blockout's up; a Sliding leaf's origin is its closed place; the
   Wings' origin the drum's axis. **Open Amount sets the leaf's local rotation (or offset) only** - no mesh is made and
   no build is asked for: Single / Double turn Open x 90 degrees about Y into the room the door faces; Sliding moves Open x
   its width along the wall; Wings turn Open x 90 degrees. A leaf's mesh is made again only when a number of its shape
   changes (width, height, thickness, style, Leaf Thickness): a box (a slab) with round 482's UVs, in the Blockout's Leaf
   material (the Leaf class keeps its slot and its override).
4. **Both ways.** Turning a leaf object about its hinge with the Rotate tool (or a Play-mode script, or an animation)
   moves it like any object. When the editor sees a leaf's rotation about Y differ from its door's Open Amount, it writes
   Open Amount back (clamped to 0-1) in the same undo step - so the Inspector shows the angle the user swung it to.
   Other changes the user makes to a leaf's transform are kept and not overwritten until Open Amount is typed again.
5. **The door owns its leaves.** They are its children, so moving the door, its wall or the Blockout moves them; duplicate,
   delete, copy / paste and undo take them along with no code of their own. The editor keeps them in step with the door
   (on each change of the scene, the same mark as the build hash): a missing leaf is made, a leaf whose door has Leaf off
   or no hinged style is removed, a leaf the user deleted turns that door's Leaf off (one undo step, said in the status).
6. **Bake to Mesh** bakes the Blockout without the leaves (they never were in it) and leaves the leaf objects as they
   are, moved under the baked object with their world place kept; Unbake puts them back under their door. They stay
   separate objects - said in the status and the Learn section.
7. **Light and collision: a leaf is a dynamic object.** `contribute_gi` off (not in the lightmap bake), lit by light
   probes (the Light Probe Groups of round 455) like any moving object; it casts and receives real-time shadows as other
   renderers do. The wall's lightmap is baked with the doorway open (Unity does the same with a door prefab). A leaf gets
   a Box collider fitted to its slab and a kinematic Rigidbody without gravity, so in Play mode its body moves when its
   transform does (a collider alone is a static body in the physics, built once, which would leave a closed door's box
   in a doorway swung open - the round's review).
8. **Export.** A piece is never exported (ADR 0129 decision 3). Blendity's exporters (OBJ, FBX, GLB) write every mesh
   object as its own node and keep no parents, so a leaf is its own node beside the Blockout's - not its child, as first
   written here - with its pivot at the hinge and, with Keep Transforms, its matrix at the current Open Amount: in Unity a
   Rotate on its Y swings it. A hierarchy in export is a change to the exporters for every object, not this round's.
   Its collider is named for the engine as other colliders are.
9. **Windows are unchanged** (their glass and bars stay in the mesh: nothing moves).
10. **Old scenes.** A scene saved by round 499's build (unmerged, but the owner's local Blendity carried it) has styled
   doors whose leaf was in the mesh: on open, the editor makes their leaf objects (decision 5) and the Blockout rebuilds
   without the leaf - the mesh of those doors changes, the door looks the same. Scenes from before round 499 have no
   styled doors and load to the same bytes.

## Consequences
- Open Amount is a transform write: a swing drag is a frame of the editor, no build (the 16.7 ms budget holds trivially).
- The 2,000-door sweep's open rows lose their meaning for the leaf; the sweep keeps the doorway (frame and cut) and adds
  a row that the leaf objects follow their doors through moves, duplicates, deletes and undo.
- ADR 0129's "no child objects" for a styled piece now has one exception, the leaves, made and kept by the editor.
- A user who opens many doors sees more objects in the Hierarchy (one or two a door); they sit under the door's piece,
  folded.
- The Leaf material class remains, applied to leaf objects rather than Blockout faces.

## Alternatives
- **Keep the leaf in the boolean (round 499):** every angle is a rebuild and the swing cut stays; the owner said no.
- **An Add leaf after the cuts (a per-piece build order in the kernel):** fixes the cut but still rebuilds per angle and
  still cannot be animated outside Blendity.
- **A render-only leaf the editor draws but does not save:** cheap, but Unity export, collision and the owner's animating
  need a real object.

## Not decided here
- Swinging windows (casements) as parts - the same mechanism would serve; not asked for.
- Door hardware, glass in a leaf, a leaf's own material per door beyond the Leaf class override.
