# 0579 — Isolate the toon distance energy measurement (round 597)

- **Status:** done (round 597)
- **Decisions:** ADR 0108 and the existing round 184 sampling contract where applicable. No new architectural decision.
- **Implementation:** implementer in C:/blwt597 from PR #485 head f04dad30.

## Goal

Measure the hatched plane, face-shadow sphere and box-mapped cube using consistent Scene view pixels. PR #484 sanitizer shard 4 gained 19,406 pixels at 2 m for the sphere. PR #485 gained exactly 19,406 at 20 m. Both observations can change the measured energy while leaving the other distance stable.

## Scope and acceptance

1. Identify how unrelated pixels can enter the capture. Pin the failure deterministically using an independently observable disturbance or compare against an authoritative object mask or scene image. Document the limits of attribution to the CI runs.
2. Exclude that disturbance from both color and coverage at each distance. Preserve both devices, all three materials and their scene configuration, the minimum sample sizes, the existing energy computation and every energy bound. A true change in the object's interior must remain measurable.
3. Verify the focused case plus the render and editor groups under the hub's machine schedule. Independent testing and review precede handoff. Record the old/new evidence and any unverified platform separately.

Owned files are tests/test_render.cpp, this spec and changelog.d/597-render-reference.md. If required, a minimal read-only Scene view capture accessor in src/editor/editor.h is in scope. Use scratch paths and the hub's build, GPU and timed reservations.

## Implementation and evidence

The old test compared two crops of the finished editor frame. A toast drawn over only one crop
changes the colors and adds unrelated pixels to the moved-away difference mask. The Scene view
cache stores the rendered view before the UI draws toasts, and headless rendering reads back
GPU color before the cache is stored. The three mapped-material comparisons use that cached
picture for both captures at each distance. They retain the moved-away mask, 3D overlays,
scene contents, downsample, Laplacian, sample minima and energy limits from round 184.

The stable CPU result for the face-shadow sphere was 50,912 pixels at 2 m and 295 at 20 m, with
energy 18.18 in the downsampled near reference and 36.38 at 20 m. PR #484 reported 70,318 and
295 pixels with energies 15.35 and 36.38, which failed the sphere's energy bound. PR #485 reported
50,912 and 19,701 pixels with energies 18.18 and 0.46, which passed the bound for the sphere despite
the extra coverage. Both comparisons added 19,406 pixels to one mask. The CI logs record the counts
and energies. They do not identify the pixels or any toast that was present.

At both distances, the regression introduces a visible toast between the away and present frames
of both the composited and cached captures. It counts false coverage inside the toast's recorded
rectangle in the composited capture and checks that the shared mapped-material capture returns
identical colors and coverage in the clean and toasted runs. It also changes the surface to a checker while the toast is visible and verifies that the same
interior samples register more detail.

## Verification

On the corrected sanitizer shard, the toast added 10,586 pixels to each composited mask.
The cached colors and masks matched the clean captures at both distances. Adding a checker
raised interior energy from 0.95 to 5.55 at 2 m and from 0.00 to 65.88 at 20 m. The face-shadow
sphere returned to 50,912 and 295 pixels with reference/far energy 18.18/36.38. All 49,030
checks passed. [Shard 4 log](https://github.com/EmilianoManaloIV/blendity/actions/runs/44584505699/job/135794014141)

| Build | Focused checks | Render checks | Editor checks |
|---|---:|---:|---:|
| Windows release, dependency-free | 50 | 40,419 | 3,846 |
| Windows release, Blender libraries | 72 | 40,540 | 3,865 |

All listed checks passed with MSVC 14.51. The card's sphere covered 50,220 and 292 pixels with
reference/far energy 18.26/36.04. The hosted Linux and macOS full suites passed 125,283 and
125,274 checks respectively. The hosted Windows diagnostic lacked the pinned toolset.
After WSL failed again, the hub selected hosted Linux verification. Run 44584505699 built
candidate 9cc83b6586f5e78de50a77fd872ada93e78f8b9c and ran the full unfiltered unit binary,
including the render and editor groups and all three focused toon cases. All six sanitizer shards
also passed. [Linux unit log](https://github.com/EmilianoManaloIV/blendity/actions/runs/44584505699/job/135781975902)
