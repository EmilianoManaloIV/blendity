# 0613: Reclaim oversized CPU viewport shadows when selecting Low quality

- **Status:** in review
- **Authorization:** Owner optimization assignment through the Pass 33 Hub, round 631. Implementation depends on the baseline reproduction below.
- **Requirements:** Q-04, Q-06 (interaction and responsiveness).
- **Decisions:** ADR 0026 (minimum machine and quality tiers), ADR 0105 (responsiveness budget), ADR 0123 (documentation fragments).
- **Implementation:** Project implementer, then independent test engineer and code reviewer.
- **PR:** Hub owns integration and publication.

## Goal

Make more memory available for editing after a software-rendered shaded view switches from High to Low. Preserve the picture and the existing resolution policy when users switch back.

## Scope

Measure the real tier transition before changing cache behavior. Include completed CPU viewport shadow maps, static layers and the depth storage exchanged with the rasterizer. GPU, splats, thumbnails and changes to final-render quality are outside scope.

## Likely files

- src/editor/render_view.cpp (set_quality_tier and update_shadow_map), src/editor/editor.h and src/editor/editor.cpp (shadow memory accounting as needed).
- src/render/raster.cpp and raster.h (render_shadow_map and thread-local depth storage if implicated).
- tests/test_editor.cpp or existing shadow/quality test's subsystem file, the existing editor stress workload, this spec and changelog.d/631-low-tier-shadow-cache.md.

## Acceptance criteria

1. Reproduce on the tracked baseline with the CPU renderer, real production tier change after a completed High frame, and actual retained depth bytes attributed by resolution and holder. Confirm via a real window or interactive-equivalent normal entry point. Compare against Low before first frame. Record any harness startup effects. Stop with findings if no obsolete plane is retained.
2. At matched settled Low checkpoints, reclaim at least 16 MiB across the completed maps, static layers and thread-local depth storage on a fixture with one obsolete 2048-square plane. Report live capacities, current process residency and process lifetime peak separately. Claim process memory benefit only if reproduced beyond baseline variation.
3. Preserve same-tier picture and shadow depths, ordinary cache reuse and correct results with static/moving casters, multiple views, repeated tier switches, unrestricted High and final render. A Low-only launch must avoid extra rebuilds.
4. Check switch-frame and editing tails including returning to High. Characterize changes against baseline variation with paired quiet runs and disclose rebuild costs. Accept only with verified image fidelity and no material sustained interaction regression.

## Tests and checks to run

Use generated scratch fixtures and isolated preferences/cache/project/trash. Use the production Preferences setter or equivalent console command and record actual backend. Collect deterministic bytes before/after each transition, the first Low redraw, settled Low, caster movement and High restoration. For timings use the Hub's exclusive window, same-path matched builds, a baseline/baseline control and at least three alternating baseline/candidate pairs. Record hashes and actual processor affinity for constrained results. Run focused tests plus affected groups and editor on Windows and Linux, relevant editor stress and fragment checks. Independent testing and review precede handoff.

## Risks and edge cases

Cache eviction can move capacity into renderer scratch. Static layers may share resolution across tiers. Preserve live borrowers, render workers and in-flight F12 jobs; measure peak during reallocation and the first High redraw after eviction. Headless initialization draws before command dispatch and omits preferences. Separate a constrained desktop result from physical low-memory hardware.

## Documentation to update

Record measurements, limits and receipt in this spec. Round 631 supplies the fragment under ADR 0123, including user-facing and performance sections when warranted.

## Outcome

### Baseline reproduction

The tracked baseline is integration/4 (9246bcb1c0f5f547c12439a8a11a6849bfabe361). The Windows observer used
`Editor::init` and `Editor::windowed_check` in a real 1280 × 720 window with the CPU viewport and system presentation.
Fresh scratch preferences set the launch tier before any frame. The generated point-lit starter produced no sun
shadow map. Adding a directional light and cube produced one completed shadow plane in the first settled shaded view.
The stock headless screenshot harness draws a frame before its first command and omits preferences, so it was
unsuitable for the Low launch control.

| Checkpoint | Completed maps | Static layers | Main-thread raster scratch | Live depth total |
|---|---:|---:|---:|---:|
| High, shaded | 16 MiB (2048²) | 0 | 0 | 16 MiB |
| Low selected, before redraw | 16 MiB (2048²) | 0 | 0 | 16 MiB |
| First and settled Low redraw | 16 MiB (2048²) + 4 MiB (1024²) | 0 | 0 | 20 MiB |
| First Low caster edit | 8 MiB (two 1024²) | 4 MiB (1024²) | 16 MiB (2048²) | 28 MiB |
| Low launch, matched settled view | 4 MiB (1024²) | 0 | 0 | 4 MiB |

The first caster edit moved the oversized buffer from the completed-map slot to the rasterizer's spare buffer.
Windows reported 57,540,608 bytes of working set and lifetime peak at the High-to-Low settled checkpoint and
37,982,208 bytes at the separate Low launch's settled checkpoint. These diagnostic runs establish live depth
retention. Matched quiet baseline and candidate runs will judge process residency and interaction costs.
Observer source, arguments, executable hash, affinity and raw logs are in `C:/blab/0631-shadow` under
`prepare_probe.py`, `baseline-high-v1` and `baseline-low-v1`. Discovery evidence is in
`C:/blab/pass33-hub/low-end-audit.md`.

### Candidate

