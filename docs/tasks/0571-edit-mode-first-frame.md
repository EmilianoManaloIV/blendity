# 0571: Reduce the first Edit Mode frame on a smooth 500k-face mesh (round 589)

- **Status:** done, findings independently reviewed, 2026-11-19. The measured candidate was rejected and removed from the product.
- **Requirements:** Q-06 responsiveness, C-01 / C-02 builds
- **Decisions:** ADR 0105 (responsiveness), ADR 0123 (documentation fragments)
- **Related:** task 0567, full-size performance survey
- **PR:** no product change proposed

## Observation and target

On the 32-thread Ryzen 9 5900XT, with the RTX 4070 SUPER and Windows MSVC 14.51 libraries build at 4dbfc99, entering Edit Mode on the selected 500k sphere took 111.9, 113.0 and 101.4 ms across three fresh runs of the full responsiveness selector. Median 111.9 ms exceeds the 100 ms unsplittable-frame limit. The historical reference also exceeded the limit at 100.3 ms.

Three independent scratch launches of the same editor split the command from its following frames. The edit command took 0.1–0.5 ms. The first frame took 98.7–108.4 ms and the second took 2.6–2.9 ms. Every first frame reported one full render mesh build (33.90–34.31 ms) and one full send (42.90–49.93 ms including preparation). CPU overlay time was 0.04 ms. These scopes are nested inside the Scene view and GPU-path totals.

The active smooth mesh requests force-flat shading in Edit Mode. This selects a distinct render-mesh cache. Its first frame constructs the flat representation and prepares a new card mesh. Reduce this first-frame work while preserving the exact visible flat-shaded result, edit element identity and first interactive frame.

## Work and ownership

1. Reconfirm the baseline at the integrated stack top before implementation. Use scratch instrumentation to partition the force-flat build, vertex packing, index preparation and card submission. Record mesh sizes, upload bytes and process working set/peak. The elapsed upload counter includes CPU preparation. Verify the single-material path and compare a mesh with multiple material slots.
2. Optimize the measured bottleneck within the existing mesh and card cache contracts. Candidate scope is the force-flat construction in src/scene/mesh.cpp or the CPU-side mesh preparation in src/editor/gpu_views.cpp. Prefer a single measured change. A single-slot upload currently returns the indices by value even when order is unchanged. Measure that copy before changing it. Check render-mesh serial, topology and attribute invalidation. Preserve complete fallback behaviour and the original pixel result on the first frame. Moving the hitch to object selection, the next edit frame or a later upload does not satisfy the task.
3. The implementation worker owns those two candidate source files plus tests/test_gpu.cpp and tests/test_render.cpp for narrowly relevant correctness coverage, this spec and changelog.d/589-edit-mode-first-frame.md including its docs/PERFORMANCE.md section. Read-only call sites include src/editor/scene_view.cpp, src/editor/render_view.cpp and stress/stress_main.cpp. Coordinate shared files with the hub. Request a spec amendment if profiling points to different product files or a change to a rendering contract.
4. Verify geometry and presentation on an unpainted smooth sphere, an already-flat mesh, a multi-material mesh, and a mesh with UV seams and discontinuous face-corner RGBA. Compare positions, normals, indices, triangle-to-face and material mapping, UVs, colour/alpha and pixel output using identical camera/device settings. Verify Edit Mode selection and picking, a subsequent topology or colour edit, undo, exit and re-entry. Include a modifier visible in Edit Mode and a CPU fallback comparison.

## Acceptance

