# 0637: Seeded Blockout geometry and dense mesh actions

- **Status:** locally verified
- **Authorization:** Owner Pass 37 assignment, round 655, base d7503d4e.
- **Requirements:** B-01, B-02, Q-01, R-02
- **Decisions:** ADR 0128, 0129, 0056, 0134
- **Implementation:** Project implementer, independent test engineer and reviewer.
- **PR:** Hub integration pending.

## Goal
Identify the exact geometric defects reported for Railing, Roof, Stairs L and Spiral Stairs, correct confirmed defects and verify high-density editing actions with geometry and Undo oracles.

## Scope
Replay both published path configurations and stable seeds. Distinguish invalid output from reference sampling error. Retain only evidence-supported changes. Round 663 is reserved for a separable additional geometry change if needed.

## Likely files
Blockout kernel and architecture sweep in src/scene/blockout*.cpp and stress/stress_blockout.inc, tests/test_blockout.cpp or focused fixture include; round-specific dense workload, evidence and fragment. Record exact ownership in the receipt.

## Acceptance criteria
1. Report exact failing seed, path, geometry check and observed output for all four families.
2. Verify each fix against independent geometry expectations and a production editor action with Undo. Preserve closed geometry, reproducibility, attributes and distinct legitimate refusals.
3. Exercise dense operations with precise topology, selection and Undo postconditions. No latency claims.
4. Compare candidate and baseline stress, affected groups and editor on Windows/Linux using isolated scratch and independently review the candidate.

## Tests and checks to run
Production architecture stress controls, focused oracles, relevant blockout/modeling and editor groups, fragment and ownership checks. Hub schedules runtime and joined checks.

## Risks and edge cases
Tiny boundary features, ambiguous near-surface winding, field discretization, thin railing members, mirrored geometry, false refusals and unchanged-output operators.

## Documentation to update
This spec, docs/perf/0637-mesh-blockout.md and changelog.d/0655-mesh-blockout.md.

## Outcome
The seven historical flags all fell below the existing overlap threshold. Local projection and double-precision area accumulation corrected the checker. The candidate passed all 900 Windows and 450 Linux arrangements, plus the Blockout, modeling and editor groups on Windows with and without Blender libraries and on dependency-free Linux. Windows and Linux also matched across 20 dense editor exports. The [round report](../perf/0637-mesh-blockout.md) records the face pairs, independent calculations, Undo control, group counts and reproduction commands. No separate geometry change was needed for round 663. The independent reviewer approved the final candidate after checking the numerical oracle, final test logs and all 20 exported mesh digests. Hub publication and hosted CI are pending.
