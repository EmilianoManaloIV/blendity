# 0154: "Anime" and "Manga" as single camera filters

- **Status:** done (round 165)
- **Requirements:** R-03 (rendering), R-02 (the editor)
- **Decisions:** ADR 0008 (the filter stack) unchanged
- **Model:** main session
- **PR:** (round 165, with tasks 0145 and 0146)

## Goal
The user: "For the camera 'Anime' and 'Manga' filter. Make its own separate filters not a group composite and make it
one unit." The Looks menu's Anime and Manga used to append three or four effects; each is now one effect with its
own few fields, drawing exactly what the composite drew.

## Scope
- In: `AnimeEffect` (Lines, Line Color, Line Thickness, Crease Angle, Saturation, Contrast, Bloom, Bloom Threshold)
  and `MangaEffect` (Grayscale, Tones, Dots, Dot Cell, Dot Contrast, Lines, Line Thickness, Paper) in
  `src/scene/scene.h/.cpp`, registered under Stylize; their `contribute()` builds the part effects (Line Art, Color
  Grading, Bloom; Grayscale, Posterize, Halftone, Line Art, Paper) and lets them push their passes, so the picture is
  the parts'; `apply_filter_look` adds the one effect (the Looks menu, `filter look`, `filter add Anime`); the Looks
  tooltips and the anime lesson updated.
- Out: removing the part effects (older scenes hold them; they stay); a GPU path for the passes (none existed).

## Acceptance criteria
1. `filter look Anime` adds one Anime effect whose rasterized frame matches the old three-effect stack within a
   step of 255; turning Lines off changes the picture; the effect saves and loads with its fields.
2. `filter look Manga` the same against the four-effect stack; Tones 3 changes the picture; a scene holding the old
   composite loads as its four effects and renders as before.
3. The filter registry's table test covers both (a `set` of Line Thickness and Dot Cell), Stylize now has ten.

## After the review
- Nothing to change: the parts' passes capture their parameters by value, the defaults match the old stacks, old
  scenes keep their part effects.

## As built
- `src/scene/scene.h`, `src/scene/scene.cpp`, `src/editor/panels.cpp` (tooltips), `src/editor/lessons.cpp`; tests:
  round 165 and the round-31 registry table, round 61's looks test.
