# 0015: GPU viewport: a measured go / no-go spike

- **Status:** spike done, verdict no-go. Waiting for the user's decision; no viewport code is written
- **Requirements:** R-05, Q-01
- **Decisions:** ADR 0013, GPU viewport
- **Model:** main session (Opus)
- **PR:** (with task 0014's)

## Goal
Decide with numbers whether moving the rasterized views to the GPU (Vulkan) is worth building. The
alternative is to keep the views on the CPU and move only the heavy per-pixel passes.

## Scope
- In:
  - **The stress section `gpu_viewport`** times a 1080p view on the CPU against the same picture's
    work on each GPU, with and without ray tracing hardware.
    - **Scene:** 256 icospheres and a floor, with a sun and shadows.
    - **Camera:** orbits each frame, so nothing is cached.
    - **CPU side:** deferred PBR with the sun's shadow map, cached as in the editor.
    - **GPU side:** the existing Vulkan path tracer at 1 sample and no bounces (a primary hit, the
      sun's light and a shadow ray). It is read back and tone mapped, with the frame split into camera
      and buffer clears, render plus read-back, and resolve.
  - ADR 0013 records the rule and the outline of a GPU viewport, should it ever be a go.
- Out: any viewport code. It needs this spec approved with a go, as CLAUDE.md requires.

## Result (2026-10-10; RTX 4070 SUPER and RTX 3060 Ti, 32 CPU threads, two runs agree within 2%)
| device | 1 view end to end ms | of it render + read-back ms | 2 views, GPU work vs CPU | verdict |
|---|---|---|---|---|
| CPU (deferred + shadow map) | 19.6 | - | 1.00x | - |
| RTX 4070 SUPER, compute | 33.0 | 11.8 | 0.60x | no-go |
| RTX 4070 SUPER, ray tracing hardware | 31.5 | 10.6 | 0.54x | no-go |
| RTX 3060 Ti, compute | 39.0 | 17.5 | 0.89x | no-go |
| RTX 3060 Ti, ray tracing hardware | 36.5 | 15.4 | 0.79x | no-go |

- **End to end, the GPU path is slower than the CPU.** Through the path tracer, about 16 ms a frame
  goes to clearing its float buffers on each camera change and 9 ms to tone mapping on the CPU. A
  viewport would do both on the GPU, so the verdict uses the GPU's render plus read-back only.
- **Even that misses the bar** (two views at ≤ 0.50× the CPU's time) on both cards. The CPU views
  became cheap in task 0011, and a 1080p frame's read-back alone is a large share of the GPU's time.
- **Recommendation:** keep the views on the CPU. The next step that pays off is the per-pixel passes,
  either on the GPU as compute or cheaper on the CPU:
  - voxel GI adds about 12 ms at 1080p;
  - probe sampling adds about 15 ms at 1080p;
  - both are dominated by per-pixel work that a GPU does well and that reads back a small buffer.

  A follow-up spec would cover them.

## Acceptance criteria
1. `blendity_stress --only gpu_viewport` runs on a machine without a GPU and prints the CPU row and a
   note.
2. With a GPU, it prints each device's upload, end-to-end, render plus read-back time and verdict.
3. ADR 0013 and this spec record the numbers and the verdict.

## Tests and checks to run
- Stress: `gpu_viewport`, run twice.

## Risks and edge cases
- **The proxy traces primary rays,** where a viewport would rasterize them, so it can make the GPU look
  slower than a real viewport would be. A rasterizing prototype could still win, and the spike can't
  rule that out. It is a larger piece of work for the user to choose.

## Documentation to update
- ADR 0013, CHANGELOG.
