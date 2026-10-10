# Usability review: Blender's editing inside Unity's editor

Blendity has two kinds of users. Unity users expect Unity's navigation, its W / E / R tools and an Inspector. Blender users expect to model with the keyboard: G, R and S, axis keys, typed values and the mouse. This review covers where those two clash, what Blendity does about it now, and what is still left.

## The recommended split

The two programs rarely want the same thing from the same input, so the rule below avoids most clashes.

| Job | Follow | Why |
|---|---|---|
| Moving the view | **Unity**: hold RMB + WASD to fly, Alt+LMB orbit, MMB pan, wheel zoom, F frame | This is muscle memory for most people who open a "Unity-style" editor. Blender's MMB orbit stays available as a Preference later; it needs no other key. |
| Picking a tool | **Unity**: Q / W / E / R / T / Y and gizmos | Clicking and dragging gizmos is discoverable, and it suits the mouse-first Unity user. |
| Changing geometry | **Blender**: a verb key, then the mouse or a typed number, then click to confirm or Esc to cancel, then F9 | Blender's real strength is modeling fast without leaving the keyboard. Every edit operator now follows this one pattern. |
| Settings | **Unity**: the Inspector, with expressions in fields | Easier to find than Blender's N panel and redo panel, and multi-object editing works. |

### One pattern for every edit

Every modal operator now behaves the same way. This is the most important consistency rule: users learn it once and it carries over to every tool.

1. **Start** with a key: G move, R rotate, S scale, E extrude, I inset, Ctrl+B bevel, P Push/Pull. Loop Cut (Ctrl+R) cuts at once and is adjusted in F9.
2. **Shape it**:
   - Move the mouse.
   - **X / Y / Z** lock to an axis. Press the same key again for the object's local axis, and a third time to unlock.
   - **Shift+X / Y / Z** lock to the plane without that axis.
   - **Type a number**: expressions work, Backspace edits.
   - **Ctrl** snaps and **Shift** gives fine control. During a bevel, the wheel changes the segments.
   - During a G, pressing **R / S / G** switches between move, rotate and scale.
3. **Finish**: a click or **Enter** confirms, and **Esc** or a **right-click** cancels and leaves no undo step.
4. **Adjust afterwards** in the F9 panel: changing a value re-runs the operator as the same undo step.

A hint bar at the bottom of the Scene view lists the keys while any of these operations is running.

### Where the keys clash, and how it is resolved

| Key | Unity | Blender | Blendity |
|---|---|---|---|
| G | (unused) | Move | **Move, as in Blender** |
| R | Scale tool | Rotate | Scale tool by default. With **Edit > Blender Transform Keys**, R rotates and the Scale tool moves to **T**. During a G, R always switches to rotate. |
| S | (unused) | Scale | Scale with Blender Transform Keys on. During a G, it always switches to scale. |
| E | Rotate tool | Extrude | Rotate tool; **Ctrl+E** extrudes Blender-style: faces move along the normal; in vertex / edge mode edges grow faces and lone vertices grow edges, then move freely |
| I | (unused) | Inset | **Inset**, drag to adjust (Ctrl+I too) |
| K | (unused) | Knife | **Knife / Line**: click two points on a face's edges to split it (SketchUp's Line tool, with endpoint / midpoint / on-edge snapping) |
| Ctrl+J | (unused) | Join | **Join** the selected meshes into the active one (Object Mode) |
| X | (unused) | Delete menu | An axis lock during a transform. Ctrl+X dissolves whatever the current selection mode selects; Del deletes. |
| Y | Transform tool | Split | Transform tool. Split is in the Mesh menu and the Inspector. |
| P | (unused) | Separate | **Push/Pull** (SketchUp) |
| Ctrl+D | Duplicate | (Shift+D) | Duplicate: objects in Object Mode, the selected faces in Edit Mode (Shift+D as well) |
| Ctrl+Z / Y | Undo / Redo | Undo / Shift+Ctrl+Z | Both work |
| RMB | Fly / context menu | Cancel, or a context menu | **Fly**, except while an operator runs, when it cancels |
| MMB | Pan | Orbit | Pan, with orbit on Alt+LMB |
| Alt+LMB | Orbit | Loop select (click) | **Orbit when dragged, loop select when clicked** in Edit Mode; Ctrl+Alt+click selects a ring |

