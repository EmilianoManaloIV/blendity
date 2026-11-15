## 2026-11-14 (round 507): landing frames and long single calls (task 0491, parts A, B, E, C, D)

**Asked:** "Do an HCI and Performance sweep across the program to test, verify, and optimize components." (Pass 25 Speed:
`docs/perf/sweep-504.md`'s list of things the user waits for in one frame or one call. This round: the Unity package export,
the video render's timing, `budget pc`, the bake's landing, and the Unity scene import's cost (part B, below). Edit Mode
entry at 500k on the CPU views (part F, 101.7 ms against 100) is left as the spec says.)

**Fixed:**
- **`export unitypackage` of a package with nested folders threw `bad allocation` or wrote wrong path names** (round 504's sweep
  found it): the folder walk took the parent folder's Unity path by reference into `job->items` and kept pushing children into
  the same vector, so the reference dangled when the vector grew. The path is now copied before the recursion. The new test
  (three folders deep, 40 entries each, 1,639 entries, read back from the package, every path name compared) fails on the old
  walk (restored for one run: the export stops with an error, 0 of 1,639 entries written) and passes on the new.
- **The video-render stress row timed only the start frame**: it waited on `rendering_for_test()`, which falls after each
  frame's picture lands. It now waits on the animation's own `active` flag, prints the frames actually rendered, and splits into
  the start frame, the steady frames and the end frame.

**Changed:**
- **`export unitypackage` does its folder walk on the worker** (`ExportJob::walk_folder`, run first by `ExportJob::run`): the two
  filesystem calls a file (the `.unity-meta` read, the `LightingData.bin` check) are the job's, over copies it owns (`known`,
  `edits`, `abs_folder`, `folder_rel`). On the main thread stay `save_dirty_clip_assets` and `unity_edits` (they read the scene).
  An empty folder is said from `poll_unity_export` (the job ends "... is empty", no package) as it was said by the call. A
  headless editor still runs the whole job in the call and returns whether anything was written.
- **`budget pc` in a window counts on a job** (`Editor::budget_start_count`, `poll_budget_job`, `budget_print`): the objects' facts
  (lights, spring chains, skin joints, material slots, each drawn map's size) are taken on the main thread with the evaluated
  meshes held (`scene/budget.cpp`'s `budget_snapshot`; meshes are copy-on-write); the count (`budget_count_snapshot`: every
  triangle, every texture's header or `.meta`) runs on a worker; the command prints "Budget: counting N object(s) ..." and the
  report follows when it is done, polled once a frame. `budget_count` is the two in a row, so the windowed report and the headless
  one are the same code and the same text. The Profiler's recount every 0.5 s starts the same job and shows the last result (a
  "Counting ..." row before the first); a `budget pc` typed while that count runs is reported when it lands. Headless stays one
  call (`set_budget_wait_for_textures`). An object deleted while it counts changes nothing: the report is of the scene as the
  command found it.
- **A windowed bake's files are moved into the scene's lighting folder on a job of their own** (`move_baked_files`,
  `lighting_move_poll`): the landing used to rename every page and map file on the main thread (about a millisecond a file: 60
  files over older ones took 36 - 58 ms here). The landing frame starts the move, the next frames' poll finishes it (the scene's
  "saved to" folder is set then, and a scene that moved meanwhile has its lighting written where it is); a second bake or Clear Baked
  Data waits for it first (both use the same folder); `save_lighting_data` leaves the lightmaps to it. Headless without workers
  still moves them in the landing.
