# Round 590 checklist evidence

The eight workstreams below preserve the IDs and original groups in the [141-item checklist](checklist.json).
A label name check confirms that the listed text occurs in the source or manual. The live sweep and focused checks cover
control presentation, keyboard focus and contrast on the exercised screens. An individual checklist action retains its
own evidence note where the audit or a named regression covers part of the route.

U-01 through U-06 apply across the visible controls in every workstream. U-07 governs repeat verification when layouts
or controls change. Measured values, scope limits and ranked findings are in the [live sweep report](sweep-590.md).

| Workstream | Original groups | Rows | What the sweep can observe |
| --- | --- | ---: | --- |
| Navigation and preferences | A, B | 15 | Preferences, theme contrast, scale and navigation controls. |
| Hierarchy and command discovery | C, O, P | 16 | Inspector, Palette, Wheel, Learn, tooltips and keyboard focus. |
| Modeling and drawing | D, E, R | 32 | Modeling Tools, Draw, Blockout, compact controls and tablet density. |
| UV and surface work | F, G | 18 | UV Editor, Materials, Textures, paint and sculpt controls. |
| Stylized work, lighting and rendering | H, I | 21 | Stylized layouts, Lighting, Render, Editing and renderer settings. |
| Rigging, animation and play | J, K, L | 24 | Rigging, animation, posing, Sequencer and runtime controls. |
| Assets and interoperability | M, N | 11 | Project, import/export, Live Link, bridge installation surfaces. |
| Performance and display | Q | 4 | Viewport and window layout controls. The performance survey owns timing. |

## Navigation and preferences

### A. Getting around and Preferences

| ID | Checklist task | Evidence for this round |
| --- | --- | --- |
| A1 | Switch to the Animation layout and back to Default | Layout surfaces in the live sweep. The switch-back sequence was not replayed. Listed control names passed the static check. |
| A2 | Open a window that isn't shown | Window menu and Live Link surface in the live sweep. |
| A3 | Change the editor feel to Blender and back | Keymap and navigation settings inspected in source. Preset switching was not replayed. |
| A4 | Set the theme to Light, then High Contrast, then Dark | All themes checked for U-02 text and dim text contrast in the live sweep and focused check. Listed control names passed the static check. |
| A5 | Raise the UI scale to 1.5 | Both UI scales in the live sweep. Changing scale through Preferences was not replayed. |
| A6 | Turn layout guides off and on | Listed control names passed the static check. The workflow was not replayed. |
| A7 | Find any command by name | The shared keymap confirms Ctrl+K. The unwrap route was not replayed. |
| A8 | Pin a command to Quick Favorites | The shared keymap confirms Ctrl+K and Shift+Q. The Favorites edit was not replayed. Listed control names passed the static check. |
| A9 | Undo and redo three edits | The shared keymap confirms Ctrl+Z and Ctrl+Y. The three-step sequence was not replayed. |

### B. Navigation and selection

| ID | Checklist task | Evidence for this round |
| --- | --- | --- |
| B1 | Frame the selected object | F is valid in Unity. The Blender preset uses the period key. |
| B2 | Orbit, pan and zoom with the mouse (Unity feel) | Unity mouse route documented. A smoothness judgment needs a manual movement check. |
| B3 | Orbit with the pen only | Needs a physical pen to evaluate the documented route. |
| B4 | Box-select three objects | The documented workflow has no control-name probe. It was not replayed. |
| B5 | Snap a move to the grid | The documented workflow has no control-name probe. It was not replayed. |
| B6 | Look through the top view and back | The documented workflow has no control-name probe. It was not replayed. |

## Hierarchy and command discovery

### C. Hierarchy and Inspector

| ID | Checklist task | Evidence for this round |
| --- | --- | --- |
| C1 | Parent the Sphere under the Cube | Existing real-mouse regression for Hierarchy reparenting in test_editor.cpp, round 506. |
| C2 | Rename an object | The documented workflow has no control-name probe. It was not replayed. |
| C3 | Add a component by search | Component Inspector and chooser surfaces in the live sweep. Adding a Light was not replayed. Listed control names passed the static check. |
| C4 | Edit a field on three objects at once | The documented workflow has no control-name probe. It was not replayed. |
| C5 | Pin the Inspector and select something else | Listed control names passed the static check. The workflow was not replayed. |
| C6 | Pick a modifier's vertex group from the list | The documented workflow has no control-name probe. It was not replayed. |
| C7 | Add and remove a shape key | Listed control names passed the static check. The workflow was not replayed. |

### O. Palette, Wheel, My Tools and UI files

