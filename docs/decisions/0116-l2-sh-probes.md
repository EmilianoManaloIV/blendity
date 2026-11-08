# ADR 0116: Light probes as L2 spherical harmonics, and Unity's probes over their tetrahedra

- **Status:** accepted (round 442, task 0418)
- **Builds on:** ADR 0012 (probe volumes), ADR 0098 (Unity's lightmaps), ADR 0113 (the bake's quality target)
- **Requirements:** R-03 (rendering), R-08 (VRChat worlds and avatars)

## Context
Blendity's probes kept an ambient cube - six directions of light from geometry and six sky shares. That is exact along
the axes and blurred between them: a lamp at 45 degrees lit a probe-lit surface facing it at 0.58 of what it should.
Unity keeps its light probes as L2 spherical harmonics (nine coefficients per colour), and interpolates a Light Probe
Group's probes over their tetrahedra; Blendity took Unity's probes along six axes and spread them to a grid from the
nearest four.

## Decision
1. **L2 SH per probe** (`ProbeSH`, src/render/probe_sh.h), stored already convolved with the cosine lobe (Ramamoorthi
   and Hanrahan 2001: bands x A_l / pi = 1, 2/3, 1/4), so `eval(n)` is in the ambient cube's units: irradiance / pi from
   radiance, a lamp's colour x N.L. Clamped at 0. `ProbeVolumeData::sh`, baked from the same rays and lamps as the
   cubes; when present it replaces the cube's light from geometry for baked shading. The sky shares stay the cube's (the
   sky relights without a re-bake, ADR 0012), and the live probes (voxel GI) stay cubes.
2. **Files:** `BLPROBE2` adds the SH after the cubes; `BLPROBE1` files still load (cubes only, as before).
3. **The card:** a fourth header row in the probe buffer (`pv[3].x`, the SH's start, 0 with none), seven rows a probe;
   `probe_cube` evaluates them for baked shading, as the CPU.
4. **Unity's probes** (the `.unity-lighting` file's version 3): Unity's editor script evaluates each probe's
   `SphericalHarmonicsL2` along 26 directions (the cube's faces, edges and corners, `probe_fit_directions`); Blendity
   fits nine coefficients per colour by least squares (exact for an L2 function), in its own axes (the directions
   turned by the scene's placement), with no convention of Unity's to reproduce.
5. **Unity's interpolation:** the probes' positions (its Light Probe Groups') are tetrahedralised (Bowyer-Watson,
   `Tetrahedra`), and each probe of Blendity's volume over them takes its tetrahedron's four by barycentric weights, as
   Unity's renderers do; outside their hull, the nearest four as before.

## Consequences
- 108 bytes more a baked probe (a 200,000-probe volume: 21 MB), and seven rows more a probe on the card.
- L2 rings: a lamp leaves 1/16 of its light behind a surface facing away (Unity's probes do the same); no windowing.
- Light Probe Groups aren't objects in Blendity: their probes arrive with Unity's lighting file; moving them needs Unity.
- Tetrahedralising is O(n^2) in the points (capped at 20,000; past that, the nearest four).

## Superseded in part (round 453, 2026-11-08)
Decision 1's "the sky shares stay the cube's" no longer holds for baked probes:
- **The sky lives in the SH.** `probe_bake`, given the world (`Environment`), adds the world's radiance in the direction
  of every ray that leaves the scene to the probe's SH (`ProbeVolumeData::sky_in_sh`), so a gradient or HDRI sky has a
  direction in a probe, as Unity's probes hold the environment. Baked shading then takes no whole sky share from the
  cube (it would count twice); the cubes keep their shares for the live probes and the dots. A caller that gives no
  world (the Light Volume bake, which reads only the cubes) leaves the SH without the sky and `sky_in_sh` false.
- **The live relight is kept (ADR 0012).** The probes store the world's irradiance / pi at the six axes they were baked
  under (`has_baked_sky`, `baked_sky`). Shading adds `sky_change(now, n)` x the cube's sky share: the n-squared blend of
  (the world's irradiance now at each axis - the baked one there), both sides blends of the same six axes, so an
  unchanged sky adds exactly nothing; the result is clamped at 0 (a darker sky cannot take more than the SH hold).
  `sample` takes the current `Environment` for it; without one the SH stay as baked.
- **Known error:** the weight is the cube's sky-visibility share, not the SH's direction, and the six-axis blend is the
  ambient cube's own approximation: a 2.5x brighter Gradient came within about 10 % of a re-bake on the axes (up 2.36
  against 2.14, side 0.82 against 0.92) and 15 % off them (1.35 against 1.60 at (0.6, 0.5, 0.62)); it is never negative. The miss grows with the sky's top-to-side contrast and with the size of
  the change (about 30 % for a 2.5x change of a sky with a black horizon); an unchanged sky is exact, and a re-bake
  removes it.
- **File format:** after the SH, one byte (`sky_in_sh`), then - only with a baked sky - 72 bytes (six `Vec3`). Files
  that end before the byte (before round 453) or at it load with no baked sky.
- **The card:** `pv[3].y` the flag, `pv[3].z` where the six baked-sky vec4 start (after the SH rows, 0 none);
  `probe_sample` evaluates the same like-for-like change with the shader's `irradiance` at the three axes facing n.
