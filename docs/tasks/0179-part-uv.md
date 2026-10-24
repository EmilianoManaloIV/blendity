# 0179: unwrap by parts - PartUV's geometric core

- **Status:** done (round 195)
- **Requirements:** R-04 (UV mapping)
- **Decisions:** ADR 0057 (PartUV's recursion on a geometric part tree, ABF++ beside LSCM)
- **Model:** main session
- **PR:** #140 (stacked on #139)

## Goal
The user: a part-based unwrapper after PartUV (SIGGRAPH Asia 2025), a geometric core plus an opt-in PartField model
(task 0180). The roadmap's targets: on Blendity's own shapes (PartUV's "Common Shapes" idea), a fifth of Smart UV
Project's charts at distortion at most 1.3 and seams at most 0.6 x as long; every chart under tau; no overlaps; parts
following material slots on a 50k-face sphere in at most 2 s; a non-manifold edge split cleanly; undo restoring the
old UVs; `uv report` matching the test's own measure.

## Design
- `src/scene/uv_parts.cpp`: the part tree (Ward clustering over manifold edges, material slots last), the search
  (whole part, Normal heuristic 2..10, Merge heuristic by oriented-box labels, else the children; a part spanning
  material slots goes to its children), packing by part (or a UDIM tile per piece), `uv_report`.
- `src/scene/uv_abf.cpp`: ABF++ ported from Blender's `uv_parametrizer.cc` (the reduced Newton system, Eigen's
  SparseLU). `src/scene/uv_internal.h`: the chart topology (corner fans, triangles in one order) LSCM and ABF++ share;
  `lscm_chart` lays out given angles (each triangle rebuilt by the law of sines on its first side).
- LSCM screens every candidate; ABF++ runs on near misses (within 1.5 x tau) and is kept when better. The top-level
  parts are solved on threads.
- The editor: UV Editor > Part Unwrap, UV menu > Part Unwrap, Modeling Tools > Part Unwrap, console `uv parts [bound]
  [multi|single]` and `uv report`.

## Results
The stress section `uv` (distortion: mean / worst):

- cube + boss + torus (416 faces): Smart UV Project 22 charts, seams 32.56 m, distortion 1.05 / 1.17, 0.4 ms; Part Unwrap 6 charts, seams 16.90 m, distortion 1.13 / 1.22, 35.9 ms (3 parts, 42 tried, 1 ABF++).
- teapot (2.0k faces): Smart UV Project 58 charts, seams 42.94 m, distortion 1.08 / 1.20, 1.5 ms; Part Unwrap 11 charts, seams 15.74 m, distortion 1.13 / 1.18, 108.0 ms (4 parts, 101 tried, 2 ABF++).
- 50k-face sphere, 12 slots (50.2k faces): Smart UV Project 8 charts, seams 22.90 m, distortion 1.09 / 1.10, 40.5 ms; Part Unwrap 12 charts, seams 37.70 m, distortion 1.03 / 1.03, 265.9 ms (12 parts, 12 tried, 0 ABF++).

The roadmap's "a fifth of Smart UV Project's charts" holds where a mesh allows it (the teapot); on the cube, boss and torus six charts is the floor for face-set charts (two for each closed piece), a third of Smart UV Project's. Smart UV Project's distortion is a little lower (planar projections barely stretch) at many more charts; on the 12-slot sphere it ignores the slots, which Part Unwrap follows.

## Tests
tests/test_modeling.cpp: "uv parts: a cube with a cylinder boss and a torus ...", "uv parts: the charts follow
material slots ...", "uv abf: ABF++ closes every triangle at pi ...", "uv parts: the console's uv parts ...". The
metric table: stress `uv` ("Part Unwrap (task 0179)").
