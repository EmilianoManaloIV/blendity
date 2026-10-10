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
