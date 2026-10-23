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
| A drag in the Scene view also drew the Game view and the Camera Preview again every frame, and on a computer without a GPU the Scene view's shadows cost the same while moving as standing still: a 50k-face Edit Mode drag took 28 ms a frame on the CPU. | The other views keep their picture during a drag and draw once on release (Preferences > Other Views Follow Drags brings the old way back), and the CPU views' shadows are cheaper while you move and exact the frame after (Simpler While Moving): 21.5 ms (tasks 0103, 0104). |
| With Viewport Device on the GPU, a Sky or HDRI world, a toon material or a toon outline still sent the whole view to the CPU (a new scene's Sky did). | They draw on the card, as the CPU draws them: the textured editor scene with a Sky 22.1 -> 9.9 ms a frame on the laptop setting (task 0102). |
| With Viewport Device on the GPU, any object with an image texture, a Checker or Noise, Box or Generated mapping, a normal map or several materials sent the whole view back to the CPU: most real scenes never reached the card. | Those draw on the card now, as the CPU draws them (ADR 0038); the textured editor scene 18.0 -> 10.4 ms a frame on the laptop setting (task 0101). Axis materials, live procedural textures, see-through, cutout and toon materials, and Sky / HDRI worlds still use the CPU, and Preferences says why. |
| Only Blendity's own two renderers could render a scene. | Render Engine > MoonRay renders F12 images, camera sequences and animations in DreamWorks' MoonRay: an optional download offered on the first run (in WSL on Windows), with what it can't take named in the log (tasks 0111-0114). |
| On a big mesh, entering Edit Mode, every operator, every undo and the first selection after an undo stalled the editor for about a third of a second (the overlapping-vertices check). | The check's answer is kept for geometry already checked, runs about 15 times faster, re-checks only what a drag moved, and past 200,000 vertices runs beside the editor (task 0108). |
| A jump animated by hand could float or spin in ways no body can, and nothing showed where a character's weight was. | The Animation window's Center of Mass, Contacts and Flight buttons: the body weighed from its skin, its center of mass drawn through the clip, the feet's contacts found and pinned, and each jump shown corrected (a true parabola, its spin kept) as a ghost, with Apply as one undo step (tasks 0115-0117). |
| The physics tools' buttons were always there, for animators who never use them. | Preferences > Animation Assistants: each tool a switch, and hand animation the same with all of them off (task 0124). |
| A walk or run cycle had to be keyed pose by pose. | Walk Cycle: a preset's numbers, Generate, and the rig walks or runs, feet planted, as ordinary keys to tweak (task 0125). |
| Walking a character along a route, up stairs or round a corner meant placing every step by hand. | Footsteps: Walk Along Path lays footprints along a curve onto whatever is under them; move one and Update, or Re-plan keeping the moved ones (task 0126). |
| One toon look (bands, a shadow colour, a spot, a rim); nowhere to paint where shadows may fall or to get Guilty Gear's hard two-tone. | Toon styles as presets, a shadow bias map, specular and rim maps, a shade map, a ramp, a hair highlight, light wrap and a key light - the same in every renderer (task 0148, ADR 0048). |
| A Wind, a Rotator or an Oscillator was an invisible empty in the Scene view: nothing to click, no sign of where the wind blew. | Icons for the effectors on the icons layer, clicked like a light's; a selected Wind draws its arrow, streamlines and rings (task 0158). |
| A small emissive object baked as a scatter of bright blocks on the surfaces round it, and the Preview Lighting's first pass was coarse. | Each texel samples the emitters directly, weighted against the hemisphere (the path tracer's own light sampling); a firefly clamp; the preview at half density with 32 rays (task 0157). |
| An imported character's IK handles were the size of the room (or invisible) because the rig's scale multiplied them, and a foot stayed flat however its handle was turned. | Handles drawn in metres with a size preference and Fit Control Sizes; Rotate End turns the foot or hand with its handle (task 0156, ADR 0031). |
| A breast or belly bone swung like a tail (pulled by position, no cone, no volume), a mesh without such bones couldn't jiggle, and hair cards had to be rigged bone by bone. | The Soft label (acceleration lag, a cone limit, volume kept), the Jiggle modifier, Rigging > Rig Hair from Mesh (task 0153, ADR 0049). |
| Hair swung as one lump, a curl straightened under its own weight, nothing blew in the wind, and a skirt sank into a thigh thinner than the leg. | Spring Bones' Bend, Root To Tip, Wind (a Wind component) and Separate, colliders fitted to the skin (task 0153, ADR 0049). |
| A dropped model landed in the project as the file made it; turning a character toon meant editing every material afterwards, and a wrong import left files to clean up. | The Import Preview: the model shown first, its shading switchable (the file's, PBR, a toon style) and an outline, Import or Cancel; `import <file> <shading>` on the console (task 0152). |
| Anime backgrounds (hedges, walls, roofs, skies, water) had to be painted by hand or made from noise and bricks that look photographic next to a toon character. | Nine flat-toned generators and their presets, cards cut out by their cover (Alpha From Cover), `animemat` for a texture and a toon material in one step (task 0150). |
| A toon face shaded its nose and brows like a scanned head; nowhere to put an anime face's flat shading, its shadow sweep or its drawn lines, and every frame moved. | Custom normals (smoothed, transferred from a head proxy, or the file's own), a Face Shadow Map read by the key light's angle, Line and Hatch maps, the Outline's painted width and screen size, Hold on 2s / 3s (task 0149, ADR 0048). |
| Changing a toon threshold on twenty materials meant twenty edits (the Materials window selected several but edited one). | An edit reaches every selected material that has the field; differing values show a dash; `matset` from the console (task 0147). |
| The camera's eleven composition-guide rows filled the Inspector. | They sit in a Composition Guides drop-down, closed until opened; the Toon and axis fields of a material in theirs (task 0145). |
| A rigged character was its mesh under a thicket of bones, control shapes and icons, with no way to hide one kind. | The Scene view's Overlays menu hides each layer on its own - the bones while the IK / FK handles stay - and a hidden layer isn't clicked (task 0146). |
| The Anime and Manga looks were three or four effects to keep in step. | Each is one effect with its own fields, drawing what the stack drew (task 0154). |
| An area light lit like a point light at its centre: a wide panel close to a floor gave a lamp's falloff, and a surface a hand from the panel was no brighter than one a metre away. | An area light is an emissive plane: every point of it lights with the inverse square, as a quad of an emissive material does; Range is where it fades out (task 0155, ADR 0050). |
| A Blender FBX character's bone handles were metres long (its `_end` leaf bones under a x100 armature). | Leaf bones are sized in metres whatever the armature's scale, and Blender's `_end` leaves fold into their parents (task 0144). |
| A Sketchfab download (`source/model.fbx` + `textures/Name_Base_color.png`, the FBX naming no textures) came in grey. | The texture finder looks through the folders around the model and pairs materials with sets by name likeness; the Console says what it did (task 0143). |
| An imported FBX came in grey: textures inside the file or at another machine's path were lost, and the materials existed only in the scene. | Materials are made as `.mat` assets and applied, embedded textures extracted, links and texture sets found beside the model (task 0134). |
| MMD characters (.pmx, .pmd) couldn't be opened at all. | They import rigged, toon-shaded, with leg IK, spring chains and their faces (task 0136). |
| A Collada export (.dae) couldn't be opened. | It imports with its materials, skins and animation, the right way up and size (task 0141). |
| Motion capture (.bvh) couldn't be opened. | It imports as a skeleton with its clip playing (task 0140). |
| A Mixamo FBX or a glTF came in without its animation. | Its clips are saved under `Assets/Animations/<model>/` and the first plays on the root (task 0139). |
| A VRM avatar (.vrm) couldn't be opened. | It imports as a character: humanoid names, expressions, spring bones, MToon look (task 0138). |
| An MMD dance (.vmd) couldn't be played on its character. | Dropped onto the character, it becomes a clip asset played by its Animation component (task 0137). |
| Faces from FBX or glTF couldn't make expressions. | Shape keys: imported with their values, BlendShape sliders, animatable (task 0135). |
| A MoonRay render showed nothing for a minute, so it looked as if it wasn't rendering. | The image fills in about once a second from MoonRay's checkpoints, with how many updates and how long so far (round 153). |
| Filling the motion between two key poses meant keying every in-between by hand. | AI Inbetween: the span between the keys around the playhead made by a network trained on motion capture, in a style, shown as a ghost; Apply writes it as keys and nothing outside the span changes (tasks 0128-0131). |
| A character moved in Play slid along with its legs still. | Locomotion: its legs walk and run wherever it is moved, feet put on the ground, leaning into turns (task 0127). |
| Posing a character meant turning bone after bone; dragging a hand moved only the hand. | AutoPosing: drag a hand, foot, head or the hips and the whole body follows, the feet down and balanced; keyed at the playhead on release (task 0122). |
| A hit or a fall had to be animated by hand, and never looked like weight. | The active ragdoll: the clip played by a ragdoll whose motors follow it; hits and going limp are events in the clip, simulated in the background and shown before Apply (task 0121). |
| Hair, ears, tails and skirts had to be keyed by hand to swing. | Spring bones: found automatically by name and shape (or recursively from a selected bone), each chain unticked, relabelled or set to None as you like (remembered), live in Play or baked into the clip (task 0120). |
| Flight fixed only the air: a take-off too sudden for the legs, a landing that stopped too fast or a lean nothing could hold stayed as keyed. | AutoPhysics makes the whole clip possible (forces only from the contacts, within friction), solved in the background and shown as a ghost before Apply (tasks 0118, 0119). |
| In MoonRay, textured materials rendered black and the world map was turned the wrong way. | Measured against the real MoonRay: textures go as tiled, mip-mapped EXRs, the world's directions and the sun's brightness match the path tracer (round 136). |
| With the views on the GPU, the Edit Mode cage (edges and vertex dots) was still drawn on the CPU every frame, and the Scene view was drawn at the laptop setting's 67 % and scaled up. | The card draws the cage too, and the Scene view at full size there: a 50k-face drag 14.3 -> 11.5-12.3 ms, and a sharper Scene view (task 0100). |
| Dragging vertices of a big mesh in Edit Mode still redid the whole mesh every frame: a 50k-face drag took 45 ms a frame on the laptop setting (25 ms with the views on the GPU). | Only the faces around what moved are worked out again, and only they go to the card: 28 ms on the CPU, 14-15 ms on the GPU (task 0099). |
| The views' 3D was drawn on the CPU even with a graphics card in the machine: moving an object or playing on the laptop setting took 17-28 ms a frame. | Preferences > Viewport Device (Auto): the views draw on the GPU (4.1-6.8 ms there; a 50k-face Edit Mode drag 45 -> 25 ms); what it doesn't draw yet falls back to the CPU, and Preferences says why (task 0096). |
| Denoising a path-traced 1080p frame took about half a second on the CPU, even with an NVIDIA or AMD card in the machine. | Render Settings > Denoise Device (Auto): OpenImageDenoise runs on the card (43 ms on an RTX 4070 SUPER), the CPU as the fallback (task 0094). |
| An MP4 needed FFmpeg installed separately, and the installers lacked Blender's libraries (Embree, denoising, colour management). | FFmpeg linked in; the installers carry every library; one script fetches them for a source build (tasks 0077-0078). |
| Updating meant finding a new installer by hand; a missing library stopped Blendity starting. | Updates offered with their notes, any older build from Version History; Blendity runs without the libraries and offers them, by your choice (tasks 0079-0080). |
| Dragging vertices of a big mesh in Edit Mode was slow (98 ms a frame on 50k faces, laptop setting). | The render mesh follows the drag in place and the drag's own work stays near the moved vertices: 43 ms (tasks 0086-0087). |
| On Windows the editor ran at about 33 fps where 60 was asked for, however light the frame. | The frame-rate cap holds: precise waits and a frame pacer (task 0084). |
| Installing meant finding one file among many on GitHub's release page, with no instructions. | One download link per system in the README, a step-by-step guide there and in each release, and releases holding only the installers (task 0082). |
| Redoing an edit to a material saved as a .mat brought back the undone values. | Undo and redo keep each state's own material values (task 0081). |
| How to do things was spread over a long README, a lighting guide and the Learn tab. | One manual by stage of the work, with examples (docs/MANUAL.md, Help > Open Manual); the README is a short front page. |
| Rendering felt slow and "CPU and GPU usage look low", but nothing showed what the card itself did: the frame profile held only how long a frame spent in the GPU path, and the Profiler not even that. | The Profiler's GPU block reads the card's own clock: the views' pass times, the read-back, the wait, the busy share of the frame, and a running render's busy / wait (task 0159). |
| A view on the card waited for the GPU twice a frame - once for its picture, once for the copy out - so the card sat idle between them. | A view is one submission: the copies out are recorded behind the frame's passes (task 0160, ADR 0051). |
| An F12 render or the Camera Preview used the card only inside the editor's slice of each frame (40 ms / 12 ms), waited for every submission, and blocked the UI for the slice: the card was busy a fifth of the render's time and the editor felt slow. | The cards render on their own threads between frames, two submissions in flight; the editor polls for a few milliseconds a frame (task 0161, ADR 0052). |
| Moving one object or editing one material with the Camera Preview on the card re-sent the whole scene to it, buffer by buffer. | The scene's sections are hashed and only the changed one goes up - the instances, the materials or the lights - and a whole upload sends its buffers in one submission (task 0162). |
| A scene with an area light, or an object with a cutout material, drew its views on the CPU even with the card chosen, and nothing said why. | Area lights and cutouts draw on the card as on the CPU; the Profiler's GPU block names each view slot's remaining fallback reason (task 0163). |
| Backing away from a Toon material with painted maps showed moire in the hatching, a sparkling face shadow and band edges, a dotted outline. | Every map reads at the pixel's lod, a band edge is one antialiased pixel at any distance, procedurals fade under a pixel, a new outline keeps a pixel (task 0164, ADR 0048 amended). |
| An Extrude, Inset or Bevel on a 50k-face mesh took a tenth of a second before anything drew. | The operator itself is under 10 ms (was 60): one mesh copy instead of three, Auto Smooth's count taken once and only when it can matter, from a sorted edge table (task 0165, ADR 0053). |
| Select More on a 50k-face mesh stalled for 55-68 ms, and with the views on the card a face selection drew the edited object on the CPU. | A selection operator copies nothing and only the Scene view draws again; the card draws the face highlight itself (task 0166). |
| Undoing an extrude in Edit Mode left the extruded faces' selection on the restored mesh. | Undo gives back the selection as it was before the operator, redo the operator's, as in Blender (task 0167). |
| Hair felt stiff and jello-like: pinned to the pose, then ringing against it. | Strands are mass-weighted XPBD rods: a lag that doesn't ring, a sag by their weight, air drag always on, the same at any frame rate (task 0168, ADR 0049 amended). |
| OptiX existed only in builds made on a PC with the CUDA Toolkit; the installers never had it. | The kernel's PTX is committed; any build with OptiX's headers (CI fetches NVIDIA's) has it, and `gpu list` says what the card and driver offer (task 0169, ADR 0054). |
| There was no OptiX render device (only task 0097's spike). | Render Settings > GPU Backend: OptiX renders the same picture as GPU Compute (one integrator); opt-in, as it measured slower (tasks 0170-0171, ADR 0054 amended). |
| Getting a scene into Blender or Unity meant exporting and importing again after every change. | Window > Live Link sends the scene and every change as it happens to bridges on the same computer (task 0172, ADR 0055; the bridges are tasks 0173-0175). |
| Seeing a Blendity scene in Blender (to render it in Cycles, say) meant exporting it after every change. | The Blender add-on in bridges/blender follows the scene live: objects, meshes as their vertices move, materials (toon ones as a cel setup), lights, cameras, poses (task 0173). |
| The same in Unity meant re-importing an export. | The Unity package in bridges/unity follows the scene live as GameObjects with the same numbers (task 0174; its Unity side not yet run). |

## Still worth doing

1. **A Blender navigation preference**: MMB orbit, Shift+MMB pan and numpad views, for users who want Blender navigation too. It shares no keys with modeling, so it is independent of everything above.
2. **A context menu on right-click without dragging** (Unity's Scene view menu, Blender's W menu): the current mode's operators under the cursor. Right-click is already split between fly (drag) and cancel (during operators), so a click without movement is free.
3. **Pie menus or a search** (Blender's F3; the keymap's action list is a start): with about 50 operators now, a type-to-find box is the fastest way to reach a rare one.
4. **Snapping during G** to vertices, edges and faces (Blender's snap targets): the move gizmo now has vertex (V) and surface (Ctrl+Shift) snapping; G itself still snaps only to the grid.
5. **Proportional editing during G / R / S** (it already works with the gizmos), with the wheel changing its radius.
6. **Spin** (an interactive Screw on the selection) and **Knife cuts through several faces** in one stroke: the Knife and the Screw modifier are in; these are their next steps.
7. **The Inspector's operator grids are long.** The Modeling Tools window now has collapsible sections; collapsible groups (Select, Transform, Topology), or showing only the operators the current selection can use, would help.
