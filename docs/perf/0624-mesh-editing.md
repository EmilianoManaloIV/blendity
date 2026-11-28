# Complex mesh and object editing

[Task 0624](../tasks/0624-mesh-editing.md) uses the editor’s production commands in a fresh
headless process for each sample. The executable, runtime libraries and fixture inputs stay fixed
within a profile. Candidate comparisons alternate baseline-first and candidate-first order.

## Reproduce

From the repository root, with a release-package executable and its runtime libraries available:

```powershell
py tools/bench_mesh_editing.py --baseline $baseline --out "$scratch/ordinary" --repeats 3
py tools/bench_mesh_editing.py --baseline $baseline --out "$scratch/large" --workload join --cubes 4000
py tools/bench_mesh_editing.py --baseline $baseline --out "$scratch/four-cpu" --cpus 4
py tools/bench_mesh_editing.py --baseline $baseline --candidate $candidate --workload slice --workload push-pull --repeats 3 --out "$scratch/paired"
```

Add `--candidate $candidate` to the same command for paired trials. Set the scratch path to a
new directory for each invocation. Each editor gets a separate project, trash, preferences, cache
and temporary directory. The run retains the log, exports and parsed samples in `report.json`.
Use the Hub's exclusive timing window on the development machine. Windows and Linux support
`--cpus`; the report records the inherited mask. The restricted profile uses four logical CPUs
on the same host.

## Workloads and checks

| Workload | Fixture and action | Output check |
| --- | --- | --- |
| Dense Edit Mode | One 256 × 196 UV sphere. Enter face mode and select all 50,176 faces. Run Poke Faces, change its Offset to 0.01 through the Adjust Last Operation command, then undo. | Poke produces 200,192 triangles. F9 moves the generated centers with the same topology, undo restores the original geometry, and a Flip Normals probe confirms the restored face selection. |
| Slice | One face (1200) on the 50,176-face sphere places the cut plane. Slice the full object and undo. | The cut adds 256 faces and 256 vertices. The closed mesh has consistent winding. Flip Normals probes the cleared output selection and restores exactly the original face after Undo. |
| Push/Pull | Pull two adjacent sphere faces (1200 and 1201), undo, redo, and probe selection on both sides of the operation. | The result has six additional faces and vertices. Compare the complete ordered OBJ output and selected faces, including the original two after Undo. |
| Duplicate selected objects | 1,000 cube siblings sharing one mesh, at distinct grid positions, under a single parent. Select all cubes, duplicate, then undo and redo. | The selection contains only copies. All exported world-space polygons occur twice, undo restores the original scene output, and redo restores the post-edit selection. |
| Join selected objects | Identical grid with 1,000 or 4,000 cubes. Select all cubes, join, then undo and redo. | The selected target contains the full original set of world-space polygons. Undo restores the scene and the target's original mesh, and redo restores the joined selection. |

The no-op control puts consecutive marks around the same command boundary, then advances one
frame. For each action the first interval covers synchronous dispatch. The next interval includes
one editor frame, when pending scene changes settle and the undo step is committed, plus command bookkeeping and logging. Fixture
construction, process startup, export and verification are outside both intervals. The frame
figures include CPU viewport and UI work at 480 × 320. Command intervals use the existing
0.1 ms `perf mark` resolution. Selection from viewport input, interactive modal dragging,
GPU presentation and desktop input latency require separate measurements.

## Results

The initial full-library pilot measured Join dispatch at 1.4 ms for 1,000 cubes and 6.3 ms for 4,000. The following frame took 3.6 and 7.4 ms respectively. Duplicate at 1,000 cubes took 1.6 ms, followed by a 9.4 ms frame. All four pilot samples passed their output checks. A dense all-face Poke took 13.6 ms, followed by a 52.4 ms frame. Adjusting the offset took 11.7 ms plus a 47.9 ms frame. The first pilot process selected the CPU viewport after initialization. Later trials select it at process start.

On the dependency-free Windows build, 22 of 23 individual operations on the same sphere both changed its exported positions or faces and restored them on Undo. The largest measured dispatches were Slice (869.7 ms), Push/Pull (157.4 ms) and Grid (57.7 ms). All remaining figures here describe individual baseline samples. Flatten Loops on the chosen vertices did not pass the output-change check.

Slice previously rebuilt all mesh faces for every crossing edge and face. The candidate uses the existing batch edge-split operation and emits ordinary face halves in one rebuild. It retains the previous paths for colored geometry, wires, partial attributes and ambiguous polygon intersections. For Push/Pull, a two-face selection on a dense object has a narrow projected sweep. The candidate inserts an edge in the deduplication set after the existing bounds test, retaining first-encounter behavior for non-finite projections.

