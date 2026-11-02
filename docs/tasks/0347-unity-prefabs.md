# Task 0347: Unity prefabs and scenes into the scene (Phase E, part 2)

- **Status:** done (round 370, 2026-10-31)
- **Asked:** "import Unity packages and prefab packs related to VRChat into Blendity (Mesh, Materials, Lights, no-code,
  etc.)" - with code kept in the engines.
- **Plan:** E2. Materials and VRChat's components come next (task 0348), then the way back (0349).

## What
- **`scene/unity_yaml`:** Unity's subset of YAML.
  - "--- !u!<class> &<fileID>" documents, "stripped" ones;
  - block mappings and sequences by indentation, with a sequence at its key's own indent as Unity writes them, and an
    item's mapping starting on its dash's line;
  - flow mappings and sequences;
  - plain, single- and double-quoted scalars, plain ones wrapped onto more-indented lines.
  - A binary-serialized file is refused.
- **`scene/unity_scene`, `build_unity_objects`:**
  - GameObjects (name, active) and Transforms / RectTransforms (local position, rotation, scale; parents by
    m_Father). There is no axis conversion: Unity and Blendity are both Y-up, left-handed, in metres.
  - MeshFilter, MeshRenderer and SkinnedMeshRenderer meshes through a callback; a renderer takes its model's
    materials.
  - Light (Unity's Spot, Directional, Point, Rectangle and Disc to Blendity's types; colour, intensity, range, spot
    angle) and Camera (field of view, clip planes, orthographic).
  - Box, Sphere, Capsule and Mesh (convex or not) Colliders become ColliderComponents with Unity's own sizes.
  - **Nested prefabs (PrefabInstance):** the source prefab is built by its GUID (at most 8 deep). The position,
    rotation, scale, name and active overrides are applied; others are counted and left to Unity. Its roots go under
    m_TransformParent, and stripped documents map to the instance's objects.
  - **Everything else is an `EngineComponent`:** its type is the script's class, from its GUID's file name, or
    Unity's component type. The script's GUID is kept, and its document is kept whole with its header.
- **`editor/unity_scene_ui.cpp`, `import_unity_scene`:**
  - **The GUID index:** the packages' `.unity-guids` (with their `.unity-meta`) and any `.meta` file beside an asset.
  - **A mesh by GUID and fileID:** Unity's built-ins (10202 Cube ... 10210 Quad) become Blendity's primitives. A
    model is read once per import, and its part is chosen by the name its `.meta` gives the fileID
    (`fileIDToRecycleName`, or `internalIDToNameTable`), else its first mesh.
  - The objects go under one root at the view's centre, which is selected; one Undo step; a summary and notes in the
    Console.
- **Entry points:** `import <file>.prefab|.unity`, a drop on the window, and the Import dialog's filter.
- **The package unpacker** (task 0346) now keeps each asset's `.meta` in the folder's hidden `.unity-meta`.

## Checked
- **`tests_round370`:**
  - **The YAML reader on a level written as Unity writes it:**
    - 13 documents;
    - the MonoBehaviour's script GUID, number, quoted string with an escaped quote and colon, and a sequence of
      references;
    - the PrefabInstance's modifications;
    - the stripped Transform;
    - a binary file refused;
    - a .meta's `fileIDToRecycleName`.
  - **The level imported:**
    - Floor (built-in Plane, its scale, a Box Collider at Unity's size);
    - Tri (the OBJ's one face found by its .meta's fileID, turned 90 degrees, under Floor);
    - Spin kept as an Engine Component, its fields in its data;
    - the Camera's values;
    - the Lamp prefab renamed "Porch Lamp" and moved to x = 3 by its overrides, its point light's range, under Floor;
    - one Undo leaving the scene as it was.
  - **The Try-it.**

## The Opus review's fixes
- **Majors:**
  - **A prefab holding itself** (or A holding B holding A) grew exponentially, reading the file again each time. The
    prefabs being built are now shared down the nesting: a second appearance is refused, with one note. Prefab files
    are read once per import, and an import stops at 200,000 objects.
  - **A model placed as a prefab** (an FBX dragged into a scene, a VRChat avatar's usual root) was dropped along with
    the components added to it. Its nodes now come in as objects under one, and stripped objects and added components
    (an Avatar Descriptor, PhysBones) fall to its root. A component whose object can't be found is counted and said,
    never dropped silently.
  - **Objects outside the import's root:** a variant's base, a parent loop, a GameObject without a Transform. Everything
    the import made that is left at the top goes under the root, and a loop's parent is refused.
  - **Every part of a multi-mesh model got its first mesh.** When the .meta names no part (2019+ files often don't),
    the node or mesh named like the object is chosen; the first mesh is used only as a counted, reported fallback.
  - **Names written as \uXXXX** (Japanese, emoji) came in garbled. Double-quoted escapes are now decoded to UTF-8:
    \u, surrogate pairs, \U, \x, \0, \r, \b, \e, \/.
- **Minors:**
  - **The Engine Component's Data** shows as a read-only line count in the Inspector. Files, undo and hashing see the
    whole document.
  - **The kept document is byte for byte:** the header line as written, CRLF kept.
  - **One import reads each prefab, model and .meta once.**
  - **Colliders:** a trigger is kept as Unity's (Blendity's colliders are solid), a capsule along X or Z is noted, and
    m_Enabled is read.
  - **Lights:** inner spot angle, area size, colour temperature, Light Mode (Realtime, Mixed, Baked) and m_Enabled.
    **Cameras:** m_Enabled.
  - **The YAML reader:** a stray deeper line after a value no longer swallows its siblings; a quoted flow key may hold
    a colon.
  - **Children in Unity's order** (m_Children).
  - **m_LocalEulerAnglesHint and m_RootOrder overrides** aren't counted as left to Unity; removed components are noted.
  - **The console's import** matches the path's own extension, so a folder named com.unity.x no longer misroutes it.
  - **A DLL's scripts** (VRChat's SDK) are typed "MonoBehaviour (guid:fileID)", not the DLL's name.
  - **Notes are said once** with how many times they came up.
  - **An import with nothing in it** makes no root and no Undo step.
- **Tests:**
  - a prefab holding itself;
  - a model placed as a prefab with an SDK component added;
  - \u names;
  - a 2019 .meta's internalIDToNameTable;
  - a part chosen by its object's name;
  - a parent loop;
  - a rotation override made whole;
  - the console's path match;
  - CRLF data kept exactly.

## Not done
- **Materials** (.mat to Blendity's, by shader): task 0348. Renderers take the model's own materials until then.
- **Skinned meshes' bones and blend shape weights** (the mesh comes in; the bind to the bones does not yet).
- **Overrides on other components' fields** (a light's intensity override): they're counted and left to Unity.
- **Prefab variants and old (2017) prefab files.**
- **Dragging a .prefab from the Project window into the Scene view:** use import, or a drop from outside the window.
- **FBX space against Unity's transforms** isn't checked against a real Unity project. Blendity's FBX import bakes the
  axis and unit conversion into the mesh, while Unity may keep it on the model's objects (Blender's FBX: -90 degrees
  on X, scale 100). A model's parts may come in turned or scaled until a real Blender-made FBX with its Unity prefab is
  tested.
- **A model placed as a prefab's overrides:** its objects' fileIDs are Unity's hashes of node names, so they stay
  Unity's.
- **Overrides on a grand-nested prefab's objects**, and prefab variants' own overrides past the transform.
