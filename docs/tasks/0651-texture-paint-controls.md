# 0651: Textures and Paint UI end states

- **Status:** in review
- **Authorization:** Hub assignment for round 669 on frozen d79d27a7, local next-pass work.
- **Requirements:** R-04 and Q-06.
- **Decisions:** ADR 0056, 0123 and 0134.
- **Implementation:** Existing implementer and independent test and review roles.
- **PR:** Local handoff.

## Goal
Verify what a user can change through remaining Textures and Paint controls and measure representative complex texture work in the clean combined editor.

## Scope
Select unexercised exact field, panel or menu keys from 0640 and its family map. Batch compatible real pointer interactions with separate visible or persisted assertions per key. Limit profile runs to scratch using the clean combined d79d27a7 package. Preserve shared registers for their owning worker.

## Likely files
Round-owned reusable UI driver under tools/dev, task-local keyed receipts and report under docs/perf, round fragment and this spec.

## Acceptance criteria
1. Identify exact register keys and existing evidence before claiming new coverage. Preserve individual input routes, observed state, references, and the identity of the exercised executable.
2. Use real UI input and an observable effect for every claimed key. Check independent changes, cancellation, Undo, no-op, alpha or seam behavior where applicable. Report unsupported or unexercised cases separately.
3. Profile representative complex texture actions using existing measurement boundaries in an exclusive window before considering a product change. Compare measurements only where fixtures and settings match.
4. Independently verify the receipts against raw outputs, review the frozen local tree, and hand the Hub a keyed overlay and reviewed local commit.

## Tests and checks to run
Run the reusable batch against the frozen stock editor with isolated project, trash and preferences. Validate output assertions and distinct exact keys. Run fragment and diff checks. A retained product change additionally requires independent cross-platform tests.

## Risks and edge cases
Dynamic labels, translated/scaled positions, and duplicate controls can falsely credit keys. Save/reopen and exact state or pixel comparisons must distinguish real effects from frame or status-text changes. Existing engine tests do not establish a pointer route.

## Documentation to update
Round 669 fragment, report, exact-key overlay and independent evidence links.

## Outcome
A [separate 57-key overlay](../perf/0651-texture-paint-controls.tsv) records newly verified panel, field and menu controls on the frozen stock editor. The [report](../perf/0651-texture-paint-controls.md) documents the two-stage first-stroke history, 2D edge behavior, 24-process profile, remaining gaps and replay commands. [Independent verification](C:/blab/pass38-hub/0651-test.md) and [review](C:/blab/pass38-hub/0651-review.md) accompany the local handoff. No product edit was needed.
