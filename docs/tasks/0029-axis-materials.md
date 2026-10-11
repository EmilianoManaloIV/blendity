# 0029: Axis materials (grass on top, dirt on the sides, whatever the rotation)

- **Status:** done (as built below)
- **Requirements:** R-03
- **Decisions:** ADR 0018
- **Model:** main session (Opus)
- **PR:** (link when opened)

## Goal
The user asked for "procedural materials/textures that can take axis as a parameter (a material that will
always have grass on top followed by dirt regardless of rotation)". Afterwards a material can name a Top,
Side and Bottom material; surfaces facing up get Top, steep ones Side, downward ones Bottom, chosen by the
normal in world space (or the object's own), projected without UVs, in the Scene view, the Game view,
renders on the CPU and the GPU.

## Scope
- In:
  - **`Material` fields** (`src/scene/material.h`): `axis_mode` (Off / World / Local), `axis_top`,
    `axis_side`, `axis_bottom` (`.mat` asset paths), `axis_angle` (degrees, 45), `axis_blend` (0..1, 0.1),
    `axis_scale` (metres per tile, 1); in `reflect()` so files, undo, hashing and `set` see them; an
    Inspector foldout **Axis** in `draw_material_fields` with three material pickers (the Materials
    window's picker popup) and the numbers.
  - **`evaluate_material`** (`src/render/shading.cpp`): when `axis_mode` is on, the slope weights, then
    each sub-material with a weight above 1e-3 sampled **triplanar** at the world / local position
    (`sample_triplanar`, weights `|n|^4`), blended (albedo, roughness, metallic, emission linearly; normals
    by whiteout); a sub-material that is itself an axis material is used plainly.
  - **The rasterizer** resolves sub-materials (and their textures) once per flush with the parent.
  - **The CPU path tracer** through `evaluate_material`; **the GPU kernel** (`gpu_kernel.h`): the
    sub-materials appended to the material table and their indices in `flags.w` / `color2.w`, the same
    weights and triplanar sampling in GLSL.
  - **The render hash** mixes the sub-materials' pointers and versions (one level).
  - **Materials window preview** shows an axis material (and fixes its ignoring of procedurals, which the
    roadmap noted at `material_window.cpp:34`).
  - Console: `set Material.AxisMode World`, `set Material.AxisTop Assets/Materials/Grass.mat` work through
    the reflector.
- Out: baking an axis material to an image for export (task 0030); more than three bands (snow lines).

## Likely files
- `src/scene/material.h/.cpp`, `src/render/shading.h/.cpp`, `src/render/raster.cpp`, `src/render/pathtracer.cpp`
  (material table for the GPU), `src/render/gpu_kernel.h`, `src/editor/panels.cpp`, `src/editor/render_view.cpp`
  (hash), `src/editor/material_window.cpp`, `tests/test_main.cpp` (round 51), `stress/stress_main.cpp` (`shading`).

## Acceptance criteria
1. A cube with an axis material (Top green, Side brown, World) renders green on its top face and brown on
   its sides in the rasterizer, the CPU path tracer and (when a Vulkan GPU is present) the GPU path tracer.
2. Rotated 90° about X, the cube's face now pointing up is green and the one that was the top is brown.
3. Local mode: the same rotation keeps the green on the face that was the top.
4. `axis_blend` 0 gives a hard edge (a pixel row across the 45° slope of a ramp changes in one pixel);
   a larger blend spreads it.
5. A sub-material's texture shows without UVs (triplanar) and doesn't stretch on a sphere's sides.
6. The fields round-trip through a `.mat` and a scene file; undo restores an edit; editing a sub-material
   redraws the views.
7. Stress `shading`: an axis material costs under 1.6× a plain one per pixel at 1080p (two sub-materials
   in view).

## Tests and checks to run
- Unit: `BLENDITY_TEST_FILTER="axis material"`, then the full suite (Windows, Linux, sanitizer, verify
  mode). Stress: `shading`, `editor_fuzz`.

## Risks and edge cases
- A sub-material path that doesn't exist, points at the parent itself, or at another axis material.
- Normals that are NaN or zero: weights fall back to Side.
- The GPU table's indices: a sub-material used by several parents is appended once.

## Documentation to update
- ADR 0018, CHANGELOG, README (axis materials), ARCHITECTURE row, USABILITY row.

## As built
Built as specified: the `Material` fields and `Reflector::material_ref` (files, undo, hashing and `set` see the
paths as text; the Inspector draws a picker button and a clear button), the parts resolved with the parent in
`Material::textures()` (one thread, before shading), `evaluate_material`'s axis branch over a shared
`evaluate_surface(..., force_box)`, the GPU kernel's (parts appended to the material table once each,
`GMaterial::axis` / `axis_i`), the views' hash mixing the parts' versions, and the Materials window's
preview through `evaluate_material` (procedurals and axis materials now show). The F12 raster render and
the CPU / GPU path tracers agree (both RTX GPUs within 0.0006 of a 0.187 mean). Stress `shading`: an axis
material with two parts costs 1.51× a plain triplanar one per point (target under 1.6×).

**Deviations:**
1. **Normals blend as a weighted, renormalised sum**, not a whiteout blend: the parts' normals are already in
   world space here, where a whiteout blend (of tangent-space maps) doesn't apply.
2. **A part's normal map uses the mesh's UV tangents** (as the Box mapping always has), so it is approximate
   on the projected parts; their colours and roughness are exact.
3. **The Inspector's picker is not click-tested** (its popup is the Materials window's picker, already
   tested); the fields are tested through the material, its file and undo.
4. **The GPU parameters live in a new `GMaterial::axis` / `axis_i`** rather than the spare `flags.w` /
   `color2.w`: four numbers and three indices don't fit there.
5. Found and fixed on the way: on a hash-memo miss the object hash re-mixed only the materials' pointers and
   versions, not the full material key; it now mixes the key (which carries the parts).

**Review:** the code reviewer found two blockers and two major issues, all fixed with tests: two axis
materials naming each other recursed without end (now cut while resolving); appending parts to the GPU
table could invalidate an iterator; the GPU projected procedural parts at a different point than the CPU;
and the shadow-map (RSM) cache key missed the parts. Also fixed: a NaN normal gave black, and the views'
hash now mixes the parts as resolved.
