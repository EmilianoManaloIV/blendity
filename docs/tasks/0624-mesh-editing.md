# 0624: Mesh editing under complex workloads

- **Status:** in review
- **Authorization:** Owner's Pass 36 assignment, round 642, on 90136ffc.
- **Requirements:** R-02, R-08, Q-01, Q-06
- **Decisions:** ADR 0056 (measurements), ADR 0123 (fragments), ADR 0134 (verification)
- **Implementation:** Project implementer, independent test engineer, independent code reviewer.
- **PR:** [#505](https://github.com/EmilianoManaloIV/blendity/pull/505) against main. First published head `d2b47b445d835f84b783ce2833ea621efb0fd7a4`.

## Goal
Identify editing actions that become slow on dense geometry or in populated scenes and reduce costs where matched trials verify a gain.

## Scope
Inventory object and mesh editing, selection, topology, modifiers, transforms, snapping, curves and surfaces. Exercise production actions and record unexercised paths explicitly. Other Pass 36 leads own sculpt, UV, painting, rigging and viewport rendering.

## Likely files
Use `tools/bench_mesh_editing.py`, `docs/perf/0624-mesh-editing.md`, `docs/perf/0624-inventory.md` and `changelog.d/0642-mesh-editing.md`. The initial survey selected `src/scene/mesh_tools4.cpp` for Slice after a measured 869.7 ms dispatch on a 50,176-face sphere. The additional Push/Pull candidate changes `src/scene/mesh_tools.cpp` after a 157.4 ms dispatch. The independent regression cases live next to the previous Slice checks in `tests/test_modeling.cpp`.

## Acceptance criteria
1. Every inventoried action names its entry point and a verified workload or explicit gap. Deliver the inventory to 0630.
2. Exercise valid dense and many-object actions. Record fixture size, complete action, result checks, control and elapsed time under coordinated conditions.
3. Retain an optimization only after repeated paired baseline/candidate runs with equivalent output, topology, selection and undo. Exercise constrained and ordinary CPU configurations where relevant.
4. Obtain independent test and review evidence. Record unresolved latency and verification gaps.

## Tests and checks to run
Use exact joined binaries where applicable, scratch project and trash, modeling and editor groups and relevant geometry stress. Coordinate builds, timed runs and joined verification with the Hub. Run fragment and documentation checks.

## Risks and edge cases
Wrong selection modes can silently refuse a tool. Separate startup and fixture construction from operation latency. Verify topology renumbering, undo and F9 where applicable. Track high-cost cases and restricted CPU affinity explicitly.

## Documentation to update
Round fragment, performance report and Hub inventory/receipt.

## Outcome
The [performance report](../perf/0624-mesh-editing.md) records twelve paired production edits and the Windows/Linux verification totals. Median ordinary-affinity dispatch on 50,176 faces improved from 905.9 to 14.5 ms for Slice and from 154.9 to 124.2 ms for Push/Pull. Restricted-affinity results, the Push/Pull following-frame cost, baseline controls, exact output checks and the unchanged stress problems are reported there.

The [inventory](../perf/0624-inventory.md) covers 195 modeling paths. The Hub has the same inventory in `C:/blab/pass36-hub/0624-inventory.md` and the 615-key 0630 worksheet in `0624-exact-keys.tsv`. Tests, performance data, remaining UI gaps and the 100-piece Blockout coordinate comparison are distinguished there. The independent tester identified a duplicate legacy-wire case, which uses the original Slice path and has regression coverage. The independent reviewer approved the candidate after checking output order, finite/non-finite edge handling, all twelve paired results, platform logs, stress reports and inventory totals. The local candidate awaits Hub integration and the owner's merge.