- In coordinator-owned quiet windows, run at least three full cold responsiveness observations per side using identical source base apart from the proposed change, same compiler/libraries, card, Release flags, staged shaders and scratch inputs. Alternate before/after process launches. Publish every result and the medians. The original 500k entry median must be below 100 ms, and a reproducible improvement must exceed within-build variation. Investigate remaining over-budget samples.
- Repeat the diagnostic app launches at least three times per side. Compare selection and the first and subsequent frame timings, build/upload counts, bytes, peak memory and viewport status. Confirm the frame fully presents the flat cage, attributes, and mesh on entry. Check for a shifted stall and measure smaller meshes as a control. Record any change in memory or upload volume.
- Run the approved repository build/test chain and relevant mesh, GPU and render checks. Build both app and tests on the integrated top. Run the full-size responsiveness selector, the established editor-render and operations controls, the required quick stress and CI checks. Use isolated scratch paths throughout.
- Record verified wins and adverse results with the exact build IDs and controls. If the change fails correctness or timing acceptance, retain the findings and leave the unsuccessful optimization out of the product.

## Integrated baseline and scratch partition

The baseline program inputs match #480 at `22ead0eb`. The matched Release stress binary
was rebuilt on MSVC 19.51.36260 / toolset 14.51.36231 with Blender libraries and default
LTCG. The integrated app and both scratch builds use the same staged shaders.

Window05 produced a 103.5 ms full-selector baseline observation. The integrated app's
first Edit Mode frames were 114.0, 106.6 and 103.3 ms. Its edit command took 0.1–0.2 ms,
its second frames took 2.7–3.2 ms, and the real object selection control took 2.0, 2.8
and 2.4 ms for its first frame.

On the instrumented app, 2,004,992 packed vertices and 3,004,416 indices accounted for
108,257,280 bytes per cold flat upload. The serial partition's median was 25.113 ms for
vertex packing, 1.491 ms for the single-slot index copy, and 14.908 ms for card submission.
Multi-slot grouping took 8.825 ms. Flat normal construction took 6.454 ms and the
remaining render mesh build took 28.792 ms, with 15.053 ms spent numbering and filling
the render vertices. The large JobSystem packing probe took 17.708 ms median and the
first-frame medians were 102.3 ms serial and 95.6 ms parallel. First-frame samples were
112.7 / 102.3 / 99.4 ms serial and 97.4 / 92.1 / 95.6 ms parallel. Peak working-set medians
were 850.05 and 849.89 MiB. The 50k comparison held at 15.8 ms median on both sides and
remained serial at 200,192 render vertices.

The candidate changed only vertex packing in `src/editor/gpu_views.cpp`. At 262,144 render
vertices it used existing JobSystem ranges of 16,384 vertices. It kept the original
working representation, index paths, cache checks and card submission.

## Matched production measurements

Window06 alternated fresh baseline and candidate processes in that order for each pair. Both used base `22ead0eb6f953a2006d17813e94ae0da3f5bb1b7`, the same MSVC Release/LTCG and Blender-library configuration, and byte-identical staged DLLs and shaders. The first 12 SHA-256 digits for the app/stress binaries are `77efd1abc32c`/`553b7a35790c` before and `dd0ba74a9aaf`/`4807cb798044` after. The uncommitted product diff SHA-256 is `caa805f08a4528efc53a2d5b9dff6f64c7c3c40adf8c8c8828db98e75fdd85ec`.

| Observation, ms | Baseline in launch order | Candidate in launch order | Median before → after |
|---|---|---|---|
| Full responsiveness, 500k Edit Mode | 141.5, 101.4, 100.6 | 107.2, 96.4, 99.6 | 101.4 → 99.6 |
| Diagnostic app, first edit frame | 112.4, 102.7, 105.7 | 99.0, 103.6, 104.2 | 105.7 → 103.6 |
| Following edit frame | 3.2, 3.3, 3.5 | 2.8, 3.0, 3.1 | 3.3 → 3.0 |
| First frame after object selection | 2.1, 2.4, 2.1 | 2.4, 2.4, 2.2 | 2.1 → 2.4 |
| 50k first edit frame | 15.5, 16.3, 15.0 | 14.6, 15.8, 20.5 | 15.5 → 15.8 |