The modifier survey used a twice-subdivided plane. Nine of thirteen default components produced a changed mesh, with matching evaluated, applied and post-Undo exports. A separate Path started with 133 control points. Move changed point 20 to (9, 8, 7), Extrude raised the count to 134, Cut to 135, Merge returned it to 134, Close set its serialized Closed flag, and Delete reduced it to 133. These actions took 1.1–1.3 ms each including the following frame.

See [the action inventory](0624-inventory.md) for fixtures and gaps. Individual timings above come from the Pass 36 hub's `0624-atlas/rows.json`, `0624-secondary/rows.json`, serialized `0624-secondary/curve/c*.scene`, and `0624-pilot/*/report.json`. The pilot and dense survey ran under `pass36-window-1795760418682634700.log`, the modifier and curve trials under `pass36-window-1795761210155829200.log`. Both windows completed with zero taint.

## Paired results

Each cell gives the baseline and candidate medians in milliseconds from three paired runs at the same affinity. The last column takes the median of the sums for individual runs. Both executables use the Windows dependency-free release configuration. The baseline app hash begins `31b505af` and the candidate `5bc93f70`. The no-op dispatch control measured 0.0 ms at the timer's 0.1 ms resolution on both builds.

| Action | Logical CPUs | Dispatch, baseline → candidate | Following interval, baseline → candidate | Dispatch plus following interval |
| --- | ---: | ---: | ---: | ---: |
| Slice | 32 | 905.9 → 14.5 | 22.3 → 22.6 | 928.2 → 37.1 |
| Slice | 4 | 954.4 → 14.0 | 33.2 → 29.6 | 995.4 → 43.8 |
| Push/Pull | 32 | 154.9 → 124.2 | 22.0 → 21.0 | 176.9 → 145.6 |
| Push/Pull | 4 | 150.1 → 130.9 | 40.0 → 46.1 | 190.1 → 177.7 |

All twelve pairs reduced both dispatch and combined latency. Push/Pull still exceeds the 100 ms one-off target on this fixture. Its following interval lengthened in all three restricted-affinity trials. The paired reports retain every sample and the hashes of exported object, material, UV, normal and face records. Slice added 256 vertices and faces. Push/Pull added six of each. Both kept closed, consistently wound topology and restored the original selected faces and geometry on Undo. See the Hub's `0624-combined-pairs/{0,4}/report.json` and zero-taint `pass36-window-1795766225205669000.log`.

On a 1,600-quad grid, twelve of thirteen topology cases produced changed geometry and restored the reference on Undo. Edge Split on one grid edge left the exported mesh unchanged. All eight selection trials passed a Poke-based probe, including five selected faces after Grow, 1,599 after Invert, and 1,600 after Linked. Direct Move, Rotate and Scale gizmo drags in both modes changed the 50,176-face sphere and restored its vertex and face digest on Undo at 1280 × 800. The first 320 × 240 probes missed their handles and provide no successful timings.

All four initial modifier gaps produced changed geometry on the retry with a subdivided cube and full libraries, including a configured weld distance. Mark/Clear Seam, Mark/Clear Sharp and smooth/flat shading completed through the registered tool on a plane among 600 cube siblings, with the resulting edge or face flags checked in saved scenes. Baking and unbaking the 100-piece architectural interior preserved its 20,117 ordered polygon index lists. The strict coordinate check found changes of at most 0.000001 on 486 glass, frame and door-leaf vertices. The Interior object's vertices matched.

Scripts, logs and serialized checks for these cases are in the Pass 36 Hub's `0624-edge`, `0624-selection`, `0624-drags2`, `0624-mod2`, `0624-marks` and `0624-blockout` directories. The inventory distinguishes action-specific results from previously executed source references and unmeasured UI paths.

## Candidate verification

Windows dependency-free MSVC release and native Linux GCC release each passed 32,615 modeling and 4,038 editor assertions. The three new named Slice tests provided 46 assertions within modeling. They compare ordered cuts with reference geometry, check material/UV/edge data and exercise selection through Undo and Redo. Windows test binary SHA-256 begins `fc89be3b` and Linux `af736c64`. Group logs are under the Hub's `0624-verify/combined/{win,linux}/`.

Baseline and candidate Linux geometry stress counts matched. Push/Pull completed 26,608 operations with zero problems and 394 cases of newly overlapping faces. The modifier sweep completed 11,540 operations on odd meshes and built 450 shapes with zero problems. The curved sweep reported the same three problems and 68 newly overlapping results in 5,036 operations on both builds. Its 236 projected draws gave 82 refusals, zero problems and one new overlap on each build. Compare `0624-stress` and `0624-combined-stress` for per-case reports.

The tested candidate covers these two source files and the subsystem tests. The Pass 36 Hub owns full-library joined verification, CI and publication. The workload drivers used isolated project, trash and preferences directories.
