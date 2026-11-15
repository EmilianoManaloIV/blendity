## 2026-11-15 (round 503): Pass 23b's write-up - the bench interior takes the pass's pieces, the lessons, the numbers in one table, what is left (task 0487)

**Asked** (the owner, 2026-11-11, with Pass 23b through the hub): the pass's write-up - the bench interior, the Learn
lesson, the numbers and the open decisions; on 2026-11-15, "development must not wait on PR merges": built on round
511's #416 (which carries the pass's open PRs #411, #413, #414 and #402-#412) and published as its stack's top.

**What Pass 23b built** (rounds 497-502 and 513-515; each round's fragment has its detail):
- **497** an L-shaped outline room's wall is one face again on the exact path (#399).
- **498** windows with a style - Single, Double, Arched, Fixed, Grid, Round: a frame, bars, a sill board, glass (#400).
- **499** doors with a style - Single, Double, Sliding, Arched, Revolving; doors and windows added upright; each style
  carries its size (#405).
- **500** nine architectural pieces - Column, Pilaster, Beam, Railing, Roof, Stairs L / U, Spiral Stairs, Arcade - each
  one piece with numbers, and a Convex kernel shape (#410).
- **501** Blockouts snap to each other, and touching Blockouts join (#406).
- **502** modifiers live on a Blockout (#403).
- **513** Draw Piece draws a door or a window onto a wall (#414).
- **514** modifiers on a Blockout piece (#413).
- **515** a door's leaf is its own object that swings about its hinge without a rebuild (#411).

**Changed in this round:**
- **The bench interior** (`benchscene blockout [400]`) now holds the pass's kinds - six styled windows (every window
  style), six styled doors (Single, Double, Sliding: their leaves are objects, not pieces), the nine architectural
  pieces and a piece with a Bevel stack - in place of padding columns: still exactly 100 / 400 pieces, the same mesh on
  two runs, sound on both paths. The 400-piece interior is 9 x 6 rooms (it was 10 x 6) so the ten new pieces keep it
  at 400. Only the first six doors and windows have a style: every styled opening leaves unresolved places on the grid
  path, which stop faces merging, and styling all 51 made the 100-piece tests 2.4 s -> 10.7 s. The Railing stands in
  the row of new pieces beside the two stairs, not on the building's stair well (the spec asked for "a railing on the
  stairs"): on the well it took the 400-piece mesh from 21,195 faces to 109,475. Round 497's notch wall is the bench's
  L-shaped room. The 400-piece scene changed, so its bench rows are not comparable with earlier rounds' rows.
- **`meshops::overlapping_faces` sweeps** over the triangles' boxes sorted by x instead of testing every pair: the
  400-piece interior's soundness check took 432-780 s with the new pieces (1.96 s before them) - 1.2 s now (the
  100-piece test 1.6 s, 2.4 s before; untimed, below-normal priority). The same
  pairs as before, pinned against a copy of the old loop on box soups, near-tolerance faces, degenerate and NaN faces,
  many triangles sharing one x and a Blockout interior; the stress counts that use it are unchanged (below).
- **Adjust Last (F9) follows a console or Inspector edit:** after `blockout set style arched` on a window, `blockout set
  open 0.5` on a door or a typed Open Amount in the Inspector, F9 showed the old number - and its next edit wrote that
  old number back. It now shows the piece's numbers and keeps them (found by the HCI worker; three real-event tests).
- **Learn:** "Building rooms with Blockout" gains a styled door (and its leaf swung), a styled window and a Column, and
  points to "Doors with a style", "Windows with a style", "Architectural pieces" and "Modifiers on a piece". "Doors with
  a style" is settled: round 515's leaf steps, then round 513's two Draw Piece steps, the text saying which door is
  selected and where the drawn door lands; its Try-it runs twice from a new scene.
- Every spec of the pass (0481-0487, 0495-0497) says done with its PR.

**Numbers** (the timed window's reports, named in the PR): `blendity_stress --only blockout`, timed in the hub's window on 2026-11-15
(14:24-14:49, nothing else running; reports `report_20261115_142320` without the libraries, `report_20261115_143426` with
them). Frame median / 1 % slowest (or worst) / move-to-landing median, in ms; every row 0 main-thread builds:

| Row | Without the libraries | With them |
|---|---|---|
| bench 100 pieces, a door slid along its wall (600 frames) | 5.96 / 55.30 / 93.35 | 6.25 / 19.62 / 54.05 |
| bench 100 pieces, a room's face handle dragged | 1.59 / 26.90 / 64.80 | 1.77 / 18.63 / 49.29 |
| bench 400 pieces, a door slid along its wall | 7.43 / 37.22 / 275.32 | 8.27 / 23.31 / 109.87 |
| bench 400 pieces, a room's face handle dragged | 2.23 / 43.62 / 250.34 | 2.21 / 24.19 / 96.90 |
| interior 100, a Grid window moved along its wall (round 498; worst) | 1.19 / 10.15 / 48.31 | 4.06 / 6.33 / 16.24 |
| interior 100, a Single door moved along its wall (round 499; worst) | 3.16 / 12.19 / 50.10 | 4.16 / 6.26 / 4.27 |
| the bench with one of each architectural piece, 109 pieces (round 500; worst) | 6.21 / 55.37 / 93.71 | 6.59 / 21.41 / 56.48 |
| the snap call alone among 900 pieces (round 501): first / warm median | 0.50 / 0.025 | 0.50 / 0.024 |
| a column with a Bevel stack dragged (round 514; worst) | 1.47 / 13.65 / 48.64 | 5.44 / 7.97 / 5.53 |

- The bench rows still say OVER: a landing's frame (the 1 % slowest) is over 16.7 ms - round 483's open "landing frame"
  (item 1 below). The bench scene changed in this round (the pass's pieces, 9 x 6 rooms for the 400), so these rows
  are not comparable with round 483's.
- The untimed counts (problems, faces on top of each other, unresolved places) of every sweep are the same as round
  515's row for row on both builds (95 / 144 rows): the `overlapping_faces` sweep changed no count. The rows that
  differ are the live rows' landing counts (timing) and the bench scene's own faces (17,409 -> 19,868 without the
  libraries, 0 problems).

**What is left, for the owner's decision** (carried from round 483's list, with what each would cost):
1. **The landing frame:** build the new mesh's render data on the worker too - a round.
2. **A partial rebuild inside a cluster** (ADR 0129) - one to two rounds; what the 400-piece numbers ask for.
3. **The grid path's two outline-room problems** of round 497 (seed 534: turned thin rooms) - a grid-path round.
4. **Styled openings leave unresolved places on the grid path** (804 in the 100-piece interior with its twelve styled openings; about 2,950 with all 51 styled), so their faces
   do not merge: a round on the grid path's openings; it would let the bench style all its openings again.
5. **The exact path's one styled-door problem** (round 515: seed 1927, a Sliding door, a coplanar contact) - small.
6. **The Wireframe modifier's own overlapping struts** (round 514) and **an Array with a non-finite offset** (round
   502) - a modifiers round.
7. Round 483's others stand: a turned box beyond about 100 km stalls the kernel; Realtime GI on Automatic with a sun
   redoes the realtime lightmap's layout on each landing; an exact boolean for the dependency-free build; per-face
   materials of mesh pieces; round 496's leftovers.

**Checked:** the groups blockout, editor, lighting, physics, hci, blockout_modifiers and blockout_join, 0 failed
on each, on #416 (round 511, 99bd4751) merged in: Windows without the libraries 22,845 / 3,204 / 9,825 / 190 / 1,849 /
1,023 / 505 checks, with them 30,095 / 3,227 / 9,881 / 208 / 1,849 / 1,029 / 506, Linux 22,848 / 3,205 / 9,827 / 190 /
1,847 / 1,023 / 505. Coverage map clean. Two code reviews (the first found an F9 refresh before a multi-object edit
reached its piece, F9 kept for another piece's edit, and a dropped exact-path check - fixed). A picture of the bench
with the pass's pieces: `docs/images/blockout_23b_pieces.png`.
