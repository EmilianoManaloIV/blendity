# 0630: Whole-program complex tool sweep

- **Status:** in review
- **Authorization:** Owner's Pass 36 assignment through Hub, round 648, base 90136ffc.
- **Requirements:** Q-03, Q-04, Q-06, B-06
- **Decisions:** ADR 0056, 0123, 0134
- **Implementation:** project implementer (inventory), lead (cross-program evidence and reconciliation)
- **PR:** [#505](https://github.com/EmilianoManaloIV/blendity/pull/505) against main. First published head `d2b47b445d835f84b783ce2833ea621efb0fd7a4`.

## Goal
Give the owner a searchable account of each exposed tool and action, its complex workload and observed outcome, or the specific gap that still prevents a conclusion.

## Scope
- In scope are registry and toolbar entries, modes, commands, subtools, brushes, modifiers, panels, keymaps and other named product actions, including work outside the six specialist lanes.
- Product changes, deployment and remote publication belong outside this round. Route measured fix candidates to their owning lane or Hub.

## Likely files
| Owner | Paths |
|---|---|
| Implementer | tools/dev/tool_inventory_0630.py, docs/perf/0630-inventory.tsv |
| Lead | docs/tasks/0630-all-tool-sweep.md, docs/perf/0630-survey.md, changelog.d/0648-all-tool-sweep.md, scratch C:/blab/pass36-hub/0630* |
| Six other lanes | Their own product files, cases and evidence per dependencies.md |

## Acceptance criteria
1. Reconcile inventory keys and aliases against source with reproducible extraction, including user operations beyond lane names.
2. Map every key to an observed action, workload and result or an explicit unexercised reason. Distinguish successful dispatch, checked end state and measured complex work.
3. Exercise uncovered families on synthetic scratch inputs with valid production preconditions and control outputs. Compare ordinary and constrained paths where relevant. Use Hub's timing slot for measurements and repeated paired output-checked observations for any gain claim.
4. Reconcile six final receipts and gaps. Deliver the inventory to Hub and report evidence, follow-ups and test/review decisions on the local candidate.

## Tests and checks to run
- Recreate the inventory and audit source coverage, uniqueness, aliases and each evidence join. Verify package identity, actual action outputs and any failed or omitted cases.
- Use existing exact tested packages and focused tests/stress/production probes in scratch. Independent tester chooses checks from these criteria. Check fragment and whitespace.

## Risks and edge cases
A tool selection, help listing, stale timing, or test group pass can hide a missing action. Preserve exact spellings, variant context, output checks, fixture sizes and resource limits.

## Documentation to update
Round 648 fragment and survey with linked evidence and inventory.

## Outcome
The source register contains 5,871 entries across 23 surfaces. Windows and WSL source checks passed. The independent tester verified native scene round trips, property persistence, UI states, screenshot controls and board redraws. The survey carries the specialist results, rejected candidates and route-specific gaps. The Hub owns integration and publication.
