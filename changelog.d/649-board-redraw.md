## 2026-11-26 (round 649): Redrawing images on the Reference Board

**Asked:** Investigate the delay after duplicating hundreds of visible image placements.

**Changed (task 0631, ADR 0089)**
- The board looks up a canonical file path for a full-resolution picture only when the export has supplied overrides. Drawing the board uses its existing picture cache.

**Checked:** In five paired full-library Windows runs per size, duplicating 512 visible placements to 1,024 reduced the median next frame from 66.7 to 5.4 ms. Duplicating two to four took 0.3 ms, down from 0.6 ms. All ten pairs matched in the board pixels, ordered saved items and selection counts. The Windows board group passed 96 checks, including full-size export and updates to linked paper.

<!-- docs/PERFORMANCE.md -->
append:
## Round 649: Reference Board image duplication

An 800×600 CPU viewport displayed a grid of 8×8 placements referencing one image. Five alternating baseline/candidate pairs per size measured the first frame after duplicating the selected items. The board now skips per-placement file canonicalization when its full-resolution export override table is empty.

| Placements before → after | Previous median frame | New median frame |
|---|---:|---:|
| 2 → 4 | 0.6 ms | 0.3 ms |
| 512 → 1,024 | 66.7 ms | 5.4 ms |
