# 0008: One Camera Filters stack per camera, with effects chosen by category

- **Status:** accepted (2026-10-09, the user chose the structure and categories); refines ADR 0007
- **Requirements:** R-10

## Context
ADR 0007 made each filter its own component (`Retro Console Filter`). The user wants a general system
like Unity's post-processing: a camera carries a stack of effects, picked by category from one menu. The
categories are:
- Retro Console;
- Color;
- Lens;
- Stylize;
- Bloom & glow.

Local volumes (blending by where the camera is) are left for later.

## Decision
- **One component, `Camera Filters`**, on the camera's GameObject, holding an ordered list of
  **effects** (`FilterEffect`).
  - Each effect has an on/off toggle, its own settings, and a type registered with a category.
  - The Inspector shows the stack like Unity's post-processing profile: a foldout per effect with its
    toggle and a menu (Move Up / Move Down / Reset / Remove), plus **Add Filter**, a menu grouped by
    category.
- **Effects contribute to the same `FilterStack` as before** (ADR 0007), in list order:
  1. raster settings (vertex snap, affine UVs, texture rules);
  2. the internal resolution;
  3. image passes.

  The render paths (`Editor::render_camera`, the path-traced paths) don't change.
- **Serialization, undo and hashing** go through the reflection system:
  - `Reflector::filter_effects` has a default implementation that writes the effects' type names as one
    `;`-separated text field, then each effect's fields under an index prefix (`E0 Width`). Files, undo snapshots,
    `hash_component` and `set CameraFilters.E0Width 320` all work through it.
  - The Inspector overrides it to draw the stack UI. It records edits under the same prefixed names, so
    an edit made with several cameras selected reaches each camera's effect at that position.
  - `set` cannot change the type list (use the `filter` command).
- **Effects are deep-copied** when the component is cloned (undo snapshots, Duplicate).
- **The `Retro Console Filter` component of task 0004 becomes the effect `Retro Console`.**
  - Its fields keep their names.
  - A scene saved with the old component loads into a Camera Filters stack holding that effect.
- **Bloom needs light above 1.0.** The rasterizer gains an optional linear HDR copy of the colour (filled
  by `shade_deferred` before it encodes for display). Only stacks that need it ask for it (task 0007). It
  costs about 3 ms at 1080p when asked for, and nothing otherwise.

## Consequences
- The Add Component menu shows one entry (Camera Filters) instead of one per effect. New effects are a
  `FilterEffect` subclass plus one registration line.
- Two filter components on one camera (task 0004's stacking) become two effects in one list.
- Only effects that don't need depth or HDR apply to path-traced output: their image passes ignore a
  null `FilterFrame` (as in ADR 0007).

## Amendment (2026-10-11, task 0032): the rasterizer stages in the path tracers
The user asked for "all the camera filter modifier stack to show up ... in the path traced system as well".
Image passes already did (depth and ids from the tracer's first hits, ADR 0016). The Retro Console's four
rasterizer stages now have path-traced equivalents, on the CPU and the GPU:
- **Vertex snap:** the editor gives the tracer copies of the meshes whose vertices are snapped as the
  rasterizer snaps them (screen position rounded at the filter's internal size, depth and w kept), mapped
  back into each object's own space so Box and Generated mapping keep reading object coordinates (of the
  snapped surface: unlike the rasterizer, the tracer also lights and shadows the snapped geometry).
- **Affine textures and the screen door** apply at a camera ray's hits (`PTSettings::Retro`): the hit is
  projected through the camera to find its place on the filter's image, the UVs come from the triangle's
  screen-space barycentrics, and a Transparent surface is solid or absent by the 2 x 2 checkerboard.
- **The texture rules** apply on every hit: the CPU through `SurfacePoint::tex`; the GPU's images are
  uploaded at the capped mip level and sampled with the override filter (the 3-point filter included).
The consequence above ("only effects that don't need depth or HDR apply to path-traced output") no longer holds.