**Why this is the best compromise:**
- Nothing Unity users already know changes by default.
- Everything Blender users reach for while modeling works: G, the axis keys, typed values, E / I / Ctrl+B with the mouse, F9.
- The one real clash, R, is a single tick box.

## What was fixed in this pass

| Problem found | Fix |
|---|---|
| Selecting two opposite edges selected all four edges of the quad. Mark Seam, Bevel and the edge highlight all saw four. | Edge mode keeps edges themselves, as Blender does (`edge_sel_`). The edge tools read exactly those edges (`meshops::EdgeSelectionScope`). |
| Shade Smooth on some faces smoothed the whole object. | Shading is per face (`Mesh::face_smooth`). Smoothing stops at flat faces, at edges marked sharp (Mark Sharp / Clear Sharp), at the Smooth by Angle limit, and optionally at UV seams ("Seams Are Hard Edges"). |
| In Edit Mode, clicking another mesh did nothing. | It now becomes the mesh being edited (as in Unity, where a click always selects); picking another object in the Hierarchy does the same. |
| The scene gizmo's axes and label ignored clicks (the click started a box select first). | The gizmo keeps its clicks: named views, and Persp / Iso on the label. |
| Right-clicking empty Hierarchy space opened nothing (the menu closed in the same frame). | Popups opened after their owner declared its menus survive their first frame. |
| The Adjust Last Operation panel (e.g. after Push/Pull) never went away. | An x button, Esc and the next selection click put it away; F9 brings it back. |
| The Camera Preview's Shaded / Rendered button did nothing. | The click fell through to the Scene view and deselected the camera, and Rendered showed the rasterizer when the engine was Rasterized. The inset now keeps its clicks, and Rendered always path-traces. |
| Inset and Bevel ran at a fixed amount. | Both follow the mouse, as in Blender. Bevel takes the wheel for segments and has Clamp Overlap. |
| Colours were RGB only, with a bare popup. | Colours work like Unity's: a swatch with an alpha bar, and a Color window with a saturation / value square, hue strip, RGB 0-255 / 0-1 / HSV modes, alpha, hex RRGGBBAA, and preset and recent swatches. Base Color carries the material's alpha. |
| High render resolutions took typing. | Resolution presets from 640 x 360 to 8K, plus print sizes, each with its megapixels. The output size shows in MP and MB. |
| Exported FBX files did not open in Blender ("ASCII FBX files are not supported"). | FBX is written in binary like Blender's own exporter (ASCII stays as an option for Unity). Every export format is now checked by importing it into Blender 5.2 itself. |
| Exports, screenshots and renders always went to fixed project folders. | Each one asks where to save with the system's Save As dialog (after Blender's export options); File > Import Model and Open Scene use the Open dialog. |
| An OBJ object could pick up an extra, unused material slot from the object before it. | The carried-over material is only added when a face uses it before any `usemtl`. |
| Set Origin could only use fixed points (or Edit Mode's selection, hidden in a menu). | Edit Origin with Handles: the gizmo moves the origin alone, and a click snaps it to a vertex, edge midpoint or face centre. Edit Mode has Origin to Selection as a button. |
| Modifiers were scattered among the components, with only an on/off switch. | One Blender-style modifier stack with Edit Mode / Viewport / Render toggles, Apply per modifier, Duplicate, Copy to Selected and reordering. |
| A shape could only start from a primitive: there was no way to extrude a vertex or an edge. | E / Ctrl+E in vertex and edge mode, with wire edges as in Blender. |
| A new origin didn't act as the transform point: the gizmo sat at the bounds centre (the toolbar defaulted to Center) and scaling used the origin anyway. | Pivot (the origin) is the default and remembered; Center uses the bounds for the gizmo, rotation and scale alike, and so do G / R / S. |
| Materials lived only inside objects; reusing one meant picking it from a list per slot. | Material assets (.mat) dragged from the Project window onto objects, faces, Hierarchy rows or slots, as in Unity. |
| Only the last material slot could be removed. | An x on every slot, and Remove Unused Slots. |
| Shapes could only come from primitives or extrusion. | Draw polylines, rectangles, circles, arcs and polygons onto faces (cut into them) or the ground (new faces), with SketchUp-style snapping. |
| Drawing on a rotated object snapped only to the world's axes. | The drawing plane follows the face's edges (or the object's rotation), and from the last point it snaps parallel or perpendicular to the mesh's edges and square to the previous segment, with a dotted guide (SketchUp inference). |
| An arc drawn on a rectangle's side, then Push/Pull, gave a broken face and an open bottom. | The arc becomes a second face sharing that side, and Push/Pull on a free-standing face closes the solid. |
| Materials could only be seen and changed inside an object's Inspector. | The Materials window shows every material and asset as a preview sphere to edit, drag or assign. |
| Placing one object on another meant nudging it by eye. | Ctrl+Shift while moving (or dragging from the Hierarchy) drops it onto the surface; V snaps vertex to vertex, as in Unity. |
| Framing a shot meant moving the camera by numbers and checking the preview. | Pilot Camera: fly the Scene view and the camera follows, Ctrl+wheel sets its FOV, with a frame and thirds guides. |
| The modeling tools were only in the Inspector of a selected object. | The Modeling Tools window holds them all, including creating and drawing with nothing selected. |
| A circle drawn over several faces became a separate face lying on top of them. | It is cut into every face it covers; the pieces inside come out selected for Push/Pull. |
| Piloting a camera narrower than the view showed the wrong frame, and moving the camera from the Inspector was undone by the view. | The frame is exactly the camera's view, and changes made elsewhere move the view with the camera. |
| Assets lived in flat folders and couldn't be moved, renamed or deleted from the editor; renaming a material left its file's old name. | Unity's Project window: folders, drag to move, F2, Delete to the Recycle Bin / Trash, and the name is the file name. |
| Faces took materials by slot number. | The Face Material field picks the material itself from preview spheres. |
| Rectangles only went corner to corner and circles only from the centre. | Plasticity's centre / 3-point rectangles (optionally square) and 2- / 3-point circles. |
| Nothing to line drawings up with except existing edges and the axes. | Guide lines (drawn, or from edges) with snapping to their crossings and along them. |
| Every mouse move drew a frame, with no way to limit the cost. | A frame rate cap and redraw mode in Preferences, with the measured cost and what each setting trades. |
| Push/Pull left faces on top of each other or a hair apart (a block pushed back flush hovered over the surface; halves pulled level left a sliver). | It goes exactly to flush and merges faces back, continues from there when asked for more, and never builds a wall over a neighbour; Select Overlapping finds any left over. |
| No way to remove stray vertices and wire edges. | Delete Loose (Blender's Clean Up). |
| Several faces could only be inset one by one. | Inset Individual off insets the selection as one face. |
| Clearing a number field and pressing Enter kept the old value. | It gives 0 (or the field's minimum), as in Unity. |
| A New Material made in an Inspector slot lived only in the scene. | It is saved as an asset in Assets/Materials. |
| Centring a shape on a face meant eyeballing it. | The face's exact centre snaps; Start at Face Center puts circles and polygons there. |
| Depth of field only showed in path-traced renders, and piloting reset the focus. | The Game view, Camera Preview and piloted view blur by depth (foreground too), and the picked point stays in focus. |
| An archway pushed through left flickering faces in its half circle (the exit's ring folded over itself). | Rings around holes are checked and rebuilt with bridges when needed; every archway comes out exact. |
| Patching an open hole meant drawing a new face corner by corner, and cracks made zero-area faces. | Smart Fill closes holes and welds shut cracks and slits. |
| Bevelled and round results looked faceted until shaded by hand. | Auto Smooth shades them smooth, with corners kept hard. |
| Rendering several views meant moving the Main Camera and saving each render. | Render Camera Sequence renders every In Sequence camera, each saved. |
| Shortcuts were fixed, Unity-style only. | A rebindable keymap with Unity, Blender, Maya, 3ds Max and SketchUp presets. |
| A camera could only render one look; a retro style meant faking it in materials. | Camera filter components (first: Retro Console Filter, PS1). They apply everywhere that camera's image shows, and the Scene view can preview them with one toggle. |
| Piloting a camera showed the editor's render (its lighting, grid and gizmos), not what the camera would show. | Piloting shows the camera's game view in its frame: filters, lens, exposure and depth of field, with no editor overlays. |
| Emissive materials and bright lights looked flat: nothing glowed. | A Bloom filter, from the renderers' linear light (rasterized and path traced). |
| Realtime GI looked blocky in the Scene view (traced at a quarter of the pixels, so zoomed-out objects were a few cells tall). | Contribute GI objects get realtime lightmaps, the same at any zoom; the Learn lesson and the guide explain the rest. |
| A baked scene still lagged with Realtime GI on (it traced every frame), and probe-lit objects cost 15 ms a frame. | Realtime GI is idle while a bake covers the scene (the Lighting window says so); probe sampling costs about 4 ms. |
| Baking and live GI ran only on the CPU. | On a Vulkan GPU they run 4 to 8 times faster (GI Device, and Render > Device for Generate Lighting). |
| The Scene view's Rendered (path-traced) mode made editing lag. | Removed: the Scene view rasterizes; path tracing is in F12, the Render window and the Camera Preview's Rendered toggle. |
| Moving objects didn't match baked lighting, and every object needed lightmap UVs for GI. | Probe volumes (Unity 6 APV style): automatic probes, baked and live, read per pixel by everything without a lightmap. |
| Changing a light meant baking again, and moving objects had flat ambient light. | Realtime GI (voxel-based, from the research folder): live bounce and sky occlusion with nothing to bake; Auto Generate re-bakes once changes settle. |
| There was no baked lighting: rasterized views had flat sky ambient, no colour bleeding and no soft shadowing in corners. | Unity-style baked lighting: Contribute GI, Light Mode, generated lightmap UVs and Generate Lighting in a Lighting window. |
| The editor felt sluggish: moving the mouse over a panel, or a blinking caret, re-rendered every 3D view and the sun's shadows (about 46 ms a frame in a working scene). | Views reuse their picture until something they show changes, and the shadow map until its casters or the light do: an idle frame is about 2 ms, orbiting about 10 ms. |
| The only camera filter was the retro look; colour and lens effects needed an outside tool. | Twelve more filters under Color, Lens and Stylize in Add Filter, stackable in any order. |
| Each camera filter was its own component, mixed in with everything in Add Component. | One Camera Filters stack per camera, like Unity's post-processing: Add Filter by category, a foldout per effect, reorder by menu. |
| `component` in the console took only one word, so "Retro Console Filter" (or any spaced name) couldn't be added, and such components were dropped when a scene was loaded. | Names with spaces work in the console and in scene files. |
| Selection tools were scattered across one long grid of 60 buttons. | Grouped tools, Select first; pairs share a button. |
| Overlapping faces could only be found inside one mesh, with no advice. | The Z-fighting check works across objects and says which faces can go. |
| The UV editor only picked vertices. | Face and island modes, sync, and rotate / scale / move / flip / fit / align. |
| Changing a light's colour temperature left the Camera Preview stale. | Every light setting re-renders it. |
| Off the mesh, shapes could only be drawn on the ground. | Open Space Plane (ground, front, side, view, last face's plane, offset) and Draw in Open Space Only. |
| UV islands could only be moved by dragging vertices with a tool. | The Scene view's gizmo in the UV editor, with axis handles. |
| Merge by Distance only worked on whole objects. | In Edit Mode on the selection, with F9's distance and Unselected. |
| Delete in the Hierarchy deleted faces while in Edit Mode, and a Project file selected earlier could catch the key. | Delete in the Hierarchy removes objects; the Project window only takes Delete under the mouse. |
| Polylines could only be drawn on the face clicked or the open-space plane. | X / Y / Z pick the YZ / XZ / XY plane, also mid-line, for 3D Follow paths. |
| Turning a shape drawn on a face twisted the faces around it. | The ring is zipped again while it turns. |
| The keymap preset dropdown in Preferences closed the dialog. | Dropdowns open over dialogs and close on their own. |
| Materials could be made but never deleted. | Delete Material (asks first; slots emptied; asset file to the Recycle Bin). |
| Extruding or insetting a region that touches itself at a vertex broke the mesh. | One vertex copy per side of the pinch. |
| Ctrl+B sometimes bevelled at once, and the helper stopped at two segments. | Auto Smooth no longer ends the helper; the wheel goes to 64. |
| Make Face and Smart Fill were two tools, and F worked only in some modes and over the Scene view. | One Make Face, in every mode, with its key anywhere in Edit Mode. |
| Axis planes (X / Y / Z) were a separate idea from guides. | X / Y / Z and Ctrl+click on an edge lay guides; polylines follow them in 3D. |
| A shape drawn at an angle got a world-aligned object and gizmo. | The new object sits on the shape; Local in Edit Mode follows the selection. |
| Overlapping vertices and edges were invisible. | Live markers in Edit Mode, with Select and Merge. |
| Follow needed a drawn wire path. | It also takes picked edges (UModeler's way). |
| Follow with picked edges ran beside them (a copy started at the face's centre). | It runs along the edges where they are. |
| An Open Space Plane chooser duplicated what guides do. | Removed: off the mesh is the ground; guides do the rest. |
| Push/Pull took one connected group of faces. | Several groups at once, or every face along its own normal (Each Face). |
| A bevel was always round. | Bevel Profile: concave to convex (Inspector, F9, Alt+wheel in Ctrl+B, the modifier). |
| An arc across the faces round a drawn shape became loose wire edges. | It cuts every face it crosses, and the area inside it is one face. |
| Insetting several faces too far jumped between halves. | It slides up to the furthest inset that doesn't fold and holds there. |
| Tool numbers (thickness, angle, distance, segments...) crowded the Inspector under Draw. | They are in each tool's helper / F9 and kept for next time; the Inspector keeps switches. |
| Array, Grid, Pipe, Taper and Recess ran at once with Inspector values. | Each opens a drag helper (mouse, wheel, X / Y / Z, typing) and then F9. |
| Push/Pull refused faces beside a fin or a second solid on the edge. | Only faces carrying the surface on across the edge count. |
| A long edge with a piece lying on it was marked overlapping too. | Only the shorter edge of a pair is marked. |
| Merge Coplanar always took the whole mesh. | With faces selected it merges only those. |
| Array copies of a drawn shape lay on top of the surface (z-fighting). | They are cut into it like the original. |
| An arch drawn on a rectangle stayed two faces, and its array copies too; copies past a wall's edge lay over it. | The arch is one face; a copy past the edge is clipped to the surface and cut in. |

## Still worth doing

1. **A Blender navigation preference**: MMB orbit, Shift+MMB pan and numpad views, for users who want Blender navigation too. It shares no keys with modeling, so it is independent of everything above.
2. **A context menu on right-click without dragging** (Unity's Scene view menu, Blender's W menu): the current mode's operators under the cursor. Right-click is already split between fly (drag) and cancel (during operators), so a click without movement is free.
3. **Pie menus or a search** (Blender's F3; the keymap's action list is a start): with about 50 operators now, a type-to-find box is the fastest way to reach a rare one.
4. **Snapping during G** to vertices, edges and faces (Blender's snap targets): the move gizmo now has vertex (V) and surface (Ctrl+Shift) snapping; G itself still snaps only to the grid.
5. **Proportional editing during G / R / S** (it already works with the gizmos), with the wheel changing its radius.
6. **Spin** (an interactive Screw on the selection) and **Knife cuts through several faces** in one stroke: the Knife and the Screw modifier are in; these are their next steps.
7. **The Inspector's operator grids are long.** The Modeling Tools window now has collapsible sections; collapsible groups (Select, Transform, Topology), or showing only the operators the current selection can use, would help.
