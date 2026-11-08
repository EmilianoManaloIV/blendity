# ADR 0108: Approximations on the card instead of refusals - the Scene view stays on the GPU for a Unity world

## Status
Accepted (round 414, task 0390). Builds on ADR 0036 (the views on the GPU, the CPU the reference), ADR 0038 (textures
on the card the CPU's way), ADR 0061 (the see-through pass) and ADR 0105 (the budget, the honest CPU badge). Amends
ADR 0036's rule that the card draws a view only when it can draw everything in it exactly.

## Context
Since task 0096 the rule has been: the card draws a view when it can draw everything in it as the CPU does, else the
whole view goes back to the CPU with the reason (Preferences, the badge since round 411). That kept the GPU views
honest - a test compares them with the CPU to within 1 / 255 - but one material sends a whole scene to the CPU. Round
411's baseline measured it on the Unity-world stand-in and on the user's own VRChat world: SideRail's Scene view was
drawn by the CPU for one see-through toon material among 600 parts (25 ms a frame on the card and on the CPU alike -
the card never drew it), and the Game view with its Bloom was refused for "a picture kept in linear light with
see-through surfaces". A VRChat world has these in every corner: transparent lilToon and Poiyomi materials (see-through
toon), glass, cloth with sheen, a fog volume, 4K textures past the card's buffers. The rule meant the card drew the
starter scene and refused the real ones.

What the CPU does for each refused case is known exactly (`raster.cpp`): a see-through toon pixel is lit by the toon
branch with the pixel's own normal and, in the see-through pass, no neighbour normals (`normal_dx` unset) and no
painted maps beyond what `evaluate_material` read; a Volume dims what is behind by the view ray's length through the
mesh's bounds toward its colour lit by the sky and the suns; the Principled extras (round 330) add a subsurface wrap, a
sheen lobe and a coat lobe to the base; a see-through pixel over a picture kept in linear light marks its linear light
"none" so the bloom decodes the 8-bit picture there. None of these needs the view on the CPU: three can be drawn on the
card nearly, one exactly.

## Decision
1. **A material the card can't draw exactly is drawn approximately and said so, per view.** `material_unsupported`
   (gpu_views.cpp) no longer refuses a see-through toon material, a Volume or the Principled extras;
   `approximation_of` names what the card does differently and `gpu_view_draw` collects the names per view
   (`gpu_slot_approx_`, shown by `gpu_view_status` as "approximated: ...", by `viewport status`, and by the Profiler's
   GPU block as "<view> approximated on the card", with a count of the instances `FrameProfile::gpu_approximated`).
   The badge stays what round 411 made it: it shows when a view is on the CPU, not for an approximation.
2. **The approximations, each the CPU's formula folded into what the card has:**
   - *A see-through toon material* goes through the see-through pass as Transparent or Glass with the toon branch of
     `light_pixel`; its band edges come from `toon_neighbour`, which finds other objects in the g-buffer at a
     see-through pixel and gives none (the CPU's see-through pass has none either); its painted toon maps aren't in the
     toon planes (those hold the g-buffer's winners), so `F_TOONMAP` is masked in the see-through shader rather than
     read from another object's pixel.
   - *A Volume* is a tinted shell (`approximate_volume`): unlit, Transparent, its colour the CPU's fog colour (the
     volume colour lit by the sky from every side and each sun's forward term at g = 0, plus its emission), its alpha
     1 - exp(-density x the mesh's mean thickness through its bounds), its front faces - or its back faces when the eye
     is inside its bounds, as the CPU's; the CPU dims by the view ray's real length, cut where something opaque is
     drawn.
   - *The Principled extras* (`approximate_extras`): Subsurface tints the base toward its colour by its weight (the
     wrap left out); Sheen lightens the base by a quarter of its tint, its roughness and the metallic share (the grazing
     Fresnel averaged); Coat adds its Fresnel's share to the specular, moves the roughness halfway toward its own and
     dims and tints the base under it; Anisotropic is ignored.
3. **See-through surfaces over a picture kept in linear light** are exact: a third variant of the see-through fragment
   shader (`BL_SEE_HDR`) draws the triangles inside the linear-light pass, after the full-screen lighting, writing the
   "none" mark the CPU writes (`no_hdr`), so `read_linear_light` keeps the see-through pass's 8-bit colour there and the
   bloom decodes it as on the CPU. Without the pipeline such a view stays the CPU's (`can_draw_see_through_hdr`).
4. **Textures are capped on the card, never refused for their size:** a material's texture over 2048 a side
   (`kCardMaxTexture`, Unity's default import size) goes up from its first mip level within 2048 (`texture_capped`:
   the finer levels are read only closer than one texel a pixel - seen so, the card's picture is softer); a texture the
   card's storage buffers can't hold goes up at 64 x 64 (straight to 64, not the largest level that fits: once the
   buffers are full every later texture stays small until the others are forgotten - a chosen simplicity). Both are
   noted as approximations. Lightmaps, the world's map and the reflection probes' maps go up as they are (their level
   counts are the CPU's). The texture being painted is never capped: a stroke writes its first level only, so a capped
   copy would come from stale mips, and its rows go up as they are painted; a capped texture painted by other means
   goes up again whole at the next frame rather than row by row.
5. **What still sends a view to the CPU**, each said in the status: a toon axis material, a live procedural texture seen
   close up, a Camera Flatten, a camera filter's rasterizer stage, a probe volume of more than 262,144 probes, a
   texture that fails even at 64 x 64, Edit Mode's face highlight or lightmaps on a card without geometry shaders, a
   see-through view on a card without dual-source blending, a GPU error.
6. **F12 stays exact.** The approximations are the views'; the renders use the CPU's formulas (or the path tracer), as
   ADR 0036 made them the reference. A test compares each approximated kind with the CPU over the object's own pixels
   to a stated distance (a see-through toon veil within 16 / 255 on average, the extras within 24 / 255, a fog cube
   within 48 / 255 - its shell's mean thickness against the CPU's ray length - the capped texture within 6 / 255 where
   minified; the bloom-over-glass view within 1.5 / 255 over the whole view and over the glass's own pixels, as exact
   views are held).

## Consequences
- A VRChat world's Scene view and Game view draw on the card: the stand-in's with 0 refusals (its 6 see-through toon
  materials, the cloth's sheen and the glass over the camera's Bloom all approximated), and SideRail's, measured in the
  spec. The card's frame then costs what round 415 looks at (the CPU's share of a card frame).
- The GPU views are no longer pixel-exact for every scene: the status, the Profiler and the lesson say which objects are
  approximated and how. The honest comparison tests keep their 1 / 255 bound for the exact kinds and state the bound
  for each approximated kind.
- A 4K texture seen closer than one texel a pixel is softer on the card than on the CPU; Unity shows the same 2048
  unless the import size is raised, so a creator comparing with Unity sees Unity's.
- Not done, left to later rounds: the texture arenas as a descriptor array (no per-arena cap), probes coarsened to the
  cap, toon axis materials and live procedurals baked to textures per version, lilToon's own formula on the card
  (round 416), MatCap / backlight, a Volume's real ray length (a ray march through the bounds in the see-through
  shader).
