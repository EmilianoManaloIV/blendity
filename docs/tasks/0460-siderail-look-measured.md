# 0460: SideRail's mirror finish and splotches, reproduced and measured (round 484)

- **Status:** done (round 484; approved by the owner's words of 2026-11-10, 00:50, relayed by the hub session:
  "Continue and finish all tasks, I approve all specs from all passes."; the six points at the end stand unless the
  owner says no). The results are at the end; rounds 485 and 486 are specified from them, 486 cut again.
- **Requirements:** R-04 (materials), R-05 (rendering), R-11 (baked lighting like Unity's), Q-03 (tests), Q-06
  (responsiveness), C-04 (the user's files untouched)
- **Decisions:** none needed (a measurement; Pass 24's plan, approved 2026-11-10). Six points are the owner's to say
  yes or no to: "For your decision" at the end.
- **Model:** main session (Opus) for the helpers, the tool and the measuring; then test-engineer, then code-reviewer
- **PR:** (link when opened)

## Goal
The owner opened their imported Unity / VRChat world SideRail and reported (2026-11-10): "Baked lighting looks really
weird here, its like it has a mirror finish and there is splotches still.", with the reference
https://booth.pm/en/items/7083269 (matte wood and concrete, warm lamps, a purple mist). Pass 24's plan names causes for
both, read from the code and the files; none was run or reproduced.

Afterwards each thing that makes SideRail look wrong is named with evidence - which materials, by how much against
Unity, why the Inspector showed their maps as "None", what the bake's blotches come from - with the numbers rounds
485-487 must beat, on SideRail and on a small committed set of our own materials that shows each fault without it.
**This round changes no behaviour and fixes nothing.**

## What the owner's two screenshots show
- **00:30, Blendity's own bake** ("Baked 2 lightmaps in 43.1 s", 2 x 2048 px, 249 objects): floor and counters like
  polished metal, pools of light far brighter than the reference's, small coloured specks over the floor.
- **00:34, Unity's bake** ("Shown: Unity's bake", 1 x 2048 px, 180 objects): the mirror look is still there. The
  Inspector on `ServiceTunnel_Wall_1`, material `ConcretePanel`: Metallic 1, Roughness 0, Base Map / Metallic Map /
  Roughness Map all "None (Texture)".
- Both: the scene is `SideRail*` (743 objects, 381,081 triangles), Blendity 0.13.0 (local build), **the Materials window
  is open**, and its spheres are plain: no wood or concrete texture on any.

## The plan's and the brief's leads, checked against main (75c615b2)
Every file:line lands on what it names: `unity_material.cpp` `read_base` 134-165 (the metallic map at 147-149),
`read_mirror` 225-229, `read_filamented` 248-273; `lightmapper.cpp` 886 (64 emitter samples on the GPU path), 1038 (the
keep decision), 1045 (chance-kept lights go with the emitters' part, never clamped), 1544-1575
(`denoise_page_variance`); `light_pick.h` (`kLightPickOver` 8, `kLightPickRays` 8); `gpu_raster.cpp` 1352-1353 (probe
mip), 1355-1382 (`probe_specular`), 1464-1469 (`unity_indirect_specular`), 1869-1881 (the final mix, exposure occlusion
at 1873-1880); `raster.cpp` 2213-2222; `shading.cpp` 1055; `shading.h` 158; `tests/test_assets.cpp` 9038-9042;
`src/editor/lighting.cpp` 436 and 569. `_SmoothnessTextureChannel`, `_GlossyReflections`, `_SpecularHighlights`,
`_LightmapSpecular` and `_OcclusionStrength` are read nowhere.

What the plan and the brief say is wrong or incomplete in these places (read, not yet reproduced - reproducing them is
this round):

1. **Two faults make the mirror, and the screenshot shows the other one.** A pixel's metal is `metallic x the metallic
   map's red` and its roughness `roughness x the roughness map's red` (`shading.cpp:613-614`,
   `gpu_raster.cpp:439-440`).
   With the mask map attached, "metallic 1, roughness 0" is a polished non-metal (the map's red is about 0): sharp
   reflections at a few percent, more at grazing angles. With **no** map attached - the Inspector's picture - it is
   chrome. The plan's cause 1 (smoothness dropped) explains the first; the screenshot shows the second.
2. **Why the maps can be "None": a Unity `.mat` read before its package's GUID table exists.** `read_base` sets
   metallic 1 and roughness 0 from the `.mat`'s texture slot alone; `set_tex` leaves the slot empty when the project's
   GUID index has no path for the texture (`unity_material.cpp:106-110`). A package is unpacked on a worker and its
   table (`.unity-guids`) is written last (`unity_package_ui.cpp:80`); the Materials window re-lists the project's
   `.mat` files every 2 s (`project_window.cpp:34`) and loads each one every frame it is open
   (`material_window.cpp:195-198`); the asset library keeps what it read for the session (`scene_io.cpp:1463-1475`),
   and opening the Unity scene then takes those (`unity_scene_ui.cpp:362-365`). SideRail's unpack writes its 853
   files in a little over 2 s (the file times of the owner's last import). So: Materials window open, package imported,
   scene opened - every material read in between has no maps until Blendity restarts. This is the first thing to
   reproduce.
3. **The scene of 2026-11-02 did not lose its maps.** `SideRail Blendity\Assets\Scenes\SideRail.scene` holds 67
   materials named from the models (`concrete_worn_scratched`, `01_MetalSheet`, `Material.001`), none linked to a
   `.mat`, none with Metallic 1, and no `ConcretePanel`: an older importer's scene. The screenshot is not that scene.
   The scene of 2026-11-09 in `blendity\Assets\Scenes` (40 materials, 38 linked to a `.mat`, 743 objects) has
   `ConcretePanel` with its base, mask and normal maps, Metallic 1 and Roughness 0.
4. **A saved scene's material values do not matter while the `.mat` is there.** A scene names each material's `.mat`
   and on open the file wins (`relink_material_assets(scene, false)`, `editor.cpp:2401`); the saved values are the
   fallback for a missing file. So a fixed importer shows fixed materials on the next open of a linked scene; what
   "Reimport Unity Materials" must cover is a running session's kept materials and scenes like 3 (both scenes keep each
   object's Unity origin - 634 `UnityOrigin` components - so their materials can be found again).
5. **In Unity the shader keyword decides, not the slot.** Filamented's own input code in the package
   (`UnityStandardInput.cginc:207-228`) takes metal from the map's red and smoothness from its alpha times
   `_GlossMapScale` only under the `_METALLICGLOSSMAP` keyword; without it, the sliders. Blendity decides by the slot.
   17 of SideRail's 57 materials have both; one (`CrossStandardTest`) has a texture in the slot and no keyword.
6. **The plan's mechanism for round 485 has an open end.** `derive_channel` (`src/scene/import_materials.cpp:369`, not
   `src/editor`) belongs to a model's import (it needs its `ImportResult` and cache folder); a Unity `.mat` is read
   through `material_asset` at every scene open, on the calling thread. Where a derived roughness map lives and what
   decoding the mask maps costs at open (16 of them, PNGs of up to 16 MB) is measured here, for spec 0461.
7. **A third cause the plan does not hold: the lights' intensity.** A Unity scene's lights are taken in gamma unless
   the project's `GraphicsSettings` says linear (`unity_lights_linear_intensity`, `unity_scene_ui.cpp:391-397`). A
   project made by unpacking a `.unitypackage` has no `ProjectSettings`, so Lights Gamma stays on, without a log line.
   The owner's saved scene has `Lights_Gamma 1`. The Unity project the package was opened in (a VRChat Creator
   Companion world project; its copy is `C:\blsiderail\world`) has `m_LightsUseLinearIntensity: 1`, and round 459
   measured what gamma does to SideRail: lights 2.3 to 7 times too bright, lightmap pages 2 to 5 times Unity's. The
   comparison tool has passed `unity lightsgamma off` since then, so the chains' numbers are of the linear bake; the
   owner's own Generate Lighting was not. It would explain the bright pools in the 00:30 picture, and it makes the
   specks brighter.
8. **Pass 21's SideRail numbers describe a state the owner never sees:** the Unity scene imported fresh in a headless
   run (textures resolve, lights linear), rendered by F12's CPU rasterizer. The owner looks at the views on the card,
   in a session whose materials had no maps, with the lights in gamma. This round measures both.
9. **The keep decision** (`lightmapper.cpp:1038`) is one plain random number per candidate light per texel. A light
   kept by chance adds `visibility x (sum of the estimates) / 8` in brightness whichever light it is, so a texel's
   chance-kept light is that step times how many it kept - a count that scatters by up to about 3 around its mean of
   at most 8. That supports the plan's stratified decisions; the measurements below test it before spec 0462 picks a
   method.
10. **`tunnel_lamps` cannot show SideRail's fault:** its 10 Baked point lights have a range of 2 m, so a texel is
    reached by one or two of them and little of its light is ever kept by chance (its blotch is 0.027 against
    SideRail's 0.251). A stand-in with many lamps is added here.
11. **`tools/dev/chain.sh` runs the comparison in `C:\Users\trick\Downloads\SideRail Blendity` by default**, and
    `tools/unity_compare.py` writes there (its bake's scene and lighting folder, Unity's lightmaps file). This round
    runs from scratch copies only and leaves the default as it is (point 5 of "For your decision").

SideRail's 57 materials by shader: 22 Filamented Standard, 13 FakeInnerLight, 8 built-in, 5 TextMeshPro fonts (their
shader is not in the package), 3 mirror, 2 Light Volume particles, 4 others. `_SpecularHighlights 0` on 2,
`_GlossyReflections 0` and `_SmoothnessTextureChannel 1` on none, `_GlossMapScale` other than 1 on one.

## Scope
- In:
  1. **Three measuring verbs on the console** (none is saved, none is in a scene's key, none changes the scene):
     - `unity materials [all] [to "<file>"]`: every material the scene's renderers wear (`all`: every `.mat` under
       Assets too), one block each - name, its `.mat` and whether that is a Unity file, how many renderers wear it and
       the first one's path, surface, base colour, metallic, roughness, specular, exposure occlusion, emission
       strength, and for each map slot the path, `-` for none or `missing` when the file is not there.
     - `unity mapstats "<image>" ... [to "<file>"]`: an image as Blendity decodes it as data - size, channels, each
       channel's mean, least and greatest value, and the decode's milliseconds. A script's: refused in a window, as
       `unity lightmaps wait` is.
     - `lighting pick on|off|<rays>`: the next bake keeps every Baked point and spot light at every texel (`off`), or
       about `<rays>` of them by chance (`on` is 8, today's bake bit for bit); the bake's log line says the setting
       when it is not the default, and how many lights a texel kept by chance and dropped, on average.
       `BakeSettings::pick_lights` exists; the number becomes a field beside it, 8 unless set (the light probes keep
       8).
  2. **`tools/unity_materials.py`** (standard library only): reads every Unity `.mat` under a package folder on its
     own - shader through `.unity-guids`, keywords, floats, texture slots - asks Blendity for `unity mapstats` of the
     maps Unity reads and for `unity materials`, and writes `materials.md` / `materials.json`: per material Unity's
     metallic and roughness (the means, by the shader's own rule, with the map and channel they come from), Blendity's
     (value times its map's mean), the differences, the maps Unity uses that Blendity has not attached, and a fault
     class: `maps missing`, `smoothness dropped`, `slot without keyword`, `reflections off ignored`, `highlights off
     ignored`, `occlusion ignored`, `not modelled` (a shader the tool has no rule for), `ok`. It also writes the
     console lines that set each faulty material to Unity's mean values by hand (for the stations below).
     `--self-test` runs it on the committed set without Blendity.
     *(Built another way: no console lines. `state <project> byhand <materials.json>` writes Unity's mean values onto
     the sliders of the scratch project's `.mat` files and empties their metallic-gloss slot; `table` and `state` refuse
     a project without an empty `BLENDITY_SCRATCH` file beside its Assets folder. Two more fault classes came with
     the round's findings: `values differ` and `decoders differ`.)*
  3. **A committed set of our own materials** (`tests/fixtures/unity_gloss/`: hand-written `.mat` texts, a stub
     shader that declares Filamented's properties, 8 x 8 PNGs of one colour each with their generator, a GUID table;
     nothing from SideRail), one material per fault with Unity's values worked out by hand in its README:
     smoothness in the metallic map's alpha; the same scaled by `_GlossMapScale`; smoothness in the albedo's alpha
     (`_SmoothnessTextureChannel 1`); a texture in the slot without the keyword; metal from the map's red (right
     today: a control); `_GlossyReflections 0`; `_SpecularHighlights 0`; occlusion in the mask's green; the Filamented
     setup SideRail uses (`_LIGHTMAPSPECULAR`, exposure occlusion); URP Lit's `_Smoothness` with a map; a mirror shader
     (stays a mirror: a control); and "read before its textures are known".
  4. **The Inspector's picture, reproduced** with this round's own build in a scratch project and scratch
     preferences, in the real window (`--windowed-check`, so the package unpacks on its worker as it does for the
     owner): the Materials window open, SideRail's package imported, `SideRail.unity` opened, `unity materials`
     written. Run five times, and once each with the Materials window closed, with the Project window showing the
     folder instead, with the package imported over an existing copy, and with the folder deleted and imported again
     in one session. Also: the owner's scene of 2026-11-09 opened in a copy of its project, and the scene of
     2026-11-02 in a copy of its own.
  5. **SideRail's material table** (`unity_materials.py`) for three states: the Unity scene imported fresh, the
     owner's scene of 2026-11-09, the scene of 2026-11-02 - all 57 materials, the ones the scene wears marked.
  6. **Station pictures against Unity** (`tools/unity_compare.py world`, all six stations of
     `tools/dev/siderail_stations.json`; `side_bar_high` and `side_tunnel_eye` are the owner's two views), each with
     MAE, SSIM and the split by component (`--components`: how much of the error is reflections):
     - with Unity's bake: materials as imported fresh; without their maps (the reproduced state); set to Unity's mean
       values by hand (what round 485 should reach, and whether reflections need any change once the materials are
       right);
     - with Blendity's bake: lights linear (as the chains since round 459) and in gamma (as the owner's project
       gives).
     The tool gets `--after-open <file>` (console lines run after the scene opens) for the by-hand and gamma states.
     *(Built another way: no `--after-open`. The gamma state is `--lights-as-imported`, a bake's own lines are
     `--bake-cmd "<line>"`, Unity's lightmaps file already beside the scene is taken with `--reuse-unity-lightmaps`, and
     the material states are `tools/unity_materials.py state <project> nomaps | byhand | png8`, which rewrite the
     scratch project's files so the importer reads them as it reads Unity's - see "Deviations".)*
     Beside F12's pictures, the Game view at the owner's two stations as the views draw it, on the card and on the
     CPU.
  7. **The blotches**, on SideRail in a scratch project (seeds 0 and 1 of each, `--pages --noise`):
     - today's bake: grain, blotch (all pages, each page, the worst objects) and the bake's time, against Pass 21's
       0.382 / 0.251 / 39 s;
     - `lighting pick off`, 16, 32: what is left when no light is kept by chance, and what more rays cost;
     - the filter off, and round 423's (`lighting denoiser atrous`): whether the filter makes blotches or only fails
       to remove them;
     - lights in gamma: the same numbers for the bake the owner ran;
     - **station noise:** `unity_compare.py --noise` also renders the owner's two stations with each seed's bake and
       reports the two pictures' grain and blotch (the same measure on the picture's luminance), with the difference
       picture - the number for "no visible blotches at the two stations".
  8. **A stand-in for the bake** in stress `lightmap_quality`: `lmq::lamp_hall()` - a hall with a side room and about
     70 Baked point and spot lights of mixed strength and colour, part of them hidden from any one texel, lights
     linear - scored against its 16x reference like the other scenes, and a table of its bake at `pick` off, 8, 16
     and 32 (bake ms, relative RMSE, bias, grain, blotch).
  9. **The results**: in this file (the tables, the cause of each fault with its evidence, what was tried and ruled
     out), `docs/PERFORMANCE.md` "Pass 24: the baseline" (marked "measured locally"), the CHANGELOG. Then specs 0461
     and 0462 from them - and the rounds cut again if the tables contradict the plan (point 7 above may need a round
     of its own).
  10. A short Learn section, "A Unity material beside Unity's own values": what `unity materials` shows, a Try-it on
      the committed set, two questions; `lighting pick` gets a paragraph and a Try-it in "A bake's noise".
- Out (not in this task): any change to how a Unity material is read, shaded or kept, to which lights a bake keeps at
  its default, to the filter, to Lights Gamma's default, to the import's order (rounds 485-487); the mist and light
  cookies; SideRail's files or pictures in the repository.

## Likely files
- `src/editor/editor_commands.cpp` (`unity materials`, `unity mapstats`, `lighting pick`), `src/editor/unity_scene_ui.cpp`
  or a new `src/editor/unity_material_report.cpp` (the listing), `src/editor/lighting.cpp` (the setting into the bake),
  `src/editor/lessons.cpp`
- `src/render/lightmapper.{h,cpp}` (`BakeSettings::pick_rays`), `src/render/light_pick.h` (`light_pick_keep` takes the
  rays, 8 by default)
- `tools/unity_materials.py` (new), `tools/unity_compare.py` (`--after-open`, the station noise), `tools/README.md`,
  `tools/dev/README.md`
- `tests/fixtures/unity_gloss/` (new), `tests/test_assets.cpp` and `tests/test_lighting.cpp` (`tests_round484`)
- `stress/stress_main.cpp` (`lmq::lamp_hall`, the pick table), `stress/sweep_commands.inc` if an example changes it
- `docs/PERFORMANCE.md`, `CHANGELOG.md`, `README.md` (the check count), this file

## Acceptance criteria
1. **The Inspector's picture is reproduced, or ruled out.** A sequence the owner can do by hand, run in the real
   window, ends with `ConcretePanel` at Metallic 1, Roughness 0 and no maps, as in the screenshot; the results give
   the sequence, how many of five runs showed it, the listing, and the control runs that do not show it. If no sequence
   shows it, the results say what was tried and the question goes to the owner before spec 0461 is written.
2. **The mechanism at its smallest, in the unit suite** (the committed set, a scratch project): a Unity `.mat` loaded
   before its package's GUID table is written has Metallic 1, Roughness 0 and no maps, and still has none after the
   table is written and the index refreshed; loaded after the table, it has its maps. The test says it pins a fault
   round 485 removes.
3. **`unity materials`**: on a scene wearing one material from a Unity `.mat`, one Blendity `.mat` and one scene
   material it prints one block each with the fields of the scope; a map whose file is gone reads `missing`, an empty
   slot `-`; `all` adds a `.mat` no renderer wears; `to "<file>"` writes the same text; the scene's text is the same
   before and after, and there is no undo step.
4. **`unity mapstats`**: an 8 x 8 PNG of one known colour reads its size and each channel's mean within 1 / 255; an
   image without alpha reads alpha 1; a missing file is refused by name; in a window it is refused with the reason.
   *(Added while the round ran, with the fourth cause below: the first word `decoders` adds the image's means through
   Blendity's own decoder; on the committed 16-bit map both readings are 128 / 255 in the dependency-free build, and
   the libraries build's is 186 / 255 (a gamma of 2.2 applied to one half) until round 485. The refusal in a window
   is not in the suite - every Editor a unit test makes is headless - and was checked in the real window instead:
   `unity mapstats "<image>"` and `unity mapstats decoders "<image>"` both answer "a script's ... run it in a
   headless Blendity", and `unity materials` lists.)*
5. **`lighting pick`**: `on` and `8` give the default bake's pages bit for bit; `off` keeps every candidate light at
   every texel (the bake's count of lights kept by chance is 0) with the same mean light within 2 %; 16 differs from 8
   and drops fewer lights a texel by chance; junk is refused and the setting stays; the lighting is not marked out of
   date and the setting is not in the saved scene.
   *(Corrected while the round ran: this read "keeps more lights a texel". The count the bake reports is of lights
   kept by chance - those with a chance below 1 - and at 16 more lights are kept for certain, so that count falls
   too: 4.08 kept by chance and 5.00 dropped a texel at 8, 3.57 and 2.28 at 16 in the test's room. "Drops fewer" is
   what the setting is for, and what the test pins.)*
6. **The committed set**: `py tools/unity_materials.py --self-test` gives each material Unity's metallic and
   roughness as its README works them out (within 0.005) and the fault class listed there; `tests_round484` shows
   Blendity's values for each today - the two controls equal to Unity's, every other one off by more than 0.05 or
   missing a map - each test named for what the owner sees and saying which round changes it.
   *(As built, said exactly: of the twelve materials five are off in their values today and pinned so in
   `tests_round484` - the three "smoothness dropped" ones on Standard, the Filamented and the URP Lit one - and one is
   off by its keyword (`slot_without_keyword`). Three have Unity's values and their maps and differ from Unity only by
   something Blendity's material has no property for - two keywords and an occlusion map (`reflections_off`,
   `highlights_off`, `occlusion_in_green`): the listing shows nothing a C++ test could pin, so the tool's fault class
   alone names them. Three are controls:
   `metal_from_red`, `mirror` and `sixteen_bit_normal`, whose fault is the decoder's, not the material's.)*
7. **SideRail's table** is in the results for the three states: all 57 materials, Unity's and Blendity's metallic and
   roughness, the maps, the fault class, the count per class; the mask maps' mean smoothness against the brief's
   (0.32 to 0.39, `Wood_MOS` 0.99); what decoding each mask map costs.
8. **The stations**: MAE, SSIM and the component split at the six stations for the five states of the scope, the Game
   view's pictures at the owner's two, and one sentence each on: whether the by-hand materials remove the mirror look,
   whether reflections need a change of their own, how far the gamma lights move the bake from Unity's.
9. **The blotches**: the table of the scope (today, `pick` off / 16 / 32, filter off, A-Trous, gamma) with bake
   times taken on a quiet machine, the station noise at the owner's two stations, and one sentence each on: how much
   of the blotch is the lights kept by chance, whether the filter adds to it, what 1.25x the bake time buys.
10. **`lamp_hall`** is in `lightmap_quality` with its row and its pick table; at `pick` 8 its blotch is at least
    twice its `pick off` blotch (it shows the fault; the results give the factor), and the other scenes' rows are
    unchanged.
11. **Nothing else changes**: the existing unit tests pass unchanged; a bake at the defaults saves the same pages,
    byte for byte, from main's build and from this round's (`lighting save` of a scene one console script makes: a
    room and twelve Baked point lights); headless screenshots of the starter scene are the same pixels.
    *(Said exactly, after the review: this is a check of two builds, made with the libraries build the owner runs -
    MSVC, `/O2 /GL /fp:fast`, where the optimizer may order arithmetic by what surrounds it. The results give the
    compare, what it found the first time and what was changed for it. The editor's own screenshot is not the same
    pixels from one run to the next even with one exe (its console prints each command's milliseconds), so F12's
    picture of the starter scene is compared instead.)*
12. **The owner's folders are as they were**: file count, sizes and modification times of the package's folder,
    `blendity\Assets`, `SideRail Blendity` and `C:\blsiderail` compared before and after the round's runs.
13. `python tools/test_coverage_map.py --check` and `python tools/sweep_commands.py --check` pass; the full unit suite
    passes on Windows (both builds) and Linux; CI's four jobs pass.

## Tests and checks to run
- Unit: `BLENDITY_TEST_FILTER="Unity material"`, `--group assets`, `--group lighting` in both Windows builds first,
  then the full suite (Windows both builds and Linux). `python tools/unity_materials.py --self-test`,
  `python tools/unity_compare.py --self-test`.
- Stress: `blendity_stress --only lightmap_quality` (minutes).
- SideRail (the worker, after the review and the chain): scratch copies under `C:/blcmp484` - the package unpacked by
  the round's own build, the owner's two scenes with their folders, the Unity project copy copied once more
  (`C:/blcmp484/unity_world`, so Unity writes nothing into `C:/blsiderail`). Bake times only with the Pass 22 worker
  holding and no build, test or stress process of another tree running; the noise numbers and the pictures do not
  need a quiet machine.

## Responsiveness and cold first use (ADR 0105)
- The three verbs cost nothing unless typed. `unity materials` reads no image and stays within one frame's 100 ms on
  SideRail's 57 materials (its time is in its log line); `unity mapstats` decodes images and is a script's (refused in
  a window); `lighting pick` moves no work onto the main thread.
- This round measures a cost round 485 has to place: what reading a mask map's alpha takes per material and for the
  whole scene at open, cold (the image not in the texture cache) and warm - so the fix is specified with that work
  off the frame.

## Risks and edge cases
- **The reproduction is a race.** It needs the real window and a package big enough that the Materials window's
  2-second re-list falls inside the unpack; hence five runs with SideRail's own package and the unit test of the
  mechanism, which does not depend on timing.
- **A window opens on the desktop** for each windowed run (this round's build, scratch project, scratch preferences,
  about a minute); the owner's running Blendity, its project and its preferences are not touched.
- **Means, not pictures.** The table compares each material's mean metallic and roughness; a map's pattern is not
  compared here (round 485's tests compare texels on the committed set).
- **Unity's side must be the package's.** The 57 `.mat` files and their maps in the Unity copy are compared byte for
  byte with the package's before its pictures are used; a difference is reported, not worked around.
- **Dark stations**: SSIM on a nearly black picture reads noise; the tool's rule (MAE and means) stands, and the
  station noise is reported relative to the picture's mean light.
- **F12 is not the views**: the CPU rasterizer, the card's views and the CPU views may differ at roughness 0; all
  three are captured at the owner's two stations.
- **`unity lightmaps wait`** needs Unity for a few minutes the first time in a scratch project; the lightmaps file
  Unity already wrote is copied along with the scene so later runs do not wait.
- **The by-hand state is an approximation** (one roughness per material, no map): it shows the direction and a lower
  bound for round 485, not its exact result.
- The dependency-free build, GCC's explicit lambda types around `get<T>()`, shallow nesting, an SPDX header in each
  new file; the stub shader carries property names only, no code of Silent's.

## Documentation to update
- CHANGELOG (round 484; a fragment in `changelog.d/` if task 0443 is on main by then), README (the check count),
  ARCHITECTURE rows (`unity materials`, `lighting pick`, `lmq::lamp_hall`), PERFORMANCE ("Pass 24: the baseline"),
  `tools/README.md` and `tools/dev/README.md`, the Learn section, this file marked done with its results and
  deviations.

## For your decision (say no to any; the rest stands)
1. **Three developer verbs and one tool are added** (`unity materials`, `unity mapstats`, `lighting pick`,
   `tools/unity_materials.py`). The plan allowed "measurement helpers"; these are them. Nothing else you can see
   changes.
2. **The round's unit tests pin today's wrong values** on our own materials (a reproduction: they pass now, and round
   485 rewrites each with the reason, as the plan already says for `test_assets.cpp:9038`). With a no, only the tool's
   table shows the faults and the suite gains the helpers' tests alone.
3. **A third cause is measured beside the two you approved**: the lights taken in gamma in a project that has no
   Unity `ProjectSettings` (point 7). If the tables confirm it, it comes back to you at the next gate as a round of its
   own or as part of 485.
4. **A stand-in scene `lamp_hall`** goes into stress `lightmap_quality`, so round 486 has a committed scene that shows
   the blotches (`tunnel_lamps` does not).
5. **`C:\Users\trick\Downloads\SideRail Blendity` and `C:\blsiderail\world` are treated as yours**: this round reads
   them and runs in copies under `C:/blcmp484`. The chain's default still points the comparison at the first (the
   tool writes its bake there, as every chain since round 421 has); say if that default should change too.
6. **Pictures of SideRail stay out of the repository** (in `C:/blcmp484`, shown to you through the hub); the tables -
   material names and numbers - go into this file and `docs/PERFORMANCE.md`.

One answer would save a run: before those two screenshots, in that same Blendity session, had you imported SideRail's
package (or deleted its folder and imported it again), or only opened the saved scene?
*(No longer needed for the round: the picture is reproduced for an import made in the session, and opening the saved
scene alone does not give it - run d of the results. The answer would still say which of the two it was that night.)*

## Results (round 484)

Everything below was run in scratch copies under `C:/blcmp484` (the package unpacked by Blendity itself; the Unity
project copied once more; scratch preferences, cache and trash), with the build with Blender's libraries - the one the
owner runs - unless a line says otherwise. Pictures are F12's CPU rasterizer at the six stations of
`tools/dev/siderail_stations.json`; `side_bar_high` and `side_tunnel_eye` are the owner's two views. Nothing of
SideRail is in the repository: names and numbers only.

### In short
**Six things make SideRail look wrong, not two, and the plan named the wrong cause for one of its two.**

| | what | how much | whose round |
|---|---|---|---|
| 1 | **Materials read without their textures** - the Inspector's picture. A Unity `.mat` read before its package's GUID table exists gets Metallic 1, Roughness 0 and no map, and keeps that until Blendity restarts | reproduced in 6 of 7 runs; 17 to 37 of the scene's 38 Unity materials; `side_bar_high` 3.75 -> 13.71 MAE, 0.898 -> 0.702 SSIM | 485 |
| 2 | **Delete a package's folder and import it again in one session**: the Unity scene opens with none of its Unity materials | 54 materials of the models' own, 0 linked | 485 |
| 3 | **Smoothness dropped** where Unity keeps it in the mask map's alpha | 10 worn materials (181 renderer slots) at roughness 0 where Unity's is 0.24 to 0.76 - but F12's picture hardly moves when the values are set right (3.75 -> 3.74 MAE, 0.898 -> 0.905 SSIM) | 485 (the values) |
| 4 | **16-bit PNGs read brighter** by the libraries build (libpng takes them as linear light) - found by this round | 14 of SideRail's 111 PNGs, 7 of them named by materials; 4.3 % and 1.7 % of the pixels at the owner's two stations | 485 (one flag) |
| 5 | **The lights taken in gamma** in a project made from a package (it has no Unity `ProjectSettings`; SideRail's own project is linear) | with Blendity's own bake the owner's two stations read 17.76 / 0.734 and 16.40 / 0.761 against Unity, 6.64 / 0.863 and 5.97 / 0.885 with the lights linear; the views are 2.9 and 2.6 times as bright there | the owner's decision |
| 6 | **The splotches are mostly not the lights kept by chance.** With every light kept at every texel the bake's blotch is the same (0.2507 against 0.2510) and so are the specks at the owner's stations (178 and 139 against 147 and 133) | of about 140 specks a view about 100 are in the gathered bounce, where the bake scales the noise it measures down to an eighth and so takes rare bright samples for steady light; about 20 are lights kept by chance. In the owner's own session (no maps, lights in gamma) a view had about 330 | 486, cut again |

### 1. The Inspector's picture, reproduced (criteria 1 and 2)
Unmodified main (75c615b2), the real window (`--windowed-check`, so the package unpacks on its worker), a scratch
project and scratch preferences. "Owner's layout" is a copy of the owner's preferences file - their dock layout, the
Materials window open - with Live Link, the update check and the recent scenes off. A healthy import has 40 materials,
38 linked to a `.mat`, 10 of those without any map (they have none in Unity either) and 3 at Metallic 1, Roughness 0
with no metallic map (the three mirrors).

| run | layout | in one session | linked materials without any map | Metallic 1, Roughness 0, no metallic map | `ConcretePanel` |
|---|---|---|---|---|---|
| e1 | owner's | import the package, open `SideRail.unity` (Unity numbers the models' parts; the scene opens twice) | 37 of 38 | 15 | **no maps** |
| a1 | owner's | import, open (the part numbers already there) | 17 of 38 | 5 | maps |
| a2 - a5 | owner's | import, open (four runs) | 36 of 38 each | 13 each | **no maps** each |
| c | default + `window Materials` | import, open | 37 of 38 | 15 | **no maps** |
| b | default (no Materials window) | import, open | 10 of 38 | 3 | maps (the control) |
| d | owner's | open only (the package unpacked in an earlier session) | 10 of 38 | 3 | maps (a restart is clean) |
| e | owner's | import again over the unpacked copy, open | 10 of 38 | 3 | maps |
| f | owner's | `deleteasset` the folder, import, open | 54 materials, **0 linked** | - | not there: the models' own materials |

- **The sequence the owner can do by hand:** open Blendity with the Materials window showing, Assets > Import Unity
  Package (SideRail's), open `SideRail.unity`. `ConcretePanel` then reads Metallic 1, Roughness 0 and three empty map
  slots, as in the 00:34 screenshot: 6 of the 7 runs that way, and every one of the 7 lost maps on 17 to 37 materials.
- **Why:** the log of e1 shows Unity `.mat` files being read while the package is still unpacking (five "Unity
  material ..." lines between "unpacking" and the package's summary line); b's shows none. The unpack writes
  `.unity-guids` last (`unity_package_ui.cpp:80`); the Materials window lists the project's `.mat` files again every 2
  seconds and loads each one it shows (`project_window.cpp:34`, `material_window.cpp:195`); a material read then finds
  no path for its textures' GUIDs, and the asset library keeps what it read for the session
  (`scene_io.cpp:1463`). The unit test of criterion 2 shows the same without timing.
- **f** is a second fault: `deleteasset` marks the folder as deleted for the session (`forget_material_assets`,
  `scene_io.cpp:1777`), every material path under it then resolves to nothing (`material_asset`), and the import that
  follows does not clear the mark - so the scene's materials fall back to the models' own (`Material`, `Wood_Common`,
  `concrete_worn_scratched`, `01_MetalSheet`, `CastConcrete` ... - the names in the owner's scene of 2026-11-02).
- **Until round 485: restart Blendity after importing a package** (or close the Materials window while importing).
  The owner's saved scene of 2026-11-09 is itself healthy: it names each `.mat`, and the file wins at the next open.
- Not run: the variant "the Project window showing the folder instead". The Materials window alone decides it (b
  against c); whether anything else that loads a `.mat` during the unpack does the same is round 485's to close at
  the source (no material read while its package unpacks is kept).

### 2. SideRail's materials beside Unity's (criterion 7)
`py tools/unity_materials.py table` on the scene imported fresh. **The owner's scene of 2026-11-09 gives the same
table** (the same 38 materials linked, the same values: its `.mat` files win at open). The scene of 2026-11-02 wears
none of them: 67 materials of the models' own, 0 linked.

| material | shader | worn by | Unity: metallic / roughness | Blendity today | maps filled / named | what is wrong |
|---|---|---|---|---|---|---|
| 01_MetalSheet_Metalic | Filamented | 65 | 1.00 / 0.24 | 1.00 / 0.00 | 3 / 4 | smoothness dropped |
| 02_BlackPaint | Filamented | 36 | 0.08 / 0.76 | 0.08 / 0.00 | 3 / 4 | smoothness dropped, highlights off ignored |
| LightBulb | Filamented | 36 | 0.67 / 0.51 | 0.67 / 0.00 | 3 / 4 | smoothness dropped |
| BakedLightProfile_A | Standard | 30 | 0.00 / 1.00 | 0.00 / 1.00 | 1 / 1 | ok |
| Emission_Filament | Filamented | 22 | 0.00 / 1.00 | 0.00 / 1.00 | 1 / 2 | ok |
| FootLight | FakeInnerLight | 20 | - / - | 0.00 / 1.00 | 3 / 4 | not modelled |
| MatBrown | Filamented | 20 | 0.00 / 0.50 | 0.00 / 0.50 | 0 / 1 | ok |
| Orrange_Cloth | Filamented | 16 | 0.00 / 1.00 | 0.00 / 1.00 | 2 / 3 | decoders differ |
| 01_MetalSheet | Filamented | 15 | 0.00 / 0.50 | 0.00 / 0.50 | 2 / 3 | ok |
| BakedLightProfile_B | Standard | 12 | 0.00 / 1.00 | 0.00 / 1.00 | 1 / 1 | ok |
| Emission_LightBulb_Orrange | FakeInnerLight | 12 | - / - | 0.00 / 1.00 | 0 / 1 | not modelled |
| LightBulbGlass | VFD Glass | 12 | - / - | 0.00 / 0.00 | 0 / 0 | not modelled |
| WoodPlanks_Rough | Filamented | 12 | 0.00 / 0.64 | 0.00 / 0.00 | 3 / 4 | smoothness dropped, decoders differ, occlusion ignored |
| FakeBloom | built-in 10720 | 10 | - / - | 0.00 / 1.00 | 1 / 1 | not modelled |
| FootLight_Blue | FakeInnerLight | 10 | - / - | 0.00 / 1.00 | 3 / 4 | not modelled |
| Lantan | Filamented | 10 | 0.42 / 0.64 | 0.42 / 0.00 | 3 / 4 | smoothness dropped |
| ConcretePanel | Filamented | 9 | 0.00 / 0.68 | 0.00 / 0.00 | 3 / 4 | smoothness dropped |
| ConcreteTrimsheert | Filamented | 8 | 0.00 / 0.61 | 0.00 / 0.00 | 3 / 4 | smoothness dropped, occlusion ignored |
| Glass | Glass_NoFog | 7 | - / - | 0.00 / 0.00 | 0 / 1 | not modelled |
| FakeBloom_Blue | built-in 10720 | 6 | - / - | 0.00 / 1.00 | 1 / 1 | not modelled |
| Candle | VFD CandleFlicker | 5 | - / - | 0.00 / 1.00 | 0 / 1 | not modelled |
| CuttingLamp_Blue | FakeInnerLight | 5 | - / - | 0.00 / 1.00 | 1 / 1 | not modelled |
| VelvetChair1_Red | Filamented | 5 | 0.00 / 1.00 | 0.00 / 1.00 | 2 / 3 | ok |
| Black | Filamented | 4 | 0.00 / 1.00 | 0.00 / 1.00 | 0 / 1 | ok |
| CuttingLamp_Blue 1 | FakeInnerLight | 4 | - / - | 0.00 / 1.00 | 1 / 2 | not modelled |
| CuttingLamp_Orrange | FakeInnerLight | 4 | - / - | 0.00 / 1.00 | 1 / 1 | not modelled |
| ConcreteWorn | Filamented | 3 | 0.00 / 0.50 | 0.00 / 0.50 | 2 / 3 | ok |
| Wood_Rough | Filamented | 3 | 0.00 / 0.01 | 0.00 / 0.00 | 2 / 4 | highlights off ignored |
| Barrel | Filamented | 2 | 0.83 / 0.35 | 0.83 / 0.00 | 3 / 4 | smoothness dropped |
| Concrete | Filamented | 2 | 0.00 / 0.63 | 0.00 / 0.00 | 3 / 4 | smoothness dropped |
| Emission_Filament_Blue | Filamented | 2 | 0.00 / 1.00 | 0.00 / 1.00 | 1 / 2 | ok |
| ExitSign | FakeInnerLight | 1 | - / - | 0.00 / 1.00 | 3 / 5 | not modelled |
| ExitSign_Arrow_Right | FakeInnerLight | 1 | - / - | 0.00 / 1.00 | 3 / 6 | not modelled |
| PlayersOnlyMirror_HQ | VRC mirror | 1 | - / - | 1.00 / 0.00 | 0 / 0 | not modelled |
| PlayersOnlyMirror_LQ | VRC mirror | 1 | - / - | 1.00 / 0.00 | 0 / 0 | not modelled |
| PlayersOnlyMirror_Transparent | VRC mirror | 1 | - / - | 1.00 / 0.00 | 0 / 0 | not modelled |
| TransparentBackground | mirror background | 1 | - / - | 0.00 / 0.50 | 0 / 0 | not modelled |
| WoodPanel_2 | Filamented | 1 | 0.00 / 0.72 | 0.00 / 0.00 | 3 / 4 | smoothness dropped, decoders differ, occlusion ignored |
| Bokor-Regular SDF | not in the package | 0 | - / - | 0.00 / 0.50 | 1 / 1 | not modelled |
| CandleFlame | built-in 211 | 0 | - / - | 0.00 / 0.50 | 1 / 1 | not modelled |
| CastConcretePanel | Filamented | 0 | 0.00 / 0.77 | 0.00 / 0.00 | 3 / 4 | smoothness dropped |
| CrossStandardTest | Filamented | 0 | 1.00 / 0.00 | 1.00 / 0.00 | 0 / 1 | slot without keyword |
| DMSerifDisplay-Regular SDF | not in the package | 0 | - / - | 0.00 / 0.50 | 1 / 1 | not modelled |
| DownLight | FakeInnerLight | 0 | - / - | 0.00 / 1.00 | 3 / 4 | not modelled |
| DownLight_White | FakeInnerLight | 0 | - / - | 0.00 / 1.00 | 3 / 4 | not modelled |
| DustParticle | built-in 211 | 0 | - / - | 0.00 / 0.50 | 1 / 1 | not modelled |
| DustParticle_LightVolume | Light Volume particles | 0 | - / - | 0.00 / 0.50 | 1 / 1 | not modelled |
| ExitSign_Arrow_Left | FakeInnerLight | 0 | - / - | 0.00 / 1.00 | 3 / 6 | not modelled |
| FootLight_ | FakeInnerLight | 0 | - / - | 0.00 / 1.00 | 3 / 4 | not modelled |
| Lantan_Glass | Filamented | 0 | 0.90 / 0.10 | 0.90 / 0.00 | 2 / 3 | smoothness dropped |
| LightVolumeParticle | Light Volume particles | 0 | - / - | 0.00 / 0.50 | 1 / 1 | not modelled |
| Logo | not in the package | 0 | - / - | 0.00 / 0.50 | 1 / 1 | not modelled |
| Righteous-Regular SDF | not in the package | 0 | - / - | 0.00 / 0.50 | 1 / 1 | not modelled |
| RubikGlitch-Regular SDF | not in the package | 0 | - / - | 0.00 / 0.50 | 1 / 1 | not modelled |
| SeparateLamp | Standard | 0 | 1.00 / 0.31 | 1.00 / 0.31 | 1 / 1 | decoders differ |
| TaperLamp_01_FakeInner_Orrange | FakeInnerLight | 0 | - / - | 0.00 / 1.00 | 1 / 2 | not modelled |
| UnlitBlack | built-in 10755 | 0 | - / - | 0.00 / 0.50 | 0 / 0 | not modelled |

- **Per class, among the 38 worn:** not modelled 17 (101 renderer slots: the lamp shaders, glass, mirrors - Unity's
  values unknown to the tool), smoothness dropped 10 (181 slots), ok 9, decoders differ 3 (29 slots), occlusion
  ignored 3 (21), highlights off ignored 2 (39). Of the textures the worn materials name, Blendity fills 61 of 92
  slots; the rest are slots it has no place for (occlusion, height, the lamp shaders' masks).
- **Smoothness:** every one of the ten comes in at Metallic x its map's red (right) and Roughness 0 (wrong): Unity's
  roughness is 0.24 (`01_MetalSheet_Metalic`, 65 slots), 0.76 (`02_BlackPaint`, 36), 0.51 (`LightBulb`, 36), 0.64
  (`WoodPlanks_Rough`, 12; `Lantan`, 10), 0.68 (`ConcretePanel`, 9), 0.61, 0.35, 0.63, 0.72.
- **The mask maps' mean smoothness** (alpha) against the brief's: `ConcretePanel` 0.324, `Concrete_MOS` 0.367,
  `WoodPlanks_Rough_MOS` 0.360, `ConcreteTrimsheet` 0.390, `Wood_MOS` 0.991 - the brief's 0.32 to 0.39 and 0.99.
- **What decoding the mask maps costs** (14 maps, cold, decode and mip levels, one after another on one thread): 1,685
  ms in the libraries build, 4,077 ms in the dependency-free one. Round 485 has to keep that off the frame.
  `unity materials all` itself took 114 ms on SideRail the first time it was asked (it loads the `.mat` files no
  renderer wears) and 61 ms for the scene's own; in a project with 1,116 `.mat` files `all` takes about a second.
- **A material `unity materials all` loads is kept by the session like any other**: asked while a package unpacks it
  would cause fault 1 itself. The runs above ask it after the scene is open.

### 3. A fourth cause: 16-bit PNGs (found 02:10; criterion 4's `decoders`)
`decode_png_lib` (`src/image/image_libs.cpp:238`) reads PNGs through libpng's simplified interface, which takes a
file of 16 bits a sample as linear light and hands it back with a gamma of 2.2 applied: one half becomes 186 / 255.
Blendity's own decoder - the dependency-free build - takes the high byte, 128, as Unity does. `unity mapstats
decoders` on the 7 such PNGs SideRail's materials name (14 of its 111 PNGs are 16-bit):

| image | this build's mean R G B | the file's (Blendity's own decoder) | named by |
|---|---|---|---|
| `WoodPlanks_Rough_basecolor.png` | 0.708 0.661 0.606 | 0.472 0.405 0.336 | `WoodPlanks_Rough` (12 slots) |
| `WoodPlanks_Rough_normal.png` | 0.729 0.727 0.997 | 0.500 0.500 0.994 | `WoodPlanks_Rough` |
| `WoodPlanks_Rough_MOS.png` | 0.000 0.947 0.000 | 0.000 0.896 0.000 | `WoodPlanks_Rough` |
| `WoodFrame_normal.png` | 0.728 0.730 0.997 | 0.500 0.500 0.993 | `WoodPanel_2` |
| `Cloth_Normal.png` | 0.720 0.726 0.986 | 0.499 0.498 0.972 | `Orrange_Cloth` (16 slots) |
| `Cloth_AO.png` | 0.952 0.952 0.952 | 0.900 0.900 0.900 | `Orrange_Cloth` |
| `GradientLinear_2.png` | 0.864 0.864 0.864 | 0.748 0.748 0.748 | `SeparateLamp` (not worn) |

The plank floor's colour is about 2.4 times too bright in linear light and its normals lean by about 30 degrees where
the map says flat. At the stations it changes 39,413 pixels of `side_bar_high`'s 921,600 (4.3 %, by up to 157 of 255
levels) and 15,706 of `side_tunnel_eye`'s (1.7 %, up to 77), none at the other four; the MAE against Unity moves by
0.02. Every chain's SideRail number since the libraries were linked was taken with this. The fix is one flag
(`PNG_IMAGE_FLAG_16BIT_sRGB`); the round's tests pin 186 in a build with libpng and say round 485 flips it.

### 4. The stations against Unity (criterion 8)

MAE / SSIM against Unity's picture of the same station (F12's rasterizer; the targets are MAE at most 4 and SSIM at
least 0.90):

| state | spawn | spawn_back | floor | ahead | side_bar_high | side_tunnel_eye |
|---|---|---|---|---|---|---|
| Unity's bake, materials as imported | 2.42 / 0.866 | 1.39 / 0.758 | 2.15 / 0.828 | 2.16 / 0.925 | 3.75 / 0.898 | 3.67 / 0.902 |
| Unity's bake, materials **without their maps** (the reproduced state) | 4.65 / 0.759 | 1.63 / 0.784 | 6.90 / 0.682 | 5.04 / 0.780 | **13.71 / 0.702** | **12.50 / 0.697** |
| Unity's bake, materials at Unity's values by hand | 2.42 / 0.867 | 1.39 / 0.758 | 2.15 / 0.829 | 2.17 / 0.926 | 3.74 / 0.905 | 3.58 / 0.912 |
| Unity's bake, 16-bit PNGs read as stored | 2.42 / 0.866 | 1.39 / 0.758 | 2.15 / 0.828 | 2.16 / 0.925 | 3.73 / 0.898 | 3.67 / 0.902 |
| Unity's bake, by hand and 16-bit PNGs as stored | 2.42 / 0.867 | 1.39 / 0.758 | 2.15 / 0.829 | 2.17 / 0.926 | 3.71 / 0.905 | 3.57 / 0.912 |
| Blendity's bake, lights linear | 3.53 / 0.846 | 1.35 / 0.773 | 3.04 / 0.844 | 2.25 / 0.936 | 6.64 / 0.863 | 5.97 / 0.885 |
| Blendity's bake, lights **in gamma** (as a package's import leaves them) | 3.16 / 0.909 | 1.17 / 0.834 | 4.86 / 0.891 | 3.11 / 0.941 | **17.76 / 0.734** | **16.40 / 0.761** |

"Blendity's bake, lights linear" is Pass 21's last row to the digit. What the reflections carry, at the owner's two
stations (the picture rendered again without them):

| state | station | full | without reflections | change |
|---|---|---|---|---|
| materials as imported | side_bar_high | 3.75 / 0.898 | 3.38 / 0.899 | -0.38 / +0.000 |
| materials as imported | side_tunnel_eye | 3.67 / 0.902 | 3.02 / 0.914 | -0.65 / +0.012 |
| without their maps | side_bar_high | 13.71 / 0.702 | 11.95 / 0.610 | -1.76 / -0.092 |
| without their maps | side_tunnel_eye | 12.50 / 0.697 | 10.83 / 0.615 | -1.67 / -0.082 |
| by hand and 16-bit PNGs as stored | side_bar_high | 3.71 / 0.905 | 3.35 / 0.899 | -0.36 / -0.006 |
| by hand and 16-bit PNGs as stored | side_tunnel_eye | 3.57 / 0.912 | 3.01 / 0.915 | -0.57 / +0.003 |

- **Do the by-hand materials remove the mirror look?** The mirror look is the materials without maps, and their
  return removes it: at the owner's two stations 13.71 / 0.702 and 12.50 / 0.697 without maps against 3.75 / 0.898
  and 3.67 / 0.902 with them. Setting the ten materials' values right on top moves F12's picture little (3.74 /
  0.905 and 3.58 / 0.912): a surface at roughness 0 that is no metal mirrors its probe at about 4 %.
- **Do reflections need a change of their own?** Not for these two faults: with the materials right, the
  reflections carry 0.36 and 0.56 MAE of the 3.74 and 3.58 left at the owner's stations, as before. They stay Pass
  19's open item.
- **The Game view** (`C:/blcmp484/views/`, the owner's two stations as the views draw them, on the card and on the
  CPU): as imported, with the 16-bit PNGs as stored and with the values by hand the bar and the tunnel are matte, as
  the reference; without maps they are the chrome of the 00:34 screenshot.
- **How far do the gamma lights move the bake from Unity's?** Further than anything else measured here. A project
  made by unpacking a package has no `ProjectSettings`, so `unity_lights_linear_intensity` finds nothing and Lights
  Gamma stays on, silently; SideRail's own Unity project has linear intensity on. With Blendity's own bake the
  owner's two stations read 17.76 / 0.734 and 16.40 / 0.761 in gamma against 6.64 / 0.863 and 5.97 / 0.885 linear,
  and their pictures are 2.9 and 2.6 times as bright in linear light (mean 0.0284 and 0.0282 against 0.0098 and
  0.0109). The owner's 00:30 screenshot is this bake on materials without maps: both faults at once. Whether a
  package-only project should take its lights linear changes every such import, so it is put to the owner
  (the chains have measured the linear bake since round 459 by passing `unity lightsgamma off`).

### 5. The splotches (criterion 9)

SideRail at its LightingSettings asset (40 texels a unit, 32 direct and 1024 indirect samples, 2 bounces; 2 pages of
2048 px, 820,031 texels, 890 M rays, every texel gathered on the card), `tools/unity_compare.py noise`: each bake at
seeds 0 and 1. Grain and blotch are round 463's measures over the lightmap pages. **Specks** are new
(`speck_count`): at a station, rendered with each seed's bake, the connected spots of six pixels or more where the two
pictures differ by more than half the picture's mean light - a count the eye agrees with, where grain and blotch are
means that a hundred small spots hardly move. Pass 21's last row was grain 0.382, blotch 0.251.

| bake (lights linear unless said) | grain | blotch | blotch, page 0 / 1 | lights kept by chance / dropped a texel | side_bar_high: grain / blotch / specks | side_tunnel_eye: grain / blotch / specks |
|---|---|---|---|---|---|---|
| today (`pick` 8) | 0.382 | 0.251 | 0.197 / 0.548 | 0.17 / 0.46 | 0.0976 / 0.0318 / **147** | 0.0913 / 0.0279 / **133** |
| every light (`lighting pick off`) | 0.384 | 0.251 | 0.197 / 0.548 | 0 / 0 | 0.1037 / 0.0283 / 178 | 0.0934 / 0.0257 / 139 |
| `lighting pick 16` | 0.383 | 0.251 | 0.197 / 0.548 | 0.14 / 0.35 | 0.0987 / 0.0268 / 161 | 0.0902 / 0.0243 / 135 |
| `lighting pick 32` | 0.383 | 0.251 | 0.197 / 0.548 | 0.12 / 0.26 | 0.1019 / 0.0279 / 167 | 0.0921 / 0.0251 / 136 |
| the filter off (`Lighting.Denoise false`) | 5.824 | 1.149 | 1.020 / 1.951 | 0.17 / 0.46 | 1.4997 / 0.4253 / 893 | 1.2847 / 0.4163 / 721 |
| round 423's filter (`lighting denoiser atrous`) | 0.513 | 0.273 | 0.218 / 0.583 | 0.17 / 0.46 | 0.2445 / 0.0774 / 747 | 0.1925 / 0.0653 / 489 |
| lights in gamma (as a package's import leaves them) | 0.482 | 0.222 | 0.179 / 0.603 | 0.16 / 0.48 | 0.0733 / 0.0229 / 138 | 0.0668 / 0.0193 / 137 |
| **the owner's session: no maps, lights in gamma** | 2.493 | 1.335 | 0.490 / 4.802 | 0.16 / 0.48 | 0.2081 / 0.0808 / **326** | 0.2729 / 0.0928 / **344** |

Where the specks come from - one setting changed a run:

| bake | grain | blotch | side_bar_high: specks | side_tunnel_eye: specks |
|---|---|---|---|---|
| today (1024 indirect samples from a Sobol sequence) | 0.382 | 0.251 | 147 | 133 |
| the Baked lights' direct light alone (`Lighting.IndirectIntensity 0`) | 0.190 | 0.058 | 24 | 47 |
| the same with every light kept (`lighting pick off` too) | 0.187 | 0.055 | 1 | 30 |
| no bounce: the lights, and the emitters sampled at the texel (`Lighting.Bounces 0`) | 0.217 | 0.070 | 24 | 45 |
| no texel filled from its neighbours (`Lighting.BackfaceTolerance 0`) | 0.373 | 0.253 | 147 | 137 |
| a quarter of the indirect samples (256) | 1.971 | 0.818 | 59 | 78 |
| twice the indirect samples (2048) | 0.621 | 0.349 | **271** | **210** |
| plain random samples (`lighting sampler random`) | 3.367 | 0.982 | 59 | 64 |

- **How much of the blotch is the lights kept by chance? None that the bake's measure sees, and about one speck in
  seven.** On SideRail a texel keeps 0.17 lights by chance and drops 0.46: its lamps reach a texel a few at a time.
  With every light kept the bake's grain and blotch are the same to the third decimal. In the direct light alone the
  picking shows: 24 and 47 specks with it, 1 and 30 without. The plan's cause for the splotches - and Pass 21's note
  that SideRail's noise is "the chance-kept point lights" - holds for about 20 of a view's 140 specks.
- **Does the filter add to it? No: it removes most of it and leaves the rest.** Unfiltered the bake's grain is 5.82
  and a station shows about 800 specks; round 466's filter leaves 0.38 and about 140, round 423's A-Trous 0.51 and
  about 600.
- **What the splotches are:** small round spots, a few texels wide, in one seed's bake and not in the other's - what
  the filter makes of a texel that drew rare very bright samples. About 100 of a view's 140 are in the **gathered
  bounce**: they go when the bounced light is switched off, and neither the emitters sampled at the texel nor the
  texels filled from their neighbours move them.
- **Why the bounce's bright samples get through - the round's lead for 486.** More samples make it *worse*: 2048
  give 271 and 210 specks and a bake's blotch of 0.349, 256 give 59 and 78. No sampling noise grows with the sample
  count; a wrong estimate of the noise does. Since round 465 the bake scales the variance it measures for Sobol
  samples by `4 / sqrt(samples)` (`qmc_scale`, `lightmapper.cpp:103`: stratified samples beat what their spread
  says) - an eighth at SideRail's 1024 samples, an eleventh at 2048, a quarter at 256, none for plain random
  samples. The firefly clamp ("past 8 times its neighbours, and within 4 of its own deviations: a lucky ray") and the
  filter's edge-stop both read the scaled number. Stratification does not help a texel whose noise is two or three
  rays that met a small bright patch, so there the noise is understated - 2.8 times at 1024 samples - and the texel
  passes for steady light: not clamped, not filtered enough, a speck. The specks follow the scale, not the samples:
  59 and 64 with plain random samples (no scale) at 1024, 147 and 133 with Sobol's and the scale. Sobol samples do
  what round 465 measured for the bake as a whole (plain random ones: grain 3.37 against 0.38); it is the rare
  bright samples the scale is wrong for. **This is a lead, not yet a proof**: the two samplers differ in more than
  the scale, and the run that would settle it - Sobol samples with the scale off - needs a switch the bake does not
  have. It is round 486's first step.
- **Lights in gamma add no specks of their own** (138 and 137); **materials without maps more than double them** (326
  and 344): white, unmapped surfaces bounce far more light, and that bounce is where the specks are.
- **What 1.25 times the bake time buys:** nothing here if it is spent on the picked lights. On a quiet machine (the
  Pass 22 worker holding; five bakes one after another in one session, the round's final build) the default bake takes
  **38.9 s** (lightmaps 36.5 s, the filter 2.0 s) - Pass 21's 39 s - and `pick` 32 takes 40.1 s (37.6 s). The
  session's first bake took 78.3 s: it pays once for the meshes going onto the card and the materials frozen. `pick
  off` and 16 ran while another worker's build did (61.7 and 67.7 s) and are not timings; by its rays, keeping every
  light costs SideRail about 1 % (0.46 lights a texel dropped: at most 12 M shadow rays more on 890 M). So the budget of
  1.25 times the bake time - about 49 s - is free for round 486 to spend on the bounce.

### 6. `lamp_hall` (criterion 10)

`lmq::lamp_hall()`: a hall of 24 x 6 x 3.2 m with a side room, six pillars and a counter, 72 Baked point and spot
lights with Unity's falloff under a black sky, 5 texels a metre (27,693 texels); its reference keeps every light at
every texel at 16 times the samples. `blendity_stress --only lightmap_quality`, the libraries build:

| scene | texels | relative RMSE | bias | grain | blotch |
|---|---|---|---|---|---|
| a room lit by a ceiling area light | 38,895 | 0.013 | 0.0007 | 0.0084 | 0.0028 |
| a corridor lit by 8 point lights | 31,329 | 0.004 | -0.0003 | 0.0033 | 0.0008 |
| a bent tunnel lit by lamps | 85,896 | 0.103 | -0.0001 | 0.0622 | 0.0209 |
| a courtyard under a sun and the sky | 22,000 | 0.007 | -0.0001 | 0.0055 | 0.0014 |
| **a hall lit by 72 lamps** | 27,693 | 0.043 | 0.0005 | 0.0327 | 0.0096 |

The four older rows are those of Pass 21's last chain to the last digit (round 466: 0.013 / 0.0007 / 0.0084 / 0.0028,
0.004 / -0.0003 / 0.0033 / 0.0008, 0.103 / -0.0001 / 0.0622 / 0.0209, 0.007 / -0.0001 / 0.0055 / 0.0014).

| `lamp_hall`: lights a texel keeps by chance | filter | relative RMSE | bias | grain | blotch | kept a texel | dropped a texel |
|---|---|---|---|---|---|---|---|
| every light (`pick` off) | on | 0.010 | 0.0003 | 0.0082 | 0.0020 | 0 | 0 |
| about 8 (the bake's own) | on | 0.043 | 0.0005 | 0.0327 | 0.0096 | 1.504 | 5.304 |
| about 16 | on | 0.023 | 0.0000 | 0.0175 | 0.0054 | 1.321 | 4.332 |
| about 32 | on | 0.015 | 0.0002 | 0.0117 | 0.0036 | 1.172 | 3.467 |
| every light (`pick` off) | off | 0.013 | 0.0000 | 0.0139 | 0.0022 | 0 | 0 |
| about 8 (the bake's own) | off | 0.081 | -0.0001 | 0.0870 | 0.0132 | 1.504 | 5.304 |

- **Here the picking is the noise**: about 8 lights a texel leaves **4.8 times the blotch** of keeping every light
  (0.0096 against 0.0020, the filter on) and 4.3 times the error against the reference; a texel drops 5.3 lights by
  chance. On SideRail it drops 0.46: the hall is the stand-in for a world whose lamps overlap, which SideRail's do
  not. Both are true, and round 486 has to keep them apart.
- The section's bake times are not reported: the run shared the machine with the measurements above (the hall's
  reference took 305 s there; the whole section 27 minutes). Round 486 times it when it changes the bake.

### 7. Nothing else changes (criterion 11), the owner's folders (criterion 12)
- **A bake at the defaults saves the same pages from main's build and from this round's**: a room with twelve Baked
  point lights made by one console script, baked and saved by each exe (`C:/blab/drafts/484/c11.sh`): `Lightmap-0.hdr`
  (4,194,368 bytes), `LightmapDir-0.hdr`, `LightingData.bin` and `objects.json` are the same bytes, and so are F12's
  pictures of the starter scene and of the baked room. Each build repeats its own pages (two runs each).
- **It did not hold at first.** With `light_pick_keep(pk, li, rays)` taking the rays as a run-time number, the round's
  build and main's differed in 2 bytes of `Lightmap-0.hdr` - every time, the same two. MSVC (`/O2 /GL /fp:fast`)
  ordered `rays * est / sum` another way than `8 * est / sum`: a last-bit difference in a keep probability, enough to
  turn one decision in millions. `light_pick_keep` is main's again and the measuring path is a function of its own,
  out of line and never inlined (`light_pick_keep_rays`, as `corner_normalize` in `mesh.cpp`). The measurements above
  were taken with the build of 01:49, before that change; today's bake measured again with the final build gives the
  same numbers (grain 0.382, blotch 0.251, 147 and 133 specks).
- **The editor's own screenshot is not the same pixels from one run to the next** even with one exe (its console
  prints each command's milliseconds: 1,586 pixels differ between two runs of main), so F12's picture is compared
  instead.
- **One line is new in a default bake's log**: a bake that picks among more than eight lights says "light picking at
  about 8 lights a texel - ... kept by chance and ... dropped a texel". It is the measurement itself, at the default.
- **The owner's folders**: no file under the package's folder (4 files, 506,170,552 bytes), `blendity\Assets` (857
  files, 553,171,886 bytes), `SideRail Blendity` (867 files, 756,721,036 bytes) or `C:\blsiderail` (23,071 files,
  2,404,081,811 bytes) has a modification time after 2026-11-10 00:40, when the round began. No listing was taken
  before the round, so this is the check there is. Two folders of about 111 MB each did land in the owner's
  `~/.blendity/cache/unity-model-ids/` (01:27 and 01:31): the first two runs did not yet point `BLENDITY_CACHE_DIR` at
  the scratch folder. They are caches Blendity makes on any import; they were left as they are.

### What was tried and ruled out
- The scene of 2026-11-02 as the screenshot's scene (it has no `ConcretePanel`).
- A saved scene's own values as the cause (the `.mat` wins at open).
- The lights kept by chance as the splotches (section 5).
- The filter as the maker of the blotches (section 5).
- Reflections as a third of the mirror look (section 4).
- The emitters sampled at the texel, and the texels filled from their neighbours, as the specks' source (section 5).
- The lights in gamma as a source of specks (they brighten the picture; the specks stay as many).
- One of the round's own runs is not used: the first bake with the lights in gamma lost the graphics card ("could not
  open NVIDIA GeForce RTX 4070 SUPER", while three workers used it) and gathered one of its two seeds on the CPU in
  44 minutes; it was run again.

## Deviations from this spec
- **`--after-open` was not built**; the states are `tools/unity_materials.py state <project> nomaps | byhand | png8`
  (they rewrite a scratch project's files, so the importer itself reads each state) and `unity_compare.py`'s
  `--lights-as-imported`, `--bake-cmd`, `--reuse-unity-lightmaps`. `table`, `state` and `unity_compare.py noise`
  refuse a project without an empty `BLENDITY_SCRATCH` file beside its Assets folder.
- **Added beyond the scope:** `unity mapstats`' first word `decoders` and `decode_image_own` (`image.h`) behind it; the
  12th material and the 16-bit map of the committed set; `state png8`; the fault classes `values differ` and `decoders
  differ`; `unity_compare.py noise` (a bake's noise without Unity) and the speck count at the stations; the runs of
  section 5 that look for the specks' source.
- **Criterion 5's wording** ("16 keeps more lights a texel") was wrong and is corrected in place.
- **Criterion 4's refusal in a window** is not in the suite (checked in the real window); **criterion 6** is said
  exactly in place; **criterion 11** became a compare of two builds.
- **`unity materials` and the 100 ms frame:** 61 ms for SideRail's scene, 114 ms the first time with `all`. In a
  project with 1,116 `.mat` files `all` takes about a second: it is a developer's verb, and it says its time.
- **`lamp_hall` is 5 texels a metre**, not the 10 it was first written with: its reference keeps every light at every
  texel at 16 times the samples, and at 10 that one bake took half an hour on a busy machine.
- **Bake times**: the default bake and `pick` 32 were timed on a quiet machine; `pick off` and 16 were disturbed by
  another worker's build (its "hold" had not reached that session) and are given as that, with the ray count's
  estimate beside them. The stress section's times were not taken quiet either.
- **The libraries suite's four `gpu` picture comparisons fail as they have in every worktree build since round 463**
  (a mean of 1.025 against a limit of 1.0, 0.101 against 0.1; the Pass 22 worker's tree fails the same four with the
  same numbers), and Linux's "Live Link never pauses the editor" failed while the machine was baking and passes alone.
  Neither is this round's; both are in the CHANGELOG's "Checked".
- **`tools/dev/README.md`** needed no change (nothing in `tools/dev` changed).
