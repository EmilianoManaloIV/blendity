## 2026-11-27 (round 662): Cold Textures startup in a mixed project

**Asked:** Check whether skipping unrelated filenames earlier makes the Textures window open faster in a project with mixed assets.

**Checked (task 0644):** In a fresh Windows release editor with 144 textures and 6,912 additional unrelated files, the median paired first-frame savings were 0.3 ms at default concurrency and 1.7 ms at two workers. Both fell below the predefined thresholds. The listed paths, panel pixels and saved edit matched. The round records the measurement and retains the original file discovery code.

<!-- docs/PERFORMANCE.md -->
append:
## Round 662: Cold Textures startup in a mixed project

An early filename filter saved a median 0.3 ms at default concurrency and 1.7 ms with two workers in a fresh Windows process on a project with 144 textures and 6,912 additional unrelated files. The experiment fell below its thresholds and was removed. [Round 662 results](perf/0644-textures-startup.md)
