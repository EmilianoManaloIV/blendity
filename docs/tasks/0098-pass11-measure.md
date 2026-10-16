# 0098: Performance pass 11 - measure first

- **Status:** done
- **Requirements:** R-05, Q-01
- **Decisions:** none needed
- **Model:** main session (Opus)
- **PR:** shipped with task 0099's PR (the rows and the counters it reads are in the same files)

## Goal
The user asked "What should be more optimization gains?" and chose all four areas: real scenes on the GPU, Edit Mode on
big meshes, computers without a GPU, path-traced renders. Before changing anything, the `editor_render` stress section
should show where those frames go, so pass 11's tasks have a baseline.

## Scope
- In: `editor_render` rows and notes:
  - the 50k-face Move gizmo drag with the views on the GPU, broken down as the CPU drag is (mesh writes, moved in
    place, draw items), plus the GPU path's time a frame and the bytes the meshes sent to the card;
  - a textured scene (a Checker procedural on the icospheres, a Sky world) with the views on the GPU: its frame time,
    and how many views drew on the GPU or fell back to the CPU, with the reason.
- In: `FrameProfile::gpu_upload_bytes`, what the views' meshes sent to the card.
- Out: the path tracer's per-sample split (trace against passes and accumulation). It moves to task 0105, which
  changes that code and measures it there.

## Acceptance criteria
1. The rows run in `--only editor_render` and `--laptop --quality low --only editor_render`.
2. The textured row says why its views drew on the CPU.
3. The existing rows and the stress problem counts are unchanged.

## As built
- The textured scene: 0 views on the GPU, 33 on the CPU ("a Sky or HDRI world"). Before textures can matter, the world
  has to go to the card (task 0102); then the procedural (task 0101).
- The GPU drag before task 0099 (`--laptop --quality low`, from pass 10): 25 ms a frame, of which the render mesh moved
  in place about 6.8 ms, the cage overlay about 6.5 ms, and every vertex and index of each drawn cache re-sent to the card
  each frame.
