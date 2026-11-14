# 0483: Door styles for a Blockout (round 499, Pass 23b R3)

- **Status:** approved with Pass 23b (the owner, 2026-11-11, through the hub - `pass23b-plan-approved-2026-11-11.md`;
  their words: "No, just the blockout of space for a rotating door. I want a preset of parameters. No, keep things like
  a blockout so I can texture and add materials later."). **Done (round 499)**; what differs from the text below:
  - An open hinged leaf (Open Amount > 0) cuts whatever stands in its swing in front of the wall; the Blockout's
    status names what is cut (ADR 0129's round 499 amendment says why and what was measured). Put to the owner.
  - Arched: the leaf is closed only (Open Amount is not read). Revolving: the ring has its two entries cut so the
    rooms are joined; Width is the entries' width.
  - Criterion 4's "no unresolved places" holds for the Open Amount sweep on the exact path; the 2,000-door sweep has
    0 problems on both paths with the counts in the fragment.
  - The Door Style and Window Style dropdowns stand under the Blockout buttons in the Create section
    (`tools_window.cpp`); the buttons there are three to a row with short labels (the HCI survey of round 505 found
    them clipped).
  - A selected Arched or Revolving door is outlined as its box.
- **Requirements:** R-02 (modeling), U-01, C-02, R-09
- **Decisions:** ADR 0129 as amended in round 498 (a styled opening is one piece that generates its solids)
- **Model:** implementer, then test-engineer, then code-reviewer
- **PR:** its own, base `main`, on top of round 498's

## Scope
- In:
  1. **A Style on a door piece** (round 498's mechanism): Opening Only (today's hole; the default, older scenes
     unchanged), Single (a frame and one leaf), Double (two leaves), Sliding (a leaf beside the opening, on the wall's
     face), Arched (a round head), Revolving - **only the blocked-out space of a revolving door** (the owner's answer):
     a round drum cut through the wall, a ring of wall thickness round it standing proud of the wall on both sides, no
     wings and no mechanism.
  2. **The numbers:** Width, Head, Depth (as today), Frame Width, Frame Depth, Leaf Thickness, Open Amount (0 to 1: the
     leaf drawn swung about its hinge, or slid; for a blockout's view), Hinge Side (left / right), Slide Direction,
     Arch Rise, Drum Radius (Revolving), Leaf (on / off).
  3. A leaf is blockout-level: one flat box. It takes a new class **Leaf** (so a door material can be given later);
     the frame takes Solid, as a window's. A swung leaf (Open Amount > 0) is a turned box: it must build clean on both
     paths at any amount.
  4. The panel's Style dropdown beside Add Door; `blockout add door [style] [width head]`; `blockout set style`,
     `set leaf on|off [thickness]`, `set open <0..1>`, `set hinge left|right`, `set drum <radius>`; F9 rows; the
     Inspector's rows for the style in use.
  5. A Learn section "Doors with a style"; a stress sweep of styled doors in a wall (sizes, places, turns, Open
     Amounts; both paths) and a live row.
- Out: wings, a turning drum, hinges and handles, an animated door (Open Amount is a number, not a clip), door
  hardware, a physics door.

## Acceptance criteria
1. Each of the six styles by console and by the panel with real events, on a wall between two rooms: closed, valid, no
   faces on top of each other on each path; the two rooms are joined through the door (a point in the doorway is air)
   unless a closed leaf (Open Amount 0, Leaf on) fills it - then the leaf separates them and is its own class.
2. Opening Only gives the same mesh bytes as a door made before this round.
3. Revolving: a round passage of Drum Radius through the wall, the drum's ring proud of both wall faces, nothing
   inside the drum.
4. Open Amount swept 0 to 1 in twenty steps for Single, Double and Sliding at two hinge sides: every result sound; the
   leaf never leaves a sliver against the frame (no unresolved places in the test's cases).
5. Style changes keep the shared numbers; one undo step; F9 re-runs from `before`; handles change Width / Head with
   real events and the frame and leaf follow.
6. Leaf faces are in the Leaf class's slot; frame in Solid's; a Blockout's Leaf Material and a piece's override work;
   UVs follow round 482's rule.
7. Hostile numbers (a leaf thicker than the wall, a drum wider than the wall is long, Open Amount -1 / 2 / NaN) give a
   valid mesh or a clean refusal.
8. Save / open, Bake / Unbake, OBJ / FBX / GLB keep a styled door.
9. The round's groups and `editor` on the three builds; the coverage map; the existing sweeps unchanged row for row.

## Likely files
As round 498: `src/scene/blockout_object.h/.cpp`, `src/editor/blockout_editor.cpp`, `blockout_tool.cpp`,
`blockout_handles.cpp`, `adjust_last.cpp`, `lessons.cpp`, a few lines in `panels.cpp`, `tests/test_blockout.cpp`,
`stress/stress_blockout.inc`.

## Documentation to update
`changelog.d/499-blockout-door-styles.md`; ADR 0129's amendment extended; this spec marked done.
