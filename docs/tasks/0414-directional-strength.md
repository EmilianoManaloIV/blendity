# Task 0414: Directional strength (round 438)

- **Status:** built
- **Requirements:** R-03 (rendering), R-08 (VRChat worlds and avatars)
- **Decisions:** ADR 0113 (the bake's quality target), ADR 0114 (directional lightmaps, amended)
- **Model:** main session (Opus 5.5)
- **PR:** stacked on #335 (round 437)

## Asked
Pass 18's plan, round 438: store a direction's length as Unity does, so a normal map leans the baked light as much as
Unity's render does. Check against Unity's own direction maps on SideRail (the length's spread) and the stations' SSIM.

## Found
- Since round 427, a texel's saved direction is the luminance-weighted mean of where its direct light came from, with
  the bounced and sky light weighing in with no direction. Its length is the share of light one lamp or the sun gives.
- On SideRail, that share's median is 0.744 (90th percentile 0.949). Unity's own direction maps for the same scene:
  median length 0.117 (90th percentile 0.715). Unity keeps a direction short unless one light clearly dominates.
- The decoders (`lightmap_direction_factor`, the card's `lm_dir_factor`) tilt the light by the half-Lambert ratio,
  so the length is directly how strongly a bump catches baked light: ours leaned about six times as much as Unity's
  where light was mixed.
- Unity's divisor (the map's alpha, median 0.72) needs no copy: our decoder takes the divisor at the surface's own
  normal from the saved vector, which is the same rebalance, so an unbumped surface keeps its baked light exactly.

## Built
- **`Lightmapper::saved_direction`:** the mean direction scaled to length share^7.25 (`kDirectionPower`), the way it
  points kept. A single light alone stays at full length; 0.744 becomes 0.117, and 0.949 becomes 0.684.
- The power is the one exponent that maps our median onto Unity's; the 90th percentile then lands within 0.03 of
  Unity's, which says the curve's shape fits and not just one point.

## Measured (this machine)
| SideRail (Blendity's own bake, against Unity's render) | round 430 | round 438 |
|---|---|---|
| direction length over lit texels: median / 90th percentile (Unity's maps: 0.117 / 0.715) | 0.744 / 0.949 | **0.165 / 0.701** |
| spawn: MAE / SSIM / grid | 3.71 / 0.812 / 0.899 | 3.70 / 0.813 / 0.899 |
| spawn_back (dark) | 1.37 / 0.772 / 0.360 | 1.37 / 0.772 / 0.360 |
| floor | 4.36 / 0.747 / 0.974 | **4.25 / 0.773** / 0.974 |
| ahead | 3.06 / 0.883 / 0.958 | 3.06 / 0.885 / 0.959 |

- The floor, the station with the most normal-mapped surface in view, gains the most SSIM (+0.026). The other stations
  barely move: their surfaces are mostly lit by bounced light, whose direction was already short.
- Unity's 10th percentile is also 0.117: Unity never stores a direction shorter than that, while ours go to 0.007
  where the light is all bounced. Both mean "almost no lean" - a bump tilts the light by under 1 % either way - so
  it was left as it is.
- ADR 0113's target is still missed on SSIM at spawn, floor and ahead. The next rounds take the other causes named in
  Pass 17's write-up: emitters (439), seams (440).

## Tests (tests_round438, lighting group)
- The calibration: shares 0.744 and 0.949 give 0.117 and 0.684; full length stays 1; zero stays zero; the direction
  kept.
- A floor under a slanted sun: alone, its directions are full length; with a bright sky, under half as long, and a
  bump toward the sun gains less light (x1.005 against x1.142), never less than none.
