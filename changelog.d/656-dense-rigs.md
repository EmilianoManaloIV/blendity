## 2026-11-27 (round 656): Animation preview and skin transfer on dense rigs

**Asked:** Measure partially trusted weight transfer and exercise IK, timeline scrubbing, playback and Live Link on humanoid and quadruped rigs in a dense scene.

**Changed (task 0638, ADR 0028 and ADR 0127)**

- Partially trusted skin transfers now build independent stiffness rows and solve weight groups in parallel. Default-worker median transfers fell from 37.3 to 29.2 ms on the humanoid and from 65.25 to 49.3 ms on the quadruped.
- Animation Preview uses the editor clock when starting, ticking and resuming playback. This lets headless checks drive the toolbar, console and Animation ruler at exact timestamps.
- Added reproducible dense rig and local Live Link workloads that verify edits and evaluated motion.

**Checked:** Saved native poses and exported frame-60 meshes matched across isolated and dense scenes on both rigs. Measurements and verification are in `docs/perf/0638-dense-rigs.md`.
