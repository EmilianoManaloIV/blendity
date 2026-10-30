# Task 0311: a Volume (fog) material

- **Status:** done (round 331, 2026-10-30)
- **Asked:** "For material types there should be a volume like material that adds 'fog'." The user chose a true volume
  in the path tracer, with approximations in the views.
- **ADR:** 0092

## What
- Surface Type "Volume (Fog)" (`MaterialSurface::Volume = 4`): the surface invisible, the closed mesh's inside a
  medium. Fields after Blender's Principled Volume: Fog Density, Fog Color, Fog Anisotropy, Fog Emission and Strength,
  Fog Density Noise and Fog Noise Scale.
- Presets Fog, Smoke and Mist.
- The path tracer: delta tracking, Henyey-Greenstein scattering, the lights at each scatter seen through the fog,
  emission, shadow rays dimmed; a camera in a fog starts in it.
- The CPU views: the bounds' thickness cut by what is drawn behind, exp(-density x length) toward the fog's lit colour;
  from inside, the far side draws it.
- The GPU path kernel, OptiX and the GPU views fall back to the CPU and say why. MoonRay hides the mesh and says so.
- An open mesh warns above the fields.

## Acceptance
- exp(-1) through 1 m of black fog at density 1 in the path tracer, within 0.04; density 0 changes nothing.
- Anisotropy 0.8 brightens the fog toward the sun.
- From inside a fog room, the path tracer dims by the fog from the near plane.
- The CPU view dims what is behind a fog cube, and fogs the view from inside one.
- The GPU path and views fall back with a reason naming the Volume.
- The fields round-trip through a scene file under their own names.
- A Plane with a fog warns of 4 open edges; a Cube doesn't. MoonRay writes presence 0 and a note.

## The review's fixes
Shadow rays start in the path's fogs (a lamp in fog); fogs nest and add; the denoiser's guides for a scatter; the
GPUs' reason shown and the devices parked; fog picked in the views; the 64-face end; null collisions; lights sampled
as for surfaces; per-render and per-build caches; NaN and parallel-ray guards.

## Not done
- Volumes in the GPU path kernel, OptiX and the GPU views.
- Shadows and light shafts in the views (the path tracer only).
