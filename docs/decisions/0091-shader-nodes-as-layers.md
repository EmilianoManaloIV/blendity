# ADR 0091: Blender's shader nodes as the texture stack's layers and options

- **Status:** accepted (2026-10-30, tasks 0307-0309, rounds 327-329); amends ADR 0017 (the layer stack)
- **Deciders:** the user and the main session. The user asked for the Textures window to be "similar to shader graph in
  blender while maintaining the style that is already established"; asked whether to add a node view, they chose to
  extend the layer stack.

## Context
A procedural texture is a stack of layers (ADR 0017), drawn by reflection in the Inspector style the rest of Blendity
uses. Blender's artists think in nodes: a Mapping before a texture, a ColorRamp after it, Math and Map Range between.

## Decision
- **No node canvas.** Each node becomes a layer (a generator, or a filter on one plane: Map Range, Clamp, Math, RGB
  Curves, Separate Channel, the ColorRamp), a layer's option (Noise's Musgrave types, Voronoi's features and metrics,
  Wave's rings and profiles) or a field every pattern generator has (Mapping).
- **Mapping is part of the layer's data:** `map_offset`, `map_rotation`, `map_scale`, `map_seamless`, reflected after
  the layer's own fields (the Inspector matches edits by the fields' order) under names starting "Map". Applied before a
  generator samples, in both the planes and live shading.
- **Periodic everything:** a texture is a tile. Noise lattices are whole numbers of cells, octaves whole multiples;
  Mapping's Seamless keeps whole repeats and quarter turns.
- **Old files are unchanged pictures:** each new option's first value (Smooth noise, Cells, Bands, Sine, no mapping, a
  ramp of its two old colours) is the old behaviour, and new fields are appended.
- **Ramps and curves are reflector data** (`Reflector::ramp`, `curve`): text to files, undo, hashing and edits; the
  Inspector draws Blender's ColorRamp and curve widgets.

## Consequences
- What a node graph can wire freely (a texture driving another's scale) a stack can't; the layers' masks, Use Mask,
  Separate Channel and Math with another plane cover the common cases.
- A preset names its layers' fields by text; a misspelled name is counted and warned (a test wants none).
