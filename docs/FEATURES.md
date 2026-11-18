# Blendity: what it does, in full

The [README](../README.md) has the short list; this is the long one, stage by stage, with the round each part
arrived in (the [CHANGELOG](../CHANGELOG.md) has every round). The [manual](MANUAL.md) teaches the same ground
with examples.

## Stage by stage

Each stage is a **layout** that arranges the windows and shows only that stage's tools (Show All Tools shows
everything):

- **Reference & Planning** - reference images, physical cameras with depth of field, composition guides, Pilot Camera.
- **Blocking Out** - primitives and parametric shapes, a **Blockout** whose boxes, cylinders and spheres add to or cut one mesh that follows them as they move (round 477), with **rooms** - air that brings its walls, floor and ceiling and merges with its neighbours or keeps its walls - doors, windows and pieces that intersect (round 478), dragged out with handles on every face that snap to the other pieces and a **Room tool** that draws a room on the ground or a face (round 479), **windows and doors with a style** - frame, bars, sill board and glass; single, double, sliding, arched and revolving - made from numbers (rounds 498 and 499), **architectural pieces** - columns, pilasters, beams, railings, roofs, stairs with a landing, spiral stairs and arcades - each one piece with numbers (round 500), Blockouts that **snap to each other and join when they touch** (round 501), **modifiers on a Blockout** (round 502) and **on a single piece** - Bevel the arches, Array ten columns from one (round 514), door leaves that are objects and swing about their hinge without a rebuild (round 515), **a door's and a window's parts - leaf, frame, glass - each an object that takes its own modifiers** (round 547), with a **Blend radius** a piece that rounds its join like a metaball while the rest stays crisp (round 481), **materials by floor, wall and ceiling with world-scale UVs, Bake to Mesh and back** (round 482), **a Draw Piece tool that shows the piece while it is drawn, scale that never shears a piece** (round 496), with **wedges, stairs, arches, outlines pulled up and any mesh** as pieces (round 480), pieces drawn straight onto a Blockout's faces - a Subtract box pushed in, a ramp, stairs, an arch (round 495), SketchUp-style drawing and **Push/Pull** (shapes projected onto curved surfaces, Plasticity-style; what you draw is welded, T-junctions joined and flat faces merged only across continuous UVs, and **Solve Mesh** cleans a mesh that already has doubles), Booleans, snapping;
  **drawing in perspective** with a pen - SketchBook-style rulers, ellipses, symmetry and a 3-point guide that *is* the
  camera, strokes corrected to what the hand meant, canvases on the ground and on faces, and boxes, cylinders, faces and
  whole scaffolds built from the construction lines, never guessed.
- **Modeling & Topology** - Blender's Edit Mode (extrude, inset, bevel, loop cut, bridge, knife, n-gons, LoopTools, F9; a
  50k-face mesh enters it in about a frame, the selection's size typed in (Dimensions, round 536), its overlapping-vertex check off the main thread), a
  modifier stack (Array, Bevel, Boolean, Mirror, Subdivision, Lattice, Curve, Mesh Deform...), curves and lattices
  a Quad Sphere primitive (Even Area or Even Angle, F9 for its subdivisions) and Snap to Object Origins for the V snap, a modal G with Ctrl and the drawing tools (round 535),
  panoramic cameras (Equirectangular, Fisheye, Central Cylindrical, Mirror Ball: Camera > Projection > Panoramic) in the views, F12's raster render and the path tracer (round 538),
  edited with the same keys.
- **UV Unwrapping** - a UV Editor with LSCM unwrap, Smart UV Project, Part Unwrap (PartUV's charts by parts, ABF++),
  projections and island packing.
- **Texturing & Materials** - Principled materials (with Subsurface, Sheen, Coat and Anisotropic) and material
  assets, **Volume (fog)** materials - fog, smoke and mist inside a mesh, scattered truly by the path tracer - layered
  **procedural textures** with Blender's shader nodes as layers (Add Layer's search, Reset to Default on any field,
  the preview up to a layer), **Camera Flatten** for the anime 2D look (depth squashed toward the camera, shaded as
  modelled; a bone or any object dropped on its pivot; below 0 or through a lens of its own, forced perspective) and
  **View Angle Shapes** (shape keys that come in by the camera's
  angle - a mouth slid to the side in profile), axis materials, toon shading (five styles, painted shadow / highlight / rim control, shade maps, ramps, a face shadow
  map, inner lines and hatching, custom normals from a head proxy, hold frames on 2s / 3s), anime background
  generators (leaves, grass, walls, windows, roofs, sky, clouds, rocks, water; cards cut out by their cover), an
  import preview with the shading switchable before anything lands in the project, hair that keeps its curl, parts into
  strands and moves in the wind (and is rigged from its own mesh in one step), soft tissue that trails the body's
  acceleration and keeps its volume, texture baking.
