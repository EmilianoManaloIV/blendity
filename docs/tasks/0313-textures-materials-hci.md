# Task 0313: the Textures and Materials windows' HCI pass

- **Status:** done (round 332, 2026-10-30)
- **Asked:** "Look into improving HCI experience in texture and material tab."
- **Method:** `docs/hci/walkthrough-textures-materials.md` - a cognitive walkthrough of five tasks and Nielsen's
  heuristics; every finding of severity 2 or more fixed.

## What
- Reset to Default (right-click a field's name) on texture layers' and materials' numbers, switches, colours and
  lists: the defaults read once from a new layer of the type or a new material (`FieldDefaults`, `DefaultsReader`).
- Add Layer: a search field, Blender's node groups (`texture_layer_groups`, `texture_layer_group`).
- Alt+click a layer's name: the preview up to it (`Editor::draw_texture_upto`).
- A layer that changes nothing says why (`texture_layer_no_effect`).
- "not used" on a texture no material uses (`Editor::used_texture_assets`).
- Materials: Of Type... and the slot's New Material of Type by group (`material_preset_groups`).

## Acceptance (`tests_round332`, by clicks)
- Reset to Default puts a layer's Scale and a fog's Density back to their defaults.
- Add Layer's search narrows the list; a pick adds the layer and the next click lands.
- Alt+click shows the preview up to a layer; again, for all.
- A new texture is tagged "not used".
- Every layer type is in a group; every material preset in exactly one.

## Not done
Preset thumbnails in the New menu, a texture-only undo, favourites, drag a tile onto a slot (severity 1).
