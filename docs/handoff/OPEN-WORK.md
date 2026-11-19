# Open work

Every follow-up that was named and not taken, as of 2026-11-18 (the tree of PR #472 plus Pass 28's write-up).
Each item gives what is wrong or missing, the evidence, the files, and what "done" would look like, so it can
become a task spec (`docs/tasks/TEMPLATE.md`) with little more reading. Line numbers are those of this tree and
will drift; the function names will not. Item 19 could have held up 1.0.0. Windows CI and release builds use MSVC 14.51.

Sizes: **S** an hour or two, **M** a round, **L** a pass or a decision first.

## Performance

### 1. The graphics card's power state holds posing frames in a world over 16.7 ms (L, owner's decision first)
- **Evidence:** round 565 (`docs/tasks/0547-card-world-draw-cost.md`; `changelog.d/565-card-world-draw-cost.md`
  until the fold, then `docs/PERFORMANCE.md` "Round 565"). With
  skinning on the card (Pass 26), posing alone meets 16.7 ms everywhere; in the SideRail world five rows do not:
  a Puppet handle 19.0 ms, Play 24.6, a scrub with two views 22.6, with one 18.3, the clip 17.3. The world's
  frame is 1 to 2.3 ms of card work at the card's full clock and 10 to 16 ms in its lowest power state (P8,
  210 MHz on an RTX 4070 SUPER at about 30 % load): every part is 4 to 6 times slower there.
- **What the app could do** (the round named these and changed none; the editor must never change a driver or
  power setting):
  1. document the driver's per-program setting for the user (NVIDIA "Power management mode: Prefer maximum
     performance" for `Blendity.exe`) - a manual / Preferences hint, no code risk;
  2. give the card steady work while a drag or playback runs, so the driver keeps the clock up - costs power for
     nothing drawn; needs a measurement of how much work holds the clock, and a Preferences switch;
  3. reduce what a frame asks of the card in P8 (items 2 and 3 below).
- **Files:** `src/render/gpu_raster.cpp`, `src/editor/gpu_views.cpp`, the `posing` stress section;
  `gpu::RasterDetail` (`Raster::detail()`) prints a card frame's parts.
- **Done:** the five rows met, or the owner's choice among 1 to 3 recorded in an ADR. Measure only with
  `nvidia-smi` sampled beside the run (`PITFALLS.md`, "The card's power state").

### 2. The see-through corners are compared on the CPU every frame (S-M)
- **Evidence:** round 565's fragment, "Named, not fixed": 7 MB read twice, 0.5 to 1 ms a card frame - as long
  as the copy it replaced. The editor already knows when its sorted list is last frame's (round 471).
- **Files:** `src/editor/gpu_views.cpp`, `src/render/gpu_raster.cpp`.
- **Done:** a key on the frame spares the compare; a test that an unchanged list sends nothing and a changed one
  is sent; the `posing` rows no worse.

### 3. A camera with filters lights its picture twice on the card (M)
- **Evidence:** round 565: the linear-light pass costs 2 to 3.6 ms in P8 in the Play row; one pass with two
  targets would halve it (the see-through and overlay draws share that pass).
- **Files:** `src/render/gpu_raster.cpp` and the card's shaders.
- **Done:** one lighting pass for a filtered camera, pictures unchanged within the existing card-vs-CPU tests.

### 4. Round 565's kept fix is 1.2x, under ADR 0112's 1.3x bar (owner's decision)
- **Evidence:** `docs/tasks/0547-card-world-draw-cost.md`: the corners kept on the card take the card's frame
  13.11 -> 10.70 ms in P8 (1.2x); the timed rows moved 0.3 to 2 ms, inside the run-to-run spread. ADR 0112 adopts
  a change only at 1.3x or more on a suite row.
- **Done:** the owner either accepts it as a correctness-neutral simplification of what goes to the card each
  frame (and the ADR notes the exception) or it is reverted.

### 5. Splat import: the whole-file read, SPZ's inflate, serial validation (M)
- **Evidence:** round 567's fragment: a 1M-splat PLY imports in 273 ms of which `fs::read_file` is 184; an SPZ
  in 365 ms of which inflate and the copy `gunzip` makes of its output are 253; `pointcloud_validate` is serial,
  about 30 ms. The import already runs off the main thread and meets ADR 0105's frame budget.
