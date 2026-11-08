# Task 0429: Probes see the sky, and pick lights (round 453)

- **Status:** done
- **Requirements:** R-03 (rendering), R-08 (VRChat worlds and avatars)
- **Decisions:** ADR 0113 (the bake's quality target); ADR 0116 amended ("Superseded in part (round 453)"); a trailing byte and floats in the probe file, old files load
- **Model:** implementer
- **PR:** (link when opened; stacked on round 452's task 0428 - the lightmapper hunks are adjacent)

## Goal
A probe's SH held only the light bounced from geometry; the sky came at shading time by a flat share of the sky the
probe could see (the cubes' `sky` weights), so a gradient or HDRI sky had no direction in a probe. Afterwards the bake
puts the sky's own radiance into the SH, and with many Baked point lights the probes pick among them as the
lightmapper's texels do (SideRail has 71).

## Scope
- In: `probe_bake` (`render/probe_volume.cpp`): rays that leave the scene add the world's radiance
  (`PathTracer::incoming_radiance`) in their direction to the SH; `ProbeVolumeData::sky_in_sh` (saved as a trailing
  byte; a file without it loads as false and keeps the cubes' sky share); `ProbeVolumeData::sample`, the card's probe
  shader (`pv[3].y`, `probe_pack`) and the dots overlay take no cube sky share when `sky_in_sh`; `src/render/light_pick.h`
  (`LightPick`, `light_pick_estimate`, `light_pick_keep`, `kLightPickOver`, `kLightPickRays`), used by the lightmapper's
  texel bake (round 441's code moved out) and `probe_bake`.
- Out: the guard courtyard's probe error against a reference and SideRail's probe bake time (measured by the chain);
  the live (Realtime GI) probes, whose cubes keep their sky share; specular.

## Likely files
- `src/render/light_pick.h` (new), `src/render/lightmapper.{h,cpp}` (`bake_texel`), `src/render/probe_volume.{h,cpp}`
- `src/editor/gpu_views.cpp` (`probe_pack`), `src/render/gpu_raster.cpp` (`probe_cube`), `src/editor/scene_overlays.cpp`
  (the dots): the one-line call-site updates for `sky_in_sh`, needed so the card, the dots and the CPU agree.
- `tests/test_lighting.cpp` (`tests_round453`), `src/editor/lessons.cpp` ("Probes see the sky")

## Done
- The sky term: each ray of the probe's even sphere spread that leaves the scene adds the world's radiance there to
  the SH (weight 4 pi / samples), `sky_in_sh = true` after a bake. Baked shading of a probe whose SH hold the sky takes
  no sky share from the cubes (CPU `sample`, the card's `probe_cube`, the dots).
- Light picking: `light_pick.h` holds round 441's estimate / keep / divide (Russian roulette, unbiased, about 8 shadow
  rays a point past 8 lights); `bake_texel` and `probe_bake` call it (a probe takes light from every side: no N.L).
- Probe files: one trailing byte, `sky_in_sh`; files from before it load and behave as before.

## Acceptance criteria
1. A probe in the open under a Gradient world: up and down irradiance within 6 % (up) / 0.02 + 10 % (down) of the world's
   integrated irradiance; `sky_in_sh` true; no second sky share (the baked sky passed to `sample` gives the world's irradiance, not twice); saved and
   read back with the flag; a file without the trailing byte loads with it false.
2. A sky changed after a bake (a brighter Gradient), shaded with no re-bake, is within 10 % + 0.02 of a re-bake under it,
   an unchanged sky adds nothing, and a probe without the baked sky takes no change.
3. Forty Baked point lights in one spot light a probe grid with the same mean SH as one light forty times as bright
   (within 5 %).
4. The existing lightmapper tests (picking: 40 lamps, rays and error) and the sky-colour relight test pass unchanged.
5. Both suites and the audits pass.

## Tests and checks to run
- `blendity_tests --group lighting` (filter "round 453"), both builds; the GPU group on a card (the shader change).

## Risks and edge cases
- **A sky-colour change must still relight baked probes without a re-bake** (the existing test "probe volumes: changing
  the sky colour relights probe-lit objects with no re-bake" pins it). With the sky in the SH, the probes store the
  world's irradiance at six axes (`has_baked_sky`, `baked_sky`: trailing floats in the file, six vec4 on the card at
  `pv[3].z`) and shading adds (sky now - baked sky) x the cube's sky share; an unchanged sky adds nothing. It is an
  approximation (the share is the cube's visibility, not the SH's direction): a brightened Gradient sky came within
  0.126 / 0.127 (down), 2.36 / 2.14 (up, +10 %) and 0.82 / 0.92 (side, -11 %) of the re-bake. Files without the floats
  keep the SH's sky as baked.
- Live (Realtime GI) probes still take their cubes' sky share.

## Measured
- Unit tests: up 0.858 0.699 0.540 against the truth 0.860 0.700 0.540, down 0.051 (0.053); forty lights / one light
  mean SH L0 ratio 0.997.
- Not measured: the chain's bake-quality section has no probe row (its courtyard's lightmap error 0.060, as before) and
  SideRail has no light probes (its probe phase 0.0 s), so the stations cannot show this round. They were measured on
  rounds 458, 452 and 453 together (task 0428's rows).

## Documentation
- CHANGELOG (round 453), README, ARCHITECTURE row (`light_pick.h`), USABILITY row, the Learn section "Probes see the
  sky".
