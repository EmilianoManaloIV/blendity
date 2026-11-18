# 0522: The sky's sun lights the scene, and saved sRGB images carry an ICC profile

- **Status:** done (round 542)
- **Requirements:** Pass 27 plan (Blender 5.3's new features), worker R1, round 542
- **Decisions:** none needed (a new light in the existing light list; a metadata chunk in the existing writers)
- **Model:** main session (Opus) - implementation; test-engineer and code-reviewer agents
- **PR:** (link when opened)

## Goal
1. Blender 5.3's EEVEE draws the Sky texture's sun disc and its world sun extraction turns it into a light, so a sky
   alone lights the scene with its sun. Blendity's Sky world (Hosek-Wilkie) did not: a scene with the Sky world and no
   Directional Light got the sky's ambient and no sun. Now the Sky world's sun lights it in the Scene view's rasterizer
   and in the path tracer alike, with a switch back to the old look.
2. Blender 5.3 embeds an ICC profile in the images it saves in sRGB. Blendity's PNG and JPEG writers now embed the
   standard sRGB profile, so colour-managed viewers show them as sRGB by tag rather than by assumption.

## Scope
- In:
  - `EnvironmentSettings::sun_lights_scene` (Inspector "Sun Lights Scene", on by default) and `sun_intensity`
    (Blender's Sun Intensity, 1 by default), shown on the Sky world.
  - `sky_sun_light()` in a new `src/render/sky_sun.cpp`: the sun as a `RenderLight` (directional, toward the
    sky's sun, Blendity's units: intensity 1 is the clear-noon sun the Sky world's ambient was calibrated to) tinted
    and dimmed by the atmosphere's transmittance at the sun's elevation (Blender's Nishita model in
    `extern/sky/source/sky_multiple_scattering.cpp`, the same code 5.3's sun disc uses), nothing below the horizon.
    Turbidity maps to the aerosol density (turbidity / 3: 3 is Blender's default clear sky, density 1).
  - `Editor::make_lighting` adds it to the light list - which both the rasterizer and the path tracer take - when the
    world is the Sky, the switch is on and no enabled Directional Light drives the sky's sun (a Directional Light
    there already is the sun: it would light twice).
  - `src/image/icc_profile.cpp`: the compact sRGB ICC v4 profile (D50 colourants, Bradford `chad`, the sRGB
    parametric curve), built in code. `encode_png` writes it as an `iCCP` chunk, `encode_jpeg` as an `APP2
    ICC_PROFILE` marker.
- Out (not in this task):
  - A visible sun disc in the Sky world's background (the 0.5-degree disc is drawn by neither renderer; the path
    tracer would light twice through the environment and the light).
  - The sky's sun in lightmap bakes (they gather lights separately; the sky's sun is a realtime light).
  - Solid shading (it draws the scene's lights only, never the world).
  - HDR, EXR and other linear formats: unchanged. Non-colour 8-bit maps (normal, roughness) get the same tag as
    every 8-bit PNG: untagged 8-bit images are read as sRGB everywhere, so no reader changes its reading.

## Likely files
- new `src/render/sky_sun.h/.cpp`; `src/editor/render_view.cpp` (`make_lighting`, one call and the keep's key);
  `src/scene/scene.h/.cpp` (`EnvironmentSettings`); build lists (`CMakeLists.txt`, `build.bat`, `build.sh`).
- new `src/image/icc_profile.h/.cpp`; `src/render/canvas.cpp` (`encode_png`); `src/image/image.cpp` (`encode_jpeg`).
- `tests/test_render.cpp` (or the subsystem's group), `tests/test_image.cpp`.

## Acceptance criteria
1. A Sky world with no Directional Light: a plane facing the sun is lit by the sun (its rasterized brightness rises
   well above the ambient-only render).
2. The switch off: the light list and the render match today's exactly.
3. A Directional Light in the scene: the sky's sun is not added (one sun, as before).
4. The rasterizer and the path tracer agree within tolerance on the sun-lit plane.
5. The sun dims and reddens toward the horizon and is gone below it; NaN or zero directions refuse cleanly.
6. A saved PNG holds an `iCCP` chunk before `IDAT`, a saved JPEG an `APP2 ICC_PROFILE` marker; Blendity's own
   decoders (and libpng / libjpeg in the library build) load them to the same pixels as before.
7. The profile is a valid ICC v4 display profile: size field equals its length, `acsp` signature, RGB / XYZ, D50
   illuminant, every tag inside the profile.

## Tests and checks to run
- Unit: `BLENDITY_TEST_FILTER="sky sun"`, `"ICC"`, then the round's groups and `editor` on Windows and Linux.
- Picture rows (rasterizer vs path tracer) in a graphics-card window from the hub.

## Risks and edge cases
- The lighting keep (round 506) must miss when the switch, the intensity, the turbidity or the world mode changes.
- A sun at or below the horizon, a zero or NaN sun direction.
- Scenes saved before this round get the new default (on): breaking for Sky worlds without a Directional Light, as
  in Blender 5.3; the switch gives the old look back.

## Documentation to update
- `changelog.d/542-sun-disc-and-icc.md` with README and USABILITY lines; `licenses/README.md` rows for the
  Nishita file now compiled (MIT) and the ported sun extraction.
