## 2026-11-16 (round 545): Colour attributes and Vertex Paint (Blender 5.3)

**Asked:** Pass 27, worker P1, after round 539: the user said yes to a Vertex Paint mode ("Yes, add it") - colour
attributes on meshes, saved, shown and exported, painted with Blender's brushes and the automasking of round 539. The
material input and the importers are round 545b.

**Added (task 0525, ADR 0152)**
- **Colour attributes** on `Mesh`: named layers of linear RGBA, one per vertex (Point, the default) or per face corner
  (Corner), with an active (painted) and a render (shown and exported) layer. Saved with the scene; old files load
  with none. `colorattr add|remove|active|render|rename|fill` in the console.
- **Vertex Paint** (the Inspector's MeshFilter, under the Rigging row; Blender's `paint_vertex.cc`): Draw with Mix, Add,
  Subtract, Multiply, Lighten or Darken (Ctrl: the inverse colour, or the opposite blend - Blendity's own reading, as
  Weight Paint's; Blender paints its secondary colour), Blur, Average and Smear; a layer keeps HDR colours (not clamped
  to 1);
  colour, alpha, radius, strength, falloff, Mirror X, Front Faces Only and round 539's automasking. One stroke, one
  undo step; a stroke gives an element at most one dab's worth, as Weight Paint does. Console `vpaint`, `vpbrush`,
  `vpblend`, `vpcolor`, `vpset`, `vpautomask`, `vpstroke`; a quick-menu set; a Learn lesson section with a Try-it and a
  question.
- **Export:** glTF writes the render layer as `COLOR_0` (linear float RGBA; a Corner layer splits the vertices where the
  colour changes); PLY as red, green, blue and alpha bytes (sRGB, as Blender's exporter writes them).
- **What edits do to a layer** (ADR 0152's table; never misplaced, never a crash):
  - Point layers go where vertex groups go: exact through copy-on-write, Mirror, Array, Delete, Dissolve, Merge, Join
    (by name) and Separate; interpolated by position through the Subdivision, Boolean, Bevel, Screw and Wireframe
    modifiers and through Extrude, Loop Cut, Inset and Subdivide.
  - Corner layers stay through Flip Normals, renumbering, save / load and export (the export's Triangulate option
    averages them per vertex first, softened rather than dropped), and are dropped whole by edits that rebuild the
    corners. The Inspector says so ("Undo brings it back") and the log names the layer.
  - Known gaps: Boolean *apply* and object-mode Subdivide build a new mesh and drop the layers, as they drop vertex
    groups today; Corner layers through Edit Mode tools and modifiers.

**Changed**
- Weight Paint and Vertex Paint share one stroke engine (`src/editor/paint_automask.cpp`, renamed from
  `weight_paint_automask.cpp`): the brush disc, falloff, Front Faces Only, Mirror X and the automask.

**Checked:** both suites - with Blender's libraries CHECKS_LIBS; dependency-free CHECKS_W (Windows) and CHECKS_L (Linux).

<!-- README -->
replace: skins, weight paint (with Blender 5.3's automasking) and IK in a Rigging window; **automatic
with: skins, weight paint (with Blender 5.3's automasking), vertex paint and colour attributes, and IK in a Rigging window; **automatic

<!-- docs/USABILITY.md -->
after: | Vertex groups could only be set a selection at a time with one weight.
| A mesh had no vertex colours: nothing to paint them with, and exports to games carried none. (round 545) | Colour attributes, Vertex Paint with Blender's brushes and automasking, glTF COLOR_0 and PLY colours. |

<!-- docs/ARCHITECTURE.md -->
after: | `Mesh::sculpt_mask`, `Mesh::face_group` (`scene/mesh.h`)
| `Mesh::color_attributes`, `ColorAttribute`, `meshops::transfer_point_colors` (`scene/mesh.h`) | Colour layers per vertex (Point) or per corner (Corner), an active and a render one; Point layers carried as vertex groups are, Corner layers only while their corner list matches, a dropped one reported (ADR 0152) | Blender's colour attributes (`blenkernel/intern/attribute.cc`) | Unity's `Mesh.colors`, glTF `COLOR_0` (round 545) |
| `PaintMeshHelpers`, `PaintAutomask`, `Editor::paint_dab_amounts` (`editor/paint_automask.h`) | The stroke engine Weight Paint and Vertex Paint share: helpers per mesh, the brush disc, falloff, Front Faces Only, Mirror X, the automask | `editors/sculpt_paint/mesh/sculpt_automasking.cc` | - (round 545) |
