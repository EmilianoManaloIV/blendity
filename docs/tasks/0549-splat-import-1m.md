# 0549: A measured 1M-splat import (round 567)

- **Status:** done (round 567); the timed rows come from Pass 28's timed batch
- **Requirements:** R-10 (import), Q-03 (responsiveness), Q-01 (robust data)
- **Decisions:** none new (ADR 0147 the layout, ADR 0056 measurements, ADR 0105 / 0107 the frame budget and the import job)
- **Model:** main session (worker W5 of Pass 28); code-reviewer agent
- **PR:** (link when opened; on Pass 28's current single top)
- **Approval:** covered by the user's approval of Pass 28's plan (2026-11-18, relayed by the hub).

## Goal
Round 531 measured the splat readers at 100,000 splats; round 544's write-up says of a million "not measured (about
0.4 s estimated at 2.6 M splats / s)". Afterwards the `splats` stress section measures a 1,000,000-splat import of a
3DGS PLY and of an SPZ - time and memory per stage - and the editor's frames while such a file is imported; whatever
the numbers show to be plainly wasteful in `src/scene/import_splat.cpp` is fixed, and the editor never stalls past
the responsiveness budget (ADR 0105: 50 ms a frame while a job runs, one frame of at most 100 ms for what can't be
split).

## Scope
- In:
  - `stress/stress_splats.inc`: rows for 1,000,000 splats of SH degree 3 (`--quick`: 100,000), PLY binary and SPZ v3
    (gzip), built in code as the 100k rows are and written to the run's scratch folder (`BLENDITY_PROJECT`, which the
    stress program points at a temp folder): file size; the file read from disk; the unpack (SPZ's inflate); the
    decode (bytes to the cloud's arrays); the build (the change of axes and `pointcloud_validate`); the total and
    M splats / s; the cloud's bytes and the process's working-set growth at its peak. A second table: the same files
    through the editor's `import` on a headless editor with jobs on workers - the longest frame while the file is
    read, the landing frame, the total.
  - `src/scene/import_splat.h/.cpp`: the stage times a reader leaves in its `SplatReadReport` (a few clock reads),
    and the fixes the measurement asks for (candidates: the PLY reader's per-property `double` decode, the single
    thread of the decode and the change of axes, a second copy of the unpacked SPZ).
  - `tests/test_splats.cpp`: a sample of the sweep (20,000 splats, PLY and SPZ) - the stage times are filled and
    printed, never asserted; the cloud a fixed reader gives is the same, value for value, as before the fix.
- Out: the draw and trace of 1M splats (rounds 557, 566), SPZ v4's zstd, the glTF reader, `image.cpp`'s inflate
  (another owner's file: named in the write-up if it is the bottleneck), export.

## Likely files
- `src/scene/import_splat.h`, `src/scene/import_splat.cpp`
- `stress/stress_splats.inc` (`measure_import_1m`, `measure_import_editor`)
- `tests/test_splats.cpp` (`tests_round567`)

## Acceptance criteria
1. `blendity_stress --only splats` prints, for 1M splats, a PLY row and an SPZ row with file MB, read, unpack,
   decode and build ms, total, M splats / s, cloud MB and peak working-set growth; the files are written under the
   run's scratch folder and removed afterwards.
2. The same section prints the editor rows: longest frame while reading, the landing frame, total; a row over the
   budget is marked, and the round fixes it or says why not.
3. A reader's result is unchanged by the round's fixes: for the same bytes the cloud's arrays are equal to a
   reference decode kept in the test (to the float), on damaged files as on good ones (the fuzz: 0 problems).
4. A unit test keeps a small sample of the sweep and prints its times; no test asserts a time.
5. `docs/PERFORMANCE.md`'s "1M under 2 s (not measured)" cell is replaced by the measured numbers (through the
   changelog fragment).
6. Windows and Linux: groups `splats` and `editor` pass; `--only splats` 0 problems.

## Tests and checks to run
- Unit: `--group splats`, `--group editor` (Windows libraries + dependency-free, Linux).
- Stress: `--only splats` (untimed for the problem counts; timed in the hub's window for the rows).

## Risks and edge cases
- A fast path must refuse what the slow one refuses: counts are checked against the file's bytes before any
  allocation, as now; a parallel decode writes disjoint ranges only; NaN and unnormalised rotations go through
  `pointcloud_validate` as before.
- The 1M PLY is about 250 MB and the cloud about 236 MB: the stress row frees each file's bytes before the next.

## Documentation to update
- `changelog.d/567-splat-import-1m.md` (PERFORMANCE and ARCHITECTURE sections), this spec marked done.
