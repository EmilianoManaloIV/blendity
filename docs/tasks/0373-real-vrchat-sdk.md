# Task 0373: the Unity bridge against VRChat's real SDK (round 396)

## Asked
"VCC is installed so you can test the actual world and avatar VRChat SDK" - the Test Avatar and Test World projects
(SDK 3.10.6, Unity 2022.3.22f1); plan cozy-fluttering-corbato, round 396. Never upload.

## Built
- **`bridges/unity/run_sdk_tests.py`:** copies each VCC project into a work folder (`C:/blvrc` by default; its Library
  too, without Temp, Logs and the PackageCache - Unity unpacks that again, and its paths pass Windows' 260 characters;
  a copy cut short goes on where it was), adds the bridge from the repository and runs, per project, in batch mode:
  - `SdkCheck/BlendityVrcChecks.cs`: Blendity's recorded Live Link session replayed through the bridge onto the SDK's
    real types, then the SDK's own verdict - the control panel's checks for the avatar or the scene (its errors and
    warnings, run without its window through reflection), and for an avatar `AvatarPerformance.CalculatePerformanceStats`'
    values and rank per category on PC and Android. A JSON report per project;
  - the bridge's replay test (`ReplayTests`) on the real types.
  `--update` writes the avatar's report to `tests/fixtures/vrchat_sdk_avatar.json`. A check fails when the SDK lists
  an error, or when its lists or rank can't be read (an SDK that renamed them is said, not read as none). The copy is
  refused inside the project (or holding it), over a folder that isn't this script's copy, or when the copy is of
  another project; each Unity run has an hour.
- **What the real SDK found, fixed:**
  - the avatar held no body: the recorded session's Face and Arm sat beside its Rig, and with no common root Live Link
    had put the descriptor on the armature - the SDK counted 0 polygons. Live Link now sets up no avatar until the
    rig and its meshes share a root (its warning said so already and now says why); the recorded session has them
    under "Avatar", as a character's file does (re-recorded);
  - "This avatar does not contain an Animator": the bridge adds one to the avatar root when it has none;
  - "There must be exactly one [pipeline manager] on the same game object as the scene descriptor": the bridge adds
    VRChat's PipelineManager beside a Scene Descriptor (and an Avatar Descriptor) when there is none;
  - "The following component types are found on the Avatar and will be removed by the client: BlendityLink,
    BlendityPart": with VRChat's SDK in the project the link's two bookkeeping components are VRChat's editor-only
    components (`VRC.SDKBase.IEditorOnly`, through a version define on `com.vrchat.base`), which its builds drop;
  - the Budget's Bones row counted Bone components (4 here) where VRChat counts the transforms the skins name (5: the
    armature itself one of them). Both of the Budget's counts now take the skins' joints as a set: a skinned mesh
    selected alone counts its rig, an armature nothing is skinned to counts none, a hair rig inside a body's counts
    once (review 396: the export's count had added a nested rig's joints twice).
- **No body, no avatar** (review 396): an armature with eyes or a Head but nothing drawn under its root sends no avatar
  either.
- **The stand-ins** gain a PipelineManager, and the replay test checks the Animator and the pipeline manager; a world
  project has VRChat's PhysBone type (it is in the base package), so its PhysBones are no longer expected absent.

## Checked
- Test Avatar and Test World (copies): both reports pass and both replay tests pass; the avatar's checks list no error
  (one warning: a generic rig, not humanoid, plays none of VRChat's own animations - Unity's import of the character's
  file decides that); its rank Medium on PC and Android (the test avatar's 3.3 x 4.7 m bounds; two skinned meshes on
  Android).
- tests_livelink: Blendity's Budget of the recorded avatar against the SDK's report - triangles, skinned meshes,
  meshes, material slots, bones, PhysBone components and transforms - every value and rank the same on PC and Quest;
  the Face taken out from under the root sends no avatar.
- The Blender bridge's smoke test on the re-recorded session: SMOKE OK (67 messages, 20 objects); the stand-in replay
  tests on Unity 6 (built-in and URP): both pass. The Godot and Unreal replays weren't run here (they find objects by
  name; the new root is at the origin).
- **A project synced before this round** keeps the descriptor the bridge had put on the armature: remove it by hand
  when the rig and its meshes go under one root (MANUAL).
- **Not done here, the user's:** Build & Test and uploads need a VRChat login.
