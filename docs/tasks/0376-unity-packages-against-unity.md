# Task 0376: Unity packages against Unity - an avatar and a world (round 399)

## Asked
"Here is the Shinano file, do proper testing ... do significant comparison testing with Blendity VRchat content and
Unity Content", "Are you going to fix the issues between Blendity and the real VRchat SDK/Unity?" (yes), "download any
required shader into Blendity to do the test" (lilToon 2.4.1, into the Unity test copy only), and "compare the
[Unity] package for worlds against Unity too just like you are doing with Shinano".

Both packages are the user's (Shinano: VN3 license; SideRail_Ver1.0.0): nothing of them is in the repo. The tests
below are synthetic; the comparison ran locally - Unity 2022.3.22f1 with VRChat SDK 3.10.6 in copies of the user's
VCC projects (never the projects), a dump script reading what Unity and the SDK hold, Blendity reading the same
package from its own project.

## Built
- **A model's Materials tab** (`unity_material_remap`, `apply_unity_material_remap`): a model's .meta
  `externalObjects` send its embedded materials to .mat files by name - Unity's remap, wherever the model is placed.
  Shinano's 16 renderers had worn the FBX's untextured materials; they wear the lilToon .mat files now. (The first
  version wrote into a copy of the renderer's slots - `mr ? mr->materials : {}` is a copy - and counted it done.)
- **Skeletons from prefabs** (`bind_import_skins`, split out of `instantiate_import`): a model placed from a prefab
  gets its Bones, rests and Skin as an import does, with its blend shapes as the model holds them (Shinano's Corset
  and Toe_heels at 100, as Unity).
- **The Budget as VRChat's SDK counts**: hidden outfits and switched-off renderers count; several PhysBones on one root
  each count (`SpringBone::physbones`, saved); every texture a Unity material names counts (`Material::engine_maps`:
  lilToon's masks, matcaps, second layers - not drawn, not saved) at the size Unity imports it
  (`unity_texture_megabytes`: the .meta's default or Standalone override, maximum size, format - a crunched one as
  what it unpacks to - automatic DXT1 / DXT5 by alpha, BC7 at high quality, a normal map a byte a texel, mips);
  outside Unity's settings an opaque image counts half (`Texture::has_alpha`); a texture only Unity reads is read on a
  worker once per version of its file (the Budget recounts on the UI thread), Unity's size for each kept.
  `budget list`: each object's share. ADR 0095 amended.
- **PSD**: Photoshop's composite (RGB or grey, 8 or 16 bit, raw or PackBits) - a model's .psd textures; at most
  2^28 pixels, and the file must hold what it claims before anything that size is made (a downloaded package's file
  is untrusted).
- **Overrides Unity keeps unused**: an override naming a part Unity's own numbers don't have is stale, said, not
  "waiting" (Shinano's six).
- **Worlds as Unity places them**:
  - **Nested ids**: an object inside a nested prefab, as the file around it names it - its id XOR the
    PrefabInstance's, the top bit cleared (`unity_nested_file_id`). SideRail's LowTables and FootLights (variants of
    models) had stood at the scene's top with their prefab names, 12 bulb filaments had stayed hidden; 2,519 overrides
    applied became 2,915.
  - **One top node is the root** (`unity_collapse_model_root`): Unity's root of a model with a single top node is that
    node, named for the file - not an empty root around it (SideRail's gratings). An armature keeps its own name (its
    skins find it by name); renaming an armature by an override renames the skins of its placement that name it.
  - **Only the materials in use** (`unity_drop_unused_materials`): a slot no face uses isn't one (Unity makes no
    submesh for it): Tunnel_Rail 5 slots here, 1 there. A mesh several nodes share is renumbered once.
  - **Unity's Default-Material** (`fileID 10303`): a white slot, one for the import.
  - **A removed renderer** (`m_RemovedComponents`) is removed, as in Unity; other removed components still come back,
    said.
- **The console's `import <path in the project>`** of a model, as a scene's and a prefab's.

## Checked
**Shinano** (`Shinano.prefab`; Unity's numbers from the SDK's own performance stats):

| | Blendity before | Blendity now | Unity / VRChat SDK |
|---|---|---|---|
| Triangles | 137,438 | 140,034 | 140,034 |
| Skinned meshes / meshes | 13 / 2 | 15 / 1 | 15 / 1 |
| Material slots | 16 | 17 | 17 |
| Bones | 0 | 283 | 275 |
| PhysBones / their transforms | 1 / 0 | 61 / 199 | 61 / 197 |
| Texture memory | 42.7 MB | 61.3 MB | 61.27 MB |
| Materials | the FBX's, untextured | 18 lilToon .mat files, textured | lilToon |

Every category's rank matches on PC and Quest (Very Poor overall on both). Bones: Blendity counts the armature
under each skin, Unity the skin's own bones - same rank. Blend shapes equal. The rendered avatar matches Unity's in
shape, size and textures.

**SideRail** (`SideRail.unity` opened as the scene):

| | Blendity before | Blendity now | Unity |
|---|---|---|---|
| Objects | 755 | 743 | 743 |
| Active / hidden | 587 / 78 | 611 / 66 | 611 / 66 |
| Mesh renderers shown | 324 | 347 | 347 |
| Triangles (shown) | 429,843 (376,933) | 429,843 (381,081) | 429,935 (381,125) |
| Material slots / unique materials | 444 / 51 | 420 / 41 | 419 / 39 |
| Lights (type and mode) | 71 | 71 | 71, the same |
| Colliders | 40 | 40 | 45: 4 triggers kept as Unity's (Blendity's colliders are solid), one object with two |

Every object's hidden state, switched-off renderers, parent and name match by path. Unity's particle renderers are
kept as Engine Components (Blendity doesn't draw them).

Tests: `tests_round399` (Unity's Materials tab and stale overrides end to end, worlds as Unity places them, Unity's
texture sizes, PSD and a hostile one, skeletons and the Budget, `budget list`, shared meshes' slots, a renamed rig
and a PhysBone over the model's spring); round 395's one-node model is its root;
the pilot depth test compares the view piloting showed (it had compared the view from before piloting, inside its
tolerance only because far depths all sit near 1); the Simple Deform redraw test's cylinder stands in view (inside the
Cube, it had been seen only where their tops fought for depth).

**Review** (Opus, then fixes): the near plane taken back; a hostile PSD refused before allocating; the Budget's
texture reads moved off the UI thread and cached; skins following a renamed armature; shared meshes' slots; a
removed renderer removed; one slot-remap helper (counting .mat files converted); a PhysBone over the model's spring;
Unity's format numbers (46 / 47), alpha source, power-of-two rounding and a nested package's list; the test project
left clean.

## Not done
- **SideRail's light**: 70 of its 71 lights are Baked, so Unity draws the world from its lightmaps; Blendity keeps
  those as Unity's (they need Unity's generated lightmap UVs) and shows the world dark until Generate Lighting bakes
  it here.
- **Layered clothing fights for depth at a distance**: Shinano's tights and boots sit a hundredth of a millimetre over
  its skin, and at 2.6 m the Scene view's depth (1 cm near plane, the usual order) can't keep them apart. A near plane
  that follows the distance was tried and taken back in review (it hid construction lines and cut nearby geometry in
  fly mode); a reversed depth, as Unity's, is the fix - the CPU raster's depth plane is read in 19 places, and the GPU
  raster's couldn't be checked on a card from this session (`no Vulkan GPU`) - a later round.
- **Unity's particles** stay Engine Components (not drawn).
- **Two colliders on one object** (Blendity holds one).
