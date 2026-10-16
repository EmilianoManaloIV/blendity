# 0101: Textures on the GPU (real scenes)

- **Status:** done
- **Requirements:** R-05, Q-01
- **Decisions:** ADR 0038 (new): one texture-sampling contract for the CPU and GPU views
- **Model:** main session (Opus)
- **PR:** (link when opened)

## Goal
A view whose objects have image textures, a Checker or Noise procedural, Box or Generated mapping, normal maps or
several materials on one mesh should draw on the card, as plain materials have since task 0096. It should look as the
CPU draws it.

## Scope
- In: the G-buffer pass evaluates the material per pixel (ADR 0038):
  - UV, Box and Generated mapping, tiling and offset;
  - base colour (tinted), metallic, roughness and emission maps, and a tangent-space normal map with its strength;
  - the Checker and Noise procedurals;
  - the CPU's mip chain, filters (Closest, Linear, Trilinear, 3-point) and wraps, with its level of detail.
- In: the mesh's UVs and tangents sent with its vertices; the textures kept on the card while drawn.
- In: several materials on one mesh, drawn as one part per material.
- Out (still the CPU, reason shown): axis materials, live procedural textures, see-through and cutout materials, toon
  (task 0102), Sky / HDRI worlds (task 0102), affine UVs.

## Acceptance criteria
1. Per feature, the Scene view on the card matches the CPU's within ADR 0013's bar: an image texture on UVs (mips
   at a grazing angle), Box and Generated mapping, a normal map, metallic / roughness / emission maps, Checker and
   Noise, Closest and Linear filtering with Mirror and Clip wraps, several materials on one mesh. (The 3-point filter
   is ported too, but only a camera filter uses it, and camera filters stay on the CPU.)
2. The editor's textured test scene (task 0098's stress row, with a Gradient world) draws on the card: no fallback.
3. An Edit Mode drag on a textured mesh still sends only what moved.
4. The suite, verify mode and the stress problem counts are unchanged.

## Risks and edge cases
- A mesh without UVs (UV mapping reads (0, 0), as the CPU), without tangents (no normal map), NaN UVs.
- A texture larger than the card's storage-buffer limit: the view falls back to the CPU, with the reason.
- Many textures: the arena grows and is compacted on the card.
- Tangents worked out again during a drag (a normal-mapped mesh): the whole mesh goes up.

## Documentation to update
- CHANGELOG, README count, PERFORMANCE pass 11, ARCHITECTURE row, ADR 0038, USABILITY row.

## As built
- Round 126: 11 features match the CPU on the object's own pixels (mean 0.15-0.69 / 255, 99.1-100 % within 3);
  an axis material, a cutout and a live procedural texture still send the view to the CPU with their reasons; the
  card's texture store keeps the right texels through growth and compaction; a textured mesh's drag still sends only
  what moved.
- The textured editor scene (a Checker on the icospheres, Gradient world), laptop setting: 18.0 ms on the CPU, 10.4
  ms on the card with all 33 views there (before: all on the CPU). Under 1 ms of GPU time per view.
- Costs: vertices are 48 bytes on the card (UVs and tangents); a mesh with several materials, or with a normal map,
  sends the whole mesh during an Edit Mode drag.
- The Opus review's findings fixed (CHANGELOG round 126).
