# Task 0364: Adobe in the HCI comparison (round 387)

## Asked
Phase D's first round (plan cozy-fluttering-corbato; the user, 2026-10-31: "consider the basic adobe suite in the HCI
testing like the programs; fix all fixes"; their choice: Photoshop + Fresco, Substance Painter, Illustrator, After
Effects + Premiere):
- four new use cases made in Blendity as round 350's were;
- each compared with the Adobe apps' documented steps, cited;
- Adobe in the KLM table, the audience letter A in the findings, the comparison and its page updated.

## Done
- **Four productions** made headless with Blendity built on 2026-11-01 from rounds 351-386, each stage from an empty
  scratch project (stage lists `scratchpad/hci_D.json` to `hci_G.json`, run by `hci_prod.py` with `HCI_FRESH`; the film
  with the build with Blender's libraries). The review found the first run had used an editor from before round 364
  (`build.bat release tests` builds the tests alone): both editors were rebuilt and every stage run again.
  - **E. An illustration and concept sheet** (Photoshop + Fresco): a paper, three layers with blend modes, pressure
    linework with the guides, colour; free transform, selections, fill and masks tried.
  - **F. Texturing the avatar** (Substance Painter): UVs, a surface, mesh maps, smart materials and a generator, paint
    channels and fills, the texture set exported for Unity, Unreal and glTF; URP and Poiyomi tried.
  - **G. A logo** (Illustrator): a paper as the artboard, shapes by the guides; vector shapes, the pen, fill and SVG
    tried.
  - **H. Editing the film** (After Effects + Premiere): a shot, a paper and sound on the Sequencer, a camera filter, an
    MP4; footage, a blade cut, a ripple delete, keyed opacity and export presets tried.
- **`docs/hci/compare-2026-10.md`:** a section per production (Blendity as made, Adobe per its docs, the friction
  found), Adobe's row in the verdict per audience, findings 16-28 (audience A), sources [A1]-[A11].
- **KLM:** Adobe as a sixth program (`tools/hci_klm.py`'s `PROGRAMS`), on rows 5, 7 and 10 and ten new rows (11-20):
  free transform, a blend mode, a clipping mask, a rectangle, SVG export, baking mesh maps, exporting the texture set,
  a blade cut, a ripple delete, keyed opacity. `py tools/hci_klm.py docs/hci/compare-2026-10.md --compare --check`
  recounts every cell (20 tasks; each program's total is over the rows it does, which differ: Blendity's 13 and
  Adobe's 13 share 4). `--check` now also fails on a table it reads no row from, and a row from before Adobe counts
  Adobe's cells as "-".
- **The page** ("Blendity Against Four Programs", the same link) gained the Adobe section, its findings, the ten
  Adobe tasks and an Adobe column.

## Findings (Nielsen severity; A = Adobe users)
| # | Finding | Sev. | Fix |
|---|---|---|---|
| 16 | No free transform (Ctrl+T) on a sketch layer or paper | 3 | Round 388 |
| 17 | Four blend modes on sketch layers | 2 | Round 388 |
| 18 | No layer masks or clipping masks | 3 | Round 388 |
| 19 | No pixel selections and no fill | 3 | Round 388 |
| 20 | `paper new` leaves the Draw tab drawing on the 3D scene | 2 | Round 388 (small) |
| 21 | No vector shapes, Bezier pen, fills or strokes on a paper | 3 | Round 389 |
| 22 | No SVG export or import | 3 | Round 389 |
| 23 | No video footage, blade cut or ripple delete on the Sequencer | 3 | Round 390 |
| 24 | No keyed opacity or transform on cards; filters not keyed per clip | 2 | Round 390 |
| 25 | No video export presets | 2 | Round 390 |
| 26 | No URP or VRChat-shader texture export presets | 2 | Round 391 |
| 27 | A paper card takes a file path, not a name | 1 | Round 390 (small) |
| 28 | Unknown sub-verbs answer with a usage line, not a warning | 1 | Round 388 (small) |
| 29 | "Object > Bake Mesh Maps" in messages; the menu is GameObject | 1 | Round 391 (small) |

## Checked
- The KLM recount: `--compare --check` exits 0.
- The coverage map and the consistency audit stay at their baselines (no product code changed).

## Limits
- One analyst, an expert path; Adobe's apps not installed or run - their steps are what Adobe's help describes.
- The productions are console-driven; GUI paths were read from the code (`file:line` in the comparison).
- A scripted stroke with the Ruler or Ellipse guide on drew only a tick (the guides shape a live drag), so E3's guide
  strokes don't stand for the GUI's.
- The mesh-map bake was still running when F's headless runs ended: its generators read flat grey there.
- Adobe steps marked "(assumed)" in the comparison have no Adobe page behind them.
