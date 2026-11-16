# ADR 0150: Modifiers on a door or window

- **Status:** accepted 2026-11-16 (round 546, task 0526; the owner, through the hub, approved the recommendation: "Array
  and mirror repeat the whole opening" - the numbers stay one opening's size, repeats copy the whole opening through
  `blockout_opening_placements()`, shapers reshape the hole only, repeats run after shapers)
- **Date:** 2026-11-16
- **Amends:** ADR 0138 (modifiers on a Blockout piece: "no stacks on doors / windows at first")
- **Builds on:** ADR 0129 (the Blockout object), ADR 0128 (the kernel), ADR 0136 (a door leaf is its own object),
  ADR 0135 (a Blockout takes scale in its numbers)
- **Next to:** ADR 0149 (round 547: a door's / window's parts - leaf, frame, glass - as objects with their own stacks)

## Context
The owner (2026-11-15): "Why can't I add modifiers to windows and doors?", then "Lets do both" - the opening itself
(this ADR) and its parts (ADR 0149, round 547).

Round 514 refuses a stack on an opening ("a door or window is a cut sized by its Width, Head and Sill",
`blockout_piece_stack_refusal`). What an opening is today:
- **A plain opening** (Opening Only) is one kernel piece: a Subtract box `width x (head - sill) x depth`, centred between
  Sill and Head, through the wall (`BlockoutPiece::to_piece`). It already passes the place where round 514 notes a
  stack (`collect`), so lifting the refusal alone would run a stack on its box.
- **A styled window or door** is several generated kernel pieces from its numbers (`blockout_window_pieces`,
  `door_pieces`): Subtract reveals that make the hole, Add frame parts (the Blank), Subtract lights per pane, a sill
  board; a door's leaf is a child object of its own (ADR 0136) that the kernel never sees.
- **Width, Head, Sill and Depth** are read by more than the cut: the Inspector, the size handles (Width, Head, Sill),
  F9, the console, Join's overlap test, the yellow cut outline in the Scene view, the leaf's plan
  (`blockout_leaf_plans`), and Draw Piece's "not inside an opening" check.

The owner's examples: a Bevel that rounds the hole's edges; an Array that repeats a window along a wall, each copy a hole
with its frame (and a door's leaf); a Mirror.

The hub's design question: how a stack meets Width, Head and Sill - either the stack changes the cut's size freely
and the Inspector says so, or the numbers stay the size of the first copy.

## Decision (recommended)
1. **The numbers stay the opening's own size: Width, Head, Sill and Depth are the size of the first copy before its
   modifiers.** The handles, F9, the console, the leaf's plan and Join keep reading them unchanged; the Inspector's
   Modifiers section says what the stack does to them ("Width, Head and Sill are one opening's size, before its
   modifiers"). (Rejected: letting the numbers describe the result - a Bevel or an Array has no single width, and
   every reader above would need the evaluated shape.)
2. **Two kinds of modifier on an opening:**
   - **Repeats - Array (Offset or Radial, Fixed Count or Fit Length) and Mirror** - repeat the whole opening. Each copy
     is the opening as its numbers build it: the hole, the frame, the glass, the sill board, and a door's leaf. They are
     read as a list of placements in the opening's own space (Array's relative offset in multiples of the opening's
     size, `width x (head - sill) x depth`, as Blender's is of the mesh's bounds; its constant offset in metres; a
     Mirror across the opening's own planes - X through its middle, Y through its floor line, Z through the wall's
     middle), and each copy's pieces are emitted at its placement. **Nothing is evaluated as a mesh:** every copy stays
     exact numbers, as cheap as that many openings, and the cut stays crisp on the exact path.
   - **Shapers - every other modifier a piece takes (round 514's list: Bevel, Subdivision, Displace, Cast, Simple
     Deform, Smooth, Triangulate, Weld, Decimate, Solidify, Screw, Wireframe)** - change the hole: they run on the
     hole's solid (a plain opening's box; a styled opening's reveals, which are the whole hole) as round 514 runs a
     piece's stack, and the result is cut as a mesh piece. The frame, glass, sill board and leaf do not take them -
     those are round 547's parts, each with its own stack (ADR 0149).
