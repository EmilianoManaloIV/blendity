# 0102: Sky / HDRI worlds and toon materials on the GPU

- **Status:** done
- **Requirements:** R-05, Q-01
- **Decisions:** none new (ADR 0036, ADR 0038's sampling contract for the world's map)
- **Model:** main session (Opus)
- **PR:** (link when opened)

## Goal
After task 0101, a view whose world is a Sky or an HDRI, or with a toon material or its outline, still drew on the CPU.
The default new scene often has a Sky. These should draw on the card, as the CPU draws them.

## Scope
- **In: Sky and HDRI worlds.** The world's equirectangular map goes into the card's texture store (ADR 0038, mips and
  all), and its nine spherical-harmonic irradiance coefficients go into the frame's constants. The lighting pass gets
  `Environment::radiance` (with `dir_to_equirect`'s polynomial atan2 / asin), `irradiance` (the SH, or the gradient
  without them) and `specular` (the map's level from the roughness). The background uses the world's radiance,
  through the view transform.
- **In: toon materials (ADR 0020).** `shade_toon` per light (`toon_level`'s bands, the shadow colour, the specular
  spot), and `light_finish`'s toon branch (the flat or normal ambient, the rim, no reflections). The G-buffer gains a
  plane with each pixel's instance, so the lighting pass reads its toon settings.
- **In: the toon outline's inverted hull.** Its back faces are always culled, whatever the view's setting.
- **Out:** realtime GI, probes and lightmaps; area lights; see-through materials.

## Acceptance criteria
1. A Sky world and an HDRI world each match the CPU's picture within ADR 0013's bar: the background, and objects lit
   by the map (diffuse from the SH; rough and glossy reflections).
2. Toon materials (one and several bands, the specular spot, a rim, flat ambient on and off, the shadow), and an
   outline, match the CPU's within the bar.
3. task 0098's textured scene with a Sky world draws on the card: no fallback.
4. The suite, verify mode and the stress problem counts are unchanged.

## Risks and edge cases
- A world map too large for the card: the view falls back to the CPU with the reason.
- The CPU fills smooth runs of sky by interpolating every fourth pixel; the card evaluates each pixel (a fraction of a
  display step apart).
- A Sky / HDRI world without its map or SH yet: the gradient, as the CPU.

## Documentation to update
- CHANGELOG, README count, PERFORMANCE pass 11, ARCHITECTURE row, USABILITY row.

## As built
- Round 127: a Sky and an HDRI world under rough and glossy spheres match the CPU (mean 0.18-0.27 / 255 on the
  object, 0.13-0.14 on the whole view); toon two-tone, three bands with a spot and a rim, flat ambient off, in a
  shadow, and an outline (culling on, off, mirrored) match (0.04-0.28 / 255).
- Criterion 3 is shown by `editor_render`'s textured Sky row: all 33 views on the card, 22.1 -> 9.9 ms on the laptop
  setting.
- The Opus review's findings fixed (CHANGELOG round 127). A world map too large for the card falls back as task
  0101's textures do (the same path), not tested separately.
