# Changelog

One entry per round of requests, newest first: what was asked, what changed, and how it was checked. Earlier rounds are summarised from their commits.

## 2026-10-08 (round 13): Push/Pull without overlapping faces, Delete Loose, region inset, depth of field everywhere

**Asked:** commit and push round 12; check Push/Pull and similar tools for the overlapping-face artifacts still showing up; Blender's Delete Loose; inset several faces individually or as one; clearing a number field and pressing Enter should give 0, as in Unity; a New Material made in the Inspector should appear in the Materials folder; draw polygons from a face's exact centre, as in Plasticity; let depth of field blur the foreground while the background stays sharp.

**Done first:** round 12 committed and pushed.

**Fixed: Push/Pull's overlapping faces.** A new check finds faces lying on top of each other, or a hair apart (they z-fight). On the stress suite's 26,620 Push/Pull operations the old code produced 1,502 such results, plus 2 broken (open) meshes. Now there are 676 and none broken: 0 problems. What's left is mostly holes through curved surfaces and deliberately broken inputs (unwelded faces, inside-out normals). The causes, each fixed:
- **Stopping 2 mm short.** Push/Pull stopped just short of any surface in its way. A block pushed back down hovered a hair above the surface, with paper-thin walls, and two halves pulled to the same height left a sliver between them. Now, where the walls beside the selection end (flush), it goes exactly there: the walls vanish and the faces merge back, as in SketchUp. Asked for more, it carries on from there as a second step, so pushing a pulled block past flush makes a pocket, and pulling one half past the other works in one go.
- **Sliding past a neighbour counted as hitting it.** Edges lying on the selection's outline, such as a neighbouring block's top edge, stopped the move. Only geometry inside the swept area does now.
- **New walls in a neighbour's plane.** With Merge Coplanar off, pushing along a coplanar side built a wall inside that side. And when a neighbour stretched along one edge but got a wall along another, the wall covered the strip it grew into. Now a neighbour that stretches takes every side lying in its plane.
- **Folds at corners.** Faces touching the outline only at a corner kept the old corner, making a fold outside the solid. When every face around the outline contains the move's direction, the corners now simply slide (Blender's move along the normal).
- **Faces flattened to nothing** are dissolved into their neighbour, the way Blender's Dissolve Degenerate does it, instead of being left to flicker or turn inside out. Back-and-forth spikes left by welding are removed too.