- `BakeTimes` has `land_move_ms` and `land_save_ms`, and the phases line of the log says them (the landing frame's two file jobs).

- **A Unity scene's import (part B: `import X.unity`, `open X.unity`, drag and drop) is a third to a quarter of what it was, the same
  scene object for object.** The importer fills a parts table (`Editor::unity_import_parts_`, `UnityImport::pass_ms`; the stress
  section `unity_export` prints it at 100, 1,000 and 10,000 objects) and it named two causes. (1) The materials: a scene's renderers
  asked the library for their `.mat` once an object (`material_asset`: path normalised, redirects followed, 190 us a call - 1.9 s of
  the 2.4 s at 10,000 objects); the importer now asks once a GUID (`ImportCache::materials`: the library hands out one shared
  instance a path, so the answer cannot differ within an import). (2) The GUID index was walked afresh on every import (every folder
  of Assets listed and every `.meta` read); `unity_guid_index_checked` keeps it while a fingerprint of Assets (every entry's name,
  and each file's size and write time, the hidden `.unity-guids` / `.unity-stats` / `.unity-meta` files too) is the same, and walks
  again when anything differs - a file added, removed, renamed or edited outside the editor between two imports is found (tested). A
  folder's own time is left out of the fingerprint: NTFS shows it late in its parent's listing, so it differed between two walks of
  an untouched folder (found by the first test). No other pass of `build_unity_objects` was quadratic or did a linear lookup an
  object (the table: all the other passes together are 85 ms at 10,000 objects).
  **Left (part B step 3, not done, by the numbers):** the one call is 111 ms at 1,000 objects (a second import; 160 ms the first, which
  walks Assets) against the 100 ms budget, and 533 ms at 10,000 (578 ms) against 250. What is in it at 10,000 objects: reading and
  parsing the 7.8 MB scene file 190 ms (`unity_yaml.cpp`, outside this round's files), the build 220 ms (130 of them the 200
  materials' first loads and the components, 36 `add_mesh`, 50 the make / place / parent passes), the rest the importer's walks and
  the frame. Moving the parse onto a worker would take a third off; the rest needs the objects made in slices, which needs the builder
  to make them in a scene of its own and move them in (nothing in `Scene` does that today): a round of its own, with its rule for a
  second import / New Scene / Play while slices are pending (finish or drop whole).

**Added (unit checks):**
- `tests/test_assets.cpp`: a Unity scene imported again keeps the GUID index (`unity_import_index_reused_for_test`) and comes out as
  the same scene text as the first import and as one with the index walked afresh; a material file added to Assets between two
  imports is found by the second (the index walked again). On the old code both tests' first checks (the scene text equal, the
  material found) hold too - the old path walked every time - so what they pin is that the faster path changes nothing; the
  reuse check itself has no old-code equivalent.
- `tests/test_assets.cpp`: the nested export (above) and an empty folder said to be empty with no package.
- `tests/test_editor.cpp`: `budget pc` in a window says "counting" at once and then reports the same lines as the one-call
  count; an object deleted while it counts leaves the report unchanged and nothing crashed.
- `tests/test_lighting.cpp`: the lightmaps of a bake that lands on a worker equal those of the inline landing (memcmp of every page),
  a bake started the moment the first landed waits for its files and lands whole, a third bake's files arrive and no `.baking`
  folder is left.
- Stress: `unity_export` has nested folders in its package (Extras/A/B, 40 files each), a row for the export call itself (what blocks
  before the worker starts) beside the whole job, and three `budget pc` rows (the whole count in the call, the windowed call, the
  longest frame while it counts); `sequencer`'s video row is the three rows above.

**Checked (one untimed run each, `--quick`, below-normal priority, this machine; indicative, not a measurement):**
- Export call (100 objects, 19 KB package with nested folders): the call 0.4 - 0.9 ms (the walk and its filesystem calls are the
  worker's now); the whole job inline 55 - 200 ms (the first run was cold).
- `budget pc` over the stress package's 101 objects (5 materials): the whole count in the call 2.0 ms, the windowed call 1.1 ms,
  the longest frame while it counts 0.0 ms. That scene is too small to show the gain; SideRail's 10 s on record is what the job is for
  (not re-measured here: the world isn't in the scratch project).
- Video render, an 8-strip sequence at 640 x 360, 6 frames (the old row's 181 ms longest frame was the start frame alone). Two runs:
  a cold first run: start 2,325 ms, steady frames median 4.3 / longest 111.6 ms, end 128.7 ms (the steady outlier and the end are over
  their budgets 50 and 100); a warm second run: start 348.8 ms (OVER 100), steady median 2.0 / longest 6.2 ms over 45 frames (ok), end
  34.1 ms (ok). So a steady frame is under 50 ms once the first use is paid, and the card composite stays where it is (it is
  `seq_cards.cpp`'s, outside this round's files, and the spec asks for it only if a warm steady frame is over 50 ms). The start
  frame's 349 ms - 2.3 s (the folder, the preview, the snapshot, the first render and composite, first use of everything) is the
  figure left for a round on the render's start.
- The bake's landing, a four-object scene, one page, CPU: the landing frame 5.8 ms inline, 8.4 ms with a worker, of which the file
  moves 5.7 and 8.4 ms (the hashes were moved to the worker in round 422); with the follow-up job the windowed landing frame is
  0.1 ms. The page conversion and upload on the card (`gpu_lightmap`, the next frame) is not changed: spreading pages needs the old
  page set kept whole until the new one is up (a pending `LightingData` every reader of `lighting_data_` would have to know), which is
  a data-model question for the card, not a loop; it needs a card's numbers first.
- Groups on Windows (release, with libraries): `editor` 2,670 checks, `lighting` 9,849, `assets` 2,989 (with part B's two tests), `render` 2,639, no failures (the new
  checks are in `assets`, `editor` and `lighting`). Stress `unity_export`, `sequencer` and `bake` `--quick`: no problem counts. Not run: Linux and the dependency-free tree (the task said so).

### docs/USABILITY.md ("fixed" row)
| Problem | Fixed |
|---|---|
| Export Unity Package failed ("bad allocation") or put assets at the wrong Unity path when the package had folders inside folders with more than a few files each | the folder walk copies each folder's path before descending into it; a package of nested folders exports whole (round 507) |

**Found by the test engineer:**
- Fixed: `budget quest` typed while a `budget pc` count ran changed the target before it was refused, so the running count's report
  said "VRChat Quest" (and the Profiler ranked against Quest). A count under way is now refused before anything changes.
- Fixed: a folder skipped for being deeper than 16 was reported as "left out (couldn't be read)"; each skipped entry now carries its
  own reason.
- Left (not this round's, and not a regression): a Unity `.mat` edited outside the editor between two imports keeps its first colour,
  because the material library keeps a path's first load for the session (`material_asset`), as before this round. The kept GUID index
  still finds the material; re-reading an edited file belongs to the library (`forget_material_assets`), so it is a follow-up.
- Not tested: the windowed export runs on a detached thread with no test hook, so a folder removed while an export runs is untested.

**Fixed in review:**
- Fixed: opening, reverting or New-Scene-ing the scene the moment a bake landed read a half-moved lighting folder and set the saved folder with nothing loaded; loading the lighting now waits for the files' move job first.
- Fixed: a Budget count that landed after New Scene / Open reported and cached the old scene as "the scene"; the count is tied to its scene and dropped ("Budget: the scene changed while counting - type budget pc again"), and the Profiler recounts.
- Changed: the kept GUID index's comment no longer claims every edit is seen (one that keeps size and write time is not); on Linux / macOS the change time joins the fingerprint.
- Fixed: `export unitypackage` of an empty folder is refused at once again ("<folder> is empty"), before the job starts.
- Fixed: the export job releases its folder-walk copies of the GUIDs and edits once the walk has run, not at the next export.
