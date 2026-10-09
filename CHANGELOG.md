# Changelog

One entry per round of requests, newest first: what was asked, what changed, and how it was checked. Earlier rounds are summarised from their commits.

## 2026-10-08 (round 17): drawing on any axis plane, turning drawn shapes cleanly, the keymap dropdown, the Utah teapot, deleting materials, curved surfaces

**Asked:** commit and push round 16; draw polylines on the YX and YZ planes as well as XZ, with an axis-lock workflow like Extrude's, usable with Follow; rotating a face just drawn onto a face left artifacts; the keymap preset dropdown in Preferences disappeared when clicked; the Utah teapot with a camera and a point light as the scene you start with; deleting a material from the Materials window; more edge-case testing on curved surfaces for Push/Pull and the other tools.

**Done first:** round 16 committed and pushed.

**Fixed**
- **Dropdowns inside dialogs.** A dropdown opened inside Preferences (the keymap preset, the frame-rate cap) closed every popup, the dialog included, so the list vanished. It now opens on top of the dialog. Picking an item, or clicking the field again, closes only the list.
- **Turning a shape drawn on a face.** Its ring of faces twisted over itself because the inner corners turned while the outer ones stayed. The ring is now rebuilt between the two outlines whenever a move folds it, live during gizmo drags and Blender's G / R / S. The selection follows.
- **Follow on a face of a solid** no longer leaves the old face inside as a start cap. That cap put three faces on each of its edges. The solid now grows the way Extrude grows it.
- **Regions that touch themselves at a vertex** (a cone's apex between two arms of a selection, a pinch on a torus) can now be extruded, inset and refilled without an edge ending up in four faces. Each such vertex gets one copy per side.
- **Smart Fill** fills holes whose outline touches itself one lobe at a time. Before, it made one face that ran through the shared vertex twice.
- **Inset Region** asked to go further than the region is wide now backs off until no face turns over, instead of folding the ring onto the inner faces.
- **Bevel** on curved surfaces no longer leaves zero-area patches where two bevels meet. The twin arcs are welded instead.
- **Push/Pull through a curved region** no longer leaves faces lying on the opening. When the cut can't open the far side cleanly, it stops at the far side, as it already does for other unclean holes.

**Added**
- **Axis planes while drawing.** X, Y and Z put the shape on the YZ, XZ or XY plane, like an axis lock on Extrude:
  - Before the first click, the plane goes through the first point. Pressing the same key again goes back to the surface's own plane.
  - Mid-polyline, the plane turns through the last point, so one line can climb a wall and then run across a floor. Enter keeps it as wire edges: a 3D path for Follow.
  - A closed loop that turned planes stays a loop of wire edges, since it isn't flat.
  - Also in the Inspector and Modeling Tools (Axis Plane), and the console: `drawmode axis x|y|z|none`.
- **The Utah teapot.** GameObject > 3D Object > Teapot is Martin Newell's 32 Bezier patches, tessellated and welded, with UVs. A new project, and File > New Scene, now start with it on a ground plane, along with a Main Camera and a Point Light (Blender's start-up cube, camera and light). Your existing SampleScene is unchanged.
- **Delete Material** in the Materials window, from the right-click menu or the Delete key over the window:
  - It asks first and shows how many slots use the material.
  - Those slots are emptied, so their faces get the default material.
  - A material asset's .mat file goes to the Recycle Bin.
  - Undo puts the slots back.
  - Console: `deletemat <name | Assets/...mat>`.
- **A curved-surfaces stress section** (`blendity_stress --only curved`). It runs 4,396 operations on 10 curved meshes: UV and ico spheres, a torus, a cylinder, a cone, Catmull-Clark and bevelled boxes, a bumpy blob, the teapot and an open hemisphere. The operations:
  - Push/Pull on single faces and on curved regions, in, out, through and onto.
  - Inset (individual and region), Extrude, Bevel, Poke, Triangulate / Tris to Quads.
  - Shapes drawn on and across curved faces, then turned.
  - Follow, Smart Fill, Bridge, Push Through, Delete Loose, Merge by Distance and Auto Smooth.
- Console: `newscene`.

