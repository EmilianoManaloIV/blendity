# Task 0372: Unity scenes and prefabs as Unity shows them, and Filamented (round 395)

## Asked
"I have a VRChat world unitypackage here, see if you can import these models into the editor and place them with
textures, shaders, etc." (SideRail_Ver1.0.0, a 500 MB world package); "support .unity scenes and .prefab objects";
"Blendity should also have filamented shader supported by default." (plan cozy-fluttering-corbato, round 395).

## What the first import showed
The package unpacked and `SideRail.unity` opened with its 634 objects placed right, but every model came in grey:
- the models keep their materials inside the FBX, and the scene puts the package's .mat files on them as **prefab
  overrides** (`m_Materials.Array.data[n]`) on the model's parts. Those overrides name each part by the fileID Unity
  gave it on import, which Unity 2019.3+ doesn't write into the model's .meta; the import had left all 1,558 overrides to
  Unity;
- 42 mesh parts weren't found by name ("the model's first mesh used");
- a nested prefab's own .mat files came in as its model's materials (the nested import had no material converter);
- the console's `import Assets/...unity` didn't find a path relative to the project, and the scene came in on top of
  the starter scene.

## Built
- **A model's part numbers** (`scene/unity_scene`, `core/hash.h`):
  - Unity's numbers for a model's meshes and materials are xxh64 of `Type:<type>-><name><n>` read as signed (checked
    against Unity's own table for the package's 32 models: every mesh and material). A MeshFilter's mesh is matched
    by it first; the 42 parts are now found;
  - a model's root has the same numbers in every model (the GameObject, Transform, renderer and filter; persistent and
    legacy), and a legacy .meta names its parts (`fileIDToRecycleName`);
  - the nodes' and their components' numbers aren't a hash Blendity could find (paths, parents, FBX node ids and
    seeds tried against Unity's table). So **Unity is asked once** when it's installed (Unity Hub's, or
    `BLENDITY_UNITY`): `unity modelids [all] [wait]` (and the import's message's Fix, "Number Parts with Unity") copies
    the models with their own .meta files into a scratch project in the cache, runs an editor script in batch mode on a
    worker, and writes `Assets/.unity-model-ids` (`<guid> <fileID> <class> <path>`). The next import uses it;
  - a model with a single renderer takes a renderer-only override (materials, shadows, lightmap scale) without numbers;
  - a model with one top node: Unity's root is that node - its renderer is the root's renderer.
- **Overrides applied where Blendity holds the field**, by the target's class: a renderer's material slots and slot
  count, its switch, shadows (cast and receive) and lightmap scale; a light's switch, colour, intensity, range, spot
  angles and bake mode; a GameObject's name, active state and static flags (Contribute GI, a renderer's in Blendity);
  transforms (as before). The rest is kept for Unity and counted. A nested prefab's and a variant's components are
  override targets now (their stripped documents included), so a variant of a variant's material lands.
- **The renderer's own fields** read too: shadows, lightmap scale, and Contribute GI from the GameObject's static flags.
- **Filamented** (Silent's, `scene/unity_material.cpp`), from its UnityStandardInput: `Silent/Filamented` as Standard;
  `(Roughness setup)` with `_Glossiness` the roughness and `_SpecGlossMap`'s red the roughness map; `(Specular setup)`'s
  specular colour as a metal estimate (its brightest channel over the 4% of plastic) with the base mixed toward it;
  `(Cloth setup)`'s specular colour as the sheen; the Extras as Filamented. The occlusion map has no slot: said.
- **Paths**: `import` and `open` of a .unity or .prefab find a path relative to the project or Assets.
- **A .unity opens as a scene**: `open <file>.unity`, File > Open (the Unity Scene type) and dropping one on the window
  start a new scene - no starter objects, the objects at the top as Unity's Hierarchy shows them, nothing to undo,
  asked first when the scene has unsaved changes - and frame it. The console's `import` still adds one to the scene.
- **The scene's lighting**: RenderSettings' ambient (Flat as Color, Trilight as Gradient, with its intensity) and fog
  (linear as it is; exponential as linear reaching 95% where Unity's does). A skybox ambient, probes and baked
  lightmaps stay Unity's, said once each (the lightmaps need Unity's UVs; the file goes back unchanged).

## Checked
- **SideRail** (the user's package, in a project of its own; not in the repo): before, 0 material overrides on models
  and 42 mesh parts on the first mesh; after, 425 Unity materials converted, 2,519 overrides applied (2,193 before
  Unity numbered the parts, 329 then waiting on 9 models), 0 waiting, 0 mesh parts on a guess; the walls, the bar, its
  stools and the bench wear their textures (screenshots in the session). The scene itself is dark as Unity's is without
  its lightmaps: black ambient, deep blue fog, Baked lights.
- tests_assets: xxh64 against the reference and Unity's numbers; a table, root numbers and a legacy table; a single top
  node; RenderSettings; the four Filamented setups (and a dark specular as plastic); a scene with a model's material,
  a part turned off and its shadows by the table, a lone renderer's override, a variant of a variant's material, a
  nested prefab's .mat, a light's overrides, static flags and a renderer's shadows, a relative path, the scene opened
  (no starter objects, at the top, its fog, nothing to undo), and without the table the two-renderer model's override
  waiting; round 370's lamp keeps its intensity override now.
