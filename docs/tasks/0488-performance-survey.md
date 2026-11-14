# 0488: The performance survey (round 504, Pass 25)

- **Status:** done (round 504)
- **Requirements:** Q-performance (ADR 0105's budgets)
- **Decisions:** none needed (ADR 0105 budgets, ADR 0056 tests never assert times, ADR 0112's 1.3x bar)
- **Model:** implementer (Sonnet) for the sections; main session for the timed run and the ranked list
- **PR:** (link when opened)

## Goal
The user asked: "Do an HCI and Performance sweep across the program to test, verify, and optimize components."
Before anything is optimized, every part of the program has a time on record: the parts the suite times today, and
the parts it does not - image adjustments and masks, sculpt and paint, the sequencer and video, the VRM / MMD / Godot
importers, the Unity exporter, the Materials / Textures / History windows, the command palette - plus what a first
use costs (the first open of each window, the first use of each tool). The result is one list ranked by what the
user would notice, `docs/perf/sweep-504.md`, from which rounds 506-508 fix from the top.

## Scope
- In:
  - New timed stress sections in a new file `stress/stress_survey.inc`, included by `stress/stress_main.cpp` and
    registered in its table (as `stress_posing.inc` / `stress_blockout.inc` are). Seven sections:
    1. `image_edit` - the image editor: each adjustment (levels, curves, hue / saturation, blur, sharpen, whatever
       the adjustments list holds), each mask operation (make, feather, invert, apply) and a stroke of mask paint on the image,
       at 1024^2 and 4096^2. Rows: the call's ms and, where it runs through the editor, the longest frame.
    2. `sculpt_paint` - Sculpt Mode strokes with each brush on a 100k- and a 500k-face mesh, vertex paint and texture
       paint strokes, driven by real mouse events through `step_frame_headless`: frame ms median / p95 / longest per
       stroke, against the live-drag budget (16.7 ms).
    3. `sequencer` - the sequencer and video: adding strips, scrubbing and playing a timeline with 1, 8 and 32 strips
       (image, colour, video where a codec is built in), a cross-fade, the encode of N frames: frame ms and total s.
    4. `importers` - VRM, MMD (PMX / VMD) and Godot (.tscn / .tres / .escn) imports of files the section makes itself
       in the scratch project (a writer where the program has one, else the smallest hand-made file scaled up - no
       file from `Assets/` or `research/`): total ms, the longest frame while it imports, the landing frame.
    5. `unity_export` - `export unitypackage` and the other Unity exporter commands on a 100-, a 1,000- and a
       10,000-object scene with materials and textures: total s, the longest frame (it is one call today), bytes.
    6. `asset_windows` - the Materials, Textures and History windows and the command palette: first open, the frame
       after, scrolling, typing a filter, with 10 / 200 / 2,000 materials, textures, undo steps; the palette's open
       and each typed letter.
    7. `cold_first_use` - in one fresh editor: the first open of every window in the Window menu (the first frame it
       is drawn and the one after), and the first use of every tool / mode (Edit Mode, each `edittool`, the draw tool,
       Sculpt, paint, Pose, Play, Blockout's tools through their console commands only), each against the second use.
       A row whose first use is over 100 ms, or more than 3x its second, is marked.
  - Every row's last cell is `ok` or `OVER` against its ADR 0105 budget (16.7 ms live drags and typing, 50 ms a frame
    while a job runs, 100 ms the one frame an indivisible operation lands in; a single call that blocks the window for
    longer than 100 ms is OVER whatever it is), in the tables' usual shape so `tools/perf_suite.py` reads them.
  - The sections are minutes long: left out of a run without `--only`, as `responsiveness` and the others are; `--quick`
    runs each at its smallest size so CI's quick stress and a unit check can cover them.
  - `tools/perf_suite.py`: the seven names added to `DEFAULT_SECTIONS` (round 511 saves the baseline that holds them).
  - `docs/perf/sweep-504.md`: the machine, the date, how it was measured, every OVER row of the whole suite and of
    the new sections, the misses already on record in `docs/PERFORMANCE.md` (Pass 16's and Pass 19's open lists,
    Pass 22's posing rows, Pass 23's landing frame), ranked by: how far over the budget (ratio), times how often the
    user hits it (every frame > every drag > every use of a common tool > a rare command). Each line: what the user
    sees, the number, the budget, the likely cause (file and function), the round that takes it (506 / 507 / 508 /
    out of this pass / left, with why).
  - A unit test per new section's set-up path where it adds a console command or a helper others build on (none
    expected); `tools/test_perf_suite.py` gets a case that the seven names are in `DEFAULT_SECTIONS` and are known to
    `blendity_stress` (the table's names read from the source).
  - `changelog.d/504-performance-survey.md`.
- Out (not in this task): any product code change (a fault found is written in the list, not fixed); `README.md`,
  `CHANGELOG.md`, `docs/PERFORMANCE.md`, `docs/ARCHITECTURE.md`, `stress/results/baseline.md` (rounds 511-512);
  skinning on the graphics card; anything of Blockout's files (its console commands are only called).

## Likely files
- `stress/stress_survey.inc` (new), `stress/stress_main.cpp` (the include, the table, the "named only" line)
- `tools/perf_suite.py`, `tools/test_perf_suite.py`
- `docs/perf/sweep-504.md` (new), `docs/tasks/0488-performance-survey.md`, `changelog.d/504-performance-survey.md`

## Acceptance criteria
1. `blendity_stress --only <name> --quick` runs each of the seven sections to its end in both builds (with Blender's
   libraries and `BLENDITY_NO_LIBS=1`), on Windows and Linux, writes its tables, and leaves the mesh / scene checks
   it makes (structural validity after sculpt, a re-import of what was exported) with zero problems. A part a build
   does not have (a video codec, an importer behind a `BL_WITH_*` switch) is a note in the report, not a failure.
2. Every row ends in `ok` / `OVER`; `python tools/perf_suite.py --sections image_edit --quick` reads the tables.
3. Nothing of the sections touches a file outside the scratch project and trash.
4. `docs/perf/sweep-504.md` holds the ranked list with numbers from one timed run of the whole suite on a quiet
   machine (the hub's window), and names the round for every line.
5. No product source file under `src/` changes.
6. Existing unit tests (`editor` group, the perf-suite tool test) pass; `python tools/test_coverage_map.py --check`
   still passes.

## Tests and checks to run
- `py tools/test_perf_suite.py`; unit: `--group editor` in both Windows builds and on Linux (WSL).
- Stress: each new section with `--quick` in both Windows builds and on Linux; the full run only in the timed window.

## Risks and edge cases
- A section that takes too long at full size: cap sizes so the seven together stay under about 25 minutes.
- Cold first-use rows are only cold once in a process: one fresh `Editor` for the section, each thing used first
  then second, nothing warmed before.
- Sculpt / paint through real events needs the Scene view's rectangle: use the helpers the `responsiveness` and
  `pushpull` sections already use.
- A window's name changing: read the list from the Window menu / the windows registry, not a hand-kept list.
- The dependency-free build has fewer codecs and importers: note, don't fail.

## Documentation to update
- `changelog.d/504-performance-survey.md` (with its `docs/PERFORMANCE.md` and `tools/README.md` lines as sections).
