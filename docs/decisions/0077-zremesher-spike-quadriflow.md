# ADR 0077: a ZRemesher-class retopology through QuadriFlow (a spike)

- **Status:** accepted as a spike (task 0241, round 261): the library wired in behind a build flag, the operator, the
  measurements; what it does not do is written down
- **Relates to:** ADR 0076 (DynaMesh: even quads for form), ADR 0074 (Sculpt Mode), the dependency and licensing
  rules in CLAUDE.md (Blender's bundled sources, their licences and patches, build flags)

## Context
ZBrush's ZRemesher turns a sculpt into clean quads that follow the form - edge loops around the eyes and the mouth,
the polygon count the artist asks for. It is proprietary. Blender's Quad Remesh is QuadriFlow (Huang et al. 2018, BSD-3-Clause by its text -
Blender's README says MIT; `blender/extern/quadriflow`, patched by Blender to drop Boost), which builds a smooth 4-direction field on the
surface, finds its singularities and integrates a quad mesh from a position field - the same family of method as
Instant Meshes, without ZRemesher's guide curves and painted density. Blendity has it in the tree already, unused.

## Decision
- **QuadriFlow behind `BL_WITH_QUADRIFLOW`** (`meshops::quad_remesh`, `src/scene/quad_remesh.cpp`): the bundled
  sources compiled as part of the engine when Eigen is present (the only dependency; LEMON and pcg32 are bundled with
  it), the operator calling the `Parametrizer` directly as Blender's `quadriflow_capi.cpp` does (the input welded and
  triangulated, `NormalizeMesh`, `Initialize(target faces)`, the boundary constraints when asked, the orientation and
  position fields, `ComputeIndexMap`, the quads out), without Blender's allocator or progress callback (a cancel is a
  flag the editor's thread sets). Without the flag the command says so and does nothing.
- **Parameters, as Blender exposes them:** the target face count, Preserve Sharp, Preserve Boundary, Adaptive Scale, a
  seed; the mask and vertex groups carried by position (as DynaMesh carries them); groups, UVs, seams and shape keys
  dropped.
- **Off the main thread.** QuadriFlow takes seconds on a sculpt (Blender's runs it with a progress bar): the operator
  runs on a worker with the mesh copied first, the editor's frames go on, the result lands as one ordinary step when
  it is done - unless the mesh was changed meanwhile, in which case it is dropped with a log line.
- **What this is not:** ZRemesher's guide curves, painted polygroup density, symmetry and "keep groups" are out; the
  spike measures what QuadriFlow gives on the sculpt test meshes (quad share, time, the target count hit) and the
  write-up decides whether a later round adds guides (QuadriFlow's constraints can take a direction field; a curve
  drawn with the Sketch tool is a natural source) or leaves it here.

## Consequences
- The engine grows by QuadriFlow (about 7k lines of BSD-3-Clause code plus LEMON under the Boost Software License) on builds with Eigen; the
  licences join the third-party list.
- Remeshing a 24k-vertex sculpt to 5k faces is expected in seconds, not frames: the worker and the step-on-arrival
  are the shape every long operator will take from here (DynaMesh at 256 is a candidate).
- Determinism: the seed fixes the field's start; the result is the same on a machine for the same input.
- An open surface is refused before QuadriFlow runs: Blender lets one through (its boundary constraints exist for it),
  but QuadriFlow stalled without end on an open sheet in the test here, and a stall on the worker is a job that never
  lands. Preserve Boundary stays for a later round that lets open meshes through with a watchdog.
- QuadriFlow calls `exit(0)` on a few states it cannot recover from (a valence-2 singularity, a NaN in a solve): the
  input is checked first (manifold, consistently wound, no wire or zero-length edge, finite) and exceptions are caught
  on the worker, but an `exit` inside the library would still close the program - a residual risk recorded here,
  watched by the tests' bad-input cases.
