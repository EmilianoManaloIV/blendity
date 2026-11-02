# Task 0332: a performance budget - VRChat's ranks in the Profiler

- **Status:** done (round 355, 2026-10-31)
- **Asked:** "fix all fixes as stated in comparing with the other established programs" - finding 3 of
  docs/hci/compare-2026-10.md: no performance check against VRChat's ranks (or any budget); the avatar went out at
  76,796 triangles, Very Poor on PC, without a word.

## What
- **`scene/budget`**: the counts of the selection (and what's under it) or the scene - triangles (an Outline's shell
  counts twice, as an engine draws it), skinned and basic meshes, material slots, bones, spring chains and their bones
  (VRChat's PhysBone components and transforms), texture memory (a byte a texel, a third more for mips: BC7 / ASTC),
  lights, shape keys (shown, not ranked) - ranked against **VRChat PC** and **VRChat Quest**'s Avatar Performance Ranks
  (Excellent, Good, Medium, Poor; past Poor, Very Poor): each row the first rank it fits, the avatar the worst row's.
- **The Profiler's Budget section** (under the frame graph): the target (None until one is chosen: then nothing is ranked
  and exports stay quiet), the rank and what it's for (the selection or the scene), a row
  a count coloured by its rank with the next rank's limit; **Decimate to Good** when the triangles hold it back - a
  Decimate modifier, one ratio for every mesh (Undo takes it back).
- **`budget [pc|quest]`** lists the rows in the Console; **`budget fix [good|medium|poor]`** decimates to that rank.
- **Every glTF and FBX export says the file's rank** (with a target) - what the file holds, not the selection: its
  lights stay out - and what holds it back, a warning past Poor.

## Checked
- `tests_round355`: the ranks (the first fitting rank a row, the worst row, past Poor); a cube's 12 triangles, one mesh
  and slot, an Outline doubling them, a spring chain and its bones, the scene's light; the Profiler's line; `budget
  quest`'s rows; the export's line; Decimate to Good bringing a dense sphere under Quest's 10,000 (the libraries' build:
  meshoptimizer); the lesson's Try-it.

## Review (Opus): changes needed, all fixed
- **Decimate overshot or undershot:** one ratio for every mesh took the small ones' triangles as if they shrank too; the
  ratio is now (limit - the meshes left whole) / the big ones x 0.98, meshes under 500 triangles are left whole, and an
  existing Decimate's ratio is multiplied rather than replaced. The test lands between 85 % of the limit and the limit.
- **Decimate broke a rig and its keys:** `decimate` dropped the vertex groups and shape keys; they now follow each kept
  vertex, and a new Decimate goes before the Skin (as Blender's stack order needs). The test exports a skinned, keyed
  sphere after the fix: one skin, its key by name.
- **The export's line counted the selection, not the file:** it counts the exported items (no lights: glTF and FBX
  carry the meshes), only for glTF and FBX, and only once a target is chosen.
- **Minors:** keyed meshes count as skinned (VRChat's rule); a misspelt rank warns; a None entry in the target combo;
  the counts cached half a second (the Profiler had counted every frame); the tests exact (Poor, not "Poor or Very
  Poor").

## Not done
- A Mobile or Custom target (no published limits to cite; VRChat's are its own); fixes for material slots (merge) and
  texture memory (halve) - the rows say what holds the rank back.
