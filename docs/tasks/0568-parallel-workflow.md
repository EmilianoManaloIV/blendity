# 0568 — Reuse verified work across parallel rounds

- **Status** — Owner approved. Implementation complete, in review.
- **Round** — 586
- **Requirements** — Project workflow in AGENTS.md and handoff/METHOD.md
- **Decisions** — ADR 0123 and ADR 0134. Documentation-only.
- **PR** — Pending

## Goal

Make the parallel workflow used in Pass 29 discoverable to future Blendity workers. Reduce repeated compiles and measurements while preserving the existing review, verification and publication rules.

## Changes

- Add a brief pointer in the parallel-work section of AGENTS.md. Put the procedure in docs/handoff/METHOD.md. Add changelog.d/586-parallel-workflow.md. Keep machine-specific resource limits and helper details in MACHINE.md.
- The coordinator records owners, candidate revisions, checks, artifact locations, pending reservations and the next publishable round in one scratch ledger. Workers prepare fixtures, commands and expected results before reserving shared hardware.
- Run focused checks during development. When the source and regression tests settle, take the final candidate through the required build and test configurations. A review fix invalidates affected results. Review remains independent, and the coordinator verifies the actual stack top before publishing.
- Reuse an existing executable only with documented equivalence of all build inputs, including generated code, compile/link options, toolchain, dependencies and deployed runtime files. A change confined to docs or tooling still gets its own relevant checks. Changed compiled inputs require a matching build.
- Reuse a measurement only for the same binary and runtime configuration, workload, scale, inputs, timing conditions and cache state. Record hashes or equivalent evidence, original report, exit status and section coverage. A quick run, different backend, different process context or tainted timing window requires its own measurement. Required checks on the actual combined stack still run as specified.
- One coordinator launches timed batches. Group ready compatible workloads in a quiet window, reconcile each child result, resume at the first missing or failed workload, and release hardware for other rounds promptly. Publish each ready reviewed round on the current single top while unrelated work continues.

## Acceptance and checks

1. A new worker starting at AGENTS.md can locate the procedure and determine who schedules local resources, which evidence may be reused, and when a fresh verification is necessary.
2. Walk three actual Pass 29 examples against the procedure: source-identical command inventory, a colour regression test added after a build, and identical full-size stress workloads versus quick runs. The outcomes should be reuse, rebuild the affected target, and reuse only the matching measurements.
3. Review against the original rules in AGENTS.md, METHOD.md and MACHINE.md. Validate the fragment, document links and diff. Preserve owner approval of specs, owner merges, scratch isolation, local resource limits, and stack and CI checks.

## Acceptance walkthrough

1. Task 0565 generated the release's exact command table. The compiled inputs for the dependency-free release
   artifacts still matched. The worker reused the Windows and Linux artifacts, ran the editor groups, and ran the
   changed generator and its regression tests separately.
2. A new corner-colour regression in task 0566 changes the test executable's inputs. Rebuild the test target in
   each required configuration and run the relevant groups. The prior build cannot exercise the new assertion.
3. For Pass 29's geometry stress comparisons, a recorded full-size section can supply a second round's matching
   measurement when the ledger establishes identical binary, configuration, workload, process context, cache
   state and quiet timing conditions. A quick section, or the corresponding section in a tainted batch, needs
   a fresh full-size measurement. Verify both sides of a required before/after pair.
