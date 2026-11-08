# Task 0396: Pass 16's write-up (round 420)

- **Status:** built
- **Requirements:** Q-06 (responsiveness)
- **Decisions:** none new (ADR 0105-0112 summarised)
- **Model:** main session (Opus 5.5)
- **PR:** stacked on #317 (round 419)

## Asked
The plan's round 420: docs/PERFORMANCE.md's Pass 16 tables before and after, the responsiveness table, SideRail and
Shinano navigation beside Unity's editor (Unity timed on the same scene), the honest negatives;
docs/hci/compare-unity-vrchat-2026-11.md gets the navigation table; `stress/results/baseline.md` refreshed; the lessons
"Rendering on the GPU" and "Performance and Stress Testing" extended; CHANGELOG / README.

## Built
- **`tools/unity_frames.py` + `bridges/unity/SdkCheck/BlendityFrameTiming.cs`:** Unity's editor opened with a window on
  a project copy (any project without `BLENDITY_COPY` refused), the scene or prefab opened (a missing one, a bad frame
  count or a scene without a main camera is an error), its renderers framed in the Scene view, the Scene view and then
  the Game view asked to draw again as soon as each has drawn while the view's camera is nudged; the interval between
  two of the view's actual draws (the Scene view's repaint events, the main camera's renders) over 300 draws after 30
  warm-up ones. No editor preference is changed.
- **docs/PERFORMANCE.md:** "Pass 16 in one table" (before / after, Unity beside Blendity, the open items ranked), the
  round-411 baseline kept as "Before Pass 16", and the final quick suite as "After Pass 16" (`pass16-after-table`).
- **`stress/results/baseline.md`** refreshed from the final quick run.
- **docs/hci/compare-unity-vrchat-2026-11.md:** the navigation table.
- **The lessons:** "Performance and Stress Testing" (Pass 16 in numbers), "Rendering on the GPU" (how the views compare
  with Unity's).
- **CI (round 413's test, the macOS job of #316):** "mesh operators on a worker" accepts the cube unchanged when the
  worker finished before the check but hasn't landed yet (a fast runner met it).

## Measured
- The quick suite on the final build against round 411's baseline: responsiveness rows over the budget 21 -> 4.
  Like-for-like regressions: Apply Modifiers (Subdivision) 39 -> 54 ms (oneTBB's allocator, ADR 0112); Embree's build
  of 64 meshes 4.2 -> 6.8 ms, the UV-grid texture's frame 51 -> 61 ms, `newslotmat` 4.3 -> 6.6 ms and moving an object
  in a textured Sky scene 7.3 -> 8.9 ms (1.2-1.6x, not chased this round); the remaining findings are the expected
  frames-count growth of operations now on workers, and one row label two tables share (the path tracer's "256").
- A full suite run's rows are other sizes than the quick baseline's (the undo section's 10 objects hold 983k vertices
  in a full run, 61.5k in a quick one): its "regressions" against the quick baseline are not regressions. Checked:
  the deep copy, the Embree build and Live Link's bytes are the same with oneTBB's proxy on and off.
- Unity 2022.3.22f1 against Blendity on the user's projects (the runs' JSON, not committed: the user's projects):
  SideRail's Scene view median / mean / p95 10.0 / 25.6 / 99.2 ms, its Game view 4.5 / 4.8 / 6.6; Shinano's Scene view
  2.1 / 2.2 / 2.9, Game view 2.4 / 2.5 / 3.4. Blendity (bench_world's means): SideRail 10.3 / 19.6 (card / system) and
  10.4 / 12.5 with both views; Shinano 7.0 (round 415) and 5.1 / 7.2 (round 416). A first Unity run left at the last
  layout's close-up pivot (and with the editor's Interaction Mode set to No Throttling, since dropped) gave SideRail
  1.9 / 2.0 and Shinano 1.5 / 1.2 - the framing decides the Scene view's number.

## The Opus review's fixes
No editor preference changed by the timing (Unity's EditorPrefs are the user's, shared by every project; the value was
checked back at its default after the runs); a missing prefab, a bad frame count and a camera-less Game view are errors;
every project must carry `BLENDITY_COPY`; argparse and a clear timeout; the Scene view framed on the scene's renderers
(the Unity numbers re-measured); the undo row out of the headline (a parity row that rose, now listed); "60 fps" only
where the card presents; the lessons' new sections after the existing questions, with their own Try-it and question;
`perf_suite.py --write` writes the after block; the CI fix checks that a worker job started (one check more: README
counts 16203 checks).

## Not done
- The open items docs/PERFORMANCE.md ranks: SideRail's `budget pc` and the unitypackage export run in one call;
  Generate Lighting's landing frame; Shinano's first F12 frame; Edit Mode at 500k faces on the CPU; the OBJ / USDA
  imports' landing frames; oneTBB's two slower rows.
- The `--laptop` and minimum-laptop runs the plan named: not run (no second machine here; `--laptop` throttles threads
  only and was not repeated for the write-up).
- The books (docs/book) are not re-edited (the user's standing instruction).
