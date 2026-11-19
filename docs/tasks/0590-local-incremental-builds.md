# 0590. Dependency-aware local Windows builds

**Status** Done, 2026-11-19. The Hub approved round 608 under the owner assignment. Base 899fca32.
**Decision** [ADR 0155](../decisions/0155-local-incremental-builds.md).
**Implementation** Build-tool implementer, followed sequentially by independent test and review roles.

## Goal and scope

Shorten the Hub's published-revision local editor refresh by reusing verified compiler outputs. Cover the Windows release app and unit-test targets. CI, installers, release builds, Linux builds and owner deployment keep their existing entry points. Run this round exclusively in scratch trees.

## Acceptance and evidence

Use one quiet Hub-granted timed window with the machine helper holding both Windows slots and the WSL lock. Record revision, file-level input changes, exact cl/link versions and paths, environment, effective compile/link options, feature defines, library roots, SDK, output inventories, elapsed seconds, objects compiled and targets linked per invocation. Compare the stock clean path with empty-cache and warm incremental builds using the same 14.51 installation, release optimization, runtime and installed owner libraries. Also exercise dependency-free configuration. Use separate output roots, scratch project, preferences and trash.

| Scenario | Required observation |
|---|---|
| Clean and empty cache | All required sources compiled. App and test link inputs match the clean reference. |
| Repeated build and docs-only revision | Zero compiles and links. Measure elapsed time including validation and staging. |
| Single implementation edit | Exactly the affected unit recompiles. Each requested consumer relinks once. |
| Shared ABI header edit | Every direct and transitive consumer rebuilds. Run affected tests against the changed ABI and compare to clean at that edit. |
| Target isolation | App-only and test-only edits invalidate their own consumer. Switching targets cannot lose a pending relink. |
| Source list and configuration | Added/removed sources alter the link list. Changed effective flags, library selection, build identity or toolset invalidate affected records. An already-active mismatched compiler fails before reuse. |
| Failure and retry | Exercise compile and link failure. Last good scratch output remains launchable. Repair and retry, including a concurrent request against the same cache. |
| Staging and runtime | Compare DLL, license and warmed shader inventories and content, executable identity, unit results and the same headless screenshot on identical scratch projects. Delete a staged asset and verify repair. Explain any nondeterministic differences with reproducible checks. |

Record the precise commands and logs for independent reproduction. Exercise the connected build/link/stage/launch path first, then repeat verification on the final candidate. Run the applicable project chain. The reviewer checks evidence and invalidation against the exact candidate. If parity or useful time savings remain unproven, report the failed criterion and keep the existing default. After acceptance update AGENTS.md, docs/handoff/MACHINE.md, docs/BUILDING.md, relevant handoff instructions and changelog.d/608-local-incremental-builds.md. Submit the reviewed local commit to the Hub for publication.

## Scratch entry point

With Python 3.11 or later installed, open PowerShell in a scratch worktree. Set `BLENDITY_INCREMENTAL=1`,
`BLENDITY_INCREMENTAL_DIR` to a dedicated scratch cache, and `BLENDITY_VCVARS_VER=14.51`.
Relative input and cache paths use the build script’s worktree. Set
`BLENDITY_LIBDIR` to the installed Blender libraries for the full build. Run
`./build.bat release app-tests` for both programs, or use `app` or `tests` to request
one. The build prints the successful package path, which is also recorded in
`current.json` in the cache. `BLENDITY_OUT` is for the clean path. Set
`BLENDITY_NO_WARM=1` for a run that has no GPU allocation. If an include flag
searches the whole worktree, put the cache outside that search root.

The active manifest protects the last successful state if a compile, link or
staging step fails. Keep saved package paths as long as their programs are
running. Ordinary successful requests drop superseded private object and link
files. Older packages and generations left by abrupt interruptions remain on
disk. To reclaim them, stop builds and programs using the old scratch cache,
then retire that cache and choose a new `BLENDITY_INCREMENTAL_DIR` for the next
build. A name change inside a compiler include root may rebuild all requested
sources. Outside those roots, the worker checks candidate names from each translation
unit's actual source and headers, including negative `__has_include` lookups.
Literal and object-macro header names are tracked directly. When a conditional can
paste tokens or otherwise construct a header name that the worker cannot resolve,
MSVC preprocesses that translation unit to validate reuse.

## Connected build verification

The first native build exposed MSVC response-file formatting and Windows file-stat differences. After correction the full editor linked, launched from its staged package and reused that package on the next request. The independent tests below exercise the repaired path.

In the final full-library graph, conditional macros in 19 of the 329 editor translation units required MSVC preprocessing to check which headers they would see. Cold discovery performs an additional compile of those units between validation passes. A warm request checks the fingerprints without regenerating their object code.

## Independent acceptance on 2026-11-19

The stock worktree and candidate used base `899fca32`, x64 MSVC 14.51.36231 (compiler 19.51.36260), the same Windows SDK, OptiX 9.1 headers and Blender library directory. The matched plans had 328 common translation units, 329 objects in the app link and 366 in the tests link in the same order. The owner configuration used /O2, /GL, /fp:fast, /MD and /LTCG. See `C:/blab/0590-evidence/candidate3-plan/` and `candidate3-parity.json` in its parent directory.

