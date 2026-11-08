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
