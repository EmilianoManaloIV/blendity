# 0183: textures through the Editing tab, and the stack as a camera look

- **Status:** done (round 200)
- **Requirements:** R-04 (textures), R-05 (rendering), R-07 (reachable from the windows and the console)
- **Decisions:** ADR 0058 amended (textures and the camera look)
- **Model:** main session
- **PR:** #145 (stacked on #144)

## Goal
The roadmap: "Edit Texture" opens a texture asset's image; "Apply to Texture" writes the result as a new Image layer
on top of the stack (the asset stays procedural underneath; dirty-saved as `.ptex`) or replaces that layer; "Use as
Camera Look" converts the stack's global (unmasked) adjustments into camera filters on the active camera, so the
Game view and F12 carry the look live.

## Design
- `src/editor/editing_textures.cpp`: `edit_open_texture` (the albedo output with an earlier edit's layer turned off,
  then that edit's stack from `<texture> edit.png.edit`), `edit_apply_to_texture` (the export to `<texture> edit.png`
  beside the asset, the stack beside it, the texture cache forgotten for it, the Image layer found by its image path
  or added with `targets = TargetAlbedo`, `note_texture_edit` / `touch` / `mark_changed` so the asset saves as any
  edit of it does), `edit_use_as_camera_look` (the Main Camera's Camera Filters, added when missing; `camera_look_step`
  maps each unmasked step: Exposure / Contrast / Saturation exactly, Temperature / Tint and the Tone Curve - lift,
  gamma and gain through black, middle grey and white - approximately, a darkening Vignette and Sharpen as their own
  effects; the rest named in the Console).
- `edit::Doc::save_stack` / `load_stack`, `edit::image_from_bitmap`; `Editor::set_layer_field` (the console's field
  setter on one texture layer).
- The panel's "Apply to Texture" (with a texture open) and "Use as Camera Look"; the Textures window's Edit;
  console `develop open texture <name>`, `develop apply`, `develop look`.

## Acceptance criteria
1. A texture's albedo after Apply matches the Editing export within 1/255.
2. "Use as Camera Look" on an exposure +1 stack gives a camera Color Grading with exposure 1, and mid-grey doubles.

## Results
Both hold (tests/test_editor.cpp; the worst albedo difference is printed by the test). Editing the texture again
reopens the same adjustments on the albedo under the edit and Apply reuses the layer; contrast, saturation, a curve,
a vignette and sharpening carry over to the camera, Clarity and a masked step are left out.

## Tests
tests/test_editor.cpp: "editing a texture: ...", "editing as a camera look: ...".
