# Task 0427: Where the stations' error lives (round 451)

- **Status:** in review
- **Requirements:** R-03 (rendering), R-08 (VRChat worlds and avatars)
- **Decisions:** ADR 0113 (the bake's quality target; no new ADR - diagnostics and one capture change)
- **Model:** implementer
- **PR:** (link when opened)

## Goal
Blendity's own bake of SideRail misses ADR 0113's target against Unity's lightmaps (SSIM 0.77-0.88 at the four
stations against 0.90) and nobody knows which component carries the error. Afterwards the comparison says where: a map
of the local SSIM per station, the error split by Blendity's lighting components, and the lightmap pages compared
object by object. One known difference is fixed: the reflection probes are captured from the lightmapped scene, as
Unity captures them, instead of path-tracing raw geometry.

## Scope
- In: `tools/image_compare.py` (SSIM map, worst windows, `--self-test`); `tools/unity_compare.py` (`<name>_ssim.png`,
  `--components`, `--pages`, the `pages` subcommand, `--self-test`); `BLENDITY_RENDER_COMPONENTS` for F12's rasterized
  render; `lighting save <dir>`; the reflection probes' bake from the lightmaps.
- Out: running Unity or the SideRail measurement (the owner's session runs it); the path tracer's split (the switch is
  the rasterizer's); specular on lightmapped surfaces in the probe capture (Unity's capture shades it from the sky and
  other probes; Blendity's shows the diffuse part and emission); the lightmapper, probe volume and GPU raster code
  (other rounds own them).

## Likely files
- `tools/image_compare.py` (`ssim_windows`, `write_ssim_map`, `worst_windows`, `self_test`), `tools/unity_compare.py`
  (`page_compare`, `read_lighting`, `read_hdr`, `component_rows`, `run_pages`, `self_test`)
- `src/render/reflection_probe.{h,cpp}` (`ProbeLitCapture`, `reflection_probe_capture_lit`),
  `src/render/pathtracer.{h,cpp}` (`first_surface`, `direct_diffuse`)
- `src/editor/light_volume_bake_ui.cpp` (`bake_reflection_probes_on_worker`, `save_shown_lighting`),
  `src/editor/render_view.cpp` (`apply_render_components`), `src/editor/editor_commands.cpp` (`lighting save`)
- `tests/test_lighting.cpp` (`tests_round451`), `src/editor/lessons.cpp` ("How good is the bake?")

## Acceptance criteria
1. A probe over a lightmapped floor (a made-up lightmap, no lights, a black world) reads the lightmap x albedo; the sky
   stays path-traced; a Realtime sun the maps don't hold adds its direct light x albedo.
2. Generate Lighting with a Baked sun: the probe looking down sees the floor's lightmap x albedo (within 10 %);
   without lightmaps the bake path-traces as before.
3. `BLENDITY_RENDER_COMPONENTS` `direct` / `indirect` / `noprobes` / `noreflections` each remove only their part
   (`apply_render_components`), an unknown word nothing; `direct` also drops Baked and Subtractive lights (theirs is the
   lightmaps': no double count beside `indirect`), keeping the shadowed sun's map; the render logs what it applied.
4. `lighting save <dir> [overwrite]` writes the shown lightmaps (LightingData.bin, Lightmap-N.hdr) and objects.json (id,
   path); a relative folder is the project's; a folder holding a bake is refused without `overwrite`.
5. `py tools/image_compare.py --self-test` and `py tools/unity_compare.py --self-test` pass: the SSIM map marks a
   changed square and nothing else; the page compare pairs objects by id or path, skips another mesh, ranks a half-lit
   object first with its light ratio 0.5.
6. The existing command lines of both tools still work; the new outputs are additions.

## Tests and checks to run
- Unit: `blendity_tests --group lighting` (and the reflection probe test in `--group assets`), both builds.
- `py tools/image_compare.py --self-test`, `py tools/unity_compare.py --self-test`.
- SideRail: `tools/unity_compare.py world ... --blendity-bake --components --pages` (the owner's session).

## Risks and edge cases
- A lightmapped object whose mesh changed since the bake: its entry's UVs don't cover the hit triangle - path-traced.
- Cutout and Transparent surfaces in the capture: passed by alpha as the path tracer passes them.
- Unity's and Blendity's object ids differ between scenes: the page compare pairs by path where ids disagree; objects
  with the same path twice are left out; another mesh (a different triangle count) is skipped and listed.

## Review fixes (451)
- `direct` drops Baked and Subtractive lights; the capture culls single-sided back faces as the views do and decodes a
  double-sided back's direction page in the front's frame (`sp.back_side`); Mixed lights added in realtime use the
  realtime falloff (`baked_falloff` cleared); a cancel stops a face within a row; `lighting save` resolves against the
  project and asks for `overwrite`; the page compare never writes inf (null, "-") and counts paired-but-uncovered objects
  apart; `--components` with `--engine Path` is refused.

## Built
- The probes' capture from the lit scene (`reflection_probe_capture_lit`): jittered rays per texel, a lightmapped hit
  shaded as the views shade it (lightmap x direction factor x albedo x (1 - metallic) + emission) plus the direct light
  of the lights the maps don't hold (`lights_not_in_lightmaps`: Realtime, Mixed, Shadowmask); anything else path-traced.
  Generate Lighting uses it whenever it just baked lightmaps; the log says which way the probes were baked.
- The comparison's diagnostics: the SSIM map and worst windows, the component split, the page compare with
  `lighting save`.

## Documentation to update
- CHANGELOG entry (its checks line `CHECKS_451` and the README's check count filled by `tools/dev/fill_checks.py`; the
  README is otherwise unchanged), an ARCHITECTURE row (`reflection_probe_capture_lit`, `apply_render_components`,
  `lighting save`, the tools), a USABILITY row, and in the Learn lesson "How good is the bake?" a new section "Where the
  error lives: probes from the lit scene, and the comparison's diagnostics" (two questions, a Try-it).
