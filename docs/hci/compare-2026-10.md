# Blendity against Blender, Unity, Godot and Unreal: three productions (round 350, 2026-10-31)

**Asked (2026-10-31):** "Can we do another HCI analysis of the whole program, all tabs, several use cases. Comparing it
to ease of use with Blender, Unity3D, Godot, and Unreal?" - then: make "a VRChat World" and "a VRChat avatar"
("from pre-production, to modeling, and doing finishing renders, exporting, and showcasing"), and "a basic production
pipeline for an animation or short-film", because "the program needs to serve users making game assets, character
assets, and cinematic renders."

This round changes no product code. Its findings are ranked at the end for the user to choose from.

## Method

- **Blendity was used for real.** Each production was made in Blendity 0.12.0 with rounds 337-349, in a scratch project
  (`BLENDITY_PROJECT`, never the user's Assets), through its headless mode: every stage a fresh run with the commands of
  the stages before it, a 1920 x 1080 screenshot, and the Console's log split per command
  (`scratchpad/hci_prod.py`, the stage lists `hci_A.json`, `hci_B.json`, `hci_C.json`). The exported files were then
  measured (`hci_glb.py`, against VRChat's published limits).
- **What the console can't do was done by reading the GUI path in the code** (`file:line` given), and every refusal or
  silent no-op in the logs is recorded as it happened.
- **The other four programs were not installed or run** (no downloads). Their steps come from their own documentation,
  cited per row: Blender's manual, Unity's manual and package docs, Godot's docs, Epic's Unreal docs, VRChat's creator
  docs and wiki. Their counts read "per their docs".
- **KLM** (Card, Moran and Newell; the rules at the top of `klm.md`): ten tasks every pipeline has, the shortest
  documented expert path in each program's default keymap, Blendity in its Unity preset. `py tools/hci_klm.py
  docs/hci/compare-2026-10.md --compare --check` recounts every cell.
- **Nielsen's ten heuristics**, severity 0 (none) to 4 (catastrophe), for each finding.
- The audits were rerun: the coverage map and the consistency audit stay at their baselines (no code changed).

Limits: one analyst, an expert path; the console stands in for clicks where the GUI path was read from code; the four
other programs' times are what their documentation describes, not measured sessions.

## What was made, and what came out

| Production | Stages | Commands run | Refused or no-op | Output measured |
|---|---|---|---|---|
| A. VRChat avatar | 9 (board and papers, blocking, sculpt + remesh, UVs + texture set + toon, rig + skin + spring bones, visemes, checks, renders, export) | 75 | 9 on the first pass, 2 after an expert's corrections | `Avatar.glb`: 76,796 triangles (PC Good is 70,000; Quest Good 10,000), 4 meshes, 1 skinned, 4 material slots, 27 bones, 0 textures, 0 shape keys, 0 animations |
| B. VRChat world | 7 (mood board, a modular kit with real-size textures, scatter + grass + sky + fog, lightmaps + an emissive sign, optimization, a 1200 x 900 thumbnail, export) | 51 | 4 | `World.glb`: 8 meshes; the thumbnail render showed only the sky; the lightmap bake took 0 objects |
| C. Short film | 6 (beats + boards + animatic, layout with lenses, a walk and an acting beat, lights + fog + the anime filter, renders, the cut and a video) | 42 | 2 | A pose keyed at frame 30 by Puppet Posing; 24 frames path-traced as PNG; no video (no FFmpeg) |

Screenshots: `scratchpad/hci/A1.png` ... `C6.png` (the Artifact shows the key ones).

### Measured against VRChat

| Limit (PC Good / Quest Good) | The exported avatar | Verdict |
|---|---|---|
| Triangles 70,000 / 10,000 | 76,796 (the DynaMesh result: the Quad Remesh to 4,000 was still running on its worker when the export ran, and nothing said the export took the old mesh) | Poor on PC, Very Poor on Quest |
| Skinned meshes 2 / 1 | 1 | Good |
| Material slots 8 / 1 | 4 (the ground and the kit came along: Export All) | Good on PC, Poor on Quest |
| Bones 150 / 90 | 27 (Unity Humanoid's own names: Hips, Spine, Chest, Neck, Head, LeftUpperArm ...) | Excellent |
| Visemes (15 `vrc.v_*` keys) | 5 keys made in Blendity; **0 in the file** - neither the glTF nor the FBX exporter writes shape keys | **Blocker**: no lip sync, no blink |
| Textures | The painted texture set was written beside the file but not referenced by the .glb (only .png/.jpg files a material points at are embedded, `export.cpp:1142`) | **Blocker** for a one-file hand-off |
| Upright, 0.5-5 m | 2.24 m of mesh, the rig preset 1.7 m at the origin, not fitted to the mesh (Auto-Rig fits; the preset doesn't) | OK once fitted |

## Stage by stage

Steps (in brackets) are UI actions - a click, a key, a drag, a typed name - counted the shortest documented way; they are approximate ("10+" where a documented path branches), and only the KLM table below is timed. "In a DCC" means the
program's users make that stage in Blender or Maya and import it: the hand-off is counted, not held against it.

### A. VRChat avatar

| Stage | Blendity (as made) | Blender | Unity (+ VRChat SDK) | Godot | Unreal | Blendity's friction |
|---|---|---|---|---|---|---|
| A1 Pre-production | Board: New, Add Image, papers on the board (4) | Reference images as empties, or the Storyboarding template's 2D workspace (4) [B1] | In a DCC / PureRef | In a DCC | In a DCC | None in the run. Papers made on a board open in the Draw tab: better than any of the four |
| A2 Blocking | GameObject > 3D Object (no Capsule), Position, Join (7) | Shift+A, G/S, Ctrl+J (6) | ProBuilder (package) | CSG nodes (prototype only) | Modeling Mode shapes (6) [U1] | `create Capsule` made an **empty object with no warning**; a new object takes a name already in the scene ("Sphere" twice) so `select` picks the old one; Join needs the active object (selected last) to be a mesh: Select All picked a light last and Join refused |
| A3 Sculpt, retopology | Sculpt Mode, pressure, DynaMesh, Quad Remesh in the job tray (5) | Sculpt Mode, Voxel Remesh, QuadriFlow (5) | - | - | Sculpt in Modeling Mode (6) | The remesh runs on a worker; nothing stops an export from taking the mesh before it (A9) |
| A4 UVs, texturing, toon | Smart UV, New Surface, a smart material, Paint, Toon style, Outline (9) | U > Smart UV Project; Texture Paint; nodes for toon (12+) [B2, B3] | Shader Graph toon (in Unity) | - | Material editor | Fill Island and every dab need a point on the model (a click); smart materials need a surface first, said by the refusal with its fix |
| A5 Rig, skin, spring bones | Humanoid Rig, Skin modifier, Bind, Weight Paint, Add Spring Bones (8) | Add Armature (or Rigify), Ctrl+P > With Automatic Weights, Weight Paint (8) [B4] | Rig tab > Humanoid > Configure, Automap (6) [U2]; PhysBones (SDK) | Import; Skeleton3D retarget | Import; Skeletal Mesh Editor | The preset rig isn't fitted to the mesh; the Skin's Armature is a typed name (or a drop); the bind runs on a worker. The bone names are Unity Humanoid's, so Unity's Automap maps them all |
| A6 Visemes, blink | Inspector > Add Shape Key, sculpt it; weights in the MeshRenderer (2 per key + the sculpt) | Shape Keys +, edit, name (3 per key) [B5] | Avatar Descriptor > Lip Sync > Auto Detect by `vrc.v_*` names [V1, V2] | - | Morph targets from the DCC | **No VRChat viseme preset** (15 names typed by hand) and **no export of shape keys** |
| A7 Checks | `stats`, Overlaps (2) | Statistics overlay (1) | The SDK's Build panel lists the Performance Rank (1) | - | - | **No performance check against VRChat's ranks** (or any target budget) |
| A8 Renders | F12, Save Render; Render Animation (4) | F12, Ctrl+F12 (2) | Recorder (9) [U3] | Movie Maker (2) [G1] | MRQ (6) [R1] | `saverender` ignores the path typed after it; a new camera faces +Z, not the view (the GUI's Align to View fixes it) |
| A9 Export | File > Export > FBX / glTF (4) | File > Export > FBX (4) | (import) drag in + Rig Humanoid (5) | (import) glTF (2) [G2] | (import) FBX (3) | The export path is relative to the working folder, not the project; Export All took the ground and the kit |

### B. VRChat world

| Stage | Blendity (as made) | Blender | Unity | Godot | Unreal | Blendity's friction |
|---|---|---|---|---|---|---|
| B1 Mood board, layout | Board, a paper (3) | Image empties / Grease Pencil (4) | In a DCC | In a DCC | In a DCC | - |
| B2 Modular kit, real-size textures | Cubes on the grid, duplicate, New Texture (Bricks, Wood Floor), Physical Size (8) | Modeling, snapping, UVs by hand (10+) | ProBuilder + grid snap (8) | GridMap from a MeshLibrary (6) [G3] | Modeling Mode + grid snap (8) | The textures were made but assigning them is another step per material; `set` can't reach a Transform's fields and the console has no rotate or scale verb |
| B3 Set dressing | Scatter (pick, density, paint), Strokes grass, Sky, Fog (7) | Geometry Nodes scatter or particles (10+) | Terrain trees/details; Polybrush (8) | MultiMesh by script; add-ons | Foliage Mode, Shift+3, paint (4) [R2] | Scatter needs a dab on the surface (a click: fine in the GUI) |
| B4 Lighting | Contribute GI, Lighting > Generate Lighting (6) | No lightmapper for games | Static + Generate Lighting (6) [U4] | Import UV2 + LightmapGI + Bake (8) [G4] | Build Lighting (2) | **The bake took 0 objects and said only "0 objects, 0 texels"**: Contribute GI is off by default, as Unity's Static is, but neither says why nothing baked |
| B5 Optimization | Decimate modifier for LODs by hand; no collider component; `stats` (5+) | Decimate per LOD; UCX_ naming for Unreal (8) | LOD Group, Box Collider (6) [U5] | -col / -colonly suffixes on import (2) [G5] | LODs auto on import; collision on import (2) | **No LOD helper, no collision mesh or naming** (VRChat worlds need both, built in Unity); no draw-call / material count against a budget |
| B6 Thumbnail 1200 x 900 | Camera, F12 (4) | Output size, F12 (4) | Recorder or a screenshot script (9) | Window size + screenshot by script | High Res Screenshot (5) | The render size isn't a console field (`set Render.Width` refused); the new camera faced away and the render was sky |
| B7 Export | File > Export (4) | File > Export (4) | (import) | (import) | (import) | No second UV set (lightmap UVs) in the glTF/FBX; Unity and Godot can make their own on import |

### C. Short film

| Stage | Blendity (as made) | Blender | Unity | Godot | Unreal | Blendity's friction |
|---|---|---|---|---|---|---|
| C1 Beats, boards, animatic | Board + papers; Sequencer: New (5) | Storyboarding template: a scene per shot on the Sequencer (4) [B6] | Timeline + images (8) | AnimationPlayer (8) | Sequencer + image tracks (8) | **Papers can't go on the Sequencer**, and the Sequencer has no audio track: no animatic with scratch audio |
| C2 Layout | Cameras, Focal Length, Active Camera (6) | Add Camera, lens, Ctrl+Numpad0 (5) | Cinemachine cameras on a Timeline track (6) [U6] | Camera3D nodes (5) | Camera button in Sequencer: a Cine Camera and a cut track (2) [R3] | Shots are cut by Render Camera Sequence, not by a cut track you edit |
| C3 Animation | Humanoid Rig, Puppet Posing (a drag poses the body), keys (5) | Rigify, IK, keying (10+) | Animation window (8) | AnimationPlayer (8) | Control Rig (8) | **Walk Cycle is off by default** (Preferences > Animation Assistants) - its refusal does give a Fix button. Puppet Posing: a whole-body pose in one drag, which none of the four has |
| C4 Lighting, look | Spot light, fog, the Anime filter on the camera (5) | Lights, volume, compositor (8) | URP/HDRP volume (8) | Environment + shaders (8) | Post Process Volume (6) | - |
| C5 Rendering | Path tracer, samples, Render Animation (4) | Cycles, Ctrl+F12 (3) | Recorder (9) | Movie Maker (2) | MRQ (6) [R1] | Renders run on a worker: fine in the GUI |
| C6 Edit, deliver | Sequencer clips, Render Camera Sequence; **video needs FFmpeg on the PATH** (5 + an install) | Video Sequencer, FFmpeg built in (5) | Timeline + Recorder Movie (H.264) [U3] | Movie Maker AVI (2) [G1] | MRQ + an encoder (6) | **No video without installing FFmpeg** (Blender, Unity and Godot write video themselves) |

## KLM: ten tasks in five programs

Seconds by the operators (rules in `klm.md`); "-" where the program doesn't do it (made in a DCC, or not documented).

| # | Task | Blendity | s | Blender | s | Unity | s | Godot | s | Unreal | s |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | Add a cube | M, P, BB (GameObject), P (3D Object), P, BB (Cube) | 5.05 | M, H, 2K (Shift+A), H, P (Mesh), P, BB (Cube) | 4.95 | M, P, BB (GameObject), P (3D Object), P, BB (Cube) | 5.05 | M, H, 2K (Ctrl+A), M, 6K (MeshIn), K (Enter), H, P, BB (Mesh), P, BB (New BoxMesh) | 7.9 | M, P, BB (Quick Add), P (Shapes), P, BB (Cube) | 5.05 |
| 2 | Assign a material to an object | M, P, B, P, B (drag the material onto it) | 3.75 | M, P, BB (Material Properties), P, BB (Browse), P, BB (the material) | 5.25 | M, P, B, P, B (drag from Project) | 3.75 | M, P, B, P, B (drag onto Surface Material Override) | 3.75 | M, P, B, P, B (drag from the Content Browser) | 3.75 |
| 3 | Bind a mesh to its skeleton with automatic weights | M, P, BB (the mesh), P, BB (Skeleton), P, BB (the armature), P, BB (Bind) | 6.55 | M, P, BB (the mesh), P, K, BB (Shift+click the armature), 2K (Ctrl+P), P, BB (With Automatic Weights) | 5.85 | - | - | - | - | - | - |
| 4 | Add a shape key (blend shape) | M, P, BB (Add Shape Key) | 2.65 | M, P, BB (Object Data), P, BB, BB (+ twice: Basis, then the key) | 4.15 | - | - | - | - | - | - |
| 5 | Key the selected object's transform | M, H, 2K (Shift+K) | 2.15 | M, H, K (I) | 1.95 | M, H, K (K, the Animation window focused) | 1.95 | M, P, BB (the key icon beside the property) | 2.65 | M, H, K (S in Sequencer) | 1.95 |
| 6 | Render a still from the camera | M, H, K (F12) | 1.95 | M, H, K (F12) | 1.95 | M, P, BB (Window), P (General), P (Recorder), P, BB (Recorder Window), P, BB (Add Recorder), P, BB (Image Sequence), P, BB (Start Recording) | 10.05 | - | - | M, P, BB (the viewport menu), P, BB (High Resolution Screenshot), P, BB (Capture) | 5.25 |
| 7 | Render the animation to a video file | M, H, 2K (Ctrl+F12; FFmpeg must be installed first) | 2.15 | M, P, BB (Output Properties), P, BB (File Format), P, BB (FFmpeg Video), M, H, 2K (Ctrl+F12) | 7.4 | M, P, BB (Window), P (General), P (Recorder), P, BB (Recorder Window), P, BB (Add Recorder), P, BB (Movie), P, BB (Start Recording) | 10.05 | M, P, BB (Movie Maker), M, H, K (F5) | 4.6 | M, P, BB (Render), P, BB (Movie Render Queue), P, BB (+ Setting), P, BB (an encoder), P, BB (Render (Local)) | 7.85 |
| 8 | Bake the level's lightmaps (one object marked) | M, P, BB (Contribute GI), M, P, BB (Window), P, BB (Lighting), P, BB (Generate Lighting) | 7.9 | - | - | M, P, BB (Static), M, P, BB (Window), P (Rendering), P, BB (Lighting), P, BB (Generate Lighting) | 9 | M, P, BB (Static Lightmaps), P, BB (Reimport), M, H, 2K (Ctrl+A), M, 9K (LightmapGI), K (Enter), H, P, BB (Bake Lightmaps) | 11.15 | M, P, BB (Build), P, BB (Build Lighting Only) | 3.95 |
| 9 | Export the character as FBX | M, P, BB (File), P (Export), P, BB (FBX), P, BB (Export) | 6.35 | M, P, BB (File), P (Export), P, BB (FBX), P, BB (Export FBX) | 6.35 | - | - | - | - | - | - |
| 10 | Find any command by name | M, H, 2K (Ctrl+K), 6K (a word), K (Enter) | 3.55 | M, H, K (F3), 6K, K (Enter) | 3.35 | M, H, 2K (Ctrl+K), 6K, K (Enter) | 3.55 | M, H, 3K (Ctrl+Shift+P), 6K, K (Enter) | 3.75 | - | - |

**Reading it:** where both do the task, Blendity is within 0.2 s of Blender on the modeling and keying rows (1, 3,
5, 9, 10) and faster where Blender goes through a properties tab (2, 4, 7). Against the engines it matches Unity's
drag-and-drop (2) and beats Unity's and Unreal's stills and videos (6, 7) - the engines render through a recorder.
Unreal's one-menu lighting build (8) is the fastest of all. Row 7's 2.15 s holds only once FFmpeg is installed.

## Verdict per audience

| Coming from | Modeling, sculpting, UVs, texturing | Rigging and animation | Rendering | Getting it into the engine |
|---|---|---|---|---|
| Blender | Same: the operators and many shortcuts match (Blender preset) | Easier: Puppet Posing, Motion Check, FK / IK matched | Same; harder for video (FFmpeg) | **Harder**: no shape keys or painted textures in the export |
| Unity | Easier than Unity alone (it has none); the Unity preset keeps its keys | Easier than Unity's Animation window | Easier (F12 vs the Recorder) | **Harder for avatars** (visemes lost); same for props |
| Godot | Easier (Godot models nothing beyond CSG) | Easier | Easier | Same (glTF is Godot's own route) |
| Unreal | Easier than Modeling Mode for organic work | Same as Control Rig for posing; easier for a first walk | Same | Harder for worlds: Unreal makes LODs and collision on import, Blendity hands neither over |

## Heuristic findings (Nielsen), ranked

Severity 0-4 by how many users it stops and how often. Audiences: B Blender, U Unity, G Godot, R Unreal.

| # | Finding | Heuristic | Sev. | Hits | Where | Fits |
|---|---|---|---|---|---|---|
| 1 | Shape keys aren't exported (glTF morph targets, FBX blend shapes): visemes, blinks and expressions are lost on the way to VRChat or any engine | 7 Flexibility / task fit | 4 | B U G R | `scene/export.cpp` (no `targets`) | New task |
| 2 | A painted texture set isn't referenced by the .glb (only image files a material points at are embedded) | 1 Visibility / 5 Error prevention | 3 | U G R | `export.cpp:1142` | New task (or Export Textures' `--assign` offered at export) |
| 3 | No VRChat (or any) performance check: triangles, materials, bones, shape-key names against a chosen budget | 7 Flexibility | 3 | U | - | New task: a Budget panel with VRChat PC / Quest presets |
| 4 | No viseme preset: the 15 `vrc.v_*` names typed by hand, nothing says one is missing or moves nothing (VRChat's `pp` missing breaks lip sync) | 6 Recognition over recall | 3 | U | `panels.cpp:3875` | New task |
| 5 | An export taken while a remesh still runs on its worker says nothing (the old mesh went out) | 1 Visibility / 5 Error prevention | 3 | all | job tray, export | New task: export waits or warns |
| 6 | No video without installing FFmpeg | 7 Flexibility | 3 | B U G | `render_animation.cpp` | New task (bundle or a download offer) |
| 7 | `create <unknown>` makes an empty object without a word (`create Capsule`); no Capsule primitive | 9 Error recovery | 2 | U | `editor.cpp:3276` | Small fix |
| 8 | New objects take names already in the scene (two "Sphere"s); `select` then finds the old one | 4 Consistency (Unity: "Sphere (1)") | 2 | U R | `editor.cpp` create | Small fix |
| 9 | Generate Lighting with nothing marked says "0 objects, 0 texels" and no reason | 9 Help users recognize errors | 2 | U G | `lighting.cpp:296` | Small fix: name Contribute GI, with a Fix |
| 10 | No LOD helper and no collision mesh or naming for engines | 7 Flexibility | 2 | U R | - | New task |
| 11 | Papers can't be placed on the Sequencer; no audio track: no animatic with scratch audio | 7 Flexibility | 2 | B R | `sequencer_window.cpp` | New task |
| 12 | Walk Cycle is off by default (its refusal has a Fix button) | 6 Recognition | 1 | all | `assist_prefs.cpp` | Consider on by default |
| 13 | The Console's `help` lists about 25 of ~200 verbs; there is no `save`/`open` scene verb; `set` can't reach Transform; no rotate or scale verb; `saverender` ignores a path; a relative export path is the working folder's, not the project's | 10 Help and documentation / 4 Consistency | 2 | scripting users | `editor_commands.cpp:1192` | One console round |
| 14 | The humanoid preset rig isn't fitted to a selected mesh (Auto-Rig is) | 6 Recognition | 1 | U | `ik.cpp:400` | Small: fit to the selection's bounds |
| 15 | Join refuses when the last-selected object isn't a mesh, after Select All | 5 Error prevention | 1 | B U | `object_ops.cpp` | Small: take the last mesh |

**Fixed in this pass** (found by making the film, a bug in round 347's own line): after Puppet Posing drew the left hand
across the chest, the Inspector said "The left hand is on the right" on Blendity's own humanoid preset. Round 349
(#248) compares the shoulders and the hips as the rig stood.

## What Blendity does better than all four

- **One program from the board to the render:** papers on a reference board open in the Draw tab; the same file is
  modeled, sculpted, painted, rigged, animated and rendered. The engines need a DCC; Blender has no reference board.
- **Puppet Posing:** a whole-body pose from one drag, with balance; FK / IK matched both ways (round 348).
- **F12 in an engine-style editor:** Unity and Unreal render stills and video through a recorder (KLM rows 6, 7).
- **Unity Humanoid's bone names out of the box:** the preset maps in Unity's Automap without a Configure pass.

## Already fixed elsewhere

Checked against `walkthrough-*.md` and `sweep-313.md`: none of the 15 findings above was on those lists (the earlier
walkthroughs ended at the export without measuring what the file carried).

## Sources

- [B1] Blender: Storyboarding template - https://docs.blender.org/manual/en/latest/video_editing/storyboarding/app_template.html
- [B2] Blender: UV unwrapping - https://docs.blender.org/manual/en/2.90/modeling/meshes/uv/unwrapping/index.html
- [B3] Blender: Texture Paint brush (pressure) - https://docs.blender.org/manual/en/2.81/sculpt_paint/texture_paint/tool_settings/brush.html
- [B4] Blender: Armature Deform parent with automatic weights - https://docs.blender.org/manual/en/dev/animation/armatures/skinning/parenting.html
- [B5] Blender: Shape keys workflow - https://docs.blender.org/manual/en/latest/animation/shape_keys/workflow.html
- [B6] Blender: Render output (FFmpeg) - https://docs.blender.org/manual/nl/dev/render/output/properties/output.html
- [U1] Unreal: PolyGroup Edit in Modeling Mode - https://dev.epicgames.com/documentation/en-us/unreal-engine/polygroup-edit-tool-in-unreal-engine
- [U2] Unity: Configuring the Avatar (Humanoid) - https://docs.unity3d.com/es/Manual/ConfiguringtheAvatar.html
- [U3] Unity: Recorder - https://docs.unity3d.com/Packages/com.unity.recorder@4.0/manual/index.html
- [U4] Unity: Lightmapping - https://docs.unity3d.com/6000.6/Documentation/Manual/progressive-lightmapper.html
- [U5] Unity: LOD Group - https://docs.unity3d.com/Manual/lod-group-configure.html
- [U6] Unity: Cinemachine and Timeline - https://docs.unity3d.com/Packages/com.unity.cinemachine@3.1/manual/setup-timeline.html
- [G1] Godot: Movie Maker mode - https://godotengine.org/article/movie-maker-mode-arrives-in-godot-4
- [G2] Godot: 3D formats (glTF, .blend) - https://docs.godotengine.org/en/4.2/tutorials/assets_pipeline/importing_3d_scenes/available_formats.html
- [G3] Godot: GridMaps - https://docs.godotengine.org/en/4.2/tutorials/3d/using_gridmaps.html
- [G4] Godot: LightmapGI - https://docs.godotengine.org/en/stable/tutorials/3d/global_illumination/using_lightmap_gi.html
- [G5] Godot: import hints (-col, -colonly) - https://docs.godotengine.org/en/stable/tutorials/assets_pipeline/importing_3d_scenes/node_type_customization.html
- [R1] Unreal: Movie Render Queue - https://dev.epicgames.com/documentation/unreal-engine/rendering-high-quality-frames-with-movie-render-queue-in-unreal-engine?lang=en-US
- [R2] Unreal: Foliage Mode - https://dev.epicgames.com/documentation/en-us/unreal-engine/foliage-mode-in-unreal-engine
- [R3] Unreal: Camera Cut track - https://dev.epicgames.com/documentation/en-us/unreal-engine/cinematic-camera-cut-track-in-unreal-engine
- [V1] VRChat: Avatar Performance Ranking - https://creators.vrchat.com/avatars/avatar-performance-ranking-system/
- [V2] VRChat wiki: Visemes - https://wiki.vrchat.com/wiki/Visemes ; Avatar Descriptor - https://wiki.vrchat.com/wiki/Avatar_Descriptor
- [V3] VRChat: making a world public - https://creators.vrchat.com/worlds/submitting-a-world-to-be-made-public/
