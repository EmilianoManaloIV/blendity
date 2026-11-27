# 0640: Input gestures for the outstanding tool register

- **Status:** in review
- **Authorization:** Pass 37 owner assignment through Hub, round 658 on d7503d4e.
- **Requirements:** Q-03, Q-04, Q-06, B-06
- **Decisions:** ADR 0056, 0123, 0134
- **Implementation:** project implementer for the gesture batch; lead for evidence and register reconciliation
- **PR:** local handoff to Hub

## Goal

Let the owner identify which previously unexercised or dispatch-only controls a user can operate successfully, using the actual event route and a checked result.

## Scope

Start with the 0630 exact-key register, verify it against the current sources and distinguish earlier failures with newer fixes. Prioritize reachable, high-use controls with observable effects and missing input evidence. Use controlled fixtures and package-backed replay. Route product failures to the Hub. Product edits and publication are separate assignments.

## Extended sweep

The Hub requested further grouped real-input batches on November 27. Exercise registered components through Add Component and typed, canceled and committed input on their visible Inspector fields. Extend the palette and menu sweep beyond its first 136 trials, keeping per-key observations and request-specific controls. Separate broad desktop work that remains feasible from operations requiring external software or physical equipment. The implementer owns the additional palette driver in `tools/dev/gesture_palette_0640.py`. The lead owns `tools/gesture_inspector_0640.cpp` for Inspector and stack groups, `tools/gesture_bindings_0640.cpp` for enabled and cleared keybindings, and the per-family gap accounting. Reuse successful exact-route fixtures directly.

## Likely files

- Implementer owns `tools/dev/gesture_sweep_0640.py` for real event replay.
- Lead owns the source catalogue correction in `tools/dev/tool_inventory_0630.py`, the keyboard Wheel probe in `tools/gesture_wheel_0640.cpp`, `docs/perf/0630-inventory.tsv`, `docs/perf/0640-input-sweep.md`, this spec, and the round 658 fragment, and the historical backlog dispositions in `docs/handoff/OPEN-WORK.md`. Shared scratch is `C:/blab/pass37-hub/0640`.

## Acceptance criteria

1. Recompute current keys, record the starting backlog and order remaining work by observable user impact, available input route and ability to verify results.
2. Run a reproducible batch of actual mouse or keyboard events on previously unexercised or dispatch-only entries. Each promotion names the event, starting state, result, negative control and underlying evidence. A named fixture or command alone does not prove a click.
3. Check failures and stale findings against newer accepted evidence. Document remaining gaps, package identity, resource use and the limits of each input route.
4. Independently verify the joined register, replay evidence, counts, changelog and frozen candidate. Hand local commit and failure receipts to the Hub.

## Tests and checks to run

Use isolated projects and package-backed functional windows for event replay. Verify exact keys, absence of unrelated changes and reproducibility of the register and evidence. Check fragment applicability and staged whitespace.

## Risks and edge cases

Focus, context, disabled controls, animation, duplicate labels and user preferences can change the event recipient. Compare the actual result to an independent expected state and a missing or ineffective event. Keep performance conclusions separate from functionality.

## Documentation to update

The per-action register, a link from the historical 0630 survey to the current results, the round survey with prioritized outstanding work, and the round changelog fragment.

## Outcome

The register grew by 34 explicit source keys. The round brought 757 keys to checked status and recorded 25 additional dispatch-only results. The final Windows run passed 243 gesture trials. The corrected Linux Inspector, material, binding and contextual checks also passed. The [round report](../perf/0640-input-sweep.md) gives counts, residual fixtures, external dependencies and prior-failure dispositions. The Hub receives the independently checked local commit for integration.

## Combined-source verification

The Hub assigned a documentation and inventory follow-up on the frozen product at `d79d27a7e09eeaa6cdde1a33906cbdfd5beaeaf9`. Regenerate in an isolated worktree, compare every existing key and evidence cell, check changed command locations and new declarations against source, and account for the 3,542 prior gaps plus any new routes by family. Independently verify the staged tree and hand the local follow-up commit to the Hub.
