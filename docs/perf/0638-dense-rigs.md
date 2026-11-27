# Dense-world rig interaction and partially trusted skin weights

## Interactive rig fixtures

The benchmark imports a closed mesh, places markers, builds and binds the rig, and frames it in the Scene and Animation layout. The isolated and dense cases use the same mesh, markers, clip and camera. The dense cases add the worldlikequick fixture with 600 parts, 70 lights, four probes and 64 textures. All four run in the Shaded CPU viewport at 1600 × 1000.

| Fixture | Isolated objects | Dense objects | Dense vertices / faces | Rig bones |
| --- | ---: | ---: | ---: | ---: |
| Humanoid | 29 | 705 | 169,662 / 169,960 | 17 |
| Quadruped | 32 | 708 | 175,294 / 175,592 | 20 |

Each case drags the selected hand or forepaw gizmo along X, saves its pose, then undoes the drag. The selected target moved about 0.42 m and the driven lower limb rotated. Undo restored both transforms. A ruler drag from frame 0 to 60 produced the same exported vertex coordinates as direct evaluation at frame 60. The isolated and dense exports also matched across each rig pair. Playback advanced and deformed the body on both rigs. Its position depends on elapsed time, so the fixed-frame comparison uses the scrub.

To reproduce the isolated/dense comparisons, use an editor package and a fresh output directory.

```sh
python tools/bench_dense_rigs.py --exe PATH_TO_EDITOR --out NEW_DIRECTORY --frames 30 --repeat 3
```

The table reports medians of three repetitions per rig and condition on Windows 11 (build 26200), AMD Ryzen 9 5900XT, 32 logical CPUs, default worker setting. Each gesture requested 30 input frames. The drag timer reports the first move separately from the mean of the remaining 29. Playback elapsed is divided by all 30 frames. The order within each rig pair alternated across repetitions.

| Rig and scene | IK drag ms/frame | Ruler scrub ms/frame | Playback ms/frame |
| --- | ---: | ---: | ---: |
| Humanoid, isolated | 3.87 | 4.14 | 4.21 |
| Humanoid, dense | 28.66 | 33.65 | 35.30 |
| Quadruped, isolated | 3.78 | 4.31 | 4.28 |
| Quadruped, dense | 28.27 | 33.33 | 35.22 |

All six comparisons matched the target transform, driven-bone rotation and scrubbed mesh exactly. Across the active dense rows, the Scene view took 26.38–32.72 ms per frame, skinning 0.09–0.12 ms and constraints 0.13–0.17 ms. The view dominates this CPU workload. The headless runs exclude window presentation and GPU timings.

## Preview clock

The Animation window previously read wall time directly when starting playback from the toolbar or console, when resuming after a scrub, and when calculating the current preview frame. Using the editor clock at all four points allows a headless input sequence to exercise the same playback logic at exact timestamps. The normal editor clock reads the platform timer.

A Linux differential linked the same test object against the original and updated clock implementations. The original failed 12 of 21 assertions. After a simulated 0.25 seconds from frame 10 at 60 fps, the original playhead read 10.7654 and the bone's X was 1.0765 m. The updated playhead reached 25 and its bone reached 2.5 m exactly. The test also held time stationary, scrubbed and resumed via the ruler, stopped, restarted from the console and crossed the clip boundary. Both focused tests passed all 40 assertions with the updated window object.

## Partial-weight transfer recipe

Generate the source fixtures with `python tools/bench_animation_rigs.py --exe PATH_TO_EDITOR --out NEW_DIRECTORY --case humanoid --case creature`. Keep each project's Assets and Results/pretransfer.scene. Body and Transfer use separate mesh assets with corresponding vertices.

On a copy of the scene, add an InpaintMask vertex group to Transfer. Sort the target mesh's local X coordinates. Set the mask to one for vertices between indices floor(0.30 × n) and floor(0.70 × n), including all coordinates tied at either endpoint. Set all other mask values to zero. Both fixtures yield endpoints of ±0.20000000298 m. A native scene writer can append the vgrp section to the target mesh before its hsmb footer and update the section count and chunk length. The original scene supplies the fully trusted control.

