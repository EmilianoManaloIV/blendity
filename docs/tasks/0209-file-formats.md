# 0209: exporting and importing the 50k sphere

- **Status:** done (round 229)
- **Requirements:** R-02, R-07, Q-01; task 0203's rank 6
- **Decisions:** none
- **Model:** main session
- **PR:** #174 (stacked on #173)

## What was there
- **Exporting** (OBJ, FBX, glTF, STL, PLY, USD): `split_normals` worked out each corner's normal by flooding across
  the faces round its vertex, and searched its list for every face it met. At the sphere's poles, 256 faces meet at a
  vertex, and that flood ran once for each of their 256 corners: 271 ms of every export.
- **The numbers** in the text formats were written through `printf("%.7g")`, one call per value.
- **The binary PLY reader** compared each property's type name as a string for every value it read (about 24
  comparisons for each of 800,000 values) and allocated a vector for every face.

## What changed
- **`split_normals`** floods once per fan, not once per corner. Each face gets a mark naming the fan it joined at a
  vertex; the fan's normal is the sum over its faces in sorted order, given to every corner in it. A face joins
  across an edge at the vertex it shares with the face it came from, when that edge isn't sharp and the angle is
  under the smooth limit, as before.
- **Numbers** are written with `std::to_chars` in its general format at precision 7. That is the text `%.7g`
  gives; compilers without it keep `printf`.
- **PLY:** each property's type is looked up once, read through a switch, and a face's corners go straight into the
  mesh.

## Measured (stress `blender_parity`, Pass 15, the 50k sphere)

| Row | Before | After | Blender 5.2.2 |
|---|---|---|---|
| Export OBJ | 791 ms | 108 ms | 45 ms |
| Export FBX | 322 ms | 24 ms | 76 ms |
| Export glTF (GLB) | 329 ms | 32 ms | 82 ms |
| Export STL | 365 ms | 30 ms | 12 ms |
| Export PLY | 336 ms | 38 ms | 7 ms |
| Export USD (USDA) | 827 ms | 112 ms | 253 ms |
| Import PLY | 65 ms | 43 ms | 6 ms |

- FBX, glTF and USD are now ahead of Blender. OBJ, STL and PLY are 2.4-5x behind: Blendity prepares every format
  the same way (a copy of the mesh in world space, turned to Y up, its split normals worked out),
  though STL needs only each face's normal.
- **Importing** STL and PLY stays 2-7x behind; welding the imported vertices takes 10-15 ms of it.

## Checked
- tests/test_assets.cpp `tests_round229`:
  - every "v" number is the text `%.7g` writes;
  - a smooth sphere's corners at a vertex share one normal, and a cube smooth by angle keeps each face's own;
  - on a sphere with sharp edges, flat faces and a smooth angle, every corner's normal matches the old per-corner
    flood (kept in the test as the reference): 608 of 608.
- The assets, modeling and editor groups; the import/export round trips.
- An Opus review found two things, both fixed:
  - two faces back to back (sharing two edges at a vertex) joined or not depending on which the flood started from;
    each shared edge is now tested on its own, as the old flood did;
  - a PLY face with two lists of corners kept both; it keeps the last again.
