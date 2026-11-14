## 2026-11-12 (round 481): the Blend radius - a rounded join in a box of its own, stitched to the crisp rest (task 0457)

**Asked** (the user's first request for the Blockout: "meta-ball like modeling but with primitives"; the pass's plan,
approved 2026-11-10): joins crisp by default, a **Blend radius** per piece that rounds the join like a metaball - and
one rounded column must not cost or change a whole building.

**What was there:** the kernel's grid path could melt a blended piece into what it touches, but a cluster with one
blended piece went to the grid **whole**, at the Blockout's cell: a connected interior is one cluster, so a rounded
column rebuilt the building on a 0.1 m grid, several times too coarse for a blend of a few centimetres.

**Added:**
- **A blend is meshed in a box of its own** (ADR 0130): round each piece with a Blend radius, its bounds grown by the
  radius and two cells; the box's inside on a finer grid, the rest of the cluster built as if nothing were blended -
  on the exact path where the build has it - and the two **stitched along the box's faces**, by which corner is which.
  Outside the box the building is the same mesh, to the bit.
- **Blend Detail** on the Blockout (Low, Medium, High): the blend's grid is a third, a sixth or a twelfth of the radius.
- An Add piece with a Blend radius gets a rounded fillet all along its join; a Subtract piece a rounded rim round its
  cut. Two blended pieces that meet blend with the larger of their radii, whatever order they are in. A radius
  below a thirty-second of Field Cell (the finest the blend's grid goes) is built unblended and the status says so.
- `blockout set blend <metres>`, `blockout set detail low|medium|high`; a handle for the radius on the active piece;
  the Learn section "Rounding joins: the Blend radius".
- When a seam does not close the boxes are grown and built again (four times at most), then the whole cluster is built
  on the blend's grid; if that grid will not close, at the Blockout's own cell; if that will not either, **without its
  blends** - and the status says which: never an open mesh, and a blend never costs the result.

**Fixed, older than this round:** a blended piece's fillet ended in a crease - 96 degrees between a ball sunk into
a box and the box, at any detail - wherever it ran past the bounds of the unblended piece it melts into: that piece was
left out of the blend there (`Grid::candidates` grew a solid's bounds by its own radius, not by the radius that
reaches it). Found by this round's roundness test. **Every blended join of that kind is wider and rounder than it
was.**

**Changed:** a cluster with a Blend radius is no longer the grid path's whole, unless the boxes take more than half
of it (the metaball case) - and then, as when a seam will not close, it is built on the **blend's** grid, finer than
the Blockout's cell it was built at before (a cost: the blobs row). With the method set to Field the rest is the
grid's and the blend still has its box; with Exact the radii are ignored, as before. Blended pieces that meet used to
blend with the later one's radius. The Inspector shows Blend Radius for boxes, cylinders and spheres that add or
subtract (round 477 hid it). A cluster's cache key holds the Blend Detail where a piece is blended.

**Timed** (2026-11-12 17:08 to 17:29, the machine reserved by the hub; `blendity_stress --only blockout`, median of
five builds on a worker thread; `interior 100` as 36 rooms with 28 doors, a column 0.25 m thick from floor to ceiling
in the first room):

| a blended column in `interior 100`, radius 0.15 m | with Blender's libraries (the rest on the exact path) | without (the rest on the grid, cell 0.1 m) | boxes | tries again | blend cell | faces (of them blended) | verdict (100 ms) |
|---|---|---|---|---|---|---|---|
| the column unblended | 12.7 ms | 49.5 ms | 0 | 0 | - | 460 / 479 (0) | - |
| Low | **22.6 ms** | **61.1 ms** | 1 | 0 | 0.050 m | 3,158 (2,690) | ok |
| Medium (the default) | **42.7 ms** | **83.2 ms** | 1 | 0 | 0.025 m | 10,686 (10,202) | ok |
| High | 128.2 ms | 174.4 ms | 1 | 0 | 0.013 m | 39,059 (38,508) / 36,980 (36,428) | **over** |

So a rounded column adds 10 to 34 ms to a building at Low and Medium, in both builds, and the walls stay the exact
path's where the build has it; before this round the building went to the 0.1 m grid whole (42 ms with the libraries,
49 without: the row `interior 100, field cell 0.1`), with a cell and a half across a 0.15 m blend. High misses
the 100 ms: the editor does not yet build Medium while a piece is dragged and High when it is let go (a follow-up).

| blobs: 40 spheres and boxes, every one with a Blend radius of 0.2 m (7 clusters, each built whole) | with the libraries | without | blend cell | faces |
|---|---|---|---|---|
| Low | 24.7 ms | 66.0 ms | 0.067 m | 34,784 |
| Medium | 94.4 ms | 298.7 ms | 0.033 m | 136,475 |
| High | 530.2 ms | 1,618.7 ms | 0.017 m | 536,408 |

(The blobs are the grid path in both builds; the build with the libraries has their threads.)

| sweep with Blend radii: 500 hostile arrangements a path (radii: none, 1 mm, 5 to 30 cm, 5 m, not a number) | built | refused | problems | with small faces on top of others | with faces of no area | boxes stitched | clusters built whole | clusters whose seam would not close (then built whole, or without their blends) | tries again | pieces below a thirty-second of the cell | clusters built without their blends |
|---|---|---|---|---|---|---|---|---|---|---|---|
| the rest on the exact path | 500 | 0 | **0** | 8 | 63 | 28 | 447 | 5 | 23 | 291 | 7 |
| the rest on the grid (cell 0.08 m), with the libraries | 500 | 0 | **0** | 8 | 62 | 24 | 450 | 8 | 38 | 291 | 7 |
| the rest on the grid (cell 0.08 m), without | 500 | 0 | **0** | 8 | 62 | 24 | 450 | 8 | 38 | 291 | 7 |

