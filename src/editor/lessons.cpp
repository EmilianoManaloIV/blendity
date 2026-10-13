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
Navigation stays Unity's (hold RMB to fly, Alt+LMB orbit, MMB pan), and so do the W / E / R gizmo tools. Editing works the Blender way. G grabs the selection and moves it with the mouse. During a G, R rotates and S scales. X, Y or Z lock to an axis (press twice for the object's own axis), and Shift+X / Y / Z lock to the plane without it. Type a number for an exact value, hold Ctrl to snap, then click or press Enter to keep it, or press Esc to cancel. Ctrl+E extrudes and then moves along the normal, I insets as you move toward the centre, and Ctrl+B bevels as you move away (the wheel sets the segments). Edit > Blender Transform Keys makes R and S rotate and scale directly. docs/USABILITY.md explains how the two keymaps share the keyboard.
@ blender/source/blender/editors/transform/transform_mode_translate.cc
! cmd:select Cube; edit face; fsel facing 0 1 0 | Select the Cube's top face, then press G, Z, type 0.5 and Enter in the Scene view
)"},
    {"Modifiers vs Components", "Non-destructive editing and Catmull-Clark", R"(
Blender's modifier stack changes a mesh on the fly without touching the original: Subdivision Surface, Mirror, Smooth... Each modifier feeds the next. Unity has no modifier stack. Any procedural change is done by a script component.
Blendity combines the two ideas: modifiers are components, shown together in the Inspector's Modifiers panel as Blender's stack. Add Modifier lists them by Blender's groups: Edit (Weld), Generate (Array, Bevel, Boolean, Decimate, Mirror, Screw, Solidify, Subdivision Surface, Triangulate, Wireframe) and Deform (Cast, Curve, Displace, Lattice, Mesh Deform, Simple Deform, Smooth). They run from the top down, and the result is cached until the mesh or a setting changes. Each header has Blender's three toggles: Edit Mode (draw the result while you edit the cage), Viewport and Render. Its menu applies, duplicates, copies to the other selected objects or moves the modifier. Apply bakes one modifier into the mesh, like Blender's Ctrl+A; Apply All bakes the whole stack.
# Order matters
Mirror then Bevel bevels the seam in the middle; Bevel then Mirror doesn't, because the seam doesn't exist yet when the bevel runs. Array then Simple Deform bends the whole row; Simple Deform then Array repeats one bent copy. Move a modifier up or down and watch.
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
A skeleton is a hierarchy of bones: in Blendity each bone is a GameObject with a Bone component, under a root with an Armature. Pose it by turning bones with the Rotate gizmo, like any object. A mesh follows through its Armature (Skin) modifier: each vertex moves with the bones its vertex groups are named after, weighted. Select a mesh and choose GameObject > Armature: a bone the mesh's height is made and the mesh bound to it with automatic weights (by distance); `bone add <parent>` adds bones at a parent's tip, and Bind: Automatic Weights binds again; Weight Paint fixes what automatic weights get wrong. Linear blending makes twisted joints thin (the candy wrapper); Armature > Deform: Dual Quaternion keeps their girth. Armature > Rest Position shows the mesh unposed; `armature reset` puts the bones back, `armature apply` makes the pose the new rest. Rigged FBX and glTF characters import with all of this set up.
# Inverse kinematics
Turning a thigh and then a shin until the foot lands on a step is slow; IK does it backwards. An IK constraint on the shin (Chain Length 2: shin and thigh) turns both so the shin's tip reaches the Target object: move the target and the leg follows, every frame and in Play. Two bones have one exact answer (the triangle of thigh, shin and hip-to-target, the law of cosines) up to which way the knee points: a Pole object picks that. Longer chains (a tail, a spine) are solved by cyclic coordinate descent: from the tip up, each bone turns the tip towards the target, a few dozen rounds. Weight fades from the rest pose to the solution. A bone IK turns refuses the gizmo and G / R / S: move its target. GameObject > Rig > Humanoid or Quadruped makes a whole skeleton with targets (FootIK, HandIK) and poles; Auto IK on Selected Skeleton finds the legs and arms of an imported Mixamo, Unity or Rigify skeleton by their names. Console: `ik add <bone>` (makes its target and pole) or `ik add <bone> <target> [pole] [chain]`, `rig humanoid|quadruped|autoik`. All of it is in the Rigging window (Window > Rigging).
> Welman, C. (1993) Inverse Kinematics and Geometric Constraints for Articulated Figure Manipulation. MSc thesis, Simon Fraser University (CCD).
@ blender/source/blender/ikplugin/intern/iksolver_plugin.cc
? A thigh and a shin are 0.45 m and 0.43 m. Can the foot reach a target 0.9 m from the hip? | No: the chain is at most 0.88 m long, so IK points the straightened leg at it.
! cmd:rig humanoid; window Rigging | Make a humanoid rig and open the Rigging window
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
Show Guides (Camera inspector, or the Game view's Guides menu) draws lines over what the camera sees, never into the render. The rule of thirds splits the frame into a three by three grid: a horizon on one of the horizontal lines and a subject where two lines cross usually reads better than one dead centre. The golden ratio does the same at 0.382 and 0.618. The golden and harmonious triangles lay a diagonal across the frame with lines from the other two corners (square to the diagonal for the golden ones, to the golden section of the far edge for the harmonious ones), for compositions built on diagonals. Title Safe and Action Safe mark the margins a television might crop (Blender's defaults: Title Safe 10 % across and 5 % down, Action Safe 3.5 % both ways, of half the frame on each side).
! cmd:select Main Camera; guides thirds on; window Game | Show the Main Camera's rule-of-thirds guides in the Game view
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
# Light types and colour temperature
| Type | Blendity | Blender | Unity
| Sun | Directional | Sun | Directional
| Bulb | Point (Range) | Point | Point
| Cone | Spot (Spot Angle, Inner Spot Angle) | Spot (Size, Blend) | Spot
| Panel | Area (Rectangle / Disc, Width, Height) | Area | Area (HDRP / baked)
A spot light lights a cone along its forward axis and fades between the inner and outer angles. An area light lights only the side its arrow points to. In path-traced renders it is sampled across its whole surface, so its shadows are soft: the larger the panel, the softer they get. Real lights have a colour temperature. Tick Use Color Temperature and the colour is tinted like a black body at that many kelvin. The tint runs warm to cool: 1900 K is candlelight, 2700 K a bulb, 5500 K noon sun and 6500 K white, while 10000 K and above is the blue of shade.
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
hull). Both show in the views, F12 renders on the CPU and GPU, and exports. For the printed-page look, a camera's
Add Filter > Looks > Manga stacks Grayscale, Halftone dots, Line Art and Paper; Anime adds Line Art, a little
saturation and a soft Bloom. Line Art draws creases where the surface turns more than an angle, so a cube's edges
get lines and a smooth sphere only its outline.
! cmd:select Sphere; material Toon; component Outline | Make the Sphere toon-shaded with an outline
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
# A small graphics card
A GTX 1050 Ti has no ray tracing hardware: it traces with Blendity's compute kernel, roughly as fast as a 4-core CPU. Render > Device > GPU + CPU (Auto) uses both, each taking samples as fast as it finishes them, so neither waits for the other. A scene that wouldn't fit in the card's memory goes with its textures a mip level smaller at a time; one that can't fit renders on the CPU, with a message, instead of failing. Each submission to the GPU stays near 100 ms, under the 2 seconds after which Windows resets a GPU that seems stuck.
! cmd:engine path; device auto; window Render | Render on the GPU and the CPU together (Auto)
? Why are GPU submissions kept short? | Windows' driver watchdog (TDR) resets a GPU whose work takes over 2 seconds; a slow card given 64 samples of a big frame at once could pass it.
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
