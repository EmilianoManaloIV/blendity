## 2026-11-15 (round 503): Pass 23b's write-up - the bench interior takes the pass's pieces, the lessons, the numbers in one table, what is left (task 0487)

**Asked** (the owner, 2026-11-11, with Pass 23b through the hub): the pass's write-up - the bench interior, the Learn
lesson, the numbers and the open decisions; on 2026-11-15, "development must not wait on PR merges": built on a local
merge of the pass's open PRs (#411, #413, #414) and published as their stack's top.

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

**Numbers** (the timed window's reports, named in the PR): NUMBERS_TABLE

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

**Checked:** CHECKED_LINE
