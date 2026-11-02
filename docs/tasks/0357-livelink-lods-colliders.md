# Task 0357: LODs and colliders in Live Link (round 380)

## Asked
Phase B's fifth round (plan cozy-fluttering-corbato: "LOD and Collider components arrive as an LOD Group and
colliders"). A world built in Blendity keeps its levels of detail and its collision in the engines while you work.

## Built
- **A `collider` message:** `{"t":"collider","id":7,"shape":"box","center":[..],"size":[..],"radius":0.5,"height":2}`
  in Live Link's axes; `"shape":"none"` when the component goes or is turned off.
  - Convex and Mesh name the object's own mesh, which the client already has.
  - Sent at a sync and when its fields change.
- **An `lod` message:** `{"t":"lod","id":7,"screen":[0.5,0.25,0.1],"ratio":[1,0.5,0.25],"cull":false}` - level i
  shows while the object stands at least `screen[i]` of the view's height, as `lod_level` picks. `"screen":[]` when the
  LOD Group goes; none for a mesh that deforms every frame.
  - Each level past 0 as `lodmesh` (`{"t":"lodmesh","id":7,"level":1,"topology":..,"verts":..,"bin":N}`, a glTF
    binary like `mesh`'s).
  - The levels are decimated on the mesh worker from a copy of the mesh.
  - A level follows its mesh once the mesh has stood still for half a second (a stroke doesn't decimate every tick).
- **The bridges:**

  | Bridge | LODs | Colliders |
  |---|---|---|
  | Unity | an LOD Group: level 0 the object's renderer, the others child renderers `<name>_LOD<n>` wearing its materials; each level's transition height its screen size (the last 0 unless it culls) | Box, Sphere, Capsule and Mesh Colliders (Convex: a convex Mesh Collider), made again when the mesh comes |
  | Godot | MeshInstance3D children `<name>_LOD<n>`, switched by visibility ranges (a screen size made a distance from the object's size and a 75° view) | a StaticBody3D child with a CollisionShape3D: BoxShape3D, SphereShape3D, CapsuleShape3D, the mesh's convex or trimesh shape |
  | Blender | the group as a custom property, its levels hidden child objects `<name>_LOD<n>` (what an export to Unity names) | a custom property, and a wire empty for a box or sphere |
  | Unreal | the static mesh's own LODs (built once every level has come), LOD i from Blendity's `screen[i-1]` | the body's simple collision (box, sphere, sphyl, convex hull); Mesh: complex as simple |

## Checked
- `tests_round380`: a capsule collider and three levels at the sync (two `lodmesh`); the radius changed sends the
  collider once; the ratios changed send the group and both levels; the mesh edited sends no levels at once and both
  after it stood still; the collider taken off sends `none`; the Try-it.
- The recorded session adds a crate with an LOD Group and a 1 x 2 x 1 box collider 0.1 m off centre.
- Each bridge's replay:

| Bridge | Levels | Collider |
|---|---|---|
| Unity 6000.6.7f1, built-in and URP | an LOD Group of 3, its renderer then Crate_LOD1 and Crate_LOD2, transitions 0.5, 0.25, 0 | a BoxCollider 1 x 2 x 1 at (0.1, 0, 0) |
| Godot 4.7.2 | Crate_LOD1 and Crate_LOD2 with ranges that meet | a BoxShape3D 1 x 2 x 1 at (-0.1, 0, 0) |
| Blender 5.2 | Crate_LOD1 and Crate_LOD2 hidden children | a cube empty scaled 0.5 x 0.5 x 1 at (-0.1, 0, 0) |
| Unreal 5.8 | 3 LODs, LOD 1 from 0.5 | a box 100 x 100 x 200 cm at (0, 10, 0) |

## The review's fixes
- Level jobs for levels a group no longer has are dropped; every bridge ignores a `lodmesh` past the latest `lod`.
- **Finding the bridge's own children:** Unity and Godot mark them (a BlendityPart component, a `blendity_part` meta),
  so a rename or a Blendity object named like a level doesn't confuse them.
- **The user's own children and colliders are left alone:** Unity puts its collider on a child "Blendity Collider".
- **Mesh and Convex colliders after deforms:** made again in Unity and Godot once the deforms have stopped for half a
  second (Unreal already did).
- **A kind change** (an empty given a mesh) sends the collider and group again.
- **What resends levels:** only the level count or a ratio changing sends them, not a screen size.
- **Unreal:**
  - rebuilds once per batch (`Flush`), and only the body for a collider change;
  - a new mesh drops the old levels.
- **Godot's switch distances:**
  - use the largest side of the world bounds, as Blendity does, scale included;
  - follow transform changes.
- **A one-level group that culls** is kept (Unity, Godot).

## Limits
- Unreal takes a collider only on an object with a mesh (an empty's collider - a trigger, an invisible wall - isn't
  made there).
- Godot's levels switch by distance, not screen size; Unreal's screen size measures the bounds' sphere, not the height,
  and doesn't cull past the last level.
- Blender keeps colliders and levels for export, not for physics or drawing.
- The levels are copies of the mesh in a build without decimation (the dependency-free build).
