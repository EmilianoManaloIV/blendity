# Cognitive walkthrough: the Textures and Materials windows (round 332, task 0313)

The request (2026-10-29): "Look into improving HCI experience in texture and material tab."

Method: a cognitive walkthrough (Wharton et al.) of five tasks, by a Blender user new to Blendity, asking at each step
the four questions - will they try it, will they see the control, will they connect it with the goal, will they see the
progress - and Nielsen's heuristics over both windows. Clicks are counted from the window open, before and after.

## The tasks

| # | Task | Before (clicks) | Problem found | Heuristic | Severity | After (clicks) |
|---|------|-----------------|---------------|-----------|----------|----------------|
| 1 | Make a brick texture and put it on a wall | 4 (New, Architecture, Bricks, Assign) | None that blocked; nothing told which textures no material used yet | Visibility of system status | 2 | 4, and an unused texture is tagged "not used" |
| 2 | Add a Noise to break it up | 2 + a scan of 48 names under two headings, Generators and Filters | The Add Layer menu was a long list with categories that are not Blender's | Recognition rather than recall; match with the real world | 3 | 2, with a search ("noi") or Blender's groups (Texture, Pattern, Color, Converter, Filter, Mask & Wear...) |
| 3 | See what one layer adds | Toggle every layer above it off and back on: 2 per layer | No way to see the stack up to a layer | Visibility; user control | 3 | 1: Alt+click its name (and again for all) |
| 4 | Undo a slider pushed too far | Remember the old value, or undo every step | No Reset to Default | User control and freedom | 3 | 2: right-click the name > Reset to Default |
| 5 | Make a fog volume | Inspector > slot > New Material of Type > a list of 12 to read through; the Materials window had no way at all | Types in one flat list; none in the Materials window | Recognition; consistency | 2 | 3 in the Materials window (Of Type... > Volume > Fog), the list grouped (Basic, Glass, Metal, Stylized, Volume) |
| - | A layer at Opacity 0, or with no targets | It looked like a bug: the layer was on and did nothing | - | Visibility; error prevention | 2 | The row says "Opacity 0" or "Writes nothing" |

## Fixed (each with a click test in `tests_round332`)
- Reset to Default on every number, switch, colour (Base Color with its Alpha), vector and list of a texture layer, a
  mask effect or a material - in the Textures and Materials windows and the object Inspector (right-click its name):
  the value a new one starts with. Ramps, curves and texture slots keep their own editing.
- Add Layer: a search field first (typing narrows to the names and groups that match), the layers under Blender's node
  groups. Picking a layer ends the search's edit, so the next click is the user's.
- Alt+click a layer's name, or its menu's Preview Up to Here (a pen has no Alt): the preview shows the stack up to it,
  outlined, with "Up to <layer> - Alt+click it again for all" over the preview. A stack edit drops it.
- Add Layer's search: Esc closes the menu, Enter takes the first match, "No layer matches" when none does.
- A layer that changes nothing says why at its row's end.
- A texture no material in the scene (or among the material assets opened) uses is tagged "not used" in the grid.
- Materials: Of Type... beside New Material Asset, and the slot's New Material of Type, by group.

## Already fine (checked, not changed)
- Delete asks first and names how many materials use a texture (task 0053).
- Texture edits are in the undo history (Ctrl+Z), with the materials' re-resolve.
- The Material Inspector folds its groups (Principled extras, Volume) - progressive disclosure since round 330.

## Not done (severity 1, or larger than this round)
- Thumbnails for the New menu's presets (a cache of 36 pictures evaluated on a worker): the names are themed already.
- A per-window undo for textures (the scene's undo already holds them).
- Favourites among presets; drag a texture tile onto a material's slot field.
