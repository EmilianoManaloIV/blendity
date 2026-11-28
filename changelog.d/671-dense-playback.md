## 2026-11-27 (round 671): Faster dense rig playback in the CPU Scene view

**Asked:** Break down the remaining dense humanoid and quadruped playback cost, measure the instrumentation, and test a bounded improvement on the same scenes.

**Changed (task 0653)**

- The scalar PBR renderer checks a point light's existing attenuation before normalizing its direction. In the measured dense CPU scenes, the combined editor's median playback frame fell from 32.94 to 31.02 ms for the humanoid and from 33.36 to 31.55 ms for the quadruped.
- The opt-in headless command `perf frames N scene-raster` reports CPU Scene renderer stages and the number of eligible fresh frames.

**Checked:** The combined editor reproduced all 15 reference Scene images and the corresponding fixed-pose geometry exactly. See `docs/perf/0653-dense-playback.md` for the paired measurements, workload settings and control results.
