# Blendity test checklist

Every feature, as an artist reaches it. Time each task from your first move to the result; a task over 1.5x its target is marked slow. Targets are an expert's times from the keystroke-level model (docs/hci/klm.md) or estimates on the same rules; the first time through, expect to be slower.

The interactive page (tick, time, export) is built from the same data: `py tools/hci_checklist.py --html <file>`.

## A. Getting around and Preferences

| # | Task | Steps | Expected | Target | Done | Time |
|---|---|---|---|---|---|---|
| A1 | Switch to the Animation layout and back to Default | Layout dropdown (top right) > Animation; then > Default | Windows rearrange; the guide strip says the layout's purpose | 4 s | [ ] | |
| A2 | Open a window that isn't shown | Window > Live Link | Live Link opens as a tab beside the Hierarchy | 3 s | [ ] | |
| A3 | Change the editor feel to Blender and back | Edit > Editor Feel > Blender; then > Unity | MMB orbits in Blender's feel; RMB + WASD flies in Unity's | 6 s | [ ] | |
| A4 | Set the theme to Light, then High Contrast, then Dark | Edit > Preferences > Theme | All text readable in each; dim text still legible | 8 s | [ ] | |
| A5 | Raise the UI scale to 1.5 | Edit > Preferences > UI Scale 1.5 | Everything larger; long Preferences scroll; labels end in ... with tooltips where cut | 5 s | [ ] | |
| A6 | Turn layout guides off and on | Edit > Preferences > Layout guides | The guide strip under the tabs hides and shows | 4 s | [ ] | |
| A7 | Find any command by name | Ctrl+K, type unwrap, Enter | The selected mesh is unwrapped | 3.5 s | [ ] | |
| A8 | Pin a command to Quick Favorites | Ctrl+K, right-click a row > Add to Quick Favorites; Shift+Q | The command shows in Quick Favorites | 6 s | [ ] | |
| A9 | Undo and redo three edits | Move a cube three times; Ctrl+Z x3; Ctrl+Y x3 | Each step back and forward; Edit > History lists them | 6 s | [ ] | |

## B. Navigation and selection

| # | Task | Steps | Expected | Target | Done | Time |
|---|---|---|---|---|---|---|
| B1 | Frame the selected object | Select Cube, F | The Scene view centres and fits the cube | 2 s | [ ] | |
| B2 | Orbit, pan and zoom with the mouse (Unity feel) | Alt+LMB orbit, MMB pan, wheel zoom | Smooth, no jumps | 5 s | [ ] | |
| B3 | Orbit with the pen only | Drag the Scene Gizmo; drag the hand below it to pan | The view follows the pen | 5 s | [ ] | |
| B4 | Box-select three objects | Drag a rectangle over Cube, Sphere and Cylinder | Three selected; the Inspector shows multi-edit | 3 s | [ ] | |
| B5 | Snap a move to the grid | Hold Ctrl while moving the cube | Steps of the Move snap (0.25 m) | 4 s | [ ] | |
| B6 | Look through the top view and back | Click the gizmo's Y; then Persp | An orthographic top view; perspective again | 3 s | [ ] | |

## C. Hierarchy and Inspector

| # | Task | Steps | Expected | Target | Done | Time |
|---|---|---|---|---|---|---|
| C1 | Parent the Sphere under the Cube | Drag Sphere onto Cube in the Hierarchy | Sphere indents under Cube; moving Cube moves it | 3 s | [ ] | |
| C2 | Rename an object | Double-click it in the Hierarchy, type, Enter | The new name everywhere | 5 s | [ ] | |
| C3 | Add a component by search | Inspector > Add Component, type light, Enter | A Light component on the object | 5 s | [ ] | |
| C4 | Edit a field on three objects at once | Select three, change Position Y | All three move; a differing field shows a dash | 5 s | [ ] | |
| C5 | Pin the Inspector and select something else | Inspector > Pin; click another object | The Inspector keeps the pinned object | 3 s | [ ] | |
| C6 | Pick a modifier's vertex group from the list | Add a Smooth modifier on a mesh with groups; Vertex Group dropdown | The mesh's groups listed; picking one limits the smoothing | 3.75 s | [ ] | |
| C7 | Add and remove a shape key | Inspector > MeshRenderer > Add Shape Key; Remove... > Key 1 | BlendShape/Key 1 appears, then goes | 6 s | [ ] | |

