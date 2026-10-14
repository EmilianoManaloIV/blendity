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
| Adjusting an area light's size made objects look pixel-like for a moment (Preview Lighting's first pass was coarse and noisy). | Area lights are sampled on a stratified grid in the baker, and the preview gives small objects enough texels. |
| A Baked light still lit moving objects and the unbaked scene live, so it showed twice or where it shouldn't. | Baked lights exist only through the bake (and Preview Lighting before one), as in Unity; Mixed and Realtime stay live. |
| Moving things around recomputed the lighting every frame (a static object's drag cost 84-108 ms a frame with Realtime GI on). | Lighting Updates: Manual (the default) keeps the last lighting until Update Lighting (Ctrl+Shift+L); a drag costs what it costs without GI; Preview Lighting shows a rough bake once you stop. |
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
| A camera's filters showed in the Camera Preview (the selected camera) but not in the Game view, which showed "Main Camera" by name. | The scene has an explicit Active Camera (Camera inspector, Hierarchy menu, the Game view's camera dropdown, `activecamera`, Ctrl+0 in the Blender keymap); the Game view, F12 and the Scene view's filters use it (task 0025, ADR 0016). |
| Edge Outline and the Retro fog did nothing in the Rendered Camera Preview and in a path-traced render; Solid Color clear was ignored there. | The path tracers record the first hit's depth and object id, so every pass runs on traced pictures too; Solid Color fills the traced background. |
| With filters on, the Scene view ignored the camera's exposure and Solid Color clear; an F12 raster render without filters had no depth of field. | The Scene view looks through the active camera for those; F12 raster applies depth of field before the supersampling downsample. |
| A camera's Pixelate, Edge Outline, Film Grain, CRT and Sharpen looked different in the Camera Preview, the Game view and a render (device pixels), and a small Preview inset rendered a retro stack at the inset's rows. | Pixel sizes scale with the picture's height (1080p = 1x), and a small view renders at the stack's resolution and is shrunk into it (task 0026). |
| Renders always had the sky or world colour behind them: no PNG with a transparent background. | Transparent (Film > Transparent) gives renders an alpha channel (task 0035). |
| The Research window (a paper drop folder) served no purpose in the editor. | Retired; the papers behind features are cited in Learn > References, with the feature each became (task 0034). |
| No composition guides on cameras (rule of thirds, safe areas) to frame a shot. | Ten guides behind a Show Guides switch, in the Camera inspector and the Game view's Guides menu (task 0033). |
| A Retro Console camera looked retro only when rasterized: path-traced renders had no snapped vertices, affine warping, texture rules or screen door. | All four show in path-traced renders, on the CPU and the GPU (task 0032). |
| A procedural texture on an object changed only when the mouse was released, and up close it was as blurry as its image. | Objects follow a texture edit while dragging; Live Shading evaluates the layers per pixel up close (task 0031). |
| A large area light (10 x 10 m) baked blotchy lightmaps with Generate Lighting, and in the views left the sides of a cube under it black. | Bakes compute the light and use their shadow rays only for what blocks it; the rasterizer lights from the whole surface of the light (task 0030). |
| After Set Origin in the Inspector the gizmo stayed on the old origin until the mouse moved. | A change asks for another frame (task 0030). |
| No way to select the faces in one plane (a floor broken into pieces, the faces of a wall). | Select Coplanar adds every face in a selected face's plane (task 0030). |
| A material couldn't put grass on top and dirt on the sides: rotating the object rotated its maps. | Axis materials pick Top / Side / Bottom materials by the surface's slope in the world (or the object), projected without UVs (task 0029). |
| There was no place to make textures: only image files and two built-in grids. | Window > Textures makes procedural textures as a layer stack (bricks, wood floors, tatami, tiles...), previewed tiled, dragged onto objects (tasks 0027 and 0028). |
| Bloom on a progressive trace grew as samples accumulated; changing the path tracer's Samples redrew the Game view; the Game view showed the editor's sky colours instead of the world's gradient. | Bloom reads linear light at every resolve; the views hash only the raster-relevant settings; the raster clears with the world's gradient. |
| Array copies of a drawn shape lay on top of the surface (z-fighting). | They are cut into it like the original. |
| An arch drawn on a rectangle stayed two faces, and its array copies too; copies past a wall's edge lay over it. | The arch is one face; a copy past the edge is clipped to the surface and cut in. |
| Objects with a negative scale (mirrored) were drawn inside out in the views. | They cull their own back faces. |
| New Material Asset (and New in the Textures window) seemed to do nothing when a search was left in the box: the new item was filtered out. | New clears the search; a search says how many items it hides. |
| Textures could not be deleted from the Textures window. | Delete (button, right-click, Del) with a confirmation; it goes to the Recycle Bin. |
| Rigged characters imported as statues: bones were plain empties and the mesh didn't follow them. | Skins: bones, an armature and a Skin modifier come in with the file; turn a bone and the mesh bends. |
| Vertex groups could only be set a selection at a time with one weight. | Weight Paint: brush the weights on, seen as colours. |
| A Displace or a bend affected the whole mesh: no way to keep part of it still. | Vertex groups (Assign in Edit Mode) and every deformer's Vertex Group / Invert. |
| A box-shaped lattice can't follow a character's shape. | Mesh Deform: bind the mesh to any closed cage mesh and edit the cage. |
| Squashing or bulging part of a mesh meant moving its vertices by hand. | Lattices: a box of points fitted round the mesh; drag a point and the mesh follows smoothly. |
| Nothing could follow a path: a rail, a pipe or a fence along a bend meant modelling it bent. | Curve objects with clickable, draggable points; the Curve modifier bends a mesh along one; Array's Fit Curve fills one. |
| A colour-only procedural texture (a Color Ramp from the bricks' height) also bumped the surface: turning the layers' height off lost the ramp's input. | The texture's Normal / Roughness / Height Map switches turn those outputs off and keep the height inside. |
| A material's texture field said "(missing)" (and Non-Color) for a procedural texture it was using. | It shows the texture's image and name, and the picker lists the procedural textures. |
| Edge Outline in path-traced renders took its depth from a random sample per pixel, so lines could wobble. | The tracers give the filters one ray through each pixel's centre, as the rasterizer's depth. |
| Switching the Camera Preview to Rendered on the GPU froze the editor for over a minute the first time (Windows closed it as not responding). | The GPU opens in the background: the preview traces on the CPU meanwhile ("GPU starting") and moves to the GPU when it is ready. |
| Procedural, axis and Box-mapped materials looked grey or wrong in other programs after an export; glTF lost normal and roughness maps, FBX every texture. | Bake Textures (Object menu and the export option) turns an object's look into images on new UVs; glTF and FBX carry their image maps. |
| Posing a leg meant turning thigh and shin by hand, and there was no ready-made character skeleton. | IK constraints move a limb by its foot or hand target (with a pole for the knee or elbow); GameObject > Rig makes a humanoid or quadruped with IK, and Auto IK rigs an imported skeleton's limbs by name. |
| On a 4-core laptop the editor stuttered: dragging an object took about 47 ms a frame. | Quality tiers (Low on small machines): views drawn at 67 % and scaled up, cached shadows, a cheaper sky; dragging is about 15 ms a frame at 8 threads. Preferences > Performance also has CPU Threads. |
| The Inspector's MeshFilter section was crowded: modeling buttons, vertex groups and the Weight Paint brush in one list. | A Rigging window (beside the Inspector) holds skeletons, skins, vertex groups, Weight Paint and IK; MeshFilter keeps modeling and one row that opens it. |
| Deleting many materials or textures meant one at a time, a question each. | Ctrl / Shift click and Ctrl+A select several in the Materials, Textures and Project windows; Delete asks once for all of them. |
| Curves only grew at the end, from the console, and lattices only by typing a resolution. | Extrude, Loop Cut (Ctrl+R), Merge at Center, Delete and Alt+C work on curve and lattice points with Edit Mode's keys. |
| Every tool showed all the time: a first-time user faced all of them at once. | Layouts for each stage of the work (Layout dropdown) show that stage's tools; Show All Tools and the keys reach the rest. |
| A character exported mid-pose imported with that pose as its rest: the skin was wrong and Reset Pose did nothing. | Bones rest at their bind pose (as in Blender, Unity, Maya); the saved pose stays on them and Reset Pose gives the bound shape. |
| Fields that name another object (Boolean, Lattice, IK Target...) had to be typed. | Drag the object from the Hierarchy onto the field; the Inspector stays on the object being edited. |
| Clicking the Layout dropdown crashed the editor. | Fixed: a dropdown's list keeps its own copy of its names. |
| Automatic weights bled across gaps (a hand by a thigh took leg weight) and clothing had to be weighted by hand. | Bind: Automatic Weights inpaints from vertices each bone clearly owns and sees; Transfer Weights copies a body's weights onto clothing and inpaints where they don't match. |
| Rigging a character meant placing every bone by hand. | Auto-Rig: click a few markers on the mesh (Mixamo's, or an animal's), Build Rig. |
| Nothing could be animated. | The Animation window (a Unity-style dopesheet with Record and Preview) and the Animation component. |
| Animated motion could only be timed, not shaped: every key eased the same way. | The Animation window's Curves view: drag keys in value, drag tangent handles, Free Smooth / Broken / Flat / Clamped Auto keys (task 0061). |
| Going from one animation clip to another needed something like a state machine to wire up. | The Sequencer window: lay clips on tracks, overlap them to crossfade, and give keys to jump between them in Play (task 0062). |
| Users of Unreal, Blender or Maya had to relearn the mouse: the Scene view only navigated Unity's way. | Edit > Editor Feel: Unreal, Blender or Maya keys and mouse navigation together (task 0063). |
| Object fields took any object and asset references were typed paths: nothing said what a field wanted. | Unity's typed fields: "None (Armature)", wrong kinds refused, a picker per field; clips and sequences dragged from the Project window (task 0064). |
| A rig's IK targets were invisible empties beside bones, bones could only be moved whole, the auto-rig's markers were all green in one view, and keys were picked one axis at a time. | Rig controls coloured by side, bone head/tail handles and Extrude, character roots, coloured markers in five views, grouped x/y/z rows with box select (tasks 0065-0069). |
| Stacks (modifiers, camera filters, texture layers, Sequencer tracks) could only be reordered one step at a time from a menu. | Drag an item's name to its new place, as in Unity's Inspector and Blender's stack (task 0071). |
| A texture's Physical Size changed nothing visible: its materials kept their old tiling. | Physical Size re-tiles the scene's materials using the texture, except ones tiled by hand (task 0072). |
| The editor had one colour scheme. | Preferences > Theme: Dark, Light, and Emiliano Light / Dark from the author's site palette (task 0073). |
| An animation or a sequence could only be rendered one image at a time. | Render Animation (Ctrl+F12): numbered PNGs and, with FFmpeg, an MP4 (task 0074). |
| On a touch laptop the Scene view could only be navigated with a mouse, and a small machine was set to Low silently. | Two-finger zoom, pan and turn; pen pressure for weight paint; a first-run welcome offering High (task 0075). |
| Getting Blendity meant building it or unpacking CI artifacts. | Installers for Windows, Linux (AppImage, tar.gz) and macOS (dmg), on GitHub Releases (task 0076). |
| An MP4 needed FFmpeg installed separately, and the installers lacked Blender's libraries (Embree, denoising, colour management). | FFmpeg linked in; the installers carry every library; one script fetches them for a source build (tasks 0077-0078). |
| Updating meant finding a new installer by hand; a missing library stopped Blendity starting. | Updates offered with their notes, any older build from Version History; Blendity runs without the libraries and offers them, by your choice (tasks 0079-0080). |
| Installing meant finding one file among many on GitHub's release page, with no instructions. | One download link per system in the README, a step-by-step guide there and in each release, and releases holding only the installers (task 0082). |
| Redoing an edit to a material saved as a .mat brought back the undone values. | Undo and redo keep each state's own material values (task 0081). |
| How to do things was spread over a long README, a lighting guide and the Learn tab. | One manual by stage of the work, with examples (docs/MANUAL.md, Help > Open Manual); the README is a short front page. |

## Still worth doing

1. **A Blender navigation preference**: MMB orbit, Shift+MMB pan and numpad views, for users who want Blender navigation too. It shares no keys with modeling, so it is independent of everything above.
2. **A context menu on right-click without dragging** (Unity's Scene view menu, Blender's W menu): the current mode's operators under the cursor. Right-click is already split between fly (drag) and cancel (during operators), so a click without movement is free.
3. **Pie menus or a search** (Blender's F3; the keymap's action list is a start): with about 50 operators now, a type-to-find box is the fastest way to reach a rare one.
4. **Snapping during G** to vertices, edges and faces (Blender's snap targets): the move gizmo now has vertex (V) and surface (Ctrl+Shift) snapping; G itself still snaps only to the grid.
5. **Proportional editing during G / R / S** (it already works with the gizmos), with the wheel changing its radius.
6. **Spin** (an interactive Screw on the selection) and **Knife cuts through several faces** in one stroke: the Knife and the Screw modifier are in; these are their next steps.
7. **The Inspector's operator grids are long.** The Modeling Tools window now has collapsible sections; collapsible groups (Select, Transform, Topology), or showing only the operators the current selection can use, would help.
