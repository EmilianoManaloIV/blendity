# 0003: Copy-on-write meshes and scene snapshots for undo

- **Status:** accepted (recorded 2026-10-09; from phase 1)
- **Requirements:** Q-04, Q-05, R-08

## Context
Undo must cover every edit and be cheap; adjustable operations (F9) must re-run from the mesh as it was.
Copying every mesh into each undo step would make big scenes slow.

## Decision
- Meshes are shared through `MeshPtr` (`std::shared_ptr<Mesh>`), as Blender's implicit sharing does. An
  undo step is a clone of the scene that shares unchanged meshes.
- Code that edits a mesh calls `mesh_make_mutable(ptr)` first; it clones when the mesh is shared. After an
  edit, callers re-read `MeshPtr` from the `MeshFilter` - an old reference may point at the previous copy.
- Every change bumps `Mesh::version` (`touch()`); caches (render mesh, overlap checks, F9 validity) key on
  pointer + version.
- Adjustable operations keep `LastOp::before` and re-run from a fresh copy of it.

## Consequences
- Undo snapshots are cheap; editing one object in a big scene copies only that mesh.
- Holding a `Mesh&` across an operation that may replace the mesh is a bug; reviewers look for it.
- Anything that changes a mesh after an operation's result was recorded (e.g. auto smooth) must update the
  recorded version, or F9 / drag helpers treat the operation as gone (the round-18 bevel bug).
