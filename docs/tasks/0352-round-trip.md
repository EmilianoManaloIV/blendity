# Task 0352: the round trip as a production (round 375)

## Asked
Phase E's last round (plan cozy-fluttering-corbato, E7). The round trip is run as a production:
- a VRChat avatar package and a world kit go through Blendity and back into Unity;
- then the same with a Godot scene, a .blend and an Unreal level.

Fixtures are made by the tests. Real files come only from the user or their installed programs. The HCI report gains
an import column.

## Run
- **Unity (`tests_round375`, the first test):** one package holds an avatar and a world kit.
  - **The avatar:** a lilToon .mat with its outline shader, a PhysBone, the Avatar Descriptor.
  - **The kit:** a kiosk with a UdonSharp script, Udon, a Box Collider, a lamp.
  - **In Blendity:** the package is unpacked and both prefabs are brought in, then edited:
    - the avatar is renamed;
    - the kiosk is moved;
    - the lamp is switched off.
  - **Back:** Export Unity Package. The package carries:
    - the same GUIDs;
    - the .mat, the script, the shader and the texture byte for byte;
    - every VRChat component's document unchanged;
    - prefabs that differ from the originals by exactly the three edited lines.
  - **Imported again**, as Unity would see it: the edits, the Toon material and the spring bone are all there.
- **Godot:** a scene with a rotated kiosk, a script and a turned sun.
  - Exported with nothing changed, it comes back byte for byte (CRLF too).
  - Moved and exported, then imported again, the kiosk is where it was put, with its rotation and script.
- **Blender** (BLENDITY_TEST_BRIDGE; the installed Blender 5.2):
  - A .blend comes in through Blender, the crate is moved, and the scene is exported as glTF with Keep Transforms.
  - Opened in Blender again, the crate is where Blendity put it: Blendity's (4, 1, -3) is Blender's (-4, 3, 1).
- **Unreal:** round 374's FBX frame test stands for the way in. There is no way back onto the sent actors yet. Live
  Link streams Blendity's objects into Unreal as new actors, beside the level's own (a snapshot's objects don't carry
  their actors' ids).

## What the production found, and fixed
- **A .blend (or any snapshot) with one object lost its name.** Its root took the file's ("Crate" came in as
  "Shed"). Round 374's review had noted the snapshot's rename; the import's own naming was the rest.
  - A snapshot's lone root now carries its object's name.
  - A root that is the model's one object isn't renamed for the snapshot.
- **Export baked every object's transform into its vertices**, so a round trip lost pivots and rotations.
  - glTF has a new **Keep Transforms** option (the Export dialog; `export glb ... transforms`): the object's
    transform goes on its node, and its vertices stay around its own pivot, as Blender's glTF export writes them.
  - Skinned meshes are unchanged.
  - It's off by default, so earlier exports stay as they were.
- **A prefab file named as its root object** ("Avatar.prefab" holding "Avatar"): the import's root and the object
  share the name. This is Unity's naming too, and the Hierarchy shows both; it's noted, not changed.

## The Opus review's fixes
- **Majors:**
  - **Keep Transforms wrote node matrices glTF forbids.** A turned child under a stretched parent has a sheared world.
    Such an object, a mirrored one, or one that isn't finite is now baked into its vertices.
  - **The lesson and spec said Live Link carries changes back onto Unreal's sent actors. It doesn't.** They now say so.
- **Minors:**
  - The export's Scale goes on positions, not on each node's own scale.
  - `transforms` on a non-glTF export is said.
  - The option survives a GLB/glTF switch.
  - The HCI section's wording.
  - The test now uses a lopsided shape, compares vertex sets and face winding, and covers the sheared and mirrored
    cases.

## Checked
- **`tests_round375`:**
  - the Unity production;
  - the Godot round trip;
  - the Blender round trip (BLENDITY_TEST_BRIDGE; about 6 seconds);
  - Keep Transforms: a turned, moved, scaled lopsided block exported and imported again lands on the same world
    vertices, faces outward, on its node; a sheared child and a mirrored object are baked and come back right;
  - the Try-it.
- **docs/hci/compare-2026-10.md:** "D. Round trip" adds the import column.

## Limits
- **Not done here:** the real Unity, Godot and Unreal editors importing what Blendity exported. Unity and Godot read
  back through Blendity's own readers, which are written to their formats.
- **Keep Transforms** is glTF only. FBX still bakes the transforms.
  - Every object is written as a top-level node: parents aren't kept.
  - Blendity's own glTF reader doesn't undo a mirror on a node, which is why mirrored objects are baked.
- **No way back onto Unreal's sent actors yet.**
