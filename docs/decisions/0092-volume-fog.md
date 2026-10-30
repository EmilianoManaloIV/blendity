# ADR 0092: a Volume (fog) material as a surface type

- **Status:** accepted (2026-10-30, task 0311, round 331)
- **Deciders:** the user and the main session. The user asked for "a volume like material that adds 'fog'" and chose a
  true volume in the path tracer, with approximations in the views.

## Context
Fog was only the screen-space distance fog of the world. Blender puts a Principled Volume on a mesh's Volume output:
the mesh bounds a medium. Blendity's materials have one surface type each (Opaque, Cutout, Transparent, Glass).

## Decision
- **A surface type, not a second output:** `MaterialSurface::Volume` makes the whole material a medium with an
  invisible boundary - a mesh is either a surface or a fog. Its fields are appended (and named "Fog ..." so that no
  field name is shared: files and the Inspector match fields by name and order).
- **The path tracer is the reference:** the path carries the medium it is in, entered and left at the boundary's front
  and back faces (a closed mesh is required; an open one warns). Free flights by delta tracking against a majorant
  (twice the density with noise), Henyey-Greenstein scattering, next-event estimation to the lights through
  `transmittance`, which dims by the fog crossed. A camera ray first asks which fog its origin is in.
- **The views approximate:** the length through the mesh's bounding box (exact for boxes), cut by the depth behind,
  as exp(-density x length) toward a lit fog colour. No shadows, no shafts.
- **No GPU volumes yet:** the GPU kernel's Material can't grow (SPIR-V's id bound, round 330), and the views' g-buffer
  has no room. A scene with fog is traced on the CPU, and the views go to the CPU, each with a reason.

- **Fogs inside fogs add up:** a path keeps the fogs it is in (up to four, entered and left by object); a free flight
  tracks their summed majorant and a collision picks a fog by its share of the density. Shadow rays start with the
  fogs the path is in, so a lamp in a foggy room is dimmed on its way out; a camera ray starts with the fogs the camera
  is in (one probe a render() call).
- **Lights from fog are sampled as from surfaces** (`sample_light`: the sun's disc, an area light's panel, a point
  light's radius), so fog shadows are as soft as surfaces'.

## Consequences
- One mesh can't be both a glass shell and the smoke inside it: two meshes do it. A path keeps its fogs through glass:
  a glass ball in a foggy room is foggy inside (as a ray that refracts is still in the room).
- The views' fog is a thickness through the mesh's bounds from the view's eye: a non-convex mesh (a torus) is fogged
  as its box, more than once where several front faces overlap a pixel; an orthographic view measures from the orbit's
  eye. The path tracer has neither limit.
- The camera's fogs are found by one probe from its position: a lens's aperture (depth of field) starts in the same.
- Fog in the views is a thickness, not lighting; F12 with Path Tracing shows the real thing.
- GPU volumes are a later task: a medium stack in the kernel and its own records after the materials, as the extras.
