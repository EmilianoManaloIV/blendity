# 0502: The card's skin pass (Pass 26, round 519)

- **Status:** in review (implemented in round 519; the card's tests (criteria 1, 3, 5, 6 on a card, 2's picture) wait for a graphics-card window)
- **Requirements:** Q-04, Q-06
- **Decisions:** ADR 0146 (skinning on the card; normals turned by the bones as Unity does)
- **Model:** implementer, reviewed by the main session; test-engineer; code-reviewer
- **PR:** (stacked on #434, round 518)

## Goal
A posed character in a view on the card is skinned by the card: the CPU sends each skinned mesh's palette (its bones'
matrices) a frame, not the mesh. Round 518's CPU kernel (`skin_render_vertices`, `src/scene/skin_render.cpp`) is the
reference the card matches.

## Scope
- In:
  - `gpu::Raster` (`src/render/gpu_raster.h`): `can_skin()`, `add_skin(rest mesh id or rest vertices, weights) -> mesh
    id` (a mesh whose vertex buffer the skin pass writes; the rest mesh's indices), `set_skin_palette(mesh, palette)`,
    `remove_skin`; the defaults say "no" so other rasters need nothing.
  - `VulkanRaster` (`src/render/gpu_raster.cpp`): a compute pipeline with its **own** descriptor layout and pool,
    made lazily by the first `add_skin` (never in `init`); its GLSL written line for line from `skin_blend_point` /
    `skin_blend_dir` / `skin_render_vertices`; one dispatch per skinned mesh whose palette changed, before the
    frame's first pass, then one barrier (shader write -> vertex attribute read); palettes in a host-visible ring of
    three (as built: every recording settles the frame before, so one frame is in flight at most and the ring is spare
    room - review 519); `warm_shaders` compiles it. Both views of a frame draw the one posed buffer.
  - **Pass 27 overlap (the hub, 2026-11-15/16):** no binding in the lighting layout (set 0, `make_layout()`); no edit
    to `init`, `add_mesh`'s buffer usage flags or the shader variants (M2 edits those; S2 uses bindings 40-41). **M2's
    hook:** `add_skin` can give the posed buffer an extra usage flag (a raster setting M2 sets: the
    acceleration-structure build input), and the raster lists, a frame at a time, the skinned meshes whose posed
    buffers the pass wrote (for M2's BLAS refit). As built, the flag reaches the posed vertex buffer alone: the index
    buffer, and the device-address allocation flag `make_buffer` doesn't set, are M2's to add (review 519).
  - `src/editor/gpu_views.cpp` (and where the draw items are gathered): a skinned mesh whose pose holds
    (`Mesh::skin_pose`, round 518) on a raster that can skin goes up once as its rest render mesh plus weights, then
    sends only its palette a frame. It must not ask the CPU for the posed render mesh on the card path
    (`Mesh::render_mesh()` on a posed mesh runs round 518's CPU kernel). Its bounds (culling, the shadow fit, the
    instance's box) come from per-bone rest boxes moved by the palette - conservative, never smaller than the posed
    mesh. A view on the CPU still draws `render_mesh()` (round 518).
  - Per render vertex weights for the card: from the pose's `SkinBinding` through the render-vertex -> mesh-vertex
    table, up to 8 bones (16-bit indices) and the weights already divided by their sum on the CPU (so the card's
    weights are the CPU's floats); a vertex with no weight stays at rest. A mesh whose bones or weights don't fit
    (over 65,535 bones; a card without the feature) stays on the CPU path with a reason in the view's list (ADR 0108).
  - Profiler / `perf status`: skins on the card, palettes sent, the reason a skin is not on the card.
  - The Learn tab's posing lesson (`src/editor/lessons.cpp`): what the card does now, what still needs the CPU.
- Out: the CPU positions for picking, physics, export, the path tracers (round 520); outline shells and shape keys on
  the card (round 521); the timed check (round 522).

## Acceptance criteria
1. **The card against the CPU** (`tests/test_gpu.cpp`, where a card is present; skipped with the reason otherwise):
   the posed buffer (read back in the test only, `read_mesh`) equals `skin_render_vertices`' output within 1e-5 of
   the bounding diagonal for positions and 1e-4 for normals and tangents - linear and dual quaternion, 1 to 8
   influences, a vertex with no bone, an unevenly scaled owner, a mirrored bone, 292 bones.
2. **The picture:** a posed stand-in (`benchscene avatarlikequick`) drawn by the card and by the CPU agree within ADR
   0036's 1 / 255 tolerance, ADR 0108's listed approximations aside.
3. **Nothing posed goes up as a mesh:** on the card, a posed frame sends 0 mesh uploads for the skinned meshes and
   skins 0 render meshes on the CPU (`MeshCounts::skinned`), only palettes (counted); a second view of the same frame
   adds no dispatch.
4. **Fallback:** a card without the pass, a skin with a limit group, a modifier after the skin, Edit Mode: the mesh
   draws correctly from the CPU, its reason listed.
5. **Bounds:** a character culled at the edge of the view or casting into the shadow fit is never cut off: the
   bone-box bounds contain every posed vertex (a test with stretched bones).
6. **M2's hook:** with the flag set, `add_skin`'s posed buffer has the extra usage; the per-frame list names exactly the
   meshes dispatched that frame.
7. The dependency-free build has no card and builds; CI passes; the groups `gpu`, `render`, `editor`, `animation`
   pass on Windows (with the libraries) and Linux.

## Tests and checks to run
- Unit: `--group gpu` (needs a graphics-card window from the hub on this machine), `render`, `editor`, `animation`.
- Stress: `--only posing` is round 522's timed run; an untimed `--only posing` check here shows the card rows run.

## Risks and edge cases
- A mesh drawn by two characters (one mesh asset, two armatures): the posed buffer is the skinned instance's.
- Device lost, the card's memory full: that skin to the CPU path, said in the view's list.
- Undo / redo, Record, Play, leaving Play: the palette is the drawn frame's pose (ADR 0126).
- The rest-position switch, a skin turned off, a mesh re-bound: the card's skin removed and made again, never stale.

## Documentation to update
- `changelog.d/519-the-cards-skin-pass.md` (ARCHITECTURE section for the raster's skin calls; USABILITY line if the
  user sees a change; the Learn lesson's text).
