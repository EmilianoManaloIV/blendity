# 0155: Area lights as emissive planes

- **Status:** done (round 164)
- **Requirements:** R-03, R-05
- **Decisions:** ADR 0050
- **Model:** main session
- **PR:** (round 164)

## Goal
The user: "The area light is still treated like a point light. The area light needs to act like a planar mesh using
an emissive material that has varying intensity." Chosen: F12 path-traced renders and the lightmap bakes / probes,
with physical falloff. Afterwards an area light lights a surface the way a quad of an emissive material does: from
every point of its surface, with the inverse square of the distance from each, so a wide panel close to a floor
lights it evenly and a near surface is brighter than a far one.

## Scope
- In: `area_light_falloff` (facing cosine, inverse square clamped at 1 cm, the Range window) and
  `area_light_irradiance` (its exact integral, replacing `area_light_cosine` and `light_range_falloff`) in
  `src/render/raster.h/.cpp`; the CPU path tracer's next-event estimate and the GPU kernel's; the rasterizer's
  diffuse and specular strength; the lightmapper's unshadowed term and shadow-ray weights; the probes; the toon
  branches (the centre's falloff); MoonRay's export (the radiance as it is); the Light inspector's help; the lesson.
- Out: the GPU rasterizer (no area lights there, as before); a visible glowing quad; an emission texture across the
  panel; a spread angle; point and spot lights' falloff.

## Acceptance criteria
1. A 20 cm panel 1 m above a floor gives albedo x intensity x its area in the path tracer, 2 m up a quarter of it.
2. A 10 x 10 m panel 3.5 m up: the path tracer, the exact term and a 10 x 10 m quad of an emissive material of the
   same radiance agree on the floor within 3 %, under the centre and 3 m out.
3. The floor under a panel's edge is dimmer than under its centre and brighter than beyond it, without a step.
4. A surface 10 cm from a panel matches the exact term in the path tracer within 5 %; a millimetre from it both stay
   finite (the inverse square stops at 1 cm).
5. Range 4 m lights nothing at 4 m or beyond, and at 1 m is within 2 % of an unlimited light.
6. MoonRay's RectLight takes the panel's radiance (pi for intensity 1).
7. Task 0030's references (the exact term against a million-sample Monte Carlo, the rasterizer against the path
   tracer, the bake against the path tracer) hold with the new integrand.

## After the review
- The specular's strength and its lobe's widening both use the representative point's distance, capped at the
  hemisphere's worth (a glossy floor a hand under a wide panel flared past its diffuse); the zero-size fallback
  starts where the clamped area does (a 5 mm panel was a hundred times dimmer in the views than in F12); the toon
  branches of both path tracers take an area light's falloff at its centre, as the views do (per-sample values
  blurred the bands); MoonRay's notes tell point and spot lights (matched at 1 m) from area lights (their radiance
  as it is); the zero-size test checks a hand number and the "3 m out" case the exact term too.

## As built
- `src/render/raster.h` (`area_light_falloff`, `light_falloff`'s Area case), `raster.cpp` (`area_light_irradiance`,
  the forward and deferred lights), `pathtracer.cpp`, `gpu_kernel.h`, `lightmapper.cpp`, `probe_volume.cpp`,
  `moonray_export.cpp`, `src/scene/scene.cpp` (`Light::reflect` help), `src/editor/lessons.cpp`; tests: round 164
  and the references of rounds 44 and 52.
