## 2026-11-27 (round 660): Windows release toolset verification

**Asked:** Verify MSVC 14.52 against the original Windows release workloads and investigate the hosted macOS failures.

**Changed (task 0642, ADRs 0133 and 0134)**
- Recorded the original and current Windows failures with compiler versions and matching debug symbols. The two single-process unit runs stopped in different operations. The 14.51 release pin remains in effect.
- Identified four macOS runner acquisition failures and one Preferences assertion in the reviewed jobs. Later hosted runs completed both the unit suite and quick stress.

**Checked:** The pinned, dependency-free Windows build passed 132,595 checks in 3,951 tests and completed quick stress on the comparison revision. Its stock 14.52 counterpart failed both workloads. A separate three-file 14.52 trial completed quick stress but its unit process still terminated after 416 tests. See [task 0642](docs/tasks/0642-msvc-release-triage.md) for the exact source revisions, jobs and symbol evidence.
