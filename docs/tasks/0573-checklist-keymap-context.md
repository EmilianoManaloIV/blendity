# 0573: Make the HCI checklist's shortcut steps match the active keymap (round 591)

- **Status:** in review, 2026-11-19 (approved by owner)
- **Requirements:** U-03, U-04
- **Decisions:** ADR 0085, ADR 0123
- **Implementation:** implementer
- **PR:** pending, targets `main`

## Goal

An artist following the checklist can identify the intended action and use the shortcut that applies to their preset or custom bindings.

## Reproduction

Open a fresh Blendity profile, select a cube, enter Edit Mode and select a face. Checklist item D2 says to press E to extrude. The fresh profile uses Unity, where E selects Rotate and Ctrl+E invokes Extrude. Blender uses E for Extrude and 3ds Max uses Shift+E. The keyboard dispatcher uses the active chord table in Edit Mode. A label-only source audit cannot catch this checklist instruction because D2 lists no control names to verify.

## Scope and ownership

- Update `docs/hci/checklist.json` and regenerate `docs/hci/checklist.md`. State the preset for concrete shortcut examples and explain how to find the active binding for a different preset or customized keymap.
- Fix D2. Review the directly related preset-dependent steps in B1 (Frame Selected), D1/D2 (Edit Mode and mesh selection), D3 (Loop Cut), K1 (Set Key), and Q2/R12 (Edit Mode). Check the current `shortcut_actions()` and `keymap_preset()` tables, including the absence of a default chord where applicable.
- Preserve unrelated procedures, row IDs, task outcomes and timings. Product bindings, keymap behavior, UI code, historical reports and baseline thresholds are out of scope.
- Add `changelog.d/591-checklist-keymap-context.md` and mark this spec in review after checks.

## Acceptance criteria

1. In a fresh Unity profile, D2 leads to Extrude through Ctrl+E. It also identifies Extrude by action name so readers with different or customized presets can find their binding.
2. The reviewed navigation, Edit Mode, Loop Cut and Set Key examples either name their preset or identify the intended action without promising a universal chord. The text stays consistent with the current source action and context tables.
3. All 141 checklist IDs and their expected outcomes remain present. `py tools/hci_checklist.py --check` passes, the generated Markdown mirrors the JSON, and a scratch HTML generation shows the updated instructions. `py tools/dev/changelog.py check` and `git diff --check` pass.

## Verification

The keyboard tables in src/editor/keymap.cpp define the following defaults for the reviewed actions. The editor initializes new profiles to Unity. Preferences > Keymap shows the actual binding after customization.

| Preset | Frame Selected | Edit Mode On / Off | Edge / Face Select Mode | Extrude | Loop Cut | Set Key |
|---|---|---|---|---|---|---|
| Unity, Unreal, Adobe | F | Tab | 2 / 3 | Ctrl+E | Ctrl+R | Shift+K |
| Blender | . | Tab | 2 / 3 | E | Ctrl+R | Shift+I |
| Maya | F | F8 | F10 / F11 | Ctrl+E | Ctrl+R | S |
| 3ds Max | Z | Tab | 2 / 4 | Shift+E | Ctrl+R | Shift+K |
| SketchUp | Shift+Z | Tab | 2 / 3 | Ctrl+E | Unbound | Shift+K |

Frame Selected and the Edit Mode toggle use Scene view context. Edge select, face select and Loop Cut use Scene view in Edit Mode. Extrude uses Edit Mode, and Set Key is a global action. The seven amended rows now include their control names for the source-label check. The Markdown and scratch HTML were generated from the same JSON and verified against it. All 141 IDs, expected outcomes and time targets are preserved.