- **Files:** `src/core` (`fs::read_file`), `src/scene/import_splat*`, the gunzip helper, `pointcloud_validate`;
  the `splats` stress section has the rows.
- **Done:** the stress rows show the gain; frames while importing stay within ADR 0105.

### 6. Splats on the CPU: named leftovers (S each, optional)
- **Evidence:** round 566's fragment, "Not done (named)". The 1M orbit meets its 50 ms budget (a median of 38.6 / 37.9 ms).
  Left: the binning's count table (tiles x chunks, made every frame, summed on one thread) grows with picture
  size and thread count - a few ms at 8K on 64 threads; under a curve view transform (Filmic, ACES) a splat's
  display colour may differ by one step from `gsplat_project_one`'s; SH kept between frames or lower-order SH at
  distance were not needed and change the picture.
- **Files:** `src/render/gsplat_raster*`, `tests/test_splats_raster.cpp`.

### 7. Older named rows (L, from Pass 25's verification)
- **Evidence:** `docs/perf/verify-511.md` and the PRs it names: Noise Reduction 18.3 ms, full-size camera
  filters 102 to 332 ms, Unity import 533 ms, two views or a drag at 50,000 objects, two-level card rows while
  dragging.
- **Done:** each needs its own measurement round first; treat the document as the list, not as a plan.

## Testability and flaky checks

### 8. `anim_tick` and playback read the real clock (M)
- **Evidence:** round 568's fragment, "A testability gap": `Editor::anim_tick`
  (`src/editor/animation_window.cpp`) and the places that start playback set and read `anim_.play_clock` from
  `now_seconds()`, not the editor's clock, so `set_clock_for_test` cannot pin a playing clip. The Centre of Mass
  test works around it with a slowed clip and a 5 ms floor per frame. Play mode's tick (`last_play_tick_` in
  `src/editor/editor.cpp`) and Play's physics also step by the wall clock (tests pause and run `play.step`).
- **Done:** playback reads `clock_now()`; a test steps a clip by exact frames; the workaround in the Centre of
  Mass test is removed; playback by hand unchanged.

### 9. Make `sweep_commands`' pick stable (S)
- **Evidence:** three red Linux jobs (#443, #462, #466). `tools/sweep_commands.py` `examples()` picks, per verb,
  "the shortest invocation with arguments the tests run", so any new test line can change the committed
  `stress/sweep_commands.inc`; CI's check step then exits 1 with no failing test.
- **Files:** `tools/sweep_commands.py`, `stress/sweep_commands.inc`, `tools/dev/chain.sh`.
- **Done:** either the pick no longer depends on what tests happen to run (a fixed table of examples, new verbs
  only added), or `--check` runs in the local chain and the failure names the verb. A self-test for the tool.

### 10. Preferences focus: two cases the re-reveal does not see (S-M)
- **Evidence:** round 573 (`docs/tasks/0555-preferences-budget-rows.md`, "Out, from the review"): a text or
  number field that Tab is *editing* goes through `Context::walk_stop` only (`src/editor/ui.cpp`) and is not
  watched, so rows changing above it can still push it out of view; rows that go away above the control on the
  very frame its scroll area's offset is clamped are not seen as a push-out. Also out of that round: other
  Preferences texts that change with the machine's state (the "Now: ..." status under Viewport Device, search
  rows filling in) still change their row count.
- **Files:** `src/editor/ui.cpp`, `src/editor/ui.h`, `src/editor/panels.cpp`, `tests/test_hci.cpp`.
- **Done:** a test for each case with real Tab / text events on a pinned clock; the Tab-walk tests need no
  `fps unlimited` workaround.

### 11. F9 named by hand (check, then S)
- **Evidence:** round 570 moved the five log lines that named "F9" by hand to the keymap's own chord
  (`docs/tasks/0552-f9-messages-from-keymap.md`). A search of `src/editor` on this tree finds one user-visible
  string left: the tooltip "Adjust Last Operation (Blender: F9). ..." in `src/editor/scene_view.cpp`, which names
  Blender's key as a reference, not the bound one - probably intended. `python tools/hci_label_audit.py` is the
  judge; it also has a known false positive at a comment in `src/editor/blockout_editor.cpp`.
