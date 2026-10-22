# 0162: incremental scene uploads, one submission per upload

- **Status:** in progress (round 182)
- **Requirements:** R-03 (rendering), Q-01 (responsiveness)
- **Decisions:** none (ADR 0052's device model; the sections are a cache keyed by hashes)
- **Model:** main session
- **PR:** #127 (stacked on #126)

## Goal
Every `PathTracer::build()` re-flattened the whole scene and re-uploaded it to each card buffer by buffer - a
staging copy and a fence wait per buffer (13 of them), then one submission per acceleration structure - so moving
one object or editing one material cost the whole upload (182-252 ms in the OptiX spike's measurements). This task
sends the scene in one submission, and sends only the sections that changed when the cards already hold the
geometry and the textures.

## Scope
- In: `Renderer::update_instances / update_materials / update_lights` and `submits()`; the Vulkan device's `upload`
  from one staging arena in one submission (`Batch`: `batch_put`, `batch_patch`, `batch_send`), its BLAS builds in
  one submission (`build_blases`) and the TLAS in its own (`build_tlas`, rebuildable alone over the kept BLASes);
  `PathTracer::gpu_scene_sections` (hashes of the geometry, the instances with the software TLAS, the materials,
  the lights, the texture arena), `upload_gpus_partial` (the changed sections to every card when
  all hold the rest, else a whole upload), `PTStats::upload_calls / instance_updates / material_updates /
  light_updates / upload_ms`; a card that joins (adopted, parked and back, dropped) gets the whole scene.
- Out: not re-flattening the scene on the CPU (`build_gpu_scene` still builds every section each build; the hashes
  cost a pass over the bytes); partial texture updates (a changed texture arena is a whole upload); the bake's gather
  device.

## Acceptance criteria
1. Moving one object: no whole upload, one instance update, and a fixed-seed sample within 1e-5 of a fresh whole
   upload's.
2. A material edit: one material update, the whole build (Embree's refit, the flattening, the hashes, the send)
   under 5 ms (the plan's 2 ms was the send alone).
3. A light edit: one light update. A new mesh: a whole upload.
4. A whole upload sends its buffers in one submission; with the acceleration structures (each with its input
   buffer), the parameters and the clear, the test counts 9 submissions for a first whole upload on ray tracing
   hardware and 5 for an instance update (5 and 3 with Blendity's BVH). The stress gpu section's upload row shows the time of the whole build (the plan's 60 ms
   target against the measured number, honestly). An object added that reuses a mesh is a whole upload.

## Results
Measured (a quiet machine):

- NVIDIA GeForce RTX 4070 SUPER (compute, Blendity BVH): a whole upload of the stress gpu scene (400 spheres, the checker floor, their textures) 11.4 ms in 1 submissions; one object moved 4.9 ms in 1; one material edited 4.7 ms in 1.
- NVIDIA GeForce RTX 4070 SUPER (ray tracing hardware): a whole upload of the stress gpu scene (400 spheres, the checker floor, their textures) 14.0 ms in 5 submissions; one object moved 6.1 ms in 3; one material edited 4.9 ms in 1.
- NVIDIA GeForce RTX 3060 Ti (compute, Blendity BVH): a whole upload of the stress gpu scene (400 spheres, the checker floor, their textures) 11.1 ms in 1 submissions; one object moved 5.2 ms in 1; one material edited 5.2 ms in 1.
- NVIDIA GeForce RTX 3060 Ti (ray tracing hardware): a whole upload of the stress gpu scene (400 spheres, the checker floor, their textures) 14.5 ms in 5 submissions; one object moved 6.3 ms in 3; one material edited 4.3 ms in 1.
- Before this round a whole upload was a submission a buffer (13) plus one an acceleration structure, and every build sent the whole scene.

## Tests
- "incremental uploads: moving one object sends the card its instances alone ..." (tests/test_gpu.cpp).
