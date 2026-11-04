# Task 0380: the world's VRChat attributes, Unity components as editable data (round 403)

- **Status:** built
- **Requirements:** R-unity-parity (the 2026-11-03 plan: rounds 401-405), Q-tests, Q-docs
- **Decisions:** ADR 0101
- **Model:** main session (Fable)
- **PR:** stacked on #301 (round 402)

## Asked
The same comparison as rounds 401-402: "Implement any attributes that are present in Unity but not present in Blendity"
- this round the world's VRChat components and every kept Unity component as editable data.

## Built (ADR 0101)
- **VRC Scene Descriptor** (`SceneDescriptorComponent`): spawns (by name), spawn order and orientation, the reference
  camera, respawn height and behaviour, forbid user portals, interact-through layers, auto-spatialize, gravity, capacity,
  draw distance, the Unity version - imported from the scene, shown with Unity's names, written back by Export Unity
  Package (the scalars), sent over Live Link (`vrccomponents` kind `scenedescriptor`, the bridge setting the SDK's
  component, spawns by id). **Pipeline Manager** (`PipelineManagerComponent`): blueprint id, content type - the same way.
- **Every kept Unity component editable** (`EngineComponent::reflect_fields`): the document's fields typed by their
  shape under Unity's names, an edit rewriting that line of the kept text; the SDK's classes named by their fileIDs
  (`unity_sdk_types.*`: 36 VRChat classes; `unity_script_file_id`, `unity_sdk_class_name`). SideRail's mirrors, pickups,
  Udon behaviours, audio sources, reverb zones, canvases and particle systems read as themselves.
- **Unity's LOD Group** stays data (review 403: it switches authored children where Blendity's decimates its own mesh).
- **Live Link parity** (the bridge's README): the scene's look, lights, cameras, reflection probes (401), the avatar's
  components (402), the world's (403) - every native component goes both ways.

## Scope
- Out (said): a speaker gizmo and audio playback; canvases and particles drawn (the user's choice); Udon public variables
  decoded from the serialized program; the LOD Group written back; references and lists edited in the generic Inspector.

## Acceptance criteria
1. The fileID formula gives the ids the user's files carry (the descriptor, the PhysBone collider, a rotation
   constraint); an unknown id names nothing.
2. A 12-field document (numbers, text, vector, colour, reference, nested block, list) shows its fields under Unity's
   names; one edit changes that line and no other; serialising keeps the whole text.
3. A scene's descriptor, pipeline manager and LOD group import with their fields; the write-back changes only the edited
   lines; the Live Link message carries them.
4. The suites, the audits.

## Checked
`tests_round403` (the three above); the suites on Windows (both builds) and Linux; the audits. SideRail's own script ids
(its Scene Descriptor -17141911, its Pipeline Manager -1427037861) equal the formula's - local.

## Review (Opus, 2026-11-03) - fixed
- A generic edit never reached Unity: an edited kept document now goes back whole through Export Unity Package's edits (when it differs); a document a Blendity component holds is read-only text (`EngineComponent::native`).
- The respawn behaviour's order is the SDK's (Respawn 0, Destroy 1) in the component, Live Link and the bridge.
- Every number is a float field (Unity writes whole floats short); digits past a float's or led by zeros stay text; inline Vector2 / 4 shown; a wrapped string's continuation lines go with the rewritten line; the parse is cached; labels that clash with the component's own are suffixed; nested bookkeeping keys hidden.
- Unity's LOD Group no longer becomes Blendity's (it would decimate the wrong mesh); the blueprint id written as YAML text; the imports' casts clamped; the bridge takes a GameObject reference and never a shorter spawn list; the SDK table without the .cs script and unverified names (31 classes).
- Not taken: the write-back still writes a native component's fields wherever Blendity's value differs from the file's (as rounds 401-402), so a line changed in Unity since the import goes back to Blendity's value; references resolved by name.

## Not done
- The SDK checks (`run_sdk_tests.py`) extended with the world fixture, and the bridge's ReplayTests with the new messages
  (round 404's harness round).
