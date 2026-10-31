# Task 0316: a texture's Physical Size at 0.15 turned a cube black (the GPU views' tangents)

- **Status:** done (round 337, 2026-10-31)
- **Asked:** "Physical size parameter at textures on a cube at 0.15 turn the cube black."

## What was wrong
- **The GPU views drew any mesh with a normal map black** when the mesh had been on the card before the normal map
  came. `Mesh::render_mesh_tangents` adds the tangents to the same render mesh in place, its serial kept, and the
  card's mesh cache (`Editor::gpu_mesh`) re-sent a mesh only on a new serial: the card kept zero tangents, and the
  shader's `normalize` of a zero tangent was NaN - a black pixel.
- **Physical Size only showed it.** A procedural texture with Live Shading is drawn on the CPU where its texels are
  magnified (`live_close_up_of`); at a 2 m tile a cube's texels are, so the cube was drawn on the CPU, textured. At
  0.15 m the texels are small, the card draws the cube, black. With Live Shading off it was black at any size.
- **Two smaller faults found on the way:**
  - Assign left a material's Tiling z at 1 while x and y took 1 / Physical Size, so Box mapping tiled two of its
    projections differently and z never followed the size.
  - `texture <file>.ptex` in the console read the procedural texture as a picture ("unsupported TGA type").

## Fix
- The card's mesh cache keys a mesh by whether it went up with tangents too (`GpuMesh::tangents`): one that gains them
  is sent again.
- The GPU shader's normal mapping leaves the normal as it is when the tangent is zero, NaN or along the normal, as the
  CPU's `normalize` of zero does.
- Assign sets z to 1 / Physical Size too.
- `texture` given a `.ptex` assigns it as the Textures window's Assign does.

## The review's fixes
- The GPU test checks the card drew the cube (a CPU fallback would have passed the picture check without the fix).
- Both tests stop when the cube can't be projected (they read the frame at a negative row).
- The retro vertex-snap copies are keyed by the tangents too (a still camera showed a new normal map only after a move).

## Not done
- The GPU path kernel (`gpu_kernel.h`) keeps its unguarded tangent normalize: its tangents always come from
  `render_mesh_tangents` (never zero), only a tangent exactly along the normal reaches it; guarded when the kernel and
  the OptiX PTX are next rebuilt.

## Acceptance
- `test_gpu` round 337: a cube drawn on the card, then given a Bricks texture (Live Shading off) and Physical Size
  0.15, reads within 40 % of the CPU's picture in its middle (it read 10 against 103 before the fix; 103.5 against
  103.5 after). Skipped without a Vulkan GPU.
- `test_assets` round 337: the CPU's Shaded view keeps the cube textured at 0.15, and its Tiling is 1 / 0.15 on x, y
  and z.
- The round-98 Physical Size test still passes.