Entering Low with a lower shadow cap releases obsolete completed-map and static-layer buffers and the current
thread's oversized spare buffer. Compatible cached depths retain their keys and contents even when their buffer
had grown at a higher resolution. The rasterizer continues to exchange the same thread-local vector with the
completed map. The CPU viewport completes its synchronous raster flush before the editor can set the tier.
Interactive F12 draws from its own snapshot into a job-owned map on its own thread. The shadow memory account now
includes the main thread's spare buffer.

Matching Windows CPU observer packages used MSVC 14.51 and release optimization with LTCG disabled.
The baseline SHA-256 is `bd214817b84dd99a75150e1214f26018146cc1cad16388196f0b0bba45faaab6`.
The candidate SHA-256 is `39260fea39790b2970b81dfa9a7018c560cdc76e9948ce1f9a1313bb37d29c7f`.
Both packages were built through the locked launcher at the same scratch source path.
The implementation adds a CPU viewport regression covering the High-to-Low release, still-layer reuse after
moving the cube and matching pixels on return to High. Independent testing added split-view, compatible-layer, repeated-switch and final-render cases. The Windows libraries build passed all 45 focused checks with the test blob f6691b257ee16653e87edd7d1fc2a18ef52eb317. The initial framebuffer comparison included transient controls. The final tests compare Scene pixels before overlays and the Game content rectangle, with exact equality at equal dimensions. See C:/blab/0631-shadow/independent-focused-full-v2/stdout.log.

Across nine matched real-window CPU trajectories, the active shadow depths and the rendered Scene and Game images matched. In both single and split views, the settled Low frame held 4 MiB across the four cache entries and the rasterizer spare, versus 20 MiB before the change. The first caster edit held 12 versus 28 MiB. Switching after warming a High static layer held 8 versus 56 MiB. A separate fixture with a reusable 1024-square map held 16 versus 28 MiB after compaction and reused its map and static layer without a rebuild. A launch already at Low preserved build counts. All six F12 PNGs matched. Returning immediately to High rebuilt one completed map; after the High static-layer warm-up it also rebuilt one layer. Compare C:/blab/0631-shadow/independent-results.md and independent-run-1/comparison.json.

### Paired memory and frame results

The exclusive Windows run used a Ryzen 9 5900XT, two physical cores (verified child affinity 0x5), a two-thread editor limit, identical runtime files and a 1280 × 720 software-rendered window. Each fixture used fresh processes, a baseline/baseline control and three alternating baseline/candidate pairs at the same executable path. Separate launches checked pixels and depth. The wrapper held both Windows slots and the Linux slot, exited successfully and recorded zero contention. See C:/blab/splat629-v2-shadow631.log and C:/blab/0631-shadow/lead/paired-7paqkzqu/{manifest,comparison}.json. An earlier staging attempt stopped after two control launches; the successful series independently repeated all controls.

| Fixture and metric | Paired observations |
| --- | --- |
| Settled Low, current working set | Across six comparisons, baseline 52.45–52.84 MiB and candidate 36.47–36.88 MiB. Paired savings 15.57–16.35 MiB. The baseline-only pairs differed by 0.15 and 0.01 MiB. |
| Process peak through returning to High | Candidate minus baseline ranged from −0.35 to +0.19 MiB. The earlier High frame and restored resources contribute to the lifetime peak. |
| Enter Low, command and first frame | Candidate minus baseline ranged from −0.14 to +1.88 ms across the six pairs. |
| Sixteen Low caster edits | Paired changes in per-run median were −0.19, +0.09 and −0.09 ms; changes in the longest frame were −0.77, −0.02 and −0.48 ms. Candidate maxima ranged from 10.26 to 10.34 ms. |
| Return to High | With no intervening edit the candidate rebuilt one completed map and took 3.68–7.99 ms longer (17.74–19.97 ms total). After sixteen edits it took 1.20–1.78 ms longer (21.18–22.44 ms total). |

The no-edit baseline/control pair differed by 4.04 ms on returning to High, larger than the smallest of the three positive candidate deltas. In the editing control, the median changed by 0.006 ms and the maximum by 0.69 ms. This desktop fixture demonstrates working-set relief after the switch. Physical low-memory-device performance remains unmeasured. Compaction of a compatible 1024-square map can temporarily allocate its 4 MiB replacement before releasing the larger capacity; process peak sampling does not isolate that transient.

### Verification

On Windows with Blender libraries, the independent focused suite passed 45 checks and the full render group passed 40,585 checks on the final test blob. The editor group passed 3,908 checks on the same production blobs. Quick editor stress processed 2,674 events in 1,500 frames with zero crashes, stale pictures or missed changes. See C:/blab/0631-shadow/lead/full-verification-4/render/receipt.json and full-verification-3/{editor,stress}. The earlier render suite using the superseded UI-crop checks failed only those three comparisons. The joined Linux dependency-free verification passed 40,479 render checks and 3,860 editor checks, including all three focused cases on the final test blob. The build log verifies the source hashes against 0611v2-0613v4.json. See C:/blab/splat-timed-629/linux-joined-v2-build.log and verify-linux-joined-v2.log. An earlier isolated archive omitted the bridge sources required by test_livelink.cpp and stopped during compilation. The joined build included those sources.





The Linux editor stress run checked the joined executable hash against its build receipt and completed 1,500 frames and 2,674 events with zero crashes, stale pictures or missed changes. See C:/blab/0631-shadow/lead/verification-4/linux-editor-stress.md. The independent code reviewer approved all five source and test blobs, checked the memory and latency comparisons against their controls, and required green joined Linux checks for acceptance. Those checks passed. The lead rehashed all five worktree files against verification-4/inputs.json after testing. The Hub owns publication, CI and the joined pass handoff.