**Checked:** 1643 unit checks on Windows and 1619 on Linux, 0 failed. New tests:
- The preset dropdown in Preferences opened, closed and picked with real clicks while the dialog stays open.
- The teapot's welding, facing, UVs and size.
- The starter scene, including save/load and New Scene.
- Drawing up and over a cube with Z then X, then Follow along that path into a closed solid.
- A bent closed loop staying wire edges.
- A drawn rectangle turned 30, 45, 90 and 170 degrees with R (no bad triangulations, no overlaps, still closed, still selected), and the ring repair on its own.
- Delete Material with its confirmation, a scene material, an asset going to the (scratch) trash, and undo.
- A cone region pinched at its apex extruded, inset, and refilled with Smart Fill.

Stress:
- Curved surfaces went from 59 problems to 4 (zero-area faces in rare corners) and from 283 results with faces on top of each other to 91.
- Push/Pull: 0 problems, and the results with faces newly on top of each other fell from 676 to 390.
- The modeling section: 6,080 operations with 0 problems.
- The editor fuzzer: 6,000 frames with no crash.

## 2026-10-08 (round 16): drawing in open space, a UV gizmo, Merge by Distance in Edit Mode, Z-fighting highlights, Delete in the Hierarchy

**Asked:** commit and push round 15; draw outside of a face (UModeler) or in open space (Plasticity); the Scene view's gizmo in the UV editor for moving, scaling and rotating; Blender's Merge by Distance; Z-fighting faces stayed highlighted in Object Mode and after the object was deleted; the Delete key for objects in the Hierarchy.

**Done first:** round 15 committed and pushed.

**Fixed**
- **Delete in the Hierarchy.** In Edit Mode, Delete removed the selected faces of the mesh being edited even with the Hierarchy focused. It now removes the selected objects there (leaving Edit Mode first), as in Unity.
- **The Project window's Delete key** now also needs the mouse over the Project window, so a Delete meant for the Hierarchy or the Scene view can never reach a file selected there earlier. (Several files went to the Recycle Bin this afternoon while the editor was open, which is what this guards against.)
- **Z-fighting highlights** are dropped once their object is deleted or its mesh changes. They only show while you edit one of the objects involved, or while the Z-Fighting panel is open after checking from it, so nothing looks selected in Object Mode.
- **Drawing rectangles:** the opposite corner no longer snaps onto the first corner's axes, which could flatten the rectangle into a line when seen at a shallow angle.

**Added**
- **Drawing in open space** (Inspector and Modeling Tools, while drawing):
  - Clicks off the mesh go onto an **Open Space Plane**: the ground, a **front** (XY) or **side** (YZ) plane, a plane **facing you** through the point you orbit, or the **last face's plane**, to keep drawing past a face.
  - A **Plane Offset** moves the plane along its normal.
  - **Draw in Open Space Only** ignores surfaces like Plasticity's construction plane. Corners and edges of the mesh still guide the point, which stays on the plane.
  - A faint grid shows the plane under the cursor. A closed shape there becomes a new face; an open one becomes wire edges.
- **The UV editor's gizmo**, like the Scene view's, at the centre of the UV selection:
  - **Move** has U (red) and V (green) arrows and a centre square for free movement. **Rotate** has a ring. **Scale** has U and V handles and a centre box for uniform scaling. **Transform** (Y) shows all three.
  - W, E, R and Y pick them in the UV editor too, and Ctrl snaps.
- **Merge by Distance in Edit Mode** (Merge & Clean Up; Blender preset: Alt+M):
  - Selected vertices closer than the distance weld (all of them when nothing is selected), and it reports how many were removed.
  - F9 adjusts the **Merge Distance** and **Unselected**, which lets selected vertices weld onto unselected ones, as in Blender.
- Console: `drawmode plane ground|front|side|view|face [offset]`, `drawmode space on|off`, `zfight clear`, `delete`, `editop merge_distance`.

**Checked:** 1573 unit checks on Windows and 1549 on Linux, 0 failed. New tests:
- Clicking a Hierarchy row and pressing Delete or Backspace, also from Edit Mode, with a Project file selected and focused earlier that must survive.
- Drawing with real clicks on the front plane at a depth, on the view plane, and "open space only" over a cube.
- Merge by Distance on an unwelded cube (24 to 8 vertices, closed), its F9 distance, selected only, and Unselected.
- Dragging the UV gizmo's U arrow, V scale handle and rotate ring with real mouse events.
- Z-fighting outlines gone in Object Mode, and pruned when the objects are deleted.

Stress: Push/Pull 0 problems; the modeling section 6,080 operations with 0 problems; the editor fuzzer 6,000 frames with no crash.

## 2026-10-08 (round 15): rebindable keymaps with presets, draw axes, a Z-fighting check, UV editing tools, grouped Edit Mode tools

