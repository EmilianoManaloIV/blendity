# 0574: Keep the Lighting update mode and bake counts readable in a narrow pane (round 592)

- **Status:** independent review approved; Windows verified; hosted Linux checks pending
- **Requirements:** U-05, U-07
- **Decisions:** ADR 0085, ADR 0123
- **Implementation:** implementer; hub owns dispatch and publication
- **PR:** pending, targets `main`

## Goal

An artist can tell whether realtime lighting updates are Manual or Automatic and recover the full bake summary at tablet scale without scrolling away from the operation buttons.

## Reproduction

Use a scratch scene in the Default layout. At 1024×640, Dark theme, UI scale 1.5, open Window > Lighting. The pane displays `Lighting Updates: …` with its current mode missing, and the Contribute GI summary cuts off its object and lightmap counts. Hover has no full-text help for either row. The initial labels are drawn in `src/editor/panels.cpp` through `Context::label`, which elides without creating a tooltip. The button audit does not include these passive labels. See the [round 590 screenshot](../hci/sweep-590/lighting-status-dark-1024-x1.5.png).

## Scope and ownership

- `src/editor/panels.cpp`: keep the active mode readable at the start of its status row. Make the full update-mode status and the Contribute GI summary recoverable on hover, including lightmap size and baked object counts. Use the existing Lighting status data and tooltip API.
- `tests/test_hci.cpp`: add a focused status and hover regression using the original narrow configuration and a wider control. Reuse the HCI fixture and real mouse events.
- Preserve the existing controls, bake and update semantics, defaults and informational counts. Toolkit-wide label policy, other panes, benchmark work, and changes to the established sweep baselines are outside this task.
- Add `changelog.d/592-lighting-status-labels.md`, including its usability entry, and mark the spec in review after checks.

## Acceptance criteria

1. At 1024×640, Dark, scale 1.5, the status visibly distinguishes Manual from Automatic before any elision. Hover over the status in each state shows the complete current explanation.
2. At the same configuration, hover over the Contribute GI summary exposes its full contributing object, lightmap size and baked object counts. The default scene and a fixture with at least one contributing mesh demonstrate that the label uses live counts.
3. A wider Lighting pane at 1366×768, Dark, scale 1 preserves the complete status and summary, and the first bake controls remain reachable. The focused real-mouse HCI regression, `hci` group, static label and consistency checks, and full live sweep pass against the existing baseline.

## Verification

| Release build | Focused Lighting | HCI group | Editor group |
|---|---:|---:|---:|
| Windows, Blender libraries | 66 | 2,091 | 3,872 |
| Windows, dependency-free | 66 | 2,088 | 3,846 |

All listed checks passed. The Windows live sweep covered all twelve configurations and matched the existing baseline of seven elided controls and three small handles. The static label and consistency audits reported zero findings. The coverage, command list and changelog fragment checks passed.

The regression compares the painted mode with the expected text before hovering and checks the full explanation with real mouse movement. A populated fixture contributes one mesh, two 64 px lightmaps and one baked object. Stationary hover covers a transition from waiting to completed preview. Review confirmed that changing the in-progress percentage leaves the tooltip text stable within each stage.

Hosted Linux verification is pending for the dependency-free release build. Run the focused Lighting regression and the full `hci` and `editor` groups. The local WSL service failed before producing a test binary.
