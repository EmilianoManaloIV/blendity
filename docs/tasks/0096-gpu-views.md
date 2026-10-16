# 0096: The views on the GPU, read back into the canvas (Viewport Device)

- **Status:** done
- **Requirements:** R-05, Q-01
- **Decisions:** ADR 0036 (new): the views' 3D on the GPU, the CPU renderer the reference and fallback
- **Model:** main session (Opus)
- **PR:** https://github.com/EmilianoManaloIV/blendity/pull/87

## Goal
Task 0095's spike said go. The user approved building it ("if it's more performant, then yes"). This first step puts
the views' deferred 3D on the GPU through route (A): read back into today's CPU canvas. Overlays, gizmos, picking, the
UI, camera filters' passes and the window's present stay exactly as they are. Route (B), a swapchain with no
read-back, comes next. This step is the one everything else builds on, and it changes the least at once.

## Scope
- **In: Preferences > Viewport Device (Auto / CPU / GPU).**
  - Auto uses the first Vulkan GPU, unless `BLENDITY_VIEWPORT_DEVICE=cpu` is set. The unit tests set it, so this
    machine and CI run the same code; the GPU's own tests ask for it.
  - The console has `viewport device auto|cpu|gpu`.
  - Saved in the prefs.
- **In: `Editor::render_deferred` draws on the GPU when everything the view shows is supported, else on the CPU.**
  The Profiler shows each view's device and, on the CPU, why.
- **Supported in this step:**
  - plain materials (`material_uniform`: no textures, procedurals or axis mapping), PBR, opaque, unlit, emission;
    one material per mesh;
  - sun, point and spot lights; the sun's shadow map (3x3 or the Low tier's 2x2 bilinear), cast and received as the
    CPU decides;
  - the world: Gradient or Color, any strength; the editor's own sky (no scene lights); a camera's Solid Color;
  - Standard, Filmic and ACES view transforms with exposure;
  - back-face culling as the view's option; double-sided surfaces;
  - any viewport resolution scale.
- **Falls back to the CPU** (named in the Profiler):
  - textures, procedurals, axis materials, toon, see-through materials, several materials on one mesh;
  - Edit Mode's face highlight, vertex colours (Weight Paint);
  - lightmaps, probes, realtime GI;
  - area lights;
  - Sky and HDRI worlds, OpenColorIO views;
  - a camera filter stack, a transparent F12 pass;
  - no GPU, or any GPU error (the view draws on the CPU that frame).
- **In:** `gpu::Raster` grows into the views' renderer:
  - a set of targets per view;
  - instance flags (unlit, receive and cast shadows, double-sided) and emission;
  - the world, the background and the view transforms in the lighting pass;
  - two G-buffer pipelines (back faces culled or not);
  - meshes updated in place, indices included (an Edit Mode drag re-splits quads), and removed when unused.
- **Out:** route (B) (task 0097); the unsupported list above (later tasks, by what the Profiler shows falling back);
  overlays on the GPU.

## Acceptance criteria
1. Scene, Game and Camera Preview views of a scene within the supported set match the CPU's pictures on covered
   pixels away from edges: mean under 1/255, 99 % within 3/255, ids exact. Picking works the same. Skipped without a
   GPU.
2. Each unsupported feature sends the view to the CPU with its reason, and the picture is then the CPU's exactly.
3. An Edit Mode drag on a supported mesh updates its GPU copy in place (no new mesh), and the picture follows.
4. Viewport Device survives a restart (prefs), and `BLENDITY_VIEWPORT_DEVICE=cpu` keeps Auto on the CPU.
5. Stress: `editor_render` frames with the GPU on and off, on this machine, recorded in PERFORMANCE.md.

## As built
- `src/editor/gpu_views.cpp` (Editor: `gpu_view_draw`, the reasons, the mesh cache, the slots), the hook in
  `render_deferred`, Viewport Device in Preferences, the prefs and the console.
- `gpu::Raster` grew from the spike:
  - slots;
  - instance flags and emission;
  - the world, the backgrounds and the view transforms;
  - OpenColorIO views through `colormanagement::view_lut`, the CPU's own table;
  - culling pipelines;
  - meshes updated and removed.
- OpenColorIO views had to be in from the start: the default scene shows AgX, so without them every view fell back.
- Tests: rounds 121 (depth now compared, which proves the culling) and 122. Stress `editor_render`:

| Laptop setting (`--laptop --quality low`), ms a frame, two runs | CPU | GPU (RTX 4070 SUPER) |
|---|---|---|
| moving an object | 16.63-16.85 | 4.05-4.10 |
| playing | 20.58-22.07 | 5.61-6.78 |
| Edit Mode: Move-gizmo drag, 50k faces | 43.68-46.57 | 24.68-25.05 |

  - A view the GPU draws renders its own shadow map; the CPU only fits its frame (`update_shadow_map(..., frame_only)`).
  - What is left of the drag is the CPU's own work: the render mesh moved in place, the cage overlays, and the
    sphere's vertices sent up.
- The GPU opens on a worker in the editor (68 ms here with the shaders cached), and on the spot in headless runs.
- F12 renders stay on the CPU (the reference).
- Review (Opus):
  - **Fixed, mirrored placements:** they cull the other side, with a Mirrored flag and a front-culling pipeline.
  - **Fixed, CPU work on GPU frames:**
    - the CPU no longer renders the shadow map for a GPU-drawn view;
    - depth comes back only when picking or a depth of field reads it;
    - the colour comes straight into the canvas when its rows are contiguous.
  - **Fixed, the mesh cache:** meshes on the card have a budget (a quarter of its memory, at most 1 GB), and removing
    one no longer waits for the device.
  - **Fixed, shadows:** casting follows update_shadow_map's any-slot rule.
  - **Fixed, reporting:** each view's reason is kept for Preferences; Solid / Wireframe are named as the CPU's; the
    views' statistics count what the GPU drew; an unknown `viewport device` value warns.
  - **Checked and left as is:** the review's ambient-light finding for the editor's own lighting. The CPU's fallback
    environment takes the LightingEnv's colours, as the GPU did; the new test shows it.
