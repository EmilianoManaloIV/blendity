# 0028: The Textures window

- **Status:** approved (the roadmap of 2026-10-11; after task 0027)
- **Requirements:** R-03
- **Decisions:** ADR 0017 (the window is its editor; no new decision)
- **Model:** main session (Opus)
- **PR:** (link when opened)

## Goal
The user asked for "a new tab dedicated to making procedural textures, following a modifier stack like
Blender". Afterwards a **Textures** window lists the project's texture assets as thumbnails, creates one
from a preset (Bricks, Wood Floor, Tatami, Tiles, Noise, Checker), edits the selected texture's settings
and its layer stack (add by category, reorder, enable, reset, remove, every field), previews its four
outputs, and assigns it to the selection by a button or by dragging a thumbnail onto an object.

## Scope
- In:
  - **`WindowKind::Textures`**, added before `Count` (saved layouts store the kind as a number), with its
    title and icon in the tables (`editor.cpp` `names[]` / `icons[]`, now with `static_assert`s on their
    sizes), the Window menu, `window Textures`, and `dock_draw_window`.
  - **`draw_textures_window`** in a new `src/editor/texture_window.cpp`, the Materials window's pattern:
    - a toolbar: **New** (a dropdown of the presets), the size (256 / 512 / 1024 / 2048), **Assign to
      Selection**, a search field;
    - a grid of thumbnails (the albedo output at 64², drawn with `Canvas::blit`), selecting one; a
      thumbnail dragged onto an object in the Scene view or a Hierarchy row assigns it (`asset_drag_`);
    - the selected texture's editor below: its fields through the Inspector's reflector, then **the layer
      stack** drawn like the camera filters' (`filter_effects` in `panels.cpp`): a foldout per layer
      with its checkbox, a menu (Move Up / Down, Reset, Remove), **Add Layer** by category; fields
      edited with the `L<i> ` prefixes, each edit one undo step;
    - a preview pane: the chosen output (Albedo / Normal / Roughness / Height), 1× or tiled 2×2; while a
      field is being dragged the preview evaluates at 256² and the full size on release.
  - **Undo for texture edits:** a texture asset lives outside the scene, so an edit records the asset's
    text before and after in the undo step (`UndoStep::texture_edits`); undo and redo restore it.
  - **`texture_stack_op(asset, op, i, j, name)`** shared by the window's menus and the `texlayer` command.
  - Double-clicking a `.ptex` in the Project window opens the Textures window with it selected.
- Out: per-layer mask painting, node graphs, live (per-pixel) textures.

## Likely files
- New `src/editor/texture_window.cpp`; `src/editor/editor.h` (`WindowKind`, members), `src/editor/editor.cpp`
  (tables, menu, `window`, undo of texture edits, `texture_stack_op`), `src/editor/panels.cpp` (the stack
  drawing generalised), `src/editor/project_window.cpp` (double-click), `tests/test_main.cpp` (round 50).

## Acceptance criteria
1. `window Textures` opens the window; a layout saved with it reloads with it (the new kind's number).
2. **New > Bricks** by mouse clicks creates `Assets/Textures/Bricks.ptex` with the preset's layers and
   selects it.
3. **Add Layer > Blur** by clicks appends a Blur; **Move Up** in its menu moves it; the checkbox disables it.
4. Editing a layer field in the window changes the texture (its hash) and the preview; **undo** restores
   the field's value and the preview; redo re-applies it.
5. Dragging a thumbnail onto the Cube in the Scene view assigns the texture's albedo / normal / roughness.
6. The preview evaluates at 256² while a field is dragged and at the texture's size on release.
7. Existing unit tests and `editor_fuzz` (verify mode) show no regressions.

## Tests and checks to run
- Unit: `BLENDITY_TEST_FILTER="textures window"`, then the full suite (Windows, Linux, sanitizer, verify
  mode). Stress: `editor_fuzz`, `textures`.

## Risks and edge cases
- A layout file from before has no Textures kind: unchanged behaviour (it is only appended).
- Undo of an asset edit after the asset was deleted or renamed: the step finds no asset and does nothing.
- Thumbnails for many textures: evaluated once per hash at 64², cached.

## Documentation to update
- CHANGELOG, README (the Textures window), ARCHITECTURE row, USABILITY row.
