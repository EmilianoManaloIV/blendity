# 0577: Move one cube in a large scene (round 595)

- **Status:** done, findings only
- **Base:** c86f3d6b
- **Decisions:** ADR 0056 and the existing GPU view keeps

## Goal and baseline

Reduce the cost of the existing scene_views Shaded drag on cubes50k. The hub measured 218.6–241.9 ms frame medians and 150,000 instance rows written per frame. The archived baseline also records three row-table generations per frame, including with keeps enabled. The Vulkan rasterizer copies every source instance on set_instances and repacks every row after each generation change.

## Scope and ownership

Use src/render/gpu_raster.cpp, tests/test_gpu.cpp, this spec and changelog.d/595-moving-object.md. Reuse the existing scene_views stress driver. Leave editor gathering and task 0571's vertex packing separate.

## Acceptance

1. After warming the shared instance buffer, moving a single cube in a stable array writes only its changed row. Repeated identical arrays write zero rows. Disabling the card buffer keep still exercises full writes.
2. With updates across views, visibility changes, replacement, shrink, growth, multiple set_instances calls before drawing and material-only edits, colour, picking IDs and transforms match a full-write reference.
3. In a hub-allocated matched quiet window run the existing workload on baseline and candidate with identical builds, settings, power sampling and scenes. Target at least 90% fewer written rows and a 5% lower median on the 50k keeps-on drag. Record p95, max and the orbit/10k controls. Release a missed candidate with its measurements.

## Checks

Independent gpu acceptance tests followed by independent review. Run gpu/editor under hub allocation, Windows with and without libraries and Linux as required, plus coverage, command and fragment audits. Use scratch project and trash. Report timing, rows, residual work and limits in the round fragment.

## Decision and evidence

The sparse-upload candidate missed the frame-time target. Its source and candidate-specific tests were withdrawn. The paired Windows Vulkan builds used MSVC 14.51.36231, matching runtime DLLs and shaders, with the reference stress executable at SHA-256 `553b7a35790c021bafcc327e086de6af2633e74ee6d30232182c041570e96d6b` and the candidate at `235dc79ec8f2f25a8f1445c83309df46342befe10a9fa0653ec73fe9cee1a604`.

Window08 verified a continuous quiet minute before the pair and recorded no taint. Both arms ran the full `--only scene_views` workload in fresh scratch directories. Each Shaded keeps-on row measured 60 frames. Times below are milliseconds.

| Workload | Reference median / p95 / max | Candidate median / p95 / max | Rows written per frame, reference / candidate |
|---|---:|---:|---:|
| Move one cube among 50,000 | 221.7 / 246.9 / 272.8 | 214.5 / 240.4 / 249.4 | 150,000 / 1 |
| Move one cube among 10,000 | 41.4 / 44.5 / 51.3 | 40.4 / 43.9 / 49.3 | 30,000 / 1 |
| Orbit 50,000 cubes | 14.0 / 59.8 / 63.3 | 12.3 / 48.7 / 53.5 | 0 / 0 |
| Orbit 10,000 cubes | 3.5 / 10.4 / 13.9 | 3.1 / 10.4 / 12.3 | 0 / 0 |

The 50,000-cube drag median fell 7.2 ms (3.25%). The 5% threshold required 210.615 ms or less. Both orbit controls also ran faster with zero rows written in either arm. With keeps disabled the 50,000-cube drag still wrote 150,000 rows per frame in both builds. The candidate reduced mean card-write time from 8.40 to 0.66 ms. Mean collection time was 86.68 ms on the reference and 87.86 ms on the candidate. The profiler's component times can overlap.

The earlier window07 pair recorded a 246.6 to 226.4 ms drag median. Window08 superseded it with a preflight that checked full Linux executable paths. Only window08 determines acceptance. Independent Windows GPU checks on the parked candidate passed 81 checks for updating and picking across two views and 152 existing round 506 checks. The deliverable is documentation only.
