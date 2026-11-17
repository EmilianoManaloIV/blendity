# ADR 0152: Colour attributes on Mesh, and the Vertex Paint mode

- **Status:** accepted (2026-11-16; round 545, task 0525; the hub approved the file list and the operator rule). The user said yes to a Vertex Paint mode ("Yes, add it",
  relayed by the hub session, 2026-11-16); the file list waits for the hub's overlap check.
- **Builds on:** task 0046 (Weight Paint), task 0519 (automasking), ADR 0001 (no new dependency)
- **Requirements:** R-rigging / R-texturing (painting on meshes); Blender 5.3 parity

## Context
Blender keeps colours on a mesh as **colour attributes**: named layers, each on a domain - Point (one per vertex) or
Face Corner (one per corner, so a seam can change colour sharply) - and of a type, float or byte RGBA. One is the
active (painted) layer and one the render layer; Vertex Paint mode paints the active one. Game engines read them as
the mesh's vertex colours (glTF `COLOR_0`, FBX `LayerElementColor`, Unity's `Mesh.colors`).

Blendity's `Mesh` has no colour data. It does have two ways per-element data already survives editing:
- **per vertex**, as the vertex groups do: operators that add a vertex call `Mesh::copy_vertex_weights(from)`, and
  `remove_loose_verts` remaps the groups that fitted the old numbering;
- **per corner**, as the UVs do: `normalize()` resizes them to the corners, and every operator that rebuilds faces
  carries them (19 files in `src/scene`).

## Decision
1. **`Mesh::color_attributes`**: a list of `ColorAttribute { std::string name; Domain domain (Point | Corner);
   std::vector<Vec4> rgba; }` (float RGBA, linear, as Blender's float colour - byte colours are imported into it),
   with `active_color` (the painted one) and `render_color` (the exported and shaded one), -1 for none.
2. **What each operator does to a layer** (the hub's condition, 2026-11-16). A layer is carried exactly, interpolated
   or dropped whole - **never misplaced, never a crash**:
   - **Point layers go where vertex groups go**, through the same code:
     | Operator | Point colours |
     |---|---|
     | Copy-on-write (`mesh_make_mutable`, any `MeshPtr` copy), save / load, undo | exact (Mesh's copy constructor) |
     | Mirror, Array, radial Array (modifiers) | exact (`copy_vertex_weights`) |
     | Delete, Delete Loose, Dissolve (vertices, edges, faces, limited), Merge / Collapse | exact (`remove_loose_verts`, `remove_vertices`); a merge keeps the survivor's colour |
     | Join | exact, layers matched by name; a side without the layer takes white |
     | Separate (selection, loose parts, material) | exact (`extract_faces`, `split_by_part`) |
     | Subdivision (modifier), Boolean (modifier), Bevel, Screw, Wireframe | interpolated: the modifier stack's carry by position (`run_modifier`: the vertex groups' barycentric transfer map) |
     | Extrude, Loop Cut, Inset, Subdivide edges, Poke, Split, Duplicate, Bevel tool | interpolated: the editor's carry by position at the undo step (`carry_vertex_groups`) |
     | Sculpt Dyntopo (splits, collapses, the stroke's compaction) | interpolated: a new vertex the mean of its edge's ends, a collapse the mean of the two; the loops cost nothing on a mesh without layers (printed by its test) |
     | Voxel Remesh, Quad Remesh | interpolated by position (`carry_colors_rebuilt`, beside their group transfer); Corner layers reported dropped |
     | Decimate | exact: each kept vertex its own colour (meshopt keeps original vertices) |
     | An edit that adds vertices without carrying a layer, then renumbers (a delete, a weld, Dyntopo's compaction) | the renumbering empties the layer it finds stale (`remap_color_vertices`), so a count coincidence can never make it fit on the wrong vertices; the editor's carry then restores it by position. A layer an operator carried exactly is never resampled (the review of round 545: a by-position resample of every layer at the same count would have left Grab-stroked paint behind) |
     | Boolean *apply* and object-mode Subdivide (they build a new Mesh) | dropped, as their vertex groups are today - a known gap, listed in the changelog |
   - **Corner layers** carry `corners`, the corner list they belong to, and count only while it equals the mesh's
     `corner_verts`. Kept exactly by copy-on-write, save / load, undo, winding flips (`reverse_corner_layers`, used by
     Flip Normals and the exporters) and vertex renumbering (`remove_loose_verts`). The export's Triangulate option
     averages a Corner layer per vertex before it rebuilds the corners: softened, never dropped. **Dropped whole** by every operator
     that rebuilds corners (the Edit Mode tools, the modifiers, Join, Separate) - even one that ends with the same
     corner count, which a size check alone would have let through onto the wrong corners. Carrying them through
     `FaceBuilder` and interpolating them as the UVs are is the follow-up.
   - `sync_attributes` (the operators' common tidy-up) drops a Corner layer that no longer matches; it leaves a Point
     layer of the wrong size for the carry by position to mend (dropping it there would lose it before the carry ran).
     Readers (the Scene view, the exporters) only ever read a layer that fits.
   - **Point is the default** (the hub, 2026-11-16): entering Vertex Paint adds a Point layer, and New Point Layer is
     the first button. A Corner layer comes only from New Corner Layer (or an importer, round 545b), because today
     only Point layers survive the common edits.
   - **Never lost silently:** a layer an edit dropped is named in a warning in the log and in a note in the Inspector
     (under the MeshFilter, until OK is pressed), which says Undo brings it back. `Mesh::dropped_colors` carries the
     names from `prune_colors` to the editor's carry at the undo step.
3. **Saved** in the scene text as one block per layer; old files load with none.
4. **Vertex Paint** is a mode beside Weight Paint and Sculpt (one mode at a time), painting the active layer with
   Blender's brushes (Draw with its blend modes, Blur, Average, Smear) and the automasking of task 0519, shared with
   Weight Paint in one file.
5. **Shown** in the Scene view through the colours path Weight Paint already uses (`RasterFrame::vertex_colors`);
   **exported** as glTF `COLOR_0` and PLY colours (round 545a). **A material's Vertex Color** multiplies the render
   layer into Base Color and Alpha (round 545b1: the CPU rasterizer, the CPU path tracer for the gathered objects, the
   texture bake; 545b2: the card). The colours reach a renderer per render vertex from a cache on the mesh, one slot per
   render cache variant, keyed on the render mesh's build and the mesh's version - never part of the render mesh's
   topology, so a paint stroke doesn't rebuild it. A Corner layer's corners that share a render vertex give it their
   mean (each mesh corner once): a colour seam inside one render vertex softens there. **Importers** read PLY's
   colours and glTF's `COLOR_0` as a Point layer, FBX's first colour set as a Corner layer (round 545b1).

## Consequences
- Every renderer and exporter reads one layer (`render_color`), so a mesh with several layers behaves predictably.
- Operators that rebuild corners without carrying a Corner layer drop it; the round lists which ones do, and a test
  pins the drop. Point layers survive every operator that keeps vertex groups.
- Memory: 16 bytes per element per layer (float RGBA). Byte storage would quarter it; it can come later behind the
  same struct if large scans need it.

## Alternatives considered
- **Byte RGBA only** (Blender's default for painting): smaller, but loses HDR colours from imports and makes blending
  lossy; float is Blender's other type and what the renderers use.
- **A generic attribute system** (any name, domain and type, as Blender's): the right end state, but it would touch
  every operator; colour layers are what users ask for now, and the struct can be generalised later.
