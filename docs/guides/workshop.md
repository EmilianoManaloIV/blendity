# Steam Workshop characters and maps

Subscribe to the items in Steam and let the downloads finish. In Blendity, open **File > Import > Source Engine
Assets** and select the installed item folder, a loose compiled model or map, or a VPK. Choose the model or map
by its relative path. Import each entry you want in the scene.

## Find the installed content

Look in the Steam library that holds the game. These are places to look for the compiled files. Confirm the
item ID against the Workshop item's URL.

| Title | Files to select |
| --- | --- |
| Classic Source Filmmaker (1840) | Look for loose content under the installed Workshop item or Source Filmmaker's `game/workshop` folder. Select the root above `models`, `materials` and `maps`. For a character, keep its `.mdl`, `.vvd` and `.vtx` companions together. Select a `.bsp` for a map. |
| Half-Life: Alyx (546560) | Start at `steamapps/workshop/content/546560/<item-id>/`. Select the item VPK or the compiled files from the add-on's game content. Models use `.vmdl_c`. A map package contains compiled map and world resources. |
| Counter-Strike 2 (730) | Start at `steamapps/workshop/content/730/<item-id>/`. Select the downloaded folder, which can contain a multipart item with a map VPK inside it. Choose the map entry and check its provider if the same name appears more than once. Characters may be in a separate item or in a mounted game archive. |

For a multipart VPK, select the `_dir.vpk` with its numbered siblings in the same directory. If Blendity
reports a missing virtual path, mount the game content or Workshop item that provides it and retry. Check the
mount order if two roots contain the same path. An unsupported version message identifies a format gap to
record with the item ID and resource path.

## Pose and render

1. Import the map and character into the same scene. Check their scale and alignment in the Scene view.
2. For a character that already has a skin, select its armature or a bone, then choose **GameObject > Rig >
   Auto IK on Selected Skeleton**. Move the hand and foot targets that Blendity finds. For a limb that needs
   its own setup, select its lower bone and use **Add IK to Bone** in the Rigging window.
3. To rig an unskinned mesh, use **Window > Rigging**. Fit a skeleton, select the mesh and bind it with
   **Automatic Weights** or **Envelope**. Alternatively, select the fresh mesh and use **Auto-Rig > Humanoid**
   or **Quadruped**. Place the markers shown in the diagram, then choose **Build Rig**.
4. Position the camera and add suitable lighting. Save the scene and render it. To check that another editor
   session can use the native assets, copy the item to scratch storage for an import, save the scene, move the
   scratch source copy aside and reopen the scene.

The import creates a snapshot in the project. Reimporting the same recognized source updates geometry and
keeps the existing project PNGs and material edits, even when the Workshop item supplies new texture pixels.
When moving a standalone VPK, keep its filename so Blendity can recognize it. Renaming the archive can
create a separate snapshot.

## Compatibility evidence

| Title | Redistributable layout check | Real-file check on Windows |
| --- | --- | --- |
| Classic Source Filmmaker | Windows and Linux passed, including offline reopen | Scout posed in The Warehouse and the scene rendered after removing the source copy. The compiled Scout bind pose lies sideways after Source coordinate conversion. All 5,241 imported positions matched the original vertex data. Place the unskinned copy upright before setting Humanoid markers. Available materials supplied base textures for 45 of 80 slots. [Workshop map](https://steamcommunity.com/sharedfiles/filedetails/?id=791304326) |
| Half-Life: Alyx | Windows and Linux passed, including offline reopen | The AlyxMP avatar posed in the Bad Kitty map and the saved scene rendered after removing both source copies. The avatar and map lacked the base-game textures. A separate author-hosted foliage model retained its 2048 × 2048 cutout texture on offline reopen. |
| Counter-Strike 2 | Windows and Linux passed, including offline reopen | On app build 26445667, the SAS agent posed using all four limbs and resolved a base texture for all five material slots. The downloaded map added 61 mesh objects. All 75 material slots in the combined scene resolved base textures and the posed scene rendered identically after detaching the game and Workshop copies. Baked map lightmaps were omitted. [crashz' Crosshair Generator v4](https://steamcommunity.com/sharedfiles/filedetails/?id=3070193546) |

Steam's installation documentation describes local Workshop item access. Valve's Source SDK headers define
classic model and BSP records. The Source 2 resource implementation and world loader in ValveResourceFormat
describe the other format family. See the [fixture provenance](../../tests/fixtures/source_workshop/README.md)
for the exact format references and the [verification record](../tasks/0588-workshop-verification.md) for test
results. [Steam Workshop installation](https://partner.steamgames.com/doc/features/workshop/implementation#ItemInstallation)
