# 0463: Generate Lighting's gather on every ticked graphics card (round 487)

- **Status:** done (2026-11-11)
- **Requirements:** Q-06 (responsiveness), R-08 (VRChat worlds and avatars)
- **Decisions:** none needed (task 0018's "the first ticked GPU" becomes "the ticked GPUs", as F12 has had since task
  0051; no data model, file format or dependency changes)
- **Model:** main session (GPU devices, threads and the bake's determinism)
- **PR:** targets main

## Asked
The owner (2026-11-11), after baking SideRail: "when baking lights it uses all the GPU's available. I only saw the
4070ti working." Their machine has an RTX 3060 Ti and an RTX 4070 SUPER. Put the options, they chose: "Yes, both
cards". (Round 487 was first light cookies - the world has none - then two faults of their bake, which they dropped:
"2 and 3 can be ignored".)

## Goal
Generate Lighting's bounce-light gather runs on every graphics card ticked in Preferences, as an F12 render does, and
says which cards it used. Today it runs on the first ticked card only.

## What is there (measured in a scratch copy, a machine that was not quiet)
- `Editor::choose_bake_gpu` returns one device; `PathTracer::gpu_gather` holds one renderer; the lightmapper sends each
  batch of texels to it and adds the lamps' direct light, the emitters and the filter on the CPU.
- SideRail, 820,031 texels: lightmaps 50.1 s at 1024 gather samples, 31.5 s at 256 - so about 25 s is the gather on
  the card and about 25 s is on the CPU. Only the first half can gain from a second card.

## Scope
- In: the lightmap gather (`gpu_gather` mode 1) of Generate Lighting on every ticked card; each batch of texels shared
  between the cards by the speed each showed on the batches before; a card that fails, is still opening or cannot hold
  the scene leaves its share to the others; the bake's log and the Lighting window say which cards gathered and their
  shares.
- Out: Preview Lighting (it steps on the main thread: one card, as today); the light probes' gather (mode 2: one card);
  moving the CPU half (direct light, emitters) to the card - the larger win, a pass of its own; OptiX; any change to
  which cards are ticked.

