## 2026-11-26 (round 645): Faster texture export and assignment

**Asked:** Measure complex material, texture, paint and bake tools and preserve their outputs.

**Changed (task 0627, ADRs 0056 and 0082)**
- Exporting the separate texture set with Assign now evaluates and writes the seven maps once. The material uses the paths returned by that export.
- The PNG writer recognizes identical consecutive scanlines and uses the same filter and residual bytes as its full filter search.

- The screenshot regression test observes the asynchronous tray job and waits for it to finish with a bounded watchdog. Its synchronous branch still requires the file on the capture frame.

**Checked:** Three paired Windows release runs per size and worker setting. At 2048² the duplicate-export fix saved 2.14 s with 32 workers and 2.54 s with two. The PNG change additionally saved 195 / 178 ms on plain export. All seven PNGs matched. The seeded image control with unique rows showed small mixed timing differences and byte-identical output. The final screenshot fixture passed three isolated runs and all 4,021 editor checks per platform on Windows and Linux. The tools and remaining gaps are in [the performance report](docs/perf/0627-complex-texturing.md).

<!-- docs/PERFORMANCE.md -->
append:
## Round 645: export and assign a separate texture set

On a 2048² four-layer surface, eliminating the duplicate set write saved 2.14 s with 32 workers and 2.54 s with two. A separate comparison measured an additional 195 / 178 ms saving on plain export from recognizing repeated PNG rows. All seven files matched between each pair. See [the workloads, controls and inventory](perf/0627-complex-texturing.md).
