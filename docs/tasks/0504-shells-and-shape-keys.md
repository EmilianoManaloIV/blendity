# 0504: Outline shells and shape keys on the card's skin (Pass 26, round 521)

- **Status:** done (round 521, published; review: approve)
- **Requirements:** Q-04, Q-06
- **Decisions:** ADR 0146 (skinning on the card; an addition to point 4 below), ADR 0020 (toon outlines: the inverted hull),
  ADR 0108 (approximations on the card, listed per view), ADR 0112 (the 1.3x bar)
- **Model:** implementer (the editor and the raster's one new call), main session reviewing; test-engineer; code-reviewer
- **PR:** stacked on #457 (round 520); base main

## Goal
A posed toon character keeps its outline on the card with no shell built on the CPU a frame (round 518's baseline: outlines
2.5 ms a posed frame, CPU and card; round 519 left the card falling back to the CPU's shell), and a blink or a viseme while
posing stays on the card instead of sending the character back to the CPU while its shape keys move (round 519's
`kSkinRestStill` fallback).

## Decisions in this spec
1. **The shell of a posed skin is the rest's shell, skinned.** `Outline::shell` of a posed mesh builds the inverted hull of
   the pose's *rest* (pushed along the rest's smoothed normals, the thickness group's widths; cached by the rest, the
   offset and the group - a pose doesn't rebuild it) and poses it with the same bones and weights: the shell `Mesh` has the
   source's vertices one for one, so the source's `SkinBinding` applies unchanged. On the card it is a skin of its own (a
   second stand-in, the body's palette); on the CPU its render mesh is round 518's kernel on the rest shell. So the CPU stays
   the card's exact reference (ADR 0146 point 4), and both differ from today's shell (pushed along the *posed* smoothed
   normals) by the blend of the push: exact for a vertex on one bone, the linear blend's shrink elsewhere. Listed as an
   approximation (ADR 0108's list, the Learn lesson) with its measured size on the stand-in (the worst offset against
   today's shell, as a fraction of the thickness).
2. **The thickness's distance steps** (task 0164's 2 % steps with hysteresis) rebuild the rest shell when the step changes,
   as today; a drag at one distance builds none.
3. **Shape keys under the skin go up as ranges.** When the rest's render vertices move (a shape key's weight changed) and
   the topology holds, the stand-in's rest on the card is written in the changed vertex ranges (a new raster call,
   `update_skin_rest(mesh, vertices, ranges)` - the rest buffer's ranges uploaded and the skin dispatched that frame) and
   the bone boxes grow to hold the moved vertices (rebuilt whole once the rest holds still). The `kSkinRestStill` CPU
   fallback goes for this case; it stays for a card that refuses ranges and a render vertex whose UVs change (a flipped diagonal goes up as index ranges, a changed vertex count whole). Blend shapes computed *on* the card stay out
   unless a measured row gains 1.3x (ADR 0112) - the round measures a blink while posing and says.

## Scope
- In: `Outline::shell` (`src/scene/scene.cpp`, `scene.h`) for a posed source; `outline_draw_item` (`render_view.cpp`) and
  the card gather (`gpu_skin.cpp`, `gpu_views.cpp`) making the outline a card-skin item; exports' shells (`editor.cpp`
  `keyed_shell`, slot 3) write the posed shell; F12 / path tracers get it posed (lazily, as round 520's render meshes);
  `gpu::Raster::update_skin_rest` (`gpu_raster.h`, `gpu_raster.cpp`: the rest buffer's ranges, a re-dispatch; no new
  binding, no change to `init` or the pipelines); `skin_card_item`'s shape-key path; Profiler counts (outline shells built,
  rest ranges sent); the Learn tab's posing lesson (the approximation, what the card does now).
- Out: the timed rows (round 522); blend shapes computed on the card (unless measured, above).

## Acceptance criteria
1. A posed toon character on the card (the stand-in `benchscene avatarlikequick` with an Outline): over posed frames, 0
   outline shells built (`outline_shells`), 0 render meshes posed on the CPU, the outline drawn by the card as a skin.
2. The card's posed shell against the CPU's (both the rest shell skinned): within 1e-5 of the diagonal (positions), the
   card test of round 519's kind. The CPU picture of a posed outlined character against today's shell: the worst offset
   printed as a fraction of the thickness; a one-bone part exact.
3. Changing the thickness, the colour, the thickness group or the distance step rebuilds the rest shell once; posing
   doesn't. An outline on a mesh that isn't skinned is built exactly as before (bit for bit).
4. A shape key's weight changed while posed on the card: the character stays on the card (no `skin_note` fallback), only
   the changed ranges go up (a count: vertices sent < the mesh's), the posed picture equals the CPU's. A flipped quad
   diagonal stays on the card (`update_skin_triangles`); a changed render-vertex count goes up whole on the card; what falls
   back to the CPU, with its reason, is a card that refuses ranges or a render vertex whose UVs change. A blink builds no
   outline hull (the rest shell follows in place). Its frame time printed (no assertion).
5. Exports of a posed outlined character write the posed shell; F12 renders it.
6. Full unit suite on Windows and Linux; stress `posing`, `animation`, `editor`, `editor_render` no new problems.

## Tests
`tests/test_animation.cpp` (CPU: the shell of a posed skin, its cache across poses, the unskinned shell unchanged, exports,
F12, shape keys' ranges through the test hooks), `tests/test_gpu.cpp` (card: the shell as a skin, 0 shells built, card vs
CPU shell, shape keys on the card).

## What was built (round 521)
- `Outline::shell` of a posed skin (`scene.cpp`, `Outline::posed_shell`): the rest's hull (`outline_hull`, built from the pose's
  rest mesh - which has dropped its vertex groups, so the Thickness Group's weights come from the posed mesh) is cached per
  slot by the rest (object, version, deform version), the offset and the group; each new pose makes a bare copy of it that
  carries a `SkinPose` copy (the body's bones and binding, `rest` = the rest shell, the shell's own `rest_lock`) and whose
  `positions` are posed by `skin_pose_positions` (the deform's blend), so whoever reads positions (exports, bounds, the dual
  quaternion box rule) sees the posed shell. Only the rest's build counts as `outline_shells`. A mesh that isn't a posed skin
  takes the old path, bit for bit (a test recomputes a cube's hull).
- `outline_draw_item` (`render_view.cpp`): when the body's item is a card skin, the shell is one too (`skin_card_item`, stand-in
  kinds 2 and 3 apart from the body's 0 and 1; `kSkinStandsAnObject` 3 -> 6); otherwise its render mesh, posed lazily by the
  kernel. F12, the path tracers and the CPU views use the latter; exports write the posed copy's positions (OBJ test).
- `gpu::Raster::update_skin_rest(mesh, vertices, ranges)` (the skin's rest buffer written in ranges, the skin dispatched
  again, no new binding) and `update_skin_triangles(mesh, indices, ranges)` (added: see Doubts). `skin_card_item` keeps a rest
  that moved with its render vertex count held on the card: the render vertices that changed are joined into ranges
  (`SkinStand::moved`, gaps under 8 vertices joined), the stand-in takes the new rest, the bone boxes grow
  (`skin_bone_boxes_grow`) and are made whole once the rest has held still for 3 gathers; `gpu_skin_mesh` sends the ranges
  (and the index ranges a flipped diagonal changed). The `kSkinRestStill` CPU fallback stays for a card that refused ranges.
- Profiler: "skin rest updates / ranges sent / vertices sent" and "skin triangle ranges sent"; the Learn lesson's
  three paragraphs.

## Measured (round 521)
- Approximation (the stand-in avatar, chest turned 0.5 rad, 4 outlined skins): the skinned rest shell is at most 43.7 % (about 44 %) of
  the thickness from today's shell (the one-bone glove: 0, to 1e-5; the test tube's body, two bones blending: 20.9 %).
  Where bones blend the push points another way (linear: shorter; dual quaternion: the same length, tilted).
- CPU cost of a pose's shell on the stand-in (4 outlined skins, summed): 0.37 ms (a bare copy + the blend) against 2.37 ms to
  build today's shell (normals, faces, and then the render mesh built whole); the render mesh of the shell is the kernel's.
- 0 outline shells built and 0 render meshes skinned over six posed gathers of the stand-in (24 outline items, all skins).
- A blink while posing (CPU side, no card): on the test tube, 24 of 72 render vertices moved (the shell 28-36 of 72), kept as
  ranges; on the stand-in avatar's head every shape key is dense, so one key moves all 3,056 render vertices (still one
  range, 147 KB, no skin sent again and no CPU pose). Frame times on the card: the test `tests_round521_gpu` prints the blink
  frames and the posed frames without one - to be run in the hub's card window.

## Doubts
- A weight that changes how a quad splits (a diagonal flips) changes the triangles with their count held. The lead's spec
  named only vertex ranges; a blink on a real face would flip some, so I added `update_skin_triangles` (index ranges) rather
  than fall back to the CPU. A weight that changes the render vertex count (corners split another way) is a new rest: the
  skin goes up whole, once, on the card (not on the CPU).
- "Colour rebuilds the rest shell once" (criterion 3): the colour is in the material, not the shell's shape, so it rebuilds
  nothing (before and after); the test pins that. Thickness, group and the distance step rebuild once.
- ADR 0108's per-view list is for card-versus-CPU differences; this one is the same on both (both are the rest shell,
  skinned) and differs from the earlier shell, so it is in the Learn lesson, not in `gpu_view_approximations`.
- Not measured here: outline pose cost as its own Profiler row (the copy and blend are inside the gather's time).
