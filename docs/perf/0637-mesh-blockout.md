# Round 655 mesh editing and Blockout verification

## Baseline

Pass 36 recorded seven failing arrangements across four architectural presets. All 400 arrangements built in the four-preset quick sweep. The reported failure in each family was overlapping flat faces. The sweep counts small overlaps, zero-area faces and unresolved cells separately.

| Preset | Exact failures / 50 | Field failures / 50 | Failing seeds in the replay |
|---|---:|---:|---|
| Railing | 2 | 1 | Exact 16 and 35, field 6 |
| Roof | 0 | 2 | Field 32 and 45 |
| Stairs L | 0 | 1 | Field 26 |
| Spiral Stairs | 0 | 1 | Field 39 |

The [Pass 36 survey](0630-survey.md#disposition-of-the-seven-earlier-failed-checks) gives the original findings. The field grid uses a cell width of 0.08. The exact path uses Manifold in the Windows full-library build. The assigned revision is `d7503d4e`. Its production sources, tests, stress sources, external sources and platform build scripts matched the previously compiled `a3d6cc8c` revision. Large verification artifacts are in the Pass 37 task 0637 scratch directory.

## Why the seven arrangements were flagged

The face checker projected triangles in world coordinates and accumulated clipped polygon area in single precision. Subtracting two nearly equal products lost precision for thin polygons away from the origin. The corrected checker projects relative to the first triangle and accumulates the clipped polygon area in double precision about a polygon vertex. It keeps the original plane, angle and area tolerances.

The independent check restored the captured coordinates to their native binary32 values, then clipped the render triangles in double precision. It used the original triangle order for the one-sided plane-distance test and separated the sweep’s ridge warnings. The table gives zero-based face IDs and the largest eligible projected intersection for each reported non-ridge pair. Areas are in square meters.

| Preset and path | Seed | Face pairs | Independent area | Existing area threshold |
|---|---:|---|---:|---:|
| Railing, exact | 16 | 56/69 | 0 | 0.0000481197 |
| Railing, exact | 35 | 398/399 | 0 | 0.00000952522 |
| Railing, field | 6 | 48/67 | 0 | 0.0000418538 |
| Roof, field | 32 | 6/197 | 0.00000288383 | 0.0000399482 |
| Roof, field | 45 | 244/246, 543/544, 547/548 | 0 for each | 0.0000261482 |
| Stairs L, field | 26 | 1291/1292 | 0 | 0.0000367611 |
| Spiral Stairs, field | 39 | 127/128 | 0 | 0.00000651723 |

On Roof seed 32 the two triangles shared two points and the maximum normal separation over their projected intersection was about 0.000000052 m. The regression suite retains actual contact triangles from Railing seed 35 and an analytic coincident-triangle control near the origin and 60 m away. This checks that a true overlap remains detectable after translation.

The Windows candidate built all 900 arrangements across nine presets and both paths, with zero refusals and zero geometry problems. Zero-area and unresolved counts matched their original values in every row. Two other field rows reported fewer arrangements with small overlaps, Column (two to one) and Stairs U (one to zero). The Linux dependency-free build exercised all 450 field arrangements, also with zero refusals and zero geometry problems. The seven failures were in the checker, so this round changes no generated Blockout surfaces.

The baseline face dumps and independent calculations are in `blockout-diagnostic/windows/full-baseline.csv` and `blockout-diagnostic/windows/clip-analysis-binary32.csv`. The stock replay log is `blockout-pass36/replay.log`. Candidate sweep reports are in `full-sweep-1/report/latest.md` and `linux-sweep-1/report/latest.md`. The combined functional queue recorded wrapper and child exit zero and zero taint.

## Dense mesh editing

The editor created a Plane and applied four simple subdivisions. The resulting mesh contained one object, 25,921 vertices and 25,600 quads. Each workload started a fresh headless editor with isolated project, trash, preferences and cache directories.

| Action | Geometry and selection checks |
|---|---|
| Select Ring | Subdividing the selected edges inserted precisely the 161 predicted midpoints. Each midpoint appeared in the correct oriented face loops. Undo restored the source mesh and repeating the probe reproduced the same result. |
| Select Loop | The same probe located all 160 predicted chain edges. The remaining edges and faces retained their connectivity and winding. |
| Tris to Quads | Triangulating 256 disjoint quads produced 512 selected triangles. Reassembly recovered the original oriented quads. Flipping selected faces confirmed the merged selection and the selection restored by Undo. |
| Dimensions | Local X extent increased from 10 to 20 about the original center. All 25,921 vertices matched the expected positions and face indices stayed fixed. A subsequent displacement along the normal checked the full selection before and after Undo. |

The first Windows driver rejected the ring fixture and the preserved Dimensions coordinates on strict decimal comparisons. One original ring edge had Z endpoints of 0.6875001 and 0.6875002. Dimensions reached X bounds of −10 and 10, with maximum errors of 4 × 10⁻⁷ in predicted X, zero in Y and 10⁻⁸ in Z. The saved OBJ exports passed a second check that validates the regular grid and uses a 0.00001-unit coordinate tolerance. The selection and connectivity comparisons still require exact vertex and face identities. The original and corrected verdicts are retained in `dense-1/report.json` and `dense-1/report-rechecked.json` under the task scratch directory.

The corrected driver then passed all four actions on Linux. Its 20 exported meshes matched the corrected Windows results by parsed OBJ digest, including face winding, supported attributes and selection probes. The Linux report is in `dense-linux-1/report.json`.

## Local test groups

The modeling and editor groups passed on all three builds. Windows with Blender’s libraries ran 34,938 modeling checks and 4,064 editor checks. Dependency-free Windows ran 32,615 and 4,045. Dependency-free Linux ran 32,615 and 4,027. The logs are under the respective `full-groups-2`, `lite-groups-2` and `linux-groups-2` directories.

An initial Undo fixture marked its direct setup edits without advancing a frame. The next edit joined those changes into the same history step. The corrected fixture commits the setup and verifies the new Undo depth before changing the rail height. It then checks restoration of the exact source height, mesh digest and per-face origins. In a paired Linux run, skipping the setup frame restored the default 1 m railing with 124 faces. Committing it restored the 0.993425906 m railing with all 1,782 faces and the original digest 16960580882909986003. The two logs are in `undo-diagnostic/uncommitted/checks.log` and `undo-diagnostic/committed/checks.log`.

The complete Blockout group passed on the final test revision. Windows with Blender libraries passed 20,864 checks, dependency-free Windows passed 16,057 and Linux passed 16,060. Each group exercised the extracted Railing contact, the translated true-overlap control and the live height edit with Undo. Logs are in `full-groups-3/blockout.log`, `lite-groups-3/blockout.log` and `linux-groups-3/blockout.log`.

## Reproducing the checks

Run the release stress program with `BLENDITY_BLOCKOUT_PRESET_SWEEP=1` and arguments `--only blockout --quick --out report` from a scratch project. The environment flag selects the architectural rows within the kernel section. The substring filter also selects other Blockout stress sections. Set `BLENDITY_BLOCKOUT_PRESET` to 4 for Railing, 5 for Roof, 6 for Stairs L or 8 for Spiral Stairs. Add `BLENDITY_BLOCKOUT_PRESET_SEED` to inspect one seed on all available paths. For the dense checks, run `python tools/bench_mesh_0637.py --exe <release-editor> --out <new-scratch-directory>`. The directory contains the report, per-action command logs and original OBJ exports.