| ID | Checklist task | Evidence for this round |
| --- | --- | --- |
| O1 | Open the Wheel and pick a slice | The documented workflow has no control-name probe. It was not replayed. |
| O2 | Add a command to a Wheel ring | Listed control names passed the static check. The workflow was not replayed. |
| O3 | Make a My Tools panel | The documented workflow has no control-name probe. It was not replayed. |
| O4 | Assign a shortcut | The shared keymap confirms Ctrl+K. A shortcut override was not replayed. Listed control names passed the static check. |
| O5 | Save your UI to a file and load it | The documented workflow has no control-name probe. It was not replayed. |

### P. Learn and help

| ID | Checklist task | Evidence for this round |
| --- | --- | --- |
| P1 | Open a lesson and use its Try It | Existing Learn Try It coverage includes the physics lesson in test_editor.cpp. |
| P2 | Answer a lesson's question | The documented workflow has no control-name probe. It was not replayed. |
| P3 | Read a tooltip on a cut-off button | The focused sweep-fixes check exercises the Lighting tooltip on an elided label. |
| P4 | Follow a refusal's Fix | The documented workflow has no control-name probe. It was not replayed. |

## Modeling and drawing

### D. Modeling and Edit Mode

| ID | Checklist task | Evidence for this round |
| --- | --- | --- |
| D1 | Add a cube and bevel its edges | The documented workflow has no control-name probe. It was not replayed. |
| D2 | Extrude a face | Confirmed documentation fault. Default Unity uses Ctrl+E for Extrude. E is Rotate. |
| D3 | Loop cut | Ctrl+R is valid in Unity. The SketchUp preset has no default Loop Cut chord. |
| D4 | Mirror a half model along Y | Listed control names passed the static check. The workflow was not replayed. |
| D5 | Add a Subdivision Surface modifier | The documented workflow has no control-name probe. It was not replayed. |
| D6 | Boolean difference | Listed control names passed the static check. The workflow was not replayed. |
| D7 | Clear imported custom normals | Listed control names passed the static check. The workflow was not replayed. |
| D8 | Undo a bevel's segments in F9 | Existing F9 behavior coverage and prior keymap-label work in task 0552. This exact bevel route was not replayed. |
| D9 | Add a guide line by numbers | Listed control names passed the static check. The workflow was not replayed. |

### E. Drawing, sketch and Push-Pull

| ID | Checklist task | Evidence for this round |
| --- | --- | --- |
| E1 | Draw a rectangle on the ground and push-pull it | The documented workflow has no control-name probe. It was not replayed. |
| E2 | Sketch with the pen in red | Needs a physical pen for the pen-specific claim. Listed control names passed the static check. |
| E3 | Steady a shaky line | Listed control names passed the static check. The workflow was not replayed. |
| E4 | Draw on the ground plane | Listed control names passed the static check. The workflow was not replayed. |
| E5 | Three-point perspective guide | The documented workflow has no control-name probe. It was not replayed. |
| E6 | Lift a corner's three lines to 3D | Listed control names passed the static check. The workflow was not replayed. |
| E7 | Clear the sketch | Listed control names passed the static check. The workflow was not replayed. |
| E8 | Build a box from three lines | The documented workflow has no control-name probe. It was not replayed. |
| E9 | Zoom in on the Draw tab to draw a detail, turn the canvas, then fit | Listed control names passed the static check. The workflow was not replayed. |
| E10 | Switch to the Marker and make it bigger and fainter with the puck | Listed control names passed the static check. The workflow was not replayed. |
| E11 | Erase the middle of a line, then pick the last brush back | The shared Draw-pad keymap confirms E for Eraser and B for Brush in Draw context. |

### R. Blockout

| ID | Checklist task | Evidence for this round |
| --- | --- | --- |
| R1 | Start a Blockout | Listed control names passed the static check. The workflow was not replayed. |
| R2 | Cut a hole with a cylinder | Listed control names passed the static check. The workflow was not replayed. |
| R3 | Draw a room with the Draw Piece tool | Listed control names passed the static check. The workflow was not replayed. |
| R4 | Stretch a piece by a face handle | The documented workflow has no control-name probe. It was not replayed. |
| R5 | Join two rooms | The documented workflow has no control-name probe. It was not replayed. |
| R6 | Add a door and a window | The documented workflow has no control-name probe. It was not replayed. |
| R7 | Add stairs and turn them | Listed control names passed the static check. The workflow was not replayed. |
| R8 | Draw an L-shaped room | Listed control names passed the static check. The workflow was not replayed. |
| R9 | Round the joins | Listed control names passed the static check. The workflow was not replayed. |
| R10 | Scale a Blockout without shear | Listed control names passed the static check. The workflow was not replayed. |
| R11 | Bake to a mesh and back | Listed control names passed the static check. The workflow was not replayed. |
| R12 | Try Edit Mode on a Blockout | The documented workflow has no control-name probe. It was not replayed. |

## UV and surface work

### F. UV

