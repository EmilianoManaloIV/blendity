# 0051: Minimum spec, GPU: a GTX 1050 Ti (4 GB, no ray tracing hardware)

- **Status:** done (as built below)
- **Requirements:** Q-04
- **Decisions:** ADR 0026 (amended: the GPU part)
- **Model:** main session (Opus)
- **PR:** (link when opened)

## Goal
Path tracing and GPU bakes work on the user's minimum GPU, a GTX 1050 Ti. It has 4 GB, Pascal, Vulkan 1.2, and no
ray tracing hardware, so it traces with Blendity's compute kernel. On that card a render never crashes the
driver, never fails for lack of memory, and is never slower than the CPU alone.

## Scope
- In:
  - **No submission past the driver's timeout.** Samples go to a GPU in submissions of at most about 100 ms
    (Windows resets a GPU after 2 s).
  - **The GPU's memory.** A scene that wouldn't fit in 80 % of the card's memory goes with every texture a mip
    level smaller at a time (never under 256 texels a side). When even then it doesn't fit, the GPU leaves the
    render to the CPU with a message, instead of failing to allocate. The same applies to GPU bakes.
    `BLENDITY_GPU_VRAM_MB` emulates a smaller card.
  - **Render > Device: GPU + CPU (Auto).** Both devices take samples as fast as they finish them (the existing
    speed-guided scheduling), so a slow card costs nothing and a fast one does most of the work.
  - **Re-measuring ADR 0013's GPU viewport at 8 threads.**
- Out: a GPU viewport, tiling a single sample that alone takes over 2 s.

## Acceptance criteria
1. A scene over a card's budget uploads with smaller textures; a 2048 texture on a 12 MB budget arrives at 1024.
2. A frame whose buffers alone pass the budget renders on the CPU.
3. The stress `gpu` section's longest submission is about 100 ms on the compute kernel.
4. Device 2 is saved, and renders on the CPU and the GPUs together.

## As built
- **Longest submission** at 1280 x 720 (RTX 4070 SUPER and 3060 Ti, compute kernel): 106 and 98 ms. The first
  submission of a frame is one sample, before a speed is known. On a 1050 Ti at 1080p, one sample of a heavy scene
  could still pass 2 s; that would need tiling.
- **The CPU at 8 threads** renders 6.5 samples/s of the stress frame. The 3060 Ti's compute kernel does 60.9. A
  1050 Ti (about a sixth of that) is near the CPU's speed or a little above, so Auto is the right default advice
  for it.
- **GPU viewport (ADR 0013) at 8 threads**: the RTX cards' GPU part of a view is 0.26-0.40 of the CPU's, so the
  spike's verdict is "go" for them. But 14 ms of each frame is camera and clears, and a 1050 Ti tracing in software
  would be slower than the CPU's rasterizer. The minimum machine keeps the CPU viewport; ADR 0013 stands.
- **The texture budget** applies to the whole scene for every GPU at once (one upload): the smallest card's memory
  decides.

## Documentation to update
- ADR 0026 (GPU part), CHANGELOG, PERFORMANCE pass 8 (GPU).
