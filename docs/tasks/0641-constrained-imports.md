# 0641: Constrained Linux splat and Unity imports

- **Status:** implemented and measured
- **Authorization:** Pass 37 owner assignment via Hub, round 659, base d7503d4e.
- **Requirements:** Q-06, Linux portability and asset import.
- **Decisions:** ADR 0147 (splats), ADR 0056 (measurements).
- **Implementation:** implementer for the importer probe and workload, lead for VM/host integration.
- **PR:** Hub publication.

## Goal
Measure actual disk PLY import, including read and serial validation cost, and exercise a Unity import under enforced low-end Linux resource budgets.

## Scope
- Reuse task 0622's native hosted ARM and local WSL profiles, scratch isolation and hard limits. Cover Gaussian PLY and a generated Unity fixture. Preserve importer semantics. Task 0646 owns separate splat rendering work.
- Use measurements to identify useful follow-up work. Retain a production optimization only with evidence. Avoid private assets, physical-device or storage-speed claims, local installs and duplicated build banks.

## Likely files
- src/scene/import_splat.{cpp,h}, importer diagnostic tooling, bounded hosted diagnostic procedure, task report and round 659 fragment.

## Acceptance criteria
1. At known source and executable identities, measure disk read separately from decode and pointcloud_validate, with repeated imports and a deterministic checked splat fixture. State timer coverage, cache conditions, actual input size and process peak.
2. Exercise a generated Unity scene or package in the CPU editor under the same resource budgets. Verify imported content and successful completion independently of process exit.
3. Confirm actual affinity and enforceable memory/swap ceilings. Identify host/profiles and distinguish infrastructure failures or missing measurements.
4. Verify diagnostics and importer compatibility independently. Route a reproducible bounded ARM procedure to Hub and record launch/results.

## Tests and checks to run
Run focused importer checks, compare expected fixture geometry/attributes and reject invalid output, verify Unity object identity, and run the unchanged product groups where affected using coordinated existing builds. Queue local timings through pass37-hub.

## Risks and edge cases
Read includes operating system cache effects. Interleaved block decoding complicates read attribution. A low fixture footprint alone cannot establish the largest supported scene. Different VM processors cannot stand in for physical devices.

## Documentation to update
This record, measured report under docs/perf and changelog.d/659-constrained-imports.md.

## Outcome
Four native constrained profiles completed the million-record PLY and 256-cube Unity imports. Independent audit covered all 20 PLY samples and 12 fresh editor opens/saves. The [report](../perf/0641-constrained-imports.md) records per-stage timings, cache conditions, hard limits, per-process memory and separate output digests for each architecture. Axis conversion was the largest CPU stage on both architectures. The x86 Windows-mounted vertex read dominated local elapsed time. A same-binary, same-fixture comparison measured warmed vertex reads of 15.77 ms on WSL ext4 and 874.18 ms on DrvFS. The Hub authorized a separate axis-conversion candidate under task 0646.
