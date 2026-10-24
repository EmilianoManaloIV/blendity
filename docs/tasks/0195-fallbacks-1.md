# 0195: fewer CPU fallbacks I - many lights, Solid shading, see-through and axis materials, Weight Paint

- **Status:** done (round 214)
- **Requirements:** the user's "the views should run on the GPU natively, like Blender's, with the CPU as the
  fallback" (2026-10-23)
- **Decisions:** ADR 0061 (fallbacks removed feature by feature)
- **Model:** main session
- **PR:** #159 (stacked on #158)

## What was there
With the views on the card, any of these sent a whole view back to the CPU for the frame (`gpu_views.cpp`):
- more than 16 lights (the lights lived in the frame's uniform block);
- a see-through material (Transparent or Glass);
- an axis material;
- Weight Paint's vertex colours;
- a transparent render's black / white pass;
- and, without a reason shown, the Scene view's Solid and Wireframe shading, which always drew on the CPU.

## What changed
- **Any number of lights:** the frame's lights are a storage buffer (binding 21), looped as the CPU's light loop.
- **Solid shading on the card:** the CPU's Solid is Gouraud - `shade()` per vertex (the editor's ambient by the
  normal's height, Lambert and a Blinn-Phong ^32 highlight per light, area lights from their whole surface), the
  colours interpolated and written without the view transform.
  - The card does the same in the g-buffer pass's vertex shader (`solid_shade`, in the light helpers now shared by
    every stage, `kLightFns`).
  - The colour goes through the emission plane flagged raw; the lighting pass writes it as it is.
  - A cutout is shaded per pixel, as the CPU's see-through pass shades it.
  - The Scene view's Solid and Wireframe draw through `gpu_view_draw` with `solid_items` (what `submit_scene` draws).
- **Weight Paint:** its colours are a per-vertex buffer (binding 22) read by the vertex shader in place of the
  object's colour.
- **See-through materials:** Renderer3D::draw_see_through's rules on the card.
  - The CPU sorts the see-through triangles back to front: clipped at the near plane, culled by their winding unless
    two-sided, keyed by mean depth (a stable sort). It sends their corners (binding 23).
  - After the lighting, the card draws them over the lit picture with dual-source blending (`src0 + dst x src1`).
    Transparent is `lerp(behind, lit, alpha)`; Glass is `behind x colour x (1 - F) + its reflection`. Both work in the
    display's 8-bit space, as the CPU blends, and the depth test is against the g-buffer's depth.
  - Then an ids pass of its own writes their ids and instances: Glass, and Transparent from alpha 0.3, as the CPU
    writes ids. So clicks pick them and the selection outline goes round them.
  - They cast no shadow, as on the CPU. Their maps are read at the first level, as the CPU's see-through pass reads them.
  - A card without dual-source blending, a see-through toon material and a screen-door camera filter still go to the
    CPU, saying why.
- **Axis materials (ADR 0018):** the parts are instances of their own in the instance table (never drawn). The parent
  carries its angle, blend, scale, noise and height band. The g-buffer pass blends the parts as `evaluate_any` does,
  each projected triplanar at the position over the axis scale (`evaluate_part`'s forced Box mapping). A toon axis
  material and a part with a live procedural still go to the CPU.
- The lighting pass's body is `light_pixel()`, shared by the see-through pass.
- A transparent render's black / white pass no longer refuses the card (a rasterized F12 still draws on the CPU, ADR
  0036).

## What stays on the CPU
- Live procedural textures, toon materials with painted maps or a hair highlight, see-through toon materials (task
  0196).
- Lightmaps, probes, realtime GI, camera filters, a picture kept in linear light (task 0197).

## Measured
- stress editor_render, this round's scene: glass, transparent and axis-material icospheres, one moved each frame, with
  the 50k-face sphere (RTX 4070 SUPER, headless, so every picture is read back):

  | Shading | Views on the CPU (all of them, before this round) | Views on the card | Views on the card / on the CPU |
  |---|---|---|---|
  | Shaded | 33.27 ms a frame | 27.07 ms | 33 / 0 |
  | Solid | 28.81 ms | 25.07 ms | 33 / 0 |

- The Scene view's own share:
  - Shaded: 14.14 ms on the CPU against 9.68 on the card.
  - Solid: 8.34 against 8.85. The CPU's Gouraud Solid is cheap, and headless the card's picture is copied back, which
    the presented window of task 0193 doesn't do.

## Checked
- Card against CPU (RTX 4070 SUPER, tests/test_gpu.cpp round 214):
  - 24 lights in Shaded, Solid and Wireframe: mean 0.19-0.22 / 255;
  - Weight Paint: 0.36 / 255;
  - Transparent, Glass and a textured Transparent sphere: 99.9 % of pixels within 3;
  - glass and faint panes in Shaded and Solid, and the glass pane selected: 100 %;
  - axis materials in World mode (band and noise) and Local mode: 99.96 % and 99.69 %.
- A click on the glass picks it, and through the faint pane the cube behind, on both devices.
- An Opus review found:
  - the outline missing round see-through objects over the sky (the outline pass now reads a "nothing here" value in
    the instances plane, not the depth);
  - derivatives taken after a discard;
  - an unknown surface value drawn opaque (the CPU draws it as Glass);
  - Solid building axis parts it then drops.

  All are fixed.
- Two differences remain:
  - The CPU draws an item's cutout triangles inside its sorted see-through pass, so a farther-sorted Transparent
    triangle in front of a cutout at some pixels is overwritten there on the CPU and shown on the card.
  - A see-through triangle cut by the near plane is sorted as one, by its first piece; the CPU sorts each piece.
- Windows 11,904 checks in verify mode (985 tests), Linux 11,493, sanitizers 11,369, all 0 failed; fuzz 6,000 frames
  clean. The gpu group's slow-open test failed under the parallel load and passed alone.
