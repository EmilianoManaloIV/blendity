# ADR 0125: The world stays kept while a character moves - the card's instances and see-through lists by item

- **Status:** accepted (round 471, task 0447; approved by the user 2026-11-10: "I approve all specs from all
  passes"). **As built it is narrower than the draft that was approved** - see "What was measured first".
- **Extends:** ADR 0115 (the scene's change counter) and round 458's kept instance array
- **Builds on:** ADR 0009 (render caching by hash), ADR 0105 (the performance suite), ADR 0109 (baked corners kept on
  the card)
- **Requirements:** Q-04 (interactive editor), Q-06 (responsiveness)

## Context
ADR 0115 gave the scene one counter: while it holds, the views keep their scene hash and their draw items, and round
458 keeps the card's instance array while every item holds still. A posed character changes something on every
frame, so in a world everything was made again for every object - and in a baked or see-through world the instance
array was never kept in the first place (`InstanceKeep` gives up when an instance is baked or see-through).

The draft of this ADR answered that with a list of what moved, read by the scene hash, the draw items, the instance
array and the lighting key.

## What was measured first
Round 471 began by naming the card's own parts in the posing rows (`card shadow maps`, `card g-buffer`, `card
lighting`, `card copies and wait`, `card render call`, `baked corners`, `see-through triangles`, `presenting`). On
the owner's world (SideRail, 743 objects, 430,000 triangles; one untimed run on a busy machine - proportions, not
times) an orbit frame's card path of 10 to 15 ms is:

| part | ms |
|---|---|
| the card's render call (g-buffer 2.5 to 3.7, lighting 1.2 to 2, copies and wait 1 to 3.6) | 6 to 10 |
| the instance array, made again | 2.1 to 3.1 |
| see-through triangles: transformed, keyed, sorted, their corners filled | 1.6 to 2.3 |
| draw items | 0.1 to 0.2 |
| scene hashes | 0.2 to 0.9 |

A posed frame in the world carries the same parts, plus the avatar's own. So most of "the world's share" is the card
drawing the world, which a posed frame owes like any other; what the editor does again for nothing is the instance
array and the see-through lists, 4 to 5 ms. The hash and the draw items - what the list of moved objects was for -
are under 1.5 ms together.

## Decision
1. **The instance array is kept by item** (`Editor::InstanceRows`, one a view slot). A row is one drawn item's
   instances in the last frame's array. An item whose words (`instance_item_sum`: placement, flags, the mesh's
   address and serial, the materials' versions, the lightmap pages' addresses and serials) and mesh id on the card
   are its row's takes a copy; any other is made again. Baked and see-through scenes included - a copy carries its
   lightmap ids and corner base.
2. **The card ids inside a copy are not asked again.** Every mesh, texture or corner block that leaves the card
   bumps one generation (`card_ids_gen_`) and the rows are dropped; every 64th draw call gathers whole, which marks
   every id drawn (unused ones leave after 256 calls). The meshes are asked for on every frame as before - that is
   what sends a moved mesh.
3. **Per-frame items are made again every frame**: a face highlight, vertex colours, a volume, a live procedural
   texture, an axis material's parts, a texture still waiting for the upload budget, a mesh with no serial.
4. **See-through lists by item.** A row keeps its see-through triangles with their sort keys; they are copied while
   the item's job holds (the view's matrix times its own, its culling, its fog side). When no list was made again and
   every row sits where it sat, the sorted corners are the last frame's (handed back from the frame after the
   render). A camera move makes every list again, as before.
5. **The whole-array keep of round 458 stays first.** A frame that copied rows can still leave it (the rows carry
   their textures for its list), so a change gathers once and then settles as before.
6. **Verify mode** (the render cache's) gathers a by-rows frame whole as well and compares instances and see-through
   triangles; a difference is counted as a stale picture.

## Not built, with the reason
- **A list of what moved, the scene hash by object, draw items by object** (the draft's decisions 1 to 3): 0.3 to
  1.1 ms of the owner's world's frame. The counter and the whole gather stay.
- **Instances sent to the card in ranges**: the array goes up whole when any row changed (a memory copy of a few
  hundred kilobytes); not seen in the parts.
- **The lighting key left alone for a move outside the bake** (decision 5) and **clips not "volatile"** (decision 6):
  `lighting key` reads 0.00 ms on every posing row of rounds 468 to 470; the "volatile" walks are round 472's rows.

## Consequences
- In a baked or see-through world a posed frame makes instances for the character's objects only, and its
  see-through triangles are not transformed and sorted again while the camera rests.
- A kept row is a stale INSTANCE if something `instance_item_sum` does not read changes what `raster_instance`
  makes: the verify mode compares every frame in the tests and the fuzz. A new field read by `raster_instance` or
  the baked path belongs in the sum.
- The kept state is a second copy of the instance array a view slot, the rows, and the see-through corners of the
  last frame.
- The world's own frame on the card (its g-buffer and lighting passes, the copies and the wait) is untouched: it is
  the larger part, and the next thing to look at for a posed frame in a world.
