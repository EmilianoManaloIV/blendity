# 0141: Collada

- **Status:** done
- **Requirements:** R-6, R-13
- **Decisions:** ADR 0047
- **Model:** main session
- **PR:** (with tasks 0136-0140)

## Goal
Collada scenes (.dae: Blender, Maya, SketchUp, Mixamo) open with their geometry, materials, skins, hierarchy and
animation, the right way up and the right size.

## Scope
- In: `core/xml.h`, a small XML reader of our own (elements, attributes, text, entities; comments, CDATA and the
  prolog skipped; a depth limit).
- In: `asset`: `up_axis` (Y, Z or X up) and `unit meter`, as one change of basis C (with the X mirror): points `C p`,
  node matrices `C M C^-1`.
- In: `library_images`, `library_effects`, `library_materials`: the diffuse colour or texture (through sampler and
  surface newparams), emission, shininess as roughness, transparency (A_ONE or RGB_ZERO).
- In: `library_geometries`: positions through `vertices`, UVs by offset, `triangles`, `polylist` and `polygons`, a
  material slot per symbol.
- In: `library_controllers` skins: the bind shape matrix on the vertices, joints by sid (or id), inverse bind matrices
  as the joints' binds, vertex weights as groups named after the joint nodes.
- In: `library_visual_scenes`: nodes with matrix / translate / rotate / scale stacks, JOINT nodes as bones, instanced
  geometry and controllers with their bound materials.
- In: `library_animations`: channels on `node/sid[.member]`, matrix or component, sampled at the keys' union per node,
  the node's stack recomposed, as one clip at the rate the keys fit.
- Out: morph controllers' targets (the base mesh is taken; a skin over a morph takes the morph's geometry), cameras,
  lights, physics, `lookat` and `skew` transforms, `instance_node`, `<ph>` (polygons with holes), `<lines>`,
  double-sided `<extra>` flags, textured `transparent`; BEZIER and HERMITE samplers are read as linear.

## Acceptance criteria
1. A Z-up centimetre file: standing in metres facing +Z; the texture found; the emission and roughness read; the
   skin's groups named after the joints; at rest unmoved.
2. Its matrix animation as a clip: at its end the upper joint has turned 90 degrees and the mesh with it.
3. Cut-short files refuse or import without harm.
4. Dropped into the editor: the `.mat`, the texture copied in, the clip playing; the lesson's Try-it.

## As built
- `core/xml.h`, `scene/import_dae.cpp`; tests: round 161.
- After the review: the `<source>` cache lives with the import (a static one outlived its file); CDATA cut short is
  refused rather than read past; X_UP mirrored like the others; a skin whose source is a morph controller (Blender's
  shape keys under an armature) finds its geometry; the clip starts at its first key; a line in a polylist no longer
  ends the mesh; joints are looked for under the instance's `<skeleton>` roots first; a byte-order mark is skipped;
  `(r)(c)` matrix members; each primitive its own UV table; indices read as integers; times checked finite; the
  first node of an id wins like a sid; twin joint names share a group; one `best_frame_rate` for every reader; the
  tests add a Y-up metre file with translate / rotate component channels, a BOM and CDATA.
