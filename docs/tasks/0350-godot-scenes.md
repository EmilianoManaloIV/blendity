# Task 0350: Godot scenes in and back (round 373)

## Asked
Phase E's fifth round (plan cozy-fluttering-corbato, E5): a Godot project's scenes open in Blendity for the art and
the layout, while code stays in Godot, kept intact. The way back writes the scene with the edits made here.

## Built
- **`scene/godot_scene`** reads Godot's text formats: a .tscn, and the .tres materials it names.
  - **The parser** (`parse_godot_text`) reads `[tag key=value ...]` sections and `key = value` properties. A value
    may run over several lines. Every byte stays with its section, so the sections joined give back the file.
  - **Values:** `godot_string`, `godot_numbers` and `godot_ref`. References work in both forms: Godot 3's numeric
    `ExtResource( 1 )` and Godot 4's `ExtResource("1_abc")`.
  - **Transforms:** `godot_transform` and `godot_transform_text` convert Transform3D / Transform (the basis by rows,
    then the origin) to and from Blendity's local transform. Godot's axes are glTF's, so X mirrors both ways (S R S
    and S o with S = diag(-1, 1, 1)), as every importer and Live Link's Godot bridge do (ADR 0055). A scene's node
    and a model instanced in it therefore agree, and Live Link sends an imported object back to where it was.
  - **Lights and cameras** look down -Z in Godot; Blendity's forward is +Z. Each is its object turned 180 degrees
    about Y (`godot_turn`), and its children's local transforms carry the turn against it, as the Godot bridge's
    lights and cameras are children turned 180 degrees.