## D. Modeling and Edit Mode

| # | Task | Steps | Expected | Target | Done | Time |
|---|---|---|---|---|---|---|
| D1 | Add a cube and bevel its edges | GameObject > 3D Object > Cube; Tab; 2 (edges); select all; Bevel | Rounded edges; F9 adjusts the last operation | 10 s | [ ] | |
| D2 | Extrude a face | Edit Mode, 3 (faces), click a face, E, drag | A new face pulled out | 5 s | [ ] | |
| D3 | Loop cut | Edit Mode, Ctrl+R over an edge ring, click | A loop across the ring | 4 s | [ ] | |
| D4 | Mirror a half model along Y | Modeling Tools > Mirror Y | The mirrored half added, faces doubled | 3 s | [ ] | |
| D5 | Add a Subdivision Surface modifier | Add Component > Subdivision Surface, Levels 2 | Smooth surface, cage kept | 6 s | [ ] | |
| D6 | Boolean difference | Select cutter then target; Modeling Tools > Difference | The cutter's shape removed | 5 s | [ ] | |
| D7 | Clear imported custom normals | Modeling Tools > Clear Custom Normals | Shading follows the faces and smooth angle | 3 s | [ ] | |
| D8 | Undo a bevel's segments in F9 | After a bevel, F9; change Segments to 4 | The bevel redone with 4 segments | 5 s | [ ] | |
| D9 | Add a guide line by numbers | Edit Mode tools > Guide Through X/Y/Z, Guide Direction > Add Guide Line | A construction line appears | 8 s | [ ] | |

## E. Drawing, sketch and Push-Pull

| # | Task | Steps | Expected | Target | Done | Time |
|---|---|---|---|---|---|---|
| E1 | Draw a rectangle on the ground and push-pull it | Mesh > Draw > Rectangle; click twice; P, drag | A box from the rectangle | 8 s | [ ] | |
| E2 | Sketch with the pen in red | Mesh > Draw > Pen Color > Red; draw a stroke | A red stroke, corrected to a line or curve | 5.95 s | [ ] | |
| E3 | Steady a shaky line | Mesh > Draw > Steadiness > Strong; draw | The line trails the pen and comes out smooth | 5 s | [ ] | |
| E4 | Draw on the ground plane | Mesh > Draw > Canvas > The Ground; draw | The stroke lies on the ground in 3D | 5 s | [ ] | |
| E5 | Three-point perspective guide | Mesh > Draw > Guides > 3-Point Perspective; draw lines | Lines snap to the vanishing points | 6 s | [ ] | |
| E6 | Lift a corner's three lines to 3D | Draw three lines toward the axes; Mesh > Draw > Lift the Sketch to 3D | A Scaffold wire object | 4.85 s | [ ] | |
| E7 | Clear the sketch | Mesh > Draw > Clear the Sketch; Ctrl+Z | Strokes gone, then back | 3 s | [ ] | |
| E8 | Build a box from three lines | Mesh > Draw > Build from Sketch > Box | A box mesh from the sketched corner | 5 s | [ ] | |

## F. UV

| # | Task | Steps | Expected | Target | Done | Time |
|---|---|---|---|---|---|---|
| F1 | Smart UV project | UV Unwrapping layout > Smart UV | Islands packed in 0-1, no overlaps | 3 s | [ ] | |
| F2 | Mark seams from sharp edges and unwrap | UV editor > Seams from Sharp | Seams on sharp edges, unwrapped | 3 s | [ ] | |
| F3 | Part Unwrap at a looser bound | UV editor > Parts > 1.5; then Part Unwrap | Fewer charts; the options alone don't unwrap | 6 s | [ ] | |
| F4 | Edit only two materials' UVs, the others hidden | UV editor > material list > tick two; Others Hidden | Only their faces shown and editable | 6 s | [ ] | |
| F5 | Rotate selected UVs by 90 | Select an island; type 90 in the field; Rotate | The island turns 90 degrees | 5 s | [ ] | |
| F6 | Check stretching | UV editor > Stretch | Faces coloured blue / green / red by distortion | 2 s | [ ] | |
| F7 | UV editor in a narrow window | Shrink the UV editor below about 700 px | Its right-hand controls move to their own row; nothing overlaps | 3 s | [ ] | |