- **Done:** the audit is clean in both keymap presets, or the tooltip's wording is confirmed and the false
  positive is excluded in the audit.

### 12. macOS CI: time limits and lost runners (S)
- **Evidence:** Pass 27's notes: macOS jobs timed out on the larger trees (#427, #428) and runners were lost
  several times (#445, #449). In `.github/workflows/build.yml` only the "Quick stress test" step has a limit
  (`timeout-minutes: 20`, and in `macos-stress` a 15-minute alarm, `perl -e 'alarm shift; ...' 900`);
  `macos-unit` and `macos-build` set none, so their failures were lost or queued runners or the job default.
  The sanitizers were sharded in rounds 492-493 (`tools/dev/test_shards.py`), macOS was not.
- **Done:** which macOS job times out is read from the runs first; then the stress alarm raised or the macOS
  unit checks sharded like the sanitizers, with the minutes measured.

### 13. Four card picture checks fail in the libraries build only (M)
- **Evidence:** Pass 24's notes: `tests/test_gpu.cpp` picture checks (then at lines 888, 2227, 4887) failed on
  `main` with Blender's libraries on the development machine's card, with and without the round under test.
  Round 551 (`docs/tasks/0531-card-cpu-picture-tolerances.md`) dealt with three card-vs-CPU picture tests; check
  whether any remain by running the `gpu` group with the libraries on a card.
- **Done:** the `gpu` group is clean in the libraries build on a card, or each remaining difference has a
  justified tolerance written in the test.

### 19. Programs built by MSVC 14.52 crash in mesh code (M; CI builds with 14.51 or not at all meanwhile)
- **What happened:** GitHub Actions run 44354841677 (PR #472's tree `36dd42af`, the dependency-free Windows build) crashed
  in two programs built by one `windows-build`: `blendity_tests.exe`, run as CI runs it - the whole suite in one process -
  with a segmentation fault as test 645 ran ("smart materials (ADR 0082): Worn Steel on a subdivided cube's baked maps
  ...", 36 ms after test 644 "a Paint layer (ADR 0081)" finished), and `blendity_stress.exe --quick` with exception
  0xc0000005 in the section `modifiers_ngon`, right after its table of 26 meshes (its crash report: 12 frames on the main
  thread, every one printed as `?` - no symbols).
- **What differs is the compiler (round 579):** that build was the first on runner image `windows-2025-vs2026`
  20261116.315.1, whose default toolset is MSVC 14.52.37025. Every green Windows build before it (#468 to #471) was on
  image 20261103.298.1 with MSVC 14.51.36231 - the development machine's compiler too, so no local run could show it.
- **Not reproduced with 14.51:** the same commit built here: that test alone 50 of 50 under CPU load, the suite's tests
  1-650 in one process on four cores 27 of 27 (25 under load), and the other sessions' runs (about 40 more).
- **Ruled out, by reading the changes between the passing and the crashing trees:** no `Editor` exists yet at that test
  (rounds 563 / 564's session); that binary holds no graphics-card code (round 565's); round 572's depth writes are not
  on either crash site's path, and nothing reached before test 645 differs between #468 and #472 (round 562 / 572's);
  every branch Pass 26 added runs only for a skinned mesh. The built-in job pool was suspected before the compiler was
  noticed; the stress crash is on the main thread in a section that runs no bake.
- **What is in place (task 0561):** `build.bat` prints its compiler in every build and takes `BLENDITY_VCVARS_VER`;
  CI's `windows-build` and the release's Windows job ask for `14.51` with `BLENDITY_VCVARS_STRICT=1`: on a runner whose
  image has no 14.51 (the new image) the build stops with a line saying so, and the job is run again until it lands on
  one that has it. So CI and the installer are never built by 14.52 - at the price of red Windows jobs to re-run, more
  of them as the new image rolls out, and none passing once the old image is gone. The image's other toolset, 14.44,
  is no way out: it stops at `src/editor/lessons.cpp` and `unity_lighting_ui.cpp` (C2026, a string literal too long
  for that compiler). The fault itself is still there for anyone who builds with 14.52.
- **Round 582 compatibility status (2026-11-19).** The 14.52 candidate compiled and passed quick stress under
  release defaults, with precise floating-point mode, and with link-time code generation disabled. The full
  Windows unit suite failed in all three runs. The cause remains unresolved. [Task 0564 and CI results](../tasks/0564-msvc-1452.md)
- **Acceptance remains open.** Keep Windows CI and release builds pinned to 14.51 until the original failures
  are explained and addressed and the full Windows unit suite and quick stress both pass on the intended 14.52 release build.

## Product follow-ups

### 14. Corner colour layers are dropped by most mesh operators (M-L)
- **Evidence:** ADR 0152 and round 545 (`docs/tasks/0525-vertex-paint.md`): point colours follow vertex
  groups through edits; a Corner layer is dropped whole by every operator that rebuilds corners (the Edit Mode
  tools, the modifiers, Join, Separate), and the Inspector says when one was dropped.
- **Done:** operators that rebuild corners carry Corner layers instead of dropping them, operator by operator,
  with tests, and ADR 0152's table updated.

### 15. Sun Elevation / Rotation on the Sky world (S-M)
- **Evidence:** round 542's entry in `CHANGELOG.md` (the sun disc, task 0522): "Not in this round: Sun Elevation /
  Sun Rotation on the Sky world".
- **Done:** the two fields in the Sky world's Inspector, reachable from the console, with a Learn step.

### 16. Leftovers from Pass 25 (S each; unverified)
- **Evidence:** only the coordinating session's private notes of 2026-11-15 - nothing in the tree records them,
  so reproduce each before writing a spec: a Live Link painted-dab tick assert (`src/editor/livelink*`,
  `docs/LIVELINK.md`); a GUID list filled from a worker that is not pruned when a GUID is reused (the Unity
  import's GUID index); the README check count once four short (the fold now sets it - verify after the 1.0.0
  fold).

### 17. Pass 24's open points (owner's decisions and M-L work)
- **Evidence:** `docs/tasks/0460-*` to `0464-*`, Pass 24's write-up and rounds 484-488 in `CHANGELOG.md`: a re-read replacing a typed material
  value; the direction-dependent slow Scene view; the bake's remaining specks; the bake's CPU half on the card;
  bakes over 2^23 texels depend on batch size; `tools/dev/chain.sh` copies into the shared checkout and is not
  usable from a worktree (rounds since ran its steps by hand).
- **Done:** each needs a spec; the `chain.sh` item is tooling and the most useful to a new agent - a chain that
  runs in whatever tree it is called from.

### 18. `build.sh` gaps (S)
- **Evidence:** `PITFALLS.md`: `build.sh` checks only `src/*/*.h` and `src/*/*/*.h` for newer headers (editing a
  `tests/*.h` alone rebuilds nothing), compares mtimes only, and compiles with `nproc` jobs (no `JOBS`
  override; on a memory-limited WSL that means `taskset`).
- **Done:** test headers in the dependency check; a `JOBS` setting; both documented in `tools/README.md` via a
  fragment.

## The owner's decisions and housekeeping
These wait on the owner, not on a task. Do not act on them unasked.
- Item 4 above (round 565's fix under the 1.3x bar) and item 1's choice.
- ADR 0146's amendments made during Pass 26 (rounds 520 and 521: vertex deform stays per frame and F12 poses
  at the snapshot on the main thread; the outline is the rest shape's hull skinned, up to 44 % of thickness off
  the old hull where bones blend) were published for the owner's reading.
- Whether to keep `tools/dev/integrate.sh` (the integration PR trial, ADR 0132) or retire it: Passes 26 to 28
  used one top per stack instead.
- The backlog of merged `task/*` branches on GitHub and the `diag/549-gcc15` branch: `AGENTS.md` asks for
  merged branches to be deleted after each merge, but the agents' deletes were refused by the permission
  system, so the owner deletes them or allows it.
- Removing finished worktrees and scratch folders on the development machine (`MACHINE.md` lists the families;
  one folder with a mangled name, `C:\blcmp565$tag`, was left because its delete was blocked).
- A larger WSL memory limit (`.wslconfig`) was never asked for; it would lift the one-build rule.
- Releases after 1.0.0: version numbers, tags and what goes in them.
