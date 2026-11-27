## 2026-11-26 (round 642): Mesh and object editing under complex workloads

**Asked:** Find slow editing actions in dense meshes and populated scenes, and reproduce gains against the same work.

**Changed (task 0624)**
- Slice batches cuts on ordinary meshes. UV seams, face order, colors, wires and Undo retain the tested editing behavior.
- Push/Pull skips edge hash-table entries outside the projected bounds of the selected region.

**Added**
- A headless production-command workload for dense face selection, Poke Faces, Adjust Last Operation, Slice, Push/Pull, Duplicate, Join and Undo.
- A matched trial driver that checks geometry, topology and selection, with separate command and follow-up frame timings.

**Checked:** On a 50,176-face sphere, median Slice dispatch fell from 905.9 to 14.5 ms and Push/Pull from 154.9 to 124.2 ms on 32 logical CPUs. All twelve ordinary and restricted-affinity pairs preserved the exported geometry and selections. Push/Pull remained above 100 ms. Windows and Linux each passed 36,653 modeling/editor assertions. Geometry stress counts matched baseline, including 26,608 Push/Pull operations with zero problems and the three existing curved-mesh problems. See the [performance report](docs/perf/0624-mesh-editing.md).

<!-- tools/README.md -->
append:
## Complex mesh editing
Use `python tools/bench_mesh_editing.py --baseline PATH --out SCRATCH` to run the dense-face and
many-object editing workloads. Add `--candidate PATH` for paired trials. See
[the fixtures and verification](../docs/perf/0624-mesh-editing.md).

<!-- docs/PERFORMANCE.md -->
append:
## Round 642: mesh and object editing
The [complex editing benchmark](perf/0624-mesh-editing.md) separates command latency from the
follow-up frame and verifies the operation's geometry, selection and undo.
