## 2026-11-20 (round 623): Avoid per-file attribute queries in the Textures browser

**Asked:** Shorten the first usable frame when opening an existing library of 2,000 procedural textures.

**Changed (task 0605)**
- Use attributes returned by Windows directory enumeration to classify ordinary texture files. Preserve canonical path resolution for reparse entries and use the filesystem scan when the native search cannot open a directory.

**Checked:** Three interleaved production pairs per worker setting improved the median first open from 177.6 to 123.2 ms at default parallelism and from 242.6 to 193.4 ms at two workers. Both remain above the 100 ms target. The Windows reference passed all 17 focused checks. Windows libraries passed 3,204 assets and 3,868 editor checks. Linux dependency-free passed 3,166 assets and 3,860 editor checks on the combined tree.

<!-- docs/PERFORMANCE.md -->
append:
## Round 623: Windows Textures directory discovery

The median first usable frame with a complete 2,000-texture library took 123.2 ms at default parallelism (177.6 ms before) and 193.4 ms with two workers (242.6 ms before). The round removes 2,000 extra Windows attribute queries during discovery. Both settings remain above the 100 ms target. The median next frame took 3.7 and 3.8 ms respectively. The fresh-process CPU viewport runs used an uncontrolled OS file cache. [Task 0605](tasks/0605-cold-texture-directory.md)
