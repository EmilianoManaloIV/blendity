# 0629: Complex rendering and viewport costs

- **Status:** in review
- **Authorization:** Owner's Pass 36 complex-tools assignment, round 647. Hub coordinates shared measurement resources and publication.
- **Requirements:** R-05, R-10, Q-03, Q-04, C-04
- **Decisions:** ADR 0056, 0123, 0134
- **Implementation:** Project implementer, independent test engineer and reviewer
- **PR:** Pending Hub integration

## Goal
Measure what users pay to display and export complex scenes across the available rendering paths and identify evidence-backed opportunities to reduce that cost while preserving pixels.

## Scope
- Scene and Game views, raster, path tracing, lights and shadows, splats, camera filters, offline renders, sequence and file output, CPU and optional GPU paths.
- Document inaccessible hardware, unsupported combinations and unmeasured actions. Other editing tools belong to neighboring rounds.

## Likely files
- `tools/dev/bench_render_0629.py`, `src/render/camera_filter.cpp` (apply_bloom), `tests/test_render.cpp`, `docs/perf/0629-rendering.md` and `changelog.d/647-rendering.md`.

## Acceptance criteria
1. Inventory production actions with source references and an actual workload or explicit gap for each. Deliver the inventory to 0630 in Hub scratch.
2. Use repeatable complex scenes with valid active cameras or selections, a baseline control, user-visible frame/render cost and process memory. Distinguish CPU configurations and actual backend participation.
3. Check rendered pixels and completed output. Any proposed speedup requires repeated paired measurements and unchanged output.
4. Independent testing and review cover the candidate, evidence and limitations.

## Tests and checks to run
- Smoke the connected paths using verified existing packages in isolated projects. Run controlled trials in the Hub's timed window. For product edits run affected groups and editor on Windows and Linux plus relevant stress.
- Verify source/package identity, measurements and documentation format.

## Risks and edge cases
- Fallback can hide unavailable hardware. Asynchronous dispatch can finish before output exists. Process overhead, warm caches and viewport resolution can distort measurements.

## Documentation to update
- docs/perf/0629-rendering.md and changelog.d/647-rendering.md.

## Approved filter investigation
Compare a complex camera render with and without CPU bloom when no sample passes the bright threshold, including a positive-highlight control. Investigate skipping the pyramid only when its input is entirely black. Preserve every output channel and the existing composite when any bright sample survives. The clean four-run baseline selected this edit. Empty-threshold Bloom cost 44, 51, 71 and 71 ms above the intensity-zero control on the F12 render worker. Both controls produced identical PNG and RGBA bytes. Validate the edit with repeated paired baseline/candidate output. Reserve src/render/camera_filter.cpp for this investigation.

## Outcome

The [rendering report](../perf/0629-rendering.md) records complete editor, CPU, Vulkan/OptiX, splat, external MoonRay, filter, lighting and output workloads. Thirty ordinary/restricted CPU trials retained identical pixels across each workload. The isolated 27–31 s GPU shader compilation cost occurred in a package missing a normally shipped shader; in three matched pairs the shipped shader reduced median fresh-process F12 from 30.104 s to 0.763 s at identical output.

On the generated complex world with four logical CPUs, the selected Bloom change reduced median F12 worker time from 381.5 to 329.5 ms and editor completion from 390.65 to 335.60 ms. Every matched PNG and its decoded RGBA pixels agreed across all 36 renders, including the visible-glow control. Ordinary unfiltered sculpt frames use other rendering paths. On the adjacent 500k-face diagnostic, suppressing shadows changed rendered pixels, so round 647 retains the shadow pass.

Independent verification re-derived the CPU and Bloom tables, compared GPU device participation and decoded camera/filter/export outputs. The full Windows package passed the focused regression (26 checks), render (40,611) and editor (4,050). Dependency-free Linux passed the focused regression (26), render (40,505) and editor (4,016) with zero failures, using a fresh test directory and repository working directory. The candidate camera-filter stress recorded zero off-grid pixels out of 102,480 and zero problems over 160 unusual-input runs. The per-key deliverable for 0630 is in `C:/blab/pass36-hub/0629-keys.tsv`; `0629-inventory.md` indexes the workloads. Machine logs, candidate identities and the review receipt are indexed in `C:/blab/pass36-hub/0629-receipt.md`. Hub integration and owner merge follow the reviewed local commit.
