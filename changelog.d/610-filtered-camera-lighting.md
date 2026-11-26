## 2026-11-19 (round 610): Filtered camera lighting

**Changed (task 0592)**

- Supported Vulkan devices now produce display colour and linear light for bloom or exposure grading in one lighting draw. Devices and scenes that require separate passes retain that path.
- In the filtered avatar-world Play scene, median frame time fell from 24.82 to 22.78 ms on an RTX 4070 SUPER and from 16.20 to 15.38 ms on an RTX 3060 Ti. Both cards produced identical display, HDR, depth and ID buffers in the parity checks. Task 0592 records the repeat run, comparison method and portability checks.

<!-- docs/PERFORMANCE.md -->
append:
## Round 610: filtered camera lighting in Play

A Vulkan filtered camera can evaluate opaque lighting once for both display colour and linear light. In three within-process trials of the baked `avatarworld` Play scene, this reduced median editor frame time by 5.1–8.2%. Each trial interleaved 160 frames per path. Median frames on the RTX 4070 SUPER changed from 24.82 to 22.78 ms and from 24.59 to 22.69 ms on repeat. On the RTX 3060 Ti they changed from 16.20 to 15.38 ms. The 95th-percentile frame time also improved on both cards. Four separate whole-program runs recorded substantial GPU power-state variation and gave no consistent speedup. See the individual runs, controls and image-parity results in [task 0592](tasks/0592-filtered-camera-lighting.md).
