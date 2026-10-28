# The Blendity manual

Blendity is a 3D editor that works like Unity (windows, navigation, gizmos, the Inspector, Play mode) and models,
unwraps, textures, renders, rigs and animates the way Blender does. This manual walks through the work in the order
you'd do it, one chapter per stage. Each chapter has short **examples** to follow and the **use cases** it's good for.

Inside the editor the **Learn** tab (F1, or the orange Learn button) has the same ground as interactive lessons:
"Try it" buttons that drive the editor while you watch, questions to check yourself, and the Blender source files
and book chapters behind each idea. Each chapter here names its lesson.

**Contents**
1. [Getting around](#1-getting-around)
2. [Reference and planning](#2-reference-and-planning)
3. [Blocking out](#3-blocking-out)
4. [Modeling and topology](#4-modeling-and-topology)
5. [UV unwrapping](#5-uv-unwrapping)
6. [Materials and textures](#6-materials-and-textures)
7. [Lighting and rendering](#7-lighting-and-rendering)
8. [Rigging and skinning](#8-rigging-and-skinning)
9. [Animation](#9-animation)
10. [Files: import and export](#10-files-import-and-export)
11. [Performance and the minimum machine](#11-performance-and-the-minimum-machine)
12. [Shortcuts: Unity and Blender side by side](#12-shortcuts-unity-and-blender-side-by-side)
13. [The console](#13-the-console)

---

## 1. Getting around

**The windows.** Scene (the 3D view you edit in), Game (what the active camera sees), Hierarchy (the objects as a
tree), Inspector (the selected object's components), Project (the files in `Assets`), Console (messages and a
command line), plus the UV Editor, Render, Materials, Textures, Modeling Tools, Lighting, Rigging, Animation,
Profiler and Learn windows under **Window**. Drag a tab onto another tab bar to group it, onto an edge to split.

**Layouts for each stage.** The **Layout** dropdown (top right, as in Unity; Blender calls them Workspaces) arranges
the windows for a stage of the work and shows only that stage's tools, so the editor never feels like every tool at
once:

A layout is a stage of the work: it shows that stage's tools *and tabs* (task 0235). A tab you open from the Window
menu shows for the session whatever the stage; right-click a tab > *Show All Tabs* shows every tab. In one area,
Materials, Textures and Lighting fold into one *Shading* tab with a row of the three under it (Rigging & Animation
and Tools fold the same way; *Fold Groups* in the tab menu turns it off). A strip whose labels don't fit shows the
inactive tabs as icons, then lists the last ones under »; the stage's hidden tabs are listed under +N. The Scene view's bar reads
*Layout > Mode > Tool*, each part a dropdown.

| Layout | Windows | Tools shown |
|---|---|---|
| Default | everything | everything |
| Reference & Planning | Scene, Game, Project | create, reference images, cameras |
| Blocking Out | Scene, Modeling Tools | primitives, drawing, snapping |
| Modeling & Topology | Scene, Modeling Tools | Edit Mode, checks, curves and lattices |
| UV Unwrapping | Scene beside the UV Editor | Edit Mode, UVs |
| Texturing & Materials | Scene, Game, Materials, Textures | materials, textures, baking |
| Lighting & Rendering | Scene, Game, Lighting, Render | lights, cameras |
| Rigging | Scene, Game, Rigging | bones, skins, IK, deformers |
| Skinning & Weight Painting | Scene, Rigging | skins, weights |
| Animation | Scene, Game, Animation along the bottom | animation, rigging |

Nothing is ever gone: hidden tools still answer their keys and console commands, and **Window > Layouts > Show All
Tools** shows everything. **Save Layout...** keeps your own arrangement.

**Navigating** (Unity's mouse): Alt+left drag orbits, middle drag pans, the wheel zooms, hold the right button and use
W A S D (Q / E down / up, Shift faster) to fly, F frames the selection. Click the cones of the gizmo in the corner for
front / top / side views, its centre for perspective / orthographic.

**Fingers and a pen on a 2-in-1.** Tablet Mode (the tab menu, or Preferences > Pen and touch > Tablet Mode: Auto turns
it on when the keyboard folds away) makes the targets half as big again, and then fingers navigate while the pen
works:

| Action | In Tablet Mode | Otherwise |
|---|---|---|
| One finger drags in the Scene view | orbits round the pivot; a flick glides on | box-selects, as the mouse |
| One finger taps / double taps | selects / frames the selection (Edit Mode on vertices or edges: the edge loop) | clicks / double-clicks |
| Two fingers | pinch zooms, drag pans, twist turns | the same |
| One finger drags a list or panel | scrolls it (sideways still moves sliders and numbers) | as the mouse |
| The pen | selects, moves, draws, paints, as the mouse | the same |
| The pen's side button held while dragging | orbits; Shift pans, Ctrl zooms | the same |
| The pen in the **Draw tab** (Window > Draw) or over the Game view | draws in that view's perspective; the arrows, A / D, W / S orbit and dolly the Scene camera and Shift+arrows, Q / E pan while the pen is up; Place in 3D puts each stroke in the scene at the depth under the pen (task 0233) | the same |
| The pen in **Sculpt Mode** (Modeling Tools > Sculpt; `sculpt on`) | drags a brush over the mesh: Draw raises along the normal (Alt carves), Shift smooths keeping the volume, Grab pulls what is under the first dab; the pen's pressure drives the strength or the size (Pressure); only the vertices in the circle move; a stroke is one undo step (task 0236) | the same with the mouse |
| The pen with the **brush set** (the Sculpt section; the Wheel's Brushes and Symmetry rings; `sculpt brush|symmetry|lazy|alpha|stroke`) | ten brushes (Clay Buildup, Inflate, Flatten, Scrape, Pinch, Crease, Snake Hook join Draw, Smooth, Grab); Mirror X / Y / Z and Radial apply each dab at its instances, summed where they overlap; Lazy Mouse steadies the stroke; an image alpha shapes the dab, turned to the stroke; Dots, Drag Dot, Drag Rect, Freehand and Spray lay the dabs down (task 0237) | the same with the mouse |
| The pen with **Ctrl in Sculpt Mode** (the Sculpt section's mask and group rows; the Wheel's Masks ring; `sculpt mask|group|hide|show`) | paints the mask that holds the surface (Ctrl+Alt takes it away; off the model a click inverts, a drag masks a rectangle); Ctrl+Shift on a face shows its polygroup alone, off the model shows all; hidden faces leave the picture, the id plane and the brush (task 0238) | the same with the mouse |
| The pen on a **subdivided object** (Dynamic Subdivision in the Sculpt section; `sculpt subdiv <n>|off`; the Wheel's Subdivide + / -) | works the cage while the eye sees the Subdivision Surface: the pick is moved onto the cage along the ray, and each dab recomputes only the subdivided points it reaches, bit for bit the full subdivision (task 0239) | the same with the mouse |
| **DynaMesh** (the Sculpt section's Resolution, Smooth and button; `sculpt dynamesh [res] [smooth]`; the Wheel's Brushes ring) | rebuilds the mesh as a closed shell of even quads at the resolution, overlaps merged and inner walls gone, the mask kept, one undo step (task 0240) | the same with the mouse |
| **Quad Remesh** (the Sculpt section's Target Faces and button; `sculpt remesh [faces] ...`) | QuadriFlow on a worker: quads that follow the form at about the count, the mask kept, one undo step when it lands; Esc cancels (task 0241) | the same with the mouse |
| The pen with **Dyntopo** on (the Sculpt section's Dyntopo and Detail; `sculpt dyntopo on|off|<detail>`; the Wheel's Size ring) | tessellates under the brush as it moves: edges longer than Detail x the radius split, shorter than two fifths collapsed, then the brush moves; one undo step a stroke holding the mesh (task 0242) | the same with the mouse |
| The pen in the **Layers window** (Window > Layers; the Draw tab's Layers button) | a press on a row picks the layer, a drag on its name reorders, the eye hides (Alt: solo), the lock keeps the pen and the eraser off it, the slider is the layer's opacity (two overlapping strokes stay one tone), the combo its blend mode; held still on a row it opens the Wheel's layers set (task 0234) | the same, with double-click to rename and right-click for the row's menu |
| The pen's side button clicked, or the pen or a finger held still half a second | the Wheel: flick or tap Undo, Delete, Extrude, Face... (Space opens it with a keyboard; right-click any menu row > Add to the Wheel puts a command on it) | the same |
| Drag the Scene Gizmo / its hand / its magnifier | orbits / pans / zooms | the gizmo orbits (the hand and magnifier show with Preferences > Pen and touch > Navigation buttons: Always) |

A finger's press waits a moment before it counts, so a pinch never starts a box select. On Windows a palm resting on
the screen while the pen is near is ignored. On Linux, fingers and pens come through XInput 2 where the system has it,
and touches are ignored while the pen is moving; on a Mac, the trackpad's pinch and twist navigate and a tablet's pen
has its pressure and tilt.

> Learn: *The Editor Layout > Fingers and a pen on a 2-in-1*.

**Selecting and moving.** Click to select, Ctrl+click to add, drag a box. Q W E R Y pick the View, Move, Rotate,
Scale and Transform tools; Blender's G / R / S also work, with X / Y / Z to lock an axis and a typed number. Hold Ctrl
to snap. Ctrl+Shift while moving drops the object onto the surface under the mouse; V snaps a vertex onto another.

**Objects and components.** As in Unity, everything in a scene is a GameObject with a Transform; what it does comes
from its components (MeshFilter, MeshRenderer, Light, Camera, Rigidbody, Animation...). Create objects from the
Hierarchy's right-click menu or the GameObject menu. Drag a Hierarchy row onto another to parent it.

**Object fields.** A field that names another object (a Boolean's or a Lattice's Object, an IK Target, a skin's
Armature) takes a typed name, or an object dragged onto it from the Hierarchy; pressing a row and dragging it onto a
field doesn't select it, so the Inspector stays where it is.

**The Inspector.** Click a number field and type; fields take expressions: `2*pi`, `+=1`, `*=2` (relative to each
object's own value), `L(0,10)` (spread evenly across the selection) and `R(0,1)` (random per object). With several
objects selected, a change applies to all of them, as in Unity.

**Origins.** GameObject > Set Origin (or Inspector > Transform) moves the pivot to the bounds centre, the median, the
centre of mass, the bottom centre, a typed point, the edit selection or the Scene view pivot without moving anything
on screen; **Edit Origin (Handles)** (GameObject menu) moves only the origin with the gizmo and snaps it to vertices, edges and faces.

**Play.** Ctrl+P plays the scene; changes made while playing are thrown away on Stop, as in Unity. Components for
play: Rigidbody (with Jolt Physics when Blender's libraries are present), Rotator, Oscillator, PlayerController,
and Animation (chapter 9).

**Undo** (Ctrl+Z / Ctrl+Y) covers the scene, materials, textures and animation clips.

**Find any command.** Ctrl+K (or F3) opens the command palette: type a few words and every action, window, layout,
creatable object and console command that matches is listed with its shortcut; Enter runs it, even a tool the current
layout hides. Right-click a row (or a menu row) for its small menu: Quick Favorites (Shift+Q, one list per layout), the Wheel
(Space; the set for where the mouse is), **My Tools** (a window of your own panels, each a grid of commands; `panel`), or
**Assign Shortcut...** (the next key you press binds it, anywhere; `shortcut <chord> <command>`). Preferences > UI file saves
the whole arrangement - layout, windows, feel, keymap and your shortcuts, panels, the Wheel's sets, favorites, theme, scale -
as one `.blendity-ui` file and loads it back (`ui save|load`; two built-ins, Blender-like and ZBrush-like). The
status bar says what the mouse and keys do where the mouse is, and offers Undo for the last action. A warning appears as
a toast over the Scene view for three seconds, with a Fix button when a command puts it right (Paint on a mesh with no
surface offers one); a button that cannot work now is dim, and its tooltip says why. The Inspector shows
each component's first seven fields (Show more for the rest, All Fields for everything); each stage layout's first use
shows a guide strip with its next three steps. While dragging a gizmo arrow, type a number to move exactly that far.

**Windows on other screens.** Drag any tab out past the editor's edge and it gets a window of its own where you let
go; right-click a tab for Move to New Window, Move to Display N or Move to Main Window. Each window is a dock of its
own, at its own display's scale; the scene, selection and undo stay one, and the windows come back with the layout.
A Render or Game tab in its own window can be Render Only (the picture alone, fitted, its controls at the top) and Full
Screen on that display - a second monitor showing the render large. A tab dragged onto **another editor window** docks
there (a tab in its bar, a split at its edges), and the Scene view, the Game view and the Inspector can be **duplicated**
(the tab menu's Duplicate into New Window; `dock duplicate Scene`): a second Scene view has a camera of its own, a second
Inspector a **Pin** that keeps one object while the selection moves on - a drawing tablet beside the screen holds both
while the main window keeps its own. **Tablet Mode** (the tab menu) makes a window's
UI half as big again for a pen or a finger; a pen's pressure and tilt shape the Editing tab's brush, its eraser end
erases, and a palm resting on the screen is ignored.

**Example: the render on a second screen**
1. Drag the Render tab out of the editor onto your second display (or right-click it > Move to Display 2).
2. Right-click its tab > Render Only, then at the top of the window, Full Screen. F12 renders into it.

> Learn: *Welcome to Blendity*, *The Editor Layout*, *Navigating the Scene View*, *GameObjects, Components and Objects*,
> *Hierarchy, Parenting and the Scene Graph*, *Move, Rotate, Scale*.

---

**Your editor, your way.** Everything above is movable: the **Wheel** (hold the pen's button or Tab in a view: one radial
menu with the tools, brushes and sizes of whatever mode you are in), **panels** that dock into any window or float on a
second screen, a **shortcut for anything** (Assign Shortcut... on any menu item, `shortcut <chord> <command>`), a **UI file**
(Preferences > UI file) that saves the whole arrangement to share or restore, and the **module registry** (Window > Modules)
that hides what you do not use. The layouts are stages of one pipeline - Reference, Blocking, Modeling, Sculpting, UV,
Texturing, Stylized, Lighting, Rigging, Animation - and each shows the tools of its stage first (Show All Tools shows the
rest). The Learn tab's *Customizing the Editor* lesson walks through it.

## 2. Reference and planning

**Reference images.** GameObject > Reference Image... puts a picture into the scene as an unlit plane facing the
front, sized to the picture, kept out of renders and shadows (the console's `refimage <path> front|side|top` picks
the view). A picture from outside the project is copied into
`Assets/Textures/References`, so the scene keeps it on another machine.

**Cameras and shots.** Add a Camera (GameObject > Camera) and pick it in the Game view's camera menu; selecting a
camera shows a **Camera Preview** in the corner of the Scene view. **Pilot Camera** flies the Scene view as that
camera, and composition guides (thirds, golden ratio, safe areas) draw over its frame. Physical camera settings
(focal length, sensor, f-stop, focus) are in the Camera's Inspector; the focus eyedropper focuses on the surface you
click.

**Example: model from a front and a side drawing**
1. Layout > Reference & Planning.
2. GameObject > Reference Image..., pick the front drawing.
3. For the side drawing type `refimage <path> side` in the Console (or make a second one and turn it -90 degrees
   about Y).
4. Scale and move the planes so the drawings line up (the Inspector's Scale field takes expressions like `*=2`).
5. Switch to Blocking Out and start with primitives over them.

**Use cases:** modelling from blueprints or concept sheets; planning a shot list before building a set.

> Learn: *Cameras and Projection*, *The Editor Layout > Layouts for each stage*.

---

## 3. Blocking out

**Primitives and parametric shapes.** GameObject > 3D Object: cube, sphere, icosphere, cylinder, cone, torus, plane,
quad and the Utah teapot. GameObject > Shapes (Parametric): box, plane, cylinder, cone, sphere, icosphere, torus,
capsule, pipe, arch, stairs, wedge and prism, which keep their settings in the Inspector and rebuild as you change
them, like ProBuilder's shapes.

**Drawing, SketchUp-style.** The **Draw** tools (Polyline, Rectangle, Circle, Arc, Polygon) draw onto faces or the
ground with snapping to corners, midpoints, edges and the drawing plane's axes. A closed shape on a face is cut into
it, ready to **Push/Pull** (P): move the mouse or type a distance, click to confirm. Pushed through, it makes a hole;
pulled onto another face, it joins it.

**Booleans** (with Blender's libraries: the Manifold solver). Select the cutters, then Ctrl+click the object to cut
last; Mesh > Boolean (Union, Difference, Intersect) applies at once, or adds a live modifier whose cutter stays
editable as a wire outline.

**More tools.** **Join** (Ctrl+J) combines meshes, **Separate** splits off the selection or each loose part. From
UModeler and Plasticity: **Follow** (sweep a face along a path), **Spin / Lathe**, **Slice**, **Shell**, **Thicken**,
**Draft**, a Radial Array; **Smart Fill** closes holes and wire loops.

**Snapping.** Hold Ctrl for grid snapping; drawing snaps to the faces' own axes and to parallel and perpendicular
edges; X / Y / Z lay construction lines while drawing.

**Drawing in perspective, building from the construction.** For a drawing pad or a pen: **Draw > Sketch (pen)** draws
over the Scene view with SketchBook's aids under **Draw > Guides** - a ruler, an ellipse (its minor axis shown), French
curves, symmetry, and 1-, 2- and 3-point perspective with draggable vanishing points. The pen's pressure is the width,
its eraser erases, Ctrl keeps a stroke freehand. When the pen lifts the stroke is corrected to what it meant: split at
its corners, each piece a line, an arc, an ellipse or a curve, pulled vertical, horizontal or onto a vanishing line,
its ends joined to other strokes' (an arch is two lines and an arc). The 3-point guide *is* the camera: orbit and its
points move; drag a point and the camera turns to put it there. A **canvas** can be a plane in the scene - the ground,
a face, a wall through a point (`sketchcanvas ground|face|plane`) - and a stroke on it is corrected in the plane's
metres and sits in the scene from every view; a line along the plane's axis becomes a construction line. **Draw >
Build from Sketch** makes solids from the strokes that fix them, 3-Sweep's way: a box from three lines at a corner,
a cylinder or cone from two circles (or a circle and a line), a sweep from two circles and a path, a face from a
closed outline and its Push/Pull from a line along its normal - boxes and cylinders with their numbers in F9. **Draw >
Sketch over the Model** keeps the drawing true from its view (a layer fades as you orbit away; Back to the Layer's
View), projects a stroke onto the face under it and traces it into a cut or a curve, and shows the model's edges to
draw against. `sketchlift` lifts a whole scaffold drawn under the 3-point guide into a wire object to build from.

**Example: a box from a drawing**
1. Draw > Sketch (pen); `sketchcanvas ground`. Draw two lines from one point along the red and the blue axis (they
   snap along the axes and become construction lines).
2. `sketchcanvas plane z 0 0 0` (a wall through the origin) and draw the upright from the same corner.
3. Draw > Build from Sketch > Box: the box stands on the three lines; F9 corrects its lengths.

**Example: a doorway in a wall**
1. GameObject > Shapes (Parametric) > Box; in the Inspector make it 4 x 3 x 0.2.
2. Select the box, Draw > Rectangle (Mesh menu or Inspector), and draw a rectangle about 1 x 2.1 on its front face
   from the bottom edge (hold Ctrl to snap to the grid; the length shows as you draw).
3. Press P over the rectangle and push it through the wall: the doorway is cut.

**Example: a staircase in a minute**
1. GameObject > Shapes (Parametric) > Stairs.
2. In the Inspector set the number of steps, the height and the width; untick Fill Under for an open flight. Every
   change rebuilds it.

**Use cases:** level greyboxing; architectural massing; quick props.

> Learn: *Points, Lines, N-gons and Booleans*, *Move, Rotate, Scale*, *Drawing in Perspective*.

---

**Stylized environments.** For a Ghibli-styled or oil-paint scene: **Modeling Tools > Strokes** paints geometry - a
click on a surface adds a card (a quad facing you), a drag with Ribbon a strip - into one Stroke object whose cards shade by
their own facing, by the surface's normal, or away from a **Canopy** centre so a bush of flat cards reads as one round
volume; `strokeobj canopyfill` fills a shell round the centre, `strokeobj grass` drops blades onto the ground. **Modeling
Tools > Scatter** places instances of a named object by density (`scatter Rock`, then dab; the eraser end removes; a
painted mask keeps them off a path; `scatter merge` joins them for export). The World's **Sun Disc**, **Horizon Band** and
**Fog** finish the look in every renderer alike. The `Stylized` layout gathers these.

## 4. Modeling and topology

**Edit Mode** (Tab): 1 / 2 / 3 pick vertex, edge and face selection. The tools follow the selection mode, as in
ProBuilder and Blender:

| Do | Unity keymap | What it does |
|---|---|---|
| Extrude | Ctrl+E | Pull new geometry out of the selection; a lone vertex grows an edge, an edge a face |
| Inset | I / Ctrl+I | A smaller face inside each face (or the selection as one) |
| Bevel | Ctrl+B (wheel: segments) | Round or chamfer edges, with a profile from concave to convex |
| Loop cut | Ctrl+R over an edge | Cut a ring of new edges round the mesh |
| Bridge | Ctrl+Shift+B | Join two faces or two holes with a tube |
| Merge at center / Fill | Alt+M / Alt+F | Collapse vertices, close holes |
| Select loop / ring | Alt+click / Ctrl+Alt+click | Edge loops and rings |

**Adjust Last Operation** (F9) changes the last tool's settings afterwards, as one undo step. **Proportional
editing** (O) moves nearby vertices with falloff. **N-gon Mode** treats each flat region as one face, as SketchUp
does; the **Knife** (K) splits faces along a line.

**Modifiers** stack non-destructively, top to bottom, as in Blender: Array, Bevel, Boolean, Decimate, Mirror, Screw,
Solidify, Subdivision Surface, Triangulate, Wireframe, Weld; and the deformers Cast, Curve, Displace, Lattice, Mesh
Deform, Simple Deform and Smooth. Each has Edit Mode / Viewport / Render toggles. Drag a modifier's name to move it in
the stack (a line shows where it lands); a camera's filters, a texture's layers and the Sequencer's tracks move the
same way.

**Curves and lattices.** GameObject > Curve makes Bezier, circle and path curves; a Curve modifier bends a mesh along
one, Array > Fit Curve fills it with copies. A Lattice is a box of points that squashes the mesh inside it. Curve and
lattice points take the same keys as vertices: Extrude (Ctrl+E) adds a point beyond an end, Ctrl+R cuts a point (or a
lattice row) into a segment, Delete removes, Alt+C closes a curve.

**Checks.** Select Overlapping and the Z-fighting check find faces on top of each other; Delete Loose removes stray
vertices; Recalculate Normals (Shift+N) faces everything outwards.

**More tools.** Mesh Deform binds a detailed mesh to a low-poly cage round it; Select Similar and Select by Material;
Auto Smooth shades by angle; Mesh > From Papers has the mesh tools built from research papers (Taubin smoothing and
others).

**Example: a rounded table top**
1. Create a Cube, scale it to 1.6 x 0.05 x 0.9.
2. Tab into Edit Mode, press 2 for edges, and Ctrl+Alt+click one of the four vertical edges (they're an edge ring).
3. Ctrl+B and drag; scroll for segments. F9 to set the width exactly.

**Example: a chain along a path**
1. GameObject > Curve > Path; shape it with the Move gizmo on its points (Ctrl+E extends it).
2. Make a torus for a link; add an Array modifier with Fit Type: Fit Curve and the path as its Curve (drag the path
   from the Hierarchy onto the field), and a Curve modifier on the same path.

**Sculpting.** **Modeling Tools > Sculpt** (`sculpt on`) is ZBrush's workflow on the mesh under the pen: **brushes**
(Clay, Draw, Inflate, Smooth, Grab, Snake Hook, Crease, Flatten, Pinch) with Mirror symmetry, a lazy mouse, alpha images
and stroke types; **masks** (Ctrl paints, Ctrl+I inverts, Shift+Ctrl clears) that keep a brush off a region;
**polygroups** (one per island, by the mask, or grown from a face) and **hiding** (H / Alt+H) to work on a part alone.
Four ways to add resolution: **Dynamic Subdivision** (a Catmull-Clark preview the brush edits through: the base mesh stays
light), **DynaMesh** (the Sculpt section's Resolution and button: the shell retopologized evenly from its winding number,
two shapes merged into one skin), **Quad Remesh** (QuadriFlow: a clean quad mesh at a target face count, sharp edges and
boundaries kept, on a worker), and **Dyntopo** (Sculptris Pro: the mesh tessellated under the brush as it moves, finer where
the detail is). The pipeline order that keeps a model clean: *sculpt > remesh > unwrap > bake mesh maps > paint* - a remesh
after painting would throw the paint's UVs away.

**Use cases:** hard-surface props, architecture details, game-ready low poly with Subdivision Surface previews; organic
sculpts for characters and creatures, retopologized for games.

> Learn: *Meshes: Vertices, Edges, Faces*, *Edit Mode: Modeling Like Blender*, *Modifiers vs Components*,
> *Advanced Modeling Tools*, *Bevel, Bridge and Push Through*.

---

## 5. UV unwrapping

Layout > UV Unwrapping puts the **UV Editor** (Ctrl+9) beside the Scene view.

1. In Edit Mode, select edges where the mesh should be cut open and **Mark Seam** (or UV > Seams from Sharp Edges).
2. Select everything and press **U** to Unwrap (LSCM, as Blender's). Smart UV Project, cube, cylinder, sphere and view
   projections are in the UV menu too.
3. In the UV Editor move, rotate and scale islands with the gizmo; **Pack Islands** fits them in the square;
   the stretch overlay shows where a texture would distort.

**Example: a crate with a clean texture layout**
1. Create a Cube; Edit Mode, select all, UV > Cube Projection.
2. Set the UV Editor's background to UV Grid (Blender's test image) to check that the squares stay square.

**Use cases:** painted textures, decals, lightmap UVs (Generate Lightmap UVs does those for you when baking).

> Learn: *UV Mapping*.

---

## 6. Materials and textures

**Materials** are Unity-style, evaluating Blender's **Principled BSDF**: base colour, metallic, roughness, specular,
normal and emission, with tiling and offset, and surface types Opaque, Cutout, Transparent and Glass. The
**Materials** window shows every material as a sphere: drag one onto an object (the face under the mouse picks the
slot) or onto a face in Edit Mode. **Material assets** (`Assets/Materials/<name>.mat`) are shared by every object
that uses them, as in Unity.

**Procedural textures** (`Assets/Textures/<name>.ptex`): a stack of layers, like Blender's modifier stack:
generators (Brick, Wood Floor, Tiles, Voronoi, Noise...) and filters (Blur, Levels, Warp, Edge Wear...), each with a
blend mode. The **Textures** window makes and edits them; one texture gives albedo, normal, roughness and height
maps, and stays sharp at any distance with Live Shading. Its **Physical Size** is the metres one tile covers:
changing it re-tiles every material using the texture (Tiling = 1 / size), except one tiled by hand.

**Axis materials** pick top, side and bottom materials by slope (grass on top, rock on the sides, snow up high)
without UVs. **Toon shading** and the **Outline** component give a cel-shaded look.

**Bake Textures** turns everything an object's materials do into plain images on new UVs, so other programs show what
Blendity renders.

**Bake Mesh Maps** (Object > Bake Mesh Maps; `meshmaps [size] [samples]`) writes the mesh's own geometry as images in its UV
layout - world normal, position, curvature, ambient occlusion, thickness: Substance Painter's mesh maps - under
Assets/Textures/MeshMaps, on a worker while you keep working (Esc or `meshmaps cancel` stops it). The Mesh Renderer
records them; `meshmaps status` says whether the mesh has changed since. The layer stack's generators read them.

**A surface** (`surface new` on the selected object) is a texture in the object's own UV space whose layers write colour,
height, roughness, metallic and emission; each layer has a **mask that is a stack of effects** (the Textures window's Add
Effect: a Mesh Map through levels, Noise, Levels, Blur, an Anchor to another layer's mask), and `surface mask preset <layer>
edgewear|dirt|dust|metaledgewear|positiongradient|thickness` builds the usual ones from the baked mesh maps. A Group holds a
sub-stack. Editing a layer redoes only the layers above it inside the edited rectangle; a changed surface evaluates on a
worker while the views keep the last images.

**Paint Mode** (Modeling Tools > Paint; `paint on`) paints the surface's Paint layer on the model: what is under the brush's
disc and facing you takes the brush's colour, roughness, metallic, height and emission at once (the Paints boxes choose),
the far side and anything behind are left alone, the pen's eraser end erases, Mirror X / Y / Z paint the mirror. `paint
fill group|island|material|object` fills a part. A stroke is one undo step holding only the tiles it touched. The painted
images are saved beside the surface's texture when the project saves.

**Smart materials** are groups of layers with their masks saved as assets: the Textures window's shelf applies a built-in
(Worn Steel, Painted Metal, Rubber, Wood, Plastic, Concrete, Gold) on top of the surface, where its generators read this
object's mesh maps; `smat save <layer> <name>` keeps a group of your own under Assets/Materials/Smart and `smat apply
<name>` drops it onto another surface. `export textures gltf|unity|hdrp|unreal|separate <dir>` writes the surface as the
images a game engine reads, packed as it expects. `paint 2d on` paints on the Textures window's canvas instead of the
model; `paint stencil <image>` holds an image over the view that masks the brush.

**The hand-painted look.** The Paint section's Tip (Round, Flat turned to the drag, Bristle) and Mode (Paint, Smudge, Blend),
Wet (the brush carries the colour it went over), Jitter and `paint grain <image>` make the brush behave like paint; the palette
row keeps swatches and the picked one's shades. `paint bakelight` bakes the mesh maps' occlusion and a key light into a Multiply
layer to paint over (`paint bakegradient` the top-down value gradient), `paint gradient` lays one as paint. Set the material's
Shading Model to Hand-painted so every renderer shows the texture as painted (a rim and a painted highlight in the CPU raster;
glTF writes it unlit). Posterize, Edge Shade and Cavity are filters in the Textures window's Add Layer menu.

**Stylized environments.** Modeling Tools > Strokes paints geometry: a click on a surface adds a card (a quad facing you), a drag
with Ribbon a strip; set Normals to Canopy so a bush of cards shades as one round volume; `strokeobj canopyfill` fills a shell
round the centre, `strokeobj grass` drops blades onto the ground; give the material an alpha image for leaves. Modeling Tools >
Scatter places instances of a named object by density (`scatter Rock`, then dab; the eraser end removes; `scatter mask 1` keeps
them off a painted path; `scatter merge` joins them for export). The Lighting window's World has a Sun Disc, a Horizon Band and
Fog (near and far colours) that every renderer shows alike.

**Many at once.** The Materials, Textures and Project windows select like the Hierarchy: Ctrl+click, Shift+click a
range, Ctrl+A; Delete asks once for all of them (they go to the Recycle Bin / Trash).

**Example: a brick wall material**
1. Window > Textures > New > Bricks. Adjust the brick layer's fields; it already has an Edge Wear layer - raise it
   for worn corners.
2. Select the wall and press Assign to Selection: albedo, normal and roughness are set, with the tiling.

**Example: a terrain that paints itself**
1. Make a material, set Axis Mode: World; Axis Top: a grass material asset, Axis Side: rock, Axis Bottom: dirt.
2. Set Axis High to a snow material with High Start 30 (metres), and Axis Noise so the borders wander.

**Painting the surface** - the Substance Painter pipeline, in order. 1. **Object > Bake Mesh Maps** (`meshmaps`): the
normal, position, curvature, occlusion and thickness of the mesh baked into its UVs on a worker. 2. **A surface** (`surface
new`): a texture flagged as this object's, its layers reading those maps - **Fill** layers with a **mask stack** (a Mesh Map
through levels, Noise, Blur, Levels, an Anchor to another layer's mask) and the **smart masks** (edge wear, dirt, dust,
metal edge wear, position gradient, thickness), **Group** layers, metallic and emission channels; editing a layer redoes
only the layers above it inside the edited rectangle. 3. **Paint Mode** (Modeling Tools > Paint): a material brush paints
colour, roughness, metallic, height and emission by projection, the far side left alone, fills by polygroup / island /
material / object, a stroke one light undo step of the tiles it touched; `paint 2d on` paints on the Textures window's
canvas; `paint stencil` masks the brush with an image. 4. **Smart materials** (the Textures window's shelf: Worn Steel,
Painted Metal, Rubber, Wood, Plastic, Concrete, Gold; `smat save` keeps your own group) dropped onto any surface, their
generators reading *that* object's maps. 5. **Export** (`export textures gltf|unity|hdrp|unreal|separate <dir>`): the
texture set packed as the engine reads it.

**The hand-painted look.** When the light should live in the texture (Blizzard's and Riot's style): `paint bakelight` bakes
the maps' occlusion and a key light into a Multiply layer to paint over (`paint bakegradient` the top-down value gradient,
`paint gradient` lays one as paint); the Paint section's **Tip** (Flat turns with the drag, Bristle streaks), **Wet** mixing,
**Jitter** and `paint grain` make the brush behave like paint; Smudge pulls, Blend softens; the **palette** row keeps
swatches and shades; **Posterize**, **Edge Shade** and **Cavity** filters flatten and accent the stack; the material's
**Shading Model: Hand-painted** shows the texture as painted in every renderer (a rim and a painted highlight in the CPU
raster) and glTF writes it unlit.

**Use cases:** game assets with real PBR maps; architectural materials; stylised (toon, anime, manga) renders; hand-painted
textures for stylized games.

> Learn: *Materials and Textures*, *Assets, Scenes and Files*.

---

## 7. Lighting and rendering

**Lights:** Directional, Point, Spot and Area, with an optional colour temperature in kelvin. **Aim at Point** turns a
light to shine at the surface you click.

**The views.** The Scene and Game views rasterize (deferred PBR with sun shadows and image-based lighting), so editing
stays fast. **F12** renders the active camera with either engine: rasterized with supersampling, or **path traced**
(Cycles-like, with Embree and OpenImageDenoise when Blender's libraries are present). The **Render** window (F11) has
resolution presets and saves PNG, JPEG, HDR or EXR. The path tracer runs on the **GPU** too (Render > Device: GPU
Compute, any Vulkan GPU; ray tracing hardware where there is some; CPU and GPUs together).

**The world:** a gradient, Blender's Hosek-Wilkie physical sky, an HDRI or a colour; view transforms Standard, Filmic,
ACES and (with OpenColorIO) AgX.

**Camera filters**, like Unity's post-processing stack: colour grading, lens effects, bloom, and looks such as the
**Retro Console** (PlayStation, N64, Saturn, DOS) and **Anime / Manga** (halftone, hatching, line art).

**More for renders.** The scene's **Active Camera** (Camera inspector, the Game view's camera menu) is what F12 and
the Game view use; **Render Camera Sequence** renders every camera marked In Sequence, each to its own image;
**Render Animation** (Ctrl+F12, or `renderanim [first last] [fps]`) renders every frame of the scene's sequence (or the
selected clip) as `Renders/<scene>_animation_<time>/0001.png...` and an H.264 MP4 next to them, encoded by the
FFmpeg built in (Blender's; a build without it runs an `ffmpeg` on the PATH or `renderanim ffmpeg <path>`);
**Film > Transparent** leaves the world out for a PNG or EXR with alpha; **Live Preview** re-renders a quick preview
as you edit.

### Baked, mixed and realtime light (the short version for a scene that doesn't lag)
1. **Static objects:** select everything that never moves and tick **MeshRenderer > Contribute GI**. Lightmap UVs are
   made for you.
2. **Lights:** set each light's **Mode**: *Baked* (everything baked; it shows only after baking), *Mixed* (bounces
   baked, direct light and shadows live - the usual choice for the sun) or *Realtime* (nothing baked).
3. **Bake:** Window > Lighting > **Generate Lighting**. A baked lightmap costs about 1 ms on a 1080p frame.
4. **Moving objects** take their bounced light from a **Probe Volume** component (add one and bake again).
5. **Auto Generate** bakes again a second after you stop editing; the status bar says "Lighting out of date" until it
   does. The bake is saved next to the scene.

**Lighting Updates: Manual** (the default) keeps bounced light as last computed while you drag things; press **Update
Lighting** (Ctrl+Shift+L) to refresh it. **Preview Lighting** shows a rough bake half a second after you stop.
**Realtime GI** (voxel global illumination, Thiedemann et al. 2011) shows bounced light while you're still arranging,
with nothing to bake; it only works for what the bake doesn't hold (the Lighting window's "Live GI:" line says
which case applies), and on a Vulkan GPU it runs 4 to 8 times faster. Set **Render > Device** to GPU Compute and
Generate Lighting bakes on the GPU too.

| Setting | What it does | When to change it |
|---|---|---|
| Lightmap Resolution | Texels per metre (Unity's default is 40) | Lower for big scenes, higher for sharp contact shadows |
| Indirect Samples | Rays per texel | Raise if lightmaps look noisy |
| Bounces | How many times light bounces | 2 usually; 3 or 4 for interiors |
| GI Resolution | Half or Quarter of the screen, for objects without a realtime lightmap | Half for a cleaner Scene view |
| Realtime Resolution | Realtime lightmaps' texels per metre (2, as Unity) | Higher for small rooms; 0 traces everything per pixel |
| Indirect Intensity | Scales the baked bounce | Artistic control |
| GI Device | Where realtime GI gathers: Auto, CPU or GPU | CPU only to compare |
| Voxel Resolution | 64, 128 or 256 voxels across the scene | Higher for small detail in big scenes |
| Probe Min / Max Spacing | Probe density near geometry / in open space | Lower min spacing for small rooms |

**Example: a sunlit room that bakes**
1. Tick Contribute GI on the walls, floor and furniture. Set the sun's Mode to Mixed.
2. Window > Lighting > Generate Lighting. Add a Probe Volume (Global) and bake again for the character walking through.

**Example: a PS1-style render**
1. Select the camera, Add Component > Camera Filters, Add Filter > Retro Console, Console: PlayStation (PS1).
2. F12: the wobbling vertices, affine textures and dithered colour show in the path-traced render too.

**Developing a render.** Window > Editing (or the Editing layout) develops a render or an image the way Lightroom does:
a stack of adjustments - Exposure, Contrast, Highlights / Shadows, white balance, a Tone Curve, HSL, Clarity, Dehaze,
Sharpen, Noise Reduction, Vignette, Crop - each limited by a mask if you like (a gradient, a tone or colour range, a
brush), applied in linear light and never written into the source: Save keeps the stack beside the image, Export writes
the result. Use as Camera Look gives the camera the same look live; Apply to Texture puts an edit on a texture.

**Example: develop the last render**
1. F12, then in the Editing tab press Render; Preset > Film.
2. Add Adjustment > Exposure, Add Mask > Radial Gradient over the subject, raise Exposure; Export....

**Use cases:** product shots and turntables (path traced); game levels with baked light; stylised and retro looks.

> Learn: *How This Editor Renders*, *Lights and Materials*, *Render Engines*, *Light, Sky and Tone Mapping*,
> *Baked and Realtime Lighting*, *Rendering on the GPU*, *Editing: Develop a Render*.

---

## 8. Rigging and skinning

**Skeletons.** Bones are GameObjects with a Bone component under a root with an Armature. A mesh follows them through
its **Armature (Skin)** modifier: each vertex moves with the bones its vertex groups are named after. Everything for
characters is in the **Rigging** window (Window > Rigging): Skeleton, Skin, Transfer Weights, Vertex Groups, Weight
Paint, Auto-Rig, IK.

**Automatic weights by inpainting** (Epic's *Robust Skin Weights Transfer via Weight Inpainting*, 2023): **Bind:
Automatic Weights** gives each vertex that's clearly nearest one bone, and that the bone can see, to that bone; the
rest get the smoothest weights that agree with them. Nothing bleeds across a gap (a hand hanging by a thigh takes no
leg weight). **Bind: Envelope** is the quick distance-only binding. Large meshes solve in the background with a
progress bar.

**Weights onto clothing.** Rigging > Transfer Weights: drag the rigged body onto **From** and press Transfer Weights.
Close matches that face the same way copy the body's weights; everything else (a loose sleeve's end, a collar) is
inpainted. Paint an **InpaintMask** vertex group to force places to be inpainted.

**Weight Paint** fixes weights by hand: Add, Subtract, Draw and Blur brushes, falloff, Mirror X, Front Faces Only.
Select a bone while painting to paint its group.

**IK.** Add IK to Bone (on a shin or forearm) makes a target and a pole; move the target and the limb follows.
GameObject > Rig > Humanoid / Quadruped makes a whole skeleton with IK; Auto IK finds the limbs of an imported Mixamo,
Unity or Rigify skeleton by name.

**Auto-Rig with markers** (Mixamo's way, for people and four-legged animals): select the mesh, Rigging > Auto-Rig >
Humanoid or Quadruped. Click the markers the diagram asks for (chin, wrists, elbows, knees, groin; for an animal its
nose, skull, withers, hips, tail and legs) - each lands inside the body, and Symmetry mirrors left and right - then
**Build Rig**: the skeleton, IK on every limb, and the inpainted weights.

**Imported characters.** Rigged FBX and glTF files import with their bones, weights and skin. A file saved mid-pose
keeps its pose, with each bone resting at its bind pose, so Reset Pose gives the shape it was modelled in.

**Example: rig a character in five minutes**
1. Import or model the character standing in a T- or A-pose, facing +Z (Unity's forward).
2. Select it; Rigging > Auto-Rig > Humanoid. Click the chin, the left wrist, the left elbow, the left knee and the
   groin (Symmetry places the right side).
3. Build Rig. Select a hand's IK target (LeftHandIK) and move it: the arm follows, the mesh with it.
4. Fix any weight with Weight Paint (select the bone to paint its group).

**Example: dress the character**
1. Model or import a shirt round the rigged body.
2. Select the shirt; Rigging > Transfer Weights; drag the body onto From; Transfer Weights. The shirt gets the body's
   skeleton and follows its pose.

**Shaping bones and controls:** select a bone and drag its blue tail handle to aim and stretch it (Shift+drag the
orange head); the bones below keep their places. Ctrl+E (E with Blender's keys) extrudes a new bone from the tip: move
the mouse, click to keep it. IK targets are boxes and poles diamonds, coloured by side (left blue, right red, middle
yellow); Rigging > FK Controls puts circles on bones to turn them by. Remove IK and Remove FK Controls take them away.
A character made with GameObject > Armature or the auto-rig lives under one root ("Robot Rig"), as Unity imports one.

**Spring Bones** (Rigging window, with a bone selected; or on an imported character automatically by the bones'
names) make hair, ears, tails and cloth strips swing after the animation: Stiffness pulls a chain back to its pose,
Damping settles it, Gravity hangs it, Radius and Collide keep it outside the body's bones, whose capsules are as wide
as the skin each bone moves. Bend holds each bone's animated angle to the one before it (a curl keeps its shape),
Root To Tip frees the tips while the root follows, Separate keeps a chain apart from the others (a ponytail's
strands), and a Wind component (Add Component > Scripts: strength in m/s along the object's forward or away from it,
turbulence, gusts) blows every chain by its own Wind. Bake writes the swing into a clip, the same twice.

**Use cases:** game characters and creatures; posed renders; clothing and armour for an existing rig.

> Learn: *Modifiers vs Components > Vertex groups, Weight painting, Bones and skins, Automatic weights by inpainting,
> Weights onto clothing, Auto-rig with markers, Inverse kinematics, The Rigging window*.

---

## 9. Animation

Window > **Animation** is Unity's Animation window, a dopesheet (the Animation layout puts it along the bottom).

1. Select the object (a character's root, a door) and press **Create**: it gets an **Animation** component and a clip,
   `Assets/Animations/<name>.anim`.
2. Turn on **Record** (the red dot). Move the playhead along the ruler and pose things with the gizmo or the
   Inspector: every change becomes a key at the playhead.
3. Press play in the window (or turn on **Preview** and scrub) to see it; turning Preview off puts the scene back.
4. In Play (Ctrl+P) the Animation component plays the clip: Loop, Once, Ping Pong or Clamp Forever, at any Speed.

**Keys:** click to select, Shift+click or drag a box for several, drag selected keys sideways to retime them
(clicking or dragging the ruler moves the playhead), Delete, Ctrl+C /
Ctrl+V at the playhead, double-click a row to add one, right-click for the interpolation: **Auto** (smooth without
overshooting, Unity's Clamped Auto), **Linear** or **Constant**. **Add Property** animates any number of a component
too (a light's Intensity, a camera's field of view).

**Curves:** the window's **Curves** button (beside Dopesheet) shows how each value travels between keys: a line per
number, x red, y green, z blue. Click track names to choose the curves (Ctrl+click for several), **F** frames them,
the wheel zooms the values. Drag a key up or down to change its value, sideways to retime it (Shift: one direction
only). A selected key shows its **tangent handles**; dragging one makes the key **Free Smooth** (one slope both
sides). Right-click a key for **Broken** (each side its own slope: a bounce), **Flat** (an ease in and out) or
**Clamped Auto** (worked out from the neighbours). Double-click a curve to add a key without changing its shape.

A clip names objects by their path below the animated root ("Hips/Spine"), so it plays on any character with the
same bone names. IK runs after the clip, so animating the IK targets is enough for arms and legs.

**Example: a door that swings open**
1. Select the door's hinge object; Animation window > Create.
2. Record on, frame 0: key the closed rotation (Add Property > <hinge> : Rotation).
3. Frame 60: rotate it 90 degrees round Y. Right-click the keys > Auto for an eased swing.
4. On the Animation component set Wrap: Once.

**Example: a walk cycle for an auto-rigged character**
1. Select the rig's root, Create a clip. Record on.
2. At frames 0, 15, 30, 45 move the FootIK and HandIK targets into the contact, passing, contact and passing poses;
   at 60 copy frame 0 (select its keys, Ctrl+C, playhead to 60, Ctrl+V) so it loops.

**Example: a ball that bounces**
1. Key the ball's Position y at 2 on frame 0, 0 on frame 15, 2 on frame 30.
2. Curves view, click the Position.y track. Right-click the key on frame 15 > Broken.
3. Drag its left handle steeply down and its right handle steeply up: the ball now hits the floor hard and leaves it
   fast, instead of easing in.
4. Right-click the top keys > Flat for a hang at the top.

**Use cases:** props and doors; character cycles; light and camera animation for renders; shaping ease-in and
ease-out, bounces and overshoots in the Curves view.

**Sequencing clips (instead of a state machine):** Window > **Sequencer** is a Premiere-style timeline for clips.
Press **Create** (the scene gets a `.seq`), and drag `.anim` clips from the Project window onto its tracks: each bar is
a clip **and the object it plays on** (the selected character, or one dragged onto the bar's Object field), so one
timeline animates a whole scene. Drag a bar to move it, its ends to trim it; set Speed, In and Out below the tracks.
Bars overlapping on one track **crossfade**; a bar on a higher track wins for what it animates. **Triggers** (key,
clip, blend seconds) jump to a clip in Play, blending from the pose of the moment; from the console, `seq jump <clip>`.

**Example: walk, then run on a key**
1. Make two clips on the character, Walk and Run (Animation window > Create New Clip...).
2. Sequencer > Create; drag Walk onto Track 1 at frame 0, Run onto Track 1 at frame 100.
3. Drag Run left until it overlaps Walk's last 20 frames: the cross shows the crossfade.
4. Add Trigger: key R, clip Run, blend 0.3. In Play, pressing R goes straight into the run from wherever the walk is.

> Learn: *Modifiers vs Components > Keyframes and the dopesheet*, *Modifiers vs Components > Curves and tangents*,
> *Modifiers vs Components > Sequencing clips*, *Play Mode and the Game Loop*.

---

## 10. Files: import and export

**Import** (File > Import Model, or drop files on the window): OBJ + MTL, FBX, glTF, STL, PLY and USD text; their
materials and textures come along, and rigged files come with their skeletons and skins. The model is shown first in
the Import Preview (Preferences > Import turns it off): its shading switchable before anything is written - As
Imported, PBR, or a toon style over the file's materials - and an Outline switch; Import writes it, Cancel nothing.
The console's `import <file> [asis|pbr|classic|guiltygear|soft|painterly|manga]` imports at once with that shading.

**Export** (File > Export): every mesh format Blender 5.2 exports - OBJ, FBX (binary or ASCII), glTF (.glb or
.gltf), STL, PLY, USD - with Blender's options (Selected Only, Apply Modifiers, Scale, Up Axis, Triangulate...).
**Bake Textures** on export turns procedural materials into images other programs understand. Every format was
checked both ways against Blender 5.2.

**The project.** The Project window is `Assets`: nested folders, drag to move, F2 to rename, Delete to the Recycle Bin
/ Trash. References to a moved or renamed material, texture or clip keep working. Scenes are text files
(`.scene`).

> Learn: *Assets, Scenes and Files*, *Coordinate Systems and Handedness*.

**Updates** (Help > Check for Updates, Help > Version History; Preferences > Updates): once a day, when online,
Blendity looks for a newer release (before 1.0 called pre-releases, as 0.11.0 is; with Preferences > Updates' previews
on, also preview builds of single pull requests) and shows what it brings with **Install** / **Not now** / **Skip this one**. Installs are checked against the release's SHA-256
list. Version History installs any build, older ones too (the one before a certain change). Console: `update check`,
`update history`, `update install <tag>`.

**With or without Blender's libraries** (Preferences > Libraries): an install holds Blendity (its own code only, always
starts) and Blendity-full (with Blender's libraries). Blendity hands over to the full one when its libraries load and
**Use Blender's libraries** is on; otherwise it runs itself - offline, with a library missing or damaged, or by your
choice (`--no-libs` once). When the libraries are missing and Blendity is online it offers to download them for this
build (**Download** / **Not now** / **Never ask**). The Windows installer's "Blendity only" type leaves them out.

---

## 11. Performance and the minimum machine

**Minimum:** a thin laptop such as the HP Envy 13.3" 2-in-1: an Intel i7 with 4 cores / 8 threads (i7-1165G7
class), 8 GB of RAM shared with Iris Xe graphics, a 1920 x 1080 touch screen at 150 %. A desktop with a 4-core /
8-thread CPU and a GTX 1050 Ti is faster than that. On such a machine Blendity starts on the **Low** quality tier and
says so on the first run, with High one click away. There, editing frames run at about 35-40 fps and orbiting at 60.
The GPU is for path tracing, baking and realtime GI; an integrated one gets a quarter of the RAM for it.

**Touch and pen:** on a 2-in-1, see *Fingers and a pen on a 2-in-1* in chapter 1: Tablet Mode follows the fold, fingers
navigate and the pen works. A pen's pressure scales the weight-paint brush.

**Quality tiers** (Edit > Preferences..., Quality):

| Tier | Views drawn at | Shadow map | Frame cap | For |
|---|---|---|---|---|
| Low | 67 % (scaled up) | 1024 | 60 fps | the minimum machine |
| Medium | 85 % | 2048 | 120 fps | a mid-range PC |
| High | 100 % | the scene's | 144 fps | a fast PC |

Renders (F12) always use the scene's own settings. **CPU Threads** limits how many cores Blendity uses; the
**Profiler** window shows where a frame's time goes. With Render > Device: GPU + CPU (Auto), path tracing picks the
faster device by itself, and it never overruns a small GPU's memory or the driver's time limit.

**Tips for a scene that stays smooth:** bake lighting (chapter 7), keep Lighting Updates on Manual, keep Subdivision
Surface's Levels at 1 while modelling, and turn on the Low tier for heavy scenes.

> Learn: *Performance and Stress Testing > Quality tiers: a small machine*, *Rendering on the GPU > A small graphics
> card*, *Libraries and Performance*.

---

## 12. Shortcuts: Unity and Blender side by side

Blendity uses Unity's keys, and keeps Blender's where Unity has none. Edit > Preferences..., Keymap, starts from
Unity, Unreal, Blender, Maya, 3ds Max or SketchUp and rebinds anything.

**Themes** (Preferences > Theme, or `theme Dark|Light|Emiliano Light|Emiliano Dark`): the editor's colours. The two
Emiliano themes take the palette of the author's site (its Figma variables): paper #F1EEE5 and charcoal #292927, an amber
accent #D7882B, mint / red / blue / amber signal colours.

**Editor Feel** (Edit > Editor Feel, or `feel unity|unreal|blender|maya`) switches the keys *and* the Scene view's
mouse navigation to another program's:

| Feel | Orbit | Pan | Zoom / dolly | Look + fly | Its own keys |
|---|---|---|---|---|---|
| Unity | Alt+LMB | MMB, Alt+Ctrl+LMB | Wheel, Alt+RMB | Hold RMB + WASD | the table below |
| Unreal | Alt+LMB | MMB, LMB+RMB | Wheel, Alt+RMB | Hold RMB + WASD | Alt+P play, End drop to floor, Alt+G/H/J/K views |
| Blender | MMB | Shift+MMB | Wheel, Ctrl+MMB | — | G/R/S, A, X, Shift+D |
| Maya | Alt+LMB | Alt+MMB | Wheel, Alt+RMB | — | Q/W/E/R, F8, F9-F11 |

Window names, layouts and the Y-up axis stay Unity's whatever the feel.

| | Blendity (Unity) | Blender |
|---|---|---|
| Orbit / Pan / Zoom | Alt+LMB / MMB / Wheel or Alt+RMB | MMB / Shift+MMB / Wheel |
| Fly | Hold RMB + WASD, Q/E, Shift | Shift+` |
| Frame selection | F | Numpad . |
| Views, Persp / Iso | click the scene gizmo's axes / centre | Numpad 1 / 3 / 7, Numpad 5 |
| Create objects | right-click in the Hierarchy | Shift+A |
| View / Move / Rotate / Scale / Transform | Q / W / E / R / Y | — / G / R / S / Transform tool |
| Duplicate / Delete / Rename | Ctrl+D / Del / F2 | Shift+D / X / F2 |
| Play / Pause / Step | Ctrl+P / Ctrl+Shift+P / Ctrl+Alt+P | — |
| Edit Mode / vertex / edge / face | Tab / 1 / 2 / 3 | Tab / 1 / 2 / 3 |
| Grab / rotate / scale (modal) | G, X / Y / Z, a typed value | G / R / S |
| Extrude / Inset / Bevel | Ctrl+E / I or Ctrl+I / Ctrl+B | E / I / Ctrl+B |
| Select edge loop / ring | Alt+click / Ctrl+Alt+click | Alt+click / Ctrl+Alt+click |
| Loop cut | Ctrl+R | Ctrl+R |
| Push/Pull (face mode) | P | — |
| Bridge / Push Through | Ctrl+Shift+B / Alt+P | Bridge Edge Loops / — |
| Fill / Merge at center / Recalculate normals | Alt+F / Alt+M / Shift+N | F / M / Shift+N |
| Proportional editing | O | O |
| Adjust Last Operation | F9 | F9 |
| UV Editor / Unwrap | Ctrl+9 / U | UV Editing workspace / U |
| Render / Render window | F12 / F11 | F12 / F11 |
| Learn | F1 | — |

---

## 13. The console

Every tool can be driven from the Console's command line (type `help`), which is also how scripts and the tests drive
the editor. A few that are handy by hand:

| Command | Does |
|---|---|
| `set Camera.FStop 1.4` | sets any field of the selected objects' components |
| `layout UV Unwrapping`, `alltools on` | layouts and Show All Tools |
| `skin bind`, `skin transfer Body Shirt` | automatic weights, weight transfer |
| `autorig humanoid`, `autorig build` | the marker auto-rig |
| `anim new`, `anim frame 30`, `anim record on` | the Animation window |
| `matdelete Bricks, Wood Floor` | delete materials by name |
| `meshmaps 1024 16`, `meshmaps status` | bake the mesh maps on a worker; the record and whether it is stale |
| `surface new`, `surface mask preset 1 edgewear`, `surface eval 0 0 128 128` | a surface for the object, a smart mask on layer 1, the rect engine timed |
| `paint on`, `paint color 0.8 0.1 0.1`, `paint fill island` | Paint Mode, the brush's colour, a fill of the UV island under the pointer |
| `smat apply Worn Steel`, `export textures gltf Exports/Crate`, `paint 2d on` | a smart material on the surface, the texture set in the glTF packing, the canvas takes the brush |
| `paint brush bristle`, `paint wet 0.5`, `paint bakelight key 1 1 0.5` | a bristle tip, wet mixing, the light baked into a layer to paint over |
| `strokeobj on`, `strokeobj grass 40 0.25 1`, `scatter Rock`, `set world.Fog 1` | cards and ribbons as geometry, grass on the ground, rocks scattered by density, distance fog |
| `libs` | which of Blender's libraries this build uses |
| `sketch on`, `sketchguide perspective 3`, `sketchcanvas ground` | the Sketch tool, its guides and canvases |
| `sketchbuild box`, `sketchtrace edge`, `sketchlift` | solids, cuts and a scaffold from the construction lines |
| `sculpt on`, `sculpt brush clay`, `sculpt dynamesh 128`, `sculpt remesh 2000`, `sculpt dyntopo on` | Sculpt Mode, a brush, the remeshers, Sculptris Pro |
| `meshmaps 1024 16`, `surface new`, `surface mask preset 1 edgewear`, `paint on`, `smat apply Worn Steel`, `export textures unity Exports/Crate` | the paint pipeline from the bake to the texture set |
| `paint brush bristle`, `paint wet 0.5`, `paint bakelight`, `strokeobj grass 40 0.25 1`, `scatter Rock`, `set world.Fog 1` | the hand-painted look and a stylized scene |
| `toast <text>` | a toast over the Scene view by hand (lessons, tests) |

---

*This manual condenses the README's feature list, the lighting guide and the Learn lessons. Developer documents -
how it's built and why - are in [`ARCHITECTURE.md`](ARCHITECTURE.md), [`decisions/`](decisions/),
[`tasks/`](tasks/), [`PERFORMANCE.md`](PERFORMANCE.md) and the [`CHANGELOG`](../CHANGELOG.md).*
