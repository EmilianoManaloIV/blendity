// SPDX-License-Identifier: GPL-2.0-or-later
// Learn tab content. Every lesson connects three things:
//   the Unity editor concept  <->  the Blender concept / source file  <->  the theory,
// citing the reference books in "Blender Documents":
//   [FoCG]   Marschner & Shirley, Fundamentals of Computer Graphics, 5th ed.
//   [GEA1]   Gregory, Game Engine Architecture, Vol. I  - Foundations and Core Engine Systems
//   [GEA2]   Gregory, Game Engine Architecture, Vol. II - Graphics, Motion, and Sound
// Chapter/section numbers were taken from the books' own tables of contents.
//
// Markup (one item per line):
//   # Heading           - Bullet            | a | b | c   Table row (first row = header)
//   > Book reference    @ Blender source    ! action | Button label
//   ? Question | Answer (revealed on click)  (blank line) paragraph break   other: paragraph text
#include "learn.h"

namespace bl {

static const Lesson kLessons[] = {
    {"Welcome to Blendity", "How this editor and this tab work", R"(
Blendity is Blender's modeling core rebuilt inside a Unity-style editor. You navigate, select and organise the scene the way Unity does. The data underneath (meshes, modifiers, edit-mode operators) follows Blender's design and source code.
Each lesson connects three layers so you can move between the two programs and understand both:
- The Unity concept you can see on screen (a window, tool or shortcut).
- The equivalent Blender concept, plus the folder in Blender's C/C++ source where it lives (the blender folder next to this project).
- The theory behind it, cited from the reference books in your Blender Documents folder.
# The reference books
> [FoCG] Marschner & Shirley - Fundamentals of Computer Graphics, 5th ed. (math, viewing, rasterization, meshes)
> [GEA1] Gregory - Game Engine Architecture Vol. I (engine foundations: math, memory, game loop, tools)
> [GEA2] Gregory - Game Engine Architecture Vol. II (rendering, lighting, animation, physics, gameplay)
# How to use this tab
- Pick a lesson on the left. Your progress is saved between sessions.
- "Try it" buttons drive the editor for you, so you can watch the concept happen.
- Click a question to reveal the answer.
- Hover over almost any button or field in the editor to see its Blender equivalent.
! layout:Learning | Use the Learning layout (Learn tab beside the Scene)
! lesson:1 | Next: the editor layout
)"},
    {"The Editor Layout", "Unity windows and Blender editors", R"(
Unity and Blender both split one window into panels. Unity calls them windows and docks them with tabs. Blender calls them editors inside screen areas, and you split or join areas by dragging their corners. Both programs let you save arrangements: Unity Layouts, Blender Workspaces.
| Unity window | Blender editor | What it does
| Scene view | 3D Viewport | Edit the world with gizmos and navigation
| Game view | Camera view (Numpad 0) / Render | What the active camera sees
| Hierarchy | Outliner | Tree of objects and parenting
| Inspector | Properties editor | Fields of the selected object and its components
| Project | File Browser / Asset Browser | Files on disk (Assets folder)
| Console | Info editor / Python Console | Log messages and commands
| Toolbar | Top bar + Tool settings | Tools, pivot, snapping, play controls
| Layouts menu | Workspaces tabs | Saved window arrangements
# Docking in Blendity
- Drag a tab onto another tab bar to group it, or onto a panel edge to split that panel. Unity docks the same way.
- Drag the thin gaps between panels to resize them. In Blender you drag area borders.
- Middle-click a tab to close it. Right-click a tab to add windows. Use Window > Layouts to restore a preset.
@ blender/source/blender/editors/screen  (areas, splitting, workspaces)
@ blender/source/blender/editors/space_view3d, space_outliner, space_buttons, space_file, space_info
> [GEA2] 16.4 The Game World Editor - why editors are built around a world view, an object tree and a property grid.
? Which Blender editor plays the role of Unity's Inspector? | The Properties editor (space_buttons in the source). It also shows modifiers, materials and physics as tabs.
! layout:2 by 3 | Try the "2 by 3" layout
! layout:Default | Back to Default
)"},
    {"Navigating the Scene View", "Unity controls vs Blender controls (shortcut sheet)", R"(
Both programs describe the viewport camera as a pivot point, an orientation and a distance. Navigation changes these three values. Blender stores them in RegionView3D as ofs, viewquat and dist. Unity's SceneView has pivot, rotation and size. Blendity uses Unity's mouse mapping.
| Action | Unity / Blendity | Blender
| Orbit | Alt + Left drag | Middle drag
| Pan | Middle drag (or Q tool + Left drag) | Shift + Middle drag
| Zoom | Wheel, or Alt + Right drag | Wheel, or Ctrl + Middle drag
| Fly | Hold Right mouse + W A S D, Q/E down/up, Shift = fast | Shift + ` (Walk/Fly navigation)
| Frame selection | F (or double-click in Hierarchy) | Numpad . (View Selected)
| Axis views | Click the Scene Gizmo cones | Numpad 1 / 3 / 7 (Ctrl = opposite side)
| Perspective / Ortho | Click the gizmo centre | Numpad 5
| Frame everything | F with nothing selected | Home (View All)
# What the math is doing
The camera position is pivot - forward x distance. The view matrix is built with a look-at construction (FoCG 8.1 Viewing Transformations). Orbiting changes yaw and pitch and keeps the pivot fixed. Flying keeps the camera position fixed and moves the pivot. Zooming only changes the distance. In orthographic mode the distance sets the size of the view volume instead (FoCG 8.2-8.3).
> [FoCG] 8.1 Viewing Transformations, 8.3 Perspective Projection, 8.5 Field-of-View
> [GEA1] 5.3 Matrices, 5.4 Quaternions - camera orientation is stored as a quaternion here and in Blender
@ blender/source/blender/editors/space_view3d/view3d_navigate_view_rotate.cc  (orbit)
@ blender/source/blender/editors/space_view3d/view3d_navigate_walk.cc  (walk / fly)
? In Unity you hold the right mouse button and press W. What happens, and what is the Blender equivalent? | The camera flies forward like in a first-person game. Blender's equivalent is Walk navigation (Shift + `), which also uses W A S D.
? Why does clicking the Y cone of the Scene Gizmo give an orthographic top view? | Looking straight down an axis is used for precise alignment, and parallel projection keeps sizes comparable. Blender does the same with Auto Perspective when you press Numpad 7.
! tool:View | Select the View (hand) tool
! frame | Frame the selection (F)
)"},
    {"GameObjects, Components and Objects", "Two ways to build a thing in a scene", R"(
In Unity everything in a scene is a GameObject. A GameObject is an empty container with a Transform. Behaviour comes from the components you add: a MeshFilter holds geometry, a MeshRenderer draws it, a Light emits light, a Rigidbody falls.
Blender uses a fixed structure instead. An Object has a transform and exactly one data-block that decides its type (Mesh, Camera, Light, Curve...). Extra behaviour is attached through modifiers, constraints and physics settings.
| Unity | Blender | Notes
| GameObject | Object | Both have a name, transform, parent and visibility
| MeshFilter.mesh | Object data (Mesh data-block) | Blender can share one mesh between objects (Alt+D)
| MeshRenderer + Material | Material slots | Unity: material per renderer. Blender: per face slot
| Light / Camera component | Light / Camera object type | In Blender the data-block decides the type
| Adding components | Modifiers, constraints, physics tabs | Unity is open-ended; Blender is a fixed set of stacks
| Empty GameObject | Empty object | Used for grouping and as a pivot
This is the component-based object model described in game engine literature. Instead of a deep class hierarchy (Monster -> FlyingMonster...), an object is assembled from small parts.
> [GEA2] 16.2 Implementing Dynamic Elements: Game Objects
> [GEA2] 17.2 Runtime Object Model Architectures - object-centric vs property-centric designs
@ blender/source/blender/makesdna/DNA_object_types.h  (struct Object: loc, rot, scale, parent, data)
@ blender/source/blender/blenkernel/intern/object.cc
? A MeshRenderer without a MeshFilter draws nothing. Why does Unity split them? | Geometry (what) and rendering (how) are separate concerns. Different renderers, like a SkinnedMeshRenderer, can reuse the same idea. Blender separates Mesh data from Material slots for a similar reason.
! create:Cube | Create a Cube, then look at its components in the Inspector
! window:Inspector | Show the Inspector
)"},
    {"Hierarchy, Parenting and the Scene Graph", "Local vs world space", R"(
The Hierarchy (Blender: Outliner) shows the scene graph: a tree in which every child is positioned relative to its parent. A child's world matrix is the parent's world matrix multiplied by the child's local matrix, applied recursively up to the root.
- Unity: drag one object onto another in the Hierarchy to parent it. Drag it onto empty space to unparent it.
- Blender: select the child, then the parent, press Ctrl+P > Object. Alt+P clears the parent.
- Both keep the world position when you reparent ("Keep Transform"), so the local values change instead.
# Inside Blendity
Each GameObject caches its world matrix with a dirty flag. When you edit a transform, that object and its descendants are marked dirty, and they are recomputed only when something reads them. The stress test measures this: on deep hierarchies it is much faster than recomputing every chain every frame. Open docs/PERFORMANCE.md for the numbers.
> [FoCG] 12.2 Scene Graphs
> [GEA2] 17.6 Updating Game Objects in Real Time (dependencies between object updates)
@ blender/source/blender/editors/object/object_relations.cc  (parent set / clear operators)
@ blender/source/blender/editors/space_outliner
? You parent a cube at world position (2,0,0) to an empty at (1,0,0). What is the cube's local position? | (1,0,0). The world position stays (2,0,0), so the local offset from the parent is 2 - 1 = 1 on X.
! window:Hierarchy | Show the Hierarchy
)"},
    {"Move, Rotate, Scale", "Transform tools and gizmos", R"(
| Tool | Unity / Blendity | Blender
| View / Hand | Q | (navigate with the middle mouse button)
| Move | W | G (grab), or the Move tool
| Rotate | E | R
| Scale | R | S
| All-in-one | Y (Transform tool) | Transform tool in the toolbar
| Snap while dragging | hold Ctrl (or toggle the magnet) | hold Ctrl (toggle with Shift+Tab)
| Constrain to an axis | drag an arrow or ring | type X, Y or Z after G / R / S
| Handle position | Center / Pivot button | Pivot Point menu (Median, Active, Individual...)
| Handle orientation | Global / Local button | Transform Orientation (Global, Local, Normal...)
Gizmo colours are the same in both programs: X is red, Y is green, Z is blue. Drag a square to move on a plane, and drag the outer white ring to rotate around the view direction.
# The math
A transform is an affine matrix built as Translate x Rotate x Scale (FoCG 7.3). Rotations are stored as quaternions to avoid gimbal lock and interpolate smoothly. The Inspector shows Euler angles because people find them easier to read. Unity applies them in Z, X, Y order. Blender's default mode is XYZ Euler. The same three numbers can therefore describe different rotations in the two programs.
> [FoCG] 7.2 3D Linear Transformations, 7.3 Translation and Affine Transformations, 7.4 Inverses of Transformation Matrices
> [GEA1] 5.3 Matrices, 5.4 Quaternions, 5.5 Comparison of Rotational Representations
@ blender/source/blender/editors/transform  (the modal G/R/S system)
@ blender/source/blender/editors/gizmo_library
# The origin (pivot)
Every object rotates and scales around its origin, the point its Transform position names. Imported meshes often have it in an odd place. Set Origin (GameObject menu, or Inspector > Transform > Origin) moves it without moving anything on screen: the mesh shifts one way, the object the other, and children stay where they are. Bounds Center and Median are geometric middles. Center of Mass (Surface) weights each face by its area. Center of Mass (Volume) uses the enclosed volume, where a solid would balance. Bottom Center puts the pivot on the floor, the way Unity likes characters and props. Origin to Point takes coordinates you type, and Geometry to Origin moves the mesh instead of the pivot.
@ blender/source/blender/editors/object/object_transform.cc  (object_origin_set_exec)
! cmd:select Cube; origin bottom | Put the Cube's origin at the centre of its base
? Why might rotating 90 degrees on X then 90 on Y give a different result from Y then X? | Matrix multiplication is not commutative. Order matters, which is why Unity documents its Z-X-Y order and Blender lets you choose a rotation mode.
! tool:Move | Move tool (W)
! tool:Rotate | Rotate tool (E)
! tool:Transform | Transform tool (Y)
)"},
    {"Coordinate Systems and Handedness", "Y-up left-handed vs Z-up right-handed", R"(
| | Unity / Blendity | Blender
| Up axis | +Y | +Z
| Forward | +Z | -Y ("front" view looks along +Y)
| Handedness | Left-handed | Right-handed
| Euler order | Z, X, Y | XYZ by default (selectable)
| Units | 1 unit = 1 metre | 1 unit = 1 metre (Unit Scale 1.0)
When you move a model between the programs, an exporter converts the axes. Blender's FBX/OBJ exporters have Forward/Up settings. Unity's importer mirrors X when it reads right-handed files. Blendity's OBJ importer does the same: it negates X and reverses each face's vertex order.
Why reverse the order? Mirroring one axis turns a clockwise loop of vertices into a counter-clockwise one. The face normal (computed with a cross product) would point inward. Back-face culling would then hide the outside of the model and show the inside.
> [FoCG] 7.5 Coordinate Transformations
> [GEA1] 5.2 Points and Vectors (left- vs right-handed coordinate systems)
@ blender/source/blender/io/wavefront_obj  (axis conversion options)
? A model exported from Blender appears inside-out in another tool. What are the two likely causes? | Flipped normals (fix with Mesh > Flip Normals, or Blender Alt+N > Flip), or a handedness conversion that mirrored an axis without reversing the face winding.
)"},
    {"Meshes: Vertices, Edges, Faces", "How the two programs store geometry", R"(
A polygon mesh is a list of vertex positions plus faces that index into it (FoCG 12.1 "indexed meshes"). Blendity stores meshes exactly like modern Blender does:
- positions[] - one point per vertex
- face_offsets[] - face i uses corners face_offsets[i] up to face_offsets[i+1]
- corner_verts[] - the vertex index of each corner
So faces can have any number of corners (n-gons). Unity stores triangles only: a vertex array plus an index array of 3 indices per triangle. Vertices are duplicated wherever normals or UVs differ, for example at hard edges. When a renderer needs triangles, Blendity triangulates n-gons with ear clipping and caches the result.
| Concept | Unity Mesh | Blender Mesh
| Polygons | triangles only (plus quads for some topology) | n-gons (any corner count)
| Hard / soft edges | split vertices / smoothing angle | Shade Smooth / Flat, sharp edges
| Index limit | 65,535 by default (16-bit), optional 32-bit | 32-bit indices
| Editing | via script or ProBuilder | Edit Mode (Tab)
# Normals
Flat shading uses one normal per face. Smooth shading averages the normals of the faces around each vertex (area-weighted here) and interpolates them across the face (FoCG 9.2).
> [FoCG] 12.1 Triangle Meshes
> [FoCG] 9.2 Operations Before and After Rasterization (interpolating attributes)
@ blender/source/blender/makesdna/DNA_mesh_types.h  (face_offset_indices, corner_verts)
@ blender/source/blender/blenkernel/intern/mesh_normals.cc
? A cube has 8 vertices, 12 edges and 6 faces. Check Euler's formula V - E + F. | 8 - 12 + 6 = 2. That holds for any closed mesh with no holes, and extruding or insetting keeps it true.
! create:Cube | Create a Cube and read its stats in the Inspector
)"},
    {"Edit Mode: Modeling Like Blender", "Vertices, faces, extrude and inset", R"(
Unity has no built-in mesh modeling; the ProBuilder package adds it. Blendity gives you Blender's Edit Mode inside the Unity-style Scene view:
| Action | Blendity | Blender
| Enter / exit Edit Mode | Tab (or the Edit Mode button) | Tab
| Vertex / Face select | 1 / 3 | 1 / 3 (2 = edges)
| Select / box select | Click / drag | Click / drag (B for box)
| Select all / none | Ctrl+A / Ctrl+Shift+A | A / Alt+A
| Move selected | W tool + gizmo | G
| Extrude faces | Ctrl+E | E
| Inset faces | Ctrl+I | I
| Delete faces | Delete | X > Faces
# What extrude does to the topology
Region extrude duplicates the selected faces' vertices and moves the copies along the averaged normal. It then builds one new quad for each boundary edge of the selection (an edge used by exactly one selected face). Edges inside the region don't get walls. Winding is chosen so the new side faces point outward.
> [FoCG] 12.1 Triangle Meshes (mesh connectivity)
@ blender/source/blender/bmesh/operators/bmo_extrude.cc
@ blender/source/blender/editors/mesh/editmesh_extrude.cc
? You extrude one face of a cube. How many faces does the cube have now? | 10. The original face moves up as the cap and 4 side quads are added: 6 + 4 = 10.
! create:Cube | Create a Cube
! edit | Enter Edit Mode on the active object
)"},
    {"Modifiers vs Components", "Non-destructive editing and Catmull-Clark", R"(
Blender's modifier stack changes a mesh on the fly without touching the original: Subdivision Surface, Mirror, Smooth... Each modifier feeds the next. Unity has no modifier stack. Any procedural change is done by a script component.
Blendity combines the two ideas: modifier components. Add SubdivisionSurface or SmoothModifier from Add Component. The renderer evaluates them in order, and the result is cached until the mesh or a setting changes. "Apply Modifiers" bakes the result into the mesh, like Blender's Ctrl+A on a modifier.
# Catmull-Clark in three rules
- Face point = average of the face's corners.
- Edge point = average of the edge's two ends and the two neighbouring face points.
- Each original vertex moves to (F + 2R + (n - 3)P) / n. F averages the neighbouring face points, R averages the midpoints of its edges, P is the old position and n is the valence.
Every face becomes one quad per corner, so the face count grows about 4x per level. The stress test shows how quickly that grows.
> Catmull, E. & Clark, J. (1978) Recursively generated B-spline surfaces on arbitrary topological meshes. Computer-Aided Design 10(6).
@ blender/source/blender/blenkernel/intern/subdiv_mesh.cc  (Blender uses OpenSubdiv underneath)
@ blender/source/blender/modifiers/intern/MOD_smooth.cc
? A cube subdivided 3 times has how many faces? | 6 x 4^3 = 384 quads.
! component:SubdivisionSurface | Add a SubdivisionSurface modifier to the selection
)"},
    {"Cameras and Projection", "Scene camera, Game camera, FOV and clipping", R"(
The Scene view has its own editor camera. The Game view renders through the Camera component, preferring the object named Main Camera. In Blender, the viewport is the editor camera and the scene's active camera is the one you look through with Numpad 0.
| Setting | Unity | Blender
| Perspective size | Field of View (vertical, degrees) | Focal Length (mm) + Sensor size
| Orthographic | Size (half height in units) | Orthographic Scale (full width)
| Clipping | Near / Far | Clip Start / End
Blender's lens setting converts to a field of view with FOV = 2 atan(sensor / (2 x focal)). The default 50 mm lens on a 36 mm sensor gives about 39.6 degrees horizontal.
# Physical cameras
Tick Physical Camera (Unity's name; Blender's Lens Unit = Millimeters) and the view comes from a real lens: a focal length, a sensor size (full frame, APS-C, Super 35, IMAX ...), and which image side the sensor spans (Sensor Fit). Lens Shift slides the image without tilting the camera, which keeps an architect's vertical lines straight. Each camera can also have its own Aspect Ratio. The final render keeps the width and sets the height, and the Game view letterboxes.
# Depth of field and exposure
A real lens focuses at one distance. With Depth of Field on, each camera ray starts at a random point on the aperture and aims at the focus plane, so things nearer or farther blur. The aperture's diameter is focal length / f-stop: f/1.4 blurs a lot, f/16 hardly at all. With Aperture Blades the aperture becomes a polygon, and so do the highlights (bokeh). Physical Exposure brightens or darkens the image the way a camera does: double the ISO or the shutter time for one stop more, and close the aperture one stop (f/2.8 to f/4) for one less.
@ blender/intern/cycles/kernel/camera/camera.h  (thin lens: camera_sample_perspective)
@ blender/intern/cycles/kernel/sample/mapping.h  (regular_polygon_sample: bladed apertures)
> [FoCG] 4.3 Computing Viewing Rays, 13.4 Choosing Random Points (on the aperture), 14.10 Monte Carlo Ray Tracing
? With a 50 mm lens, what is the aperture's diameter at f/2? | 25 mm: the f-number is the focal length divided by the aperture's diameter.
! cmd:select Main Camera; set Camera.PhysicalCamera 1; set Camera.DepthOfField 1; set Camera.FStop 1.4; camerapreview rendered | Make the Main Camera physical with a shallow depth of field and preview it
# Depth precision
Perspective projection stores depth non-linearly (FoCG 8.4). Most precision sits close to the near plane, so a tiny near clip value causes z-fighting in the distance. Keep Near as large as you can.
> [FoCG] 8.3 Perspective Projection, 8.4 Some Properties of the Perspective Transform, 8.5 Field-of-View
> [GEA2] 11.2 Lights, Camera, Action!
? Why does setting Near to 0.0001 make distant surfaces flicker? | Depth precision is spread non-linearly between the near and far planes. A very small near value leaves almost no precision far away, so surfaces at similar depths fight (z-fighting).
! window:Game | Show the Game view
)"},
    {"How This Editor Renders", "A software rasterizer you can read", R"(
Unity and Blender draw with the GPU: Unity's render pipelines, and Blender's EEVEE or Workbench through its draw manager (Cycles is a path tracer). Blendity has no GPU dependency, so it implements the classic pipeline on the CPU, in src/render/raster.cpp:
- Vertex stage: transform positions to clip space and light each vertex.
- Clipping: cut triangles against the near plane so the perspective divide is safe.
- Culling: drop back faces, and drop whole objects outside the view frustum.
- Binning: sort triangles into 64x64-pixel screen tiles.
- Rasterization: one thread per tile tests pixels with edge functions, then the depth buffer.
- Object-ID buffer: every pixel records which object drew it. This gives pixel-exact picking and the orange selection outline.
Open the Profiler window to toggle each optimisation and watch the cost change.
> [FoCG] 9.1 Rasterization, 9.2 Operations Before and After Rasterization, 9.4 Culling Primitives for Efficiency
> [GEA2] 11.3 Foundations of 3D Rendering, 11.4 Programming the 3D Graphics Pipeline, 11.5 Pipeline Management
> [GEA1] 4.3 Explicit Parallelism, 4.6 Thread Synchronization Primitives
@ blender/source/blender/draw/intern/draw_manager.cc
@ blender/source/blender/gpu/intern/gpu_select.cc  (Blender's GPU-based selection)
? Why sort triangles into tiles before rasterizing in parallel? | Each tile has its own pixels and depth values, so threads never write the same memory. No locks are needed, and each tile's data stays in the CPU cache.
! window:Profiler | Open the Profiler
)"},
    {"Lights and Materials", "Diffuse, specular and ambient", R"(
Blendity's MeshRenderer uses the classic shading model: Lambertian diffuse, a Blinn-Phong highlight, and a sky/ground hemisphere ambient term. Unity's Standard and URP Lit shaders, and Blender's Principled BSDF, are physically based extensions of the same ideas. They add energy conservation, a metallic workflow and microfacet roughness.
| Concept | Unity | Blender
| Sun light | Directional Light | Sun light
| Bulb | Point Light (Range) | Point light (Radius, falloff)
| Base colour | Albedo / Base Map | Principled BSDF > Base Color
| Shininess | Smoothness | Roughness (inverted)
| Ambient | Environment lighting / Skybox | World shader / HDRI
# The equations
- Diffuse = albedo x light x max(0, N . L). Brightness depends on the angle to the light, not the viewer.
- Specular (Blinn-Phong) = light x (N . H)^p, where H is the half vector between the light and view directions.
- Ambient blends sky and ground colours by how much the normal faces up.
> [FoCG] ch. 5 Surface Shading
> [GEA2] 12.3 The Rendering Equation, 12.4 The Shading Equation, 12.5 Lighting with Triangle Rasterization
? Why does a directional light's position not matter, only its rotation? | It models a source infinitely far away (the sun). All rays are parallel, so only the direction affects N . L.
! create:Point Light | Add a Point Light
)"},
    {"Play Mode and the Game Loop", "Update, physics and why changes revert", R"(
Press Play (Ctrl+P). Blendity copies the scene and starts the game loop. Every frame it updates the components (Rotator, Oscillator, PlayerController) and then steps physics (Rigidbody). Press Stop to restore the copy, so edits made while playing are lost. That matches Unity's behaviour, and it surprises everyone the first time.
Blender has had no game engine since 2.80; the UPBGE fork continues it. The closest built-in idea is animation playback (Space), and rigid body simulations (Bullet) bake over the timeline.
| | Unity | Blender | Blendity
| Run the game | Play (Ctrl+P) | - (UPBGE fork) | Play (Ctrl+P)
| Per-frame code | MonoBehaviour.Update | Drivers / Python handlers | Component::update
| Physics | PhysX Rigidbody | Rigid Body World (Bullet) | Rigidbody component
| Pause / step | Ctrl+Shift+P / Ctrl+Alt+P | - | same as Unity
# Inside the loop
Each frame measures elapsed time, clamps it so one hitch can't break the simulation, and updates objects. Physics runs as 4 smaller sub-steps (semi-implicit Euler): velocity += gravity x dt, then position += velocity x dt, then collision response with restitution.
> [GEA1] 8.2 The Game Loop, 8.5 Measuring and Dealing with Time
> [GEA2] 14.3 The Collision Detection System, 14.4 Rigid Body Dynamics
> [GEA2] 17.6 Updating Game Objects in Real Time
? You move an object during Play mode and press Stop. Where does it go? | Back to where it was before Play. The editor restores the scene copy taken when Play started.
! component:Rigidbody | Add a Rigidbody to the selection
! play | Press Play
)"},
    {"Assets, Scenes and Files", "The Project window and file formats", R"(
Unity projects are folders: every file in Assets/ is an asset, and scenes are text (YAML) files. Blender keeps everything in a single .blend file: meshes, materials, scenes, even UI layout. Other files are linked in or appended.
| Concept | Unity | Blender | Blendity
| Project root | Assets/ folder | the .blend file (+ relative paths) | Assets/ folder
| Scene file | .unity (YAML) | inside .blend | .scene (plain text)
| Import a model | drag into Assets | File > Import | drag onto the window / Assets > Import OBJ
| Shared data | prefabs / assets | linked data-blocks, Asset Browser | one mesh shared by many objects
Blendity's .scene format writes each shared mesh once and has objects reference it. That is how Blender writes data-blocks. Unknown fields are skipped, so older files still load after new fields are added. Blender handles the same problem with "DNA" versioning.
> [GEA1] 7.1 File System, 7.2 The Resource Manager
> [GEA2] 17.3 World Chunk Data Formats, 17.4 Loading and Streaming Game Worlds
@ blender/source/blender/blenloader/intern/writefile.cc
@ blender/source/blender/io/wavefront_obj
! window:Project | Open the Project window
)"},
    {"Undo, Selection and Memory", "Snapshots and copy-on-write", R"(
Unity records undo per object (Undo.RecordObject). Blender's global undo stores snapshots of the whole file ("memfile undo") and only writes the parts that changed. Edit Mode has its own mesh undo.
Blendity takes a snapshot of the scene graph after each completed action: a gizmo drag, an Inspector edit, an operator. Mesh data is not copied. Snapshots share meshes through reference counting, and a mesh is cloned only when you are about to modify a shared one (copy-on-write). Blender does the same with "implicit sharing", so an undo step costs memory only for what actually changed.
> [GEA1] 6.2 Memory Management, 6.3 Containers
@ blender/source/blender/blenlib/BLI_implicit_sharing.hh
@ blender/source/blender/editors/undo
? Why doesn't moving an object require copying its 1-million-vertex mesh into the undo history? | The snapshot only copies the object (its transform and components). The mesh is shared by pointer and copied only if someone edits it.
)"},
    {"Performance and Stress Testing", "Finding limits and efficiencies", R"(
Every engine has a profiler: Unity's Profiler window, and Blender's statistics overlay and --debug timings. Blendity has a Profiler window and a separate headless program, blendity_stress, that pushes each subsystem until it breaks a time budget. It then writes a report to stress/results/.
- Triangles per frame until the renderer exceeds 16.7 ms (60 FPS)
- GameObject counts, and flat vs deep hierarchies
- Picking: brute-force ray casting vs bounding-box rejection vs the ID buffer
- Subdivision levels until time or memory runs out
- Scene save/load throughput, and undo snapshot cost
- Merge-by-distance: spatial hash vs O(n^2)
- UI cost of a 100,000-row Hierarchy
Each test compares a naive version with the optimised one, so you can see what each technique buys. The findings are in docs/PERFORMANCE.md.
> [GEA1] 2.3 Profiling Tools, 10.8 In-Game Profiling, 4.10 SIMD/Vector Processing
> [FoCG] 12.3 Spatial Data Structures
! window:Profiler | Open the Profiler
! spawn:1000 | Spawn 1,000 spheres and watch the frame time
)"},
    {"Your Research", "Turning papers into features", R"(
Drop papers onto the editor window, or copy them into research/papers. Then ask Claude Code to implement a technique, for example: "Read research/papers/taubin95.pdf and add lambda-mu smoothing as a Blendity feature."
New features follow the pattern in src/research/research.cpp: a function that edits a Mesh plus a short registration (name, citation, description). They then appear in the Research tab, the Mesh > Research Features menu and the Inspector. research/FEATURES.md tracks which paper produced which feature.
An example is included: Taubin's lambda|mu smoothing. Plain Laplacian smoothing shrinks a mesh toward its centre. Taubin alternates a positive and a negative smoothing step, which removes noise while keeping the volume. Try both on a noisy sphere and compare.
> Taubin, G. (1995) A signal processing approach to fair surface design. SIGGRAPH '95.
> [FoCG] 10.3 Convolution Filters (smoothing as low-pass filtering)
! research | Open the Research tab
)"},
    {"UV Mapping", "Seams, unwrapping and the UV Editor", R"(
A texture is a flat image, but a mesh is a surface in 3D. UV mapping gives every face corner a 2D coordinate (u, v) inside the image. Blender stores UVs per face corner, so two faces that share a vertex can still use different parts of the texture. Unity stores UVs per vertex (Mesh.uv), so the importer splits a vertex wherever its UVs differ. Blendity keeps Blender's per-corner layout and splits only in the render cache.
| Task | Blendity | Blender | Unity
| Open the UV editor | Ctrl+9 / Window > UV Editor | UV Editing workspace | ProBuilder UV Editor (package)
| Mark a seam | select vertices, Mark Seam | Ctrl+E > Mark Seam | ProBuilder only
| Unwrap | U in the UV Editor / Unwrap button | U > Unwrap | done in the DCC tool
| Smart UV Project | Smart UV button | U > Smart UV Project | Generate Lightmap UVs (similar idea)
| Pack islands | Pack button | UV > Pack Islands | Lightmap packing
# Seams and islands
A closed surface such as a sphere can't lie flat without cutting it. Seams are the cuts. Faces connected without crossing a seam form one island, and each island is flattened on its own. A cylinder needs one vertical seam plus the two cap rings: then the side unrolls into a rectangle and the caps become discs.
# How Unwrap works (LSCM)
Blender's default Conformal method is Least Squares Conformal Maps. For each triangle it measures how far the mapping is from a pure rotation plus uniform scale (angle-preserving), then finds the UVs that minimise that error over the whole island, with two vertices pinned. It's one sparse linear system per island. Blendity solves it with conjugate gradients on the assembled normal equations, running on all cores for large islands (the stress test measured a 10x gain over the first version).
> Levy, Petitjean, Ray & Maillot (2002) Least Squares Conformal Maps for Automatic Texture Atlas Generation. SIGGRAPH.
> [FoCG] 11.2 Texture Coordinate Functions, 11.1 Looking Up Texture Values
@ blender/source/blender/geometry/intern/uv_parametrizer.cc
@ blender/source/blender/geometry/intern/uv_pack.cc
@ blender/source/blender/editors/uvedit
# Reading the UV Editor
Turn on Stretch to colour faces by area distortion: blue means the face is too small in UV space, red means too large. Seams are drawn red in both the UV Editor and the Scene view.
? Why can't a sphere be unwrapped without seams? | Its Gaussian curvature isn't zero, so any flat map must stretch or tear it. Seams choose where it tears.
! cmd:select Cylinder; texture uvgrid; window UV Editor | Show the Cylinder's UVs with a UV Grid texture
! cmd:select Cylinder; uv smart | Smart UV Project the Cylinder
)"},
    {"Materials and Textures", "Principled BSDF behind a Unity Material", R"(
A Unity Material picks a shader (Standard or URP Lit) and fills its slots: Base Map, Metallic, Smoothness, Normal Map, Emission. A Blender material is a node graph, usually one Principled BSDF node with Image Texture nodes plugged in. Blendity shows a Unity-style Material in the Inspector and evaluates Blender's Principled BSDF underneath.
| Blendity field | Blender Principled BSDF | Unity Standard / Lit
| Base Color + Base Map | Base Color (+ Image Texture) | Albedo / Base Map
| Metallic (+ map) | Metallic | Metallic
| Roughness (+ map) | Roughness | 1 - Smoothness
| Specular | IOR Level (0.5 = 4% reflectance) | Specular (Specular setup)
| Normal Map + Strength | Normal Map node | Normal Map + scale
| Emission + Strength | Emission Color / Strength | Emission (HDR colour)
| Tiling / Offset | Mapping node | Tiling / Offset
# Colour spaces
Colour images (base colour, emission) are stored in sRGB and converted to linear light before lighting. Data images (roughness, metallic, normal maps) must not be converted: Blender calls this Non-Color, Unity unticks sRGB in the texture import settings. Blendity marks those slots Non-Color automatically.
# Mipmaps and filtering
Far-away surfaces cover many texels per pixel. Sampling only one texel then flickers (aliasing). Mipmaps store the image at half, quarter, ... resolution. The renderer picks the level from the UV derivatives, and trilinear filtering blends two levels.
> [FoCG] 11.3 Antialiasing Texture Lookups, 11.4 Applications of Texture Mapping, 11.5 Procedural 3D Textures
> [FoCG] 18.2 Color Spaces
> [GEA2] 11.3 Foundations of 3D Rendering (textures, materials)
@ blender/source/blender/nodes/shader/nodes/node_shader_bsdf_principled.cc
@ blender/intern/cycles/kernel/svm/checker.h, noise.h
# Getting textures in
Drag images onto the window: they're copied to Assets/Textures and, if an object is selected, assigned to its material. Drag an .obj or .fbx and its materials and textures come with it (OBJ uses the .mtl file; FBX is read with ufbx, a library Blender itself bundles).
? Why is a normal map stored as Non-Color? | Its RGB values are vector components, not colours. Converting them from sRGB would bend every normal.
! cmd:select Sphere; texture colorgrid | Put a Color Grid texture on the Sphere
)"},
    {"Render Engines", "Rasterized (EEVEE-like) vs Path Traced (Cycles-like)", R"(
Blender has two main engines: EEVEE rasterizes triangles in real time; Cycles traces light paths and converges to a physically based image. Unity's equivalents are the URP/HDRP rasterizer and HDRP's path tracer. Blendity has both, running on the CPU.
| Scene view mode | Blendity | Blender | Unity
| Wireframe | edges only | Wireframe shading | Wireframe draw mode
| Solid | Gouraud, no materials | Solid shading | Shaded (unlit-ish)
| Shaded | deferred PBR + shadows + environment | Material Preview / EEVEE | Shaded (URP)
| Rendered | progressive path tracing | Rendered (Cycles) | HDRP Path Tracing
# Deferred shading with a visibility buffer
The rasterizer first stores, per pixel, which object and triangle is visible. A second pass shades each pixel exactly once from that buffer, so overdraw costs nothing in shading. Sun shadows come from a shadow map: the scene's depth seen from the light. A point is in shadow if something sits closer to the light. The 3x3 PCF filter softens the edge.
> [GEA2] 11.4 Programming the 3D Graphics Pipeline, 12.5 Lighting with Triangle Rasterization
# Path tracing
For each pixel, random rays bounce through the scene. At every hit the tracer samples a light directly (next-event estimation) and picks one new direction from the material (GGX for reflections, cosine for diffuse), combining both with multiple importance sampling. Each extra sample reduces noise by 1/sqrt(N). A denoiser filters what's left using albedo, normals and depth.
Rays are tested against a two-level BVH: one tree per unique mesh in object space, shared by every copy, plus a small tree over the objects. Moving an object only rebuilds the small tree, which is how Cycles handles instancing.
> [FoCG] 4 Ray Tracing, 12.3 Spatial Data Structures, 13.3 Monte Carlo Integration, 14.10 Monte Carlo Ray Tracing
> [GEA2] 12.3 The Rendering Equation, 12.6 Lighting with Stochastic Ray Tracing
@ blender/source/blender/draw/engines/eevee
@ blender/intern/cycles/kernel/integrator
@ blender/intern/cycles/bvh
# Final renders
F12 renders the Main Camera at the resolution in the Render window (F11), using the engine chosen there. Save Render writes PNG, JPEG or Radiance HDR to Renders/.
? Why does the path-traced view start noisy and get cleaner? | Each pixel is a Monte Carlo estimate. The error falls as 1/sqrt(samples), so 4x the samples halves the noise.
! cmd:shading shaded | Switch the Scene view to Shaded
! cmd:shading rendered | Switch the Scene view to Rendered (path traced)
! cmd:render; window Render | Render the Main Camera (F12)
)"},
    {"Light, Sky and Tone Mapping", "World settings, HDRIs and view transforms", R"(
Everything not covered by a light comes from the environment: Blender's World, Unity's Lighting window (Environment). Blendity's World settings live in the Render window.
| Source | Blendity | Blender | Unity
| Gradient | Sky / Equator / Ground colours | (a colour ramp on the World) | Environment Lighting: Gradient
| Physical sky | Hosek-Wilkie, follows the sun | Sky Texture (Hosek / Wilkie) | Procedural Skybox
| Image | HDRI (.hdr), drop one with nothing selected | Environment Texture | Skybox/Cubemap or Panoramic
| Flat colour | Color | World colour | Environment Lighting: Color
# Image-based lighting
An HDRI stores real light levels, far above 1.0. Diffuse lighting needs the HDRI blurred over a hemisphere. Blendity projects it onto 9 spherical harmonics, which capture that blur in 27 numbers. Shiny reflections use pre-blurred mip levels chosen by roughness.
# Tone mapping
Scene light is unbounded but a screen shows 0 to 1. A view transform compresses highlights instead of clipping them: Standard clips, Filmic (Blender's former default) rolls off softly, ACES is the film-industry curve Unity also offers in post-processing. Exposure scales light before that, in stops (each +1 doubles brightness).
> [FoCG] 20 Tone Reproduction (20.2 Dynamic Range, 20.9 Sigmoids)
> [FoCG] 5.3 Ambient Illumination
> [GEA2] 12.2 Radiometry and the Theory of Light Transport, 12.7 Post-Processing
> Hosek & Wilkie (2012) An Analytic Model for Full Spectral Sky-Dome Radiance. SIGGRAPH.
@ blender/intern/sky/source/sky_hosek.cpp  (bundled with Blendity in extern/sky)
@ blender/source/blender/imbuf/intern/colormanagement.cc
? Why render in linear light and tone map at the end? | Light adds linearly. Doing the lighting maths in sRGB values gives wrong falloff and blending; the curve is only for display.
! cmd:world sky; shading shaded | Use the physical sky
! cmd:world gradient | Back to the gradient
)"},
    {"Advanced Modeling Tools", "Edge loops, loop cuts and generative modifiers", R"(
Edge mode (press 2) selects edges. An edge loop is a chain of edges running straight through 4-way vertices, around a cylinder or across a grid. Loops are how modellers add detail: you cut a new loop exactly where a bend needs geometry.
| Tool | Blendity | Blender | ProBuilder
| Select edge loop | double-click an edge | Alt+click | double-click / Select Loop
| Loop cut | Ctrl+R over an edge | Ctrl+R | Insert Edge Loop
| Fill | Alt+F | F | Fill Hole
| Merge at center | Alt+M | M > At Center | Collapse Vertices
| Recalculate normals | Shift+N | Shift+N | Conform Normals
| Proportional editing | O, scroll while dragging | O | (no equivalent)
# How the loop walker decides
At each vertex it continues along the edge that shares no face with the incoming edge, but only if the vertex has exactly 4 edges. A vertex with 3 or 5 edges (a pole) ends the loop. On a boundary it follows the border instead. A loop cut walks the matching ring of quads and splits each one; faces at the ends of the ring receive the new vertices so the mesh stays watertight.
@ blender/source/blender/bmesh/intern/bmesh_walkers_impl.cc  (BMW_EDGELOOP, BMW_EDGERING)
@ blender/source/blender/editors/mesh/editmesh_loopcut.cc
# Generative modifiers
- MirrorModifier: model half, see both. Vertices on the mirror plane are welded.
- ArrayModifier: repeat the mesh with a relative or constant offset.
- SolidifyModifier: give a surface thickness, with rim faces along open edges.
They stack with SubdivisionSurface: Mirror, then Solidify, then Subdivision is a classic hard-surface setup.
@ blender/source/blender/modifiers/intern/MOD_mirror.cc, MOD_array.cc, MOD_solidify_extrude.cc
> [FoCG] 12.1 Triangle Meshes (mesh connectivity), 12.2 Scene Graphs
? Why does an edge loop stop at the top of a UV sphere? | The pole vertex has many edges (one per segment), so there's no single straight continuation.
! cmd:select Plane; edit edge | Edit the Plane in edge mode
! cmd:select Cube; component MirrorModifier | Add a MirrorModifier to the Cube
)"},
    {"Libraries and Performance", "Blender's external libraries, and making the CPU go fast", R"(
Blender doesn't write everything itself. It builds on about 40 external libraries: ray tracing, denoising, colour science and image formats. Blendity uses the same prebuilt set when it finds a blender/lib folder next to the project. Without it, every feature falls back to Blendity's own dependency-free code. Type libs in the Console to see which backends are active.
| Library | Used for in Blendity | Used in Blender for | Fallback
| Embree | Path tracer ray casts (2-3x faster) | Cycles CPU BVH | Built-in BVH
| OpenImageDenoise | Final-frame denoising | Cycles denoiser | A-Trous filter
| TBB | Job system (parallel_for) | BLI_task, Cycles threads | Built-in thread pool
| Eigen | LSCM unwrap solver | UV unwrap, IK, deform | Conjugate gradient
| OpenColorIO | AgX / Filmic view transforms | Colour management | Built-in curves
| Manifold | Boolean modifier | Boolean (Manifold solver) | (needs the library)
| OpenPGL | Path guiding (opt-in) | Cycles path guiding | Off
| Jolt | Play-mode rigid bodies | (in the library set) | Built-in physics
| OpenEXR, libpng, libjpeg-turbo, zstd | Images, compressed .scene files | Image I/O, .blend compression | Built-in codecs
@ blender/build_files/build_environment/cmake  (how Blender builds each library)
@ blender/intern/cycles/bvh/embree.cpp, intern/cycles/integrator/denoiser_oidn.cpp, intern/cycles/integrator/guiding.h
@ blender/source/blender/geometry/intern/mesh_boolean_manifold.cc
> [GEA2] 14.2 Collision/Physics Middleware, 14.5 Integrating a Physics Engine into Your Game
# Measure first
Every optimisation in Blendity started with the stress suite (blendity_stress) and the Profiler window. Guesses are often wrong. Blendity's rasterizer looked memory-bound, but regrouping its work changed nothing. Timing each stage showed the real cost: per-vertex lighting for triangles that were then thrown away.
> [GEA1] 2.3 Profiling Tools
> [FoCG] 22.3 Optimization Techniques
# Do less work
The biggest wins skip work rather than speed it up. A triangle whose bounding box holds no pixel centre can never cover a pixel, so it is dropped before it is stored. Vertices are lit only when a surviving triangle needs them. Each triangle's interpolation setup happens once, not once per pixel. Together these made a 21-million-triangle frame about 5x faster.
> [FoCG] 9.4 Culling Primitives for Efficiency, 12.3 Spatial Data Structures
# SIMD: one instruction, many values
Modern CPUs have vector registers: SSE holds 4 floats, AVX2 holds 8. One instruction adds, compares or gathers all lanes at once. Blendity checks the CPU at start-up and picks a kernel, the way Cycles keeps SSE4.1 and AVX2 kernel variants. Its triangle setup rejects 8 triangles per step. Its display encoder converts 8 pixels per step, and an exact lookup table replaces pow() for the sRGB curve: 16x faster on one core, same 8-bit result.
> [GEA1] 4.10 SIMD/Vector Processing, 3.5 Memory Architectures, 3.3 Data, Code and Memory Layout
@ blender/intern/cycles/util/simd.h, blender/source/blender/blenlib/intern/math_color.cc
# Many cores
The job system splits loops across every core: tiles in the rasterizer, rows when shading, pixel blocks in the path tracer. TBB's scheduler steals work from busy threads, so dispatching tens of thousands of small jobs costs about 7x less than a simple thread pool.
> [GEA1] 4.3 Explicit Parallelism, 4.8 Some Rules of Thumb for Concurrency
@ blender/source/blender/blenlib/BLI_task.hh
? Why does the sRGB table give exactly the same bytes as pow()? | Inside each table bucket the curve moves by less than one 8-bit step, so at most one rounding boundary falls inside it. The table stores the bucket's value plus that one threshold.
? Why can a faster algorithm make a scene slower? | Overhead. OpenSubdiv is built for animated meshes and GPUs, and on small one-off subdivisions Blendity's direct Catmull-Clark is about 3x faster. Measure at the sizes you actually use.
! cmd:libs | List the active libraries in the Console
! cmd:select Cube; component DecimateModifier | Add a Decimate modifier (meshoptimizer) to the Cube
)"},
    {"Bevel, Bridge and Push Through", "Edge tools, joining faces, and adjusting the last operation", R"(
Blender's edge tools change topology rather than just moving vertices. Each one is an operator: it reads the selection, rebuilds part of the mesh, and leaves the new geometry selected so you can keep working.
| Tool | Blendity | Blender | ProBuilder / Unity
| Bevel | Ctrl+B (edges selected) | Ctrl+B | Bevel
| Bridge | Ctrl+Shift+B (two faces or two holes) | Edge > Bridge Edge Loops | Bridge Edges
| Push/Pull (SketchUp) | P in face mode, then move or type; click to confirm | (Extrude + Boolean) | ProBuilder Extrude
| Push Through | Alt+P (a face) | (a Boolean difference) | (none)
| Connect | J (two vertices of a face) | J | Connect Edges
| Dissolve / Collapse | Ctrl+X / Edge menu | Ctrl+X / Merge > Collapse | Delete Edges / Collapse
# Bevel
A bevel replaces a sharp edge with a strip of faces. Each face beside the edge pulls its corners back along its other edges by the width. The new strip follows a curve through the old corner, with one face per segment. Where several beveled edges meet, a small patch closes the corner.
@ blender/source/blender/bmesh/tools/bmesh_bevel.cc, blender/source/blender/editors/mesh/editmesh_bevel.cc
# Bridge, at any angle
Bridging removes two faces and joins their outlines with a tube. The outlines must be matched vertex to vertex, which is easy when they face each other and hard when they don't. Blendity rotates one outline onto the other's plane, scales both to unit size, and picks the pairing with the smallest total distance. If the counts differ, the shorter outline gets extra vertices on its longest edges. With more segments the tube follows a curve that leaves one face along its normal and arrives at the other. For opposite faces that curve runs straight through the inside (a tunnel); otherwise it arches outside (a handle).
@ blender/source/blender/bmesh/operators/bmo_bridge.cc
> [FoCG] 15.5 Cubics (the Hermite curve the tube follows), 12.1 Triangle Meshes
# Push/Pull (SketchUp)
Push/Pull is a face operation. Select faces in face mode (3), press P, and move the mouse or type a distance; click or press Enter to keep it, Esc or a right-click to cancel. It moves the faces along their normal. For each side of the face it looks at the neighbouring face. If that face lies in the plane the side moves through, the neighbour stretches or shrinks instead of getting a new wall, the way SketchUp merges coplanar faces. That is why pulling the top of a box makes it taller rather than stacking a second box on it. Pushing past the far side switches to Push Through, and pulling onto a face in front lands every vertex on that face's plane, so a tilted face is met at its own angle, and joins the two. Ctrl always adds walls (SketchUp's "new starting face").
It never cuts through the solid. A push stops at the far side, or at anything inside (a tunnel under the face), unless it can make a clean hole. When the faces around it lean over it, like the slanted top of a wedge, walls would come out through them, so the corners slide along those faces instead, as Blender moves a face. A stress test runs it on every face of 26 awkward meshes at hundreds of distances and checks that no result is broken.
# Tools follow the selection mode
Vertex mode (1) offers vertex operations: merge, connect, make face, smooth. Edge mode (2) offers edge operations: bevel, loop cut, bridge, subdivide, dissolve, collapse and seams. Face mode (3) offers face operations: Push/Pull, extrude, inset, push through, bridge, fuse and materials. The Mesh menu, the Inspector and the shortcuts all show only the current mode's list, as ProBuilder and Blender's Vertex / Edge / Face menus do. A shortcut from another mode says which mode it needs.
# Push Through, and extruding onto a face
Push Through casts a ray from each outline vertex along the face's inward normal and finds where it leaves the object. It cuts that projected shape out of the exit face, so a tilted or curved-in back works too, and joins the two openings with a tube. Extruding (or moving) a face until it lies on another face does the matching join: the face it lands on gets an opening, and the prism becomes part of the solid. When the faces are the same shape, their vertices are welded instead.
> [FoCG] 21.5 Constructive Solid Geometry
# Adjust Last Operation
After an operator runs, a panel at the bottom left of the Scene view shows its settings (F9 toggles it). Changing one doesn't stack a second operation. Blendity keeps a copy of the mesh from before the operator, runs it again with the new values, and replaces the same undo step. The panel also moves the result in X, Y, Z or along the normal, the way Blender lets you adjust an extrude afterwards.
@ blender/source/blender/windowmanager/intern/wm_operators.cc  (WM_operator_last_redo)
@ blender/source/blender/editors/undo/ed_undo.cc
> [GEA1] 1.6 Tools and the Asset Pipeline
? You bevel one edge of a cube with 3 segments. How many faces does it have? | 9. The six faces stay, and the edge becomes a strip of 3 faces. The two faces at its ends take the curve's extra vertices instead of growing new faces.
? Why does a hole through a cube change Euler's formula? | V - E + F = 2 - 2g, where g is the number of holes (the genus). A cube with a tunnel has g = 1, so V - E + F = 0.
! cmd:select Cube; edit edge all; editop bevel | Bevel every edge of the Cube
! cmd:select Cube; edit face; fsel facing 0 0 -1; editop inset; editop push_through | Inset the Cube's front face and push it through
)"},
    {"Rendering on the GPU", "Vulkan, ray tracing hardware and combined CPU + GPU rendering", R"(
A path tracer does the same small job millions of times: follow one ray, bounce it, add up the light. A GPU runs tens of thousands of these at once, so the same render finishes many times faster. Cycles has a backend for each vendor (CUDA and OptiX for NVIDIA, HIP for AMD, oneAPI for Intel, Metal for Apple). Blendity uses one portable API, Vulkan, which every one of those GPUs supports.
| | Blendity | Blender (Cycles) | Unity (HDRP)
| Choose the GPU | Render Settings > Device: GPU Compute | Render Properties > Device: GPU Compute | DirectX 12 / Vulkan backend
| Pick devices | Render window > Render Devices | Preferences > System > Cycles Render Devices | (one GPU)
| RT cores | Hardware Ray Tracing | OptiX / HIP RT / Embree GPU | DXR ray tracing
| CPU + GPU | tick the CPU under Render Devices | tick the CPU in Preferences | (none)
# One kernel on two kinds of processor
The GPU runs a compute shader that is a line-for-line copy of the CPU path tracer, and both use the same random number sequence for each sample. Sample 37 of a pixel gives the same light path whichever device traces it. That is why CPU and GPU samples can simply be averaged. The shader is compiled from GLSL to SPIR-V by shaderc as soon as you choose GPU Compute. That takes a few seconds once, and the result is cached in ~/.blendity/cache.
@ blender/intern/cycles/device
@ blender/intern/cycles/kernel/integrator
# Ray tracing hardware
Most of a path tracer's time goes into finding which triangle a ray hits first. Recent GPUs have fixed-function units for exactly this: NVIDIA's RT cores, AMD's ray accelerators and Intel's ray tracing units. Through VK_KHR_ray_query Blendity builds the GPU's own acceleration structures, one per unique mesh plus one over the objects, so moving an object only rebuilds the small one. On an RTX 4070 SUPER this is about 7x faster than walking Blendity's BVH in the shader. GPUs without the hardware still work, through the software BVH.
> [FoCG] 12.3 Spatial Data Structures
# Combined rendering
With several devices ticked, they share one counter of sample numbers. Each device takes the next few samples it hasn't rendered, and fast devices take bigger batches. The CPU stops taking samples once the GPUs would finish the rest sooner, so it never holds up the end of a frame. Every 100 ms the devices' sums are added together for the display. With fast GPUs the CPU adds little, and switching it off can be quicker. It helps more alongside a GPU without ray tracing hardware.
@ blender/intern/cycles/integrator/path_trace.cpp, blender/intern/cycles/integrator/work_balancer.cpp
? Why can the CPU and GPU render the same frame without seams or blotches? | They don't split the image. Each one renders whole samples of every pixel, from the same random sequences, so averaging their sums gives exactly what one device would.
? Why can adding the CPU make a render slower? | One CPU sample can take 30x longer than a GPU sample. If the CPU is still finishing a sample after the GPUs are done, everyone waits. Blendity stops the CPU from taking new samples near the end, but a fast GPU pair still does better alone.
! cmd:engine path; device gpu +cpu; window Render | Switch to path tracing on the GPU, with the CPU helping
! cmd:engine path; device gpu; render; window Render | Render the Main Camera on the GPUs only
)"},
};

int lesson_count() { return (int)(sizeof(kLessons) / sizeof(kLessons[0])); }
const Lesson &lesson(int i) {
  if (i < 0) i = 0;
  if (i >= lesson_count()) i = lesson_count() - 1;
  return kLessons[i];
}

}  // namespace bl
