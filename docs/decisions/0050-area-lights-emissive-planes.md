# 0050: Area lights are emissive planes

- **Status:** accepted (task 0155; the user asked, 2026-10-21: "The area light is still treated like a point light.
  The area light needs to act like a planar mesh using an emissive material that has varying intensity", and chose
  the path-traced renders and the lightmap bakes / probes as where it showed, and physical falloff as the meaning)
- **Date:** 2026-10-21
- **Requirements:** R-03 (rendering), R-05 (baked lighting)

## Context
Every renderer already sampled an area light's rectangle or disc (task 0030 gave the rasterizer its whole surface,
the path tracers pick a point on it per sample, the lightmapper shoots stratified shadow rays at it). What still made
it a point light was its brightness: Unity's range curve, `(1 - d / Range)^2`, taken from the light's **centre**, with
no inverse-square term from the points of the surface. A 10 m panel 3 m over a floor lit it like a lamp at its middle,
and a surface a hand's width from the panel was no brighter than one a metre away. A quad with an emissive material -
which the path tracers treat as a real light (`collect_mesh_lights`) - behaved differently from the light that is
meant to be one.

## Decision
- **The model.** An area light is a Lambertian emitter of uniform radiance over its rectangle or disc. What a point
  `p` receives is `E = ∫ L · cos θ_p · cos θ_light / r² dA` over the surface - exactly what a quad with an emissive
  material gives, so the two can stand in for each other.
- **The calibration.** `Intensity` is the panel's radiance over pi: `L = Intensity · π`, so a quad with an emissive
  material of strength `π · Intensity` gives the same light, and a bigger panel gives more (what the user asked for:
  "a planar mesh using an emissive material"; Unity HDRP's luminance convention, not Blender's power in watts, whose
  numbers run to thousands while Intensity runs 0 to 8). A 1 x 1 m panel two metres from a wall gives about what it
  gave before. MoonRay's export writes that radiance as it is, no longer scaled by the range curve.
- **Range is a window, not the falloff.** The light falls as the inverse square; `Range` fades it out to nothing at
  Range by Unreal's window `(1 - (r / Range)^4)^2`, so a light still ends where its Range says and a scene's lights
  don't leak through every wall. Point and spot lights keep Unity's curve (not asked for; their scenes stay as they
  are).
- **No nearer than 1 cm.** The inverse square is taken with `r² ≥ 0.0001`, so a surface touching the panel stays
  finite. A surface a centimetre or more away is exact (a plane touching an infinite panel gets pi x the radiance);
  nearer than that it gets less than it should, which no one sees. The path tracers draw the light's point
  uniformly over the panel, so a surface within a few centimetres of it is noisy (Cycles samples by solid angle to
  avoid that; a later task, if it matters).
- **One formula, every consumer.** `area_light_falloff(l, q, p)` (`raster.h`) is what the light's point `q` sends
  toward `p` besides N.L: the facing cosine, the clamped inverse square, the window. The path tracers' next-event
  estimate multiplies by it per sampled point (the GPU kernel the same way); `area_light_irradiance` integrates it
  over the surface exactly (the Gauss quadrature of task 0030 with the new integrand, the horizon clipped) for the
  rasterizer's diffuse, the lightmapper's unshadowed term and the probes; the toon branches take it at the centre
  (per-sample values would blur the bands), the specular's strength and lobe width at the representative point,
  capped at the hemisphere's worth. The lightmapper's shadow rays weigh each point of the light by its own falloff, so
  the near part of a wide light counts more (the ratio estimator keeps the noise down).
- **Still invisible.** The panel is not seen by camera rays and not drawn as a glowing quad (not asked for; Blender's
  area lights are invisible by default too). Nothing changes for the GPU rasterizer, which has no area-light code
  and hands such views to the CPU as before.

## Consequences
- Area-lit scenes change on purpose: a 1 x 1 m light 3 m from a surface gives about a quarter of what it gave (the
  range curve gave 0.49 of the full value at 3 m with Range 10; the inverse square gives 0.11), the same light 30 cm
  away about five times more, and a 10 x 10 m panel ten times what a 1 x 1 m one gives at the same distance (it gave
  the same). Raise or lower old scenes' area-light intensities to taste. Point and spot lights are untouched.
- The views, lightmaps, probes and both path tracers agree on one number (the tests check it), and an emissive quad
  of the same radiance renders the same floor.
- The window isn't exported to MoonRay (its lights fall off physically without one); a Range well beyond the scene
  makes the two match.
- Watch: the quadrature's point counts were tuned for `1/r²`; the new integrand is `1/r⁴`-shaped near a large
  light, and the tests of task 0030 (a 10 m light 30 cm above a floor, strips, discs) hold it within 1 % of a
  million-sample reference.
