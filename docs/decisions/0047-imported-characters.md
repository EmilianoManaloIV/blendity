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
  `_Gloss`/`_Smoothness`, `_MetallicSmoothness`, `_ORM`, two-word forms like `Base_color` and `Normal_OpenGL`, sizes
  dropped) onto materials the file left without textures. Packed and inverted maps are split into the red-channel
  maps Blendity reads. The finder (task 0143) reads the folders around the model - its own three levels down and the
  folders beside it two levels down, never Downloads, Desktop, a home folder or a drive root - and pairs each bare
  material with the set its name (or its meshes' names) is likeliest to own: the same key, one inside the other, or
  the share of words in common, with the best above 0.6 taken, a 1:1 leftover paired by best, and the pairing said in
  the import's notes so a wrong guess is one Inspector slot to change.
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
- **VRM rides on glTF:** the reader runs the glTF import, then the extension (0.x `VRM`, 1.0 `VRMC_vrm` and its
  companions). A 0.x model faces -Z in glTF and is turned half a turn about Y; a 1.0 model faces +Z. MToon becomes
  Toon shading (the shade colour as a ratio of the lit one, 0.x's sRGB colours made linear; toony as the edge's
  softness, shift as its place) with an Outline per mesh of the widest width its materials ask.
- **A model's own animations become clips** (`Assets/Animations/<model>/<clip>.anim`, the first on the root's
  Animation component): FBX stacks sampled a frame at a time at the file's rate, glTF samplers at their own keys with
  rotation baked, BVH rows at 1 / Frame Time. Rotations are keyed as Euler angles each nearest the frame before.
- **Bone lengths are in a bone's own units** (so a bone drawn through a scaled armature stays where its child is), and
  a leaf bone's default is given in metres (half its parent's world length, else 10 cm) divided by the bone's world
  unit; weightless end bones (`_end`, `_End`, `.end`, ` End`: Blender's leaves, Mixamo's `HeadTop_End`, BVH's end
  sites, VRoid's spring tails) fold into their parents' tails (task 0144). Tools wanting metres measure the bone in
  the world (`bone_tail`), never its `length` alone.
- **Motions become ordinary clips** (`Assets/Animations/<character>/<motion>.anim`, 30 fps for MMD), the motion file
  copied in beside its clip. MMD keys a bone as a turn in its rest frame, so a key becomes the local rotation
  `R_parent^-1 · C(q) · R_bone` (R: the rests relative to the root, C: MMD's space turned half a turn about Y) and the
  position `rest + R_parent^-1 · C(t) · 0.08`. MMD's Bezier timing becomes the keys' Hermite slopes; a run the slopes
  can't follow within half a degree or a millimetre (a long turn, an Euler wrap) is keyed every frame from the true
  slerp, linear, each frame's Euler angles chosen nearest the frame before.

## Consequences
- Re-importing a model whose materials were renamed in the file makes new `.mat` files beside the old ones. The old
  ones are left for the user to delete.
- Shape keys aren't sculpted in Blendity: they come with imports, or from `shape add` (an inflate along the normals).