## G. Materials, textures, Paint and Sculpt

| # | Task | Steps | Expected | Target | Done | Time |
|---|---|---|---|---|---|---|
| G1 | Give an object a new material and change its color | Inspector > Material > New; Base Color | The object recolours | 6 s | [ ] | |
| G2 | Add a surface for painting | GameObject > New Surface | A texture stack on the object | 3 s | [ ] | |
| G3 | Bake mesh maps | GameObject > Bake Mesh Maps...; size 1024; Bake | AO, curvature and thickness baked | 6 s | [ ] | |
| G4 | Add an edge-wear mask preset | Textures > a layer's menu > Mask Preset > edgewear | Wear on the edges | 6 s | [ ] | |
| G5 | Read one layer's mask from another (Anchor) | Name a layer; add an Anchor to another's mask; pick the name from Layer | The anchored mask reused | 8 s | [ ] | |
| G6 | Save a layer group as a smart material | Layer menu > Save as Smart Material... | A free name offered; it appears in the shelf | 8 s | [ ] | |
| G7 | Paint a stroke on the model | Paint section > Paint; drag over the model | Paint follows the surface | 4 s | [ ] | |
| G8 | Bucket-fill an island | Paint > Bucket Fill (Island); click the model | That island filled; a miss fills nothing | 4 s | [ ] | |
| G9 | Sculpt with Clay, then smooth | Sculpt > Clay; drag; Shift-drag | Clay builds; Shift smooths | 5 s | [ ] | |
| G10 | Make the sculpt brush bigger by one step | ] | Radius up a quarter | 1.95 s | [ ] | |
| G11 | Export the textures to a folder | Textures > Export Textures...; type or pick a folder | PNG maps written | 8 s | [ ] | |

## H. Stylized

| # | Task | Steps | Expected | Target | Done | Time |
|---|---|---|---|---|---|---|
| H1 | Give an object the anime rock look | GameObject > Anime Look > Rock | Flat-toned toon rock | 4 s | [ ] | |
| H2 | Paint grass cards | Strokes > Grass Density / Height / Radius; Grass under the View's Middle | Upright blades on the surface | 6 s | [ ] | |
| H3 | Start a second bush | Strokes > New Stroke Object; paint | A new Stroke object holds the new cards | 3 s | [ ] | |
| H4 | Fill a canopy | Strokes > Canopy Centre from Strokes; Fill Canopy | A round shell of cards | 5 s | [ ] | |
| H5 | Scatter rocks with another seed | Scatter > Source; Seed 7; dab | A different arrangement for the same dab | 6 s | [ ] | |
| H6 | Sun disc and fog | Lighting > Sky: sun disc; fog on | A sun in the sky; distance fog | 8 s | [ ] | |

## I. Lighting and rendering

| # | Task | Steps | Expected | Target | Done | Time |
|---|---|---|---|---|---|---|
| I1 | Aim a light at a point by numbers | Select the light; Inspector > Aim X/Y/Z; Aim at These Numbers | The light turns toward the point | 8 s | [ ] | |
| I2 | Bake lighting | Lighting > Generate Lighting | Lightmaps baked in the background; the view stays responsive | 3 s | [ ] | |
| I3 | Render an image | F12 | The Render window shows the frame | 2 s | [ ] | |
| I4 | Render frames 30-40 at 12 fps | Render window > First 30, Last 40, Frame Rate 12; Render Animation | 11 PNGs and an MP4 | 10 s | [ ] | |
| I5 | Render a camera sequence to a folder | Render window > Sequence Folder; Render Camera Sequence | One image per camera in that folder | 8 s | [ ] | |
| I6 | Path-traced preview | Render window > Preview with the path tracer | Noise clears progressively | 4 s | [ ] | |
| I7 | Develop a render (exposure, curve) | Editing window > Render; adjust Exposure | The picture brightens; Save keeps an .edit | 8 s | [ ] | |
| I8 | Point Blendity at your own ffmpeg | Preferences > FFmpeg; type the path, Enter | Render Animation uses it | 8 s | [ ] | |
| I9 | MoonRay: check it works (needs MoonRay) | Preferences > Renderers > Check | A test render's result and time | 4 s | [ ] | |

