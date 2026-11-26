# 0603: Reduce the cost of single-segment mesh bevel

- **Status:** in review
- **Authorization:** approved for dispatch under the owner's engine performance assignment, round 621 of parent task 0600.
- **Requirements:** R-02, Q-01, Q-02, Q-03, Q-04, Q-05
- **Decisions:** none needed
- **Implementation:** project implementer, followed by an independent test engineer and code reviewer.
- **PR:** [#501](https://github.com/EmilianoManaloIV/blendity/pull/501) against main. Initial published head `d6af4056d5b103d9a5aad648d8301217d394f21d`.

## Goal

Shorten a one-segment bevel in the modifier and Edit Mode while preserving the mesh and selection produced by the original operator. Use the all-edge bevel on the 50k-face sphere to measure the full modifier evaluation on an ordinary and a CPU-constrained configuration.

## Scope

- **In scope** Examine per-strip arc vector and map construction when the result has two endpoints. Remove redundant work inside `meshops::bevel_edges` while retaining traversal order, endpoint orientation and multisegment behavior.
- **Outside scope** Changes to bevel settings, profile sampling math, corner-patch construction, modifier or editor interfaces, and the existing parity or stress fixtures unless separately coordinated.

## Likely files

- `src/scene/mesh_tools.cpp`, `meshops::bevel_edges`.
- `tests/test_modeling.cpp`, the round 621 modeling tests owned by the test engineer.
- `stress/stress_main.cpp`, the existing `parity_sphere`, `parity_modifiers` and `curved` fixtures used for verification.
- `docs/tasks/0603-single-segment-bevel.md` and `changelog.d/621-single-segment-bevel.md`.

## Acceptance criteria

1. Demonstrate a repeatable full-operation wall-time improvement against a current release baseline on identical geometry. Report multiple trials, the elapsed-time distribution, peak RSS, and output vertex, face and corner counts for ordinary and CPU-constrained configurations.
2. Preserve deterministic positions, face and corner ordering and indices, UV and color layers, seams, materials, normals, and selection behavior supported by the baseline. Compare selected subsets, all edges, boundary and degenerate or empty cases, reversed orientations, and multiple segments.
3. Confirm Edit Mode Undo and F9 behavior against the baseline. The Windows and Linux modeling and editor groups pass, and the matching geometry stress problem and overlap counts are unchanged.
4. Obtain independent testing and review before integration and document the limits of the measurement.

## Tests and checks to run

- Run the matched before-and-after full-operation benchmark on the 50k sphere with identical release flags, inputs and affinity. Keep timing outside unit assertions under [ADR 0056](../decisions/0056-what-a-unit-test-is-for.md).
- Compare ordered output bytes in each Windows affinity and compare the records from the independent tests within each platform. Run `BLENDITY_TEST_FILTER="round 621 bevel"`, then the `modeling` and `editor` groups on Windows and Linux.
- Run `blendity_stress --only curved` on baseline and candidate. Compare the correctness counts and annotations with the timing columns removed.
- Run `tools/test_coverage_map.py --check`, `tools/dev/changelog.py check`, and the repository CI checks on the published head.

## Risks and edge cases

- Preserve the incident-face swap rule, including inconsistent windings. Boundary-only and nonmanifold selections, collapsed edges, and empty input must continue to match the baseline.
- Multisegment strips share their oriented arcs. Their curved profiles, traversal and expanded face loops must preserve the same ordered output.
- Check layer metadata and values through topology renumbering and the editor's color carry under [ADR 0152](../decisions/0152-colour-attributes.md). Compare Adjust Last and plain Undo separately because the two paths record selection differently at baseline.
- Verify MSVC release with Blender libraries and the Linux dependency-free build. Report the scope of the affinity and peak working set measurements.

## Documentation to update

- Record implementation, results, limits, and review evidence in this spec. Add the round entry to `changelog.d/621-single-segment-bevel.md` under the fragment format in `changelog.d/README.md`.

## Outcome

### Implementation and measurement

`meshops::bevel_edges` retains the existing edge walk and orientation rule. It checks whether the first incident face runs from the low vertex index to the high index and swaps the two faces once otherwise. With consistent winding, this places the low-to-high face first. A straight bevel uses the two resulting endpoint pairs and rebuilds each touched face from its existing trimmed loop. A multisegment bevel inserts the same profile samples and expands neighboring loops using the directional arc cache. Strip face construction, attributes and corner-patch repair still use the shared path.

The Windows comparison starts from `66feaeb3adee0f85c373f304a181c2ce0e23b6ce` and uses MSVC 14.51 release flags with Blender libraries and the oneTBB allocator. The scratch relink exchanges only the `mesh_tools` object and uses the same fixture and other release objects for both variants. The full-operation probe recreates `parity_sphere(false)` and the Bevel row of `parity_modifiers` in `stress/stress_main.cpp`. A fresh process constructs that scene before timing `GameObject::evaluated_mesh()`. The probe reads the process peak working set immediately afterward. Ordinary and one-logical-CPU affinity each use eight pairs with alternating order. Exact output comparisons and editor checks run separately.

Reproduction scripts and source fingerprints are in `C:/blab/0621`. `prepare.py` writes the before/after linker inputs and `relink-locked.bat` builds the packages. `run-window.py parity` compares serialized mesh data. The Hub runs `timed-window-621.sh` under the timed-window lock and captures the raw samples in `measurements.csv`.

### Timing and output parity

The Windows benchmark completed in the 2026-11-20 quiet window with zero taint lines (`C:/blab/pass32-spz01.log`). Every trial produced 200,192 vertices, 200,194 faces and 800,768 corners. The candidate was faster in all eight pairs at both affinities.

| CPU affinity | Before wall time, median (min–max) | After wall time, median (min–max) | Before peak RSS, median (min–max) | After peak RSS, median (min–max) |
|---|---:|---:|---:|---:|
| 32 logical CPUs available | 574.0 ms (538.6–761.0) | 439.6 ms (423.2–483.5) | 184.6 MiB (184.2–186.0) | 166.9 MiB (166.5–167.9) |
| 1 logical CPU | 537.9 ms (525.0–624.4) | 452.3 ms (418.7–508.9) | 181.9 MiB (181.9–181.9) | 163.5 MiB (163.5–163.6) |

Median wall time fell by 23.4% and 15.9%, respectively. Peak RSS includes initialization and the source fixture. The timing and memory figures apply to the full one-segment modifier evaluation on this sphere under the two tested CPU affinities.

The separate Windows probe compared 51,327,200 serialized bytes per affinity (SHA-256 `a015ad92a21354ff117ca283081dd6c05f694444c419c288e256ac2a4a2a9741`). This covers the sphere, single-edge and all-edge painted cube, reversed face winding, multisegment profiles, open boundaries, zero-area and empty inputs, and Edit Mode width and segment reruns through the console's Adjust Last setters. The scratch editor probe lost the selected object after Undo on both builds. The independent test exercises Undo through the named fixture object.

### Tests, stress and review

The matched Windows `curved` stress reports recorded 5,032 operations, 6 problems and 67 results with new overlaps on both builds. The projected drawing sweep recorded 236 draws, 82 refusals, and zero problems or new overlaps on both. The dependency-free Linux reports recorded 5,036 operations, 3 problems and 68 results with new overlaps on both builds. The projected drawing sweep recorded 236 draws, 82 refusals, zero problems and one result with new overlaps on both. Comparisons excluding timing columns matched 154 Windows and 149 Linux correctness rows and annotations. Reports are `C:/blab/0621/curved-{before,after}-curved.md` and `linux-{before,after}-curved.md` in the same folder.

The independent fixtures in `tests/test_modeling.cpp` passed 392 filtered checks on both sides of the Windows and Linux comparisons. The 32 receipts matched exactly within each platform. The editor cases type a new width into Adjust Last, rerun with a different segment count, and check restored mesh, colors and selection through Undo. The plain bevel Undo restored the selected edge and selection buffers. After Adjust Last, the comparison pins the selection state each baseline records. Evidence is `C:/blab/0621/matching-receipts.txt` and `linux-matching-receipts.txt` in the same folder. The Linux pair used the pass's joined dependency-free objects and the same revised modeling tests, with only the bevel implementation exchanged.

The full Windows `modeling` and `editor` groups passed 34,570 and 3,848 checks. The dependency-free Linux groups passed 32,247 and 3,819 checks, respectively. The `editor` runs used source roots with the manual and book fixtures present. Every run used scratch project, cache, preferences and trash paths. Logs are `C:/blab/0621/groups-after-modeling.log`, `editor-only-after-editor.log`, `linux-after-modeling.log` and `linux-stock-editor.log`.

The earlier joined Linux editor package contained a round 622 fixture whose `restored_group != nullptr` check failed on both before and after executables. The full 621 editor check used the stock `tests/test_editor.cpp` from `66feaeb3` (Git blob `50038219a221a400387459d9c1f0c5006ee8232a`) alongside the candidate bevel object and revised round 621 modeling tests. The linked executable SHA-256 is `3ee032f1bcff5a17df3562a94a1ffe8226f954b9115574ad1bc914f93109eec7`.

The independent reviewer approved integration with a wording correction about mixed winding, incorporated above. The coverage map and changelog fragment checks passed.

### Remaining gates

Hub publication, the published-head CI checks and owner final review follow local verification.
