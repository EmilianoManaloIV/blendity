# Task 0394: Every operation the sweep ranked (round 418)

- **Status:** built
- **Requirements:** Q-06 (responsiveness), R-05 (files and interchange)
- **Decisions:** ADR 0105 (the responsiveness budget) applied row by row; no new ADR (no subsystem or format changes)
- **Model:** main session (Fable)
- **PR:** stacked on #315 (round 417)

## Asked
The plan's round 418: "the inventory sweep's list, worked from the top: each operation over its budget or
disproportionately slow for its size gets its fix (an algorithm, a cache keyed on content, parallelism through the job
system, a worker with the tray, or a cheaper default), its stress row before / after, and a test for the behaviour
kept; operations where nothing helps are recorded as honest negatives with the reason."

## The list (`blendity_stress --only operations` on the round-417 build: 969 operations on three scenes, 9 over 50 ms, 4 over 100)
| Operation | Scene | Longest frame | Found (the round's probe test, times printed) |
|---|---|---|---|
| Materials window | starter | 525 ms (probe: 269) | every .mat under Assets read and parsed one by one (871 in the tests' project), then every preview sphere shaded on one thread |
| `device cpu` | starter | 193 ms | the command's status line opened Vulkan (`gpu::status` -> `init_global`) to say which cards there are |
| Modeling Tools window | 10,000 cubes | 146-156 ms, every frame | the Scatter panel's source list: a `std::find` over the names so far per object - quadratic, 112 ms a frame |
| `deletemat Orange` | starter | 122 ms (probe: 234) | `pickable_materials` loaded every .mat to compare names |
| F12 (`render.image`) | worldlike, cubes10k | 92 / 60 ms | the sweep's editors ran without workers, so the whole rasterized render landed in the frame (round 412's synchronous headless path) - a measurement artifact |
| `layout 2 by 3` | worldlike | 56 ms | six views drawn once on the CPU in one frame: 16 + 30 + 32 ms (Scene, Game, UI); the frame after 2 ms |
| Screenshot (`render.screenshot`) | worldlike, cubes10k | 55 / 52 ms (probe: 82) | the PNG encoded and written in the deferred frame |
| Separate 10,000 loose parts (the responsiveness row) | cubes10k joined | 107-118 ms | the operation 37 ms; the frame after 128: 96 ms building 10,000 new render meshes one after another inside the gather |
| Enter Edit Mode on the 500k sphere (the responsiveness row) | sphere500k | 117 ms | round 417's finding: the cage's million overlay commands on the CPU and the benchmark's cold render mesh |

## Built
- **The Materials window:** `fs::find_files` finds the .mat files (no stat or sort of all 23,000 files in Assets; each
  project-relative path built from Assets' own); `preload_material_assets` reads and parses the window's materials on
  every core (Blendity's own .mat text; a foreign one is left to `material_asset`); `material_preview` shades a sphere's
  rows on every core; the previews a frame draws are held to 20 ms (`material_preview_shown`: a tile over the budget
  shows its last preview or a blank one, and the window draws again) - each preview resolves its material's textures,
  and the stress project's 50 textured materials had loaded every image in the first frame.
- **`gpu::status`** reports "not opened yet" until a view, a render or `gpu list` opens the card; it no longer opens
  Vulkan for a log line.
- **The Scatter panel's source list** through a set (`scatter_source_choices`).
- **A material by name** (`material_by_name`, which `deletemat` and the other by-name verbs use) loads only the files
  whose stem is that name - a material asset's name is its file's stem.
- **The screenshot's PNG on a worker** (`ShotJob`, `shot_job_start` / `shot_job_poll`; the tray's "Saving screenshot";
  the headless runner waits for it like the other file jobs; synchronous in an editor without workers).
- **Cold render meshes built on every core before a gather** (`Mesh::render_cache_warm`, `prebuild_render_meshes`,
  called by `collect_items` and `solid_items`): a scene whose meshes aren't built yet - 10,000 parts just separated,
  a scene just opened, an import just landed - builds them in parallel instead of one after another inside the frame.
- **The sweep's editors run their renders on workers** (`set_async_jobs_for_test(true)` in `sweep_one`), as the windowed
  editor does, so an F12 row measures the frame, not the render.
- `tests_round418` (test_editor.cpp): the probe; the Scatter list's names unique and complete on 10,000 objects; a
  material deleted by name with its slots emptied; the screenshot job in the tray and its file; the prebuilt render
  meshes identical to the ones a serial gather builds; the Materials window's tiles drawn.

## The Opus review's fixes
The sweep's scene opened before its workers go on (with workers on, the open had run on a worker and landed inside the measured frames: the worldlike and cubes10k rows had measured the starter scene - round 414's bug, back in the sweep), a preloaded material named by its file as material_asset names it (a copied .mat had kept the old name inside), one prebuild job per mesh with both of its variants (a mesh shared with the Edit Mode object had been queued twice: two threads on one mesh's caches), `deletemat <name>` taking a scene material of that name before an asset as it did before, `libs`, the stress reports and the GPU test opening the card for its status line, the files neither material loader takes remembered so the window doesn't read them every frame, the tests made as strict as their titles (a material in a slot, every prebuilt mesh compared in full, a copied .mat, a mesh shared with the Edit Mode object). Noted and left: a Unity project's .mat files still load one by one on the main
thread when the window first opens (the preload takes Blendity's own .mat text; the foreign loader isn't made
thread-safe here); a game view's prebuild builds LOD0 where `lod_for_view` may then draw another level; the prebuild's
walk runs every gather (on 10,000 cubes the idle frame stays 3.7-3.8 ms); a material key inside a linked folder is now
`Assets/...` rather than the link's absolute target.

## Acceptance criteria
1. The sweep's rows above fall under 50 ms where a fix applies (the Materials window on the starter scene, Modeling
   Tools on 10,000 cubes, `deletemat`, `device cpu`, F12, the screenshot); `layout 2 by 3` stays as it is (six first
   draws in one frame, under the one-frame limit) and is recorded as such.
2. The responsiveness row "Separate the joined cubes by loose parts" lands under the one-frame limit (100 ms).
3. Both suites, the audits; the sweep re-run on the built binary with its table in the spec.

## Measured (this machine; `blendity_stress --only operations`, `--only responsiveness`; the build with Blender's libraries)
`gpu list` 19 -> 213 ms is the same cost moved: Vulkan opens once a process, and the sweep runs every row in one process - `device cpu`'s row had opened it before, so the later `gpu list` found it open. Acceptance criterion 2 is missed by 2 ms: Separate 10,000 loose parts takes 102 ms in its frame (the split 37 ms, the 10,000 new objects' first gather 24 and their UI 15); criterion 1 holds for every row with a fix.

