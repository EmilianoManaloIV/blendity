# Task 0309 - Blender's converters as texture filters (round 329) - done

Part of the shader-graph request (task 0307).

## What changes
- ColorRamp with stops (`std::vector<RampStop>`), Interpolation (Constant, Linear, Ease, B-Spline), Input (Height,
  Mask, Luminance); Low / High Colors shown until the ramp has stops of its own, so old files read as before.
- Map Range, Clamp, Math (15 operations; a value or another plane), RGB Curves (Combined, Red, Green, Blue; monotone
  cubic), Separate Channel (R, G, B, H, S, V into the mask or height) - each on one plane via `convert_plane` /
  `convert_point`, the same in the planes and live shading.
- `Reflector::ramp` / `curve` (text by default); `ui::Context::color_ramp` and `curve_field`; the Inspector draws them
  and records the edit as text; `core/ramp.h` (`ramp_eval`, `curve_eval`).

## Checks (tests_round329)
- Ramp evaluation exact at the stops; Constant / Linear / Ease; the old ramp unchanged; saved and read back.
- Map Range, Clamp, all Math operations, Separate Channel against formulas; identity and lifting curves.
- By clicks: add, move and remove a stop; add a curve point.

## The review's fixes
Low / High stay (the ends' colours) so the bar never moves under a click; the position field keeps the picked stop;
Blender's formulas (Snap, smooth min / max, stepped Map Range, Combined first, Clamp reversed); Cardinal named so; the
albedo clamped in live shading too; no list per point; a grab offset; finger scrolling; 32 at most; exact text; the
lesson's Try-it sets three stops.
