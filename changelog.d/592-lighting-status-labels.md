## 2026-11-19 (round 592): Read Lighting status in a narrow pane

The Lighting window shows Manual or Automatic at the start of the update status. Hovering over the row reveals the full explanation and the current preview stage while the row shows live preview progress. Hovering over the Contribute GI summary shows the contributing object count, lightmap count and size, and baked object count.

Verified the painted labels and full hover text at 1024×640 and 1366×768. The full twelve-configuration HCI sweep matched its existing baseline.

<!-- README -->
after: lightmaps and probes, Gaussian splats (PLY, SPZ, glTF).
The Lighting window shows the active update mode first and reveals the full bake counts on hover.

<!-- docs/USABILITY.md -->
after: ## Interface conventions

In the Lighting window, the update status starts with Manual or Automatic so the active mode remains visible at tablet scale. Hover over that row for its full explanation and preview status, or over Contribute GI for the full lightmap and object counts.
