# Task 0340: the VRChat world kit, part 2 - LOD Groups

- **Status:** done (round 363, 2026-10-31)
- **Asked:** "fix all fixes as stated in comparing with the other established programs" - finding 9 of
  docs/hci/compare-2026-10.md: a world's objects had no levels of detail for its engine.

## What
- **`LODGroupComponent`** ("LODGroup"; Add Component > Rendering), as Unity's LOD Group, has:
  - Levels (1-4);
  - for each level after LOD0, a Triangles share (its Decimate ratio: 1, 0.5, 0.25, 0.1 by default);
  - for each level, a Screen Height (0.5, 0.25, 0.1, 0.03 by default);
  - Cull Below Last.
- **`scene/lod`:**
  - `lod_level` picks the first level whose Screen Height the object reaches, else the last, or -1 when culled;
  - `lod_mesh` makes a level with meshoptimizer's decimation and keeps it on the group until the mesh or the ratios
    change. Without meshoptimizer every level is the mesh;
  - `lod_export_name`.
- **The Game view:** `collect_items` passes each mesh through `Editor::lod_for_view`. The object's height on screen is
  its world bounds' largest side times the projection's half focal term, over the distance from the eye (an
  orthographic view: no distance). Only the Game view's draws use it (`lod_view_`, set in `render_deferred`); the
  Scene view, renders, bakes and exports draw LOD0.
- **Exports** (glTF, FBX): with `ExportOptions::lods` (the Export dialog's LOD Levels, on), an LOD Group object's item
  becomes `<name>_LOD0`, and each next level is an item beside it, `<name>_LOD<n>`, its skin made again for its
  vertices. Unity's model import builds an LOD Group from the suffixes; Unreal's FBX import reads them as LODs.

## Checked
- `tests_round363`:
  - the levels at screen heights 0.9, 0.3, 0.15 and 0.01, with and without Cull;
  - a 48 x 32 sphere's levels at 40-60 % and 15-35 % of its triangles, the same mesh returned again (the libraries'
    build);
  - in the Game view's items, a tree 1 m away drawn whole (720 triangles) and 60 m away at LOD2 (180) - the libraries'
    build; without meshoptimizer both whole;
  - a keyed tree drawn whole far away;
  - a .glb's nodes Tree_LOD0, Tree_LOD1 and Tree_LOD2;
  - the lesson's Try-it.

## Review (Opus): changes needed, all fixed
- **The test failed in the libraries' build** (a blocker): its "near" eye stood at LOD1. Now 1 m away is LOD0, and the
  test checks the exact triangle counts of the mesh and LOD2.
- **Deforming objects froze at a level:** a Skin or keyed weights moves only `deform_version`. Such an object is now
  drawn whole (`lod_deforms`), and the levels' key carries `deform_version`, so a sculpt refreshes them.
- **F12's raster, the camera preview, the pilot view and the pad switched levels.** Only the Game view does now
  (`game_view_drawing_`).
- **The cache was keyed by a pointer** that can be reused at the same version. It's now keyed by the mesh's own id and
  versions, with up to four bases kept, so views and exports don't wipe each other's levels.
- **The bounds** come from the mesh being drawn, not another evaluation.
- **Exports:**
  - the item is found by its index;
  - no full copies are written where nothing decimates;
  - `UCX_` follows LOD0's name;
  - the Unreal claim is withdrawn.
- **NaN or negative ratios** are cleaned before they are keyed or decimated.

## Not done
- **Renders and the path tracer** draw LOD0: the camera's LODs are a Game view matter, as in Unity's editor.
- **A cross-fade** between levels (Unity's Fade Mode).
- **Lightmaps on LOD1 and up:** a baked object's levels light from the probes (its lightmap fits LOD0's triangles).
- **Unreal's LODs:** an FBX LOD Group node.
- **Levels for a deforming object in the Game view:** a decimation remap carried through each frame's pose.