Hostile arrangements are heaps: nine tenths of their clusters are built whole (their boxes take more than half), so
this sweep is mostly the grid path's; 24 to 28 boxes were stitched. Before the last two rungs of the fallback 11 of the
500 were refused (the blend's grid would not close), then 7 (nor the Blockout's): those 7 are built without their
blends. The twelve rows of the older sweep (2,000 arrangements a kind) read what round 480's did, to the number: 0
problems, 0 refused.

**Unit tests' numbers** (untimed): the rounded cut removes 0.0371 m3 more than the square one (two rims of that
radius: 0.0364); its rim lies within 0.0293 m of the arc of its radius (a fifth: 0.03); 9 of 9 faces of a building
away from the column are bit for bit the unblended build's, on both paths; flat faces that run through a box lie on
their planes to 3e-9 m; two blended balls with radii 0.3 and 0.1 give the same volume in either order; roundness on a
ball sunk into a box: 26.3 degrees at Medium, 15.1 at High between neighbouring blended faces (96 at any detail before
the fix above; the spec had guessed 25 and 13 - a deviation, in the spec); the same bytes on one thread or many; a
cancelled build returns "cancelled" and the next is sound; a changed radius or detail rebuilds the blended cluster
only.

**Found and fixed before publishing:** the first code review (changes needed): a failed build of the rest was a
refusal, not a failed try; a box had the first piece's number and could be taken for a part of a room that keeps its
walls; blended pieces that meet blended with the later one's radius, not the larger; "below the detail" did not
depend on the detail (the texts said it did); the box's clearance, the weld's reach, a face with one corner twice.
The test-engineer: the crease above. The stress sweep: the refusals above. The second review (approve with changes):
the last rung was skipped when the blend's cell is the Blockout's; the report's fields after a fallback; more than
sixteen blended pieces at one point were melted in out of order; the handle's snap had no test. The third review
(of those changes; in a follow-up commit): the last rung's test was a random arrangement ported from the stress
section, whose pieces depend on the order a compiler evaluates arguments in - a different heap under Clang - so it is
now a test's setting that makes every build with blends fail (`Settings::fail_blends_for_test`); the status said one
cluster's fallback only when no other had lost its blends; the blended pieces' sort took memory at every grid point.

**Known limits:** the box holds the whole blended piece, so the cost follows the piece, not only the join - a small
radius on a large piece is expensive (the box's budget of 600,000 grid cells bounds it; the status shows the cell used);
a blended ball or column is round all over inside its box, on the blend's grid, and its faces there do not merge; a
flat face that runs through a box is two faces, cut along the box; where round pieces melt into each other in a
cluster built whole, small faces can lie on top of each other (the sweep's row counts them; whether the coarser
build before this round left them too was not measured); a cluster no grid closes over with its blends is built
without them; a failed seam is paid for by building the cluster again, up to eight times at the very worst; High misses
100 ms on the column in a building (the editor does not yet build Medium while a piece is dragged); Intersect pieces,
rooms and mesh pieces are not blended.

**Checked:** on main `037ec4fd` (the round's groups and `editor`, ADR 0134; the full suite is CI's). Windows with Blender's libraries: `blockout` 5,848 checks, `editor` 2,346, 0 failed; Windows dependency-free: 4,346 and 2,331, 0 failed; Linux (GCC, dependency-free): 4,348 and 2,331, 0 failed. Stress `--only blockout` in both Windows builds, timed in the hub's window (above); the other geometry sections were not run (the kernel's change is the blended path; the unblended sweep's twelve rows are unchanged to the number). Not run here: the ASan + UBSan group (CI's sanitizer shards run it). Audits: the coverage map (378 entries, 0 empty), `changelog.py check`, `docs_check.sh`. Implementer (the object and editor part), test-engineer (7 tests, one real fault), code review three times. After the third review's follow-up commit: Windows dependency-free `blockout` 4,357 and `editor` 2,331, Linux 4,359 and 2,331, 0 failed; the build with Blender's libraries was not run again for it (the same code on both).

<!-- README -->
replace: (round 479), with **wedges
with: (round 479), with a **Blend radius** a piece that rounds its join like a metaball while the rest stays crisp (round 481), with **wedges

<!-- docs/USABILITY.md -->
after: | A Blockout knew boxes, cylinders and spheres
| A rounded join sent its whole cluster - a whole connected interior - to the coarse grid: one rounded column changed and slowed the building. (round 481) | A piece's **Blend Radius** is meshed in a box of its own on a finer grid (Blend Detail) and stitched to the rest, which stays the same mesh to the bit. |

<!-- docs/ARCHITECTURE.md -->
append:
## Blockout: a blend in a box of its own (round 481, ADR 0130)
`build_cluster` (`src/scene/blockout.cpp`) builds a cluster with a Blend radius in parts: `plan_blend` (the boxes),
`build_regions` (the rest - the solids unblended plus each box as a Subtract solid, `Solid::region` - and each box's
inside on the grid at the blend's cell, the box as an Intersect solid), `stitch_parts` (`src/scene/blockout_seam.cpp`:
the faces on the boxes dropped, the two sides' corners made one, closed or refused). `mesh_cluster` is one cluster
through its path and the face maker. `ClusterReport` says what happened (`blend_regions`, `blend_whole`,
`blend_fallback`, `blend_dropped`, `seam_retries`, `seam_points`, `blend_below`, `blend_cell`, `seam_why`);
`Settings::blend_detail`. The blended field takes a solid into a brick by `Solid::melt` - the largest radius that
reaches it (`Grid::candidates`) - and melts the blended pieces in by ascending radius (`inside_blended`).
