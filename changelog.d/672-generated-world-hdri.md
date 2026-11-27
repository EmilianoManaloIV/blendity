## 2026-11-27 (round 672): Generated grids in the World HDRI

**Asked:** Make the World HDRI use the generated image selected in the Lighting window.

**Fixed (task 0654)**
- A World set to UV Grid or Color Grid uses the same built-in texture as the image picker and materials. The selected grid supplies the sky and environmental lighting.

**Checked:** The Windows baseline preserved the generated selection on reopen but logged a file-read error. A disk HDRI changed 57,600 pixels and its rotation changed 51,233 pixels in the same fixture. Candidate verification is pending.

<!-- README -->
after: Find and apply Low, Medium or High Viewport Quality from the command palette.
The World HDRI picker can use a generated UV Grid or Color Grid to light the scene.

<!-- docs/USABILITY.md -->
after: | With Viewport Device on the GPU, a Sky or HDRI world
| Picking a generated image for the World HDRI showed the same background as an empty slot and reported a file-read error. | UV Grid and Color Grid render directly from the built-in textures and rotate with the environment (task 0654). |
