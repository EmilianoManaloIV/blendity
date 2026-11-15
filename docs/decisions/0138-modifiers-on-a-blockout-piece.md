# ADR 0138: Modifiers on a Blockout piece

- **Status:** accepted 2026-11-14 (round 514, task 0496; the owner, through the hub: "Approve 514 and 515" - the stack on the
  piece's object, no stacks on rooms, doors / windows and Intersect pieces at first, saved modifiers on pieces start working)
- **Date:** 2026-11-14
- **Amends:** ADR 0129 (the Blockout object: a piece has no mesh of its own), ADR 0128 (the kernel's mesh pieces)
- **Builds on:** round 502 (a modifier stack on the Blockout object), ADR 0107 (stacks evaluated on a worker)

## Context
The owner (2026-11-14, through the hub): "Yes, per piece modifiers would be a great implimentaiton to add." - Bevel just the
arches, Array just a column, Mirror one wing of a plan, while the rest of the Blockout stays as it is.

Today a piece is numbers (ADR 0129): a box's size, a cylinder's radius, a room's walls. The kernel (ADR 0128) turns each
piece's numbers into an exact solid in the piece's own space and combines them; a piece made from any mesh (`Shape::Mesh`,
round 480) enters the kernel as that mesh, held, with a `mesh_key` for its cache. A modifier component can already be put
on a piece's object (it is an ordinary GameObject), but nothing reads it: a piece has no MeshFilter, so the stack is
inert and the Inspector shows no Modifiers section for it.

## Decision
1. **Where the stack lives: ordinary modifier components on the piece's own object** - the same `BevelModifier`,
   `ArrayModifier` ... an object carries, in the same order, with the same header toggles. So saving, opening, undo,
   Duplicate (Ctrl+D), Copy to Selected and the console's `component <Type>` work as they do for any object, with no new
   data field. (Rejected: a list inside `BlockoutPiece` - a second copy of every modifier's fields, serializer and
   Inspector for the same thing.)
2. **What it does:** a piece whose object carries at least one enabled modifier that the piece takes (decision 4) enters
   the kernel as a mesh piece: the piece's own shape, made from its numbers by the kernel's own shape code (the solid it
   builds today, as a mesh, in the piece's own space), then the stack run over it top to bottom (in the piece's space: a
   Mirror mirrors across the piece's planes, an Array steps along the piece's axes), then added, cut or - not at first,
   see 4 - intersected like any piece. Its `mesh_key` is a hash of the numbers and of the stack, never of the piece's
   place: moving or turning the piece re-uses the evaluated mesh; changing a number or a modifier evaluates it again.
3. **Where it runs: on the Blockout build's worker**, before the kernel (the snapshot hands the job the piece's numbers
   and a clone of its modifiers; the job evaluates the stacks whose key it does not hold and keeps them in the
   Blockout's build cache). The main thread does no stack work for a piece (ADR 0107's rule for slow work; the
   responsiveness rule: no main-thread stall).
4. **Which modifiers a piece takes:** every modifier that changes a mesh by itself - Bevel, Array, Mirror, Solidify,
   Subdivision Surface, Displace, Decimate, Triangulate, Weld, Wireframe, Screw, Cast, Simple Deform, Smooth. **Refused
   (shown off in the piece's Add Modifier list, with the reason):** modifiers that read another object (Boolean - the
   Blockout is the boolean; Curve, Lattice, Mesh Deform, Armature, Normal Transfer) and the ones that only change shading
   (Smooth Normals - the kernel makes the shading of the result). **Pieces that take no stack at first:** a room (its
   shell and air come from its numbers, and a mesh is never a room in the kernel), a door or window (a cut sized by its
   width, head and sill), and an Intersect piece (the kernel skips a mesh piece as Intersect). A stack put on such a piece
   by the console stays on it, inert, and the Blockout's status names the piece and says why.
5. **What it costs and what it loses** - said in the Inspector and the lesson: a piece with a stack builds as a mesh: on
   the exact path through Manifold like any mesh piece (round 480: a 10,000-face mesh piece costs about 60 ms more in
   `interior rooms 100`), on the grid path as coarse as the cell; its Blend radius is not read; flat faces it makes still
   merge with its neighbours' in one plane, curved ones are as fine as the modifier made them. A stack whose result is
   not a closed surface is refused by the kernel's existing mesh-piece rules (the piece is skipped, the status says so).
6. **With the Blockout's stack (round 502):** piece stacks first (each on its own piece, before the pieces are combined),
   the Blockout's stack last (over the combined result). Picking follows round 502's rule on the drawn mesh.
7. **Picking and handles:** a piece's handles and its wire stay on its numbers (the shape before its stack); a face the
   piece's stacked mesh made names that piece, as a mesh piece's faces do today.
8. **Bake to Mesh, Unbake, export, lightmaps:** the result holds the piece stacks' work; Bake keeps each piece's stack on
   the piece under "Blockout Pieces" and Unbake brings it back live; OBJ / FBX / GLB and Generate Lighting read the
   result as before.
9. **Old scenes:** a piece with no modifier builds bit for bit as before. A scene saved with a modifier on a piece (inert
   until now) shows that modifier's effect from this round on - the change is named in the changelog.

## As built (round 514, after review)
- The snapshot only notes a stacked piece (its place in the piece list, the key, clones of its modifiers, carried by the
  `BlockoutSurface` the build already receives); `to_piece` is unchanged. The build (its worker, or the synchronous
  build of tests and scripts) turns the noted piece into a mesh piece just before the kernel runs.
- The evaluated meshes are kept in one process-wide cache keyed by the piece's numbers and its stack (so the same column
  in two Blockouts is evaluated once), bounded at 512 entries and 64 MB with the least recently used half dropped past
  either, guarded by one mutex, emptied by a new or opened scene. A stack that gives no shape is kept too, as its reason,
  so it is not run again at every build; the status names the piece and why, and the piece builds from its numbers.
- Decision 5 refined: a stack's result is tidied (points at one place made one, faces exactly on top of each other the
  opposite way round dropped - copies of an Array that touch - and the same face twice kept once), then refused, with the
  piece built from its numbers, when it is not one closed surface, is flat or has fewer than four faces. The kernel is
  not left to send the cluster to the grid for an open mesh.
- A stacked piece is a mesh piece for the kernel, and a mesh piece turns off the Blend radius of the pieces in its
  cluster (round 480's rule): a Bevel on a column beside a blended piece removes that neighbour's rounding; the kernel's
  note reaches the status. Its faces shade smooth by the 40-degree Auto Smooth angle (a stacked cylinder is not faceted).
- An Array that fits a curve does nothing on a piece (the clone has no scene to find the curve); the status says so.

## Consequences
- A piece's Inspector gets the Modifiers section an object has; the kernel gains one public function (a piece's shape as
  a mesh) and reads nothing new; the build job evaluates stacks and caches them by key.
- A piece with a stack is slower and less exact than a numeric one; the status and the lesson say so, and the stress
  rows measure it.
- Not decided here: stacks on rooms, doors and windows, and Intersect pieces with a stack (a later round, if wanted);
  Apply on a piece's modifier (it would have to turn the piece into a mesh piece of its own - refused at first, with
  the reason).
