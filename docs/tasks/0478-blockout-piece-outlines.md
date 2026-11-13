# 0478: A Blockout's pieces show their outlines and can be clicked by them (round 494)

- **Status:** done (round 494, 2026-11-12). Approved by the owner 2026-11-11 ("Yes, add outlines.", through the hub) as
  the proposal in `pass23-picking-bug.md`; round 494, task 0478 from the hub.
- **Requirements:** R-03 (modeling), U-01 (selection does what the cursor says)
- **Decisions:** ADR 0129 (the Blockout object: a click selects the piece that made the face). No new ADR: no data-model change.
- **Model:** main session (small, in files this pass owns), then test-engineer, then code-reviewer
- **PR:** #390 (https://github.com/EmilianoManaloIV/blendity/pull/390)

## Goal
The owner, after round 479: "Selecting things on my cursor doesn't always select." What is left of it after the picking fix: a
door, a window or a piece that cuts is **air** where it is seen head-on - a click in it selects what is behind, and the piece
can only be clicked on its reveal (the wall's thickness) or in the Hierarchy. Its outline is drawn only while the Blockout
itself or that piece is selected. After this round, while anything of a Blockout is selected, **every piece of it shows its
outline** (faintly, the selected ones as today), and **a click on an outline selects that piece**.

## Scope
- In:
  1. **Drawing** (`Editor::draw_blockouts`, `blockout_editor.cpp`): a Blockout's pieces draw their wires when the Blockout
     **or any of its pieces** is selected (today: the Blockout itself, or the piece alone). Selected pieces as today; the others
     fainter and thinner than today's "Blockout selected" look, in the piece's colour (orange Add, yellow cut / opening, violet
     Intersect). Depth-tested as today: an outline behind matter is hidden.
  2. **Picking** (`Editor::pick` in `scene_pick.cpp`, at round 477's hook; new `Editor::blockout_pick_wire` in
     `blockout_editor.cpp`): when a Blockout's outlines are shown, a click within 6 px (the reach Wire / Bounds objects have)
     of a drawn outline segment of a piece **that has no surface of its own under the cursor** - an opening, a Subtract piece,
     an Intersect piece - selects that piece. The nearest segment in pixels wins; a segment hidden behind the picture
     (its depth behind the depth buffer's at the nearest point, with a small bias) is not hit. Add pieces and rooms keep the
     surface pick of rounds 477 / 479 (their outlines run along edges that two pieces share: a wire pick there would guess).
     **Stated plainly: outlines are drawn for every piece; only openings, Subtract and Intersect pieces are clicked by them.**
     If the owner wants Add pieces and rooms clickable by outline too, that is a follow-up.
     Shift / Ctrl toggle as any click. Nothing changes while no outline is shown, in Edit Mode, in Play, or with a tool active.
  3. **Box select** unchanged (round 479's fix: the pieces whose faces are in the box).
  4. **Learn:** left to round 483's write-up (`lessons.cpp` is open in another pass's round; the hub, 2026-11-11).
  5. **Tests** (`tests/test_blockout.cpp`, real mouse events): with a room selected, a click on a door's outline seen head-on
     selects the door, a click in the door's middle selects what is behind (as before), a click on the wall away from the
     outline selects the room; a window; a Subtract box in a slab seen from above; an outline hidden behind a wall is not
     clickable; with nothing of the Blockout selected no outline is drawn and the click is the surface's; Shift adds; the
     outlines are drawn (overlay line count) when a piece is selected and not when another object is.
- Out: outlines for unselected Blockouts (hover); filling an opening with a clickable pane; changing the 6 px reach.

## Likely files
`src/editor/blockout_editor.cpp`, a new `src/editor/blockout_pick.h` (free functions: `editor.h` is open in another pass's
round, so nothing is declared there), `src/editor/scene_pick.cpp` (the hook in `pick`, granted by the hub for the picking work),
`tests/test_blockout.cpp`, a fragment `changelog.d/494-blockout-piece-outlines.md`.

## Acceptance criteria
1. A door, a window and a Subtract piece can each be selected by one click in the Scene view while seen head-on, once anything
   of their Blockout is selected - on their outline.
2. Clicks away from outlines select exactly what they selected before (rounds 477 / 479's tests pass unchanged).
3. An outline that is not visible is not clickable.
4. No per-frame cost when nothing of a Blockout is selected; with one selected, the wires of its pieces only (a Blockout of 100
   rooms: measured, printed, not asserted).
5. The full suite on Windows (both builds) and Linux, the sanitizer group, the audits; CI.

## Done: deviations
- The pick function is `blockout_pick_outline` in `src/editor/blockout_pick.h` (a free function), not `Editor::blockout_pick_wire`.
- **Hidden outlines:** the depth buffer's numbers are compared in metres along the pixel's ray, not with a small bias in the
  buffer's own numbers (no one bias fits near and far); in an orthographic view the allowance is a share of the view's height
  (the code review: a share of the ray's length was two metres there).
- **Drawn-outline test:** no hook counts overlay lines, so the test compares the picture's pixels around an outline in the air.
- **Not done, known:** a line of an outline that runs past the eye is drawn (clipped) but not clicked, as with Wire objects
  (the review's finding 5, low priority); no test with a moved, turned and unevenly scaled Blockout clicked by outline (the
  outline is drawn and picked through the same `world_matrix()`).
- Criterion 4, measured (printed, not asserted): 100 rooms and 20 doors, a frame with one piece selected costs about 1 ms more
  than with nothing selected.
- Criterion 5: the round's groups and `editor` locally (ADR 0134), the full suite in CI.

## Risks
- **A click near an outline that meant the surface behind it** now selects the cutter (6 px): the same trade Wire / Bounds
  objects make; the outline is visible, so the click is on something drawn.
- **Many pieces:** 100 rooms with doors draw some thousands of lines while one piece is selected - measured in criterion 4.
