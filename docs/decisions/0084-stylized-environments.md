# ADR 0084: stylized environments - strokes as a mesh with custom normals, scatter as children, fog as a scene setting

- **Status:** accepted (task 0248, round 268)
- **Relates to:** ADR 0083 (the hand-painted look), ADR 0071 (the Draw tab's strokes), ADR 0081 (the Paint layer:
  masks for scatter and grass), ADR 0003 (copy-on-write meshes), task 0012 (the environment and the generated sky)

## Context
Ghibli-styled and oil-paint 3D scenes are built from flat cards with painted alphas - leaves, grass, brush strokes in
the air - whose normals are faked so a bush of cards shades as one round volume, from hand-scattered props, and from a
painted sky with a soft sun and a horizon band, with distance fog carrying the palette. Blendity has custom normals on
meshes (ADR 0034), the Paint layer's coverage as a mask (0245), sketch outlines (0232) and a gradient sky (0012); fog
exists only as a retro display filter.

## Decision
- **A Stroke object is one mesh built from a stroke list.** The `Strokes` component keeps the strokes (cards and
  ribbons with their frames and sizes); the mesh is rebuilt from the list on every edit, so the strokes stay editable
  (undo is the list) while renderers and exporters see a plain mesh with custom normals - no new render primitive.
- **The canopy normal is a custom normal**, the direction from a centre: the shading trick lives in the mesh data the
  renderers already honour, so it exports.
- **Scatter places children, not a particle system.** Each instance is a GameObject sharing the source's mesh (copy on
  write: no copies until edited) under one parent, so it is selectable, movable and deletable like anything else; a
  merge for export is a plain join. Density is per area of the dab's disc, jittered from a seed, so a dab is
  repeatable.
- **Fog is a scene setting applied in every renderer** by the view distance, with a two-colour ramp; the retro filter's
  fog stays a display effect. The sky's disc and band are gradient-mode fields, so the HDRI and Hosek-Wilkie modes are
  untouched.

## Consequences
- A Stroke object's mesh grows with its strokes; thousands of cards rebuild in one pass (positions appended), fine for
  a session's bushes; a forest would want chunks (not this round).
- Scatter instances are real objects: ten thousand of them cost the hierarchy ten thousand rows; `scatter merge` is the
  way out for export and for crowds.
- Fog in the path tracer is applied to camera rays only (no scattering): a stylization, not participating media.
