## 2026-11-20 (round 617): Fresh-process Textures browser

**Changed (task 0599)**

- The Textures browser reads and decodes uncached procedural assets in parallel while retaining the existing path, cache and selection semantics.
- In three matched Windows CPU runs per build with 2,000 existing synthetic textures, median first open fell from 334.5 to 169.0 ms at default parallelism and from 351.0 to 244.7 ms with a parallelism ceiling of two. The following frame and same-process reopen took 3.5–3.9 ms. All first opens remain above the 100 ms budget.

- The combined Windows libraries build passed all 68 checks in the focused regression for row order, shared asset identity, repaired files, and unsaved edits.

<!-- docs/PERFORMANCE.md -->
append:
## Round 617. Existing Textures library in a fresh process

At 1600 × 900 with CPU viewport rendering, the median first open of an existing 2,000-texture library fell from 334.5 to 169.0 ms at default parallelism and from 351.0 to 244.7 ms with a parallelism ceiling of two. Three matched fresh processes per build at each limit used the same nested fixture. The OS file cache was uncontrolled and potentially warm. Following frames and same-process reopens took 3.5–3.9 ms. First open still exceeded the 100 ms budget. Independent checks verified the visible names and count, case-insensitive search, stable selection, and thumbnail pixels. [Task 0599](tasks/0599-cold-textures.md)
