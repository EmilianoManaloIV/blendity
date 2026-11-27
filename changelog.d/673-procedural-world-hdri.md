## 2026-11-27 (round 673): Procedural textures in the World HDRI

**Asked:** Use a project procedural texture selected in the Lighting window for the World environment.

**Fixed (task 0655)**
- Selecting a procedural output uses the same texture as the picker thumbnail and materials. The World refreshes its background and environmental lighting when the texture changes or a surface worker publishes new pixels.

**Checked:** On stock Windows the selected Bricks albedo reference survived reopening and its thumbnail changed after a layer edit. The World continued to render the empty background. Candidate verification is pending.

<!-- README -->
after: The World HDRI picker can use a generated UV Grid or Color Grid to light the scene.
A procedural texture selected for the World updates its sky and lighting when the texture changes.

<!-- docs/USABILITY.md -->
after: | Picking a generated image for the World HDRI
| A project procedural texture selected for the World showed a thumbnail but rendered an empty background. | The World renders the selected output and follows its edits and finished surface evaluations (task 0655). |
