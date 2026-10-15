# 0095: Spike 2 - a rasterising GPU view, two ways to the window (amends ADR 0013)

- **Status:** done (go)
- **Requirements:** R-05, Q-01
- **Decisions:** ADR 0013 (amended with the result)
- **Model:** main session (Opus)
- **PR:** (link when opened)

## Goal
ADR 0013's spike (task 0015) said no-go to a GPU viewport. But it stood in for a GPU view with the path tracer: one
sample of primary rays, float buffers read back over PCIe. A viewport draws triangles instead, so it was an upper
bound. Re-run today, the RTX 4070 SUPER came out at 0.82-0.87x and the RTX 3060 Ti at 1.07-1.29x, against a bar of
0.50x.

This spike measures what a viewport would really cost:
- a Vulkan graphics pipeline drawing the editor's scene;
- (A) read back into today's CPU window;
- (B) shown through a Vulkan swapchain, with no read-back.

It then decides go or no-go with the numbers. No viewport code is kept beyond the spike unless it is a go and the user
approves (CLAUDE.md, ADR 0013).

## Scope
- **In:** `gpu::Raster` (spike, `src/render/gpu_raster.*`, behind `BL_WITH_VULKAN`):
  - its own `VkDevice`;
  - render passes (Vulkan 1.2, as the instance asks for);
  - GLSL compiled by shaderc (the existing cache);
  - vertex and index buffers per render mesh;
  - instances (model matrices);
  - a depth-only shadow pass for the sun;
  - a G-buffer pass (normal, material, object id, depth);
  - a full-screen deferred pass lighting plain materials as `shade_deferred`'s uniform path does: sun with 3x3 PCF,
    point and spot lights, sky ambient, the view transform;
  - read-back of RGBA8 colour, depth and ids (A).
- **In, stress section `gpu_raster`** (kept out of `--quick` and CI), on the `editor_render` scene at 1080p, on each GPU:
  - the frame's GPU time;
  - a moved object (one instance changed);
  - an Edit Mode drag on the 50k-face mesh (its positions and normals uploaded);
  - (A) render plus read-back;
  - the CPU's two views for the same frame.
- **In, a windowed run (B)** (as `--only present`): a swapchain on the editor's window, the view drawn into it, and the
  UI canvas uploaded as a texture (only its changed bands) and drawn over. Timed per frame, against today's present.
- **In:** the picture against the CPU's for the same frame: mean difference, the share within 3/255, ids away from
  silhouettes.
- **Out:**
  - textures, procedurals, toon, area lights, GI, probes and camera filters (later tasks, if a go);
  - overlays and gizmos on the GPU;
  - any editor wiring.

## Acceptance criteria
1. `blendity_stress --only gpu_raster` runs on a machine without a GPU and prints the CPU row and a note.
2. With GPUs, it prints for each: the GPU time, (A) and (B) end to end, the moved-object and drag updates, the picture
   match, and the verdict.
3. The verdict is go if two views end to end cost at most 0.50x the CPU's on both cards, for (A) or (B).
4. The laptop is estimated from Iris Xe's published throughput and shared memory, and labelled as an estimate.
5. ADR 0013 records the result, either way.

## As built
- `gpu::Raster` (`render/gpu_raster.*`): a spike, its own device. It is reached through `render/gpu_vk.h`
  (gpu_device.cpp's instance, now with window-surface extensions, and GLSL compiled for any stage).
  `platform::native_handle` gives it the window (Win32 only).
- Stress `--only gpu_raster` (two runs, within 3 %):

| | CPU, one 1080p view (32 threads) | RTX 4070 SUPER | RTX 3060 Ti |
|---|---|---|---|
| GPU work: shadow / G-buffer / lighting | - | 0.05 / 0.15 / 0.05 ms | 0.08-0.09 / 0.21-0.24 / 0.09-0.10 ms |
| rendered, waited for | 14.33-14.56 ms | 0.37 ms | 0.50-0.57 ms |
| (A) plus colour, depth and ids read back | - | 2.67-2.70 ms (0.18-0.19x) | 5.71-5.78 ms (0.40x) |
| a moved object / a 50k-face drag, (A) | 14.3-14.6 ms | 2.62-2.64 / 2.78-2.79 ms | 5.64-5.68 / 6.06-6.10 ms |
| (B) rendered and shown in a window (the UI's changed rows up) | 16.52-16.77 ms (drawn, its rows presented) | 0.52 ms (0.03x) | 2.22 ms (0.13x) |
| picture vs the CPU's (1.33 M covered pixels away from edges) | - | mean 0.32 / 255, 99.8 % within 3 / 255, ids 100 % | the same |

- Verdict: **go** on both cards for (A) and (B). The laptop is estimated in ADR 0013's amendment.
- **Where the comparison is harder on the GPU than a viewport would be:** the GPU draws its shadow map every frame (the
  CPU's is cached). (A) reads back colour, depth and ids every frame, where a viewport would read ids only on a click.
- **Where it is easier:** (B)'s timing ends at the GPU's copies. The system's own work after the present (the
  compositor's blit, and a copy to the display's card for the RTX 3060 Ti) isn't timed, where the CPU's GDI copy is.
  Even three times 2.2 ms stays under the bar.
- Tests: round 121. A GPU that isn't there fails cleanly. A small frame with a sun, a point and a spot light matches
  the CPU's on 19,491 covered pixels away from edges: mean 0.08 / 255, 100 % within 3 / 255, ids exact. It is skipped
  without a GPU.
- Review (Opus): the go stands. Fixed:
  - the verdict needs every card measured, and every call in the timed loops is checked;
  - the passes have explicit dependencies;
  - the shadow map is always cleared;
  - the swapchain is asked for only where the card lists it, and its format and usage are checked;
  - a suboptimal acquire is accepted, with one semaphore per image;
  - the picture is scored on covered pixels away from edges, by one helper (`gpu::raster_match`);
  - the shader takes the triangle's own normal for the shadow offset and the back-side flip, and the material's
    specular, as the CPU does.
