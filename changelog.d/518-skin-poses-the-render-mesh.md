## 2026-11-16 (round 518): The skin poses the render mesh

**Asked:** move skeletal skinning to the graphics card (the user, 2026-11-15; Pass 26, task 0501, ADR 0146: normals
turned by the bones as Unity does, and the CPU views by the same rule). This round lays the ground on the CPU: the
reference the card's skin pass (round 519) is written from and checked against, and the CPU views' path.

**Changed (task 0501, ADR 0146)**
- A posed character's render mesh is its rest render mesh with the vertices moved by the skin
  (`skin_render_vertices`, `src/scene/skin_render.cpp`): positions by the same blend as the Skin modifier (the two
  share `skin_blend_point`, so a corner lands on the deform's own bits), normals and tangents turned by the blended
  bones as Unity's SkinnedMeshRenderer does - no longer worked out again from the posed faces. A pose therefore
  never builds a render mesh whole, moves one in place, splits a corner again or redoes MikkTSpace.
- `GameObject::evaluated_mesh` keeps, on a mesh whose last modifier drawn is a Skin (without a limit group, outside
  Edit Mode's cage), the pose and the mesh before the skin (`Mesh::skin_pose`); the rest's render mesh is kept from
  frame to frame while the mesh before the skin is the same, and follows in place when only its positions moved (a
  shape key). Any change to the mesh after that ends it, and the usual build takes over.
- A posed quad or n-gon keeps the rest's diagonal (as on the card, and in Unity); before, it was split again from
  the posed positions.
- Profiler: "render meshes skinned" and "render meshes posed by the skin" (ms) in the Posing block.

**Measured:** main's baseline for the pass (068207d9: `posing`, `animation`, the owner's avatar in the owner's world,
measured locally) - in round 522's comparison.

**Reviewed (code-reviewer, two passes):** worker copies (`MeshCopyBare`) no longer carry the pose; each pose's rest has
its own lock, taken inside the new `JobSystem::isolate` (one lock for all skins hung with oneTBB when a waiting thread
stole another skin's piece); a rest that followed a shape key is settled with the mesh; the rest is written in place
when nothing else holds it, without the vertex weights; the round 470 / 474 checks that pinned the in-place move keep
it through a limit group (the skin's own render mesh no longer moves in place).

**Checked:** with Blender's libraries (oneTBB) the animation, render and editor groups - 2,636 / 2,639 / 3,220 checks,
the only failures the 21 of "assets: Ctrl and Shift click select several materials", which fails on this machine on
main too and passes on CI and Linux; dependency-free the animation group 2,635 checks on Windows (the same 21) and
2,635 on Linux, 0 failed. CI runs the whole suite.

<!-- docs/ARCHITECTURE.md -->
after: | `Bone`, `Armature`, `SkinModifier`, `armature_bones`, `skin_bind_automatic`
| `SkinPose`, `skin_blend_point` / `skin_blend_dir`, `skin_render_vertices` (`scene/skin_render.cpp`), `Mesh::skin_pose`, `Mesh::skinned_render_mesh` (`scene/mesh.cpp`) | Round 518: one pose of a skin (bone matrices, dual quaternions, the binding, the mesh before the skin) and the kernel that poses a rest render mesh by it - the blend the Skin modifier shares, normals and tangents turned by the bones (Unity's way); the CPU reference of the card's skin pass | ADR 0146 |
