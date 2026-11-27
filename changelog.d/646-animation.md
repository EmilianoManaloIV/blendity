## 2026-11-26 (round 646): Measure articulated animation and rigging workloads

**Asked:** Measure animation and rigging tools on a humanoid and a branching creature, with a simpler control and a constrained CPU run.

**Changed (task 0628, ADR 0028)**
- Transfer Weights skips assembling the inpainting system when every target vertex already has a trusted match. On the measured 10,242-vertex humanoid and 15,874-vertex quadruped shells, complete transfer fell from 15.3 to 4.3 ms and from 23.8 to 7.2 ms at ordinary CPU affinity. Every transferred weight matched the baseline.
- A scratch editor workload for marker Auto-Rig, skin weights, IK, pose editing, keyed clips, sampled deformation, playback and humanoid retargeting.
- The runner retains stage and frame timings, native scenes and clips, and evaluated mesh samples for independent comparison.

**Checked:** See the [round report](docs/perf/0628-animation.md) for package identities, measured results and remaining coverage gaps.

<!-- tools/README.md -->
append:
- `python tools/bench_animation_rigs.py --exe <editor> --out <fresh scratch directory>` measures the editor's animation and rigging actions on generated control, humanoid and creature fixtures. See [task 0628's measurements](../docs/perf/0628-animation.md).

<!-- docs/PERFORMANCE.md -->
append:
## Round 646: Articulated animation and rigging

The [animation and rigging workload report](perf/0628-animation.md) records control, humanoid and quadruped measurements, deformation checks and coverage limits.
