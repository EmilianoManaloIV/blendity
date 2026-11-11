# 0461: Unity materials keep their maps and their gloss (round 485)

- **Status:** done (round 485; approved by the owner's words of 2026-11-10, relayed by the hub session: "Continue and
  finish all tasks, I approve all specs from all passes."; cut from round 484's results - what changed against Pass
  24's plan is said under "Against the plan"). The results and the deviations are at the end.
- **Requirements:** R-04 (materials), R-05 (rendering), Q-03 (tests), Q-06 (responsiveness), C-04 (the user's files)
- **Decisions:** ADR 0131 (a Unity material's smoothness: one number a material now, a texel later)
- **Model:** main session (Opus) - a worker job, the asset library and two decoders; then test-engineer, code-reviewer
- **PR:** (link when opened)

## Goal
Round 484 named what makes the owner's imported world look like polished metal (docs/tasks/0460, "Results"). After this
round: a Unity package imported and its scene opened in one session - whatever windows are open, and after deleting
the folder and importing again - gives every material the maps and values a fresh session gives it; a material whose
smoothness Unity keeps in a map has Unity's roughness on average instead of none; and a 16-bit PNG reads the same in
both builds. The owner's saved scene shows it at its next open, with nothing to redo by hand.

## What round 484 measured (the numbers this round has to beat)
- **Materials without maps** (the Inspector's picture): 6 of 7 runs of "Materials window open, import the package, open
  the scene"; 17 to 37 of 38 linked materials without any map (10 is right); at the owner's two stations 13.71 / 0.702
  and 12.50 / 0.697 (MAE / SSIM against Unity) where a fresh import gives 3.75 / 0.898 and 3.67 / 0.902.
- **Delete the folder, import again, open**: 54 materials, none linked to a `.mat`.
- **Smoothness dropped**: 10 worn materials (181 renderer slots) at roughness 0; Unity's 0.24 to 0.76. Twelve of the 57.
- **16-bit PNGs**: 7 the materials name read brighter in the libraries build (one half reads 186 / 255, not 128).
- With the values set by hand and the PNGs as stored: 3.71 / 0.905 and 3.57 / 0.912 at the owner's two stations.

## Scope
- In:
  1. **A material read too early is read again, where the import ends.** When a package's unpack finishes
     (`Editor::poll_unity_package`, after the GUID index is refreshed), every Unity material the asset library holds
     under the package's folder is read again into the same `Material` object - its users, the Inspector and the
     Materials window follow without a reopen - and the folder's "deleted this session" marks are cleared
     (`scene_io.cpp`: `reload_foreign_materials(under)`, `revive_asset_paths(under)`). The list of the project's `.mat`
     files leaves out a folder that is still unpacking (`Editor::material_asset_paths`: the Materials window, the
     material picker and `unity materials all` all read it). Reading a file again is not an edit: no undo step, the
     scene not marked changed - and the copies the undo and redo states keep of those materials take the new reading
     too (`Editor::reread_unity_materials`), since a restored state's values win over the asset's when they differ and
     an Undo back past the re-read would otherwise put the old reading on the asset for the rest of the session; so
     does the scene Stop brings back when the re-read falls in Play. What that costs: an edit made by hand to a Unity
     material in this session goes with the re-read, as it goes with a restart - for the materials that are read
     again: an import's own folder, the materials whose maps a job measured, or all of them when `unity rematerials`
     is asked for. A scene's copy that stands in for a `.mat` missing when the scene opened is read from the Unity
     file once it is there, and is never written over it.
  2. **The keyword decides, as in Unity** (`read_gloss`, the Standard / Filamented / URP Lit families): without
     `_METALLICGLOSSMAP` (URP: `_METALLICSPECGLOSSMAP`) the sliders count and the slot's texture is not attached; with
     it, metal is the map's red (as today) and smoothness its alpha times `_GlossMapScale` (URP: `_Smoothness`); under
     `_SMOOTHNESS_TEXTURE_ALBEDO_CHANNEL_A` smoothness is the albedo's alpha times the scale, whatever the metallic
     map's alpha holds. A slot a keyword names and nothing fills is Unity's white texture: all metal, alpha 1.
     Filamented's roughness setup: its metal follows the keyword the same way; its roughness (`_Glossiness`, a
     `_SpecGlossMap` in the slot) stays as round 395 reads it. Not read: URP Lit's specular setup (its keyword samples
     `_SpecGlossMap`), and Standard (Specular setup), which stay as they were.
  3. **Smoothness from a map, as one number a material** (ADR 0131): roughness = 1 - (the map's mean alpha) x scale,
     the alpha being the one Unity's shader samples (the image's own; 1 when the texture's Alpha Source is None; its
     grey when From Gray Scale). The mean comes from a table beside the package's GUID table, `.unity-stats` (one line
     a texture: GUID, the file's bytes when it was measured, width, height, mean R G B A; a line for a file of another
     size is not read), written
     - by the unpack's worker for the textures its materials read their gloss from by their keywords
       (`unity_material_gloss_textures`; decoded there, off the main thread; SideRail: 14 maps, 1.7 s with Blender's
       libraries, 4.1 s without), before the GUID table, so whoever finds the one finds the other;
     - for a package unpacked before this round, by one background job (`Editor::GlossJob`; the job tray: "Reading
       Unity materials' gloss") started when a scene opens, a Unity scene is imported or `unity rematerials` is typed
       and a material asked for a mean no table has: the materials first with the alpha at 1 (what Unity shows for
       a texture it cannot find; for most that is today's value), then - the ones that asked, no others - read again
       when the job lands, the lines added to the package's table for the next session. Nothing starts or lands
       while a package unpacks. In a headless run the job runs and lands at once, so a script's next line reads the
       measured gloss. A texture that belongs to no package folder (a Unity
       project's Assets copied in) is measured the same way and kept for the session only - good while the file's
       path and bytes are the ones measured, as a table's line is.
     A texture that cannot be read, or a line that is not numbers, leaves the material at today's value; such a
     texture is not sent to the worker again on its own in the session (`unity rematerials` tries it again). Cancel
     in the job tray stops the job at its next texture: the materials whose maps were not reached stay as they are
     until `unity rematerials`. Quitting waits for the worker, as it does for an unpack.
  4. **16-bit PNGs read as stored in the libraries build**: `decode_png_lib` asks libpng for the file's own samples
     (`PNG_IMAGE_FLAG_16BIT_sRGB`), so both decoders agree within one 8-bit step on every 16-bit PNG that carries no
     gamma chunk of its own. No decoded texture is kept on disk (the channel maps a model's import derives are
     written again at each import), so no old reading of a texture outlives the session. **A bake does**: lightmaps
     baked while the materials had no maps, or with the brighter 16-bit textures, were lit by those materials, and
     the lighting's key holds a material's colour, emission and Metallic but not its maps - such a bake is not marked
     out of date. Generate Lighting again; the changelog says so.
  5. **`unity rematerials`** (console) and **Assets > Reimport Unity Materials**: every `.mat` of the project loaded,
     the Unity ones read again from their files at once (it says how many changed), and the job of 3 started for the
     textures whose mean is still missing. The way to bring a running session in line without restarting, and what the
     tests drive. Refused while a package unpacks (its materials are read again when it ends).
  6. **Round 484's pins flip**, each with its reason: the tests of `tests_round484` that pin today's faults (maps lost
     at import; smoothness in the mask's alpha, scaled, in the albedo's alpha; the slot without its keyword; the 16-bit
     PNG's libpng branch), `tests/fixtures/unity_gloss/blendity_today.txt` and `expected.json`'s `faults_today`
     (`python tools/unity_materials.py --self-test`), `test_assets.cpp:9038`.
  7. Learn: "A Unity material beside Unity's own values" gains what changed (the gloss a map holds; Reimport Unity
     Materials), a Try-it and a question.
- Out (not in this task):
  - **Smoothness per texel** (a wall's worn patches glossier than its dust): ADR 0131's second step - a channel on the
    texture slot through every renderer and exporter. Round 484's stations do not ask for it yet.
  - **Reflections off, highlights off, occlusion maps**: three things a Unity material says that Blendity's has no
    property for (2 + 3 worn materials on SideRail).
  - **The 17 worn materials on shaders the tool has no rule for** (lamps, glass, mirrors): they are read as rounds
    400-405 left them.
  - **The lights' intensity** in a project made from a package: the owner's decision (round 484's results, 5).
  - **Scenes saved by an older importer** (67 materials of the models' own, none linked): opening the `.unity` again
    is the repair; the plan's relinking through each object's Unity origin is not built unless asked for.

## Against the plan
Pass 24's plan cut this round as "materials import as in Unity + Reimport Unity Materials (a repair)". Round 484
changes three things: the maps go missing at import, not in the reader, so the fix is where the import ends and the
repair command shrinks to a re-read (the old-scene relinking drops out); a fourth cause joins (the 16-bit PNGs); and
smoothness per texel is put after the picture's larger faults, with the per-material number now.

## Likely files
- `src/editor/unity_package_ui.cpp` (`PackageJob::unpack`: the table; `poll_unity_package`: the re-read),
  `src/scene/scene_io.cpp` + `scene.h` (`reload_foreign_materials`, `revive_asset_paths`),
  `src/editor/project_window.cpp` (`material_asset_paths`: the list every reader of `.mat` files uses),
  `src/editor/unity_scene_ui.cpp` (the index carries the stats; the loader's mean), `src/editor/unity_gloss_ui.cpp`
  (new: the job, its landing, `reread_unity_materials`, `unity rematerials`), `src/editor/job_tray.cpp`,
  `src/editor/editor_commands.cpp` + the Assets menu
- `src/scene/unity_material.{h,cpp}` (`read_gloss`: keyword, the mean; `unity_material_gloss_textures`),
  `src/scene/unity_scene.{h,cpp}` (`UnityGuidEntry`, `unity_texture_mean`, `unity_stats_read`)
- `src/image/image_libs.cpp` (`decode_png_lib`)
- `tests/test_assets.cpp` (`tests_round485`; round 484's pins), `tests/fixtures/unity_gloss/` (`blendity_today.txt`,
  `expected.json`, a `.unity-stats`), `tools/unity_materials.py` (reads `.unity-stats` in its self-test)
- `docs/decisions/0131-unity-smoothness.md`, `docs/ARCHITECTURE.md`, `docs/USABILITY.md`, `CHANGELOG.md`, `README.md`

## Acceptance criteria
1. **The Inspector's picture cannot be made.** Round 484's sequence in the real window (the owner's layout: the
   Materials window open; import SideRail's package; open `SideRail.unity`; `unity materials`), five runs: each has 10
   linked materials without any map and `ConcretePanel` with its base, mask and normal map. Delete the folder, import
   again, open: 38 materials linked.
2. **The mechanism, in the suite** (the committed set): a Unity `.mat` loaded before its package's GUID table exists
   has no maps (as round 484 pinned); after the import ends - the set packed as a `.unitypackage` and imported in the
   test - the same `Material` object has its maps and the values of a fresh load, and the unpack's `.unity-stats` is
   the committed one byte for byte. After `unity rematerials` a renderer wearing such a material shows them, with no
   undo step and the scene not marked changed (a scene's text keeps a linked material's values as a fallback beside
   its `.mat`'s path, so its next save writes the new ones). A folder deleted with `deleteasset` and imported again:
   its materials load. An edit made before the re-read and undone after it leaves the material at the new reading,
   not the old one. While a package unpacks (a job standing in for the worker: `Editor::set_package_job_for_test`)
   the list of `.mat` files leaves its folder out, the open Materials window loads none of them and `unity
   rematerials` is refused.
3. **The keyword**: `slot_without_keyword` reads Metallic 0.2, Roughness 0.7 and no metallic map.
4. **The gloss**: with the set's `.unity-stats`, `gloss_in_metal_alpha` reads roughness 0.639, `gloss_scaled` 0.820,
   `gloss_in_albedo_alpha` 0.799, `filamented_mask` 0.639, `urp_lit_mask` 0.820 (each within 0.005) and `metal_from_red`
   stays 0.8 / 0.75; without the table they read today's values and the job (run to its end in the test) brings the
   same numbers and writes the table; a line of junk or a texture that is gone changes nothing and does not crash.
   `python tools/unity_materials.py --self-test`: no `smoothness dropped`, no `slot without keyword` (the tool counts
   a texture left in the slot as a fault only when Blendity went by it: the file itself is as it was), the committed
   `.unity-stats` agrees with the tool's own reading of the PNGs, and round 484's reading - built again from the
   listing - still gets round 484's faults.
5. **16-bit PNGs**: `unity mapstats decoders` gives `mean` within 1 / 255 of `own_mean` on the committed 16-bit map and
   on generated 16-bit grey, RGB and RGBA ramps, in both builds; an 8-bit PNG's bytes are as before.
6. **SideRail** (scratch copies, as round 484): the tool's table has `smoothness dropped` 0 (10), `decoders differ` 0
   (3 worn); at the owner's two stations with Unity's bake no worse than round 484's by-hand row - `side_bar_high` at
   most 3.71 MAE and at least 0.905 SSIM, `side_tunnel_eye` 3.57 and 0.912 - and the other four stations within 0.02
   MAE of round 484's.
7. **Nothing waits on a decode**: opening `SideRail.unity` in a project with the table takes no longer than round
   484's open (within 5 %, measured the same way); without the table the open is as fast and the job's time is said;
   the unpack's added time is said (SideRail: under 2 s on its worker).
8. Materials that are not Unity's read as before (the existing tests pass unchanged); the full suite passes on
   Windows (both builds) and Linux; the audits pass.

## Tests and checks to run
- Unit: `BLENDITY_TEST_FILTER="Unity material"`, `--group assets` in both Windows builds, then the full suite (Windows
  both builds, Linux). `python tools/unity_materials.py --self-test`.
- SideRail, in scratch copies under `C:/blcmp484` only (the owner's folders are read, never written): the windowed
  runs of criterion 1, the table, the stations with Unity's bake.
- Stress: none (no geometry).

## Responsiveness and cold first use (ADR 0105)
- No image is decoded on the main thread for a material's values: the unpack's worker and the job do it; the frame
  that lands the job re-reads materials (text only: SideRail's 57 in under 20 ms).
- A session's first open of a scene whose package has no table shows today's gloss for the job's seconds, then the
  right one: said in the log and the job tray.

## Risks and edge cases
- **A material edited in Blendity** while its package's job runs: Unity materials are read from their file at every
  open; the re-read replaces what the session shows with the file's reading, as a restart would.
- **Two packages sharing a texture's GUID**; a package unpacked over itself; a cancelled unpack (the table holds what
  was unpacked); a texture replaced on disk after its line was written (the line carries the file's bytes: a line for
  another size is not read and the job measures the texture again; a replacement of the very same size is not noticed
  until the package is unpacked again).
- **The unpack measures by keywords alone** (the shader's family is not known on its worker): a stale keyword on a
  toon material costs one decode and nothing else. **The table only grows**: a texture replaced on disk leaves its
  old line (not read: another size) beside the new one. **A folder imported again under a deleted parent**: the
  parent's "deleted" mark goes with it (`revive_asset_paths`), so a deleted sibling's materials then behave as
  missing files do after a restart.
- **The lighting's "out of date"**: its key holds a material's Metallic, not its Roughness. Only a material whose
  slot held a texture without the keyword changes its Metallic (1 before, the slider now), and a scene that wears one
  asks for a new bake once - rightly: its bake took the surface for metal.
- **A PNG with a gamma chunk of its own** still goes through libpng's conversion (none in the measured world; the
  tool's `decoders differ` names such a file).
- **One number is not a map**: a material whose smoothness varies a lot across its map (worn metal) is right on
  average and flat in detail until ADR 0131's second step.
- **Unity's missing-texture case**: a keyword on and no texture file is Unity's white texture - metallic 1, smoothness
  1 x scale. Kept for a texture that is truly not in the project; no longer reachable by reading too early.
- The dependency-free build, GCC's explicit lambda types around `get<T>()`, shallow nesting, SPDX headers; the worker
  touches nothing of the editor but its own job.

## Documentation to update
- CHANGELOG (round 485), README (the check count, the Unity paragraph), ARCHITECTURE rows, USABILITY ("fixed": a Unity
  world's materials come in as mirrors after importing its package), ADR 0131, the Learn section, this file.

## Results (round 485)

### In short
- **The Inspector's picture cannot be made any more.** Round 484's sequence in the real window with the owner's
  layout (the Materials window open; import SideRail's package; open its scene), five runs: no material was read
  while the package unpacked, all 38 Unity materials are linked, and `ConcretePanel` has its base, mask and normal
  map in each (round 484: maps lost in 6 of 7 runs, 17 to 37 of 38 materials). Delete the folder, import again, open:
  38 linked (round 484: none).
- **The materials read Unity's values.** Of SideRail's 38 worn Unity materials "smoothness dropped" went from 10 to 0
  and "decoders differ" from 3 to 0; each of the 21 Blendity has a rule for reads Unity's metallic and roughness to
  two decimals. Over all 57: smoothness dropped 12 -> 0, slot without keyword 1 -> 0, decoders differ 4 -> 0.
- **The picture is where round 484's by-hand row put the mark**: 3.71 / 0.905 and 3.57 / 0.912 at the owner's two stations
  (3.75 / 0.898 and 3.67 / 0.902 before) - `side_bar_high` now passes the target.
- The gain in the picture is small, as round 484 said it would be: the mirror finish in the owner's screenshots was
  the lost maps (13.71 / 0.702), and that is what can no longer happen. **Lightmaps baked in such a session want one
  more Generate Lighting** - a bake is not marked out of date by a material's maps.

### 1. Round 484's sequence in the real window (criterion 1)
`C:/blab/drafts/485/repro_all.sh` (scratch projects under `C:/blcmp485/repro`; the owner's package and preferences
read, nothing of theirs written), this round's build with Blender's libraries, `--windowed-check`:

| run | linked to a `.mat` | of them without any map | read while the package unpacked | `ConcretePanel` |
|---|---|---|---|---|
| the Materials window open, import, open (five runs) | 38 of 40, each run | 10, each run (the ten that have none) | 0, each run | base, mask and normal map; Metallic 1 x its map, Roughness 0.676 |
| the unpacked folder deleted, import again, open | 38 | 10 | 0 | the same |
| a project unpacked by round 484's build (no table), open | 38 | 10 | - | the same, once the job landed |

Round 484, same sequence: 17 to 37 of the 38 without any map in 6 of 7 runs; after delete and import, none linked.
Three of the ten are the world's mirrors (`PlayersOnlyMirror_*`): Metallic 1, Roughness 0 and no map on purpose.

### 2. SideRail's materials and stations (criterion 6)
`py tools/unity_materials.py table` on a scratch copy of round 484's fresh import (`C:/blcmp484/p_r485`; its package
was unpacked by round 484's build, so the first open measured the maps and wrote the table: 13 lines):

| | round 484 (fresh import) | round 485 |
|---|---|---|
| worn by the scene (38) | not modelled 17, smoothness dropped 10, ok 9, decoders differ 3, occlusion ignored 3, highlights off ignored 2 | not modelled 17, ok 16, occlusion ignored 3, highlights off ignored 2 |
| all (57) | not modelled 32, smoothness dropped 12, ok 9, decoders differ 4, occlusion ignored 3, highlights off ignored 2, slot without keyword 1 | not modelled 32, ok 20, occlusion ignored 3, highlights off ignored 2 |
| 16-bit PNGs the materials name: read differently by the two decoders | 7 of 7 | 0 of 7 |

The stations with Unity's own bake (MAE / SSIM against Unity's picture; `tools/unity_compare.py world
--reuse-unity-lightmaps`):

| | spawn | spawn_back | floor | ahead | side_bar_high | side_tunnel_eye |
|---|---|---|---|---|---|---|
| round 484, as imported | 2.42 / 0.866 | 1.39 / 0.758 | 2.15 / 0.828 | 2.16 / 0.925 | 3.75 / 0.898 | 3.67 / 0.902 |
| round 484, by hand and 16-bit PNGs as stored (the mark) | 2.42 / 0.867 | 1.39 / 0.758 | 2.15 / 0.829 | 2.17 / 0.926 | 3.71 / 0.905 | 3.57 / 0.912 |
| **round 485** | 2.42 / 0.867 | 1.39 / 0.758 | 2.15 / 0.829 | 2.16 / 0.926 | **3.71 / 0.905** | **3.57 / 0.912** |

Rendered twice - with the build from before the review's fixes and with the final one - to the same numbers; the
worn materials' listing is the same in both. Criterion 6's marks (3.71 / 0.905, 3.57 / 0.912, the other four within
0.02 MAE) are met exactly, and `side_bar_high` passes the stations' own target (SSIM 0.90) for the first time.

### 3. The mechanism in the suite (criteria 2 to 5)
`tests_round484` (round 484's pins, rewritten: each says what it pinned), `tests_round485` (14 tests) and
`tests_round485_independent` (the test-engineer's 10) in `tests/test_assets.cpp`; round 371's block. What they hold:
- a material read while its package unpacks is chrome without maps; a package built from the committed set and
  imported in the test gives the same `Material` object its maps and Unity's roughness, and the unpack's
  `.unity-stats` is the committed one byte for byte;
- a wall wearing such a material shows the maps after `unity rematerials` with no undo step and the scene not marked
  changed; an Undo and a Redo across the re-read keep them; so does Stop when the job lands in Play;
- a folder deleted and imported again loads its materials; a file inside it, or a folder above it, deleted likewise;
- the keyword: `slot_without_keyword` 0.2 / 0.7 with no metallic map; a keyword over an empty slot is Unity's white;
  both keywords read the albedo's alpha; Filamented's Roughness setup follows the keyword for its metal only;
- the gloss: 0.639, 0.820, 0.799, 0.639, 0.820 on the committed set (each within 0.005), `metal_from_red` unchanged;
  without the table the job brings the same numbers and writes the table; a table of junk, of 1 MB of random bytes,
  a map that is a `.txt`, a zero-byte or a half PNG change nothing and leave nothing running;
- one map measured reads one material again: a value typed on another stays; a cancelled job lands what it
  measured and its untouched textures wait for `unity rematerials`;
- two packages sharing a texture's GUID; a package imported over itself; a Unity project's own Assets (no file
  written beside them; a replaced map measured again);
- a scene opened while its package's folder is missing: the import reads the Unity files into its copies and no
  save writes over a Unity `.mat`;
- 16-bit PNGs of the four colour types within one 8-bit step through both decoders, in both builds; 8-bit bytes as
  before.
`py tools/unity_materials.py --self-test`: all passed (no `smoothness dropped`, no `slot without keyword` in round
485's reading; round 484's reading, built again, still gets round 484's faults; the committed `.unity-stats` agrees
with the tool's own reading of the PNGs).

### 4. Nothing waits on a decode (criterion 7)
All of it was measured while two other passes built and tested on the machine - no quiet window was to be had in
this round - and the numbers that depend on that say so.
- **With the table, the open decodes nothing for a material's values** (no "measuring" line in any open's log) and
  takes what round 484's build takes. A headless start that opens `SideRail.unity` and saves a screenshot, the two
  builds in turns, eight turns: the four undisturbed turns average 2.23 s for round 484's build and 2.23 s for this
  round's (the least: 2.10 s and 2.02 s); the other four were disturbed for both alike (3 to 20 s).
- **Without the table** (a package an older build unpacked) the open itself is the same. In the window the job
  measured SideRail's 13 maps in 2.3 and 2.6 s on its worker, and the frame that took its result and read the 13
  materials again cost 30 and 40 ms - once for a package, ever. (Headless, the job's decode runs on the one thread,
  by design, so a script's next line reads the result: 1.1 to 2.1 s in those runs.)
- **The unpack** measured its 14 maps in 2.3 to 3.4 s on its worker over six unpacks - on the busy machine; the
  maps' decode alone took 1.6 s in round 484. The spec's "under 2 s" is not met as measured (see the deviations).
- In a window no image is decoded on the main thread for a material's values: the unpack's worker and the job's do
  it (`PackageJob::gloss_stats`, `GlossJob::run`).

### 5. The suites and the audits (criterion 8)
- Windows, dependency-free: 23,541 checks, 0 failed. With Blender's libraries: 24,455 checks; the four worktree `gpu`
  picture comparisons fail as in rounds 463 to 484 (`test_gpu.cpp:887` twice, `:2226`, `:4821` - not this round's:
  the same four by the same numbers on main's own build in another pass's tree); every other group passes, the
  assets group at 2,972 checks with the 16-bit tests through libpng.
- Linux (WSL, GCC): 23,547 checks, 0 failed.
- The audits (`test_coverage_map.py --check --widgets`, `hci_consistency_audit.py`, `hci_klm.py`, `unity_meta.py`,
  `make_books.py --check-cited`, `sweep_commands.py`) pass; both tools' self-tests pass.
- Code review: changes needed, all applied - a landing read every Unity material again (now the ones that asked); a
  landing in Play was undone by Stop; a scene's stand-in copy could be written over a Unity `.mat` (older than the
  round); a landing could write the table beside an unpack; the keyword over an empty slot; the docs' "bake again"
  and the tooltip. One more fault found by the round's own tests after that: a mean held more digits in the session
  that measured it than in the table, so a material read again came out "changed" - means are held at the table's
  six decimals everywhere now.

## Deviations from this spec
- **Criterion 2's "the scene's text is unchanged"** was wrong and is corrected above: a scene's text keeps a linked
  material's values as a fallback beside its `.mat`'s path, so it changes with the reading. What holds, and is
  tested: no undo step, the scene not marked changed.
- **Criterion 7's "under 2 s on its worker"** is not met as measured: 2.3 to 3.4 s for SideRail's 14 maps, with two
  other passes building beside the runs (the maps' decode alone was 1.6 s in round 484). Said, not hidden; a quiet
  machine was not to be had in this round.
- **Seams for tests**: `Editor::set_package_job_for_test` (a job that stands in for an unpack still under way - a
  headless import runs to its end at once), `land_unity_gloss_for_test` (a job as a worker left it, cancelled part
  way), `unity_gloss_failed_for_test`.
- **`unity materials all` measures first** in a headless run: the first table of this round listed two unworn
  materials at the old gloss because the verb loaded and listed them in one line.
- **`tools/unity_materials.py`'s "slot without keyword"** is a fault only when Blendity went by the slot; the file is
  as it was, the reading right.
- **Added while writing and in review**: a session's mean counts only while the file's path and bytes are the measured
  ones; a GUID two packages share takes no mean measured on the other's file; a cancelled job's untouched textures
  are not sent again on their own; quitting waits for the job's worker; the job says its time and what its landing
  cost the frame; a Unity `.mat` is never written over by a scene's stand-in copy; the Materials window's tooltip.
- **Not reached by a test**: Cancel in the job tray itself (the landing of a cancelled job is tested through a seam);
  the frame's own cost is a logged number, not an assertion (ADR 0056).
- **Found on the way, not this round's** (follow-ups, nothing changed for them here):
  - `tools/dev/copy_w_to_main.py` takes a committed round back out of the libraries tree once the edit tree has a
    commit that tree's HEAD lacks (it restored eleven of round 484's files to main; a byte compare of the trees
    caught it before a build). This round mirrored the trees by content.
  - The tests' imports cache their images in the user's own `~/.blendity/cache/import` and `gltf_images` whatever
    `BLENDITY_CACHE_DIR` says, and a test touches `~/.blendity/downloads`: four `floor-*` folders and two small PNGs
    from this round's runs lie there beside those of earlier rounds. Left alone. The owner's four folders (the
    project's `Assets`, `SideRail Blendity`, `C:\blsiderail\world`, the package) have no file changed since the
    round began.
  - A scene's material slots are not linked to their `.mat` assets again after Stop (they stay the copies Play set
    aside) until the next undo or open; this round's re-read reaches those copies too, the general case is as it
    was.
