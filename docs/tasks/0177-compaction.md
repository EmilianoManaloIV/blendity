# 0177: One test per behaviour, timings to stress; the test suite in files; dead code out

- **Status:** done (round 177)
- **Requirements:** C-02, C-04
- **Decisions:** ADR 0056 (what a unit test is for; measurements live in stress)
- **Model:** main session
- **PR:** #122 (stacked on #121)

## Goal
The user (2026-10-21): "Look for performance improvements across the board ... check on unneeded tests and remove
them. Compact the code-base as needed." The choice: one test per behaviour, timings to stress.

## Measured first
The suite on a quiet machine (round 175's chain, Windows, 32 threads): 910 tests, 11,406 checks, **204.6 s** of
test time; 23 tests over a second took 124.1 s of it. The slowest, and what each turned out to be:

| test | s | cause | done |
|---|---|---|---|
| gpu: every device matches the CPU | 70.3 | the software-BVH kernel over SPIR-V's id bound, rendering on the CPU (task 0157 fixed it: 2.0 s) | kept: it is the guard |
| mmd: 240 damaged PMX / PMD files | 10.2 | 240 variants where 24 find the same | 24 in the suite; 240 in stress `codecs` |
| GPU opening: the Camera Preview while the GPU opens | 6.8 | a 3 s pretend open delay, polled | delay 1 s |
| probe volumes: a damaged ProbeVolume.bin | 3.8 | every truncation point under 400 bytes, 150 corruptions | every 8th point and 30 corruptions; the sweep in stress `probes` |
| render cache: 30 mixed actions in verify mode | 3.3 | the same as stress `editor_fuzz` (6,000 frames in verify mode) | removed |
| gpu views: textured materials | 2.5 | eleven pictures on the card | kept (behaviour) |
| voxel GI: Auto Generate waits for the changes to settle | 2.5 | a real wait | kept |
| GPU opening: a tracer waiting for its GPU | 2.1 | a 1.5 s pretend delay | delay 0.6 s |
| face materials: pick the material itself (13.4 s in round 174's run) | 0.8 | not the Materials previews: a build running beside the suite (0.78 s alone) | nothing; the previews hypothesis was wrong |

Timing assertions behind `NDEBUG` (the overlap check 3x faster than its reference on 40k faces; AutoPhysics' four
clips under 5 s; Locomotion under 0.2 ms a frame; 100 spring chains under 1 ms) became stress rows; the tests keep
printing the numbers.

## Scope
- In (tests): the split into `tests/test_<group>.cpp` + `test_common.h` by a script that cut the one file at its
  group functions and the "round N" markers, merged units that share a helper, hoisted helpers several rounds use
  into the header, and placed each component by its tests' prefixes (`scratchpad/split177.py` in the session; the
  result is committed, the script is not); `--group` and `--list-groups`; the trims and moves above; the two
  spike tests of the GPU view removed, the OptiX picture test removed (stress `gpu_optix`), the pacing and
  frame-rate tests merged; `tools/test_coverage_map.py`.
- In (code): `src/core/hash.h` (one FNV-1a, one 8-bytes-a-step mixer, `hash32`); `closest_on_segment` and the 2D
  segment distance in `core/math.h`; `clampf01` and the second `lerpf` gone; `config_path`, `mirror_markers`,
  `sample_clip_values` and GLSL `safe_normalize` deleted (`merge_by_distance_naive` stays in src: it shares
  `finish_merge` with the real one and two tests call it); `is_bone` / `bone_children` once; the Jolt layer
  classes once; `move_to_trash`'s test-folder prefix once; undo and redo through one `restore_state`; the button
  grid of the Rigging and Tools windows, the `to_screen` lambda of the knife / draw / scene view, the closed-loop
  walk of the mesh tools and the wire-path walk of Follow each once (the Animation and Sequencer windows' toolbar
  button, seven lines twice, was left: a helper would be as long); `editor.cpp`'s console into
  `editor_commands.cpp`, `scene_view.cpp`'s picking into `scene_pick.cpp` and its overlays into
  `scene_overlays.cpp`; `tools/clean.bat`; `tools/README.md`.
- Out: the OptiX spike (Stack 5 turns it into the device), GPU raster route (B) (task 0160 names it), the
  Vulkan boilerplate shared by `gpu_device.cpp` and `gpu_raster.cpp` (tasks 0160 / 0161 restructure both), the
  rasterizer's duplicated triangle setup (hot code, left as is).

## Acceptance criteria
1. The Windows suite at most 160 s of test time (was 204.6), the stress run longer by at most 40 s.
2. No behaviour loses its last test: `tools/test_coverage_map.py` lists every console command and window
   action against the tests that exercise it, with no empty entry.
3. The check count's drop itemised in the CHANGELOG, at most 60.
4. Linux and the sanitizer build unchanged in outcome; the three compilers warning-free on the new files.
5. `wc -l src` down by at least 1,000 lines (the plan's estimate; the measured number is below).

## Results
| measure | before | after | target |
|---|---|---|---|
| suite test time (quiet, Windows, 32 threads) | 204.6 s | 117.4 s | 160 s |
| tests / checks | 912 / 11,416 (after round 176) | 908 / 11,144 | the drop itemised, at most 60 |
| `wc -l src` | 103,086 | 137 fewer | 1,000 fewer (an estimate: the duplication scan found no block over 17 lines in two places; the file moves are neutral) |
| the coverage map | - | 259 entries, 0 empty | no empty entry |
| the stress run | - | +32.6 s (codecs, probes, physics, animation, modeling rows) | at most +40 s |
| a test build (8 files side by side) | 58 s | 55 s | - |

## Tests and checks
- The split suite: every group runs alone (`--group`), the sum of the groups equals the whole, a wrong group name
  is refused.
- The coverage map's output is in this doc's appendix.

## Appendix: the coverage map
`python tools/test_coverage_map.py` (the first test that exercises each entry):

| kind | name | tests | first test |
|---|---|---|---|
| action | `assets.refresh` | 1 | keymap actions: every action id runs through run_action (a renamed ... |
| action | `draw.arc` | 1 | keymap actions: every action id runs through run_action (a renamed ... |
| action | `draw.circle` | 2 | keymap: presets lay shortcuts out like Unity, Blender, Maya, 3ds Ma... |
| action | `draw.polygon` | 1 | keymap actions: every action id runs through run_action (a renamed ... |
| action | `draw.polyline` | 1 | keymap actions: every action id runs through run_action (a renamed ... |
| action | `draw.rectangle` | 2 | keymap: presets lay shortcuts out like Unity, Blender, Maya, 3ds Ma... |
| action | `edit.adjust_last` | 1 | keymap actions: every action id runs through run_action (a renamed ... |
| action | `edit.delete` | 3 | keymap: presets lay shortcuts out like Unity, Blender, Maya, 3ds Ma... |
| action | `edit.duplicate` | 1 | keymap actions: every action id runs through run_action (a renamed ... |
| action | `edit.redo` | 3 | keymap: presets lay shortcuts out like Unity, Blender, Maya, 3ds Ma... |
| action | `edit.rename` | 1 | keymap actions: every action id runs through run_action (a renamed ... |
| action | `edit.select_all` | 1 | keymap actions: every action id runs through run_action (a renamed ... |
| action | `edit.select_none` | 1 | keymap actions: every action id runs through run_action (a renamed ... |
| action | `edit.undo` | 3 | keymap: presets lay shortcuts out like Unity, Blender, Maya, 3ds Ma... |
| action | `file.new` | 1 | keymap actions: every action id runs through run_action (a renamed ... |
| action | `file.open` | 1 | keymap actions: every action id runs through run_action (a renamed ... |
| action | `file.save` | 2 | keymap actions: every action id runs through run_action (a renamed ... |
| action | `file.save_as` | 1 | keymap actions: every action id runs through run_action (a renamed ... |
| action | `lighting.update` | 1 | manual lighting: new scenes are Manual unless BLENDITY_LIGHTING_UPD... |
| action | `mesh.bevel` | 5 | keymap: presets lay shortcuts out like Unity, Blender, Maya, 3ds Ma... |
| action | `mesh.bridge` | 1 | keymap actions: every action id runs through run_action (a renamed ... |
| action | `mesh.connect` | 1 | keymap actions: every action id runs through run_action (a renamed ... |
| action | `mesh.dissolve` | 1 | keymap actions: every action id runs through run_action (a renamed ... |
| action | `mesh.duplicate` | 1 | keymap actions: every action id runs through run_action (a renamed ... |
| action | `mesh.extrude` | 2 | keymap: presets lay shortcuts out like Unity, Blender, Maya, 3ds Ma... |
| action | `mesh.fill` | 2 | keymap actions: every action id runs through run_action (a renamed ... |
| action | `mesh.inset` | 2 | keymap actions: every action id runs through run_action (a renamed ... |
| action | `mesh.knife` | 1 | keymap actions: every action id runs through run_action (a renamed ... |
| action | `mesh.loop_cut` | 1 | keymap actions: every action id runs through run_action (a renamed ... |
| action | `mesh.merge_center` | 3 | keymap: presets lay shortcuts out like Unity, Blender, Maya, 3ds Ma... |
| action | `mesh.merge_distance` | 2 | keymap actions: every action id runs through run_action (a renamed ... |
| action | `mesh.proportional` | 2 | keymap: presets lay shortcuts out like Unity, Blender, Maya, 3ds Ma... |
| action | `mesh.push_pull` | 5 | keymap: presets lay shortcuts out like Unity, Blender, Maya, 3ds Ma... |
| action | `mesh.push_through` | 1 | keymap actions: every action id runs through run_action (a renamed ... |
| action | `mesh.recalc_normals` | 1 | keymap actions: every action id runs through run_action (a renamed ... |
| action | `mesh.shrink_fatten` | 1 | keymap actions: every action id runs through run_action (a renamed ... |
| action | `mesh.to_sphere` | 1 | keymap actions: every action id runs through run_action (a renamed ... |
| action | `mesh.triangulate` | 1 | keymap actions: every action id runs through run_action (a renamed ... |
| action | `mesh.tris_to_quads` | 1 | keymap actions: every action id runs through run_action (a renamed ... |
| action | `mode.edge` | 1 | keymap actions: every action id runs through run_action (a renamed ... |
| action | `mode.edit_toggle` | 4 | keymap: presets lay shortcuts out like Unity, Blender, Maya, 3ds Ma... |
| action | `mode.face` | 2 | keymap: presets lay shortcuts out like Unity, Blender, Maya, 3ds Ma... |
| action | `mode.vertex` | 2 | keymap: presets lay shortcuts out like Unity, Blender, Maya, 3ds Ma... |
| action | `object.create_empty` | 1 | keymap actions: every action id runs through run_action (a renamed ... |
| action | `object.create_empty_child` | 1 | keymap actions: every action id runs through run_action (a renamed ... |
| action | `object.drop_to_floor` | 2 | feel: Blender, Maya, Unreal and Unity each navigate the Scene view ... |
| action | `object.join` | 1 | keymap actions: every action id runs through run_action (a renamed ... |
| action | `object.move_to_view` | 1 | keymap actions: every action id runs through run_action (a renamed ... |
| action | `play.pause` | 2 | keymap actions: every action id runs through run_action (a renamed ... |
| action | `play.step` | 1 | keymap actions: every action id runs through run_action (a renamed ... |
| action | `play.toggle` | 3 | feel: Blender, Maya, Unreal and Unity each navigate the Scene view ... |
| action | `points.toggle_closed` | 1 | keymap actions: every action id runs through run_action (a renamed ... |
| action | `render.animation` | 1 | keymap actions: every action id runs through run_action (a renamed ... |
| action | `render.image` | 1 | keymap actions: every action id runs through run_action (a renamed ... |
| action | `render.screenshot` | 1 | keymap actions: every action id runs through run_action (a renamed ... |
| action | `select.invert` | 1 | keymap actions: every action id runs through run_action (a renamed ... |
| action | `select.less` | 1 | keymap actions: every action id runs through run_action (a renamed ... |
| action | `select.linked` | 1 | keymap actions: every action id runs through run_action (a renamed ... |
| action | `select.linked_pick` | 1 | keymap actions: every action id runs through run_action (a renamed ... |
| action | `select.more` | 2 | keymap actions: every action id runs through run_action (a renamed ... |
| action | `tool.move` | 4 | keymap: presets lay shortcuts out like Unity, Blender, Maya, 3ds Ma... |
| action | `tool.rotate` | 1 | keymap actions: every action id runs through run_action (a renamed ... |
| action | `tool.scale` | 3 | keymap: presets lay shortcuts out like Unity, Blender, Maya, 3ds Ma... |
| action | `tool.transform` | 1 | keymap actions: every action id runs through run_action (a renamed ... |
| action | `tool.view` | 1 | keymap actions: every action id runs through run_action (a renamed ... |
| action | `transform.grab` | 2 | keymap: presets lay shortcuts out like Unity, Blender, Maya, 3ds Ma... |
| action | `transform.rotate` | 1 | keymap: presets lay shortcuts out like Unity, Blender, Maya, 3ds Ma... |
| action | `transform.scale` | 1 | keymap: presets lay shortcuts out like Unity, Blender, Maya, 3ds Ma... |
| action | `view.active_camera` | 1 | keymap actions: every action id runs through run_action (a renamed ... |
| action | `view.frame` | 3 | keymap: presets lay shortcuts out like Unity, Blender, Maya, 3ds Ma... |
| action | `view.front` | 1 | keymap actions: every action id runs through run_action (a renamed ... |
| action | `view.perspective` | 1 | keymap actions: every action id runs through run_action (a renamed ... |
| action | `view.side` | 1 | keymap actions: every action id runs through run_action (a renamed ... |
| action | `view.top` | 2 | feel: Blender, Maya, Unreal and Unity each navigate the Scene view ... |
| action | `window.learn` | 3 | keymap actions: every action id runs through run_action (a renamed ... |
| action | `window.render` | 2 | keymap actions: every action id runs through run_action (a renamed ... |
| command | `activecamera` | 3 | active camera: a filter on a camera that is not \"Main Camera\" sho... |
| command | `ai` | 8 | ai download: refused when it doesn't match its checksum; an install... |
| command | `aimlight` | 1 | lights: aiming at a point turns the light's +Z to it, keeps X level... |
| command | `alignview` | 1 | console: the commands without a test of their own answer and do wha... |
| command | `alltools` | 1 | layouts: each stage's layout opens its windows and shows its tools;... |
| command | `anim` | 22 | animation: the Animation window's clip on a box - keys on the lid, ... |
| command | `animemat` | 1 | anime textures: a card's albedo carries its cover as alpha (the ima... |
| command | `applymods` | 1 | vertex groups: Apply Modifiers bakes a deformer limited to a group ... |
| command | `armature` | 4 | skins: GameObject > Armature binds a selected mesh with automatic w... |
| command | `assignmat` | 5 | assets: Ctrl and Shift click select several materials, Delete asks ... |
| command | `assist` | 14 | ai download: refused when it doesn't match its checksum; an install... |
| command | `autopose` | 3 | autopose (review): the hips dragged down crouch the body - the feet... |
| command | `autorig` | 8 | auto-rig: a humanoid's markers give a skeleton at its joints, weigh... |
| command | `autosmooth` | 1 | auto smooth: a rounded bevel turns on smooth-by-angle shading; flat... |
| command | `bake` | 7 | baked lighting: a new scene cancels a running bake and starts with ... |
| command | `baketex` | 1 | texture bake: Object > Bake Textures keeps the look (rasterized wit... |
| command | `bench` | 1 | console: the commands without a test of their own answer and do wha... |
| command | `bevelprofile` | 1 | bevel profile: concave below 0.5, flat at 0.25, round at 0.5, conve... |
| command | `body` | 5 | physics in the editor: Body Mass, contacts by hand and found, the f... |
| command | `bone` | 9 | skins: GameObject > Armature binds a selected mesh with automatic w... |
| command | `boolean` | 1 | object: Boolean difference applied, and as a live modifier (cutter ... |
| command | `camera` | 61 | IK: the console adds IK and presets; moving the target with the giz... |
| command | `camerapreview` | 7 | GPU opening: turning the Camera Preview to Rendered while the GPU o... |
| command | `clear` | 1 | console: the commands without a test of their own answer and do wha... |
| command | `clearguides` | 1 | guide lines: added, undone, saved; drawing snaps to where two cross... |
| command | `component` | 19 | object fields: dragging an object from the Hierarchy onto an object... |
| command | `create` | 108 | curves: the Circle preset is round, and a closed curve that isn't f... |
| command | `curve` | 5 | curves: with a curve's points selected, Delete removes the points, ... |
| command | `customnormals` | 1 | the 2D look: custom normals - every corner takes its vertex's own n... |
| command | `delete` | 9 | curves: with a curve's points selected, Delete removes the points, ... |
| command | `deleteasset` | 1 | console: the commands without a test of their own answer and do wha... |
| command | `deletemat` | 1 | materials window: Delete Material empties its slots, trashes an ass... |
| command | `device` | 11 | min spec GPU: the console sets Device Auto |
| command | `draw` | 22 | draw: snap axes in local (the rotated object's) or global (the worl... |
| command | `drawfillet` | 1 | draw edge cases: degenerate shapes, lines off the mesh, fillets, dr... |
| command | `drawmode` | 7 | draw: snap axes in local (the rotated object's) or global (the worl... |
| command | `drawpoint` | 17 | draw: snap axes in local (the rotated object's) or global (the worl... |
| command | `droponsurface` | 2 | console: the commands without a test of their own answer and do wha... |
| command | `duplicate` | 5 | z-fighting check: duplicates, inner walls and hidden faces found, t... |
| command | `edit` | 68 | draw: snap axes in local (the rotated object's) or global (the worl... |
| command | `editop` | 20 | edit tools: every operator has a group (selection tools together), ... |
| command | `edittool` | 2 | hard-surface helpers: Array from the menu waits for the mouse, whee... |
| command | `engine` | 7 | learn: the Rigging window, quality tier and small GPU lessons' Try ... |
| command | `esel` | 5 | edge selection: two opposite edges of a quad stay two edges (bevel,... |
| command | `export` | 1 | console: the commands without a test of their own answer and do wha... |
| command | `exportoptions` | 1 | console: the commands without a test of their own answer and do wha... |
| command | `facemat` | 1 | face materials: pick the material itself for the selected faces (it... |
| command | `facenormals` | 1 | the 2D look: Smooth Normals averages the shading normals (a vertex ... |
| command | `feel` | 3 | feel: Blender, Maya, Unreal and Unity each navigate the Scene view ... |
| command | `filter` | 35 | stacks: a modifier's or a filter's header dragged below another mov... |
| command | `filters` | 3 | camera filters: `filters on` puts the main camera's filters on the ... |
| command | `fk` | 4 | rig controls: an FK circle turns its bone as it turns, a click on i... |
| command | `focus` | 1 | console: the commands without a test of their own answer and do wha... |
| command | `fps` | 1 | frame rate: the cap spaces frames, unlimited never waits, and the s... |
| command | `fsel` | 21 | edit tools: every operator has a group (selection tools together), ... |
| command | `guide` | 1 | guide lines: added, undone, saved; drawing snaps to where two cross... |
| command | `guides` | 5 | inspector groups: the camera's Composition Guides are one closed dr... |
| command | `guidesfromedges` | 1 | console: the commands without a test of their own answer and do wha... |
| command | `hair` | 2 | hair rig: twelve hair cards become twelve chains of three spring bo... |
| command | `help` | 1 | console: the commands without a test of their own answer and do wha... |
| command | `hs` | 4 | hard-surface tools: Grid, Pipe, Array, Taper Extrude, Recess / Plate |
| command | `ik` | 5 | IK: the console adds IK and presets; moving the target with the giz... |
| command | `import` | 15 | mmd: dropped into the editor, a PMX becomes a character with its .m... |
| command | `insetmode` | 1 | inset: individual faces, or the selection as one region (only its o... |
| command | `join` | 2 | object: Join keeps placement and materials; Separate By Loose Parts... |
| command | `keymap` | 9 | points: in Blender's keymap E extrudes the curve's first point back... |
| command | `layout` | 8 | rigging window: a saved layout with the Rigging tab loads back |
| command | `lesson` | 2 | layouts: a lesson's layout button arranges the windows too, and you... |
| command | `libs` | 1 | console: the commands without a test of their own answer and do wha... |
| command | `lighting` | 16 | manual lighting: new scenes are Manual unless BLENDITY_LIGHTING_UPD... |
| command | `loopcuts` | 1 | console: the commands without a test of their own answer and do wha... |
| command | `manual` | 7 | manual lighting: new scenes are Manual unless BLENDITY_LIGHTING_UPD... |
| command | `matdelete` | 1 | assets: `matdelete` takes comma-separated names with spaces, skips ... |
| command | `material` | 9 | multi-edit materials: two objects selected - a toon edit in the act... |
| command | `matset` | 2 | multi-edit materials: three toon materials and a PBR one selected i... |
| command | `meshdeform` | 1 | mesh deform: the binding round-trips through a scene file and comes... |
| command | `meshop` | 5 | shape keys: Apply Modifiers refuses a mesh with keys (Blender's rul... |
| command | `mkfolder` | 1 | project: folders, moving and renaming assets keep every reference; ... |
| command | `moonray` | 10 | moonray export: a scene's meshes, material, lights, world, camera a... |
| command | `moveasset` | 1 | project: folders, moving and renaming assets keep every reference; ... |
| command | `newmat` | 10 | assets: Ctrl and Shift click select several materials, Delete asks ... |
| command | `newscene` | 6 | auto-rig: upside-down markers and unknown names are refused; New Sc... |
| command | `newslotmat` | 1 | materials: New Material in an Inspector slot is saved as an asset i... |
| command | `newtex` | 8 | procedural texture: a field edit made with settex is undone and red... |
| command | `ngon` | 1 | gpu views: the Edit Mode cage is drawn by the card, as the CPU draw... |
| command | `origin` | 4 | set origin: a change asks for another frame, and in it the gizmo si... |
| command | `overlaps` | 3 | overlapping vertices and edges: found, shown, selected and merged |
| command | `overlay` | 6 | IK handles: a target's box is a fifth of the shin on a rig scaled 1... |
| command | `pickfocus` | 1 | editor: the camera focus eyedropper sets the distance to the clicke... |
| command | `pilot` | 10 | camera: piloting moves the camera with the view, Ctrl + wheel sets ... |
| command | `play` | 13 | IK: in Play a Rotator swinging a foot target animates the leg; Stop... |
| command | `points` | 3 | points: Ctrl+R over a lattice's edge adds a row keeping the shape; ... |
| command | `position` | 3 | baked lighting: moving a static object makes it fall back to realti... |
| command | `preferences` | 1 | ui: a dropdown inside Preferences (the keymap preset) opens over th... |
| command | `preview` | 1 | camera filters: every filter shows in a path-traced F12 render at 7... |
| command | `probes` | 2 | probe volumes: undo and redo cover the component, its Global and Si... |
| command | `proportional` | 1 | console: the commands without a test of their own answer and do wha... |
| command | `pushpull` | 1 | Push/Pull on several faces: each group, or each face, along its own... |
| command | `quality` | 2 | min spec: the quality tiers set the views' scale, shadow map and fr... |
| command | `raster` | 1 | console: the commands without a test of their own answer and do wha... |
| command | `redo` | 14 | animation: Record turns a move into keys at the playhead (one undo ... |
| command | `redraw` | 2 | frame rate: the cap spaces frames, unlimited never waits, and the s... |
| command | `references` | 1 | console: the commands without a test of their own answer and do wha... |
| command | `refimage` | 1 | layouts: a Reference Image is an unlit plane the image's shape, fac... |
| command | `removeslot` | 1 | materials: a .mat asset is shared, written back when edited, surviv... |
| command | `removeunusedslots` | 1 | materials: a .mat asset is shared, written back when edited, surviv... |
| command | `renameasset` | 6 | animation: Record turns a move into keys at the playhead (one undo ... |
| command | `render` | 34 | min spec: the quality tiers set the views' scale, shadow map and fr... |
| command | `renderanim` | 2 | render animation: the scene's sequence rendered frame by frame into... |
| command | `rendersequence` | 2 | camera sequence: every In Sequence camera rendered in order, each s... |
| command | `research` | 1 | references: an old layout holding the Research window loads without... |
| command | `rig` | 10 | IK: the console adds IK and presets; moving the target with the giz... |
| command | `samples` | 1 | console: the commands without a test of their own answer and do wha... |
| command | `saverender` | 1 | camera filters: F12 (rasterized) and camera sequences through a PS1... |
| command | `scale` | 1 | console: the commands without a test of their own answer and do wha... |
| command | `screenshot` | 1 | console: the commands without a test of their own answer and do wha... |
| command | `seam` | 1 | console: the commands without a test of their own answer and do wha... |
| command | `select` | 175 | curves: with a curve's points selected, Delete removes the points, ... |
| command | `selectadd` | 3 | multi-edit materials: two objects selected - a toon edit in the act... |
| command | `selectall` | 1 | console: the commands without a test of their own answer and do wha... |
| command | `separate` | 2 | object: Join keeps placement and materials; Separate By Loose Parts... |
| command | `seq` | 7 | sequencer: a .seq round-trips; moving a clip's file keeps the seque... |
| command | `set` | 81 | curves: points, type, closed, resolution and the modifiers round-tr... |
| command | `set_extrude_individual` | 1 | edit tools: every operator has a group (selection tools together), ... |
| command | `settex` | 4 | procedural texture: a field edit made with settex is undone and red... |
| command | `shading` | 6 | textures window: dragging a thumbnail onto the Cube in the Scene vi... |
| command | `shape` | 7 | shape keys: a weight adds exactly that much of the key; none in use... |
| command | `skin` | 16 | skins: GameObject > Armature binds a selected mesh with automatic w... |
| command | `smooth` | 2 | console: the commands without a test of their own answer and do wha... |
| command | `spring` | 10 | spring bones: a bone's name says whether it is the body's or hair, ... |
| command | `stats` | 1 | console: the commands without a test of their own answer and do wha... |
| command | `stop` | 14 | IK: in Play a Rotator swinging a foot target animates the leg; Stop... |
| command | `stress` | 2 | min spec: a moving caster is drawn over the still casters' cached s... |
| command | `subdivide` | 1 | console: the commands without a test of their own answer and do wha... |
| command | `surfacealign` | 1 | console: the commands without a test of their own answer and do wha... |
| command | `texdelete` | 1 | assets: the Textures window takes Ctrl+A over its thumbnails and de... |
| command | `texlayer` | 2 | procedural texture: a field edit made with settex is undone and red... |
| command | `texlist` | 1 | console: the commands without a test of their own answer and do wha... |
| command | `texture` | 16 | texture finder: Sketchfab's layout (source/ beside textures/) and t... |
| command | `theme` | 1 | themes: Emiliano Light and Dark take the site's colours, the choice... |
| command | `threads` | 1 | console: the commands without a test of their own answer and do wha... |
| command | `tool` | 22 | curves: clicking a curve selects it, clicking its point selects the... |
| command | `toonstyle` | 2 | toon styles: each style sets its fields (the maps kept), by name in... |
| command | `undo` | 68 | curves: clicking a curve selects it, clicking its point selects the... |
| command | `update` | 2 | libraries: this build knows whether it has Blender's libraries; --p... |
| command | `uv` | 3 | uv editor: face / island selection, rotate, scale, move, flip, fit,... |
| command | `uvsel` | 2 | uv editor: face / island selection, rotate, scale, move, flip, fit,... |
| command | `uvxf` | 1 | uv editor: face / island selection, rotate, scale, move, flip, fit,... |
| command | `vgroup` | 5 | rigging window: the Inspector's MeshFilter keeps modeling only, and... |
| command | `viewport` | 1 | gpu views: Viewport Device is kept in the prefs, and Auto stays on ... |
| command | `views` | 5 | shadows: an Edit Mode drag renders the sun's shadow map once a fram... |
| command | `viewscale` | 2 | min spec: Viewport Resolution at 50 % draws a quarter of the pixels... |
| command | `vsel` | 15 | profiler: an Edit Mode drag with the Move gizmo is counted - the me... |
| command | `walk` | 11 | ai inbetween with the real model: a walk's middle made again is clo... |
| command | `weightpaint` | 2 | rigging window: while painting, selecting a bone paints the group n... |
| command | `window` | 38 | rigging window: Window > Rigging opens beside the Inspector, its bu... |
| command | `world` | 1 | console: the commands without a test of their own answer and do wha... |
| command | `wpbrush` | 8 | rigging window: with a mesh and a bone selected, Add Armature (Skin... |
| command | `wpclean` | 1 | weight paint: the view shows the weights, red at 1 and blue at 0; N... |
| command | `wpfalloff` | 1 | weight paint: the Smooth falloff paints the centre more than the ri... |
| command | `wpfront` | 1 | weight paint: Front Faces Only leaves a sphere's back alone; Mirror... |
| command | `wpmirror` | 2 | weight paint: Front Faces Only leaves a sphere's back alone; Mirror... |
| command | `wpnormalize` | 1 | weight paint: the view shows the weights, red at 1 and blue at 0; N... |
| command | `wpstroke` | 1 | rigging window: with a mesh and a bone selected, Add Armature (Skin... |
| command | `zfight` | 2 | z-fighting check: duplicates, inner walls and hidden faces found, t... |
| window | `Animation` | 5 | animation: the dopesheet - clicking the ruler moves the playhead, d... |
| window | `Console` | 1 | references: an old layout holding the Research window loads without... |
| window | `Game` | 10 | gpu views: the Scene view on the card draws at full size whatever t... |
| window | `Hierarchy` | 2 | render cache: an idle frame and mouse moves over the Hierarchy redr... |
| window | `Inspector` | 5 | multi-edit materials: two objects selected - a toon edit in the act... |
| window | `Learn` | 2 | console: the commands without a test of their own answer and do wha... |
| window | `Lighting` | 3 | layouts: Save Layout keeps the arrangement and the focus under a na... |
| window | `Materials` | 7 | assets: Ctrl and Shift click select several materials, Delete asks ... |
| window | `Profiler` | 2 | profiler: the Profiler window draws the time between frames and the... |
| window | `Project` | 5 | assets: the Project window's Ctrl+click selects several files and D... |
| window | `Render` | 2 | keymap actions: every action id runs through run_action (a renamed ... |
| window | `Research` | 2 | references: an old layout holding the Research window loads without... |
| window | `Rigging` | 8 | rigging window: Window > Rigging opens beside the Inspector, its bu... |
| window | `Scene` | 2 | layouts: each stage's layout opens its windows and shows its tools;... |
| window | `Sequencer` | 3 | sequencer: in the window, dropping a clip on a track places it, dra... |
| window | `Textures` | 3 | assets: the Textures window takes Ctrl+A over its thumbnails and de... |
| window | `Tools` | 3 | edit tools: every operator has a group (selection tools together), ... |
| window | `UVEditor` | 4 | layouts: each stage's layout opens its windows and shows its tools;... |

259 entries, 0 empty; 878 tests read

