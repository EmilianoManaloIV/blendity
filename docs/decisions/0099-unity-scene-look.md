# ADR 0099: A Unity scene's look as Unity shows it - fog by view depth, the skybox, post-processing, lights' shadows, reflection probes (round 401, task 0378)

## Status
Accepted (2026-11-03). Amends ADR 0098 (the `.unity-lighting` file: version 2).

## Context
Round 400 showed SideRail with Unity's lightmaps at 0.94 of Unity's brightness, and the user asked for lighting,
emissions and the scene's properties "nearly the same and almost indistinguishable" between Blendity and the Unity VCC
projects. What stood between: Blendity's fog was linear by distance (Unity's exponential by view depth was approximated),
its ambient ignored the skybox material, the camera's clear flags and background weren't read, the scene's post-processing
(ACES, +1 EV, bloom) wasn't applied, lights had no shadow type, and reflections came from the sky alone where Unity's
surfaces reflect four baked reflection probes.

## Decision
- **Fog** is Unity's three modes, evaluated on the **view depth** (the distance along the camera's forward, what Unity's
  fog coordinate is), not the Euclidean distance: Linear from Fog Start to Fog End, Exponential `1 - e^-(density x depth)`,
  Exponential Squared `1 - e^-(density x depth)^2` - the same formula in the CPU rasterizer, the GPU rasterizer and both
  path tracers (the GPU kernel's `fog.w` the mode, `fog_color.w` the density). The sky is never fogged (Unity's forward
  path doesn't; the PPv2 fog excludes it in VRChat's worlds). Blendity's far fog colour stays as an extra (a Unity import
  sets both the same). Export and Live Link write the mode and density back.
- **Ambient and skybox:** a Unity scene's Skybox ambient becomes the skybox material's world - `Skybox/Procedural` the
  Sky (turbidity 1 + 2 x Atmosphere Thickness, strength the exposure), `Skybox/Panoramic` and `Skybox/Cubemap` the HDRI
  (the picture, rotation, strength the exposure times the tint over neutral grey), `Unlit/Color` a flat colour, a 6 Sided
  one its tint (not stitched yet); the ambient's Intensity Multiplier the world's strength (it scales Blendity's background
  too - Unity's scales the ambient alone; noted). Flat and Custom ambients are the sky colour (Custom's probe isn't in the
  file). The Reflection Intensity is kept and sent, not applied.
- **Camera:** clear flags, background (gamma in the file, linear here), HDR (kept), FOV Axis (a horizontal field of view
  is turned to the vertical one per aspect in `vertical_fov_deg`), the physical camera (focal length, sensor, lens shift,
  gate fit as the sensor fit). The scene's `Main Camera` is the Active Camera, as before.
- **Post-processing:** a scene's global PPv2 volume's profile goes on the camera the PostProcessLayer sits on: Color
  Grading's tonemapper as the scene's view transform (ACES; Neutral as Filmic; None as Standard), its post exposure as the
  scene's exposure, its white balance, saturation, contrast and lift / gamma / gain wheels as a Color Grading effect
  (PPv2's wheels xyz the colour and w the offset: lift adds, gamma and gain scale), Bloom as a Bloom effect with PPv2's own
  scaling (`intensity = 2^(i / 10) - 1`, the threshold gamma, diffusion a tenth as the scatter), Vignette as a Vignette.
  Effects Blendity doesn't draw (ambient occlusion, depth of field, chromatic aberration, grain, auto exposure, motion
  blur) stay in the kept volume and are said. The Scene view looks through the active camera's filters (ADR 0016), so it
  shows the same look as the Game view and F12.
- **Lights** carry Unity's Shadow Type (None, Hard, Soft) and Strength, Indirect Multiplier, Cookie and Cookie Size, and
  Culling Mask. No Shadows is honoured in the views (the sun's shadow map) and the CPU path tracer (no occlusion test for
  that light); Hard and Soft draw as before. The cookie is kept and sent, not drawn yet. A light's colour is gamma in
  Unity's file, as a material's: read as linear (it had been read as it was).
- **Reflection probes** are a component of Blendity's (`ReflectionProbe`: Unity's mode, box size and offset - in world
  axes, scaled by the object's scale - box projection, intensity, blend distance, importance, resolution, HDR, clips,
  culling mask, custom cubemap) and a bake: six faces of linear RGB, row 0 the top, the faces laid as OpenGL's (and Unity's
  Cubemap's). The rasterizers sample a bake through an equirectangular texture with mips (the level per roughness as the
  environment map's), box-projected by Unity's `BoxProjectedCubemapDirection` when asked, blended as Unity blends: of the
  boxes holding the point, the most important (then the smallest) first with weight 1 but within Blend Distance of a wall,
  the second filling what it leaves, the sky the rest. The path tracers trace reflections and ignore probes. Bakes come
  from **Unity's** - the lightmap fetch (ADR 0098) writes `.unity-lighting` **version 2** with each probe's cubemap after the
  light probes (its path, box, settings, size, six faces of half RGB as `Cubemap.GetPixels` gives them - the top row first,
  unlike `Texture2D.GetPixels`, checked against Unity's own bake: the sky in row 0 of every side face; version 1 files
  still read) - or from **Blendity's**: Generate Lighting's last phase renders each probe's six faces with
  the path tracer from its position (Baked and Realtime probes; Custom keeps its cubemap). Bakes live in
  `ReflectionProbes.bin` beside the lightmaps (magic, version, per probe its object id, size, source, a checksum, the faces
  as floats), saved with the scene, cleared with the baked data. Live Link sends the probe (Unity's own component on the
  object); its bake isn't sent yet.
- **Write-back:** Export Unity Package patches a Camera's, a Light's and a ReflectionProbe's documents by class and
  GameObject, and the scene's RenderSettings by class (fog, ambient, reflection intensity) - only the lines that differ,
  nested keys like `m_Shadows/m_Type` inside their mapping. The scene remembers the `.unity` it was opened from
  (`unity` block in the scene file).

## Consequences
- An opened Unity scene looks as its Game view does: SideRail's blue exponential fog, black sky and ambient, ACES with
  +1 EV and bloom, and its metal reflecting the rooms Unity baked (once the lightmaps are fetched).
- Fog by view depth changes Blendity's own fog at a picture's edges (it was by distance): a corner at 27 degrees off the
  axis fogs about 10% less than before at the same distance.
- A probe's faces from Unity are taken as OpenGL's layout with the top row first: the row order is checked against
  Unity's bake of the default sky (the sky in row 0 of every side face); the columns' order (a face mirrored left to
  right would pass that check) is taken as OpenGL's until the round-404 harness checks the whole layout on a
  six-coloured box with a marker. The round-401 test pins Blendity's own bake that way.
- Unity's built-in pipeline takes a light's colour x intensity in gamma (`lightsUseLinearIntensity` off): a Unity scene's
  lights are lit that way in Blendity (`Lights Gamma` on the scene's Unity link), so a light at intensity 2 is about 4.6
  times as bright in linear light, as in Unity. Above 1 the curve is a 2.2 power (Unity's own approximation there
  differs a little): round 405's calibration.
- Nothing the user didn't change is written back: the ambient mode as Unity had it (a Custom ambient stays Custom, a
  skybox's intensity as read), a camera's Depth-only or Nothing clear flags and its background alpha.
- Not done: the cookie's projection, the custom cubemap, the probes' bake sent over Live Link, a 6 Sided skybox stitched,
  per-light shadow softness in the rasterizers, the probes in the path tracers (they trace the real room).
