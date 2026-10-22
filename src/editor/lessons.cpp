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
# The manual
The same ground in one document, by stage of the work - getting around, reference and planning, blocking out, modeling, UVs, materials, lighting and rendering, rigging, animation, files, performance, shortcuts, the console - with short examples to follow (a brick wall material, rigging a character in five minutes) and what each stage is good for: docs/MANUAL.md, also on GitHub (Help > Open Manual). Each chapter names the lessons here that go deeper. Console: `manual`.
# Installing and sharing
Blendity comes as an installer for each system, from the Releases page on GitHub: a setup program for Windows that installs for you alone (no administrator needed) and opens .scene files with a double-click, an AppImage for Linux (one file: make it executable and run it) and a disk image for macOS. Your work is kept apart from the program, in a project folder - BlendityProject in your home folder unless you open another - so updating or removing Blendity never touches it. A project is just a folder with Assets inside, like Unity's: copy it to another machine and open a scene in it.
? Why is the project kept outside the program's folder? | The program's folder belongs to the installer: an update replaces it and an uninstall removes it. Your scenes, materials and renders must survive both.
# Updates and older versions
Once a day, when online, Blendity asks GitHub whether a newer build exists - every pull request merged into the project makes one, and version tags make the stable releases. If there is one, a window shows what it brings, with Install, Not now and Skip this one. Install downloads it, checks its SHA-256 against the release's list (a corrupted or altered download is refused), saves your scene and installs it. Help > Version History lists every build with its notes: pick an older one to go back to how Blendity was before a certain change. Preferences > Updates turns the daily check off, or limits it to stable releases.
# With or without Blender's libraries
An install holds two programs: Blendity, which uses only its own code and always starts, and Blendity-full, built with Blender's libraries (Embree's ray tracing, OpenImageDenoise, OpenColorIO, Jolt physics, FFmpeg). Blendity starts first and hands over to the full one in a moment - if its libraries are there, they load, and you want them. If one is missing or damaged, or you are offline, Blendity simply runs on its own code; when it is online it asks whether to download the libraries for this build. Preferences > Libraries > Use Blender's libraries is always your choice, and --no-libs starts without them once.
? Blendity starts but path tracing is slower and there is no MP4 output. What is the likely reason and the fix? | It runs without Blender's libraries (missing, damaged or turned off): Preferences > Libraries shows why, and Download or the switch brings them back.
! cmd:update history | Open the Version History
! cmd:manual | Open the manual
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
# Layouts for each stage
A 3D job goes through stages, and each needs only some of the tools. The Layout dropdown (top right, as in Unity; Blender's Workspaces) has a layout for each: Reference & Planning, Blocking Out, Modeling & Topology, UV Unwrapping, Texturing & Materials, Lighting & Rendering, Rigging, Skinning & Weight Painting and Animation. Each arranges the windows for its stage (the UV Editor beside the Scene view for UVs; the Materials and Textures windows for texturing; the Rigging window for rigs) and shows that stage's tools in the Modeling Tools window, the GameObject menu and Add Component; the others step aside. Nothing is gone: the keys and the console reach every tool, and Window > Layouts > Show All Tools shows them all. Default shows everything, as do the arrangements (2 by 3, Tall, Wide, Learning). Save Layout... keeps your own arrangement under a name.
| Stage | Windows | Tools shown
| Reference & Planning | Scene, Game, Project | create, reference images, cameras
| Blocking Out | Scene, Modeling Tools | primitives, drawing, snapping
| Modeling & Topology | Scene, Modeling Tools | Edit Mode, checks, curves and lattices
| UV Unwrapping | Scene + UV Editor | Edit Mode, UV
| Texturing & Materials | Scene, Game, Materials, Textures | materials, textures, baking
| Lighting & Rendering | Scene, Game, Lighting, Render | lights, cameras
| Rigging / Skinning | Scene, Rigging | bones, skins, weights, IK
Reference & Planning has GameObject > Reference Image...: a picture to model from, an unlit plane the picture's shape, kept out of renders (Blender: Add > Image > Reference; console `refimage <path> front|side|top`).
! layout:Modeling & Topology | Use the Modeling & Topology layout
! layout:UV Unwrapping | Use the UV Unwrapping layout
! layout:Texturing & Materials | Use the Texturing & Materials layout
! cmd:alltools on | Show All Tools
! layout:Default | Back to Default (everything)
? A tool you need isn't in the menu in this layout. What do you do? | Its key and its console command still work; or Window > Layouts > Show All Tools; or switch to the layout of that stage.
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
# Object fields: drag, don't type
Some fields name another object: a Boolean's or a Lattice's Object, a Curve modifier's curve, an IK Target and Pole, a skin's Armature. Type the name, or drag the object from the Hierarchy and let go over the field, as with Unity's object fields (Blender: the eyedropper, or dragging from the Outliner). The field lights up while an object is over it. Pressing a row and dragging it onto a field doesn't select it, so the Inspector stays on the object whose field you're filling (in Unity the press selects, so you lock the Inspector first); a click, or a drag anywhere else, selects as usual.
! cmd:select Cube; component BooleanModifier | Give the Cube a Boolean, then drag another object onto its Object field
? Why does Unity need the Inspector's lock icon to drag one object onto another's field, and Blendity doesn't? | Unity selects a row as you press it, which turns the Inspector to the dragged object. Blendity selects when the button comes up, and not at all when an object field took the drop.
# Typed fields and asset fields
Each object field says what it takes, as Unity's do: a skin's Armature reads None (Armature) until you drop an armature on it, then Armature (Armature); a Lattice modifier's Object takes only a Lattice, a Boolean's only a mesh. Drag an object of the wrong kind over a field and it turns red; let go and nothing changes (the Console says why). The small target at the field's right (Unity's object picker) lists None and every object of that kind in the scene. Fields that hold a file work the same way with the Project window: drag a clip (.anim) onto an Animation's Clip, or a .seq onto a Sequencer's Sequence; the picker lists the project's files of that kind. A press on a file row that turns into a drag doesn't select the file, so the Inspector stays where it was.
? A Skin's Armature field turns red as you drag the Cube over it. Why? | The field takes objects with an Armature component; the Cube has none, so the drop would be refused.
! cmd:create Armature; create Cube; component SkinModifier | A Cube with a Skin modifier: drag the Armature from the Hierarchy onto its Armature field
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
# Blender's modal keys, Unity's navigation
In the Unity feel (the default) navigation is Unity's (hold RMB to fly, Alt+LMB orbit, MMB pan), and so are the W / E / R gizmo tools; Edit > Editor Feel switches to Unreal's, Blender's or Maya's. Editing works the Blender way. G grabs the selection and moves it with the mouse. During a G, R rotates and S scales. X, Y or Z lock to an axis (press twice for the object's own axis), and Shift+X / Y / Z lock to the plane without it. Type a number for an exact value, hold Ctrl to snap, then click or press Enter to keep it, or press Esc to cancel. Ctrl+E extrudes and then moves along the normal, I insets as you move toward the centre, and Ctrl+B bevels as you move away (the wheel sets the segments). Edit > Blender Transform Keys makes R and S rotate and scale directly. docs/USABILITY.md explains how the two keymaps share the keyboard.
@ blender/source/blender/editors/transform/transform_mode_translate.cc
! cmd:select Cube; edit face; fsel facing 0 1 0 | Select the Cube's top face, then press G, Z, type 0.5 and Enter in the Scene view
# Work like Unreal, Blender or Maya
Coming from another program, your hands already know its keys and its mouse. Edit > Editor Feel (also in Preferences > Keymap, or the console's feel command) switches both together. Unity is the default. Unreal keeps Unity's navigation (hold RMB and WASD to fly, Alt+LMB orbit) and adds Alt+P to play, End to drop the selection to the floor, and Alt+G / H / J / K for the perspective, front, top and side views; holding LMB and RMB together pans. Blender orbits with MMB, pans with Shift+MMB and zooms with Ctrl+MMB or the wheel, with G / R / S, Tab and A as in Blender; RMB and Alt+LMB don't move the view there. Maya tumbles with Alt+LMB, tracks with Alt+MMB and dollies with Alt+RMB, with Q / W / E / R and F8 for components. Window names, layouts and the Y-up axis stay Unity's whatever the feel. The keymap preset can still be chosen alone afterwards, and your own key changes stay on top of whichever feel you pick.
? A Blender user drags with RMB in the Scene view and nothing happens. Why, and what should they use? | In the Blender feel RMB doesn't move the view (Blender keeps it for menus); the view orbits with MMB, Shift+MMB pans. Alt+click selects a loop, as in Blender.
? Which part of the editor doesn't change with the feel? | Window names, layouts and the up axis: only keys and mouse navigation change.
! cmd:feel Blender | Switch to the Blender feel (MMB orbits)
! cmd:feel Unity | Back to the Unity feel
# Colours: Dark, Light and Emiliano
The editor's colours are a theme: Preferences > Theme, or the console's theme command. Dark is Blendity's own, close to Unity's Professional skin; Light is like Unity's Personal skin. Emiliano Light and Emiliano Dark take the palette of the author's site, defined as variables in Figma: a warm paper background (#F1EEE5) with charcoal text (#292927) in Light, the two swapped in Dark, an amber accent (#D7882B) for selection and buttons, and its signal colours for meaning: mint for focus and the green axis, red for errors and X, blue for Z, amber for warnings. The windows draw from the theme, so a list's stripes, a key's diamond, a tooltip and a ruler change with it; the Scene and Game views and the UV editor's drawing keep their own colours, which read over any picture. The choice is kept with your preferences.
? Why do the themes keep red, green and blue for the X, Y and Z axes? | Every 3D program uses them; a theme may change their shade, but changing their meaning would mislead.
! cmd:theme Emiliano Light | Switch to Emiliano Light
! cmd:theme Dark | Back to Dark
)"},
    {"Modifiers vs Components", "Non-destructive editing and Catmull-Clark", R"(
Blender's modifier stack changes a mesh on the fly without touching the original: Subdivision Surface, Mirror, Smooth... Each modifier feeds the next. Unity has no modifier stack. Any procedural change is done by a script component.
Blendity combines the two ideas: modifiers are components, shown together in the Inspector's Modifiers panel as Blender's stack. Add Modifier lists them by Blender's groups: Edit (Weld), Generate (Array, Bevel, Boolean, Decimate, Mirror, Screw, Solidify, Subdivision Surface, Triangulate, Wireframe) and Deform (Cast, Curve, Displace, Lattice, Mesh Deform, Simple Deform, Smooth). They run from the top down, and the result is cached until the mesh or a setting changes. Each header has Blender's three toggles: Edit Mode (draw the result while you edit the cage), Viewport and Render. Its menu applies, duplicates, copies to the other selected objects or moves the modifier. Apply bakes one modifier into the mesh, like Blender's Ctrl+A; Apply All bakes the whole stack.
# Order matters
Mirror then Bevel bevels the seam in the middle; Bevel then Mirror doesn't, because the seam doesn't exist yet when the bevel runs. Array then Simple Deform bends the whole row; Simple Deform then Array repeats one bent copy. Move a modifier up or down and watch: drag its name in the header to another place in the stack (a line shows where it lands, as in Unity's Inspector and Blender's stack), or use its menu's Move Up / Down. The same drag reorders a camera's filters, a texture's layers and the Sequencer's tracks; each drop is one undo step, and a click without moving still folds the header.
? You drag Bevel from the top of a stack of Bevel, Array and Smooth to below Smooth. What is the order now? | Array, Smooth, Bevel: the dragged modifier goes where the line was, and the ones below it move up.
! cmd:select Cube; component BevelModifier; component ArrayModifier | Give the Cube two modifiers, then drag Bevel's name below Array's in the Inspector
# A lathe from a line
Screw sweeps edges round an axis. Draw a profile with wire edges (Edit Mode, vertex mode: select a vertex and press Ctrl+E repeatedly to grow a line), add Screw, and the outline becomes a vase or a bottle. Screw > Screw raises each turn: springs and threads.
# Bending along a curve
GameObject > Curve adds a path: Bezier (smooth, near its points), Catmull-Rom (through every point) or Poly (straight lines). Click the curve to select it, click a point to select that point (Shift adds), then move, turn or scale points with the gizmo; Esc lets go of them. A Curve modifier on a mesh bends it along the curve: the mesh's X (or the Deform Axis) runs along the path from its start, and the other two axes stay across it, so a straight curve changes nothing. Array with Fit Type Fit Curve makes as many copies as fit the curve's length: Array then Curve, both naming the same curve, makes a fence, a chain or a ring of blocks. Each point's Tilt twists the mesh round the path and its Radius thickens it there.
# More points, by the vertex and edge keys
Curves and lattices take Edit Mode's keys for their points (your keymap's, Unity's shown first, Blender's in brackets). Click a curve's end point and press Extrude (Ctrl+E, E): a new point continues the curve, selected, ready to move. Loop Cut (Ctrl+R) with the mouse over a segment drops a point on the curve right there; over a lattice's edge it adds a row of points across it, keeping the lattice's shape. Merge at Center (Alt+M, M) joins the selected points into one; Delete removes the selected points (a curve keeps two), or a whole selected row of a lattice. Alt+C opens or closes a curve. Each is one undo step. Console: `points extrude|cut|merge <object> <index...>`, `points close <object>`, `points lattice <object> u|v|w +1|-1`.
! cmd:create Curve: Path; select Path | Make a curve to grow: click its last point, then Ctrl+E
? Why does a lattice's new row land at even spacing, not under the mouse? | Lattices are uniform grids (as in Blender): their points sit at even fractions of the box. The row is added across the edge's axis, and the old shape is kept by resampling.
# Squashing with a lattice
Select a mesh and choose GameObject > Lattice: a box of points is fitted round it and the mesh gets a Lattice modifier naming it. Click a point and move it with the gizmo: the part of the mesh near it follows. Points U / V / W add points along each side (the shape is kept); Linear follows the points exactly, B-Spline is smoother and softer. Strength on the modifier fades the effect. It is how cartoon squash and stretch, bulging muscles and bent buildings are done without touching a vertex.
# A cage round a mesh
A lattice is always a box; a cage can be any shape. Model a rough, closed, low-poly mesh round a detailed one (a few boxes round a character), add Mesh Deform to the detailed mesh, set Object to the cage and press Bind. Now move the cage's vertices in Edit Mode: the detailed mesh follows smoothly, near parts most. Blendity binds with mean value coordinates (Ju, Schaefer and Warren 2005): each vertex is a weighted average of the cage's vertices, weights that reproduce any move, turn or stretch of the whole cage exactly. The cage must be closed; hide it from renders (Display As Wire) and keep Mesh Deform first among the deforming modifiers.
# Vertex groups
A vertex group is a name and a weight (0 to 1) for every vertex. In the Inspector, MeshFilter > Vertex Groups > Add makes one; in Edit Mode select vertices, set Weight and press Assign. Every deforming modifier (Displace, Simple Deform, Smooth, Cast, Curve, Lattice, Mesh Deform) has Vertex Group and Invert: name a group and only its vertices move, each as much as its weight, so a Displace can rough up the top of a rock and leave its base. The groups follow their vertices through every edit: a vertex that stays keeps its weights, a new one takes what the surface had where it is.
# Weight painting
Under Vertex Groups, press Weight Paint: the mesh turns blue (weight 0) to red (weight 1) for the chosen group, and dragging over it paints. Add raises the weights, Subtract lowers them, Draw moves them towards Weight, Blur evens out sharp borders. Radius is in screen pixels, Strength how much each dab gives, Falloff how it fades towards the edge of the brush. Mirror X paints both sides of a symmetric model at once; Front Faces Only stops the brush reaching the back. Each stroke is one undo step. Normalize All makes every vertex's weights add up to 1 (what a skin wants), Clean drops tiny weights.
# Bones and skins
A skeleton is a hierarchy of bones: in Blendity each bone is a GameObject with a Bone component, under a root with an Armature. Pose it by turning bones with the Rotate gizmo, like any object. A mesh follows through its Armature (Skin) modifier: each vertex moves with the bones its vertex groups are named after, weighted. Select a mesh and choose GameObject > Armature: a bone the mesh's height is made and the mesh bound to it with automatic weights (by distance); `bone add <parent>` adds bones at a parent's tip, and Bind: Automatic Weights binds again; Weight Paint fixes what automatic weights get wrong. Linear blending makes twisted joints thin (the candy wrapper); Armature > Deform: Dual Quaternion keeps their girth. Armature > Rest Position shows the mesh unposed; `armature reset` puts the bones back, `armature apply` makes the pose the new rest. Rigged FBX and glTF characters import with all of this set up. A character saved mid-pose imports in that pose: each bone's rest is its bind pose (where it was when the skin was bound, as Blender, Unity and Maya read it), so `armature reset` gives the shape it was modelled in.
# Inverse kinematics
Turning a thigh and then a shin until the foot lands on a step is slow; IK does it backwards. An IK constraint on the shin (Chain Length 2: shin and thigh) turns both so the shin's tip reaches the Target object: move the target and the leg follows, every frame and in Play. Two bones have one exact answer (the triangle of thigh, shin and hip-to-target, the law of cosines) up to which way the knee points: a Pole object picks that. Longer chains (a tail, a spine) are solved by cyclic coordinate descent: from the tip up, each bone turns the tip towards the target, a few dozen rounds. Weight fades from the rest pose to the solution. A bone IK turns refuses the gizmo and G / R / S: move its target. GameObject > Rig > Humanoid or Quadruped makes a whole skeleton with targets (FootIK, HandIK) and poles; Auto IK on Selected Skeleton finds the legs and arms of an imported Mixamo, Unity or Rigify skeleton by their names. Console: `ik add <bone>` (makes its target and pole) or `ik add <bone> <target> [pole] [chain]`, `rig humanoid|quadruped|autoik`. All of it is in the Rigging window (Window > Rigging).
> Welman, C. (1993) Inverse Kinematics and Geometric Constraints for Articulated Figure Manipulation. MSc thesis, Simon Fraser University (CCD).
@ blender/source/blender/ikplugin/intern/iksolver_plugin.cc
? A thigh and a shin are 0.45 m and 0.43 m. Can the foot reach a target 0.9 m from the hip? | No: the chain is at most 0.88 m long, so IK points the straightened leg at it.
! cmd:rig humanoid; window Rigging | Make a humanoid rig and open the Rigging window
# Automatic weights by inpainting
Weights by distance alone (Envelope) go wrong wherever two parts sit close: a hand hanging by the thigh takes some of the leg, an inner arm some of the chest. Bind: Automatic Weights does it in two steps, after Epic's "Robust Skin Weights Transfer via Weight Inpainting" (SIGGRAPH Asia 2023). First, the vertices it can trust: those clearly nearest one bone (nearer than 0.8 of the next one's distance) and in its sight (nothing of the mesh between the bone and the vertex) take that bone whole. Then the rest are inpainted: given the trusted weights, it finds the smoothest weights for everything else, the ones that change least in curvature over the surface. That is one sparse linear solve per bone, Q w = 0 with the trusted weights fixed, Q = -L + L M^-1 L built from the cotangent Laplacian L and the vertex areas M: a bi-Laplacian, so the weights blend round joints without kinks. A loose part with nothing trusted (a button, an eye) takes the distance weights. Large meshes solve on a worker thread with a progress bar; the editor keeps drawing.
> Abdrashitov, R., Raichstat, K., Monteiro, J., Stoll, C., Dvoroznak, M., Jacobson, A. (2023) Robust Skin Weights Transfer via Weight Inpainting. SIGGRAPH Asia 2023 Technical Communications.
> Meyer, M., Desbrun, M., Schroeder, P., Barr, A. (2003) Discrete Differential-Geometry Operators for Triangulated 2-Manifolds (the cotangent Laplacian and Voronoi areas).
? Why does the solve fix some weights instead of computing all of them? | With nothing fixed, the smoothest weights are constant: every vertex the same. The trusted vertices are the boundary condition that makes the answer mean something, as the temperature at a radiator does for heat spreading through a room.
? A vertex in the armpit is about as near the upper arm as the chest. What happens to it? | It isn't trusted (the ratio test fails), so it's inpainted: it gets a smooth blend of both from the trusted vertices around it.
! cmd:skin bind | Bind the selected mesh again, by inpainting
! cmd:skin bind envelope | Compare: bind it by distance alone (Envelope)
# Weights onto clothing
A shirt, a sleeve or armour should follow the same bones as the body under it. Rigging > Transfer Weights copies them: drag the rigged body from the Hierarchy onto From, then press Transfer Weights (console: skin transfer <body> [mesh]). Each vertex of the clothing looks for the closest point on the body; it copies the body's weights there only when that point is near (within 5 % of the clothing's size) and faces the same way (normals within 30 degrees, or opposite, for a garment's inside). Everywhere else - a sleeve's end that hangs past the hand, a collar near the neck and the shoulders at once - the weights are inpainted from the copied ones, as in automatic weights. A vertex group named InpaintMask (weight over 0.5) forces its vertices to be inpainted, for places where copying would pick the wrong part. The clothing gets the body's Armature (Skin) modifier.
? A sleeve's end passes close to the hip. Plain closest-point copying (Blender's Data Transfer) gives it hip weights. Why doesn't Transfer Weights? | The hip's surface there faces sideways, the sleeve's outward from the arm: the normals disagree, so the match is refused and those vertices are inpainted from the sleeve's arm-weighted part.
! window:Rigging | Open the Rigging window (Transfer Weights)
# Auto-rig with markers
Mixamo rigs a character from a few markers: you tell it where the chin, wrists, elbows, knees and groin are, and it fits a skeleton. Rigging > Auto-Rig does the same, for people and for four-legged animals. Select the mesh and press Humanoid or Quadruped: the Scene view turns to an orthographic front view (an animal: its left side) and the Rigging window draws a little figure with the marker to place next in orange. Click it on the mesh. The marker goes inside the body, not on its skin: the click's ray enters the first part under the mouse and leaves it again, and the marker sits halfway between - the middle of the arm, not its front. With Symmetry on, placing a left marker places its right twin, mirrored across the mesh's middle; drag any marker to move it, Remove Last takes one back. Build Rig makes the skeleton with the same bone names as GameObject > Rig (Hips, Spine, Chest, Neck, Head, LeftUpperArm, ...; an animal also gets a three-bone tail), puts IK on every limb, and binds the mesh with inpainted weights. Bones the markers say nothing about follow proportions: shoulders 40 % of the way from the neck to the elbows, ankles just above the floor, a quadruped's leg tops a quarter of the way down from the withers or the hips.
? Why do the markers go to the middle of the limb and not where you clicked on its surface? | A joint turns about the inside of the limb. A marker on the skin would put the bone at the surface, and bending it would fold the mesh round the wrong axis.
? You placed the left elbow with Symmetry on. Where is the right one, and when would you turn Symmetry off? | Mirrored across the mesh's middle (same height and depth, the other side). Turn it off for a character posed unevenly, such as one arm raised.
! cmd:rig humanoid; window Rigging | Compare: the Humanoid preset rig (the same bone names Auto-Rig makes)
! window:Rigging | Open the Rigging window (Auto-Rig)
# Keyframes and the dopesheet
An animation is numbers that change over time: a bone's rotation, a door's position, a light's intensity. You set a few of them at chosen frames (keyframes) and the program fills in the frames between. Window > Animation is Unity's Animation window: select an object, press Create and it gets an Animation component and a clip (Assets/Animations/<name>.anim). Each row is one number of one object below it (Lid : Position.y); each diamond a key. Turn on Record (the red dot), move the playhead along the ruler, and move or turn objects with the gizmo or the Inspector: every change becomes a key at the playhead. Preview shows the clip on the scene as you scrub, and turning it off puts everything back, so scrubbing never changes the scene. Click a key to select it, Shift+click or drag a box for several, drag them along the ruler to retime them, Delete removes them, Ctrl+C / Ctrl+V copy them to the playhead, double-click a row to add one, right-click for how the curve leaves a key: Auto (smooth, never overshooting between two keys, Unity's Clamped Auto), Linear or Constant (a jump). In Play the Animation component plays its clip (Loop, Once, Ping Pong, Clamp Forever), before IK solves, so you can animate IK targets as well as bones. A clip names objects by their path below the animated root (Hips/Spine), so it plays on any character with the same bone names.
? Why does the clip store "Hips/Spine" rather than the bone itself? | So the clip is a separate asset you can reuse: any object whose children have the same names can play it. (Rename the bone and its rows turn yellow: the clip can't find it.)
? Two keys: 0 at frame 0, 10 at frame 20. What is the value at frame 10 with Linear, and with Constant? | Linear: 5, halfway. Constant: 0 - it holds the first key's value until frame 20, then jumps.
! cmd:window Animation | Open the Animation window
! cmd:layout Animation | Use the Animation layout (the Animation window along the bottom)
# Curves and tangents
The dopesheet says when keys are; the Curves view (the Animation window's Curves button, as Unity's) says how the value travels between them. Each property is a line of its value over time: x red, y green, z blue. Click track names to choose which curves show (Ctrl+click for several), F to frame them, the wheel to zoom the values. Drag a key to move it in time and value (Shift: one direction only). A selected key shows its tangent handles: the slope the curve leaves it at. Drag one and the key turns Free Smooth (the same slope both sides, so the motion stays smooth); right-click for Broken (each side its own slope: a bounce, a sudden turn), Flat (level: an ease in and out) or back to Clamped Auto (worked out from the neighbours, never overshooting). The curve is a cubic Hermite spline: between two keys, the one smooth curve with their two values and those two slopes.
? A ball drops and bounces. Which tangent mode suits the key where it hits the floor? | Broken: it comes down steeply (a big negative in slope) and leaves going up (a positive out slope), a sharp corner at the floor.
? Why does Flat at both ends of a door's swing look natural? | The slope is 0 at each key, so the door starts slowly, speeds up and slows to a stop (an ease in and ease out).
! cmd:window Animation | Open the Animation window (then Curves)
# Physics for animation: the center of mass
Cascadeur makes keyframed animation physical by weighing the body. Blendity does the same: each bone weighs the share of the skinned mesh it moves (its volume, scaled so the whole is the Armature's Body Mass, 75 kg unless you change it), and an armature without a skin gets capsules round its bones. The Animation window's Center of Mass button draws where the body's center of mass goes over the whole clip, where it is at the playhead, and the point straight below it. A standing pose looks balanced when that point is over the feet.
! cmd:window Animation | Open the Animation window (then Center of Mass, with a character selected)
? Why does a character with heavy legs keep its center of mass lower than one with heavy arms raised? | Each bone's mass sits where its share of the mesh is; the center is the mass-weighted average of those places.
# Animation assistants: switched on when you want them
Blendity has tools that make animation for you - the physics tools (center of mass, contacts, flight, AutoPhysics, the ragdoll, AutoPosing, spring bones), Walk Cycle, footsteps along a path, Locomotion in Play and AI inbetweening. Not every animation needs them, so each is a switch in Preferences > Animation Assistants: the physics tools start on, the others off. A tool switched off is simply not there - no buttons, no overlays, its console command says how to turn it on. Animating by hand never changes: keys, curves, Record, posing and the IK and FK controls work the same with every assistant off, and whatever an assistant makes is ordinary keys you edit, delete or undo like your own.
! cmd:assist list | List the assistants and whether each is on (console: assist on walk, assist off physics)
? You switch the physics tools off: what happens to the keys AutoPhysics wrote last week? | Nothing: they are ordinary keys in the clip. Only the buttons and overlays go.
# Walk Cycle: walking made from the rig
Walk Cycle (switch it on in Preferences > Animation Assistants) makes walking and running cycles from the rig, as 3ds Max's CATMotion does: pick a preset - walk, jog, run, sneak, limp, or walk, trot, gallop for four legs - in the Rigging window's Walk Cycle section, change any of its numbers (speed, stride, how long each foot stays down, foot lift, heel-toe roll, bounce, sway, hip twist, arm swing, lean, crouch) and press Generate. Each foot is planted at one spot for as long as it is down, so it never slides; the pelvis bounces, sways and twists over the planted foot, the spine turns back against the hips and the arms swing against the legs. The stride follows the leg's length (the Froude number), so the same preset looks right on a child or a giant. In Place makes a treadmill loop for games; off, the body walks forward through the clip. The keys land on the rig's own controls - the hips, the feet's IK targets, spine and arms - so you tweak the result by hand like any animation. Console: walk cycle run 4 2 travel, walk set bounce 0.04, walk save MyWalk.
! cmd:rig humanoid; anim new Walk; assist on walk; walk cycle walk 1.4 2 travel | A person walking two cycles forward (Walk Cycle switched on)
? Why does a run have frames with both feet off the ground and a walk never? | Each foot is down for less than half the cycle in a run (Feet Down under 0.5), so there are moments when neither is; a walk keeps each down for more than half.
? A character twice as tall walks with the same preset: why are its steps not just twice as long and as quick? | The Froude number: at the same look its speed grows with the square root of its leg, so its steps are twice as long but only 1.4 times slower.
# Footsteps: walking along a path
Footsteps (switch it on in Preferences > Animation Assistants) walks a character along a curve, as 3ds Max's Biped footsteps do. Draw a path (Add > Curve > Path), pick it in the Rigging window's Footsteps section and press Walk Along Path: footprints are laid along it, left and right in turn, at the stride of Walk Cycle's style - shorter for the first and last steps and in turns, the inner foot's shorter than the outer's - and each lands on whatever is under it (a floor, a ramp, the stairs' steps). The character is keyed walking on them: each foot planted on its print, the pelvis carried over the planted feet at their height, swaying and twisting with the gait, the swing foot lifted higher over a step up. Every print is an object of its own (blue left, red right, its landing frame on it): move or turn one with the gizmo, change its Frame in the Inspector, and Update walks the character on the prints as they are; Re-plan lays them out along the path again (after changing the path or the style) and keeps the ones you moved by hand. The keys are ordinary keys, one undo step each time. Console: walk path <curve>, walk steps update, walk steps replan.
! cmd:rig humanoid; anim new Steps; assist on footsteps; create Curve: Path; select Humanoid; walk path Path | A person walking along a path on its footprints (Footsteps switched on)
? Why are the inner foot's steps shorter in a turn? | It walks a smaller circle: both feet take the same number of steps, so the inner foot covers less ground with each.
? You drag one print 10 cm sideways and press Re-plan: where does that print end up? | Where you put it: Re-plan keeps prints moved by hand and lays out only the others.
# Locomotion: walking live in Play
Walk Cycle and Footsteps write keys; Locomotion (switch it on in Preferences > Animation Assistants) walks a character live in Play instead, as a game's locomotion controller does. Add the Locomotion component to the character's root and press Play: W walks forward, Shift+W runs, A and D turn, S steps back slowly - or a PlayerController, a Rigidbody or a script's set_move moves the character and Locomotion follows. Each frame the gait goes on with the speed (the walk blending into the run as it speeds up), each swinging foot is aimed at where the body will be when it lands and put on whatever is under it there - a ramp, stairs - and the pelvis stays within reach of both feet, leaning into turns. Let go and it finishes the step it is taking, then stands with both feet down. Upper Body off leaves the spine, head and arms to an Animation clip (carrying something, aiming) while Locomotion walks the legs. Nothing is keyed: Stop puts everything back.
! cmd:rig humanoid; assist on locomotion; component Locomotion | A person with Locomotion: press Play, then W, A, D and Shift
? Why does a foot land where the body will be, not where it is now? | The body moves while the foot swings: aiming at where the body will be half-way through the next stance keeps it under the body, so the step looks balanced at any speed.
? You let go of W mid-step: why doesn't the character freeze on one leg? | It finishes the step under way - the swinging foot lands - and only then stands still.
# Automatic walking: which tool when
Blendity has three ways to make a character walk, each a switch in Preferences > Animation Assistants (all off until you want them; hand animation works the same either way). Walk Cycle makes a loop or a straight walk from a preset - the quickest start for a game's walk, run or sneak. Footsteps walks a character along a curve on footprints you can move one by one - for a route, a corner, stairs. Locomotion walks it live in Play wherever the keys, a controller or a script move it - nothing is keyed. The first two write ordinary keys you edit like your own (and AutoPhysics can make them physically possible afterwards); the third never touches the clip.
! cmd:assist on walk; assist on footsteps; assist on locomotion; assist list | Switch the three walking tools on and list the assistants
? You need a character to climb a staircase in a cutscene: which tool? | Footsteps: lay a path up the stairs; each print lands on a step and the body is keyed walking on them.
? A game character must walk wherever the player steers it: which tool? | Locomotion: it steps live in Play from how the character moves, so nothing has to be keyed for every direction.
# AI inbetweening: the motion between two keys
AI Inbetweening (Preferences > Animation Assistants; it needs its download - ONNX Runtime and the model, about 25 MB - offered the first time you switch it on, or Install there, or the console's ai install) fills the motion between two keys with a small network trained on ten hours of the CMU motion-capture database, as Cascadeur's AI Inbetweening does. Put the playhead between two keys, pick a style (walk, run, jump, fall, crawl, combat, acrobatic or other) and press AI Inbetween in the Animation window: the span is made in the background and shown as a purple ghost; Variation makes it again a little differently, Apply writes it as keys (one undo step). Only that span changes - the keys at its ends and everywhere else stay exactly as you set them - and if you change a key at either end afterwards the ghost turns grey: generate again. Spans of up to about a second work best: longer ones slide the feet more (Contacts and AutoPhysics clean them up). Fit to Clips teaches the network how your character moves from the clips of yours that animate it (its last layer, fitted in the background in a few seconds; ai finetune off goes back to the shipped model).
! cmd:rig humanoid; anim new Inbetween; anim key Hips Position 1 0 0.95; anim key Hips Position 1 40 0.95; assist on ai; ai style walk; ai span 0 40 | A person with two keys 40 frames apart, AI Inbetweening asked to fill them (it says how to get its download if it isn't installed)
? Why does AI Inbetween leave the keys at both ends exactly as they were? | It only makes the frames strictly between them: the network is given both ends and only fills the gap, so your poses stay yours.
? You move the second key after generating: why does the ghost turn grey instead of updating? | Nothing changes your keys on its own: the made span belongs to the old keys, so it waits for you to press AI Inbetween again.
# Importing models: their materials made for you
Drop a model onto the window (or File > Import, or the console's import <file>) and Blendity does what Unity's importer does. The file goes into Assets/Meshes, its textures into Assets/Textures/<model>, and every material into Assets/Materials/<model>/<name>.mat, already on the model's meshes: edit a .mat and every copy of the model changes with it. Textures packed inside the file (FBX's Embed Textures, a .glb's images) are taken out. A link that points somewhere that no longer exists (another machine's C:\ path) is looked for beside the model, in its .fbm folder and in a textures or images folder. A material the file left bare gets a texture set found by name: Body_BaseColor, Body_Normal, Body_Roughness, T_Body_ORM and so on, matched to the material's or the mesh's name, or the folder's only set. The finder looks through the folders around the model (its own and three levels below, and the folders beside it, as Sketchfab's source/ and textures/ are laid out), reads two-word names (Base_color, Normal_OpenGL), ignores sizes (_2k, _4096), and pairs each bare material with the set its name is likeliest to own; the Console says which file went where, and a wrong guess is one slot to change in the material's Inspector. Gloss and smoothness maps are inverted into roughness, and packed maps (glTF's metallic-roughness, Unity's metallic-smoothness, Unreal's ORM) are split into the channels Blendity reads. A mesh with no material at all gets one named after it. Importing the model again keeps the .mat files you edited.
! cmd:window Project | Open the Project window (Assets/Materials/<model> holds an imported model's materials)
? A model's textures are in a folder called "textures" beside it, but the FBX names a path on someone else's computer. What happens? | Blendity looks for the file by its name beside the model, in its .fbm folder and in textures/images folders, and uses the one it finds.
? You recoloured an imported model's material and import the model again. Is your colour lost? | No: the .mat file already exists, so the import uses it as it is.
? A Sketchfab download has source/model.fbx and textures/Body_Base_color.png, and the FBX names no textures. Is Body textured? | Yes: the finder reads the folders beside the model and pairs Body with the Body_Base_color set; the Console says so, and the Inspector can change it.
# Shape keys: faces and morphs
A shape key (Blender's name; Unity says blend shape, glTF morph target, MMD vertex morph) is a named offset of a mesh's vertices: a smile, a blink, a puffed cheek. The mesh keeps its basis shape and each key; the MeshRenderer's BlendShape sliders say how much of each is added, from 0 to 1, before the skin bends the mesh, so an expression moves with the head. Imported FBX, glTF, MMD and VRM characters bring their keys and values. The weights are ordinary numbers to key in the Animation window (Add Property > MeshRenderer > BlendShape/...). Console: shape list, shape add <name> [amount] (a key pushing the surface out along its normals), shape set <name> <weight>, shape remove <name>.
! cmd:create Sphere; shape add Puff 0.2; shape set Puff 0.5 | A sphere with a shape key "Puff", half applied (see MeshRenderer > BlendShape/Puff)
? Why are shape keys added before the skin rather than after? | So the expression is bent with the bones: a smile keyed on the face turns with the head instead of staying where the face was at rest.
? An imported face has 50 keys. Does every frame cost 50 times as much? | Only the keys with a weight above 0 are added.
# Importing: preview the look
A model dropped onto the window (or opened with File > Import) is shown first, in the Import Preview (task 0152): the
model on a slow turntable (drag to turn it) lit from the preview's own camera, with the import's notes beside it - what the
file held, which textures were found. The Shading row decides how every material comes in before anything is
written: As Imported (what the file made: a VRM's MToon, an MMD's toon), PBR, or a toon style - Classic, Guilty
Gear, Soft Anime, Painterly, Manga - over the file's materials (their maps stay). Outline puts a line on every mesh.
Import writes the model, its textures and the .mat files with that shading into Assets (a model imported before gets
its existing .mat files rewritten with the look, unless As Imported); Cancel writes nothing. Several files dropped at
once take their turns, one preview after another.
Preferences > Import turns the preview off (the model comes in at once, as before) and sets the shading it opens
with. From the console, import <file> <shading> skips the dialog: import Assets/Meshes/hero.fbx guiltygear.
? Why pick a toon style at import rather than afterwards? | The .mat files are written with it, so every copy of the model and every scene that uses them has the look from the start; the Materials window changes them later all at once.
! cmd:window Project | Open the Project window: drop a model onto the Scene view to see the Import Preview
# MMD models: PMX and PMD
MikuMikuDance characters (.pmx, and the older .pmd) import like any model: drop one onto the window. MMD measures in units of 8 cm and faces its characters the other way, so the model comes in scaled to metres and turned to face +Z like every other character. Its bones point at their tails, and the standard ones get the names Blendity's rig tools read (左ひざ becomes LeftLeg, 頭 Head), so Walk Cycle, Add IK and the physics tools find the legs, spine and head. Hair, skirt and accessory bones keep their own names. The legs' IK comes with it (an IKConstraint on each knee, aimed at the foot's IK bone, with a pole in front of the knee). Bones MMD simulates (hair, skirts, ribbons) become Spring Bones chains. Faces (vertex and group morphs) become shape keys under their Japanese names. Materials are Toon-shaded with MMD's colours, textures and toon ramp, and the model's edge becomes an Outline. Bone, UV and material morphs, and bones that copy another's turn (twist and eye helpers), are listed in the Console as not carried over.
! cmd:window Project | Open the Project window: drop a .pmx or .pmd there or onto the Scene view
? Why does an MMD model come in 0.08 times its size in the file? | MMD's unit is about 8 cm: a model 20 units tall is a 1.6 m person in metres.
? A PMX's hair swings in MMD. What makes it move here? | Its simulated bones become Spring Bones chains, which move in Play and can be baked into keys.
# MMD motions: VMD dances on your character
A .vmd file is a MikuMikuDance motion: keys for bones by their Japanese names, for the face's morphs, and switches for the legs' IK. Drop one onto the Scene view with an MMD character selected (or File > Import, or the console's import <file>) and it becomes Assets/Animations/<character>/<motion>.anim, played by the character's Animation component and open in the Animation window like any clip. MMD keys a bone as a turn in its rest frame; Blendity's bones point at their tails, so each key is converted into the bone's own rotation, and MMD's timing curves become the keys' slopes - where a curve can't follow a long turn exactly, the frames between are keyed from the true arc. Morph keys land on the BlendShape sliders of every mesh that has the morph; IK switched off in the motion keys the constraint's Weight to 0. Bones the character hasn't are listed in the Console. MMD's own camera and lights aren't imported.
! cmd:window Project | Open the Project window: drop a .vmd onto the Scene view with its character selected
? A motion made for one MMD model is dropped onto another. Does it fit? | Yes, as far as the bones are standard: they are matched by MMD's names (左ひざ, 頭), so any model with them plays it; extra bones are listed as not found.
? Why does a long turn get a key on every frame between two of the motion's keys? | A clip's curves are Euler angles; a big turn interpolated in angles drifts off MMD's arc, so the frames between are keyed from the arc itself.
# VRM characters: VRoid and UniVRM avatars
A .vrm file is a glTF with the VRM extension: the format VRoid Studio, UniVRM and most VTuber tools write, in its 0.x or 1.0 form. Drop one onto the Scene view and it comes in as a character facing +Z: its humanoid bones named for the rig tools (leftUpperLeg becomes LeftUpLeg, head Head), so Walk Cycle, Add IK and the physics tools find them; its expressions (A, I, U, E, O, blink, joy...) as shape keys on the meshes they move, each the weighted mix of morph targets the file says; its spring bones (hair, skirts, ribbons) as Spring Bones chains with the file's stiffness, drag, gravity and radius; and its MToon materials as Toon shading with the shade colour, toony edge and an Outline of the width the file asks for. The model's title, author and license are listed in the Console: a VRM's terms (what it may be used for) belong to its author.
! cmd:window Project | Open the Project window: drop a .vrm onto the Scene view
? A VRM 0.x and a VRM 1.0 of the same avatar: why does Blendity turn one of them around? | A 0.x model faces -Z in the file and a 1.0 model +Z; both are turned to face +Z here, like every character.
? Where did the expression "blink" go? | It is a shape key on each mesh the file binds it to: the MeshRenderer's BlendShape/blink slider, animatable like any key.
# Imported animations: a model's clips
A model file often carries its animations: a Mixamo character with its walk, a Blender export with its actions, a glTF with animations. Blendity does what Unity's importer does: each becomes a clip asset in Assets/Animations/<model>/, the first one playing on the model's root through an Animation component, open in the Animation window and listed in the Sequencer. An FBX's clips are read at the file's frame rate, every bone's turn and move a frame at a time; a glTF's at the rate its keys fit (Blender's 24, 25, 30...), the turns baked a frame at a time (a quaternion curve isn't a few Euler keys) and the moves at the file's own keys. Shape-key animation comes along as BlendShape tracks. The clips are files: edit them, bake springs onto them, or play another on the character.
! cmd:window Animation | Open the Animation window: an imported model's clips play on its root
? A Mixamo FBX has one animation. Where is it after the import? | In Assets/Animations/<model>/<clip>.anim, playing on the model's root; the Animation window shows it.
? Why is a turn keyed on every frame when the file had only two keys? | The file's turn is a quaternion arc; Blendity's curves are Euler angles, which can't follow a long arc with two keys, so each frame is keyed from the arc.
# Motion capture: BVH
A .bvh file is motion capture in Biovision's text format: a hierarchy of joints with their offsets and channels, then a row of values a frame. Drop one onto the window and it comes in as a skeleton (a bone per joint, at the file's offsets, in metres: a capture in centimetres is scaled) with its motion as a clip in Assets/Animations/<file>/, playing on the skeleton's root at the file's frame rate. The rotation channels are composed in the order the file lists them, as the format means, and keyed as Euler angles that never wrap. The skeleton has no mesh: skin one to it (Rigging > Bind), bake its clip onto another character's rig with the same bone names, or copy keys across in the Animation window. Retargeting onto a rig with other names isn't automatic.
! cmd:window Animation | Open the Animation window: a capture's clip plays on its skeleton
? A CMU capture comes in 1.7 m tall although the file's numbers are around 170. Why? | A skeleton taller than 10 units is taken to be in centimetres and scaled to metres.
? The file lists Zrotation Xrotation Yrotation. In what order are they applied? | In that order, as the format means: each about its own axis, one after another.
# Collada: .dae scenes
Collada is the XML scene format Blender, Maya, SketchUp and Mixamo export: geometry with materials and textures, skins with their joints, a node hierarchy, and animations. Blendity reads it with its own XML reader: drop a .dae onto the window. The file says its up axis (Z up from Blender, Y up from most others) and its unit (centimetres from many tools), and everything is brought into Blendity's Y-up metres, so a Z-up model in centimetres stands the right way up at the right size. Materials come from their effects (the diffuse colour or texture, emission, shininess as roughness, transparency); skins bring their joints as bones with their bind matrices; the animations (matrix channels, or translate / rotate / scale channels by name) become one clip in Assets/Animations/<model>/, playing on the root.
! cmd:window Project | Open the Project window: drop a .dae onto the Scene view
? A SketchUp export is Y up in inches. What happens to it? | The file's unit (0.0254 m) and up axis are read from its asset block: it comes in at the right size, the right way up.
? Blender's exporter writes a 4x4 matrix per joint per frame. How does that become a clip? | Each node's matrix at each key is turned into position, rotation and scale keys, the rotation as Euler angles nearest the key before.
# Physics for animation: a jump from start to finish
Blendity's physics for animation follows Cascadeur's order. Weigh the body (from its skin), mark where it touches the ground (Contacts), then let physics correct what you keyed: Flight fixes the air alone, AutoPhysics the whole clip. Secondary motion (spring bones on hair, ears, tails and cloth) and an active ragdoll (hits and falls) come on top, and AutoPosing helps make the keys in the first place. Each correction is shown as a ghost with its frames coloured - green fine, blue changed, red impossible - and nothing is written until you press Apply, as one undo step. The Try-it below makes a person jump in straight lines, the way a first blocking pass often looks: find the contacts, show the flight, and see the straight lines become a parabola.
! cmd:rig humanoid; anim new Jump; anim key . Position 1 0 0; anim key . Position 1 5 0; anim key . Position 1 15 1; anim key . Position 1 25 0; anim key . Position 1 40 0; window Animation; body contacts find; body flight show | A person jumping in straight lines: its contacts found and its flight corrected (the ghost; Apply writes it)
? Why are the contacts found before the flight is corrected? | The contacts say when the body is in the air: only between them does gravity alone decide where the center of mass goes.
? What does AutoPhysics change that Flight doesn't? | The frames on the ground too: a take-off too sudden for the legs or a landing that stops too fast, using the forces the contacts can give.
# Contacts and flight
Contacts are the frames a foot or hand rests on something (Cascadeur's fulcrum points): Contacts finds them where an IK limb's tip is low and still, shows them as bars under the ruler, and keeps any you add by hand (console: body contacts add <bone> <from> <to>). Between contacts the body is in the air, and in the air physics is simple: the center of mass follows a parabola under gravity, and the spin keeps its angular momentum (tucking in turns faster, opening out slower). Flight shows the clip corrected that way as a ghost and colours the frames: green already fine, blue changed, red impossible (a jump the timing can't carry: change the poses or the timing). Apply writes it into the clip as keys on the hips (and the feet's and hands' targets), one undo step.
? A jump keyed as straight lines up and down: what does Flight change? | The rise and fall become a parabola: the body slows to the top and speeds up falling, and the top is as high as the jump's time in the air allows.
? Why does a diver turn faster when tucked? | The angular momentum stays the same in the air; tucking makes the body's inertia smaller, so it turns faster.
# AutoPhysics: the whole clip
Flight fixes only the air. AutoPhysics (Cascadeur's name for it) fixes the whole clip, the take-offs and landings too: it looks for the motion nearest yours that a body could really make, where the only things pushing the body are gravity and the contacts, and each contact can only push (never pull) and only as hard sideways as friction allows (0.8 of the push). It works on the body as a whole - its center of mass and its spin - and puts the result back through the hips, with the feet and hands held where their contacts are. A take-off too sudden for the legs, a landing that stops too fast or a lean nothing could hold come back changed (blue); frames that were already possible stay green. It needs contacts first, solves in the background (a moment for a few seconds of animation) and shows a ghost; Apply writes it as keys, one undo step. Console: body physics show|apply|off.
? A character keyed leaning far forward with both feet planted and not moving: what does AutoPhysics do? | It moves the center of mass back over the feet or lets the body start to fall: standing still needs the center above what holds it up, and the feet can't pull.
? Why does AutoPhysics move the body at all on a frame where the feet are planted? | Planted feet can only push, and only within friction: if the keyed motion needs a force they can't give (a pull, or more sideways than friction allows), the motion must change there too.
! cmd:window Animation | Open the Animation window (Contacts, then AutoPhysics, with a character selected)
# AutoPosing: pose the whole body by one end
Posing a character bone by bone is slow: drag the hand and the arm, the spine and the hips should all follow. With AutoPosing on (the Animation window's button), dragging a bone with the move gizmo poses the whole body: a full-body inverse kinematics turns every joint of the spine and limbs a little (each within its limit, and pulled towards the pose it started in so it stays natural) and may move the hips, while the feet stay where they are (the contacts at the playhead, the bones you lock, or else the feet) and the center of mass stays over them so the pose is balanced. On release the moved bones are keyed at the playhead. Console: autopose on|off, autopose drag <bone> <x> <y> <z>, autopose lock <bone>.
? You drag the head forward with AutoPosing: why do the hips move back? | The center of mass must stay over the feet: the hips go back to balance the head going forward.
? Why does a hand dragged far out of reach stop with the arm straight? | Each joint is turned only as far as it may; the arm points at the target and the rest stays balanced.
# The active ragdoll: hits and falls
An active ragdoll is the character as rigid bodies - a capsule a bone, weighed as the body is weighed - joined at its joints with human limits, whose joint motors try to follow the animation. At full strength it plays the clip; hit it (an impulse on a bone at a frame) and it reacts, then recovers; turn the motors off from a frame ("limp") and it falls under gravity and comes to rest on the ground, like a knock-out. The Animation window's Ragdoll button simulates the clip in the background and shows the result as a ghost; Apply keys it. Events live in the clip. Console: body ragdoll hit <bone> <frame> <x> <y> <z>, body ragdoll limp <frame>, body ragdoll strong <frame>, body ragdoll show [strength]. It needs Blender's libraries (Jolt Physics).
? Why does a ragdoll at full strength still differ a little from the clip? | The motors are springs with limits: they pull the joints towards the clip's pose but need a moment to get there, and the body's weight and contacts push back.
? You hit the head at frame 20 with the motors on: what happens after the hit? | The head and the bones joined to it are knocked away first, then the motors pull the body back towards the clip.
# Spring bones: hair, ears, tails and cloth
Hair, ears, tails and skirts shouldn't be keyed by hand: they follow the body with a lag, swing past when it stops and settle (secondary motion). Rigging > Spring Bones > Find Spring Chains lists every chain hanging off the body that isn't part of it, found by the bones' names (hair, ponytail, ear, tail, skirt, cape... Mixamo's, VRM's J_Sec_, Japanese rigs' kami, mimi, shippo) and by shape: named ones come ticked and labelled (hair, ear, tail or cloth, each with its own stiffness, damping and gravity), unnamed extras unticked. Untick any, change a label, or set one to None (not a spring chain: remembered on its first bone, so Find won't offer it again until you Unignore it), then Apply. Add Spring Bones on a selected bone does it recursively: a hair root's strands become one chain each. In Play they move live; Bake into Clip keys them frame by frame (and turns the live simulation off). Console: spring find, spring add <bone>, spring remove <bone>, spring bake.
? Why is a hair root with five strands five chains, not one? | Each strand swings on its own; the root they share stays where the head's animation puts it.
? You set a chain to None and run Find again: why isn't it listed? | None is remembered on the chain's first bone ("Not a Spring Chain"); Show Ignored lists it, and Unignore brings it back.
! window:Rigging | Open the Rigging window (Spring Bones, with a character's bone selected)
# Strands: a curl that holds, wind, the body's real width
A chain's Bend keeps each bone's animated angle to the one before it (task 0153): a curl keeps its shape instead of straightening under its own weight, a strand bends where it was bent, whichever way the chain swings. Root To Tip fades the pull toward the animation along the chain's length, so long hair's tips swing free while the root follows the head. Add a Wind component (Add Component > Scripts) anywhere: it blows along the object's forward (or away from it, Spherical, at full strength whatever the distance) at its Strength in metres a second, with Turbulence stirring and Gusts breathing; a chain's own Wind says how much it feels it, and a bake under a wind comes out the same twice. The body's capsules are as wide as the skin each bone moves, so a skirt rests on a thigh's real width; Separate keeps a chain apart from the others by its radius - never further apart than the animation laid them - so a ponytail's strands don't pass through each other.
? A ponytail's curl straightens when Play starts. Which field holds it? | Bend: each bone keeps its animated angle to the one before it; 0 lets the curl fall out, 1 holds the animated shape.
! cmd:create Cube; select Cube; component Wind | Add a Wind to the Cube (its forward is the wind's direction)
# Winds you can see
A Wind shows an icon in the Scene view like a light (task 0158), on the icons layer, clicked to select it. Selected, it draws its arrow: along its forward, half a metre plus a quarter metre for every metre a second of Strength; with Turbulence, wavy streamlines beside it; Spherical shows three rings instead, since it blows away from its object every way. A Rotator and an Oscillator show icons too, so a Play-mode mover isn't an invisible empty.
? The Wind's arrow points the wrong way. What do you turn? | The object: the wind blows along its forward (+Z); turn it with the rotate gizmo and the arrow follows.
! cmd:create Empty; select Empty; component Wind; set Wind.Strength 6 | A strong wind on an Empty: a long arrow when it is selected
# Soft tissue: breasts, thighs and bellies that move
A breast, a thigh or a belly isn't a tail: it trails the body's acceleration, not its position (task 0153). Spring Bones' fifth label, Soft, is for a single bone: Lag says how much of the body's acceleration the tissue trails (a landing throws it down, a step up, a slow walk hardly moves it), Stiffness pulls it back and Damping settles it, Limit keeps it within a cone of degrees about where the animation points the bone, and Volume narrows the bone across as it stretches along its length, so the mass keeps its volume. Find Spring Chains labels bones named breast, bust, belly, butt, a thigh or hip with jiggle or soft, MMD's 胸 and 尻, VRM's J_Sec_L_Bust as Soft. A mesh whose rig has no such bones takes a Jiggle modifier instead: a vertex group's vertices move together on the same spring.
? A breast bone bounces on a slow walk as much as on a run. Which Soft field is wrong? | None - check the label: a Hair or Tail label pulls by position; Soft trails the acceleration, which a slow walk hardly has.
! cmd:create Cube; select Cube; component JiggleModifier | A Jiggle on the Cube (give it a vertex group to move part of it)
? The bone stretches but the tissue looks thinner each bounce. Which field? | Volume: at 1 the bone narrows across exactly as much as it stretches along, so the product of its scales stays 1.
# Rig hair from its mesh
Hair comes as cards (loose parts) or a shell with no bones at all. Rigging > Rig Hair from Mesh (hair rig [locks] [bones] on the console) makes a chain of bones from the scalp to the tip of every card - or of every sector of a shell cut into locks - under a new hair armature, skins the mesh to them and puts Spring Bones (hair) on each, in one undo step; a mesh already skinned to a character hangs its rig under the head bone, so it follows the head.
! cmd:create Cube; select Cube; hair rig 4 2 | The Cube cut into four locks of two bones, each a spring chain
? Twelve hair cards give how many chains? | Twelve: every loose part is a lock with its own chain from the scalp.
# Sequencing clips
Moving between clips doesn't need a state machine here: Window > Sequencer is the scene's timeline, as in Premiere Pro. Press Create (the scene gets Assets/Animations/<scene> Sequence.seq), then drag clips (.anim) from the Project window onto its tracks. Each bar is two things: the clip, and the object it plays on - the selected character when you drop it, or any object dragged onto its Object field below the tracks - so one timeline moves a whole cast. Each clip is a bar: drag it to move it (onto another track too), drag its ends to trim it, and set its Speed, In and Out below the tracks. Where two bars on one track overlap and play on the same object, the clips crossfade over the overlap (Premiere's cross dissolve); a bar on a higher track wins for the numbers it animates, as video layers do. Preview and the playhead work as in the Animation window. Triggers make it interactive: in Play, pressing a trigger's key (a letter, a digit, Space or F1-F12) jumps to its clip, blending from the pose of that moment over the trigger's seconds, and the timeline plays on from there. From a script or the console: seq jump <clip>.
? Two clips, Walk and Run, overlap by 20 frames on one track. What happens halfway through the overlap? | Each animated number is half Walk's and half Run's: the crossfade's weights go from Walk to Run linearly across the overlap.
? Why does a trigger blend from the pose of the moment rather than from the clip it jumps from? | The jump can come at any time, even in the middle of another blend: starting from wherever the character is now never makes it snap.
! cmd:window Sequencer | Open the Sequencer window
# A character's root
Unity imports a rigged character as one object holding the mesh and the skeleton, so you move, animate or delete the character as a whole. Blendity builds rigs the same way: GameObject > Armature with a mesh selected, Rigging > Add Armature (Skin) and the auto-rig's Build Rig put the mesh and its Armature inside a new object named after it ("Robot Rig" holding "Robot" and "Armature > Hips ..."), in the mesh's old place in the Hierarchy. Move the root and the whole character moves without deforming; give the root the Animation or Sequencer component, and the Animation window picks it when you select the mesh or a bone. A second armature on the same mesh joins the same root; a skeleton that several meshes share stays where it is.
? Why put the mesh and the skeleton under one root instead of side by side? | Moving, animating or prefab-ing the character is then one object, and a clip's paths start at the root (Armature/Hips/Spine), as in Unity.
! cmd:create Cylinder; create Armature | A cylinder and an armature: see "Cylinder Rig" in the Hierarchy
# Bone heads, tails and Extrude
A bone runs from its head (the object's origin) to its tail. With a bone selected, its two ends show handles in the Scene view: drag the tail (blue) to aim and stretch the bone, Shift+drag the head (orange: a plain press there is the gizmo's) to move its start while the tail stays; the bones below keep their places, and the result becomes the rest pose (Blender's armature Edit Mode, without the mode). Extrude (Ctrl+E with Unity's keys, E with Blender's) grows a new bone from the tip whose tail follows the mouse; click to keep it, Esc to take it back - the quickest way to draw a spine or a tail. Hold Ctrl while dragging to snap to the grid. Console: bone tail <bone> x y z, bone head <bone> x y z, bone extrude <bone>.
? You drag a thigh's tail to make it longer. Why doesn't the shin below it stretch too? | The children keep their world places; only the dragged bone's length and direction change.
! cmd:create Armature | An armature: select its bone, drag the blue tail handle, then press Ctrl+E
# Rig controls: IK and FK
Animators grab controls, not bones. Add IK to Bone makes the target a box and the pole a diamond, coloured by side as in Maya and Blender: left blue, right red, the middle yellow; a dashed line runs from the pole to the knee or elbow it aims, and the bones IK turns are tinted. Rigging > FK Controls > Add FK Controls puts a circle on each selected bone: turn the circle and the bone turns with it (keyframe the circles; a bone with a circle is posed through it, and Reset Pose puts both back). Bones that IK turns don't get circles - remove the IK first. Any object can be a control: add the Rig Control component and pick its Shape (Circle, Square, Box, Diamond, Sphere, Arrows, Cross, Root), Size and Side. Remove IK takes away the constraint with its target and pole controls (an object of your own used as a target stays); Remove FK Controls takes the circles away and leaves the bones posed as they were. Console: ik remove <bone>, fk add|remove [bone...].
? Why are a left leg's controls blue and the right's red? | The convention of Maya and Blender rigs: you can tell the sides apart at a glance, even from behind.
? What happens to the target and pole when you remove an IK? | They go with it, unless another IK uses them.
! cmd:create Armature; bone add Bone 1; ik add Bone.001 | Two bones with IK: a box target and a diamond pole
# IK handles: size and turn
A handle is drawn at its Size in metres round its position, whatever the rig's scale (task 0156): a centimetre import's handles are no longer a hundred times too big. Preferences > Rig Control Size scales every handle at once, and Rigging > Fit Control Sizes sizes them from their bones again (a fifth of the shin for a foot target). Turn a handle and the foot or hand below the chain turns with it: Rotate End on the IK (on for new IK, whose handle starts world-aligned; ik orient <bone> turns it on for an old one, keeping the bone as it is).
? A character imported from FBX shows handles the size of the room. What fixes them? | They're drawn in metres now; if the file still holds old sizes, Rigging > Fit Control Sizes (ik fitsizes).
? You turn a foot's handle but the foot stays flat on the floor. Why? | The IK's Rotate End is off: turn it on in the Inspector, or ik orient <shin>.
! cmd:rig humanoid; select RightFootIK; tool rotate | A humanoid's foot handle under the rotate gizmo: turn it and the foot follows
# Auto-rig colours and views
The auto-rig's markers are coloured by the part of the body they are on: left blue, right red, the middle (chin, spine, hips) yellow and a tail purple; a mirrored twin is lighter, a marker not yet placed is dark, and the next one is ringed. Front, Left, Right, Back and Top under the diagram switch the orthographic view while you place them. A new marker goes to the middle of the body part the click hits; a marker you drag in a side or top view slides on the plane facing you, keeping its depth, so dragging a knee in the side view fixes how far forward it sits. Console: autorig view front|left|right|back|top.
? Why place some markers in the side view? | The front view says where a joint is left to right and up and down; the side view fixes its depth (a knee in front of the hip line, an elbow behind).
# The Rigging window
Window > Rigging gathers everything for characters, beside the Inspector: Skeleton (Add Armature, Add Bone at the selected bone's tip, the Humanoid and Quadruped rigs, Reset Pose, Apply as Rest), Skin (Add Armature (Skin) to a mesh, Bind: Automatic Weights), Vertex Groups, Weight Paint and Inverse Kinematics (Add IK to Bone makes its target and pole; Auto IK finds the limbs by name). The buttons work on the selection: to skin a mesh, Ctrl+click the mesh and one bone of the skeleton in the Hierarchy, then press Add Armature (Skin). While Weight Paint is on, selecting a bone in the Hierarchy paints the group named after it, as Blender does in Weight Paint mode.
! window:Rigging | Open the Rigging window
! cmd:create Cylinder; create Armature; bone add Bone 1; window Rigging | A cylinder bound to a two-bone armature: pose Bone.001 or press Add IK to Bone
? Why does Add Armature (Skin) want a bone selected as well as the mesh? | A scene can hold several skeletons: the selected bone (or armature) says which one the mesh follows.
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
# Focusing, and watching the shot
The Camera's Pick Focus Point button is an eyedropper. Click a surface in the Scene view and the focus distance becomes how far that point is in front of the camera. Lock on the Camera Preview keeps that camera's preview up while you select other things and edit meshes, so you can model while you watch the shot.
! cmd:select Main Camera; pickfocus | Pick the Main Camera's focus point (then click a surface)
! cmd:select Main Camera; set Camera.PhysicalCamera 1; set Camera.DepthOfField 1; set Camera.FStop 1.4; camerapreview rendered | Make the Main Camera physical with a shallow depth of field and preview it
# Composition guides
Show Guides (the Camera inspector's Composition Guides drop-down, or the Game view's Guides menu) draws lines over what the camera sees, never into the render. The rule of thirds splits the frame into a three by three grid: a horizon on one of the horizontal lines and a subject where two lines cross usually reads better than one dead centre. The golden ratio does the same at 0.382 and 0.618. The golden and harmonious triangles lay a diagonal across the frame with lines from the other two corners (square to the diagonal for the golden ones, to the golden section of the far edge for the harmonious ones), for compositions built on diagonals. Title Safe and Action Safe mark the margins a television might crop (Blender's defaults: Title Safe 10 % across and 5 % down, Action Safe 3.5 % both ways, of half the frame on each side).
! cmd:select Main Camera; guides thirds on; window Game | Show the Main Camera's rule-of-thirds guides in the Game view
# Depth precision
Perspective projection stores depth non-linearly (FoCG 8.4). Most precision sits close to the near plane, so a tiny near clip value causes z-fighting in the distance. Keep Near as large as you can.
> [FoCG] 8.3 Perspective Projection, 8.4 Some Properties of the Perspective Transform, 8.5 Field-of-View
> [GEA2] 11.2 Lights, Camera, Action!
? Why does setting Near to 0.0001 make distant surfaces flicker? | Depth precision is spread non-linearly between the near and far planes. A very small near value leaves almost no precision far away, so surfaces at similar depths fight (z-fighting).
! window:Game | Show the Game view
)"},
    {"Overlays: See the Mesh, Hide the Rig", "The Scene view's overlay layers", R"(
A rigged character in the Scene view is its mesh under a thicket of bones, control shapes, icons and outlines.
The Overlays menu in the Scene view's bar (Blender: the Overlays popover; Unity: the Gizmos menu) turns each layer
off on its own: Bones, Rig Controls (the IK and FK handles), Light & Camera Icons, Curve & Lattice Points, Object
Origins, Footprints & Contacts, Selection Outline, Grid, Gizmos and Statistics. Hide the bones and keep the rig
controls, and you see the mesh while you still pose it by its handles; All Overlays shows or hides the lot. A hidden
layer can't be clicked either: with the bones off, a click lands on the mesh behind them, and the bones' head and
tail handles stay away too. The Hierarchy still selects anything, and the gizmo still moves it. The choice is
remembered between sessions.
! cmd:overlay bones off; overlay icons off | Hide the bones and the light and camera icons
! cmd:overlay all on | Show every overlay again
# From the console
overlay list prints each layer's state; overlay <layer> on|off sets one and overlay all on|off the lot - the same
names as the menu: bones, controls, icons, points, origins, footprints, outline, grid, gizmos, stats.
? You want to see a character's skin while dragging its IK targets. Which layers do you turn off? | Bones (and perhaps Selection Outline); Rig Controls stay on, so the IK handles are still drawn and can be dragged.
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
# Light types and colour temperature
| Type | Blendity | Blender | Unity
| Sun | Directional | Sun | Directional
| Bulb | Point (Range) | Point | Point
| Cone | Spot (Spot Angle, Inner Spot Angle) | Spot (Size, Blend) | Spot
| Panel | Area (Rectangle / Disc, Width, Height) | Area | Area (HDRP / baked)
A spot light lights a cone along its forward axis and fades between the inner and outer angles. An area light is a glowing panel: it lights only the side its arrow points to, from every point of its surface, with the inverse square of the distance from each - the same light a quad with an emissive material of strength pi x Intensity gives, so Width and Height make it give more light, and softer shadows. Its Range is where it fades out, not how it falls. In path-traced renders it is sampled across its whole surface, so its shadows are soft: the larger the panel, the softer they get; the views, lightmaps and probes compute the same light exactly. Real lights have a colour temperature. Tick Use Color Temperature and the colour is tinted like a black body at that many kelvin. The tint runs warm to cool: 1900 K is candlelight, 2700 K a bulb, 5500 K noon sun and 6500 K white, while 10000 K and above is the blue of shade.
@ blender/intern/cycles/kernel/light/spot.h, blender/intern/cycles/kernel/light/area.h
@ blender/intern/cycles/kernel/svm/blackbody.h
! cmd:create Spot Light; set Light.UseColorTemperature 1; set Light.Temperature 2700 | Add a warm spot light above the scene
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
# Many at once
The Project, Materials and Textures windows select like the Hierarchy and Unity's Project window: a click selects one, Ctrl+click adds or removes one, Shift+click selects everything from the last click to this one, and Ctrl+A (with the mouse over the list) selects everything shown; a search narrows what "everything" means. Delete (or Backspace) asks once for all of them ("Delete 7 materials?") and sends their files to the Recycle Bin / Trash, where they can be restored. Deleted from the Materials window, a material leaves its slots to the default material (one undo step brings them back); deleted as a file from the Project window, objects keep it as a scene material. Console: `matdelete Bricks, Wood Floor` (names separated by commas).
! cmd:newmat Practice1; newmat Practice2; newmat Practice3; window Materials | Make three materials to practise on (Ctrl+click two, then press Delete over the window)
? Where do deleted assets go? | To the Recycle Bin (Windows) or the Trash, so a mistake can be put back. From the Materials window the slots fall back to the default material (Undo restores them); from the Project window objects keep the material as a scene material.
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
# Quality tiers: a small machine
Blendity's views are drawn by the CPU, so their cost follows the cores and the pixels. Preferences > Performance > Quality tier picks a set of limits for the editor's views (never for F12 renders): Low draws the Scene and Game views' 3D at 67 % and scales it up (lines, gizmos and text stay sharp, and clicks still pick exactly), with a 1024 shadow map, lighter Realtime GI and 60 fps; Medium is 85 %; High has no limits. A first run on a machine with 8 threads or fewer, or about 8 GB, starts on Low. CPU Threads sets how many threads Blendity works on, which also lets a big machine feel like a small one.
| Tier | Views | Shadow map | Realtime GI | Frame cap
| Low | 67 % | 1024 | 4 rays | 60
| Medium | 85 % | 2048 | 8 rays | 120
| High | 100 % | the scene's | the scene's | 144
Two more tricks keep a small machine smooth. Cached shadows: while one object is dragged, the objects that stay still keep a shadow map of their own and only the moving one is drawn over a copy of it. The sky last: the background is drawn after the surfaces, only where none landed.
! cmd:quality low | Switch to the Low tier (watch the views soften slightly)
! cmd:quality high | Back to High
! cmd:threads 8; quality low | Feel the minimum machine: 8 threads, Low
! cmd:threads 0 | Every thread again
? Why do the overlays stay sharp at 67 %? | Only the 3D picture is drawn smaller; the grid, gizmos, outlines and text are drawn afterwards at the view's full size, on top of the scaled-up picture.
# A laptop with a touch screen
The minimum machine is now a thin laptop: an Intel i7 with 4 cores and 8 threads, 8 GB of memory shared with its Iris Xe graphics, and a 13.3" 1080p touch screen at 150 % (the HP Envy 2-in-1). On a first run like that Blendity starts on Low and says so, with High one click away. Its 4 cores' 8 threads do about the work of 5 full cores, and a laptop holds a lower clock under load than a desktop, so editing frames run at about 35-40 fps there and orbiting at 60. The Low tier's views read shadows with 4 weighted samples instead of 9: a little sharper-edged, a little faster. Iris Xe is given a quarter of the memory for path tracing, so the editor keeps room. On the screen, two fingers over the Scene view navigate in every editor feel: pinch to zoom, drag to pan, twist to turn round the pivot. One finger is the mouse. A pen's pressure sets how much the weight-paint brush adds, as in Blender's tablet support.
? Why does one finger act as the mouse rather than orbiting? | One finger has to pick, drag gizmos and press buttons; the view takes two fingers, so nothing you touch moves the camera by accident.
! cmd:quality low; threads 8 | Feel the laptop: Low and 8 threads (Envy-class machines also run slower per thread)
# Reading a frame
The Profiler's frame breakdown says where the last frame went. Between frames is what the screen sees: the time from one frame's start to the next, cap and waiting included; at a 60 fps cap it should sit near 16.7 ms. Below the views' times come the parts an edit pays for: draw items (gathering what the views draw, building any render mesh that changed), the scene hashes, an Edit Mode drag's writes to the mesh, the overlapping-vertices check, the lighting key and snapping. The mesh rows count what the mesh caches did that frame: copies made for an edit, render meshes built from scratch or moved in place, tangents and edge tables. A big mesh dragged in Edit Mode shows its cost there. blendity_stress --only editor_render drags a 50k-face mesh the same way, and --only present opens a window to time putting frames on screen.
! window:Profiler | Open the Profiler
! cmd:create Sphere; edit vertex | Edit a sphere, then drag a vertex and watch the mesh rows
# Overlaps checked without a stall
In Edit Mode, yellow rings mark vertices lying on top of each other and magenta lines edges lying along each other (Tools > Check: Overlapping Vertices & Edges selects or merges them). Blender has no such check running all the time; Blendity's keeps it from stalling a big mesh. First, it doesn't redo work: an answer is kept by what it checked (the mesh's topology and every position), so an undo, a redo, re-entering Edit Mode or a selection that copied the mesh takes it at once. Then the work is cheap: the check runs on every thread and finds exactly the pairs the old one found, about 15 times faster. After a drag only the vertices that moved and the edges at them are checked again. Past 200,000 vertices the check runs beside the editor: the panel says Checking... and no rings show until the answer for the mesh as it is now arrives.
! cmd:create Sphere; edit vertex | Edit a sphere: the check's time is the Profiler's overlap row
? You undo an extrude on a big mesh. Why are the rings back at once? | The geometry is the same as before the extrude, and the answer for it was kept: nothing is checked again.
? Why does a drag's release cost almost nothing to check? | Only the moved vertices and the edges at them are binned and tested again; every other pair is kept from the last check.
? Why can Between frames be longer than the frame itself? | The loop waits for the next frame's turn under the cap. Blendity's frame pacer keeps that wait from running long (Windows rounds plain waits up to 15.6 ms), so at a 60 fps cap the frames start 16.7 ms apart unless a frame itself takes longer.
)"},
    {"References", "The papers behind Blendity's features", R"(
Some of Blendity's features come straight from research papers. Each one is listed here with the feature it became and where to find it, so you can read the original next to the code. The code cites the same papers in its comments.
# Modeling
- Taubin smoothing: Inspector > MeshFilter > From Papers, or Mesh > From Papers. Smooths without shrinking the mesh.
> Taubin, G. (1995) A signal processing approach to fair surface design. SIGGRAPH '95.
! cmd:create Sphere; meshop research:surface_noise; meshop research:taubin_smoothing | Add noise to a sphere, then smooth it with Taubin's filter
- Subdivision (Subdivide, the Subdivision Surface modifier).
> Catmull, E. & Clark, J. (1978) Recursively generated B-spline surfaces on arbitrary topological meshes. Computer-Aided Design 10(6).
- UV unwrapping (U > Unwrap): least squares conformal maps, as Blender's unwrapper.
> Lévy, B., Petitjean, S., Ray, N. & Maillot, J. (2002) Least squares conformal maps for automatic texture atlas generation. SIGGRAPH '02.
- Tangent space for normal maps (MikkTSpace, as Blender and most engines).
> Mikkelsen, M. (2008) Simulation of wrinkled surfaces revisited. Master's thesis, University of Copenhagen.
- The Utah Teapot primitive.
> Newell, M. (1975) The utilization of procedure models in digital image synthesis. PhD thesis, University of Utah.
# Rendering
- Realtime GI (Lighting window): voxel columns, the mip-map ray test, one bounce of the sun.
> Thiedemann, S., Henrich, N., Grosch, T. & Müller, S. (2011) Voxel-based global illumination. I3D '11.
> Amanatides, J. & Woo, A. (1987) A fast voxel traversal algorithm for ray tracing. Eurographics '87.
- Rough reflection and glass (the GGX microfacet model) and its sampling in the path tracer.
> Cook, R. & Torrance, K. (1982) A reflectance model for computer graphics. ACM Transactions on Graphics 1(1).
> Walter, B., Marschner, S., Li, H. & Torrance, K. (2007) Microfacet models for refraction through rough surfaces. EGSR '07.
> Heitz, E. (2018) Sampling the GGX distribution of visible normals. Journal of Computer Graphics Techniques 7(4).
> Schlick, C. (1994) An inexpensive BRDF model for physically-based rendering. Computer Graphics Forum 13(3).
- Large area lights in the views (their specular highlight) and image-based lighting's split sum.
> Karis, B. (2013) Real shading in Unreal Engine 4. SIGGRAPH course: Physically Based Shading in Theory and Practice.
- Area lights in Generate Lighting (an exact unshadowed term, shadow rays for the visible share).
> Heitz, E., Hill, S. & McGuire, M. (2018) Combining analytic direct illumination and stochastic shadows. I3D '18.
- Disk-shaped area lights (an even mapping of the square onto the disk) and stratified light samples.
> Shirley, P. & Chiu, K. (1997) A low distortion map between disk and square. Journal of Graphics Tools 2(3).
> Cranley, R. & Patterson, T. (1976) Randomization of number theoretic methods for multiple integration. SIAM Journal on Numerical Analysis 13(6).
- The path tracer's BVH (surface area heuristic) and ray-triangle test.
> Wald, I. (2007) On fast construction of SAH-based bounding volume hierarchies. IEEE Symposium on Interactive Ray Tracing.
> Möller, T. & Trumbore, B. (1997) Fast, minimum storage ray-triangle intersection. Journal of Graphics Tools 2(1).
> Duff, T. et al. (2017) Building an orthonormal basis, revisited. Journal of Computer Graphics Techniques 6(1).
- Path guiding (Render Settings > Path Guiding, through Open PGL's parallax-aware mixtures).
> Ruppert, L., Herholz, S. & Lensch, H. (2020) Robust fitting of parallax-aware mixtures for path guiding. ACM Transactions on Graphics 39(4), SIGGRAPH '20.
- The A-Trous denoiser (when OpenImageDenoise isn't there).
> Dammertz, H., Sewtz, D., Hanika, J. & Lensch, H. (2010) Edge-avoiding À-Trous wavelet transform for fast global illumination filtering. HPG '10.
- The world: the physical sky, its irradiance for diffuse lighting and probes.
> Hosek, L. & Wilkie, A. (2012) An analytic model for full spectral sky-dome radiance. SIGGRAPH '12.
> Ramamoorthi, R. & Hanrahan, P. (2001) An efficient representation for irradiance environment maps. SIGGRAPH '01.
- Light colour temperature (Use Color Temperature on a light).
> Kang, B. et al. (2002) Design of advanced color temperature control system for HDTV applications. Journal of the Korean Physical Society 41(6).
# Textures
- Noise in materials and procedural textures (gradient noise and its fractal sums).
> Perlin, K. (2002) Improving noise. SIGGRAPH '02.
> [FoCG] 11.5 Procedural 3D Textures
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
# Materials as assets
In Unity a material is a file in the Project window that many objects share; change it once and they all change. Blendity works the same way: Assets > Create Material (or a slot's Save as Material Asset) writes a .mat file in Assets/Materials. Drag it onto an object, a face, a Hierarchy row or a slot; drag an image onto a texture field. Blender keeps materials as datablocks that objects link to, which is the same idea inside one .blend file.
@ blender/source/blender/nodes/shader/nodes/node_shader_bsdf_principled.cc
@ blender/intern/cycles/kernel/svm/checker.h, noise.h
# Getting textures in
Drag images onto the window: they're copied to Assets/Textures and, if an object is selected, assigned to its material. Drag an .obj or .fbx and its materials and textures come with it (OBJ uses the .mtl file; FBX is read with ufbx, a library Blender itself bundles).
# How big is a tile?
A procedural texture knows its real size: Physical Size is the metres one tile covers (bricks 2 m, a wood floor 2 m). A material using it tiles by its inverse, so 2 m is Tiling 0.5: one tile every two metres whatever the object's size, as SketchUp's textures keep their size. Change Physical Size and every material in the scene using that texture follows, in one undo step. A material whose Tiling you set by hand keeps it: Blendity sees that it no longer matches the old size.
? A Bricks texture of Physical Size 2 m is changed to 4 m. What Tiling do the materials using it get? | 0.25: one tile every 4 metres.
! cmd:newtex BigBricks Bricks; create Cube; settex BigBricks PhysicalSize 4 | Make a brick texture and set its tile to 4 m (put it on the Cube from the Textures window to see it)
# Anime backgrounds: procedural textures
A background painter works in two or three flat tones with a line: the leaves are a lit tone, a shade tone and a dark
rim, the wall one tone with a patch of another and a crack or two, the clouds a white top over a flat blue-grey
underside. The Textures window's generators Leaves, Grass, Plaster, Window Grid, Roof Tiles, Sky Gradient, Clouds,
Rocks and Water draw that way (task 0150), each with its tones as colours, and the presets Anime Leaves, Anime
Grass, Anime Wall, Anime Windows, Anime Roof, Anime Sky, Anime Clouds, Anime Rock and Anime Water set one up with a
sensible tile size. The cards - leaves, grass, clouds - cover only their shapes: with the texture's Alpha From Cover
on, the albedo's alpha is that cover (stacked cards add up; Warp and Tile move it with the colour), and a Cutout
material shows holes between the leaves (a foliage card, a bush of three crossed quads, a cloud over the sky).
`animemat <preset>` on the console does it in one step: a new texture asset and a flat-toned Toon material (a Cutout
for a card) in the selected objects' first material slot; undo takes the material back and leaves the texture file,
as newtex's stays.
! cmd:create Quad; select Quad; animemat Anime Leaves | A leaf card: a Quad with the Anime Leaves texture cut out
! cmd:create Cube; select Cube; animemat Anime Windows | A building block with the Anime Windows texture
! cmd:newtex Sky Anime Sky | An anime sky texture (put it on a backdrop plane from the Textures window)
? Why do the anime generators avoid gradients? | A toon material steps the light into few tones; a texture with few tones keeps the picture flat and drawn, as the painter's would be.
# Terrain: grass on top, dirt on the slopes, snow up high
An axis material (Axis Mode: World) takes a Top, Side and Bottom material by how steep the surface is, projected
without UVs, so a cliff's top is grassy and its face dirt or rock. Axis High adds a material above a height (High
Start, faded over High Blend): snow on the peaks. Axis Noise makes those borders wander instead of following a
clean line, the way real ground does; Noise Scale is how far apart their bumps are.
# The anime look
Anime and cel shading draw light in a few flat tones with hard edges instead of a smooth gradient, a coloured
shadow side, a hard highlight and black lines round the shape. Blender gets it with Shader to RGB and a Color
Ramp (Goo Engine builds it in); Unity with a toon shader. In Blendity set a material's Shading Model to Toon (or
pick the Toon preset): Toon Bands sets how many lit tones, Threshold where light turns to shadow, Shadow Color the
dark side's tint, and a rim light can outline the silhouette from inside. Add an Outline component for the lines:
a slightly larger copy of the mesh, turned inside out, so only its rim shows round the object (the inverted
hull). Both show in the views, F12 renders on the CPU and GPU, and exports. The material's Toon fields sit in a
Toon drop-down of the Inspector (and the Axis Material ones in theirs), opened by its header. Select several
materials in the Materials window (Ctrl-click, Shift for a range) and a change reaches every one of them that has
the field: a Toon Threshold lands on the toon ones and leaves a PBR one alone, Shading Model converts them all; a
dash marks a field whose values differ, and +=0.1 moves each from its own value. The same with several objects
selected in the Scene (their materials in the same slot) or several .mat files in the Project window. Console:
matset ToonThreshold 0.4 on the selection. For the printed-page
look, a camera's Add Filter > Looks > Manga is one effect: greys (with flat Tones if you like), halftone dots, ink
lines and paper; Anime is one too: ink lines, a little saturation and a soft bloom. Each has its own few fields,
and the parts (Halftone, Line Art, Paper...) are still there as effects for stacks of your own. Line Art draws
creases where the surface turns more than an angle, so a cube's edges get lines and a smooth sphere only its
outline.
# Toon styles
Toon Style (the top of a material's Toon group) is a starting point: Classic is the two-tone with a soft rim; Guilty
Gear is Arc System Works' look - a hard two-tone, the key light alone banding the tones, no rim, the highlight only
where its map is painted; Soft Anime has three tones with Light Wrap (the light reaching round the form) and a rim on
the lit side; Painterly four soft tones under the sky's gradient; Manga a hard grey two-tone. Every field stays yours
afterwards. The painted controls come from the same studio practice: a Shadow Bias Map (white keeps a point lit,
black in shadow, grey neutral - under the chin, inside the hair), a Toon Specular Map (where the spot may show), a
Toon Rim Map, a Shade Map (the shadow side's own picture, as VRM's MToon paints it), and a Toon Ramp (the tones as an
image, made by the Textures window's Toon Ramp generator; MMD's toon images land here). Toon Hair Highlight turns the
spot into the band across the hair that anime draws. Toon Key Light Only keeps several lights from banding like a
disco ball: the brightest sun bands, the others fill smoothly. The GPU viewport draws the plain settings; a material
with painted maps or a hair highlight draws on the CPU.
! cmd:select Sphere; material Toon; toonstyle Guilty Gear | Give the Sphere the Guilty Gear toon style
! cmd:select Sphere; material Toon; toonstyle Soft Anime; matset ToonWrap 0.4 | The Soft Anime style with more light wrap
? Several lights on a toon character band its tones into a mess. Which field fixes it? | Toon Key Light Only: the brightest sun bands the tones, the other lights add smooth fill.
! cmd:select Sphere; material Toon; component Outline | Make the Sphere toon-shaded with an outline
! cmd:select Sphere; material Toon; matset ToonThreshold 0.4 | Set the toon threshold on the Sphere's materials from the console
# The 2D look: normals, the face shadow, inner lines, hold frames
A drawn character has no bumps: its face is one tone with a shadow that sweeps across as the light turns. Blendity gets
there four ways (task 0149). Custom normals: a mesh can carry its own shading normals (an FBX or glTF whose normals an
artist edited keeps them; the Smooth Normals modifier averages them over the surface so a nose stops casting its own
shading; the Normal Transfer modifier reads them off a proxy - a sphere round the head, Blender's Data Transfer
trick - which Rigging > Anime Face Normals adds, fitted to the Head group). The face shadow map: a Toon material's
Face Shadow Map is read by the key light's angle round the head instead of N.L (HoYo's SDF faces); the Textures
window's Face Shadow generator makes the plain sweep to paint over. Inner lines and hatching: a Line Map multiplies
contour lines into the colour (or only into the shadow tones), a Hatch Map tiles strokes into the shadow. The Outline
component's Thickness Group paints the line's width, Distance Fade thins it far away, Min and Max Pixels keep it
constant on screen. And Hold: an Animation or Sequencer on 2s or 3s changes pose every second or third frame while the
camera moves every frame, as hand-drawn animation is shot; renders hold the same frames.
! cmd:select Sphere; component SmoothNormalsModifier | Smooth the Sphere's shading normals
! cmd:select Sphere; facenormals | Add a head proxy and a Normal Transfer to the Sphere
! cmd:select Sphere; material Toon; component Outline; set Outline.DistanceFade 0.5 | A toon outline that thins with distance
? Why does a drawn face have one tone while a scanned one has many? | Its shading follows a smooth shape (a ball), not its bumps: smoothed or transferred normals give the model the same.
? What does Hold 2s do to the camera? | Nothing: only the held object's clip changes pose every other frame; the camera and the rest move every frame.
# Baking textures for other programs
Procedural textures, axis materials and Box mapping are computed by Blendity's shaders; another program only understands images on UVs. Object > Bake Textures lays the object out on new UVs (Smart UV Project and Pack Islands, as the lightmapper does) and evaluates its materials at every texel into four images: albedo, roughness, metallic and a normal map. Blender does the same with Cycles' Bake panel, Unity users with Substance's baker. The export option Bake Textures does it on copies for OBJ, FBX and glTF, leaving the scene alone.
? Why can't a Mirror modifier be baked in place? | The images go on the base mesh's UVs, and the mirrored half shares them: it would repeat the base's images. Apply the modifier first, or export with Bake Textures, which bakes what it exports.
? Why is a normal map stored as Non-Color? | Its RGB values are vector components, not colours. Converting them from sRGB would bend every normal.
! cmd:select Sphere; texture colorgrid | Put a Color Grid texture on the Sphere
)"},
    {"Render Engines", "Rasterized (EEVEE-like) vs Path Traced (Cycles-like)", R"(
Blender has two main engines: EEVEE rasterizes triangles in real time; Cycles traces light paths and converges to a physically based image. Unity's equivalents are the URP/HDRP rasterizer and HDRP's path tracer. Blendity has both, running on the CPU.
| Scene view mode | Blendity | Blender | Unity
| Wireframe | edges only | Wireframe shading | Wireframe draw mode
| Solid | Gouraud, no materials | Solid shading | Shaded (unlit-ish)
| Shaded | deferred PBR + shadows + environment | Material Preview / EEVEE | Shaded (URP)
| (path tracing) | F12, the Render window, a Rendered Camera Preview | Rendered (Cycles) | HDRP Path Tracing
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
! cmd:camerapreview rendered | Path trace the selected camera's preview
! cmd:render; window Render | Render the Main Camera (F12)
# Rendering an animation to video
Render Animation (File menu, the Render window, or Ctrl+F12, Blender's key) renders every frame of the scene's sequence - or, without one, the selected object's clip - through the same render as F12, one frame after another: Renders/<scene>_animation_<time>/0001.png, 0002.png... Numbered images are what film studios keep, because a crash at frame 900 loses one frame, not the whole file. Then Blendity makes an MP4 (H.264) next to the frames with FFmpeg's libraries, the same FFmpeg Blender ships and encodes its movies with, built in. (A build made without Blender's libraries runs an ffmpeg program instead, from the PATH or the console's renderanim ffmpeg, or leaves the frames for Blender's Video Sequencer or DaVinci Resolve.) Each frame takes the render's own settings, so a path-traced animation takes its samples on every frame: the rasterizer is the quick way to check timing first.
? Why render numbered PNGs rather than straight to an MP4? | A video file is useless until it is finished, and its compression is lossy; frames survive a crash, can be re-rendered one at a time and keep full quality for editing.
! cmd:engine raster; renderanim 0 30 24 | Render 31 frames of the scene's sequence (or the selected clip) at 24 fps with the rasterizer
# MoonRay, an optional third engine
MoonRay is DreamWorks' production renderer, made open source in 2023 (Apache-2.0). It is a separate program: Blendity offers to download it the first time you start it, and Edit > Preferences > Renderers installs or removes it any time. MoonRay builds on Linux and macOS only, so on Windows it runs inside WSL, the Linux that Windows can run beside itself. With Render Engine set to MoonRay, F12 writes the scene in MoonRay's own scene language (RDLA): your meshes with their modifiers applied, Principled materials as MoonRay's DwaBaseMaterial, the four light kinds, the world as an environment light and the camera. MoonRay renders it, and Blendity shows the image through its own view transform and exposure. Previews keep using the path tracer. Some things are Blendity's own and aren't sent: toon shading, camera filters, procedural textures and alpha clipping; the log names what was left out. MoonRay's lights fade physically, so a point light that Blendity fades by its Range is matched in brightness at one metre.
! cmd:moonray status | Is MoonRay installed, and where?
! cmd:engine moonray; render; window Render | Render the Main Camera in MoonRay (the path tracer renders it when MoonRay isn't installed)
? Why does Blendity run MoonRay as a separate program instead of building it in? | MoonRay needs a dozen libraries Blender doesn't ship, builds only on Linux and macOS, and is large. As its own program it stays optional: nothing changes for anyone who doesn't download it.
? A point light looks brighter near the floor in MoonRay than in the path tracer. Why? | MoonRay's lights fall off physically, with the square of the distance; Blendity's follow Unity's Range curve. The two are matched at one metre, so closer surfaces get more light in MoonRay.
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
    {"Baked and Realtime Lighting", "Lightmaps, light modes, probe volumes and voxel GI", R"(
Light that bounces between surfaces is either baked ahead of time with the path tracer, or computed live by Realtime GI. Baked light costs about 1 ms a frame to show; live GI costs time whenever a view re-renders. The Lighting window (Window > Rendering > Lighting) holds both, as in Unity.
| Piece | Blendity | Unity | Blender
| Static objects | MeshRenderer > Contribute GI | Contribute Global Illumination | (none: Cycles traces everything)
| Light mode | Light > Mode: Realtime, Mixed, Baked (exists only through the bake) | Light > Mode | (none)
| Bake | Lighting > Generate Lighting | Generate Lighting | Bake (Cycles: Render > Bake)
| Moving objects | Probe Volume component | Adaptive Probe Volumes | Light Probe Volume (EEVEE)
| Live bounce | Realtime GI (voxel based) | Realtime GI (Enlighten) | (EEVEE: screen-space GI)
# A scene that doesn't lag
Tick Contribute GI on everything that never moves, make the sun Mixed (or Baked) and press Generate Lighting. A Probe Volume lights moving objects from the bake. Lighting Updates: Manual (the default) keeps the lighting as it was while you move things: press Update Lighting (Ctrl+Shift+L) when you want Realtime GI to catch up, and turn on Preview Lighting for a rough bake each time you stop (like Bakery's Realtime Preview). Automatic follows every change live instead.
# Baked emission: a small lamp without blocks
An emissive object is a light in a bake, not just a bright surface: each texel samples the emitter's surface directly (task 0157), one point a ray, the way the path tracer samples lights, and the hemisphere rays that happen to hit it are weighted down so nothing counts twice. Before, a 10 cm lamp was found only by chance - one hemisphere ray in sixty - and the floor under it came out as a scatter of blocks. Preview Lighting bakes at half the density now, with 32 rays first; a small emitter's glow shows its shape at 80 texels per metre.
? A tiny bright bulb lit the floor in blocks. What changed? | The texel samples the bulb's surface directly instead of waiting for a hemisphere ray to hit it; the two estimates are weighted so the light isn't counted twice.
? The glow under a small lamp looks soft but coarse. Which setting? | Lightmap Resolution: 80 texels per metre shows a small emitter's shape; 40 is Unity's default for the room.
# Realtime lightmaps, and why GI can look blocky
Contribute GI objects get realtime lightmaps (Realtime Resolution, 2 texels per metre as in Unity): voxel GI lights them a slice at a time when the lighting changes, so moving the camera traces nothing and they look the same from any distance. Other objects are traced at a quarter (or half) of the screen's pixels and smoothed; zoomed out, an object is only a few of those cells tall, so they can look blocky. Contribute GI or GI Resolution = Half fixes that; a bake has no blocks at all.
> Thiedemann, Henrich, Grosch & Müller (2011) Voxel-based Global Illumination. I3D.
> [GEA2] 12.6 Lighting with Stochastic Ray Tracing
@ blender/source/blender/draw/engines/eevee  (light probes)
? Why can't a moving object have a lightmap? | A lightmap stores light for one place. Once the object moves, the light it stored belongs somewhere else; probes store light in space instead, so anything can read it wherever it goes.
! cmd:window Lighting | Open the Lighting window
! cmd:select Plane; set MeshRenderer.ContributeGI true | Make the Plane static (Contribute GI)
! cmd:bake start | Generate Lighting
! cmd:set Lighting.RealtimeGI true; set Lighting.GIResolution 0 | Realtime GI at Half resolution
! cmd:lighting update | Update Lighting (Ctrl+Shift+L)
! cmd:lighting preview on | Preview Lighting: a rough bake when you stop
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
| OpenImageDenoise | Denoising path-traced renders (on an NVIDIA or AMD card when there is one) | Cycles denoiser | A-Trous filter
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
# The cards work between frames
A path-traced render used to get the card only inside the editor's slice of a frame: 40 ms for F12, 12 ms for the Camera Preview, one submission at a time, each waited for. The card did nothing for the rest of the frame, and the editor froze for the slice. Since task 0161 each card has its own thread that keeps two submissions in flight from one frame to the next, and the editor only polls it for a few milliseconds a frame: the Profiler's "GPU busy over the render" row rises from a fifth to most of the render's time, and moving around the editor while F12 runs stays smooth. Cycles does the same in Blender: its render runs on its own threads and the window's draw only reads the result.
@ blender/intern/cycles/session/session.cpp
? Why did a card report 99 % busy inside its submissions and 21 % busy over the whole render? | Between submissions - the rest of the editor's frame - the card had nothing queued. The busy share inside a submission says nothing about the gaps.
? Two submissions in flight: why not ten? | A third waits for the oldest, so the queue holds at most two chunks of under 50 ms each: the card never idles between them, and a stop or a change of camera waits at most for those two (about 100 ms of work) and the copy of the sums.
? You move one object while the Camera Preview renders on the card. What goes up to the card? | Only the instances (and the top-level acceleration structure rebuilt over the meshes it already holds): the scene's sections are hashed, and a build sends the ones that changed. A new mesh or a changed texture sends the whole scene, its buffers in one submission.
! cmd:engine path; device gpu; render; window Profiler | F12 on the card with the Profiler's GPU rows
# Combined rendering
With several devices ticked, they share one counter of sample numbers. Each device takes the next few samples it hasn't rendered, and fast devices take bigger batches. The CPU stops taking samples once the GPUs would finish the rest sooner, so it never holds up the end of a frame. Every 100 ms the devices' sums are added together for the display. With fast GPUs the CPU adds little, and switching it off can be quicker. It helps more alongside a GPU without ray tracing hardware.
@ blender/intern/cycles/integrator/path_trace.cpp, blender/intern/cycles/integrator/work_balancer.cpp
? Why can the CPU and GPU render the same frame without seams or blotches? | They don't split the image. Each one renders whole samples of every pixel, from the same random sequences, so averaging their sums gives exactly what one device would.
? Why can adding the CPU make a render slower? | One CPU sample can take 30x longer than a GPU sample. If the CPU is still finishing a sample after the GPUs are done, everyone waits. Blendity stops the CPU from taking new samples near the end, but a fast GPU pair still does better alone.
! cmd:engine path; device gpu +cpu; window Render | Switch to path tracing on the GPU, with the CPU helping
! cmd:engine path; device gpu; render; window Render | Render the Main Camera on the GPUs only
# A small graphics card
A GTX 1050 Ti has no ray tracing hardware: it traces with Blendity's compute kernel, roughly as fast as a 4-core CPU. Render > Device > GPU + CPU (Auto) uses both, each taking samples as fast as it finishes them, so neither waits for the other. A scene that wouldn't fit in the card's memory goes with its textures a mip level smaller at a time; one that can't fit renders on the CPU, with a message, instead of failing. Each submission to the GPU stays near 100 ms, under the 2 seconds after which Windows resets a GPU that seems stuck.
! cmd:engine path; device auto; window Render | Render on the GPU and the CPU together (Auto)
? Why are GPU submissions kept short? | Windows' driver watchdog (TDR) resets a GPU whose work takes over 2 seconds; a slow card given 64 samples of a big frame at once could pass it.
# The denoiser on the GPU
OpenImageDenoise, Cycles' denoiser, is a neural network that turns a few noisy samples into a clean picture using each pixel's colour, albedo and normal. Blender's libraries ship it with modules for NVIDIA cards (CUDA) and AMD cards (HIP), so Render Settings > Denoise Device on Auto runs it there when there is one. A 1080p frame took 463 ms on a 16-core CPU and 43 ms on an RTX 4070 SUPER. The CPU stays the fallback: without such a card, or after an error, the frame is denoised on the CPU, and the two pictures differ by well under one step of 255. Intel's integrated graphics (Iris Xe) aren't supported by its GPU modules, so a laptop like that denoises on the CPU.
@ blender/intern/cycles/integrator/denoiser_oidn.cpp, blender/intern/cycles/integrator/denoiser_oidn_gpu.cpp
? Why are the GPU's images kept in its own memory rather than shared with the CPU? | Shared ("managed") memory moves across the PCIe bus page by page while the network reads it. A 1080p denoise took 220 ms that way and 15 ms with the images in the card's memory.
! cmd:set Render.DenoiseDevice GPU; engine path; render; window Render | Render the Main Camera and denoise it on the GPU
# The views on the GPU
The Scene view, Game view and Camera Preview can draw their 3D on the GPU too (Preferences > Viewport Device). They use the same formulas as the CPU's views: the same lighting, the same shadow lookups, even the same colour table for AgX and the other OpenColorIO views. So the two pictures agree to within a step of 255. The GPU draws the frame in about a third of a millisecond; reading it back into the editor's canvas takes a few more. Picking and most overlays run on the CPU; the Edit Mode cage's edges and dots are drawn by the card too. Textures are sampled the CPU's way from its own mip chain, so a textured object, a Sky or HDRI world and a toon material look the same on both. What the GPU doesn't draw yet - GI and probes, axis materials, see-through materials, camera filters - draws on the CPU, and Preferences tells you why.
? Why does an Edit Mode drag on a 50,000-face mesh only send a few bytes to the card each frame? | The drag works out normals only around the faces that moved, and only those vertices and triangles go up: the rest of the mesh on the card is already right.
? Why does a Sky world cost the GPU almost nothing extra? | Its picture is one texture the card keeps: the background and the reflections read it, and the diffuse light comes from nine numbers (spherical harmonics) worked out once on the CPU.
! cmd:set World.Source Sky; viewport device gpu | A Sky world, drawn on the GPU
? You back the camera away from a hatched toon character and the hatching turns to moire, the band edges sparkle and the outline goes dotted. What has gone wrong, and since when is it fixed? | The maps were read at full resolution whatever the distance, the band edge fell on whichever side the pixel centre landed, and the outline shell shrank under a pixel. Since task 0164 every map reads at the pixel's level of detail, a band edge is one antialiased pixel, procedurals fade under a pixel, and a new outline keeps at least one pixel.
? Why does the GPU read textures from storage buffers instead of using its texture units? | The texture units filter, choose mip levels and decode sRGB in their own slightly different ways. Reading the CPU's own mip chain and filtering it with the CPU's formulas keeps the two pictures within a step of 255.
! cmd:viewport device gpu | Draw the views on the GPU
! cmd:viewport device cpu | Draw the views on the CPU again
# The Profiler's GPU block
The Profiler window (Window > Profiler) shows how long a frame spent in the GPU path: the time from handing the card its work to getting the picture back. That is wall time, and most of it can be waiting. The GPU block puts the card's own clock beside it: how long the shadow, g-buffer and lighting passes ran (timestamp queries on the card), how long the copies of the picture out to the CPU and the wait for them took, and "GPU busy in the frame" as a share of the whole frame. While F12 or the Camera Preview is running, two rows show the path tracer's busy and wait time inside its submissions and its busy share over the whole render (F12) or the frame (Camera Preview): the card sits idle between submissions, and that is where the time goes. A card without timestamp queries shows n/a beside the wall time. Blender reads the same GPU timestamps for its own debug timings.
@ blender/source/blender/gpu/opengl/gl_debug.cc
? The frame spent 8 ms in the GPU path and the block says the card was busy for 1 ms. Where did the other 7 go? | The copies of the picture out and the fence after the submission (their own row), and the rest: gathering the objects, uploads, recording. The card was idle for them; that is what tasks 0160-0163 cut.
? Why does the block say n/a on some cards? | Their queue reports no timestamp support, so there is no clock to read. The wall time row is still there.
? A view on the card used to wait for the GPU twice a frame. What were the two waits, and which one is gone? | One fence after the picture's passes, one after the copy of the planes to the CPU. The first is gone: the copy is recorded behind the passes in the same submission, and the one fence after the copy remains (ADR 0051).
! cmd:viewport device gpu; window Profiler | The views on the GPU, the Profiler open
! cmd:bench 3 | bench prints "GPU busy" for the last frame
# Drags that stay quick
While you drag something in the Scene view, the Game view and the Camera Preview keep the picture they had and draw once when you let go, as Blender's sculpt mode updates its other views when the stroke ends. Play always draws every frame. Preferences > Other Views Follow Drags makes them follow every frame instead. On the CPU, the Scene view also draws more cheaply while you drag or orbit: the sun's shadow map at half size with a softer filter. The frame after you stop is exact again, as Blender's viewport is after it lowers its quality while you navigate (Preferences > Simpler While Moving).
@ blender/source/blender/editors/sculpt_paint/mesh/sculpt.cc, blender/source/blender/draw/engines/eevee/eevee_instance.cc
? Why does the Game view seem to lag behind a drag and then jump? | It keeps its last picture during the drag and draws once on release: rendering the scene again for it every frame of a drag is what made drags slow. Turn on Other Views Follow Drags to see it follow.
? Is the picture you see after a drag ever the cheaper one? | No. A picture drawn the cheaper way is never reused: the first frame after you stop draws exactly, and the tests check it pixel for pixel.
! cmd:views follow on | The Game view follows every frame of a drag
! cmd:views follow off | It catches up when you let go (the default)
! cmd:viewport device cpu; views simpler off | Every frame exact on the CPU, even while you move
! cmd:views simpler on | Cheaper shadows while you move (the default)
)"},
    {"Points, Lines, N-gons and Booleans", "Building from a vertex, SketchUp faces, combining solids", R"(
Blender can start a model from a single vertex. Ctrl+E (Blender's E) on a vertex grows an edge: a "wire" or loose edge that no face uses yet. Ctrl+E on that edge grows a face. Blendity keeps those wire edges in the mesh (Mesh::loose_edges), saves them, and lets the Screw modifier sweep them into a solid.
@ blender/source/blender/bmesh/operators/bmo_extrude.cc
# SketchUp's faces
SketchUp has no triangles or quads to think about: a face is any flat polygon, and drawing a line across it splits it in two. Blender can do the same with n-gons. Blendity's N-gon Mode shows a flat region of faces as the one face SketchUp would show: its inner edges hide and one click selects all of it. Merge Coplanar (Blender's Limited Dissolve) makes that one real n-gon. The Knife (K) draws a line between two points on a face's outline and splits it, snapping like SketchUp: endpoints green, midpoints cyan, anywhere on an edge red.
| | SketchUp | Blender | Blendity
| A face | any flat polygon | an n-gon | an n-gon (N-gon Mode hides coplanar edges)
| Split a face | Line tool | Knife (K) | Knife / Line (K)
| Merge flat faces | erase the edge | Limited Dissolve | Merge Coplanar
| Push a face | Push/Pull | Extrude | Push/Pull (P)
@ blender/source/blender/editors/mesh/editmesh_knife.cc
@ blender/source/blender/bmesh/operators/bmo_dissolve.cc
# Combining solids
Join (Ctrl+J) puts several meshes into one object; they still don't touch. A Boolean actually merges them: Union keeps the outside of both, Difference cuts the second out of the first, Intersect keeps only the overlap. The solver needs closed, watertight meshes. Blendity uses Manifold, the same library as Blender's default Boolean solver. Applying it removes the cutters; as a modifier, the cutter becomes a wire outline you can keep moving, and renders leave it out.
@ blender/source/blender/geometry/intern/mesh_boolean_manifold.cc
? Why does a Boolean need closed meshes? | It has to know which side of every face is inside the solid. With a hole in the mesh, "inside" isn't defined.
? You join a cube and a sphere that overlap. How many objects, and do their surfaces merge? | One object, but the two surfaces still pass through each other; only a Boolean Union merges them.
# Drawing onto faces
SketchUp and UModeler build models by drawing: a rectangle drawn on a face splits off a new face you can Push/Pull, a line across a face cuts it in two, and a profile pushed along a path (Follow) makes mouldings and pipes. Blendity's Draw tools do the same, snapping to corners (green), midpoints (cyan), edges (red) and faces (blue). Draw a polyline on the ground for a path, select a face and use Follow; or draw half a vase outline and Spin it.
@ blender/source/blender/editors/mesh/editmesh_bisect.cc
? You draw a rectangle inside the top face of a cube. How many faces does the cube have now, and why? | 10: the top face becomes a ring of 4 faces around the new inner face, because a face here can't have a hole in it.
! cmd:create Shape: Stairs | Create parametric Stairs (change Steps in the Inspector)
! cmd:select Cube; edit face; draw rectangle | Draw a rectangle onto the Cube (click two corners on a face, then press P to Push/Pull it)
! cmd:select Sphere; selectadd Cube; boolean difference modifier | Cut the Sphere out of the Cube as a live Boolean modifier
! cmd:create Shape: Box; edit face; ngon on | A Box in Edit Mode with N-gon Mode on
)"},
};

int lesson_count() { return (int)(sizeof(kLessons) / sizeof(kLessons[0])); }
const Lesson &lesson(int i) {
  if (i < 0) i = 0;
  if (i >= lesson_count()) i = lesson_count() - 1;
  return kLessons[i];
}

}  // namespace bl
