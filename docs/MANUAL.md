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

> Learn: *Welcome to Blendity*, *The Editor Layout*, *Navigating the Scene View*, *GameObjects, Components and Objects*,
> *Hierarchy, Parenting and the Scene Graph*, *Move, Rotate, Scale*.

---

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

> Learn: *Points, Lines, N-gons and Booleans*, *Move, Rotate, Scale*.

---

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

**Use cases:** hard-surface props, architecture details, game-ready low poly with Subdivision Surface previews.

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

**Many at once.** The Materials, Textures and Project windows select like the Hierarchy: Ctrl+click, Shift+click a
range, Ctrl+A; Delete asks once for all of them (they go to the Recycle Bin / Trash).

**Example: a brick wall material**
1. Window > Textures > New > Bricks. Adjust the brick layer's fields; it already has an Edge Wear layer - raise it
   for worn corners.
2. Select the wall and press Assign to Selection: albedo, normal and roughness are set, with the tiling.

**Example: a terrain that paints itself**
1. Make a material, set Axis Mode: World; Axis Top: a grass material asset, Axis Side: rock, Axis Bottom: dirt.
2. Set Axis High to a snow material with High Start 30 (metres), and Axis Noise so the borders wander.

**Use cases:** game assets with real PBR maps; architectural materials; stylised (toon, anime, manga) renders.

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

**Use cases:** product shots and turntables (path traced); game levels with baked light; stylised and retro looks.

> Learn: *How This Editor Renders*, *Lights and Materials*, *Render Engines*, *Light, Sky and Tone Mapping*,
> *Baked and Realtime Lighting*, *Rendering on the GPU*.

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
materials and textures come along, and rigged files come with their skeletons and skins.

**Export** (File > Export): every mesh format Blender 5.2 exports - OBJ, FBX (binary or ASCII), glTF (.glb or
.gltf), STL, PLY, USD - with Blender's options (Selected Only, Apply Modifiers, Scale, Up Axis, Triangulate...).
**Bake Textures** on export turns procedural materials into images other programs understand. Every format was
checked both ways against Blender 5.2.

**The project.** The Project window is `Assets`: nested folders, drag to move, F2 to rename, Delete to the Recycle Bin
/ Trash. References to a moved or renamed material, texture or clip keep working. Scenes are text files
(`.scene`).

> Learn: *Assets, Scenes and Files*, *Coordinate Systems and Handedness*.

---

## 11. Performance and the minimum machine

**Minimum:** a 4-core / 8-thread CPU at about 3 GHz, 8 GB of RAM, a GTX 1050 Ti class GPU (Vulkan 1.2; the GPU is for
path tracing, baking and realtime GI - the editing views draw on the CPU). On such a machine Blendity starts on the
**Low** quality tier.

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
| `libs` | which of Blender's libraries this build uses |

---

*This manual condenses the README's feature list, the lighting guide and the Learn lessons. Developer documents -
how it's built and why - are in [`ARCHITECTURE.md`](ARCHITECTURE.md), [`decisions/`](decisions/),
[`tasks/`](tasks/), [`PERFORMANCE.md`](PERFORMANCE.md) and the [`CHANGELOG`](../CHANGELOG.md).*
