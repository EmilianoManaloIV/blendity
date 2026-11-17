## 2026-11-17 (round 519): The card's skin pass

**Asked:** move skeletal skinning to the graphics card (the user, 2026-11-15; Pass 26, task 0501, ADR 0146). Round 518
made the CPU reference (`skin_render_vertices`); this round writes the card's pass from it, line for line, and sends the
card a palette a frame in place of the posed mesh.

**Added (task 0502, ADR 0146)**
- `gpu::Raster::can_skin()`, `add_skin(rest vertices, indices, weights)` -> a mesh id the instances draw as any mesh,
  `set_skin_palette`, `remove_skin`; the defaults say no, so a raster without the pass needs nothing. `VulkanRaster` makes
  a compute pipeline of its own - its own descriptor layout and pool, made by the first `add_skin`, not in `init`, and no
  binding in the lighting layout - from `kSkinComp`, GLSL written from `skin_blend_point` / `skin_blend_dir` /
  `skin_render_vertices` (the same order of operations, dual quaternions only when every bone of the vertex is rigid, a
  vertex with no bone or a degenerate turn left at rest, a non-finite position kept at rest). One dispatch per skin whose
  palette changed, before the frame's first pass, between two barriers (the frame before's vertex reads -> the write;
  the write -> the vertex fetches and the next write); the palettes live in a host-visible ring of three, bound by a
  dynamic offset (spare room: every recording settles the frame before). Every pass and both views of a frame draw the
  one posed buffer: a second view, or a still frame, adds no dispatch. `add_skin` refuses weights naming a bone the
  palette doesn't hold or more bones a vertex than slots, and the shader clamps both. `warm_shaders` compiles the shader.
- Pass 27's hook (M2): `set_skin_extra_usage(flags)` adds a VkBufferUsageFlags to every posed vertex buffer made after
  it (`skin_usage(mesh)` says what a buffer was made with), and `take_skin_writes()` lists the skinned meshes the last
  recorded frame's pass wrote - what a BLAS refit is made from. The index buffer and the device-address allocation flag a
  build input also needs are M2's to add.
- `skin_card_weights` (per render vertex up to 8 bones, 16-bit indices, the weights already divided by their sum on the
  CPU, so the card's are the CPU's floats; a vertex with no weight stays at rest; more than 65,535 bones refused),
  `skin_card_palette` (the owner's matrices, then per bone a matrix, a dual quaternion and a rigid flag: 96 bytes a
  bone, 28 KB for 292), `skin_bone_boxes` / `skin_posed_bounds` (`src/scene/skin_render.cpp`): the bounds of a posed
  mesh without its vertices. A linear blend is a weighted mean of the bones' moves, so each bone's rest box moved by its
  matrix holds it. A dual quaternion blend is a rigid motion about each box's centre, padded by the box's diagonal and
  twice the farthest the bones sharing its vertices carry that centre (a blend about a far pivot swings by the pivot's
  distance); with vertices on three bones or more, whose turns can cancel, the box of the deform's positions, which the CPU
  still makes this round. `Mesh::skin_pose_holds()` is the kernel's own check, so a caller needn't ask the render mesh.
- Profiler / `perf status`: skins on the card, palettes sent, skins sent to the card, skins posed on the CPU, and the last
  reason a skin was left to the CPU.

**Changed (task 0502)**
- A card view's gather (`render_deferred`) keeps a posed skinned mesh as a stand-in: its rest render mesh (a copy), one per
  object and rest - the Scene and Game views share it when they pose one rest, and keep one each when a modifier before
  the Skin gives each its own - with the posed `Mesh` in `DrawItem::skin`, the box in `skin_box` (the shadow map's frame
  and the close-up test read it through `draw_bounds`) and the pose's stamp in the item's words (the serial stays while a
  pose changes). The card holds it once and is sent the palette when it changed; a re-bind (Max Influences, a bone's
  Deform) makes the stand-in and the card's copy again. Nothing there asks the posed mesh for its render mesh: the CPU
  kernel does not run for it. Every other reader of the items (`collect_items`: F12, the path tracers, bakes, MoonRay)
  gets the posed render meshes as before. A stand-in costs a second copy of the rest render mesh in memory.
