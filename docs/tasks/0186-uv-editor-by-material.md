# 0186: the UV Editor shows one material's islands

- **Status:** done (round 197)
- **Requirements:** R-04 (UV mapping)
- **Decisions:** none (an editor filter; no ADR)
- **Model:** main session
- **PR:** #142 (stacked on #141)

## Goal
The roadmap: a Material filter in the UV Editor (All, then each slot of the edited mesh; several at once), so only the
chosen slots' faces are drawn, selectable, transformed, unwrapped and packed; the others dimmed or hidden; the
Materials window's "Edit UVs" opens the editor on that material; the background's Material Texture follows the chosen
slot; console `uv material <name>|all`.

## Design
- `Editor::uv_slots_` (empty: every material) and `uv_hide_others_`; `uv_slot_active(m, f)`.
- The UV Editor draws the other faces as a faint outline first (or not at all), and its "shown" faces - the ones
  drawn as editable, picked and box-selected - are the filter's; `uv_clip_selection` unselects every corner outside it
  each frame and before a selection or transform, so nothing out of the filter moves.
- `uv_op` puts the filter in its face mask (with the Edit Mode selection when there is one), so Unwrap, Smart UV, Part
  Unwrap, the projections, Pack and Average Scale touch only those faces; packing packs them into 0-1, the others stay.
- The toolbar's picker (All materials, then "slot: name"), the background texture of the first chosen slot, console
  `uv material <name>` / `+<name>` (adds) / `all` and `uv others dim|hide`, Materials window > Edit UVs.

## Acceptance criteria
1. On a cube with two materials, with the second chosen: Unwrap changes only its faces' UVs, Select All selects only
   its corners, Pack puts its faces in 0-1 and leaves the first material's UVs exactly as they were.
2. The other faces are drawn dimmed, or hidden after `uv others hide`; `+<name>` adds a material, `all` clears; an
   unknown name changes nothing.

## Results
The test checks all of it (tests/test_modeling.cpp, "uv material: ...").

## Tests
tests/test_modeling.cpp: "uv material: with the second of a cube's two materials chosen, ...".
