# 0201: every editor feature measured against Blender 5.2 - the parity harness

- **Status:** done (round 221)
- **Requirements:** R-07 (performance), Q-04 (the editor stays interactive); the user's "Measure why and improve
  performance in Blendity by comparing all editor features and performance with Blender 3D" (2026-10-23)
- **Decisions:** none (a measuring tool; PERFORMANCE.md's Pass 15 holds the numbers, as Pass 13 does for task 0178)
- **Model:** main session
- **PR:** #166 (stacked on #165)

## Goal
Pass 13 (task 0178) compared 11 rows with Blender. Most of the editor was never measured beside it: files, many
objects, most Edit Mode operators, modifiers, animation, physics, rendering, memory. This task measures them on the
same machine, on scenes both sides build alike, so that tasks 0203-0211 can rank and close the gaps.

## What was built
- **Blendity's side:** the stress section `blender_parity` (stress/stress_main.cpp). It writes one table: row,
  Blendity, unit, operator alone, method.
  - An editor row is the console command, the headless frame that shows it and the frame after. Each frame counts
    only what it costs beyond an idle frame measured just before: work left for the next frame (a rebuild, a present
    one frame behind) is counted, the frame's fixed cost is not.
  - The views are on the GPU when there is one.
  - "Operator alone" is the command without its frames.
  - Each row is undone outside its timer, and is the mean of three runs.
- **Blender's side:** `tools/bench_blender.py --parity` runs the twin of each row headless, under the same name.
  Each times the operator, the undo step Blender's editor pushes, and the depsgraph update.
- **The table:** `tools/bench_table.py --parity <stress folder> --blender-parity <json>` joins the two by name, in
  areas, between PERFORMANCE.md's `pass15-table` markers. `tools/bench.bat` runs both.
- **CI:** the section's quick mode runs in the stress quick run, in about 7 s (1,000 objects, the 50k sphere, two
  file formats, no F12).
- **Fixed on the way:** `redo <field> <value>` from the console now keeps the numbers for the tool's next use, as
  the F9 panel does. Before, the next bevel started from the old width.

## The rows (59)
- **Files:**
  - save and open 10,000 cubes, and a 500k-face mesh;
  - export and import of the 50k sphere as OBJ, FBX, glTF (GLB), STL, PLY and USD (USDA).
- **Objects:** select all, duplicate, delete and undo of the delete, on 10,000 cubes.
- **Edit Mode,** on the 50k sphere (256 x 196) and a 500k one (1024 x 490):
  - enter and exit;
  - select all and select linked;
  - extrude, inset, bevel (0.005, 3 segments) and loop cut;
  - subdivide every edge, merge by distance and triangulate;
  - the undo of the subdivide and of the triangulate.
- **Modifiers,** on the 50k sphere:
  - Subdivision levels 1 and 2 (Blender's limit surface off: Blendity subdivides a step at a time), and level 1 on
    a cube of 24,576 quads with no high-valence poles;
  - Mirror, Array x10, Solidify, Bevel on every edge, Decimate 0.5 and Boolean union;
  - the stack again after a vertex moved.
- **UV, animation, physics:**
  - unwrap (Conformal, a seam down one meridian);
  - a frame of 1,000 keyframed objects;
  - ten shape keys;
  - 1,000 rigid boxes in their first 30 frames at 60 Hz, one step a frame on both sides.
- **Rendering and memory:**
  - F12 at 1920 x 1080 of Blendity's `stress 400 Sphere`, built alike in Blender (Blendity's raster against
    EEVEE at its 64 samples);
  - the resident memory of 10,000 cubes and of a 500k-face mesh.

## Results (Ryzen 9 5900XT, RTX 4070 SUPER, Blender 5.2.2; PERFORMANCE.md Pass 15 has every row and method)
Blendity is behind on 23 rows of 59.

| Behind | Blendity | Blender 5.2 |
|---|---|---|
| Duplicate 10,000 objects | 648 s | 1.5 s |
| Export the 50k sphere: PLY / STL / OBJ / FBX / GLB / USDA | 336 / 365 / 791 / 322 / 329 / 827 ms | 7 / 12 / 45 / 76 / 82 / 253 ms |
| Import the 50k sphere: PLY / OBJ / STL | 65 / 105 / 42 ms | 6 / 37 / 21 ms |
| Save / open a 500k-face mesh's scene | 266 / 474 ms | 22 / 27 ms |
| Bevel three faces' edges, 50k | 118 ms | 32 ms |
| Inset three faces, 50k / 500k | 67 / 765 ms | 24 / 230 ms |
| Boolean union / Array x10 / Bevel modifier | 265 / 23 / 613 ms | 80 / 11 / 523 ms |
| Select all, 50k / 500k | 4.7 / 42 ms | 2.6 / 23 ms |
| Merge by distance, 50k / 500k | 63 / 602 ms | 41 / 496 ms |

Blendity is ahead on:
- **Objects:** delete (316x) and its undo (13x); opening many objects (7x).
- **Files:** glTF, FBX and USD import (2-8x).
- **Edit Mode:** entering and leaving it (1.2-4.6x); extrude, select linked, loop cut, subdivide, triangulate and
  their undo (1.2-3.5x).
- **Modifiers:** Subdivision (11-21x), Decimate (3x).
- **Other:** UV unwrap (4x), animation (2x), physics (23x), memory (3-41x) and F12 (2.9x).

## Checked
- Every Blendity row has a Blender twin, under the same name.
- The quick mode runs in the stress quick run.
- tests/test_modeling.cpp: the console's `redo` keeps a bevel's width and segments for its next use.
- An Opus review found, all fixed:
  - the bevel row running the old width: the console's `redo` didn't keep its numbers (fixed in the editor, with a
    test);
  - Blender's subdivision evaluating its limit surface (17 s on the sphere's 256-edge poles), where Blendity
    subdivides a step;
  - the Bevel modifier rows bevelling nothing (every edge under the angle limit);
  - a Boolean that fails timed as a success;
  - physics simulating different time (Blender at 24 fps with 10 substeps);
  - "less the frame after" hiding work left for that frame;
  - a failed import overwriting its export's row, `.blend1` backups saved inside the timer, temp folders left;
  - a duplicate of linked meshes against shared ones;
  - quick runs too heavy for CI.
