# 0642: MSVC 14.52 release compatibility and hosted CI evidence (round 660)

- **Status:** in review (findings only)
- **Authorization:** Pass 37 Hub assignment, based on PR #505 at d7503d4e.
- **Requirements:** Q-01, Q-02, Q-03 and handoff items 12 and 19.
- **Decisions:** ADRs 0133 and 0134.
- **Implementation:** Round 660 compiler and CI investigation.
- **PR:** Pending Hub publication.

## Goal

Establish whether the current Windows release configuration with MSVC 14.52 passes the original acceptance workloads, and identify how the prior results apply to today's tree.

## Scope

Inspect historical build and diagnostic logs, PE/PDB provenance, local toolset availability and current hosted workflow coverage. Run the unfiltered unit executable in one process and quick stress against the same release build. Review the macOS CI budget and coverage separately. Production toolset changes require evidence satisfying item 19.

## Likely files

This spec, the round's findings and changelog fragment. Any hosted diagnostic workflow is isolated on a diagnostic branch submitted through the Hub.

## Acceptance criteria

1. Attribute each result to its exact source, compiler and workload. Identify any distinction between a failed check, an exception and an unavailable result.
2. For new Windows results, record actual compiler version and release flags, both native exits and named trial logs. Treat PDB evidence as matching only for the build that produced it.
3. Reconcile macOS test/stress coverage and recent failure modes against the current workflow.
4. Independently verify evidence and review the result before handoff.

## Tests and checks to run

Original release unit suite in project CI order, quick stress and the stock pinned toolset as provenance control. Use scratch projects and the pass machine scheduler where local execution is needed.

## Risks and edge cases

The hosted default compiler may have moved again; enforce 14.52 explicitly. A later passing program or a different assertion failure alone does not explain the historical exceptions.

## Documentation to update

Record findings in this task and its changelog fragment. Identify any remaining hosted or symbol dependency explicitly.

## Outcome

Retain the production MSVC 14.51 pin. On the same product revision, the pinned compiler passed the complete dependency-free unit and quick stress workloads. Stock 14.52 built successfully, then both processes exited with Windows access violation `0xc0000005`. Matched release-symbol replays identify separate unit failures on the historical and current trees. Attribution to a particular compiler optimization remains open.

### Original and symbolized Windows evidence

