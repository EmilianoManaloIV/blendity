## 2026-11-26 (round 636): CPU live procedural close-ups

**Asked:** Reduce the deferred raster cost of close-up live materials while preserving every rendered pixel.

**Changed (task 0618)**

- Prepare the enabled layer order when a live texture snapshot has no remapper. The normal samples use the existing height and mask subset in the same order.
- Cover full samples and both normal sampling paths with an identity Tile comparison on ordinary and nested, masked stacks, including seams and invalid UVs.

**Checked:** All 32 matched Windows CPU launches returned identical pixels per mode. All five live-frame pairs improved on both two physical cores and normal affinity. Windows with Blender libraries passed 3,509 asset, 3,871 editor and 40,476 render checks. Joined dependency-free Linux passed 3,471 asset, 3,984 editor and 40,479 render checks. Quick procedural-texture stress passed 343 fuzz cases on each of three joined configurations. Independent review approved the candidate.

<!-- docs/PERFORMANCE.md -->
append:
## Round 636: CPU live procedural close-ups

The live evaluator prepares the layer order for stacks without a remapper once per snapshot, including the shorter order for height samples. For the 1080p Bricks close-up, the median paired live-frame saving was 6.73% on two physical cores and 8.03% at normal Windows affinity. Median live/image ratios fell to 2.63× and 2.58×. Both remain over the 2× target. Task 0618 records the exact frame, memory and baseline-only controls.
