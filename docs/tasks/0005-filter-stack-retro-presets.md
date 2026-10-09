# 0005: The Camera Filters stack, with Retro Console presets for PS1, N64, Saturn and DOS

- **Status:** done (merged as PR #6)
- **Requirements:** R-10
- **Decisions:** ADR 0008 (new), ADR 0007
- **Model:** main session (Opus): reflection, Inspector UI, rasterizer additions
- **PR:** (link when opened)

## Goal
A camera has one **Camera Filters** component holding a list of effects, added from a menu grouped by
category. The first category, **Retro Console**, offers the PS1 look of task 0004 plus N64, Sega Saturn
and DOS PC presets.

## Scope
- In:
  - the `CameraFilters` component, the `FilterEffect` base and its registry (name, category, help);
  - `Reflector::filter_effects` (files, undo, hash, `set`) and the Inspector stack UI (foldouts,
    toggles, per-effect menu, Add Filter by category);
  - console commands `filter add <name>`, `filter remove <i>`, `filter move <i> <j>`, `filter list`;
  - migration of saved `Retro Console Filter` components;
  - the `Retro Console` effect with the four presets, and what they need:
    - `TexFilter::ThreePoint`, the N64's 3-sample bilinear;
    - screen-door (checkerboard) transparency in `draw_see_through` (Saturn);
    - a fixed 256-colour palette with ordered dither (DOS), its nearest colour computed exactly (a
      32x32x32 lookup table first used here got 14 palette colours wrong; the tests caught it);
    - colour depth options Full / 15-bit / 256 colours;
  - tests, the stress section updated, docs.
- Out:
  - the Color / Lens / Stylize effects (task 0006);
  - Bloom and the HDR target (task 0007);
  - local volumes.

## Acceptance criteria
1. Add Filter lists effects under their categories. Effects can be added, toggled, reordered, reset and
   removed from the Inspector (real-event test) and from the console.
2. The stack saves, loads, undoes, clones and hashes:
   - a field change refreshes the Camera Preview;
   - `set CameraFilters.E0Width 160` works;
   - a scene with the old `Retro Console Filter` component loads into a stack with an equivalent
     effect.
3. With an empty stack (or all effects off), every image matches an unfiltered camera.
4. Presets:
   - **PS1** is unchanged from task 0004 (same pixels).
   - **N64:** 320x240; no snap; perspective-correct textures; 3-point filtering, which differs from
     bilinear (exact texel values in a test); 64 px texture cap with mipmaps; 15-bit colour with
     dither; fog on.
   - **Saturn:** 320x224; snap; affine textures; nearest; transparent materials drawn as a checkerboard
     (every other pixel, no blending); 15-bit colour.
   - **DOS:** 320x200; affine textures; nearest; every pixel one of the palette's 256 colours; ordered
     dither.
5. Several effects in order: resolution and raster settings combine as in task 0004 (later wins,
   smaller texture cap wins), and image passes run in list order.
6. Stress `filters`:
   - each preset's cost;
   - the fuzz pass extended to random stacks (random effects, orders, odd values) with no crash.
7. CI passes on all four jobs.

## Documentation to update
- CHANGELOG, README (feature paragraph), ARCHITECTURE (rows), ADR 0008, USABILITY, this file.
