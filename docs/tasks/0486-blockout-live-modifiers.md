# 0486: Modifiers live on a Blockout (round 502, Pass 23b R1)

- **Status:** approved with Pass 23b (the owner, 2026-11-11, through the hub: "Yes, modifiers should work on the
  blockout."). **Done 2026-11-14** (round 502, by the Pass 23b Modifiers worker, in parallel with rounds 498-500 on
  main - its PR is on `main`, not on round 501's). Open: the stress rows' times (a timed window).
- **Requirements:** R-02, R-03 (modeling, modifiers), Q-06 (responsiveness), C-02, R-09
- **Decisions:** ADR 0129 decision 10 (Apply on a modifier is refused on a Blockout) stands; amended with "a modifier
  stack on a Blockout is evaluated over its rebuilt mesh"
- **Model:** the main session or implementer (cross-cutting), then test-engineer, then code-reviewer
- **PR:** its own, base `main`, on top of round 501's

## What is there today
A Blockout's mesh is rebuilt from its pieces by a background job and lands on its MeshFilter. Whether a modifier
stack added to the Blockout's object is evaluated over that mesh is not specified and not tested (the proposal:
"treat it as not covered"). Apply is refused on a Blockout; Bake to Mesh (round 482) makes it an ordinary mesh.

## Scope
- In:
  1. **A modifier stack on a Blockout object works live:** the rebuilt mesh is the stack's input; the stack is
     evaluated again after every landing (on the worker that already evaluates slow stacks - ADR 0105 - never on the
     main thread beyond today's budget rule); the views, the lightmapper, colliders and the exporters use the
     evaluated mesh as they do for any object with modifiers. Bevel, Mirror, Array, Solidify, Subdivision, Displace,
     Decimate, Triangulate, Weld at least - every modifier that takes a mesh.
  2. **Apply stays refused** on a Blockout (Bake to Mesh first; the refusal offers it). **Bake to Mesh keeps the
     stack** on the baked object (it is an ordinary mesh then, the stack still live over the baked base); Unbake keeps
     it too.
  3. **Picking with a stack:** the Blockout's handles, outlines and the Draw Piece tool work on the PIECES (the base),
     as before. A click on a face of the evaluated mesh selects the piece that made it only while the stack keeps the
     topology (no modifier, or modifiers that do not change faces); with a topology-changing modifier a click selects
     the Blockout, and the status says "modifiers change the faces: click selects the Blockout".
  4. **Material slots and UVs** go through the stack as any mesh's do (round 482's slots are the base's).
  5. The Inspector's modifier section on a Blockout says the stack is live over the Blockout's result; the Apply
     button's refusal text; nothing else in that section changes for other objects.
  6. A Learn section "Modifiers on a Blockout" (Bevel the whole building; Mirror half a plan; Array a bay) and
     stress rows: a door dragged in `interior 100` with Bevel, with Mirror, with Subdivision 1 on the Blockout - the
     frame, the move-to-landing and the move-to-evaluated-mesh times, with a verdict.
- Out: modifiers on a single piece (a piece is numbers); a modifier that changes the pieces; new modifiers.

## Acceptance criteria
1. For each modifier listed: added to a Blockout by the Inspector's Add Modifier with real events and by the console;
   the drawn / exported mesh is the evaluated one (measured: Mirror doubles the bounds along its axis, Array's count,
   Solidify's thickness, Bevel's new faces, Subdivision's face count); after a piece moves and `blockout wait` the
   evaluated mesh follows (it equals the stack run by hand on the new base).
2. No frame of the drag is longer because of the stack than the budget rule allows: the stack runs on a worker
   (a counter says no main-thread evaluation during the drag); the rows report what the user waits for.
3. Apply on a Blockout's modifier is refused with the offer; Bake to Mesh then Apply works; Bake / Unbake keep the
   stack; one undo step each.
4. Picking: with no modifier and with a non-topology modifier a click on a face selects its piece; with Mirror or
   Subdivision it selects the Blockout and the status says why; handles and the Draw Piece tool still work on the
   pieces with any stack.
5. Generate Lighting, colliders, OBJ / FBX / GLB use the evaluated mesh (tests); Save / open keeps the stack.
6. Objects that are not Blockouts evaluate their stacks exactly as before (the `modifiers` tests and
   `blendity_stress --only modifiers_ngon` unchanged).
7. The round's groups, `modifiers` and `editor` on the three builds; the coverage map; stress `--only blockout` and
   `modifiers_ngon`.

## Likely files (cleared by the hub for this round)
`src/scene/modifiers.cpp`, `src/scene/scene.cpp` (the stack's evaluation), `src/editor/panels.cpp` (the modifier
section), `src/editor/editor.cpp` (`eval_jobs_poll` with `blockout_jobs_poll`), `src/editor/blockout_editor.cpp`,
`blockout_handles.cpp` / `blockout_pick.h` (picking), `lessons.cpp`, `tests/test_blockout.cpp`, a `tests_round502` in
the modifiers' test file if its tests belong there, `stress/stress_blockout.inc`. Anything else: ask the hub.

## Documentation to update
`changelog.d/502-blockout-live-modifiers.md`; ADR 0129 amended; this spec marked done.