The sweep on the round-417 build against this one, the longest frame around each operation:

| Operation | Scene | Before: longest frame ms | After |
|---|---|---|---|
| Materials | starter | 525.0 | 39.1 |
| Materials | worldlike | 35.6 | 41.1 |
| Materials | cubes10k | 38.2 | 14.4 |
| device cpu | starter | 192.9 | 0.8 |
| Modeling Tools | cubes10k | 156.1 | 7.5 |
| deletemat Orange | starter | 121.5 | 3.7 |
| render.image | worldlike | 91.5 | 4.1 |
| render.image | cubes10k | 60.0 | 18.7 |
| render.screenshot | worldlike | 55.0 | 4.0 |
| render.screenshot | cubes10k | 52.4 | 6.9 |
| layout 2 by 3 | worldlike | 56.1 | 54.2 |
| gpu list | starter | 19.4 | 212.7 |

Over 50 / 100 ms: 9 / 4 before, 2 / 1 after (969 operations). The responsiveness rows: Separate 10,000 loose parts 107-118 -> 102.2 ms; Enter Edit Mode on the 500k sphere 124.3 ms (still over: round 417's finding).

## Not done (honest negatives)
- **`gpu list`** (218 ms once): it opens Vulkan to list the cards - what the user asked for. `device cpu` no longer
  opens it.
- **`layout 2 by 3`** (56-78 ms once): six views' first draw on the CPU in one frame; each later frame reuses them.
  Under the one-frame limit; left.
- **Entering Edit Mode on the 500k sphere with the views on the CPU** stays over the one-frame limit (117 ms): the
  cage's 1.2 million overlay commands (round 417 took them from 80 to 55 ms) and the benchmark's cold render mesh
  (35 ms; warm in use). The card draws the same cage in 6 ms.
- The rows between 30 and 50 ms on 10,000 cubes (a rotate, a scale, a drop to surface: the frame that re-hashes and
  redraws 10,000 objects on the CPU) are within the budget and left.
- The real projects' landing frames the plan listed (SideRail's Generate Lighting, Shinano's F12 start, imports) are
  measured again in round 420's write-up with `tools/bench_world.py`; the prebuilt render meshes apply to them.