## Files this round touches (ownership)
- `src/render/pathtracer.h`, `src/render/pathtracer.cpp` (`gpu_gather`, the bake's device state only)
- `src/render/lightmapper.h`, `src/render/lightmapper.cpp` (`BakeSettings::gpu_devices`, the gather's call, the shares)
- `src/editor/lighting.cpp`, `src/editor/editor.h` (which cards a bake gets; the log line; the Lighting window's line;
  `lighting cards all|one`, the measuring verb that keeps a bake to the first ticked card - not saved, not in the
  scene's key, as `lighting pick`), `src/editor/editor_commands.cpp` (its one dispatch line)
- `src/editor/lessons.cpp` (the lesson's paragraph)
- `tests/test_gpu.cpp`, `tests/test_lighting.cpp` (no stress row after all: the measurement is SideRail's, which is not
  in the repository; its numbers are in `docs/PERFORMANCE.md`)
- `changelog.d/487-bake-on-every-card.md` (with its shared-docs sections), this file
- Not touched: `src/editor/render_view.cpp`, `src/scene/scene.*`, the Blockout and posing files.

## Acceptance criteria
1. **The same picture.** With one card in the list the gather's output is what `gpu_gather(device, ...)` returned
   before, bit for bit (the old entry point stays and is the one-card case).
2. **Whichever card gathers a point, the point's random sequence is its own**: a gather split between two cards equals,
   point by point, the same gather on the first card alone within 1e-4 relative (bit for bit where the two cards are
   the same model; two different models may round differently) - tested with the same device listed for both halves
   (always available where one card is) and, where the machine has two cards, with both.
3. **A card that cannot** (an index that is no device, a scene that does not fit it, an error while gathering) **does
   not stop the bake**: the others take its share and the result is still complete; with no card left the call returns
   false as today and the bake goes on on the CPU.
4. **The bake says what it used**: after a Generate Lighting on the cards, the log holds one line naming each card and
   its share of the gathered points, and the Lighting window shows the cards beside the bake's time.
5. **Machines with one card, or none, bake as before** (the dependency-free build and CI have no card: the
   tests of 1 to 3 skip there, as the other GPU tests do). *Amended while measuring (see Results):* a one-card bake of
   more texels than one old batch now draws other random directions (a texel's sequence is its place in the bake, not
   in its batch) - the same samples, other noise; smaller bakes and the probes' gather are bit for bit what they were.
6. **Measured**: SideRail's default bake in a scratch copy with one card ticked and with both, like for like in one
   queue; the lightmaps' seconds and each card's share in the task's Results. No number is asserted by a test.
7. Existing unit tests and stress `lightmap_quality` show no regressions.

## Tests and checks to run
- Unit: `--group gpu`, `--group lighting` filtered to this round's, then the full suite (Windows both builds, Linux).
- Stress: `--only lightmap_quality`.

## Risks and edge cases
- Two renderers used from two threads at once (F12 does it already through `gpus_`).
- A card with less memory gets a smaller copy of the scene's textures (`build_gpu_scene_fit`), so its points may differ
  from the other card's by more than rounding: said in the log when it happens; not on SideRail.
- The interactive editor never waits for a card that is still opening: that batch goes to the cards that are ready.
- Small batches (8192 points at 1024 samples): a share under 256 points is not worth a submission - the fastest card
  takes it.

## Results (2026-11-11)
SideRail in a scratch copy, 820,031 texels, 1024 gather samples, the build with Blender's libraries, a quiet machine,
three runs each after a warm-up; the lightmaps' seconds.

| | Run 1 | Run 2 | Run 3 |
|---|---|---|---|
| Before: one card, batches of 8192 texels | 37.7 | 39.1 | 36.6 |
| The first build: both cards sharing batches of 8192 texels | 41.1 | 40.9 | 39.7 |
| Now: one card (RTX 4070 SUPER) | 17.7 | 17.5 | 17.8 |
| Now: both cards (71 % RTX 4070 SUPER, 29 % RTX 3060 Ti) | 16.2 | 16.2 | 16.5 |

- **The first build made the bake slower** (row 2). Each card's own clock showed the cause: a card's time for a batch
  of 8192 texels is mostly its wait for the longest paths of each dispatch, so splitting such a batch saves nothing,
  and the one-card bake was held back the same way. What was added to the plan above:
  - a texel's random sequence is its place in the bake (`first_index`), so batches can be any size without changing a
    lightmap;
  - `PathTracer::gpu_gather_batch`: each card's batch is what it returned in about 750 ms (from about 8M paths, at
    most fourfold growth a call, at most 131,072 texels a card - a step, and Cancel, stay under about two seconds);
    Generate Lighting uses it (`BakeSettings::gpu_batch_by_speed`), Preview Lighting keeps the small batch;
  - the cards are made ready together: one scene build for every card it fits, the uploads at once.
- Criteria 1 to 4: met - the tests of round 487 in `tests/test_gpu.cpp` (the split, a card that cannot, two cards:
  0 of 20,000 points differ; a run cut into calls; a call cut across submissions; the wrap) and
  `tests/test_lighting.cpp` (the verb; a bake on all cards and on one: the pages differ by 0, the log and the Lighting
  window name the cards). Criterion 5: amended above. Criterion 6: the table. Criterion 7: the gpu and lighting
  groups pass but for four checks of three Scene-view tests that fail the same way on `main` (see the changelog).
- The pages of the six runs of rows 3 and 4 are byte for byte one file.
- Two code reviews; the second found that the large batches would have reached Preview Lighting (the editor's
  thread) and made Cancel slow - both fixed before the timed runs above.
- Not built: the bake's CPU half on the card; a stress row.

## Documentation to update
- `changelog.d/487-bake-on-every-card.md` with `<!-- docs/USABILITY.md -->`, `<!-- docs/ARCHITECTURE.md -->`,
  `<!-- docs/PERFORMANCE.md -->` and `<!-- README -->` sections; the Learn lesson on GPU baking.
