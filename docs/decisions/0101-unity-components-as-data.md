# ADR 0101: Unity's components as editable data; VRChat's SDK classes named by their fileIDs (round 403, task 0380)

## Status
Accepted (2026-11-03). Extends ADR 0095 (Unity packages) and ADR 0100 (the avatar's components).

## Context
A Unity world carries components Blendity doesn't run - audio sources and reverb zones, animators, canvases, particle
systems, Udon behaviours, VRChat's mirrors and pickups. Round 370 kept them as Engine Components: the document's text,
shown as "N lines, kept as the engine wrote them", named "MonoBehaviour (guid:fileID)" when the script lives in a DLL
(every VRChat SDK class does). The user asked for Unity's attributes present in Blendity; a component one can't read or
edit isn't.

## Decision
- **The document's own fields are the Inspector.** `EngineComponent::reflect_fields` parses the kept text and shows each
  top-level field by its shape: a number as a number (an integer when written as one), text as text, `{x, y, z}` as a
  vector, `{r, g, b, a}` as a colour, a reference as read-only text ("guid ..., fileID ..."), a nested block's scalars
  one level in ("Limits / Max"), a list as its count. Unity's keys become labels as Unity's Inspector shows them (`m_`
  dropped, the camel case split - `unity_field_label`). An edit rewrites that one line of the text (`unity_set_field`:
  the same line replacer the write-back uses) and no other, so the document goes back to Unity as Unity wrote it but for
  the edit. Unity's bookkeeping fields (hide flags, the prefab links, the GameObject, the script) stay hidden. Saving,
  undo and hashing keep the whole text as before.
- **A DLL's class by its fileID.** Unity's fileID of a class in a DLL is the first four bytes of
  `MD4("s\0\0\0" + Namespace + Class, no dot between)` as a little-endian int32. `unity_sdk_types.cpp` carries the VRChat SDK's (and
  Light Volumes') class names and names a kept component by them (`VRCMirrorReflection`, `VRCPickup`, `UdonBehaviour`),
  checked against the ids the user's files carry (the descriptor 542108242, the PhysBone collider -1631200402). A class
  not in the list keeps "MonoBehaviour (guid:fileID)".
- **The world's two VRChat components as Blendity's** - the Scene Descriptor (spawns, their order and orientation, the
  reference camera, the respawn height and behaviour, portals, interact layers, gravity, capacity, draw distance) and the
  Pipeline Manager (blueprint id, content type) - as the avatar's are (ADR 0100): imported, shown, written back, sent
  over Live Link in `vrccomponents`. The kept document of a component a Blendity component holds is read-only text (the
  fields live on the component); an edited kept document goes back to Unity whole through Export Unity Package. Unity's
  LOD Group stays data: it switches authored child renderers, where Blendity's decimates the object's own mesh.

## Consequences
- Every kept Unity component is readable and its scalars editable in Blendity, under Unity's names; the user's own
  scripts too. Lists, references and deeper blocks are shown, not edited (the text keeps them).
- The audio, canvas, particle and animator components are data here (edited, written back), not drawn or played - the
  user's choice for canvases and particles; a speaker gizmo and the clip's playback are a later round.
