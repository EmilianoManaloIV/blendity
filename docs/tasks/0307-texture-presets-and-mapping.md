# Task 0307 - Texture presets by theme, and Mapping on every generator (round 327) - done

The user asked on 2026-10-29: "For the Texture tab and options, recommend other options to make it similar to shader
graph in blender while maintaining the style that is already established. Organize all texture presets according to
their theming..." They chose to extend the layer stack, Blender's node ideas as layers and options, and presets by
subject. Rounds 328-333 follow (Perlin Noise and richer textures, converters, mix modes and Principled extras, the
Volume material, the HCI pass).

## What changes
- `texture_presets()`: {name, theme, anime}; `texture_preset_themes()`: Basics, Architecture, Floors & Wood, Nature,
  Sky & Weather, Metal & Fabric, Stone. 35 presets (20 new), each built from today's layers; the anime ones tagged.
  The New menu has a list per theme.
- `TextureLayer::map_offset / map_rotation / map_scale / map_seamless`, reflected by `reflect_mapping` after the
  layer's own fields (the Inspector matches edits by the fields' order); applied in `apply` and `apply_point` before a
  generator samples. Seamless: whole repeats, quarter turns.

## Checks (tests_round327)
- Every preset in a listed theme, none twice, the old fifteen kept; each builds and evaluates at 64 px.
- Scale 2 doubles a Checker's cells; Offset 0.25 shifts it a quarter tile exactly; Rotation 90 is a quarter turn;
  Seamless 1.5 equals 2; rows close across the seam; saved and read back.
- New > Nature > Sand by clicks makes the asset.
