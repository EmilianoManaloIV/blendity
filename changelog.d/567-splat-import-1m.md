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

**Measured** (NOT yet in a timed window: the rows below are from an untimed run on a busy machine, 2026-11-18, best
of three reads, 1,000,000 splats of SH degree 3; the hub's timed batch replaces them)

| file | file MB | read ms | unpack ms | setup ms | decode ms | axes ms | validate ms | total ms | cloud MB | peak growth MB |
|---|---|---|---|---|---|---|---|---|---|---|
| PLY binary, before | 236.5 | 811 | 0 | (in decode) | 880 | 261 | 75 | 2,027 | 225.1 | 435 |
| PLY binary, after | 236.5 | 346 | 0 | 61 | 23 | 17 | 70 | 516 | 225.1 | 435 |
| SPZ v3 gzip, before | 45.1 | 96 | 570 | (in decode) | 153 | 288 | 82 | 1,189 | 225.1 | 356 |
| SPZ v3 gzip, after | 45.1 | 68 | 452 | 54 | 15 | 19 | 63 | 671 | 225.1 | 355 |

- The file read's difference between the two runs is the machine's load, not this round: the readers do not read
  the file.
- The editor (headless, jobs on workers): the PLY lands 0.71 s after `import`, the SPZ 0.67 s; the longest frame
  while the worker reads is 4.1 ms (PLY) and 14.5 ms (SPZ), the landing frame 1.5 ms - within ADR 0105's budget
  (50 ms a frame while a job runs, 100 ms for one frame). The read was already off the main thread (round 413);
  nothing had to move.
- Not changed, named for a later round (outside this round's files): the whole-file read (`fs::read_file`, most of
  a PLY's time now), SPZ's inflate and the copy `gunzip` makes of its output (most of an SPZ's time), and
  `pointcloud_validate`, which is serial.

**Checked:** both suites' groups `splats` and `editor` - Windows with Blender's libraries 427 and 3,866 checks,
Linux dependency-free 425 and 3,843, 0 failed; the dependency-free Windows build is left to CI.
`blendity_stress --only splats`: 16,500 damaged files, 0 problems.

<!-- docs/ARCHITECTURE.md -->
after: | `read_ply_point_cloud()`, `read_spz()`, `gltf_point_cloud()`, `pointcloud_change_axes()`
| `SplatReadReport::times` | `src/scene/import_splat.h` | where a splat read's time went - unpack, setup, decode, axes, validate - for the `splats` stress rows; the readers decode in parallel pieces of 4,096 points (round 567) |

<!-- docs/PERFORMANCE.md -->
replace: | 1M under 2 s (not measured; ~0.4 s estimated) |
with: | 1M under 2 s: measured in Pass 28's timed batch; untimed 0.52 s PLY, 0.67 s SPZ (round 567) |

append:
## Round 567: a million splats imported

To be measured in Pass 28's timed batch (`blendity_stress --only splats`, the rows "Round 567"). Untimed, on a busy
machine, best of three reads of 1,000,000 splats of SH degree 3:

| file | read ms | unpack ms | setup ms | decode ms | axes ms | validate ms | total ms | peak growth MB |
|---|---|---|---|---|---|---|---|---|
| PLY binary, 236.5 MB | 346 | 0 | 61 | 23 | 17 | 70 | 516 | 435 |
| SPZ v3 gzip, 45.1 MB | 68 | 452 | 54 | 15 | 19 | 63 | 671 | 355 |

Before the round, the same way: PLY 2,027 ms (decode 880, axes 261), SPZ 1,189 ms (decode 153, axes 288). Through the
editor's import the longest frame while the worker reads was 14.5 ms and the landing frame 1.5 ms.
