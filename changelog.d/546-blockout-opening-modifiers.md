## 2026-11-17 (round 546): modifiers on a door or window (task 0526, ADR 0150)

**Asked** (the owner, 2026-11-15, through the hub): "Why can't I add modifiers to windows and doors?", then "Lets do both" - the
opening itself (this round) and its parts (round 547). Approved: "Array and mirror repeat the whole opening".

**Added**
- **A door or window takes a modifier stack.** Its Array and Mirror repeat the whole opening: each copy is the opening as its
  numbers make it - its hole and (with round 547) its frame, glass, sill board and a door's leaf - placed in the opening's own
  axes (an Array steps in multiples of its size, a Mirror mirrors across its middle; a mirrored copy is made as a turn, never a
  negative scale). The copies are exact, from the numbers - a row of ten windows from one costs what ten placed by hand cost.
  The other modifiers a piece takes (Bevel, Subdivision ...) reshape the hole, as round 514 runs a piece's stack.
- **Width, Head and Sill stay one opening's size,** before its modifiers: the handles, F9, the console and the leaf read them
  unchanged and the copies follow. Repeats run after the reshaping modifiers whatever their order in the stack.
- **The Inspector** says what an opening's modifiers do, warns when a repeat sits above a reshaping one, and says why the
  copies were refused; **the status** (and `blockout info`) says how many copies an opening makes, or why it is built once
  (more than 256 copies, a number that is not finite).
- A copy's faces belong to the opening: a click on one selects it. Copies share one evaluation of the hole's stack.
- A Learn section "Modifiers on a door or window".

**Changed:** round 514's "no stacks on doors and windows" (ADR 0138) - a modifier already saved on an opening (inert until now)
works when the scene opens. Rooms, presets and Intersect pieces still take none.

**Limits:** before round 547 lands, a styled window's reshaping modifiers wait for its one-piece hole (the copies are made);
a copy that falls outside every wall cuts nothing, and the status does not name it yet.

**Checked:** CHECKED_546

<!-- docs/ARCHITECTURE.md -->
after: | `Editor::blockout_hit`, `blockout_wall_under_mouse`
| `blockout_opening_placements()`, `blockout_opening_copies()` | `src/scene/blockout_piece_stack.cpp` | a door's or window's copies from its Array and Mirror, in its own space; the snapshot's copies of its kernel pieces (round 546, ADR 0150) |

<!-- docs/USABILITY.md -->
after: | Adding a door or window meant the Create section's buttons
| A door or window took no modifiers: a row of windows meant placing each by hand. (round 546) | An Array or Mirror on a window repeats the whole window; a Bevel rounds its hole. |
