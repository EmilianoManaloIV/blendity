# Changelog

One entry per round of requests, newest first: what was asked, what changed, and how it was checked. Earlier rounds are summarised from their commits.

## 2026-10-08 (round 11): snapping, a Materials window, Modeling Tools, camera piloting, Plasticity tools

**Asked:** commit and push round 10 and remove old Blendity builds; drawing that snaps to edges parallel to a rotated object, not only world axes; test edge cases (a rectangle, an arc on its side, then Push/Pull); materials as objects you can see and adjust outside any object (Unity); drag a mesh onto another and snap it to the surface, and Unity's vertex snapping; move and frame a camera through the Scene view, including its FOV; useful tools from Plasticity; a separate tools window like UModeler's; sanity and edge-case testing.

**Done first:** round 10 committed and pushed; `Blendity.old.exe` and the stale `build\dev.exe` deleted, leaving only `dist\windows\Blendity.exe`.

**Fixed (found by the new edge-case tests)**
- An arc drawn from corner to corner of a rectangle, bulging outside it, used to "split" the rectangle with points outside it (a broken face). It now becomes a second face sharing that side, as in SketchUp.
- Push/Pull on a free-standing face (nothing attached along its outline, like a shape drawn on the ground) left the bottom open. It now makes a closed solid, keeping the original face as the floor (or lid when pushed).
- Shapes drawn on a rotated object's face came out lined up with the world. The drawing plane now takes its axes from the face's edges (or the object's own rotation).
- Imprinting a shape that lines up exactly with the face's outline could make extra triangles in the ring (a floating-point tie); the ring is now clean quads.

**Added**
- **Drawing inference** (SketchUp-style), from the last point: the plane's own red and green axes, any mesh edge's direction (parallel) and its perpendicular, and square to the previous segment, each shown with a dotted guide; Shift locks to the nearest. **Corner Radius** rounds rectangles, polygons and closed polylines (Plasticity's curve fillet).
- **Materials window:** every material in the scene and every material asset as a preview sphere. Click one to edit it there, drag it onto objects, faces, Hierarchy rows or slots, or right-click to assign, save as an asset, duplicate, or select the objects using it.
- **Modeling Tools window** (UModeler's tool panel): Create (primitives and parametric shapes), Draw, Object tools (Join, Boolean, Reset XForm, Mirror, origin, camera), Edit Mode's tools, UV and snapping, always available. It shares the Edit Mode tool code with the Inspector.
- **Surface snapping:** Ctrl+Shift while moving drops objects onto the surface under the mouse, resting on their lowest point, and optionally aligned to its normal. Dragging objects from the Hierarchy into the Scene view does the same.
- **Vertex snapping** (Unity's V): hold V and drag; the selection's nearest vertex lands on the vertex under the mouse (objects, and selected vertices in Edit Mode), with Unity's square marker.
- **Pilot Camera:** look through a camera and frame the shot by navigating. The camera follows the view, the wheel dollies, Ctrl+wheel changes its FOV (or focal length on a physical camera), and a frame with thirds guides shows the shot. **Align to View** is there too. Both are in the Camera Inspector and the Modeling Tools window.
- **From Plasticity:** **Shell** (hollow a solid, opening the selected faces), **Thicken**, **Draft** (taper side faces by an angle) and a **Radial** mode on the Array modifier.

**Checked:** 1049 unit checks on Windows and 1025 on Linux, 0 failed. New tests cover: drawing on a rotated object and pushing the result through; the rectangle + arc + Push/Pull case; degenerate shapes and lines off the mesh; fillets and drawing into a drawn face; Shell, Draft, Thicken and Radial Array; dropping onto a surface; V vertex snapping with real mouse events; piloting with the wheel; both new windows. Stress results:
- Push/Pull ran 26,620 operations and still has only its 2 long-standing problems. A first run found 36 new zero-area faces from the new floor; fixed.
- The modeling section ran 5,066 operations with 0 problems.
- The editor fuzzer ran 6,000 frames with no crash.

## 2026-10-08 (round 10): materials as assets, drawing tools, UModeler tools, origin fix

**Asked:** push the previous round; keep a changelog; Shift + scale on a face makes a new face, as in Blender; automatic seams from sharp edges; a light "focus" like the camera's; UModeler features Blendity lacks; Unity-style material assets you drag onto objects and slots; removing material slots; a polyline tool for circles, arcs and so on, drawn onto a mesh with snapping; fix the origin not being used as the transform point.

**Fixed**
- **Origin as the transform point.** The toolbar's handle position defaulted to *Center*: the gizmo sat at the bounds centre and rotations turned about it, while scaling still used the origin, so a moved origin seemed to do nothing. It now defaults to *Pivot* (the origin, as Blender transforms one object), the choice is saved, and Center scales about the same point it rotates about; G / R / S follow the same setting.
- The "Blender Transform Keys" preference was never saved (it was missing from the preferences file).

**Added**
- **Shift + drag** a gizmo handle in Edit Mode extrudes first: faces get walls and a new cap, so Shift + scale makes an inset face and Shift + move an extrusion (Blender's E then S, ProBuilder / UModeler's Shift-drag). In vertex / edge mode the edges grow faces.
- **Seams from Sharp Edges** (UV menu, UV Editor, edge-mode tools): a seam on every edge whose faces meet at more than the Sharp Angle (30 degrees, Blender's default) and on edges marked sharp; *+ Unwrap* does both. **Select Sharp Edges** too.
- **Light aiming**: Light > *Aim at Point* is an eyedropper like the camera's focus picker. Click a surface and spot, area and directional lights turn to shine at it (X kept level, Unity's LookAt); the range grows to reach it. `pickfocus` and `aimlight x y z` in the console.
- **Material assets** (Unity): Assets > Create Material, or a slot's *Save as Material Asset*, writes `Assets/Materials/<name>.mat`. Drag one from the Project window onto an object in the Scene view (the face under the mouse picks the slot), onto a face in Edit Mode (all selected faces if it is one of them), onto a Hierarchy row or onto an Inspector slot. Images drag onto a material's texture fields. Selecting a `.mat` shows its full material editor; every renderer using it shares one instance, edits are written back to the file, and scenes refer to the file (the values are kept as a fallback).
- **Material slots**: an x on each slot removes it (its faces move to the slot before, as in Blender), plus **Remove Unused Slots**.
- **Drawing tools** (UModeler / SketchUp): Polyline, Rectangle, Circle, Arc and Polygon, drawn onto the edited mesh or the ground (Inspector > Draw, Mesh > Draw, `draw <shape>`). Snapping to corners (green), midpoints (cyan), edges (red), faces (blue) and the ground, with Ctrl for the grid and Shift for the plane's axes, plus a live length. A closed shape inside a face is cut into it, ready for Push/Pull; a line between points on a face's edges splits it through every bend; anything else becomes a new face or wire edges. With nothing selected it starts a new mesh.
- **From UModeler**: **Follow** (sweep a face along a drawn path, SketchUp's Follow Me, with mitred corners), **Spin / Lathe** (edges or vertices turned round an axis), **Slice** (cut by an X / Y / Z or view plane through the selection, optionally removing a side; Blender's Bisect), **Reset XForm** (bake rotation and scale into the mesh), **Mirror** (applied, welded), **Select Similar** (normal, area) and **Select by Material**.

**Checked:** 1005 unit checks on Windows and 981 on Linux, 0 failed (new tests: Shift-drag extrude, seams, light aiming, material assets with save / load and slot removal, drawing on a face and on the ground, Follow / Spin / Slice, Reset XForm and the pivot). The stress suite ran 4,728 operations of the new and existing tools on 26 awkward meshes, plus 390 random shapes, with 0 problems; the editor fuzzer ran 1,500 frames with no crash.

## 2026-10-08 (round 9): every Blender export format, file dialogs, Boolean, modifier stack, shapes, n-gons

**Asked:** Blender's export options and formats (tested against an installed Blender); Save As dialogs for objects and screenshots; origin handles; Boolean operations; combining meshes; parametric objects; SketchUp-style n-gon editing; extruding vertices and edges; a Blender-style modifier stack.

**Added:** export to OBJ, binary / ASCII FBX, glTF (.glb / .gltf), STL, PLY and USD (.usda) with Blender's options, and import of the same; every format checked both ways in Blender 5.2 (this found that the earlier ASCII FBX did not open in Blender). Native Save / Open dialogs. Join, Separate, Boolean (applied or a live modifier with a wire cutter). Wire edges and vertex / edge extrusion. Edit Origin with handles and click snapping. A modifier stack with Edit Mode / Viewport / Render toggles and Bevel, Weld, Triangulate, Wireframe, Displace, Simple Deform, Cast and Screw. Thirteen parametric shapes. N-gon Mode, Merge Coplanar and the Knife (K).

**Checked:** 963 checks on Windows, 939 on Linux; a new stress section with 0 problems.

## 2026-10-08 (round 8): lights, export, selection fixes

Spot and Area lights with colour temperature (rasterizer and CPU / GPU path tracing); OBJ and FBX export; editing another mesh by clicking it in Edit Mode; the Hierarchy's create menu; Alt+click loops and Ctrl+Alt+click rings; the scene gizmo's views; origin dots; a lockable Camera Preview; a camera focus eyedropper; old app builds removed.

## 2026-10-08 (round 7): Blender's modal editing, edge selection, per-face shading

G / R / S modal transforms with axis locks and typed values; edge mode keeps the picked edges; per-face Shade Smooth / Flat and Mark Sharp; Bevel Clamp Overlap and a dozen more Blender operators; Unity-style colour fields; render resolution presets; docs/USABILITY.md.

## 2026-10-07 (rounds 4-6): GPU rendering, Push/Pull, physical cameras

Vulkan GPU path tracing with ray tracing hardware and combined CPU + GPU rendering; SketchUp-style Push/Pull as a confirmed operator, hardened by a stress test on awkward meshes; tools split by selection mode; Set Origin; physical cameras with depth of field and exposure; edge tools (Bevel, Bridge, Push Through); Adjust Last Operation (F9); Inspector expressions and multi-object editing; surface types (opaque, cutout, transparent, glass).

## 2026-10-06 / 07 (rounds 1-3): Blendity

The editor itself: Blender's mesh model and Edit Mode behind a Unity-style editor; UV unwrapping, materials and textures; a deferred rasterizer and a path tracer; Blender's bundled libraries (Embree, OpenImageDenoise, OpenSubdiv, OpenColorIO, Manifold, Jolt and others), each optional; SIMD kernels; the Learn tab; stress tests.