The full-selector median met the 100 ms limit, but its 1.8 ms improvement was smaller than the observed ranges of 40.9 and 10.8 ms. Even the two later candidate runs differed by 3.2 ms. The app's median first-frame improvement of 2.1 ms also fell within each build's range. All six edit commands took 0.1–0.2 ms. Each of the six smooth 500k GPU entry frames built and sent one mesh (105,720 KiB). Median upload time, including preparation, fell from 45.58 to 39.76 ms while median mesh build time rose from 34.94 to 37.21 ms. Median peak process working set was 847.1 MiB before and 837.0 MiB after. The multi-material and authored-flat controls each had one observation, 128.9 → 113.7 ms and 21.8 → 21.0 ms respectively.

The baseline's first selector also ran Boolean difference and Separate slower (74.6 and 130.8 ms) than its next two launches (57.1/103.1 and 49.0/96.0 ms). The card sampled several power states across all six processes. These process-wide observations cannot attribute the first pair's stall to a specific cause. All samples count toward the result. The candidate fails the reproducibility criterion.

The 0571 workloads completed successfully in Window06, which recorded zero taint lines. Both full editor-render controls displayed all 33 views on the RTX 4070 SUPER, with zero CPU fallbacks; the GPU moving-object row was 4.45/4.57 ms per frame before/after. The operations control ran 1,056 operations per build in 76.5/77.6 seconds. Three/four exceeded 50 ms, one each exceeded 100 ms.

Decoded 1280×720 captures showed identical visible mesh and cage pixels on both GPU and CPU entry frames. The dense vertex cage appears as a black silhouette, so the authored-flat fixture and exact attribute checks provide the separate shading evidence. The full images differed in the live performance text and bottom status bar only (119 GPU pixels, 243 CPU pixels); the two-material and authored-flat second-frame captures also differed only in these fields. The focused GPU test passed 27 checks including exact packed vertices, indices and first-frame Scene pixels for one and two slots.

## Verification of the rejected experiment

Release app, stress and test targets built on Windows with and without Blender libraries and on dependency-free Linux. The initial CPU fixture inserted its panel directly, outside the editor's Undo history. Undo removed the panel, producing one failed check out of 41. Registering the fixture before the edit preserved the original selection assertion and allowed all 45 CPU checks to pass on both Windows builds, including the modifier and re-entry checks.

The experimental patch, including its tests, was removed after the builds. Product and test files in the findings worktree match the integrated baseline. The candidate was preserved separately at `6e4bc62f76177c15e8b8593158b7a732281ff0f4`. Diagnostic commit `82b6f5b26842acb09508d42032a4b353771515d1` adds a dispatch-only hosted Linux quick stress workflow on that exact source. There is no product PR.

| Local test group, checks passed | Windows with libraries | Windows without libraries | Linux without libraries |
|---|---:|---:|---:|
| Modeling | 34,178 | 31,855 | 31,855 |
| Render | 40,535 | 40,426 | 40,441 |
| GPU | 1,682 | 159 | 160 |
| Editor | 3,872 | 3,846 | 3,846 |

The Windows libraries GPU group exercised the physical card. Dependency-free builds skipped Vulkan-only assertions. Standalone Windows render/editor used CPU fallback, and Linux used the software-only configuration. The libraries GPU receipt predates the CPU-fixture correction; the correction changed only the Render group and both Windows Render receipts include it.

The dependency-free Windows quick stress ran all 48 sections with four threads, exited 0 and wrote a complete report in 670.1 seconds. The first Linux quick stress ended during the bake section while a compiler also used the 15 GiB WSL VM. The kernel logged an out-of-memory kill of `blendity_stress`.

The hosted dependency-free Linux Release retry on candidate source `6e4bc62f` exited 0. With four threads pinned to CPUs 0–3, it completed the same 48 named sections in 628.4 seconds and peaked at 15,149,352 KiB (14.45 GiB) resident. Its stress executable SHA-256 was `39edcc3b086933446050d0664b6e15f46b9756988f5b68a721b231ef42a5c5da`. [Hosted job](https://github.com/EmilianoManaloIV/blendity/actions/runs/44609521187/job/135862079065) · [Report, command, revisions and memory receipt](https://github.com/EmilianoManaloIV/blendity/actions/runs/44609521187/artifacts/14573803150)
