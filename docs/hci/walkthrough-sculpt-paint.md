# Cognitive walkthrough: sculpt > remesh > unwrap > bake mesh maps > paint > smart material > export (baseline, round 271)

Same conditions and columns as `walkthrough-model-uv-material-light-render.md`. The user: a ZBrush / Substance artist on a
pen.

| # | Step | Try | Notice | Associate | Progress | Finding | Fix |
|---|---|---|---|---|---|---|---|
| 1 | Enter Sculpt Mode on the sphere | Y | Y | Y | Y | Modeling Tools > Sculpt, the hint says what to do | - |
| 2 | Pick the Clay brush, change its size | Y | Y | N | ? | Size changes by `sculpt size +` or two Wheel slices; no key (Blender F, Substance / Photoshop `[ ]`), no numeric readout while adjusting (`keymap.cpp` has no brush actions) | 0261 |
| 3 | Mask a region with Ctrl | Y | ? | Y | Y | The mask darkens toward grey; under deuteranopia the polygroup hues neighbour each other (`sculpt.cpp:1452`) | 0261 (F7) |
| 4 | DynaMesh at 128 | Y | Y | Y | Y | One step, the status line tells | - |
| 5 | Quad Remesh to 2,000 | Y | Y | Y | ? | Runs on a worker: the hint says Esc cancels, there is no progress bar or time | 0253 (F2) |
| 6 | Unwrap | Y | N | N | Y | As step 7 of the modelling walkthrough; a remesh after painting would lose the paint and nothing warns of the order | 0255, the manual (0249 did) |
| 7 | Bake Mesh Maps | Y | Y | Y | ? | A worker again, no progress; refused with modifiers in a console warning after the click | 0252 (F1), 0253 (F2) |
| 8 | `surface new`, a Fill with an edge-wear mask | ? | N | ? | Y | The surface is made from the console or the Textures window; the Textures window is under Shading, not where a Substance user looks (Texturing layout helps) | 0261 |
| 9 | Paint Mode: paint colour and roughness | Y | Y | Y | Y | The material brush; the eraser end erases - but the eraser state is a ring colour only | 0261 (F7) |
| 10 | Change the brush size while painting | Y | N | N | N | No key, no HUD; `paint size` differs from `sculpt size` (no +/-) | 0261 |
| 11 | Drop a smart material from the shelf | Y | Y | Y | ? | Applied on a worker; the preview keeps the old image until it lands with no indicator | 0253 (F2) |
| 12 | Export the texture set for Unity | N | N | - | - | Console only (`export textures unity <dir>`); nothing in the window | 0257 |

**Baseline score** (an N in any of the four columns): 12 steps, **5 failing** (2, 6, 8, 10, 12). The step most likely
abandoned: 12 (export), which has no UI at all; the step most likely to frustrate: 2 and 10, the brush size without a
key or readout.
