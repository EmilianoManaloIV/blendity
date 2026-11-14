# 0458: Materials, UVs, lightmaps, Bake to Mesh, export (round 482)

- **Status:** done (round 482, 2026-11-13). Approved with the pass (the owner, 2026-11-10: "Continue and finish all tasks, I approve all specs from
  all passes.", relayed by the hub session). **Done (round 482, 2026-11-12)** - see "Done: deviations".
- **Requirements:** R-04 (UVs and materials), R-05 (rendering: lightmaps), R-06 (file IO: export), R-07 (reachable
  from the menus and the console), Q-05 (undo), C-02
- **Decisions:** ADR 0129 (the object); none new expected
- **Model:** implementer (Sonnet), then test-engineer, then code-reviewer
- **PR:** #396 (https://github.com/EmilianoManaloIV/blendity/pull/396)

## Goal
The owner's fourth answer: "bake to a normal mesh; materials per surface; collision and lightmaps; Unity / VRChat
export." After this round a Blockout's floors, walls, ceilings and cut faces each take a material - the Blockout's
defaults, or a piece's own - textures run at one real-world scale over every piece and stay put when a piece moves,
a lightmap bake of the interior works, **Bake to Mesh** turns the Blockout into an ordinary editable mesh and
**Unbake** brings the pieces back, and the exporters write one mesh with its materials and no pieces.

## Scope
- In:
  1. **A material a class of face.** The kernel says which piece and side made each face; `blockout::side_class`
     (rounds 478 and 480) says Floor, Ceiling, Wall, Outside, Cut or Solid (a plain Add piece), and Blend.
     - `Blockout`: **Floor**, **Walls**, **Ceiling**, **Outside**, **Cut** and **Solid** materials (each may be
       empty: the default material);
     - `BlockoutPiece`: the same six as overrides (empty: the Blockout's), shown for the classes the piece has;
     - the build's job turns each face's class into a slot of the mesh (`Mesh`'s per-face material index) and gives
       the list of materials in a fixed order - the Blockout's six, then the pieces' overrides in the pieces'
       order, unused ones left out; landing sets `MeshRenderer::materials` to it. A blended face takes the material
       of the piece whose radius made it (its Solid, or Cut for a Subtract).
  2. **UVs at a real-world scale.** Every face gets UVs by box projection in the Blockout's space
     (`project_cube`, `src/scene/uv.h:32`, on the worker): the axis by the face's normal, the coordinates the
     position in metres divided by **Texture Scale** (`Blockout`, metres a tile, 1). So a texture has the same
     size on every wall, its pattern runs on across pieces that meet in a plane, and it does not slide when
     another piece - or this one - moves. A piece's **Texture Offset** and **Rotation** shift its own faces.
  3. **Shading:** flat faces are flat; blended faces are smooth across each other (the mesh's auto-smooth angle
     set to 40 degrees on a Blockout's mesh).
  4. **Lightmaps:** a new Blockout's `MeshRenderer` has Contribute GI on (it is static geometry; the lightmap UVs
     are generated at bake time as for any mesh - `generate_lightmap_uvs` is already the default). The lightmap
     unwrap must cope with the result's faces (n-gons, faces with many corners).
  5. **Bake to Mesh** (the Inspector's button on a Blockout, Mesh menu, `blockout bake`): the object keeps its
     mesh, materials and UVs and loses the `Blockout` component; the pieces move under one disabled child
     ("Blockout Pieces"), and a small `BlockoutBaked` component remembers the Blockout's settings. The object is
     then an ordinary mesh: Edit Mode, sculpting and modifiers work, and round 477's refusal hint now offers the
     bake ("... Bake to Mesh makes it editable").
     **Unbake** (`blockout unbake`, the button on a baked object): the component comes back with its settings,
     the pieces return, the mesh is rebuilt. If the baked mesh was edited since, Unbake asks first (a dialog; the
     console's `blockout unbake force` skips it): the edits are lost.
     One undo step each.
  6. **Export:** the exporters (`export_objects`, `editor.cpp:3313`: OBJ, FBX, GLB, the Unity package) write a
     Blockout as one mesh with its material slots and UVs; pieces and the disabled "Blockout Pieces" child are
     not written. A scene with a live Blockout waits for its build first (round 477).
  7. **Console:** `blockout set material floor|walls|ceiling|outside|cut|solid <material>`, `blockout set
     texscale <metres>`, `blockout bake`, `blockout unbake [force]`.
  8. **Tests** in `tests/test_blockout.cpp` (and `tests/test_lighting.cpp` for the bake of an interior), a Learn
     section ("Materials, baking and export of a Blockout"), docs.
- Out (not in this task): a mesh piece's own materials carried per face into the result (a follow-up if round 480's
  mesh pieces are used that way); trim sheets, per-wall UV editing (bake first, then the UV editor); LODs; a
  collider simpler than the mesh.

## Likely files
- `src/scene/blockout_object.h/.cpp` (the materials, the scale, `BlockoutBaked`), the build job (slots and UVs on
  the worker), `src/editor/blockout_editor.cpp` (bake, unbake, the buttons), `editor_commands.cpp`, the mode
  switch's hint, `export` code only if a test shows pieces leaking, `lessons.cpp`.
- `tests/test_blockout.cpp`, `tests/test_lighting.cpp`, `stress/stress_blockout.inc` (a row: slots and UVs' share
  of a build).

## Acceptance criteria
1. **Each class takes its material:** a room with a door: the floor's faces are in the Floor slot, the inside
   walls in Walls, the ceiling in Ceiling, the outer faces in Outside, the door's faces in Cut; a piece's override
   changes its faces only; the slots list holds each used material once, in the fixed order; an empty material
   falls back as said.
2. **One texture scale everywhere:** on two rooms of different sizes, for every wall face the UV distance between
   two corners equals their distance in metres divided by Texture Scale (to 1e-4); on faces of two pieces in one
   plane the UVs agree at shared corners.
3. **Textures do not slide:** after one piece moves, every face of the other pieces that kept its corners has the
   same UVs, bit for bit; the moved piece's faces keep the world-scale rule.
4. **A lightmap bake of an interior** (a two-room Blockout with a door, a light in each room; headless, low
   samples): the bake finishes, the lightmap UVs of the Blockout's mesh do not overlap (the existing overlap
   check of the lightmap tests), and a texel in the lit room is brighter than one behind the wall.
5. **Bake then Unbake gives the same mesh** (bytes), the same materials and the pieces where they were; Bake
   alone leaves an object without the component that enters Edit Mode; Unbake after an edit asks, and `force`
   proceeds; undo and redo of each restore the state before.
6. **Export holds one mesh and no pieces:** an FBX and a GLB of a scene with one Blockout (three materials used)
   read back with one mesh object, three material slots with the right faces in each, UVs present, and no object
   for a piece or for "Blockout Pieces"; the same for a baked Blockout.
7. **Play mode** still collides with the result after a material change (no rebuild of the collider is lost).
8. **The frame holds:** slots and UVs are computed on the worker; the stress row says what they add to a build;
   criterion 3 of round 477 (no kernel call on the main thread) still passes.
9. **Menus, buttons, commands and the lesson** each have a test; scenes of earlier rounds open unchanged; the
   audits pass.
10. **Nothing else changes:** rounds 476-481's tests pass unchanged; the full suite on Windows (both builds) and
    Linux; CI's four jobs. No test asserts a time.

## Tests and checks to run
- Unit: `--group blockout`, `--group lighting` in both Windows builds first, then the full suite.
- Stress: `--only blockout` (timed, quiet machine); the lightmap sections only if the unwrap code is touched.
- By eye (headless screenshots, both builds): a textured two-room interior from inside and outside; the same baked
  and lit.

## Responsiveness and cold first use (ADR 0105)
- Slots and UVs: one pass over the result's faces on the worker; nothing in the frame but the landing.
- Bake and Unbake are one-off commands; Unbake's rebuild is round 477's job.
- A lightmap bake is the existing job with its own progress.

## Done: deviations
1. **UVs are projected on each face's own plane**, not by `project_cube`'s box projection (it compresses a turned
   wall's and a slope's pattern, flips at 45 degrees and mirrors half the walls): the face's normal (snapped to an
   exact axis within about 0.08 degrees), U and V as the Cube primitive has them on its six faces, a face within about
   2.6 degrees of level taking the floor's directions. Two pieces' faces in one turned plane agree only to rounding.
2. **Shading:** round sides (a sphere, a cylinder's side, an arch's curves) are smooth as well as blended faces; every
   other face is flat by its own flag, not by an auto-smooth angle (40 degrees smoothed a 30 degree ramp into its
   floor). Mesh pieces' faces are flat.
3. **A data-model change the spec did not expect:** a new serialized component `BlockoutBaked` (the Blockout's
   settings, the pieces' holder by id, a hash of the baked mesh, the slots flag), a serialized `Blockout::slots_set`,
   and the "Blockout Pieces" convention - recorded as an amendment to ADR 0129.
4. **Files outside the Blockout's own**, each agreed with the hub or found by a review: `src/editor/editor.h`,
   `editor.cpp` (the Mesh menu's two entries, the Unbake dialog), `panels.cpp` (one line: the baked object's Inspector
   button), `paint.cpp` (Paint refused on every live Blockout), `src/scene/scene.cpp` (one line: the component's
   registration), `src/render/lightmapper.cpp` (for every mesh: tiling UVs are not used as lightmap UVs),
   `src/editor/ui.cpp` (for every menu: a submenu takes the tooltip written after it), `lessons.cpp` (one section).
5. **No export code changed:** OBJ, FBX and GLB already write a Blockout as one mesh and skip its pieces (tested, live
   and baked, with a blended piece too). **The Unity package** export repacks an imported package and writes no scene
   object: not covered, though the spec listed it.
6. **Lightmaps:** nothing in the bake changed - it baked a live Blockout before (round 477); Contribute GI is now on
   for a new Blockout. A piece move / a material / a Texture Scale change drops the lightmap; Save and Open keep it
   (both tested with real bakes). A lightmap baked before this round is out of date on open.
7. **Bake / Unbake:** `blockout bake` and `blockout unbake` need a selection; the Mesh menu acts on the selection;
   several edited baked objects are unbaked one at a time; Unbake with the pieces' holder gone is refused unless
   forced; a folder with pieces AND another Blockout refuses the bake; Unbake asks about edits to the mesh only (not
   materials set on the baked renderer); "edited" is a hash of the mesh's contents, cached; Unbake's rebuild is the
   next frame's, not waited for.
8. **The slots rule:** an older scene's hand-set renderer material is kept until a class material is set; after that
   the list is written at every build.
9. **Criterion 2** holds exactly on planes square to the axes and to 1e-4 on turned ones; **criterion 8**'s stress row
   says slots and UVs are about 0.1 % of a build, and a second pair of rows measures the landing with Realtime GI
   (Automatic) on: frames of 20 to 35 ms, a stated limit.
10. **One older test changed:** round 477's "... leave a Blockout's mesh as its pieces make it" asked for no UVs; it
    asks for UVs unchanged by the command (said in the test). The lighting test has its own `tests_round482` in
    `tests/test_lighting.cpp`.
11. **Not done:** headless screenshots by eye; the first-piece material rule is in the Solid field's help and the
    lesson, not in every field's help.

## Risks and edge cases
- **Slot numbers shift** when a material comes into or goes out of use: the list is rebuilt from the classes each
  build, and `MeshRenderer::materials` set in the same landing step as the mesh, never one without the other.
- **A face made of two pieces' faces in one plane** names the first piece (round 476): it takes that piece's
  material. Stated in the fields' help.
- **N-gons with many corners** in the lightmap unwrap and the exporters' triangulation: tested on a wall with
  sixteen windows.
- **Unbake after the pieces under "Blockout Pieces" were edited or deleted:** it rebuilds from what is there.

## Documentation to update
- CHANGELOG (round 482), README (the Blockout paragraph), ARCHITECTURE (slots, UVs, bake), USABILITY, PERFORMANCE
  (the row), the Learn section, this spec marked done with its deviations.
