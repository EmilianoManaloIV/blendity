# 0134: Materials made and applied on import

- **Status:** done
- **Requirements:** R-6
- **Decisions:** ADR 0047
- **Model:** main session (Opus)
- **PR:** (with task 0135)

## Goal
An imported model comes in looking as it should: its materials made as assets, applied, textured, even when its
textures are inside the file, at a path from another machine, or only lying beside it.

## Scope
- In: `find_texture`: the link, else by name beside the model, in its `.fbm`, `textures`, `Textures`, `tex`,
  `images` and `texture` folders, any letter case.
- In: `save_embedded`: FBX `content` written out and used (glTF's embedded images already were).
- In: `derive_channel`: the packed and inverted maps split into the red-channel maps Blendity reads:
  - glTF's metallic-roughness (now read at all);
  - FBX's roughness-as-glossiness;
  - `_Gloss`/`_Smoothness`, `_MetallicSmoothness`, `_ORM`.
- In: `complete_materials` after every import:
  - a material for every mesh without one;
  - texture sets matched by the material's name, a mesh's name, or the folder's only set, onto materials without a
    base map (the file's own links never replaced).
- In: FBX opacity (transparent surface) and double-sided.
- In: the editor's import:
  - the model copied into `Assets/Meshes` when it comes from outside the project;
  - textures outside the project into `Assets/Textures/<model>/`;
  - every material as `Assets/Materials/<model>/<name>.mat`, applied; an existing `.mat` reused;
  - the console's `import <file>`.
- In: the FBX exporter's Embed Textures (binary FBX: a Video's `Content`, as Blender writes it).

## Acceptance criteria
1. Blender's FBX with embedded textures, and its GLB with a packed metallic-roughness map, import textured, the
   channels split right.
2. A link to another machine is found in the model's textures folder; a texture set fills a bare material; every mesh
   gets a material.
3. Blendity's own FBX with Embed Textures imports textured with the image file gone; the `.mat` is made and applied;
   an edit survives a re-import.

## As built
- Tests: round 154; fixtures `tests/fixtures/embedded_quad.{fbx,glb}` (`make_embedded.py`).
- After the review:
  - each import folder lists the files it came from (`.import-source`): models of one name get `<name> 2`, the same
    model reuses its own;
  - the cache folder is named by the model's full path;
  - single-letter suffixes count only in sets of two maps or more;
  - the folder's only set goes only to materials the import made;
  - channel splits are made once per import, and only for empty slots;
  - the texture folders are listed once per import;
  - an `.mtl`'s own folder is tried first.
