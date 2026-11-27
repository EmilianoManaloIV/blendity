## 2026-11-27 (round 655): Correct overlap checks on translated mesh faces

**Asked:** Diagnose the seeded Blockout geometry failures and verify dense mesh editing and Undo.

**Changed (task 0637, ADR 0056)**

- The overlap checker measures clipped area relative to the triangles. This fixes false overlap reports on translated faces and preserves detection of coincident small faces.
- The Blockout kernel stress section can select the architectural sweep and report each failing seed. Four dense editor workloads check geometry, selection and Undo.

**Checked:** The seven previously flagged arrangements all fell below the existing overlap threshold under independent triangle clipping. All 900 Windows full-library cases and all 450 Linux field cases built with zero geometry problems. The Blockout, modeling and editor groups passed on all three builds. See the [round report](docs/perf/0637-mesh-blockout.md).
