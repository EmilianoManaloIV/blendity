# 0149: The 2D look - custom normals, a face shadow map, inner lines and hatching, hold frames

- **Status:** done (round 168)
- **Requirements:** R-03, R-04
- **Decisions:** ADR 0048 (extended)
- **Model:** main session
- **PR:** #113 (stacked on #112)

## Goal
The user: renders that "look more 2D while posed in 3D", research-based; chosen: shadow control with smooth normals,
a face shadow map, inner lines and hatching, animation on 2s and 3s. A drawn character's face is one tone with a
shadow that sweeps across as the light turns, its lines are drawn where the artist wants them, and it moves every
second or third frame while the camera moves every frame.

## Scope
- In: `Mesh::custom_normals` (one per vertex; `corner_normals` takes them over the smoothing; saved as `cn` lines;
  `weld_exact` renumbers them; `transfer_custom_normals` and the editor's `carry_custom_normals` carry them across
  edits as shape keys are; `update_in_place` refuses so a build keeps them); `keep_file_normals` and the glTF / FBX
  readers keeping a file's own normals when over 1 % of them differ from the recomputed ones by a degree (with a
  note); the exporters writing custom normals as they are. `SmoothNormalsModifier` (N averaging steps, a factor, a
  group) and `NormalTransferModifier` (the nearest point on a proxy's triangles, a blend, a group, re-evaluated when
  the proxy moves), both deform-only; Rigging > Anime Face Normals / console `facenormals` (a sphere proxy fitted
  to the Head group or the top sixth, without a renderer, as the mesh's child, plus the modifier). Material fields
  `face_shadow_map` / `face_shadow_softness` / `face_forward`, `line_map` / `line_only_shadow`, `hatch_map` /
  `hatch_scale`; `toon_face_lit` (the key light in the object's space round the face's up axis: `DrawItem::local_key`,
  `PathTracer::local_keys_`, the kernel's `to_local`) and `SurfaceSample::toon_shadow_mul` in `shade_toon`, in the
  CPU rasterizer, both path tracers (the kernel's eight reads in one fetch loop; the face term once per hit); the
  GPU rasterizer hands the new maps to the CPU like the other painted maps; the Face Shadow generator. Outline
  `thickness_group`, `distance_fade`, `min_pixels` / `max_pixels` (`outline_distance_scale`, applied per object by
  the views through `Editor::outline_view_`). `Animation::hold` and `Sequencer::hold` (`hold_frame`), renders holding
  the same frames. The lesson section.
- Out: a painted SDF generator from N shapes (the sweep is the start; the map is painted over); screen-space hatching
  (per material over the UVs); spring bones stepping at the held time (they step every frame, so hair settles
  between holds - a follow-up if it shows); a scene-wide hold default (the component's field does it per character).

## Acceptance criteria
1. A vertex's custom normal reaches every corner; saved and loaded; carried onto another mesh by position; kept
   through a glTF round trip with a note; the file's normals kept only when they differ.
2. Smooth Normals brings adjacent normals closer and moves no vertex, limited by a group; Normal Transfer makes a
   cube's normals radial from a sphere proxy and follows the proxy; `facenormals` adds the proxy and the modifier
   and one undo removes both.
3. `toon_face_lit`: front all lit, behind all shadow, the sides by the map and its mirror, straight above lit, no map
   -1; the generator sweeps 0 to 1 across; on a sphere the rasterizer, the CPU path tracer and the GPU's light every
   pixel one tone from the front and the shadow tone from behind.
4. The shadow multiplier halves the shadow tone and leaves the lit one; a grey line map darkens the colour (or the
   shadow tones only); a hatch map the shadow tones; the Outline's group scales the shell per vertex (0: none) and is
   part of the cache's key; the distance scale's fade and pixel limits.
5. On 2s a clip shows 12 poses over 23 frames, on 3s 8, on 1s 23; Hold saved with the scene.

## After the review
- The GPU kernel sat at SPIR-V's id bound again: the face term is computed once per hit (the key light is one
  direction a frame) and carried as one float, and the kernel's `Material` struct did not grow - the new map ids and
  flags are packed into `tex3` bits and `color2.w`.
- Normal Transfer indexed the mesh's vertex arrays with the render mesh's triangles: it reads the render mesh's own
  vertices and shading normals now; the closest point is `closest_point_on_triangle` with early returns.
- A normal modifier compounded on every re-evaluation (it started from its own last result): the deforming tail
  resets the custom normals from the head's.
- Custom normals didn't follow the skin: the Skin modifier turns them with the blended bones, and Anime Face Normals
  puts the transfer before the skin (once, in the rest pose; a 16 x 12 proxy).
- `keep_file_normals` fired on ordinary imports (area- vs angle-weighted normals) and on hard edges: it refuses split
  normals (a zero from the weld or the FBX corners) and compares against both weightings at two degrees; the glTF
  weld is as before (no normals passed). The FBX reader uses `ufbx_matrix_for_normals`.
- The Outline's scaled shells thrashed one slot with the path tracer's (a freed shell under a progressive render):
  views' scaled shells have slots 4 / 5, the scale moves in 2 % steps; orthographic views and object-unit thickness.
- `hold_frame` rounds a float frame just under a multiple up; a render range is held from its first frame.
- `customnormals show|clear` on the console; the exporters turn custom normals by the inverse transpose.
- Out, noted in the ADR: the other deformers leave custom normals as they are; the face map ignores cast shadows;
  the Sequencer's Hold and Render Animation's holding aren't tested beyond `hold_frame_from`.