## J. Rigging, skinning and hair

| # | Task | Steps | Expected | Target | Done | Time |
|---|---|---|---|---|---|---|
| J1 | Rig a humanoid | Rigging > Humanoid Rig | A skeleton with IK on hands and feet | 4 s | [ ] | |
| J2 | Auto-rig from markers | Rigging > Auto-Rig; place the markers; Build | A fitted skeleton | 30 s | [ ] | |
| J3 | Skin a mesh with automatic weights | Select the mesh and the armature; Bind | The mesh follows the bones | 5 s | [ ] | |
| J4 | Clean small weights | Rigging > Weight Paint; Clean Below 0.05; Clean | Weights under 0.05 become 0 | 6 s | [ ] | |
| J5 | Rig hair from a mesh with 4 locks of 2 bones | Rigging > Rig Hair from Mesh...; Locks 4, Bones a Lock 2; Rig | Spring-bone chains, the mesh skinned | 8 s | [ ] | |
| J6 | Add spring bones (jiggle) | Select bones; Add Component > Spring Bones | Bones sway in Play | 6 s | [ ] | |

## K. Animation, Puppet Posing, physics and AI

| # | Task | Steps | Expected | Target | Done | Time |
|---|---|---|---|---|---|---|
| K1 | Key a position at frame 0 and 30 | Animation window > New clip; move; Shift+K at 0 and 30 | Two keys; the object moves between them | 8 s | [ ] | |
| K2 | Pose a reach by the hand (Puppet Posing) | AutoPosing on; drag the hand's dot | The body follows, feet planted | 3.75 s | [ ] | |
| K3 | Pose with the learned prior (local AI install) | Puppet > Use the Pose Prior; drag the hand toward the knee | The spine bends naturally; the hand reaches exactly | 4 s | [ ] | |
| K4 | Lock a foot | Select the foot bone; Puppet > Lock the Selected Bone | It stays put while the body moves | 3 s | [ ] | |
| K5 | Mark a contact on the foot at the playhead | Puppet > Contacts > Add One Here | A bar under the ruler, 10 frames | 4.85 s | [ ] | |
| K6 | Find and fix a foot slide | Puppet > Motion Check; a row's Fix | The slide gone; the report one row shorter | 6.3 s | [ ] | |
| K7 | Motion trails and onion skin | Puppet > Motion Trails; Onion Skin | A hand's path with key dots; ghosts before and after | 4 s | [ ] | |
| K8 | Pose a neighbouring frame in the Zoetrope | Puppet > Zoetrope; drag a hand in the +2 cell | That frame keyed, the playhead unmoved | 5 s | [ ] | |
| K9 | Type a key's slopes | Curves view; right-click a key; In 0.5 | The key goes Broken with that slope | 6 s | [ ] | |
| K10 | AI Inbetween a span (local AI install) | AI Inbetween; Apply | A ghost between the keys, then keys | 5 s | [ ] | |
| K11 | AI: a span by frame numbers | AI ... options > From / To > Make This Span | Only that span made | 8 s | [ ] | |
| K12 | Ragdoll a fall (needs Blender's libraries) | Puppet > Ragdoll Events > Go Limp Here; Ragdoll | The body falls and settles | 6 s | [ ] | |
| K13 | Generate a walk cycle and keep the style | Rigging > Walk Cycle > Generate into the Clip; Save Style | A walking clip; Assets/Gaits/<name>.gait | 8.1 s | [ ] | |

## L. Sequencer and Play

| # | Task | Steps | Expected | Target | Done | Time |
|---|---|---|---|---|---|---|
| L1 | Make a sequence of two clips | Sequencer > Create; drop two clips on a track | Bars on the timeline; preview plays them | 10 s | [ ] | |
| L2 | Add a trigger key | Sequencer > Add Trigger; click its key; press J | J jumps to its clip in Play | 5 s | [ ] | |
| L3 | Enter and leave Play | Ctrl+P (or the Play button) twice | The scene runs; edits made in Play are put back | 2 s | [ ] | |

## M. Import, export and assets

| # | Task | Steps | Expected | Target | Done | Time |
|---|---|---|---|---|---|---|
| M1 | Import an FBX character | File > Import... (or drop it on the Scene view) | A preview, then the character with its textures | 8 s | [ ] | |
| M2 | Export the selection as glTF | File > Export...; glTF; Selection only | A .glb that opens in Blender | 8 s | [ ] | |
| M3 | Add a reference image from the side | Reference & Planning; GameObject > Reference Image > Side > an image | A plane facing the side view | 5 s | [ ] | |
| M4 | Save the scene and open a new one | Ctrl+S; Ctrl+N | Saved; a new scene; unsaved changes prompt if any | 3.9 s | [ ] | |
| M5 | Find an asset in the Project window | Project > Search: brick | Matching assets listed | 4 s | [ ] | |

## N. Live Link and the bridges

| # | Task | Steps | Expected | Target | Done | Time |
|---|---|---|---|---|---|---|
| N1 | Start Live Link | Window > Live Link > Start | Listening on 40100 (or the next free port) | 3 s | [ ] | |
| N2 | Install the Blender bridge | Live Link > Install a Bridge > Blender | The add-on installed and enabled | 4 s | [ ] | |
| N3 | Blender connects by itself | Open Blender with Blendity running | Blender's panel: Connected to Blendity; edits stream | 10 s | [ ] | |
| N4 | Install the Unity bridge into a project | Live Link > Install a Bridge > Unity Project...; pick the folder | The package in Packages; Unity connects | 10 s | [ ] | |
| N5 | Install the Godot add-on | Install a Bridge > Godot Project... | addons/blendity_livelink, enabled in project.godot | 10 s | [ ] | |
| N6 | Install the Unreal plugin | Install a Bridge > Unreal Project... | Plugins/BlendityLiveLink, listed in the .uproject | 10 s | [ ] | |

## O. Palette, Wheel, My Tools and UI files

| # | Task | Steps | Expected | Target | Done | Time |
|---|---|---|---|---|---|---|
| O1 | Open the Wheel and pick a slice | Space over the Scene view; click a slice | The command runs | 2 s | [ ] | |
| O2 | Add a command to a Wheel ring | Preferences > Wheel > Into Ring; Pick a Command...; type, click | The command in that ring; Preferences stays open | 8 s | [ ] | |
| O3 | Make a My Tools panel | My Tools > +; name it; right-click a menu row > Add to My Tools | A panel with that button | 10 s | [ ] | |
| O4 | Assign a shortcut | Ctrl+K, right-click a row > Assign Shortcut...; press keys | The shortcut runs it | 8 s | [ ] | |
| O5 | Save your UI to a file and load it | Preferences > UI file > Save...; Load | Layout, keys and panels restored | 10 s | [ ] | |

## P. Learn and help

| # | Task | Steps | Expected | Target | Done | Time |
|---|---|---|---|---|---|---|
| P1 | Open a lesson and use its Try It | Learn > a lesson > Try it | The scene set up as the lesson says | 4 s | [ ] | |
| P2 | Answer a lesson's question | Learn > a question > reveal | The answer and the feature named | 4 s | [ ] | |
| P3 | Read a tooltip on a cut-off button | Hover a button whose label ends in ... | The whole label first, then its help | 2 s | [ ] | |
| P4 | Follow a refusal's Fix | In Play, try Set Key; click the toast's Fix | Play stops; the key can be set | 4 s | [ ] | |

## Q. Performance

| # | Task | Steps | Expected | Target | Done | Time |
|---|---|---|---|---|---|---|
| Q1 | Orbit a 1M-triangle scene | Benchmark scene million; orbit | Smooth (the status bar's fps at 30 or more on the GPU) | 5 s | [ ] | |
| Q2 | Edit Mode on a 200k-face mesh | Tab on a big mesh; select and move a face | No pause over a fraction of a second | 5 s | [ ] | |
| Q3 | First render after start | Start Blendity; F12 | The first render as fast as the second (no long cold start) | 5 s | [ ] | |
| Q4 | Resize a window to 1024 x 640 | Shrink the window | Nothing overlaps; long labels end in ... | 3 s | [ ] | |
