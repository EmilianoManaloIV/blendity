## 2026-11-17 (round 561): The splat lesson walks through the renderers

**Asked:** the hub, from M3's Learn walk. The "Point clouds and Gaussian splats" section stopped after making test
splats.

**Changed (task 0543)**
- The Learn section now takes you through rounds 532-534:
  - orbit the splats;
  - put a Points cloud beside them;
  - look through the Main Camera in the Game view;
  - render with F12 on the CPU path tracer;
  - switch the Device to GPU Compute to render on the graphics card.
- Four new questions: Points against splats, why Points are missing from F12, why a splat lights the floor only in F12,
  and why the card's picture and the CPU's differ slightly.

**Checked:** the section's walk in a fresh editor: 6 Try-it steps, none warned, 6 questions. `splats` 387 checks, 0 failed, on Windows with Blender's libraries; console audit at the baseline.

**Review:** both camera steps looked up from under the floor (a negative pitch); the card step used Device Auto, which mixes the CPU in, and is now GPU Compute; F12 leaves Points out, which the text now says.
