# Task 0351: Blender and Unreal into Blendity (round 374)

## Asked
Phase E's sixth round (plan cozy-fluttering-corbato, E6). The user's choice: binary formats go through their own
program.
- **.blend:** read through the installed Blender in the background.
- **Unreal:** a "Send to Blendity" in its bridge, using Unreal's own exporter.
- **Blender's bridge** gets the same button.

## Built
- **Snapshots** (`scene/snapshot.*`, docs/LIVELINK.md "Send to Blendity"):
  - A folder `<name>.blendity-snapshot` holds the program's model and `snapshot.json`, written last.
  - `snapshot.json` carries what the model can't: each object's light and camera in Live Link's own fields, an
    engine class (an Unreal Blueprint's path), custom properties, and a transform for what the model doesn't hold.
  - `read_snapshot` refuses a model path leaving the folder.
- **The import** (`editor/snapshot_ui.cpp`): `import <folder>.blendity-snapshot` (or its snapshot.json, or a drop).
  - The model goes through the import every model takes (`finish_import`: skins, shape keys, clips, materials into
    Assets).
  - In the same Undo step (a new `before_commit` hook), the root takes the snapshot's name. Each object then gets
    its light, camera, transform, and an Engine Component holding its class and properties.
  - A light or camera that came in the model (glTF's, looking down -Z) is turned 180 degrees about Y.
- **.blend:** `import <file>.blend`, a drop, or File > Import Model... (a "Blender files" filter).
  - It runs `blender -b <file> --python bridges/blender/blend_to_snapshot.py -- <out>` on a worker (at once headless).
    The snapshot goes to `Library/Blend/<name>.blendity-snapshot`, imported when Blender is done.
  - `blender path [exe]` names Blender (kept in the preferences); otherwise `find_blender` looks for it.
  - Without Blender, it refuses and names blender.org/download.
- **The inbox:**
  - Live Link's discovery file names `<project>/Incoming`.
  - While Live Link listens, Blendity imports a snapshot there within a second and moves it to `Incoming/Done`.
  - One that can't be moved isn't imported again.
  - `livelink inbox` checks now.
- **Blender's bridge** (`blendity_livelink/snapshot.py`):
  - Send to Blendity (and Selected) in View3D > Sidebar > Blendity.
  - The model comes from Blender's glTF exporter (meshes, skins, shape keys, actions, materials, lights and cameras
    as nodes).
  - Lights and cameras are written in Live Link's fields: `apply.light_energy` turned around (a sun's irradiance /
    pi; a point or spot's power / (4 pi^2 x 0.81) at range 10; an area's / (pi^2 x its area)). Custom properties
    come along.
  - Live Link's own objects are never sent back.
- **Unreal's bridge** (`BlendityLinkSnapshot.cpp`, and in the protocol layer `blendity_link::snapshot_json`, tested in
  Blendity's suite):
  - Send to Blendity and Send Selected in the dock tab.
  - The level goes out through Unreal's FBX exporter as an automated `UAssetExportTask` (no options dialog).
  - Each actor's transform is written in Blendity's axes: (x, y, z) cm is (y, z, x) / 100, both left-handed, and a
    rotation's parts are permuted the same way, so a light's +X front is Blendity's +Z.
  - Lights are written in Live Link's fields, the applier's units turned around (lux / pi, candela / 1000, the half
    cone doubled). A camera's horizontal fov is made vertical by its aspect. A Blueprint's class path is kept.
  - Without Blendity running, the snapshot goes to Saved/Blendity, to be dropped on Blendity.

## The Opus review's fixes
- **Majors:**
  - **Unreal's meshes turned 90 degrees.** Unreal's FBX comes in as (-x, z, y), not the snapshot's (y, z, x). The
    plugin now writes `model_rotation` (a quarter turn about Y), set by the export options it fixes itself, and a
    mesh the model holds keeps the model's placement.
  - **A one-object model** had the snapshot's transform stacked on its root. That object now keeps the model's place
    and its own name.
  - **Live Link's own actors went back in Unreal's FBX** (they were only left out of snapshot.json). Exactly the sent
    actors are now selected for the export, and the selection is put back after.
- **Minors:**
  - Imports wait out Play.
  - Blender runs with `--factory-startup --disable-autoexec`, so a file's scripts and drivers never run.
  - The FBX options are set by the plugin, not taken from the user's last dialog.
  - Light units are converted to candela.
  - A camera's sensor goes to Custom, with its fit and shift.
  - A turned light's or camera's children are turned back.
  - Only actors with a mesh, light, camera or Blueprint are sent, and a light's or camera's own component placement is
    used.
  - Blender's selection is restored.
  - Modifiers are applied unless a mesh has shape keys.
  - A failed snapshot is said aloud.
  - `blender path` refuses a missing file, and `clear` forgets it.
  - The object list is capped.
  - Unreal's Saved path is written in full.

## Checked
- **`tests_round374`:**
  - snapshot.json read, with a model path leaving its folder refused;
  - a snapshot imported: its root named, a class and properties kept, a light and a camera made where the snapshot
    says, one Undo step;
  - **an FBX written as Unreal writes it** (Z up, front -Y), turned by `model_rotation`: a car's nose lands 0.5 m
    along +Z from the headlight the snapshot places, and a lone object keeps its place and name;
  - a refused snapshot is said aloud;
  - the inbox imports a snapshot once and moves it to Done;
  - **a real .blend through the installed Blender 5.2:** a crate with custom properties, a lamp and a camera, the
    camera looking down as Blender's does. It runs with BLENDITY_TEST_BRIDGE; it took about 4 seconds here.
  - the Try-it.
- **Unreal's snapshot read by Blendity** (test_livelink): a yawed sun's place and facing, light units, a spot's cone,
  a camera's fov, a Blueprint's class, and the discovery file's inbox.
- **Blender, by hand:** blend_to_snapshot.py run on a .blend made in Blender 5.2. Its glTF nodes and snapshot.json
  were read.
- **Unreal:** the plugin built with BuildPlugin in Unreal 5.8 before and after the review's fixes. Send to Blendity
  hasn't been clicked in a running editor here; the FBX frame was checked with an FBX carrying Unreal's axis settings,
  not one Unreal wrote.

## Limits
- **One way at a time:** a snapshot is an import, not a live link back. Sending again imports a second copy.
- **Unreal's FBX:** materials come as the FBX exporter writes them (colours, no material graphs). Landscapes,
  foliage and Niagara are left to Unreal.
- **Blender:**
  - What its glTF exporter drops (geometry nodes' instances unless realized, Grease Pencil, volumes) doesn't come.
  - When any sent mesh has shape keys, modifiers aren't applied (applying them would drop the keys): a Mirror comes
    in as half.
- **The Blender job:** there's no Cancel, and it has a 10-minute limit. Quitting Blendity leaves Blender to finish
  writing Library/Blend.
- **A light's or camera's turn:** an imported clip keying its rotation isn't turned with it.