| ID | Checklist task | Evidence for this round |
| --- | --- | --- |
| F1 | Smart UV project | Listed control names passed the static check. The workflow was not replayed. |
| F2 | Mark seams from sharp edges and unwrap | Listed control names passed the static check. The workflow was not replayed. |
| F3 | Part Unwrap at a looser bound | Listed control names passed the static check. The workflow was not replayed. |
| F4 | Edit only two materials' UVs, the others hidden | Listed control names passed the static check. The workflow was not replayed. |
| F5 | Rotate selected UVs by 90 | Listed control names passed the static check. The workflow was not replayed. |
| F6 | Check stretching | Listed control names passed the static check. The workflow was not replayed. |
| F7 | UV editor in a narrow window | The focused U-02 check also exercises a narrow UV editor. Its layout result is logged. |

### G. Materials, textures, Paint and Sculpt

| ID | Checklist task | Evidence for this round |
| --- | --- | --- |
| G1 | Give an object a new material and change its color | The documented workflow has no control-name probe. It was not replayed. |
| G2 | Add a surface for painting | Listed control names passed the static check. The workflow was not replayed. |
| G3 | Bake mesh maps | Listed control names passed the static check. The workflow was not replayed. |
| G4 | Add an edge-wear mask preset | Listed control names passed the static check. The workflow was not replayed. |
| G5 | Read one layer's mask from another (Anchor) | The documented workflow has no control-name probe. It was not replayed. |
| G6 | Save a layer group as a smart material | Listed control names passed the static check. The workflow was not replayed. |
| G7 | Paint a stroke on the model | Listed control names passed the static check. The workflow was not replayed. |
| G8 | Bucket-fill an island | The documented workflow has no control-name probe. It was not replayed. |
| G9 | Sculpt with Clay, then smooth | Listed control names passed the static check. The workflow was not replayed. |
| G10 | Make the sculpt brush bigger by one step | The documented workflow has no control-name probe. It was not replayed. |
| G11 | Export the textures to a folder | Listed control names passed the static check. The workflow was not replayed. |

## Stylized work, lighting and rendering

### H. Stylized

| ID | Checklist task | Evidence for this round |
| --- | --- | --- |
| H1 | Give an object the anime rock look | Listed control names passed the static check. The workflow was not replayed. |
| H2 | Paint grass cards | Listed control names passed the static check. The workflow was not replayed. |
| H3 | Start a second bush | Listed control names passed the static check. The workflow was not replayed. |
| H4 | Fill a canopy | Listed control names passed the static check. The workflow was not replayed. |
| H5 | Scatter rocks with another seed | Listed control names passed the static check. The workflow was not replayed. |
| H6 | Sun disc and fog | The documented workflow has no control-name probe. It was not replayed. |

### I. Lighting and rendering

| ID | Checklist task | Evidence for this round |
| --- | --- | --- |
| I1 | Aim a light at a point by numbers | Listed control names passed the static check. The workflow was not replayed. |
| I2 | Bake lighting | Listed control names passed the static check. The workflow was not replayed. |
| I3 | Render an image | The documented workflow has no control-name probe. It was not replayed. |
| I4 | Render frames 30-40 at 12 fps | Listed control names passed the static check. The workflow was not replayed. |
| I5 | Render a camera sequence to a folder | Listed control names passed the static check. The workflow was not replayed. |
| I6 | Path-traced preview | Listed control names passed the static check. The workflow was not replayed. |
| I7 | Develop a render (exposure, curve) | Listed control names passed the static check. The workflow was not replayed. |
| I8 | Point Blendity at your own ffmpeg | Needs an ffmpeg executable chosen by the user, outside the scratch run. |
| I9 | MoonRay: check it works (needs MoonRay) | Needs MoonRay, outside the scratch run. Listed control names passed the static check. |
| I10 | Put light probes where you want them | Listed control names passed the static check. The workflow was not replayed. |
| I11 | Bake a Mixed light as Shadowmask | Listed control names passed the static check. The workflow was not replayed. |
| I12 | Soften a baked light's shadow | Listed control names passed the static check. The workflow was not replayed. |
| I13 | Take the ringing out of the probes | Listed control names passed the static check. The workflow was not replayed. |
| I14 | Choose the bake's denoiser | Listed control names passed the static check. The workflow was not replayed. |
| I15 | Match a Unity project's light intensity | Needs an appropriate Unity project and consent to use it. Listed control names passed the static check. |

## Rigging, animation and play

### J. Rigging, skinning and hair