- **Lighting & Rendering** - point, spot, area and sun lights; baked lightmaps (denoised where they're noisy, soft
  shadows from Unity's Baked Shadow Radius and Angle, directional as Unity's), probe volumes and voxel realtime GI;
  baked VRC Light Volumes light moving objects in the views as VRChat does;
  a light's Indirect Multiplier scales the light the bake bounces (round 452);
  baked probes hold the sky's own light by direction, and a changed sky still relights them without a re-bake (round 453);
  Shadowmask keeps four Mixed lights' baked shadows (a fifth is baked), Distance Shadowmask shows the key light's realtime shadow near the camera and the mask beyond the Shadow Distance, and Unity's own masks and light channels come in with Use Unity's Lightmaps while an exported package carries the masks as Shadowmask textures (round 454);
  a Unity scene's lights take their intensity as its project's Graphics settings say (linear or gamma), so a world like SideRail bakes far closer to Unity's light (round 459);
  the Sky world's sun lights the scene when no Directional Light does, as Blender 5.3's EEVEE (round 542);
  saved PNG and JPEG images carry the sRGB ICC profile, as Blender 5.3 writes them (round 542);
  a bake's filter reads each texel's noise by the samples that carry its light, so a texel lit by a few lucky rays is smoothed away and a bake leaves far fewer specks (round 486);
  Generate Lighting's gather runs on every ticked graphics card, as F12 does, in batches sized by the cards' speed, and says which cards gathered (round 487: SideRail's lightmaps 37.7 s to 16.2 s);
  Remove Ringing (Hanning or Lanczos, a view setting) softens the dark ring a bright lamp leaves in baked probes, and Light Probe Groups are objects - placed and moved in the Scene view, imported from Unity - that Generate Lighting bakes at in place of the automatic grid (round 455);
  Point Cloud objects - Points or Gaussian Splats with spherical harmonics up to degree 3 (round 530);
  Gaussian splats drawn in the Scene view on the CPU and the card - EWA ellipses, view-dependent colour, sorted back to front on a worker (round 532);
  Gaussian splats traced on the graphics card too - GPU Compute and OptiX (round 534);
  splat import from PLY, SPZ (v1-4) and glTF's KHR_gaussian_splatting (round 531);
  Draw Piece draws a door or a window onto a wall of a Blockout by dragging a rectangle, from a Shape list that groups them under Openings (round 513);
  a door or window takes modifiers: an Array or a Mirror repeats the whole opening, a Bevel rounds its hole (round 546);
  F12 rasterized or path-traced renders (Embree, OpenImageDenoise, GPU through Vulkan), or in DreamWorks' MoonRay, an
  optional download run as a program of its own (in WSL on Windows); the editor's views drawn on
  the GPU as the CPU draws them (textures, Sky and HDRI worlds, toon, the Edit Mode cage, shadows traced by the card's ray tracing where it has it - round 543); camera filters from colour
  grading to PlayStation and manga looks; the **Editing** tab develops a render or an image (exposure, white balance,
  curves, HSL, clarity, dehaze, crop; presets), non-destructively.
- **Rigging**, and **Skinning & Weight Painting** - bones (drag their heads and tails, Extrude new ones), Blender/Maya-style
  **rig controls** (IK boxes and poles, FK circles, coloured by side), characters under one root as Unity imports them,
  skins, weight paint (with Blender 5.3's automasking), vertex paint and colour attributes (shown by a material's Vertex Color, read from PLY / glTF / FBX), and IK in a Rigging window; **automatic
  weights and weight transfer by inpainting** (Epic, 2023); a **Mixamo-style marker auto-rig** for people and
  four-legged animals; rigged FBX / glTF import with bind poses.
- **Animation** - a **Unity-style dopesheet** and **Curves view** (key tangents: Clamped Auto, Free Smooth, Flat,
  Broken) with Record and Preview, **any Inspector field keyed** (right-click > Insert Keyframe, or I - a material
  slot's fields too; visibility through Add Property), the Animation component for Play, and a **Premiere-style Sequencer**: clips laid out
  on tracks crossfade where they overlap, and keys jump between them in Play. **Physics for animation**, as in
  Cascadeur: each bone weighed from the skinned mesh, the center of mass followed through a clip, contacts found from
  the motion and pinned, a jump corrected to a true parabola with its angular momentum kept, and **AutoPhysics**: the
  whole clip made physically possible, take-offs and landings included (Blendity's own trajectory solver), each shown
  as a ghost before it is applied. **Spring bones** for hair, ears, tails and cloth, found automatically by name and
  shape (Mixamo, VRM, Japanese rigs), live in Play or baked into the clip. An **active ragdoll** (Jolt) plays the clip
  with motors, so hits and falls react, and **AutoPosing** poses the whole body by dragging one hand, foot, head or
  the hips, balanced over the feet. **Walk Cycle** makes walking and running cycles from the rig (feet never sliding,
  for two legs or four), **Footsteps** walks a character along a curve on footprints you can move (onto stairs and
  slopes), and **Locomotion** walks and runs it live in Play wherever it is moved. **AI Inbetweening** (an optional download:
  ONNX Runtime and a network Blendity trained on ten hours of CMU motion capture) fills the motion between two keys
  in a style, shown as a ghost before it is applied, and can be fitted to your own clips. Imported models arrive with
  their materials as `.mat` assets and textures found or extracted (the finder reads the folders around the model and
  pairs each bare material with the texture set its name is likeliest to own), and with their **shape keys**.
  **MikuMikuDance** characters (`.pmx`, `.pmd`) come in rigged: leg IK, their hair and skirts as spring chains, their
  faces as shape keys, toon-shaded with their outline, and their `.vmd` dances play on them. **VRM** avatars (0.x,
  1.0) come in with their humanoid names, expressions, spring bones and MToon look. A model's **animations** (FBX
  stacks, glTF animations) become clips in `Assets/Animations/<model>/`, the first playing on it; **BVH** motion
  capture comes in as a skeleton with its clip; **Collada** scenes with their skins and animations. Every such assistant is a
  switch in Preferences: hand animation never changes.

Files: import and export OBJ, FBX, glTF, STL, PLY and USD, checked both ways against Blender 5.2 (shape keys go out as
glTF morph targets and FBX blend shapes, round 351;
FBX carries the skeleton, skin and clip as glTF does, round 352;
the texture set and lightmap UVs go along, round 353;
an export waits for a remesh or bind still running on it, round 354;
the Profiler ranks a scene or avatar against VRChat's PC and Quest limits, round 355;
posing pays for the meshes a pose moves, not for the avatar: each mesh is skinned once a frame and only when its bones moved, follows in place, and the world's rows stay on the card (dragging an IK target on a 140,000-triangle stand-in: 48 ms a frame before, 10 ms now; a whole-body pose 35 to 53 ms, 7 to 12 ms from Pass 26's skinning on the card: the card moves a posed character's vertices from a palette a frame, outlines and shape keys stay on it, and the CPU views and everything that reads a posed shape (picking, snapping, colliders, exports, bakes) use the same rule - on the stand-in avatar alone every pose row meets 16.7 ms, in a world the card's own draw of the world holds the rest), Pass 22, rounds 468-475, Pass 26, rounds 518-523;
the Avatar window makes VRChat's 15 visemes and checks an avatar,
after the CATS plugin,
round 356,
and its Fix Model readies an imported character for Unity's Humanoid in one step,
round 357,
with CATS' Model Options and Pose and Shape tools,
round 358,
and its Decimation modes,
Bone Root and Bone Merging,
round 359,
Eye Tracking and the Texture Atlas,
round 360,
Merge Armatures,
Attach Mesh,
Translate and Export for VRChat,
round 361;
a Collider component written for Unity, Unreal and Godot, round 362, and LOD Groups switched in the Game view and exported as _LOD0.._LODn for Unity, round 363);
the Sequencer holds storyboard cards and camera cuts for animatics and cinematics (round 364) and WAV sound, played in step and mixed into Render Animation's video (round 365), and footage, keyed opacity and transform, Add Edit, Ripple Delete and ProRes and VRChat export presets (round 391);
Poiyomi, lilToon and URP texture presets, Material ID masks and Bake All Maps (round 392);
the dependency-free build's Render Animation offers Blender's libraries for its MP4 (round 366);
a Capsule, Unity's unique names, and the console's save / open / new / rotate / scale (round 367);
the pen's eraser end, tilt and a pressure curve in sculpting (round 368);
a brush keeps its own size, strength and colour, with Unified switches, and Paint's hardness and spacing follow the pen's pressure curve (round 540);
Unity packages unpacked into the project (round 369) and their prefabs and scenes brought in,
scripts kept as Engine Components (round 370),
with Unity's materials converted by shader (lilToon, Poiyomi, MToon, UTS2, Standard, URP) and PhysBones' settings kept as spring bones (round 371),
the materials keeping their maps whatever is open while a package unpacks, the shader's keyword deciding which sliders and maps count, and a smoothness kept in a map read as its mean (round 485),
a .unity opened as the scene with its materials on its models' parts (numbered by the installed Unity once),
its lights' and renderers' overrides,
ambient and fog,
and Silent's Filamented as PBR (round 395),
and Unity's own baked lightmaps and light probes shown,
read by the installed Unity (round 400),
its fog modes,
skybox,
camera,
post-processing,
lights' shadows and reflection probes as Unity shows them (round 401);
lilToon's own lighting model on a Toon material - checked against lilToon's renders - and the avatar's VRChat components (descriptor, PhysBones, colliders, contacts, constraints, Animator) as Blendity's, written back and sent over Live Link (round 402);
every kept Unity component editable under Unity's names, VRChat's SDK classes named, the world's Scene Descriptor and Pipeline Manager as Blendity's (round 403);
the same stations rendered by Unity and Blendity and scored - Shinano passing at every station, SideRail near the target - with the attributes table and the report (round 404);
Unity's own ACES curve, its BRDF's weight on a Unity scene's reflections and three more shaders read, the stations measured again (round 405);
Unity's BlendShapes foldout with Edit this shape and the Animation window in Unity's layout (round 406);
Unity .anim clips read and written (round 407);
clips retargeted between humanoid rigs like Mecanim (round 408);
rotation keys converted between Euler and Quaternion, motion copied and pasted in world space, Paste Pose with an influence and flipped, Align to Transform Orientation, a ruler that pauses playback while it is scrubbed and a Wrap Playhead switch, as in Blender 5.3 (round 541);
Export Unity Package sends the work back under the same GUIDs (round 372);
Godot scenes (.tscn) come in, scripts kept, and go back with their nodes' transforms (round 373);
.blend files come in through the installed Blender, and the Blender and Unreal bridges Send to Blendity (round 374);
the round trip run as a production, glTF's Keep Transforms keeping pivots on the way back (round 375);
its world, fog, exposure and the render camera's Bloom, Vignette and grading go to every bridge as you light (round 376), and textures go by their pixels - a painted surface each stroke, a procedural texture as you drag (round 377);
skinned characters go once with their skin and blend shapes, then only poses and weights (round 378);
clips go baked as the engines' own clips when keyed (round 379);
LOD Groups and colliders arrive as the engines' own levels and collision (round 380);
with VRChat's SDK in a Unity project, avatars get their Avatar Descriptor and PhysBones and worlds a Scene Descriptor (round 381);
all of it streams at a millisecond or two a frame with four engines connected, and goes on while jobs run on workers (round 382);
VR View puts you in the scene at life size through OpenXR - SteamVR,
Quest Link,
Windows Mixed Reality (Edit > VR View, `vr start`; round 383) - where your controllers teleport,
walk,
snap-turn,
grab,
turn and scale the world and measure it (round 384),
and sketch ribbons and tubes in the air with pressure and a palette (`vr sketch`; round 385) and paint the object in Paint mode with the desk's brush (`vr paint`; round 386);
**Live Link** sends the
scene as you change it to Blender, Unity, Godot or Unreal on the same computer (`docs/LIVELINK.md`). The keymap starts
from Unity, Unreal, Blender, Maya, 3ds Max or SketchUp, and **Edit > Editor Feel** switches the keys and the Scene view's
mouse navigation together to Unity's, Unreal's, Blender's or Maya's. Any tab can have a window of its own, on any
display (drag it out of the editor). Ctrl+K finds any command by name.
## Machines

| | Minimum (a laptop such as the HP Envy 13.3" 2-in-1) | Recommended |
|---|---|---|
| CPU | Intel i7, 4 cores / 8 threads (i7-1165G7 class) | 8+ cores |
| RAM | 8 GB (shared with the graphics) | 16 GB+ |
| GPU | Iris Xe or a GTX 1050 Ti (Vulkan 1.2; for path tracing, baking and realtime GI) | a GPU with ray tracing hardware (RTX, RX 6000+, Arc) |
| Screen | 1920 x 1080 at 150 % (1280 x 720 of layout), touch and pen | 1920 x 1080 or more |

The minimum machine starts on the **Low** quality tier (Edit > Preferences..., Quality) and says so on the first run:
the views draw at 67 % and scale up, with a 1024 shadow map and 60 fps. Editing frames there run at about 35-40 fps,
orbiting at 60. Renders always use the scene's own settings. On a 2-in-1, Tablet Mode follows the fold: fingers
navigate (one orbits, two zoom, pan and twist), the pen works, and **the Wheel** - one radial menu for Space, the pen's
button or a finger held still, with a set per mode you fill from any menu's right-click - brings Undo, Delete and the
rest without a keyboard ([the manual](MANUAL.md), chapter 1). **My Tools** holds panels of your own buttons, any command
takes a key of your own (right-click it > Assign Shortcut...), and one `.blendity-ui` file carries the whole arrangement. On a
drawing tablet beside the screen, a second Scene view and a pinned Inspector in a window of their own (*Duplicate into New
Window*), and tabs dock into whichever window they are dropped on. The **Draw tab** is the pad: the scene from the Scene or the Game camera, the
pen drawing in that perspective while the keyboard moves the view, every stroke placed in the scene where it was drawn;
it works like SketchBook Pro's canvas - one slim toolbar (the brush, its color and size, the Ruler, Ellipse and Curve,
symmetry, perspective, Steady and Predictive Stroke, Layers, More), the pad zoomed, panned, turned and flipped with the
wheel,
a middle-drag or Space+drag and two fingers,
Tab to hide the tools,
and blank Paper as a source - its layers with Photoshop's 27 blend modes,
clipping and layer masks,
and rectangle and lasso selections to fill and delete (round 388),
and Ctrl+T's Free Transform and a brush's pen tilt (round 389),
Illustrator's shapes,
Bezier Pen,
fills,
artboards and SVG in and out (round 390) - and SketchBook's
brushes (Pencil, Pen, Inker, Felt Pen, Marker, Airbrush, an Eraser that cuts) in a floating palette with the puck for size
and opacity, B / E / [ ] over the pad. The **Board** is a reference board in PureRef's way: images dropped, pasted or
dragged from the Project window onto an endless canvas, moved, scaled and turned by their handles, kept in
Assets/Boards and opened again next time (Drawing with References shows it beside the pad). The **Layers window** composites the sketch's layers as Photoshop does - opacity,
blend modes, groups, locks - with rows a pen can work and the Wheel's layers set a hold away. Tabs never clip (icons, then an overflow), related tabs
fold into one, and a stage layout shows its own tabs; the Scene view's bar reads Layout > Mode > Tool. **Sculpt Mode** (Modeling Tools > Sculpt) pushes, smooths and grabs a dense mesh with
brushes that touch only what is under them, each stroke one light undo step - ten brushes, symmetry, a lazy mouse, image alphas
and five stroke types; masks Ctrl paints (a delta undo step too), polygroups, and hidden faces that leave the render mesh; Dynamic
Subdivision: a Subdivision Surface the brush's cage drives, each dab recomputing only the points it reaches, bit for bit the full result; DynaMesh, a closed shell of even quads
at a chosen resolution from winding columns and surface nets, self-intersections merged, the mask kept; Quad Remesh
through QuadriFlow on a worker (ZRemesher's class of result, without guides), when the build has Blender's Eigen; Dyntopo, the
mesh tessellated under the brush as it moves (Sculptris Pro), one snapshot step a stroke. Mesh maps (normal, position,
curvature, ambient occlusion, thickness) baked from the mesh into its UV layout on a worker, Substance Painter's set. A
layer stack on an object's surface - fills, a mask as a stack of effects on every layer, generators reading the mesh
maps (edge wear, dirt, dust), anchors, groups, metallic and emission - redone inside the edited rectangle only. Painting
on the model by projection with a material brush (colour, roughness, metallic, height, emission in one dab), the far
side and hidden surfaces left alone, fills by polygroup, island, material or object, a stroke one undo step of the
tiles it touched. Smart materials (a saved group with its masks, seven built in) dropped onto any surface, texture
sets exported in the glTF, Unity, HDRP and Unreal packings, 2D painting on the UV canvas, stencils. The hand-painted look:
flat and bristle tips, wet mixing, jitter, grain, Smudge and Blend, a gradient fill and a palette, the lighting baked into a
layer to paint over, a Hand-painted shading model that shows the texture as painted, Posterize / Edge Shade / Cavity filters.
An HCI pass over the whole editor is under way (ADR 0085, `docs/hci/`): cognitive walkthroughs, a KLM table, a Fitts target
audit and a shortcut-label audit in the test suite, with toasts, a job tray, keymap-driven labels, unsaved-changes prompts, a
History panel and an accessibility theme to follow. Round 272 landed the first: every warning is a toast over the
Scene view with a Fix button where the remedy is a command, and a button that cannot work is disabled with its reason;
round 273 the second: one job tray in the status bar with every long job's progress, time run, estimate left and Cancel;
round 274 the next four: the status hint from a table that covers every mode, every shortcut label from the keymap preset,
"Save changes?" before a scene is replaced, and a History window of the undo steps. Round 275 took the modeling, placing and
UV findings: Ctrl's three meanings said in the hints, a snap-increment dropdown, keys for every wheel-only parameter, a UV
editor that folds its toolbar and takes its tool keys from the keymap, Hierarchy drop zones in thirds with a Move into... menu, clearer operation names, a
Boolean dialog that names the object kept. Round 276: the Environment edited in one window, every device choice read in one
section with Auto defined once, the rendering components in Add Component whatever the layout. Round 277 put compositing and materials within a pen's reach: the
render image pans with a left drag, a curve point goes by Alt+click, Assign to Selection is a button, Export Textures is a dialog. Round 278 gave animation its keymap actions (set a key, play
the clip, step to a key, Record) in every preset, 18 px key targets, a RECORD cue in the Scene view and a toast for a key asked for during Play. Round 279: Weight Paint's Add brush subtracts
with Ctrl or the pen's eraser like every other brush, and the skin binds to a skeleton chosen from a dropdown. Round 280: the console recalls its history with Up and Down and
answers an unknown command with the nearest known ones, a .anim asset opens the Animation window, the Wheel carries Set Key,
Record and Play Clip. Round 281: the welcome asks where you come from (Unity, Unreal, Blender, Photoshop & Substance) and
sets the keys and the feel; an Adobe keymap preset (B brush, E eraser, V move, [ ] size); [ ] step every brush; a High
Contrast theme measured against WCAG's 4.5:1; `keymap table`. Round 282: the HCI pass measured - the KLM estimate for 19
frequent actions 80.9 s -> 54.65 s, 0 wrong shortcut labels, every tool mode hinted, 24 failing walkthrough steps -> 7;
the think-aloud study is written but not yet run (`docs/hci/`). Round 283: the tests that failed only on CI (the dependency-free build, two
cores, sanitizers) or without LTCG pass there too. Rounds 284-290: Scatter's and Strokes' settings as dropdowns and buttons, a
pen-tappable step bar for the modal tools, Pan in the UV editor, Adjust Last for primitives, UV operators and Draw shapes, 3D Object
in every layout, time zoom in the Animation window, glTF export of a rig with its weights and clip, Okabe-Ito polygroups, the
eraser by shape, a CPU badge, 7 mm pen targets in Tablet Mode (asserted), and a ? beside the hint that opens the lesson.
Rounds 291-293: what only the Console could do in Sculpt, Paint and texturing has buttons, pickers and dialogs (New Surface, a
layer's Mask Preset and Save as Smart Material, a Bake dialog, Bucket Fill, palettes); refusals carry a Fix; results show in a
Report dialog.
Rounds 294-298: Live Link starts with Blendity and writes where it listens, and bridges for Blender, Unity, Godot and Unreal
connect by themselves (the Godot and Unreal ones written, their in-engine runs pending); the bridges as installable packages,
and Window > Live Link > Install a Bridge.
Rounds 299-305, Puppet Posing: drag a hand, a foot, the head or the hips and a pose prior trained on motion capture bends the
body, exact IK making the goals and the planted feet hold; any humanoid rig (Mixamo, UE5, Character Creator, Daz, VRoid, Biped,
Rigify; T or A pose); motion trails, onion skin, a Zoetrope, Reach, Motion Check with Fixes, a smooth brush, Lock in Place.
Rounds 306-310: the rest of the console-only verbs in menus, panels and Preferences - the sketch pen and canvases, Anime Look,
grass, reference images from the side and top, UV material lists, the render range, MoonRay and AI checks, contacts and
ragdoll events, gait files, vertex group and shape key pickers - and a test that every one stays reachable.
Rounds 311-315: the last console-only items in the interface; five interface conventions checked; a live sweep of every
screen with the fixes it found (labels that don't fit, narrow panes, contrast); a 118-task checklist with target times
(docs/hci/checklist.md); the Unity bridge run in Unity 6000.6. Round 350: a VRChat avatar, a VRChat world and a short
film made in Blendity and compared stage by stage with Blender, Unity, Godot and Unreal (docs/hci/compare-2026-10.md):
the export's missing shape keys and painted textures lead the 15 ranked findings.
Rounds 468-475, Pass 22: posing and playing an avatar cost what the avatar cost, whatever was moved; a mesh is skinned once a frame and only
when its bones moved, a posed mesh follows in place, the world's rows stay on the card, the full-body solver works on a pose buffer and the
scene is written once a frame (dragging an IK target on a 140,000-triangle stand-in 48 ms -> 10 ms; whole-body poses 35 to 53 ms, then skinning on the card (Pass 26, rounds 518-523: the chest 37 -> 7 ms, a Puppet handle 56 -> 12, Play 46 -> 10 on the stand-in alone;
in a world the chest 43 -> 11, R on the chest 57 -> 16, a Puppet handle 63 -> 21, Play 71 -> 25: the world's draw on the card is what is left).
Rounds 476-483, 494-502 and 513-515, Blockout: a mesh built from boxes, cylinders and spheres that add, cut or intersect and follow as they
move - rooms, handles, the Room tool, wedges, stairs, arches, outlines and any mesh as pieces, a Blend radius, materials by floor, wall and
ceiling, Bake to Mesh, outlines you click, Draw Piece with a Shape list that ends in Door and Window, styled windows and doors,
architectural pieces, snap and join between Blockouts, modifiers on the Blockout and on a piece, door leaves as objects - measured on 100- and
400-piece interiors (`blendity_stress --only blockout`) and taught by lessons.
Rounds 484-488, Pass 24: SideRail's look - Unity's materials keep their maps and gloss, the bake's specks, Generate Lighting on every
ticked graphics card.
Rounds 504-517, Pass 25, speed and usability: every part of the program has a timed row (`image_edit`, `sculpt_paint`, `sequencer`,
`importers`, `unity_export`, `asset_windows`, `cold_first_use`, `scene_views`) and the ranked list is `docs/perf/sweep-504.md`; the Scene
view of 50,000 cubes went from 33 ms a frame to 10 by keeping what the scene holds, the image editor's heavy sliders from 30-51 ms to 10,
a 4K picture opens in the Editing window on a worker, a 500k-face sculpt stroke and a bake's landing frame are shorter, and a Unity import,
`budget pc` and an export's folder walk leave the main thread. A live sweep of every screen in 12 configurations (three sizes, Dark and
Light, UI scale 1 and 1.5; `docs/hci/sweep-505.md`) found what was wrong and rounds 509, 510 and 513 fixed it: **the keyboard reaches every
control** (F6 into a window, F10 or Alt for the menus, then Tab, Space, Enter and the arrows; Tab stays Edit Mode until you ask), no control
lies on another, 42 of 49 cut labels are whole (the 7 left are named in `docs/hci/sweep-509.md`), every button has a tooltip that ends a sentence, every target is a row at mouse density and
7 mm at Tablet Mode's scale (`docs/hci/sweep-509.md`, `docs/requirements.md` U-01 to U-07). Round 511 measured it all again on the joined
tree (`docs/perf/verify-511.md`: what is still over budget, and why), round 512 brought these docs and both books up to
date, and round 517 made the unit checks that failed only on a busy machine check behaviour, their times now stress rows.
Stylized environments: brush strokes as geometry (cards and ribbons with canopy normals), grass and canopies from strokes, a
scatter brush with a painted mask, a sun disc and horizon band on the sky, distance fog in every renderer. The manual's
chapters and five Learn lessons with Try-it buttons walk the sculpt, paint and stylize pipelines in order.
