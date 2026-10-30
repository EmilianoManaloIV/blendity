# 0273: texturing and bakes without the console

- **Status:** done (round 293)
- **Requirements:** R-14
- **Decisions:** ADR 0085 (console round)
- **Model:** main session
- **PR:** #228

## Asked
The user's request of task 0271 for texturing: `surface new` had no button at all, and a layer's mask presets,
grouping and Save as Smart Material were console verbs; the bakes offered only a size.

## What changed
- **New Surface**: GameObject > New Surface, the Textures window's New > Surface on the Selected Object, the palette's
  New Surface, the Texturing layout guide's first step.
- **A layer's menu** (its menu button in the layer stack) adds, for a top-level layer: **Mask Preset** (a submenu of
  the presets: edge wear, dirt, dust, metal edge wear, position gradient, thickness), **Clear Mask**, **Group with the
  Layer Below** and **Save as Smart Material...** (a Group layer only; a dialog offers the layer's name, or the next
  free "name 2", and says when a typed name would replace a saved one - its button then reads Replace). The Mask Preset
  submenu shows on a surface only (the presets read its mesh maps). Each goes through `Editor::texture_layer_op`, one
  undo step for the layer edits.
- **The Textures shelf** lists the project's saved smart materials after the built-in ones.
- **A Bake dialog** for Bake Mesh Maps... and Bake Textures... (GameObject menu and palette): size, samples and Keep
  UVs (each bake its own: on for the mesh maps, off for Bake Textures, which made new UVs before), then Bake or Cancel.
  It replaces the size-only submenus. Load Mesh Maps' Fix opens it instead of starting a bake.

## Tests (`tests_round293`)
By clicks: GameObject > New Surface gives the cube a surface; the layer's menu (the Textures window scrolled to show
it) applies the edge wear preset (2 effects), clears it, groups two layers into one Group of two, and saves a smart
material file through the dialog, then offers "Smat293 2" instead of replacing it, and the item is greyed on a Fill
layer; Bake Mesh Maps... with size 512 and Bake closes the dialog and starts a 512 bake; Keep UVs on for the mesh maps,
off for Bake Textures.

## Not done
- "Group Selected Layers" for more than two: the menu groups a layer with the one below it.
