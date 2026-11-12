## 2026-11-12 (round 494): a Blockout's pieces show their outlines and are clicked by them (task 0478)

**Asked** (the user, after round 479's picking fix: "Yes, add outlines."): a door, a window or a piece that cuts is air
where it is seen head-on - a click in it selects what is behind, and the piece could only be clicked on its reveal or
in the Hierarchy; its outline was drawn only while the Blockout itself or that piece was selected.

**Added:** while the Blockout or any of its pieces is selected, **every piece shows its outline** - the selected ones
as before, the others fainter and thinner, in the piece's colour (orange Add, yellow cut / opening, violet Intersect),
hidden behind matter as before. **A click within 6 px of the outline of a door, a window, a Subtract or an Intersect
piece selects that piece** (the nearest outline wins; an outline behind the picture is not clicked; Shift / Ctrl as
any click). Add pieces and rooms are clicked on their surface as before: their outlines run along edges two pieces
share, where an outline click would guess.

**Changed:** nothing while no outline is shown, in Edit Mode, in Play or with a tool active; box select is round
479's. New `src/editor/blockout_pick.h` (`blockout_outlines_shown`, `blockout_pick_outline`), called from
`Editor::pick` before the wire pick.

**Known limits:** a click within 6 px of a cutter's visible outline that meant the surface behind it selects the
cutter (the trade Wire and Bounds objects make); **only doors, windows, Subtract and Intersect pieces are clicked by
their outline** - every piece's outline is drawn, Add pieces and rooms are clicked on their surface; an outline's line
that runs past the eye (the camera inside a room, a door's edge on the wall beside it) is drawn but not clicked, as
with Wire objects; the Learn lesson's lines come with round 483's write-up (another
pass has the lessons file open).

**Fixed on the way** (found by the tests and the code review, before any of it was published): an outline behind a
surface was clickable - the depth buffer's numbers were compared with a fixed allowance; they are compared in metres
now, and in an orthographic view (whose rays start a kilometre behind the eye) the allowance is a share of the view's
height, not of the ray; a click with several Blockouts shown asked each about the ones before it again.

**Checked:** on main `c1c634cc` (the round's groups and `editor`, ADR 0134; the full suite is CI's). Windows with Blender's libraries: `blockout` 5,215 checks, `editor` 2,349, 0 failed; Windows dependency-free: 3,714 and 2,311, 0 failed; Linux (GCC, dependency-free): 3,716 and 2,330, 0 failed. The nine outline tests 30 runs in a row on Windows, 0 failed (they failed 8 runs of 25 before the click helper waited a double click out). Printed, not asserted: 100 rooms and 20 doors, a frame with one piece selected about 1 ms more than with nothing selected. Not run here: the ASan + UBSan group (stopped for another pass's timed window; CI's `linux-sanitizers` runs it). Audits: the coverage map (378 entries, 0 empty), `changelog.py check`, `docs_check.sh`. Test-engineer: six tests added; code review: changes needed, all made (above).

<!-- docs/USABILITY.md -->
after: | A click on a Blockout piece sometimes selected another piece
| A door, a window or a piece that cuts could not be clicked where it is seen head-on (it is air there), and its outline showed only while it was itself selected. (round 494, asked by the user) | While anything of a Blockout is selected every piece shows its outline, and a click on the outline of a door, a window or a cutting piece selects it. |

<!-- docs/ARCHITECTURE.md -->
append:
## Blockout: pieces picked by their outline (round 494, task 0478)
`src/editor/blockout_pick.h`: `blockout_outlines_shown(blockout, selected)` - the Blockout or any piece of it is
selected; `blockout_pick_outline(scene, r3d, rt, selected, x, y, reach)` - the piece without a surface of its own
(an opening, Subtract, Intersect) whose drawn outline is nearest the pixel within `reach`, where the outline is not
behind the picture's depth (compared in metres along the pixel's ray; the allowance is a share of the distance in a
perspective view and of the view's height in an orthographic one). `Editor::pick` asks it after the icons and before
`pick_wire_object` and the id buffer;
`Editor::draw_blockouts` draws the same outlines (`piece_outline`), so what can be clicked is what is drawn.