- A skinned mesh the card can't take - a Skin with a limit group or a modifier after it (no pose: the reason is listed in
  the view's notes), Edit Mode, a see-through, volume, baked or highlighted surface, a card that is full or has no compute
  pass, a camera's flatten or View Angle Shapes, Realtime GI on Automatic (voxelizes the posed vertices), and a rest that
  shape keys under the Skin move (a blink, a viseme: sending the whole skin each frame would cost more; back on the card
  once it holds still for three gathers, until round 521 puts shape keys on the card) - is posed on the CPU and sent as a
  mesh, the rest of the view staying on the card. A view the CPU draws gets the posed render meshes too, and so do the
  Solid shading mode and Display As Bounds.
- A toon outline's shell is keyed by its source's pose, not by the posed render mesh's serial, and an object's box
  (`GameObject::world_bounds`, the outline's width by distance) is a posed skin's posed positions: both asked the posed
  mesh for its render mesh every frame, which ran the CPU kernel the card was to spare (found on the card: 3 meshes a frame
  of the stand-in avatar). The shell itself is still made on the CPU a pose (round 521).
- The instance's Generated mapping box and a volume's thickness read the rest box, not the posed one (an approximation
  on a skinned mesh with such a material).

**Reviewed (code-reviewer):** a re-bind with the same rest left the card's weights stale; the stand-ins, kept by object
alone, were made again every frame when the Scene and Game views posed different rests; a rest moved by shape keys sent
the whole skin each frame; the dual quaternion bound (first half the rest's diagonal, which a test engineer's far pivot
broke) didn't hold three bones whose turns cancel; `add_skin` trusted the bone indices; the list of written skins grew
without end; the barrier after the pass lacked the next write; Edit Mode's reason wasn't listed. All fixed, with tests.

**Checked:** on the card (`--group gpu`, the hub's window): the pass against the CPU kernel - 20 cases, worst position
5.4e-7 of the diagonal, worst direction 6.9e-7 (bounds 1e-5 / 1e-4); 8 influences and unbound vertices 0 off; M2's hook
(the extra usage, exactly the dispatched meshes); the stand-in avatar posed over 12 frames: 0 skins sent whole, 0 render
meshes skinned on the CPU, a palette a pose, and with its outlines off 0 meshes sent; its picture against the CPU's
99.4 % of 561,946 pixels within 3 / 255 (ADR 0108's approximations aside); a skin never stale through two characters
of one mesh, a re-pose, the rest position, a re-bind, Max Influences, a bone's Deform, a shape key held and a Skin put back
- round 519's gpu tests 238 checks, 0 failed. With Blender's libraries the animation, render and editor groups ANIM_TBD /
2,639 / 3,230 checks; dependency-free animation 4,558 on Windows and 4,562 on Linux; the only failures those of "assets:
Ctrl and Shift click select several materials" (21 on Windows, 4 on Linux), which every run on this machine fails while it
shares one default test project with other sessions - with a test folder of its own it passes (#433). The three gpu picture tests
that fail on this machine (textured materials, overlays, the Lanczos probe ball) fail on main too and #436 fixes them;
two more that failed in one full run only pass alone (a starved run: round 555). CI runs the whole suite.

(README's feature paragraph and USABILITY's rows: round 523's write-up, once round 522's timed rows say what the user sees;
the Learn tab's "Where a Posing Frame Goes" lesson says what runs on the card now and what still needs the CPU.)

<!-- docs/ARCHITECTURE.md -->
after: | `SkinPose`, `skin_blend_point` / `skin_blend_dir`, `skin_render_vertices`
| `gpu::Raster::add_skin` / `set_skin_palette` / `take_skin_writes` (`render/gpu_raster.cpp`, `kSkinComp`), `skin_card_weights` / `skin_card_palette` / `skin_posed_bounds` (`scene/skin_render.cpp`), `Editor::gpu_skin_mesh` / `skin_card_item` / `skin_stand_for` (`editor/gpu_views.cpp`, `gpu_skin.cpp`), `DrawItem::skin` | Round 519: the card's skin pass - a compute dispatch per skin whose palette changed, before the frame's first pass, writing a posed vertex buffer every pass and view draws; the CPU sends weights once (a stand-in per object and rest) and a palette a frame; a skinned mesh the card can't take is posed by the CPU kernel | ADR 0146 |
