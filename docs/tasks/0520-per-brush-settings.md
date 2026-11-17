# 0520: Size, strength and colour kept per brush; pressure curves for Hardness and Spacing (round 540)

- **Status:** done (round 540; Pass 27's plan, approved by the user 2026-11-15)
- **Requirements:** R-paint (sculpt, texture paint, weight paint), Blender parity (5.3's brushes)
- **Decisions:** none needed (a store beside the existing brush structs, no data-model change to scenes)
- **Model:** implementer (Sonnet), main session for the design
- **PR:** (link when opened)

## Goal
In Blender 5.3 every brush keeps its own size, strength and colour: switching from Draw to Smooth and back gives Draw
its own size again, instead of one size shared by all of a mode's brushes. Blendity does the same in Sculpt, Paint
(texture) and Weight Paint, with a "Unified" switch per value for those who want the old shared one. Paint's Hardness
and Spacing follow the pen's pressure through a curve you draw, as Blender 5.3's `curve_hardness` / `curve_spacing`. What
you set is still there after a restart.

## Blender's behaviour (read in `blender/source/blender`)
- `makesdna/DNA_brush_types.h`: `Brush::unified_paint_flags` and the brush's own `size`, `alpha`, `rgb`; the curves
  `curve_hardness`, `curve_auto_smooth`, `curve_spacing`, created as the default (linear 0 -> 1) curve in
  `blenkernel/intern/brush.cc` (`BKE_paint_default_curve`).
- `editors/sculpt_paint/mesh/paint_image_ops_paint.cc:618` / `sculpt.cc:5641`: with the hardness-pressure flag the dab's
  hardness is `hardness * curve_hardness(pressure)`.
- `editors/sculpt_paint/paint_stroke.cc:636`: with the spacing-pressure flag the spacing is
  `spacing * (1.5 - curve_spacing(pressure))` - a firm press packs dabs closer.
- `curve_auto_smooth` scales the sculpt brush's auto-smooth. Blendity's sculpt brush has no auto-smooth, no hardness
  and a fixed spacing, so only texture Paint gets curves (the task's "where Blendity's brushes have those settings").

## Scope
- In:
  1. **A brush bank** (new `src/editor/brush_slots.h/.cpp`): `BrushSlot {size, strength, color}` and
     `BrushBank {std::map<int, BrushSlot> slots; int active; bool unified_size, unified_strength, unified_color}`.
     `brush_bank_switch(bank, to, size, strength, color*)` stores the live values in `slots[active]`, then loads `to`'s
     slot (only the values that are not unified; a brush never used yet keeps the live values, so its first use carries
     the last one's), and sets `active = to`. Keyed by an int, so Weight Paint's new modes (round 539, worker P1) get
     their own slots without a change here.
  2. **Three banks on the Editor**: Sculpt keyed by `sculpt_brush_.kind` (size `radius`, strength), Weight Paint by
     `wp_brush_.mode` (radius, strength), Paint by `paint_brush_.mode` (Paint / Smudge / Blend: radius, `opacity` as the
     strength, `color`). `WeightBrush` / `SculptBrush` keep their fields untouched (the live brush); the banks sit beside
     them. `brush_banks_sync()` switches any bank whose `active` differs from its brush's current index. It runs right
     after every place a brush index changes (the Inspector's Brush / Mode enumerations, `sculpt brush`, `wpbrush`,
     `paint mode`) and once a frame as a backstop, so a size typed after a switch lands on the new brush.
  3. **Unified switches** in each brush section of the Inspector: "Unified Size", "Unified Strength" (and "Unified
     Colour" in Paint), off by default (per brush, as 5.3).
  4. **Paint's pressure curves**: `PaintBrush` gains `hardness_pressure`, `spacing_pressure` (off by default, so nothing
     changes until you turn them on) and `hardness_curve`, `spacing_curve` (`std::vector<Vec2>`, default {(0,0),(1,1)},
     evaluated by `curve_eval` in `core/ramp.h`). On: the dab's hardness is `hardness * curve(pressure)` (each dab, in
     `paint_dab_begin`, used by `paint_stamp`), and the step between dabs is `spacing * (1.5 - curve(pressure))`, at
     least 0.05 of the radius. The Inspector shows each curve (`ui::curve_field`) under its switch when on.
  5. **Saved**: `brushes.txt` beside `prefs.txt` (written with the preferences, read at start): the three banks' slots,
     their active index and unified switches, the curves and their switches. Unknown or malformed lines are skipped;
     NaN / out-of-range numbers are clamped as the Inspector clamps them. Tests write it in their scratch prefs folder.
  6. **Console** `brush`: `brush unified sculpt|weight|paint size|strength|colour on|off`,
     `brush curve hardness|spacing on|off [x y x y ...]`, `brush save`, `brush load`, `brush list` (each bank's slots).
- Out: Auto-Smooth (Blendity's sculpt has none), sculpt hardness / spacing curves, brush assets as files of their own,
  vertex paint (Blendity has none yet; round 539 adds one and can key a fourth bank with one line), the sketch and
  scatter brushes.

## Likely files
- new `src/editor/brush_slots.h`, `src/editor/brush_slots.cpp` (bank, switch, text save / parse)
- `src/editor/editor.h` (three `BrushBank` members, `brush_banks_sync`, `PaintBrush`'s four fields, test hooks)
- `src/editor/panels.cpp` (only the Sculpt, Paint and Weight Paint brush sections)
- `src/editor/sculpt.cpp` (`sculpt brush` setter), `src/editor/weight_paint.cpp` (one line in `wpbrush`, agreed with
  the hub / P1), `src/editor/paint.cpp` (curves at the dab and the step; `paint mode` setter)
- `src/editor/editor.cpp` (load at init, save in `save_prefs`, the once-a-frame sync), the console dispatcher
- `tests/test_sculpt.cpp` (new group at the end), `licenses/README.md` (a "Ported, not copied" row - same licence
  family as Blender, GPL-2.0-or-later), `changelog.d/540-per-brush-settings.md`

## Acceptance criteria
1. Real events: in Sculpt, choose Draw, set Size 30; choose Smooth in the Inspector's Brush field, set Size 90; choose
   Draw again: Size reads 30; Smooth again: 90. Strength likewise.
2. The same through the console (`sculpt brush`, `sculpt size`), Weight Paint (`wpbrush add 40` / `wpbrush blur 70`) and
   Paint (Mode Paint / Smudge with different sizes and colours).
3. With Unified Size on, switching brushes keeps the one size; strength stays per brush unless its switch is on too.
4. A brush used for the first time starts with the size you had (no jump to a default).
5. Paint: Hardness Pressure off - a dab's hardness is the field's whatever the pressure. On with the default curve - a
   dab at pressure 0.5 has half the hardness; a curve (0,1),(1,1) gives the full hardness. Spacing Pressure on: at
   pressure 1 the dabs come closer than at 0.25 (fewer dabs for the same drag at low pressure).
6. Saved: set different sizes on two brushes and a curve, `brush save` (or the preferences saving), reset the banks,
   `brush load` (or a new Editor on the same prefs folder): every value is back. A truncated / garbage `brushes.txt`
   loads what it can and never crashes; NaN sizes come back clamped.
7. `tools/test_coverage_map.py --check` finds the `brush` command tested.
8. Existing sculpt / paint / weight-paint tests pass unchanged (defaults keep today's behaviour until you switch brushes).

## Tests and checks to run
- Unit: `BLENDITY_TEST_FILTER="per brush"`, groups `sculpt` and `editor`, then the full suite on CI (Windows + Linux).
- Stress: none (no geometry change).

## Risks and edge cases
- A size typed in the same command after a brush switch must reach the new brush (sync right after the switch).
- Sculpt's Spray stroke changes `radius` / `strength` for a dab and restores them (`sculpt.cpp:1026`): a sync must
  not run inside that window (it only runs at switches and at the frame's start).
- P1's round 539 changes Weight Paint's mode list: the bank is keyed by index, and an index past the list is ignored.
- The preferences folder in tests is a scratch one; nothing writes the user's.

## Documentation to update
- `changelog.d/540-per-brush-settings.md` (with a README line and a USABILITY "fixed" row as sections), a Learn
  lesson step for per-brush size (the Sculpt lesson), `licenses/README.md` row.
