# Task 0339: the VRChat world kit, part 1 - Colliders, and Generate Lighting's Fix

- **Status:** done (round 362, 2026-10-31)
- **Asked:** "fix all fixes as stated in comparing with the other established programs" - findings 9 and 10 of
  docs/hci/compare-2026-10.md: a VRChat world's meshes reached the engine without their collision, and Generate
  Lighting with nothing marked baked "0 objects" without saying why.
- **Split:** the plan's world-kit round is two. LODs are round 363, and the later rounds move one along.

## What
- **`ColliderComponent`** ("Collider"; Add Component > Physics) has:
  - a shape: Box, Sphere, Capsule (along Y), Convex (the mesh's hull) or Mesh (its triangles);
  - a center, a size, a radius and a height;
  - Fit to Mesh, which sizes it to the mesh's bounds on the next frame and unticks itself.

  Added, it fits the mesh at once.
- **`scene/colliders`:** `fit_collider`, `collider_mesh` (the shape as a mesh in the object's space; Convex its
  bounds box) and `collider_export_name`.
- **Play (Jolt):**
  - a rigid body with a Collider takes its shape (a moving Mesh collider is its hull);
  - a still object with one is a static body of that shape at its place;
  - without one, nothing changes: a hull, or the triangles.

  The object's scale is applied, and the center is an offset.
- **The Scene view** draws each collider (not Mesh) as a green wire, brighter when selected.
- **Exports** (glTF, FBX): ExportOptions' `collider_names` writes each Collider as its own mesh after its object's:
  - Unity: `<name>_Collider`, or `<name>_MeshCollider` for a Mesh shape. The Unity bridge's new
    `BlendityColliderPostprocessor` makes each a MeshCollider (convex, or the triangles) and removes its renderer.
    It hasn't yet been run inside Unity: the tests check the names.
  - Unreal: `UCX_<name>_00`, convex shapes only, read by Unreal's FBX import. A Mesh collider is Unreal's "use complex
    as simple".
  - Godot: `<name>-convcolonly` for the convex shapes, `<name>-colonly` for a Mesh.

  It's in the Export dialog and the console (`export glb colliders=unreal <path>`).
- **Generate Lighting** with no Contribute GI object refuses, naming Unity's Static, with Mark Static Meshes as its
  Fix. `lighting markstatic` ticks Contribute GI on every mesh without a Rigidbody, a Skin or an Animation.

## Checked
- `tests_round362`:
  - Fit to Mesh on a 1 x 2 x 1 cylinder (and unticked);
  - the collider meshes' sizes for a box, a sphere and a capsule;
  - the export names, with UCX_ refused for a Mesh collider and Unity's and Godot's for both kinds;
  - a .glb's nodes with no colliders and with each engine's;
  - an added collider undone and redone;
  - a scaled level's floor and a switched-off bush in Play (Jolt);
  - Mark Static leaving a windmill's blade;
  - the refusal, and Mark Static marking the cube but not the Rigidbody ball;
  - in Play (Jolt, the libraries' build), a cube-meshed ball with a 0.3 m sphere collider resting at 0.3 on a floor
    whose box collider lies under its plane;
  - the lesson's Try-it.

## Review (Opus): changes needed, all fixed
- **Ctrl+Z stuck after adding a collider from the Inspector** (a blocker). The fit had made its own step from a state
  that asked for it again. Fits now run just before a step is kept and join it; a loaded or undone state asking for
  one is fitted quietly. A test adds a collider, then undoes and redoes it.
- **Nothing in Unity read `_Collider`.** The Unity bridge gets `BlendityColliderPostprocessor`, and Mesh shapes are
  named `_MeshCollider`.
- **A still collider used its local scale:** a scaled level's floor collided at half its size. Both branches now use
  the world scale. A test drops a ball on a floor 7 m out from a level scaled 2.
- **Auto Generate refused once a second:** it now refuses once for each lighting state, and says when the Contribute
  GI meshes are hidden or inactive.
- **Mark Static marked moving things:** it now skips objects under an Animation, Rigidbody, Oscillator or
  PlayerController, and inactive ones. A test covers a windmill's blade.
- **The wire is cached** by the collider's fields and the mesh's version; Box doesn't scan the mesh.
- **Disabled and hidden colliders:**
  - a switched-off collider collides with nothing (a test with a bush);
  - a Mesh collider without a renderer collides;
  - an empty's Box, Sphere or Capsule collides and is drawn.
- **Exports:** a Mesh collider is written without shape keys, and collider items have no textures baked and aren't
  counted by the budget.
- **Godot:** `-convcolonly` for the convex shapes.
- **Minors:**
  - Center is shown only where it's used;
  - the console's `colliders=` may come anywhere among the options, and a misspelt one is said;
  - a fit with no mesh isn't marked.

## Not done
- **LODs:** round 363.
- **Exporting an empty's collider** (an invisible wall): it collides and is drawn, but only meshes are exported.
- **A real convex hull** in exports and the wire: Convex is drawn and exported as the bounds box; Jolt builds the true
  hull.
