# Lighting Inspector gesture verification

The Windows release editor is the full-library build at `d79d27a7e09eeaa6cdde1a33906cbdfd5beaeaf9`. Its executable SHA-256 is `0de45444849c25761af4edd4e5cb25ac337e42e8c23051b824733e3469a3afad`. The `src` trees at the task base `a0cd6307` and the tested revision match.

The visible controls passed for all 66 previously unexercised inventory keys under World, Lighting Settings and Light, plus 21 keys whose earlier evidence used a console setter. Light's Cookie Size already had a visible field test. The two disclosure rows are verified by exposing a child control, editing it and restoring the collapsed state. [Per-key evidence](C:/blab/pass38-hub/0652-evidence.tsv)

| Control | Before | Edited and reopened | Changed pixels in 320 × 180 F12 render |
| --- | ---: | ---: | ---: |
| Sky Sun Elevation | 49.00195° | 61.00195° | 57,600 |
| Sky Sun Rotation | 147.52881° | 172.52881° | 36,265 |
| Sun Disc | 8° | 18° | 1,692 |
| Sun Disc Softness | 0.3 | 0.7 | 291 |
| Sun Disc Color | 1, 0.95, 0.8 | white | 385 |
| Ground Albedo | 0.3 | 0.7 | 57,600 |
| Sun Lights Scene | enabled | disabled | 21,300 |
| Spot Angle | 30° | 60° | 800 |
| Area Light Width | 1 | 2.5 | 20,902 |
| Horizon Band Color | 0.95, 0.75, 0.6 | white | 9,162 |

For both Sky angles the changed and original images exactly matched their respective console references. With a directional lamp active, both gestures still changed the saved angles and Undo restored them. The resulting renders differed by zero pixels. The independent audit checked 56 raw trials in the first batch. [Field snapshots and render comparisons](D:/Blendity-agent-scratch/pass38/0652/matrix1/results.json) · [Independent audit](D:/Blendity-agent-scratch/pass38/0652-independent/matrix1-audit.json)

The Sun Disc trials aimed a camera at the gradient sun. Each change remained visible after reopening, and Undo restored the starting value. [Disc camera and renders](D:/Blendity-agent-scratch/pass38/0652/sundisc1/results.json)

The first HDRI trial selected the visible **UV Grid (generated)** option. The scene retained `generated:UV Grid` across reopen. F12 reported a read failure for that name, and the two renders differed by zero pixels. Selecting a real asymmetric disk image changed all 57,600 pixels. Rotating that image by 45° changed 51,233. Task 0654 owns the generated-grid repair. [Generated versus disk audit](D:/Blendity-agent-scratch/pass38/0652-independent/hdri-audit.json) · [Disk controls](D:/Blendity-agent-scratch/pass38/0652/hdrifile1/results.json)

An independent driver used opposite 48-pixel scrubs and reopened every edited scene in a fresh process. Sun Elevation reached 53.80195° and 44.20195°, Sun Rotation reached 152.32881° and 142.7288°, and Spot Light Range reached 12.4 and 7.6. Undo restored the initial value in every case. The two sides of each pair differed in 124,177, 67,679 and 492 pixels respectively, in the same 460 × 270 shaded Scene view crop. [Independent gesture verification](C:/blab/pass38-hub/0652-test.md)

## Fixture and readbacks

Each process starts with its own copy of the legacy Sky test scene. The fixture contains a subdivided plane and no lamps. The test copies the scene file alone, so a saved bake cannot supply the active Sky lighting. The native editor constructs the requested context and draws a full-height Lighting window or the selected lamp's Inspector. `perf drag at` sends pointer press, movement and release events to the visible control. The fixture keeps the workspace, scene, preferences, caches, renders, temporary files and trash under the run directory. [Seed scene](../../tests/fixtures/sky_sun_bakes/sky/Legacy.scene) · [Replay driver](../../tools/dev/lighting_inspector_0652.py)

Each trial saves the starting scene and the result of an off-target gesture, edits the field, saves again, undoes the edit and reopens the edited file. The driver compares the complete saved World, Lighting Settings or Light component block at each stage. A successful trial changes the named property alone and reaches its independently specified value. The off-target and Undo blocks must match the starting block. The reopened block must match the edited block.

For rendered comparisons, the driver compares RGB pixels in 320 × 180 CPU raster output after Undo and after reopening the edited file. For each physical Sky angle it also compares both images with explicit setting commands and repeats the gesture with a directional lamp present. The lamp keeps precedence even when the saved world angle changes. Pipeline settings that require a bake, probe volume or Unity export have separate downstream effects.

## Reproduction

Use Python 3.11 or newer and a matching Windows package. Choose a new work directory for each run.

```powershell
py -3 -B tools/dev/lighting_inspector_0652.py --app $env:BLENDITY_TEST_APP --work "$env:TEMP/0652-sky" --field "field/EnvironmentSettings/Sun Elevation" --field "field/EnvironmentSettings/Sun Rotation"
```

Omit the `--field` arguments for the complete matrix. The driver retains each invocation, editor log, saved scene and render, plus `results.json` and `per-key.tsv` in the work directory.
