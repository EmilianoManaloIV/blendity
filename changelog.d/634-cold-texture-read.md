## 2026-11-26 (round 634): Measure cold texture file reads on two cores

**Asked:** Determine whether avoiding size seeks for ordinary texture files improves the first complete Textures frame.

**Checked (task 0616)**

- On 2,000 small textures, four of five matched pairs favored the 4 KiB reader. Its median whole-frame gain of 4.8 ms fell below the 6.8 ms difference observed between baseline runs.
- With half the textures padded to 64 KiB, the prototype was slower in four of five pairs. First-frame pixels and all 2,000 loaded paths matched. The round records the measurements and rejects the prototype.
