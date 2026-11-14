# ADR 0135: A Blockout takes scale in its numbers, never in a transform

- **Status:** accepted (2026-11-13; round 496, task 0480). The owner's words "Go ahead with A + B", relayed by the hub
  session, after the study of how a scaled Blockout shears.
- **Builds on:** ADR 0129 (the Blockout object: pieces are objects under it, their matrices in its space); ADR 0085 (HCI
  rules); ADR 0056 (a measurement is a stress row - nothing here is timed)
- **Requirements:** U-01 (the tool does what the cursor says), Q-01 (robust geometry), C-02 (every tool from the console)

## Context
The owner: "Scaling for the block out objects is weird, especially when I want to scale the object using the local
axis." - "It sheared."

A piece's own transform is a position, a rotation and a scale, applied in that order: it cannot hold a shear. A shear
comes from a parent. The kernel is handed each piece's matrix in the Blockout's space - the product of the local
matrices from the piece up to the Blockout - so a folder or a parent piece scaled unevenly, with a turned piece below it,
puts a shear into the piece the kernel builds; and the Blockout's own scale does the same to the mesh in the world. It
was reproduced with the Blockout scaled 2 1 1 and a box turned 30 degrees inside it: no face of the result lay square to
the box's axes.

The Scale gizmo and the modal S both wrote a transform's scale (`gizmo_update`, `transform_apply`), whatever the object.
So a Blockout scaled by hand sheared its turned pieces, and a piece scaled by hand kept the scale in its transform beside
its size: **two sources of truth** (a box that says Size 2 2 2 with Scale 2 1 1; a cylinder that is an ellipse and says
"Radius 1"; a room whose wall the kernel measured square to the piece's sides in the Blockout's space while the air
stretched).

## Decision
1. **Scaling a piece changes its numbers, along its own axes, and its transform's scale stays 1 1 1**
   (`blockout_piece_scale`, `blockout_object.cpp`): a box's, wedge's and stairs' Width, Height, Depth; a cylinder's radius
   and height; a sphere's radius; an arch's two radii and depth; an outline's points and height; an opening's width,
   sill, head and depth. A cylinder or sphere scaled along one of its round axes scales its radius - of two round axes
   the one that changed more, so it stays round. A room's wall, floor and ceiling are in the Blockout's space and are
   not scaled: the air grows, the wall keeps its thickness. A Blend radius scales by the smallest factor. A mesh piece
   has no numbers of its own: it keeps its transform's scale. A factor of exactly 1 changes no bit; a size stops at
   `kBlockoutMinSize` below and at `kBlockoutMaxSize` (10000 m, F9's limit) above, an outline's corner at plus or minus
   the same. A number for which 0 is a meaning keeps it: an arch's inner radius (0 is the filled arch the Subtract tool
   draws) scales with a floor of 0 and stays below the radius, as F9 keeps it; a sill stops at 0.