Select Transfer, issue `perf reset`, `skin transfer Body Transfer`, `perf wait`, then `perf mark skin_transfer` and save the result. The mask leaves 5,426 known and 4,816 unknown vertices on the humanoid and 7,554 known and 8,320 unknown vertices on the quadruped.

The initial profile attributed 11.5–12.4 ms on the humanoid and 19.5 ms on the quadruped to Laplacian construction. Eigen factorization took 10.7–12.0 ms and 20.1–21.2 ms respectively. The subsequent triangular solves took 5.1–5.7 ms and 11.0–12.6 ms. Each range comes from one instrumented transfer per worker profile.

The candidate constructs independent stiffness rows in parallel, inserts each diagonal in sorted order, and solves independent right-hand-side columns against the same factorization. Each worker writes to its own columns in the existing output buffer. The original and candidate executables were timed without instrumentation. Four fresh paired transfers per condition alternated which executable ran first. The constrained condition restricted the process to two logical CPUs and set two workers.

| Rig and CPU setting | Transfer median, original / candidate (ms) | Solver median, original / candidate (ms) |
| --- | ---: | ---: |
| Humanoid, default | 37.30 / 29.20 | 32.0 / 24.5 |
| Humanoid, two logical CPUs | 35.95 / 36.25 | 31.5 / 31.5 |
| Quadruped, default | 65.25 / 49.30 | 57.5 / 41.5 |
| Quadruped, two logical CPUs | 65.15 / 63.30 | 57.5 / 56.5 |

All 16 pairs produced bit-identical stored weights across 3,932,752 comparisons. The independent audit also compared 3,893,152 trusted values to unmasked controls, verified the stored mask and checked finite, normalized weights and zero fallbacks across 32 partial transfers. The candidate was faster in all eight default-worker pairs, one of four constrained humanoid pairs and three of four constrained quadruped pairs.

Default-worker transfers took 21.7% less time for the humanoid and 24.4% less for the quadruped. The constrained humanoid median increased by 0.30 ms. The constrained quadruped median fell by 1.85 ms. These results support a default-worker gain on these fixtures. The Eigen solve and its speedup were measured on Windows. Linux exercises the conjugate-gradient fallback functionally.

## Live Link

The separate `blendity_stress --quick --only livelink` run isolates the cost of comparing the scene on the editor thread. With four loopback clients, it deformed a 99,602-vertex mesh, posed a 50-bone rig and edited a clip over 40 frames. The comparison averaged 0.91 ms and peaked at 1.77 ms. The producer recorded 40 deforms, 39 poses and four clips. In a separate 1,000-moving-cube row, one peer raised mean frame time from 13.37 to 15.48 ms. These controls establish processing costs under their specified loads. The dense peer workload below checks the bytes received over the socket.

For each dense rig, the peer followed the editor's private PID and ephemeral port, read the initial synchronization and decoded the binary clip. It received a 121-frame clip at 60 fps with 2.3999996 m of authored root travel. The reopened humanoid hand target moved 0.4104 m and its lower-arm quaternion matched a received pose. The quadruped forepaw moved 0.4975 m and its lower-leg quaternion also matched exactly after conversion to glTF axes. The baseline package and final candidate each produced 105 full pose samples per rig, including 104 distinct samples, in these functional runs. The transferred pose verifies the identified edit. The attachment independently verifies the clip.

The receiver is passive and receives default deform traffic. It saves the native command log, original protocol headers, clip binaries, discovery metadata and a native post-drag scene. Earlier exploratory input used a named peer. The Windows editor closed that stream during explicit stop and the probe failed before recording complete evidence. The retained runs use a passive peer and confirm completed synchronization, clip contents, edited bone, one connected client and normal editor exit. They measure full CPU editor frames and protocol payload volume. Per-message latency, external engine processing and remote networking remain unmeasured.

Each of the six timed active peers received 105 full pose samples, including an exact match to the saved edited limb.

Reproduce with a dense project from the first benchmark. Use 20 bones for the quadruped and 17 for the humanoid. Run each condition in a separate new directory.

```sh
python tools/bench_rig_livelink.py --exe PATH_TO_EDITOR --project DENSE_PROJECT --out NEW_PEER_DIRECTORY --bones 17 --dense --peer
python tools/bench_rig_livelink.py --exe PATH_TO_EDITOR --project DENSE_PROJECT --out NEW_CONTROL_DIRECTORY --bones 17 --dense
```

