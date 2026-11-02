# Task 0355: skins and blend shapes in Live Link (round 378)

## Asked
Phase B's third round (plan cozy-fluttering-corbato, "real skinned meshes and blend shapes"):
- A skinned character and its blend shapes go once.
- After that only its bone poses and shape weights stream, instead of 12 bytes per vertex every tick.
- The bridges build real skinned meshes.

## Built
- **The skin outside the editor:** `skin_for_export` (scene/skin.cpp) holds the armature's skeleton and the mesh's
  weights, moved from the editor's export, with each joint's object id (`ExportSkin::joint_ids`). The export adds its
  clip on top.
- **Live Link** (`editor/livelink.cpp`):
  - **A rig:** a mesh posed only by its Skin (normal-only modifiers aside), or carrying shape keys (`rig_of`).
  - **Its mesh:** sent at rest with its skin and morph targets, its joints' inverse binds in the mesh's own space. The
    message names the joints' objects (`skin.joints`) and the keys (`shapes`).
  - **When it's sent again:** only when the rest mesh or its rig changes - the armature, each joint's rest, name,
    parent and deform flag, the Skin's settings, the mesh's place from the armature (compared with a tolerance). A
    rename or a material keeps what the clients hold.
  - **A rig no more** (the armature deleted, Preserve Volume or Rest Position on, more than four influences, another
    modifier) goes as a plain mesh, never as a deform a skinning client would ignore. The skeleton is read on the main
    thread and the weights made on the mesh worker (`skin_joints` / `skin_weights`).
  - **Shape weights:** a new `weights` message when they change.
  - **Deforms:** go only to clients whose hello lacks `"caps":["skins"]` (`Server::client_hello`). Such a client also
    gets the posed vertices right after the rest mesh is built.
- **The bridges skin:**
  - **Unity:** a SkinnedMeshRenderer on the bone GameObjects. Bind poses are glTF's inverse binds mirrored into Unity's
    axes; blend shapes; the weights set once the mesh exists.
  - **Godot:** a Skeleton3D under the mesh (a flat bone per joint) posed from the bone nodes in the mesh's space once
    after each batch of messages (`flush`). A Skin of the inverse binds. Blend shapes (normalized).
  - **Each bridge** takes the skin off a mesh sent again without one, and leaves nothing behind when it is sent again
    or deleted.
  - **Blender:**
    - an armature under the mesh, its bones at the joints' rest, each following its bone empty (its place and turn
      copied, its scale the empty's over the joint's rest scale, so a scaled rig skins right);
    - made in Object Mode only: a user sculpting or editing keeps their mode, the skin waits for Object Mode;
    - vertex groups and an Armature modifier;
    - shape keys on a Basis.
  - **All three:** say they skin in their hello, and ignore deforms for a skinned or keyed mesh.
  - **Unreal:** a skeletal mesh can't be built cheaply at run time, so it doesn't say "skins" and keeps getting the
    posed vertices as deforms.
- **The recorded session** adds a skinned cylinder "Arm" on the rig with a Smile key at 0.25.

## Checked
- **`tests_round378`** (two clients, one saying it skins):
  - the rest mesh with 2 joints, a skin and a morph target, its joints the bone objects;
  - a bone turned sends its pose to both, but vertices only to the plain client;
  - a weight change sends one weights message to each, and nothing while unchanged.
- **The rig as it changes** (review 378): a rename sends no mesh; Max Influences and a bone's rest send the skin
  again; Preserve Volume, Rest Position and the armature deleted send a plain mesh, and the skin again when undone.
- **The recording's test:** one weights message, and three deforms for the plain recorder. Its rig is scaled x2.
- **Blender** applies the Arm's mesh three times: one armature, one modifier, three vertex groups each time.
- **Each bridge, on the recording:** skinned on its own, against the vertices Blendity posed.

| Bridge | Result | The skinned Arm against Blendity's posed vertices |
|---|---|---|
| Unity 6000.6.7f1, built-in and URP | Passed (SkinnedMeshRenderer.BakeMesh) | at most 1.2×10⁻⁷ m |
| Godot 4.7.2 | SMOKE OK (skinned on the CPU from the skeleton's poses and the skin's binds) | at most 3.0×10⁻⁵ m |
| Blender 5.2.2 | SMOKE OK (the evaluated mesh, each of three sends) | at most 1.2×10⁻⁷ m |
| Unreal 5.8 | Replay passed (deforms) | - |

## Limits
- **Unreal** stays on deforms.
- **A rig's mesh with another modifier** (a Mirror, a Subdivision) streams its vertices as before; so does one on a
  Preserve Volume armature, at Rest Position, or with more than four influences.
- **A joint scaled unevenly and turned** from its rest skins with Blender's scale per bone axis (a shear is lost).
- **Sculpting or editing a rig's mesh** sends the rest mesh again, whole, as it changes.
- **Blender's armature** is rebuilt with each rest mesh.
