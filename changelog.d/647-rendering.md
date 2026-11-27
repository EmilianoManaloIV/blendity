## 2026-11-26 (round 647): Measure complex rendering and skip empty camera bloom

**Asked:** Exercise rendering tools on complex scenes, measure user-visible cost and memory, and improve demonstrated bottlenecks while preserving the picture.

**Changed (task 0629):**
- Added a Windows benchmark that waits for completed camera renders, verifies saved output, and compares ordinary and restricted CPU affinity on a generated world with 600 parts and 70 lights.
- CPU camera Bloom checks whether any positive bright-pass sample survived thresholding and clamping. A completely black input can skip the blur pyramid and composite. A single surviving channel keeps the original path.

**Checked:** Six paired 960 × 540 F12 trials on a 600-part world at four logical CPUs reduced median worker time from 381.5 to 329.5 ms and editor completion time from 390.65 to 335.60 ms. All 36 PNGs and their decoded RGBA pixels matched, including the visible-glow control. Checked CPU and GPU viewport/render paths, native ray-traced shadows, Gaussian splats, 21 individual camera effects, MoonRay and image/sequence outputs. The report in `docs/perf/0629-rendering.md` gives the controls, coverage gaps and verification evidence.

<!-- docs/PERFORMANCE.md -->
append:
## Round 647: Complex viewport, path tracing, splats and camera output

See the [round 647 measurements](perf/0629-rendering.md) for the workloads, controls, backend participation and output checks.
