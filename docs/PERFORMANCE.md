# Performance, limits and efficiencies

All numbers come from `blendity_stress` (full run, about 130 s with Blender's libraries, which add the library-vs-fallback comparisons) on the development machine: **AMD Ryzen 9 5900XT (16 cores / 32 threads), Windows 11, MSVC 14.51 `/O2`**. Re-run it on your own machine:

```
dist/windows/blendity_stress            # full run → stress/results/report_<date>.md + .csv
dist/windows/blendity_stress --quick    # ~3 s (CI)
dist/windows/blendity_stress --only raster
```

The raw reports are in [`stress/results/`](../stress/results/): the first pass in `report_20261006_001100.md`, the rendering/UV/modeling pass in `report_20261006_091049.md`, and the libraries + bare-metal pass in `report_20261006_233746.md` (`latest.md`, about 130 s).

## Pass 10: viewport frame rate and big meshes in Edit Mode (tasks 0083-0092)

**Summary** (task 0093). `editor_render` on the laptop setting, ms a frame, raw (x 1.4 for the laptop):

| Frame | Before the pass | After 0092 | On the laptop (x 1.4) |
|---|---|---|---|
| idle | 0.76 | 0.77 | 1.1 |
| orbiting the Scene view | 9.36 | 8.04 | 11.3 (88 fps) |
| moving an object | 19.53 | 16.12 | 22.6 (44 fps) |
| editing a material colour | 17.96 | 15.66 | 21.9 (46 fps) |
| playing | 16.97 | 14.70 | 20.6 (49 fps) |
| Edit Mode: Move-gizmo drag, 50k faces | 97.75 | 33.09 | 46.3 (22 fps) |
| Edit Mode: G, 50k faces | 100.20 | 31.32 | 43.8 (23 fps) |

Pacing at a 60 fps cap with 13 ms frames: 30.6 → 16.6 ms between frames (95 % within 32.0 → 16.7); a present when
nothing changed 2.42 → 0.07 ms.

What the plan's targets came to:
- **ADR 0026's laptop targets** (60 fps orbiting, 30 fps editing on Low): met - orbiting 88 fps, editing 44-49 fps.
- **The plan's stretch, 60 fps while moving, editing materials and playing:** missed (44-49 fps on the
  laptop). What is left of a moved-object frame: the Scene view 7.8 ms (its shading 3.8, sky 0.5), the Game view
  6.3, the Camera Preview 1.3, the UI 0.8, the shadow map 0.5.
