# 0548: A million splats on the CPU within the frame budget (round 566)

- **Status:** done. Timed (the hub's windows, 2026-11-18): the 1M CPU orbit 38.6 / 37.9 ms median, the longest of 40
  frames 44.0 / 40.5 ms (round 557: 84.4 / 94.6) - ADR 0105 met; the card path untouched.
- **Requirements:** Q-06 responsiveness (ADR 0105: no editor frame over 50 ms while work runs)
- **Decisions:** none needed (the same projection, order and blend; only how the work is laid out in memory and over the
  threads changes)
- **Model:** main session (worker W4 of Pass 28); code-reviewer as usual
- **Plan:** Pass 28 (approved 2026-11-18), the follow-up round 557 named: "the CPU at 1M still misses it (86.8 ms)".
  Stacked on the pass's current single top.
- **PR:** https://github.com/EmilianoManaloIV/blendity/pull/470

## What round 557's timed window found
Orbiting the Scene view round a 1M-splat degree-3 cloud on the CPU rasterizer: median frame **86.8 ms** (ADR 0105:
50 ms); in the last frame the projection took 41.0 ms, the draw 36.9 ms, the sort 0.05 ms (a worker's). The card draws
the same orbit in 6.2 ms. The CPU draws a view when there is no card, or when the card refuses a frame.

Where the two parts go (read from the code, measured in this round):
- **Project.** `SplatPass::project_cloud` walks the splats in their back-to-front order, so every splat's position,
  opacity, scale, rotation, base colour and 15 SH triples (236 bytes in six arrays) are read at a random place of a
  236 MB cloud: the loop waits on memory, not on arithmetic, and more threads don't help it. Each splat's colour then goes
  through three `powf` (sRGB to linear) and a one-pixel call of the display encoder.
- **Draw.** `SplatPass::draw_cpu` bins the splats into tiles on one thread (two passes over a million splats), then every
  tile reads its splats through an index (another random read each) and calls `exp` for every pixel of every splat's box.

## Goal
The Scene view orbits a 1M-splat cloud on the CPU rasterizer within 50 ms a frame, with the picture, the pick ids and
the stats what they were.

## Scope
- In (`src/render/gsplat_raster.{h,cpp}`, the CPU path only):
  1. **The projection in the cloud's own order.** Each splat is projected where it lies in the arrays (sequential
     reads), a splat is culled before its SH is touched, and the visible ones are then gathered in back-to-front order
     (one read of a projected splat each, prefetched). The same `gsplat_project_one` values, bit for bit, in the same
     order.
  2. **Colours by the span.** A chunk's linear colours go through the display encoder as one span instead of one call a
     splat (the same encoder, the same pixels).
  3. **Binning in parallel**: each chunk of the screen list counts its tiles, the counts are summed in chunk order (the
     order in every tile stays), each chunk fills its own entries.
  4. **The tile loop**: the next splats of a tile prefetched; a row of a splat's box evaluated with the row's terms
     hoisted; the Gaussian through an `exp` written out in plain arithmetic (within 2e-7 of `std::exp` over the
     billboard's range), so the compiler can keep it in registers.
  6. What the second window named (47.4 ms, the longest 51.4: projection and draw unchanged by item 5 - the frame is
     limited by the memory it moves): each splat written straight to its place in the drawing order (no second copy
     unless something is culled), the list's room reused from the last frame, the binning's fill from an 8-byte tile box
     a splat.
  5. What the first timed A/B named (84.4 -> 49.2 ms, the longest frame 54 ms): the sRGB-to-linear curve of a splat's
     colour read from a 4,096-step table instead of three `powf` (within 3e-8 of the curve below 1; the curve itself at 1
     and above); the C library's `floor` out of the binning and the tiles (a call each without SSE 4.1); the pixel
     budget's count over the screen list in parallel.
- Out: the card path (`gsplat_gpu*`, `gpu_raster*`, `scene_splats_to_card`), the sorter, the pixel budget's value,
  lower-order SH at distance and SH kept between frames (both change the picture: only if 1 - 4 miss, and then as a named
  follow-up with its error measured), the path tracers.

## Likely files
- `src/render/gsplat_raster.{h,cpp}`
- `tests/test_splats_raster.cpp` (a new group at the end: round 566)
- `stress/stress_splat_raster.inc` only if a row needs a part printed (the section is W4's row's; W5 owns `splats`)
- `changelog.d/566-splat-cpu-1m.md`

## Acceptance criteria
1. `SplatPass::prepare`'s screen list for a cloud equals, splat for splat and bit for bit, what `gsplat_project_one`
   gives each splat in the sorter's order (culled ones left out) - for a degree-3 cloud, a Points cloud, a perspective
   and an orthographic view. Under Standard the colours too; under a curve (Filmic) within one display step - the span
   encoder's wide kernels round a curve one step apart from its one-pixel path (test_render's "all kernels agree").
2. `draw_cpu`'s picture of a dense cloud is within 1/255 a channel of a plain reference blend of the same screen list
   (front to back with `gsplat_alpha_at`, written in the test), and the id plane is the same.
3. A splat whose box crosses tile and chunk borders is drawn once in every tile it touches, in order: a picture of
   overlapping splats is the same whatever the thread count (the job system at 1 thread and at its default).
4. Round 532's and 557's tests pass as they are (NaN, zero-scale, huge, transparent, empty; the budget; the card match
   under 3/255).
5. Timed (stress `--only splat_raster`, a hub window): the 1M CPU orbit's median frame under 50 ms; or the miss named
   with its parts and what it would take. A test prints, never asserts, a time (ADR 0056).

## Tests and checks to run
- `--group splats_raster`, `splats`, `editor` on Windows (both builds) and Linux (WSL lock); the full suite on CI.
- Stress `--only splat_raster` in the hub's timed window (the exe in `C:/blab/w4-timed-566/`).

## Risks and edge cases
- The gather must keep the sorter's order exactly (several clouds are merged by depth afterwards).
- A cloud whose arrays are short (opacity, SH) is handled by `gsplat_project_one` as before - the span encoder only
  takes what that function would have encoded.
- OCIO view transforms don't go through `encode_span`: those keep the one-splat path.
- GCC / Clang / MSVC `/fp:fast`: the written-out `exp` must not depend on the compiler's own (tested against
  `std::exp` at fixed points).
- A view of zero size, an empty order, a cloud edited while a worker sorts: as before (the order's size is checked).

## Documentation to update
- `changelog.d/566-splat-cpu-1m.md` with its PERFORMANCE section (the timed row) and an ARCHITECTURE line if a function
  others build on appears.
