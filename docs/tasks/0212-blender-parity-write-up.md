# 0212: Blendity against Blender 5.2 - the write-up

- **Status:** done (round 232)
- **Requirements:** R-02, R-07, Q-01
- **Decisions:** ADR 0061 amended (task 0204)
- **Model:** main session
- **PR:** #178

The user asked to "measure why and improve performance in Blendity by comparing all editor features and performance
with Blender 3D". Tasks 0201-0211 did that, one round each (221-231):
- 0201-0202: the harness;
- 0203: the causes;
- 0204-0211: eight fixes in the order users feel them.

This is the summary.

## How it was measured
- **Headless (0201):** stress section `blender_parity` against `tools/bench_blender.py --parity`.
  - Each side builds the same scene its own way: the same primitives, sizes and seeds.
  - Each row times the operation alone, plus whatever lands in the frames after it. A mesh rebuilt or a picture
    presented one frame later counts.
  - Means are of three warm runs, on this machine: an RTX 4070 SUPER and Blender 5.2.2 LTS.
  - Each row writes down both sides' methods. Rows that did different work were corrected in the harness, not
    "fixed" in the code; 0203 lists them.
- **Windows (0202):** `tools/bench_ui.py`. Blendity's `--windowed-check` and Blender's own window
  (`wm.redraw_timer`, with the view nudged so Blender can't show a finished picture). Both at 2560 x 1369, the mean
  of 200 frames after warm-up.

## The headless rows: 27 of 59 behind, now 17
Every row that was behind in round 223's Pass 15, or is behind now. A row that "turned" was behind and now leads.

| Row | Blendity before -> after | Blender 5.2.2 | Before -> after |
|---|---|---|---|
| Save a scene: 10,000 cubes | 24.8 ms -> 25.5 ms | 23.0 ms | 1.1x behind -> 1.1x behind |
| Save a scene: a 500k-face mesh | 268.8 ms -> 141.1 ms | 21.6 ms | 12.4x behind -> 6.5x behind |
| Open it: a 500k-face mesh | 470.1 ms -> 132.3 ms | 26.7 ms | 17.6x behind -> 5.0x behind |
| Export the 50k sphere as OBJ | 762.0 ms -> 112.1 ms | 44.7 ms | 17.0x behind -> 2.5x behind |
| Import the 50k sphere from OBJ | 104.1 ms -> 44.7 ms | 36.7 ms | 2.8x behind -> 1.2x behind |
| Export the 50k sphere as FBX | 289.3 ms -> 23.7 ms | 75.5 ms | 3.8x behind -> 3.2x ahead (turned) |
| Export the 50k sphere as glTF (GLB) | 300.1 ms -> 32.3 ms | 82.1 ms | 3.7x behind -> 2.5x ahead (turned) |
| Export the 50k sphere as STL | 331.2 ms -> 30.2 ms | 12.4 ms | 26.7x behind -> 2.4x behind |
| Import the 50k sphere from STL | 45.6 ms -> 24.8 ms | 20.5 ms | 2.2x behind -> 1.2x behind |
| Export the 50k sphere as PLY | 307.6 ms -> 38.2 ms | 7.2 ms | 42.9x behind -> 5.3x behind |
| Import the 50k sphere from PLY | 70.0 ms -> 42.2 ms | 6.0 ms | 11.7x behind -> 7.1x behind |
| Export the 50k sphere as USD (USDA) | 775.1 ms -> 113.2 ms | 253.1 ms | 3.1x behind -> 2.2x ahead (turned) |
| Duplicate 10,000 objects | 725772.2 ms -> 68.5 ms | 1525.2 ms | 475.9x behind -> 22.3x ahead (turned) |
| Select all (50k faces) | 14.9 ms -> 3.6 ms | 2.6 ms | 5.7x behind -> 1.4x behind |
| Extrude three faces (50k faces) | 41.1 ms -> 25.1 ms | 30.0 ms | 1.4x behind -> 1.2x ahead (turned) |
| Inset three faces (50k faces) | 75.7 ms -> 26.7 ms | 24.1 ms | 3.1x behind -> 1.1x behind |
| Bevel three faces' edges (50k faces) | 96.1 ms -> 30.0 ms | 31.7 ms | 3.0x behind -> 1.1x ahead (turned) |
| Merge by distance, everything (50k faces) | 76.6 ms -> 53.7 ms | 41.2 ms | 1.9x behind -> 1.3x behind |
| Exit Edit Mode (50k faces) | 4.1 ms -> 1.9 ms | 4.0 ms | 1.0x behind -> 2.0x ahead (turned) |
| Select all (500k faces) | 248.7 ms -> 27.8 ms | 23.1 ms | 10.8x behind -> 1.2x behind |
| Extrude three faces (500k faces) | 503.4 ms -> 220.8 ms | 353.1 ms | 1.4x behind -> 1.6x ahead (turned) |
| Inset three faces (500k faces) | 930.2 ms -> 249.3 ms | 229.8 ms | 4.0x behind -> 1.1x behind |
| Merge by distance, everything (500k faces) | 916.7 ms -> 598.7 ms | 495.6 ms | 1.8x behind -> 1.2x behind |
| Triangulate every face (500k faces) | 702.6 ms -> 467.0 ms | 679.7 ms | 1.0x behind -> 1.5x ahead (turned) |
| Modifier: Array x10 on the 50k sphere | 24.0 ms -> 8.1 ms | 11.2 ms | 2.2x behind -> 1.4x ahead (turned) |
| Modifier: Bevel on the 50k sphere | 633.7 ms -> 618.8 ms | 522.5 ms | 1.2x behind -> 1.2x behind |
| Modifier: Boolean union with a second 50k sphere on the 50k sphere | 270.5 ms -> 158.4 ms | 79.8 ms | 3.4x behind -> 2.0x behind |

## The window rows
Seven of the nine rows trailed in round 223. Now one does: Edit Mode on 500k faces, by 1.1x.

| Row | Blendity, round 223 -> now | Blender 5.2.2 (now) |
|---|---|---|
| Window: 50k-face sphere, Solid | 6.0 -> 3.1 ms | 4.7 ms |
| Window: 500k-face sphere, Solid | 8.2 -> 4.3 ms | 4.8 ms |
| Window: a million triangles, Solid | 6.3 -> 3.3 ms | 4.2 ms |
| Window: a million triangles, Material Preview / Shaded | 6.4 -> 3.4 ms | 5.5 ms |
| Window: 10,000 cubes, Solid | 20.1 -> 14.9 ms | 25.5 ms |
| Window: Edit Mode on the 50k sphere | 6.2 -> 3.1 ms | 5.0 ms |
| Window: Edit Mode on the 500k sphere | 16.6 -> 5.6 ms | 4.9 ms |
| Window: playing 1,000 keyframed objects | 17.4 -> 4.1 ms | 11.2 ms |
| Starting up: the first frame, then quit | 498 -> 493 ms | 2,796 ms |

## Each gap, its cause and its fix
| Task (round) | Gap (Blendity / Blender, before) | Cause | Fix |
|---|---|---|---|
| 0204 (224) | Every window frame: 500k sphere 8.2 / 4.1 ms | The GPU path waited for the card every frame, and copied its depth and ids back though only a click reads them | Frames are submitted and left running; depth and ids stay on the card until a click asks |
| 0205 (225) | Edit Mode on 500k faces 16.6 / 5.4 ms | The cage's vertices, and Face mode's dots, were worked out and sent every frame | Kept on the card; rebuilt only when the mesh, matrix, mode or selection change |
| 0206 (226) | Playing 1,000 keyed objects 17.4 / 11.3 ms | Every animated number was remembered for Record, even with Record off; each track found its object by path | Remembered only while Record is on; the clip's objects found in one walk |
| 0207 (227) | Duplicate 10,000 objects 648 s / 1.5 s | Each copy's name was tried against every sibling, and each copy was moved through the list | One go for the selection: names gathered once per parent, each list rebuilt once |
| 0208 (228) | Select All 14.9 / 2.6 ms, Inset 76 / 24, Bevel 96 / 32 | Select All filled an edge set in every mode; Inset rebuilt the whole mesh on each of 17 tries; Bevel rebuilt every face | The edge set only in Edge mode; Inset built once, its new vertices moved alone; Bevel rebuilds only the faces it changes |
| 0209 (229) | Export OBJ 791 / 45 ms, PLY 336 / 7 ... | Each corner's normal was flooded from its own face (256 x 256 at the poles); PLY compared type names per value | One flood per fan of faces; `to_chars`; PLY types looked up once |
| 0210 (230) | Open a 500k-face scene 474 / 27 ms | The file was read through a stream buffer and copied; the text parsed on one core | One read at the file's size; big blocks written and parsed on every core |
| 0211 (231) | Boolean 265 / 80 ms, Array x10 23 / 11 | A map per corner and per face in the conversions; Array added vertices one by one | Chains and tables; whole arrays appended; and the shaders compiled when Blendity is built |

## Where Blendity still trails, and why
| Row | Blendity / Blender | Why |
|---|---|---|
| Save / open a 500k-face mesh's scene | 141 / 22 ms, 132 / 27 ms | `.scene` is text: about 20 MB of decimal numbers, written and parsed on every core; `.blend` stores its arrays as they are in memory |
| Export PLY, STL, OBJ | 38 / 7, 30 / 12, 112 / 45 ms | Every format is prepared the same way: a world-space copy turned to Y up, with split normals worked out, which STL and PLY don't all need |
| Import PLY | 42 / 6 ms | Welding the imported vertices takes 10-15 ms of it; Blender's PLY importer reads the arrays as they are |
| Import OBJ, STL | 45 / 37, 25 / 21 ms | Within 1.2x: the parse plus the weld |
| Boolean union of two 50k spheres | 158 / 80 ms | The conversions are fixed; Manifold's own work (the two solids and the union) is about 145 ms of the old 265, and the same library takes 80 ms in Blender. Its settings are the next thing to compare |
| Bevel modifier, Merge by distance, Select All, Inset | 1.1-1.4x | Merge by distance's and Select All's operators are under Blender's whole row: the frame after them, which shows the result, is the rest |
| Edit Mode on the 500k sphere (window) | 5.6 / 4.9 ms | The cage's edges: Blendity's edge shader draws a four-row strip per edge, mostly discarded (0203's rank 2, its CPU half fixed in 0205) |

## Where Blendity leads
- **Objects:**
  - duplicating many is 22x faster, deleting 316x and undoing it 13x;
  - opening 10,000 objects is 8.5x faster.
- **Files:** importing and exporting FBX, glTF and USD.
- **Edit Mode:**
  - entering and leaving;
  - extrude, now 1.2x faster on 50k faces and 1.6x on 500k;
  - select linked, loop cut, subdivide and triangulate, and their undo.
- **Modifiers:** Subdivision (10-22x), Decimate (3x), Mirror, Solidify, Array (1.4x), and the stack evaluated again
  after a vertex moves (4.6x).
- **Elsewhere:**
  - unwrapping, animation, physics and memory;
  - F12;
  - 10,000 cubes in the window;
  - starting up, 5.6x faster.

## What would close the rest
- **A binary block for big meshes in `.scene` files:** save and open 5-6x behind. It changes the file format, so it
  needs its own decision.
- **Boolean:** Manifold's own work. Its settings and threading against Blender's are the next thing to compare.
- **The PLY and STL writers:** a path that skips the corner normals they don't use.
- **Merge by distance and Select All:** the frame after them.

## Checked
- Each fix's round: its own tests, an Opus review with its findings fixed, and the full chain (Windows in
  render-cache verify mode, the GPU leg, Linux, sanitizers, fuzz, the stress sections).
- Pass 15's tables in docs/PERFORMANCE.md were measured again for this write-up on a quiet machine. The headless
  rows come from round 231's build, the window rows from round 231's exe.
