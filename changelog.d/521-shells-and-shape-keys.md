## 2026-11-17 (round 521): Outline shells and shape keys on the card's skin

**Asked:** a posed toon character keeps its outline on the card with no shell built on the CPU a frame, and a blink or a
viseme while posing stays on the card instead of sending the character back to the CPU (Pass 26, task 0504, ADR 0146).

**Changed (task 0504)**
- The shell of a posed skin is the shell of its rest, skinned (`Outline::posed_shell`): the inverted hull of the pose's rest
  mesh is built once for a rest, an offset and a Thickness Group (a pose never rebuilds it; a thickness or distance step change
  does, once), and each pose makes a bare copy of it that carries the body's bones and weights - so on the card it is a skin
  of its own (a stand-in of its own, the body's palette) and on the CPU its render mesh is round 518's kernel on the rest
  shell. Its `positions` are the posed ones (exports, bounds). A mesh that isn't a posed skin builds its shell as before.
- **An approximation, in the Learn lesson:** the shell is the rest's hull pushed along the rest's normals and then posed, not
  the posed mesh pushed along its posed normals. A one-bone part is exact; where bones blend the line is thinner (the
  stand-in avatar, chest turned 0.5 rad: at most 43.7 % of the thickness off today's shell, 0.37 ms a pose for its four
  outlined meshes against 2.37 ms to build today's).
- A shape key under a skin goes up as ranges (`gpu::Raster::update_skin_rest`, and `update_skin_triangles` for a quad whose
  diagonal flipped): the stand-in takes the new rest, the render vertices that changed are written into the card's rest buffer
  and the skin runs again; the bone boxes grow to hold them and are made whole after 3 still gathers. The round 519
  `kSkinRestStill` CPU fallback stays only for a card that refuses ranges or a render vertex whose UVs change; a blink builds no outline hull (the rest shell follows in place); a whole rebuild of the rest sends the skin whole (weights from the new table); a weight that changes the render vertex count
  sends the skin whole once.
- Profiler: "skin rest updates", "skin rest ranges sent", "skin rest vertices sent", "skin triangle ranges sent".

**Checked:** the round's tests (animation group): 0 shells built and 0 render meshes skinned over posed gathers of a character
and of the stand-in avatar, the same pose gives the same shell, rebuilds by thickness / group / distance step, the unskinned
shell bit for bit, the one-bone shell exact, OBJ export and the CPU gather write the posed shell, a blink goes up as 24 of 72
render vertices (the shell 28-36 of 72); on the card (the hub's window, the full gpu group 1,510 checks, 0 failed): `update_skin_rest` / `update_skin_triangles`
against the kernel (8.9e-8 of the diagonal), the 4 posed outline shells read back against the CPU kernel (worst 3.5e-7), four posed
frames of the outlined avatar with 0 shells built and 36 skins drawn by the card, six blink frames with 0 hulls built, 0 skins
sent whole (12 rest updates, 30,444 vertices; 5.5 ms a blink frame against 3.5 ms posed, untimed). Reviewed twice (code-reviewer:
the lesson's numbers, a shell rebuilt every blink frame, weights after a rest rebuilt in another vertex order, the card
read-back test - fixed; approve). Round 519's outline test changed (a new pose builds no shell; it built one a pose) and its M3 shape-key test now runs
with `skin_ranges_off_for_test` (the fallback it pins is the path for a card that refuses ranges). Unit suite on Windows
with Blender's libraries (all groups but gpu, which the hub runs on the card): modeling 6,751, render 40,472, lighting 9,926,
assets 3,027, animation 5,029, physics 208, editor 3,869, livelink 365, input 283, sketch 521, board 96, sculpt 466, hci 1,985,
vr 116, blockout 20,809 (+ its four modifier / join groups), splats 387 / 175 / 3,996, runner 64 - 0 failed (render's
"OpenPGL path guiding learns" failed once in a full run while other work ran and passes alone and in the final run); Linux (WSL,
dependency-free) animation 5,029, editor 3,843, render 40,378 - 0 failed. Stress `posing` (a 50k-vertex pose change 0.41 ms
Linear, 0.49 ms Dual Quaternion), `animation`, `editor`, `editor_render`: no problems, nothing over its target; the timed rows
are round 522's.

<!-- docs/ARCHITECTURE.md -->
after: | `mesh_tris`, `render_bounds` (`scene/skin_render.cpp`)
| `Outline::posed_shell`, `skin_pose_positions`, `skin_bone_boxes_grow` (`scene/scene.cpp`, `skin_render.cpp`), `gpu::Raster::update_skin_rest` / `update_skin_triangles`, `Editor::skin_rest_moved` (`editor/gpu_skin.cpp`) | Round 521: a posed skin's outline shell is the rest's shell skinned (built once per rest, offset and group; a card skin of its own); a shape key under a skin goes up as vertex and index ranges, not the skin again | ADR 0146 |