- **`build_godot_scene`** makes the nodes into objects, with Godot 4's names and Godot 3's beside them.
  - **Node3D / Spatial / Marker3D:** an object.
  - **MeshInstance3D:** Godot's primitive meshes at Godot's sizes (Box, Godot 3's 2 m Cube, Sphere, Cylinder, Capsule,
    Plane, Quad, Torus). The material comes from the override, the surface override (`surface_material_override/0`,
    Godot 3's `material/0`) or the mesh's own.
  - **StandardMaterial3D / ORMMaterial3D / SpatialMaterial:**
    - colour, made linear from Godot's sRGB, with its alpha;
    - albedo, normal, metallic, roughness and emission maps;
    - metallic and roughness;
    - emission and its energy;
    - transparency: alpha or depth pre-pass become Transparent, scissor or hash become Cutout;
    - cull disabled becomes double-sided;
    - unshaded becomes unlit.

    A ShaderMaterial is said, and comes in as a plain material. Meshes and materials used many times are made once.
  - **Lights:** directional, omni and spot, with colour, energy and range. Godot's spot angle is the half angle, so
    it's doubled.
  - **Camera3D:** fov, near, far and orthographic size. Godot's size is the whole height, so it's halved.
  - **CollisionShape3D:** a Collider (box, sphere, capsule, convex or mesh). Godot 3's box extents are doubled.
  - **Instanced scenes:** read recursively, with their transform override. A scene that instances itself, or that
    nests past 8, is stopped and named.
  - **Instanced models** (.glb, .gltf, .obj, .fbx): read through Blendity's importers via `objects_from_model`, which
    moved from the Unity import into `scene/unity_scene`.
  - **Everything else,** and any script on a node: an **Engine Component** (engine "Godot") holding its section as
    Godot wrote it, with the script's res:// path and its type ("level (Node3D)", "AnimationPlayer").
  - **`visible = false`** makes the object inactive.
  - **GodotOrigin** on each object records the .tscn and the node path ("." for the root, "Body/Shape"). Instanced
    scenes' objects carry none, since their edits belong to their own file.
- **GodotOrigin** also keeps the transform's text as it came in and whether the object or its parent is turned.
  Export leaves an unchanged object's transform line alone.
- **`patch_tscn`** writes edits back. For each edited node:
  - its `transform` line is replaced, or added after the header, but only when the transform differs;
  - `visible = false` is added or taken away.

  Every other byte stays as it was, and CRLF line endings are kept.
- **The editor** (`editor/godot_scene_ui.cpp`):
  - **Import:** `import <file>.tscn`, a drop on the window, or File > Import Model... (a "Godot scenes" filter) brings
    the scene in under one root named for the file, at the view's pivot and selected, as one Undo step. res:// paths
    are found from the nearest project.godot above the file. Notes are counted once each.
  - **Export:** File > Export Godot Scene, or `export tscn [out]`, writes the selected object's scene (found through
    its GodotOrigin, a parent's or one below it) with the edits. It goes to Exports/<file>.tscn, or to `out`. A
    duplicate carries its original's GodotOrigin, so it's counted and left out. An existing file of another kind is
    never written over.

## The Opus review's fixes
- **Majors:**
  - **The axes:** Z had been flipped, against the rest of Blendity (X mirrored), so an instanced model came in turned
    and Live Link would have sent an imported object elsewhere. It is X now, with lights and cameras turned.
  - **Override sections** (an inherited scene's, editable children's: no type, no instance) made copies of the
    nodes. They now edit the instanced node found under its parent by name: transform, visibility, material.
  - **Each format's defaults:** Godot 3's sphere, cylinder, capsule and their shapes are 1 m in radius, a Godot 3
    camera's far is 100, and Godot 4's sphere is 1 m tall whatever its radius.
  - **References in the wrong table:** a .tres's own ids were looked up in the scene's, and ExtResource / SubResource
    weren't told apart. Each .tres now has its own tables, and every reference goes through one resolver.
  - **No cap on instancing:** a shared count stops at 100,000 objects. Scene paths are compared normalized and in
    lower case, and the imported file itself counts as being built.
- **Minors:**
  - material_override is taken before a surface's;
  - an unchanged instance's transform isn't written;
  - emission with no colour is black;
  - long values parse in one pass;
  - numbers that aren't finite are refused and sizes are clamped;
  - the shared `count_notes` and `forward_slashes` helpers replace three copies.
- **Nits:**
  - \r, \b, \f, \v and \uXXXX escapes are read;
  - a line with no " = " is kept with its section;
  - a BOM is read and kept;
  - rotations are compared to a millionth;
  - cylinder and other shapes are named in the notes;
  - Keep Width cameras are said;
  - the inner spot angle is kept within the angle.

## Checked
- **`tests_round373`:**
  - **The parser:** attributes with spaces, a multi-line dictionary, escapes, both reference forms, the sections
    joined giving the file back, and a binary file refused.
  - **A transform:** Godot's +90 degrees about Y at (1, 2, 3) lands at (-1, 2, 3), and goes back to Godot's numbers.
    Godot 3's text form, the turn being its own inverse, and path keys are checked too.
  - **Odd text:** a BOM, an escaped quote and bracket in a header's string, \u escapes, a line with no " = ".
  - **A Godot 4 project imported:** a 4 m box with its red material, a hidden lamp, a camera, a body's collision box,
    an instanced scene placed at its override, an AnimationPlayer and the root's script kept, and a self-instancing
    scene stopped.
  - **The way back:** the box is moved and the lamp shown, then `export tscn` writes exactly those two changes, and the
    project's file is unchanged.
  - **Godot 3:** a scene with Spatial, MeshInstance, a 2 m CubeMesh and an unshaded SpatialMaterial.
  - **The review's cases:**
    - an inherited scene's override moving its node and choosing material_override, with no copy made;
    - Godot 3 ids shared between a .tres and its scene, the texture found in the .tres's own;
    - 1 m default spheres and cylinders;
    - a built-in script;
    - an instanced OBJ's vertex and a marker at the same Godot x landing together;
    - Godot 3 written back with CRLF;
    - the object cap.
  - **The Try-it.**
- **Not checked:** a real Godot project opened in Godot after the export, since Godot isn't installed here.

## Limits
- **A texture held inside a file** (a sub_resource image) isn't read; it's said.
- **A light's or camera's own non-uniform scale** isn't carried exactly through the turn.
- **Exports/<file>.tscn** is shared by scenes of the same name in different folders: name the output for those.
- **What isn't read:** ArrayMesh and imported-mesh resources, Skeleton3D and skins (a model's comes with the model),
  and Godot 3's Z-axis capsules (they come in upright, said).
- **What isn't written back:** names, since a node's name is its path, so renaming isn't written. Objects added here
  aren't written either. Like Unity's, an object moved to another parent writes its local transform under the old
  parent.