Three interleaved off/on pairs per rig used the same saved scene and editor executable. Times are median milliseconds per frame. Every measured 90-frame playback run applied the clip. The peer compared each received drag to the corresponding native saved edit. The no-peer process never started the listener.

| Reopened dense rig | Drag, off / on (ms) | Playback, off / on (ms) |
| --- | ---: | ---: |
| Humanoid | 51.02 / 54.62 | 58.58 / 61.40 |
| Quadruped | 55.09 / 55.82 | 61.09 / 61.84 |

Adding a passive reader raised the median playback time by 2.83 ms per frame on the humanoid and 0.75 ms on the quadruped. Compare conditions within this replay. The first interaction matrix builds and plays its fixtures in the same process. The Live Link matrix reopens a saved world and then warms up, drags, holds and plays.

## Verification and evidence

The Windows full baseline editor was `aa8b79aabd734f4782e8860e17aec89a5ce3102cabd45f5ca41dc471664df989` (SHA-256), and the candidate was `c04ebef3c1f6a0a081d83d0ca72ece1387544c8ee5c1b200b0ae2040a7ffdfe2`. The donor package had the same product sources as base `d7503d4e`; all 67 recorded runtime files matched. The candidate reused the donor objects outside its changed translation units. Windows used the full Eigen build and MSVC 14.51. The WSL dependency-free build exercised the conjugate-gradient route on an analytical partial mask with two trusted vertices, one solved vertex and one disconnected fallback. The dense interaction and peer timing matrices used the baseline package. A separate candidate run verified both dense rigs, saved gesture states and wire delivery.

The first dense functional check counted the first mouse move twice. Re-reading the native records with the corrected check verified all four inputs. The 30-frame timing matrix used the corrected count from the start. A checker regression initially expected an older diagnostic string; the corrected check still requires rejection of a playback batch with zero clip applications.

The original Centre of Mass redraw test froze the editor clock, slept in real time, and expected the preview playhead to advance. After the clock correction, its two visibility variants stayed at frame zero. The revised test advances the controlled clock by 10 ms per step and requires all 24 moving frames to draw for each variant.

| Scoped final verification | Windows full | WSL Linux, dependency-free |
| --- | ---: | ---: |
| Two focused clock and transfer tests | 40 / 40 | 40 / 40 |
| Animation group, 376 named tests | 5,173 / 5,173 | 5,173 / 5,173 |
| Editor group, 346 named tests | 4,047 / 4,047 | 4,021 / 4,021 |
| Dense benchmark checker regressions | 2 / 2 | 2 / 2 |

Both final editor-group runs checked that all 24 moving frames drew with the Centre of Mass overlay hidden and shown. The WSL focused and animation groups used the identical product and animation-test objects before the editor-test-only relink. Windows ran the complete scoped set from the relinked executable. These functional jobs shared their window with other scheduled verification processes. Only the separately recorded exclusive windows support timing claims.

The Hub's local independent audits and queue receipts are under `C:/blab/pass37-hub`. Fixture generation, raw native scenes, exported geometry, headers and clip attachments are under `D:/Blendity-agent-scratch/pass37/0638`.

| Evidence | Files or directories |
| --- | --- |
| Dense pairs and joined candidate | `dense-pairs1/report.json`, `dense-final1/report.json`, Hub `0638-dense-pairs-audit.json` and `0638-dense-final-audit.json` |
| Skin phases and paired transfers | `skin-fixtures/recipe.json`, `skin-profile1/runs.json`, `skin-pairs-final1/runs.json`, Hub `0638-skin-final-audit.json` |
| Live Link processing and delivery | `link/tick1/report/latest.md`, `link/pairs2`, `link/final1-*`, Hub `0638-link-pairs-audit.json` and `0638-link-smoke-final-audit.json` |
| Test packages and group output | `build-final/full-candidate`, `build-testeditor`, `wsl-verify`, `independent-groups` |
| Accepted timing windows | Hub queue results for `0638-skin-profile1`, `0638-dense-pairs1`, `0638-linktick1`, `0638-linkpairs2` and `0638-skin-pairs-final1`; each child exited 0 with zero recorded taint |
