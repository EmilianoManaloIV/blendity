# Open work

The original inventory covers follow-ups named by 2026-11-18 (PR #472 plus Pass 28's write-up).
Later updates link the task or evidence that changed an item's status.
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
- **Comparison removed for unchanged lists in round 614.** On the RTX 4070 SUPER, eligible posing workloads fell from one CPU comparison per card frame to 0.00–0.15. A changing camera still uploads its new list. Controlled replays covered dragging and Rigging playback. Live Link rendered the same frames, but asynchronous mesh traffic varied between trials, so its whole-frame timing remains inconclusive. [Task 0596](../tasks/0596-transparent-corner-reuse.md)
- **Evidence:** round 565's fragment, "Named, not fixed": 7 MB read twice, 0.5 to 1 ms a card frame - as long
  as the copy it replaced. The editor already knows when its sorted list is last frame's (round 471).
- **Files:** `src/editor/gpu_views.cpp`, `src/render/gpu_raster.cpp`.
- **Done:** a key on the frame spares the compare; a test that an unchanged list sends nothing and a changed one
  is sent; the `posing` rows no worse.

### 3. A camera with filters lights its picture twice on the card (M)
- **Closed in round 610.** On the RTX 4070 SUPER and RTX 3060 Ti, filtered cameras produced identical display and HDR values with one opaque lighting draw. The original two-draw path covers incompatible resources and mixed shadow modes. Matched Play trials on both cards reduced median and 95th-percentile frame times. [Task 0592](../tasks/0592-filtered-camera-lighting.md)
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
- **SPZ copy removed in round 615.** On a million-splat input the legacy gzip reader used 61.92 MiB less peak memory during decompression. Peak memory across the complete import stayed near 350 MiB. Round 619 subsequently reduced complete-import median peak from about 350 to 293 MiB at both two and four allowed CPUs. [Task 0601](../tasks/0601-spz-file-peak.md) The PLY file read and serial validation remain open. [Task 0597](../tasks/0597-splat-import-memory.md)
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
- **Later measurements.** Round 648 measured a 100-object Unity package unpack at 608.8/537.2 ms (32/two workers) and first import of a separate 10,000-object scene at 597.8/659.9 ms. Round 650 rejected the single-control Noise Reduction candidate after comparing the floating-point output. Neither result closes the original performance target. [Import measurements](../perf/0630-survey.md) · [Noise Reduction decision](../tasks/0632-single-control-noise.md)
- **Evidence:** `docs/perf/verify-511.md` and the PRs it names: Noise Reduction 18.3 ms, full-size camera
  filters 102 to 332 ms, Unity import 533 ms, two views or a drag at 50,000 objects, two-level card rows while
  dragging.
- **Done:** each needs its own measurement round first; treat the document as the list, not as a plan.

### Fresh-process Textures window still misses its 100 ms budget

Task 0644 tested deferring path construction for unrelated entries in the Windows scan. The mixed-directory paired gains fell below both predeclared thresholds. The trial retained no product change. [Round 658 reconciliation](../perf/0640-input-sweep.md#older-open-work)

Round 623 reduced the Windows first-open median for 2,000 procedural textures from 177.6 to 123.2 ms at default affinity and from 242.6 to 193.4 ms with two workers. The next frame took 3.7–3.8 ms. The 100 ms first-open target remains open. Profile the remaining startup work with the same nested-path, alias, search and selection controls. [Task 0599](../tasks/0599-cold-textures.md) and [task 0605](../tasks/0605-cold-texture-directory.md)

## Testability and flaky checks

### 8. `anim_tick` and playback read the real clock (M)
- **Verified on the Pass 37 stack.** Task 0638 at `ae6c5456` routes preview playback through the editor clock. The focused comparison moved frame 10 to exactly 25 in 0.25 seconds. The Centre of Mass redraw test steps that clock explicitly and checks all 24 moving frames in both overlay states on Windows and Linux.
- **Evidence:** round 568's fragment, "A testability gap": `Editor::anim_tick`
  (`src/editor/animation_window.cpp`) and the places that start playback set and read `anim_.play_clock` from
  `now_seconds()`, not the editor's clock, so `set_clock_for_test` cannot pin a playing clip. The Centre of Mass
  test works around it with a slowed clip and a 5 ms floor per frame. Play mode's tick (`last_play_tick_` in
  `src/editor/editor.cpp`) and Play's physics also step by the wall clock (tests pause and run `play.step`).
- **Done:** playback reads `clock_now()`; a test steps a clip by exact frames; the workaround in the Centre of
  Mass test is removed; playback by hand unchanged.

### 9. Make `sweep_commands`' pick stable (S)
- **Verified:** The explicit inventory retains all 203 runnable verbs and 32 skips on both test scenes. A missing verb fails the local check and names the verb. [PR 477](https://github.com/EmilianoManaloIV/blendity/pull/477)
- **Evidence:** three red Linux jobs (#443, #462, #466). `tools/sweep_commands.py` `examples()` picks, per verb,
  "the shortest invocation with arguments the tests run", so any new test line can change the committed
  `stress/sweep_commands.inc`; CI's check step then exits 1 with no failing test.
- **Files:** `tools/sweep_commands.py`, `stress/sweep_commands.inc`, `tools/dev/chain.sh`.
- **Done:** either the pick no longer depends on what tests happen to run (a fixed table of examples, new verbs
  only added), or `--check` runs in the local chain and the failure names the verb. A self-test for the tool.

### 10. Preferences focus: two cases the re-reveal does not see (S-M)
- **Closed in round 611.** Preferences keeps an edited field visible when rows move or the scroll offset is clamped. Scrolling with the wheel during recovery still moves the field out of view. Windows full, Windows dependency-free and Linux passed the focus, HCI and editor tests. Keeping dynamic status text at a fixed row height is a separate layout suggestion. [Task 0593](../tasks/0593-preferences-focus.md)
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
- **Acceptance verified on the Pass 37 base.** The source-equivalent Windows full-library `gpu` group passed 1,836 checks across 164 fixtures with the RTX 4070 SUPER drawing actual views. The old failures remain useful history. Repeat on other hardware before drawing a cross-device conclusion. [Round 658 verification](../perf/0640-input-sweep.md#older-open-work)
- **Evidence:** Pass 24's notes: `tests/test_gpu.cpp` picture checks (then at lines 888, 2227, 4887) failed on
  `main` with Blender's libraries on the development machine's card, with and without the round under test.
  Round 551 (`docs/tasks/0531-card-cpu-picture-tolerances.md`) dealt with three card-vs-CPU picture tests; check
  whether any remain by running the `gpu` group with the libraries on a card.
- **Done:** the `gpu` group is clean in the libraries build on a card, or each remaining difference has a
  justified tolerance written in the test.

### 19. Programs built by MSVC 14.52 crash in mesh code (M; hosted builds use 14.51 meanwhile)
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
- **Hosted 14.51 setup (task 0591).** `windows-build` and the release's Windows job select the newest usable x64
  14.51 patch in the Visual Studio instance chosen by `vswhere`. On a runner that lacks it, the helper adds the
  versioned component with Visual Studio Installer. Each build activates that exact patch and checks the first compiler
  and linker on PATH along with `VCToolsVersion`. Setup or activation failures stop the job. The run log records the image,
  revision, inventory, installer result and tool paths. Image `20261103.298.1` reused its installed
  14.51 toolset. On `20261116.315.1`, the installer added `14.51.36231` in 651 seconds, repeat setup
  reused it, and both installer modes passed. [Task 0591 evidence](../tasks/0591-hosted-msvc.md#verification)
  MSVC 14.44 previously stopped at `src/editor/lessons.cpp` and `unity_lighting_ui.cpp` with C2026 (string literal too long).
- **Branch-only MSVC 14.44 trial (task 0562).** With the long strings split, the diagnostic build compiled and passed quick stress. Four bit-for-bit Catmull-Clark checks failed in the full Windows suite. [Trial run](https://github.com/EmilianoManaloIV/blendity/actions/runs/44380568201)
- **Round 582 compatibility status (2026-11-19).** The 14.52 candidate compiled and passed quick stress under
  release defaults, with precise floating-point mode, and with link-time code generation disabled. The full
  Windows unit suite failed in all three runs. The cause remains unresolved. [Task 0564 and CI results](../tasks/0564-msvc-1452.md)
- **Unresolved.** The 14.52 full suite failed in each tested configuration. [PR 485](https://github.com/EmilianoManaloIV/blendity/pull/485)
- **Pass 37 compiler trial.** Task 0642 (c4292781) found that the 14.52 unit run still exited 0xc0000409 after 416 passing cases. The bounded stress run passed. Its diagnostic changes were not adopted. [Round 658 reconciliation](../perf/0640-input-sweep.md#older-open-work)
- **Acceptance remains open.** Keep Windows CI and release builds pinned to 14.51 until the original failures
  are explained and addressed and the full Windows unit suite and quick stress both pass on the intended 14.52 release build.

## Product follow-ups

### 14. Corner colour layers are dropped by most mesh operators (M-L)
- **Verified:** Mesh edits carry per-corner paint, including across seams. The modeling checks cover Boolean difference, remesh, Join, Separate and modifier application. [PR 480](https://github.com/EmilianoManaloIV/blendity/pull/480)
- **Evidence:** ADR 0152 and round 545 (`docs/tasks/0525-vertex-paint.md`): point colours follow vertex
  groups through edits; a Corner layer is dropped whole by every operator that rebuilds corners (the Edit Mode
  tools, the modifiers, Join, Separate), and the Inspector says when one was dropped.
- **Done:** operators that rebuild corners carry Corner layers instead of dropping them, operator by operator,
  with tests, and ADR 0152's table updated.

### 15. Sun Elevation / Rotation on the Sky world (S-M)
- **Evidence:** round 542's entry in `CHANGELOG.md` (the sun disc, task 0522): "Not in this round: Sun Elevation /
  Sun Rotation on the Sky world".
- **Done:** the two fields in the Sky world's Inspector, reachable from the console, with a Learn step.

### 16. Leftovers from Pass 25
- **Checked in task 0643.** The full-library README count matched the 31-group run. The native same-tick GUID rewrite and cross-project reuse fixtures passed, as did package reimport with a renamed file and removal of the obsolete GUID. Four loopback peers received seven sends over 120 paint dabs. [Round 658 reconciliation](../perf/0640-input-sweep.md#older-open-work)
- **Evidence:** only the coordinating session's private notes of 2026-11-15 - nothing in the tree records them,
  so reproduce each before writing a spec: a Live Link painted-dab tick assert (`src/editor/livelink*`,
  `docs/LIVELINK.md`); a GUID list filled from a worker that is not pruned when a GUID is reused (the Unity
  import's GUID index); the README check count once four short (the fold now sets it - verify after the 1.0.0
  fold).

### 17. Pass 24's open points (owner's decisions and M-L work)
- **Evidence:** `docs/tasks/0460-*` to `0464-*`, Pass 24's write-up and rounds 484-488 in `CHANGELOG.md`: a re-read replacing a typed material
  value; the direction-dependent slow Scene view; the bake's remaining specks; the bake's CPU half on the card;
  bakes over 2^23 texels depend on batch size. The original `chain.sh` assumed the shared checkout.
- **Reproduced in task 0643.** With a missing gloss mean, automatic background completion rewrote a locally edited Metallic value from 0.37 to 1.0. Task 0650 owns the correction. Explicit Reimport Unity Materials reads values from the Unity file. [Round 658 reconciliation](../perf/0640-input-sweep.md#older-open-work)
- **Verification chain update:** the helper now accepts a selected dependency-free checkout through `BL_W` and
  requires explicit scratch targets through `BL_MAIN` and `BL_WSL_TREE`. Use the receipt's actual command and
  test results when checking the chain. [Migration inventory](CODEX.md) · [Helper](../../tools/dev/chain.sh)
- **Remaining work:** write a spec for each unresolved product item before implementation.

### 18. `build.sh` gaps (S)
- **Updated:** `build.sh` now checks `tests/*.h` and `tests/fixtures/*.h` when deciding whether to rebuild test
  objects. Its dependency checks still use modification times.
- **Caller-provided jobs verified in Pass 37.** Task 0647, integrated at `e0bbb37b`, preserves an explicit positive `JOBS` value and passes it to `xargs -P`. An unset or empty value uses platform detection. Zero and malformed values stop before compilation. Nine shell-entry cases passed. The mtime-based dependency check and the WSL affinity limit still apply. [Build script](../../build.sh) · [WSL limits](MACHINE.md#wsl-limits)

## The owner's decisions and housekeeping
The owner decides the open items below. Standing authorization already covers hub cleanup of verified merged
`task/*` branches through the guarded helper.
- Item 4 above (round 565's fix under the 1.3x bar) and item 1's choice.
- ADR 0146's amendments made during Pass 26 (rounds 520 and 521: vertex deform stays per frame and F12 poses
  at the snapshot on the main thread; the outline is the rest shape's hull skinned, up to 44 % of thickness off
  the old hull where bones blend) were published for the owner's reading.
- Whether to keep `tools/dev/integrate.sh` (the integration PR trial, ADR 0132) or retire it: Passes 26 to 28
  used one top per stack instead.
- Only the hub may exercise the standing cleanup approval. The helper requires verification of each merged
  `task/*` branch. Other refs, including `diag/549-gcc15`, remain owner decisions. [Guarded cleanup approval](../tasks/0570-project-parity.md#owner-decision-on-merged-branch-cleanup)
- The owner authorized task 0594 to retire verified stale worktrees and branches. It removed 31 worktrees,
  59 local branches and 97 remote branches. The task records the retained exceptions and the limits of that
  cleanup. Refer new ambiguous paths or refs to the hub. [Cleanup outcome](../tasks/0594-verified-cleanup.md)
- A larger WSL memory limit (`.wslconfig`) was never asked for; it would lift the one-build rule.
- Releases after 1.0.0: version numbers, tags and what goes in them.
