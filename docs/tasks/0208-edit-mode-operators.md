# 0208: Edit Mode's Select All, Inset and Bevel on big meshes

- **Status:** done (round 228)
- **Requirements:** R-02, R-07, Q-01; task 0203's rank 5
- **Decisions:** none
- **Model:** main session
- **PR:** #173 (stacked on #172)

## What was there
- **Select All** filled the edge selection (a hash set) with every edge, whatever the element mode. That took
  168 ms on 500k faces, and the Scene view's key walked the set every frame after.
- **Inset** on faces too small for its thickness bisects the thickness to stop short of a fold (Blender leaves the
  fold). Every try copied the whole mesh and rebuilt all its faces: 17 tries, 107 ms on the 50k sphere's top cap.
- **Bevel** walked, expanded and rebuilt every face of the mesh corner by corner to change a few. It then gathered
  every directed edge of the mesh to find the holes at the corners.

## What changed
- **Select All** fills the edge set only in Edge mode, the one mode that reads it. Switching to Edge mode works it
  out from the vertices, and now the console's `edit edge` does too (it goes through the same switch as the
  buttons). An undo into Edge mode works the edges out when its selection kept none.
- **Inset** builds the result once. Its shape doesn't depend on the thickness, only where its new vertices sit does.
  Each try moves those vertices alone, by the same expression as before, and checks only the faces the inset made.
  The result is bit for bit the old one.
- **Bevel:**
  - faces with no bevelled corner are copied as they were;
  - the corner holes are looked for only among the faces that changed and the new strips;
  - the corner patches come out in a fixed order: the open edges sorted, each patch starting at its smallest
    vertex. Before, a hash set's order decided which patch came first and which twin a fold kept.

## Measured (stress `blender_parity`, Pass 15)

| Row | Before | After | Blender 5.2.2 |
|---|---|---|---|
| Select all, 50k / 500k | 14.9 / 249 ms | 4.0 / 28.1 ms | 2.6 / 23.1 ms |
| Inset three faces, 50k / 500k | 75.7 / 930 ms | 25.9 / 259 ms | 24.1 / 230 ms |
| Bevel three faces' edges, 50k | 96.1 ms | 30.0 ms | 31.7 ms |

- Bevel is now ahead of Blender. Inset and Select All are within 1.1-1.5x; Select All's operator alone is 0.02 ms,
  and the rest is the frame that shows it.
- **Merge by distance** (49 against 41 ms) was left. Its operator is under Blender's whole row; the frame after it,
  which shows the merged mesh, is the rest.
- **Select linked from one face, 50k** read 21 ms in this run against 11.9 in the last (the operator alone 15.6
  against 8.2), though its path didn't change. On 500k faces it stays ahead (113 against 185).

## Checked
- tests/test_modeling.cpp `tests_round228`:
  - Select All in Vertex mode keeps no edge set, and `edit edge` then selects every edge;
  - Inset on the cap's tiny faces leaves no face of the cap turned over;
  - Bevel twice gives the same mesh, patch for patch.
- The modeling, editor, gpu and animation groups, and the geometry stress sections (problem and overlap counts).
- An Opus review confirmed Inset's result is bit for bit the old one, and found three things, all fixed:
  - Bevel's corner patches in a different order (now a fixed one);
  - the console's `edit edge` leaving Edge mode without edges after a Select All;
  - an undo into Edge mode restoring no edges.

  On a face that uses one vertex twice, Bevel now keeps each corner's own UV; before, both took the first's.
