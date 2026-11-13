## 2026-11-12 (round 482): a Blockout's materials, UVs and lightmaps; Bake to Mesh and back; export as one mesh (task 0458)

**Asked** (the user's answer to the pass's plan: "bake to a normal mesh; materials per surface; collision and
lightmaps; Unity / VRChat export"; the pass's specs approved 2026-11-10).

**Added:**
- **A material a class of face.** A Blockout has six materials - Floor, Walls, Ceiling, Outside, Cut, Solid - and each
  piece may override them for its own faces. The build puts every face in its material's slot (a blended face takes
  its piece's Solid, or Cut for a piece that cuts) and the mesh and its slots land together.
- **UVs at a real-world scale:** every face is projected on its own plane in the Blockout's space (a turned wall and a
  ramp's slope too), metres divided by **Texture Scale** (metres a tile). A texture is the same size on every wall,
  runs on across pieces that meet in a plane, and does not slide when a piece moves. U and V run the way the Cube
  primitive's do on each of its six faces, so a texture reads the same on a Blockout wall as on a cube beside it. A
  piece's Texture Offset and Rotation shift its own faces.
- **Shading:** flat faces are flat whatever angle they meet at; blended faces and round sides (a sphere, a cylinder's
  side, an arch's curves) are smooth.
- **Bake to Mesh** (the Blockout's Inspector, the Mesh menu, `blockout bake` with a Blockout or one of its pieces
  selected): the object keeps its mesh, materials and UVs and becomes an ordinary mesh - Edit Mode, sculpting,
  modifiers and Bake Textures work (Paint too, after an unwrap: the baked UVs repeat a tile on every face); the pieces
  wait under a switched-off child, "Blockout Pieces", and a mesh the Blockout used as a piece stays out of the views,
  the colliders, the lightmaps and the exports while they do. **Unbake** (`blockout unbake`, the object selected)
  brings the Blockout back and asks first if the baked mesh was edited - its shape, shading, seams, groups, shape keys
  or sculpt mask (`blockout unbake force` does not ask; several edited objects are unbaked one at a time). One undo
  step each. A new component, `BlockoutBaked`, is what a baked object carries.
- **Blockouts take part in lighting** (Contribute GI on for a new one, however it is made).
- Console: `blockout set material floor|walls|ceiling|outside|cut|solid <material|none>`, `blockout set texscale
  <metres>`, `blockout bake`, `blockout unbake [force]`. The Learn section "Materials, baking and export of a Blockout".

**Lighting, said plainly:** Generate Lighting bakes a **live** Blockout as it stands - it could since round 477, once
Contribute GI was ticked; now it is ticked. No Bake to Mesh is needed for lightmaps, and the lightmap's UVs are the
bake's own, not the mesh's. Moving a piece, or changing a material or the Texture Scale, **drops** the Blockout's
lightmap - the old one is not put on the new mesh - and the scene's lighting reads out of date (bake again); undoing
the change brings it back; a saved scene keeps it through Save and Open. Both are pinned by tests with real bakes of
a live two-room Blockout. Bake Textures, Mesh Maps and Paint are refused on a live Blockout - Bake to Mesh first.

**Changed:** a Blockout's mesh now has UVs, material slots and shading, so **a lightmap baked on a Blockout before
this round is out of date when its scene is opened** (bake again). Paint is refused on every live Blockout (it was
refused only on one without UVs; its UVs now repeat a tile on every face). The refusal of Edit Mode on a Blockout
offers the bake. Once a Blockout has class materials its renderer's material list is written at every build - a
material set by hand on the renderer is replaced; an older scene's hand-set material stays until a class material is
set. **For every mesh:** with Generate Lightmap UVs off, a mesh whose own UVs lie outside 0 to 1 (tiling) now gets
generated lightmap UVs; they used to be clamped into one texel's edge (`src/render/lightmapper.cpp`). **For every
menu:** a tooltip written after a submenu went to the row before it (the Mesh menu's Boolean, Separate and Draw);
it is the submenu's now (`ui.cpp`).

**Export:** OBJ, FBX and GLB write a Blockout - live or baked - as one mesh with its material slots and UVs; no
object is written for a piece or for "Blockout Pieces". No export code changed: the tests pin it. (The Unity package
export repacks an imported package and does not write scene objects: not part of this.)

**Numbers** (`blendity_stress --only blockout`, timed, alone on the machine, 2026-11-12; with Blender's libraries /
without):
- **Slots, UVs and shading are 0.2 % / 0.1 % of a build:** interior rooms 100 - 0.03 ms of 11.8 ms / 0.03 ms of
  47.8 ms (392 faces, 6 slots); interior rooms 400 - 0.09 ms of 37.3 ms / 0.11 ms of 187.3 ms (1,424 faces).
- **A door dragged for 120 frames, nothing lit** (Contribute GI off, Realtime GI off, no sun): frame median 3.0 ms /
  1.1 ms, worst 5.2 ms / 9.9 ms (round 481: 2.9 / 1.1 and 4.0 / 5.3) - the landing with slots is no slower.
- **The same drag with Realtime GI on Automatic, a sun and Contribute GI on:** frame median 20.0 ms / 3.1 ms, worst
  22.4 ms / 32.6 ms - **over the 16.7 ms frame** (the row's verdict is OVER; a known limit below).
- **The sweeps are unchanged from round 481, row for row** (12 crisp and field sweeps of 2,000 arrangements, 2 sweeps
  of 500 with Blend radii): 0 refused, 0 problems, the same counts of small faces on top of others (crisp 0, 0, 1, 51,
  165, 183; field 2, 43, 185, 12, 26, 123; blends 63 / 62).

**Known limits:** a face made of two pieces' faces in one plane takes the first piece's material; blended faces are
projected a face at a time, so a pattern has seams across a rounded join; UVs are in the Blockout's own units (scaling
the Blockout object scales the texture); two pieces' faces in one TURNED plane agree at shared corners only to
rounding (exactly on planes square to the axes); a face within about 2.6 degrees of level takes the floor's direction
for its pattern (a seam on a nearly flat ramp); a mesh piece's faces are shaded flat and its own materials are not
carried into the result; Unbake asks about edits to the mesh, not about materials changed on the baked object's
renderer; a folder that holds both pieces and another Blockout refuses the bake until that Blockout is moved out;
Unbake of an object whose pieces were deleted is refused (`force` makes it an empty Blockout); with Realtime GI set
to Automatic and a sun, each landing of a Blockout that contributes GI costs a frame of 20 to 35 ms (the realtime
lightmap's layout is redone on the main thread; both settings are off by default - a follow-up); the Inspector
hashes a baked mesh again while it is being sculpted with the object shown.

**Checked:** the round's groups and `editor`, 0 failed on each: Windows without the libraries, Windows with them and
Linux (the counts are in the PR); `tools/test_coverage_map.py --check` 378 entries, 0 empty; the timed Blockout stress
section on both Windows builds. Two code reviews and the test-engineer's 19 tests; the changes made after the second
review were not reviewed again. The full suite, macOS and the sanitizers are CI's.

<!-- README -->
replace: (round 481), with **wedges
with: (round 481), **materials by floor, wall and ceiling with world-scale UVs, Bake to Mesh and back** (round 482), with **wedges

<!-- docs/USABILITY.md -->
after: | A Blockout knew boxes, cylinders and spheres
| A Blockout had one material and no UVs, could not be textured, and could only be edited as pieces. (round 482) | Floors, walls, ceilings, outsides and cuts each take a material at one **Texture Scale**; **Bake to Mesh** makes it an ordinary mesh and **Unbake** brings the pieces back. |

<!-- docs/ARCHITECTURE.md -->
append:
## Blockout: materials, UVs, Bake to Mesh (round 482, task 0458)
`blockout_finish_mesh(mesh, faces, pieces, surface)` (`src/scene/blockout_object.cpp`) runs on the build's worker after
the kernel: each face's class (`blockout_face_class`) to a material slot, UVs (`project_face`: the face's own plane in
the Blockout's space over Texture Scale), shading. `BlockoutSurface` is the snapshot of what it reads and is part of
the snapshot's hash. The landing (`Editor::blockout_land`) sets the mesh and `MeshRenderer::materials` in one step
(`blockout_set_slots`). `BlockoutBaked` is what Bake to Mesh leaves on the object (the Blockout's settings, the pieces'
holder, a hash of the baked mesh); `Editor::blockout_bake` / `blockout_unbake` in `src/editor/blockout_editor.cpp`.
