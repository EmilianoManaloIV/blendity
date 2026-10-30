# ADR 0082: smart materials as saved groups, texture sets as packings, 2D painting on the same brush

- **Status:** accepted (task 0246, round 266)
- **Relates to:** ADR 0080 (the layer stack on a surface: groups, masks, the Mesh Map generator), ADR 0081 (painting
  by projection: the brush and the Paint layer this round reuses in 2D), ADR 0017 (procedural texture assets and
  their text format), task 0036 (Bake Textures: images other programs read)

## Context
Substance Painter's smart materials are layer groups with their masks and generators, saved and dropped onto any
asset, where the generators read that asset's baked maps; its export writes the channels packed as each target wants
(glTF's ORM, Unity's metallic-smoothness, Unreal's ORM); its 2D view paints on the UV square. Blendity has the stack
with groups, masks and the Mesh Map generator (0244), the brush and the Paint layer (0245), a texture text format
(ADR 0017) and image exporters for materials (0036).

## Decision
- **A smart material is a saved Group layer.** Its text is the layer reflection the `.ptex` already uses ("C0 ...",
  "M0 ..."), so no second format; applying it adds a copy of the group on top of the surface's stack, and its Mesh
  Map effects read whatever maps the surface has - the adaptivity is the stack's, not the asset's. The built-ins are
  built in code and written out as `.smat` files on request (`smat builtin <name>`), so they are plain examples the user
  can open and change.
- **Texture sets are packings of the evaluated planes**, one function per target (glTF, Unity, HDRP, Unreal, separate)
  over the same `TexPlanes` the surface evaluates to, with the mesh maps' occlusion as the AO channel; PNG only this
  round. The material can be pointed at the files, so the existing exporters carry them without knowing surfaces.
- **2D painting is the projection brush without the projection:** the Textures window's preview pane is the canvas,
  a pane pixel is a UV, the dab writes the texel's neighbourhood in the layer's images through the same stroke, tile
  undo and dilation; stencils multiply a dab's weight by an image held over the Scene view.

## Consequences
- A smart material carries no images of its own: Paint layers inside a group are saved by their PNG paths, which the
  applying surface does not have - a smart material with paint is refused with a reason (procedural materials only,
  as Substance's generators-only smart materials behave).
- The export is a snapshot: later strokes do not update exported files until exported again (the log says so).
- A 2D dab paints every triangle sharing the texel alike (no depth test applies in UV space), which is what a 2D view
  does in Substance too.