- The clean and incremental packages matched byte-for-byte on all 44 DLLs, 20 license files and 37 warmed shaders. The app and test executable headers identify separate link times. Verify a package by comparing its inputs, staging and runtime behavior. Evidence: `candidate3-stage-parity.json`.
- The stock and incremental CPU camera renders at 384 by 256 pixels had the same PNG SHA-256 for the sample (1,024 triangles) and again after deleting the sphere (304 triangles). The two scene hashes differed. Use `overlay stats off` for headless UI comparisons. On repeated 640 by 360 UI captures the live frame-time digits in the status bar varied even between stock runs. The camera render compared every byte. Commands and results: `C:/blab/0590-evidence/camera.py`, `camera4-hashes.json` and the corresponding capture folders.
- The 31 Windows candidate groups passed 139,512 checks. Stock's initial 139,513 checks included one failure in assets because the suite runner supplied a cache outside that group's scratch home. The isolated group passed all 3,118 checks with its cache beneath its scratch home. Both editor runs executed the same 343 named tests. Their internal check totals differed by one. The candidate that ran the full suite and the linked app/test pair from the corrected worker were identical after normalizing just the COFF and debug-directory timestamps. The corrected pair also passed the camera renders and the 64 runner checks. See `candidate2-suites/result.json`, `stock/full-suite.log`, `stock-fixed-assets/stdout.log` and `pe-candidate2-candidate3.json`.
- The stock dependency-free WSL suite initially reported two failures across 130,433 checks. Native Linux temporary storage and a per-group cache beneath the scratch home passed both isolated reruns (899 Source content checks and 3,085 assets checks). See `linux/suites.log`, `linux/native-source_content.log` and `linux/native-assets.log`. Command/action/window coverage had 398 entries and zero empty ones. The optional widget-label audit listed 173 undriven labels.
- Independent tests caught an escaping macro-expanded optional include. Following the fix, a native fixture also synthesized the probe operator with token pasting and checked appearance, edits and disappearance. A failing second target retained both previous executables. Two concurrent repairs compiled one object and linked both targets in total. Logs: `C:/bltest608-incremental/candidate3-independent.log` and `candidate3-independent-paste.log`.
- Review caught a caller-directory dependency and an unobserved optional header beneath a directory junction. The revised worker anchors inputs at the worktree, checks physical targets beneath linked directories, and diagnoses a cache inside an included directory before compiling. All 367 real translation-unit cache identities survived these corrections; the ordinary repeated request returned the same package with zero compiles and zero links. See `C:/blab/0590-evidence/candidate4warm/build.log`.

The final fixture includes eight scenarios. Launching by absolute path from a different working directory matched the stock build. Adding a header through an NTFS junction and retargeting the junction both changed the program result correctly. The token-paste regression is part of the fixture. Independent results are in `C:/bltest608-incremental/candidate4-independent.log`. Worker SHA-256: `9cf29cc8a5143cbffdb0f761de18a038eec43ce69eb589a5e6192f44f3d6a47c`. Test SHA-256: `0f72fa4b2020d0d4e1ce34bf63227edd50a4b0a23816a8ec3dc96710c178c0c5`.

## Local timing and adoption

The Hub reserved both Windows build slots and the WSL slot on 2026-11-19. After 62 consecutive seconds of verified idle time, the matrix began at 13:48:28 Pacific. Both build paths used the same scratch checkout, compiler and owner library configuration. Each elapsed time covers one full invocation including validation, runtime staging and applicable shader warming. The sequence uses warm filesystem and driver caches after the first run. The owner's already-running editor stayed open.

| Request | Source files selected | Targets linked | Seconds |
|---|---:|---:|---:|
| Stock clean editor | 329 | 1 | 234.424 |
| Empty incremental editor cache | 329 | 1 | 303.374 |
| Unchanged editor | 0 | 0 | 19.431 |
| Documentation-only edit | 0 | 0 | 18.294 |
| Edit editor main.cpp | 1 | 1 | 202.064 |
| First tests request after editor | 38 | 1 | 201.578 |
| Test-only edit, request editor | 0 | 0 | 19.269 |
| Request edited tests | 1 | 1 | 163.739 |
| Shared CPU ABI edit, app and tests | 42 | 2 | 321.182 |
| Stock all at the changed ABI, including stress | 368 | 3 | 414.565 |
| Deliberately broken app source | 1 | 0 | 16.228 |
| Repair and request app | 1 | 1 | 147.934 |
| Request unavailable MSVC 14.52 | 0 | 0 | 0.314 |
| Change build identity | 329 | 1 | 214.548 |
| Dependency-free editor | 308 | 1 | 124.791 |

Selected-source counts above are distinct translation units. Cold discovery recompiled 19 units with the full libraries, and 13 with the dependency-free configuration, to validate dynamic includes. The stock ABI reference additionally built the stress program. The changed ABI passed 34,178 modeling checks on each build, and both full camera renders still matched byte-for-byte. The dependency-free editor launched in scratch with `--no-libs` and wrote its screenshot without staged DLLs or shaders.

The timing helper logged 177 build/test activity samples during the matrix, which intentionally ran compiler, linker, editor and test processes. The separate process-tree sampler found no unrelated compiler or test in its Windows samples. The external editor was the owner's running instance. The strict script exited zero and released all three locks at 14:29:42 Pacific. Commands, file hashes, process attribution, outputs and wall times are under `C:/blab/0590-evidence/` (`timed.py`, `timed/base.json`, `timed/processes.log`, `timed/results.json`, per-request logs). The outer lock log is `C:/blab/0590-hub-timing.log`.

Adopt the incremental path for the Hub's routine published-revision local Windows refresh. In this sequence unchanged and documentation-only requests took 18–20 seconds, while one editor source change still required a full LTCG link and shader warming. The empty cache took 69 seconds longer than the stock clean editor. CI, installer and release builds continue using the clean path.

The owner also requested that the pass handoff identify a single cumulative PR for final review. The project rules and handoff now group supporting materials with their deliverable and retain the existing main-base and owner-merge requirements.
