# Task 0358: VRChat's setup in Unity through Live Link (round 381)

## Asked
Phase B (plan cozy-fluttering-corbato):
- **An avatar project:** when the VRChat SDK is in the Unity project, the bridge sets the avatar up - an Avatar
  Descriptor (view position between the eyes, Lip Sync on Viseme Blend Shape from the vrc.v_* keys, Eye Look) and
  PhysBones from Blendity's spring bones.
- **A world project:** a Scene Descriptor and the static flags (from Contribute GI).
- **Without the SDK,** nothing VRChat-specific is set.

## Built
- **Blendity:**
  - An `avatar` message for each armature that has eyes (LeftEye and RightEye), a mesh skinned to it with vrc.v_*
    keys, or a humanoid Head:
    - its avatar root, the nearest object above the armature and every mesh skinned to it;
    - the view position (between LeftEye and RightEye at rest, else a little above the Head), in the avatar root's
      space;
    - the mesh carrying the vrc.v_* keys, and each of the 15 names;
    - the eye bones;
    - the blink keys.
  - A `springs` message per armature: each SpringBone chain's root bone, stiffness, damping, gravity and radius.
  - The object message's `static` (Contribute GI).
  - Sent at a sync and once a second when changed. These messages have no coalescing key: the key's 4 bits are taken.
- **Unity:**
  - **Static flags:** set for every `static` object, SDK or not (Unity's own lightmapping).
  - **Avatar Descriptor and PhysBones** (VRChat's avatar SDK present): made through reflection, so the bridge compiles
    without the SDK, by the SDK's field names.
  - **Scene Descriptor** (VRChat's world SDK present): on the Live Link root, its spawn the root.

## How Blendity's springs become PhysBones
The importer's mapping (round 371, `unity_scene.cpp`) turned round, so a PhysBone comes back as it went:

| PhysBone | From the SpringBone |
|---|---|
| Root Transform | the chain's first bone |
| Pull | Stiffness |
| Spring | 1 - Damping |
| Gravity | Gravity (-1 to 1) |
| Radius | Radius |

- **Where the PhysBones go:** a child "Blendity PhysBones" of the armature (marked with a BlendityPart), made again
  each time.
- **Where the Avatar Descriptor goes:** the avatar root's.
- **What the link writes on the descriptor:** it owns the fields it sets and writes each group (the view, lip sync,
  eye look) only when Blendity's values for it change, so tuning a field Blendity doesn't send survives. Lip sync goes
  to Default and Eye Look off when the visemes or eyes go. An armature no longer an avatar leaves its descriptor as it
  is.
- **The Scene Descriptor:** made only when the scene has none (a world's VRCWorld prefab has one).
- **Static flags:** taken off only where the link put them.

## The review's fixes (Opus, round 381)
- **Main-thread cost:**
  - each look used to scan the scene twice per armature, and every armature fell due on the same tick (a 200 ms
    spike a second with 200 rigs);
  - the skinned meshes are now indexed during the object loop;
  - at most 4 armatures are looked at a tick;
  - the humanoid analysis is cached by the skeleton's structure.
- **The descriptor** went on the armature, outside its meshes; it now goes on the avatar root.
- **Scene Descriptors:** a second one had been added beside a world's own.
- **The PhysBones' holder** is marked, so it goes with its armature.
- **An armature that stops being one** says so.
- **A hair rig's chains** are no longer sent twice.
- **The PhysBone mapping** is now the importer's turned round.
- **Robustness:** a destroyed eye, a renamed enum value or a wrong field type no longer stops the link (null checks,
  defined enums only, type checks, a warning instead).
- **Stale descriptor fields** are cleared, and fields are written only on change.
- **Static flags** are cleared only where the link set them.
- **Testing:**
  - the stand-ins and their runner are in the repository (`bridges/unity/VrcStandIns`, `run_replay_tests.py`);
  - the URP project is a world project;
  - a test covers the root, a blink on its own mesh, a hair rig, a chain taken off, and the Armature taken off.

## Checked
- **`tests_round381`:**
  - the view between eyes 6 cm apart at 1.6 m;
  - the Face's 15 visemes and blink;
  - the eye bones;
  - Contribute GI's `static`;
  - nothing sent while unchanged;
  - a SpringBone's chain;
  - the eyes deleted (still an avatar by its visemes);
  - the Try-it.
- **The recorded session:**
  - its rig gets eyes at 1.5 m in its own space (3 m in the world: the rig is scaled x2);
  - a Face skinned to it carries the 15 visemes and a blink;
  - the Box is static;
  - Lower gets a spring chain at the end.
- **Unity 6000.6.7f1** (`py bridges/unity/run_replay_tests.py <Unity.exe> <work folder> [<manifest>]`):
  - **The built-in project** is an avatar project, with stand-ins of VRChat's avatar SDK types under their full names
    and documented fields (VRC_AvatarDescriptor, VRCAvatarDescriptor with CustomEyeLookSettings, VRCPhysBone). It gets
    the descriptor (the view at (0, 1.5, 0.05), lip sync, the 15 visemes, eye look, the blink's index) and a PhysBone
    on Lower with Pull 0.4.
  - **The URP project** is a world project (a VRCSceneDescriptor stand-in). It gets one Scene Descriptor and nothing
    of an avatar's.
  - **Both** get the Box's static flags.
- **Godot 4.7.2, Blender 5.2 and Unreal 5.8:** replay the new recording (they ignore `avatar` and `springs`).

## Your own check in a VRChat project
1. In a Unity project made by the VRChat Creator Companion with the Avatars SDK, install the bridge
   (`livelink install unity <project>`) and connect.
2. In Blendity, give a rigged character its eyes and visemes (Avatar window: Eye Tracking, Create Visemes) and turn on
   Live Link.
3. In Unity, select the character's root:
   - its VRC Avatar Descriptor should show the view between the eyes, Lip Sync "Viseme Blend Shape" with the 15
     visemes filled, and Eye Look with both eyes;
   - a child "Blendity PhysBones" should hold a VRC Phys Bone per spring chain.
4. In a world project (Worlds SDK), the "Blendity Live Link" root should get a VRC Scene Descriptor.

If a field stays empty, VRChat renamed it; the bridge leaves fields it can't find alone.

## Limits
- **The real SDK isn't installed here.** The field names are VRChat's documented ones, checked against stand-ins;
  "Your own check" above is how to confirm them.
- **The PhysBone mapping is a starting point,** to tune in Unity.
- **Godot and Blender take no VRChat setup.** Godot's meshes already count for its global illumination by default.
