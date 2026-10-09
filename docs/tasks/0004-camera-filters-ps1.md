# 0004: Camera filters, with the PS1 look

- **Status:** done (merged as PR #4); the filter stack it started is generalised in task 0005 (ADR 0008)
- **Requirements:** R-10
- **Decisions:** ADR 0007 (new)
- **Model:** main session (Opus): rasterizer internals and editor render paths
- **PR:** (link when opened)

## Goal
A camera can carry filter components. The first, `Retro Console Filter`, makes it look like 3D on
the original PlayStation, wherever that camera's image is shown: the Game view (and Play mode), the
Camera Preview, F12 renders and camera sequences, plus the Scene view through a toggle.

## Scope
- In:
  - the `CameraFilter` base and `FilterStack`;
  - the `Retro Console Filter` component with the PS1 preset and its fields:
    - internal resolution and fit;
    - vertex snap;
    - affine textures;
    - texture filter, max texture size and mipmaps;
    - colour depth and dither;
    - fog;
  - the rasterizer's snap, affine and texture override;
  - the image pass and nearest upscale;
  - `Editor::render_camera` at every camera render path;
  - the path-traced output (resolution and image passes);
  - the Scene view toggle and `filters on|off`;
  - camera preview refresh;
  - tests, a stress section and docs.
- Out (task 0005): the N64 / Saturn / DOS presets, 3-point filtering, screen-door transparency, the
  palette.

## Acceptance criteria
1. With no filter component, every render path gives the same pixels as before.
2. PS1 preset, in the Game view: every colour channel is a multiple of 8 (15-bit); the image is made of
   whole blocks of the internal resolution; Fill keeps square pixels; Letterbox shows 4:3 with black bars.
3. The PS1 dither adds the console's 4×4 offsets (-4..+3) before truncating to 5 bits.
4. Vertex snap: rendered corners land on whole internal pixels. A camera move under half a pixel can
   leave the image unchanged; a larger one jumps it whole pixels.
5. Affine textures: on a slanted textured quad, the UV at a pixel is the screen-space (affine)
   interpolation, not the perspective-correct one.
6. Texture override: Nearest returns exact texels; Max Texture Size samples a mip level no bigger than
   the cap; Mipmaps off samples one level.
7. Fog blends geometry toward the fog colour by linear depth between start and end; the sky is left
   alone.
8. The component saves, loads, undoes and clones; the console sets its fields; choosing the PS1 preset
   fills in the fields.
9. F12 (raster) and camera sequences are filtered. A path-traced F12 is rendered at the internal
   resolution with the colour pass. The Camera Preview refreshes when a filter field changes.
10. `filters on` applies the main camera's filters to the Shaded Scene view; `filters off` restores it.
11. Stress: a `filters` section (cost of the PS1 filter at 1080p versus no filter) and a fuzz pass of
    odd values (NaN, 1×1, huge sizes, extreme snap) with no crash. Raster numbers are unchanged with
    no filter.

## Documentation to update
- CHANGELOG, README (feature paragraph, check count), ARCHITECTURE (rows), USABILITY, requirements
  (R-10), ADR 0007, this file.
