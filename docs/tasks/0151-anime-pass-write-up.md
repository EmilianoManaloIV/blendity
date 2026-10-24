# 0151: The anime pass - write-up

- **Status:** done (round 173)
- **Requirements:** R-03, R-04, R-08, R-13
- **Decisions:** ADR 0048 (anime rendering), ADR 0049 (hair and soft tissue), ADR 0050 (area lights), ADR 0017
  (amended: the cover plane)
- **Model:** main session
- **PR:** #118 (stacked on #117)

## Goal
The anime pass (tasks 0145-0155, rounds 164-172) written up in one place: what was built, where it lives, what
the research behind it says, and how to use it - so the next pass starts from the documents, not the diff.

## What the pass built (by task)
| Task | Round | PR | What |
|---|---|---|---|
| 0155 | 164 | #111 | Area lights as emissive planes: physical falloff from every point of the surface, Range a window (ADR 0050) |
| 0145 | 164 | #111 | Collapsible groups in the Inspector (`Reflector::group`); the camera's composition guides folded |
| 0146 | 164 | #111 | Overlay layers in the Scene view (bones, rig controls, icons, points, springs, contacts, outline, grid, gizmos, stats); `overlay` |
| 0154 | 164 | #111 | "Anime" and "Manga" as single camera filters |
| 0147 | 165 | #111 | Multi-edit materials: the Materials window, the Project window and the Inspector edit every selected material where the property applies; `matset` |
| 0148 | 166-167 | #112 | Toon styles: five presets and the painted controls (shadow bias, specular, rim, shade map, toon ramp, light wrap, key light only); `toonstyle` (ADR 0048) |
| 0149 | 168 | #113 | The 2D look: custom normals, Smooth Normals / Normal Transfer, Anime Face Normals, the face shadow map, line and hatch maps, the outline's painted width and distance fade, hold frames on 2s / 3s; `customnormals`, `facenormals` |
| 0150 | 169 | #114 | Procedural anime textures: nine flat-toned generators, their presets, cards that cover only their shapes, Alpha From Cover; `animemat` |
| 0152 | 170 | #115 | The import preview with switchable shading; `import <file> <shading>`; Preferences > Import |
| 0153 | 171-172 | #116, #117 | Hair and soft tissue on Spring Bones: Bend, Root To Tip, Wind, colliders fitted to the skin, Separate; the Soft label, the Jiggle modifier, Rig Hair from Mesh; `hair rig` (ADR 0049) |

## Where it lives
- Shading: `src/render/shading.cpp` (`shade_toon`, `ToonInputs`), mirrored in `gpu_raster.cpp` and `gpu_kernel.h`
  (the kernel's `Material` packed into `tex3` bits and `color2.w`: it sits at SPIR-V's id bound), camera rays in
  `pathtracer.cpp`; the fields in `scene/material.h` (the "Toon" group).
- Normals and the outline: `scene/mesh.h` (`custom_normals`), `scene/scene.cpp` (the normal modifiers, the hull);
  hold frames in `scene/animation.h` and the Sequencer.
- Textures: `texture/texture_layers.cpp` (the generators, `voronoi_cells`), `procedural_texture.h` (the cover
  plane); presets in `scene/material.cpp`.
- UI: `editor/panels.cpp` (groups), `editor/scene_view.cpp` (overlays), `editor/material_window.cpp` (multi-edit),
  `editor/assets.cpp` (the import preview), `editor/spring_ui.cpp` and `editor/rigging_window.cpp` (springs, the
  hair rig).
- Physics: `scene/spring_bones.cpp` (the chain solver's constraints, the wind, the fitted radii, separation, the
  soft tissue, the Jiggle modifier), `editor/editor.cpp` (`rig_hair_from_mesh`).
- Lessons (the Learn tab): Inspector groups, Overlays, Multi-edit materials, Toon styles, The 2D look, Anime
  backgrounds, Importing: preview the look, Spring bones, Strands, Soft tissue, Rig hair from its mesh.

## The research, and what was taken from it
- Arc System Works, "GuiltyGearXrd's Art Style: The X Factor Between 2D and 3D" (GDC 2015): edited normals,
  per-vertex shadow and specular control painted by artists, an inner-line texture, animation on hold frames. Taken:
  custom normals and the normal modifiers, the shadow bias and specular maps, the line map, hold frames; the
  per-vertex outline width.
- HoYoverse's face shading (Genshin Impact and after): a signed-distance "face shadow map" swept by the light's
  angle round the head. Taken: the face shadow map and its generator.
- VRM's MToon (shade texture, rim, outline) and MMD's toon ramps: imported into the same fields.
- Pixar, "Procedural stylized patterns" (Elemental, 2023): flat-toned procedural textures with a line. Taken: the
  nine generators' two or three tones and a line, periodic, with cards that cover only their shapes.
- Jakobsen 2001 (verlet with constraints by projection), Kugelstadt and Schoemer 2016 (position-based Cosserat
  rods: the bend holding a rest shape, simplified), Macklin 2016 / 2019 (XPBD, small substeps), Daviet 2023 (strand
  contacts: a grid), Akyuerek and Sahillioglu 2025 (spring-decomposed skinning: the Jiggle modifier at its simplest),
  games' jiggle bones (a spring-damper on a bone tip driven by acceleration, a counter-scale for volume).
- Baum, Rushmeier and Winget 1989 (the exact irradiance of a Lambertian polygon by its contour integral): the area
  light's rasterized shading; Karis 2013 (the representative point) for its specular.

## How to use it (the short path)
1. Import a character: the Import Preview shows it turning; pick Guilty Gear, Soft Anime or another style, Outline
   on, Import.
2. Rigging window: Find Spring Chains ticks hair, ears, tails, cloth and soft tissue by name; Apply. Hair as
   cards without bones: Rig Hair from Mesh.
3. Anime Face Normals on the head; a Face Shadow map (Textures window > Face Shadow) in the face material.
4. Backgrounds: `animemat Anime Leaves` (and the other presets) on cards and walls.
5. The camera: Looks > Anime or Manga, one filter each; Animation hold 2 for the character.
