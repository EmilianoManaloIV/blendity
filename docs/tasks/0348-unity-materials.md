# Task 0348: Unity materials and VRChat's components (Phase E, part 3)

- **Status:** done (round 371, 2026-11-01)
- **Asked:** "import Unity packages and prefab packs related to VRChat into Blendity (Mesh, Materials, Lights,
  no-code, etc.)". E3: the materials, and VRChat's components.

## What
- **`scene/unity_material`:** a .mat's `m_SavedProperties` read in both of Unity's layouts (`- _MainTex: {...}` and
  Unity 5's `first: {name:} / second:`). The shader family comes from the shader's name (built-in GUIDs for
  Standard and URP; else the shader file's name) or from tell-tale properties (`_lilToonVersion`,
  `_ShaderOptimizerEnabled`, `_ShadeColor` + `_ShadeTexture`, `_1st_ShadeColor`).
  - **Every family:**
    - colour and alpha (`_BaseColor` / `_Color`), and the main map (`_BaseMap` / `_MainTex`) with its scale and
      offset;
    - normal map and strength;
    - `_Metallic` and its map;
    - roughness = 1 - `_Smoothness` / `_Glossiness`;
    - emission when `_EMISSION` is on or a map is set, the HDR colour split into colour and strength;
    - Standard's `_Mode` / URP's `_Surface` and `_AlphaClip` to Opaque, Cutout or Transparent, with `_Cutoff`;
    - `_Cull` 0 to double-sided.
  - **Toon** (lilToon, Poiyomi, MToon, UTS2): shading model Toon, the shade colour and texture, and the outline's
    width and colour.
    - MToon and lilToon give the width in centimetres. Poiyomi's `_LineWidth` and UTS2's `_Outline_Width` are taken
      at about that scale, which is an approximation.
    - The outline becomes an Outline component on the objects that wear the material (Blendity's outline belongs to
      the object).
  - **Unlit:** unlit.
  - **An unknown shader:** the same reading, its main texture and colour, and a Console warning naming the shader.
- **The asset library** (`set_foreign_material_loader`): a .mat Blendity's own reader refuses goes through the
  Unity loader, and the material is `foreign`. `save_dirty_material_assets` never writes a foreign one, so Unity's
  file stays Unity's and an edit lasts the session.
- **The renderers:** each `m_Materials` slot's GUID loads its .mat; a slot without one keeps the model's material.
- **The GUID index** is shared and kept 5 s for the loader (many materials load at once). An import reads it fresh.
- **VRChat's components** (their scripts live in DLLs, so the GUID names the DLL) are named by their fields:
  VRCAvatarDescriptor, VRCPhysBone, VRCPhysBoneCollider, VRCSceneDescriptor, UdonBehaviour.
  - **A PhysBone** also adds a SpringBone on its root transform, as settings:
    - stiffness = pull (Simplified integration) or (pull + stiffness) / 2 (Advanced);
    - damping = 1 - spring;
    - gravity -1..1 and radius as they are.
    The settings move nothing yet: the import makes no Bone components (skinned meshes come in unbound), and a
    PhysBone on a forked root will want a spring bone per branch once they do.
- **Fixed from round 370:** an object could hold one Engine Component only (a second script replaced the first).
  Engine Components aren't unique any more.

## The Opus review's fixes
- **Majors:**
  - **A Duplicate or Save As of a converted material** kept it `foreign`, so its edits were never saved - the very
    workaround the docs gave. `create_material_asset` makes it Blendity's own.
  - **Unity's colours are gamma** (as the VRM reader already knew). Colours, shade, outline and emission (an HDR
    channel past 1 by the same curve's power) are made linear.
  - **A metallic map turned metal off** (Unity ignores the slider, left at 0). With the map, metal is 1, times its
    red, and roughness is 1 - _GlossMapScale (Standard) or _Smoothness (URP).
  - **The PhysBone claims:** the docs said the hair moves in Play, but no Bone components exist after an import. The
    lesson, quiz, USABILITY, MANUAL, README and CHANGELOG now say the settings are kept until the bones are bound.
  - **Every lilToon material got an outline:** only lilToon's outline shaders (lts_o, "Hidden/lilToonOutline")
    draw one. The shader's own declared name is read from its .shader file.
- **Minors:**
  - **MToon** agrees with the VRM reader: _CullMode, _BlendMode, _OutlineWidthMode (default 0; screen mode 3 mm;
    MToon10's width in metres), toony and shift, and the shade colour as a ratio when there's no shade texture.
  - **lilToon's _TransparentMode.**
  - **An empty material slot** stays in place (it had shifted later materials onto the wrong part).
  - **A partly outlined object** is said.
  - **VRChat's contacts** aren't taken for PhysBone colliders.
  - **A project's own script** with PhysBone-like fields isn't made a spring bone.
  - **A shader not in the project** is unknown and said, unless its properties are Standard's.
  - **The GUID index** is refreshed when an unpacking finishes, and non-material YAML walks no index.
  - **The import's summary** counts materials, outlines and spring-bone settings.
  - **The material info** is reset per material.
- **Tests:**
  - linear expected colours;
  - a metallic map with _Metallic 0;
  - MToon's cutout and Cull Off;
  - a plain lilToon without an outline and lilToon's outline shader with one;
  - a duplicated foreign material's edit saved;
  - a shader not in the project said.

## Checked
- **`tests_round371`:**
  - **Standard:** colour, alpha, a 2 x 3 tiled map with its offset, normal at 0.5, metal 0.7, roughness 0.2, Cutout
    at 0.3, emission 2 x red.
  - **URP Lit:** its map, roughness 0.75, Transparent, double-sided.
  - **MToon** by its properties: Toon, shade colour and map, a 0.5 cm outline.
  - **lilToon** by its shader name.
  - **Unity 5's layout.**
  - **An unknown shader:** kept by its main texture, and refused for Blendity's own text.
  - **An avatar prefab:**
    - the lilToon material on its renderer, Toon and foreign, its texture found by GUID;
    - the 1 mm outline on the object;
    - the .mat byte-identical after an edit and a save;
    - the PhysBone's settings as a spring bone on Hair (0.40 / 0.70 / 0.5 / 0.03) and the PhysBone kept;
    - the Avatar Descriptor and an Udon Behaviour named, on the same object as the PhysBone.
  - **The Try-it.**

## Not done
- **The Avatar Descriptor's viseme mapping and eye look** into Blendity's avatar checklist (kept for Unity).
- **PhysBones moving:** the skin's bones bound on import, and a spring bone per branch below a fork.
- **Smoothness from a texture's alpha** (Standard's metallic or albedo alpha), and _OcclusionMap.
- **PhysBone colliders** as spring-bone colliders.
- **Poiyomi's and UTS2's many other features** (matcaps, rim, emission scrolling): Blendity's Toon has its own.
- **Saving an edited Unity material as a Blendity one** in one step (Save As by hand).
