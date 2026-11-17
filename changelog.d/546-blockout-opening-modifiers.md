## 2026-11-17 (round 546): modifiers on a door or window (task 0526, ADR 0150)

**Asked** (the owner, 2026-11-15, through the hub): "Why can't I add modifiers to windows and doors?", then "Lets do both" - the
opening itself (this round) and its parts (round 547). Approved: "Array and mirror repeat the whole opening".

**Added**
- **A door or window takes a modifier stack.** Its Array and Mirror repeat the whole opening: each copy is the opening as its
  numbers make it - its hole and round 547's frame, glass and a door's leaf, each copy's own objects - placed in the opening's own
  axes (an Array steps in multiples of its size, a Mirror mirrors across its middle; a mirrored copy is made as a turn, never a
  negative scale). The copies are exact, from the numbers - a row of ten windows from one costs what ten placed by hand cost.
  The other modifiers a piece takes (Bevel, Subdivision ...) reshape the hole, as round 514 runs a piece's stack.
- **Width, Head and Sill stay one opening's size,** before its modifiers: the handles, F9, the console and the leaf read them
  unchanged and the copies follow. Repeats run after the reshaping modifiers whatever their order in the stack.
- **The Inspector** says what an opening's modifiers do, warns when a repeat sits above a reshaping one, and says why the
  copies were refused; **the status** (and `blockout info`) says how many copies an opening makes, or why it is built once
  (more than 256 copies, a number that is not finite).
- A copy's faces belong to the opening: a click on one selects it, and the Scene view draws the cut outline of every copy.
  Copies share one evaluation of the hole's stack.
- **Each copy's parts** (round 547's objects): a turned copy's stand turned with it; a mirrored copy's leaf hinges on the other
  jamb; a copy's part takes the modifiers of the first copy's part of its kind - edit those, the copies follow (the Inspector says
  so on a copy's part). Deleting a leaf turns Leaf off for every copy, as for one.
- A Learn section "Modifiers on a door or window".

**Changed:** round 514's "no stacks on doors and windows" (ADR 0138) - a modifier already saved on an opening (inert until now)
works when the scene opens. Rooms, presets and Intersect pieces still take none.

**Limits:** a styled door's or window's copy that would not stand upright (a Mirror Y, a turn about X or Z) is left out - its
frame and leaf cannot - and the status says so; a plain opening's is made. A copy that falls outside every wall cuts nothing
(its parts still stand there) and the status does not name it yet. Join's overlap test and the size handles see the first copy.

**Checked:** the round's groups (`blockout_opening_modifiers`, `blockout_piece_modifiers`, `blockout_modifiers`, `blockout_join`,
`blockout`, `modeling`, `editor`, `hci`), not the full suite: Windows with Blender's libraries 32,900 checks, 0 failed; on round 547's
head with main merged, Windows without them (as CI) 27,975 and Linux (with `lighting`) 37,815, 0 failed. Stress: the new section
`blockout_opening_modifiers` - a 40 m wall, ten windows by hand against one with an Array of 10: 70 / 70 faces plain and Grid, a
drag of the arrayed, bevelled window 0 stack evaluations and 0 main-thread builds, the sweep 30 cases with 0 problems and 0 faces
newly on top of each other. `--only blockout`: no problem count rose against round 514's run.

<!-- README -->
after:   Draw Piece draws a door or a window onto a wall of a Blockout by dragging a rectangle
  a door or window takes modifiers: an Array or a Mirror repeats the whole opening, a Bevel rounds its hole (round 546);

<!-- docs/ARCHITECTURE.md -->
after: | `Editor::blockout_hit`, `blockout_wall_under_mouse`
| `blockout_opening_placements()`, `blockout_opening_copies()` | `src/scene/blockout_piece_stack.cpp` | a door's or window's copies from its Array and Mirror, in its own space; the snapshot's copies of its kernel pieces (round 546, ADR 0150) |

<!-- docs/USABILITY.md -->
after: | Adding a door or window meant the Create section's buttons
| A door or window took no modifiers: a row of windows meant placing each by hand. (round 546) | An Array or Mirror on a window repeats the whole window; a Bevel rounds its hole. |