The original dependency-free run used source `36dd42af` and MSVC toolset 14.52.37025. The normal unit process exited with status 139 immediately after the Paint layer case reported success. Worn Steel was the next case in suite order. Quick stress raised an access violation after the 26-mesh table in `modifiers_ngon`. Its executable reported unsymbolized frames. [Original run](https://github.com/EmilianoManaloIV/blendity/actions/runs/44354841677)

The later debugger jobs used source `4f880bb3` and the executables and PDBs from the same 14.52 build, artifact 14505045246. The unit stack reaches `SurfaceMaps::sample3` through the mesh-map layer. The stress stack reaches the Blockout border builder, `Maker::mark_borders` at `blockout_faces.cpp:249`. These observations localize the later faults. The original executables have no matched PDB in these receipts. [Symbol build](https://github.com/EmilianoManaloIV/blendity/actions/runs/44468688661) · [Unit stack](https://github.com/EmilianoManaloIV/blendity/actions/jobs/135385511375) · [Stress stack](https://github.com/EmilianoManaloIV/blendity/actions/jobs/135384053637)

Two diagnostic-only source changes altered Blockout border endpoint lookup and surface-map sampling. Three subsequent builds on that trial tree passed quick stress and failed the full unit process under release defaults, precise floating point and disabled link-time code generation. All three unit logs passed Worn Steel and continued at least through the later Perlin Noise case. Those failures do not establish that the earlier fault recurred. [Release defaults](https://github.com/EmilianoManaloIV/blendity/actions/runs/44473243028) · [Precise floating point](https://github.com/EmilianoManaloIV/blendity/actions/runs/44478574180) · [No LTCG](https://github.com/EmilianoManaloIV/blendity/actions/runs/44481953061)

### macOS job attribution

| Historical PR | Job | Recorded outcome |
| --- | --- | --- |
| #427 | [Build](https://github.com/EmilianoManaloIV/blendity/actions/jobs/133411196035) | Hosted runner acquisition failed. Unit and stress were skipped. |
| #428 | [Unit](https://github.com/EmilianoManaloIV/blendity/actions/jobs/133446970976) | Hosted runner acquisition failed. Build and stress passed. |
| #445 | [Stress, first attempt](https://github.com/EmilianoManaloIV/blendity/actions/jobs/134231548660) | Hosted runner acquisition failed. The rerun of that workflow succeeded. |
| #445 | [Unit, later revision](https://github.com/EmilianoManaloIV/blendity/actions/jobs/134538216386) | The executable ran 3,726 tests and returned failure. Its annotation names the CPU viewport Preferences check for disabled Ray-Traced Shadows. |
| #449 | [Build](https://github.com/EmilianoManaloIV/blendity/actions/jobs/134272742202) | Hosted runner acquisition failed. Unit and stress were skipped. |

The workflow gives the macOS stress process a 900-second alarm and the stress step a 20-minute limit. The unsharded unit job runs the complete suite from the build artifact. Four later successful source builds had build/unit/stress job durations of 52/995/566, 676/894/551, 58/1,065/585 and 724/1,049/259 seconds respectively. Job durations include setup and artifact transfer. The four hosted-acquisition failures happened before the affected command could run. Increasing the stress alarm or dividing the unit suite would leave those failures unaddressed. [Workflow](../../.github/workflows/build.yml) · [Run 1](https://github.com/EmilianoManaloIV/blendity/actions/runs/45992318288) · [Run 2](https://github.com/EmilianoManaloIV/blendity/actions/runs/45918998099) · [Run 3](https://github.com/EmilianoManaloIV/blendity/actions/runs/45908047978) · [Run 4](https://github.com/EmilianoManaloIV/blendity/actions/runs/45846488174)

### Remaining testability items

The current animation playback path still initializes `anim_.play_clock` from `now_seconds()` when playback starts. The injectable editor test clock therefore cannot drive that entire path deterministically; the existing manual-step physics path is a different control. This is an open editor testability task. The console sweep's formerly unstable shortest-example selection was replaced by an explicit inventory with coverage validation. [Playback implementation](../../src/editor/animation_window.cpp) · [Sweep inventory](../../tools/sweep_commands.py) · [PR 477](https://github.com/EmilianoManaloIV/blendity/pull/477)

### Current hosted run

The local VS 18 Build Tools instance exposes x64 toolset 14.51.36231. The dedicated hosted diagnostic uses the product, tests, stress sources and stock build script from `d7503d4e`. Its branch-only changes select 14.52 in the hosted preparation helper and add a workflow which runs the two original workloads on the same release artifacts. The strict helper verifies the selected compiler and linker paths. A symbol build would be a separate binary. [Diagnostic workflow and source](https://github.com/EmilianoManaloIV/blendity/commit/77ca33432753bbdc56485b0a190446328f690a14) · [Hosted run](https://github.com/EmilianoManaloIV/blendity/actions/runs/46003147249)

The stock Windows control on PR #505 selected toolset 14.51.36231. Its single unit process passed 132,595 checks in 3,951 tests. Its quick stress job passed, including `modifiers_ngon`. This is the dependency-free hosted control; full-library local check totals are configuration-specific. [Pinned Windows control](https://github.com/EmilianoManaloIV/blendity/actions/runs/45992318288)

The current stock release built successfully on `windows-2025-vs2026` using toolset 14.52.37025 and compiler 19.52.37029. Artifact `15202347885` has SHA-256 `be77ce22b18f468239dace63a63df45edbc192a3de87bbae9775cba0da942b08`. Both native processes returned `-1073741819` (`0xc0000005`). The unfiltered unit process recorded 416 successful tests; its last completed case checked repair of an imprint ring with a nonfinite corner. It had not reached the historical smart-material case. Quick stress completed Push/Pull and entered `modifiers_ngon` at 359.4 seconds. It printed the mesh table and an access violation with 12 unnamed application frames. [Compiler job](https://github.com/EmilianoManaloIV/blendity/actions/jobs/140519625438) · [Unit job](https://github.com/EmilianoManaloIV/blendity/actions/jobs/140533819828) · [Quick stress](https://github.com/EmilianoManaloIV/blendity/actions/jobs/140533819869)

A second run builds the exact current and original source revisions under release optimization with debug information. It verifies the transferred program/PDB hash pairs before invoking the original individual CI workloads. [Exact-source symbol replays](https://github.com/EmilianoManaloIV/blendity/actions/runs/46008140480)

The symbol replays reproduced both native exits on both exact sources. A read-only check independently matched the CodeView GUID and age in each downloaded executable to stream 1 of its PDB, and the four EXE/PDB hashes to their respective build manifests. All four GUIDs were distinct and the age of each pair was 1. [Paired build artifacts](https://github.com/EmilianoManaloIV/blendity/actions/runs/46008140480) · [LLVM PDB format](https://llvm.org/docs/PDB/PdbStream.html)

| Exact-source unit replay | Successful tests before exit | Verified stack on that binary |
| --- | ---: | --- |
| [Original source](https://github.com/EmilianoManaloIV/blendity/actions/jobs/140556211009) | 644 | `SurfaceMaps::sample3` through `MeshMapLayer::value`, `MeshMapLayer::sample` and `TextureLayer::apply`. |
| [Current source](https://github.com/EmilianoManaloIV/blendity/actions/jobs/140556211302) | 416 | `Vec4::operator+` in `CornerSample::read`, called by `FaceBuilder::add_colors` and `bevel_edges` in round 621's painted-bevel parity test. |

The two unit boundaries and stacks are distinct. The existing compiler pin is a verified workaround for the tested current tree. This evidence does not establish that an optimizer defect is the initiating cause.

### Candidate release build

The diagnostic branch changes three source files: it resolves color samples with checked vector access in `CornerSample::read`, computes four explicit bilinear taps for `SurfaceMaps`, and takes a border edge's head from its opposite twin in `Maker::mark_borders`. The combined trial tests whether ordinary 14.52 release builds can pass both workloads with these changes. A combined result cannot attribute an improvement to one change in isolation. [Candidate diff](https://github.com/EmilianoManaloIV/blendity/commit/0cd71351626c52b271c5dfaeedb35de19df01532) · [Hosted release workloads](https://github.com/EmilianoManaloIV/blendity/actions/runs/46015798582)

The candidate built with compiler 19.52.37029.0 and linker 14.52.37029.0. Its release archive, artifact `15207051349`, has SHA-256 `29667490c79b6aa07fd130680b974d935bf126f24f2d1e9bbe5ab43572d0eadd`. The single-process unit run again completed 416 tests. Its native exit changed to `-1073740791` (`0xc0000409`, fail-fast). The artifact contains no fail-fast subcode or exception text establishing why it terminated. [Candidate build](https://github.com/EmilianoManaloIV/blendity/actions/jobs/140562464163) · [Unit results](https://github.com/EmilianoManaloIV/blendity/actions/jobs/140571089581)

The checked vector reads preserve the white default for a missing sample, direct selection for one corner, and the three original weights for interpolation. The layer selection already checks the corner domain and `color_fits`. The change validates each selected index at the consumer. It does not change how the triangle produces those indices. The Blockout trial relies on the opposite direction of paired edges; the crisp mesh producer explicitly looks up the reversed vertex pair. The surface-map trial preserves the half-texel offset, border clamping and blend order for finite UVs. None of these source invariants identifies the initiating failure. [Color sampling and layer selection](../../src/scene/vertex_groups.cpp) · [Edge pairing](../../src/scene/blockout_crisp.cpp) · [Surface sampling](../../src/texture/surface.cpp)

Quick stress on the same release artifact completed `modifiers_ngon` at 369.2 seconds, finished in 646.1 seconds and returned 0. The complete unit workload still failed, so none of the diagnostic source changes are proposed for release. [Candidate quick stress](https://github.com/EmilianoManaloIV/blendity/actions/jobs/140571090220)

The downloaded candidate unit executable matched its build manifest (SHA-256 `32cf24978faeffe73edccdfcf175bd93a4d08be11512fa831b8429a7ddf9b62b`). Its only PE debug-directory entry has type 13, and the release artifact contains no PDB. The captured stacks above belong to the earlier symbol builds.

Independent review checked the original job logs, current stock controls, all four EXE/PDB GUID-and-age pairs, both distinct unit stacks, and the candidate's executable hash and native unit exit. To reconsider the pin, require the unfiltered unit executable and ordinary quick stress to succeed on the same replacement release build.
