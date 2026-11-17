## 2026-11-16 (round 545): Vertex colours in the pictures, and from files (545b)

**Asked:** Pass 27, worker P1: the second half of the Vertex Paint round (the user's "Yes, add it") - a material that
shows the mesh's vertex colours, and importers that read them. This round covers the CPU renderers and the importers;
the card's raster and path tracer follow (545b2).

**Added (task 0533, ADR 0152)**
- **Material > Vertex Color**: the mesh's render colour layer multiplied into Base Color and Alpha (Blender: a Color
  Attribute node into Base Color; Unity's vertex-colour shaders), before the cutout test. Shown by the CPU rasterizer
  (Shaded and Game views, see-through materials), the CPU path tracer (F12, the Camera Preview) and the texture
  bake. Off by default: existing scenes render as before. The colour factor is capped at 1 for the albedo (an HDR layer
  must not add light).
- The colours reach the renderers per render vertex from a cache on the mesh (kept per render-mesh build, mesh version
  and layer), so a paint stroke never rebuilds the render mesh. A Corner layer's corners sharing a render vertex give it
  their mean.
- **Import:** PLY `red` / `green` / `blue` / `alpha` (bytes as sRGB with linear alpha, floats as linear) become a Point
  layer "Col"; glTF `COLOR_0` (VEC3 / VEC4, float or normalized) a Point layer "Color"; FBX's first colour set a Corner
  layer "Col" (sRGB decoded, as Blender reads it). The weld after a PLY / glTF import keeps the layer and no longer joins
  two vertices of different colours.

**Not yet:** the card (the Vulkan raster's g-buffer, toon, baked and see-through shaders; the card path tracer and
OptiX) - round 545b2; the bakes with their own object lists - the lightmapper, the light probes, the reflection
probes - whose bounce takes the base colour.

**Checked:** both suites - with Blender's libraries CHECKS_LIBS; dependency-free CHECKS_W (Windows) and CHECKS_L (Linux).

<!-- README -->
replace: skins, weight paint (with Blender 5.3's automasking), vertex paint and colour attributes, and IK in a Rigging window; **automatic
with: skins, weight paint (with Blender 5.3's automasking), vertex paint and colour attributes (shown by a material's Vertex Color, read from PLY / glTF / FBX), and IK in a Rigging window; **automatic

<!-- docs/ARCHITECTURE.md -->
after: | `Mesh::sculpt_mask`, `Mesh::face_group` (`scene/mesh.h`)
| `Mesh::render_vertex_colors`, `render_vertex_colors_into` (`scene/mesh.h`); `DrawItem::material_colors`, `PTObject::colors` | The render colour layer per render vertex for a material's Vertex Color: cached per render cache variant, build and version (the free function for worker threads); read by the CPU raster, the CPU path tracer and the texture bake | Blender's Color Attribute node | Unity's vertex-colour shaders (round 545b) |

<!-- docs/USABILITY.md -->
after: | Vertex groups could only be set a selection at a time with one weight.
| Painted or imported vertex colours showed only in Vertex Paint, and PLY / glTF / FBX files lost theirs. (round 545b) | A material's Vertex Color shows them in the views and F12; the importers read them. |
