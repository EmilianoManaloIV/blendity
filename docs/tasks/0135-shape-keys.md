# 0135: Shape keys

- **Status:** done
- **Requirements:** R-13
- **Decisions:** ADR 0047
- **Model:** main session (Opus)
- **PR:** (with task 0134)

## Goal
Faces and morphs: named vertex offsets on a mesh, weighted per renderer, animated, skinned with the character, and
read from FBX and glTF (MMD and VRM next).

## Scope
- In: `Mesh::shape_keys` (`ShapeKey`: a name, an offset per vertex); `find_shape`.
- In: `MeshRenderer::shape_weights` (by name), reflected as `BlendShape/<name>` sliders, so clips animate them.
- In: `GameObject::evaluated_mesh`:
  - the keys added to the basis before every modifier;
  - hashed by the weights in use;
  - the deform path when no modifier changes topology.
- In: scene files (`sk <name> <count> <index dx dy dz>...`, sparse).
- In: FBX blend shapes (each channel's full shape, through the geometry transform, mirrored; the channel's weight)
  and glTF morph targets (POSITION offsets, `extras.targetNames`, the node's or mesh's weights), kept through
  `weld_exact`.
- In: `shape list | add <name> [amount] | set <name> <weight> | remove <name>`.
- Out: sculpting keys in Blendity; normal offsets (normals are recomputed from the shaped positions).

## Acceptance criteria
1. A weight adds exactly that much; none in use, the mesh itself.
2. Saved and loaded; animated by a clip.
3. Added before the skin: the offset turns with the bone.
4. Blender's FBX and GLB keys, offsets and values come in.

## As built
- Tests: round 155; fixtures `tests/fixtures/shapes_quad.{fbx,glb}` (`make_shapes.py`).
- ufbx scales blend-shape offsets with the geometry (its MODIFY_GEOMETRY conversion) and leaves the axis turn on the
  node, like the vertices: the offsets are in the mesh's own space.
- After the review:
  - evaluated meshes carry no keys;
  - Edit Mode's cage is the basis;
  - the weights are in the head's cache key only when a modifier runs before the tail (otherwise they deform
    again);
  - Apply Modifiers refuses a mesh with keys;
  - `Editor::carry_shape_keys` remaps keys after any edit that changes the topology (`meshops::transfer_shape_keys`,
    the vertex groups' map);
  - the weights are saved as one list by exact name (`Reflector::weight_list`, `serializing()`);
  - `sk` lines are parsed as integers.
