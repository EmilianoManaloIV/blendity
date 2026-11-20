# 0604: Reduce transient draw-list work when moving an object in a large scene

- **Status:** in review
- **Authorization:** The owner's engine performance assignment in the Hub authorizes this pass.
- **Requirements:** Q-04 and Q-06.
- **Decisions:** ADR 0115 governs editor cache invalidation.
- **Implementation:** Hub planning task for the editor collector, with independent test engineering and code review.
- **PR:** Pending cumulative publication against main.

## Goal

Reduce the time spent rebuilding draw lists during a drag in large scenes without changing the picture or the authoritative scene.

## Scope

Task 0577 attributed 86.68 ms mean to collection on the 50k moving-cube reference. Establish current end-to-end median and p95, collection time and temporary draw-list storage. On repeated invalidation, the collector builds a growing DrawItem vector alongside its previous fully populated list. Reuse safe storage where measurement supports it; preserve full recollection, cache verification, object/dependency invalidation, generations and per-view isolation. Avoid unbounded retention when a large scene shrinks.

## Likely files

src/editor/render_view.cpp, the matching private declaration and test accessor in src/editor/editor.h, tests/test_editor.cpp, and changelog.d/622-moving-scene-collection.md.

## Acceptance criteria

1. Compare matched before/after 50k and 10k movers with orbit and two-view controls.
2. Verify correct invalidation for edits, undo, material and view changes, skins and picking. Confirm stale capacity is released after shrinking the scene.
3. Demonstrate a repeatable 5% median improvement or a measurable memory reduction with non-regressing p95. Record the evidence for both the mover and controls.

## Tests and checks to run

Run GPU and editor groups on Windows and Linux, focused cache verification, and balanced scene_views benchmarks on the unchanged release baseline and candidate. Compare median and p95 separately for dragging, orbit and two-view controls.

## Risks and edge cases

Verification needs the original list while comparing it to a fresh gather. A second view can retain its own list. Undo, deletion, hidden hierarchies and material changes must invalidate the correct generation.

## Documentation to update

Record the disposition of both the mover and controls, capacity measurement scope, parity and remaining limits in the round 622 fragment and this task.

## Outcome

### Initial comparison

The two order-balanced launch pairs on the RTX 4070 SUPER ran 60 frames per row. For the 50,000-object mover, median/p95 changed from 225.2/255.9 to 200.0/216.1 ms and from 222.0/250.1 to 202.7/227.1 ms. Mean gather time fell from 89.94 to 67.13 ms and from 90.42 to 69.51 ms. The capacity probe measured 44,241,840 versus 22,120,920 bytes of simultaneously reserved draw-list storage immediately after a warm miss at 50k objects. At 10k objects it measured 8,739,360 versus 4,369,680 bytes. These are list allocations at a specific point, rather than total process peak.

The 50k two-view p95 increased in both launch orderings despite zero collection measured in those rows. Diagnose that control with balanced, same-executable batches, equal camera and transform progression, draw and row counters and identical viewport output. Retain the launch-level results when interpreting the diagnostic. Scratch evidence: C:/blab/0622/pair-20261120-084424-433906/comparison.json and C:/blab/0622/storage-20261120-085653-090505/summary.json.

The focused Windows regression passed 41 assertions. It checks independent Scene and Game list generations and capacities across hits, movement, verification, Undo and a large scene shrinking. The fixture commits its setup before applying the movement, so Undo reverses that operation.

### Matched viewport comparison

All 72 paired workloads matched in work and rendered output. For the 50k mover, median frame time fell from 244.10 to 220.75 ms and p95 from 285.48 to 245.18 ms. Both launch orders improved. For the 10k mover, pooled median fell from 46.81 to 43.63 ms and p95 from 53.10 to 52.65 ms, although p95 rose by about 1.1–1.3 ms within each launch order. The 50k two-view p95 changed from 108.29 to 108.39 ms. The earlier two-view slowdown did not recur. Repeating the orbit control with two baseline arms reproduced the order-dependent variation with zero gathers. See `C:/blab/0622/interleaved-v2-20261120-093948-306130/summary.json` and its sibling `by-order.json`.

### Independent checks

The Windows editor and GPU groups passed 3,904 and 1,768 checks on the subsystem candidate. The focused forced-verification case passed 42 checks with zero stale comparisons. Separate forced-verification comparisons on the stock and candidate binaries gave identical results for light changes (18 checks), transform-only lighting changes (17), Solid view (19), hierarchy changes (27), draw-list verification (2) and worker listing (5). The isolated idle-trim case failed one of six checks on each binary. The full forced-verification run reported four stale pictures on each. Isolated tests on both binaries reproduced one stale picture during the procedural/Fit-to-Mesh walks, one during clip playback with Centre of Mass shown and two during the collider-wire bounds test. The matched 1–211 prefix reported two stale pictures and two failed assertions on each build. A previous baseline replay of that prefix reported four stale pictures. The full-sequence counts cannot establish which individual frames differed. See `C:/blab/0622/verify-prefix-matched/comparison.json`. See `C:/blab/0622/verify-diff-20261120-093217-507757/comparison.json`.

The matched remainder (212–343) reported two stale pictures on each build. Independent testing found no new reproducible failure. The code reviewer approved the frozen product changes, and the Hub corrected the acceptance heading and recorded the W4 controls. The full independent report is in C:/blab/0622/test-report.md.
