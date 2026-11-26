## 2026-11-26 (round 633): Bounded reads for large binary point-cloud PLY files

**Asked:** Reduce peak memory when importing a large binary point cloud from disk.

**Changed (task 0615, ADR 0147)**

- Read fixed-width binary point-cloud PLY records from disk in batches. Check the vertex range against the file length before allocating the decoded cloud.
- Reuse the PLY decoder for disk batches and borrowed input. Small files, ASCII and mesh PLY files use the existing byte importer.

**Checked:** Generated test cases cover both byte orders, mixed-width and padded records, skipped elements, batch boundaries, degree-4 SH warnings, malformed sizes, dispatch and the editor import command. The three affected groups passed 7,909 checks on dependency-free Windows, 7,991 on Windows with Blender libraries and 7,914 on Linux. Quick splat stress reported zero problems across 2,100 damaged files.

<!-- docs/PERFORMANCE.md -->
append:
## Round 633: Importing binary point-cloud PLY files from disk

On a generated 236.5 MiB, million-point degree-3 PLY, bounded disk reads reduced the dependency-free Windows import peak from 470.6 to 242.0 MiB on two physical cores and from 470.7 to 242.1 MiB on four. Through the headless editor import and a subsequent frame, peak working set fell from 491.8 to 322.9 MiB and from 492.0 to 322.6 MiB, respectively. Median editor elapsed times changed from 751 to 708 ms and from 621 to 572 ms. Task 0615 records the five-pair ranges, the wide-record case, small-file timings and the warm file-cache method.
