## 2026-11-15 (round 542): The sky's sun lights the scene; saved sRGB images carry an ICC profile

**Asked:** Pass 27 (Blender 5.3's new features), worker R1: the Sky texture's sun disc lighting the scene, as 5.3's
EEVEE now does, with a switch for the old look; an ICC profile in saved sRGB PNG and JPEG files, as 5.3 writes.

**Changed (task 0522)**
- The Sky world (Hosek-Wilkie) lights the scene with its sun when no Directional Light drives it: one directional
  light toward the sky's sun, in the light list the Scene view's rasterizers and the path tracer both take, so they
  agree. Its colour and strength are the sun's light through the atmosphere at its elevation - Blender's Nishita
  transmittance, the code 5.3's sun disc uses (`extern/sky/source/sky_multiple_scattering.cpp`, now compiled) -
  scaled so the noon sun is intensity 1, the sun the Sky world's ambient was calibrated to; it dims and reddens
  toward the horizon and is gone below it. Turbidity sets the haze (3, a clear day, is Blender's aerosol density 1).
  The disc made one light is EEVEE's world sun extraction (`sun_extraction`), ported in `src/render/sky_sun.cpp`.
- World > Sky: **Sun Lights Scene** (on; off is the look before 5.3) and **Sun Intensity** (Blender's). A scene with
  a Directional Light is unchanged: that light is the sun, and the sky's sun is not added on top of it.
- Every 8-bit PNG Blendity writes carries an `iCCP` chunk and every JPEG an `APP2 ICC_PROFILE` marker holding the
  standard sRGB profile (ICC v4, D50 colourants, Bradford `chad`, the sRGB parametric curve), built in code in
  `src/image/icc_profile.cpp` - Blender ships the Compact ICC Profiles project's file (CC0); none is downloaded or
  bundled. HDR and EXR files are unchanged.

**Not in this round:** Sun Elevation / Sun Rotation on the Sky world, as Blender's Sky node has them - without a
Directional Light the sky's sun stands where it always did (about 49 degrees up), so the low reddened sun shows only
through a light's angle (a follow-up); a visible disc in the Sky world's background (the path tracer would light twice, through the
world and the light); the sky's sun in lightmap bakes (they gather lights on their own); Solid shading (it never draws
the world). Non-colour 8-bit maps (normal, roughness) carry the same tag as every PNG: untagged 8-bit images are read
as sRGB everywhere already, so no reader changes its reading.

**Checked:** the full Windows suite with Blender's libraries, 54,365 checks: 2 tests fail, neither this round's - the
Live Link paint-dab timing (60 ms worst tick under 13 sessions' load; 3.7 ms run alone, passes) and "assets: Ctrl and
Shift click select several materials", which fails the same 21 of 25 checks on `main` (068207d9) and on rounds 536,
537 and 540's builds. Linux (dependency-free build) render, lighting and editor groups: 15,713 checks, 0 failed. The
round's own: 24 checks in `render` (the sun by elevation and haze, refusals, the key, the ICC profile, the PNG iCCP
chunk, the JPEG APP2 marker, both decoders), `lighting` (the light list with the switch on, off and toggled, one sun
with a Directional Light, the plane lit 238 against 146 grey unlit, rasterizer and CPU path tracer 195.5 against
195.5) and `editor` (the switch, Sun Intensity, Turbidity and the world changed with no edit: the kept views equal the
uncached ones, no stale lighting in verify mode).

<!-- README -->
after: a Unity scene's lights take their intensity as its project's Graphics settings say
the Sky world's sun lights the scene when no Directional Light does, as Blender 5.3's EEVEE (round 542);
saved PNG and JPEG images carry the sRGB ICC profile, as Blender 5.3 writes them (round 542);

<!-- docs/USABILITY.md -->
after: | Adding a door or window meant the Create section's buttons and then moving it to the right wall and size by hand. (round 513)
| A Sky world with no Directional Light lit the scene with its ambient only: no sun, though the sky shows one. (round 542) | The sky's sun lights the scene (World > Sky > Sun Lights Scene; off for the old look). |

<!-- docs/ARCHITECTURE.md -->
after: | `render/sky.*` | Hosek-Wilkie sky texture driven by the first Directional Light
| `render/sky_sun.*` (`sky_sun_light`, `sky_sun_transmittance`, `sky_sun_key`) | The Sky world's sun as one directional light in `make_lighting`'s list, tinted by the atmosphere's transmittance; its key in the lighting keep and the scene hash (round 542) | EEVEE's world sun extraction; `intern/sky`'s Nishita sun | Procedural Skybox's sun |
| `image/icc_profile.*` (`srgb_icc_profile`) | The sRGB ICC v4 profile built in code; `encode_png` writes it as iCCP, `encode_jpeg` as APP2 ICC_PROFILE (round 542) | Blender 5.3's embedded ICC profiles | - |
