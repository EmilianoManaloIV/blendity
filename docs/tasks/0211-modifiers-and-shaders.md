# 0211: Boolean and Array on big meshes; the GPU shaders compiled by the build

- **Status:** done (round 231)
- **Requirements:** R-02, R-07, Q-01; task 0203's rank 8, and the shaders' cold compile it folded in
- **Decisions:** none
- **Model:** main session
- **PR:** #177

## What was there
- **Boolean**, converting to Manifold's mesh, looked up a `std::map` keyed by (vertex, UV) for every corner. Converting
  back, it counted each source face's edges in a `std::map` and walked its boundary through hash maps, and it grouped
  the triangles by face in an `unordered_map`. Both ways passed 64-bit coordinates where the positions are floats.
  Blender passes 32-bit ones; Manifold computes in doubles either way.
- **Array** copied the whole mesh, then added each copy's vertices and faces one at a time.
- **The GPU shaders** compile with shaderc the first time a card opens after their source changes, and are cached.
  On this machine that took about 96 s for everything: the path tracer's two kernels 37 and 26 s, and the views'
  shaders about 33 s, during which the views draw on the CPU.

## What changed
- **Boolean:**
  - each vertex's property vertices are found through a short chain;
  - each face's triangles are grouped through a table indexed by face, and its boundary is found in sorted vectors;
  - 32-bit `MeshGL` both ways, as Blender passes it.

  The result is the old one, with two differences. Each rebuilt n-gon now starts at its smallest vertex; before, a
  hash map's order picked the start. And a face whose triangles have no boundary now comes back as its triangles; it
  used to come back as an empty face.
- **Array** appends each copy array by array: positions, corners, offsets, UVs, materials, smooth flags, seams and
  weights. It gives the same mesh as before, including meshes whose lists are out of step with their faces.
- **The shaders:**
  - `Blendity --warm-shaders <dir>` compiles the path tracer's kernels, and every shader a GPU view needs on the
    first card, into a new folder;
  - `build.bat` / `build.sh` run it after building the editor with Blender's libraries, into `shaders` beside it;
  - a shader not in the user's cache is read from there before it is compiled;
  - the Windows installer, the macOS bundle and the Linux packages carry the folder.

  A build without a card (CI's, the releases') makes none, and the first GPU view compiles them as before.

## Measured
| Row | Before | After | Blender 5.2.2 |
|---|---|---|---|
| Modifier: Boolean union of two 50k spheres | 276 ms | 158 ms | 79.8 ms |
| Modifier: Array x10 on the 50k sphere | 24.2 ms | 8.1 ms | 11.2 ms |
| The first GPU view after the shaders changed (this machine, an RTX 4070 SUPER) | about 96 s compiling | 0.3 s | - |

- Array is now ahead of Blender.
- **Boolean** stays 2x behind. Task 0203 timed Manifold's own work, building the two solids and the union, at about
  145 ms of the old 265, and that work is unchanged. Blender runs the same library in 80 ms all told, so its build or
  settings must differ in a way not yet found.
- **The shaders:** the 96 s is `--warm-shaders` into an empty folder, while the round-230 checks ran on this machine.
  The second run found every shader beside the program and compiled none. A user's own cache, once filled, already
  skipped the compile. What changes is the first open after a new build or install.

## Checked
- tests/test_modeling.cpp `tests_round231`:
  - Array gives the same mesh as adding each vertex and face, with UVs, materials, smooth flags, seams and weights;
  - so does a mesh whose UV, material and smooth lists, weights, seams and loose edges are out of step;
  - a union of two UV spheres is closed, the same twice, mostly quads, and most corners keep an input's UV.
- `--warm-shaders`: 21 shaders compiled into the build's folder; a second run finds them all, compiling none in
  0.3 s; a folder that isn't empty is refused and left alone.
- The modeling, editor and gpu groups; the `modifiers_ngon` stress section.
- Two Opus reviews:
  - Boolean's conversions: no defect. Array, for meshes whose lists are out of step with their faces, differed in
    four ways, all fixed: UVs gained, a loose edge dropped, seams left unsorted, and a vector read while appended to.
  - The build step found five things, all fixed:
    - `build.sh` stopped on the unset `BLENDITY_NO_WARM`, which would have failed every Linux and macOS build;
    - it ran before the libraries were beside the program;
    - the macOS bundle and the Linux packages left the folder out;
    - the dependency-free build, made after the full one, removed its shaders;
    - `--warm-shaders` cleared any folder it was given; now it takes only a new or empty one.
