## 2026-11-14 (round 499): doors with a style - a frame, a leaf, sliding, arched, and the space of a revolving door (task 0483, ADR 0129 amended)

**Asked** (the owner, 2026-11-11, Pass 23b through the hub): door styles as presets of parameters, blockout-level -
"No, just the blockout of space for a rotating door. I want a preset of parameters. No, keep things like a blockout
so I can texture and add materials later." And, from the HCI survey of round 505 (through the hub): the Blockout
buttons of the Create section were cut at the default window size, and three texts sent the artist to the Console.

**Added:** a door piece of a Blockout has a **Style**: Opening Only (the hole as before, the default), **Single**
(a frame and one leaf), **Double** (two leaves), **Sliding** (a leaf on the wall's face), **Arched** (a round head)
and **Revolving** - the space of a revolving door only: a round drum through the wall with a ring round it standing
proud of both faces and an entry each side, nothing inside. Its numbers - Frame Width and Depth, Leaf (on / off) and
Leaf Thickness, **Open Amount** (0 to 1: the leaf swung about its hinge, or slid), Hinge Side, Slide Direction, Arch
Rise, Drum Radius - are in the Inspector, in F9 after the door is made, and in the console
(`blockout add door [style] [width head]`, `blockout set style | leaf | open | hinge | slide | drum`). A leaf is one
flat slab in a new material class, **Leaf** (a Blockout material, a piece override, `blockout set material leaf`); the
frame is Solid. A closed leaf separates the two rooms; with Leaf off or the door open they are joined. A styled door
is still one piece, made as round 498's windows are. A Learn section, "Doors with a style".

**Changed:**
- **Door Style and Window Style** dropdowns now stand under the Blockout buttons of the Create section, and the Door
  and Window buttons (and GameObject > Blockout) make the style they show. The console's
  `create Blockout Piece: Door | Window` makes Opening Only whatever the dropdowns show (round 498 had it follow the
  dropdown).
- **The Blockout buttons are no longer cut** (the HCI survey): short labels - Blockout, Door, Window, Add Box / Cyl /
  Sph, Cut Box / Cyl / Sph, Room Box, Room Cyl - with the whole name in each tooltip, and as many to a row as the
  widest label allows (three at the default size; fewer with a wider font or a narrow pane at UI scale 1.5).
- Turn 90's help also says where the turn is in the interface (the Turn row of F9 after a piece is drawn); the Room
  tool's fixed-operation line says "(fixed)" and its tooltip says there is no button for it.
- `blockout set style` and `set frame` change every selected opening that knows the word (a door's word the doors, a
  window's word the windows); the Inspector's Style on a mixed selection changes only the pieces of the edited kind.
- Hinge Left / Right: "Left: the hinge is on your left when you stand in the room the door opens into, facing the
  door".

**Limits, said plainly:**
- **An open hinged leaf cuts what stands in its swing.** With Open Amount above 0 the doorway's width is cleared in
  front of the wall as far as the leaf reaches (a 0.9 m door fully open: about 0.9 m): a column there loses a slab,
  and a wall closer than the leaf is long is cut through. The Blockout's status names each piece and room it cuts
  ("Door 'X': its open leaf cuts 'Y'"); Open Amount 0, the default, cuts nothing. Why: the kernel adds before it
  subtracts, so the leaf is what a cut leaves; a leaf added in front of the wall instead left 2-4 unresolved places a
  build on the exact path, and with a 1 mm overlap 414 problems of 2,000 (ADR 0129's round 499 amendment). The spec
  did not ask for this cut; **it is put to the owner** (keep it as a swing clearance, or wait for a kernel change
  that lets an added leaf stay).
- An Arched door's leaf is closed only (Open Amount is not read). A selected Arched or Revolving door is outlined as
  its box. A swung leaf is no thicker than the frame is wide; a styled door over 100 m is left out and named.
- On the grid path at the default Field Cell a 4 cm leaf is left out and the frame held at a cell; the status names
  the Field Cell that shows the door whole (as round 498's windows).

**Numbers** (`blendity_stress --only blockout`, untimed counts at below-normal priority beside other work; the
times of this run are not a measurement):
- **The new sweep, styled doors in a wall between two rooms** (2,000 arrangements a path: styles, sizes, places,
  turns, Open Amounts, hinge sides): **exact path 0 problems** (94 results with small faces on top of others, 2
  with faces of no area, 163 unresolved places); **grid path (cell 0.08) 0 problems** (0 / 18 / 49,423). Before the
  review's fixes the exact path had 2 problems (a swung leaf thicker than its frame).
- **Open Amount 0 to 1 in twenty steps** (Single both hinges, Double, Sliding both ways; exact path): every step
  sound, 0 unresolved places.
- **Hostile numbers on styled doors** (180 cases a path): exact 125 built, 55 refused, **0 problems**; grid 123
  built, 57 refused, **0 problems**.
- **A Single door at Open 0.5 dragged along its wall in interior 100, 120 frames:** no build on the main thread on
  either build. This run's times were taken under load and say OVER; the implementer's quieter run said ok (frame
  median 1.4 ms without the libraries, 3.9 ms with). The timed row comes with round 503's window.
- **Existing sweeps and round 498's window rows: the same row for row as round 498's**, on both builds.

**Checked:** the groups blockout, editor, lighting, physics and hci, 0 failed on each: Windows without the
libraries, Windows with them and Linux (counts in the PR). The Linux run found what Windows could not - with its
wider font three labels were still cut at three to a row - hence "as many to a row as fit". The coverage map: no
command, action or window without a test. The test-engineer's 12 tests (they found the revolving door's ring was a
closed tube that joined nothing, a closed Double's 2 cm crack, and a 1e9 m arched door the exact path could not
build - all fixed) and a code review (the swing's cut, Hinge Left / Right mirrored against their help, mixed
selections taking each other's styles - fixed or, for the swing, named and put to the owner). The full suite, macOS
and the sanitizers are CI's.

<!-- docs/USABILITY.md -->
after: | A Blockout knew boxes, cylinders and spheres
| A door in a Blockout was a bare hole, and the Blockout buttons of the Create section were cut ("New Block...", "Subtract Cy..."). (round 499) | A **Door Style** (Single, Double, Sliding, Arched, Revolving) with a leaf that opens by a number and a Leaf material of its own; short button labels that fit, the whole name in the tooltip. |

<!-- docs/ARCHITECTURE.md -->
append:
## Blockout: styled doors (round 499, task 0483)
A door's `style` goes through the same generator as a window's (`blockout_window_pieces`, `fit_to_grid`);
`blockout_opening_set_style` / `blockout_piece_edited(piece, style_before, opening_before)` are the one way to change
it. `kClassLeaf` is the eighth face class. `state.swing_cuts` (`note_swing_cuts`) lists what an open leaf's cut
reaches, for the status. `Editor::blockout_ui_kind(kind)` adds the panel's Door / Window Style to a kind the UI
creates; the console's `create` does not use it.