**Asked:** commit and push round 14; local or global snap axes for drawing; put the selection tools in one place and combine other tools where they can be; a Z-fighting check that says which faces are a problem and can be removed; more UV tools (selecting faces, rotating, scaling, selecting and moving whole islands); re-render the Camera Preview whenever a light's colour temperature changes; rebindable shortcuts in Preferences, with keymaps for other programs (Blender, Maya and more).

**Done first:** round 14 committed and pushed.

**Fixed**
- **Colour temperature didn't refresh the Camera Preview.** The scene's render fingerprint, which decides when the preview, the Rendered view and the live preview re-render, only included a light's type, colour, intensity and range. It now includes every light setting: temperature, spot angles, area size, shadows.

**Added**
- **Keymap** (Preferences > Keymap):
  - Every shortcut is a named action (about 75) with a context: anywhere, in Edit Mode, over the Scene view, or the Scene view in Edit Mode. So one key can mean different things in different places, as before.
  - **Presets:** **Unity** (Blendity's own), **Blender** (G/R/S, E, X, M, F, Ctrl+R...), **Maya** (Q/W/E/R, F8-F11 for component modes, B for soft select...), **3ds Max** (W/E/R, Z to frame, 1/2/4 sub-objects, Shift+E...) and **SketchUp** (P, R, C, A, L, M, Q, Space...).
  - Click a binding and press the new key (Esc cancels, Backspace clears). Actions are found by name or by key, and clashes within overlapping contexts are shown.
  - Changes are saved with the preferences and can be reset. The old "Blender Transform Keys" option still works on top of Unity.
- **Snap Axes: Local or Global** for drawing (Inspector and Modeling Tools). Local follows the face's edges or the object's rotation; Global uses the world's axes. Parallel and perpendicular edge snaps and guides work either way.
- **Z-fighting check** (Modeling Tools > Check: Z-Fighting; "Select Z-Fighting" in Edit Mode):
  - Finds faces lying on top of each other, within objects and between objects (in world space).
  - Each pair says what it is and what can go: a duplicate (remove one), both sides of an inner wall (remove both), a face hidden under another (remove it), or a partial overlap (move one).
  - Pairs have Select and Remove buttons, there's **Remove Suggested** for all of them, and the faces are outlined in the Scene view (red: can go).
  - Objects merely resting on each other aren't flagged.
- **UV editor:**
  - **Vertex / Face / Island** select modes (1/2/3). Click inside a face, box-select by face centre, or click or drag a whole island.
  - **Sync** with Edit Mode's face selection.
  - **All / None / Invert / Grow to Islands.**
  - **Rotate ±90** or by an angle, **Scale** by a factor, **Move** by an offset, **Flip U / V**, **Fit** to 0-1, **Center**, and **Align** (left, right, top, bottom, straight U / V).
  - Arrow keys nudge the selection (Shift: further).
- **Edit Mode tools in groups:**
  - **Select** first: every way of selecting in one place, with All and None. Then Create & Extrude, Cut & Divide, Merge & Clean Up, Deform, and Shading & UV.
  - Each group folds away; the Mesh menu has the same groups.
  - Mark / Clear Seam, Mark / Clear Sharp, Shade Smooth / Flat, To Tris / Quads and Grow More / Less each share one button.
  - Extrude Individual is now a switch on Extrude, like Inset's.
  - Select Overlapping became the Z-fighting check.
  - The doubled Knife and Merge Coplanar buttons are gone.
- Console: `keymap <preset>`, `keymap bind <action> <keys>`, `keymap clear <action>`, `zfight [scene] / zfight fix`, `uvsel ...`, `uvxf ...`, `drawmode axes local|global`, `duplicate`.

**Checked:** 1527 unit checks on Windows and 1503 on Linux, 0 failed. New tests:
- Light settings change the render fingerprint.
- Every preset's key layout, with no clashes in any preset, and chords that print and parse back.
- Real key presses under Unity and Maya, a rebind, clash detection, and overrides saved and loaded.
- Local and global draw axes on a rotated cube.
- Z-fighting: a duplicated cube fixed with one copy's faces removed, the inner wall of two joined boxes, and a hidden face versus a partial overlap.
- UV face and island selection and every transform, synced with Edit Mode.
- Every operator in a group, all selection tools in Select, and the Extrude Individual switch.

Stress: Push/Pull 0 problems; the modeling section 6,080 operations with 0 problems; the editor fuzzer 6,000 frames with no crash, with keys going through the new keymap.

## 2026-10-08 (round 14): archways pushed through cleanly, drawing over earlier edges, Smart Fill, Auto Smooth, camera sequences

**Asked:** commit and push round 13; fix the face artifacts in the half circle of an archway pushed through; a Smart Fill that patches open edges into a surface, tested where the edges converge to no area; drawing on a face and pushing or pulling through should work smoothly with edges already on either face (rectangles, circles, arcs); an option to shade bevels and other round surfaces smooth automatically; render the scene from a sequence of cameras.

**Done first:** round 13 committed and pushed.

**Fixed**
- **Archway artifacts.** Where a hole comes out of a face, the face around it was built by zipping its outline to the hole's outline by angle. Seen from the wall's far corner, an arch's curve runs "backwards", so some faces came out flipped, on top of their neighbours. That's the flicker in the half circle. Now every zipped result is checked: all faces must face the right way and add up to exactly the ring's area. When they don't, the ring is made of two faces joined by bridges, which works for any hole shape. Archways with 4-, 8- and 16-segment arcs, pushed through with Push/Pull or Push Through, now give a closed wall with the exact opening, no overlapping faces and every face triangulated exactly.
- **A selection reaching the wall's edge** (the doorway, the arch and the wall above it) pushed through left a folded face. Push Through now welds its result like Push/Pull does.
- **Scripted drawing on non-convex faces.** `drawpoint` found the face under a point with a test that only worked for convex faces, so a point on the ring around an earlier circle counted as "on the ground". Mouse drawing was not affected.

**Added**
- **Smart Fill** (Edit Mode tools, Mesh menu, Modeling Tools for whole objects). Open edges, whether sides of faces with nothing beyond or wire edges, are chained into loops, inside the selection or across the whole mesh. Each loop is closed the way that suits it:
  - A flat loop becomes one face, wound like the faces around it, so a box with a missing side is a closed solid again.
  - A bent loop becomes a fan from its centre.
  - A loop with no area is welded shut, so no zero-area face is ever made: a crack between unwelded copies of an edge, a slit, or a rim converging on a point (welded into one vertex).
  - A loop lying on a line is left alone and reported.
  - A face's own outline is never filled a second time.
- **Auto Smooth:**
  - Operators that make curved surfaces (a bevel with 2+ segments, a pulled circle) turn on smooth shading by angle (30 degrees by default) on flat-shaded meshes. Round parts look round and corners stay crisp. Meshes you shaded per face are left alone.
  - Toggle and angle are in Edit Mode's bevel settings and the Mesh menu, and saved with the preferences.
  - **Shade Auto Smooth** applies it to whole objects (Mesh menu, Modeling Tools).
- **Render Camera Sequence** (Render window):
  - Every camera with **Render in Sequence** on (new in the Camera Inspector) is rendered in **Sequence Order**, using the current settings and engine.
  - Each image is saved to `Renders/<scene>_sequence_<time>/NN_<camera>.<format>`.
  - It shows progress and can be stopped. Renders go back to the Main Camera afterwards.
- Console: `autosmooth`, `rendersequence [folder|stop]`, `editop smart_fill`, `meshop smart_fill`, `meshop shade_auto_smooth`.

**Checked:** 1447 unit checks on Windows and 1423 on Linux, 0 failed. New tests:
- Archways (three arc sizes) pushed through two ways, plus the notch case.
- Five "prior edges" cases, each pushed through or pulled out, each a closed solid with the exact volume, no overlaps and clean triangulation:
  - A rectangle drawn over an earlier circle's edge.
  - A circle drawn across an earlier rectangle.
  - A rectangle pushed through where the back has a circle.
  - An archway pushed through where the back has a rectangle across it.
  - A rectangle over a circle, pulled out.
- Smart Fill: one and two holes, a selection, a bent hole, a crack, a pinched rim, a line, a wire triangle, a slit, and the editor operator.
- Auto Smooth on 1- and 4-segment bevels, per-face shading, off, and by hand.
- A three-camera sequence in order with one excluded, three different images, and a stopped path-traced sequence.

Stress results:
- The modeling section, with Smart Fill on holes, cracks and wire loops in all 26 awkward meshes: 6,080 operations, 0 problems.
- Push/Pull: 0 problems, overlapping results still 676.
- The editor fuzzer: 6,000 frames, no crash.

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
