# 0634: Dense sculpt viewport on constrained CPUs

- **Status:** in review
- **Authorization:** Pass 37 owner assignment, round 652, base d7503d4e on PR #505.
- **Requirements:** Q-06
- **Decisions:** ADRs 0056, 0074 and 0134
- **Implementation:** project implementer
- **PR:** Hub integration and publication

## Goal
Reduce complete CPU viewport frame time while sculpting dense meshes without changing visible pixels, picking, brush output, masks or undo.

## Scope
Use real Draw and control gestures, including Mask and a nonlocal brush. Prior round 643 found a 27 ms pixel-shading stage at two CPUs and rejected stale shadows. Investigate repeated per-triangle work in deferred shading first. Keep shadow quality and geometry processing intact.

## Likely files
- src/render/raster.cpp, specifically Renderer3D::shade_deferred and directly related CPU shading code.
- tests/test_render.cpp (round-specific group) and existing Sculpt fixtures; separate focused test file only if already integrated by project convention.
- docs/tasks/0634-dense-sculpt.md, docs/perf/0634-dense-sculpt.md and changelog.d/0652-dense-sculpt.md. Scratch in C:/blab/pass37-hub/0634. Coordinate additional shared files before editing.

## Acceptance criteria
1. Isolate expensive CPU passes on a selected 500k-face mesh with complete press/move/release input.
2. Compare repeated alternating baseline/candidate runs at low CPU count with full-frame and stage timings. Check standard and broad strokes and smaller/nonlocal controls.
3. Verify actual edits, attributes, masks, undo and identical pixels, depth and object IDs. Exercise mixed materials, configuration changes and sharing across frames when relevant to the change.
4. Retain a product change only for a repeatable material frame-time gain with valid outputs. Record any rejected experiment and remaining gap.
5. Independent tester and reviewer verify the candidate and supporting data.

## Tests and checks to run
Run affected render/sculpt/editor checks using matched Windows and Linux objects, in scratch, coordinating shared verification with Hub. Use the official timing queue for benchmarks. Audit fragment and staged diff.

## Risks and edge cases
Avoid stale material state across triangles or frames, thread-unsafe mutable caches, bypassing highlight and vertex-color logic, excess cold cost or memory proportional to triangle count. Distinguish stage improvements from full-frame gains.

## Documentation to update
Round performance note with reproducible workloads and evidence; changelog fragment with performance section.

## Outcome
Selected the plain-material streaming variant. At two logical CPUs, dense standard and broad Draw move frames improved by 5.95% and 4.64% across five alternating pairs. The selected build matched every compared frame, edit and undo. The [performance note](../perf/0634-dense-sculpt.md) documents the controls and rejected caches. Independent review found a row-boundary streaming refresh issue during development. The selected source restores the refresh at the first covered triangle of each row. All eight native groups passed on that exact source. The original normal-affinity Mask move p95 rise (1.426 to 1.769 ms) did not repeat beyond baseline variation. Every instrumented Mask move used the view cache and made zero deferred-shading calls. The control therefore passed the repeatability guardrail.
