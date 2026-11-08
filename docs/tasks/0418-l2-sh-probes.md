# Task 0418: L2 SH probes and Light Probe Groups (round 442)

- **Status:** built
- **Requirements:** R-03 (rendering), R-08 (VRChat worlds and avatars)
- **Decisions:** ADR 0116 (new), ADR 0012 (probe volumes), ADR 0098 (Unity's lightmaps)
- **Model:** main session (Opus 5.5)
- **PR:** stacked on round 445

## Asked
Pass 18's plan, round 442: probes stored as L2 SH (optional deringing) in Blendity's bake and Unity's import; Light
Probe Groups interpolated tetrahedrally where a scene has them (the grid elsewhere); both renderers shade from them.

## Found
- The probes' ambient cube is exact on its axes and blurred between: a sun at 45 degrees lit a probe-lit surface facing
  it at 0.58 of N.L.
- Unity's probes came in as six axis values (`SphericalHarmonicsL2.Evaluate`), spread to Blendity's grid from the
  nearest four, not over their tetrahedra.

## Built
- `ProbeSH` (nine coefficients a colour, convolved: eval in the cube's units), baked beside the cubes, dilated, saved
  (`BLPROBE2`), shading baked probe light on the CPU and the card; the sky shares and the live probes stay cubes.
- Unity's lighting file version 3: each probe along 26 directions, fitted to L2 in Blendity's axes.
- `Tetrahedra`: Bowyer-Watson over Unity's probe positions (jittered by a millionth: a grid's co-spherical points),
  walking point location; a volume made from Unity's probes takes each point's tetrahedron by barycentric weights.

## Tests (tests_round442, lighting group)
- A lamp: 1.06 toward it, 0.094 sideways, 0.0625 behind (L2's ringing, as Unity's); light from all around: 1.000.
- The 26-direction fit: worst coefficient error 1.35e-06.
- A sun-lit probe volume, mean error against N.L over 200 normals: SH 0.023, the ambient cube 0.132.
- 160 points (a Light Probe Group's grid and 60 more): 752 tetrahedra, 400 of 400 points inside found, a linear field
  reproduced to 1.4e-06; a point outside found outside.
- Unity's probes written and read back as version 3; a volume from them: 1,296 of 4,096 probes inside their
  tetrahedra, light from above 0.800 where Unity's own is 0.800.

## Left open
- Deringing (a window over band 2) is not applied; negative lobes are clamped.
- Light Probe Groups as objects in Blendity (they arrive with Unity's lighting file); SideRail's file is read at
  version 2 until Use Unity's Lightmaps runs Unity again.
