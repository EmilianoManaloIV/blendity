# ADR 0140: A bake's lightmap pages go to the card before the bake lands

- **Status:** accepted (2026-11-14; round 508, task 0492 part C). Covered by the approval of Pass 25's plan (2026-11-13),
  which named an ADR from 0140-0144 for "how the new set is held and published".
- **Builds on:** ADR 0105 (budgets: 100 ms one landing frame, 50 ms a frame while a job runs); ADR 0106 (Generate Lighting
  on a worker, landing as one change); ADR 0109 (the card's baked corners); round 458 (the per-frame upload budget,
  `mesh_budget_ms_`); ADR 0056 (a measurement is a stress row)
- **Requirements:** Q-performance

## Context
When a bake finished, `bake_job_land` took the new `LightingData` (0.1 ms), and the same frame's card draw met every page
for the first time: `gpu_lightmap` converted each to floats with alpha and sent it, all in that frame. Measured with the
views on the card (stress `scene_views`, the worldlike stand-in): the landing frame 36 ms at 0.26 M texels, 50 ms at
1.05 M, 146 ms at 4.19 M (four 1024 x 1024 pages), of which the pages and baked corners were 96 ms. It grows about 20 ms
a million texels; SideRail's landing was 1.2 s on record.

Sending pages a few a frame after the landing would draw objects with the new UVs over pages not yet sent, or over the
old pages: a frame that mixes new UVs with old texels shows wrong light.

## Decision
1. **The pages go up before the landing.** When the worker is done, `step_bake` asks `bake_pages_ready(job)` first: it
   sends the job's pages not yet on the card, at least one a frame and more while the frame's upload budget lasts
   (`mesh_budget_ms_`, the meshes' budget), and the landing waits until every page is there. The landing then happens
   in the frame after the last page, as one change, as before.
2. **The old set is drawn whole until then.** Nothing about the scene's lighting changes while the pages go up:
   `lighting_data_`, the UVs, the probes stay the old bake's, so every frame is either the old lighting or the new.
   `baking_` stays true meanwhile (the tray, the Lighting window and `bake wait` read it).
3. **The card's copies are found again after the move.** The card keys a page's copy by the page's address and serial
   (`gpu_lightmaps_`); moving the job's `LightingData` into `lighting_data_` moves the vector's buffer, so both stay and the
   landing frame finds every page already sent.
4. **Where it applies:** only while the Scene view draws on the card (`scene_full_size()`: the card stays open after the
   user picks CPU views, and pages sent there would never be read nor let go) and the jobs run on workers. A headless
   editor without workers and the CPU views land in one frame as before. A cancelled or discarded bake, or one for a scene since
   replaced, lands (or is dropped) at once. A page the card refuses lands anyway; the draw reports it as it did.

## Consequences
- The landing frame no longer holds the page uploads; a frame while the pages go up holds one page or more (a
  1024 x 1024 page is about 24 ms, inside the 50 ms a frame while a job runs).
- The bake shows as running for a few frames more (one per page at worst); the tray names the phase it was in (a status
  of its own is the job tray's to add).
- A single page larger than the budget is still one long frame (a 4096 x 4096 page): no worse than before, now in a
  frame while the job runs rather than the landing frame.
- In Solid or Wireframe shading the pages go up ahead too (the old landing sent them when a lit view first drew them).
- The baked corners for the new UVs are still built in the landing frame; spreading them is a follow-up if the numbers
  ask.
- Counters in `view_perf::Counters` (`lightmap_pages_sent`, `lightmap_pages_before_landing`, `lightmap_page_ms`) let
  tests count what was sent when, never time it.
