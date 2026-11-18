## 2026-11-18 (round 567): A million splats imported, measured

**Asked:** Pass 28's follow-up of round 544's write-up, which gave the import of a million splats as "not measured
(about 0.4 s estimated)": measure it (PLY and SPZ, time and memory per stage), keep the editor responsive while it
runs, fix what is plainly wasteful in the readers.

**Added (task 0549)**
- `blendity_stress --only splats` now reads a 1,000,000-splat PLY and SPZ of SH degree 3 (built in code, written to
  the run's scratch project, removed afterwards with the copies the editor's import makes): the file read, the
  unpack, the setup, the decode, the change of axes and the validation each timed, the cloud's size and the process's
  memory growth at its peak; and the same files through the editor's `import` with jobs on workers - the longest
  frame while the file is read, the landing frame, the total.
- A reader's `SplatReadReport` carries where its time went (`times`: unpack, setup, decode, axes, validate).

**Changed**
- The splat readers decode on every core: a point's bytes go to its own entries of the cloud, 4,096 points a piece
  (`JobSystem::parallel_for`; from the editor on the import worker's background pool, never the main thread's). The
  change of axes - positions, turns, the SH bands - runs the same way.
- A PLY whose properties are all little-endian floats (what a 3DGS trainer writes) is read straight from its records;
  before, every value went through a `double` and a switch on its type. Text PLY lines are parsed in parallel too.
- The clouds are the same, value for value: the unit tests compare the little-endian, big-endian and text encodings
  of one file, a file's first points alone against the same points inside a larger file, and every scale and opacity
  byte of an SPZ against the format's formula.

**Measured** (the hub's timed window, 2026-11-18 04:21-04:23, the machine quiet; two runs of each exe, the better
one shown, each row the best of three reads; 1,000,000 splats of SH degree 3. "Before" is round 531's readers with
this round's clocks, its setup inside its decode)

| file | file MB | read ms | unpack ms | setup ms | decode ms | axes ms | validate ms | total ms | cloud MB | peak growth MB |
|---|---|---|---|---|---|---|---|---|---|---|
| PLY binary, before | 236.5 | 186.3 | 0 | (in decode) | 253.9 | 118.9 | 29.7 | 588.8 | 225.1 | 435.0 |
| PLY binary, after | 236.5 | 184.1 | 0 | 27.1 | 19.1 | 11.4 | 31.8 | 273.4 | 225.1 | 435.0 |
| SPZ v3 gzip, before | 45.1 | 32.0 | 253.7 | (in decode) | 76.4 | 123.0 | 29.2 | 514.3 | 225.1 | 356.2 |
| SPZ v3 gzip, after | 45.1 | 32.3 | 252.7 | 24.7 | 13.4 | 11.5 | 30.2 | 364.9 | 225.1 | 356.2 |

- The second run of each exe agrees within 3 % (PLY 593.7 -> 281.2 ms, SPZ 513.9 -> 367.3 ms). The readers' own
  part (setup, decode, axes, validate) went from 403 ms to 89 ms for the PLY and from 229 ms to 80 ms for the SPZ;
  memory is unchanged.
- The editor (headless, jobs on workers): the PLY lands 0.40 s after `import` (before: 1.19 s), the SPZ 0.48 s
  (before: 0.70 s); the longest frame while the worker reads is 9.9 ms, the landing frame 1.3 ms - within ADR
  0105's budget (50 ms a frame while a job runs, 100 ms for one frame), as it was before the round. The read was
  already off the main thread (round 413); nothing had to move.
- Not changed, named for a later round (outside this round's files): the whole-file read (`fs::read_file`, 184 ms
  of a PLY's 273), SPZ's inflate and the copy `gunzip` makes of its output (253 ms of an SPZ's 365), and
  `pointcloud_validate`, which is serial (about 30 ms).

**Checked:** both suites' groups `splats` and `editor` - Windows with Blender's libraries 427 and 3,869 checks,
Linux dependency-free 425 and 3,843, 0 failed; the dependency-free Windows build is left to CI.
`blendity_stress --only splats`: 16,500 damaged files, 0 problems.

<!-- docs/ARCHITECTURE.md -->
after: | `read_ply_point_cloud()`, `read_spz()`, `gltf_point_cloud()`, `pointcloud_change_axes()`
| `SplatReadReport::times` | `src/scene/import_splat.h` | where a splat read's time went - unpack, setup, decode, axes, validate - for the `splats` stress rows; the readers decode in parallel pieces of 4,096 points (round 567) |

<!-- docs/PERFORMANCE.md -->
replace: | 1M under 2 s (not measured; ~0.4 s estimated) |
with: | 1M under 2 s: 0.27 s PLY, 0.36 s SPZ from a file on disk; 0.40 s / 0.48 s through the editor's import (round 567, timed) |

append:
## Round 567: a million splats imported

`blendity_stress --only splats`, the rows "Round 567", in the hub's timed window (2026-11-18, the machine quiet);
the best of three reads of 1,000,000 splats of SH degree 3, the better of two runs:

| file | read ms | unpack ms | setup ms | decode ms | axes ms | validate ms | total ms | peak growth MB |
|---|---|---|---|---|---|---|---|---|
| PLY binary, 236.5 MB, before | 186.3 | 0 | (in decode) | 253.9 | 118.9 | 29.7 | 588.8 | 435.0 |
| PLY binary, 236.5 MB | 184.1 | 0 | 27.1 | 19.1 | 11.4 | 31.8 | 273.4 | 435.0 |
| SPZ v3 gzip, 45.1 MB, before | 32.0 | 253.7 | (in decode) | 76.4 | 123.0 | 29.2 | 514.3 | 356.2 |
| SPZ v3 gzip, 45.1 MB | 32.3 | 252.7 | 24.7 | 13.4 | 11.5 | 30.2 | 364.9 | 356.2 |

The cloud itself is 225.1 MB. Through the editor's import (the file copied into the project, read on a worker) the
PLY lands in 0.40 s and the SPZ in 0.48 s; the longest frame while the worker reads was 9.9 ms and the landing frame
1.3 ms. What is left is outside the readers: the file read for a PLY, the inflate for an SPZ.
