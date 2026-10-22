# 0164: no artifacts at distance for Toon materials

- **Status:** done (round 184)
- **Requirements:** R-03 (rendering), R-08 (the anime look), Q-02 (what the user sees is what the renderer means)
- **Decisions:** ADR 0048 amended (the maps filtered by their footprint; cel edges one pixel wide)
- **Model:** main session
- **PR:** #129 (stacked on #128)

## Goal
The user: "artifacts when backing away from a Toon material with painted maps". Every toon map (the hatching, the
face shadow map, the ramp), every Box or Generated mapping and the built-in Checker and Noise were read at level 0
whatever the distance, so a far surface showed the moire of texels smaller than its pixels; a cel edge fell on
whichever side the pixel centre landed, so a far band edge sparkled; an outline's shell shrank below a pixel and
the far silhouette went dotted. This task reads every map at the pixel's lod, widens a band's edge to the pixel's
span of lit, fades a procedural under a pixel to its mean, and keeps a new outline at one pixel.

## Scope
- In: `evaluate_material` - the hatch map at the lod of the UV derivatives scaled by the hatch scale, the face
  shadow map at its lod, a Generated mapping's derivatives from the pixel's footprint over the bounds, a Box
  projection's lod from the footprint times the tiling; the Checker and Noise fading to 0.5 once a cell is under a
  pixel (from the derivatives, or the footprint); `SurfacePoint::normal_dx / dy` (the deferred pass's forward
  differences of the shading normal, for toon materials) into `SurfaceSample::toon_n_dx / dy`, and `shade_toon`
  softening the band edges (and reading the ramp at a lod) by half the pixel's span of lit along the light. The GPU
  raster: the same edge from the neighbouring G-buffer normals, a Generated mapping's and a Box projection's lod
  from `dFdx`, the procedurals' fade. The Outline: `min_pixels` 1 for a new component, a loaded one without the
  field 0 (as it drew), a hysteresis of a third of a step on the 2 % shell steps.
- Out: the path tracers (CPU and GPU kernel) keep level 0 - their samples average the pixel, and the kernel's
  `Material` must not grow (ADR 0048); reversed-Z: the decision below.

## Reversed-Z, decided against for now
With a D32 depth buffer, near 0.3 m and far 1000 m, the post-projective depth z' = f (z - n) / (z (f - n)) has
dz'/dz = f n / (z² (f - n)) ≈ 3e-5 per metre at 100 m, and a float's step near z' ≈ 0.9997 is 6e-8: the depth
resolves about 2 mm at 100 m and 20 cm at 1000 m. An outline shell stands a centimetre or more off its surface,
so the hull and the surface are told apart out to a few hundred metres; the artifact the user saw at 60 m was the
shell under a pixel, not depth. Reversed-Z is recorded here as the next step if a scene ever needs the far half
of that range.

## Acceptance criteria
1. A hatched plane, a face-shadow sphere at softness 0 and a box-mapped cube seen from 20 m carry no more
   high-frequency energy (the mean absolute Laplacian over the object's interior pixels, found against the view
   without the object) than their 2 m pictures downsampled 10 x, within a factor of 2, on the CPU and on the card.
   (At 60 m a 1 m object is nine pixels wide and its silhouette swamps any measure: the far distance is 20 m.)
2. A two-tone sphere at 20 m passes the same measure (its cel edge is one antialiased pixel, not a sparkle).
3. An outlined sphere at 60 m - nine pixels wide - keeps a one-pixel ring (over a third of its pixels, about 27);
   a scene saved before the field loads its outline with Min Pixels 0.
4. The anime picture tests at 2 m pass unchanged (the render group).

## Results
Measured (the tests' own prints):

- High-frequency energy inside the object (the mean absolute Laplacian over its interior pixels) at 20 m against
  its 2 m picture downsampled ten times: the hatched plane 0.00 against 4.72, the box-mapped cube 12.3 against
  17.7, the face-shadow sphere at softness 0 24.4 against 13.2 (1.8 times: inside the factor of 2, the sphere's
  shading itself is most of it at 300 pixels), the two-tone sphere 2.8 against 4.3 on the CPU and 2.8 against 3.9 on
  the card. The hatched plane and the face-shadow sphere draw on the CPU in the card's views too (ADR 0048), so
  their numbers are the same on both.
- The two-tone sphere's band edge: at 2 m 848 of 48,714 pixels between the two tones (the edge's own pixels), at 20 m
  139 of 295 - the edge one antialiased pixel wide, where a hard edge gives none.
- The outlined sphere at 60 m, nine pixels wide: 27 outline pixels around its 48 (2,284 at 2 m); a saved Outline
  keeps its Min Pixels through a round trip (3), a scene saved before the field loads it as 0.

## Tests
- "toon at a distance: a hatched plane, a face-shadow sphere ... and a box-mapped cube ..." (tests/test_render.cpp).
- "toon at a distance: a two-tone sphere seen from 20 m carries no more high-frequency energy inside it ..." (tests/test_render.cpp).
- "outlines at a distance: ..." (tests/test_render.cpp).
