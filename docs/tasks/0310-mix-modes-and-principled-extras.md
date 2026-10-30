# Task 0310 - Blender's Mix modes and the Principled BSDF's extras (round 330) - done

Part of the shader-graph request (task 0307).

## What changes
- `LayerBlend` gains Darken ... Value (appended: old indices hold); `texture_blend` / `blend_color` with Blender's
  ramp_blend formulas, the HSV modes per colour.
- `Material`: subsurface (+ colour, scale), sheen (+ tint, roughness), coat (+ roughness, IOR, tint), anisotropic (+
  rotation); reflected last in a folded group (PBR only, all in files).
- `SurfaceSample` carries them (`apply_principled_extras`); `brdf_eval` / `brdf_eval_parts` add Subsurface's wrap,
  Disney's sheen, a GGX coat dimming and tinting the base, Disney's anisotropic GGX - the CPU raster and path tracer.
- The GPU path kernel: the extras in a GMaterial-sized record after every material (P.misc.z: where they start;
  flags.w bit 4096), read into file-scope values at each hit; `extras_eval` mirrors the CPU's.
- The GPU views: `material_unsupported` sends a material with extras to the CPU, with the reason.

## Checks (tests_round330, tests_round330_blends)
Zero extras = the old BRDF; each lobe where it should be; a white furnace at most 0.70; GPU path tracer against the CPU;
the views' fallback; the blends against Blender; a coated sphere in the CPU view changes everywhere.

## The review's fixes
The OptiX PTX regenerated; the 8-wide raster shader skips extras; Subsurface an energy-conserving wrap on the lit side;
the coat sampled as a lobe (CPU and GPU); the Image layer blends colours; Color Dodge; the area-light seam; the lesson's
Try-its.