2. **Scaling what holds pieces scales the pieces.** `blockout_scale_collect` takes, of the selected objects, those that
   are a Blockout, a piece with numbers, or a folder / parent piece under a Blockout with pieces below it, and everything
   below them. Each object below has a role: a *Numbers* piece (any piece but a mesh piece: its numbers scale, its scale
   is 1 1 1 - a parent piece too, its children follow); a *Carrier* - the Blockout, or a folder with pieces below it
   (its own scale is left as it is: 1 1 1 for a new one); any other object below is a *Leaf* (a light, a mesh: its transform's scale grows by the
   same factor, as it did when the folder carried the scale). **An object with geometry of its own that holds pieces - a mesh piece, a plain mesh
   object, a baked Blockout - is a Leaf, not a Carrier**: its scale is its geometry's, so it grows, the pieces below it follow through their world
   columns (each grows by what `d` makes its axis longer than the parent's growth already did), and it is left out of `blockout_uneven_scales` and of
   Apply Scale to Pieces. The roots are taken shallowest first, so a child selected before its parent is folded after it. `blockout_scale_apply` takes the world matrix `d` of the scale about its pivot:
   every object's place goes through `d`, and each own axis of a piece or leaf grows by **how long `d` makes that axis**
   (`|d * column|` over its length now). For a scale along an axis the piece is turned against, the piece is
   scaled along each of its own axes by how much that axis grew - it is not sheared, and the cost is that its sides no
   longer grow exactly as the selection did (the lesson says so). Every frame of a drag is made from the objects as they
   were at the start (`local0`, `world0`, the piece's component): no drift, and a cancel puts them back byte for byte.
3. **The two places a scale is written take this path** for the objects the set holds: the Scale gizmo's handles
   (`gizmo_update`) and the modal S (`transform_apply`, with or without an axis, global or local). Any other object is
   scaled as before (the editor group pins it). One drag or one S is one undo step.
4. **A scale an older scene left is folded, once.** A piece with a scale is folded into its numbers the first time it is
   scaled (the set makes its transform's scale 1 and its numbers `|scale|` times the factor; a mirror's sign stays in the
   transform). The same fold is the piece's Inspector button **Apply Scale to Size** and `blockout applyscale size`. For a
   Blockout, a folder or a parent piece saved with an uneven scale, the Blockout's status says "'name' is scaled
   unevenly: turned pieces are sheared" and a button **Apply Scale to Pieces** (and `blockout applyscale`) folds every
   such scale below and including the Blockout's own - a scale of 1 on those objects, the pieces' places, world sizes
   along their own axes and numbers carrying it (`blockout_scale_apply` with `d` the identity and `fold`). One undo step. A scale axis of exactly 0
   stays 0 (the object is flat there; it is not "a scale to move").
   **A fold keeps the world shape, walls included.** A room's Wall, Floor and Ceiling are in the Blockout's space, so only the Blockout's *own* scale,
   taken out of the mesh's transform, would stretch them: folding it multiplies Wall by the square root of its X and Z scale (one factor for an
   uneven horizontal scale: the walls cannot all come out as they were), Floor and Ceiling by its Y scale, and the Blockout's Texture Scale by the
   cube root of its three, so a tile covers what it did. Folding a folder's or a piece's own scale changes no wall (the kernel measures a wall in
   the Blockout's space whatever the piece's matrix), and an ordinary scale by hand never does (item 8: a room keeps its wall). Field Cell is left as
   it is: it sets how fine a build is, not the shape.
5. **The tests drag the handles with real events**: `perf drag gizmo` learns `sx`, `sy`, `sz` and `sall` (the probe's
   point is the arrow's 60 % mark, or the pivot's square for `sall`, as for the Move arrows).

The 10000 m limit is not only a courtesy: a *turned* box 2 x 2 x 1e6 m, built directly with no scale gesture at all, kept the kernel busy for minutes
(1e4 m: 0.7 s, 1e5 m: 1 s; an unturned one takes the crisp path and is quick), and a typed scale of a million on a turned piece made exactly that
piece. A piece's place is not limited (the pivot is where the user put it), only its numbers.

## What can still shear
- **An object above the Blockout** with an uneven scale (the Blockout is not the owner of it): the status line names the
  object and offers no button.
- **A scale typed into the Inspector's Scale field, or set by the console's `scale`**: literal, as every transform edit
  is. On a Blockout, folder or parent piece with pieces below it the status line then shows the uneven scale and Apply
  Scale to Pieces folds it.
- **A scale on an object with geometry of its own** (a mesh piece, a plain mesh object) that holds pieces: it is that geometry's scale and stays; a
  turned piece below an uneven one is sheared as it always was (no status line: the object is not the Blockout's to flatten).
- **A scale already in a scene** until Apply Scale to Pieces (or a first scale of the piece) is used; the status says so.
- A folder or parent piece that keeps a scale while something below it is scaled (the Carrier's own scale is left as it
  was): the pieces' sizes are made to come out right in the world, but the shear of that scale stays.
- **A Blend radius is metres in the Blockout's space, like a wall** (round 496, second review): a fold (Apply Scale to Size, Apply Scale to Pieces, Turn's
  pre-fold) leaves it as it was, times only the Blockout's own folded scale; a piece's or folder's own scale is no growth of the fillet. A hand scale grows it by
  the smallest growth along the piece's world axes, not counting the Scale the piece carried.
- **A Blockout inside a Blockout**: the fold carries the product of the Blockouts' scales down (`outer`), so the inner room's walls and Texture Scale keep their
  world size.
- **A round piece (cylinder, sphere, arch) under an uneven fold** keeps the rest of the ratio on its own Scale (the radius takes the factor farther from 1, the
  other round axis keeps the quotient): the world shape stays and the piece's Scale is then not 1 1 1. A hand scale still makes it round (the rule above).
- **Turn** under an unevenly scaled parent does not keep the world box (the swap of two sizes is in the piece's space); Apply Scale to Pieces first.
- A scale factor that is not finite (a typed 1e39) leaves each place where it is and the sizes as they were.
- **A styled window's frame numbers are sizes too** (round 498): Frame Width grows by the rounder of the growths across and up, Frame Depth by the growth
  through the wall, the Sill Overhang and the Arch Rise by theirs; an overhang or rise of 0 stays 0 (0 means "none" and "a half circle"). A door's or window's
  Depth is the wall plus 0.1 m each side: only the wall's part grows, so the cut does not reach further past a scaled wall.
- Known limits of the Scale handle: `gizmo_scale_handle_hot` reads last frame's hover, so a press can be swallowed if the mouse left the handle in that frame,
  and `gizmo_hot_` is shared by the Scene views.
- Not a shear, but worth knowing: a turned piece under a one-axis scale grows along its own axes by what each grew, so a
  box turned 30 degrees and stretched along the world's X comes out wider in both its horizontal axes, not skewed.

## Consequences
- A piece's Size, Radius and the like are the truth about its size: the Inspector, F9, the handles, the stress sections
  and a saved file all agree, and a scaled Blockout stays as its scale was (1 1 1 if it was never scaled) so the Draw Piece tool (which refuses an unevenly
  scaled one) always works in it.
- The scale tools on a Blockout, a folder or a piece no longer write a transform's scale. Anyone who wanted the old
  behaviour types the Scale field or uses `scale`.
- The Blockout's mesh is rebuilt after a scale (the numbers are no transform write: the editor bumps the change counter,
  as the handles' drags do), and a drag is as cheap as a handle drag is.
- New: `blockout_piece_scale`, `BlockoutScaleSet` with `blockout_scale_collect` / `_apply` / `_restore`,
  `blockout_uneven_scales` (`scene/blockout_object.h`); `Editor::blockout_apply_scale`, `blockout applyscale [size]`;
  `perf drag gizmo sx|sy|sz|sall`.
