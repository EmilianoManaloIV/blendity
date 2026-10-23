# 0146: Overlay layers in the Scene view

- **Status:** done (round 165)
- **Requirements:** R-02 (the editor), R-13 (rigging)
- **Decisions:** none needed
- **Model:** main session
- **PR:** (round 165, with tasks 0145 and 0154)

## Goal
The user: "Have the ability to hide certain objects like bone controls so I can see the mesh without too much
overstimulation. This also means I can hide certain layers for example I can hide the bone layer, but can still see
IK/FK handles, etc." Each kind of overlay the Scene view draws is a layer with its own switch, in a menu and on the
console, and a hidden layer isn't picked.

## Scope
- In: `Editor::show_bones_`, `show_rig_controls_`, `show_icons_`, `show_points_`, `show_origins_`,
  `show_footprints_`, `show_outline_` beside the grid, gizmos and statistics flags; `overlay_layers()` (key, label,
  help, flag) in menu order; an "Overlays" button in the Scene view's bar opening a menu of checks (All Overlays, then
  each layer; `draw_overlays_menu`); `draw_scene_icons`, `draw_origins`, `draw_footprint_frames` /
  `draw_footprint_soles`, the selection outline, `draw_bone_handles` and `bone_handles_update` honour the flags; the
  picking (`pick`'s icons, `pick_wire_object`'s bones, controls, curves and lattices) skips hidden layers; console
  `overlay list | all|<key> [on|off|toggle]`; the hidden layers saved in the preferences (`overlays_off=`); the
  lesson "Overlays: See the Mesh, Hide the Rig".
- Out: spring colliders and contacts as layers (task 0153 draws colliders and gets a layer then); per-object hiding
  (the Hierarchy's eye does that).

## Acceptance criteria
1. A humanoid rig: a click on a bone selects it; `overlay bones off` leaves the rig controls on, draws no bone
   handles for the selected bone, and the same click selects nothing (the console still selects it); `overlay all
   off` / `all on` set every layer; an unknown layer warns.
2. `overlay icons off`: a click on the sun's icon selects nothing; the Overlays menu's Bones row turns the layer off;
   the choice survives save_prefs / load_prefs.

## After the review
- The preferences reader had a dangling `else` after `overlays_off` that stopped every key after it (the test now
  writes a key after it and reads it back); hidden control points aren't picked; `overlay all` with no value turns
  everything on unless all are on (the menu's way); a value that isn't on / off / toggle warns.

## As built
- `src/editor/editor.h`, `src/editor/scene_view.cpp`, `src/editor/bone_edit.cpp`, `src/editor/editor.cpp` (the
  console command, the preferences, the help line), `src/editor/lessons.cpp`; tests: round 165.