3. **Order:** an opening's shapers run first, in their order, on the hole; its repeats then place copies of the
   shaped opening, in their order. A repeat above a shaper in the stack still repeats last - the Modifiers section says
   so on such a stack. (Rejected: running the stack strictly top to bottom - "Array then Bevel" would bevel one
   merged mesh of several holes and lose the frames, for a difference the owner's examples never need.)
4. **What each copy is:** a full opening - it cuts the wall it lies in. A copy that lands outside every wall cuts
   nothing and is named in the status ("copy 3 of 'Window' cuts no wall"); a copy that overlaps another copy is cut
   once (the kernel unites cuts). A door copy gets its own leaf: the leaf objects (ADR 0136) follow the copies - one
   leaf child per copy, made and placed by the same leaf sync, named "Leaf", "Leaf (2)"... (round 547 owns the leaf
   code: see "Shared files").
5. **The Scene view:** the yellow cut outline is drawn for every copy; the size handles stay on the first copy (the
   piece itself); picking a copy's face selects the opening piece.
6. **Refused on an opening:** an Array that fits a curve (as on any piece), and repeats past 256 copies in all (the
   status says so and the opening builds as one copy). A shaper whose result is not a closed solid leaves the hole as
   its numbers, said in the status (round 514's rule).
7. **Saved scenes:** the stack is ordinary modifier components on the opening's object (ADR 0138 decision 1); a
   modifier already saved on an opening (inert until now) works when the scene opens, as round 514 did for pieces.

## Alternatives
- **A. The stack runs on the cut alone, as on any piece (lift the refusal, nothing more).** Simplest - one line plus
  tests. A Bevel rounds the hole; an Array of a plain opening makes several holes. But a styled window's Array copies
  only the hole: the frame, glass and leaf stay at the first copy. Fails the owner's "each copy a hole with its frame".
- **B. The numbers follow the stack** (Width / Head / Sill describe the result; the Inspector greys them when a stack
  changes size). No single number describes an Array or a Bevel's result; the handles, F9, the leaf plan and Join would
  each need the evaluated shape. More work, less clear.
- **C. A "Repeat" setting on the opening (count, spacing) instead of Array / Mirror.** Exact and simple, but it is not
  a modifier: the owner asked for modifiers, and a second way to repeat beside Array is one more thing to learn.
- **D. The whole stack runs on one mesh of the opening (hole and frame together).** Frames, glass and the leaf would
  become one mesh cut and added at once - but a mesh piece is Add or Subtract, not both, and the leaf is not a kernel
  piece. Not buildable without splitting it again into what decision 2 does.

## Consequences
- An Array of windows along a wall is as cheap and as exact as placing the windows one by one; a Bevel on a hole costs
  what round 514's stacked pieces cost (one evaluation per change, cached by numbers and stack).
- Width / Head / Sill keep one meaning everywhere; the Inspector explains the stack's effect in words.
- The status names copies that cut no wall.
- Leaf copies need the leaf sync to make one leaf per door copy - shared with round 547's work on the leaf.

## Agreed with round 547 (2026-11-16, through the hub and with its worker)
- Round 547 makes a styled opening's cut one clear hole, `blockout_opening_cut(bp, to_blockout, out, grid_cell)`, called
  from `collect_window` and from `collect`'s opening branch; its frame, glass and leaf become objects of their own (ADR
  0149). So this ADR's "every generated piece emitted per placement" is, after 547, the clear hole per placement, and
  the shapers run on that hole; round 547's part sync makes one Frame / Glass / Leaf set per placement.
- The shared function: `std::vector<Mat4> blockout_opening_placements(const GameObject &opening)` in
  `blockout_piece_stack.{h,cpp}` - identity first, in the opening's own space. Whichever round lands first adds it
  (round 547 as a one-placement stub).
- This round changes only the `blockout_opening_cut` call sites (a loop over the placements) in Pass 23's files.
- The hole the shapers take: after round 547 every styled window and door but Revolving emits exactly one Subtract
  piece for its hole, part `kPartHole` (20 in `WindowPart`): a Box, or an Outline prism for an Arched or Round head. A
  Revolving door's hole is its `kPartDrum`. A plain opening's hole is its own box. The shapers run on that piece only.

## Shared files (overlap with round 547)
- This round's own: `src/scene/blockout_piece_stack.{h,cpp}` (the opening's split into repeats and shapers, the
  placements), `tests/test_blockout_opening_modifiers.cpp` (new group), `stress/stress_blockout_opening_modifiers.inc`
  (new section), the changelog fragment, a Learn lesson section.
- Call lines in Pass 23's files, in regions to be cleared: `blockout_object.cpp` `collect` (the plain opening's and
  `collect_window`'s pieces emitted once per placement), `blockout.cpp` `piece_shape_mesh` (the hole of a styled
  opening), `blockout_editor.cpp` the cut outline per copy, `panels.cpp` the opening's Modifiers-section text.
- **Overlaps round 547:** the leaf per copy (decision 4) - `blockout_leaf_plans` / the leaf sync in
  `blockout_editor.cpp` (round 547 changes the leaf, frame and glass). Proposed split: round 547 owns the leaf code;
  this round asks it for one plan per placement through a single function both use (`blockout_opening_placements`,
  in this round's file), or this round lands after 547 and adds the per-copy loop then.
