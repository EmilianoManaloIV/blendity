## 2026-11-14 (round 504): the performance survey - seven new stress sections and the sweep's ranked list (task 0488)

**Asked:** "Do an HCI and Performance sweep across the program to test, verify, and optimize components." (Pass 25
Speed's first round: every part of the program gets a time on record before anything is optimized.)

**Added (task 0488):**
- **Seven timed stress sections** in `stress/stress_survey.inc`, for the parts the suite did not time: `image_edit` (each
  adjustment, mask operation and a mask paint stroke at 1024^2 and 4096^2), `sculpt_paint` (every Sculpt brush on a 100k-
  and a 500k-face sphere, a texture-paint and a weight-paint stroke, by real mouse events; the times are the frames in
  which the brush worked), `sequencer` (1, 8 and 32 strips added, scrubbed and played, a cross-fade, a render to video,
  the encoder), `importers` (VRM, MMD PMX and VMD, Godot `.tscn`, files the section writes itself; the landing frame timed
  on its own), `unity_export` (100, 1,000 and 10,000 objects imported, exported to a package and imported again),
  `asset_windows` (the Materials and Textures windows with 10 / 200 / 2,000 things, the History window with 10 / 200 undo
  steps - its cap - and the command palette) and `cold_first_use` (the first and the second use of every window of the
  registry and of every tool and mode, from an editor that has drawn nothing). Every row ends in `ok` / `OVER` (or `-`)
  against ADR 0105's budgets. Each is named-only (`--only <name>`); `--quick` runs it at its smallest size.
- **`docs/perf/sweep-504.md`: the ranked list** of what the user would notice, from one timed run of the whole suite on a
  quiet machine (28 minutes, all 23 sections; its tables in `docs/perf/sweep-504-tables.md`), with the misses already on
  record. Its top: the Scene view of 50,000 cubes (33-34 ms a frame, round 506); the image editor's Clarity / Dehaze /
  Sharpen sliders (30-51 ms a frame) and sculpting at 500k faces (20 ms a frame) (round 508); a Unity scene's import in
  one frame (1.74 s at 10,000 objects), `export unitypackage`, `budget pc`, Generate Lighting's landing frame and a
  render to video's frames (round 507). Most of the newly timed parts are inside their budgets: the sequencer, the
  History window, the command palette, the paint strokes, the VRM / PMX imports, every cold first use but the Render
  window's.
- **Found, not fixed here:** `export unitypackage` on a folder with sub-folders of several files can throw `bad
  allocation` or write wrong path names (a reference kept into a list that grows during the folder walk). Round 507's,
  with a unit test.

**Changed**
- `tools/perf_suite.py` runs the seven by default (`DEFAULT_SECTIONS`), and a seconds column counts as slower from a
  tenth of a second (it needed a whole second: a 0.9 s import that doubled was not reported). `tools/test_perf_suite.py`
  pins the new tables' shapes: the first time column is the time, a changed frame count alone is no regression.

**Checked:** no file under `src/` changed. The seven sections `--quick` to their end on Windows (with Blender's
libraries and dependency-free) and Linux, and at full size with the libraries; the `editor` test group - with Blender's
libraries 2,333 checks; dependency-free 2,331 (Windows) and 2,330 (Linux); `py tools/test_perf_suite.py`,
`py tools/test_coverage_map.py --check`. The whole suite is CI's.

<!-- docs/PERFORMANCE.md -->
append:
## Round 504: the performance survey's sections

`blendity_stress --only image_edit | sculpt_paint | sequencer | importers | unity_export | asset_windows | cold_first_use`
(named-only; `--quick` for the smallest size). Their numbers and the ranked list are `docs/perf/sweep-504.md`.

<!-- tools/README.md -->
append:
## Round 504: the survey's stress sections

`tools/perf_suite.py` runs `image_edit`, `sculpt_paint`, `sequencer`, `importers`, `unity_export`, `asset_windows` and
`cold_first_use` with the rest of `DEFAULT_SECTIONS` (`stress/stress_survey.inc`; round 511 saves the baseline that holds them).
