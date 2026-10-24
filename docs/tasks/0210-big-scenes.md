# 0210: saving and opening big scenes

- **Status:** done (round 230)
- **Requirements:** R-02, R-07, Q-01; task 0203's rank 7
- **Decisions:** none (the `.scene` format is unchanged)
- **Model:** main session
- **PR:** #176

## What was there
- **Saving** wrote a mesh's 1.5 million positions, 2 million UVs and its face indices number by number, on one core.
- **Opening** parsed them line by line, on one core.
- **Reading the file** went through a stream buffer into a string stream, then copied out. Timed in parts on the 50k
  scene, parsing took 9.5 ms of the row's 48; most of the rest was this read. Every importer reads its file the same
  way.

## What changed
- **Saving:** a mesh's `v`, `f` and `t` lines are written in pieces of 32,768 lines on every core and joined in order.
  The text is byte for byte what the one-core writer gave. Meshes under 65,536 lines keep the old loop.
- **Opening:** a run of `v`, `f` or `t` lines of 65,536 or more is found first and parsed in pieces on every core.
  The run stops at the first other line (a blank, a comment, Windows line ends are taken as before) and never runs
  past the count the mesh's header gives. Whatever follows is read line by line as before. A bad face is reported at
  its own line number, as before.
- **`fs::read_file`** asks the file's size and reads it into the string at once. A file that can't seek, says it is
  empty or reads short (a pipe, `/proc`, a file being written) is read through the stream as before.

## Measured (stress `blender_parity`, Pass 15)

| Row | Before | After | Blender 5.2.2 |
|---|---|---|---|
| Save a scene: a 500k-face mesh | 266 ms | 140 ms | 21.6 ms |
| Open it | 474 ms | 135 ms | 26.7 ms |
| Save a scene: 10,000 cubes | 25.4 ms | 25.9 ms | 23.0 ms |
| Open it | 75.4 ms | 63.0 ms | 534 ms |
| Import the 50k sphere from OBJ | 104 ms | 46 ms | 37 ms |
| Import from STL | 45 ms | 26 ms | 21 ms |
| Import from PLY | 43 ms | 33 ms | 6 ms |

- The imports gained from the file read alone.
- **Big meshes still trail 5-6x.** The `.scene` file is text: about 20 MB for the 500k-face sphere, every number
  written and parsed as decimal. Blender's `.blend` stores its arrays as they are in memory. A binary block for big
  meshes would close most of the gap, but it would change the format, which is plain text (Zstandard-compressed when
  asked); that is left for a decision of its own.

## Checked
- tests/test_assets.cpp `tests_round230`:
  - a 131k-face sphere's scene saved twice gives the same text, and reads back exactly (positions, faces, UVs);
  - a bad vertex 100,000 lines into its faces is reported at its own line;
  - the same text with Windows line ends and blank lines every 70,000 lines reads as the same mesh;
  - read back from a file it is every byte, and an empty file reads empty.
- The assets, editor and modeling groups.
- An Opus review found the parallel save and load the same as the one-core path in every case it tried: Windows line
  ends, blank lines inside a block, wrong counts, a missing last newline, bad lines and their numbers. It found two
  things in the new file read, both fixed:
  - a file that can't seek (a pipe) no longer opened;
  - a file that says it is empty but isn't (`/proc`) read as empty.

  The test now also reads the scene with Windows line ends and blank lines inside every block, and through a file.
