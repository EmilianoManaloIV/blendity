# 0639: Large-scene view motion, camera effects and render output

- **Status:** in review
- **Authorization:** Owner's Pass 37 assignment, round 657, on the PR 505 top d7503d4e. Hub schedules shared hardware and integrates the reviewed local commit.
- **Requirements:** R-05, R-10, Q-03, Q-04, C-04
- **Decisions:** ADR 0056, 0105, 0123, 0134
- **Implementation:** Project implementer with independent test engineer and code reviewer
- **PR:** Pending Hub integration

## Goal

Measure the cost of dragging in different directions with both Scene and Game views open on a large scene. Identify and verify a pixel-preserving improvement where the measurements support one, and close the external renderer memory, camera transform and baking gaps from task 0629.

## Scope

- Use valid selection, actual pointer motion, fixed starting geometry and camera, both directions on more than one axis, and comparable single-view and dual-view controls.
- Exercise actual camera effects and AgX where the runtime supports it. Measure an installed external renderer through completed image output and observe its own memory separately. Exercise completed lighting or mesh-map bakes with artifact validation.
- Use existing verified packages and read-only object banks. Coordinate shared paths with the sculpt lane, leave owner data alone, and avoid driver, system and publication changes.

An unavailable named Render view transform must report the failed selection and preserve the active transform and scene modification state. Verify a valid built-in choice still works and exercise AgX with an installed configuration that exposes it.

## Likely files

Round-specific measurement tooling if needed, docs/perf/0639-rendering.md, changelog.d/657-rendering.md and this spec. Reserve individual renderer source and test files through the receipt before editing them.

## Acceptance criteria

1. Report fixture size, actual selection, motion direction, backend, view participation and frame costs for large-scene interactions. Distinguish workload differences from improvements.
2. Capture actual transform/effect participation, external renderer peak memory with scope and sampling method, and completed bake output or a specific evidenced gap.
3. For an optimization use repeated alternating baseline/candidate runs with frame or render output checks, including depth/picking when relevant. Retain the original behavior if a trial changes output.
4. Return an independently tested and reviewed local commit with reproducible evidence and remaining gaps.

## Tests and checks to run

Use the Hub timing queue for performance and separate functional checks. Verify package inputs, completed production actions and output checks. For changed product code run its affected groups and editor, scoped on Windows and Linux, with related stress as appropriate.

## Risks and edge cases

Different camera visibility on opposite drags can change work legitimately. A requested AgX transform may fall back without an OCIO configuration. Windows editor memory excludes WSL render workers. A synchronous bake dispatch alone cannot establish saved output.

## Documentation to update

Round 657 fragment, this spec and the rendering report under docs/perf.

## Outcome

Rotated and warmed drags established a 1.00 ms CPU direction spread in dual-follow and a roughly 47 ms cost for the second live view. The scoped task 0634 renderer reduced paired frame time by a median 0.80% in this workload with exact viewport output. Measured the configured AgX processor and selected camera effects, native MoonRay peak resident memory, persisted lightmaps and mesh maps. Fixed silent failure on unavailable console view transforms. Windows full-library and dependency-free builds and the Linux dependency-free build passed the renderer and editor groups. [Measurements, controls and reproduction](../perf/0639-rendering.md). The Hub owns integration and the owner merges.
