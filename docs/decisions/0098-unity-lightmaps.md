# ADR 0098: Unity's baked lightmaps in Blendity - asked of Unity, kept as Blendity's own baked lighting

- **Status:** accepted (round 400, task 0377)
- **Context:** the user asked to "add Unity baked light maps into Blendity": a VRChat world lit by Baked lights (SideRail:
  70 of 71) is dark in Blendity, because Unity keeps which renderer takes which rect of which lightmap - and the second
  UVs it generated for the purpose - in `LightingData.asset`, Unity's own binary serialization, and in its import of
  each model (not in the model file or the scene). The user chose to show Unity's lightmaps, not to write Blendity's
  bake into Unity.

## Decision
- **Unity is asked**, as `unity modelids` asks it for model part numbers: the installed Unity, in batch mode, opens the
  scene in a scratch project in the cache rebuilt from Blendity's copy of the package (each file at its Unity path with
  its `.meta`; no pictures, sound, film or scripts - the scripts don't compile without the package's SDK, and batch mode
  stops on them - except the scene's own lighting folder) and runs an editor script that writes `<scene>.unity-lighting`
  beside the scene (format in `render/unity_lightmaps.h`, version 1, FNV-1a 32 last; it records the scene's
  LightingData.asset's path and an FNV-1a 32 of its bytes, and is refused once Unity has baked again). The scratch
  project is set to Linear colour space, as VRChat's projects are: in a Gamma project Unity hands its HDR lightmaps over
  gamma-encoded. It holds each lightmapped renderer
  (its path - names with their place among same-named siblings - and GlobalObjectId, mesh, lightmap, scale-offset and
  world matrix), each mesh's positions, Unity's lightmap UVs (uv2, else uv) and triangles, the lightmaps' texels (each
  re-imported in the scratch project as a plain readable RGBAHalf texture: Unity writes lightmaps PIZ-compressed, which
  Blendity without Blender's libraries can't read, and in an encoding that depends on its settings), the directional
  lightmaps' texels, the lights' bake modes, and the baked probes evaluated along the six axes by Unity itself
  (`SphericalHarmonicsL2.Evaluate`: no SH convention of Unity's to reproduce).
- **Matching:** a renderer is the object at its path (an opened scene's objects are at the top, an imported one's under
  its root); each of the object's render triangles takes, per corner, the Unity vertex at the same world position
  (within a millimetre) from the Unity triangle nearest its own - the same face, however each side split it. An object
  with under half its triangles found is left out (another mesh). An imported Unity scene sits in Blendity's world as in
  Unity's (no axis change); Unity's lightmap v runs up, Blendity's down. Imported under a root, Unity's world is the
  root's: positions, the directional decode's normals and the probe cubes go through the root's matrix, wherever it
  stands or turns; only lights under the root take Unity's modes. Nothing matched: refused, the scene's bake kept.
- **What it becomes:** Blendity's own baked lighting (`LightingData`): the pages (texel = what albedo multiplies, as both
  engines do - SideRail's mean luminance from Unity's spawn view 0.94 of Unity's own render, against 0.60 without),
  directional pages multiplied once per texel by Unity's decode `(dot(n, dir - 0.5) + 0.5) / max(1e-4, dir.w)` with the
  surface's smooth normal there, each object's entry hashed as the scene is now (moving an object drops its own), the
  lights' modes, Unity's probes resampled into Blendity's probe volume (nearest four, inverse squared distance), keyed to
  the scene so it shows as baked. Applying it turns Baked GI on and Realtime GI off (Unity's VRChat scenes bake; with
  Realtime GI any change hides every lightmap until a bake, Blendity's rule). Saved with the scene like a bake; Generate
  Lighting replaces it; Unity's own files go back to Unity unchanged.

## Consequences
- Needs Unity (Unity Hub's, or `BLENDITY_UNITY`) once per scene; the file beside the scene is used after, with or
  without Unity. The first run makes the scratch project (a few minutes); later runs take seconds.
- The file holds the package's meshes and lightmaps: it stays in the user's project like the package itself.
- Not carried: Unity's per-pixel normal maps in the directional decode (the smooth normal is used), its tetrahedral
  probe interpolation (nearest four instead), reflection probes, Enlighten realtime GI, and terrain.
- A .mat is read through its shader's Properties block when the shader's source is in the project (Unity reads only
  those; a .mat keeps every earlier shader's), and data maps (normal, metallic, roughness) are marked non-colour.
- That the bake came from Unity isn't remembered across saving and opening the scene (it is Blendity's baked lighting
  then); Use Unity's Lightmaps is offered for a scene opened or imported from a `.unity`.
