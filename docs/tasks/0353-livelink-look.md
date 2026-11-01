# Task 0353: the world and the look in Live Link (round 376)

## Asked
Phase B's first round (plan cozy-fluttering-corbato, "the world and lighting"), from the user's request: "the ability
to simply do lighting, modeling, and everything in between while bridges are running so everything is seamless".
The world, the render's look and the camera's filters reach every bridge while you work.

## Built
- **Three new messages** (docs/LIVELINK.md):
  - `world`: the mode, gradient colours, sky turbidity, an HDRI path, strength, rotation and fog;
  - `render`: exposure and view transform;
  - `filters`: the render camera's stack.
- **Sending them** (`editor/livelink.cpp`):
  - The world and the look are hashed each comparison and sent when they change. They're always sent at a sync,
    before the objects.
  - Filters are the render camera's stack (the Active Camera, else "Main Camera", else the first), hashed with the
    look. They go at every sync, and when that stack changes, the render camera changes, or it is deleted. An empty
    list means no stack; id 0 means no camera.
  - Coalescing keys now have 4 kind bits.
  - Stats count `worlds`, `renders` and `filters`.
- **Each bridge applies them:**
  - **Blender:** a "Blendity Live Link" world.
    - The Gradient as a ColorRamp on Z, the Hosek-Wilkie sky, an Environment Texture, or a colour.
    - Fog as the mist pass, mixed toward the fog colour by the compositor.
    - The scene's exposure and view transform.
    - A compositor node group: the fog, Glare (Bloom), Exposure, Bright/Contrast, Hue/Saturation, and an ellipse
      vignette.
    - The scene's own world and compositor are kept and put back when the link stops.
  - **Unity:**
    - RenderSettings: Trilight ambient, the procedural or panoramic skybox, and linear fog.
    - A "Blendity Look" object keeping the exposure, view transform and filters, and the scene's own lighting (its
      Restore Scene Lighting puts it back).
    - In a URP project, a global Volume: ColorAdjustments, Tonemapping, Bloom and Vignette, through a version define.
  - **Godot:** a WorldEnvironment.
    - A procedural, physical or panorama sky, or a colour.
    - Depth fog, at full opacity by its end and off the sky.
    - Tonemap exposure and mode.
    - Glow and adjustments. There's no vignette, so it's kept as metadata.
  - **Unreal:**
    - A SkyLight captured from the level: strength, tint, the ground as its lower hemisphere.
    - An ExponentialHeightFog: start, a density within Unreal's range, colour.
    - Look actors adopted again on a reconnect.
    - An unbound PostProcessVolume: exposure bias, Bloom, Vignette, contrast and saturation.
- **The recorded session** (`tests/fixtures/livelink_stream.bin`) now ends its edits with the look: a gradient with fog
  from 5 to 40 m, half a stop up, and Bloom 0.8 and a Vignette 0.3 on the camera. Every bridge's replay test checks it.

## The Opus review's fixes
- **Blocker:** Blender's fog, a world volume, turned a Cycles render black. It's now the mist pass mixed in by the
  compositor. A 32-pixel Cycles render checked it: the average went from (0.82, 0.84, 0.87) to (0.84, 0.84, 0.84)
  with fog, where the volume had given black.
- **Majors:**
  - **Unreal duplicated its look actors on every reconnect.** They're now adopted by class.
  - **A deleted camera's filters stayed in every engine, and several cameras' stacks fought.** Only the render
    camera's stack is sent, at every sync and when it changes or goes.
  - **Godot and Unity were given linear colours as sRGB.** They're converted now.
  - **Blender's own world and compositor were lost on save.** They're kept and restored. Unity's RenderSettings are
    snapshotted with a Restore action. Godot warns about a second WorldEnvironment.
- **Minors:**
  - Godot's depth fog was nearly invisible: its density is now 1.
  - Unreal's SkyLight lower hemisphere and recapture are fixed, and its fog density is within range.
  - The bridges rebuild the sky only on a mode or HDRI change: no leaks in Unity, no per-message EEVEE recompiles in
    Blender.
  - Unity's strength now scales Trilight and Flat ambient.
  - Blender's vignette blur scales with the frame.
  - Godot's AgX needs 4.4.
  - A misplaced comment moved; doc wording.
- **Tests:**
  - a second client syncing;
  - a non-render camera's stack;
  - the render camera deleted;
  - no camera;
  - Unity's restore;
  - Unreal's reconnect.

## Checked
- **`tests_round376`** (test_livelink): the look goes at the sync; a fog, an HDRI, the exposure and view transform, a
  Bloom and the stack's removal each go once, and nothing goes while unchanged.
- **The recorded session's test:** 2 world, 2 render and 1 filters messages.
- **Every bridge on the new recording:**

| Bridge | Test | Result |
|---|---|---|
| Blender 5.2.2 | smoke_test.py | SMOKE OK: world, fog, exposure, gradient sky, Bloom and Vignette in the compositor, the camera's filters kept |
| Godot 4.7.2 | smoke_test.gd | SMOKE OK: procedural sky colour, depth fog 5-40, exposure 2^0.5, glow 0.8 |
| Unity 6000.6.7f1, built-in pipeline | ReplayTests | Passed: Trilight ambient, fog 5-40, the sky colour, Blendity Look's exposure and filters |
| Unity 6000.6.7f1, URP | ReplayTests | Passed: as above, plus the Volume's Bloom 0.8 and exposure 0.5 |
| Unreal 5.8 | Blendity.LiveLink.Replay | Success: fog from 5 m, exposure bias 0.5, Bloom 0.8, Vignette 0.3, the SkyLight |

Unity's built-in run passed, then Unity's own asset-import worker crashed at its exit (inside WorkerManagerASIO). The
URP run exited cleanly.

## Limits
- **Unreal:**
  - No HDRI.
  - The fog is exponential, so it's an approximation of Blendity's linear fog.
  - Unreal's tone mapper is kept, and the view transform isn't applied.
- **Unity:**
  - An HDRI must be a PNG or JPG; Unity doesn't read .hdr or .exr from a path.
  - Without URP, the look is only kept on Blendity Look.
- **Godot:** has no vignette.
- **Filters:** only Bloom, Vignette and Color Grading are drawn by the bridges. The others are kept as data.
- **The material and light colours** Godot and Unity were already given (before this round) are linear too. That's a
  follow-up.
- **Unity's URP Volume** shows only on cameras with Post Processing on.
