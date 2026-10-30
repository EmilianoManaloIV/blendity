# 0262: the HCI pass measured - KLM, Fitts, hints, labels and walkthroughs after; the study's status; the write-up

- **Status:** done (round 282)
- **Requirements:** R-14
- **Decisions:** ADR 0085 (the evaluation protocol)
- **Model:** main session
- **PR:** #226

## Asked
The plan's last round (ADR 0085): run the measuring sticks of round 271 again on the finished pass, run the study, and
write up what changed for an artist from Unity, Unreal, Blender or the Adobe suite, on a keyboard, a pen or a tablet.

## What changed
- **The KLM table's after column** (`docs/hci/klm.md`) and `tools/hci_klm.py`, which recounts every row's operators from
  the stated rules and sums the columns (`--check` fails on a row whose seconds disagree with its operators). The
  recount corrected the baseline: 80.9 s, not 77.0 s (row 20 was left out of the sum; four rows were off by 0.1-0.2 s).
- **The Fitts audit again** (`docs/hci/fitts-282.md`), the **label audit** (`labels-282.md`) and the **hint table**
  (`hints-282.md`), each by the commands in `docs/hci/README.md`.
- **The four walkthroughs walked again**: an *After (round 282)* section in each, step by step, with what changed and
  what is left.
- **The study's status** (`docs/hci/study-282.md`): not run; what is ready, and the predictions it should test.
- The HCI README's rows; research/FEATURES.md rows for the keystroke-level model, the cognitive walkthrough and WCAG's
  contrast; a Learn question; the manual's line on the measures.

## The numbers

| Measure | Baseline (round 271) | After (round 282) | Source |
|---|---|---|---|
| KLM, 19 frequent actions, Unity preset | 80.9 s | 54.65 s (32 % less) | `py tools/hci_klm.py` |
| KLM, the same, Adobe preset | 80.9 s | 50.6 s | `klm.md`, rows 4 and 5 |
| Hard-coded shortcut labels the keymap does not produce | 121 | 0 | `py tools/hci_label_audit.py` |
| Tool-mode cells without a hint of their own | 2 of 8 | 0 of 8 | the `hci:` hint test |
| Walkthrough steps failing (four pipelines, 46 steps) | 24 | 7 failing, 4 partly | the walkthroughs' *After* sections |
| Targets under 7 mm at UI scale 1.0 | 1009 of 1099 (92 %) | 1282 of 1634 (78 %) | `fitts-271.md`, `fitts-282.md` |
| Targets under 7 mm at UI scale 1.5 | 925 of 1024 (90 %) | 1203 of 1385 (87 %) | same |
| Distinct targets under 7 mm at 1.0 | 420 of 440 (95 %) | 430 of 476 (90 %) | same |
| High Contrast theme, least of 14 text and signal pairs | (no such theme; Dark's error on the panel 2.8) | 5.0, dim text on the selection (4.5 asked) | the 0261 test's printout |
| SUS, task times | - | not measured: the study was not run | `study-282.md` |

Reading the Fitts rows: the pass added many controls (the tray, toasts' Fix buttons, toolbar buttons that were
right-click actions, the welcome), so the target count grew by half at 1.0. The share of small targets fell, but most
targets are still under 7 mm; the plan's assertion "no pen-primary target under 7 mm in Tablet Mode" was never written,
because the audit has no notion yet of which targets are pen-primary.

## What an artist arriving now meets
- **From Unity:** the same keys, labels that are always right, toasts that say what went wrong with a button that fixes
  it, a tray for every long job, Save prompts before anything replaces the scene.
- **From Unreal:** the welcome's Unreal answer gives Alt+P, End and the Alt+G/H/J/K views with Unreal's navigation.
- **From Blender:** Blender's keys and navigation from the welcome; Shift+I keys; `[ ]` size every brush.
- **From Photoshop or Substance:** B, E, V and `[ ]` from the welcome; Export Textures in a dialog; one brush grammar.
- **On a pen:** `]` / `[` where the wheel was needed (Bevel, Array, Grid, Pipe), Alt+click where a right-click was,
  left-drag panning in the Render window, 9 px key targets, the pen's eraser in Weight Paint. Still not: panning the
  UV editor, Adjust Last outside Edit Mode, the foliage features outside the console.
- **Needing contrast:** the High Contrast theme, measured.

## Tests
- `tools/hci_klm.py --check` passes on `klm.md` and fails on a copy with one row's seconds changed (checked by hand this
  round: "row 1 ... the before operators count 3.75 s, the row says 3.85 s", exit 1).
- The `hci:` tests print the Fitts counts and the hint table copied into the docs.

## Not done
- **The study.** No artist has used this build; the SUS target (70) and the 30 % task-time target are untested.
- The pen-primary Fitts assertion; an on-screen field for the modal parameters; UV panning without a middle button;
  Adjust Last for Knife, Draw, the primitives and the UV operators; the Strokes alpha, canopy fill and Scatter mask in
  the panels; time zoom in the dopesheet; Okabe-Ito polygroup colours; a help link in the hint; the GPU fallback badge;
  the exporter's skeleton, skin and animation.
- The KLM replay test (the counts are a tool over the table, not a replay of events).
