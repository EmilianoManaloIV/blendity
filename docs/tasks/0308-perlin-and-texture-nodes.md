# Task 0308 - Perlin Noise and Blender's richer texture nodes (round 328) - done

The user asked on 2026-10-29: "Make sure to add perlin noise!" (with the shader-graph request, task 0307).

## What changes
- `PerlinNoiseLayer` ("Perlin Noise"): `periodic_perlin` - improved gradient noise on a lattice wrapping every
  `period` cells, 0 at lattice points; octaves at whole-number lacunarity so each lattice still wraps; Distortion by a
  second periodic noise; Turbulence |n|; Offset; Seed; two colours. Preset "Perlin Noise" (Basics).
- Noise: Type (Smooth as before, Perlin fBm, Multifractal, Ridged, Hybrid, Hetero Terrain - `musgrave`), Lacunarity,
  Offset, Gain - appended, so old files read as before.
- Voronoi: Feature (Cells as before, F1, F2, Smooth F1, Distance to Edge, N-Sphere Radius) and Distance (Euclidean,
  Manhattan, Chebyshev, Minkowski, Exponent) - `voronoi_features` over a 5 x 5 periodic neighbourhood.
- Wave: Type (Bands, Rings round the tile's corners), Profile (Sine, Saw, Triangle), Phase, Detail.
- Magic (a torus-mapped Blender magic), White Noise (a value or colour per cell), Gabor (sparse Gabor kernels on a
  periodic grid; Anisotropy, Orientation, Frequency).
- `texture_perlin` and `texture_voronoi_features` for tests.

## Checks (tests_round328)
- Perlin 0 at lattice points, within -1..1, periodic; the layer the same twice, its seam no rougher than inside,
  Turbulence different and varying.
- Twenty type/option cases equal themselves one tile over and vary; F2 >= F1, Distance to Edge >= 0 and near 0;
  1024 px times printed.

## The review's fixes
Smooth F1 by a log-sum-exp from F1; Distance to Edge Euclidean throughout; Hybrid / Hetero / Ridged rescaled; octaves
capped before a lattice too fine for a float; the preset setter counts misses (none); Marble's distortion in range;
Brushed Metal from Gabor; no Mapping on Fill, Mesh Map, Face Shadow, Toon Ramp; Mapping's fields named "Map ...".
