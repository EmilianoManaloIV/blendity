# 0484: Architectural pieces for a Blockout (round 500, Pass 23b R4)

- **Status:** approved with Pass 23b (the owner, 2026-11-11, through the hub: "Yes, more architectural pieces is
  good."; the hub's reading: the wider set of the proposal). **Done 2026-11-14 (round 500), with deviations:**
  - A new kernel shape was needed: `Shape::Convex` (corners, the convex hull's faces) - ADR 0128 amended.
  - Criterion 1 / 7 (no faces on top of each other): not 0 on every kind; the counts and their causes are in
    `changelog.d/500-blockout-architectural-pieces.md` (a railing's posts on a turned room's floor, flights on their
    landing, spiral steps on a shared plane, outlined roofs' overlapping rectangles, arcades through walls; the grid
    path's features finer than its cell).
  - Over an outline a roof's handles offer only the top one; Post Spacing scales; a roof's Pitch follows the slope under
    an uneven scale (the spec said only "scales its numbers").
  - The bench interior's timed verdict waits for round 503's window (measured under load here).
- **Requirements:** R-02 (modeling), U-01, C-02, R-09
- **Decisions:** ADR 0128 (the kernel's shapes) - amended only if the roof needs a new kernel shape; ADR 0129 (a preset
  is one piece that generates its solids, as rounds 498-499's styles)
- **Model:** implementer (the main session for a new kernel shape), then test-engineer, then code-reviewer
- **PR:** its own, base `main`, on top of round 499's

## Scope
- In - each is **one piece with numbers** (a preset of parameters that generates kernel solids, blockout-level):
  1. **Column / pillar:** round or square shaft; Base Height, Capital Height, Base / Capital Width, Taper (top radius
     as a share of the bottom), Segments.
  2. **Pilaster:** a flat column against a wall (the square column cut by half, its back on the wall under the mouse).
  3. **Beam / lintel:** a box between two points or along a wall's top: Width, Depth, Length.
  4. **Railing / balustrade:** along a line or an outline's sides: posts every N metres, Post Size, Rail Height, Rail
     Size, a bottom rail on / off.
  5. **Roof** over a rectangle or an outline: Gable, Hip, Shed; Pitch (degrees) or Ridge Height, Overhang, Thickness
     (a solid roof when 0). From wedges where they suffice; a new kernel shape (a ridge prism / a convex solid from
     planes) only if they do not - said in ADR 0128.
  6. **Stairs with a landing:** L and U (two flights of round 480's stairs and a landing box), and **Spiral stairs**
     (steps as wedged boxes round a post: Radius, Inner Radius, Total Height, Steps, Turn in degrees).
  7. **Arcade:** N arches on N + 1 columns along a line: Span, Column Width, Arch Rise, Depth, Count.
  8. Each from the Modeling Tools window's Blockout section (one "Architecture" dropdown + Add, not eight more
     buttons - round 496's lesson), from GameObject > Blockout, and from the console: `blockout add column|pilaster|
     beam|railing|roof|stairs l|stairs u|stairs spiral|arcade [numbers]`; the Inspector's rows; F9 after it is made;
     handles where a piece has an obvious one (height, radius, length) - at least the bounding box's face handles of
     round 479.
  9. Learn sections (one for columns / beams / railings, one for roofs and stairs, one for the arcade - or one
     "Architectural pieces" with a Try-it each) and a stress sweep row a kind.
- Out: fluting, mouldings, profiled capitals; roofs with dormers or valleys between two roofs; curved railings;
  a staircase that fits itself between two floors by itself (the numbers are the user's).

## Acceptance criteria
1. Each kind, by console and by the panel with real events: one piece in the Hierarchy; its result closed, valid, no
   faces on top of each other, on each path the build has, alone and standing in / on a room.
2. The numbers do what they say (measured on the mesh): a column's taper, base and capital heights; a railing's post
   count along a 5 m line at 1 m spacing (6 posts); a gable roof's ridge height from its pitch; a hip roof over an
   L-shaped outline closes; L / U stairs reach Total Height at the last step; spiral stairs turn by Turn degrees; an
   arcade of 4 has 5 columns.
3. Scaling a piece (round 496) scales its numbers; Turn 90 where it makes sense (stairs); one undo step each; F9
   re-runs from `before`.
4. Material classes: every generated face is Solid (or the piece's override), a roof's top faces **Roof** (a new
   class) so a roof material can be given later; UVs follow round 482's rule.
5. Hostile numbers (0 or 1,000 posts, a pitch of 0 or 90 degrees, a taper of 0, 500 steps, an arcade of 0) give a
   valid mesh or a clean refusal; counts are clamped.
6. Save / open, Bake / Unbake, export keep each piece.
7. Each kind's stress sweep (sizes, places, turns; both paths): problems 0, or the count said with its cause; the
   existing sweeps unchanged row for row. The 100-piece bench interior with one of each added keeps its verdicts.
8. The round's groups and `editor` on the three builds; the coverage map.

## Likely files
`src/scene/blockout_object.h/.cpp` (the presets and their solids), `src/scene/blockout.h/.cpp` and friends only for a
new kernel shape, `src/editor/blockout_editor.cpp`, `blockout_tool.cpp`, `blockout_handles.cpp`, `adjust_last.cpp`,
`lessons.cpp`, a few lines in `panels.cpp` (the menu), `tests/test_blockout.cpp`, `stress/stress_blockout.inc`.

## Documentation to update
`changelog.d/500-blockout-architectural-pieces.md`; ADR 0129 (and 0128 if a shape is added); this spec marked done.