**Added**
- **Delete Loose** (Blender: Mesh > Clean Up > Delete Loose): removes vertices and wire edges that no face uses, within the selection, or everywhere when nothing is selected. It's in the Edit Mode tools, the Mesh menu and the Modeling Tools window, and also works on whole objects.
- **Select Overlapping:** selects faces lying on top of each other, so leftovers (or imported ones) are easy to find and delete.
- **Inset Individual** switch (Blender's Inset > Individual), also in the F9 panel:
  - On: each face is inset by its own amount.
  - Off: the whole selection is inset as one face. Only its outline moves in, by a thickness, and the edges between the selected faces stay.
- **Empty number fields give 0**, as in Unity, within the field's limits (a field with a minimum of 1 gives 1).
- **New Material in an Inspector slot** (and New Material of Type) now saves a `.mat` in `Assets/Materials`, so it shows up in the Project and Materials windows like any other.
- **Face centre (Plasticity):**
  - When drawing, a face's exact centre (its area centre, not the average of its corners) snaps, shown in orange.
  - **Start at Face Center** makes circles, polygons and centre rectangles begin there wherever you click on the face.
- **Depth of field everywhere:**
  - The Game view, the Shaded Camera Preview and a piloted Scene view now show the camera's depth of field, not only path-traced renders.
  - Nearer things blur as well as farther ones. A blurred foreground spills softly over a sharp background, and a blurred background never spills over a sharp foreground (a "scatter as gather" pass, as in EEVEE).
- **Keep Focus on Point:** Pick Focus Point now keeps that point in focus while the camera moves (Blender's Focus Object). Piloting no longer moves the focus to the orbit pivot.
- Console: `insetmode`, `newslotmat`, `focus`, `position`, `drawmode facecenter`, `editop delete_loose`, `editop select_overlapping`.

**Checked:** 1309 unit checks on Windows and 1285 on Linux, 0 failed. New tests:
- Push/Pull sequences:
  - A square pulled up then pushed back flush, then pushed past it.
  - A pocket pulled back flush.
  - Two halves pulled to the same height, to different heights and past each other.
  - One half pulled and the other pushed.
  - Inset, push in and pull back.

  Each must stay closed, keep the exact volume and have no overlapping faces.
- Delete Loose, with and without a selection.
- Region and individual inset: face counts, a closed result, the inner area.
- Clearing float and int fields (with a minimum).
- New Material assets from the Inspector.
- Face-centre drawing with real clicks, and the area centre of an L-shaped face.
- Rasterized depth of field: the in-focus background untouched, the foreground's blur spilling over it, and the reverse.
- Focus tracking while moving and piloting.

Stress: the modeling section ran 5,716 operations with 0 problems, and the editor fuzzer ran 6,000 frames with no crash.

**Note:** your two material files in `Assets/Materials` (`New Material.mat`, `Solid Material.mat`) were moved to the Recycle Bin at 1:34 PM, while your own Blendity session was running. They can be restored from there.

## 2026-10-08 (round 12): drawing across faces, camera piloting fixes, asset folders, face material picker, drawing modes, guide lines, frame rate

**Asked:** commit and push round 11; make the camera actually adjust while piloting; fix drawing a circle onto a surface whose faces overlap inside the circle; renaming a material should rename its file; organise assets into folders like Unity; pick the material itself when assigning faces; draw shapes uniformly (from the centre), not only corner to corner, as in Plasticity; set the editor's frame rate and explain the performance cost; delete things in the Project window; construction lines to line drawings up with, as in Plasticity.

**Done first:** round 11 committed and pushed.

**Fixed**
- **A circle (or any closed shape) drawn over several faces** used to be added as a separate face lying over them. It is now cut into every face it covers, with a vertex wherever it crosses an edge, and all the pieces inside come out selected for Push/Pull. Parts hanging off the surface become wire edges.
- **Piloting a camera:**
  - When the camera's frame was narrower than the view, the frame didn't match what the camera saw, and starting to pilot changed the camera's field of view. The frame is now exactly the camera's view, and starting changes nothing.
  - A physical camera's lens was worked out from the wrong side of the sensor; it now follows the camera's Sensor Fit.
  - Moving the camera any other way while piloting (gizmo, Inspector, undo) now moves the view with it, instead of being overwritten.
- **Copied materials kept the wrong name.** A `.mat` copied outside Blendity kept the old name inside it. The file name now wins, as in Unity, so nothing gets renamed by surprise.

**Added**
- **Project window like Unity's:**
  - A nested folder tree, and a **+** menu or right-click for **Create > Folder / Material** in the current folder.
  - Drag files or folders onto a folder, a breadcrumb or `..` to move them. Dragging a scene material from the Materials window onto a folder saves it there as an asset.
  - **F2 / Rename** renames in place.
  - **Delete / Del** asks first, then sends the item to the **Recycle Bin** (Trash on Linux and macOS), where it can be restored. Only things inside Assets, research papers or screenshots can be deleted.
  - Moving or renaming keeps everything working: the shared material, texture paths, `.scene` files that mention it, and undo. A deleted material stays on its objects as a scene material.
- **Material name = file name**, both ways: renaming a material renames its `.mat`, and renaming the file renames the material.
- **Face Material** field in Edit Mode (Inspector and Modeling Tools): a grid of preview spheres with every material asset (from any folder), the object's slots and the scene's materials. Pick one and the selected faces use it; its slot is found or added for you.
- **Drawing modes from Plasticity:**
  - Rectangles from a **Corner**, the **Center**, or **3 Points** (one side at any angle, then the width), with **Square** to keep the sides equal.
  - Circles and polygons from the **Center**, **2 Points** (across) or **3 Points** (on the circle).
- **Guide lines**, Plasticity's lines and SketchUp's guides:
  - The new **Guide** draw shape, or **Guides from Edges**. They show as dashed lines and are saved with the scene, and adding them can be undone.
  - Drawing snaps to where two guides cross (or where one pierces the drawing plane), along a guide, and parallel to it.
- **Editor frame rate** (Preferences > Performance, and the Profiler):
  - A cap from 30 to 240 fps or Unlimited, now applied to mouse movement too (before, every mouse move drew a frame).
  - Redraw **only when something changes** (the default) or **always**.
  - It shows the measured frame cost and what that reaches on this machine, warns when the cap is over budget, and explains what each setting costs (CPU, battery, input lag, path-tracing samples). The stats overlay shows the live rate.
- Console: `drawmode`, `guide`, `clearguides`, `guidesfromedges`, `fps`, `redraw`, `preferences`, `facemat`, `mkfolder`, `moveasset`, `renameasset`, `deleteasset`.

**Checked:**
- 1209 unit checks on Windows and 1185 on Linux, 0 failed. New tests cover:
  - Circles across a 4 x 4 grid: the pieces' areas add up, and pulling them makes a solid.
  - The circle through grid corners, hanging off the edge, and along existing edges.
  - A circle overlapping a drawn rectangle on a cube, then Push/Pull, which stays closed.
  - Piloting in a tall view, moving the camera from the Inspector, and a physical camera's lens.
  - Folder move and rename, scene-file rewrites, undo after a move, a copied `.mat`, delete to a scratch trash, and refusing to delete outside Assets.
  - Face material picking.
  - Every drawing mode.
  - Guide snapping with real mouse clicks, save and load, and undo.
  - The frame-cap timing.
- Stress: the modeling section now also draws random circles and squares across faces on all 26 awkward meshes, plus degenerate loops: 5,716 operations, 0 problems. Push/Pull still has only its 2 long-standing problems. The editor fuzzer ran 6,000 frames with no crash.
- **Safety:** the tests and the fuzzer now work in a scratch project and a scratch trash, because the Project window can move and delete files. Your `Assets` folder was checked to be unchanged after full runs.

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
