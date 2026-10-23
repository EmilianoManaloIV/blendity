# 0200: the views native on the GPU - the roadmap written up (tasks 0192-0199)

- **Status:** done (round 220)
- **Requirements:** the user's "Why is it that the view still runs on the CPU? Shouldn't it run on the GPU natively
  like Blender?" and "Having no GPU should have a fallback on the CPU. But most computers would have a GPU."
  (2026-10-23)
- **Decisions:** ADR 0061 (native GPU views, the CPU path the reference and the fallback) and its amendments
- **Model:** main session
- **PR:** #165 (documentation only, stacked on #164)

## Where it started
- With Viewport Device on Auto, the views' 3D already drew on the card (ADR 0036). But the picture came back to the
  CPU, which drew the overlays, the UI and the camera filters over it and presented it through the system.
- Per frame the card's own work was about 0.3 ms of a 3.5 ms GPU path: the rest was the copy back, the wait for it
  and the upload.
- Any of eleven features sent a whole view back to the CPU for the frame: see-through materials, axis materials,
  live procedural textures, toon maps and the hair highlight, Weight Paint's colours, lightmaps, probes and realtime
  GI, a camera filter, a transparent render, a picture kept in linear light, more than 16 lights, and Edit Mode's
  face highlight.

## The tasks

| Task | Round | PR | What changed | Measured (RTX 4070 SUPER, Ryzen 9 5900XT) |
|---|---|---|---|---|
| 0192 the overlays on the card | 211 | #156 | The grid, wires, dots, the selection outline and Face mode's dots drawn by the card with the picture; picking reads back only the pixels asked for | Edit Mode Face mode's overlays 3.2-3.6 -> 0.2 ms a frame |
| 0193 the card presents the windows | 212 | #157 | A Vulkan swapchain on each window (Win32, X11): a view drawn on the card never comes back; the CPU's UI goes up where it changed and is blended over | A view presented: 3.27 ms by the card, 3.69 by the system; the card's own part 0.65 ms |
| 0194 macOS | 213 | #158 | Decided no-go: Blender's libraries have the Vulkan loader but no MoltenVK, so under ADR 0001 macOS keeps the system's present and the CPU path | - |
| 0195 fallbacks I | 214 | #159 | Any number of lights, Solid and Wireframe shading, Weight Paint's colours, Transparent and Glass, axis materials on the card | The stress scene: Shaded 33.27 -> 27.07 ms, Solid 28.81 -> 25.07 |
| 0196 fallbacks II | 215 | #160 | Toon painted maps, the face shadow, inner lines, hatching, the hair highlight; live procedural textures (except very close up) | The toon stress scene 24.10 -> 11.19 ms |
| 0197 fallbacks III | 216-217 | #161, #162 | Lightmaps, realtime lightmaps and the probe volume; camera filters run over the card's picture; bloom from linear light | Baked Scene view 1.49 -> 0.69 ms, realtime lightmaps 4.16 -> 1.80; the filtered scene 44.53 -> 42.09 |
| 0198 the UI's 2D | 218 | #163 | Decided no-go for a batch renderer: the UI stays the CPU's. Presents run one frame behind; only changed tiles' columns go up | A 4K scroll 6.38 -> 3.41 ms in all; a whole 2560 x 1369 canvas 6.0 -> 3.0 ms |
| 0199 the default, said | 219 | #164 | Each window's device in Preferences; Auto's fall back said once; a device switch draws the views again; the chain's GPU leg; Blender's viewport timed | Blender 3.98-5.23 ms against Blendity 5.57-7.51 ms a window |

## The fallbacks, before and after

| A view with... | Before (task 0191) | After |
|---|---|---|
| More than 16 lights | the CPU | the card |
| Solid / Wireframe shading | the CPU | the card |
| Weight Paint | the CPU | the card |
| Transparent and Glass | the CPU | the card |
| Axis materials | the CPU | the card |
| Toon painted maps, the face shadow, the hair highlight | the CPU | the card |
| Live procedural textures | the CPU | the card, except a close-up whose texels are bigger than pixels |
| Baked lightmaps, realtime lightmaps, the probe volume | the CPU | the card (a probe volume over 262,144 probes stays the CPU's) |
| Realtime GI's per-pixel gather | the CPU | the CPU |
| A camera filter | the CPU | the card draws the 3D, the filter's passes run over its picture on the CPU; a retro filter's rasterizer stage (snapped vertices, affine UVs, texture rules, a screen door) stays the CPU's |
| A transparent render, a picture in linear light | the CPU | the card |
| Edit Mode's face highlight | the CPU | the card |
| The overlays | the CPU, over the read-back picture | the card |
| Presenting | the system, after a read-back | the card (Windows, Linux); the system on macOS |

## Against Blender's viewport (task 0199)
A whole 2560 x 1369 window drawn and shown, a frame. Blender 5.2.2 with its window, timed with `wm.redraw_timer`
DRAW_WIN_SWAP; Blendity with `--windowed-check`, the Scene view drawn every frame:

| Scene | Blender 5.2.2 | Blendity, the card | Blendity, the system presenting |
|---|---|---|---|
| The starter scene | 3.98-4.70 ms | 5.57 ms | 6.81 ms |
| 400 spheres, Solid | 4.34-4.39 ms | 7.42 ms | 8.68 ms |
| 400 spheres, Material Preview / Shaded | 4.41-5.23 ms | 7.51 ms | 8.62 ms |

Blender stays ahead: 1.2-1.4x on the starter scene, 1.4-1.7x on 400 spheres. Its UI is drawn on the card where Blendity's is
the CPU's (about 2 ms at this size); Blendity's card draws each sphere with its own draw call. Blender's Material
Preview row is flattered: EEVEE reuses its last sample when nothing moves.

## What stays on the CPU, and why
- **Realtime GI's per-pixel gather:** its voxel walk per pixel would be a port of the whole GI to shaders; the
  baked results (lightmaps, probes) are already on the card.
- **A retro filter's rasterizer stage:** snapped vertices, affine UVs and its texture rules are the point of the
  filter, and they are the CPU rasterizer's own behaviour.
- **A live procedural seen very close:** its texels would show as blocks on the card; the CPU evaluates it per pixel.
- **See-through toon materials, bloom over see-through surfaces, and probe volumes over 262,144 probes** (float
  indices past 2^24).
- **The UI's 2D:** measured in task 0198 and kept on the CPU: under 4 ms at 4K once presenting was fixed.
- **macOS:** the system presents; a Metal backend would be its own roadmap (task 0194).
- **Machines without a Vulkan card, and CI:** the whole window on the CPU, the same picture. Every picture the card
  draws is checked against the CPU's.

## Follow-ups
- **The card's first open after a shader change takes about 33 s** while its shaders compile (the views draw on the
  CPU meanwhile; a warm open is about 130 ms). Compiling them when Blendity is built would end the wait.
- Instancing the same mesh into one draw call, and the UI on the card, are the two remaining gaps to Blender's
  viewport numbers above.