| ID | Checklist task | Evidence for this round |
| --- | --- | --- |
| J1 | Rig a humanoid | The documented workflow has no control-name probe. It was not replayed. |
| J2 | Auto-rig from markers | The documented workflow has no control-name probe. It was not replayed. |
| J3 | Skin a mesh with automatic weights | The documented workflow has no control-name probe. It was not replayed. |
| J4 | Clean small weights | Listed control names passed the static check. The workflow was not replayed. |
| J5 | Rig hair from a mesh with 4 locks of 2 bones | Listed control names passed the static check. The workflow was not replayed. |
| J6 | Add spring bones (jiggle) | The documented workflow has no control-name probe. It was not replayed. |

### K. Animation, Puppet Posing, physics and AI

| ID | Checklist task | Evidence for this round |
| --- | --- | --- |
| K1 | Key a position at frame 0 and 30 | Shift+K is valid in Unity. Blender uses Shift+I and Maya uses S. |
| K2 | Pose a reach by the hand (Puppet Posing) | Listed control names passed the static check. The workflow was not replayed. |
| K3 | Pose with the learned prior (local AI install) | Needs an installed pose prior, outside the scratch run. Listed control names passed the static check. |
| K4 | Lock a foot | Listed control names passed the static check. The workflow was not replayed. |
| K5 | Mark a contact on the foot at the playhead | Listed control names passed the static check. The workflow was not replayed. |
| K6 | Find and fix a foot slide | Listed control names passed the static check. The workflow was not replayed. |
| K7 | Motion trails and onion skin | Listed control names passed the static check. The workflow was not replayed. |
| K8 | Pose a neighbouring frame in the Zoetrope | Listed control names passed the static check. The workflow was not replayed. |
| K9 | Type a key's slopes | The documented workflow has no control-name probe. It was not replayed. |
| K10 | AI Inbetween a span (local AI install) | Needs the optional AI Inbetween installation, outside the scratch run. Listed control names passed the static check. |
| K11 | AI: a span by frame numbers | Needs the optional AI Inbetween installation, outside the scratch run. Listed control names passed the static check. |
| K12 | Ragdoll a fall (needs Blender's libraries) | Requires the libraries build and a rigged scene. The playback was not replayed. Listed control names passed the static check. |
| K13 | Generate a walk cycle and keep the style | Listed control names passed the static check. The workflow was not replayed. |
| K14 | Record keys while you pose | Listed control names passed the static check. The workflow was not replayed. |
| K15 | Scrub a posed humanoid | The documented workflow has no control-name probe. It was not replayed. |

### L. Sequencer and Play

| ID | Checklist task | Evidence for this round |
| --- | --- | --- |
| L1 | Make a sequence of two clips | Listed control names passed the static check. The workflow was not replayed. |
| L2 | Add a trigger key | Listed control names passed the static check. The workflow was not replayed. |
| L3 | Enter and leave Play | The shared keymap confirms Ctrl+P. Play and stop were not replayed. |

## Assets and interoperability

### M. Import, export and assets

| ID | Checklist task | Evidence for this round |
| --- | --- | --- |
| M1 | Import an FBX character | Needs a suitable scratch FBX fixture. The live sweep covers the import surface. |
| M2 | Export the selection as glTF | Needs a scratch scene and export destination. The live sweep covers the export surface. |
| M3 | Add a reference image from the side | Listed control names passed the static check. The workflow was not replayed. |
| M4 | Save the scene and open a new one | The shared keymap confirms Ctrl+S and Ctrl+N. The file workflow was not replayed. |
| M5 | Find an asset in the Project window | The documented workflow has no control-name probe. It was not replayed. |

### N. Live Link and the bridges

| ID | Checklist task | Evidence for this round |
| --- | --- | --- |
| N1 | Start Live Link | Live Link control surface covered. Starting a listening server was outside this sweep. Listed control names passed the static check. |
| N2 | Install the Blender bridge | Bridge installation surface covered. Installing into Blender was outside this sweep. Listed control names passed the static check. |
| N3 | Blender connects by itself | Needs a Blender process with the bridge installed. |
| N4 | Install the Unity bridge into a project | Needs a separate scratch Unity project and bridge installation. Listed control names passed the static check. |
| N5 | Install the Godot add-on | Needs a separate scratch Godot project and bridge installation. Listed control names passed the static check. |
| N6 | Install the Unreal plugin | Needs a separate scratch Unreal project and bridge installation. Listed control names passed the static check. |

## Performance and display

### Q. Performance

| ID | Checklist task | Evidence for this round |
| --- | --- | --- |
| Q1 | Orbit a 1M-triangle scene | Covered by the performance survey, outside this round's timing scope. |
| Q2 | Edit Mode on a 200k-face mesh | The active Edit Mode timing work is task 0571. This round did not duplicate it. |
| Q3 | First render after start | Covered by native performance work. No cold launch timing was inferred from the sweep. |
| Q4 | Resize a window to 1024 x 640 | The live sweep renders at 1024×640. A native window resize was not replayed. |