- **Edit Mode drags on 50k faces, 30 fps:** missed (22-23 fps on the laptop, from 7). What is left: the Scene view
  drawing the 50k-face mesh flat with its cage (19.2 ms, the cage 4.7), the Game view (11.2 ms), the render meshes
  moved in place (5-7 ms, inside the views' gathering of draw items), the shadow map (3.7 ms), the UI (2.7 ms).
- Pictures: the same throughout; the 8-wide shader (0091) the same on screen, its colours before display within
  1.5e-5. Verify mode stayed at 0 stale pictures in every task.

Honest negatives: the Realtime GI drag in Automatic mode (71 ms, the lighting update itself); the 8-wide shader's
gain is held back by the per-pixel shadow lookup and end; 0092 saved 0.07 ms. Not done: reusing the sky between
frames (it would change background pixels slightly), BVH face picking and snapping (not seen in the drag's numbers).

The laptop setting (`blendity_stress --laptop --quality low --only editor_render`, and `--only present`, run on the
development machine on 2026-10-15; raw times, multiply by 1.4 for the laptop's clock). `editor_render` rows, ms per
frame:

| Frame | Before (task 0083) |
|---|---|
| idle | 0.76 |
| orbiting the Scene view | 9.36 |
| moving an object | 19.53 |
| editing a material colour | 17.96 |
| playing | 16.97 |
| Edit Mode: Move-gizmo drag of a vertex, 50k faces | 97.75 |
| Edit Mode: G, 50k faces | 100.20 |

Where the gizmo drag goes, per frame: two render meshes built from scratch (38.6 ms: the Scene view's flat cache and
the Game view's smooth one), the drag's writes 24.5 ms (`repair_moved_rings` scans every face), the shadow map 7.5 ms
(the big mesh casts), the Edit Mode overlay 8.7 ms, one edge table 1.1 ms. G adds a whole-mesh copy (2.7 ms) a frame.

`present` (a real window, 2560 x 1369 at this desktop's scale): 2.40 ms a present on average, 9.34 ms at the 95th
percentile. **Pacing:** a 60 fps cap with 13 ms frames (present included) gave 30.6 ms between frames (95 % within 32.0), with no input arriving: the loop's
waits are rounded up to Windows' 15.6 ms timer, so a frame that leaves less than a tick waits a whole one more.

**Task 0084** (`--only present`, same window):

| | Before | After |
|---|---|---|
| 60 fps cap, 13 ms frames: between frames (mean / 95 %) | 30.55 / 31.98 ms | 16.58 / 16.70 ms (`FramePacer` + a high-resolution timer) |
| the same, the high-resolution timer alone | - | 16.56 / 17.61 ms |
| presenting while a Scene-view-sized block of rows changes | 2.42 / 9.38 ms (all of it) | 0.68 / 0.70 ms (the changed bands, hashing included) |
| presenting when nothing changed | 2.42 / 9.38 ms | 0.07 / 0.08 ms |

The full present's long tail (9 ms at the 95th percentile) went with it: smaller copies don't wait as long on the
desktop compositor.

**Task 0085** (per-view waste): a moved-object frame's own setup around the rasterizer was measured first - draw
items 0.07 ms, scene hashes 0.03 ms (three) - so only what showed was changed: the sun's shadow map no longer clears
and writes an ids plane (0.68 → 0.48 ms a moved frame), and a view checking its voxel GI grid no longer allocates a
column pyramid to do it. Honest negative: the Realtime GI drag in Automatic mode stays at 71 ms; that is the
automatic lighting update itself (its Camera Preview and its UI-side update), not the grid.

**Task 0086** (ADR 0034): the render mesh follows a positions-only edit in place. The Move-gizmo drag on 50k faces:
98 → 69 ms a frame (two render meshes moved in place, 10.3 ms, instead of built, 38.6 ms; no edge table); G 100 → 67
ms. What is left of the drag: its writes, 25.8 ms (ring repair, task 0087), the shadow map 7.0 ms, the overlay 7.4 ms.

**Task 0087**: ring repair looked edges up in a map of the whole mesh it built every frame; per-vertex face lists kept
per topology replace it (the drag's writes 25.8 → 1.6 ms). G reuses its copy (one copy in about ten frames). During
any drag the overlap check and the lighting key wait. The Move-gizmo drag on 50k faces: **43 ms** a frame (98 before
the pass), G **42 ms**. What is left: the Scene view 24 ms (the 50k mesh's raster and shading, its Edit Mode overlay
7.5 ms), the Game view 16.5 ms, the render meshes moved in place 9 ms, the shadow map 6.9 ms.

**Task 0088**: a material that is the same everywhere but for the normal is evaluated once per triangle (the deferred
shader's setup), each pixel taking it with its own normal - bit for bit the same picture. The Scene view's shading on a
moved-object frame 5.12 → 4.53 ms, the Game view's 5.53 → 5.08. And the shadow map keys on a caster's shape (the same
for an edited mesh's flat and smooth caches), so a drag renders it once a frame, not twice. Laptop setting, ms a frame:

| Frame | Before the pass | After 0088 |
|---|---|---|
| moving an object | 19.53 | 17.23 |
| editing a material colour | 17.96 | 16.49 |
| playing | 16.97 | 15.53 |
| Edit Mode: Move-gizmo drag, 50k faces | 97.75 | 38.63 |
| Edit Mode: G, 50k faces | 100.20 | 37.01 |

**Task 0089**: the Edit Mode cage (100,000 edges and 50,000 dots of the 50k-face mesh) is placed in the world once per
vertex and recorded in parallel batches, in order: the overlays 7.4 → 4.5 ms a frame, the gizmo drag 38.6 → 35.4 ms,
G 37.0 → 34.4 ms.

**Task 0090**: an in-place update splits only the faces with a moved vertex into triangles again and works out face
normals in parallel (bit-identical): the two caches 9.1 → 6.7 ms a frame; the gizmo drag 35.4 → 33.7 ms, G 34.4 →
33.0 ms. Honest negative: at ×1.4 that is about 47 ms on the laptop, short of the plan's 33 ms; the rest is the Scene
view drawing the 50k-face mesh flat (19 ms, its overlays 4.5), the Game view (11 ms) and the shadow map (3.5 ms).

**Task 0091** (ADR 0035): a plain material's pixels are lit 8 at a time with AVX2 (the same displayed pixels; colours
before display within 1.5e-5). Back to back in one run, off / on: moving an object 17.18 / 16.31 ms a frame, the Scene
view's shading 4.45 / 3.86 ms (75,728 pixels lit 8 at a time). After it: moving an object 16.35 ms, editing a material
colour 15.82, playing 14.88, the gizmo drag 32.90. Honest negative: the shadow lookup and each pixel's end
(reflections, ambient: an `exp2` each) stay one pixel at a time, so the gain is a third of the lights' share, not more.

**Task 0092**: below full resolution the Game view and the Camera Preview no longer scale their depth and ids up to
full size (only the Scene view is picked in). Honest result: a moved-object frame 16.16 → 16.09 ms; the colour's
upscale is most of the cost and stays.

## Pass 9: the laptop minimum (task 0075, ADR 0026 amended)

The minimum became the user's HP Envy 13.3" 2-in-1: an i7-1165G7 (4 cores / 8 threads, 28 W), 8 GB shared with Iris
Xe, 1920 x 1080 at 150 %. This machine (a Ryzen 9 5900XT, 16 cores) emulates it with `blendity_stress --laptop`: 5
threads, because 4 cores' 8 threads do about the work of 5 full cores (pass 8's `--min-spec` gave 8 *full* cores), and
the times multiplied by 1.4 for the laptop's sustained clock. `editor_render` at Low, ms per frame:

| Frame | 8 full cores (pass 8 setup), now | `--laptop` raw | `--laptop` x 1.4 | Target |
|---|---|---|---|---|
| orbiting the Scene view | 6.5 | 9.3 | 13.0 | 16.7 (met) |
| moving an object | 13.5 | 18.6 | 26.0 | 33 (30 fps; 60 not met) |
| editing a material colour | 12.9 | 17.8 | 24.9 | 33 |
| playing | 11.4 | 16.3 | 22.8 | 33 |

- Pass 8's two frames over 16.7 ms on 8 full cores (a material edit at 19.8, Play at 17.6) are now under it (12.9 and
  11.4) after the later tasks' work.
- The Low tier's views read shadows with a 2 x 2 bilinear PCF (4 weighted taps) instead of 3 x 3: about 0.6 ms a frame
  at laptop thread counts (3-4 %); the edge is smooth but narrower (about one texel against three).
- On the laptop, 60 fps while editing would need the views below 67 %, which looks soft on a 13" screen. Low keeps 67 %
  and accepts 35-40 fps while dragging; orbiting and idle stay at 60.
- Iris Xe reports about half the RAM as its own; the path tracer's scene budget now takes a quarter of RAM on
  integrated GPUs, so an 8 GB laptop's editor keeps room.

## Pass 8: the minimum machine (task 0050, ADR 0026)

The user's minimum is a 4-core / 8-thread CPU at about 3 GHz with 8 GB of RAM and a GTX 1050 Ti. It is emulated here
with a thread ceiling for the whole process (`blendity_stress --threads 8`; TBB's `global_control`, which Embree,
OpenImageDenoise and OpenSubdiv share). The clock difference is not emulated, so the targets keep some headroom.
`editor_render` (1600 x 1000, Scene + Game views + Camera Preview, 200 icospheres, a sun with shadows), ms per
frame, the fastest of four runs:

| Frame | 32 threads before | 8 threads before | 8 threads after, High (100 %) | 8 threads after, Low | Target (Low) |
|---|---|---|---|---|---|
| idle | 1.27 | 1.01 | 0.78 | 0.80 | 2 |
| orbiting the Scene view | 7.40 | 17.92 | 14.37 | 7.06 | 12 |
| moving an object | 18.42 | 47.16 | 36.11 | 15.49 | 16.7 |
| editing a material colour | 18.19 | 48.12 | 36.58 | 19.81 | 16.7 (**not met**) |
| playing | - | 39.26 | 33.78 | 17.62 | 16.7 (**not met**) |

What it took, in the order the profile showed it:

- **Viewport Resolution Scale.** The views' 3D is drawn at 67 % on Low (85 % on Medium) and scaled up bilinearly.
  Depth and ids are scaled nearest, so picking and overlays stay exact.
- **Cached shadows.** The sun's map was drawn again on every change. Now the casters that haven't moved for a second
  keep a map of their own, and the moving ones are drawn over a copy of it: 3.8 ms became 0.9 ms a moved frame at
  1024. The frame is snapped, so a drag keeps it. A material colour no longer redraws the map at all: its key holds
  surface type and Cast Shadows, not the version.
- **The sky last.** The background was evaluated for every pixel and then mostly shaded over: 2 ms a view. It is
  now drawn after the opaque surfaces, only where none landed, and only every 4th pixel where the sky is smooth.
  Polynomial atan2 / asin (2e-6 rad) replaced the library calls in every sky lookup, which is also each pixel's
  reflection.
- **Per-pixel footprints** (task 0031's live textures) are computed only for materials that have a live texture.
- **Upscaling** the depth and id planes runs in parallel and with a column table: 1.66 ms became 0.45 ms.
- **Physics:** Jolt's pool is half of Blendity's threads instead of all but one (it had the machine twice over in
  Play).

Memory: the editor with 10,200 objects, two views and 30 undo steps peaks at **448 MB** (target under 3 GB). The
texture cache and undo now have byte budgets (a quarter and an eighth of RAM, each at most 2 GB).

The two frames still over 16.7 ms are a material edit and Play. Both render the Scene view, Game view and Camera
Preview, and are about 60 % deferred shading. The next step would be the shading itself (a cheaper shadow lookup
than 3 x 3 PCF on Low, SIMD across a tile).

### The GPU (task 0051)

| Device (stress `gpu`, 1280 x 720) | Samples/s | Longest submission (ms) |
|---|---|---|
| CPU, 8 threads | 6.5 | - |
| RTX 3060 Ti, compute kernel (a 1050 Ti's path, about 6 times faster than a 1050 Ti) | 60.9 | 98 |
| RTX 4070 SUPER, compute kernel | 115.5 | 106 |

Submissions used to take up to 64 samples at once: seconds on a slow card, past Windows' 2 s driver timeout.

## Pass 7: idle frames (task 0023)

Pass 6 left an idle frame at 2.1 ms and called it "almost all hash". A per-phase profile of 100 idle
frames in the same scene (2.03 ms each) said otherwise: the scene hash was about 0.3 ms of it (four
hashes: the Scene view's key, the Game view's, the Camera Preview's, the status bar's "Lighting out of
date"), and the rest was painting the UI. Same scene, same machine, Windows, release build:

| Frame (`editor_render`) | Before (ms) | After (ms) |
|---|---|---|
| Idle (nothing changed) | 1.98 | **1.27** |
| Mouse moving over the Hierarchy | 2.01 | **1.25** |
| Orbiting the Scene view | 8.49 | **7.40** |
| Moving an object | 20.99 | **18.42** |
| Editing a material colour | 19.22 | **18.19** |

| `editor_ui` | Before | After |
|---|---|---|
| Idle frame, 1,000 mesh objects | 1.48 ms | **1.11 ms** |
| Idle frame, 10,000 mesh objects | 4.82 ms | **2.74 ms** |
| Scene hash, 10,000 mesh objects | (not measured apart) | **0.46 ms** memoised, 0.56 from scratch |

### What was found
- **The hash's cost was the component lookups, not the mixing.** Each object ran about fifteen
  `get<T>()` loops a frame (a `dynamic_cast` per component per type), three of them inside the three
  `evaluated_key` calls. A `Component::render_role()` virtual call replaces them, the three keys come
  from one pass, and each object memoises the hash of its world matrix, flags and materials on a
  transform version (ADR 0015). The hash at 10,000 meshes is 0.46 ms; the dynamic parts (lights,
  probe volumes, modifiers) are still hashed every time, so the memo can't go stale on them.
- **Every icon was rasterized on every draw** from anti-aliased lines, triangles and circles, tens of
  microseconds each, and a frame draws dozens: the dock's tab bars alone cost 0.38 ms. An icon is now
  rasterized once per size into a coverage mask and blended through it (`ui::Context::draw_icon`).
- **2.5 million pixels of fills nobody saw:** the frame cleared the whole framebuffer, every window
  painted its whole content panel, and the Scene and Game views painted theirs again with their
  pictures. The frame now fills only the border strips around the dock area (the splitters are filled
  by the dock), and the dock paints no panel behind the two views.
- **The Project window listed folders from disk every frame** (`fs::list` per visible folder and once
  more per folder to know whether it has children): 0.32 ms. The listings are kept with the entries
  and refreshed every 2 s or on a change.
- **`loaded_material_assets()` copied and sorted the library every call**, twice a frame. It is
  rebuilt only when the library's entries change.
- **`Canvas::circle` walked the circle's whole bounding box** (4 r² square roots for a ring of about
  2 π r t pixels). It walks the ring's rows now.

### Task 0024: the render-frame waste
The spec's targets for the frames that really render (orbit ≤ 8.5, move ≤ 21, material edit ≤ 22,
play ≤ 13 ms) were already met by the painting fixes above. The waste it listed was removed anyway, and
measured as noise: one filter stack per camera instead of a `std::function` per pass per render, one
internal filter image per view instead of a shared one reallocated by every differently sized view, the
rasterizer's per-item scratch kept between flushes, a material's textures resolved once per flush
instead of a version check and an atomic read per pixel, and the sun's shadow casters copied only when
the map is rendered. The lesson: in these frames the time is the rendering itself (the Scene view's
raster at 5.9 ms of an orbit frame), not what surrounds it.

### Honest negatives (pass 7)
- **The idle frame is 1.3 ms, not the 1.0 the task asked for.** What is left is legitimate drawing:
  the Inspector (0.13 ms for a Camera), the Scene view's gizmo, view gizmo, icons, Camera Preview and
  Statistics overlay (0.4 ms together), the remaining panel fills, the Game view's blit. Going further
  means not redrawing the UI when nothing changed, which is a bigger change (and in the editor itself
  an idle frame is only drawn on input, a redraw request or a continuous-redraw reason).
- **The hasher's mixing stayed a byte at a time.** The lights' and the world's component hashes are
  part of `lightmap_scene_key`, saved with every bake; a new formula would have marked them all out of
  date. It is a few microseconds a frame.

## Pass 6: editor frames (task 0011)

`blendity_stress --only editor_render` draws the frames an editor spends its time on, in a working
scene:
- **Scene:** 200 icospheres, a floor, a sun with shadows, a 50k-face sphere.
- **Layout:** "2 by 3", so the Scene and Game views are both visible.
- **Camera Preview:** on (the Main Camera is selected).
- **Size:** 1600×1000, headless.

Before and after, Linux, dependency-free build, same machine:

| Frame | Before (ms) | After (ms) | What it renders now |
|---|---|---|---|
| Idle (nothing changed) | 47.3 | **2.1** | nothing: the three views are copied back |
| Mouse moving over the Hierarchy | 44.2 | **2.1** | nothing |
| Orbiting the Scene view | 43.6 | **9.9** | the Scene view; no shadow map |
| Moving an object | 45.1 | **24.7** | three views and one shadow map |
| Editing a material colour | 45.4 | **27.0** | three views and one shadow map |
| Playing | 32.5 | **14.8** | the Scene and Game views (the preview hides in Play) |

### What was found
- **The shadow map was never cached.** `docs/PERFORMANCE.md` and the stress note both said it was. It
  was rendered on every shaded render, up to three times a frame, at about 8 ms each. It is now kept
  per caster set, two entries (ADR 0009).
- **Any input re-rendered every visible view.** A mouse move over a panel, a caret blink or a tooltip
  timer was enough. A view now reuses its picture while a content hash of what it shows is unchanged
  (ADR 0009). A verify mode re-renders and compares, and the whole unit suite runs clean in it.
- **Clears were serial:** about 12 bytes a pixel on one thread. They now run as one parallel pass, and
  the visibility plane exists only for deferred shading.
- **Overlay lines and points were drawn one by one.** The grid alone is about 2,900 lines. They are
  now binned into 32-row bands drawn in parallel. Each pixel still sees them in order, so the picture
  is bit-identical. Overlays went from 1.15 to 0.77 ms in a normal frame, and from 17.5 to 12.4 ms with
  a 50k-face Edit Mode cage.
- **The idle cost with many objects is the scene hash:** 1.9 ms at 1,000 meshes and 5.1 ms at 10,000
  (`--only editor_ui`).
- **Edit Mode on a 50k-face mesh is still slow (about 280 ms a change).**
  - Rebuilding its render mesh costs 13–17 ms and its tangents 28–32 ms.
  - Most of the rest is the overlapping-vertex check. It runs when the mesh changes outside a drag (a
    real gizmo drag skips it).
  - A positions-only mesh update and a cheaper overlap check are the follow-up; they need an ADR
    because they change what a mesh version means (ADR 0003).

## Pass 5: Push/Pull on odd meshes

`blendity_stress --only pushpull` runs `meshops::push_pull` on 26 awkward meshes:
- **Shapes:** cylinders with 32- and 128-gon caps, a cone apex, UV and ico spheres, a torus, an L-shaped and a star-shaped (concave) prism, a non-planar face, a sheared box, a wedge, a subdivided cube, a cube with a tunnel, and a thin wall.
- **Broken or unusual topology:** an open quad and grid, a cube with inside-out normals, unwelded faces, collinear corners plus a sliver, and a non-manifold fin.
- **Scale and position:** cubes at 0.1 mm and 10 km, and one 200 km from the origin.

Every face is tried, plus random groups of 2 - 6 faces, the whole mesh and disconnected pairs. The distances run from 1e-9 to 1,000 x the mesh size, exactly at and around the hole and join limits, and include NaN and infinity, each with and without Ctrl (new face). Every result is checked for:
- bad indices, NaN and repeated corners;
- a closed mesh coming out open;
- volume moving the wrong way, or the solid turning inside out;
- new zero-area faces.

A second part drives the interactive operator in a headless editor: P, a mouse sweep through the snaps, then a click, Esc or a right-click. Undo or the cancel must restore the exact mesh.

| Round | Problems in ~27,000 operations | Fixed |
|---|---|---|
| First run | 7,088 | — |
| Non-finite distances refused | 3,344 fewer | NaN / inf filled the mesh with NaN |
| Neighbour-stretch cleanup kept shared corners | 347 open meshes → 0 | a T-junction where a stretched face dropped a corner its neighbour still used |
| Stop at the nearest geometry behind / in front | 704 inside-out → 64 | pushing a whole side past the far side inverted the box |
| Weld new vertices, cancel twin faces | 2,390 zero-area faces → 175 | hole cuts landing exactly on existing corners duplicated them |
| Ignore the region's own neighbours as "in front" | 48 collapses → 0 | a 5-face selection "joined" onto its own neighbour and vanished |
| Sweep the whole outline, not just corner rays | tunnel cases → 0 | a face over a tunnel was pushed straight through it |
| Corners slide along leaning neighbours | 80 inside-out → 0 | a wedge's slanted face pushed in sent walls out through the base |
| Precision-aware tolerances, relative Newell normals | 199 far-from-origin → 2 | 200 km from the origin floats step 2 cm; normals summed raw coordinates |
| Holes that leave slivers fall back to stopping | 46 → 0 | needle triangles from cuts on curved faces |
| **Now** | **2** | both on the cube 200 km from the origin (below) |

The interactive part: every Push/Pull on every mesh was undone or cancelled back to the exact mesh, at about 7 ms a frame (1000 x 700, headless).

### Honest negatives (pass 5)

- **Some selections are refused rather than guessed.** About 6% of the attempts are refused, almost all of them because the faces around the selection lean over it and no slide keeps every neighbour flat. Examples are a whole band of faces, or a group whose corners touch three or more unselected planes. Refusing beats a broken mesh, and the cursor shows the reason.
- **200 km from the origin** a 1 m cube is only about 50 float steps wide, and 2 of 26,620 results still came out open. Model near the origin, as in every float-based editor.
- `push_through` itself still leaves slivers on some curved meshes. Push/Pull doesn't show them, because it falls back to stopping at the far side, but Alt+P (Push Through on its own) can still produce them.

## Pass 4: GPU render devices (Vulkan)

`blendity_stress --only gpu` renders the same frame on every device: 400 textured spheres on a checker floor, with sun + sky, 4 bounces and 128 samples at 1280×720. The GPUs are an RTX 4070 SUPER and an RTX 3060 Ti in the same machine. A unit test checks that every device, and CPU + GPU together, matches the CPU render (mean within 3%, per-pixel difference under 6%). Two runs agreed within 1%.

| Device | samples/s | vs CPU |
|---|---|---|
| CPU (Embree, 32 threads) | 21.0 | 1.00× |
| RTX 4070 SUPER, compute shader (Blendity BVH) | 157 | 7.5× |
| RTX 4070 SUPER, ray tracing hardware | 1,150 | **55×** |
| RTX 3060 Ti, compute shader | 80.5 | 3.8× |
| RTX 3060 Ti, ray tracing hardware | 347 | 16.5× |
| Both GPUs | 1,268 | **60×** (samples split 96 : 32) |
| CPU + both GPUs | 1,070–1,160 | 51–55× (CPU takes 1–2 samples) |

### Efficiencies found and applied

- **Ray tracing hardware is 7.3× the software BVH** on the same GPU (`VK_KHR_ray_query`, one BLAS per unique mesh plus a TLAS). The software-BVH kernel stays for GPUs without RT cores.
- **Optimised SPIR-V, cached on disk.** An early build skipped shaderc's optimiser: the kernel compiled in 70 ms and seemed just as fast. That was only because the driver's shader cache was already warm. On a cold cache, NVIDIA's driver took **~105 s per GPU** to build the pipeline from unoptimised SPIR-V, so the first GPU render took 212 s. Optimised SPIR-V fixed two things:
  - The driver builds the pipeline in 2.7 s.
  - The software-BVH kernel runs **1.7× faster**, and the RT kernel 1.6×.

  shaderc's optimiser takes 6 s, so its output is cached in `~/.blendity/cache` (keyed by a hash of the source and options). The editor starts compiling in the background as soon as Device is set to GPU Compute, and each GPU builds its pipeline on its own thread. First GPU render on a new machine: **212 s → 9.3 s**. After an update: 3.4 s. After that: 0.2 s.
- **Multi-GPU went from slower than one GPU to faster** through four changes:
  - Guided self-scheduling batches replaced fixed batch sizes.
  - Buffers are merged only at the 100 ms display interval rather than after every batch.
  - Albedo and normal are read back only when the denoiser runs.
  - Readback moved into **host-cached** memory. Reading 15 MB from the default uncached host-visible memory took about 50 ms, a big share of a 100 ms frame.
- **The CPU stops claiming samples** once the GPUs would finish the remaining ones sooner, so a slow CPU sample no longer holds up the frame.

### Honest negatives (pass 4)

- **Adding the CPU to two RT GPUs is 9–16% slower** in this scene. One CPU sample takes 47 ms while a GPU sample takes under 1 ms, so the frame waits for the CPU's last sample. The CPU still helps alongside GPUs without ray tracing hardware, or a single slow GPU. As in Blender, it is a tick box, and off is the right choice with fast GPUs.
- **The second GPU adds only 10%**, not the 30% its standalone speed suggests. The whole 128-sample frame takes about 100 ms, so each batch's submit-and-wait and the final readback are a large fixed share. Overlapping batches across two command buffers is the next step.

## Pass 3: Blender's libraries and bare-metal optimisation

This pass linked 16 of Blender's prebuilt libraries (each optional, each compared with Blendity's own fallback in the stress suite). It then hand-optimised the hottest CPU paths with SIMD and by skipping work. Same-session A/B builds were used for the before/after numbers below, because background load on this machine drifts by ~20% between runs.

### Blender's libraries vs Blendity's own code

| Library (Blender uses it for) | Blendity's fallback | Result |
|---|---|---|
| **Embree** (Cycles CPU BVH) | two-level SAH BVH | **2.0–2.9× more rays/s** (64–95 vs 22–47 Mrays/s); BVH builds 1.2–2.3× faster |
| **OpenImageDenoise** (Cycles denoiser) | À-Trous wavelet filter | RMSE vs a 512-spp reference at 4 spp: noisy 0.096, À-Trous 0.073, **OIDN 0.046**. 2–2.3× slower (250 ms at 720p), so the viewport uses À-Trous while sampling and OIDN once converged |
| **oneTBB** (BLI_task) | thread pool | **6.5× cheaper dispatch** for 10,000 small `parallel_for` calls; streaming and imbalanced loops are even |
| **Eigen** sparse Cholesky (UV unwrap) | block-Jacobi CG | LSCM 1.2–1.8× faster on islands of 4k+ faces (slower on tiny ones); a 262k-face single island becomes practical (11 s) |
| **OpenSubdiv** (Subdivision Surface) | direct Catmull-Clark | Identical meshes, but **3–6× slower** here: OSD's topology refinement pays off on animated, re-evaluated meshes, not one-off subdivisions. Kept off by default |
| **OpenColorIO** (colour management) | built-in curves | AgX / Filmic / PBR Neutral baked into 97³ LUTs: 6.8 ms per 1080p frame vs 17 ms for the exact OCIO processor, 99% of pixels within 1/255 |
| **Manifold** (Boolean, Manifold solver) | none | Boolean modifier (exact volumes in the unit tests) |
| **meshoptimizer** | none | Decimate modifier |
| **Jolt** | O(n²) sphere physics | **13.9× faster at 2,000 bodies**; 5,000 bodies in 2.2 ms. Slower below ~200 bodies (fixed per-step overhead) |
| **libjpeg-turbo / libpng** | Blendity's decoders | 4K decode **3.2× / 1.8× faster**; PNG output 22 → 10 MB with adaptive filters + zlib |
| **Zstandard** (.blend compression) | none | Text scenes **51× smaller**, still loading at ~60 MB/s |
| **Open PGL** (Cycles path guiding) | off | Learns correctly (10× more guided samples toward the light), but **0.45–0.49× efficiency** in the doorway test scene: lookups cost more than the noise removed. Opt-in, as in Cycles |

### Bare-metal and algorithmic wins

| Bottleneck | Fix | Before → After (same session) |
|---|---|---|
| **Rasterizer setup**: every object's vertices were transformed *and lit* (Gouraud) before culling, and every triangle did 3 perspective divides | Small-triangle cull (a bounding box with no pixel centre is dropped before it's stored); vertices transformed where their triangles are assembled, divided once per vertex, and **lit only when a surviving triangle needs them**; an **AVX2 kernel rejects 8 triangles per step** (gathers + vector compares, no FMA so it rejects exactly what the scalar code would) | 21M tris (16k objects, 5% visible): **174 → 34 ms** (60 → 26 ms in the quiet full run). 1 thread: 1,953 → 408 ms. 60 FPS limit at 720p: **~2.6M → ~5.2M triangles** |
| **Display encode** (every viewport pixel, path tracer resolve, sky background): `pow()` per channel for sRGB | An exact table indexed by the float's exponent + 7 mantissa bits, storing each bucket's value and the one threshold where rounding steps up (correctly rounded for every float in [0, 1]), and **SSE4.1 (4 px) / AVX2 + FMA (8 px) tone-curve kernels** picked at run time from `cpu::features()` | 1080p Filmic, 1 thread: **97.8 → 6.3 ms (15.7×)**; all threads: 6.5 → 0.65 ms |
| **Deferred shading**: barycentrics, world position and normal matrix work recomputed for every pixel | Per-triangle setup once (screen-linear planes relative to vertex 0 for precision), reused while neighbouring pixels share the triangle; `pow()` removed from GGX Fresnel and the sky gradient | 4K shading pass: ~10% faster (34 → ~30 ms) |
| **À-Trous weights**: `pow(n·n', 64)` per tap | six squarings | (inside the denoiser timings above) |

### Honest negatives (pass 3)

- **Packing objects into fewer setup chunks did nothing on its own.** One chunk per object looked expensive (16k tile tables of ~4 KB, walked by every tile). Fixing it moved no time, because the real cost was the per-vertex lighting found by timing each stage. The packing stays, since the later fixes build on it.
- **Deferred shading only got ~10% faster.** After the per-triangle setup, the cost is spread evenly over material evaluation, lights and environment (~250 cycles each per pixel). Going further needs an 8-pixels-at-a-time (ISPC-style) shader, which wasn't done.
- **Single-precision `powf` isn't monotonic** at 8-bit rounding boundaries, so a table built from it inherits its glitches. The table is built from the double-precision curve instead. Plain `powf` itself misrounds about 1 float in 2.5 million.
- **Rays per second were overstated in pass 2.** The old counter estimated rays per path instead of counting them; it's now exact. The real built-in BVH rate is **22–47 Mrays/s**, not 69–231. The pass 2 table below keeps the original figures, marked as such.
- **The machine's load matters as much as many optimisations.** Unrelated runs differed by ~20%, so every before/after above is an A/B of two binaries in the same session.

## Pass 2: rendering, UVs, texturing and modeling tools

The second pass added five suites (`shading`, `pathtracer`, `uv`, `textures`, `modeling`) and three fixes came out of them.

### Limits found

| Subsystem | Result (on this machine) |
|---|---|
| Deferred PBR (visibility buffer + one shading pass) | 256 spheres: **6.0 ms at 720p, 11.8 ms at 1080p, 39.8 ms at 4K**. Shading costs ~4 ns/pixel; Gouraud is 1.5–4× cheaper |
| Sun shadow map, 2048² | ~7.7 ms to render; the 3×3 PCF lookup adds 0.4–2.7 ms per frame. (This said "cached until lights or casters move"; it was not cached until pass 6, task 0011.) |
| Path tracer throughput | *As first reported:* 231 Mrays/s (20k tris) → 69 Mrays/s (21M tris). **Corrected in pass 3 (exact ray counting): 47 → 22 Mrays/s** with the built-in BVH, 95 → 64 with Embree. Ray cost still grows ~logarithmically with scene size |
| Texture sampling (1 thread) | closest 22–73, bilinear 10–29, trilinear 10–15 Msamples/s; drops at 4K when the texture leaves the cache |
| LSCM unwrap | a single 65k-face island in **1.3 s**; typical seam-cut islands (a few thousand faces) in milliseconds |
| Edit-mode tools on 1M faces | edge loop 18 ms, loop cut 122 ms, mirror 61 ms, solidify 155 ms, recalculate normals 449 ms |

### Efficiencies found and applied

| Bottleneck | Fix | Before → After |
|---|---|---|
| **Path tracer BVH**: one flat BVH over every world-space triangle, rebuilt whenever anything moved | Two-level BVH like Cycles instancing: one object-space BVH per unique mesh, cached across rebuilds by a content hash and built in parallel, plus a small top-level BVH over instances (`PathTracer::build`) | 21M instanced tris: **34.3 s → 15 ms**. 1,024 unique meshes (1.3M tris): first build **1,853 → 106 ms**, moving one object **→ 9.9 ms**. Ray throughput also rose by ~28% at 21M tris (reported then as 54 → 69 Mrays/s, an overcount; see pass 3) because the working set shrank |
| **LSCM unwrap**: every CG step multiplied by A and then Aᵀ, single-threaded, and stopped relative to the initial residual (a good initial guess made it work *harder*) | Assemble the normal equations once as 2×2 block-sparse rows, block-Jacobi preconditioner, rows split across threads for large islands, tolerance relative to ‖Aᵀb‖ (`uvops::lscm_island`) | 65k-face island: **11.8 s → 1.1–1.3 s (≈10×)**; 16k faces: 1.83 s → 0.22–0.26 s |
| **Edit-mode topology queries**: every tool built an `unordered_map<edge, vector<face>>` over the whole mesh | Vertex→face table built with a counting sort (CSR); an edge's faces are the intersection of two short lists (`EdgeFaces` in `mesh.cpp`) | 1M faces: edge loop **897 → 18 ms (50×)**, loop cut 951 → 122 ms (8×), solidify 1,028 → 155 ms (6.6×), recalculate normals 3,112 → 449 ms (6.9×) |

### Robustness

The input fuzzer **found a real crash** after the UV Editor and Render tabs were added: middle-clicking a tab closed it *during* the recursive dock drawing, which freed dock nodes still on the call stack (a use-after-free that had been latent since the first version). Tab closes are now queued and applied after drawing. To make the next one faster to find, `blendity_stress` now prints a symbolised stack trace on a crash (DbgHelp, Windows), `BLENDITY_FUZZ_TRACE=1` logs every frame's events, `BLENDITY_FUZZ_SEED=n` changes the sequence, and the fuzzer presses the new shortcuts too. Five seeds × 6,000 frames pass in a debug build with checked iterators.

### Honest negatives (pass 2)

- **Very large single UV islands are still CG-bound.** Iterations grow with the island's diameter, so a 65k-face island takes ~1.2 s where Blender's direct solver is faster. Multigrid or a sparse Cholesky would fix it; in practice, seams keep islands small.
- **Mip-map generation is single-threaded**: 330 ms for a 4K texture on load. It runs once per image, so it was left alone.
- **The first shadow-map render in a run is slower** (12 ms at 640×360 vs ~7.7 ms after): first-touch allocation of the 16 MB depth buffer.

## Pass 1: editor, scene graph and meshes

### Limits found

| Subsystem | Limit (on this machine) |
|---|---|
| Software rasterizer, 1280×720 @ 60 FPS | **~2.6M triangles/frame** with all cores (82k single-threaded). Peak throughput ≈ 205M tris/s at 21M tris |
| Fill rate | 935 Mpix/s at 3840×2160 (8.9 ms for 256 spheres in 4K) |
| Catmull-Clark subdivision | Level 10 of a cube = **6.3M faces in 0.34 s**, 192 MB. Level 11 (25M faces, ~770 MB) is stopped by the test's 25M-face safety cap |
| Scene graph | 100,000 objects: moving a root and re-reading every world matrix takes 1.8 ms (flat) / 6.9 ms (tree) |
| Hierarchy window | 100,000 rows expanded: **7.7 ms** per editor frame (virtualised drawing) |
| Merge by Distance | 2.46M vertices in 0.59 s |
| Undo | One step on a scene holding 49M vertices: **0.27 ms** (copy-on-write) |
| Robustness | 6,000 frames of random clicks/drags/keys (10,621 events): no crash |

### Efficiencies found and applied

Each row was a measurable bottleneck in the first stress run. The old implementation is kept inside the stress test so the comparison can be reproduced.

| Bottleneck | Fix | Before → After |
|---|---|---|
| **Edge table** (wireframes, smoothing, subdivision): global sort and an `unordered_map` both cost ~50 ns per edge | Counting-sort corners into buckets by their lower vertex index (CSR), then dedupe each small bucket with insertion sort. Linear time, cache friendly (`Mesh::edges`) | 1.57M faces: **437 ms → 51 ms (8.5×)**; hash map was 985 ms |
| **Scene save**: `ostringstream` + `printf("%.9g")` for every float | String builder + `std::to_chars` (shortest exact round-trip; `snprintf` fallback) | 1.2 MB mesh: **55 ms → 4.2 ms (13×)**; 1M floats: 248 ms → 30 ms |
| **Scene load**: `istringstream` + `getline` copies every line; `strtof` | Zero-copy line reader over the buffer + `std::from_chars` | Mesh data: **86 → 313 MB/s (3.6×)**; object-heavy files 43 → 59 MB/s |
| **Catmull-Clark**: global `std::sort` of 64-bit corner keys, serial assembly with `push_back` | Bucketed edges that also yield the corners per edge (no scatter), parallel face points, edge points, vertex points and output into presized arrays | Level 9 (1.6M faces): **133 → 62 ms (2.2×)**; level 10: 606 → 344 ms |
| Save/load round-trip drift | The loader no longer re-normalises unit quaternions (changed the last float bits) | save → load → save is now byte-identical (unit test) |

### Design choices the numbers confirm

| Technique | Where | Measured effect |
|---|---|---|
| Dirty-flag world matrices | `GameObject::world_matrix` | Deep chain of 10,000: **0.52 ms vs 1,438 ms (2,783×)**. Turns O(n²) into O(n) |
| Object-ID buffer picking | `Renderer3D` / `RenderTarget::ids` | 0.004 µs per pick vs 415 µs (AABB ray cast, 10k objects) vs 1.4 ms (brute force, 1k objects) |
| AABB rejection before triangle tests | picking | 28–33× over brute force |
| Spatial hash merge | `meshops::merge_by_distance` | 153k verts: **19 ms vs 1.79 s (93×)** |
| Copy-on-write meshes (Blender's implicit sharing) | `Scene::clone`, `mesh_make_mutable` | 500 heavy objects: **0.27 ms vs 417 ms and 1.46 GB** for deep copies |
| Tile-parallel raster | `Renderer3D::flush` | 12.2× over single-threaded at 21M tris |
| Back-face culling | raster setup | 1.65–1.7× fewer pixels/tris processed (no-cull is 70% slower) |
| 64 px tiles | `RasterOptions::tile_size` | Best of 16/32/64/128/256 (7.15 ms vs 9.6 ms at 16 px and 10.4 ms at 256 px) |

### Honest negatives (what didn't help much)

- **Exact row spans in the rasterizer** (`span_rows`) gain only **~1–5%** on icospheres, because their triangles are small and compact. The trick pays off on long, thin triangles, so it stays on, but it's not a big win here.
- **Streaming kernels don't scale with cores.** Transforming 8M vertices (≈190 MB of traffic) stops at **~2.2× from 3 threads onward**: it's memory-bandwidth bound. The 2M-vertex version that fits in cache reached 9.2× in the quick run. Lesson (GEA Vol. I 3.5): keep hot data small and touch it once.
- **Flat hierarchies** gain little from dirty flags (1.6–1.8×). The benefit grows with depth.
- Object-heavy scene loading is now limited by per-object work (component creation, a `std::map` per component), not parsing: ~3.7 µs per object.

## GPU first use (task 0037)
The first GPU render of a session builds the kernel's pipeline (2.4 MB of SPIR-V). On the user's machine a cold
driver cache took about 80 s, on the editor's thread. Now it opens in the background: with the open slowed to 3 s
by a test hook, the slowest editor frame was 17-30 ms and the preview moved to the GPU after 3.3 s. With the
driver's cache warm, the pipeline takes 84-141 ms without Blendity's pipeline cache file and 9-13 ms with it.

## Deforming modifiers in place (task 0041)
A deforming modifier's field change on a 50k-face mesh used to copy the mesh, run the stack, rebuild the render
mesh and run MikkTSpace: 56 ms. Now the topology-changing head of the stack is cached, the deformers re-run on a
kept mesh, and the render mesh moves its vertices in place (normals by the build's function, tangents from UV
gradients): 6.6 ms, 8.3 ms with sharp edges (smoothing fans) (stress `modifiers_ngon`).

## Next candidates

- **During an Edit Mode drag:** draw the Game view less often than the Scene view (it is a third of the drag), and
  the 50k-face flat raster of the Scene view itself.
- **The 8-wide shader's rest:** the shadow lookup (gathers) and each pixel's end (reflections, ambient) 8 at a time.
- **The sky behind the views:** reuse it while the camera stays (0.5 ms a view), if slightly different background
  pixels are acceptable.
- **Incremental overlap checks** after an Edit Mode drag (a 50k-face mesh changed every frame outside a drag
  costs 332 ms, the check included).

Done since the last list: SIMD in the rasterizer (pass 3, in triangle setup rather than the per-pixel edge functions, which the timings showed aren't the bottleneck), a direct sparse solver for large LSCM islands (Eigen), and a real broad phase for physics (Jolt).

1. SIMD vertex transform in the rasterizer's fused vertex stage (8 vertices per step).
2. 4-wide BVH traversal in the built-in path tracer, for builds without Embree (GEA Vol. I 4.10).
3. Reuse the path tracer's per-mesh BVH for Edit Mode face picking (currently linear in triangle count).
4. Parallel mip-map generation for 4K+ textures.
5. Replacing the per-component `std::map` in the scene loader with a flat vector.
6. A uniform-grid broad phase for the fallback `physics_step` (O(bodies²) without Jolt).
