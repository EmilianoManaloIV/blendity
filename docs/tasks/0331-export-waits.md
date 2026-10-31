# Task 0331: an export never takes a stale mesh; one glTF skeleton a rig

- **Status:** done (round 354, 2026-10-31)
- **Asked:** "fix all fixes as stated in comparing with the other established programs" - finding 5 of
  docs/hci/compare-2026-10.md: the avatar's export ran while its Quad Remesh was still working and took the old mesh
  (76,796 triangles instead of the remeshed count), without a word. And round 352's review: several meshes on one
  armature wrote a skeleton each in glTF.

## What
- **An export waits for a job still working on what it would write** - a Quad Remesh, a skin bind, mesh maps, a surface
  (`pending_job_on`): the tray shows "Export waits for Quad Remesh", the log says it goes when that ends, and it is
  written the frame the job lands. The tray's Cancel writes it at once, with a warning that the job hadn't finished.
- **glTF shares a rig's skeleton:** a body, its hair and its clothes on one armature write one skeleton, one skin and one
  clip, every mesh's shape-key weights in it (FBX since round 352).

## Checked
- `tests_round354`: an export during a skin bind on a worker isn't written and the tray says it waits; it is written
  when the bind lands, with its skin; Cancel writes it at once and warns; a Quad Remesh is waited for too (the libraries'
  build); a Cube and a Sphere on one armature give one skin on two mesh nodes, one Bone, one clip with the Sphere's
  weights channel.

## The review's fixes (Opus code-reviewer, changes needed)
- **The tray's buttons reach a waiting export:** its row comes first; **Export Now** writes it at once (said, when the
  job hadn't finished) and **Cancel drops it** (it had cancelled the remesh instead, and the wait then wrote the old
  mesh).
- **The objects and file asked for:** a waiting export writes the objects it was asked for, to the path settled then -
  not what is selected when it goes.
- **Each export keeps its place:** a second export while one waits waits beside it (it had replaced the first silently).
- New Scene, Open and Play drop the waiting exports and say so (their objects are another scene's).
- The surfaces a waiting export watches are found once, not every frame; one export path (`export_path_for`,
  `export_objects`).
- Tests: the bind's weights in the file of an unbound mesh, the selection moved meanwhile; the tray's Export Now and
  Cancel pressed; two exports waiting; New Scene dropping them; the shared rig's weights channel on the Sphere's node;
  the remeshed triangle count.

## Not done
- A baked-light layer being written into a surface, and every material slot's surface (the first slot's is watched).
- Renders and lighting bakes started during such a job still use the mesh as it is (they show it, they don't write it
  out for other programs).
