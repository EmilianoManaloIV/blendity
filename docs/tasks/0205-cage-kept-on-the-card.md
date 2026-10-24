# 0205: Edit Mode's cage kept on the card

- **Status:** done (round 225)
- **Requirements:** R-07, Q-04; task 0203's rank 2
- **Decisions:** none (an implementation inside task 0100's card cage)
- **Model:** main session
- **PR:** #170 (stacked on #169)

## What was there
After task 0204, Edit Mode on the 500k-face sphere took 11.7 ms a frame against Blender's 5.5. Every frame:
- the CPU worked out each of the cage's vertices again in world space and compared them with what the card held, to
  send only what changed (nothing, in a frame that only draws again);
- in Face mode, every face's dot was worked out again and sent through the overlays' buffer: 22 ms a frame on that
  mesh.

## What changed
- The cage's vertices are worked out again only when what they come from changes: the mesh's version, the object's
  matrix, the element mode and the selection their colours show. A frame that only draws again (an orbit, a view
  drawn again) builds and sends nothing.
- Face mode's dots go into the cage's vertex buffer after the mesh's vertices. They are kept on the card with them
  and drawn by the cage's point pipeline from there (`RasterFrame::cage_point_first`), in the same order and with
  the same depth test as before. Their radius is worked out each frame, so a change of UI scale shows at once.
- Face mode no longer needs the overlays' pipelines on the card.
- The Profiler counts the cage's rebuilds (`gpu_cage_vert_builds`).

## Measured (a 2560 x 1369 window, a frame)

| Row | Before | After | Blender 5.2.2 |
|---|---|---|---|
| Edit Mode on the 500k sphere (Vertex) | 11.7 ms | 6.0 ms | 5.5 ms |
| The same in Edge mode | - | 6.4 ms | - |
| The same in Face mode | 22.1 ms | 8.2 ms | - |
| Edit Mode on the 50k sphere | 3.6 ms | 3.2 ms | 5.2 ms |

Edit Mode on 500k faces is within 1.1x of Blender's. Of the 6 ms, the UI takes 2.4 and the card's own passes 1.4.
Blender's Face mode in Solid shading shows no dots, so there is no Blender column for it.

## Checked
- tests/test_gpu.cpp `tests_round225`:
  - a frame that only draws again works out nothing and sends nothing, in Vertex and Face mode;
  - a face selected works the dots out once and sends 96 bytes of a 195 KB cage;
  - Face mode on the card is within 0.13 / 255 of the CPU's picture.
- The gpu group passes.
- An Opus review found, all fixed:
  - Face mode's dot radius kept from the last rebuild (a change of UI scale wouldn't show);
  - a test that the old code also passed (now the Profiler counts the cage's rebuilds, and the test checks there are
    none while only drawing again);
  - Face mode sent to the CPU without the overlays' pipelines, which it no longer needs.
