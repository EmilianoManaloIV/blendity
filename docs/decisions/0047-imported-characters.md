# 0047: Imported characters: materials as assets, shape keys, and MMD, VRM, BVH and Collada readers of our own

- **Status:** accepted (tasks 0134-0142; the user asked: "When importing external fbx models, can the program auto
  create a material and apply it. Furthermore, make the program be able to handle mmd models as well. Feel free to
  implement other rigged character formats as needed", and chose full MMD support, VRM, FBX/glTF animations, BVH and
  Collada, materials extracted as .mat assets, and synthetic test files)
- **Date:** 2026-10-18
- **Requirements:** R-6 (import), R-13 (animation)

## Context
- **FBX imports** made materials, but only in memory:
  - textures embedded in the file were dropped;
  - links to another machine's paths failed;
  - texture sets beside the model were never looked at.
- **Characters from MMD, VRoid and motion capture** couldn't be read at all.
- **Shape keys:** Blendity had none, and faces need them.

## Decision
- **Import makes assets, as Unity's importer does:**
  - the model goes into `Assets/Meshes`;
  - its textures into `Assets/Textures/<model>/`, embedded ones taken out;
  - its materials into `Assets/Materials/<model>/<name>.mat`, applied to the model;
  - its clips into `Assets/Animations/<model>/`.
  - An existing `.mat` is used as it is on re-import, never overwritten.
- **Textures are looked for** beside the model, in its `.fbm`, `textures` and `images` folders, by name in any letter
  case.
- **Texture sets are matched by name** (`<name>_BaseColor`, `_Normal`, `_Roughness`, `_Metallic`, `_Emissive`,
  `_Gloss`/`_Smoothness`, `_MetallicSmoothness`, `_ORM`) onto materials the file left without textures. Packed and
  inverted maps are split into the red-channel maps Blendity reads.
- **Shape keys live on the mesh** (named offsets of every vertex), their weights on the MeshRenderer by name (Unity's
  blend-shape weights).
  - They are added to the basis before every modifier, so the skin bends an expression with the head.
  - With no topology-changing modifier they take the fast deform path, so an animated face doesn't rebuild the mesh.
  - They are saved sparsely, and animated as `MeshRenderer.BlendShape/<name>`.
  - A key whose size no longer matches the mesh (the topology edited since) is skipped.
- **No new dependency:** every new reader is Blendity's own code. Shift-JIS text (PMD, VMD) is decoded by the
  platform (Windows' code page 932, iconv elsewhere).
- **One space:** every format arrives in Blendity's left-handed Y-up metres. MMD's unit is 8 cm and its models face
  -Z.
- **What a format carries that Blendity can't show** (MMD sphere maps, UV and material morphs, SDEF) is named in the
  import's notes.

## Consequences
- Re-importing a model whose materials were renamed in the file makes new `.mat` files beside the old ones. The old
  ones are left for the user to delete.
- Shape keys aren't sculpted in Blendity: they come with imports, or from `shape add` (an inflate along the normals).
