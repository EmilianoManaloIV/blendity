## 2026-11-18 (round 566): A million splats on the CPU within the frame budget

**Asked:** round 557 left the CPU rasterizer over ADR 0105's 50 ms at a million splats (86.8 ms a frame orbiting the
Scene view: projection 41 ms, draw 37 ms; the card 6.2 ms). Bring the CPU path within 50 ms without changing the picture
beyond the splat tests' tolerances and without touching the card path.

**Found**
- The projection waited on memory, not arithmetic: it walked the splats in their drawing order, so each splat's 236 bytes
  (six arrays, 15 SH triples) were a read at a random place of a 236 MB cloud - 41 ms whatever the thread count. Then
  three `powf` (sRGB to linear) and a one-pixel call of the display encoder for every splat.
- The draw binned a million splats into tiles on one thread (two passes, eight calls of the C library's `floor` a splat),
  each tile read its splats through an index, and every pixel of every splat's box called `exp`.
- The pixel budget's count walked the million projected splats on one thread, every frame.

**Changed (task 0548)**
- Each splat is projected where it lies in the cloud's arrays, culled before its colour is touched, and written straight
  to its place in the drawing order (the order turned round: `rank[order[k]] = k`); only a frame with culled splats
  closes the list up with a second copy. The list's room is last frame's list, still constructed, so sizing it writes
  nothing. The same values in the same order: the screen list is bit for bit what it was. (An order that is not a
  permutation of the cloud - the sorter makes none - now draws nothing of that cloud.)
- The visible splats' colours go through the display encoder by the span (256 at a time), and the sRGB curve is read
  from a 4,096-step table (within 1e-7 of the curve below 1; the curve itself at 1 and above).
- The binning runs in parallel (each chunk counts its tiles, the counts are summed in chunk order, each chunk fills its
  own places from an 8-byte tile box kept from the count), the tile loop asks for its next splats ahead, takes a row's shared terms out of its pixels and evaluates
  the Gaussian with an `exp` written out in plain arithmetic (within 1e-7 of `std::exp` over a billboard's range).
- The pixel budget is counted in parallel, in chunks of a fixed size summed in order (its sums no longer add up in list
  order: a frame exactly at the budget's edge may keep or drop one splat differently than before, the same on any
  number of threads).
- `gsplat_project_one` is the same two halves the pass runs (shape, then colour), so one splat projected alone is still
  what the pass gives it.

**Timed** (the hub's windows of 2026-11-18, a quiet machine, normal priority; `blendity_stress --only splat_raster`,
RTX 4070 SUPER, 32 threads; each exe twice):

| Row (1M degree-3 splats, CPU) | Round 557 | Round 566 |
|---|---|---|
| Orbit, median frame | 84.4 / 84.2 ms | **38.6 / 37.9 ms** |
| Orbit, longest of 40 frames | 94.6 / 91.2 ms | **44.0 / 40.5 ms** |
| Project (in the orbit's last frame) | 38.2 ms | 15.1 / 16.0 ms |
| Draw (in the orbit's last frame) | 36.3 ms | 15.0 / 14.6 ms |
| Project / draw alone (the section's first table) | 36.7 / 29.2 ms | 13.0 - 14.1 / 11.6 - 11.7 ms |
| Orbit, 100k splats, median frame | 14.8 / 15.2 ms | 10.6 / 10.7 ms |
| Orbit on the card, 1M / 100k (not touched) | 5.3 / 3.2 ms | 6.0 / 3.3 ms |

Every frame of both runs is under ADR 0105's 50 ms: the CPU meets the budget at a million splats. The round's steps,
each timed against the one before in its own window (the windows differ by a millisecond or two between themselves):

| Step | Orbit, median / longest ms |
|---|---|
| Round 557 | 84.4 / 94.6 |
| The cloud's own order, colours by the span, parallel binning, the tile loop | 49.2 / 54.5 |
| + the sRGB table, no `floor` calls, the budget counted in parallel | 47.4 / 51.4 (project and draw unchanged: 3 ms was the budget's count) |
| + straight into the drawing slot, the list's room reused, 8-byte tile boxes | 38.6 / 44.0 |

What the steps showed: after the first, the frame was limited by the memory it moves, not by arithmetic - the table
instead of three `powf` a splat changed nothing measurable; taking a 64 MB copy, a 64 MB initialisation and a 64 MB
second read out of the frame took 8 ms.

**Review** (the first two passes): approve with fixes, all applied - the table's index is held inside the table whatever the compiler assumed
about NaN (`/fp:fast`), and a non-finite colour takes the curve's own answer as before; the binning's chunk count follows
the thread limit, so the one-thread test really bins differently; tests for the table against the true curve (dark, near
1, SH overshoot, Standard and Filmic) and for clouds with short arrays; the table is made when the program starts.

**Not done (named):** SH kept between frames or of a lower order at distance (both change the picture; not needed for the
budget). The binning's count table (tiles x chunks, made every frame, summed on one thread) grows with the picture's
size and the thread count - fine at 1080p, a few ms at 8K on 64 threads. Under a curve view transform (Filmic, ACES) a
splat's display colour may differ by one step from `gsplat_project_one`'s (the encoder's wide kernels round a curve one
step apart from its one-pixel path, as test_render's "all kernels agree" allows); Standard is exact.

**Second review** (the last pass): approve - every field of a slot that ends up visible is rewritten each frame and
every slot is visited; its one gap, a test that reuses one pass over frames of different clouds, ids, views, a culled
part and a trimmed frame, added.

**Checked:** on the tree merged with the stack's top - Windows with Blender's libraries: `splats_raster` 276 checks (the
card tests run), `splats` 427, `editor` 3,872, 0 failed; Linux (dependency-free): `splats_raster` 203, `splats` 425,
`editor` 3,846, 0 failed; the full suite on CI. The inventory sweep's command table, the fragments and the coverage map
check clean. Stress `--only splat_raster` in the hub's three timed windows (above); the timed exe is the code before the
merge with the top and before the second review's comments and tests - the same `gsplat_raster.cpp` statements. One of
the round's own checks was loosened with its reason in the test: "back to front" compares the projection's depths, which
are the sorter's to the last bits only (GCC put neighbours a rounding apart the other way; the list is the sorter's
order exactly, which the same test checks splat for splat).

<!-- docs/PERFORMANCE.md -->
append:
## Round 566: a million splats on the CPU

| Row (1M degree-3 splats, CPU) | Round 557 | Round 566 |
|---|---|---|
| Orbit, median frame | 84.4 / 84.2 ms | **38.6 / 37.9 ms** |
| Orbit, longest of 40 frames | 94.6 / 91.2 ms | **44.0 / 40.5 ms** |
| Project (in the orbit's last frame) | 38.2 ms | 15.1 / 16.0 ms |
| Draw (in the orbit's last frame) | 36.3 ms | 15.0 / 14.6 ms |
| Project / draw alone (the section's first table) | 36.7 / 29.2 ms | 13.0 - 14.1 / 11.6 - 11.7 ms |
| Orbit, 100k splats, median frame | 14.8 / 15.2 ms | 10.6 / 10.7 ms |
| Orbit on the card, 1M / 100k (not touched) | 5.3 / 3.2 ms | 6.0 / 3.3 ms |

The hub's timed windows of 2026-11-18 (a quiet machine, normal priority, both build slots held; RTX 4070 SUPER,
32 threads; each exe twice, round 557's in the first window, round 566's final exe in the third). Every frame of both runs is under ADR 0105's 50 ms: the CPU meets the budget at a million splats. The round's steps,
each timed against the one before in its own window (the windows differ by a millisecond or two between themselves):

| Step | Orbit, median / longest ms |
|---|---|
| Round 557 | 84.4 / 94.6 |
| The cloud's own order, colours by the span, parallel binning, the tile loop | 49.2 / 54.5 |
| + the sRGB table, no `floor` calls, the budget counted in parallel | 47.4 / 51.4 (project and draw unchanged: 3 ms was the budget's count) |
| + straight into the drawing slot, the list's room reused, 8-byte tile boxes | 38.6 / 44.0 |

What the steps showed: after the first, the frame was limited by the memory it moves, not by arithmetic - the table
instead of three `powf` a splat changed nothing measurable; taking a 64 MB copy, a 64 MB initialisation and a 64 MB
second read out of the frame took 8 ms.
