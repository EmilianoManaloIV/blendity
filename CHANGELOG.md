# Changelog

One entry per round of requests, newest first: what was asked, what changed, and how it was checked. Earlier rounds are summarised from their commits.

## 2026-10-22 (round 201): the Editing tab at 4K - a preview the window's size, tables for the display axis (task 0191, ADR 0058 amended)

**Asked:** (round 198's measured miss) an 11-step stack on a 4K render took 683 ms, and a slider near the top of the
stack stalled the editor as long.

**Changed**
- The Editing window works on a preview the canvas's size (its longest side in steps of 512 pixels), as Lightroom's
  Develop module does; zoomed in past it, the window shows the full picture. Export, Apply to Texture and the camera
  look use the full picture. The status line says when a preview is shown.
- The display axis through tables (no `pow` per pixel in Highlights / Shadows / Whites / Blacks, Clarity, HSL, Noise
  Reduction); the Tone Curve as one table per channel; Noise Reduction's tones once a pixel and its weights from
  tables; result buffers reused instead of allocated and cleared for each step.
- A step's input size is worked out from the crops and turns before it, not by running them (adding a mask after a
  crop no longer evaluates the stack at full size).

**Checked**
- The preview matches the full result made small for pointwise steps (mean difference 0.00014); a crop on it is the
  crop at its scale; results written into reused buffers equal a fresh document's; Noise Reduction still smooths
  (tests/test_editor.cpp). An Opus review's findings are fixed (spare buffers of an old size kept, a resize remaking
  the preview each frame, an infinite pixel in Noise Reduction, a tiny crop becoming the whole picture).
- Measured (stress `editing`, 3840 x 2160): what the window evaluates while you work, its 1600-pixel preview - the 11-step stack 65.4 ms, a slider on its last step 2.4 ms (task 0181's targets: 150 and 40). At full size, for export: the stack 412.0 ms (683 before), a slider on its first step 392.0 ms (707), on its last 14.0 ms (31); the Tone Curve alone 29.8 ms (347), Noise Reduction 252.1 ms (468).
- Unit checks (round 201): Windows, in render-cache verify mode, 11,634, 0 failed (960 tests), 0 stale
  pictures. Linux with libraries: 11,281, 0 failed. Sanitizer build: 11,157, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `editing`: 0 problems. (the gpu group's slow-open test failed under the parallel load and passed run again alone.)

## 2026-10-22 (round 200): textures through the Editing tab, and the stack as a camera look (task 0183, ADR 0058 amended)

**Asked:** the Editing tab for textures (results kept as texture assets), and a look made there carried live by the
camera.

**Changed**
- Textures window > Edit (or `develop open texture <name>`): the texture's albedo in the Editing tab. Apply to Texture
  (`develop apply`) writes the result beside the asset as `<texture> edit.png` and puts an Image layer on top of the
  texture's stack that writes the albedo only - the procedural layers stay underneath, the layer can be turned off,
  and the asset is saved as any edit of it is. Editing the texture again starts from the albedo under that layer with
  the same adjustments (kept beside the image as `.edit`), so edits go on rather than pile up.
- Use as Camera Look (`develop look`): the stack's global adjustments become the Main Camera's filters, so the Game
  view and F12 carry the look live: Exposure, Contrast, Saturation, Temperature / Tint and the Tone Curve (as lift,
  gamma and gain through black, middle grey and white) into one Color Grading; a darkening Vignette and Sharpen as
  their own. Masked steps and the ones no camera filter does (Highlights, Clarity, HSL...) are left out and named.
  Using it again replaces the look it gave before (the camera's first Color Grading, Vignette and Sharpen are reused).
- Fixed: a texture's Image layer read its file upside down (the layer's rows from the top, the image's from the
  bottom). Image layers now show their file the way up it is; a texture made with one before shows it flipped back.

**Checked**
- An Image layer's top row is its file's top row. A Bricks texture edited and applied: its albedo matches the
  Editing export exactly (0 / 255 apart); editing again reopens the
  same adjustments and Apply reuses the layer; an exposure +1 stack gives the camera a Color Grading with exposure 1
  that doubles a mid-grey pixel; contrast, saturation, a curve (through its black, grey and white), a vignette and
  sharpening carry over, a masked step and Clarity are left out, a second look replaces the first, a stack with
  nothing a camera does is refused (tests/test_editor.cpp). An Opus review's findings are fixed (the flipped Image
  layer, a second Apply not reaching materials, looks piling up, the curve's ends, the vignette's strength).
- Unit checks (round 200): Windows, in render-cache verify mode, 11,625, 0 failed (959 tests), 0 stale
  pictures. Linux with libraries: 11,272, 0 failed. Sanitizer build: 11,148, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `textures`, `filters`, `editing`: 0 problems. (the gpu group's slow-open test failed under the parallel load and passed run again alone.)

## 2026-10-22 (round 199): masks for the Editing tab's adjustments (task 0182, ADR 0058 amended)

**Asked:** local adjustments: each adjustment limited by a mask - a gradient, a luminance or colour range, a painted
mask - and masks combined.

**Changed**
- Each adjustment (but Crop / Straighten and Flip / Rotate) takes a mask: Add Mask > Linear Gradient, Radial
  Gradient, Luminance Range, Color Range or Brush. Parts combine top to bottom by Add (either), Subtract or Intersect
  (both), each invertible; a masked adjustment blends with what came before it by the mask's weight.
- On the picture: the selected part's handles (a radial's centre and outline, a gradient's two ends) drag; a Brush
  part paints with the mouse or a pen (pressure lightens the stroke; Erase or Alt removes), one undo step a stroke;
  Show tints where the adjustment acts in red.
- Masks are fractions of the picture, saved in the `.edit` file (a brush as run-length hex at 512 pixels on its
  longest side). Console `develop mask <step> add|subtract|intersect <kind> [numbers] | paint <u> <v> <radius> |
  invert | remove | clear`, `develop mask show on|off`.

**Checked**
- A radial mask keeps an exposure change inside its radius (a pixel outside within 1e-4 of the source); a luminance
  range doubles a shadow and leaves a highlight; subtract and intersect; invert; a colour range picks red and not blue
  or grey; a brush dab and its erase; the text form of every kind; a crop ignores a mask; the console's mask commands,
  undo, the .edit file and Show (tests/test_editor.cpp).
- Unit checks (round 199): Windows, in render-cache verify mode, 11,603, 0 failed (957 tests), 0 stale
  pictures. Linux with libraries: 11,250, 0 failed. Sanitizer build: 11,126, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `editing`: 0 problems. (the gpu group's slow-open test failed under the parallel load and passed run again alone.)

## 2026-10-22 (round 198): the Editing tab - develop a render or an image, non-destructively (task 0181, ADR 0058)

**Asked:** a new Editing tab with basic Lightroom / Photoshop adjustments on renders, images and textures,
extensible.

**Changed**
- Window > Editing (and the Editing layout): a picture with an adjustment stack beside it. Open an image (or drop one
  on the window), or the Render window's last render - its light, shown through the render's own view transform.
  Seventeen adjustments, applied top to bottom in linear light: Exposure, Contrast, Highlights, Shadows, Whites,
  Blacks, Temperature / Tint, Vibrance / Saturation, Clarity, Dehaze, Tone Curve (RGB and per channel: click, drag,
  right-click), HSL (eight hue bands), Sharpen, Noise Reduction, Vignette, Crop / Straighten, Flip / Rotate. Each step
  turns off, moves or goes; Undo / Redo; a Before / After line dragged across the picture; presets (Neutral, Film,
  Anime Pop, Manga Contrast, Cinematic Teal-Orange).
- Non-destructive: the source is never written. Save keeps the stack as an `.edit` text file beside the image (a
  render's pixels beside it as EXR, or HDR where the build writes no EXR); Export writes PNG / JPEG through the view, or EXR / HDR in linear light.
- A slider reruns only the steps from the one changed (each step's result is cached).
- The camera filters' Color Grading and Vignette share their formulas with the Editing tab (`image/grade.h`).
- The console: `develop open <file>|render`, `add`, `set <Type>.<Field> <value>`, `remove`, `move`, `enable`, `curve`,
  `preset`, `undo`, `redo`, `export`, `save`, `split`, `list` (`edit` stays Edit Mode's).

**Checked**
- Exposure +1 doubles a mid-grey pixel; Contrast 0 is the identity; Highlights -100 lowers 0.9 and leaves 0.2; a tone
  curve through (0.5, 0.75) lifts 0.5 to 0.75; HSL red -100 greys red and leaves blue; every adjustment is the
  identity at its defaults and survives its text form; crop, straighten, flips and quarter turns move the right
  pixels; a slider on the last step reruns one step; undo; save and reopen reproduce the export byte for byte; every
  preset; the window through the console, the split, export (tests/test_editor.cpp).
- Measured (stress `editing`, 3840 x 2160): the 11-step develop stack 683.2 ms in full, a slider on its last step 30.7 ms (1 step rerun), on its first 706.8 ms; the slowest adjustment alone Noise Reduction 467.8 ms. Task 0181's targets at 3840 x 2160: the full stack 683.2 ms (at most 150: missed), a slider on the last step 30.7 ms (at most 40: met). The full stack is the Tone Curve (347 ms alone) and Noise Reduction (468 ms) at full resolution: a following round takes it up (a preview at the screen's size, as Lightroom's, and look-up tables).
- Unit checks (round 198): Windows, in render-cache verify mode, 11,569, 0 failed (955 tests), 0 stale
  pictures. Linux with libraries: 11,216, 0 failed. Sanitizer build: 11,092, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `editor_render`, `filters`, `editing`: 0 problems. (the sanitizer leg's autopose timing check failed under the parallel load and passed run again alone.)

## 2026-10-22 (round 197): the UV Editor on one material's faces (task 0186)

**Asked:** the UV Editor showing one material's islands: a Material filter, the others dimmed or hidden; "Edit UVs"
from the Materials window; the background following the chosen material.

**Changed**
- UV Editor > the Material picker (All materials, then each slot): only that material's faces are drawn as editable,
  picked, selected, moved, unwrapped (Unwrap, Smart UV, Part Unwrap, projections) and packed; the others are a faint
  outline behind, or hidden (`uv others hide`), and keep their UVs. The Material Texture background is the chosen
  slot's. Console `uv material <name>` / `+<name>` (adds a material) / `all`; Materials window > Edit UVs.

**Checked**
- On a cube with two materials, the second chosen: Unwrap changes only its faces, Select All selects only its corners,
  Pack puts them in 0-1 and leaves the first material's UVs exactly; dimmed, hidden, adding a material, All, an unknown
  name refused (tests/test_modeling.cpp).
- Unit checks (round 197): Windows, in render-cache verify mode, 11,460, 0 failed (951 tests), 0 stale
  pictures. Linux with libraries: 11,107, 0 failed. Sanitizer build: 10,984, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `editor_render`, `uv`: 0 problems.

## 2026-10-22 (round 196): PartField's part tree for Part Unwrap, opt-in (task 0180, ADR 0057 amended)

**Asked:** an opt-in "semantic parts" switch: PartField's learned per-face features (NVIDIA; non-commercial weights,
never downloaded by Blendity) in place of the geometric part tree, as PartUV uses them.

**Changed**
- `uv parts semantic on|off` (off by default): Part Unwrap's part tree from PartField's features when the user has
  installed a conversion of the model (`~/.blendity/ai/partfield/partfield.onnx`, through the ONNX Runtime of the AI
  install); the geometric tree otherwise, the Console saying which and why. `src/scene/partfield.*`: the model's
  contract (face centroids and normals in -> a feature per face out), rows scaled to unit length; the part tree takes
  features of any width (wider than 32 through a fixed random projection).
- `tools/partfield/`: the conversion script the user runs with PartField's code and their weights, and its README.

**Not done:** the roadmap's spike - does PartField export to ONNX? - needs PartField's code and weights, which are
NVIDIA's under a non-commercial licence; downloading them is the user's decision, so it wasn't run. The export script
is written against the contract with placeholder names for PartField's encoder and feature query (its README says so,
and names the fallback: features written by PartField in Python, read the same way).

**Checked**
- With a stand-in model, the features reach the part tree: the teapot's charts come out otherwise (10 against the
  geometric tree's 11), every chart under the bound, also with features 448 wide; the switch is off by default, the
  geometric tree is used exactly when nothing is installed, the editor's result is the stand-in's exactly
  (tests/test_modeling.cpp).
- Unit checks (round 196): Windows, in render-cache verify mode, 11,450, 0 failed (949 tests), 0 stale
  pictures. Linux with libraries: 11,097, 0 failed. Sanitizer build: 10,974, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`, `modifiers_ngon`, `editor_render`, `uv`: 0 problems,
  394 results with faces newly on top of each other in `pushpull`, no change. (the gpu group's slow-open test (a GPU opening in the background, timed) failed under the parallel load, then in two of three runs alone (one while another build loaded the machine, one on a quiet machine) and passed in the third: a flaky timing test, not changed by this round; the sanitizer leg's autopose timing likewise failed under load and passed alone.)

## 2026-10-22 (round 195): unwrap by parts - PartUV's search on a geometric part tree, ABF++ beside LSCM (task 0179, ADR 0057)

**Asked:** a part-based unwrapper after PartUV (SIGGRAPH Asia 2025): its geometric core (the PartField model is task
0180, opt-in).

**Changed**
- UV Editor > Part Unwrap (also the UV menu, Modeling Tools, console `uv parts [bound] [multi|single]`): as few charts
  as keep every chart's distortion - the area-weighted mean of max(s, 1/s) over its faces, s a face's share of the UV
  area over its share of the 3D area - under the bound (PartUV's 1.25). A part tree clustered from the geometry
  (normal and position, across manifold edges only; material slots joined last) is searched top down: a part becomes
  one chart if it passes, else the fewest charts of PartUV's Normal heuristic (clustered by normal, 2..10) or Merge
  heuristic (the part's oriented-box labels, merged greedily) that all pass, else its two children. A chart never spans
  two material slots; a chart whose outline crosses itself or whose triangle flips fails; a non-manifold edge is a cut.
- ABF++ (`src/scene/uv_abf.cpp`, ported from Blender's Angle Based unwrap: angles moved least so a flat layout exists)
  then LSCM on a chart that LSCM alone nearly passes (within 1.5 x the bound), kept when better; LSCM screens every
  candidate (one solve). LSCM takes per-triangle angles (`uv_internal.h`).
- Each part's charts packed together as one block, the blocks at one texel density with a gap between them; or each
  separate piece on its own UDIM tile (u 0-9, then a row up). The top-level parts are solved on threads.
- `uv report`: any UV map measured PartUV's way - charts, seam length, distortion.
- The tests' scratch folder comes from `BLENDITY_TEST_DIR` when set: the check chain runs the test groups as parallel
  processes, and a shared temporary folder had let one group's files become another's (a texture deletion and an
  export read-back failed under load in round 192's chain, then passed run alone).

**Measured** (stress `uv`, "Part Unwrap (task 0179)")
- cube + boss + torus (416 faces): Smart UV Project 22 charts, seams 32.56 m, distortion 1.05 / 1.17, 0.4 ms; Part Unwrap 6 charts, seams 16.90 m, distortion 1.13 / 1.22, 35.9 ms (3 parts, 42 tried, 1 ABF++).
- teapot (2.0k faces): Smart UV Project 58 charts, seams 42.94 m, distortion 1.08 / 1.20, 1.5 ms; Part Unwrap 11 charts, seams 15.74 m, distortion 1.13 / 1.18, 108.0 ms (4 parts, 101 tried, 2 ABF++).
- 50k-face sphere, 12 slots (50.2k faces): Smart UV Project 8 charts, seams 22.90 m, distortion 1.09 / 1.10, 40.5 ms; Part Unwrap 12 charts, seams 37.70 m, distortion 1.03 / 1.03, 265.9 ms (12 parts, 12 tried, 0 ABF++).

**Checked**
- A cube with a cylinder boss and a torus: six charts, the fewest face sets allow (each closed piece needs two), a
  third of Smart UV Project's 22, half its seam length, every chart under the bound, no folds or overlaps, ABF++ used;
  a 12-slot sphere's charts each one slot; a non-manifold fin a cut; twelve pieces on twelve UDIM tiles; the Edit Mode
  mask; ABF++ closing every triangle at pi and every interior vertex at 2 pi with less angle error than LSCM alone; the
  console's `uv parts` (a non-number refused), `uv report` and undo (tests/test_modeling.cpp). An Opus review's
  findings (neighbours lost in the clustering, a wasted Merge pass, UDIM numbering, blocks touching, self-overlap,
  the bound's parsing, threads and packing costs) are fixed.
- Unit checks (round 195): Windows, in render-cache verify mode, 11,438, 0 failed (948 tests), 0 stale
  pictures. Linux with libraries: 11,085, 0 failed. Sanitizer build: 10,962, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`, `modifiers_ngon`, `editor_render`, `uv`: 0 problems,
  394 results with faces newly on top of each other in `pushpull`, no change. (the gpu group's slow-open test failed under the parallel load and passed run again alone.)

## 2026-10-22 (round 194): the Godot and Unreal bridges, specified (task 0175)

**Asked:** Godot and Unreal bridges on the same protocol, after Blender's and Unity's.

**Changed**
- `docs/tasks/0175-godot-unreal-bridges.md`: what each bridge does against `blendity-livelink/1` unchanged - Godot 4
  as a GDScript editor plug-in (`StreamPeerTCP`, an own glTF reader into `ArrayMesh` surfaces, `StandardMaterial3D`
  with Godot's toon diffuse and specular, the cameras and lights turned onto Godot's -Z), Unreal 5 as a C++ editor
  module (`FSocket`, `FMeshDescription` to a static mesh and a dynamic mesh once vertices move, a master material's
  instances, Unreal's centimetres and the basis its own glTF importer uses), each with a smoke test replaying the
  recorded session. Each gets its own task when started; the protocol needed nothing new.

**Checked**
- Documentation only: no code changed, the check chain wasn't run again (round 192's checks hold).

## 2026-10-22 (round 193): the Unity bridge - Blendity's scene live in the Unity Editor (task 0174)

**Asked:** a Unity bridge for Live Link, after Blender's.

**Changed**
- `bridges/unity/com.blendity.livelink/`: a Unity 2022.3 package - Window > Blendity Live Link: Port, Connect.
  GameObjects under "Blendity Live Link" with a `BlendityLink` (the id); Blendity is Unity's own left-handed Y up, so
  mirroring glTF's X back gives Blendity's numbers exactly. Meshes with a submesh per slot and each vertex's mesh
  vertex kept for deforms; URP Lit or Standard materials with base and normal maps; Toon fields per slot in a
  `BlendityToon` component; lights with Blendity's intensity as Unity's; cameras (disabled, so the Game view stays
  yours); bones as posed GameObjects. An own glTF reader (`GlbReader.cs`; glTFast drops the `_VERT` attribute).
- `bridges/unity/ReplayCheck`: the package's framing, JSON and glTF reading on the recorded session with any C#
  compiler.

**Checked**
- ReplayCheck, compiled with the .NET Framework's C# compiler, replays the recorded session: 32 messages, every
  mesh's vertices within its mesh, the deform applied. Not checked: the Unity-side code (the applier, the window, the
  EditMode test) has not been compiled or run - Unity isn't installed on this PC; the README's Test section is the
  first run.
- No C++ changed: the check chain wasn't run again (round 192's checks hold).

## 2026-10-22 (round 192): the Blender bridge - Blendity's scene live in Blender (task 0173)

**Asked:** a Blender bridge for Live Link (one way), the first of the four.

**Changed**
- `bridges/blender/blendity_livelink/`: an add-on (and a Blender 4.2+ extension) - View3D > Sidebar > Blendity: Port,
  Connect. The objects live in a collection "Blendity Live Link" (custom property `blendity_id`); hierarchy,
  transforms (glTF's Y up to Blender's Z up as Blender's glTF importer converts), meshes with Blendity's own vertices
  (the binary's split vertices folded back by `_VERT`, so moving vertices land in place), UVs, Blendity's hard edges as sharp edges (so the
  normals follow moving vertices), a material per slot; Principled materials with their image textures (tint, alpha
  and cutouts included); Toon materials as an EEVEE cel setup (Shader to
  RGB, a constant ramp of Toon Bands) with every field kept as `blendity_toon`; suns, points, spots and area lights in
  Cycles' units matched to Blendity's path tracer (as the MoonRay export matches them); cameras; bones as posed
  empties. A timer reads the socket with a 20 ms budget a tick and leaves the rest in it, so Blendity's queue keeps
  replacing stale updates.
- Meshes are read by the add-on (numpy), not through `bpy.ops.import_scene.gltf` as the roadmap planned: the importer
  splits vertices by normal and UV (a deform would have nothing to land on) and needs a window context a timer lacks.

**Checked**
- `bridges/blender/smoke_test.py` in Blender 5.2.2 headless replays the recorded session: every object, the Sphere
  deleted, the Box at Blender (-1, -3, 2), its Cel material recoloured with its cel setup, the moved vertex, the
  cube's 12 hard edges sharp, the camera's 60 degrees and direction, the sun at pi W/m2, the bone hierarchy and its
  30 degree pose, an object deleted in Blender made again by its next message. An Opus review's findings (camera and
  light scale, stale references, tint and cutouts, normals after deforms, inactive parents) are fixed. Live: the add-on
  connected to a running Blendity and received the sync with every mesh. Blender's extension validator accepts the
  manifest.
- Unit checks (round 192): Windows, in render-cache verify mode, 11,417, 0 failed (943 tests), 0 stale
  pictures. Linux with libraries: 11,064, 0 failed. Sanitizer build: 10,944, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`, `modifiers_ngon`, `editor_render`: 0 problems,
  394 results with faces newly on top of each other in `pushpull`, no change. (in the parallel runs, the gpu group's slow-open test, a Linux texture-deletion test and a sanitizer export read-back failed under load and passed run again alone.)

## 2026-10-22 (round 191): Live Link - the scene sent live to Blender, Unity, Godot or Unreal (task 0172, ADR 0055)

**Asked:** "bridges" in Blender, Unity, Godot and Unreal applying Blendity's changes in real time, Blender and Unity
first, one way. This round is the editor's side and the protocol; the Blender bridge is next (task 0173).

**Changed**
- Window > Live Link and the console's `livelink start [port] | stop | status`: a TCP server on 127.0.0.1 (port
  40100 by default; `src/core/net_server.*`, WinSock or BSD sockets, no library). A client gets the whole scene, then
  every change as it happens: objects and their hierarchy, transforms, a rig's pose, meshes as glTF binaries (built on
  a worker thread), vertices that moved as a `deform` (float32 positions: an Edit Mode drag, skinning, physics),
  materials (glTF's object, the toon fields, texture paths), lights, cameras, deletes, Play. The protocol,
  `blendity-livelink/1`, is in `docs/LIVELINK.md`; `tools/livelink_dump.py` is a 50-line client that prints it.
- What changed is found by comparing the scene with what was sent, at most 60 times a second and only while a client
  is connected (the roadmap had planned a diff at each undo step: comparing carries a drag while it happens, and undo,
  Stop and reloads need nothing of their own; ADR 0055).
- Each client has its own queue and thread; a newer transform, pose, deform, material, light or camera replaces one
  still queued (never past an object, mesh or delete), so a slow client skips to the present; a slow client is waited
  for, never timed out; one past 256 MB queued is closed. Sockets aren't inherited by programs the editor starts.
- glTF export: each primitive names its material slot (`extras.slot`), an optional `_VERT` attribute (each vertex's
  mesh vertex), and a Toon material carries the extension `BLENDITY_toon` with every field by its Inspector name (in
  exported files too: other readers ignore it). `JsonWriter` in json.h; `reflect_to_json`.
- `tests/fixtures/livelink_stream.bin`: a recorded session (the sync, a move, a recolour, moved vertices, a pose, a
  delete, Play and Stop) that the suite records again and compares message by message, and the bridges replay.
- The check chain, faster: `build.bat check` compiles every source once and links the unit and stress tests from the
  same objects (92 s against 112 s for the two builds); the unit suite runs once, in render-cache verify mode (the
  plain run checked nothing verify mode doesn't), its groups as parallel processes with a failed group run again
  alone; the Linux and sanitizer legs start with the Windows build and run their groups in parallel too, and their
  copy of the sources skips tools/inbetween's Python environment (24k files: the sync's 4 minutes). The stress
  sections still run on a quiet machine, after the Linux legs. `BLENDITY_NO_LTCG=1` builds without whole-program
  optimization for quick local runs only: with /fp:fast, MSVC then compiles two code paths differently that two
  tests compare bit for bit.

**Measured** (stress `livelink`, the RTX 4070 SUPER PC, 1,000 cubes moved every frame)
- A frame (mean / most, ms) with the link off 11.49 / 13.14, on without a client 11.46 / 12.93, with one client draining 12.50 / 14.16, with two 13.10 / 31.04.
- Comparing the scene with what was sent (1,000 moved objects: 1,000 transforms) 1.23 / 1.50 ms mean / most with one client, 1.42 / 3.50 with two; 60240 and 120491 messages, 5.49 and 10.98 MB in the 120 frames.
- A client's sync with a 95232-face mesh: the frames at most 11.42 ms; the worker built 1005 glTF binaries in 182 ms; the sync took 777 ms of frames.
- A vertex of the 95232-face mesh moved every frame: 14.57 ms a frame, 64 deforms, 69.76 MB (95234 vertices: 1.09 MB a deform).
- A client that leaves while a mesh is going out: the frames at most 2.17 ms over the next 30 frames; 0 client(s) left connected.
- One client cost the frame 1.00 ms on average against the link off (12.50 vs 11.49 ms; the views' rendering is most of a frame).

**Checked**
- A client gets hello, the sync with the scene's object count and every mesh as a valid glTF binary (indices 3 x the
  triangles, `_VERT` for every vertex); a move is one transform in glTF's axes, a recolour one material, moved vertices
  one deform, new faces one mesh, a posed rig one pose with every bone, a deletion one delete, no change nothing; in
  Play at most 60 transforms a second, in order, the last where the object ended; two clients get the same stream; a
  client that leaves mid-send is dropped and a new one syncs; held queues replace a keyed message only after the last
  barrier (tests/test_livelink.cpp). Play then Stop sends only the two play messages.
- Unit checks (round 191): Windows, in render-cache verify mode, 11,417, 0 failed (943 tests), 0 stale
  pictures. Linux with libraries: 11,064, 0 failed. Sanitizer build: 10,944, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`, `modifiers_ngon`, `editor_render`, `livelink`: 0 problems,
  394 results with faces newly on top of each other in `pushpull`, no change. (the gpu group's slow-open test failed under the parallel load and passed run again alone.)

## 2026-10-22 (round 190): the OptiX render device - the same picture as GPU Compute, opt-in (tasks 0170-0171, ADR 0054 amended)

**Asked:** an OptiX render device with PBR parity (a picture within 0.01 of GPU Compute's, 1.4x its samples a second on
the RTX 4070 SUPER and 1.8x on the 3060 Ti), and the toon shading on it (task 0171).

**Changed**
- One integrator for both devices: `tools/glsl_to_cuda.py` translates the GPU path tracer's own GLSL (gpu_kernel.h,
  gpu_glsl.h) to CUDA when the PTX is embedded - buffers as launch-parameter pointers, `out` parameters as references,
  GLSL's constructors as braces, every function inlined, the kernel a struct over `src/render/optix/glsl.cuh`'s GLSL
  types and built-ins. Only the rays differ: optixTrace through structures the driver builds, where GPU Compute walks its
  BVH or uses Vulkan's ray queries. Toon shading (task 0171), the lods of 0164 and the area lights of 0163 come with it.
- The OptiX device (`src/render/optix_device.cpp`, in place of task 0097's spike): a `gpu::Renderer` with the Vulkan
  device's buffers, a triangle structure per mesh, the objects as instances, eight samples a launch, the card's time from
  events, the bake points. The committed PTX is now text in pieces (3.6 MB).
- Render Settings > GPU Backend: Auto, Vulkan, OptiX. **OptiX is opt-in: Auto keeps GPU Compute** - measured slower,
  below. The renderer pool keys on the backend, the tracer reopens a card when it changes, the device summary says
  "(OptiX)"; `BLENDITY_GPU_BACKEND` overrides it (the tests and the stress run pin Vulkan; the OptiX tests ask for it).

**Measured**
- The stress gpu_optix scene (1920x1080, 256 icospheres and a floor, a sun, 4 bounces, 64 samples, each backend's best of two runs, a quiet machine):
  - NVIDIA GeForce RTX 4070 SUPER - GPU Compute: 568.5 samples a second (1.76 ms a sample), its upload 406.4 ms.
  - NVIDIA GeForce RTX 4070 SUPER - OptiX: 182.9 samples a second (5.47 ms a sample), its upload 352.6 ms, 0.32x GPU Compute's, the pictures 0.00000 apart.
  - NVIDIA GeForce RTX 3060 Ti - GPU Compute: 351.8 samples a second (2.84 ms a sample), its upload 206.5 ms.
  - NVIDIA GeForce RTX 3060 Ti - OptiX: 70.1 samples a second (14.28 ms a sample), its upload 12922.0 ms, 0.20x GPU Compute's, the pictures 0.00000 apart.
- Before the inlining and the register limit: 0.09x on both cards; the first open of a card compiles the 3.6 MB PTX in about 20 s without the driver's cache.

**Checked**
- OptiX and GPU Compute render the same picture at 64 samples - spheres, a glass ball, a toon ball, a checker floor, a
  sun and a point light - mean difference 0.00000 a channel, with content in it (luminance 0.09 to 3.2); a build
  without OptiX or a card that isn't there is refused with the reason (tests/test_gpu.cpp).
- Unit checks (round 190): Windows 11,305, 0 failed (933 tests); render-cache verify mode 11,306, 0 failed, 0 stale
  pictures. Linux with libraries: 10,961, 0 failed. Sanitizer build: 10,841, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`, `modifiers_ngon`,
  `editor_render`: 0 problems, 394 results with faces newly on top of each other in `pushpull`, no change.

## 2026-10-22 (round 189): OptiX ships - committed PTX, NVIDIA's headers at build, the driver at run time (task 0169, ADR 0054)

**Asked:** OptiX "as seamless as Blender's Cycles", shipped by embedding PTX compiled on the user's PC.

**Changed**
- The OptiX kernel's PTX is committed (`src/render/optix/optix_kernel_ptx.h`: 34,373 bytes from OptiX 9.1.0, with
  the FNV-1a hash of `optix_kernel.cu` and `optix_params.h` and the OptiX version). `tools/optix_ptx_embed.bat`
  writes it on a machine with the CUDA Toolkit and the SDK (`tools/ptx_compile.cpp --header=`); a unit test fails
  when the kernel changes without it.
- A build needs only OptiX's host headers now, no CUDA Toolkit: `OPTIX_ROOT_DIR`, NVIDIA's ProgramData folder, or -
  only with `BLENDITY_FETCH_OPTIX=1` - NVIDIA's public `optix-dev` headers cloned at `v9.1.0` into `build/optix/sdk`
  (taken from NVIDIA under NVIDIA's terms; nothing NVIDIA's is committed but the PTX built from Blendity's kernel).
  `build.bat` and `build.sh` (Linux) both; CI's Windows and Linux jobs and the release workflow set the switch, so
  the installers carry the OptiX device (ADR 0037's "never" ends).
- `BLENDITY_NO_OPTIX=1` also turns OptiX off at run time; the console's `gpu list` names every card Vulkan sees and
  OptiX's state (not in this build, turned off, no driver and why, or its devices).
- The fetch path is written but has not been run on this PC (it downloads); CI runs it.

**Measured**
- The committed PTX: 34,373 bytes from OptiX 9.1.0 (the header 114 KB of text); its hash matches the sources.
- A Windows build with `CUDA_PATH` empty (no CUDA Toolkit) and the SDK's headers from ProgramData: OptiX built in,
  `gpu list` names the RTX 4070 SUPER and the RTX 3060 Ti.
- The Linux build (WSL, `OPTIX_ROOT_DIR` at the Windows SDK's headers): OptiX built in from the committed PTX, and
  through WSL's NVIDIA driver the same two devices; the OptiX tests pass there.
- Not run here: the fetch of NVIDIA's `optix-dev` headers (`BLENDITY_FETCH_OPTIX=1`: it downloads; CI runs it).

**Checked**
- The committed PTX's hash matches the kernel's sources; `gpu list`'s OptiX line names the RTX 4070 SUPER and the
  RTX 3060 Ti here; `BLENDITY_NO_OPTIX=1` turns it off at run time (tests/test_gpu.cpp).
- Unit checks (round 189): Windows 11,299, 0 failed (931 tests); render-cache verify mode 11,300, 0 failed, 0 stale
  pictures. Linux with libraries: 10,961, 0 failed. Sanitizer build: 10,841, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`, `modifiers_ngon`,
  `editor_render`: 0 problems, 394 results with faces newly on top of each other in `pushpull`, no change.

## 2026-10-22 (round 188): hair as mass-weighted XPBD rods - film feel, not jello (task 0168, ADR 0049 amended)

**Asked:** hair physics "stiff and jello-like"; Blender-hair-dynamics / film feel, research based, for hair and other
dynamic parts.

**Why it felt that way (measured):** each substep blended a tip toward the animated pose by a stiff per-substep
factor - about a 5 Hz spring at hair's 0.15 - which pinned a strand to the pose (a 40 cm strand held horizontal
sagged 1.6 cm) and rang against it (14 wiggles of over 2 mm before it settled, 15 after a head turn).

**Changed** (Macklin et al. 2016 and 2019, Müller et al. 2007, Kugelstadt & Schömer 2016, Blender's hair dynamics)
- Strands (hair, ears, tails, cloth) are mass-weighted XPBD rods: each tip a mass from its bone's length and the
  chain's new **Density** (kg/m: hair 0.01, an ear 0.02, cloth 0.05, a tail 0.1), substeps at 240 Hz with one
  iteration each, velocities from positions.
- Stiffness and Bend are compliant constraints: a slider is a spring of 20 Hz x s^1.5 for hair's mass (1 rigid),
  applied as XPBD's w / (w + alpha / h^2), so the feel doesn't change with the frame rate and a heavier strand is
  pulled less by the same slider.
- Damping is a decay of the motion relative to where each tip's animation is going (a move or a turn alike; per
  bone from its effective spring, so free tips aren't overdamped): a lag that doesn't ring. The air's drag is always
  on (new **Air**, 1/s for hair's density; a heavier chain feels less); a Wind pulls every chain toward its motion
  by the chain's Wind, as before. The velocity is rescaled when a frame's substeps change length.
- Old scenes keep their sliders (which now mean the new spring) and load their label's Density and Air.
- The lengths are mass-weighted distance constraints before the exact root-first pass; the bend is applied after
  them, so the lower bones' weight doesn't open a curl's upper joints.
- Hair's defaults: Stiffness 0.3 and Damping 0.5 (were 0.15 / 0.15), near critical damping for the new spring. A
  scene saved before keeps its sliders, which now mean the new spring.
- Soft tissue keeps its own spring (the verlet code it was tuned with, its wind included).

**Measured**
- A 40 cm strand of eight bones (hair's defaults) released horizontally: settled within 1 cm after 0.75 s with no
  overshoot, its tip hanging 0.177 m below the head; before: 0.80 s with 14 overshoots of over 2 mm, the tip held at
  0.016 m (pinned to the pose).
- A head turned 90 degrees in 0.3 s with the strand pointing back: the tip lagged the turn by up to 9.3 degrees and
  came within 1 cm of its rest 0.73 s after it, one overshoot. (Before, measured with the strand hanging down: 0.88 s,
  15 overshoots.)
- The tip 0.25 s after release, mid-fall: 0.0060 / 0.0006 / 0.0004 m from 60 fps's at 30 / 144 / a frame time
  wavering between 1/60 and 1/59 s; the hanging tip after 4 s: 0.0000 m apart at all three (before 0.0027 at 144).
- Ten times hair's density: the tip sags 0.386 m (0.177 at hair's) and settles after 0.98 s (0.75).
- A curl of 30 degrees a joint with Bend 0.5 hanging from its root for 4 s keeps 89 % of it; Bend 1 keeps its joints
  at 40.0 degrees.
- 100 spring chains of 3 bones, ms a frame (stress physics): 0.366 plain, 0.454 with collision, 0.559 with separation too; was 0.548, 0.485, 0.730.
- 100 spring chains of 10 bones, ms a frame (stress physics): 1.445 plain, 1.752 with collision, 2.276 with separation too; was 2.020, 2.175, 2.862.
- 100 spring chains of AutoPhysics clip (100 frames), ms a frame (stress physics): solved plain, passes with collision, steps with separation too; was solved, passes, steps.
- 100 spring chains of a jump, ms a frame (stress physics): yes plain, 5 with collision, 4230 with separation too; was yes, 5, 4230.
- 100 spring chains of a run, ms a frame (stress physics): yes plain, 5 with collision, 8040 with separation too; was yes, 5, 8040.

**Checked**
- A 40 cm strand released horizontally settles within 1 cm in 1.5 s with at most one overshoot; a head turned 90
  degrees in 0.3 s swings a strand pointing back through its arc, lagging the turn, and brings it round within 1 s
  with at most one overshoot; the same motion at 30, 60 and 144 frames a second and with the frame time wavering;
  a curl with Bend 0.5 keeps a share of its curl hanging under its weight; ten times hair's density sags further and
  settles more slowly; Density and Air are saved, an old scene's chain loads its label's (tests/test_physics.cpp). The
  round 153 Bend test measures the joints' angles now (40.0 and 40.0 degrees held with Bend 1): with no pull and a
  free root the curl turns about its root until its weight hangs below, as a real one does.
- Unit checks (round 188): Windows 11,293, 0 failed (929 tests); render-cache verify mode 11,294, 0 failed, 0 stale
  pictures. Linux with libraries: 10,955, 0 failed. Sanitizer build: 10,835, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`, `modifiers_ngon`,
  `editor_render`: 0 problems, 394 results with faces newly on top of each other in `pushpull`, no change.

## 2026-10-22 (round 187): undo measured honestly, and it gives back Edit Mode's selection (task 0167, ADR 0053 amended)

**Asked:** the roadmap's "undo steps sharing unchanged arrays" (each undo ≤ 10 ms, was 30-38).

**Measured first** (the task doc): the undo stack already shares meshes by pointer (ADR 0003) - a scene clone is
0.02 ms and `undo` itself 0.1 ms. An undo's cost is its frame: every view and the shadow map drawn again. The stress
row "Undo after it" averaged three different undos (the extrude, the selection step, the Edit Mode entry), so its
21-25 ms was not an undo's frame.

**Tried and not kept:** view caches two pictures deep (and four shadow maps), so an undo would copy the pictures of
the state before. A restored snapshot never matched its earlier key: `Scene::clone` gives the snapshot a new serial
and copies its materials, and the keys hash both. Keys by content for the scene's identity and its materials would
touch every render cache's exact-picture rule (ADR 0009); ADR 0053's amendment records it as the next step. The
first build also showed that a cached Scene view picture must carry its depth and id planes (picking reads them):
verify mode caught the older picture over the newer planes.

**Changed**
- Undo gives back Edit Mode's selection as it was before the operator, as Blender does; redo the operator's. It
  stayed as the operator left it, sized for the bigger mesh.
- The stress rows "Undo of an extrude" (each rep undoes an extrude it made itself) and "Redo of it" replace "Undo
  after it"; `tools/bench_table.py` reads the new row into Pass 13.

**Measured**
- The stress rows with the views on the CPU: "Undo of an extrude" 27.7 ms a frame (the frame after 2.0), "Redo of it" 21.0 ms (the frame after 1.9); the old row "Undo after it" - three different undos averaged - was 25.5 ms with the frame after 15.3.
- The stress rows with the views on the GPU: "Undo of an extrude" 11.0 ms a frame (the frame after 1.7), "Redo of it" 10.8 ms (the frame after 1.8); the old row "Undo after it" - three different undos averaged - was 11.6 ms with the frame after 7.5.
- The probe before this task (task 0165's): one undo's frame 12.5 ms on the CPU - the Scene view 7.9 ms, the shadow map 3.9, the Game view 3.4 - with `undo` itself 0.1 ms. Blender 5.2.2's row: 16.4 ms (PERFORMANCE.md Pass 13).

**Checked**
- After an extrude of three faces, undo selects the three faces again and redo the extruded ones
  (tests/test_editor.cpp).
- Unit checks (round 187): Windows 11,283, 0 failed (926 tests); render-cache verify mode 11,284, 0 failed, 0 stale
  pictures. Linux with libraries: 10,945, 0 failed. Sanitizer build: 10,825, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`, `modifiers_ngon`,
  `editor_render`: 0 problems, 394 results with faces newly on top of each other in `pushpull`, no change.

## 2026-10-22 (round 186): a selection changes the selection, not the mesh (task 0166)

**Asked:** the roadmap's "selection changes redraw flags, not the view" (Select More on 50k faces 55-68 ms against
Blender's 15.5 ms).

**Measured first** (the task doc has the probe): Select More's frame after an undo copied the mesh (the selection-only
operators ran inside the operator path, after the mutable copy), so every view saw a "new" mesh - two render meshes
updated in place, the shadow map and the Game view drawn again - and the command itself built a vertex adjacency on
each call (8 ms). With the views on the card, a face selection sent the whole edit object to the CPU ("Edit Mode's
face highlight" was a fallback reason). A plain selection change's frame is 9 ms, of which the Scene view's 3D layer
about 3 ms and its cage overlay 4 ms: the overlays are part of the cached layer, so the planned "selection out of
the cache key" would save the 3 ms and cost the cage every idle frame, or a second cached layer. Not done; the
frame's cost is the cage, which is already drawn in parallel.

**Changed**
- The selection-only operators (Select Similar / By Material / Sharp / Z-Fighting / Coplanar / Linked / More / Less /
  Invert / Non-Manifold / Ring / Loop) run before any copy, on the mesh the undo step shares, as `const Mesh &`
  (`Editor::select_op`): no copy, no "new" mesh, only the Scene view - whose cage shows the selection - draws again.
  Loop Cut and Select Loop share `selected_edge`.
- Select More / Less are one pass over the cached edge table (`grow_selection` built an adjacency per call).
- The card draws Edit Mode's face highlight: a bit per triangle of the edited mesh (`RasterFrame::highlight_bits`,
  `RasterInstance::highlight`, binding 16) that the g-buffer pass mixes into a set triangle's albedo as the CPU
  raster does (in the card's triangle order, grouped by slot with several materials); a face selection is no
  longer a reason to draw the edit object on the CPU where the card has Vulkan's geometryShader feature
  (`gl_PrimitiveID` needs it; without it the fallback stays). A selection operator ends the F9 panel as before.

**Measured**
- The stress row "A selection operator after the undo (Select More)" with the views on the CPU: 15.4 ms a frame, was 32.2 (mesh copies in the frame 0.0; the frame after 1.7 ms).
- The stress row "A selection operator after the undo (Select More)" with the views on the GPU: 7.9 ms a frame, was 32.5 (mesh copies in the frame 0.0; the frame after 1.6 ms).
- Blender 5.2.2's row: 15.5 ms (PERFORMANCE.md Pass 13). The probe before this task: a plain selection change's frame 9 ms (the 3D layer about 3 ms, the cage overlay 4 ms), Select More's command 8 ms (the adjacency built per call), its frame after an undo a mesh copy, two in-place render-mesh updates, the shadow map and the Game view drawn again.

**Checked**
- Select More on the 50k-face sphere keeps the very mesh pointer, copies nothing, draws no shadow map or Game view
  again and renders one view; Select Less takes it back; Select Loop, Linked and Invert copy nothing
  (tests/test_editor.cpp). `grow_selection` matches the adjacency walk growing and shrinking (tests/test_modeling.cpp).
  A cube with its top face selected draws on the card without a fallback, its tint (the orange pixels beyond the
  picture with nothing selected) within 20 % of the CPU's, with one material and with two (tests/test_gpu.cpp);
  Select Loop from one edge and grow / shrink with a loose edge and an isolated vertex covered.
- Unit checks (round 186): Windows 11,280, 0 failed (925 tests); render-cache verify mode 11,281, 0 failed, 0 stale
  pictures. Linux with libraries: 10,942, 0 failed. Sanitizer build: 10,822, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`, `modifiers_ngon`,
  `editor_render`: 0 problems, 394 results with faces newly on top of each other in `pushpull`, no change.

## 2026-10-22 (round 185): Edit Mode operators without the wasted work (task 0165, ADR 0053)

**Asked:** the roadmap's "Edit Mode operators without a whole-mesh copy" (Extrude on 50k faces 96-118 ms against
Blender's 32.6 ms).

**Measured first** (the task doc has the probe): the command `editop extrude` was 60 ms on the 50k-face sphere, of
which Auto Smooth's shallow-edge count 23 ms taken twice (47 ms), three whole-mesh copies 10.5 ms, the extrude
itself 1 ms; the frame after it 38 ms (both render meshes rebuilt 13 ms, the shadow map and the views redrawn). A
whole copy is 3.5 ms, and an extrude writes every array, so the planned per-array copy-on-write would have copied
them all anyway: ADR 0053 records the decision against it, with the arithmetic.

**Changed**
- A redoable operator (Extrude, Inset, Bevel, F9's family) keeps the mesh before it as the undo step's own pointer -
  never written, copy-on-write guards every edit - and `run_last_op` makes the one copy it works on (three copies
  before: a mutable one, the `before` one, the working one). A cancelled modal gives the pointer back; undo
  restores the very mesh.
- Auto Smooth's shallow-edge count is taken once per operator (`LastOp::shallow_before`, reused by F9) and only
  when it can decide anything (`Editor::shallow_before`: not for a mesh already smooth or shaded per face), and
  `shallow_edges` works from a sorted (edge, face) table with each face normal computed once (a hash map over
  200k edge ends and two normals per edge before): 23 ms to about 4 ms on 50k faces.
- `edit_op` is the common start, `begin_redoable_op` and `edit_op_rest`; the selection flush is
  `edit_selection_faces`.
- An operator cancelled with Esc (or one that fails) gives back a copy of the mesh before it, not the undo step's
  very pointer: the pointer brought back an earlier render-cache key whose picture had been drawn with state the key
  doesn't hold (verify mode found a stale Scene view picture after an Esc; the key's gap is ADR 0053's follow-up).
- `FrameProfile::ms_edit_op` - the operator's own time inside the command that ran it - in the Profiler's "Edit
  Mode: operator / mesh writes / overlap check" row and the stress edit_frame notes (the operator itself, its mesh
  copies).
- The roadmap's 0167 ("undo steps sharing unchanged arrays") is already how ADR 0003 works - the undo stack shares
  meshes by pointer and a scene clone is 0.02 ms - so its round goes to the undo frame's own cost instead.

**Measured**
- The stress row "An operator: extrude three faces" with the views on the CPU: 48.9 ms a frame, was 130.6 (the operator itself 7.8 ms, mesh copies in the frame 1.0 of 3.4 ms; the frame after 2.2 ms, was 2.2).
- The stress row "An operator: extrude three faces" with the views on the GPU: 48.5 ms a frame, was 115.3 (the operator itself 7.3 ms, mesh copies in the frame 1.0 of 3.4 ms; the frame after 2.0 ms, was 2.1).
- The probe before this task (the task doc): the command alone 60 ms (Auto Smooth's count 23 ms twice, three copies 10.5 ms), the frame after it 38 ms; after: the command 6.9 ms with one copy of 3.5 ms, the frame after unchanged (the render meshes rebuilt 13.5 ms, the shadow map and the views). Blender 5.2.2's row: 32.6 ms (PERFORMANCE.md Pass 13).

**Checked**
- An extrude of three faces on the 50k-face sphere makes one mesh copy; the mesh before it is the very pointer
  undo gives back, and redo re-applies it; the Profiler counts the operator's own time (tests/test_editor.cpp).
  `shallow_edges` counts what the hash map did on a cube (none), a 12-sided cylinder (its 12 sides), a sphere, and
  an edge shared by three faces counted once (tests/test_modeling.cpp); Auto Smooth still shades a bevelled cube
  smooth and leaves a per-face-shaded one alone (the round 142 test).
- Unit checks (round 185): Windows 11,248, 0 failed (922 tests); render-cache verify mode 11,249, 0 failed, 0 stale
  pictures. Linux with libraries: 10,916, 0 failed. Sanitizer build: 10,796, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`, `modifiers_ngon`,
  `editor_render`: 0 problems, 394 results with faces newly on top of each other in `pushpull`, no change.

## 2026-10-22 (round 184): no artifacts at distance for Toon materials (task 0164, ADR 0048 amended)

**Asked:** "artifacts when backing away from a Toon material with painted maps".

**Fixed**
- Every toon map was read at level 0 whatever the distance: the hatching now reads at the lod of the UV
  derivatives scaled by its Hatch Scale, the face shadow map at its own lod (its hard threshold at softness 0 no
  longer sparkles far away), the toon ramp over the pixel's span of lit; a Generated mapping takes its derivatives
  from the pixel's footprint over the bounds and a Box projection its lod from the footprint times the tiling (both
  were level 0); the built-in Checker and Noise fade to their mean once a cell is under a pixel (the derivatives,
  or the footprint, say when), as Blender's Checker does.
- A cel edge fell on whichever side the pixel centre landed, so a far band edge sparkled: the deferred pass now
  hands the shading normal's change to the next pixel right and below (`SurfacePoint::normal_dx / dy`), and
  `shade_toon` softens each band edge by half the pixel's span of lit along the light (the GPU raster takes the
  same from the neighbouring G-buffer normals), so the edge is one antialiased pixel at any distance and the 2 m
  look is unchanged where the softness already covers it.
- An outline shrank below a pixel far away and the silhouette went dotted: a new Outline has Min Pixels 1 (a loaded
  one saved before the field keeps 0, as it drew), and the shell's 2 % steps get a hysteresis of a third of a step
  so a dolly on a boundary doesn't pop.
- Reversed-Z measured and declined for now: the D32 buffer resolves about 2 mm at 100 m with near 0.3 / far 1000,
  well under a shell's offset; the task doc records the calculation.
- The path tracers keep level 0: their samples average the pixel, and the GPU kernel's Material must not grow.

**Measured**
- High-frequency energy inside the object (the mean absolute Laplacian over its interior pixels) at 20 m against
  its 2 m picture downsampled ten times: the hatched plane 0.00 against 4.72, the box-mapped cube 12.3 against
  17.7, the face-shadow sphere at softness 0 24.4 against 13.2 (1.8 times: inside the factor of 2, the sphere's
  shading itself is most of it at 300 pixels), the two-tone sphere 2.8 against 4.3 on the CPU and 2.8 against 3.9 on
  the card. The hatched plane and the face-shadow sphere draw on the CPU in the card's views too (ADR 0048), so
  their numbers are the same on both.
- The two-tone sphere's band edge: at 2 m 848 of 48,714 pixels between the two tones (the edge's own pixels), at 20 m
  139 of 295 - the edge one antialiased pixel wide, where a hard edge gives none.
- The outlined sphere at 60 m, nine pixels wide: 27 outline pixels around its 48 (2,284 at 2 m); a saved Outline
  keeps its Min Pixels through a round trip (3), a scene saved before the field loads it as 0.

**Checked**
- A hatched plane, a face-shadow sphere at softness 0 and a box-mapped cube at 20 m carry no more high-frequency
  energy inside them than their 2 m pictures downsampled ten times (the box-mapped cube and the sphere on the card
  as well; a hatched or face-shadowed toon material still draws on the CPU, ADR 0048), and so does a two-tone
  sphere, whose far band edge shows the mid tones of one antialiased pixel; an outlined sphere at 60 m - nine
  pixels wide - keeps a one-pixel ring of 27 pixels, an old scene's outline loads with Min Pixels 0 and a saved one
  keeps its own (tests/test_render.cpp); the anime picture tests at 2 m unchanged.
- Unit checks (round 184): Windows 11,227, 0 failed (920 tests); render-cache verify mode 11,228, 0 failed, 0 stale
  pictures. Linux with libraries: 10,895, 0 failed. Sanitizer build: 10,775, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`, `modifiers_ngon`,
  `editor_render`: 0 problems, 394 results with faces newly on top of each other in `pushpull`, no change.

## 2026-10-22 (round 183): fewer CPU fallbacks for the GPU views - area lights and cutouts on the card (task 0163)

**Asked:** the roadmap's "fewer CPU fallbacks for the GPU views": a view with an area light or a cutout material
drew on the CPU.

**Changed**
- Area lights on the card: `area_light_irradiance` (the Gauss quadrature over the panel with the horizon clipped)
  and `area_light_specular_dir` (the representative point, Karis 2013) ported line for line into the lighting
  pass; the light block grows to six vec4s (right / width, up / height, the disc flag); the toon branch takes an
  area light at its centre as the CPU's `light_falloff` does. `gpu_view_unsupported` no longer refuses them.
- Cutout materials on the card: the instance carries the threshold and the material's alpha, the surface
  evaluation carries the base map's alpha, and the G-buffer discards under the threshold - no colour, depth or id,
  so what is behind shows and picks, as the CPU's rasteriser punches its holes.
- The Profiler's GPU block lists each view slot's fallback reason ("View slot N on the CPU: a camera filter").
- Still on the CPU, named: see-through and glass materials, toon materials with painted maps, camera filters, GI
  and probes, lightmaps, vertex colours, live procedurals, more than 16 lights.

**Measured**
- The g-buffer pass with the cutout test and the area lights in the lighting shader (stress editor_render, the
  views on the card, a quiet machine, RTX 4070 SUPER): g-buffer 0.15 ms a frame (was 0.14), lighting 0.07 (was
  0.06), the GPU path 3.50 ms a frame (was 3.58): the discard and the area-light loop cost nothing measurable on a
  scene without them.
- The tests' own numbers: an area-lit room on the card against the CPU, mean difference 0.110 / 255 per channel;
  turning the light's panel off changes about 160,000 pixels (the card draws it, not a fallback). A cutout wall's
  hole: the pixel at its centre 0x5771A5 on both devices (the sphere behind), beside it 0x892D1D (the wall); a click
  through the hole picks the object behind.

**Checked**
- An area-light scene (a rectangle, then a disc) draws on the card with no fallback and matches the CPU within the
  views' threshold; a cutout's holes show the cube behind and a click through one picks it on the card; the
  Profiler names a Game view's filter fallback by its slot (tests/test_gpu.cpp).
- Unit checks (round 183): Windows 11,201, 0 failed (917 tests); render-cache verify mode 11,202, 0 failed, 0 stale
  pictures. Linux with libraries: 10,878, 0 failed. Sanitizer build: 10,758, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`, `modifiers_ngon`,
  `editor_render`: 0 problems, 394 results with faces newly on top of each other in `pushpull`, no change.

## 2026-10-22 (round 182): incremental scene uploads, one submission per upload (task 0162)

**Asked:** the roadmap's "incremental scene uploads, one submission per upload" - every build re-uploaded the whole
scene to each card buffer by buffer, a fence wait each, and a submission per acceleration structure.

**Changed**
- The scene's buffers go up in one submission: the Vulkan device copies every section from one staging arena
  (`Batch`); every BLAS is built in one submission and the TLAS in its own, each with its input buffer sent before
  it, and the parameters and the clear follow: the test counts 9 submissions for a first whole upload on ray
  tracing hardware and 5 for an instance update (5 and 3 with Blendity's BVH; it was 13 for the buffers alone plus
  one an acceleration structure); `Renderer::submits()` counts them and the stress gpu section's upload row shows
  them.
- Only what changed goes up: `upload_gpus` hashes the scene's sections (the geometry, the instances with the
  software TLAS, the materials, the lights, the texture arena, the parameters) and, when every card holds the
  geometry and the textures, sends the instances (`update_instances`: the instance buffer, the TLAS's nodes and
  order; on ray tracing hardware the TLAS rebuilt over the kept BLASes), the materials or the lights alone; a card
  that joins gets the whole scene. `PTStats` counts `upload_calls / instance_updates / material_updates /
  light_updates` and times `upload_ms`. An object added that reuses a mesh is still a whole upload (the mesh
  table changes); a partial update re-points the descriptors and leaves the pixel buffers alone (the whole
  upload used to re-make them at one pixel and again at the frame's size).
- Not done: the scene is still flattened whole on the CPU each build (the hashes read the bytes); a changed texture
  arena is a whole upload.

**Measured (RTX 4070 SUPER, docs/PERFORMANCE.md Pass 13)**
- NVIDIA GeForce RTX 4070 SUPER (compute, Blendity BVH): a whole upload of the stress gpu scene (400 spheres, the checker floor, their textures) 11.4 ms in 1 submissions; one object moved 4.9 ms in 1; one material edited 4.7 ms in 1.
- NVIDIA GeForce RTX 4070 SUPER (ray tracing hardware): a whole upload of the stress gpu scene (400 spheres, the checker floor, their textures) 14.0 ms in 5 submissions; one object moved 6.1 ms in 3; one material edited 4.9 ms in 1.
- NVIDIA GeForce RTX 3060 Ti (compute, Blendity BVH): a whole upload of the stress gpu scene (400 spheres, the checker floor, their textures) 11.1 ms in 1 submissions; one object moved 5.2 ms in 1; one material edited 5.2 ms in 1.
- NVIDIA GeForce RTX 3060 Ti (ray tracing hardware): a whole upload of the stress gpu scene (400 spheres, the checker floor, their textures) 14.5 ms in 5 submissions; one object moved 6.3 ms in 3; one material edited 4.3 ms in 1.
- Before this round a whole upload was a submission a buffer (13) plus one an acceleration structure, and every build sent the whole scene.

**Checked**
- Moving one object: no whole upload, one instance update, a fixed-seed sample within 1e-5 of a fresh upload's; a
  material edit one material update with the BVH under 5 ms; a light edit one light update; a new mesh a whole
  upload (tests/test_gpu.cpp).
- Unit checks (round 182): Windows 11,184, 0 failed (914 tests); render-cache verify mode 11,185, 0 failed, 0 stale
  pictures. Linux with libraries: 10,878, 0 failed. Sanitizer build: 10,758, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`, `modifiers_ngon`,
  `editor_render`: 0 problems, 394 results with faces newly on top of each other in `pushpull`, no change.

## 2026-10-22 (round 181): the path tracer off the main thread, two submissions in flight (task 0161, ADR 0052)

**Asked:** "feels slower", "CPU and GPU usage look low" - task 0159 measured a card 99 % busy inside each submission
and 21 % busy over an F12 render: the path tracer ran the cards only inside the editor's 40 ms slice a frame and
waited for every submission.

**Changed**
- A worker thread per card, for the life of its slot: it idles until a render starts, claims chunks of whole
  samples from the shared counter (the guided self-scheduling stays; the chunk cap halves to 50 ms from task 0051's
  100 ms, so a pause waits for at most two of them) and keeps two submissions in flight on its device
  (`Renderer::submit`, `wait_oldest`, `wait_all`, `take_busy_ms`: two command buffers, fences and timestamp pairs;
  `render()` is submit then wait). It downloads the card's sums into a host copy when a merge is wanted (its flights
  drained first) and when it stops. Each slot's state is under one lock, a slot counts as active from the start so
  a poll never sees the cards idle before they wake, and the stats are the main thread's alone.
- `PathTracer::render()` polls: it starts the workers toward the target if needed (the counter restarts from the
  cards' finished counts), renders the CPU's own samples for the budget when the CPU is a device, waits what is
  left of the budget for the cards, takes the counts, merges the host copies when due. The editor passes 4 ms for
  F12 and 2 ms for the Camera Preview when cards work and resolves F12's picture only when samples were merged, so
  the frame stays quick (6-8 ms headless; the frame that merges and resolves the 1280x720 picture about 12 ms more)
  and the cards render between frames; the CPU-only path is unchanged.
- `pause()` before anything touches a device or the sums (reset, set_settings, set_params, uploads, sync,
  merge_aux, the editor's Stop and the sequence / animation stops, the destructor): the workers' claims stop, every
  submission in flight finishes, the workers idle. A slot that leaves has its worker stopped by its own quit flag.
  The merge counts the samples its copies hold, so the picture is always a consistent average; a reset clears the
  host copies with the sums.
- The test hook `BLENDITY_GPU_FAKE_SLOW=<ms>` makes every submission take that long on the worker's side.
- The lesson "Combined rendering" gains "The cards work between frames".

**Measured (RTX 4070 SUPER, docs/PERFORMANCE.md Pass 13)**
- Path tracing the stress gpu scene at 1280x720, NVIDIA GeForce RTX 4070 SUPER (compute, Blendity BVH): 122.1 samples/s (was 122.9); the card busy 100.0 % (1037.6 of 1037.6 ms) inside its submissions (was 99.8 % (1034.1 of 1035.9 ms)).
- Path tracing the stress gpu scene at 1280x720, NVIDIA GeForce RTX 4070 SUPER (ray tracing hardware): 799.9 samples/s (was 821.0); the card busy 100.0 % (149.9 of 149.9 ms) inside its submissions (was 99.5 % (149.6 of 150.5 ms)).
- Path tracing the stress gpu scene at 1280x720, NVIDIA GeForce RTX 3060 Ti (compute, Blendity BVH): 62.0 samples/s (was 62.9); the card busy 100.0 % (2050.8 of 2051.0 ms) inside its submissions (was 99.8 % (2022.9 of 2026.2 ms)).
- Path tracing the stress gpu scene at 1280x720, NVIDIA GeForce RTX 3060 Ti (ray tracing hardware): 402.7 samples/s (was 411.8); the card busy 100.0 % (304.2 of 304.2 ms) inside its submissions (was 99.6 % (302.8 of 303.9 ms)).
- Path tracing the stress gpu scene at 1280x720, All GPUs: 1020.9 samples/s (was 1043.5); the card busy 100.0 % (218.8 of 218.9 ms) inside its submissions (was 99.2 % (221.8 of 223.7 ms)).
- Path tracing the stress gpu scene at 1280x720, CPU + all GPUs: 1081.7 samples/s (was 1079.8); the card busy 98.9 % (200.1 of 202.4 ms) inside its submissions (was 99.1 % (205.1 of 207.1 ms)).
- F12 of the default scene from the editor's frame loop (headless, GPU Compute with both cards): the cards busy 49 % of the render's wall time, was 41-43 % (82-85 % with the two cards' busy summed, as task 0159 reported it); 2048 samples in 0.6 s, was 0.7 s.

**Checked**
- With a 30 ms card, twenty polls each return in under 10 ms while it renders, the render completes with every
  sample, pause() mid-render returns within 150 ms; the editor's F12 on that card keeps twenty frames under 10 ms
  each and finishes; the GPU Compute pictures still match the CPU's (tests/test_gpu.cpp).
- Unit checks (round 181): Windows 11,166, 0 failed (913 tests); render-cache verify mode 11,167, 2 failed in the full run (the GPU-opening frame
  budget and the pipeline-cache test, both under the chain's build load; each passes alone), 0 stale pictures. Linux with libraries: 10,878, 0 failed. Sanitizer build: 10,758, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`, `modifiers_ngon`,
  `editor_render`: 0 problems, 394 results with faces newly on top of each other in `pushpull`, no change.

## 2026-10-22 (round 180): a GPU view in one submission (task 0160, ADR 0051)

**Asked:** the roadmap's "views in one submission, read-back double-buffered", after task 0159 showed the card busy
for 7 % of the frame's GPU path.

**Changed**
- A view on the card is one submission and one fence wait: `Raster::render_read()` records the planes' copies
  behind the frame's passes (after their last barrier) and submits once; before, `render()` and `read_back()` were
  two submissions each waited for, six round trips for three views. `render()` / `read_back()` stay for tests,
  stress and route B, sharing `record_frame` / `readback_buffer` / `record_planes` / `unpack_planes`;
  `Raster::submits()` counts the queue's submissions. The Profiler's "read-back" row becomes "copies out + fence":
  the submission's wall time beyond the card's passes (the device reports `submit_wall`); the lesson, the stress
  note and `bench` say so, and the round-179 wording that called it the CPU's timer around the copy is amended.
- Not done, and why (ADR 0051): the deferred flush (every view recorded, one submission a frame, two frames in
  flight). Each view's planes are needed the moment its window draws (the overlays are depth-tested against
  them, the camera's filters and depth of field read the depth, the upscale and the view cache copy the pixels),
  so one flush a frame without a frame of latency means splitting the frame into a record phase before the
  windows draw and a resolve phase inside them - a real restructure of the immediate-mode views, with the
  rasteriser's per-device uniform, instance and read-back buffers made per slot. And during a drag the other
  views are held (`hold_other_views`), so one view renders a frame in the plan's target case and batching views
  would save nothing there. The plan's "50k-face drag ≤ 10 ms on the laptop setting" is measured as the stress
  rows below, not promised; "read_back before a flush returns false" has no meaning without a flush.

**Measured (RTX 4070 SUPER, stress editor_render, docs/PERFORMANCE.md Pass 13)**
- Moving an object with the views on the GPU (stress editor_render, the RTX 4070 SUPER): the GPU path a frame 3.70 ms, was 4.09 ms; the card's own work 0.29 ms (8 % of the path, was 7 %); the copies out and the fence wait 0.52 ms, where the read-back was its own submission of 0.85 ms.
- The frame's view count and the picture are unchanged (33 views on the card over the row, 0 on the CPU).

**Checked**
- A frame with the Scene and Game views on the card costs two submissions (was four) and the picture is bit for bit
  the two-submission path's (a test switch keeps that path; tests/test_gpu.cpp).
- Unit checks (round 180): Windows 11,158, 0 failed (910 tests); render-cache verify mode 11,159, 1 failed, 0 stale
  pictures (the pipeline-cache test: a spare opening in the background rewrote its file under it; it passes alone three
  times, and round 182 makes it wait for the spare). Linux with libraries: 10,878, 0 failed. Sanitizer build: 10,758, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`, `modifiers_ngon`,
  `editor_render`: 0 problems, 394 results with faces newly on top of each other in `pushpull`, no change.

## 2026-10-22 (round 179): GPU time in the Profiler (task 0159)

**Asked:** "CPU and GPU usage look low" when rendering - and until now nothing in Blendity could say what the card
was doing. The frame profile held how long a frame spent in the GPU path (upload, dispatch, wait, read-back) as one
wall-time number the Profiler never showed, and the path tracer's stats had no GPU time at all.

**Added**
- The card's own clock: two timestamp queries around every path-tracer submission (`Renderer::last_gpu_ms()`, -1
  where the queue has none), summed into `PTStats::gpu_busy_ms` and `gpu_wait_ms` (the submission's wall time less
  the busy); the views' pass times (shadow, g-buffer, lighting, read-back) from the raster device's existing
  timestamps into the frame profile (`FrameProfile::ms_gpu_shadow / gbuffer / lighting / readback`, `gpu_busy_ms()`).
- The Profiler window's GPU block: the GPU path's wall time, the views' busy time and its three passes, read-back
  (the CPU's timer) and wait, "GPU busy in the frame" as a share of the frame, and for a running F12 or Camera
  Preview the path tracer's busy / wait inside its submissions and its busy share over the render so far (F12) or
  the frame (Camera Preview) - the card is idle between submissions, which the submissions' own wait never shows;
  a card without timestamps shows "n/a". `bench` prints "bench GPU busy ... a frame in the GPU path" over its own
  frames, "Render finished" logs the busy share over the render.
- The lesson "The views on the GPU" gains "The Profiler's GPU block".

**Measured (the baseline for tasks 0160-0163, RTX 4070 SUPER, docs/PERFORMANCE.md Pass 13)**
- The views on the GPU, moving an object (stress editor_render): the card worked 0.28 ms of the 4.09 ms GPU path a frame (7 %; shadow 0.09, g-buffer 0.14, lighting 0.06), the read-back 0.85 ms, the rest waiting.
- Path tracing the stress gpu scene at 1280x720, NVIDIA GeForce RTX 4070 SUPER (compute, Blendity BVH): 122.9 samples/s, the card busy 99.8 % (1034.1 of 1035.9 ms) inside its submissions (busy + wait).
- Path tracing the stress gpu scene at 1280x720, NVIDIA GeForce RTX 4070 SUPER (ray tracing hardware): 821.0 samples/s, the card busy 99.5 % (149.6 of 150.5 ms) inside its submissions (busy + wait).
- F12 of the default scene from the editor's frame loop (40 ms slices, headless): the card busy 21 % (256 samples of the default scene in 0.4 s on GPU Compute with both cards, their busy summed) of the render's wall time - the idle time is between submissions (uploads, merges, the UI), which tasks 0160-0162 remove.

**Checked**
- The views' three pass times are positive where the card has timestamps and busy plus read-back stays inside
  the GPU path's wall time; a path tracer on the card reports busy and wait within its wall time, on the CPU -1 / 0;
  the Profiler's rows are there and `bench` prints the line (two tests in tests/test_gpu.cpp).
- Unit checks (round 179): Windows 11,154, 0 failed (910 tests); render-cache verify mode 11,155, 0 failed, 0 stale
  pictures. Linux with libraries: 10,878, 0 failed. Sanitizer build: 10,758, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`, `modifiers_ngon`,
  `editor_render`: 0 problems, 394 results with faces newly on top of each other in `pushpull`, no change.

## 2026-10-22 (round 178): Blender 5.2 as the benchmark - a headless harness and Pass 13's reference column (task 0178)

**Asked:** "Always refer to Blender's rendering and editor performance as a benchmark" - with Blender 5.2 run headless
on this machine for the numbers, not estimates.

**Added**
- `tools/bench_blender.py`, run inside the installed Blender (`blender -b --python tools/bench_blender.py -- --out
  <json> [--quick] [--gpu 4070]`): the scenes and operations Blendity's stress rows time, in Blender's Python API with
  `perf_counter`, the mean of three runs after a warm call - entering Edit Mode on the 50k-face sphere (the exit
  outside the timer), the extrude operator on faces 100-102 with its undo step, Select More, undo of the extrude;
  Cycles on the stress gpu scene (400 textured spheres, every 7th glass, a checker floor, a sun) at 1280x720 on
  OptiX, CUDA and the CPU, one card, samples a second from the slope of time over three sample counts (a render's
  fixed cost drops out as Blendity's row leaves out its upload), the picture's coverage checked; Smart UV Project and
  Pack Islands by bounding box on the sphere; the stress bake's 100-object scene (each object its own mesh: Blender's
  lightmap_pack packs unique meshes only) packed into one 1024 atlas and baked with Cycles on the CPU, texels a
  second over the texels its charts cover; a GLB export of 256 icospheres of 1,280 faces, smooth-shaded with their
  own mesh data, both file sizes in the table. Headless Blender's operators push no undo step, so the harness pushes
  them and checks the face count comes back; the Cycles rows check the picture's coverage over a transparent film. Blendity's left-handed
  Y-up scenes are mirrored into Blender's Z-up ((x, y, z) to (x, z, y)). Headless Blender draws no viewport, so there
  is no playback row.
- `tools/bench.bat` runs Blendity's stress sections on the same scenes and Blender, and `tools/bench_table.py`
  writes both into `docs/PERFORMANCE.md`'s Pass 13 table (between its markers) with a method column per row; the
  Edit Mode rows take Blendity's operator alone (its frame less the frame after); `BLENDER=path` overrides where
  Blender is, `BENCH_GPU` which card; a `--quick` run never writes the doc.
- Stress rows for the comparison: Smart UV Project and Pack Islands on the 50k-face sphere (`uv`), a GLB export of
  the 256-icosphere scene (`serialization`), each the mean of three after a warm call.

**Fixed**
- The `bake` stress section had reported 0 texels since the probe volumes (task 0037) added `want_lightmap` to
  `BakeObject`: its positional initialisers' trailing 0 (meant for the hash) had been turning every chart off, in the
  odd-settings fuzz too. A `bake_object()` helper sets the fields by name at every site; it bakes again (100 objects:
  122k texels in 0.22 s, 562k texels/s, 56 Mrays/s).

**Measured (Pass 13's first table, this machine: Ryzen 9 5900XT, one card used, the RTX 4070 SUPER, Blender 5.2.2)**
| row | Blendity | Blender 5.2.2 LTS | ratio | the method's difference |
|---|---|---|---|---|
| Entering Edit Mode, 50k faces, caches cold (Blendity: the frame it costs less the frame after) | 24.7 ms | 17.9 ms | Blendity 1.4x behind | mode_set(EDIT) + the depsgraph update, mean of 3 (the exit, Mesh <- BMesh, outside the timer); headless Blender builds no draw cache; Blendity's row: entering with its cage cache cold, the frame that draws it included, less the frame after |
| Extrude three faces, 50k faces (Blendity: its frame less the frame after) | 96.5 ms | 32.6 ms | Blendity 3.0x behind | bpy.ops.mesh.extrude_region_move on faces 100-102 + its undo step pushed (as Blendity's editop records one) + the depsgraph update, mean of 3, undone outside the timer; Blendity's row: editop extrude on the same three faces, the frame that draws it included, less the frame after |
| Select More, 50k faces (Blendity: its frame less the frame after) | 51.0 ms | 15.5 ms | Blendity 3.3x behind | bpy.ops.mesh.select_more from face 100 + the depsgraph update, mean of 3; Blendity's row: edittool select_more after the undo, the frame that draws it included, less the frame after |
| Undo of the extrude (the frame that restores the mesh; the cage rebuilt in the frame after, about 14 ms more, is not counted) | 23.8 ms | 16.4 ms | Blendity 1.5x behind | ed.undo of the extrude's step (pushed by hand: headless Blender's operators push none) + the depsgraph update, mean of 3, the face count checked to come back; Blendity's row: undo of the extrude (a whole-mesh copy restored), the frame that restores it; the cage rebuilt in the frame after (about 14 ms more) not counted |
| Path tracing the stress gpu scene at 1280x720 on the card's ray-tracing hardware (Cycles: OptiX) | 823.8 samples/s | 115.9 samples/s | Blendity 7.1x ahead | the stress gpu scene at 1280x720 (67 % of the frame covered), bpy.ops.render.render with 32, 64 and 128 samples (adaptive sampling and denoising off, 4 bounces), mean of 2 each after a warm render; samples/s from the slope of time over samples (the render's fixed cost, 311 ms, drops out as Blendity's row leaves out its upload; worst fit residual 8 ms); the card's ray-tracing hardware, like Blendity's ray-query row; one card: the 4070. A sample is not the same work on both sides: Cycles runs its Principled BSDF, light tree and image textures under a constant-colour world, Blendity its own integrator under its gradient sky |
| Path tracing the same scene with a software BVH (Blendity: a compute shader; Cycles: CUDA) | 122.0 samples/s | 87.0 samples/s | Blendity 1.4x ahead | the stress gpu scene at 1280x720 (67 % of the frame covered), bpy.ops.render.render with 32, 64 and 128 samples (adaptive sampling and denoising off, 4 bounces), mean of 2 each after a warm render; samples/s from the slope of time over samples (the render's fixed cost, 272 ms, drops out as Blendity's row leaves out its upload; worst fit residual 15 ms); Cycles' BVH2 in CUDA, a software BVH like Blendity's compute row; one card: the 4070. A sample is not the same work on both sides: Cycles runs its Principled BSDF, light tree and image textures under a constant-colour world, Blendity its own integrator under its gradient sky |
| Path tracing the same scene on the CPU (Blendity: Embree; Cycles: its CPU kernels) | 19.0 samples/s | 8.1 samples/s | Blendity 2.4x ahead | the stress gpu scene at 1280x720 (67 % of the frame covered), bpy.ops.render.render with 32, 64 and 128 samples (adaptive sampling and denoising off, 4 bounces), mean of 2 each after a warm render; samples/s from the slope of time over samples (the render's fixed cost, 192 ms, drops out as Blendity's row leaves out its upload; worst fit residual 23 ms); Cycles' CPU kernels, like Blendity's CPU row (Embree); the CPU, no card. A sample is not the same work on both sides: Cycles runs its Principled BSDF, light tree and image textures under a constant-colour world, Blendity its own integrator under its gradient sky |
| Smart UV Project, 50k faces | 42.1 ms | 50.1 ms | Blendity 1.2x ahead | bpy.ops.uv.smart_project at 66 degrees on the sphere in Edit Mode, mean of 3 after a warm call (its own pack is by bounding box); Blendity's row: uvops::smart_project on the same sphere, mean of 3 after a warm call |
| Pack Islands, 50k faces (both by bounding box) | 21.8 ms | 35.9 ms | Blendity 1.6x ahead | bpy.ops.uv.pack_islands after the project with shape_method AABB (bounding boxes, as Blendity packs) and rotation on, mean of 3; Blendity's row: uvops::pack_islands, mean of 3 |
| Lightmap bake, texels a second, both on the CPU (100 objects; Blendity: 122k texels in its charts; Blender: the same objects in one 1024 atlas, its charts' texels) | 561968.0 texels/s | 32030.8 texels/s | Blendity 17.5x ahead | bpy.ops.object.bake DIFFUSE direct + indirect, Cycles on the CPU (Blendity bakes on the CPU too), 64 samples, 2 bounces, under a sun and a constant sky, the floor and 100 cubes and spheres (each its own mesh) packed together into one 1024x1024 atlas by lightmap_pack outside the timer, after a warm 1-sample bake: 22.2 s for the 710,272 texels the charts cover (the atlas is 1,048,576), baked object by object; Blendity's row: stress bake's 100-object scene at 10 texels/m (122k texels), 64 indirect samples, 2 bounces, its UV generation and packing inside the timer |
| GLB export, 256 icospheres of 1,280 faces | 307.3 ms | 707.7 ms | Blendity 2.3x ahead | bpy.ops.export_scene.gltf GLB of 256 icospheres of 1,280 faces, smooth-shaded, each its own mesh data as Blendity's export writes them (6.2 MB written, with UVs), mean of 3 after a warm call; Blendity's row: export_file GLB of the same scene, mean of 3 after a warm call; Blendity wrote 13.2 MB |

**Checked**
- The harness runs end to end (`tools\bench.bat`), every Blendity row has a Blender number, each row's method is
  in the table and in docs/tasks/0178; the numbers above are from a quiet machine (a build and a WSL test run beside
  a trial run made Blender's Select More 112 ms instead of 12).
- Unit checks (round 178): Windows 11,144, 0 failed (908 tests); render-cache verify mode 11,145, 0 failed, 0 stale
  pictures. Linux with libraries: 10,878, 0 failed. Sanitizer build: 10,758, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`, `modifiers_ngon`,
  `editor_render`: 0 problems, 394 results with faces newly on top of each other in `pushpull`, no change.

## 2026-10-22 (round 177): one test per behaviour, timings to stress; the suite in files; dead code out (task 0177, ADR 0056)

**Asked:** "check on unneeded tests and remove them. Compact the code-base as needed" (and chose: one test per
behaviour, timings to stress).

**Measured first**
- The suite on a quiet machine: 910 tests, 11,406 checks, 204.6 s of test time; 23 tests over a second took
  124.1 s of it. The slowest was task 0157's find (the software-BVH kernel on the CPU, 70 s: fixed there); then the
  240-file MMD fuzz (10.2 s), a 3 s pretend GPU open (6.8 s), every truncation of a probe-volume file (3.8 s), a
  render-cache script the stress fuzz already runs (3.3 s). The "Materials previews" guess for the 13 s material
  tests of round 174's run was wrong: alone they take 0.8 s (a build was running beside that suite).

**Changed**
- **The suite is one file a subsystem** (`tests/test_modeling.cpp`, `test_render.cpp`, `test_lighting.cpp`,
  `test_gpu.cpp`, `test_assets.cpp`, `test_animation.cpp`, `test_physics.cpp`, `test_editor.cpp`; the harness and the
  helpers several rounds share in `tests/test_common.h`; `test_main.cpp` runs them): `blendity_tests --group render`
  runs one, `--list-groups` names them; eight translation units build side by side (a test build: 55 s on 32 threads, was 58).
- **Trimmed:** the MMD fuzz keeps 24 of its 240 damaged files (the 240 are a stress `codecs` row, the synthetic
  models in `tests/fixtures/synthetic_mmd.h`); the probe-volume sweep keeps every 8th truncation and 30 of 150
  corruptions (the whole sweep a stress `probes` row); the GPU-opening pretend delays are 1 s and 0.6 s (were 3 and
  1.5); the four pacing tests are one.
- **Removed:** the GPU view spike's two tests (ADR 0036 holds the verdict; stress `gpu_raster` still measures route
  (B)), the OptiX picture test (stress `gpu_optix`'s row), the 30-action render-cache script (stress `editor_fuzz`
  runs 6,000 frames in verify mode), and the four `NDEBUG` timing checks (the overlap check's 3x, AutoPhysics' 5 s,
  Locomotion's 0.2 ms, 100 spring chains' 1 ms), which are stress rows now (`modeling`, `physics`, `animation`),
  each with its target beside the number.
- **Added:** `tools/test_coverage_map.py` lists every console command, keymap action and window against the tests
  that exercise it (259 entries; `--check` fails on an empty one); the 72 it found empty got two tests: every
  keymap action id through `run_action` (a renamed or dropped id is caught) and the console commands no test drove
  (`samples`, `world`, `scale`, `threads`, `proportional`, `loopcuts`, `selectall`, `subdivide`, `seam`, `focus`,
  `export`, `exportoptions`, `screenshot`, `deleteasset`, `alignview`, `droponsurface`, `guidesfromedges`, `raster`,
  `surfacealign`, the printing ones).
- **Code:** `core/hash.h` (one FNV-1a, one 8-bytes-a-step mixer, `hash32`) replaces the hash loops written out in
  33 files - with the mistyped FNV basis corrected, except in the three places a bake saves the key, which keep the
  old constant on purpose, so every bake stays valid (a test pins the two saved keys); the corrected basis renames
  the GPU kernel's cache file (one recompile, 19 s), the pipeline cache's trailer (rewritten once), the import
  cache folders (`~/.blendity/cache/import`, re-made once) and MoonRay's converted texture names (re-converted
  once); `closest_on_segment` and the 2D segment distance in `core/math.h` (four copies gone);
  `clampf01` and a second `lerpf` gone; `config_path`, `mirror_markers`, `sample_clip_values` and the GLSL
  `safe_normalize` deleted (never called); `is_bone` / `bone_children` and `bone_armature` once (`scene.h`); the
  Jolt layer classes once (`scene/physics_jolt_layers.h`); `move_to_trash`'s test-folder prefix once; undo and
  redo through one `restore_state`; the button grid of the Rigging and Tools windows once (`button_grid`); the
  `to_screen` lambdas through `project_to_view`; the closed-loop walk of the mesh tools and Follow's wire-path walk
  once. `editor.cpp`'s console (the `set` reflector, the command handlers, `run_console_command`) is
  `editor_commands.cpp`; `scene_view.cpp`'s picking is `scene_pick.cpp` and its overlays and icons
  `scene_overlays.cpp`. `tools/clean.bat` and `tools/README.md`.
- Not done, and why: the Vulkan boilerplate `gpu_device.cpp` and `gpu_raster.cpp` share (tasks 0160 / 0161
  restructure both), the rasterizer's duplicated triangle setup (hot code), the OptiX spike (Stack 5 turns it into
  the device). The plan's "1,000 lines fewer in src" was an estimate: the duplication scan found nothing over 17
  lines in two places, and the moves are neutral, so src is 137 lines smaller (the hash loops, the helpers
  and the dead code), with the tests' files the same size as the one file was.

**Changed after the review**
- The GPU pipeline-cache test forged its foreign file's trailer with the old basis, so the trailer check refused it
  before the header check it was testing could (the trailer uses the new hash now); the shared test helpers are
  `inline` (nine translation units include them: GCC warned on every unused `static` one); the coverage map reads
  test bodies without their titles and comments, matches a command only as an argument and an action only as a whole
  id (`manual`, which opens the browser, is listed as not driven by tests); the F12 test waits for the render and
  checks its picture; the keymap test covers every id, including `file.save`, `mode.edit_toggle`, the three
  `transform.*` and `lighting.update`, and checks a drawing began after a `draw.*` id (`run_action` says yes to any
  of them); a test pins the two keys a bake saves (`lightmap_mesh_hash`, `lightmap_placed_hash`) to their values, so
  a later change to the mixers can't mark every bake out of date, and hash.h's comment says so; `mix` is `hash_mix`
  (the name meant lerp elsewhere); `walk_wire_path` takes a const map; `bone_armature` is declared once;
  `button_grid` lives in tools_window.cpp; the empty round-121 group is gone; the stress rows are functions
  (`mmd_damage_row`, `probe_file_damage_row`, `spring_chain_row`, `autophysics_row`, `locomotion_row`), the Euler
  residual's unit corrected to body weights; `sequence.cpp`'s FNV loop uses the header; the spring test's 100-chain
  timing loop went with its check (the row has it); `tools/clean.bat` keeps the training runs unless `--all`; the
  task doc drops two claims the tree didn't hold (the toolbar button was left as it is; `merge_by_distance_naive`
  stays in src, it shares `finish_merge`); the CHANGELOG says which caches the corrected basis refreshes once.
  Noted, not changed: `closest_on_segment` passes a NaN through where two of the old copies clamped it to the far end
  (their callers guard against non-finite input).

**Checked**
- The suite: 908 tests, 11,144 checks, 117.4 s of test time (was 204.6 s; the target 160 s); the check
  count down by 272 - more than the plan's 60, and almost all of it one behaviour: the probe-volume sweep
  asserted at every truncation point (349 checks, now 30 for the same behaviour), then the GPU view spike (11),
  the render-cache script (4), the four timing checks (4), the OptiX picture (2); the MMD fuzz and the pacing
  merge change nothing; the two coverage tests add 85. The stress run grows by the new rows: 32.6 s (codecs 1.0 s, probes
  29 s, physics 0.4 s, animation 0.1 s, modeling 0.7 s).
- `tools/test_coverage_map.py --check`: 259 entries, none empty.
- The new rows' first numbers (this machine): 240 damaged MMD files in 1.0 s (66 refused, 174 imported, no bad
  mesh); a 4 MB ProbeVolume.bin cut at 4,603 points (none accepted) and corrupted 150 ways (all loaded and
  sampled safely); 100 spring chains of 3 bones 0.27 / 0.31 / 0.41 ms a frame (target 1.0), of 10 bones
  1.05 / 1.20 / 1.70 ms a frame (target 5.0); AutoPhysics' jump 278 ms (target 1,000), the run 307 ms; Locomotion 0.017 ms a frame (target
  0.2); the overlap check 14.1x its reference on 50k faces (target 3x).
- Unit checks (round 177): Windows 11,144, 0 failed (908 tests); render-cache verify mode 11,145, 0 failed, 0 stale
  pictures. Linux with libraries: 10,878, 0 failed. Sanitizer build: 10,758, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`, `modifiers_ngon`,
  `editor_render`: 0 problems, 394 results with faces newly on top of each other in `pushpull`, no change.

## 2026-10-21 (round 176): the Wind gizmo and icons for the effectors (task 0158)

**Asked:** a gizmo for the Wind component, as task 0153's spec left out.

**Added**
- A Wind, a Rotator and an Oscillator show an icon in the Scene view (the icons layer, now "Light, Camera &
  Effector Icons"), clicked to select them like a light's. A selected Wind draws its arrow along its forward,
  half a metre plus a quarter metre per m/s of Strength, wavy streamlines beside it with Turbulence, three rings
  instead when Spherical; the gizmo and the simulation read one `wind_direction`.
- The lesson "Winds you can see".

**Changed after the review**
- A true orthonormal basis for a near-vertical wind (the barbs and rings came out skewed), the strength and
  turbulence read through the simulation's own finite, clamped accessors (`std::isfinite` folds away under
  `/fp:fast`), three streamlines, the gizmo beside the rig controls' shape helpers, and an Effector render role
  so the icon pass costs one virtual call an object instead of three casts.

**Checked**
- New tests (round 176): a 3 m/s wind turned to +X draws its arrow to 1.25 m along X in three segments,
  turbulence adds three streamlines, spherical three rings of 1.25 m, a wind pitched 80 degrees keeps true rings
  and square barbs; the three effectors have icons and a plain object none; a click on the wind's icon selects
  it and selects nothing with the layer off.
- Unit checks (round 176): Windows 11,416, 0 failed (912 tests); render-cache verify mode 11,417, 0 failed, 0 stale
  pictures. Linux with libraries: 11,141, 0 failed. Sanitizer build: 11,017, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`, `modifiers_ngon`,
  `editor_render`: 0 problems, 394 results with faces newly on top of each other in `pushpull`, no change.

## 2026-10-21 (round 175): baked emission without blocks (task 0157)

**Asked:** "Emissions off some objects when baked under the lighting tab still look blocky or pixelated."

**Fixed**
- A bake found emissive meshes only when a hemisphere ray chanced on them (a 10 cm lamp: one ray in sixty), so a
  small bright object came out as a scatter of blocks. Each texel samples the emitters directly now (one emitter
  point a ray, the emitter choice stratified over the rays, a shadow ray each), weighted against the hemisphere
  gather by the power heuristic - the gather's own emitter hits are weighted the other way, on the CPU and in the
  GPU kernel's bake entry - so nothing is counted twice. A 10 cm lamp over a floor: 1.4 % RMS from the analytic
  irradiance (was noise-limited), 274x smoother than the hemisphere alone, the GPU gather within 4 % of the CPU.
- A firefly clamp before the lightmap's filter (a texel over eight times its chart neighbours' mean), so a lone
  bright hit is no longer spread into a blotch.
- Preview Lighting bakes at half the density (a quarter showed blocks) and 32 rays first (was 16).

**Changed after the review**
- The emitter-hit weight carries the distance through see-through passes (a cutout leaf between a texel and a lamp
  made the hit weigh too much); a sampled emitter point respects its alpha (a cutout's hole emits nothing, a
  transparent emitter by its alpha), in the path tracer's own light sampling too; the firefly clamp touches the
  hemisphere gather only, with the filter's facing test (a one-texel sun spot or a lamp's contact peak is real
  light); the emitter rays are capped at 64 on the GPU path; the Lighting window says emitters are sampled; the
  preview's status says 32.
- The chain found the GPU's software-BVH kernel no longer compiling: shaderc's optimiser stopped with "ID overflow"
  (SPIR-V allows 4,194,303 ids; round 174's kernel used 4,191,309, and the two lines above took it over), and the
  path tracer handed the GPU's samples to the CPU without a word - the "every device" test took 70 s and passed on
  the CPU's pixels. The kernel's `main` now calls `trace` from one place (the camera's samples and a bake's share
  the loop), so the optimiser inlines the integrator once: 2,216,807 ids (2,197,372 with ray queries), the
  optimiser in 19 s instead of 57-83, the same pictures (the seeds and the maths are unchanged), and room for
  the kernel to grow again. The test now checks the device did the work (no GPU error, no CPU samples): 2.0 s.

**Added**
- `BakeSettings::sample_emissive` (on); the Lightmap Resolution help names 80 texels/m for small emitters; the
  lesson "Baked emission: a small lamp without blocks".

**Checked**
- New test (round 175): a 10 cm emissive cube 0.5 m over a floor baked at 12 texels/m with 64 rays: 1.4 % RMS from
  the cube's faces integrated analytically, 274x smoother than the hemisphere-only bake, the same twice, the GPU
  gather's bake within 3.5 % of the CPU's; the preview tests expect 32 rays first, and the preview's first pass on
  the lamp scene shows no texel over 3x its lit neighbours' mean.
- Unit checks (round 175): Windows 11,406, 0 failed (910 tests); render-cache verify mode 11,407, 0 failed, 0 stale
  pictures. Linux with libraries: 11,131, 0 failed. Sanitizer build: 11,007, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`, `modifiers_ngon`,
  `editor_render`: 0 problems, 394 results with faces newly on top of each other in `pushpull`, no change.

## 2026-10-21 (round 174): IK handles in metres, a size preference, rotatable targets (task 0156, ADR 0031)

**Asked:** "the IK handles are way too big"; "I should be able to rotate the handles".

**Fixed**
- A rig control's wire is drawn at its Size in metres round its position and rotation, with no scale from the
  control or its parents: a centimetre import (root scale 100) drew its handles a hundred times too big, a 0.01
  rig a hundred times too small. FK circles are sized from the bone's world length (they were in the bone's own
  units).

**Added**
- **Preferences > Rig Control Size** (0.1-5, `overlay controls size <x>`): every handle's wire and pick distance
  scaled at once. **Rigging > Fit Control Sizes** (`ik fitsizes [root]`): every handle sized from its bone again
  (a target a fifth of the lower bone, a pole 0.12, an FK circle 0.3) - loaded rigs whose handles were in another
  unit come right.
- **Rotate End** on an IK constraint: the foot or hand below the chain takes the handle's rotation (times an
  offset captured when the IK was made). New IK starts a world-aligned target with Rotate End on (the offset
  carries the foot's rotation, so the handle's keyed angles stay clear of the gimbal);
  `ik orient <bone>` turns it on for an existing IK, keeping the bone as it is. The end bone counts as driven:
  the gizmo says to turn its handle instead.
- The lesson "IK handles: size and turn".

**Changed after the review**
- An FK control on the foot fought Rotate End every frame: the bones IK turns (the chain and the end bone) come
  from one helper, used by the FK solve, Add FK, the tint and the gizmo. Rotate End ticked in the Inspector
  captures its offset on the first solve (it snapped the foot to the handle); Weight blends the foot; the end bone
  is the child at the chain's tip (not a twist bone beside it); Fit Control Sizes matches a control's IK on its own
  rig and warns on a mistyped name; the floors are a millimetre, so a rig at 0.01 keeps a fifth of its shin; the
  wire keeps a mirrored parent's mirroring; the gizmo says "turn its target instead" for the foot. The walk and
  footstep generators turn the handle to the foot's roll (Rotate End overrode the keyed foot: 43 mm of sliding,
  then 1.7 mm from the handle's Euler keys near the gimbal; 0.001 mm now).

**Checked**
- New tests (round 174): a humanoid rig at scale 1, 100 and 10 draws the foot target's box at 0.4 of the shin's
  world length; an FK circle at 0.3 of its bone, an old-style size on a scaled rig at 0.003 until Fit Control
  Sizes; the preference doubling the wire, a control's own scale not changing it; a fresh target starting with the
  rotation through its offset, the foot turning 30 degrees with the handle while the knee stays, Rotate End off leaving the
  rest, `ik orient` keeping a pitched foot through a turn, the foot refusing the gizmo, the fields saved; IK added
  on a rig already at scale 100 sized at once; Rotate End ticked later keeping the foot, Weight 0 leaving it at
  rest, a twist bone beside the foot not taken for it, FK on the foot refused and the solve settling, a file
  without the field loading with it off.
- Unit checks (round 174): Windows 11,395, 0 failed (908 tests); render-cache verify mode 11,396, 0 failed, 0 stale
  pictures. Linux with libraries: 11,126, 0 failed. Sanitizer build: 11,002, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`, `modifiers_ngon`,
  `editor_render`: 0 problems, 394 results with faces newly on top of each other in `pushpull`, no change.

## 2026-10-21 (round 173): the anime pass written up (task 0151)

**Asked:** the anime pass (tasks 0145-0155) documented in one place.

**Added**
- `docs/tasks/0151-anime-pass-write-up.md`: every task of the pass with its round and PR, where the code lives,
  the research and what was taken from it, the short path through the tools.
- ADR 0048 and ADR 0049 gain their References sections (Arc System Works GDC 2015, HoYoverse's face maps, VRM
  MToon, Pixar's procedural patterns; Jakobsen 2001, Kugelstadt & Schoemer 2016, Macklin 2016 / 2019, Daviet
  2023, Akyuerek & Sahillioglu 2025).

**Checked**
- Docs only: the docs test (the manual's chapters and Learn citations) and the suite unchanged.
- The docs test (the manual's chapters and Learn citations): 5 checks, 0 failed; the suite as round 172's.

## 2026-10-21 (round 172): soft tissue, the Jiggle modifier, hair rigged from its mesh (task 0153, ADR 0049)

**Asked:** realistic hair and soft-tissue physics for Spring Bones, research based. This round: the soft tissue and
the hair rig.

**Added**
- **Soft**, a fifth Spring Bones label for a single bone (a breast, a thigh, a belly): the bone trails the body's
  **acceleration** by **Lag** (a landing throws it down, a step up; a slow walk hardly moves it), Stiffness pulls
  it back and Damping settles it, **Limit** keeps it within a cone (degrees) about the animation's direction, and
  **Volume** narrows it across as it stretches along its length (the product of the scales stays 1), the stretch
  given back when Play stops or a bake ends. Found by name: breast / bust / boob / belly / stomach / butt / glute,
  a thigh / hip / chest / cheek with jiggle / soft / phys / dyn / spring / sec / bounce, MMD's 胸 / 尻 / 太もも,
  VRM's `J_Sec_L_Bust` (it read as hair before). `spring add <bone> soft`, `spring label <bone> soft`.
- **Jiggle**, a deform modifier (Add Component > Mesh) for a mesh whose rig has no such bones: a vertex group's
  vertices trail the body's acceleration together on a damped spring (Springiness, Damping, Lag, a Limit in
  metres), stepped on the simulated clock (Play), still in the editor.
- **Rig Hair from Mesh** (the Rigging window's Skeleton buttons; `hair rig [locks] [bones]`): the selected mesh's
  loose parts (hair cards) - or one shell cut into locks round its upright axis - each a chain of bones from the
  scalp to the tip under a new hair armature (under the character's head bone when the mesh is skinned to one, so
  it follows the head), the mesh skinned to the chains, Spring Bones (hair) on each; one undo step.
- The lessons "Soft tissue: breasts, thighs and bellies that move" and "Rig hair from its mesh", after Strands.

**Changed after the review**
- The Jiggle modifier evaluated for the Scene and the Game view at one clock applies one offset (it reset itself on
  the second evaluation), moves every vertex by the offset and lets the stack scale it by the group (it squared the
  weights), stops when its group is missing, and runs on the clock whether or not the Physics assistant is on;
  Springiness is Stiffness, as on a chain. It isn't baked into clips (said in its help).
- Soft tissue: the acceleration is known from the third step, not read from a speed on the second (a body already
  moving when Play starts gave a kick); a teleport forgets it; the stretch and its counter-scale go on a chain's
  last bone only (a non-uniform scale on a parent shears what hangs below it), the scale base is re-taken when
  the animation or the Inspector sets it, a chain relabelled or taken off gives its scale back, and a bake writes
  the stretch as Scale keys. Soft chains don't collide by default (the torso's capsule pushed them off their
  pose). The cone picks a side for a bone pointing straight back. Lag / Limit / Volume show for Soft only.
- The names: 尻尾 is a tail and 胸リボン a ribbon before 尻 and 胸 are soft; Chest, Hips, UpperChest stay the body's;
  MMD's Japanese physics bones get the soft label at import too.
- Rig Hair from Mesh: every card its own lock (up to 64; a count given merges them into that many sectors by their
  roots), a shell into eight; bone lengths in the bones' own units under a scaled armature; a posed head refuses
  (the rig would float when it returns to rest); a skinned mesh with no head bone refuses; the old armature's
  groups go when the skin moves to the rig; each vertex is weighted to its own lock's two nearest joints (the
  envelope bind weighted a card to its neighbours' bones); bone names carry the mesh's name; the head is found by
  the whole word (not Forehead) or 頭. A hair rig under a head bone collides with the character's body (the
  chain's armature is the outermost).
- Speed: the body-bone name test is cached (every chain walk split every bone's name into words: 3 ms a frame for
  a thousand bones), each chain is walked once a step, the separation pass works cell by cell on a sorted array
  (each pair once, by squared distance) over a dense grid of the tips' box. 100 chains of 3 bones, strands touching: 0.4 ms a
  frame with separation and collision; of 10 bones: 1.7 ms.

**Checked**
- New tests (round 172): a bust bone on a 3 Hz run trailing its acceleration by over 2 mm more than with Lag 0,
  within 25 degrees (and within 5 with Limit 5), the scales' product within 1 % of 1, at rest within 1 mm a second
  after the run stops, the scales back to 1 on reset, Lag / Limit / Volume saved; a Jiggle on a cube's top half
  moving it up to its 30 mm limit under a 4 Hz shake, the bottom half not at all, at rest a second after; twelve
  hair cards rigged as twelve chains of three bones under a hair rig with 36 vertex groups whose weights sum to 1,
  undone in one step, and a cube cut into four locks of two bones; the soft names (Breast_L, chest_jiggle, Belly,
  ThighSoft.L, 左胸, J_Sec_L_Bust1; 尻尾 a tail, 胸リボン cloth, Chest and Hips the body's); a landing throwing a
  bust bone 3 mm further down with Lag 0.5 than with 0, no kick from a body already moving, a two-bone chain
  stretching only its last bone and the bake's Scale keys, Volume 0 changing the volume, 100 chains of 3 bones
  under 0.5 ms; the Jiggle the same for both views and half as far on a half weight; a skinned hair mesh rigged
  under a scaled character's Head bone with the body's group gone and the body's capsule seen, a posed head
  refused, forty cards merged into eight locks.
- Unit checks (round 172): Windows 11,369, 0 failed (905 tests); render-cache verify mode 11,370, 0 failed, 0 stale
  pictures. Linux with libraries: 11,100, 0 failed. Sanitizer build: 10,976, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`, `modifiers_ngon`,
  `editor_render`: 0 problems, 394 results with faces newly on top of each other in `pushpull`, no change.

## 2026-10-21 (round 171): strands - bend, root to tip, wind, colliders fitted to the skin, separation (task 0153, ADR 0049)

**Asked:** realistic hair and soft-tissue physics for Spring Bones, research based. This round: the strands.

**Added**
- **Bend** on a Spring Bones chain: each bone keeps its animated angle to the one before it, by this much - a curl
  keeps its shape, a strand bends where it was bent (0: the old behaviour, a curl straightens). **Root To Tip**: the
  pull toward the animation fades toward the tip, so long hair's tips swing free while the root follows.
- **Wind**, a component (Add Component > Scripts): a wind along the object's forward (or away from it, Spherical) of
  a strength in m/s, with turbulence (eddies by place and time) and gusts (the strength breathing over seconds) from
  periodic noise of the simulated time, so a bake comes out the same twice. A chain's **Wind** scales its response.
- **Colliders fitted to the skin:** a body bone's capsule is as wide as the vertices its vertex group moves (the 85th
  percentile of their distances from the bone in the rest pose, from the skinned meshes bound to the armature),
  cached per armature until a mesh changes; a seventh of the length without a skin, as before. A skirt rests on a
  thigh's real width.
- **Separate:** a chain is kept apart from other chains by its Radius (every stepped chain's tips in a uniform grid,
  two rounds of pushing apart and putting the lengths back), so a ponytail's strands and a skirt's pleats don't pass
  through each other.
- The labels' defaults set the new fields (hair: bend 0.2, the tips free, apart; an ear stiffer; a tail its curve;
  cloth free and apart); old chains keep the old behaviour (bend 0, root to tip 0, wind 1, no Wind in the scene).
- The Learn tab's lesson "Strands: a curl that holds, wind, the body's real width", after Spring Bones.

**Changed after the review**
- The bend held a world-space turn, so a curl unwound once the chain swung out of its animated plane: the animated
  angle is carried through the simulated bone's own swing now, and not held after a bone of no length. Root To Tip
  fades by the length along the chain, not the bone count. Separate never pushes strands further apart than the
  animation laid them (hair roots are closer than their radii), skips a NaN tip, and the body's collision runs again
  after it. The fitted radii pick one bone per vertex in one pass (ties to the lower group), take the narrowest skin
  per bone (a hair shell skinned to the head doesn't widen it), match a skin's armature the way the skin does, and
  are keyed by the bones' rest, names and lengths, the skins' ids, mesh versions and relative matrices (an undo or a
  rest edit refreshes them). The wind's clock is per scene and a bake runs its own; the drag converges at any step;
  the gusts are worked out once a substep.

**Checked**
- New tests (round 171): a curled chain holding its curl with bend 1 and hanging straight with 0; root to tip 1 lagging
  the tip more while the first bone lags alike; a 5 m/s wind deflecting a chain by over 5 cm, nothing at 0 m/s or with
  the chain's Wind 0, turbulence stirring it, a bake under the wind the same twice; a 0.2 m tube's bone fitted to
  0.2 m (a seventh without the skin) and a chain resting outside it; two strands kept apart by their radii; the same
  swing at 30, 60 and 144 fps with bend, wind and separation on; no NaN after a thousand 10 m jerks; the settings and
  the Wind saved and loaded; the lengths within 0.1 % under a move with bend; a curl held out of its plane; strands
  laid out closer than their radii staying as laid out; the hair label's defaults.
- Unit checks (round 171): Windows 11,350, 0 failed (899 tests); render-cache verify mode 11,351, 0 failed, 0 stale
  pictures. Linux with libraries: 11,081, 0 failed. Sanitizer build: 10,958, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`, `modifiers_ngon`,
  `editor_render`: 0 problems, 394 results with faces newly on top of each other in `pushpull`, no change.

## 2026-10-21 (round 170): the import preview with switchable shading (task 0152)

**Asked:** a preview when importing a model, with the shading switchable before anything lands in the project, so an
anime character arrives looking like one.

**Added**
- **Import Preview:** a dropped or opened model is read and shown in a dialog first - on a slow turntable (drag to
  turn it) under the editor's light, drawn by the CPU rasterizer (every material, painted maps included), with the
  import's summary and notes - and nothing is written until Import. The **Shading** row applies one look to every
  material of the model: As Imported (what the file made: MToon, MMD's toon), PBR, or a toon style (Classic, Guilty
  Gear, Soft Anime, Painterly, Manga) over the file's materials, their maps kept; **Outline** puts a line of the
  given width on every mesh. Import writes the model, its textures and the .mat files with that look, and the
  objects with their outlines; Cancel writes nothing.
- **Preferences > Import:** "Preview before importing" (off: the model comes in at once, as before; headless runs
  never show it) and the shading the preview opens with.
- **Console:** `import <file> [asis|pbr|classic|guiltygear|soft|painterly|manga]` imports at once with that shading
  (scripts and tests); without the word, the dialog when the preference is on.
- The Importing lesson gains "Importing: preview the look".

**Changed after the review**
- A look chosen at import rewrites a model's existing .mat files (As Imported keeps what was edited); several files
  dropped at once take their turns; the Outline switch reaches a file's own outlines; the turntable turns by the
  clock in 2-degree render steps, lit from its own camera; `soft` and the two-word style names work on the console;
  a file with no mesh imports at once; meshes without a material are shaded too; the views and the preview build
  their draw items with one pair of helpers.

**Checked**
- New tests (round 170): `import <glb> guiltygear` writing a .mat with Guilty Gear's fields, then `pbr` and `asis`
  rewriting it; the shading words; the dialog opening without writing a file or adding an object, its picture lit,
  the preference's shading applied, the Shading row's buttons switching the preview's materials (Guilty Gear, PBR,
  As Imported with the file's colour back), the Outline checkbox, Cancel leaving no trace, Import with Guilty Gear and
  an outline giving the objects, the .mat and the Outline; a re-import rewriting the .mat with the look (As Imported
  keeping it); `soft` and `guilty gear`; the preference's round trip and clamp; the plain import opening the dialog
  with the preference on, a second file waiting, the popup clicked away as a Cancel, the preference off importing at
  once.
- Unit checks (round 170): Windows 11,337, 0 failed (894 tests); render-cache verify mode 11,338, 0 failed, 0 stale
  pictures. Linux with libraries: 11,068, 0 failed. Sanitizer build: 10,945, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`, `modifiers_ngon`,
  `editor_render`: 0 problems, 394 results with faces newly on top of each other in `pushpull`, no change.

## 2026-10-21 (round 169): procedural anime textures (task 0150)

**Asked:** "procedural textures for anime backgrounds": foliage (leaves, grass), buildings (walls, windows, roofs), sky
and clouds, ground and water.

**Added**
- **Nine generators** in the Textures window, drawn as a background painter draws - two or three flat tones and a
  line, no gradients: Leaves (clusters of leaf shapes with a lit top, a shaded underside and a dark rim), Grass
  (blades from the bottom in three tones), Plaster (one tone with soft patches of a second and a few cracks), Window
  Grid (frames, lit and dark glass, a wall tone), Roof Tiles (overlapping rows with a shadow band and lines), Sky
  Gradient (zenith to horizon, a sun's disc and glow), Clouds (a white top over a flat shaded underside, an outline),
  Rocks (stones lit on their upper left with a ground line between) and Water (one tone with bent highlight lines).
  Every tone is a colour field; all repeat in the tile (Grass and Sky Gradient are cards: across only).
- **Alpha From Cover** on a texture: the albedo image's alpha is what the cards cover, in the image and in live
  shading. The cards (Leaves, Grass, Clouds) cover only their shapes and write that cover into a cover plane of
  their own (stacked cards add up; Warp and Tile move it with the colour; Mask by Height and Use Mask keep the mask
  as before), so a Cutout material shows holes between the leaves: a foliage card, a cloud over the sky (ADR 0017's
  amendment).
- **Presets** Anime Leaves, Anime Grass, Anime Wall, Anime Windows, Anime Roof, Anime Sky, Anime Clouds, Anime Rock and
  Anime Water (the Textures window's New list, `newtex <name> <preset>`), each with its tile size and no normal map.
- **`animemat <preset>`** on the console: the preset's texture asset and a flat-toned Toon material reading it (a
  double-sided Cutout for a card) on the selected objects, one undo step.
- The Textures lesson gains "Anime backgrounds: procedural textures".

**Changed after the review**
- The cover left the mask plane for its own (a second card replaced the first's cover, a Mask by Height above a card
  replaced the alpha, a card's Use Mask read its own cover, Warp and Tile moved the colour but not the cover, and
  live shading reset it); a card's colour lands everywhere so the cut has no grey fringe; Roof Tiles rounds its rows
  up to an even number (an odd count broke the stagger one tile over); the Sky Gradient's sun reaches round the
  seam; a Plaster crack is one edge between two cells, not a cell's outline; Grass leans no further than the scan
  looks; the Voronoi, Plaster and Rocks generators share one cell scan; `animemat` makes nothing when nothing with a
  MeshRenderer is selected; the texture's hand-written copy carries its output switches (a preset without a normal
  map got one back as an asset).

**Checked**
- New tests (round 169): each generator deterministic, periodic one tile over (the cards across only; Roof Tiles with
  odd rows and a sun at the sky's seam too), at most four tones over what it covers (the Window Grid's six are three
  pairs), covering over a tenth of the tile; each preset's layer and card flag; a leaf card's albedo with clear and
  solid texels (none clear with the flag off); `animemat Anime Leaves` on a Quad giving a Cutout toon material with
  holes between the leaves at sampled points, an unknown preset warning, undo, nothing made without a selection; the
  texture copy keeping its switches and hash; live shading's alpha agreeing with the image's; two cards adding up; a
  card's Use Mask reading the mask below; a Tile moving the cover with the colour in the image and the live path.
- Unit checks (round 169): Windows 11,300, 0 failed (892 tests); render-cache verify mode 11,301, 0 failed, 0 stale
  pictures. Linux with libraries: 11,031, 0 failed. Sanitizer build: 10,909 (one timing check at 0.204 ms a frame against its 0.2 limit, with a Windows build running beside it; it now skips sanitizer builds like the suite's other timing checks), 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`, `modifiers_ngon`,
  `editor_render`: 0 problems, 394 results with faces newly on top of each other in `pushpull`, no change.

## 2026-10-21 (round 168): the 2D look - custom normals, a face shadow map, inner lines and hatching, hold frames (task 0149, ADR 0048)

**Asked:** renders that "look more 2D while posed in 3D", research-based: shadow control with smooth normals, a face
shadow map, inner lines and hatching, animation on 2s and 3s.

**Added**
- **Custom normals:** a mesh can carry its own shading normals, one per vertex, which replace the smooth / flat /
  sharp-edge normals at every corner; saved with the mesh, carried through edits by position as shape keys are. An
  FBX or glTF whose normals an artist edited keeps them (when over 1 % differ from the recomputed ones by a degree;
  the import says so); exports write them as they are.
- **Smooth Normals and Normal Transfer modifiers** (Add Component > Mesh): the shading normals averaged over the
  surface N times (an anime face keeps its shape and loses its bumps' shading), or taken from a proxy object's
  surface at the nearest point (a sphere round the head: Blender's Data Transfer trick), both limited by a vertex
  group and moving no vertex. **Rigging > Anime Face Normals** (console `facenormals`) adds a sphere proxy fitted to
  the Head group (or the top of the mesh), without a renderer, and the modifier reading it.
- **Face Shadow Map** on a toon material: the key light's angle round the face (0 from the front, 1 from behind)
  read against the map decides the lit side instead of N.L - HoYo's SDF faces; the map is mirrored for a light on
  the other side; Face Shadow Softness and Face Forward (+Z, -Z, +X, -X: VRM faces +Z, MMD -Z). The Textures window's
  **Face Shadow** generator makes the plain sweep to paint over. The same in the rasterized views, F12 raster and
  both path tracers (the GPU viewport hands it to the CPU like the other painted maps).
- **Line Map** (inner contour lines multiplied into the colour, or only into the shadow tones with Lines Only In
  Shadow) and **Hatch Map** with Hatch Scale (strokes tiled over the UVs, shown in the shadow tones).
- **Outline:** Thickness Group (a vertex group scaling the line's width per vertex, Guilty Gear's painted line width),
  Distance Fade (thinner far away) and Min / Max Pixels (the width kept between them on screen, by the object's
  distance, in the views and raster renders).
- **Hold** on the Animation and Sequencer components: 1s, 2s, 3s or 4s - the pose changes every so many frames while
  the camera and the rest of the scene move every frame; Render Animation holds the same frames (from the range's
  first frame).
- Console `customnormals show|clear` (the way back from kept or transferred normals). A Skin modifier turns custom
  normals with its bones; the face shadow map takes no cast shadow on top (the map is the face's shadow); a file's
  normals are kept only when they are smooth and differ from both the area- and the angle-weighted recomputed ones
  (ADR 0048's notes).

**Changed after the review**
- Normal Transfer reads the proxy's render mesh (its own vertices and shading normals), with the closest-point
  search extracted; a normal modifier starts from the head's normals on every evaluation (it compounded); Anime Face
  Normals places the modifier before the Skin and makes a 16 x 12 proxy; the Outline's distance scale moves in 2 %
  steps, has its own shell slots per view (a path tracer kept a freed shell otherwise), honours an orthographic view
  and an object-unit thickness; the hold's float frame rounds up at 1e-4; the GPU material's packed ids are built
  unsigned; the FBX reader turns normals by the geometry's normal matrix.
- The Learn tab's Materials lesson gains "The 2D look".

**Checked**
- New tests (round 168): custom normals at every corner, saved and loaded, transferred by position, through a glTF
  round trip with the note, the file's normals kept only when they differ; Smooth Normals closing the sharpest edge
  and moving nothing, limited by a group; Normal Transfer radial from a sphere and following it; `facenormals` and its
  undo; `toon_face_lit`'s six cases and the generator's sweep; a sphere one lit tone from the front and one shadow tone
  from behind in the rasterizer, the CPU path tracer and the GPU's; the shadow multiplier, a grey line map (both ways)
  and a hatch map at a point; the Outline's group, cache key and distance scale; 23 frames on 1s / 2s / 3s; Hold saved.
- Unit checks (round 168): Windows 11,236, 0 failed (890 tests); render-cache verify mode 11,237, 0 failed, 0 stale
  pictures. Linux with libraries: 10,967, 0 failed. Sanitizer build: 10,845, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`, `modifiers_ngon`,
  `editor_render`: 0 problems, 394 results with faces newly on top of each other in `pushpull`, no change (the
  glTF weld is as before; custom normals touch no geometry).

## 2026-10-21 (round 167): toon styles (task 0148, ADR 0048)

**Asked:** "Is there any way to add more toon shading details in good engine" - "The Guilty Gear / Arc System Works
is a really good baseline. I want different options of toon shading to get versatility in style."

**Added**
- **Toon styles:** a Toon Style field at the top of a material's Toon group - Classic, Guilty Gear (Arc System Works'
  hard two-tone, the key light alone, the highlight where it is painted, no rim), Soft Anime (three tones, light
  wrap, a lit-side rim), Painterly (four soft tones under the sky's gradient), Manga (a hard grey two-tone). Picking
  one sets the fields; every field stays editable and the maps are yours. Console `toonstyle <style>` on the
  selected materials; with several selected, the style lands on all of them.
- **Painted control**, as the studios do it, through textures: a Shadow Bias Map (grey neutral, white keeps a point
  lit, black in shadow), a Toon Specular Map (where the spot may show) with the spot's own colour, a Toon Rim Map and
  a rim that shows on the lit side, the shadow side or both of the key light, and a Shade Map - the shadow side's own
  picture (VRM's MToon shade texture now comes in).
- **A Toon Ramp** instead of the bands: an image read by how lit the point is (left dark, right lit, or bottom dark,
  top lit), made by the Textures window's new Toon Ramp generator (2 to 5 tones, hard or soft); MMD's toon images
  land there.
- **Toon Hair Highlight** turns the spot into the band across the hair that anime draws; **Toon Light Wrap** lets
  the light reach round the form before the tones step; **Toon Key Light Only** keeps several lights from banding
  like a disco ball: the brightest sun bands, the others fill smoothly.
- All of it the same in the rasterized views, F12 raster, the CPU and GPU path tracers; the GPU viewport draws the
  plain settings and the ramp, and hands a material with painted maps or a hair highlight to the CPU, saying so (a
  VRM avatar with a shade texture draws on the CPU there; MMD models stay on the card). A lesson section "Toon
  styles" with Try-its.

**Checked**
- New tests (round 167): each style's fields, names in any spelling, `toonstyle` undone as one step, a .mat keeping
  every field; the shared shading (the spot's colour and map, the bias's ends, the shade map, the wrap, horizontal
  and vertical ramps, the hair band, the rim's sides and mask, the key light); on a sphere wrap 0.5 grows the lit
  share by over 10 %, the rasterizer and the CPU path tracer share the tones within 4 % (the GPU's within 3 % of the
  CPU's), a second light bands unless Key Light Only (rasterizer and path tracer within 4 %); the Toon Ramp
  generator's steps and a material reading it showing exactly two tones; MMD's ramp reading lit at the top; VRM's
  shade texture in Shade Map; MMD's ramp in Toon Ramp.
- Unit checks (round 167): Windows 11,182, 0 failed (885 tests); render-cache verify mode 11,183, 0 failed, 0 stale
  pictures. Linux with libraries: 10,914, 0 failed. Sanitizer build: 10,792, 0 failed, no reports. `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`, `modifiers_ngon`,
  `editor_render`: 0 problems, 394 results with faces newly on top of each other in `pushpull`, no change.

## 2026-10-21 (round 166): multi-edit materials (task 0147)

**Asked:** "add the ability to multi-edit materials if they share the same properties (such as shading model). For
example I can modify toon threshold universally amongst selected materials."

**Added**
- **Editing several materials at once** (Unity's multi-object editing, for materials): select several in the
  Materials window (Ctrl-click, Shift for a range), several `.mat` files in the Project window, or several objects
  in the Scene, and a change made in the fields reaches every one of them that has the field - a Toon Threshold
  lands on the toon materials and leaves a PBR one alone, an axis field on the axis ones, Shading Model on all (that
  is how a set is converted). A dash marks a field whose values differ (hover it to see the active one's); what you
  type lands on all; `+=0.1` or `*=2` moves each from its own value. One undo step; the edited assets are written
  back. Console: `matset <Field> <value>` on the selection. The anime lesson says so, with a Try-it.

**Checked**
- New tests (round 166): three toon materials and a PBR one selected in the Materials window: Toon Bands typed 3
  lands on the three, not the PBR one; `+=1` moves each from its own value; the differing fields are named;
  `matset ToonThreshold 0.4` reaches the three, `matset ShadingModel 1` all four, an unknown field warns; the `.mat`
  file on disk holds the values. Three objects selected (two toon, one PBR): Toon Bands typed in the active one's
  Inspector reaches the other toon object's material, not the PBR one, and one undo takes it back on both; `matset`
  on the selected objects the same.
- Unit checks (rounds 164-166 together): Windows 11,126, 0 failed (880 tests); render-cache verify mode 11,127,
  0 failed, 0 stale pictures. Linux with libraries: 10,862, 0 failed. Sanitizer build: 10,740, 0 failed, no reports.
  `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`,
  `modifiers_ngon`, `editor_render`: 0 problems, no change.

## 2026-10-21 (round 165): Inspector drop-downs, overlay layers, Anime and Manga as one effect each (tasks 0145, 0146, 0154)

**Asked:** "For the guides in the camera, have a drop down stack so it doesn't occupy too much of the inspector's
camera tab"; "the ability to hide certain objects like bone controls so I can see the mesh without too much
overstimulation ... I can hide the bone layer, but can still see IK/FK handles"; "For the camera 'Anime' and 'Manga'
filter. Make its own separate filters not a group composite and make it one unit."

**Added**
- **Drop-downs in the Inspector** (task 0145): a component's fields can sit under a foldout header, closed or open,
  that only the Inspector sees - files, undo, the hash, `set` and clips read the fields as before. The camera's
  Composition Guides are one closed drop-down now; a material's Toon fields sit in a Toon group and the axis ones in
  Axis Material.
- **Overlay layers** (task 0146): an Overlays menu in the Scene view's bar (Blender's Overlays popover) with a check
  per layer - Bones, Rig Controls (IK / FK handles), Light & Camera Icons, Curve & Lattice Points, Object Origins,
  Footprints & Contacts, Selection Outline, Grid, Gizmos, Statistics - and All Overlays. Hide the bones and keep the
  rig controls to pose a character by its handles while seeing its mesh. A hidden layer isn't clicked either (a click
  lands on what is behind), the bones' head and tail handles go with their layer, the Hierarchy and the gizmo still
  work on anything. Console `overlay list | all|<layer> [on|off]`; the choice is kept in the preferences. A lesson:
  "Overlays: See the Mesh, Hide the Rig".
- **Anime and Manga as single effects** (task 0154): the Looks menu adds one Anime effect (Lines, Line Color, Line
  Thickness, Crease Angle, Saturation, Contrast, Bloom, Bloom Threshold) or one Manga effect (Grayscale, Tones, Dots,
  Dot Cell, Dot Contrast, Lines, Line Thickness, Paper) instead of three or four. Each draws exactly what the stack it
  replaces drew (the parts run inside it), so a scene saved with the old composite looks the same, and the parts stay
  effects of their own for stacks of your own.

**Checked**
- New tests (round 165): the camera's Composition Guides header is there and closed, `set Camera.Thirds` lands, the
  file keeps it, the render hash ignores it, a click opens it (the Add Filter button below moves down by 12 rows) and
  another closes it; a Toon material shows an open Toon group and a closed Axis Material group, a PBR one no Toon
  group, and a plain reader still sees both groups' fields. A humanoid rig: a click on a bone selects it; Bones off:
  the handles vanish, the same click selects what is behind, the console still selects it, the rig controls stay;
  all off / all on; icons off: a click on the sun's icon selects nothing; the menu's Bones row turns the layer off
  and closes; the preferences keep `overlays_off=bones`. Anime against the old Line Art + Color Grading + Bloom stack
  and Manga against Grayscale + Halftone + Line Art + Paper: the largest channel difference 0; Lines off and Tones 3
  change the picture; the effects save and load; the old composite loads as four effects; the registry table test
  covers both.
- Checked with round 166's chain (see its entry).

## 2026-10-21 (round 164): area lights as emissive planes (task 0155, ADR 0050)

**Asked:** "The area light is still treated like a point light. The area light needs to act like a planar mesh using
an emissive material that has varying intensity." Chosen: the path-traced renders and the lightmap bakes / probes,
with physical falloff.

**Changed**
- **An area light is a glowing panel now.** Every renderer already sampled its rectangle or disc, but its brightness
  was Unity's range curve from the light's centre, with no inverse square from the points of its surface - so a wide
  panel close to a floor lit it like a lamp at its middle. Now each point of the panel sends light with its facing
  cosine and the inverse square of its distance, exactly what a quad with an emissive material gives (a test renders
  the two side by side: the same floor within 3 %). `Intensity` is how brightly the panel glows per square metre (an
  emissive material of strength pi x Intensity gives the same light), so a bigger panel gives more light; a 1 x 1 m
  panel a couple of metres from a wall gives about what it gave before.
- **Range is a window, not the falloff:** the light fades to nothing at Range (Unreal's window) and falls as the
  inverse square before it. Point and spot lights keep their curve.
- The path tracers (CPU and GPU), the rasterized views' exact term, the lightmaps (the shadow rays weigh each point of
  the light by its own falloff), the probes and MoonRay's export (the panel's radiance as it is) all take the one
  formula; a surface touching the panel stays finite (the inverse square stops at 1 cm).
- **Scenes lit by area lights change on purpose:** a 1 m panel 3 m away gives about a quarter of what it gave, the
  same panel 30 cm away about five times more, and a 10 m panel ten times a 1 m one (it gave the same). Raise or
  lower old scenes' area-light intensities to taste.

**Checked**
- New tests (round 164): a 20 cm panel 1 m up gives albedo x intensity x its area, 2 m up a quarter; a 10 x 10 m
  panel 3.5 m up against a 10 x 10 m emissive quad and the exact term, under the centre and 3 m out; the falloff past
  the panel's edge has no step; a floor 10 cm under a panel matches the exact term and a millimetre under it stays
  finite; Range 4 m ends at 4 m and is within 2 % of unlimited at 1 m; MoonRay's RectLight intensity is pi for
  intensity 1; the Light's help says so.
- Task 0030's references rewritten for the new integrand: the exact term within 1 % of a million-sample Monte Carlo
  over thirteen placements (strips, discs, 30 cm below a 10 m light), the rasterizer within 10 % of the path tracer,
  the bake within 5 %; task 0022's bake noise and the toon-under-area-light shares unchanged. Task 0022's Preview
  Lighting test cuts its "lit top" at half the chart's peak instead of an absolute value (the brighter panel let the
  dim sides in with their larger noise: macOS CI).
- Checked with round 166's chain (see its entry).

## 2026-10-19 (round 163): bone handles the right size on import (task 0144, ADR 0047)

**Asked:** the Carmen import's bone handles looked huge: "is there a way to fix that when we import any model format?"

**Fixed**
- A bone's length is in its own units, and a leaf bone (no bone below it) got a default of 0.1 of them. Blender's FBX
  export scales the armature x100 and writes an `_end` leaf under every chain, so each became a 10 m spike (and
  under a x0.01 armature bones vanished). Now a leaf's length is in metres whatever the scale: half its parent's,
  else 10 cm.
- Weightless end bones are folded into their parents: Blender's `_end` leaves (`Eye.L_end`, `HairBase.006_end`...),
  Mixamo's `HeadTop_End`, BVH's end sites, VRoid's `_end` spring tails: the parent's tail reaches them and no object
  is made for them. The Console says how many. Every format goes through the same code (MMD gives its own tails;
  a Collada leaf with no end site takes the metres default).
- A baked clip's "this number never moves" test allowed a millionth of a degree; on macOS the angles read back from
  quaternions jitter by more, so a turning bone got two extra flat tracks (#109's macOS run). A thousandth of a
  degree now, a hundredth of a millimetre for moves.
- Three tools read a bone's length as metres (the spring chains' collision radius, Add IK's pole distance, the body's
  capsules): under a scaled armature they were a hundred times off; they measure the bone in the world now.

**Checked**
- New tests (round 163): a glTF with an armature scaled x100: Upper 1 m to Lower, Lower 0.5 m to its folded `_end`,
  Tip 25 cm (half its parent's), the fold noted; Blender's four rig fixtures: no bone longer than its chain, no leaf
  longer than half its parent nor shorter than 2 cm; a leaf's tail dragged to 0.3 m reads 0.3 m.
- The Rigging, IK, skin and spring groups unchanged; after the review every leaf is checked against half its parent
  and Mixamo's `HeadTop_End` folds.
- Unit checks (rounds 162-163 together): Windows 11,014, 0 failed (865 tests); render-cache verify mode 11,015,
  0 failed, 0 stale pictures. Linux with libraries: 10,750, 0 failed. Sanitizer build: 10,628, 0 failed, no reports.
  `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`,
  `modifiers_ngon`, `editor_render`: 0 problems, no change.
- The user's folder, imported headless by the rebuilt app: CarmenB, CarmenF and CarmenH textured from `textures/`,
  OL bare, 16 end bones folded, the handles the size of the bones.

## 2026-10-19 (round 162): the texture finder (task 0143, ADR 0047)

**Asked:** a Sketchfab download (`carmen-limbus-company/source/CarmenSF.fbx` with `textures/Carmen{B,F,H}_Base_color.png`)
came in untextured: "why isn't the textures importing?", and then "a system where this can automatically find relevant
textures in a folder, so that I don't have to look for them and import them manually".

**Fixed**
- The FBX links no textures at all, and the matcher of round 154 looked only in the model's own folder (`source/`)
  and read `CarmenB_Base_color` as a map called "color" of `CarmenB_Base`, never `CarmenB`.

**Added**
- **The texture finder:** every import looks through the folders around the model - its own folder and three levels
  below, then the folders beside it two levels down (Sketchfab's `source/` + `textures/`, Unity packages); never
  into Downloads, Desktop, Documents, a home folder or a drive root, and not into folders full of other files; at
  most 2,000 images.
- **Names read as people write them:** two-word kinds (`Base_color`, `Normal_OpenGL`, `Roughness_map`,
  `Metallic_Smoothness`), sizes and tiles dropped (`_2k`, `_4096`, `.1001`), AO / opacity / height maps recognised
  (and reported: Blendity has no slot for them).
- **Pairing by likeness:** each bare material is scored against every set (the same name; one inside the other;
  the words they share) through its own name and its meshes'; the best above 0.6 wins, nearest folder first. When
  as many materials as sets are left and each has its own best, they pair by that; a lone material with a lone set
  always does.
- **The Console says** which file went to which material and from where, what stayed bare, what was seen but not
  used, and that a wrong guess is one slot to change in the Inspector.
- **Learn:** the "Importing models" lesson says so.

**Checked**
- New tests (round 162):
  - Sketchfab's layout with two-word names: RobeA and RobeB textured (RobeB's normal and roughness too), Trim's own
    colour kept, the AO map reported;
  - `Body` pairs with `T_character_body_2k_BaseColor` (and its normal); `Hand_R.png` alone textures nothing;
  - a model loose in a folder called Downloads takes nothing from the folders around it; in a folder of its own
    under Downloads it looks around as usual;
  - dropped into the editor: the textures copied into `Assets/Textures/robe/`, the `.mat` files using them.
- Round 154's tests unchanged (the moved link, the lone `Rock_d`, `Hand_R`, the flat colours).
- After the review: a Unity Textures folder of `.meta` sidecars is read; a height-only set leaves its material bare
  and is reported; `Wood_01` and `Rock_01` don't pair; `Arm` doesn't take `Armor`'s set; the user's folder made
  synthetic (CarmenB/F/H textured, OL bare).
- **Fixed on review:** a Unity texture folder (one `.meta` a texture) was skipped as "a folder of other files"; the
  crawl had no bound on folder listings (a library on a slow disk could stall the drop: now 400 listings or 150 ms);
  a set of only AO or height maps could "win" a material and leave it bare without a word.
- Checked with round 163's chain (see its entry).

## 2026-10-19 (round 161): Collada (task 0141, ADR 0047)

**Asked:** round 154's "other rigged character formats": Collada, what Blender, Maya, SketchUp and Mixamo export.

**Added**
- **Collada (.dae) import** through Blendity's own XML reader (`core/xml.h`): geometry (triangles, polylists,
  polygons) with its materials (the effect's diffuse colour or texture, emission, shininess as roughness,
  transparency), skins with their joints and bind matrices, the node hierarchy, and the animations (matrix channels,
  or translate / rotate / scale channels by name) as one clip. The file's up axis and unit are honoured, so a Z-up
  model in centimetres stands the right way up at the right size.
- **Learn:** "Collada: .dae scenes".

**Checked**
- New tests (round 161), on a Collada file the tests write themselves (Z up, centimetres, a textured square skinned to
  two joints, a matrix animation):
  - standing in metres and facing +Z, its texture found in `tex/`, its emission and roughness read, its groups named
    after the joints, at rest unmoved;
  - the clip of 24 frames: at its end the upper joint has turned 90 degrees and the square's top with it;
  - 40 files cut short refuse or import without harm;
  - dropped into the editor: `Skin.mat` with the texture copied in, the clip playing on the root; the lesson's Try-it;
  - after the review: a Y-up metre cube with a byte-order mark and CDATA comes in facing out, its translate and rotate
    component channels moving it 2 m and turning it 90 degrees.
- **Fixed on review:** the `<source>` cache outlived its file (a later import could read another file's UVs); CDATA cut
  short read past the buffer; a skin over Blender's morph controller lost its mesh; X-up files came in inside out.
- Unit checks (the whole stack, rounds 156-161): Windows 10,985, 0 failed (859 tests); render-cache verify mode
  10,986, 0 failed, 0 stale pictures. Linux with libraries: 10,721, 0 failed. Sanitizer build: 10,599, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`,
  `modifiers_ngon`, `editor_render`: 0 problems, no change.

## 2026-10-19 (round 160): BVH motion capture (task 0140, ADR 0047)

**Asked:** round 154's "other rigged character formats": motion capture.

**Added**
- **BVH import:** a `.bvh` (Biovision motion capture: CMU, Mixamo's exports, Rokoko) comes in as a skeleton of bones at
  the file's offsets and its motion as a clip (`Assets/Animations/<file>/<file>.anim`) playing on it, at the file's
  frame rate. Rotation channels are composed in the file's order; a skeleton taller than 10 units is taken to be in
  centimetres.
- **Learn:** "Motion capture: BVH".
- Out (said in the Console): retargeting onto another character (bake the clip or copy its keys by name).

**Checked**
- New tests (round 160), on a BVH the tests write themselves (hips, spine, head and an end site; the hips walking a
  metre forward and the spine bending 90 degrees over 24 frames at 24 fps, in centimetres):
  - the skeleton in metres (the hips 1 m up, the head above the spine), the clip of 24 frames at 24 fps;
  - at its end the hips have moved 1 m towards +Z and the spine has turned 90 degrees (within 0.1 degree);
  - damaged files (cut anywhere, a brace too many, a NaN) refuse or import without harm;
  - dropped into the editor: the clip made and playing on the skeleton's root; the lesson's Try-it;
  - after the review: CRLF, tabs and `Frames:25` read the same; what a damaged file gives is whole.
- Checked with round 161's chain (see its entry).

## 2026-10-19 (round 159): animation clips from FBX and glTF (task 0139, ADR 0047)

**Asked:** round 154's "other rigged character formats": a model's own animations (Mixamo, Blender actions, glTF).

**Added**
- **A model's animations come in as clips**, as Unity's importer makes them: `Assets/Animations/<model>/<clip>.anim`
  for each, the first playing on the model's root through an Animation component.
  - **FBX:** each animation stack, at the file's frame rate, every node's turn, move and scale a frame at a time (a
    track for each that changes); blend-shape channels as BlendShape tracks.
  - **glTF:** each animation at the frame rate its keys fit (24, 25, 30, 50, 60, 120; else 30); moves, scales and
    morph weights at the file's own keys (step and linear; cubic splines baked), turns baked a frame at a time from
    the file's slerp.
  - Euler angles chosen nearest the frame before: no wraps.
- **Learn:** "Imported animations: a model's clips".

**Checked**
- New tests (round 159), on Blender's FBX and GLB of a two-bone arm (`tests/fixtures/anim_arm.*`, `make_anim.py`):
  - one clip of 24 frames at 24 fps from each; at its end the upper bone has turned 90 degrees (within a degree) and
    the shape key is at 1; at its start nothing has moved;
  - dropped into the editor: the FBX's `Armature|ArmatureAction` saved as `Armature_ArmatureAction.anim` and playing
    on the root; a second copy of the model plays its own clip (its armature renamed); the lesson's Try-it.
- **Fixed on review:** Blender's clip names (`Armature|ArmatureAction`) couldn't be written on Windows, so every FBX
  action was dropped silently; a second import of a model lost its clip to the armature's uniqueness rename; baked
  clips kept every frame (now reduced to the frames that matter); a glTF clip started a frame early.
- Checked with round 161's chain (see its entry).

## 2026-10-19 (round 158): VRM characters (task 0138, ADR 0047)

**Asked:** round 154's "other rigged character formats": VRM, the VTuber avatar format.

**Added**
- **VRM import** (0.x and 1.0; VRoid Studio, UniVRM): a `.vrm` dropped onto the window comes in as a character facing
  +Z.
  - **Bones:** the humanoid's bones named for the rig tools (`leftUpperLeg` -> `LeftUpLeg`).
  - **Expressions** (A, I, U, E, O, blink, joy...) as shape keys on the meshes they move, each the weighted mix of
    morph targets the file binds.
  - **Spring bones** as Spring Bones chains with the file's stiffness, drag, gravity and hit radius.
  - **MToon** as Toon shading (the shade colour, toony edge, shift; cutout and transparent modes; unlit shaders) with
    an Outline of the file's width.
  - **Meta:** the title, author and license in the Console.
  - **Not carried over:** shade textures, rim light, matcaps, first-person and look-at settings.
- **Learn:** "VRM characters: VRoid and UniVRM avatars".

**Checked**
- New tests (round 158), on an avatar the tests write themselves as a GLB, in 0.x and 1.0 form:
  - the bones named; the left leg on the character's left and the hair behind the head in both (the 0.x model turned
    half a turn);
  - `a` / `aa` the whole first morph (5 cm), `blink` half the second; the face facing +Z;
  - the hair's chain with stiffness 0.5, damping 0.4, gravity 0.5, radius 3 cm;
  - the material Toon with a shadow ratio of 0.5, a 5 mm outline in the file's colour; the meta in the notes;
  - dropped into the editor: `Skin.mat` (Toon), the outline, the lesson's Try-it;
  - after the review: bound with inverse bind matrices, the face stays at rest and follows the head; the hair outside
    the skin's joints still gets its chain; a 0.x shadow comes in at its sRGB-corrected ratio.
- **Fixed on review:** 0.x colours are sRGB (a VRoid shadow came in much too light); a forking spring root got one
  chain (every strand below it hung rigid); a spring on an accessory node outside the skin was dropped.
- Checked with round 161's chain (see its entry).

## 2026-10-19 (round 157): MMD motions (task 0137, ADR 0047)

**Asked:** round 154's MMD request: its dances.

**Added**
- **VMD import:** drop a `.vmd` onto the Scene view with an MMD character selected (or File > Import, the Project
  window, the console's `import <file>`), and it becomes `Assets/Animations/<character>/<motion>.anim`, played by the
  character's Animation component.
  - **Bones** by MMD's names (`左ひざ`, `頭`): a motion fits any model with the standard bones. The keys are turned
    from MMD's rest-frame quaternions into each bone's own rotation (Blendity's bones point at their tails).
  - **Timing:** MMD's Bezier curves become the keys' slopes; a run the curves can't follow within half a degree or a
    millimetre is keyed every frame from the true arc.
  - **Faces:** morph keys on the BlendShape sliders of every mesh with that morph.
  - **IK:** the legs' IK switched off and on keys the constraint's Weight.
  - **Reported:** bones and morphs the character hasn't; the motion's camera (not imported).
- **Learn:** "MMD motions: VMD dances on your character".

**Checked**
- New tests (round 157), on a motion the tests write themselves:
  - the hips' 90-degree turn within 0.6 degrees at every checked frame; the ankle's eased 170-degree turn half done at
    its midpoint, exact at its end;
  - the centre moved 0.2 m towards +Z; `あ` at 0.5 halfway; the IK weight 1 then 0; the missing bone reported;
  - dropped into the editor: the `.anim` made and played (the hips turn in Play), the `.vmd` copied in beside it, and
    the lesson's Try-it.
- **Fixed on review:** keys are built per track and assigned whole (a long dance was inserting each of thousands of
  keys into a growing list); a densified run can't flick into its last key; a bone keyed at its rest gets no track
  (its hair would have fought its spring); the motion file is kept beside its clip.
- Checked with round 161's chain (see its entry).

## 2026-10-18 (round 156): MMD models (task 0136, ADR 0047)

**Asked:** round 154's "make the program be able to handle mmd models as well".

**Added**
- **PMX (2.0, 2.1) and PMD import**, read by Blendity's own code. Drop a `.pmx` or `.pmd` onto the window.
  - **Scale and facing:** scaled from MMD's 8 cm unit to metres and turned to face +Z like every other character.
  - **Skin:** skinned to its bones (BDEF1/2/4; SDEF and QDEF read as linear).
  - **Bones:** each bone points at its tail. The standard bones get the names the rig tools read (`左ひざ` ->
    `LeftLeg`), so Walk Cycle and the physics tools find its legs.
  - **Leg IK:** an IKConstraint on each knee, aimed at the foot's IK bone, with a pole in front of the knee.
  - **Physics:** the bones MMD simulates (hair, skirts) become Spring Bones chains.
  - **Faces:** vertex and group morphs become shape keys.
  - **Materials:** Toon shading with MMD's colours, textures and toon ramp; the model's edge becomes an Outline.
  - **Not carried over** (listed in the Console): bone, UV and material morphs, and inherited rotation.
- **Japanese text:** PMX's UTF-16 or UTF-8, and PMD's Shift-JIS, decoded by the operating system (Windows' code page
  932, iconv on Linux and macOS).
- **Learn:** "MMD models: PMX and PMD".

**Checked**
- New tests (round 156), on a character the tests write themselves (no third-party model):
  - Shift-JIS and UTF-16 names decode;
  - the PMX, in UTF-16 and UTF-8:
    - placed to the micrometre, the thigh pointing down its length;
    - the knee's IK on `LeftFootIK` with its pole in front, the hair a spring chain of its body's radius;
    - its front faces +Z, and a turned knee moves the vertices it carries;
    - the morphs lift 8 cm and 4 cm;
    - a Toon material of MMD's lit colour with its texture and its ramp's shadow, and a 6 mm outline;
  - the PMD twin, Shift-JIS names and all, the same;
  - 240 damaged files (cut short, bytes flipped) imported or refused without harm;
  - dropped into the editor: `肌.mat`, its texture in Assets, the walking analysis finding the hips and both legs, and
    the lesson's Try-it;
  - after the review: the kitchen sink (every optional PMX record, vertex indices past 127, the skin on D bones, the
    triangles the other way round) imports the same; two copies of one character each aim their IK at their own
    controls, through a save and a load too; a forked hair gets three chains; a shared toon ramp is reported; a hard
    edge stays split; 5,000 morphs are capped at 4,096 keys; 1,000 nested bones and a vertex at 1e30 are refused; a
    parent loop is broken.
- **Fixed on review:** IK constraints found their target by name anywhere in the scene, so a second copy of a
  character aimed its legs at the first (now its own character first, `ik_target_of`); the "D bones" most PMX models
  weight their legs to now hand their weights to the bones IK and walking drive.
- Checked with round 161's chain (see its entry); round 156's own chain on task 0136 alone: Windows 10,939, 0 failed
  (848 tests), verify 10,940, 0 stale; Linux 10,675; sanitizers 10,553; stress no change.

## 2026-10-18 (round 155): shape keys (task 0135, ADR 0047)

**Asked:** round 154's MMD question: its faces, and VRM's and many FBX faces, are shape keys.

**Added**
- **Shape keys** (blend shapes, morph targets, vertex morphs): named offsets on a mesh, weighted per renderer.
  - The MeshRenderer's BlendShape sliders, animatable as `MeshRenderer.BlendShape/<name>`.
  - Added to the basis before every modifier (the skin bends an expression with the head), on the fast deform path,
    so an animated face doesn't rebuild the mesh.
  - Saved with the scene (sparsely), the weights by their exact names.
  - Carried through edits that add or renumber vertices (subdivide, triangulate, extrude), as vertex groups are.
  - Edit Mode shows the basis being edited. Apply Modifiers refuses a mesh with keys, as Blender does, rather than
    adding them twice.
- **Imports:** FBX blend shapes and glTF morph targets with their names and values.
- **Console:** `shape list | add <name> [amount] | set <name> <weight> | remove <name>`.
- **Learn:** "Shape keys: faces and morphs".

**Checked**
- New tests (round 155):
  - a weight of 0.5 adds exactly half (0 error), the mesh itself when none is in use;
  - saved and loaded; a clip's track at frame 5 of 10 gives 0.5;
  - a key on a bone turned 90 degrees turns with it (1e-7);
  - Blender's FBX and GLB keys, the 0.5 m lift and the value 0.3 come in;
  - the lesson's Try-it;
  - a changing weight deforms the same mesh again (no copy per frame); Edit Mode's cage is the basis;
  - Apply Modifiers refused with keys;
  - keys kept through a triangulate and a subdivide, every old vertex with its offset (0 error);
  - "Smile Left" and "Smile_Left" saved apart; a damaged key line (a count past any integer, an index past the mesh)
    loads without harm.
- **Fixed while checking:** the views keyed their pictures on the meshes without the shape keys' weights, so after
  `shape set` the Scene view could show the shape from before (render-cache verify mode found it, in the lesson's
  Try-it). `GameObject::evaluated_keys` now mixes them in, as `evaluated_mesh` does.
- Unit checks: Windows 10,907, 0 failed (841 tests); render-cache verify mode: one stale picture (above), and after
  the fix 10,908, 0 failed, 0 stale pictures. Linux with libraries: 10,643, 0 failed. Sanitizer build: 10,521,
  0 failed, no reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures. Stress `editor`,
  `pushpull`, `modifiers_ngon`, `editor_render`: 0 problems, no change.

## 2026-10-18 (round 154): materials made and applied on import (task 0134, ADR 0047)

**Asked:** "When importing external fbx models, can the program auto create a material and apply it. Furthermore,
make the program be able to handle mmd models as well. Feel free to implement other rigged character formats as
needed." The user chose full MMD support, VRM, FBX/glTF animations, BVH and Collada, materials as .mat assets, and
synthetic test files (tasks 0134-0142).

**Added**
- **Every import makes `.mat` assets** in `Assets/Materials/<model>/`, applied to the model. An edited one is kept on
  re-import.
- **Textures:**
  - copied into `Assets/Textures/<model>/`, including the ones packed inside an FBX (they were dropped);
  - a link to another machine's path found by name beside the model, in its `.fbm`, `textures` or `images` folder;
  - a bare material given the texture set beside it by name (`Body_BaseColor`, `T_Body_N`, `_ORM`...).
- **Maps:** gloss, smoothness and packed maps (glTF's metallic-roughness, which wasn't read at all, Unity's
  metallic-smoothness, Unreal's ORM) are split into the maps Blendity reads.
- **Materials:** every mesh gets one; FBX transparency and double-sided are read.
- **Two models of one name** (every Sketchfab download is `scene.gltf`) get folders of their own
  (`Materials/scene 2/`); importing the same one again reuses its folder. A texture or embedded image sharing a name
  with another gets its own file.
- **Matching by name is careful:** a single-letter suffix (`_d`, `_n`, `_r`) counts only in a set of two maps or more
  (`Hand_R.png` is not roughness); a folder's only set goes only to materials the import made, never over a file's
  own flat colours.
- **File > Import** from outside the project copies the model in, as dropping it does. The console has
  `import <file>`.
- **The FBX exporter's Embed Textures:** the images go inside the file, as Blender's do.
- **Learn:** "Importing models: their materials made for you".

**Checked**
- New tests (round 154):
  - Blender's FBX with embedded textures comes in textured (red 204);
  - its GLB's packed map split exactly: roughness 64, metallic 255;
  - a moved link found, a texture set matched by name, a gloss map inverted (63);
  - every STL mesh gets a material;
  - Blendity's embedded FBX reads back with its image deleted;
  - `Skin.mat` made and used, and an edit to it kept on re-import;
  - two models called `r154twin.obj` imported into two folders, each its own texture (red 255, blue), the first
    imported again into its own;
  - flat Red and Blue kept beside an unrelated texture; an `.mtl` in a subfolder finds its texture there;
  - a packed map split once for every material using it; two embedded `diffuse.png` kept apart;
  - the tests' import cache in a scratch home.
- Checked with round 155's chain (see its entry).

## 2026-10-18 (round 153): MoonRay's image fills in while it renders

**Asked:** "Why does MoonRay doesn't seem to render?" It did: a 1280x720 frame at 128 samples takes about a minute,
and nothing showed until it was done. The user agreed: "the image fill is also good."

**Changed**
- **MoonRay renders with checkpoints:** an image after every quality step, about a second apart, written beside its
  scene.
  - A watcher on the render's own thread reads each new checkpoint (never the editor's thread: it is 14 MB).
  - The Render panel shows it through the same view transform as the final image, with "filling in (N updates)" and
    the time.
  - MoonRay prints no percentage in this mode.

**Fixed**
- MoonRay's own default image (`scene.exr`) was written into whatever folder it ran from (an untracked
  `blendity/scene.exr` came from it). It now goes into Blendity's scratch folder with the rest.

**Checked**
- **A new test (round 153):** the stand-in MoonRay writes a checkpoint at half brightness, then the final image; the
  Render panel shows 0.250, then 0.500.
- **With the real MoonRay:** 53 s into a 1280x720 frame the panel showed the image, after 43 updates; nothing was
  written where it ran.
- Unit checks: Windows 10,882, 0 failed (827 tests); render-cache verify mode 10,883, 0 stale pictures. Linux with
  libraries: 10,618, 0 failed. Sanitizer build: 10,496, 0 failed, no reports. `editor_fuzz` in verify mode: 6,000
  frames, no crash, 0 stale pictures. Stress `editor`, `pushpull`, `modifiers_ngon`, `editor_render`: 0 problems, no
  change.

## 2026-10-18 (round 152): automatic walking and AI inbetweening written up (task 0132)

**Asked:** the plan's write-up.

**Added**
- Learn: "Automatic walking: which tool when" (Walk Cycle, Footsteps, Locomotion) and "AI inbetweening: the motion
  between two keys", each with a Try-it and questions.
- README, ARCHITECTURE, USABILITY; ADR 0046; the licences of the optional download (ONNX Runtime's MIT notice, the CMU
  database's credit) and `extern/onnxruntime/`.

**Checked**
- A new test (round 152) runs both lessons' Try-its.
- Unit checks (rounds 148-152 in one chain): Windows 10,881, 0 failed (826 tests); the whole suite in render-cache
  verify mode: 10,882, 0 failed, 0 stale pictures. Linux with libraries: 10,617, 0 failed. Sanitizer build: 10,495,
  0 failed, no reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.
- With a real install (`BLENDITY_AI_TEST_INSTALL`): Windows 60 frames in 3.1 ms; Linux under the sanitizers 4.2 ms,
  no reports; on both the walk's middle made again 9.3 cm from the walk, the keys' interpolation 14.5 cm.
- Stress: `editor`, `pushpull`, `modifiers_ngon`, `editor_render`: 0 problems, no change.

## 2026-10-18 (round 151): fine-tuning on your own clips (task 0131)

**Added**
- **Fit to Clips** (`ai finetune`):
  - every clip of the project that animates the character (about three minutes at most) is sampled, then fitted on
    a worker with progress and Stop;
  - a correction of the network's last layer, solved exactly (ridge regression), saved as
    `Assets/AI/<character>.aihead` and used for that character's spans;
  - `ai finetune off | on` goes back to the shipped model and returns to the fit.
- ONNX Runtime's standard build has no on-device training (its training API is null in 1.31.0), so the plan's training
  API became a fit of the last layer in Blendity's own code (ADR 0046).

**Checked**
- A new test (round 151) with a stand-in network:
  - the fit lowers the RMS error on the clips it was fitted to;
  - off, a made span is exactly the shipped model's;
  - Stop keeps the last fit.
- Unit checks: with round 152's (one chain for rounds 148-152; there).
- Stress: `editor`, `pushpull`, `modifiers_ngon`, `editor_render`: 0 problems, no change.

## 2026-10-18 (round 150): AI inbetweening in the Animation window (task 0130)

**Added**
- **AI Inbetween** (the Animation window, with the switch on and the download installed):
  - makes the span between the keys around the playhead in a style (walk, run, jump, fall, crawl, combat,
    acrobatic, other), on a worker, shown as a purple ghost (grey once the keys change);
  - Variation, and Apply as one undo step;
  - `ai span [from to] | style | styles | variation | apply | off`.
- `ai::make_request` and `ai::apply`:
  - the rig mapped onto the model's skeleton (mirrored, faced, scaled by the legs, each bone's rest direction aligned
    to the data's), a 60 fps clip resampled to the model's 30;
  - only the frames strictly between the keys keyed, the keys at the ends kept with their outer slopes frozen;
  - a turn keyed whole turns away spread over the span.

**Fixed after review**
- **Clips told apart only by their version:** Apply could write one character's span into another's clip. A span now
  knows its clip and character.
- **The ends' Auto slopes followed the new keys** and bent the curves outside the span. They are frozen now.
- **A yaw keyed past 180 degrees** spun round on the last frame. It is spread over the span now.
- **Fine-tuning kept every window in memory.** It now uses running sums, capped at about 4,000 windows and three
  minutes of clips.
- **Work on the editor's thread:** check, install and loading the runtime now run on workers, and the model's
  description is read once.
- **Installs:**
  - an archive's members are checked before unpacking, into a staging folder moved into place;
  - removing deletes only the install's own files;
  - the model's output size is checked.
- **Other fixes:**
  - the hands', head's and toes' rest alignment;
  - Variation's seed sticking;
  - a span past the clip's end refused;
  - the AI workers waited for on closing.

**Checked**
- New tests (round 150), with a stand-in network:
  - only the span is made (29 of 29 frames), its ends and every key outside untouched;
  - the curves outside the span move 0.000000 m; a 200 degree yaw is met in steps of 6.7 degrees at most;
  - another character's clip is never written;
  - the same seed gives the same keys; Apply is undone whole;
  - four legs, a span past the end and a NaN answer are refused;
  - a pose goes to the model's skeleton and back to 0.000 degrees.
- With the real model (where `BLENDITY_AI_TEST_INSTALL` names an install; skipped on CI): a walk's middle 29 frames
  made again are 9.3 cm from the walk, the keys' interpolation 14.5 cm.
- Unit checks: with round 152's (one chain for rounds 148-152; there).
- Stress: `editor`, `pushpull`, `modifiers_ngon`, `editor_render`: 0 problems, no change.

## 2026-10-18 (round 149): the AI download, optional as MoonRay (task 0129)

**Added**
- **The download:** `tools/inbetween/package.py` builds `blendity-ai-<platform>-ai-v1` with a `.sha256` each, from
  Microsoft's ONNX Runtime 1.31.0 (MIT) and the trained model: Windows 23.6 MB, Linux 28.0 MB, macOS on Apple silicon
  29.3 MB. Publishing them is the user's call.
- **In Blendity:**
  - the AI dialog, offered once when the switch is first turned on (never at startup);
  - Preferences > Animation Assistants: Install / Remove;
  - `ai status | install [archive] | remove | check`.
- ONNX Runtime is loaded with LoadLibrary or dlopen through its C API only when used, so the base build has no
  dependency on it.

**Checked**
- New tests (round 149):
  - a wrong checksum is refused;
  - an install that can't run is removed again;
  - an archive holding a foreign file is refused;
  - without the download, AI Inbetween says how to get it.
- With a real install: ONNX Runtime loads and makes 60 frames in 3.1 ms.
- Unit checks: with round 152's (one chain for rounds 148-152; there).
- Stress: `editor`, `pushpull`, `modifiers_ngon`, `editor_render`: 0 problems, no change.

## 2026-10-18 (round 148): the inbetweening network trained on CMU, GO (task 0128, ADR 0046)

**Asked:** the plan's AI inbetweening, trained on CMU motion capture. The user approved the downloads (PyTorch,
NumPy, ONNX, onnxruntime; the CMU database).

**Added**
- `tools/inbetween/` (build-time only):
  - `cmu.py`: the CMU database (2,503 clips, 10.5 hours) on one 22-joint skeleton at 30 fps, labelled by style;
  - `model.py`, `train.py`: a 4.9M-parameter transformer correcting interpolation;
  - `export.py`: ONNX with its last features, any window length;
  - `styles_eval.py`; `package.py`.
- The data and the Python environment stay outside the repository (`.venv/` and `runs/` git-ignored).

**Checked**
- **On the held-out subjects:** the joints' mean error, interpolation against the network:

  | Gap | Interpolation | Network |
  |---|---|---|
  | 15 frames | 2.7 cm | 2.0 cm |
  | 30 frames | 5.7 cm | 4.0 cm |
  | 60 frames | 10.2 cm | 7.2 cm |
  | 120 frames | 16.8 cm | 13.1 cm |

- **By style:** walk 62% better, jump 53%, acrobatic 40%, combat 35%, fall 20%, other 18%.
- **Foot sliding:** worse than interpolation past two seconds, recorded.
- **Speed:** 60 frames in 3 ms on the CPU. **GO.**
- **Fixed on the way:** the first export fixed the window's length inside attention (every other span failed in
  Blendity). The encoder is written out for export, and the export is checked at gaps of 1, 29 and 120 frames.

## 2026-10-18 (round 147): Locomotion, walking live in Play (task 0127)

**Asked:** the plan's "both baked into clips and live in Play" (round 145's question).

**Added**
- **The Locomotion component** (with the Locomotion assistant on): Walk Cycle's gait made frame by frame from how the
  character moves - W walks, Shift+W runs, A and D turn, or a PlayerController, a Rigidbody or a script's `set_move`
  moves it.
  - The phase goes on with the speed; the walk blends into the run; let go, and it finishes the step and stands.
  - Each swinging foot is aimed at where the body will be when it lands, on the ground there (a grid of the scene's
    meshes gathered at Play start): ramps and stairs.
  - The pelvis stays in reach of both feet and leans into turns. Upper Body off leaves the spine, head and arms to a
    clip.
- `gait::apply`: putting a pose on the rig, shared by bake and Locomotion.
- Learn: "Locomotion: walking live in Play".

**Fixed after review**
- A prop held in a hand was snapped back every frame (and one a script destroyed would have been written to after):
  only bones and IK controls are posed now, kept by id, and the rig is read again if one goes.
- A dog let go kept stepping on the spot (its feet are never all down while it walks): a foot that lands stays down.
- The ground left out physics bodies and other characters; Follow Ground off while physics or a controller moves the
  object.

**Checked**
- New tests (round 147):
  - 3 s of W: 3.97 m (3.95 expected), planted feet still to 0.001 mm, the stride Walk Cycle's to the millimetre,
    0.013 ms a frame;
  - running round a turn the hips lean into it; let go of a run, standing in 0.92 s (within the slowing and one step);
    a dog stops too;
  - up a ramp every landing on its surface; a prop a script moves is left alone, and destroyed in Play breaks nothing;
  - the assistant off, through the editor's Play: no object moves;
  - the lesson's Try-it, then Play with a script's `set_move`.
- Unit checks (rounds 146-147 in one chain): Windows 10,872, 0 failed (817 tests); the whole suite in render-cache
  verify mode: 10,873, 0 failed, 0 stale pictures. Linux with libraries: 10,608, 0 failed. Sanitizer build: 10,486,
  0 failed, no reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.
- Stress: `editor`, `pushpull`, `modifiers_ngon`, `editor_render`: 0 problems, no change.

## 2026-10-18 (round 146): Footsteps, walking along a path (task 0126)

**Asked:** the plan's footsteps along a path, as 3ds Max's Biped (round 145's question).

**Added**
- **Footsteps** (the Rigging window's section, with the assistant on): Walk Along Path lays footprints along a curve at
  Walk Cycle's stride - short first and last steps, the inner foot's shorter in turns - each dropped onto the mesh
  under it, and keys the character walking on them (feet planted, the pelvis over them at their height, a higher swing
  over a step up).
  - Each print is an object (blue left, red right, its landing frame drawn on it): move or turn it, or change its Frame,
    then Update. Re-plan lays them out again and keeps the ones moved by hand. One undo step each.
  - Console: `walk path <curve> | walk steps update | walk steps replan`.
- `scene_raycast` and a ground grid in the scene module (the footprints; Locomotion's feet).
- Learn: "Footsteps: walking along a path".

**Fixed after review**
- A print dragged beyond the leg's reach wasn't reached: the pelvis now goes towards it.
- Re-plan kept only moved prints, matched by frame (lost after a style change): turned and retimed prints count, and
  each is matched to the nearest new print of its foot; any left over is reported.
- A failed clip after the prints were replaced; two characters with one name sharing prints (refused now); a renamed
  path's message; a first print retimed past frame 0; prints retimed closer than a swing; a huge ground's grid.

**Checked**
- New tests (round 146):
  - a straight path: every foot on its print to 0.000 mm, planted feet still, first and last steps short;
  - a left turn: the inner steps 0.74 m against the outer's 0.81 m, no print on the wrong side;
  - box stairs: every print on a step, the hips rising 0.90 m;
  - in the editor: refused while off, a moved print kept by Re-plan and walked on, undone in one step (keys and every
    print as before), saved and loaded;
  - a print 0.8 m out reached; a turned print and a moved one kept through a faster style; twin names refused;
  - the lesson's Try-it.
- Unit checks: with round 147's (one chain for rounds 146-147; there).
- Stress: `editor`, `pushpull`, `modifiers_ngon`, `editor_render`: 0 problems, no change.

## 2026-10-18 (round 145): Walk Cycle, walking made from the rig (task 0125)

**Asked:** "Is there an automatic walk system in cascadeuer and other smart systems that would be ideal?" Cascadeur
has none (its locomotion is AI inbetweening and AI root motion). The user chose procedural gaits (as 3ds Max's
CATMotion) and footsteps along a path (as Biped's), in clips and live in Play, AI inbetweening as an optional
download, every one of them a switch, and hand animation unchanged.

**Added**
- **Walk Cycle** (the Rigging window's section and the Animation window's button, with the assistant on).
  - **Presets:** walk, jog, run, sneak and limp, or walk, trot and gallop for four legs.
  - **Numbers:** speed, stride, feet-down share, foot lift, heel-toe roll, bounce, sway, hip twist and drop, the spine's
    counter-turn, arm swing, elbows, lean and crouch.
  - **Generate** keys the rig's own controls (hips, the feet's IK targets and poles, feet, spine, head, arms):
    - feet planted while down, never sliding;
    - the stride from the Froude number, so a style fits any size;
    - a treadmill loop or travelling forward;
    - the planted frames written as contacts.
  - Presets are blended by weight and saved as `.gait` files. Console: `walk cycle | set | save | load | presets`.
- Learn: "Walk Cycle: walking made from the rig".

**Fixed on the way**
- **The loop's seam:** rounding a cycle to whole frames through the speed also changed the stride (the Froude number),
  so the period never landed on a frame. It adjusts the stride now.
- **A foot at a cycle's boundary:** float time left it "mid-swing" for a frame, so the pelvis wasn't lowered for it and
  IK fell 5 cm short. The phase is snapped at whole cycles, and every foot is kept within reach.

**Fixed after review**
- The hands kept still: the arms' IK pulled them back to rest. Their IK targets and poles are keyed now.
- The heel-toe roll turned the foot about the ankle, so the toes went through the floor at push-off. It rolls about the
  heel, then the toes, and just off the ground the ankle is raised until the toes clear (18 mm low before, 0 now).
- `walk set` reset every other number to the defaults; it changes only the one named.
- A rig facing -Z walked backwards: forward is read from the feet (ankle to toes), else the armature's turn.
- Shapes taken for legs too eagerly; a heel bone taken for the toes; IK controls found outside the character.
- Legs without IK bent their knees the wrong way: the knee bends the way it did at rest.
- Generate on a failed walk left an empty clip, cut a longer clip short and removed the hands' contacts. It works on a
  copy now, keeps the clip's length and replaces only the legs' contacts and the tracks it wrote.
- The preset list stuck on a loaded or blended style (it says "custom"); the Animation window's body strip and the
  Inspector's Body Mass showed with the physics tools off.

**Checked**
- New tests (round 145):
  - a planted foot's heel or toes (whichever is on the ground) move 0.000 mm; the treadmill's seam is 0.0002 mm;
  - the soles never below the floor; the hands swing 37 cm through their IK targets; a rig facing -Z walks -Z;
  - `walk set` keeps the other numbers; a walk too long to make leaves no clip;
  - a leg twice as long gives a cycle twice as long and sqrt(2) as slow;
  - a run is airborne, a walk never;
  - a trot moves diagonal pairs together;
  - presets, blending and `.gait` files;
  - the quadruped walks;
  - AutoPhysics moves a walk at most a few centimetres;
  - in the editor: refused while off, on as ordinary keys that edit by hand and undo in one step.
- Unit checks (rounds 144-145 in one chain): Windows 10,857, 0 failed (808 tests); the whole suite in render-cache
  verify mode: 10,858, 0 failed, 0 stale pictures. Linux with libraries: 10,593, 0 failed. Sanitizer build: 10,471,
  0 failed, no reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.
- Stress: `editor`, `pushpull`, `modifiers_ngon`, `editor_render`: 0 problems, no change.

## 2026-10-18 (round 144): animation assistants switched on, and the rig read for walking (task 0124, ADR 0045)

**Asked:** "Make sure that this is a toggle feature. Not all animation needs these feature." and "Still have the
ability to traditionally pose and animate models."

**Added**
- **Preferences > Animation Assistants:** a switch each for the Physics Tools (on, as they were), Walk Cycle,
  Footsteps, Locomotion and AI Inbetweening (off).
  - Off, a tool's buttons, sections, overlays and live work are gone, and its console command says how to turn it on.
  - Hand animation never changes.
  - Console: `assist list | on <name> | off <name>`.
- **`gait::analyse`:**
  - legs, arms, hips, spine and head, by names (Mixamo, Unity, Rigify, `create_rig`'s; `limb_of` is now public) and
    then by shape;
  - two legs or four, each with its IK target and pole;
  - the measures and the rest pose;
  - a reason when a rig can't walk.

**Checked**
- New tests (round 144):
  - the switches' first state, refusals and keeping through a restart;
  - the humanoid and quadruped rigs, Mixamo and Rigify names and an unnamed stick figure read right;
  - degenerate rigs refused with why.
- Unit checks: with round 145's (one chain for rounds 144-145; there). With every assistant off, the existing
  animation tests pass unchanged.
- Stress: `editor`, `pushpull`, `modifiers_ngon`, `editor_render`: 0 problems, no change.

## 2026-10-18 (round 143): MoonRay built for macOS too (task 0111)

**Asked:** round 135's "Do and finish all tasks" - the MoonRay archive for macOS, which CI hadn't built yet.

**Fixed**
- CI's macOS packaging stopped silently (`set -e`): `otool` failing on a scene object folder's non-library files, and a
  library's `@rpath` install name found nowhere. Both are passed over now, with progress printed.

**Checked**
- CI run 39130702928: MoonRay built on Apple silicon and packaged into a 240 MB archive (149 libraries). On a clean
  macOS 15 runner the archive unpacked and rendered its test scene in 8 s. With Linux's (round 136), both archives
  Blendity offers are built and checked; publishing them to a release is the user's call.
- No source changed (CI scripts and docs only): the suite stands as round 142 ran it.

## 2026-10-18 (round 142): physics for animation written up (task 0123)

**Asked:** the physics plan's last task, finishing "Finish and do all tasks please."

**Added**
- Learn: "Physics for animation: a jump from start to finish".
  - The whole set in Cascadeur's order: weigh, contacts, flight or AutoPhysics, then spring bones, the ragdoll and
    AutoPosing.
  - The ghost and its colours, shared by all of them.
  - A Try-it: a person jumping in straight lines, its contacts found and its flight shown corrected.
- CI's MoonRay build on macOS:
  - freetype2 on the include path for MoonRay's telemetry overlay;
  - the dependencies' cache found by prefix when the build script changes (it takes hours to rebuild).

**Checked**
- A new test (round 142) runs the Try-it as the Learn tab does: 4 contacts and one flight, moved up to 0.84 m (the
  keyed 1 m peak was too high for 0.67 s in the air).
- Unit checks (rounds 140-142 in one chain): Windows 10,832, 0 failed (798 tests); the whole suite in render-cache
  verify mode: 10,833, 0 failed, 0 stale pictures. Linux with libraries: 10,568, 0 failed. Sanitizer build: 10,447,
  0 failed, no reports (without Jolt: the ragdoll's tests say so and skip). `editor_fuzz` in verify mode: 6,000
  frames, no crash, 0 stale pictures.
- Stress: `editor`, `pushpull`, `modifiers_ngon`, `editor_render`: 0 problems, no change.

## 2026-10-18 (round 141): AutoPosing: the whole body posed by one end (task 0122)

**Asked:** the physics plan's AutoPosing (the user chose rule-based), as part of "Finish and do all tasks please."

**Added**
- **AutoPosing** (the Animation window's button, `autopose on`): drag a hand, a foot, the head or the hips with the move
  gizmo and the whole body follows.
  - **The solver:** a full-body inverse kinematics by damped least squares over every body bone and the hips' place.
  - **Feet:** the feet stay (the contacts at the playhead, bones you lock, or else the feet).
  - **Balance:** the center of mass stays over them.
  - **Natural poses:** each joint stays within 75 degrees of the pose it started in (the spine half of that) and is
    pulled back towards it.
  - **Speed:** under a millisecond for a humanoid, live while dragging.
  - **IK:** the targets move with the pose, so IK agrees with it.
  - **On release:** the moved bones are keyed at the playhead, in the drag's undo step.
- The console: `autopose on|off | drag <bone> <x> <y> <z> | lock <bone> | unlock <bone> | locks`.
- Learn: "AutoPosing: pose the whole body by one end".

**Fixed on the way**
- The Windows build's command line passed cmd's 8,191-character limit with the new files ("The input line is too
  long"): the sources now go to the compiler in a response file.
- A target far out of reach walked the body off its feet (a foot 2 m). Now:
  - each goal pulls at most 10 cm a step;
  - pins weigh ten times a goal;
  - the hips move at most 30 cm with feet pinned;
  - settle passes put a slipped pin back (29 cm before them, 0 after).

**Checked**
- New tests (round 141):
  - a hand in reach: 0.6 mm off, the feet held, joints within their limits, the head barely turned, 0.5 ms;
  - the head forward: the center of mass 3.4 cm from over the feet with balance, against 6.6 cm without;
  - 5 m out of reach: the arm points at the target (2 degrees), a foot within 12 mm, all finite;
  - the hips dragged 25 cm down: a crouch 5.6 mm off, the feet held and flat, the knees bending forward;
  - in the editor: a drag posed and keyed, one undo step, the IK targets and poles following (the elbow kept to
    0.0 mm through IK), a locked hand staying.
- An Opus review (the Jacobians, the solve and the limits checked: correct); every finding fixed:
  - dragging the hips pinned nothing (the feet were "below the dragged bone"), so the body slid;
  - IK poles weren't moved, so IK undid the knees' and elbows' bend;
  - a pinned foot kept its place but not its turn (toes through the floor);
  - the costs per drag frame (subtree sums by parents now, pins and IK chains worked out once a drag, the body kept
    between drags, settling stopping when done);
  - limits were measured from where each drag started, so they piled up, and knees could bend sideways (now from the
    rest, and a knee or elbow turns only about its bend axis);
  - a drag cut short left stale state;
  - every moved bone got Position keys, and IK-driven bones were keyed;
  - Play logged keys it didn't write;
  - balance counted locked hands as support;
  - the locks outlived the scene.
- Found while fixing:
  - balance fought a dragged goal (a crouch stopped 5 cm short). It now works only with joints that move no goal and
    no pin, leaning the torso instead.
  - A locked hand had switched off the feet's pinning.
- Unit checks: with round 142's (one chain for rounds 140-142; there).
- Stress: `editor`, `pushpull`, `modifiers_ngon`, `editor_render`: 0 problems, no change.

## 2026-10-18 (round 140): the active ragdoll: hits and falls that react (task 0121, ADR 0044)

**Asked:** the physics plan's active ragdoll (the user chose Jolt), as part of "Finish and do all tasks please."

**Added**
- **The active ragdoll** (with Blender's libraries: Jolt Physics):
  - the character as capsules, one per weighed bone, joined with humanoid limits;
  - joint motors drive it to the clip's pose with a strength; the hips are pulled to their place;
  - a floor under the clip's lowest point;
  - simulated on one thread, so the same clip always gives the same keys.
- **Events in the clip:** a hit (an impulse on a bone at a frame), limp (the motors off from a frame: a fall) and strong
  (on again). They are saved with the clip.
- The Animation window's **Ragdoll** button: simulated in the background, shown as a ghost, Apply one undo step. The
  console: `body ragdoll show [strength] | apply | off | hit | limp | strong | events | clear`.
- AutoPhysics and the ragdoll share one background worker, which hands back the finished result.
- Learn: "The active ragdoll: hits and falls".

**Fixed on the way**
- Blender's Jolt is built without RTTI, so a `dynamic_cast` on its constraints crashed. They are told apart by Jolt's
  own type.

**Checked**
- New tests (round 140), skipped without Jolt:
  - full strength follows the clip within 6.2 degrees, strength 0.2 within 64;
  - limp from a tipped pose, it falls from 1.17 m to the floor and rests;
  - a hit on a bone moves it most (0.32 m against 0.05 m);
  - a hit on the hips knocks the body back 0.7 m even at full strength;
  - an absurd impulse leaves finite keys;
  - two runs give identical keys;
  - events survive saving;
  - in the editor: a worker, the ghost, Apply and undo.
- An Opus review (no crash or lifetime fault; the threading checked); every finding fixed:
  - the hips' drive erased a hit (it now gives way for half a second after one);
  - the strength only capped the torque (it now sets the motors' stiffness too, and each joint's torque comes from the
    weight it carries);
  - the joints' limits were centred on the clip's first frame, not the rest pose;
  - bones touching in the first pose (thighs, shoulders and chest) could push apart;
  - a bone under a weightless helper was keyed in the wrong place;
  - the floor started inside the capsules;
  - the AutoPhysics button lit up for a ragdoll;
  - Jolt's layers were destroyed before the system, and a failed ragdoll left the floor in it;
  - early hits were lost, hits on bones that aren't joints said nothing, and huge ones could write non-finite keys;
  - a fallen body could sleep through "strong";
  - Apply's label, the replaced-key count, and a strength that isn't a number.
- Unit checks: with round 142's (one chain for rounds 140-142; there).
- Stress: `editor`, `pushpull`, `modifiers_ngon`, `editor_render`: 0 problems, no change.

## 2026-10-18 (round 139): spring bones for hair, ears, tails and cloth, found automatically (task 0120)

**Asked:** the physics plan's secondary motion, with "auto select hair bone, ear bone, cloth bone sequences
automatically. Or recursively if I select a parent bone that is the start of that specific chain" and "make sure that I
can also deselect or uncatogorize the chains if it's unneeded"; then "Finish and do all tasks please."

**Added**
- **SpringBone** on a chain's first bone: hair, ear, tail or cloth, each with its own stiffness, damping, gravity and
  radius.
  - The chain lags behind the body, swings past and settles, kept outside the body's bones.
  - It is the same at any frame rate: substeps, with rates per 1/60 s.
  - Live in Play, on the pose IK finished. **Bake into Clip** keys it frame by frame from the rest pose (the same keys
    when baked again, from the UI too), turns the live simulation off, and is one undo step.
- **Chains found automatically** (Rigging > Spring Bones > Find Spring Chains).
  - **By name:** hair, ponytail, ear, tail, skirt, cape and more, from Mixamo, VRM's `J_Sec_` and Japanese rigs'
    kami / mimi / shippo / sukato.
  - **By shape:** every branch of a fork is its own chain.
  - **Never offered:** limbs, fingers, toes, head, eyes or jaw.
  - Named chains come ticked and labelled; unnamed extras are listed unticked.
- **Your choice wins:**
  - untick a chain, change its label, or set it to **None**;
  - None and Remove are remembered on the chain's first bone ("Not a Spring Chain"), so Find doesn't offer it again;
  - Show Ignored and Unignore bring one back;
  - **Add Spring Bones** on a selected bone works recursively (a hair root's strands, a chain each);
  - **Remove** on a chain or a parent takes everything under it off, in one undo step.
- The console: `spring find [all] | apply | tick | label | add | remove | ignore | unignore | bake | list`.
- Learn: "Spring bones: hair, ears, tails and cloth".

**Checked**
- New tests (round 139):
  - 27 names across rig conventions read right;
  - Find on a Mixamo body picks exactly the 5 strands, 2 ears, the tail and 2 skirt strips, labelled, with an unnamed
    extra unticked;
  - a hair root makes five chains and leaves out a body bone under it;
  - at rest nothing moves; a stopping hip makes the tail swing past (1.8 cm) and settle;
  - collision keeps a tail out of a thigh (off: through it);
  - baking twice gives identical keys and puts the scene back;
  - in the editor: untick, None and Apply; ignored until Unignore; Remove on the hair root as one undo step, undone
    whole;
  - labels, settings and the ignored mark saved and loaded;
  - live in Play, and not when baked;
  - the same swing at 30, 60 and 144 frames a second (1.70, 1.70 and 1.74 cm past);
  - a 50 m teleport starts the chain afresh.
- An Opus review; every finding fixed:
  - the Find list held the bones themselves: Play and Stop, or an undo, left it pointing at freed objects (it keeps
    their ids now);
  - other chains' bones counted as the body: strands pushed each other apart (and the body's capsules were rebuilt
    per chain per step: now once per armature);
  - springs stepped before IK, a frame behind a limb IK moves;
  - Add again reset tuned chains and re-added chains set to None;
  - Add on a strand's middle bone split it;
  - None took off the chains below a fork too;
  - names looked up across characters;
  - `spring add <bone> none` added cloth;
  - a second bake found nothing;
  - Euler keys could flip near 90 degrees;
  - a zero-length bone's tip drifted;
  - a teleport whipped the chain;
  - ignoring a middle bone did nothing;
  - a frame's whole move kicked the chain at once (30 fps swung less than 60).
- Unit checks (rounds 138 and 139 in one chain): Windows 10,811, 0 failed (789 tests); the whole suite in render-cache
  verify mode: 10,812, 0 failed, 0 stale pictures. Linux with libraries: 10,547, 0 failed. Sanitizer build: 10,438,
  one failed - "spring bones in Play" measured 3.0 mm of lag against its 3 mm floor, the sanitizer's slow frames
  hitting Play's 0.05 s step cap. Its hips now move 5 cm a frame (the lag clear of the floor at any frame time); no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.
- Stress: `editor`, `pushpull`, `modifiers_ngon`, `editor_render`: 0 problems, no change.

## 2026-10-18 (round 138): AutoPhysics: the whole clip made physically possible (tasks 0118, 0119, ADR 0043)

**Asked:** "Finish and do all tasks please" (the physics plan's next tasks: the AutoPhysics spike, then AutoPhysics).

**Added**
- **The spike (task 0118, ADR 0043): GO.**
  - The body is reduced to its center of mass and its spin, pushed only by gravity and the contacts' forces
    (centroidal dynamics).
  - One trajectory optimisation over the clip finds the nearest motion with Newton's and Euler's laws holding at
    every frame and each force pushing inside its friction pyramid.
  - Measured on four clips of 100 frames (a jump, a run, a flip, a fall onto the hands): 221-493 ms each, the laws
    held to 7e-6 to 1.3e-4 of the body's weight, flights falling at exactly g, and a flip's spin constant in the air.
- **Blendity's own QP solver** (`core/qp`): ADMM as OSQP does it, with a banded Cholesky factorisation (a trajectory
  couples only frames a few apart), warm starts, an adaptive step and cancel. No new dependency.
- **AutoPhysics (task 0119):** the Animation window's AutoPhysics button.
  - Sampled on the editor's thread, solved on a worker ("Solving..."), shown with Flight's ghost, frame colours and
    Apply (one undo step).
  - The hips carry the correction; the feet's and hands' targets are held through their contacts and follow the body
    again over 4 frames after.
  - A clip edited while it solves isn't shown. The console: `body physics show|apply|off`.
- Learn: "AutoPhysics: the whole clip", with questions.

**Checked**
- New tests (round 138):
  - the QP's known answers, an equality, bounds, a warm start, a 2,000-unknown chain in its band, an empty constraint
    refused;
  - the four clips' times, residuals and moves;
  - standing coming back as it was, and bad input refused;
  - a jump keyed in straight lines made a parabola, with the foot held and the keyed clip realising the solved
    center;
  - a possible clip all green; missing contacts or contacts on bones that aren't IK ends explained; a cancelled
    solve;
  - in the editor: the worker, the ghost, Apply and undo, an edited clip not shown, Off.
- An Opus review (the math, the solver and the worker's lifetimes checked: correct); every finding fixed:
  - a finished solve waited for the mouse to move (the editor slept);
  - the turn left at the end was taken back in the air too, where nothing could do it;
  - Euler's central difference let the momentum zig-zag between even and odd frames;
  - results were judged by the solver's status, not the laws (a long clip thrown away, an unsettled one kept);
  - no limit on a clip's length, no cancel between passes, non-finite lengths not refused;
  - a result could arrive in Play, show on another scene with a clip where the old one was, or replace Flight's;
  - a contact pushed at its first frame's tip, not where the clip has it.
- Unit checks: with round 139's (one chain for both; there). CI's sanitizer runner took 13 s on the spike's four clips
  (the guard was 5 s): their time is checked in optimized builds only.
- Stress: `editor`, `pushpull`, `modifiers_ngon`, `editor_render`: 0 problems, no change.

## 2026-10-18 (round 137): physics for animation: the body's masses, contacts and flight (tasks 0115-0117, ADR 0042)

**Asked:** "Is there a way to implement body simulation for animation like in Cascadeur." The user chose the center of
mass with ballistics, the full AutoPhysics solve, secondary motion, an active ragdoll (Jolt) and rule-based
AutoPosing; then that hair, ear and cloth chains be found automatically (or from a parent bone) and can be unticked or
relabelled; then "Finish and do all tasks please." This round is the first three tasks of that plan.

**Added**
- **The body's masses (task 0115, ADR 0042):** each deforming bone weighs its share of the skinned mesh's volume
  (tetrahedra against the bone's own middle, shared by the corners' weights), with its center and inertia; bones
  without a skin get capsules; all scaled to the armature's **Body Mass** (75 kg, in the Inspector). The center of
  mass and the whole body's inertia follow any pose, the rig root's motion included.
- **The center of mass in a clip:** the Animation window's **Center of Mass** button draws its path over the clip,
  where it is at the playhead and the point under it. The body is built once per rig and skin, not every frame.
- **Contacts (task 0116):** an IK chain's end within 5 cm of the ground and slower than 0.5 m/s for 2 frames or more
  is a contact, shown as bars under the ruler (set by hand brighter, and kept when contacts are found again). Pin
  keys the IK target still through each one, its parent's motion frame by frame taken into account. Contacts are
  saved in the clip.
- **Flight (task 0117):** between a take-off and a landing the center of mass is put on the parabola through them at
  their times (g = 9.81 m/s^2), and the body turned about it so the angular momentum stays the take-off's (solved in
  the turned axes, refined until it holds); the landing pose kept by default. **Flight** shows the corrected body as a
  ghost, its frames green (fine), blue (changed) or red (impossible: nothing written); **Apply** keys it into the
  clip as one undo step, only into the clip it was worked out for.
- The console: `body show on|off`, `body mass [kg]`, `body com [frame]`, `body contacts find|pin|clear|list|add|remove`,
  `body flight show|apply|off`.
- Learn: "Physics for animation: the center of mass" and "Contacts and flight", each with Try it and questions.

**Checked**
- New tests (round 137): two skinned boxes weigh 25 and 50 of 75 kg with their centers and inertias; the center of
  mass and inertia follow a pose (parallel axes), capsules without a skin; a clip's trajectory follows the root and
  leaves the scene as it was; an IK leg's rests found within a frame, a manual contact kept, pinning holding the
  target, contacts saved and loaded; a straight jump made an exact parabola, a tuck's angular momentum kept within 3%
  (0.5% on a double spin), the landing pose kept, an impossible jump marked red and not written; in the editor Body
  Mass, contacts, the ghost and Apply as one undo step.
- An Opus review; every finding fixed:
  - the overlay rebuilt the body every frame;
  - IK targets were keyed against a fixed parent with a moving root;
  - Apply could write the clip of another character selected since;
  - contacts outside the clip weren't clamped, and replaced keys weren't counted;
  - the hips were guessed from the first bone (now the root, else the heaviest top bone);
  - a turn could flip Euler angles (the other solution is now taken when nearer);
  - the momentum correction worked in the wrong axes for a large turn;
  - the body's cache ignored the bones; Apply ran while playing; animated values other than transforms weren't put
    back; `body com` read the wrong frame; manual contacts didn't trim found ones.
- Unit checks: Windows 10,753, 0 failed (773 tests); the whole suite in render-cache verify mode: 10,754,
  0 failed, 0 stale pictures. Linux with libraries: 10,489, 0 failed. Sanitizer build: 10,380, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.
- Stress: `editor`, `pushpull`, `modifiers_ngon`, `editor_render`: 0 problems, no change.

## 2026-10-18 (round 136): MoonRay checked against the real renderer (task 0113)

**Asked:** "Do and finish all tasks" (round 135's), with CI's Linux MoonRay archive downloaded into WSL to check the
export against the real thing ("Yes, download it into WSL").

**Fixed** (each measured in MoonRay v2026.29.1 against F12's path tracer on the same scene)
- **Textured materials rendered black:** MoonRay's texture system takes only tiled, mip-mapped files (as `maketx`
  makes them), so every PNG failed. Textures now go as tiled float EXRs with every mip level (`write_texture_exr`,
  dependency-free, area-weighted levels, wrapping), linear (an 8-bit colour map's sRGB undone first), made on the
  render's thread and kept while the source is unchanged.
- **The world map was turned:** its directions were measured with a map whose pixels are their own coordinates; the
  world, its sky and its azimuth now match to 0.1%.
- **A sun was twice as bright:** a DistantLight's normalized intensity is Blendity's intensity as it is (both give
  pi x I of irradiance); it matches to 1%.
- The world didn't show behind the scene (MoonRay hides lights from the camera by default), and an empty scene
  rendered black (MoonRay needs geometry in its layer: a speck far below is added).
- MoonRay's own error lines are in the log.
- CI's MoonRay build: Python 3.11 (Boost.Python failed on 3.13), autotools on macOS, the dependencies' cache saved
  even when a later step fails, the check containers allowed `mbind`.

**Checked**
- New tests (round 136): the texture EXR's tiles where its table says on every level, down to one pixel of the
  image's exact mean (odd sizes too), made once while its source is unchanged, an unreadable one white; with `BLENDITY_REAL_MOONRAY=1` and MoonRay installed, five scenes rendered in
  both: the world 1.000, a sky's azimuth 1.000, a sun 0.991, a point light at 1 m 0.891 (MoonRay's falloff is
  physical), a four-colour texture the same colour in each quadrant.
- An Opus review (the EXR format checked field by field: no fault); every finding fixed:
  - textures were converted on the editor's thread, and again on every frame of an animation: they are now made on
    the render's own thread before MoonRay starts, kept while their source is unchanged, and written without copies;
  - a texture used by two materials was converted twice;
  - MoonRay's last error line was lost without its newline, and its errors were logged again on every frame.
- Unit checks: Windows 10,706 (763 tests); this round's tree alone, Linux without Blender's libraries: 10,334,
  0 failed. Run together with round 137 (the full chain): Windows 10,753, render-cache verify mode 10,754 (0 stale
  pictures), Linux with libraries 10,489, the sanitizer build 10,380 (no reports), all 0 failed; `editor_fuzz` in
  verify mode 6,000 frames, no crash, 0 stale pictures.

## 2026-10-18 (round 135): MoonRay as an optional external renderer (tasks 0111-0114, ADR 0041)

**Asked:** "Is it possible to integrate MoonRay from DreamWorks into this project?", then "Let's make this an external
renderer. Just like other dependencies making it an optional install when using Blendity for the first time." The
user chose to build and host it and to run it through WSL2 on Windows, then "Do and finish all tasks."

**Added**
- **MoonRay built by CI (task 0111):** `.github/workflows/moonray.yml` and `tools/moonray/` build MoonRay v2026.29.1
  (Linux x86-64 in Rocky Linux 9, macOS on Apple silicon) into `blendity-moonray-<platform>-<tag>.tar.gz` with a
  `.sha256`: the renderer, every library it loads but the system's, its scene objects, a launcher, a test scene and
  every licence. Each archive is checked on clean Ubuntu 22.04 / 24.04, Debian 12, Fedora 41 and macOS 15. It is
  run by hand or by a push to `moonray-build/*`; publishing it is the user's call.
- **The optional install (task 0112):**
  - The first run asks "Download it?" (Download / Not now / Never ask), as Blender's libraries are offered: online
    only, never headless, again at most once a day after Not now.
  - MoonRay is looked for only then (on Windows that starts WSL).
  - Preferences > Renderers installs or removes it; the console's `moonray` (status, install [archive], remove,
    folder for your own build, check) does the same.
  - On Windows it runs inside WSL2, and says how to get WSL when it's missing.
- **The scene in MoonRay's RDLA (task 0113):** what F12's path tracer is given:
  - meshes mirrored into MoonRay's right-handed space (each triangle still facing out);
  - Principled materials as DwaBaseMaterial, with image, normal and emission maps;
  - the four light kinds, matched in brightness to the path tracer at 1 m;
  - the world baked to an environment map;
  - the camera with depth of field.

  What MoonRay doesn't take (toon, camera filters, procedural textures, alpha clipping, the lights' Range curve) is
  named in the log once.
- **MoonRay in F12 (task 0114):** Render Engine > MoonRay renders F12 images, camera sequences and animations.
  - It runs on its own thread, its progress in the Render window, and Stop ends it.
  - Its image is read back through Blendity's view transform and exposure: a new uncompressed-EXR reader works
    without Blender's libraries.
  - Without MoonRay, or with an orthographic camera, F12 path traces and says why.
- `sys::run_streaming` / `run_capture`: a program's output as it comes, with cancel and timeout. On POSIX the pipe is
  close-on-exec and the child's end is watched; on Windows only the child inherits it.
- A Learn section: "MoonRay, an optional third engine".

**Checked**
- New tests (round 135), with `blendity_tests --fake-moonray` standing in for MoonRay so CI needs neither MoonRay
  nor WSL:
  - WSL's distribution list in UTF-16 and UTF-8, Windows paths in WSL, MoonRay's progress parsed;
  - a program's output streamed, its exit code, a cancel within a moment, a child holding the pipe open;
  - uncompressed EXR in float and half, a compressed one refused by name, a short file claiming a huge image refused
    before anything is allocated, PFM's row order;
  - the export's objects, values, balance and winding, the right-handed conversion, the notes, a 50k-face mesh in
    about 0.2 s;
  - F12 with the stand-in: progress, the image through the view transform, a transparent film's alpha, a failure's
    message, Stop, a new render replacing it, the path tracer without MoonRay or with an orthographic camera;
  - an archive with the wrong checksum refused, a good one unpacked, probed and removed (Linux and macOS).
- An Opus review; every finding fixed:
  - a pipe a later program inherited could keep a render open;
  - a new render didn't replace a MoonRay one;
  - Stop during the search started it anyway;
  - the console's and Preferences' MoonRay chores ran on the editor's thread;
  - a folder change during a search was lost;
  - WSL was started at every launch once installed;
  - the first-run question could be covered by an update;
  - a short EXR could ask for gigabytes;
  - the packaging check could pass with missing libraries;
  - orthographic cameras;
  - a stale MoonRay in the macOS cache;
  - shared scratch folders.
- Unit checks: Windows 10,692, 0 failed (761 tests); the whole suite in render-cache verify mode: 10,693,
  0 failed, 0 stale pictures. Linux with libraries: 10,428, 0 failed. Sanitizer build: 10,319, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.
- Stress: `editor`, `pushpull` (26,620 operations, 0 problems), `modifiers_ngon` (11,514 operations, 0 problems),
  `editor_render`: no change in problem counts.

## 2026-10-16 (round 134): performance pass 12 written up (task 0110)

**Asked:** pass 12 continued (the approved plan's last task).

**Changed** (documentation only)
- PERFORMANCE's pass 12: the targets met (no overlap-check stall; entering Edit Mode under 30 ms with the views on the
  GPU, about one frame on the CPU), what it cost, Blender's mechanisms adopted and left, and the next candidates (the
  operators' own cost, selection without drawing the view again, picking Edit Mode elements on the GPU).
- README's modeling paragraph mentions entering Edit Mode in about a frame.

**Checked**
- No code changed: the suite, verify mode and the stress sections stand as round 133 ran them.

## 2026-10-16 (round 133): entering Edit Mode in about one frame: the render mesh built on every thread (task 0109)

**Asked:** pass 12 continued (the plan the user approved: entering Edit Mode in about one frame).

**Changed**
- **The full build of a mesh's render cache runs on every thread,** the same to the bit as the serial build (kept for
  the tests): corners grouped by vertex, each sharing with the first earlier corner of the same normal and UV bits;
  render vertices numbered by a prefix sum in corner order; triangles per range of faces. A vertex with many corners
  (a pole, a fan's centre) is sorted rather than compared pairwise. The 50k-face flat cache: 42 -> 6.6 ms
  (development machine), 33 -> 5.4 ms (laptop setting). Blender threads its tessellation and draw extraction the same
  way.
- The sharing rule is exact in both builds: the same bits, never a NaN (the serial build's `==` let `/fp:fast` and
  its hash's buckets decide NaN and -0 / +0).
- A mesh with a corner past its vertices draws nothing instead of building from memory past them.
- **A full overlap check predicted past about a frame goes to the worker** (the rate learned from this machine's
  checks), not only past 200,000 vertices: entering Edit Mode on a 50k-face mesh does no check on the main thread,
  the rings arrive a moment later. The learned rate is held so a few stalled checks can't send every later one there.

**Pass 12** (laptop setting, `--laptop --quality low --only editor_render`, ms):

| Laptop setting, 50k-face sphere, ms | CPU before | 0108 | 0109 | GPU before | 0108 | 0109 |
|---|---|---|---|---|---|---|
| Entering Edit Mode, caches cold | 421 | 80 | 39 | 360 | 70 | 20 |
| of it the overlap check on the main thread | 360 | 24 | 0.5 | 308 | 23 | 0.6 |
| of it the render mesh's full build | 37 | 33 | 5.4 | 33 | 32 | 5.7 |
| Entering again, nothing changed | 24 | 21 | 32 | 11 | 5 | 6 |
| Extrude three faces | 466 | 179 | 118 | 487 | 163 | 115 |
| Undo after it | 337 | 27 | 38 | 334 | 23 | 30 |
| Select More after the undo | 208 | 56 | 68 | 213 | 55 | 62 |

Entering Edit Mode now costs about one frame: with the views on the CPU, entering again with nothing changed is
32 ms. The extrude's and Select More's rest is the operators themselves (left for later).

**Checked**
- New tests (round 133): the threaded build equals the serial one bit for bit on 10 meshes (smooth, by angle, sharp
  edges and a seam, two materials, flat, degenerate with a NaN, a cylinder, a 3,000-segment cone's pole, no UVs,
  empty), both caches; a drag's in-place update and the tangents after it equal the serial build's; the 50k-face flat
  cache builds faster threaded (asserted with more than one thread); entering Edit Mode on a 50k-face mesh checks on
  the worker, and with no budget on the main thread, the same rings either way; stalled checks don't send a
  5,000-vertex mesh's check to the worker.
- Two Opus reviews; every finding fixed (see task 0109's spec).
- Unit checks: Windows 10,601, 0 failed (751 tests); the whole suite in render-cache verify mode: 10,602,
  0 failed, 0 stale pictures. Linux with libraries: 10,334, 0 failed. Sanitizer build: 10,225, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.
- Stress: `editor`, `pushpull` (26,620 operations, 0 problems), `modifiers_ngon` (11,514 operations, 0 problems),
  `editor_render`: no change in problem counts.

## 2026-10-16 (round 132): Edit Mode's overlap check reused, fast and local; pass 12 measured (tasks 0107, 0108, ADR 0040)

**Asked:** "Look for performance gains in edit mode; compare to how blender implements their edit mode." The user chose
entering Edit Mode as the scope, "Background + local" for the overlap check, and then asked for "the most optimized
way".

**Measured first (task 0107):** new `editor_render` rows for the frames that aren't drags: entering Edit Mode with the
mesh's caches cold, entering again, an extrude, an undo, a selection after it. On the 50k-face sphere the overlap
check (the yellow rings and magenta lines) was most of every stall: 310-360 ms on the main thread.

**Changed (task 0108, ADR 0040)**
- **Answers kept by what was checked:** the topology, eps and every position. An undo or redo to checked geometry,
  re-entering Edit Mode and a selection that copied the mesh take the answer at once (the last four are kept).
- **A check about 15x faster** (21-24 ms on 50k faces, from 315-575 ms), on every thread, finding exactly the pairs the
  old check finds (kept as the reference the tests compare with).
- **After a drag only what moved is checked again:** the moved vertices and the edges at them.
- **Past 200,000 vertices the check runs beside the editor** ("Checking..." in the panel, no rings until the answer
  for the mesh as it is now). The old cap, no check at all past 200,000 vertices, is gone.
- Mesh topology serials come from one counter for all meshes, so two copies changed apart never share one.
- A vertex at infinity no longer makes the check's distance infinite (every vertex was a pair, and Merge Overlapping
  then refused to weld anything); grid cells are held in range for meshes far from the origin; a line to a point at
  infinity is no longer drawn from NaN rows (found by the sanitizer job).

**Pass 12 so far** (laptop setting, `--laptop --quality low --only editor_render`, quiet machine):

| Laptop setting, 50k-face sphere, ms | CPU before | CPU after | GPU before | GPU after |
|---|---|---|---|---|
| Entering Edit Mode, caches cold (of it the overlap check) | 421 (360) | 80 (24) | 360 (308) | 70 (23) |
| Entering again, nothing changed | 24 | 21 | 11 | 5 |
| Extrude three faces (the overlap check) | 466 (316) | 179 (27) | 487 (336) | 163 (26) |
| Undo after it (the overlap check) | 337 (311) | 27 (0.4) | 334 (311) | 23 (0.4) |
| Select More after the undo (the overlap check) | 208 (153) | 56 (0.2) | 213 (155) | 55 (0.2) |

The rest of the extrude is its render meshes (two full builds, 54 ms: task 0109 threads them) and the operator itself.

**Checked**
- New tests (round 132): the fast check equals the reference on a cube, planted doubles, a 50k-face sphere, an empty
  mesh, one vertex, wires only, infinities, a mesh far out, random soups and wires near the 512-cell limit (at least
  3x faster on 50k faces in optimized builds with 8 threads or more); topology serials of diverging copies differ; the
  local check equals a full check through random drags and refuses or equals it near the 512-cell limit; in the
  editor a drag's release is local, a selection that copied the mesh and an undo reuse the answer; on a worker the
  editor doesn't wait, every frame shows nothing or the current geometry's pairs, a mesh changed meanwhile gets its own
  answer and an undo to the superseded geometry reuses its; Merge welds only the found doubles beside a vertex at
  infinity.
- Two Opus reviews; every finding fixed (see task 0108's spec).
- Unit checks: Windows 10,591, 0 failed (746 tests); the whole suite in render-cache verify mode: 10,592, 0 failed,
  0 stale pictures. Linux with libraries: 10,324, 0 failed. Sanitizer build: 10,216 checks, its one failure the
  speed ratio (2.7x in a debug sanitizer build), now asserted only in optimized builds; the overlap tests again under
  the sanitizers: 85 checks, 0 failed, no reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale
  pictures.
- Stress: `editor`, `pushpull` (26,620 operations, 0 problems), `modifiers_ngon` (11,514 operations, 0 problems),
  `editor_render`: no change in problem counts.

## 2026-10-16 (round 131): performance pass 11 written up (task 0106)

**Asked:** pass 11 continued ("Continue all remaining tasks").

**Changed** (documentation only)
- PERFORMANCE's pass 11 opens with one table, before and after, on both paths; the targets met and missed, the
  costs, what still draws on the CPU, and the next candidates.
- ADR 0026 notes that the laptop setting's 30 fps target for big Edit Mode drags is now met on the CPU path too.
- README's rendering paragraph mentions the views drawn on the GPU.

**Checked**
- No code changed: the suite, verify mode and the stress sections stand as round 130 ran them.

## 2026-10-16 (round 130): GPU Compute traces several samples a dispatch; OptiX measured again (task 0105, ADR 0037 note)

**Asked:** pass 11 continued ("Continue all remaining tasks").

**Changed**
- **The GPU path tracer's kernel traces up to 8 samples a dispatch,** each with its own random numbers as before,
  summed in registers. The accumulation and the denoiser's albedo, normal and depth planes (and a transparent film's
  coverage) are read and written once for them, instead of once a sample. Baking too.
- **GPU Compute, `--only gpu_optix`, samples a second:**
  - RTX 4070 SUPER 538-574 -> 592-611;
  - RTX 3060 Ti 279-281 -> 284-286.

  A small gain: the memory traffic was a smaller share of a sample than estimated.
- **OptiX measured again:** 1.62-1.69x on the 4070 SUPER, 2.07-2.12x on the 3060 Ti. Still over ADR 0037's 1.5x, so
  still a go as an upper bound. A full OptiX device (with GPU Compute's shading) is the user's decision, as a task of
  its own.
- The kernel's last source part is split before `main()` (MSVC's 16 KB literal limit was near).

**Checked**
- New test (round 130): eleven samples in one call (a dispatch of 8, then 3, from sample 3) equal eleven one-sample
  calls, to 2e-7: colour, the last hit's distance and id, albedo, normal, depth and coverage.
- The GPU path tracer's tests as they were (225 GPU checks).
- Unit checks: Windows 10,555, 0 failed (738 tests); the whole suite in render-cache verify mode: 10,556,
  0 failed, 0 stale pictures. Linux with libraries: 10,288, 0 failed. Sanitizer build: 10,180, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.
- Stress: `pushpull` 26,620 operations, 0 problems, 394 overlaps (unchanged); `modifiers_ngon` 0 problems; `pathtracer` as
  before.

**Review** (Opus code-reviewer). No correctness bugs: the accumulation's and the planes' meanings are kept, each
sample keeps its own seed, the pipeline cache is keyed on the kernel's source, and a submission stays near 100 ms.
Taken: the source split, the test widened to a dispatch's remainder, a non-zero first sample and the coverage plane.

## 2026-10-16 (round 128): the other views catch up on release, and cheaper frames while moving (tasks 0103, 0104, ADR 0039)

**Asked:** pass 11 continued ("Continue all remaining tasks"). In the Blender-ideas plan the user chose "Catch up on
release" and "Yes, cheaper while moving".

**Changed**
- **The other views catch up on release (task 0103):**
  - while you drag or navigate in the Scene view, the Game view and the Camera Preview keep their last picture and
    draw once when you let go, as Blender's sculpt mode updates its other views when the stroke ends;
  - Play always draws every frame;
  - Preferences > Other Views Follow Drags (off) brings the old way back; the console's `views follow on|off`.
- **Cheaper frames while moving (task 0104, ADR 0039):**
  - while you drag or orbit, the views drawn on the CPU render the sun's shadow map at half size with the 2 x 2
    filter, as Blender lowers its viewport's quality while you navigate;
  - the frame after you stop is exact: the views' picture keys carry the cheap state, so a cheap picture is never
    reused;
  - Preferences > Simpler While Moving (on); the console's `views simpler on|off`; the GPU's own map stays whole.
- **What counts as moving:** only a drag or navigation in the Scene view itself (a gizmo, an orbit, G / R / S,
  Push/Pull, a modal tool), not any mouse button held anywhere. The frame after a move always draws.
- **Speed:** the 50k-face Edit Mode drag on the CPU path, laptop setting: 28.3 -> 21.5 ms (the Game view 8.6 -> 0.2
  ms, the shadow map 3.5 -> 2.3 ms).
- A Learn section, "Drags that stay quick", with Try its for both switches.
- The console's `views` command shares the `viewport` branch: MSVC's limit on nested blocks (C1061) was reached.

**Checked**
- **New tests (round 128):**
  - during a 10-frame gizmo drag the Game view and the Camera Preview hold 20 pictures between them and render
    nothing; on release both draw again, and both equal a fresh render; with Follow on they draw every frame;
  - a gizmo drag in Play holds nothing;
  - a click in the Hierarchy holds nothing, makes no cheap shadow and draws no view.
- **New tests (round 129):**
  - with Simpler While Moving, 6 drag frames take 6 half-size shadow maps and the settled frame is the exact picture,
    pixel for pixel; off, none;
  - verify mode finds no stale picture across such a drag.
- Round 92's shadow-sharing test turns Follow on (it needs the Game view drawing during the drag).
- Unit checks: Windows 10,538, 0 failed (737 tests); the whole suite in render-cache verify mode: 10,539,
  0 failed, 0 stale pictures. Linux with libraries: 10,288, 0 failed. Sanitizer build: 10,180, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.
- Stress: `pushpull` 26,620 operations, 0 problems, 394 overlaps (unchanged); `modifiers_ngon` 0 problems.

**Review** (Opus code-reviewer). Fixed:
- any mouse button down anywhere counted as moving, so a click in the Hierarchy redrew the Scene view twice and
  softened its shadows; now only a drag in the Scene view counts;
- a held Camera Preview could show another camera's picture: it is held only for the same camera;
- the frame after a move always draws;
- the counter counts only maps that were actually halved;
- the Play test now drags a gizmo, and the Camera Preview's pixels are compared after release;
- Try its for Simpler While Moving.

## 2026-10-16 (round 127): Sky / HDRI worlds and toon on the GPU (task 0102)

**Asked:** pass 11 continued ("Continue all remaining tasks").

**Changed**
- **Sky and HDRI worlds draw on the card.**
  - The world's map goes into the card's texture store (ADR 0038's sampling, mips and all).
  - Its irradiance is the CPU's nine spherical-harmonic numbers.
  - The lighting pass has the CPU's `radiance`, `irradiance` and `specular`, through `dir_to_equirect`'s polynomial
    atan2 and asin. The background is the world's light through the view transform.
- **Toon materials and outlines draw on the card.**
  - `shade_toon` per light (the bands, the shadow colour, the specular spot), the rim, and the flat or normal
    ambient, as `light_surface` and `light_finish` do.
  - The G-buffer gains a plane with each pixel's instance, so the lighting pass reads its toon settings.
  - An outline's inverted hull is always culled, whatever the view's setting.
- A Sky's map is made anew when its sun changes; the card lets the old one go at once (rotating the sun would have
  held dozens of float maps there).
- **Speed:** the textured editor scene with a Sky world on the laptop setting: 22.1 ms with every view fallen back to
  the CPU, 9.9 ms with all 33 on the card.
- **Still on the CPU (Preferences says why):** realtime GI, probes and lightmaps, area lights, axis and see-through
  materials, camera filters.
- The Learn lesson "The views on the GPU" and the Viewport Device tooltip now say what the card draws; a Try it shows
  a Sky world on the GPU.

**Checked**
- **New tests (round 127)**, each skipped without a GPU, compared on the object's own pixels and on the whole view:
  - **Worlds:** a Sky and an HDRI world, under a rough and a glossy metal sphere. Mean 0.18-0.27 / 255 on the
    object, 0.13-0.14 on the view, 99.97 % or more within 3. The world's map is checked to reach the card.
  - **Toon:** two-tone; three bands with a specular spot and a rim; flat ambient off under a Sky; in a slab's shadow;
    an outline with the view's culling on and off, and mirrored. Mean 0.04-0.28 / 255.
- Unit checks: Windows 10,509, 0 failed (732 tests); the whole suite in render-cache verify mode: 10,510,
  0 failed, 0 stale pictures. Linux with libraries: 10,259, 0 failed. Sanitizer build: 10,151, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.
- Stress: `pushpull` 26,620 operations, 0 problems, 394 overlaps (unchanged); `modifiers_ngon` 0 problems; `editor_render`'s textured Sky row draws all its views on
  the card.

**Review** (Opus code-reviewer). The shading ports match the CPU line for line. Fixed:
- the Sky's old maps left the card only after 256 calls;
- the two fallbacks share one helper;
- the toon settings are cleaned on the CPU (GLSL's clamp of a NaN is undefined);
- tests for an outline with culling off and mirrored, and for the toon shadow;
- the lesson gained a question and a Try it for the worlds.

## 2026-10-16 (round 126): textures on the GPU (task 0101, ADR 0038)

**Asked:** pass 11 continued ("Continue all remaining tasks").

**Changed**
- **Textured materials draw on the card.** With Viewport Device on the GPU, these no longer send the view back to the
  CPU:
  - image textures on UVs, Box or Generated mapping, with tiling and offset;
  - base colour, metallic, roughness, emission and normal maps;
  - the Checker and Noise procedurals;
  - several materials on one mesh.
- **Sampled the CPU's way (ADR 0038).** The card reads the CPU's own mip chain from storage buffers, ports
  `Texture::sample` (filters, wraps, the level of detail from the next pixel's UVs) and `evaluate_surface`, and turns
  a back face's normals before the normal map, as the CPU does.
- **Several materials on one mesh:** its triangles are grouped by material on the card and drawn as one part each.
- **Textures stay on the card while drawn,** in two arenas that grow and are compacted on the card. A texture too
  large for the card sends the view to the CPU with the reason, once, not every frame.
- **Still on the CPU** (Preferences says why): axis materials, live procedural textures, see-through, cutout and toon
  materials, and Sky / HDRI worlds (toon and the worlds are task 0102).
- **Speed:** the editor scene with a Checker on its icospheres, laptop setting: 18.0 ms on the CPU, 10.4 ms with all
  its views on the card (before, all of them fell back).
- **Costs:**
  - every vertex on the card is 48 bytes now (UVs and tangents);
  - a mesh with several materials sends the whole mesh during an Edit Mode drag;
  - a normal-mapped mesh does too, since every tangent is written again.
- The procedurals' GLSL is one string shared by the path tracer's kernel and the views (`gpu_glsl.h`).

**Checked**
- **New tests (round 126)**, each skipped without a GPU:
  - **Picture match:** 11 features match the CPU on the object's own pixels: mean 0.15-0.69 / 255, 99.1-100 % within
    3. The features: UV Grid, a plane at a grazing angle (mips), Box, Generated, Checker, Noise, a normal map (which
    does change the picture: 64,264 pixels), metallic / roughness / emission maps, Closest + Mirror, Linear + Clip,
    and two materials on one mesh.
  - **Still on the CPU:** an axis material, a cutout and a live procedural texture send the view there, saying why.
  - **Texture store:** a 1 x 1 texture, fifty more, most let go, then a big one and a float one: compaction and
    growth keep the right texels.
  - **Drag:** an Edit Mode drag on a big textured mesh still sends only what moved.
- Round 122's fallback test now uses an axis material (procedurals draw on the card).
- Unit checks: Windows 10,461, 0 failed (730 tests); the whole suite in render-cache verify mode: 10,462,
  0 failed, 0 stale pictures. Linux with libraries: 10,259, 0 failed. Sanitizer build: 10,151, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.
- Stress: `pushpull` 26,620 operations, 0 problems, 394 overlaps (unchanged); `modifiers_ngon` 0 problems; `pathtracer` (its
  kernel now takes the shared procedurals) runs as before.

**Review** (Opus code-reviewer). The sampling and the material match the CPU line for line. Fixed:
- a texture of 4 texels or fewer fit the arena's placeholder, which couldn't be copied from when the arena grew;
- a texture too large for the card was copied and tried again every frame: the size is checked first, the refusal
  is remembered, and the levels go straight into the staging buffer;
- culling is per object, as the CPU's (not per material);
- a compaction that fails changes nothing;
- a part past its mesh fails the frame instead of drawing the whole mesh;
- the slots of a one-material mesh are found without a list;
- the shared procedurals;
- tests for the texture store and the live texture.

## 2026-10-16 (round 125): the Edit Mode cage on the GPU, and the Scene view at full size there (task 0100, ADR 0036 note)

**Asked:** pass 11 continued ("What should be more optimization gains?").

**Changed**
- **The cage on the card:** with the views on the GPU, the card draws the Edit Mode cage's edges and vertex dots
  into the Scene view's picture before it comes back. Its shaders follow the CPU's own rules: the line's clip and
  two-pixel coverage, the dot's centre depth test and disc, the 8-bit blend. The cage's vertices and edge colours stay
  on the card and only what changed goes up. The CPU then draws only the rest of the overlays.
- **The Scene view on the card draws at full size**, whatever the Viewport Resolution Scale (the scale saves the
  CPU's per-pixel work). The Game view and the Camera Preview keep it.
- Small uploads to the card (the cage's ranges, task 0099's mesh ranges) go in front of the frame's own submission
  instead of waiting for the card each.
- The 50k-face Edit Mode drag on the laptop setting, views on the GPU: 14.3 -> 11.5-12.3 ms (the CPU's overlays
  4.3 -> 0.5 ms). Playing costs about 1 ms more (the Scene view's bigger read-back).
- `Mesh::topology_serial()` is public: the GPU cage keys its edge colours on it.

**Checked**
- New tests (round 125), each skipped without a GPU:
  - the cage drawn by the card in vertex, edge and N-gon modes (selected and plain vertices, a seam, hidden edges)
    matches the CPU's on the cage's own pixels: mean 0.46-0.63 / 255, 99.6 % within 3 (much of it the 3D beneath);
  - a gizmo drag sends the card 16 bytes a frame for the cage, the picture mid-drag matches the CPU's, and a click
    picks the same vertex on both devices;
  - the Scene view on the card is the same at 67 % and 100 %, the Game view isn't;
  - switching to the card at 67 %: the view goes to full size, and verify mode finds no stale picture;
  - after a GPU error, the card opened again gets the whole cage.
- Unit checks: Windows 10,412, 0 failed (726 tests); the whole suite in render-cache verify mode: 10,413,
  0 failed, 0 stale pictures. Linux with libraries: 10,259, 0 failed. Sanitizer build: 10,151, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.
- Stress: `pushpull` 26,620 operations, 0 problems, 394 overlaps (unchanged); `modifiers_ngon` 0 problems.

**Review** (Opus code-reviewer). Fixed:
- a card opened again after an error (perhaps at the same address) could miss the cage's edges: everything kept for
  the card is forgotten with it;
- the full-size choice is in the Scene view's picture key (a scaled first frame could have stayed);
- the cage is drawn by the card only on a full-size frame;
- each line is a strip along it, not a box around it (a long diagonal edge was mostly discarded pixels);
- the cage's shaders are optional: if they can't be made, the CPU draws the cage and the view stays on the card;
- the dot colour and the depth bias shared by both paths; a cage the card refuses is logged once;
- the drawing-order change (the grid and wireframes now over the cage) noted in ADR 0036.

## 2026-10-16 (round 124): pass 11 begins - measured first, and Edit Mode drags work out only what moved (tasks 0098, 0099)

**Asked:** "What should be more optimization gains?" (all four areas chosen: real scenes on the GPU, Edit Mode on big
meshes, computers without a GPU, path-traced renders; the pass 11 plan approved).

**Changed**
- **Measured first (task 0098):** `editor_render` breaks down the 50k-face Edit Mode drag with the views on the GPU
  (moved in place, draw items, the GPU path's time, the bytes sent to the card). It also adds a textured scene with a
  Sky world on the GPU device: all its views still draw on the CPU, because of the Sky.
- **Edit Mode drags (task 0099):** moving a few vertices of a big mesh no longer redoes the whole mesh every frame.
  - What moved is found with one test per vertex.
  - Only the moved faces' normals and the corners at their vertices are worked out again: the same to the bit as a
    rebuild, in every shading mode.
  - Only those corners are written, and the GPU views send the card only the changed ranges.
  - The 50k-face drag on the laptop setting: 45 -> 28 ms with the views on the CPU, 25 -> 14-15 ms on the GPU.
- The kept normals aren't copied with a mesh, and a mesh never dragged doesn't hold them (`memory_bytes` counts
  them).
- GPU uploads end in a barrier that makes their writes visible to later draws.

**Checked**
- New tests (round 124):
  - 112 random drag steps on 7 kinds of mesh, in both caches, are bit for bit a rebuild: smooth, smooth by angle,
    sharp edges with a seam, sharp edges with an angle, a degenerate quad, mixed flat / smooth faces, flat;
  - with the views on the GPU, the card's copy is read back after every frame of a real gizmo drag and equals a
    whole upload. Leaving out one range made all 12 frames differ, checked once by hand;
  - the picture after the drag is the CPU's within ADR 0013's bar.
- Unit checks: Windows 10,385, 0 failed (721 tests); the whole suite in render-cache verify mode: 10,386, 0 failed,
  0 stale pictures. Linux with libraries: 10,259, 0 failed. Sanitizer build: 10,151, 0 failed, no reports.
  `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.
- Stress: `pushpull` 26,620 operations, 0 problems, 394 overlaps (unchanged); `modifiers_ngon` 0 problems.

**Review** (Opus code-reviewer). Fixed:
- the kept normals are no longer copied with every mesh, and are counted;
- a degenerate edge (both ends one vertex) is joined as the whole pass joins it;
- the fan's corner lookup is a binary search, so a vertex with thousands of faces stays cheap;
- the partial counter counts only updates that worked;
- a range that doesn't fit is refused, and the whole mesh goes up instead;
- only the moved vertices are converted for the card;
- the upload barrier;
- the tests: more mesh kinds, and the card's copy compared exactly.

## 2026-10-15 (round 123): OptiX against GPU Compute - a spike (task 0097, ADR 0037)

**Asked:** "They are installed but haven't restarted my computer" (the CUDA Toolkit 12.8 and the OptiX SDK 9.1.0 for
the OptiX spike).

**Changed**
- **A measurement, not a render device yet:** a small OptiX path tracer (`gpu::OptixTracer`) races GPU Compute
  (Vulkan with the RT cores) on the same scene. Stress `--only gpu_optix`.
- **Result:** OptiX renders 1.78-2.08x the samples a second on both cards: RTX 4070 SUPER 1041-1059 against 551-593,
  RTX 3060 Ti 589-594 against 286-287 (picture read-backs timed on both). That is the most it could gain: its kernel
  is simpler (Lambert, flat normals, no per-sample denoiser passes). ADR 0037 records it, and a full OptiX device
  waits for approval.
- **Build:** with the CUDA Toolkit and the OptiX SDK on the machine, `build.bat` compiles the kernel to PTX through
  NVRTC (CUDA 12.8's nvcc can't parse Visual Studio 2026's headers) and builds OptiX in. Without them (CI, the
  installers) nothing changes. At run time only NVIDIA's driver is needed, through Blender's `extern/cuew`
  (Apache-2.0).

**Checked**
- New tests (round 123): a build without OptiX says so; with it, a small scene traced by OptiX matches GPU Compute's
  mean brightness (100.024 %). Run here with an OptiX build; the chain's builds have no SDK and report it skipped.
- Unit checks: Windows 10,376, 0 failed (719 tests); the whole suite in render-cache verify mode: 10,377,
  0 failed, 0 stale pictures. Linux with libraries: 10,257, 0 failed. Sanitizer build: 10,149, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). The go stands, and the upper-bound framing is right. Fixed:
- OptiX's read-back is timed, as GPU Compute's merge is (2.15-2.35x before, 1.78-2.08x now);
- the per-sample differences are listed in the docs;
- the test skips when OptiX can't start;
- a null error string, leaks on failed allocations, and the mesh index checked;
- `build.bat` picks the newest SDK, checks for NVRTC, builds its PTX compiler once, says why it falls back, and never
  builds OptiX into the installers;
- the OptiX loader's BSD-3 notice and cuew's own licence text are in `licenses/`.

## 2026-10-15 (round 122): the views on the GPU - Viewport Device (task 0096, ADR 0036)

**Asked:** "If it's more performant, then yes" (the GPU viewport after spike 2's go); "For the OptiX system let me
know what I need to do" (answered with the setup steps: CUDA Toolkit 12.8, the OptiX SDK 8.0+, `OPTIX_ROOT_DIR`).

**Changed**
- **Preferences > Viewport Device (Auto / CPU / GPU):** the Scene view, Game view and Camera Preview draw their 3D on
  the GPU (Vulkan) and read it back into the editor's canvas. Overlays, gizmos, picking, the UI and the camera
  filters' passes are unchanged.
- **What it draws:** plain materials, sun, point and spot lights with the sun's shadows (3x3 or Low's 2x2), Gradient
  and Color worlds, the editor's sky, a camera's solid background, Standard / Filmic / ACES and every OpenColorIO view
  (AgX included, through the CPU's own LUT).
- **Anything else** draws on the CPU, as before, and Preferences says why: textures, procedurals, toon, see-through,
  several materials on one mesh, GI, area lights, Sky / HDRI worlds, camera filters. F12 renders stay on the CPU.
- **On the laptop setting, this machine's GPU:** moving an object 16.6-16.9 -> 4.1 ms a frame, playing 20.6-22.1 ->
  5.6-6.8, an Edit Mode drag on 50k faces 43.7-46.6 -> 24.7-25.1. A view the GPU draws renders its own shadow map.
  The GPU opens in the background when the editor starts.

**Checked**
- New tests (round 122), each skipped without a GPU:
  - the Scene and Game views of the default scene match the CPU's (mean 0.086 / 255, 99.99 % within 3 / 255);
  - a procedural sends the view to the CPU with its reason, and the picture is then the CPU's exactly;
  - a plane seen from below matches (back faces culled as on the CPU; round 121 now compares depth too), and a click
    picks the Cube from the GPU's ids;
  - an Edit Mode gizmo drag updates the mesh on the card in place (3 updates, 1 upload);
  - Scene lighting off (the editor's headlight) and a mirrored Cube match too (mean 0.083 and 0.087 / 255);
  - Viewport Device is kept in the prefs, and Auto stays on the CPU when the tests pin it.
- Unit checks: Windows 10,374, 0 failed (717 tests); the whole suite in render-cache verify mode: 10,375,
  0 failed, 0 stale pictures. Linux with libraries: 10,255, 0 failed. Sanitizer build: 10,147, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). Fixed:
- mirrored placements cull the other side;
- the CPU no longer renders the shadow map for a GPU-drawn view, and depth comes back only when something reads it;
- the colour is read straight into the canvas;
- the meshes on the card have a budget;
- casting follows the CPU's any-slot rule;
- each view's reason is kept, and Solid / Wireframe are named;
- the views' statistics count the GPU's triangles;
- the GPU opens in the background.

Checked and kept: the editor's own lighting already matched the CPU (a finding the new test disproves).

## 2026-10-15 (round 121): a GPU view, spike 2 - go (task 0095, ADR 0013 amended)

**Asked:** "Can we look into rendering the editor with the GPU and not just the CPU?" The plan was a new spike first,
both ways to the window, then a decision by its numbers. Then "Go for it".

**Changed**
- **A measurement, not a feature yet:** a Vulkan rasteriser (`gpu::Raster`) draws the editor scene's content at 1080p
  the way the CPU's deferred view does (shadow map, G-buffer, the same lighting formulas). Then it either reads the
  picture back (A) or shows it in a window through a swapchain (B). Stress section `--only gpu_raster`.
- **Result, go on both cards:**
  - the GPU's work is 0.3-0.4 ms a frame;
  - (A) takes 2.7 ms on the RTX 4070 SUPER and 5.7 ms on the RTX 3060 Ti, against the CPU's 14.4 ms;
  - (B) takes 0.5 and 2.2 ms, against 16.6 ms;
  - the same picture: mean 0.32 / 255 on covered pixels, ids exact away from edges.

  The laptop is estimated (Iris Xe isn't here): about 2.5 ms of GPU work, against the CPU's 56 ms on the laptop
  setting. ADR 0013 records it. The GPU viewport itself waits for approval.

**Checked**
- New tests (round 121): asking for a GPU that isn't there fails cleanly with a reason; a small deferred frame (sun,
  point and spot lights) on the GPU matches the CPU's (mean 0.08 / 255, 100 % within 3 / 255, ids exact; covered pixels
  away from edges; skipped without a GPU).
- Unit checks: Windows 10,349, 0 failed (711 tests); the whole suite in render-cache verify mode: 10,350,
  0 failed, 0 stale pictures. Linux with libraries: 10,251, 0 failed. Sanitizer build: 10,143, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). The go stands; fixed: the verdict needs every card measured and every call in the
timed loops is checked, explicit pass dependencies, the shadow map always cleared, the swapchain asked for only where
the card lists it, one semaphore per swapchain image, the picture scored on covered pixels away from edges by one
helper, and the shader takes the triangle's own normal and the material's specular as the CPU does.

## 2026-10-15 (round 120): the denoiser on the GPU (task 0094)

**Asked:** "Can we also look into Cuda and Optix support?" - then "Go for it" on the plan: the denoiser first (no SDK),
then an OptiX spike. The download of OpenImageDenoise's CUDA and HIP modules from Blender's library repositories
(33.6 MB) was approved first.

**Changed**
- **Render Settings > Denoise Device (Auto / CPU / GPU):** OpenImageDenoise runs on an NVIDIA (CUDA) or AMD (HIP) card
  when there is one, with the CPU as the fallback. The installers and `dist` carry the two modules (never a Git LFS
  placeholder). A 1080p denoise: 580.9 ms on this 16-core CPU before, 43.1 ms on the RTX 4070 SUPER.
- **The CPU denoise is faster too** (580.9 -> 463.4 ms at 1080p): the filter is kept between frames of one size
  instead of made anew each time, and the images are prepared in parallel.
- The status line names the device ("denoised (OpenImageDenoise, CUDA: NVIDIA GeForce RTX 4070 SUPER)"); a Learn
  section in "Rendering on the GPU".

**Checked**
- New tests (round 120): the GPU's denoise matches the CPU's on screen (mean 0.078 / 255, 100 % within 3 / 255;
  skipped without a GPU); a GPU that fails gives exactly the CPU's picture and counts one fallback; Denoise Device is
  saved with the scene, and Auto stays on the CPU when the tests pin it.
- Unit checks: Windows 10,339, 0 failed (709 tests); the whole suite in render-cache verify mode: 10,340,
  0 failed, 0 stale pictures. Linux with libraries: 10,247, 0 failed. Sanitizer build: 10,139, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). No bugs; fixed: the filter and buffers are dropped before a new size (a card never
holds both), after an error (now also checked after the filter's commit) and before a GPU render fits its scene to
the card; a card OIDN lists but can't open counts a fallback; the test's failure comes after a real run on the card;
the CMake build ships the modules too. Kept: alternating sizes remake the filter once per finished frame.

## 2026-10-15 (round 119): performance pass 10 written up (task 0093)

**Asked:** the performance pass ("Let's increase overall editor performance in general"), its last task.

**Changed**
- **docs/PERFORMANCE.md, pass 10:** the before / after table on the laptop setting (x 1.4). The editor's own frames,
  laptop setting: orbiting 13.1 -> 11.3 ms (88 fps), moving an object 27.3 -> 22.6 (44 fps), editing a material
  colour 25.1 -> 21.9, playing 23.8 -> 20.6; Edit Mode drags on 50k faces 137 -> 46 ms (7 -> 22 fps); a 60 fps cap now
  holds (30.6 -> 16.6 ms between frames). ADR 0026's laptop targets (60 fps orbiting, 30 fps editing) are met; the
  plan's 60 fps while editing and 30 fps for 50k-face drags are not, and the write-up says what is left and why.
  Next candidates refreshed (the Game view during a drag, the shader's shadow lookup, the sky, incremental overlap
  checks). ADR 0026 notes the results.

**Checked**
- Docs only; every number is from a `blendity_stress --laptop --quality low` run on this machine (2026-10-15).

## 2026-10-15 (round 118): views nobody picks in keep no full-size depth and ids (task 0092)

**Asked:** the performance pass ("carry on the next tasks").

**Changed**
- **At a reduced viewport resolution** (Low draws the views at 67 % and scales them up), the Game view and the Camera
  Preview no longer scale their depth and object ids up to full size as well - only the Scene view is clicked in, so
  only it needs them. The same pictures.
  Small: on the laptop setting a moved-object frame went 16.16 -> 16.09 ms (copied / not, in turn in one run);
  most of a scaled view's cost is its colour, which stays.

**Checked**
- New test (round 118): at Low, with the Camera Preview up, the Game view shows the same pixels with the planes copied
  and not, without and with a retro filter (a camera's filter stack scales up on its own path).
- Unit checks: Windows 10,323, 0 failed (706 tests); the whole suite in render-cache verify mode: 10,324,
  0 failed, 0 stale pictures. Linux with libraries: 10,231, 0 failed. Sanitizer build: 10,137, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). No bugs; tidied: a camera's filter stack follows the view's choice (its upscale had
its own copy of the planes), and the test has a hook that puts the old copy back to compare.

## 2026-10-15 (round 117): plain-material pixels lit 8 at a time (task 0091, ADR 0035)

**Asked:** the performance pass ("carry on the next tasks"; checks overlapping the next task).

**Changed**
- **Shading a view:** on a processor with AVX2, the pixels of a plain material (no textures, procedural or axis
  mapping) are lit 8 at a time - the light directions, falloffs and highlights of every sun, point and spot light -
  with the same pixels on screen. Lightmaps, probes, voxel GI, toon and area lights keep the one-pixel path. On the
  laptop setting, off / on back to back: moving an object 17.18 -> 16.31 ms a frame, the Scene view's shading 4.45 ->
  3.86 ms. The Profiler's renderer options (and the console's `raster simd on|off`) can switch it off to compare.

**Checked**
- New test (round 117): a view with a sun and its 3x3 and 2x2 shadows, point and spot lights, highlighted faces, a
  two-sided face seen from behind and a procedural beside plain materials shows the same pixels with the 8-wide path on
  and off, its colours before display within 1e-4 relative (1.5e-5 measured: the release build lets the compiler
  reorder the one-pixel path's sums), and thousands of its pixels went through the 8-wide path. Skipped, and said so,
  on a processor without AVX2.
- Unit checks: Windows 10,319, 0 failed (705 tests); the whole suite in render-cache verify mode: 10,320,
  0 failed, 0 stale pictures. Linux with libraries: 10,227, 0 failed. Sanitizer build: 10,133, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). No bugs; tidied: the test compares the colours before display too (not only the 8-bit
pixels), the kernel's helpers are functions (GCC can't give a lambda the AVX2 target), the ADR's claims say "the same
displayed pixels" rather than "bit for bit".

## 2026-10-15 (round 116): in place, faster - only moved faces split again, face normals in parallel (task 0090)

**Asked:** the performance pass ("carry on the next tasks").

**Changed**
- **A drag on a big mesh:** its render data followed the drag by splitting every quad into triangles again and working
  out every face's normal on one thread; now only faces with a moved vertex are split again (a face's triangles depend
  on its own corners alone) and the normals are worked out in parallel - the same results to the bit. On a 50k-face
  mesh, laptop setting: 9.1 -> 6.7 ms a frame for the two caches; a gizmo drag 35.4 -> 33.7 ms, G 34.4 -> 33.0 ms.

**Checked**
- New test (round 116): a cache read after several moves it missed, a vertex pulled across a quad's diagonal and back
  to the same bits, and a fold that refuses the in-place update followed by more moves all draw exactly what a rebuild
  draws; round 112's 48 random drag steps still find no difference.
- Unit checks: Windows 10,311, 0 failed (704 tests); the whole suite in render-cache verify mode: 10,312,
  0 failed, 0 stale pictures. Linux with libraries: 10,219, 0 failed. Sanitizer build: 10,125, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). No bugs; the per-face work runs through a plain lambda (no `std::function` per face)
and the moved-face test is a small function of its own.

## 2026-10-15 (round 115): the Edit Mode cage drawn from parallel batches (task 0089)

**Asked:** "carry on the next tasks" (the performance pass; with the next task now written while the last one's
checks run).

**Changed**
- **The edges and vertex dots of a big mesh in Edit Mode** were recorded one call at a time, each edge placing both
  its vertices in the world; now each vertex is placed once and the edges and dots are prepared in parallel and
  recorded in order - the same picture. The overlays of a 50k-face mesh went from 7.4 to 4.5 ms a frame on the laptop
  setting: a gizmo drag 38.6 -> 35.4 ms, G 37.0 -> 34.4 ms.

**Checked**
- New tests (round 115): 30,000 lines and 10,000 dots (depth-tested, clipped, some behind the eye) drawn from
  batches give exactly the pixels one call each gives; an item left out stays out.
- Unit checks: Windows 10,304, 0 failed (703 tests); the whole suite in render-cache verify mode: 10,305,
  0 failed, 0 stale pictures. Linux with libraries: 10,212, 0 failed. Sanitizer build: 10,118, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). No bugs; the batch reserves its room in the overlay list before appending.

## 2026-10-15 (round 114): uniform materials shaded once per triangle; one shadow map for an edited mesh (task 0088)

**Asked:** the performance pass ("Keep looking for performance gains throughout the project", overnight).

**Changed**
- **Plain materials** (no textures, procedural or axis mapping - most of a blocked-out scene) are worked out once per
  triangle instead of once per pixel, with the same picture to the bit. On the laptop setting: moving an object
  19.5 -> 17.2 ms a frame, editing a material colour 17.9 -> 16.5, playing 16.6 -> 15.5. The Profiler's renderer
  options can switch it off to compare.
- **An Edit Mode drag renders the sun's shadow once a frame**, not once for the Scene view and again for the Game
  view (the edited mesh's two render caches have the same shape): a gizmo drag on 50k faces 42.5 -> 38.6 ms.

**Checked**
- New tests (round 114): 200 random plain materials evaluated once equal per-pixel evaluation to the bit; a deferred
  view with a sun and its shadows, point and spot lights, flat and smooth meshes, emission and a procedural draws
  the same pixels either way; an edited mesh's two caches share a shape and a copy or an edit makes a new one; a
  drag renders the shadow map once a frame.
- Unit checks: Windows 10,301, 0 failed (702 tests); the whole suite in render-cache verify mode: 10,302,
  0 failed, 0 stale pictures. Linux with libraries: 10,209, 0 failed. Sanitizer build: 10,115, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). No bugs; tidied: a live texture's footprint isn't worked out for a plain material's
pixels, a vertex-snapped copy of a render mesh drops its shape, the option's cache-key bit no longer overlaps the shade
mode's, a mesh id's copy is `noexcept` (moves stay moves).

## 2026-10-15 (round 113): the Edit Mode drag loop stops redoing whole-mesh work (task 0087)

**Asked:** the performance pass (plan item 0087).

**Changed**
- **Dragging vertices of a big mesh:** the check for flat rings of faces that a drag folds (they are zipped again)
  built a map of every edge of the mesh on every frame; it now looks edges up in per-vertex face lists kept while the
  mesh's faces don't change. The drag's own work on a 50k-face mesh went from 25 ms a frame to 1.6.
- **G** reuses its copy of the mesh from frame to frame instead of copying the whole mesh each time.
- **During any drag** (a mouse button held, G / R / S, Push/Pull, a modal adjustment) the overlapping-vertices check
  and the "Lighting out of date" status wait for it to end; the Modeling Tools panel shows what it found before.
- With task 0086: a Move-gizmo drag on 50k faces went from 98 ms a frame to 43 on the laptop setting, G from 100 to 42.

**Checked**
- New tests (round 113): a 10-frame gizmo drag on a big mesh copies it at most once, builds no render mesh, checks no
  overlaps and hashes no lighting key, and releasing checks once; G reuses its copy and Esc restores the mesh exactly;
  the per-vertex face lists give exactly what the old edge map gave, odd faces included, and are kept while only
  positions move.
- Unit checks: Windows 10,088, 0 failed (698 tests); the whole suite in render-cache verify mode: 10,089,
  0 failed, 0 stale pictures. Linux with libraries: 9,996, 0 failed. Sanitizer build: 9,902, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). Fixed:
- Holding the lighting answer during a drag froze Live GI and the status bar until release: during a drag a change is
  taken as out of date without the exact (costly) check, which runs when the drag ends.
- The overlapping-vertices overlay vanished whenever a mouse button was held (orbiting, a click): it keeps showing a
  result that is still current, and hides only while a drag changes the mesh.
- G's copy is remembered as a weak reference rather than an address; edge lookups scan the shorter vertex list.

## 2026-10-15 (round 112): Edit Mode - the render mesh follows a drag in place (task 0086, ADR 0034)

**Asked:** the performance pass (plan item 0086).

**Changed**
- **Dragging vertices in Edit Mode no longer rebuilds the mesh's render data every frame.** When only positions
  moved, the shaded mesh follows them in place - the same triangles and the same picture as a rebuild (quads and
  n-gons are split again from the new positions) - and the edge table and smoothing adjacency are kept. A Move-gizmo
  drag on a 50k-face mesh went from 98 to 69 ms a frame on the laptop setting, G from 100 to 67 ms.
- Normal-mapped meshes' tangents follow a drag the quick way and are made with MikkTSpace again when the drag ends.
- Deforming modifiers move their render mesh with the exact triangulation now (they kept the build's before).

**Checked**
- New tests (round 112): 48 random drag steps on six kinds of mesh draw bit for bit what a rebuild draws; a quad's
  diagonal flipping during a drag; a new face builds again while moved vertices keep the edge table; quick tangents
  within 10 degrees of MikkTSpace's, exact after the drag ends; a fold that changes where smoothing splits builds
  again; NaN positions draw as a rebuild's; F9 still adjusts a positions-only operator on a normal-mapped mesh.
- Unit checks: Windows 10,078, 0 failed (695 tests); the whole suite in render-cache verify mode: 10,079,
  0 failed, 0 stale pictures. Linux with libraries: 9,986, 0 failed. Sanitizer build: 9,892, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). Fixed:
- Settling the tangents after a drag bumped the mesh's version, which ended an F9 adjustment and re-ran modifier
  stacks: it only drops the caches now, and a separate counter the views key on makes them redraw.
- Meshes changed outside Edit Mode (an origin moved) kept their quick tangents until their topology changed, so a
  bake could depend on how a mesh got its shape: every mesh with quick tangents is settled when an interaction ends.
- Deforms worked out the triangle slots every frame: once per topology now. `memory_bytes()` (and so the undo budget)
  counts the kept topology; the test's drags are the same on every compiler.

## 2026-10-15 (round 111): per-view waste - the voxel GI grid's columns, the shadow map's ids plane (task 0085)

**Asked:** the performance pass (plan item 0085).

**Changed**
- **Realtime GI:** a view checking whether its voxel grid still fits the scene no longer allocates a fresh column
  pyramid (about 87,000 columns at 256) every frame to find out; only a grid it actually takes gets its columns.
- **Shadow maps** keep no picking-ids plane: nothing is cleared or written for it. The sun's map on a moved-object
  frame went from 0.68 to 0.48 ms on the laptop setting.
- **The GI blur** no longer copies its input plane before writing every texel anyway.
- **Measured and left alone:** gathering the views' draw items (0.07 ms a moved frame) and the scene hashes (0.03 ms)
  are too small to be worth changing in these scenes; shading is 5.1 of the Scene view's 7.2 ms (tasks 0088-0089).
  The Realtime GI drag in Automatic mode (71 ms) is the automatic lighting update, not the grid.

**Checked**
- New tests (round 111): small moves inside the GI grid make no new pyramid, leaving it makes one; a grid fitted
  without columns has the same placement and key; a shadow map has no ids plane and the same depth.
- Unit checks: Windows 10,058, 0 failed (689 tests); the whole suite in render-cache verify mode: 10,059,
  0 failed, 0 stale pictures. Linux with libraries: 9,966, 0 failed. Sanitizer build: 9,872, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). No bugs; hardened: the Gouraud raster path and the selection outline also skip a
target without ids (a shadow map), so a future pass drawing into one can't index an empty plane; a shadow target
gives its ids memory back.

## 2026-10-15 (round 110): the frame-rate cap holds, and only what changed is presented (task 0084)

**Asked:** the performance pass (task 0083 measured; this is its first fix).

**Fixed**
- **A 60 fps cap ran at about 33 fps on Windows**, whatever the frame cost: every wait was rounded up to Windows'
  15.6 ms timer tick, so a frame that left less than a tick to wait lost a whole one more. The editor now waits on a
  high-resolution timer, and a frame pacer blocks only for as long as the waits have proved on time and polls for
  the rest: a real window's 13 ms frames under a 60 fps cap went from 30.6 ms apart to 16.6 ms (95 % within 16.7).
- **Presenting** sends only the bands of rows that changed: 0.68 ms instead of 2.42 for a Scene-view-sized change
  (95 % within 0.70 ms instead of 9.38), and 0.07 ms when nothing changed.
- **A window repaint drew the same frame twice** (the paint, then a queued event); now once.

**Checked**
- New tests (round 110): the pacer under a pretend 15.6 ms timer (the old loop 31.2 ms apart, the pacer 16.6); with
  a precise timer it blocks for nearly the whole wait; input waking it early doesn't start the frame early; only
  changed bands are presented, everything on a new size or when asked; one 30 ms-late wake-up doesn't leave the
  pacer polling for good; a change only in pixels' top bits is still seen.
- Unit checks: Windows 10,037, 0 failed (686 tests); the whole suite in render-cache verify mode: 10,038,
  0 failed, 0 stale pictures. Linux with libraries: 9,945, 0 failed. Sanitizer build: 9,851, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). Fixed:
- On Linux (and Windows' fallback) a window uncovered or first shown asked for a repaint as an event, which presented
  nothing when the frame hadn't changed: a repaint, resize or DPI change now presents all of it.
- One very late wake-up could leave the pacer polling for good (it only learnt from waits it blocked in): an unused
  margin now shrinks each frame.
- The band hash could miss a change confined to pixels' top bits: each chain rotates and is finalised.
- macOS presented the whole picture once per changed run: once per frame now. `winmm` is linked for CMake builds too.
- Waits were truncated to whole milliseconds: `poll_events_for` takes seconds; Windows' waits wake for input already
  queued and can't hang on a timer that never fires.

## 2026-10-15 (round 109): performance pass 10 begins - measuring Edit Mode drags, the mesh caches and frame pacing (task 0083)

**Asked:** "Let's increase overall editor performance in general." Chosen: the viewport's frame rate and big meshes
in Edit Mode, keeping the CPU renderer; plan of tasks 0083-0091. This first task only measures.

**Added**
- **The Profiler** shows the time between frames (what the screen sees, the cap's waiting included) and what the
  mesh caches did in the last frame: copies for an edit, render meshes built or moved in place, tangents, edge
  tables; plus an Edit Mode drag's writes, the overlap check, the lighting key, snapping, the views' draw items and
  the scene hashes. The Learn tab's performance lesson explains them ("Reading a frame").
- **blendity_stress:** `editor_render` drags the 50k-face mesh for real (the Move gizmo, and G) and notes each part;
  `--only present` opens a window to time presenting frames and the cap's pacing.

**Found** (the laptop setting, `--laptop --quality low`; docs/PERFORMANCE.md, Pass 10)
- A 60 fps cap with 13 ms frames gave **30.6 ms between frames** (33 fps): Windows rounds the loop's waits to its
  15.6 ms timer. Presenting a frame costs 2.4 ms on average (9.3 ms at the 95th percentile).
- A gizmo drag on 50k faces costs 98 ms a frame: two render meshes built from scratch (38.6 ms, the Scene view's flat
  one and the Game view's smooth one), the drag's writes 24.5 ms (ring repair scans every face), the shadow map
  7.5 ms, the Edit Mode overlay 8.7 ms. G is the same plus a 2.7 ms mesh copy a frame.

**Checked**
- New tests (round 109): a gizmo drag moves the counters; the Profiler draws its new rows during a drag.
- Unit checks: Windows 10,022, 0 failed (680 tests); the whole suite in render-cache verify mode: 10,021,
  0 failed, 0 stale pictures. Linux with libraries: 9,928, 0 failed. Sanitizer build: 9,834, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). No behaviour changed; fixed:
- The pacing figure was measured with present on top of 13 ms of work: present now counts inside the 13 ms (the
  result held: 30.6 ms between frames).
- "Moved in place" counted refused moves too; the first frame after start showed everything counted before it.
- The 95th percentile was rounded down (the Profiler and stress); the Profiler shows it from two frames on.
- The stress run's undo after the gizmo drag could undo an earlier step if the drag had missed.
- The tests now look for the Profiler's rows by name, and no longer pin today's rebuilds; ARCHITECTURE has a row.

## 2026-10-14 (round 108): installing without knowing GitHub - one-click downloads, releases of installers only (task 0082)

**Asked:** "Remove build-pr71" (done: the release and its tag). "Note the installer in the releases and update the
readMe in how to install. A non-technical person should be able to install it." "Have the release not contain the
source files if the user doesn't want them. Only have an executable or similar on whatever platform it's downloaded
to." Chosen: one-click links (v0.x releases are GitHub's Latest, still called pre-releases), extras moved aside.
Then: "Make them artifacts" - a merged PR's build is no longer published as a release.

**Changed**
- **README, Download:** "Download for Windows / Mac / Linux" links that fetch the newest installer directly, then
  step-by-step installs (Windows' "Windows protected your PC" > Run anyway; the Mac's Open Anyway; Linux's Allow
  executing), uninstalling and first steps.
- **Each release's notes** say which file to download and how to install it. An update inside Blendity still shows
  only what changed.
- **A release lists only the installers** (Blendity-Setup-Windows.exe, Blendity-macos.dmg, Blendity-linux-x86_64.AppImage)
  and SHA256SUMS.txt. The libraries archives and the Linux .tar.gz moved to one "libraries" release that Blendity's
  updater reads by itself. GitHub's own "Source code" files can't be removed from a release (GitHub adds them to
  every one); the notes say they're for developers.
- **0.10.0 and later 0.x versions** are GitHub's Latest release (so the links work), titled "(pre-release)"; Version
  History calls them pre-releases. PR builds stay GitHub pre-releases.
- **Merges into main make no release any more**, only build artifacts: a release comes from a `v*` tag, made when
  the user asks for one ("Make them artifacts. I will let you know explicitly when I want a release.").
- **Mac:** the app is signed ad hoc (when the signature verifies), so macOS offers Open Anyway instead of calling the
  download damaged.

**Review** (Opus code-reviewer). Fixed:
- Builds installed before this change look for "Blendity-Setup-...": the Windows installer is
  Blendity-Setup-Windows.exe, so they are still offered updates.
- A 0.x beta would have become the Latest release (and the README's download): betas are checked first now, and
  GitHub picks the Latest.
- Tagged releases had lost their "what changed" notes: GitHub's summary of the merged PRs comes before the install
  guide again.
- The "libraries" release would have grown without end: it keeps tagged releases' archives and the newest 20 PR
  builds'. The publishing script stops if an installer is missing, copes with no archives, and is split into steps.
- Ad-hoc signing could fail on the bundle's data files: it is kept only when it verifies.

**Checked**
- New tests (round 108): an update's notes leave out the install guide; a release of version-free installers finds
  its installer, takes its libraries from the "libraries" release by exact tag (build-pr7's never build-pr72's) and
  the "libraries" release is never offered.
- The publishing script against a stand-in `gh`: installers to the release, the rest to "libraries", a deleted
  build's archive removed.
- Unit checks: Windows 10,011, 0 failed (678 tests). Linux and the sanitizer build: on CI for this PR.

## 2026-10-14 (round 107): redo of material assets, tests that never touch the project, one version number (task 0081)

**Asked:** merge the stack (done: #61-#71 into main); "push V1.0.0 (or whatever nomenclature you use for the first
release before hotfixes)"; "test material shouldn't be even in the project at all when it comes to release"; "fix the
redo material system". Then: "let's leave it at 0.10.0 because I don't want an official 1.0.0 release just yet (needs
more refining)", and "still publish that version so people know it's a pre-release of the program" - so the version
stays 0.10.0, published as the pre-release `v0.10.0`.

**Changed**
- **Blendity 0.10.0 published as a pre-release:** a `v*` tag before 1.0 (or with a suffix, v1.0.0-beta.1) makes a
  GitHub Release marked "pre-release", titled "Blendity 0.10.0 (pre-release)". The updater still offers such a tagged
  version with previews off (previews are the per-PR builds); Version History labels it "pre-release".
- **One version number:** the VERSION file (still 0.10.0). CMake's project version and the macOS bundle read it (the
  bundle said 0.9.0). A future stable release is a VERSION change plus a `v*` tag.
- **Fixed: redo of a material linked to a .mat** brought back the undone values (any field) and the asset kept them:
  an undo or redo state set aside keeps copies of its own linked materials now.
- **Fixed: tests could write into the project they ran from.** A test making a clip or material before any editor
  existed resolved its path against the working folder; the test and stress programs point the asset root at their
  scratch projects first. The leaked R86Hold/R86Spin/R86Up.anim and the empty Materials/R51 were deleted from the
  project. (Installers never carried Assets.)

**Checked**
- New tests (round 107): a .mat-linked material's edit undoes and redoes twice and the asset follows (it fails without
  the fix: green 0.10 after the redo); objects sharing a .mat still share one copy in an undo state; the tests'
  asset root is the scratch project before the first test, and a relative asset path lands there; a tagged version
  GitHub marks a pre-release is offered with previews off, PR builds only with them on.
- Unit checks: Windows 10,004, 0 failed (676 tests); the whole suite in render-cache verify mode: 9,999,
  0 failed, 0 stale pictures. Linux with libraries (before the review fixes; CI runs them after): 9,906, 0 failed. Sanitizer build: 9,812, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). Fixed:
- An undo state gave every slot its own copy of a shared .mat material, so objects that shared one material stopped
  sharing it after an undo: slots sharing one material share one copy now (as duplicating a scene does).
- The asset-root test passed whatever the tests did first: it checks the root main() set before any test now.
- CMake's project version and the macOS bundle's version read the VERSION file instead of a number of their own.

## 2026-10-14 (rounds 105-106): updates, older builds, and Blender's libraries optional at run time (tasks 0079-0080, ADR 0033)

**Asked:** "an option for the software while installed on a computer to check for updates and then ask the user if
they want to install the update. The install note should note the changes that the update will bring. Furthermore,
the user can choose to go to an older update (Before A Certain Pull Request)"; "if the DLL or external libraries isn't
found, result back to the fallback with the software that doesn't rely on dependencies (such as situations with no
internet, etc.). Then if there is internet, ask the user if they want to install the new libraries"; "The user should
always have the option to use dependencies or not."

**Changed**
- **Builds for every merged PR:** a merge into `main` publishes a pre-release `build-pr<N>` (notes: the PR's
  description); v* tags publish stable releases. Each carries the installers, the libraries archive and
  SHA256SUMS.txt. Builds know their tag and version (shown in About and the status bar).
- **Updates:** once a day, when online, Blendity asks GitHub's releases list (WinHTTP on Windows, `curl` elsewhere: no
  new library) and offers a newer build with its notes - Install / Not now / Skip this one. Help > Check for Updates
  and **Help > Version History** (every build for this system, older ones too: the build before a certain PR).
  Downloads are checked against the release's SHA-256 list; Windows installs silently and restarts Blendity, an
  AppImage replaces itself, macOS opens the disk image. Preferences > Updates: check at start, include PR builds.
- **Libraries optional at run time:** an install holds Blendity (dependency-free, always starts) and Blendity-full.
  Blendity hands over when the full build's libraries load (`--probe`) and **Preferences > Libraries > Use Blender's
  libraries** is on; otherwise - a library missing or damaged, offline, turned off, `--no-libs` - it runs on its own
  code, and online it offers this build's libraries (Download / Not now / Never ask), unpacked into
  `~/.blendity/libs/<build>`. The Windows installer's "Blendity only" type leaves them out.
- The JSON reader moved from the glTF importer to `core/json.h`; SHA-256, downloads, releases and the launcher are new
  in `core/`.
- #70's installer jobs needed: Windows Embree's SYCL runtime (`dpcpp`), Linux GCC 14 (Blender's libraries call its C++
  runtime, shipped in `lib/`), macOS's own zlib.

**Checked**
- New tests (round 105): SHA-256 against FIPS 180-4's vectors; SHA256SUMS lines; a releases list gives the newer builds
  (stable only or with PR builds), none for the newest or a local build, all in Version History; the Update and
  History dialogs at 1280 x 720, Skip remembered; (opt-in, `BLENDITY_NET_TESTS=1`) GitHub's real list fetched, a 404
  reported.
- New test (round 106): this build knows whether it has the libraries; `--probe` answers; the Libraries dialog at
  1280 x 720; Never ask and Use Blender's libraries kept with the preferences.
- By hand on Windows: a scratch install with both programs - with the libraries it handed over, with `--no-libs` and
  with `embree4.dll` removed it ran itself.
- Unit checks: Windows 9,992, 0 failed (672 tests); the whole suite in render-cache verify mode: 9,993,
  0 failed, 0 stale pictures. Linux with libraries: 9,900, 0 failed. Sanitizer build: 9,806, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). Fixed:
- On Linux and macOS, opening a folder or starting a program ignored SIGCHLD for the whole process, after which every
  download and the libraries probe failed: children nobody waits for are reaped by a thread of their own instead.
- The Windows installer never started Blendity again after an update: it does now ([Run] after an update started
  from Blendity), installing where the old copy is (for this user, or for everyone).
- A build older than the newest 100 releases never saw an update or its libraries: its own release is fetched too.
- Installing "Blendity only" over an install with the libraries left the old full build to be handed over to: it is
  removed.
- On Windows the dependency-free program returned at once while the full one worked: it waits and passes on the exit
  code.
- Downloads went to guessable names in the shared /tmp (another user could plant a link): a private
  ~/.blendity/downloads, and only plain file names from a release.
- Closing Version History by clicking outside stopped all later offers for the session.
- Replacing an AppImage could lose it on a full disk, and restarting after a libraries download ran from the old
  AppImage's vanished mount: the new one is copied beside the old first, and Blendity restarts as the AppImage.
- Unpacking the libraries and copying an AppImage ran on the editor's thread (seconds): on the downloading thread now.
- Releases are published one at a time, in merge order; the libraries are asked about at most once a day and never
  turn into an update offer when update checks are off.
- The workflow's token is read-only except in the release job (a pull request's build scripts run with it).
- curl keeps to https (redirects too) with no fixed time limit on large downloads; WinHTTP treats an unfollowed
  redirect as an error; SHA-256 streams the file; Windows' own tar.exe from System32; the probe gives up after 30 s;
  a download stops when Blendity closes; Version History scrolls through every build; the title no longer reads
  "Blendity Blendity".
- Left: a full build that crashes after a good probe doesn't fall back by itself (Preferences > Libraries or
  --no-libs does).

## 2026-10-14 (rounds 103-104): FFmpeg linked like Blender's other libraries, and the libraries in the installers (tasks 0077-0078, ADR 0032)

**Asked:** "Can we install ffmpeg and treat it like a blender dependency?" - link it, and "make sure other dependencies
should be included when installing the project. The user shouldn't really have to worry on downloading blender on
their own."

**Changed**
- **FFmpeg 8.1** (Blender's build: x264, x265, libvpx, libaom, Opus, Vorbis, Theora, LAME) is linked behind
  `BL_WITH_FFMPEG` by build.bat, build.sh and CMake. Render Animation encodes its MP4 itself (H.264, CRF 18) on a
  background thread; a build without it falls back to an `ffmpeg` program or PNG frames, as before. `libs` lists it.
- Fetched into this machine's Blender library checkouts: `ffmpeg` (Windows 46 MB, Linux 67 MB) and Windows' `aom`
  (FFmpeg's AV1 DLL, 9.5 MB); Linux also links OpenJPEG and Blender's `libc_compat` (copied to `extern/`).
- **`packaging/fetch_blender_libs.sh <platform>`** fetches only the library folders Blendity uses, at commits pinned in
  `packaging/blender-libs.txt` (about 600 MB a platform), plus Blender's colour config when missing.
- **The installers carry Blender's libraries:** the release workflow fetches them (cached), builds all three with
  them and checks each has Embree, OpenImageDenoise, OpenColorIO, Jolt and FFmpeg; the macOS app bundle holds `lib/`
  and `datafiles/`. Nobody installing Blendity needs Blender or its libraries.
- Licences: FFmpeg and its codecs listed; LGPL-2.1 and GPL-2.0 texts added.

**Checked**
- New test (round 103): six 101 x 57 PNG frames become an MP4 FFmpeg reads back as 6 frames of 102 x 58 with libx264;
  a missing frame is refused with a reason. Render Animation's test made its MP4 in-process. Both on Windows and Linux.
- Unit checks: Windows 9,965, 0 failed (667 tests); the whole suite in render-cache verify mode: 9,966,
  0 failed, 0 stale pictures. Linux with libraries: 9,873, 0 failed. Sanitizer build: 9,779, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). Fixed:
- CMake's Linux build wouldn't have linked: `libc_compat` sat in the static library, where nothing pulled it in before
  the codecs asked; it is now on every executable's link line.
- An odd frame size was stretched by a pixel (a soft, shifted picture): it is padded by repeating the edge now.
- The video wasn't tagged and used BT.601 colours: it is BT.709, limited range, tagged, as Blender writes it.
- Closing the editor while an MP4 was being encoded left the encoder running into the program's exit: it stops
  between frames and the editor waits for it.
- A failed or stopped encode left an unplayable half-MP4: it is deleted.
- x264's options went to whatever encoder was found: a fallback encoder gets a high bit rate instead.
- FFmpeg's log level was set from the worker thread: once, from the main thread.
- Only the five FFmpeg DLLs Blendity uses are shipped (not avdevice / avfilter); CMake no longer links an FFmpeg
  folder without its libraries; `extern/README.md` says where each copied file comes from.
- Left: the FFmpeg DLLs load with the program (as the other libraries' do), so a missing one stops it starting.

## 2026-10-14 (round 102): installers for Windows, Linux and macOS (task 0076)

**Asked:** installers for Windows, Linux and macOS; the choice was an Inno Setup .exe, an AppImage and a .tar.gz, and
an unsigned .dmg, attached to a GitHub Release on v* tags.

**Changed**
- **Windows:** `Blendity-Setup-<version>.exe` (Inno Setup, `packaging/windows/blendity.iss`): installs for the user
  alone (no administrator), Start menu and optional desktop shortcuts, `.scene` files opening in Blendity, an
  uninstaller in Settings > Apps. `build.bat installer` makes it locally when Inno Setup 6 is installed.
- **Linux:** `Blendity-<version>-x86_64.AppImage` and `blendity-<version>-linux-x86_64.tar.gz`
  (`packaging/linux/make_packages.sh`, built on Ubuntu 22.04 for older systems).
- **macOS:** `Blendity-<version>-macos.dmg` (`packaging/macos/make_dmg.sh`): the app with its icon and version and an
  Applications link; unsigned (right-click > Open the first time).
- `.github/workflows/release.yml` builds all three on pull requests that touch packaging, installs or mounts each and
  starts it headless, and on a v* tag makes the GitHub Release with them. `VERSION` is 0.10.0. No tag was pushed.
- An icon (a cube in the Emiliano palette) drawn by `packaging/make_icon.py` without any library.
- A `.scene` path given alone - a double-clicked file - opens, with the project folder above it.
- The debug OpenJPH DLL (`openjph.0.25d.dll`) is no longer copied into `dist\windows`.
- README: a Download section.
- Fixed: the test "feel: Unreal's End drops the selection..." failed twice on CI's sanitizer build (#61, #67): it gave
  Alt+J's camera turn a fixed 0.5 s, and a sanitizer frame can take most of that; it now waits for the turn to end.
  The auto-rig's view test (#68) and two others that waited the same way do too.

**Checked**
- New test (round 102): a lone `.scene` argument is the scene and its project is found above it; an option's value
  isn't.
- The Linux .tar.gz from the WSL build, unpacked elsewhere, starts headless and makes `~/BlendityProject`.
- Unit checks: Windows 9,953, 0 failed (666 tests); the whole suite in render-cache verify mode: 9,954,
  0 failed, 0 stale pictures. Linux with libraries: 9,861, 0 failed. Sanitizer build: 9,778, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.
- The release workflow's own run on this PR (Inno Setup isn't installed on this machine).
- A headless run writing to a path with an accented letter (UTF-8 arguments on Windows).

**Review** (Opus code-reviewer). Fixed:
- `build.bat release all` went on into the installer step (CI's Windows check would have failed without Inno Setup).
- The release job looked for the installers where the artifacts don't put them: no release would have been made.
- Installing "for all users" put the program in one user's folder: `{autopf}` follows the choice.
- A double-clicked scene with an accented letter in its path didn't open (Windows' ANSI argv): the arguments come from
  the wide command line as UTF-8. `--scene`'s value now finds its project too, and `.SCENE` in any case counts.
- The macOS note: on macOS 15 an unsigned app is allowed in System Settings > Privacy & Security > Open Anyway.
- The workflow's checks start each installed copy away from the repository (a fresh machine's project), wait for the
  Windows uninstaller and check that it removes the `.scene` association; rerunning the dmg script no longer nests
  the licences.

## 2026-10-14 (round 101): the laptop minimum - an i7, 8 GB, the HP Envy 13.3" 2-in-1 (task 0075, ADR 0026 amended)

**Asked:** a new minimum machine: an i7 with 8 GB of RAM, the HP Envy 2-in-1 13.3".

**Changed**
- The minimum is now that laptop (i7-1165G7, 4 cores / 8 threads, 8 GB shared with Iris Xe, 1080p at 150 %): README,
  MANUAL chapter 11, ADR 0026 and PERFORMANCE.md pass 9.
- **First run** on such a machine: Low, and a welcome that says so, with **Use High** one click away
  (`BLENDITY_MIN_SPEC=1` shows it on any machine).
- **Touch (Windows):** two fingers over the Scene view pinch to zoom, drag to pan, twist to turn round the pivot, in
  every editor feel; one finger is the mouse. **Pen:** its pressure scales the weight-paint brush.
- The Low tier's views read shadows with a 2 x 2 bilinear filter (4 taps instead of 9).
- An integrated GPU's path-tracing budget is a quarter of RAM (Iris Xe reports half as its own).
- `blendity_stress --laptop`: 5 threads (4 cores' 8 threads), the times to be multiplied by 1.4 for the laptop's clock.

**Checked**
- `editor_render` at Low with `--laptop` x 1.4: orbiting 13.0 ms (60 fps), moving an object 26.0, a material edit 24.9,
  Play 22.8 (30-40 fps; 60 while editing is **not met** on the laptop, and Low keeps 67 % rather than looking soft).
  On 8 full cores (pass 8's setup) the frames pass 8 left over 16.7 ms are now 12.9 and 11.4.
- New tests (round 101): a pinch of 2 halves the distance, a two-finger drag pans with the fingers, a twist turns the
  view by its angle, over the Inspector nothing, NaN ignored; a stroke at pressure 0.5 adds half; the first run on the
  minimum machine shows the welcome, which fits 1280 x 720, and Use High switches and is saved.
- The Preferences dialog checked at 1280 x 720 (a headless screenshot).
- Unit checks: Windows 9,948, 0 failed (665 tests); the whole suite in render-cache verify mode: 9,949,
  0 failed, 0 stale pictures. Linux with libraries: 9,856, 0 failed. Sanitizer build: 9,773, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). Fixed:
- `--laptop`'s message had a line break inside its string: the stress tool didn't compile.
- A pen hovering after lift-off left its pressure 0 for every later mouse stroke: only mouse messages Windows made from
  a pen carry it now, and lifting or leaving resets it.
- The first finger's press, when Windows makes one, moved a held drag to the gesture's middle: gestures leave the
  mouse position alone while a button is down.
- Windows' own gestures (begin, end, taps) went unanswered; the pointer calls are looked up so the program starts
  without them.
- The 2 x 2 filter's edge is narrower than 3 x 3's, not as soft: the comments and lesson say so.
- Left: the Low filter is a flag on the cached shadow map, safe while every render runs on the main thread (a note for
  any render moved off it).

## 2026-10-14 (round 100): render animations and sequences to video (task 0074)

**Asked:** rendering sequences to video; the choice was PNG frames plus an MP4 through FFmpeg.

**Changed**
- **Render Animation** (File menu, the Render window, Ctrl+F12, `renderanim [first last] [fps] | stop`): every frame
  of the scene's sequence - or, without one, the selected object's clip - through the F12 render, into
  `Renders/<scene>_animation_<time>/0001.png...`; the pose goes back afterwards.
- Then, when FFmpeg is installed (on the PATH, or `renderanim ffmpeg <path>`, kept with the preferences), an H.264 MP4
  is made beside the frames in the background. Blendity runs FFmpeg rather than linking it: no new dependency.
  Without it the frames are the result, and the log says how to make a video.
- A render stops cleanly when something ends what it poses through: Play, a save, an undo, another scene, another
  object selected for a clip. Camera Sequence and Render Animation don't run at once.
- Fixed: the IK test "in Play a Rotator swinging a foot target" failed once on a fast CI runner (Play runs on the
  clock); it now steps until the target has moved.

**Checked**
- New tests (round 100): 21 frames of a sequence written as 0001-0021.png at the render's size, the first and last
  different, the pose put back; FFmpeg's arguments and the Windows command-line quoting; the MP4 made where FFmpeg is on
  the PATH (not on this machine); a clip's render stops when another object is selected or its preview ends; a lone
  number is refused.
- Unit checks: Windows 9,935, 0 failed (662 tests); the whole suite in render-cache verify mode: 9,936,
  0 failed, 0 stale pictures. Linux with libraries: 9,843, 0 failed. Sanitizer build: 9,760, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). Fixed:
- The rasterizer's frames waited for mouse or key input in the real app (the editor slept between them).
- A save, an undo, Play or another scene mid-render left the rest of the frames unposed; another object selected for
  a clip was posed and left so. The render now stops with a message.
- Camera Sequence could start over a running Render Animation.
- FFmpeg read the terminal on Linux (`-nostdin`); its exit code was lost once SIGCHLD is ignored, and a failed encode
  with a file left behind counted as made.
- An empty scene sequence hid the selected clip; a frame rate of 0 or NaN from a file reached FFmpeg; a fractional end
  rendered one frame too many.
- The Render window's tooltip searched the PATH every frame; it is looked up when a render starts.
- Two renders in one second shared a folder; a failed PNG went on with a gap; a lone number or unknown word in
  `renderanim` started a render.
- Left: a PATH entry with non-ASCII characters on Windows isn't searched (set the path with `renderanim ffmpeg`).

## 2026-10-14 (round 99): themes - Dark, Light, Emiliano Light and Emiliano Dark (task 0073)

**Asked:** an editor palette / theme option using the user's Figma site ("look at the variables tab in figma").

**Changed**
- **Preferences > Theme** (console: `theme <name>`): Dark (Blendity's), Light, **Emiliano Light** and **Emiliano Dark**.
  The two Emiliano themes take the Figma file's Variables (Branding Guide, Light and Dark modes): paper F1EEE5 and
  charcoal 292927 swapped between the modes, amber D7882B / A65E1E for buttons and selection, mint 45E8A2, red E6533C,
  blue 4C76FF and amber FF9A00 as signal colours (focus, X / Y / Z, errors, warnings), stone, slate and silver for
  borders and dim text. The choice is kept with the preferences.
- Colours the windows wrote by hand (list stripes, ruler strips, row hover, keyframe diamonds, the auto-rig's figure)
  are now theme colours.

**Checked**
- New test (round 99): Emiliano Light and Dark give the site's colours, case aside; an unknown name is refused; the
  theme survives saving and loading the preferences.
- Headless screenshots of both Emiliano themes (Animation layout).
- Unit checks: Windows 9,919, 0 failed (660 tests); the whole suite in render-cache verify mode: 9,920,
  0 failed, 0 stale pictures. Linux with libraries: 9,827, 0 failed. Sanitizer build: 9,744, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). Fixed:
- In the light themes, tooltips, the F9 panel, the Hierarchy's scene row, every other Console line, the Learn sidebar,
  notes and questions, the Lighting and Render settings and the Project folder tree kept dark fills under dark text:
  they take theme colours now (tooltip, bar, pane, stripe, well, note, question, link), as do the scrollbars, menu
  separators, popup outlines, the windows' toolbars, the timeline playhead and the Edit Mode button's label.
- Emiliano Dark's borders and field outlines were the toolbar's colour; they show now. Its selected keys are amber.
- Emiliano Light's auto-rig figure and graphs sit on stone, not charcoal.
- `set_theme` and the Preferences combo share one name lookup (`ui::theme_index`).
- The Dark theme's Learn table rows, Sequencer details and curve keys moved by a shade or two (now theme colours).
- Left as they were: the Scene and Game views' and the UV editor's own drawing (they read over any picture).

## 2026-10-14 (round 98): a texture's Physical Size drives its materials' tiling (task 0072)

**Asked:** a texture's Physical Size should really adjust its tiling.

**Changed**
- Changing a procedural texture's **Physical Size** re-tiles every material of the scene using it (Tiling = 1 / size,
  in any of its maps), in the same undo step. A material tiled by hand keeps its Tiling.

**Checked**
- New test (round 98): Bricks from 2 m to 4 m takes a material from Tiling 0.5 to 0.25, and one using it only as a normal
  map; hand-tiled ones keep 3 and 1/3 (passed on the way); an unused `.mat` is left; undo and redo.
- Unit checks: Windows 9,913, 0 failed (659 tests); the whole suite in render-cache verify mode: 9,914,
  0 failed, 0 stale pictures. Linux with libraries: 9,821, 0 failed. Sanitizer build: 9,738, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). Fixed:
- A `.mat` asset no object used was re-tiled but undo couldn't put it back: only the scene's materials follow now.
- Only the colour map counted as using the texture: its normal, roughness, metallic and emission maps count too.
- A dragged Physical Size passing a hand-tiled material's size picked it up: which materials follow is decided at
  the first change of the undo step.
- Box mapping's z tiling follows too when it matched.
- The re-tile no longer scans every `.mat` file on each frame of a drag, nor logs each frame.
- New checks: a normal-map-only material, a hand-tiled one passed on the way, the unused `.mat`, redo.
- Left for its own task: redo of an edit to a material linked to a `.mat` (any field, not only tiling) keeps the
  undone values (older than this task).

## 2026-10-14 (round 97): drag stacks into order (task 0071)

**Asked:** drag-and-drop reordering of the camera filter stack, the modifier stack and the other stacks.

**Changed**
- Drag a **modifier's**, a **camera filter's** or a **texture layer's** name in its header to another place in its stack:
  a line shows where it lands and the item is outlined; letting go moves it, one undo step. A click without moving still
  folds the header; the menus' Move Up / Down stay.
- The **Sequencer's tracks** move the same way, by their names (clips go with them). Console: `seq trackmove <from> <to>`.

**Checked**
- New tests (round 97): Bevel dragged below Smooth in a Bevel / Array / Smooth stack ends last, undo puts it back in one
  step, a click still folds; a camera's first filter dragged below the second ends last; a Sequencer track dragged
  below another takes its clips, and `seq trackmove` moves it back.
- Unit checks: Windows 9,902, 0 failed (658 tests); the whole suite in render-cache verify mode: 9,903,
  0 failed, 0 stale pictures. Linux with libraries: 9,810, 0 failed. Sanitizer build: 9,727, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). Fixed:
- A drag whose list stopped showing (another object selected, the Sequencer emptied by an undo) stayed alive and moved
  an item the next time the list showed; it is now dropped.
- Undo, redo and Esc during a drag now drop it (the item under its old index could have been another one).
- The insert line could be drawn over the window above a scrolled Inspector; it is drawn inside the panel now.
- Drags that couldn't land (a track in Play, an unsaved texture's layer) no longer start.
- New checks: a filter dragged up, a drag whose list went away, Esc.
- Left as is: the line below the last item sits under its header, above its open settings.

## 2026-10-14 (round 95): a global sequencer - each clip names its object (task 0070, ADR 0030 amended)

**Asked:** "The sequencer should work globally and have two things to consider when adding a clip. Which animation clip
it is using and what object is it attributed to."

**Changed**
- The Sequencer window edits **the scene's sequence** (saved with the scene), with or without a selection.
- **Each bar is a clip and an object:** a dropped clip plays on the selected character; the Object field below the
  tracks takes an object dragged from the Hierarchy; bars read "Walk -> Robot". Console: `seq add <clip> <track>
  <start> [object]`, `seq object <clip> <object>`.
- Preview poses every bar's object and puts them all back. In Play the scene's sequence plays (triggers and `seq jump`
  too); an object's Animation component steps aside while the sequence has a bar on it.
- Overlapping bars crossfade only when they play on the same object.
- `.seq` files gain a 4th column (the object); older files load unchanged. The Sequencer component still plays a
  sequence on its own object.

**Checked**
- New test (round 95): one scene sequence moves two lids from two tracks in Preview (and puts them back), overlapping
  bars on different objects don't blend, the `.seq` and the scene file keep the objects and the sequence, and Play
  moves them.
- New test (round 96): three objects' bars overlapping on one track all play; an object given a bar during Preview is
  put back; renaming the `.seq` keeps the scene on it.
- Unit checks: Windows 9,885, 0 failed (656 tests); the whole suite in render-cache verify mode: 9,886,
  0 failed, 0 stale pictures. Linux with libraries: 9,793, 0 failed. Sanitizer build: 9,710, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.
- Stress `animation`: 201 tracks 0.0735 ms a frame (target 0.2 ms); a sequence of 4 tracks crossfading 201-track
  clips 0.3143 ms a frame (target 0.5 ms).

**Review** (Opus code-reviewer). Fixed:
- Two rigs could share a lookup key and pose each other's bones.
- Renaming the scene's `.seq` lost it.
- An object given a bar during Preview kept its previewed pose.
- A track with three overlapping objects dropped one.
- A jump before Play's first frame didn't find the bars' objects.
- `seq jump` couldn't reach a component's sequence.
- Sequences made before this task are editable again.

## 2026-10-14 (rounds 89-94): rigging - character roots, bone handles and extrude, rig controls, auto-rig colours and views, grouped keyframes (tasks 0065-0069, ADR 0031)

**Asked** (2026-10-14):
- "When adding an armature to a mesh/object nest it in the hierarchy itself; similar to how unity handle's humanoid
  models being imported."
- "Add the ability to move the base and tip of a bone ... add bones using extrude like vertices."
- "Give the inverse kinematics a special gizmo ... (also add the option to remove FK/IK systems) ... like blender and
  maya."
- "For auto-rig ... make them different colors to indicate different parts of the body ... transfer to different
  orthographic views."
- "Select multiple keyframes in the animation block (drag select) ... x,y,z in the same frame and then expand as
  needed."

**Added**
- **Character roots** (0065): GameObject > Armature on a mesh, Add Armature (Skin) and the auto-rig put the mesh and its
  Armature inside "<mesh> Rig" in the mesh's old place; the Animation window and the Sequencer animate that root.
- **Bone handles and Extrude** (0066): a selected bone's tail handle (and Shift+drag on its head) aims and stretches it,
  children staying put, the result its rest pose; Extrude (Ctrl+E / E) grows a bone from the tip that follows the mouse
  until a click (Esc cancels). Console `bone head|tail|extrude`.
- **Rig controls** (0067, ADR 0031): the Rig Control component (Circle, Square, Box, Diamond, Sphere, Arrows, Cross,
  Root) drawn as wire over the scene, coloured by side (left blue, right red, middle yellow). IK targets are boxes,
  poles diamonds with a dashed line to their joint; IK's bones are tinted. FK controls (circles that turn their bones).
  Remove IK, Add / Remove FK Controls; console `ik remove`, `fk add|remove`.
- **Auto-rig** (0068): markers coloured by body part; Front / Left / Right / Back / Top views (`autorig view`), a
  dragged marker keeping its depth away from the front view.
- **Keyframes** (0069): the dopesheet groups a vector's axes in one row that unfolds; a group diamond selects and moves
  all its axes' keys; box select takes every diamond it touches, lights them while dragging, Shift adds, Ctrl removes;
  dragged keys show where they will land.
- Learn: "A character's root", "Bone heads, tails and Extrude", "Rig controls: IK and FK", "Auto-rig colours and
  views".

**Checked**
- New tests (rounds 89-94): the rig root's shape and place (in a group and at the scene's top level), undo; the skin
  following the root; Add Armature (Skin) nesting and leaving a shared skeleton; tail and head drags (children kept, rest
  pose, one step, a half-scale armature); Extrude kept (one step) and cancelled (no step, no bone); IK controls' shapes
  and sides (including ".L" names), Remove IK (one step, a user's object kept); FK turning its bone, a click on its wire,
  Remove FK, FK refused on IK bones, Reset Pose with FK, an idle solve; marker colours; view switching; a side-view drag
  keeping depth; grouped rows, a group diamond's move, a thin box, Ctrl box.
- Unit checks: Windows 9,873, 0 failed (654 tests); the whole suite in render-cache verify mode: 9,874,
  0 failed, 0 stale pictures. Linux with libraries: 9,781, 0 failed. Sanitizer build: 9,698, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). Fixed:
- The bone handles were drawn before the picture and painted over.
- An extrude made an undo step per frame (and Esc left steps).
- Remove IK could delete a user's object used as a target.
- FK on an IK bone redrew the editor forever.
- Reset Pose was undone by FK controls next frame.
- The side and top views moved dragged markers to the wrong depth.
- A scaled armature's tail landed away from the mouse.
- Also: the head handle took the gizmo's centre (now Shift), roll drift and the grab offset, a stale bone drag across
  scenes, shortcuts and right-click fly during an extrude, the rig root's place at the scene's top level, wrapping an
  import's mesh again, any single-child parent taken as a root, Create Child > Armature's meaning, ".L" control
  names, a misspelt `fk remove` name, disabled controls drawn and picked, FK lookups by name every frame.

## 2026-10-14 (round 88): typed object and asset fields, Unity's "None (Armature)" (task 0064)

**Asked:** "Add the ability to drop objects from hierarchy into parameter fields in the inspector and not just rely on
the name of an object (and indicate type just like unity3d), like armature parameter into skin under Rigging … such as
armature, animation, sequencer etc."

**Added**
- **Object fields know their type** (`Reflector::object(name, value, type)`):
  - Shown as Unity's object field: the type's icon and "Armature (Armature)", or "None (Armature)" when empty; yellow
    when the object is missing or of the wrong kind.
  - Dragging an object of another kind over a field turns it red, and the drop is refused with a Console line.
  - The ⊙ picker at the field's right lists None and the scene's objects of that kind.
  - Clicking the field still types a name.
  - Typed fields:
    - Skin Armature (Armature)
    - Lattice Object (Lattice)
    - Curve and Array Curve (Curve)
    - Boolean and Mesh Deform Object (a mesh)
    - Transfer Weights From (a mesh)
    - IK Target and Pole take any object.
- **Asset fields** (`Reflector::asset(name, path, ext, kind)`):
  - The Animation's Clip (.anim) and the Sequencer's Sequence (.seq) take a file dragged from the Project window, and
    refuse another kind.
  - Their picker lists the project's files of that kind.
- Learn: "Typed fields and asset fields" in the Inspector lesson.

**Changed**
- A press on a file in the Project window that turns into a drag no longer selects the file, so the Inspector stays on
  the object whose field you're filling. A click selects it when the button comes up.

**Checked**
- New tests (round 88):
  - A Skin's Armature field refuses a Sphere and takes the Armature (one undo step), and shows "None (Armature)" then
    "Armature (Armature)".
  - The picker lists None and the armature but not the Sphere; picking None is an undo step of its own.
  - A .anim dragged from the Project window fills an Animation's Clip, a .seq is refused, the object stays selected,
    undo empties it, and a click on a file still selects it.
- Unit checks: Windows 9,795, 0 failed (643 tests); the whole suite in render-cache verify mode: 9,796,
  0 failed, 0 stale pictures. Linux with libraries: 9,703, 0 failed. Sanitizer build: 9,620, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). Fixed:
- A field's help tooltip (and the picker's) never showed: the picker had become the last widget.
- The asset picker walked the project's folders every frame while open; it now reads them once, as it opens.
- Each asset field checked the disk every frame; now once a second.
- A pick whose field wasn't drawn the next frame could apply much later.
- A fast press-move-release on a file selected it before seeing the drag.
- The missing type icon.

## 2026-10-13 (round 87): switch the editor's feel: Unity, Unreal, Blender or Maya (task 0063)

**Asked:** "Switch the editor's feel between Unity / Unreal / Blender / Maya". The user chose keys and mouse navigation
only.

**Added**
- **Editor Feel**: Edit > Editor Feel, Preferences > Keymap, or `feel unity|unreal|blender|maya`. It sets the keymap
  preset and the Scene view's mouse navigation together, and is kept in the prefs. Your own key changes stay on top.
- **Navigation by feel:**
  - Unity: as before.
  - Unreal: RMB look + WASD; Alt+LMB orbit; MMB, Alt+MMB or LMB+RMB (in either order) pan; Alt+RMB dolly.
    Letting go of LMB while RMB is still held looks round again.
  - Blender: MMB orbit, Shift+MMB pan, Ctrl+MMB zoom. Alt+click selects an edge loop and Ctrl+Alt+click a ring,
    without moving the view; RMB doesn't fly.
  - Maya: Alt+LMB tumble, Alt+MMB track, Alt+RMB dolly. Plain MMB and RMB don't move the view.
- **An Unreal keymap preset**: Unity's keys plus Alt+P Play, Ctrl+W duplicate, End drop to floor (the whole object
  with its children, onto y = 0), and Alt+G / H / J / K for the perspective, front, top and side views.
- New actions for any keymap: Perspective / Front / Top / Side View, Drop to Floor.
- Learn: "Work like Unreal, Blender or Maya" in the Edit Mode lesson. MANUAL chapter 12 has a table of the feels.

**Changed**
- Navigation drags end when the button that started them is let go (needed by MMB orbit and LMB+RMB pan).
- The test runner flushes after each test, so a crash shows which test was running.
- The keymap presets list Unreal second, so the Preferences dropdown test (round 79) now picks Unreal from the second
  row.

**Checked**
- New tests (round 87):
  - Each feel's mouse buttons with real drags: Blender MMB, Shift+MMB, Ctrl+MMB; Maya Alt+LMB, Alt+MMB, Alt+RMB.
  - Unreal RMB, and LMB then RMB.
  - Unity's whole set unchanged.
  - Each feel's keys; an unknown feel refused.
  - Blender Alt+click loop and Ctrl+Alt+click ring with the view still.
  - Unreal End on an object and on an empty with a child; Alt+J top view.
  - The feel kept in the prefs (an unknown one falls back to Unity); your own key change kept across a feel switch.
- Unit checks: Windows 9,774, 0 failed (641 tests); the whole suite in render-cache verify mode: 9,775,
  0 failed, 0 stale pictures. Linux with libraries: 9,682, 0 failed. Sanitizer build: 9,599, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). Fixed:
- Blender Alt+click loop select did nothing.
- Unreal LMB-then-RMB box-selected instead of panning.
- A feel switch wiped your own key changes.
- `feel` with no argument switched to Unity.
- Drop to Floor ignored children.
- Maya's plain MMB panned.
- The Learn answer about RMB, and the prefs test's path.

## 2026-10-13 (round 86): a Premiere-style clip sequencer instead of a state machine (task 0062, ADR 0030)

**Asked:** "A clip editor like Premiere Pro to go between animation clips instead of using a state machine"; for Play,
the user chose "a timeline plus triggers".

**Added**
- **Sequences**, `Assets/Animations/<name>.seq`:
  - Tracks of clip instances. Each instance has its clip, start, In and Out (trim), speed and name.
  - Two instances overlapping on a track crossfade over the overlap (Premiere's cross dissolve). Rotations blend the
    short way round.
  - A higher track's clip wins for the numbers it animates.
- **The Sequencer component** plays a sequence in Play (Loop or Once, Speed).
  - Its **triggers** jump to a named clip when a key is pressed (a letter, a digit, Space, F1-F12).
  - A jump blends from the pose of that moment over the trigger's seconds, and the timeline plays on from there.
  - An Animation component on the same object steps aside while a sequence plays.
- **Window > Sequencer** (beside the Animation window in the Animation layout):
  - Drag `.anim` clips from the Project window onto a track.
  - Drag a bar to move it (onto another track too) and its ends to trim it; Esc cancels.
  - Overlaps show the crossfade as a cross.
  - Below the tracks: the selected bar's Name, Speed, In and Out, and the trigger list.
  - Right-click a bar for Go To, Speed and Delete. Add Track.
  - Preview and Play, as in the Animation window. Every edit is one undo step.
- **Console:** `seq new | add | move | trim | speed | rename | remove | track | trigger | jump | frame | preview | play |
  stop`.
- **Renames follow:** renaming or moving a clip, a sequence or their folder keeps sequences, Sequencer components and
  undo steps pointing at them. That includes sequences not loaded.
- Learn: "Sequencing clips" in the Animation lesson. MANUAL chapter 9 has a walk/run example.

**Changed**
- Game keys (`PlayContext::key_down`) are not seen while a text field has the keyboard.
- The two Previews take turns: turning on one turns off the other.

**Checked**
- New tests (round 86):
  - Sampling: one clip trimmed and at speed; a crossfade; a higher track winning; a bar inside another; rotations the
    short way.
  - `.seq` round-trip, refusing bad lines, trigger key names.
  - A clip rename and a folder rename (with a sequence not loaded) carry the sequences.
  - In Play: a trigger's key jumps and blends from the pose of the moment. Once holds the last frame. An empty
    Sequencer leaves the Animation playing.
  - In the window: a drop places a clip, a drag moves a bar, its end trims it, each one undo step.
  - Preview restores the pose with no undo step. The two Previews take turns.
  - Huge or NaN track numbers are refused. Undo and redo of an edit.
- Stress `animation`: a sequence of 4 tracks, each two crossfading 201-track clips, costs 0.31 ms a frame (target 0.5).
  The first version took 1.30 ms (string keys and allocations per value). Values are now 64-bit keys in a reused
  table.
- Unit checks: Windows 9,741, 0 failed (638 tests); the whole suite in render-cache verify mode: 9,742,
  0 failed, 0 stale pictures. Linux with libraries: 9,649, 0 failed. Sanitizer build: 9,566, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). Fixed:
- The Animation window's Preview didn't put the Sequencer's away.
- A folder rename missed sequences not yet loaded (the listing was walked before the move).
- `seq add` with a huge track number made that many tracks.
- A bar inside another snapped back at its end.
- Once stopped a frame short.
- A click in an overlap took the bar underneath; short bars couldn't be trimmed.
- Editing In pinned Out.
- A drag outlived its window.
- Triggers fired while typing in the Console.
- An empty Sequencer silenced the Animation component.
- Dropping a clip elsewhere logged a warning.
- Preview could pose another object after a selection change.
- A bad `.seq` was read again every frame.
- The stress test's clips stayed loaded after their files went.

## 2026-10-13 (round 85): the animation curve editor, Unity's Curves view (task 0061, ADR 0029 amended)

**Asked:** "Start on the later list": first the curve editor for the keyframe animation of task 0059.

**Added**
- **Tangents on keys** (Unity's modes):
  - Clamped Auto (the old Auto), Free Smooth (one slope both sides), Flat (level) and Broken (each side its own).
  - Linear and Constant spans stay.
  - `.anim` files keep a key's mode and slopes; files from task 0059 load with Clamped Auto keys.
- **The Curves view**: the Animation window's Dopesheet | Curves buttons.
  - Each property is a line of its value over time (x red, y green, z blue) on a value axis with grid lines and
    numbers.
  - Click track names to choose the curves (Ctrl+click for several); F frames them; the wheel zooms the values.
- **Editing curves:**
  - Drag keys in time and value (Shift: one direction only); drag a box to select.
  - A selected key's handles set its slope: Free Smooth moves both, Broken each alone.
  - Right-click a key for the tangent and span modes, or Delete.
  - Double-click a curve to add a key that keeps its shape.
  - In Preview the scene follows the drag.
  - Each edit is one undo step.
- Console: `anim tangent <object|.> <property> <axis> <frame> auto|free|flat|broken [in] [out]`.
- Learn: "Curves and tangents" in the Animation lesson.

**Changed**
- The Animation window's code is split into the ruler, the rows and the new `curve_editor.cpp`.
- Moving and pasting keys keep their tangents.

**Checked**
- New tests (round 85):
  - Free Smooth, Broken and Flat slopes on both sides of a key, and the file round-trip (old files too).
  - In the Curves view, dragging a key raises it and dragging its handle sets a Free Smooth slope, each one undo step.
  - Two keys of one track moved onto each other's frames both survive.
  - 300 wheel steps in on a value of 100 stop at a hair and don't hang.
  - A clip switched mid-drag gets no edit.
  - `anim tangent` with slopes is one undo step; an unknown mode changes nothing; slopes are kept to +-1e6.
- Unit checks: Windows 9,694, 0 failed (632 tests); the whole suite in render-cache verify mode: 9,695,
  0 failed, 0 stale pictures. Linux with libraries: 9,602, 0 failed. Sanitizer build: 9,519, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures, stress `animation`: 201 tracks in 0.075 ms a frame (target 0.2).

**Review** (Opus code-reviewer). Fixed:
- Moving several keys of one track could lose keys (each was moved onto the next before it moved).
- The value grid could loop for ever when zoomed far in on a large value: whole steps, at most 200 lines, and the zoom
  is clamped.
- A drag could commit into another clip after a clip change, an undo or the window losing its object.
- The commit now uses the last preview's amounts; handles and picks are limited to the shown curves; the in-handle
  shows only where the span before is a curve.
- `anim tangent` with slopes was two undo steps.
- A double-clicked key now keeps the curve's shape.
- Slopes are clamped on load; a refused edit no longer shifts the selection.
- Ctrl+wheel time zoom, in the spec, was not built: moved to the spec's "As built".

**Found on Linux**: loading a key with tangents wrote through a wrong index under GCC (the sanitizer build stopped on it).
`set_key` returned `keys.insert(...) - keys.begin()`, and C++ leaves the order of the two sides open: GCC took `begin()`
before the insert moved the keys. Now in two statements.

## 2026-10-13 (round 84): a README for GitHub and one user manual (task 0060)

**Asked:** "Update the GitHub README and compress the tutorial docs into one document with examples and use cases",
building on the Learn tab.

**Added**
- **docs/MANUAL.md**: one manual by stage of the work, the layouts' order:
  - Getting around, reference and planning, blocking out, modeling, UVs, materials and textures, lighting and
    rendering, rigging and skinning, animation, files, performance, shortcuts and the console.
  - Each chapter has short examples to follow, its use cases, and the Learn lessons that go deeper.
  - It takes in the old README's feature list and the lighting guide.
- **Opening it:** Help > Open Manual and the `manual` console command open it (the project's copy, else GitHub's).
  The Learn tab's Welcome lesson has a "The manual" section.

**Changed**
- **README.md** is a short front page:
  - What Blendity is, the screenshots and a quick start.
  - What it does by stage, the minimum and recommended machines, building, and links to the manual and the developer
    documents.
  - The stress highlights live in PERFORMANCE.md.
- `docs/guides/lighting.md` points into the manual.
- On Windows, opening a web address keeps its slashes.
- `help` lists the rigging, auto-rig, animation and manual commands.
- CMake's unit-test run starts in the source folder, as the fixtures need.

**Checked**
- New test (round 84): the manual has its 13 chapters, the README and the lighting guide link it, and every lesson
  and lesson section it names is in the Learn tab under that lesson (with CRLF checkouts too).
- Unit checks: Windows 9,658, 0 failed (628 tests); the whole suite in render-cache verify mode: 9,659,
  0 failed, 0 stale pictures. Linux with libraries: 9,566, 0 failed. Sanitizer build: 9,483, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review**: the reviewer checked every menu path, field and number against the code. Fixed:
- Shapes are under GameObject > Shapes (Parametric).
- The lighting window is Window > Lighting.
- Reference Image faces the front (`refimage ... side` for the others).
- Rigging and Skinning are two layouts.
- Preferences are Edit > Preferences... (Quality, Keymap).
- The Bricks preset already has Edge Wear.
- Axis Top / Side / Bottom and High Start.
- Console: PlayStation (PS1).
- Retiming drags the keys, not the ruler.
- The GPU also bakes and runs realtime GI.
- Two lessons were cited under the wrong lesson.
- Old README sections were restored in the manual (origins, number expressions, Join / Follow / Shell..., Active
  Camera, camera sequences, Transparent, the lighting settings).
- The test's CRLF and working-folder handling.

## 2026-10-13 (round 83): keyframe animation with a dopesheet, Unity's way (task 0059, ADR 0029)

**Asked:** "A keyframe/animation system like Unity", "Dopesheet for now" (a Premiere-style clip sequencer later
instead of a state machine).

**Added**
- **Animation clips**, `Assets/Animations/<name>.anim`:
  - Tracks of numbers by path below the animated root: Position, Rotation (Euler degrees) and Scale, or any
    component's number (`Light.Intensity`).
  - Keys with Auto (Unity's Clamped Auto: smooth, no overshoot between two keys), Linear or Constant.
- **The Animation window** (Window > Animation; the Animation layout along the bottom):
  - Preview, Record, first / previous key / play / next key / last, the frame, the clip menu with Create New Clip,
    and Add Property.
  - The tracks, with a ruler, the playhead and key diamonds.
  - Click, Shift+click or box-select keys; drag them in time; Delete; Ctrl+C / Ctrl+V at the playhead; double-click
    to add; right-click for the interpolation.
- **Record**: gizmo, Inspector or console changes to the animated objects become keys at the playhead.
- **Preview**: scrubbing poses the scene without an undo step, and turning it off puts it back.
- **The Animation component** (Unity's legacy one) plays its clip in Play (Loop, Once, Ping Pong, Clamp Forever,
  Speed), before IK solves.
- Key edits are undo steps; renaming a clip keeps the components pointing at it.
- Console: `anim new|key|interp|frame|preview|record|play|stop`.
- Learn: "Keyframes and the dopesheet" in the Bones and skins lesson, with questions.

**Checked**
- New tests (round 83):
  - Sampling: Constant, Linear, and Auto through a peak without overshoot.
  - The file, with names holding spaces and a bad line skipped.
  - Wrapping, including a negative Speed.
  - Keys on a lid: scrubbing makes no undo step, and Preview off puts the lid back.
  - Record keys a move but not a rename or a new child, and one undo takes the keys back.
  - Preview leaves unrelated objects alone, and an undo afterwards doesn't bring the pose back.
  - In Play, a bone turns and the Blender tube's skin follows.
  - Dopesheet clicks: the ruler, dragging a key, Delete.
  - Renaming a clip, and undo after it.
  - Preview refused in Play; New Scene clears the window.
- Stress `animation`: 201 tracks in 0.074 ms a frame (target 0.2 ms).
- Unit checks: Windows 9,653, 0 failed (627 tests); the whole suite in render-cache verify mode: 9,654,
  0 failed, 0 stale pictures. Linux with libraries: 9,561, 0 failed. Sanitizer build: 9,478, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review (code reviewer)**: seven majors and several minor findings, fixed:
- Property names with spaces broke the file.
- Preview put back every object in the scene.
- The previewed pose leaked into undo.
- Preview survived Play and scene switches.
- Record keyed renames, new children and IK-driven bones.
- Clips weren't forgotten on delete or cleared on another project.
- A rename didn't reach undo states.
- Also fixed:
  - Bad numbers in a file.
  - A negative Speed.
  - Missing clips read every frame.
  - The clip list walked every 2 s.
  - Pasting into another clip.
  - Moved keys' interpolation.
  - Fractional record frames.
  - Key edits in Play.
  - Console objects outside the root.
  - Unsafe clip file names.
  - Euler flips on record.
  - No dopesheet scroll.
  - Nesting.

## 2026-10-13 (round 82): the marker auto-rig, humanoid and quadruped (task 0058, ADR 0028 part 2)

**Asked:** "A Mixamo-styled automatic weight paint ... I want the markers and inpainted weights, but I want support for
quadruped auto rigging as well."

**Added**
- **Rigging > Auto-Rig**: Humanoid or Quadruped, with the character's mesh selected.
  - The Scene view turns to an orthographic front view (an animal: its left side).
  - A diagram shows the markers, the next one in orange.
- **Placing**: each click puts the next marker inside the body, halfway through the first part under the mouse.
  - Symmetry mirrors left and right.
  - Drag a marker to move it; Remove Last takes one back.
- **Markers**:
  - A person (Mixamo's): chin, wrists, elbows, knees, groin.
  - An animal: nose, base of the skull, withers, hips, base and tip of the tail, paws, front knees, hind hocks.
- **Build Rig**:
  - The skeleton with the presets' bone names (an animal also gets a three-bone tail).
  - IK on every limb, the poles following each limb's own bend.
  - The mesh bound with inpainted weights.
  - Its markers stay for another try.
- Console: `autorig humanoid|quadruped`, `autorig mark <marker> <x> <y> <z>`, `autorig symmetry on|off`,
  `autorig build`, `autorig cancel`.
- Learn: "Auto-rig with markers" in the Bones and skins lesson, with questions.

**Checked**
- New tests (round 82):
  - A 1.7 m mannequin's and a dog's markers give their bones within 2 % of the height.
  - Every vertex weighted.
  - Bending the forearm moves the hand (0.31) not the hips; lifting a front paw moves it (0.30) not the head.
  - Clicks in the front view land mid-depth (z = 0.000) and mirror.
  - Bent limbs stay as placed (bones at rest within 6e-8).
  - Markers on the wrong side, and a dog facing -Z, keep their sides.
  - Upside-down markers and unknown names are refused.
  - New Scene ends a session.
  - The background bind, its undo, and markers kept for a rerun.
- Unit checks: Windows 9,613, 0 failed (619 tests); the whole suite in render-cache verify mode: 9,614,
  0 failed, 0 stale pictures. Linux with libraries: 9,521, 0 failed. Sanitizer build: 9,438, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review (code reviewer)**: three majors and several minor findings, fixed:
- Build Rig turned limbs that weren't bent the poles' fixed way.
- The mesh's move gizmo took marker clicks.
- A session outlived New Scene, Open Scene and Play (a new scene's object ids could point it at another object).
- Also fixed:
  - Duplicate crossings at shared edges.
  - Weight Paint and Edit Mode during a session.
  - Double-clicks framing the view.
  - Esc taken from G / R / S.
  - Build while a solve ran.
  - An animal facing -Z.
  - Upside-down markers.
  - A wide stance.
  - Unknown marker names.

## 2026-10-13 (round 81): automatic weights and weight transfer by inpainting (task 0057, ADR 0028)

**Asked:** "A Mixamo-styled automatic weight paint ... based on Epic's Robust Skin Weights Transfer via Weight
Inpainting", with transfer from a body onto clothing.

**Added**
- **Bind: Automatic Weights** now inpaints:
  - A vertex clearly nearest one bone (under 0.8 of the next one's distance) that the bone can see (nothing of the
    mesh in between) takes that bone whole.
  - The rest get the smoothest weights that agree with those: one sparse solve per bone of `Q = -L + L M^-1 L` (the
    cotangent Laplacian and Voronoi areas), by Eigen's LDLT or conjugate gradients.
  - A hand hanging by a thigh, or below its own arm bone, takes no leg weight.
  - A bone no vertex is nearest (a tip bone past the end of a column) takes the vertices nearest it, so posing it still
    moves that end (round 69's Armature test).
  - **Bind: Envelope** keeps the old distance weights.
- **Transfer Weights** (Rigging window, `skin transfer <body> [mesh]`): a rigged body's weights onto clothing.
  - Matches within 5 % of the clothing's size with normals within 30 degrees (or opposite) copy; the rest are
    inpainted.
  - An **InpaintMask** vertex group forces places to be inpainted.
  - The clothing gets the body's Armature (Skin) modifier.
  - The body is an object field: drag it from the Hierarchy.
- **Large meshes (5,000+ vertices) solve on a worker thread**, in a job arena of their own, with a progress bar and
  Cancel. They land as one undo step; undo, Play or another scene cancels them.
- Console: `skin bind [object] [inpaint|envelope]`, `skin transfer <source> [target]`, `skin cancel`.
- Learn: "Automatic weights by inpainting" and "Weights onto clothing" in the Bones and skins lesson, with questions.

**Checked**
- New tests (round 81):
  - The Laplacian is symmetric, its rows sum to 0, and the masses add up to the area.
  - Inpainting between known ends is a monotone partition of unity.
  - A transfer onto a copy (renumbered and 2 mm out) gives back the weights within 1e-4.
  - The paper's loose sleeve takes no torso weight (plain closest point gives it 1.0).
  - InpaintMask vertices are inpainted, and a source's mask isn't carried.
  - A hand by the thigh takes no leg weight, whether by the ratio test or by visibility.
  - Short spine bones in a wide torso each get a band and blend (largest step between rings 0.14).
  - A NaN vertex, a wire edge and a zero-area face still give a partition, by the direct solver.
  - `skin bind` at once and on a worker thread, each one undo step; cancel, undo during a solve and closing the
    editor mid-solve.
  - Transfer by console and by dragging the body onto the Rigging window's field; the sleeve then bends with the arm.
- Stress `skin_weights` (8 threads, Eigen): 50k vertices in 108 ms (automatic) and 31 ms (transfer), against the
  2 s target. The editor's slowest frame while it solves 50k in the background: 1.4 ms.
- Unit checks: Windows 9,576, 0 failed (613 tests); the whole suite in render-cache verify mode: 9,577,
  0 failed, 0 stale pictures. Linux with libraries: 9,484, 0 failed. Sanitizer build: 9,401, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures, stress skin_weights: 50k vertices in 108 ms on 8 threads.

**Review (code reviewer)**: five majors and several minor findings, fixed:
- Short bones got hard bands.
- A wire edge or NaN vertex made the solve singular and dropped it to conjugate gradients.
- A transfer normalised over InpaintMask and non-bone groups.
- A large mesh's Add Armature (Skin) had no undo step.
- The background solve shared the job pool with the editor's frames.
- Also fixed:
  - Undo or Play during a solve.
  - Vertices moved during a solve.
  - No frames while a solve ran with the window closed.
  - The Inspector's bind button.
  - Unconverged solves went unreported.
  - The solve depended on the mesh's scale.
  - Exceptions in the worker.
  - Copying render caches.
  - Nesting.
  - Tests that missed the closest-point path and the visibility rule.

## 2026-10-13 (round 80): flatter code after the review on PR #50

**Asked:** "Can you read feedback in my GitHub and then make appropriate changes?" The one review (PR #50, task 0053):
"Be careful of nested if statements. Use methods as noted by @CodeAesthetic 'Why You Shouldn't Nest Your Code'."

**Changed** (extraction and guard clauses):
- **`MultiSelect`**: `click` is three lines over `add`, `toggle` and `range_to`. New `sync` and `select_all` replace
  blocks each of the three windows repeated.
- **One delete question** (`ask_delete` with a `DeleteQuestion`) replaces six copies: one and several, in the
  Materials, Textures and Project windows. One context-menu item (`delete_menu_item`) and two key checks
  (`select_all_key`, `delete_key`) replace the nested `if`s around them.
- **Helpers out of nested loops:** `slots_using`, `select_objects_using`, `materials_using_texture`,
  `edit_material_asset`, `project_keys`, `project_row_pressed`, `request_*_delete`.
- **`matdelete` / `texdelete`** moved out of the console's command chain into `delete_assets_command`, with
  `material_by_name`, `texture_by_name` and a shared `split_list` replacing nested lookup loops.
- Small differences:
  - Clicking away from the Materials window's "Delete the material ...?" now cancels it, as the other windows'
    questions did.
  - Ctrl+A and Delete over the Materials and Textures windows wait while a text field is being edited.
  - `matdelete` with two assets of one name deletes the first found (it took the last).
- The rule now applies to new code too (task 0057's solver is written as small functions).

**Checked**
- New tests (round 80):
  - `matdelete` with names holding spaces and an unknown one; `split_list` trims and drops empty parts.
  - The multi-selection: Ctrl+Shift with the last click gone, `sync` with hidden and edited items, Ctrl+A.
- Reviewed by the code reviewer twice (no behaviour bugs; its remaining nesting spots were flattened too).
- Unit checks: Windows 9,523, 0 failed (599 tests); the whole suite in render-cache verify mode: 9,524,
  0 failed, 0 stale pictures. Linux with libraries: 9,431, 0 failed. Sanitizer build: 9,349, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

## 2026-10-13 (round 79): drag objects onto fields; the Layout dropdown crash; a flaky CI check

**Asked:** "Some checks fail, check those out." "Clicking the drop down for the layouts crashes the program." "Also
add the ability to drag and drop objects from the hierarchy into the parameter field rather than just typing the name
(applies to all tools)."

**Fixed**
- **Clicking the Layout dropdown crashed the editor.** A dropdown's list draws at the end of the frame, after the
  Layout dropdown's per-frame list of names was gone. Every dropdown now keeps its own copy of its names while open.
- **CI (Windows) failed on PRs #49 and #50** at "min spec: a thread ceiling holds ...": on a 4-core runner every
  thread had to join one short loop. The check now takes the best of a few tries; the other Windows run of each PR
  passed, and the failed jobs were re-run.

**Added**
- **Object fields** (Unity's `ObjectField`): every field that names another object (Boolean, Lattice, Curve and Mesh
  Deform's Object, Array's Fit Curve, IK Target and Pole, a skin's Armature) takes an object dragged from the
  Hierarchy. The field lights up under it, the drop is one undo step, and the Console warns when two objects share
  the name.
- **Pressing a Hierarchy row selects it when the button comes up**, so dragging a row onto a field keeps the
  Inspector on the object you're editing (Unity selects on press, so its users lock the Inspector first). A click,
  a wobbly click, a reparent or a drop in the Scene view still selects it.
- Learn: "Object fields: drag, don't type" in the GameObjects lesson.

**Checked**
- New tests (round 79):
  - Opening the Layout dropdown and picking UV Unwrapping by mouse, from a named layout and from a custom one.
  - Dragging the Cylinder's row onto a Boolean's Object field fills it, keeps the Cube in the Inspector, and undoes
    in one step; a click still selects.
  - A click that slides 8 px still selects; a row dropped in the Scene view ends up selected; a press right after a
    drop finds no drag still live.
- Unit checks: Windows 9,511, 0 failed (597 tests); the whole suite in render-cache verify mode: 9,512,
  0 failed, 0 stale pictures. Linux with libraries: 9,419, 0 failed. Sanitizer build: 9,337, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review (code reviewer)**: two majors and several minor findings, fixed:
- A field scrolled out of view or under a popup took the drop.
- The drag state could outlive the button and drop again on the next click.
- Also fixed:
  - A wobbly click no longer selected, and a row dropped in the Scene view stayed unselected.
  - The lesson said Unity selects on release (it selects on press).
  - Two objects with one name went unreported.
  - A field being typed in wrote its old text back over a drop.
  - Nested reflectors didn't pass object fields on.

## 2026-10-13 (round 78): bind poses on rigged imports (task 0056, ADR 0025 amended)

**Asked:** "Yes, please follow Blender, Unity, and Maya methodology" (about rigged files saved in a pose).

**Fixed**
- **A rigged glTF or FBX saved mid-pose** imported with that pose as its rest, so the skin was wrong and Reset Pose
  did nothing. Each bone's rest is now its **bind pose**, as Blender, Unity and Maya read it: glTF's inverse bind
  matrices, FBX's cluster bind matrix. The pose in the file stays on the bones, and Reset Pose gives the shape the
  mesh was bound in.
- Files saved at rest import exactly as before. A bind that can't be used (missing, zero, non-finite, under a parent
  scaled to 0) falls back to the saved transform.
- The Console's "saved in a pose other than its bind pose" warning is gone. It now warns when two meshes bound a bone
  in different poses, or an FBX mesh moved after binding.
- Learn: the Bones and skins lesson says how posed files import.
- Tests: round 75's materials test left its MA..MD materials in the scratch project, so they piled up run after run
  until MC scrolled out of the window and the test failed (and crashed when run alone). It now clears them first.

**Checked**
- New tests (round 78), with fixtures exported from Blender 5.2 with the forearm bent 60 degrees:
  - The posed glTF and FBX import bent 60 degrees and match Blender's posed mesh (FBX within 9e-7).
  - `armature reset` gives the file's mesh and puts the forearm back above the upper arm.
  - Rigs saved at rest keep each bone's saved transform exactly.
  - Unusable binds leave the bone at its saved transform.
- Unit checks: Windows 9,493, 0 failed (594 tests); the whole suite in render-cache verify mode: 9,494,
  0 failed, 0 stale pictures. Linux with libraries: 9,401, 0 failed. Sanitizer build: 9,319, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review (code reviewer)**: one major and three minor findings, fixed:
- An FBX cluster without a bind matrix (ufbx leaves zeros) was taken as a bind, which froze its vertices.
- A parent scaled to 0 made a bone's rest its absolute bind.
- Two meshes bound in different poses, and a mesh moved after binding, went unreported.
- A rig saved at rest got float-rounded rests instead of the saved ones.

## 2026-10-13 (round 77): layouts for each stage of the work (task 0055, ADR 0027)

**Asked:** "Add different layouts that focus on different sections of the 3D modeling process such as:
Reference/Planning, Blocking Out, Modeling and Topology, UV Unwrapping, Texturing and materials, and then lighting and
Rendering; of course the generic editor too ... I want to avoid that feeling of so many tools ... separate layouts for
rigging, skinning and weight painting, animation".

**Added**
- **Stage layouts** in the Layout dropdown (top right, Unity's place) and Window > Layouts: Reference & Planning,
  Blocking Out, Modeling & Topology, UV Unwrapping, Texturing & Materials, Lighting & Rendering, Rigging, Skinning &
  Weight Painting, Animation.
- **Each layout arranges the windows** for its stage, for example the UV Editor beside the Scene view for UVs, or the
  Materials and Textures windows for texturing.
- **Each layout shows only its stage's tools** in the Modeling Tools window, the GameObject menu and Add Component.
  The keys and console reach the rest; Show All Tools shows them.
- **Save Layout... / Delete Layout...** for your own layouts, kept in preferences.
- **GameObject > Reference Image...**: an unlit plane with the image, facing front, side or top. It casts no shadow and
  stays out of renders; an image from outside the project is copied in.
- Console: `layout <name>|save <name>|delete <name>`, `alltools on|off`, `refimage <path> [front|side|top]`.
- Learn: "Layouts for each stage" in The Editor Layout lesson.

**Checked**
- New tests (round 77):
  - The stage layouts open their windows and show their groups.
  - Blocking Out's Tools window offers Show All Tools, and a click brings the Edit Mode section back.
  - The console reaches a hidden Light.
  - Save Layout keeps the windows and focus; built-in names are refused; Delete removes the layout.
  - A lesson's layout button arranges the windows.
  - Preferences bring back the layout and your own after a restart.
  - A Reference Image is unlit, 2:1 for a 200 x 100 picture, faces up for "top" and +X for "side", out of renders.
- Unit checks: Windows 9,470, 0 failed (591 tests); the whole suite in render-cache verify mode: 9,471,
  0 failed, 0 stale pictures. Linux with libraries: 9,378, 0 failed. Sanitizer build: 9,296, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review (code reviewer)**: two majors and several minor findings, fixed:
- A lesson's layout button (and `--layout`) changed the tools but not the windows; one table now serves both.
- Tooltips of hidden GameObject menu items landed on others.
- Also fixed:
  - The dropdown couldn't get back to a layout from a custom arrangement.
  - An unknown `layout` name went by silently.
  - `refimage` split paths at spaces.
  - Images outside the project were named by absolute path.
  - Duplicate saved layouts weren't merged.
  - The Tools window couldn't turn Show All Tools off.

## 2026-10-13 (round 76): curve and lattice points with the vertex and edge keys (task 0054)

**Asked:** "Add the ability to add more points and loop cuts for lattice and curves using similar key binds for
vertices and edges."

**Added**
- **Curves**, with a curve's points selected and outside Edit Mode:
  - **Extrude** (Ctrl+E; Blender's E) grows the curve from a selected end.
  - **Loop Cut** (Ctrl+R) puts a point on the segment under the mouse, where the mouse is.
  - **Merge at Center** (Alt+M; Blender's M) joins the selected points.
  - **Delete** removes the selected points, keeping two.
  - **Alt+C** opens or closes the curve.
- **Lattices**: Loop Cut over an edge adds a row across it, keeping the shape. Delete with a whole row selected
  removes one.
- Each is one undo step.
- Console: `points extrude|cut|merge|close|lattice ...`.
- Learn: "More points, by the vertex and edge keys" in the Modifiers lesson.

**Checked**
- New tests (round 76):
  - By keys, Extrude puts the new point beyond the last in the curve's direction and selects it; undo is one step.
  - In Blender's keymap, E grows the first point backwards.
  - Ctrl+R puts the point on the old path (0.004 off).
  - On a Poly curve, Ctrl+R puts it where the mouse was, away from the corners.
  - Merge leaves one point at the middle.
  - Delete keeps two points; Alt+C toggles Closed.
  - A lattice's U row added by Ctrl+R leaves the deformation unchanged (0 off), and Delete of a full row takes it
    away again; undo brings it back.
  - A merge of bad indices from the console is refused.
- Unit checks: Windows 9,444, 0 failed (587 tests); the whole suite in render-cache verify mode: 9,445,
  0 failed, 0 stale pictures. Linux with libraries: 9,352, 0 failed. Sanitizer build: 9,270, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review (code reviewer)**: two majors and several minor findings, fixed:
- Ctrl+R put the new point on top of the segment's first point (always on a Poly curve).
- A console merge with an index out of range could crash.
- Also fixed:
  - The closing span of a closed curve couldn't be cut by the mouse.
  - Finding the points' samples could take billions of steps on the biggest curves.
  - Delete in the Hierarchy stopped deleting a lattice with points selected.
  - The two-point rule now holds on every Delete path.
  - Extrude at the point limit said nothing.

## 2026-10-13 (round 75): many assets at once (task 0053)

**Asked:** "I want to be able to multiselect textures and materials in their tabs so I can en masse delete assets."

**Added**
- **Multi-select in the Materials, Textures and Project windows.** Ctrl+click toggles, Shift+click selects a range
  (Ctrl+Shift adds it), Ctrl+A selects everything shown (a search narrows it).
- **One question for many.** Delete, the toolbar or the context menu asks once ("Delete 3 materials?") and sends them
  all to the Recycle Bin / Trash. The Materials window empties their slots in one undo step.
- Console: `matdelete A, B` and `texdelete A, B`, with names separated by commas.
- Learn: "Many at once" in the Assets lesson.

**Checked**
- New tests (round 75):
  - By clicks: Ctrl, Shift and Ctrl+Shift select exactly the named materials.
  - Delete asks once and deletes three of four. The slot that used one shows the default, and one undo brings it
    back.
  - Ctrl+A in the Textures window takes every texture shown, or only the search's.
  - The console deletes two textures by name.
  - The Project window's Ctrl+click selects two files and Delete removes both after one question.
- The user accepted task 0050's two frames over 16.7 ms (material edit 19.8, Play 17.6).
- Unit checks: Windows 9,422, 0 failed (583 tests); the whole suite in render-cache verify mode: 9,423,
  0 failed, 0 stale pictures. Linux with libraries: 9,330, 0 failed. Sanitizer build: 9,248, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review (code reviewer)**: two majors and several minor findings, fixed:
- The Project window's context-menu Delete ignored the multi-selection.
- The console couldn't name assets with spaces.
- Also fixed:
  - Items hidden by a search were still deleted.
  - Two delete questions could fight.
  - Ctrl+A could fall through to the scene's Select All.
  - Shift with the anchor gone selected one item.
  - The Learn text said Project-window deletes empty slots (they keep the material).

## 2026-10-13 (round 74): the Rigging window (task 0052)

**Asked:** "rigging and weight painting tools should be in their own tab. The mesh filter area is getting a little
crowded from the amount of modeling operations we have."

**Added**
- **Window > Rigging**, opened beside the Inspector, with these sections:
  - **Skeleton**: Add Armature, Add Bone, Humanoid / Quadruped Rig, Reset Pose, Apply as Rest, and the armature's
    settings.
  - **Skin**: Add Armature (Skin) to the selected mesh for the selected skeleton, Bind: Automatic Weights, the
    skin's settings.
  - **Vertex Groups.**
  - **Weight Paint**: the toggle and the brush.
  - **Inverse Kinematics**: Add IK to Bone (makes its target and pole), Auto IK, the IK's settings.
- **Selecting a bone while painting** (in the Hierarchy) paints the group named after it, as Blender does.
- Console: `ik add [bone]` makes the target and pole.
- **Learn** (the user: "I really like the Learn feature ... feel free to use this system further"):
  - "The Rigging window" in the Modifiers lesson.
  - "Quality tiers: a small machine" in Performance and Stress Testing.
  - "A small graphics card" in Rendering on the GPU.
  - Each has Try it buttons and questions.

**Changed**
- The Inspector's MeshFilter section keeps modeling; one row ("Vertex Groups, Weight Paint: Rigging...") opens the
  window.

**Checked**
- New tests (round 74), by clicks:
  - Add Bone puts a child at the tip.
  - Reset Pose and Apply as Rest work.
  - Add IK to Bone makes a target and a pole, and moving the target poses the bones.
  - The MeshFilter row opens the window with the groups.
  - With a mesh and a bone selected, Add Armature (Skin) binds the mesh.
  - The window's Weight Paint paints a stroke.
  - Selecting bones while painting switches groups.
  - A layout with the Rigging tab loads back.
  - The new lessons' Try it commands make a humanoid and open the window, switch the tier and threads, and set the device to Auto.
- Unit checks: Windows 9,384, 0 failed (580 tests); the whole suite in render-cache verify mode: 9,377,
  0 failed, 0 stale pictures. Linux with libraries: 9,284, 0 failed. Sanitizer build: 9,202, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review (code reviewer)**: three majors and several minor findings, fixed:
- Add Armature (Skin) could almost never be clicked; it now looks through the whole selection.
- Fields shared UI ids: the IK's Weight and the brush's Weight. The window, each component and the brush have their
  own ids now.
- The buttons went by name, so two characters' identical bones or a name with a space went wrong. They act on the
  objects now.
- Also fixed:
  - A failed bind left a Skin with no undo step.
  - A bone selected mid-stroke was lost.
  - Add IK on an arm put the pole in front.
  - The test hook kept old button rectangles.

## 2026-10-13 (round 73): the minimum machine's GPU (task 0051)

**Asked:** the same request: "at minimum run on a GTX 1050ti".

**Added**
- **Render > Device: GPU + CPU (Auto).** Both render, each taking samples as fast as it finishes them.

**Changed**
- **GPU submissions are at most about 100 ms** (were up to 64 samples at once), so a slow card can't pass Windows'
  2 s driver timeout.
- **A scene too big for a card's memory** goes with smaller textures (a mip level at a time, never under 256), or
  to the CPU when even that won't fit, instead of failing to allocate. GPU bakes do the same.
  `BLENDITY_GPU_VRAM_MB` emulates a smaller card.

**Checked**
- New tests (round 73):
  - A 2048 texture on a 12 MB card arrives at 1024.
  - A frame whose buffers alone pass the card's memory goes to the CPU.
  - Device 2 is saved and renders on the CPU and the GPUs.
- Stress `gpu`: the longest submission is 98 ms (3060 Ti, compute kernel) and 106 ms (4070 SUPER). At 8 threads,
  the GPU-viewport spike is "go" only for ray-tracing cards; the minimum machine keeps the CPU viewport.
- Unit checks: Windows 9,349, 0 failed (574 tests); the whole suite in render-cache verify mode: 9,350,
  0 failed, 0 stale pictures. Linux with libraries: 9,257, 0 failed. Sanitizer build: 9,175, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review (code reviewer)**: two majors and several minor findings, fixed:
- The memory check used the previous frame size, because the scene is sent before the camera sets the size. It is
  checked again when the frame size changes.
- A scene that didn't fit threw its GPUs away and was built up to seven times on every pass. The GPUs are now set
  aside, open, for the next scene, and the texture sizes per level are worked out instead of built.
- Also fixed:
  - Bakes counted the render's frame in their budget.
  - The longest-submission figure never reset.
  - A card reporting no memory restarted the minimum.
  - The CPU row in Auto could be unticked to no effect.
  - `device auto` was missing from the console.
- Tests added: the fallback path through `build` then `set_camera` with a 16 MB card, and the GPUs coming back for a
  scene that fits.

## 2026-10-13 (round 72): the minimum machine, CPU and memory (task 0050)

**Asked:** "Look for more optimizations project wide to at minimum run on a GTX 1050ti and at least a quad-core
processor with eight threads running at a modest 3 Ghz and eight gigabytes of ram" (performance first).

**Added**
- **Quality tiers** (Preferences > Performance > Quality tier; console `quality low|medium|high|custom`): Low draws
  the Scene and Game views at 67 % and scales them up, with a 1024 shadow map, lighter Realtime GI and 60 fps;
  Medium is 85 % with a 2048 shadow map; High has no caps. They hold in the editor's views only, never in F12
  renders. The first run picks Low on a machine with 8 threads or fewer, or about 8 GB or less.
- **Viewport Resolution** (`viewscale 50..100`) and **CPU Threads** (`threads N`) in Preferences > Performance;
  `BLENDITY_THREADS` and `blendity_stress --threads N | --min-spec | --quality` emulate a smaller machine.
- **Cached shadows**: objects that stay still keep a shadow map of their own while another one is dragged.

**Changed**
- **The sky is drawn after the surfaces**, only where none landed, and only every 4th pixel where it is smooth. Its
  lookups use polynomial atan2 / asin. Pictures differ by at most one display step.
- **The views' shadow frame is snapped** (steps of 10 %), so texels can be up to about 1.2 times larger. F12 still
  fits exactly.
- **A material colour edit no longer redraws the shadow map.**
- **Jolt uses half of Blendity's threads.**
- **Memory budgets**: the texture cache drops its least recently used unheld textures past a quarter of RAM, and
  undo drops its oldest steps past an eighth (each at most 2 GB).

**Checked**
- New tests (round 72):
  - A ceiling of 3 holds a loop to 3 threads with TBB and with the built-in pool after its workers started.
  - At 50 % the Scene view draws 438 x 205 of 876 x 409, a click still selects the cube, and the scale is in the
    picture keys.
  - The tiers set scale, shadow map and fps; F12 keeps the scene's 2048.
  - Two tiers at the same scale but different caps are different pictures.
  - A moving caster drawn over the still casters' map equals the full map bit for bit. Six moved frames draw the
    still layer once.
  - The deferred sky matches a per-pixel evaluation within 1 display step.
  - The polynomial atan2 / asin are within 4e-7 of the library's.
  - The texture cache lets go of unheld textures past its budget and keeps a held one.
  - Undo keeps its newest steps within its budget, and they undo.
- Stress at 8 threads, Low, fastest of four runs:

  | Frame | Before (ms) | After (ms) |
  |---|---|---|
  | Orbit | 17.9 | 7.1 |
  | Moving an object | 47.2 | 15.5 |
  | Material colour edit | 48.1 | 19.8 |
  | Play | 39.3 | 17.6 |

  The material edit and Play are still over 16.7 ms; PERFORMANCE.md pass 8 says what is next. Peak working set with
  10,200 objects: 448 MB.
- Unit checks: Windows 9,336, 0 failed (570 tests); the whole suite in render-cache verify mode: 9,337,
  0 failed, 0 stale pictures. Linux with libraries: 9,246, 0 failed. Sanitizer build: 9,164, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review (code reviewer)**: five majors and several minor findings, fixed:
- The thread ceiling didn't hold in the dependency-free pool once its workers had started.
- The tiers' shadow and GI caps weren't in the views' picture keys, so a tier change could show a stale picture.
- F12 took the tiers' GI caps.
- Preferences from before the tiers lost the user's frame cap.
- Retro filter stacks were scaled twice and came out soft.
- Also fixed:
  - Edit Mode and modifier levels kept an object "moving" (motion is by placement now, with a still layer per view
    slot).
  - Snapping was coarse (now steps of 10 %, and none for F12).
  - A pending sky could outlive its frame.
  - A NaN depth could stall the sky fill.
  - The TBB arena could be freed while in use.

## 2026-10-12 (round 71): the macOS check, and merging the stack (task 0049)

**Asked:** "check for any errors on the GitHub side ... and merge them".

**Fixed**
- macOS CI failed on #45 and #46: a test compared the skinned mesh with the base mesh bit for bit after Apply as
  Rest; Apple clang's fused multiply-adds left one vertex one ulp off. The test compares within 1e-5 now (the
  editor was right).

**Changed**
- CLAUDE.md: agents merge only when the user asks, after CI passes, with a merge commit.

**Checked**
- The test on Windows; CI's four jobs on #46 before merging.

## 2026-10-12 (round 70): inverse kinematics and rig presets (task 0048)

**Asked:** the roadmap (2026-10-11): "IK/FK for humanoid and quadruped rigs", with name-based humanoid auto-map and
the gizmo refused on an IK-driven bone.

**Added**
- **IK constraints** (ADR 0025): an IKConstraint on a chain's end bone (a shin, a forearm) turns the chain so its
  tip reaches the Target object; two bones exactly (law of cosines) bending towards an optional Pole, longer chains
  by cyclic coordinate descent (Iterations), Weight from the rest pose to the solution. Solved every editor frame
  and in Play after the scripts, only when something it depends on moved; the solved pose makes no undo step. The
  gizmo and G / R / S refuse a bone IK turns ("move its target instead"). Console `ik add <bone> <target> [pole]
  [chain]`.
- **GameObject > Rig**: Humanoid (Hips, Spine, Chest, Neck, Head, arms, legs) and Quadruped (spine, neck, head,
  tail, four legs) with IK targets on hands and feet and poles at knees and elbows; **Auto IK on Selected
  Skeleton** finds the limbs of an imported Mixamo (`mixamorig:LeftUpLeg`), Unity (`LeftUpperLeg`) or Rigify
  (`thigh.L`) skeleton by name. Console `rig humanoid|quadruped|autoik [root]`.
- Learn > Modifiers: "Inverse kinematics".

**Fixed**
- The GameObject menu's Lattice and Armature items had each other's tooltips.

**Checked**
- New tests (round 70): two bones reach a target within 1.3e-7, point straight at one out of reach, and with a pole
  the knee lies in the plane of hip, target and pole; the skin follows; nothing moved, nothing solved; five bones
  by CCD within 7.7e-4; Reset Pose solves again; Weight 0 keeps the rest; a target the chain carries is refused;
  the presets make the named bones and 4 IK chains each, lifting a foot target bends the knee forward with the
  foot on it; auto IK finds 4 limbs in Mixamo, Unity and Rigify skeletons and adds nothing the second time; in the
  editor, dragging a foot target with the gizmo poses the leg, the gizmo leaves a driven shin alone, undo puts the
  target and the leg back, `ik add` and a scene file keep the constraint; in Play a Rotator swinging a target
  animates the leg and Stop restores it; a second rig has its own targets; a target that circles back leaves no
  twist; Weight 0.5 doesn't add up over many moves; a pole the chain carries is refused; a lost target doesn't hold
  the bone; G on a driven shin starts nothing while G, R on the head turns it.
- Stress `vgroups`: a humanoid's four chains cost 0.006 ms a frame when every target moves, 0.001 ms
  when nothing does; among 20,000 other objects 0.046 ms (the per-frame pass); no IK in a scene: no walk.
- Unit checks: Windows 9,303, 0 failed (562 tests); the whole suite in render-cache verify mode: 9,304,
  0 failed, 0 stale pictures. Linux with libraries: 9,213, 0 failed. Sanitizer build: 9,131, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review (code reviewer)**: three majors and several minor findings, fixed: a second preset rig's limbs reached for
the first rig's targets (names now unique); each solve started from the last one, so a circling target wound up
twist and a Weight under 1 added up frame by frame (now from the rest pose); a pole parented under the chain
re-solved every frame; the per-frame pass walked every object of every scene and looked targets up by name (now
nothing with no IK anywhere, ids between frames); G / R / S weren't refused on a driven bone; a constraint with a
lost target still blocked the gizmo; a straight chain without a pole could bend sideways (now the rest bend, then
the bone's own Z); auto IK's poles were world +Z / -Z (now where each limb bends). Left as it is: the undo snapshot
is taken after the frame's solve, harmless now that solving doesn't depend on the previous pose.

## 2026-10-12 (round 69): skeletons and skinning (task 0047)

**Asked:** the roadmap (2026-10-11): "rigging ... v1: import rigs from FBX/glTF and pose them".

**Added**
- **Bones and armatures** (ADR 0025): a bone is a GameObject with a Bone component (Length, Deform, its rest
  transform) under a root with an Armature (Deform: Linear or Dual Quaternion, Show Bones, Rest Position). Bones
  are drawn as Blender's octahedra and picked by clicking them; pose them with the gizmo. GameObject > Armature
  (a selected mesh is bound to it), `bone add <parent> [length]`, `armature reset|apply` (Reset Pose, Apply as
  Rest).
- **The Armature (Skin) modifier**: moves the mesh with the bones its vertex groups are named after, 4 influences,
  Linear or Dual Quaternion (twisted joints keep their girth), deform-only (task 0041). **Bind: Automatic Weights**
  (`skin bind`): groups from each vertex's distance to the bones.
- **Rigged FBX and glTF import**: bones, hierarchy, weights (normalised), an Armature and a Skin modifier.
- Learn > Modifiers: "Bones and skins".

**Checked**
- New tests (round 69): a two-bone tube is unchanged at rest and turning the upper bone 90 degrees turns its
  vertices about the joint exactly (2.7e-7) while the lower ones stay; a 150 degree twist keeps the joint's radius
  at 0.200 with Dual Quaternion where Linear shrinks it to 0.052, and both agree at rest and for single-bone
  vertices; Blender's glTF and FBX two-bone tubes import with Lower and Upper bones, an armature, weights summing to
  1, unchanged at rest, and posing Upper moves its vertices and not Lower's; GameObject > Armature binds a column,
  `bone add` places a child at its parent's tip, `skin bind` gives weights summing to 1, a pose is one undo step and
  is kept by a scene file, Reset Pose and Apply as Rest; Reset Pose on a root that is a bone; a rest near 90 degrees
  stays exact through a file; an unevenly scaled mesh keeps Dual Quaternion's girth; a mesh under the armature
  follows it, and the Skin's own vertex group limits it; automatic weights give every vertex away from the joint to
  its nearest bone.
- Stress `vgroups`: a pose change on 50,176 vertices with 3 bones, 5.5 ms Linear, 5.6 ms Dual Quaternion (target 8).
- Unit checks: Windows 9,232, 0 failed (555 tests); the whole suite in render-cache verify mode: 9,233,
  0 failed, 0 stale pictures. Linux with libraries: 9,142, 0 failed. Sanitizer build: 9,060, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review (code reviewer)**: five majors and several minor findings, fixed: Reset Pose and Apply as Rest skipped a
root that is a bone; rest rotations kept as Euler angles lost up to 0.8 degrees near 90 (now a quaternion); the ADR
said bind poses were read (it now says they aren't, and an FBX saved in another pose is reported); in an FBX with
two characters every mesh took every skin's bones (now its own); making the armature's name unique could rename a
bone (the armature is never a bone now). Also: Dual Quaternion fell back to Linear on an unevenly scaled mesh;
a zero rest scale made vertices jump; hidden bones were still picked and In Front did nothing; GameObject >
Armature stacked a second Skin; glTF welded touching vertices with different weights. Left as they are (in the
spec's As built or for later): renaming a bone doesn't rename its vertex group; duplicate joint names.

## 2026-10-12 (round 68): Weight Paint (task 0046)

**Asked:** the roadmap (2026-10-11): "weight painting", the second half: the brush.

**Added**
- **Weight Paint mode** (MeshFilter > Vertex Groups > Weight Paint, or `weightpaint on`): the chosen group of the
  active mesh shows as Blender's weight colours (blue 0, green 0.5, red 1) and dragging in the Scene view paints it.
  Brush: Add, Subtract, Draw (towards Weight), Blur (towards the neighbours); Radius (screen pixels), Strength,
  Weight, Falloff (Smooth, Sphere, Root, Sharp, Linear, Constant), Mirror X, Front Faces Only. One undo step per
  stroke. Normalize All and Clean. Console: `wpbrush`, `wpfalloff`, `wpmirror`, `wpfront`, `wpstroke x y ...`,
  `wpnormalize`, `wpclean`.
- Learn > Modifiers: "Weight painting".

**Checked**
- New tests (round 68): Add raises the weights under the brush only, Subtract lowers them, Draw reaches Weight; the
  Smooth falloff paints the centre more than the rim; Blur evens a hard edge; Front Faces Only leaves a sphere's
  back alone (and off, reaches it); Mirror X gives the twin the same weight; a real mouse drag there and back is one
  stroke, one undo step, and stays within one dab's strength (also along the mirror line); leaving the mode keeps
  the weights; the view shows red at weight 1 and blue at 0 (pixels); the painted group stays the painted object's
  when another object is selected; a scene file keeps the weights; a new scene leaves the mode; Normalize All,
  Clean.
- Stress `vgroups`: a dab on a 50,176-vertex mesh 0.08 ms (target 8); a stroke's press and release 3.8 ms.
- Unit checks: Windows 9,194, 0 failed (549 tests); the whole suite in render-cache verify mode: 9,195,
  0 failed, 0 stale pictures. Linux with libraries: 9,104, 0 failed. Sanitizer build: 9,022, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review (code reviewer)**: four majors and several minor findings, all fixed: an undo, a new scene or Play in the
middle of a drag let the stroke write into meshes the undo history shared (a stroke now belongs to its scene and
mesh and stops otherwise); a new scene kept painting whatever object had the same id; choosing another group showed
a stale picture; selecting another object could switch the painted group; every click made an undo step (only a
stroke that changes weights does now); Mirror X applied twice near the centre line; strokes accumulated past their
strength (Blender's no-accumulate rule now); Normalize All moved the active group (Lock Active now); the X-mirror
map could stall on a stray far vertex (an edge-length tolerance, and cached); Esc leaves the mode.

## 2026-10-12 (round 67): vertex groups (task 0045)

**Asked:** the roadmap (2026-10-11): "weight painting", the first half: the groups it paints.

**Added**
- **Vertex groups** (ADR 0024): MeshFilter > Vertex Groups in the Inspector (Add, Remove, rename; in Edit Mode a
  Weight with Assign, Remove from, Select, Deselect) and the console `vgroup add|remove|rename|assign|unassign|
  select|deselect|active`. Saved in scene files (`vg` lines), kept by undo.
- **Every deforming modifier** (Displace, Simple Deform, Smooth, Cast, Curve, Lattice, Mesh Deform) has **Vertex
  Group** and **Invert Group**: only that group's vertices move, each by its weight.
- The groups follow the vertices through every edit: welds and merges, Mirror, Array and Join carry them exactly;
  any other change of the vertex count mends them by position (a vertex that stayed keeps its weights, a new one
  takes the surface's at its place), at each undo step and after each generative modifier of a stack.
- Learn > Modifiers: "Vertex groups".

**Checked**
- New tests (round 67): 17 Edit Mode operators (merge, delete, dissolves, subdivide, loop cut, bevel, extrudes,
  inset, triangulate, poke, split, duplicate, quads, collapse, connect, edge split) keep one weight per vertex,
  vertices that stay keep both groups' weights, and new vertices on the surface carry a linear ramp exactly; adding
  a second group and removing one survive the undo commit; Separate keeps each part's weights and Join through the
  editor brings the other object's group by name; Apply Modifiers bakes a group-limited Displace as shown; Mirror
  copies give their sources' weights, Array and simple Subdivision carry them, Join merges groups by name; a Displace limited to a group moves each vertex by its weight, Invert the rest, an
  unknown name the whole mesh; groups round-trip through a scene file, old files load without them, the console
  adds, assigns (0.75), renames and undo takes each back.
- New stress section `vgroups`: 37 Edit Mode operators (random vertex or face selections) and 10 generative
  modifiers (each followed by a group-limited Displace) on the 26 stress meshes with groups: 1,222 operations, 0
  groups of the wrong size, 0 weights not finite or out of 0..1, 0 weights moved off a vertex that stayed. Carrying
  two groups through Subdivision Surface on 25k vertices: 1.8 ms a re-run (48 ms once, building the map).
- Push/Pull: 26,620 operations, 0 problems, 394 results with faces newly on top of each other (unchanged);
  `modifiers_ngon` 11,514 operations, 0 problems.
- Unit checks: Windows 9,151, 0 failed (543 tests); the whole suite in render-cache verify mode: 9,152,
  0 failed, 0 stale pictures. Linux with libraries: 9,061, 0 failed. Sanitizer build: 8,979, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review (code reviewer)**: a blocker, five majors and several minor findings, all fixed: the undo commit's mending
undid Add and Remove group and lost a joined object's groups (it now only mends groups an operator left the wrong
size); mending by position overwrote Mirror's and Array's exact copies; Separate dropped the groups; Apply of one
modifier ignored its vertex group; carrying groups through a generative modifier cost 89 ms every frame of a drag
(now a kept map: 1.8 ms); the tests couldn't tell carried weights from re-sampled ones and the stress selected no
faces; the search could stop early for points outside the old mesh, a far point overflowed an int, a deformer's
NaN reached weight-0 vertices, saving before the commit dropped the groups (save commits first).

## 2026-10-12 (round 66): the Mesh Deform modifier, a cage of any shape (task 0044)

**Asked:** the roadmap (2026-10-11): "a Cage (lattice / mesh-deform) tool".

**Added**
- **Mesh Deform** (Add Modifier > Deform): set Object to a closed mesh round this one and press **Bind** (or
  `meshdeform bind [object]`); then moving the cage's vertices shapes the mesh smoothly. Binding uses mean value
  coordinates (Ju, Schaefer and Warren 2005; ADR 0023), closed form, so it takes a moment instead of Blender's
  voxel solve. An open, flat or missing cage is refused with the reason; a cage whose vertex count changes stops
  the deform and says "Bind again". Deform-only, so dragging cage vertices updates in place.
- Learn > Modifiers: "A cage round a mesh".

**Checked**
- New tests (round 66): right after Bind a sphere in a cube cage and in an icosphere cage is unchanged; the weights
  sum to 1 (off by 1e-7); moving and turning the whole cage moves the mesh the same way (off by 7e-8); pulling one
  cage corner moves the mesh near it 18 times more than the far side; an open cage and a flat one are refused,
  Unbind leaves the mesh, an extra cage vertex stops the deform with "Bind again"; a cage wound the other way binds
  and reproduces a stretch, one with a face turned round or doubled is refused, a stray far vertex doesn't spoil the
  volume test; weights at a cage vertex, on a face and outside the cage reproduce the point; two meshes caging each
  other end without a crash; editing the bound mesh gives what a fresh Bind would; the binding round-trips through
  a scene file; in the editor a refused Bind says why, Bind is one undo step, and undoing an Unbind binds again.
- Stress `modifiers_ngon`: Mesh Deform on all 26 meshes with cages moved, NaN, huge and flattened after Bind:
  11,514 operations, 0 problems. Binding 20,002 vertices to a 1,280-triangle cage: 97 ms (target 200); each deform
  0.8 ms (target 5).
- Unit checks: Windows 9,126, 0 failed (537 tests); the whole suite in render-cache verify mode: 9,127,
  0 failed, 0 stale pictures. Linux with libraries: 9,036, 0 failed. Sanitizer build: 8,954, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review (code reviewer)**: a blocker, a major and several minor findings, all fixed: two meshes caging each other
recursed until the stack overflowed (the cage is now hashed as the Boolean's cutter is, without evaluating its
keys); editing the bound mesh rebuilt every weight on every drag frame (rows are now redone only for vertices that
moved); a stray cage vertex could fail the volume test, which also lost precision far from the origin; the binding
text was copied and hashed byte by byte in every evaluation (the change hash now sees a key); a binding read from a
file wasn't checked; a point on a cage face missed the on-face case by rounding (its tolerance is now 1e-6).

## 2026-10-12 (round 65): lattices (task 0043)

**Asked:** the roadmap (2026-10-11): "a Cage (lattice / mesh-deform) tool".

**Added**
- **Lattice objects** (GameObject > Lattice, also in the Hierarchy's + menu): a box of points fitted round the
  selected mesh, which gets a **Lattice modifier** naming it. Points U / V / W (2 to 32, changing them keeps the
  shape), Linear or B-Spline; the modifier has a Strength. Click a point to select it and move, turn or scale it
  with the gizmo (Shift adds, Esc lets go). The modifier is deform-only, so dragging a point updates the mesh in
  place (task 0041).
- Curve and lattice points are now one kind of thing to the editor (`Component` control points); the console has
  `points select|move <object> ...` for both (`curve` still works).
- Learn > Modifiers: "Squashing with a lattice".

**Checked**
- New tests (round 65): a lattice at rest leaves a mesh as it is (both interpolations, three resolutions); Linear:
  a corner moved by d moves the mesh's corner by d, the opposite corner not at all, the centre by d / 8, Strength
  0.5 half as far; B-Spline: pulling an inner point never moves neighbouring samples by more than the pull times
  their spacing over the cell size; changing the resolution keeps an affine pull (within 1e-3); NaN, flattened,
  missing and self-referencing lattices leave a valid mesh; a turned, stretched lattice on a turned, scaled object
  bends the mesh as the same lattice would in world space (off by 1e-7), and moving it re-bends; a B-Spline
  lattice keeps its points through resolution changes 2 to 10 and back, and outside its box blends on without a
  crease; GameObject > Lattice fits the box and adds the modifier, a click selects a point, a gizmo drag moves
  only it and reshapes the mesh, Delete then removes the lattice, undo restores each step, `set` reaches the
  resolution and any point, a second lattice is Lattice.001 and its modifier names it, and a scene file keeps
  everything.
- Stress `modifiers_ngon`: lattice cases on all 26 meshes (both interpolations, three resolutions, ordinary, NaN
  and huge offsets, a flattened box): 11,384 operations, 0 problems. A B-Spline 4 x 4 x 4 lattice deforms a 50k-
  vertex mesh in 0.5 ms (target 3 ms).
- Unit checks: Windows 9,083, 0 failed (531 tests); the whole suite in render-cache verify mode: 9,084,
  0 failed, 0 stale pictures. Linux with libraries: 8,993, 0 failed. Sanitizer build: 8,911, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review (code reviewer)**: two majors and several minor findings, all fixed: changing a B-Spline lattice's
resolution sampled it as a B-spline and shrank the shape a little every time (now sampled linearly, as Blender
does); a second lattice had the same name, so its modifier found the first (now Lattice.001); outside the box
B-Spline held the face value (a crease) where Blender blends on; a click on a mesh inside its lattice could select
the lattice (only the box's outer edges pick it now); the flattened-box test was absolute (now relative to the
box's size); Delete with a lattice point selected did nothing; the timing row subtracted two noisy timings.

## 2026-10-12 (round 64): curve objects, the Curve modifier and Array along a curve (task 0042)

**Asked:** the roadmap (2026-10-11): "Curves: objects follow a path (Blender's Curve modifier and Array-along-curve)".

**Added**
- **Curve objects** (GameObject > Curve > Bezier, Circle, Path, also in the Hierarchy's + menu): Bezier (Blender's
  Auto handles), Catmull-Rom or Poly; Closed; Resolution; per point a Tilt and a Radius. Drawn in the Scene view
  (orange when selected); click the line to select the curve, click a point to select it (Shift adds, Esc lets go),
  and the Move / Rotate / Scale gizmo moves the selected points, one undo step per drag. Console:
  `curve add|move|delete|select <object> ...`, and `set Curve.P2Position x y z`.
- **Curve modifier** (Deform): bends a mesh along a named Curve object on any of six axes; the mesh's origin sits
  at the curve's start, past an open curve's end the mesh goes on straight, a closed one wraps round. It is
  deform-only, so dragging its curve updates the mesh in place (task 0041).
- **Array: Fit Type** Fixed Count, Fit Length and Fit Curve (as many copies as fit the curve's length).
- Learn > Modifiers: "Bending along a curve".

**Checked**
- New tests (round 64): a straight curve of each type leaves a beam unchanged; a quarter circle bends a beam onto
  the arc (radius off by 2.5e-6, length along it kept); a closed curve wraps a beam round and an open one goes on
  straight past its end; Fit Curve gives 5 then 7 then 14 copies as the curve grows and is scaled, Fit Length 3,
  NaN or a missing curve one; moving a point or the curve object re-bends, a curve naming its own object or
  nothing leaves the mesh; NaN, huge and coincident points on every type, closed or not, and every axis give a
  finite mesh; a scene file keeps everything; in the editor a click selects the curve, then a point, the Move
  gizmo moves only that point and undo puts it back; moving points and the curve leaves no stale picture; a
  straight curve along each of the six deform axes changes nothing, Tilt 90 turns the cross-section a quarter turn
  and Radius 2 doubles it; the Circle preset is round within 0.3 %; a closed trefoil knot's frame meets itself;
  Delete with points selected removes the points, and selecting another object lets go of them.
- Stress `modifiers_ngon`: the Curve modifier on all 26 meshes along five ordinary and hostile curves, every type,
  closed or open, every axis: 10,760 operations, 0 problems.
- Unit checks: Windows 9,034, 0 failed (523 tests); the whole suite in render-cache verify mode: 9,035,
  0 failed, 0 stale pictures. Linux with libraries: 8,944, 0 failed. Sanitizer build: 8,862, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review (code reviewer)**: approved, with minor findings fixed: Array's Fit Curve no longer re-evaluates when the
curve is only moved or turned (just its scale counts), so dragging a curve under "Array then Curve" stays on the
in-place path; a curve's path is sampled once per change (the Scene view used to sample every curve every frame)
and hashed straight from its fields; a point selection is dropped when another object is selected, and Delete
removes the selected points; the Circle preset has 8 points (4 bulged in by 4 %); `curve` is in the console's help.
Left as they are: point clicks win over a mesh in front within 9 pixels; V and Ctrl+Shift snapping don't apply to
curve points yet.

## 2026-10-12 (round 63): deforming modifiers update in place (task 0041)

**Asked:** the roadmap (2026-10-11): curves, lattices, cages and skinning all deform meshes, and dragging their
fields must stay interactive ("make sure the editor is still decently responsive").

**Changed**
- Smooth, Displace, Simple Deform and Cast only move vertices. At the end of a stack they now re-run on a kept copy
  of the mesh and the render mesh moves its positions, normals and tangents in place, instead of a copy, a full
  rebuild and new tangents on every change. A field drag on a 50k-face mesh went from 56 ms to 6.6 ms per change.
  Modifiers before them (Subdivision, Mirror, ...) are evaluated once and kept while only the deformers change.

**Checked**
- New tests (round 63): on a cube, a UV sphere, a 50k-face grid, a mesh with sharp edges and one with UV seams,
  each of the four deformers, three changes each: the in-place result equals the full evaluation (positions and
  normals bit for bit; tangents unit length and at right angles); only the tail re-runs and the render mesh keeps
  its build, a generative modifier or Edit Mode's cage rebuild in full; a flat grid made bumpy rebuilds then
  follows in place; a scene snapshot keeps its shape when the original changes again; a Boolean re-cuts when its
  cutter is deformed; tangents are the same whatever drag led to a shape (and within 18 degrees of MikkTSpace on a
  twisted cylinder); a saved and reloaded scene evaluates the same; in the editor a drag stays in place and
  re-renders the view, the shadows and voxel GI with no stale picture, and undo brings the old shape back exactly.
- Stress `modifiers_ngon`: Simple Deform angle on a 50,176-face mesh 6.6 ms per change, 8.3 ms with sharp edges
  (in full 56 ms); 6,080 operations on odd meshes, 0 problems.

**Review (code reviewer)**: two majors and several minor findings, all fixed: a Boolean whose cutter was deformed
kept its old cut (the cutter's mesh is now the same object, so its dependency mixes `deform_version`); tangents
were projected from the previous frame's and drifted with the drag (now from UV gradients); `deform_version` moved
only after the deformers ran; tests for voxel GI, save / load, the Boolean and tangents; a sharp-edges stress row.
- Unit checks: Windows 8,975, 0 failed (511 tests); the whole suite in render-cache verify mode: 8,976,
  0 failed, 0 stale pictures. Linux with libraries: 8,885, 0 failed. Sanitizer build: 8,803, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

## 2026-10-12 (round 62): axis materials by height with a noisy border; deleting textures (task 0040)

**Asked:** "procedural ... work based on global position. Like the top of a cliff where it's grassy but over the edge
it turns into dirt and rock" ("if it's quick, then do it"); "provide the option to delete textures in the texture
tab"; "Making a new material in the material tab doesn't actually show the new material."

**Added**
- **Axis High** on axis materials: a material above **High Start** (world height in World mode, the object's own in
  Local), faded over **High Blend**; and **Axis Noise** / **Noise Scale**, which make the grass-to-dirt and the
  high band's borders wander. The same in the views, CPU and GPU path tracing and bakes. Learn > Materials: the
  terrain section.
- **Deleting textures** in the Textures window: a Delete button, the thumbnail's right-click menu, or Del; it asks
  first, says how many materials use it (they then show it missing), and moves the file to the Recycle Bin.

**Fixed**
- New Material Asset (and New in the Textures window) with text left in the search box made something the grid
  filtered out, so it looked as if nothing happened: New now clears the search, and a search says how many items
  it hides ("Showing 1 of 15").

**Checked**
- New tests (round 62): above the band snow, below it the slope's parts, a blend between, nothing without it;
  Local mode by the object's height; noise 0 gives the slope rule exactly, noise makes the border wander (107 grass,
  92 dirt at the border angle) and is the same at the same point; NaN fields and positions stay finite; a tilted
  plane is 29.3 % snow rasterized, 30.0 % on the CPU, 30.1 % on both GPUs (checked to be the GPUs); the fields
  round-trip, an old .mat loads with the defaults, the pickers set every part; Delete asks first then trashes the
  texture and a material using it shows it missing; New clears a leftover search.
- Stress `shading`: with height and noise, every point near a border, 2.2x a plain material (the spec's 2x is
  2.5x for this worst case; see its As built); task 0029's axis row 1.5-1.6x. Both are now the fastest of five
  interleaved rounds against a plain run: single timings swung 1.5-2.4x for the unchanged 0029 row.

**Review (code reviewer)**: a blocker, a suspected major and several minor findings, all fixed: Axis High couldn't
be picked in the UI (the setter knew three parts); the GPU test could pass on a CPU fallback (it now checks the
device); a NaN height on the GPU; noise evaluated at NaN; weak tests; the delete popup's count (now unique
materials, assets included); undo could write a deleted texture's text into a new one of its name; unsaved texture
edits not saved before the trash.
- Unit checks: Windows 8,880, 0 failed (503 tests); the whole suite in render-cache verify mode: 8,881,
  0 failed, 0 stale pictures. Linux with libraries: 8,790, 0 failed. Sanitizer build: 8,712, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

## 2026-10-11 (round 61): anime and manga camera filters (task 0039)

**Asked:** the rest of "anime / 2D looks like Goo Engine (toon shading, outlines, halftone, ...)" (the roadmap's
item 6, second half).

**Added**
- Camera filters: **Halftone** (a dot screen, Mono ink on paper or Colour, the same dots at any resolution),
  **Hatching** (one to three line directions switched on by darkness), **Line Art** (silhouettes, object edges,
  creases by angle, Taper), **Paper** (blotches, fibres and grain) and **Color Ramp** (brightness through 2 to 4
  colours, Constant or Linear).
- **Looks**: Add Filter > Looks > Anime (Line Art, saturation, a soft Bloom) and Manga (Grayscale, Halftone, Line
  Art, Paper); `filter look Anime|Manga`. One undo step.

**Asked during the round:** "in the material tab and inspector it says the texture is either non color or missing."
And: "When I have a procedural map (bricks) and I have a color ramp that is based on height. It only works when height
map is enabled even though I just want the color ... Maybe add a toggle with the texture window to only enable or
disable certain maps."

- **Normal Map, Roughness Map and Height Map switches** on a procedural texture (the Textures window, below the layer stack): off, that
  output is neutral (a flat normal, roughness 1 so the material's Roughness rules, mid height) and Assign to
  Selection leaves it out, while the layers still compute the height inside, so a Color Ramp from the height
  colours the bricks without them also bumping the surface. Live shading follows the switches too.

- **Bake Textures in the Hierarchy's right-click menu** too (as GameObject > Bake Textures), as the user asked.

**Fixed**
- **A material's texture field said "(missing)" for a procedural texture** (Bricks.ptex#albedo is no file): it
  now shows the texture's image and its name ("Bricks (albedo)"), and its picker lists every procedural texture's
  outputs beside the images. Their Non-Color tag and colour-space switch are gone (an output's colour space is its
  own). The renderers always used them; only the field was wrong.
- Path-traced pictures gave the filters (Edge Outline, Line Art) depth from a random sample per pixel; now one ray
  through each pixel's centre, so their lines match the rasterizer's.
- The Add Filter menu showed two Color headings (Color Ramp was listed after the Stylize effects).

**Checked**
- New tests (round 61): halving Halftone's cell gives 3.9 times the dots and 540p and 1080p show the same 14,528;
  white stays white and black solid; Hatching's ink falls with brightness; Color Ramp Constant gives exactly 3
  colours; Paper at 0 changes nothing and is the same twice; Line Art draws a cube's edges rasterized and traced
  (the traced lines all within a pixel of the rasterized ones), a 30-degree fold at 20 and not at 40, nothing on
  a smooth sphere; the looks add their effects in order, undo, round-trip, and an unknown look adds nothing.
- Stress `filters`: each pass at 1080p on 8 threads against Edge Outline (about 10 ms): Halftone 0.6-0.9x,
  Hatching 0.8x, Line Art 1.8-2.5x, Paper 0.1x, Color Ramp 0.05x (the spec's flat 4 ms is below what Posterize
  costs; see the spec's As built); the fuzz of every effect with NaN / huge fields, 0 problems.

**Review (code reviewer)**: three major and several minor findings, all fixed but one noted: grey specks in pure
white and black halftone; noisy traced creases from jittered depth; Paper's cache holding one size (now four);
an unknown look adding an empty stack; scratch memory that never shrank; Taper on the sky side; dots sized from
one pixel. Noted: these colour passes veil a transparent film's background, as Invert and Posterize do.
- Unit checks: Windows 8,850, 0 failed (497 tests); the whole suite in render-cache verify mode: 8,851,
  0 failed, 0 stale pictures. Linux with libraries: 8,762, 0 failed. Sanitizer build: 8,684, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures; `filters` and `textures` stress 0 problems.

## 2026-10-11 (round 60): toon shading and outlines (task 0038)

**Asked:** "Anime / 2D looks like Goo Engine (toon shading, outlines, halftone, ...)" (the roadmap's item 6, first half).

**Added**
- **Toon shading model** (a material's Shading Model, or the Toon preset): light in Toon Bands flat tones with a
  hard (or Softness) edge at the Threshold, a Shadow Color for the dark side, a hard highlight spot, a rim light
  and a flat ambient. The same tones in the views, F12 on the CPU and the GPU, and Live Preview; bounces off toon
  surfaces stay ordinary (Lambert), so lighting bakes are unchanged.
- **Outline** component: the black line round an object (the inverted hull), in the views, path-traced renders
  (camera rays only) and exports; Thickness in metres or object units, a colour.
- Learn > Materials: *The anime look*, with a Try-it.

**Fixed**
- Objects with a negative scale (mirrored) were drawn inside out by the rasterizer.

**Checked**
- New tests (round 60): tones step through exactly Toon Bands levels; a rasterized toon sphere takes 2 tones with
  1 band and 4 with 3 (PBR: the 4 commonest colours cover 28 %); the CPU trace's tone shares are within 0.7 % of
  the rasterizer's, 3.1 % under a large area light; the GPU matches the CPU; the rim lights only the silhouette;
  an Outline adds a ring of its colour round the sphere and changes none of its pixels, rasterized and traced,
  mirrored too; thickness 0 draws nothing; the shell is pushed out with flipped faces, cached, world-space
  thickness; an outlined object with a modifier traced in the Camera Preview while the Scene view redraws; undo
  of the Outline and of a toon field; the fields round-trip through a .mat and the Outline through a scene.
- Stress `shading`: a toon material shades in 8.4 ms at 1080p against PBR's 10.4 ms (spec: at most +15 %).
- The GPU kernel came within reach of the SPIR-V optimiser's id limit (a second inlined shadow-ray call
  overflowed it): the toon light branch shares the PBR one's shadow ray.

**Review (code reviewer)**: a blocker, three major and several minor findings, all fixed but one noted: the
outline shell had one cache slot for several views (a path tracer could keep a freed one); mirrored objects drew
the hull's near side; the toon response was applied at bounces and in bakes; the rasterizer's toon ignored an area
light's facing; falloff scaled tones instead of being banded; flat ambient ignored occlusion; tests for
mirroring, modifiers, undo and area lights; the ADR now records the component and the camera-only hull. Noted:
an outline makes shadow rays use the closest-hit loop.
- Unit checks: Windows 8,756, 0 failed (490 tests); the whole suite in render-cache verify mode: 8,757,
  0 failed, 0 stale pictures. Linux with libraries: 8,668, 0 failed. Sanitizer build: 8,590, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures; `shading` and `gpu` stress 0 problems.

## 2026-10-11 (round 59): GPU devices open in the background (task 0037)

**Asked:** "Switching from shaded to rendered and path trace preview crashes/halts the program" ... "make sure the
editor is still decently responsive" (the retro stages stay in the path tracers).

**Fixed**
- **Turning the Camera Preview (or the Render window, Live Preview, a bake) to the GPU froze the editor** the first
  time in a session: the driver built the kernel's pipeline on the editor's thread, about 80 s with a cold cache,
  and Windows closed Blendity as not responding. Now the GPU opens in the background: the view traces on the CPU
  ("GPU starting") and moves to the GPU when it is ready; bakes do those slices on the CPU; the editor wakes four
  times a second meanwhile. A spare stays open so the next view gets a GPU at once.

**Added**
- A pipeline cache per GPU in `~/.blendity/cache` (checked against the GPU and a hash): later starts build the
  pipeline from it even when the driver's cache is cold (9-13 ms instead of 84-141 ms here with a warm one).

**Checked**
- New tests (round 59): a GPU that doesn't exist is refused at once; with the open slowed to 3 s the slowest
  editor frame is 17 ms, the preview traces on the CPU and is on the GPU after 3.3 s, and the spare is handed over
  at once; a tracer forgets a pending GPU when the device turns back to the CPU; a bake's gather returns at once
  while the GPU opens and uses it later; the pipeline cache is written and used, and corrupt, torn and foreign
  files are ignored. The tests keep their caches in a scratch folder.

**Review (code reviewer)**: four major and several minor findings, all fixed: the wake-up was cleared later in the
same frame; quitting while a device opened could crash in static destructors (now leaked statics and `_Exit`);
the GPU bakes still opened a device on the editor's thread; "GPU starting" stuck after switching to the CPU;
the cache file had no body check and a shared temporary name; a failed open was never retried; any other view's
open rebuilt a waiting tracer's scene; prewarm ignored the RT setting; the tests wrote the user's cache folder.
- Unit checks: Windows 8,714, 0 failed (480 tests); the whole suite in render-cache verify mode: 8,715,
  0 failed, 0 stale pictures. Linux with libraries: 8,627, 0 failed. Sanitizer build: 8,549, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures; `gpu` stress 0 problems.

## 2026-10-11 (round 58): bake textures onto a mesh for export (task 0036)

**Asked:** "Bake textures onto a mesh" for export (the roadmap's item 5), one atlas per object.

**Added**
- **Object > Bake Textures** (512 to 4096; console `baketex [size] [keepuvs]`): the active object's look (image
  maps, procedural and `.ptex` textures, axis materials, Box and Generated mapping, every material slot) baked into
  albedo, roughness, metallic and normal PNGs in `Assets/Textures/Baked`, on new UVs (Smart UV Project and Pack
  Islands); the mesh takes those UVs and a new `<Object> Baked` material on every slot. One undo step. Objects
  whose modifiers add geometry are refused with a message (apply them, or export with Bake Textures).
- **Export > Bake Textures** (OBJ, FBX, glb / glTF, with a size): each exported object baked on a copy, the
  images written beside the file and referenced; the scene is not changed.
- **Exports carry more maps** without a bake: glTF normal textures and roughness / metallic packed into one PNG in
  the buffer; FBX Texture and Video objects for base colour, normal, roughness, metallic and emission maps.

**Fixed**
- OBJ exports wrote a procedural texture's output (`X.ptex#albedo`) as a file path when it hadn't been converted.
- `CMakeLists.txt` didn't compile `src/texture` (the build scripts and CI did).

**Checked**
- New tests (round 58): six materials on a cube's faces bake their colours at the face centres on one atlas; a
  checker on a plane matches the renderer at all 64 cell centres; an axis material bakes grass on top and dirt on
  the sides, rotated too; a flat surface's normal map is within 0.3 degrees of +Z and a Bricks texture bakes its
  mortar; Object > Bake Textures keeps the rasterized look (0.02 % mean colour difference) and undo restores the
  UVs and material; OBJ, glb and FBX exports with Bake Textures write and reference the images (ufbx reads the
  FBX textures); ordinary materials' maps reach glTF and FBX; Keep UVs; NaN, empty and collapsed meshes bake
  nothing; two objects with one name get their own files; a Mirror modifier is refused; Opaque alpha bakes solid
  and a Transparent slot gives the baked material its surface.
- Opened in Blender 5.2: the OBJ, glb and FBX import with Base Color, Roughness, Metallic and Normal linked to
  the baked 256 x 256 images.
- Stress `texbake` (new): 50k faces at 2048 in 506 ms on 8 threads (spec: under 2 s), 441 ms on 32, 4096 in
  968 ms; 60 fuzzed bakes (NaN, huge, collapsed meshes, odd sizes) 0 problems. A 4096 bake from the menu holds
  the editor for about a second.

**Review (code reviewer)**: four major and several minor findings, all fixed: an Opaque material's alpha made the
baked object a Cutout; a bake overwrote another same-named object's images (now new files each time); the in-scene
bake ignored modifiers that add geometry (now refused); tracked stress reports deleted by a cleanup (restored);
mirrored objects' normal maps in exports; the Bake option shown with Materials off; Unlit and emission dropped;
packed roughness read as linear from sRGB maps; collapsed triangles baked.
- Unit checks: Windows 8,693, 0 failed (476 tests); the whole suite in render-cache verify mode: 8,694,
  0 failed, 0 stale pictures. Linux with libraries: 8,625, 0 failed. Sanitizer build: 8,547, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures; `texbake` and `textures` stress 0 problems.

## 2026-10-11 (round 57): transparent backgrounds in renders (task 0035)

**Asked:** "Add the ability to make backgrounds transparent when they are rendered (png transparency)."

**Added**
- **Transparent** (Render window; Blender's Film > Transparent): F12 renders, rasterized or path traced on the CPU
  or the GPU, leave the world out of the background while it still lights the scene. A saved PNG has alpha 0
  there, partial alpha at anti-aliased edges and behind see-through surfaces, 1 on solid objects; an EXR keeps the
  alpha too; a JPEG is composited over black. The Render window shows a transparent render over a checkerboard.
  Camera sequences save the same way.

**Checked**
- New tests (round 57): rasterized and path-traced (CPU and GPU) renders of a cube with no floor have alpha 0 at
  the corners, 255 in the middle and partial values at the edges; a 50 % see-through quad gives alpha 127 (raster)
  and 128 (traced); the PNG reads back with the alpha; off, every pixel is opaque. Opened in Blender 5.2: RGBA,
  straight alpha, 0 at the corner, 1 on the subject. A glass sphere is covered (alpha 255) and shows the world
  through it on the CPU and the GPU; a Bloom filter keeps a rasterized cube opaque; an Edge Outline keeps its
  alpha in rasterized and filtered path-traced renders.

**Review (code reviewer)**: three major and several minor findings, all fixed: glass vanished in transparent traces,
Bloom made a see-through rim in rasterized ones, a filtered trace's alpha ignored its passes, a faint veil from
colour-quantising passes, coverage accumulated on every GPU render, low coverage amplifying denoised colour.
- Unit checks: Windows 8,584, 0 failed (466 tests); the whole suite in render-cache verify mode: 8,585,
  0 failed, 0 stale pictures. Linux with libraries: 8,516, 0 failed. Sanitizer build: 8,438, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures; `filters` and `gpu` stress 0 problems.

## 2026-10-11 (round 56): the Research window retired; papers in Learn > References (task 0034)

**Asked:** "you can remove the research tab, doesn't really serve a purpose. Any features that use papers should be
simply referenced and included in the learn tab."

**Changed**
- **The Research window is gone** (Window menu, File and Help menus, the Project window's Research Papers folder,
  dropping a PDF copying it into the project). A layout saved with it loads without it.
- **Learn > References** lists the papers behind Blendity's features, each with the feature it became and where
  it is (Taubin smoothing, Catmull-Clark, LSCM unwrapping, MikkTSpace, voxel GI, GGX, the area-light work of
  Karis and Heitz et al., the BVH, path guiding, the physical sky, noise, colour temperature...), with a Try-it
  button for Taubin smoothing. `window Research`, `research`, `references` and Ctrl+8 open it. Mesh features from
  papers are now under Mesh > From Papers. Nothing in the `research/` folder was touched.

**Fixed**
- The Textures window's preview could keep an old picture after an undo (an image freed and a new one allocated
  at the same address looked unchanged); it, and the Materials window's previews, now hold their images.

**Checked**
- New tests (round 56): an old layout with Research (beside the Console, and alone in an area) loads without it,
  every preset too; `window Research` and `research` open References; every paper feature is cited there and the
  Taubin Try-it smooths a noisy sphere; dropping a PDF copies nothing; Ctrl+8 opens References from any lesson; a
  layout with Research in its root area, or only Research, loads without it.

**Review (code reviewer)**: a major and several minor findings, all fixed: Add Tab still offered Research, Ctrl+8
opened the last lesson, Research in a layout's root area stayed, the path-guiding citation (now Ruppert, Herholz
& Lensch 2020, the method Open PGL is set to), the research folder still deletable from Blendity, the Materials
previews holding textures (now a content key and weak references), dead state.
- Unit checks: Windows 8,552, 0 failed (461 tests); the whole suite in render-cache verify mode: 8,553,
  0 failed, 0 stale pictures. Linux with libraries: 8,488, 0 failed. Sanitizer build: 8,410, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures; `editor_ui` 0 problems.

## 2026-10-11 (round 55): camera composition guides (task 0033)

**Asked:** "adding 'rule of thirds' guides to the camera and can be togglable. Any additional camera guides or
helpers are also welcomed."

**Added**
- **Composition guides** on every camera (Blender's Composition Guides and Safe Areas): **Thirds**, **Center**,
  **Center Diagonal**, **Golden Ratio**, **Golden Triangle A / B**, **Harmonious Triangle A / B**, **Title Safe**
  and **Action Safe** (margins adjustable), in a colour and opacity of your choice, all behind one **Show
  Guides** switch. They draw over the camera's frame in the Game view, the Camera Preview and while piloting the
  camera (inside a filter's letterbox when there is one), never into renders. Toggle them in the Camera
  inspector, the Game view's new **Guides** menu or with `guides thirds on` / `guides off`. A Learn lesson
  explains them.

**Checked**
- New tests (round 55): the thirds lines at 1/3 and 2/3 of the Game view's camera frame (every pixel of the
  line), also in a 2.39:1 letterbox; each of the ten guides draws on its own; Show Guides off draws nothing; an
  F12 render with guides on is identical to one without; the camera's render hash ignores them; Title Safe at
  Blender's margins; the fields save, undo and redo; the Guides menu clicked with the mouse; the triangles'
  endpoints are Blender's.

**Review (code reviewer)**: a blocker and several minor findings, all fixed: the Guides menu read a dead pointer,
the triangles weren't Blender's construction, the piloted frame ignored a filter's letterbox, a hashing hazard in
`Camera::reflect`, loose console parsing, HiDPI lines, zero safe margins.
- Unit checks: Windows 8,522, 0 failed (458 tests); the whole suite in render-cache verify mode: 8,523,
  0 failed, 0 stale pictures. Linux with libraries: 8,458, 0 failed. Sanitizer build: 8,380, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures; `editor_render` idle 1.33 ms.

## 2026-10-11 (round 54): every camera filter in the path tracers (task 0032, ADR 0008 amended)

**Asked:** "I want all the camera filter modifier stack to show up in the render view ... I want the filters to
show up in the path traced system as well".

**Added**
- The Retro Console's four rasterizer stages now show in path-traced pictures (F12, the Render window's
  Preview, the Rendered Camera Preview), on the CPU and the GPU: **vertex snapping** (wobbly PS1 edges),
  **affine texture warping**, **the texture rules** (Nearest or 3-point filtering, the size cap) and the
  Saturn's **screen-door transparency**. The other filters (outlines, bloom, grading, fog...) already did.

**Checked**
- New tests (round 54): a traced quad at a grazing angle takes the rasterizer's affine UVs (mean difference
  0.19, against 0.94 for perspective), the GPU equal to the CPU; a 1-texel cap makes a textured quad one
  colour on the CPU and the GPU; the screen door is an exact checkerboard (solid and see-through) on both;
  a path-traced F12 render through a snapping Retro filter has the rasterized render's silhouette (0 pixels
  differ, 448 without the snap); the GPU takes the same capped mip level, Nearest and 3-point filters as the
  CPU; a nearly clear surface (alpha 0.05) vanishes through the door as in the rasterizer.
- Stress `filters`: the odd-settings check had been reporting 135 false problems since task 0026 (it still
  required the internal image to be no larger than the view); corrected: 300 runs, 0 problems.

**Review (code reviewer)**: three major and several minor findings, all fixed: the door kept nearly clear
surfaces, the GPU capped the sky map too, every preview rebuild copied every mesh (now cached and reused),
hits behind glass counted as camera hits, shadow rays skipped the texture rules, non-finite screen positions.
- Unit checks: Windows 8,494, 0 failed (452 tests); the whole suite in render-cache verify mode: 8,495,
  0 failed, 0 stale pictures. Linux with libraries: 8,430, 0 failed. Sanitizer build: 8,352, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures; `gpu` stress clean.

## 2026-10-11 (round 53): live procedural textures (task 0031, ADR 0017 amended)

**Asked:** "Not only do I want the procedural textures to be evaluated at a set resolution. I want to view them
change on the material itself. So one part is images at a set resolution, but still having the convenience of
live shaders."

**Added**
- **Live Shading** (a texture's field, on by default): up close, where a texel of the image would cover more
  than a pixel, the Scene and Game views, the Camera Preview, F12 raster and the Materials window's previews
  evaluate the texture's layers at the pixel, like a shader, so bricks, planks and tiles stay sharp at any
  distance (normals included). Further away they read the mipmapped image, which doesn't shimmer. The images
  at the set resolution are still what exports, bakes and the path tracers use.
- **Objects follow a texture edit while you drag:** dragging a field in the Textures window updates every
  material using the texture each frame (its images at 256 during the drag, the set size on release); before,
  they changed only on release.

**Changed**
- The Tile layer reads the texel its tiled point lands in (it read one half a cell off), so its images can
  differ slightly from before.

**Checked**
- New tests (round 53): a point evaluation equals the planes at every texel centre for every layer type (Warp
  and Tile within a mean 0.01); the live normal within 10 degrees of the image's normal; up close a mortar edge
  is sharp with Live Shading (0 in-between pixels across a row) and blurred without (13), and far away both
  give the same pixels; dragging a field gives the material a new 256 image every frame and the 1024 one on
  release; the preview's Edge Wear as wide as at full size; 12 stacked Warp / Tile layers in linear time; an
  axis material's scale and Mirror wrapping keep the image; the preview is per texture; Live Shading saves;
  NaN coordinates stay finite.

**Review (code reviewer)**: four major and several minor findings, all fixed but the frame-time target (2.4x for
a whole close-up frame, documented): extra planes kept for every texture, texel-sized filters 8x wide in the
drag preview, axis materials going live while minified, stacked Warp / Tile doubling per layer, a global
preview flag, every Materials preview redrawn on any texture edit, Live Shading toggles re-evaluating images,
Mirror / Clamp / Closest, Generated mapping, a hard switch line (now a crossfade).
- Stress `procedural_textures`: a point of the Bricks preset about 290 ns, 485 ns with its normal (one
  thread); a 1080p frame made entirely of magnified texels 2.4x the image-only one (the spec's target was
  under 2x: not met, see the spec's As built); the odd-settings fuzz now also evaluates every layer type at
  points: 147 runs, 0 problems. 1024 x 1024, 8 layers: 108 ms (unchanged).
- Unit checks: Windows 8,471, 0 failed (447 tests); the whole suite in render-cache verify mode: 8,472,
  0 failed, 0 stale pictures. Linux with libraries: 8,419, 0 failed. Sanitizer build: 8,341, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures; `editor_render` idle
  1.33 ms.

## 2026-10-11 (round 52): large area lights, the gizmo after Set Origin, Select Coplanar (task 0030)

**Asked:** "the area light when width and height are large (lets say 10 x 10) still creates weird rendered
artifacts (black splotches on the object, even a default cube)", "when you use the lighting generate lighting
feature it causes the error"; "when I retarget the origin of any object, the transform tool and gizmo doesn't
automatically apply to the new origin"; "the ability to select coplanar faces"; "certain camera filter
properties like edge outline don't show up in the render view ... in the path traced system as well".

**Fixed**
- **Generate Lighting under a large area light no longer bakes splotches.** The bake took the light's range
  falloff from each random point on the light (on a 10 m light it varied tenfold across it), so 32 samples
  per texel left heavy noise that the denoiser turned into blotches. Now the unshadowed light is computed
  (no noise) and the shadow rays only decide how much of it gets through, the falloff at the centre as in the
  path tracer. A floor under a 10 m light: texel-to-texel noise from about 15 % to 0.1 %.
- **Large area lights light the sides of things in the Scene and Game views and F12 raster.** The
  rasterizer treated an area light as a point at its centre, so a cube's sides under a 10 m light were black;
  it now takes the light's whole surface (the path tracer's answer within 1 %), with the specular from the
  light's nearest point. Light probes take area lights the same way.
- **The gizmo moves to the new origin at once** after the Inspector's Set Origin > Apply: the change asked for
  no new frame, so the app waited for the next input with the gizmo on the old origin.

**Added**
- **Select Coplanar** (Edit Mode, Mesh menu; `editop select_coplanar`): adds every face lying in the plane of a
  selected face, joined to it or not (Blender's Select Similar > Coplanar). In vertex and edge mode it starts
  from the faces whose corners are selected and adds to the selection.

**Checked, no change needed**
- Camera filters in path-traced renders: an Edge Outline shows in F12 path-traced renders at 720p with the
  denoiser, on the CPU and both GPUs and in the Render window's Preview (now a regression test). The filters
  that don't reach the path tracer are the Retro Console's rasterizer stages (vertex snapping, affine
  textures, the texture override, screen-door transparency); task 0032 brings them to it.

**Checked**
- New tests (round 52): the area light's integral against a million-point reference in 13 cases (within 1 %);
  a cube's side under a 10 m light lit in the rasterizer as in the path tracer (0.098 vs 0.098); a glossy
  floor's highlight within a factor 0.5 to 1.6 of the path tracer's; a baked floor under a 10 m light smooth
  (0.1 % texel-to-texel) and as bright as the path tracer (0.412 vs 0.413); a change asks for a frame and
  the gizmo is on the new origin in it; Select Coplanar (joined, apart, raised, flipped, tilted, degenerate,
  the vertex selection kept); the Edge Outline in path-traced renders.
- Stress `shading`: an area light per pixel, one thread: 175 ns (rectangle), 210 ns (a wall cutting it),
  280 ns (0.3 m below), 360 ns (disk); a point light is about 10 ns.
- Unit checks: Windows 8,423, 0 failed (438 tests); the whole suite in render-cache verify mode: 8,424,
  0 failed, 0 stale pictures. Linux with libraries: 8,371, 0 failed. Sanitizer build: 8,293, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures; `editor_render` idle
  1.41 ms; `bake` and `probes` odd-settings runs: 0 problems.

## 2026-10-11 (round 51): axis materials (task 0029, ADR 0018)

**Asked:** "a material that will always have grass on top followed by dirt, regardless of rotation".

**Added**
- **Axis materials:** a material's **Axis Mode** (World or Local) and three parts, **Top**, **Side** and
  **Bottom** (material assets picked in the Inspector; empty uses the material's own maps). Surfaces facing
  up take Top, steep ones Side, downward ones Bottom, by the normal in the world (World: grass stays on top
  however the object turns) or the object's own space (Local: moss that turns with the stone); **Axis
  Angle** sets where Top turns into Side, **Axis Blend** how soft the edge is, **Axis Scale** the
  projection's tile size. The parts are projected triplanar, so no UVs are needed.
- The same in the Scene and Game views, F12 raster, the CPU path tracer and the GPU path tracer; the
  Materials window's previews show axis materials and procedurals (they showed neither).

**Checked**
- New tests (round 51): World mode puts Top up, Side on steep faces and Bottom underneath whatever the
  object's rotation; Local mode follows the object; Blend 0 is a hard edge and 0.3 mixes; a part's texture
  varies across a surface without UVs; the fields round-trip through a `.mat`, an edit undoes, and a part's
  edit changes the views' hash; an F12 raster render from above shows the Top, still after a quarter turn
  in World mode and the Side in Local mode; the CPU and both GPUs agree on an axis-material cube (mean
  difference 0.0006 of 0.187).
- Stress `shading`: an axis material with two parts 1.51× a plain one per point (target under 1.6×).
- Unit checks: Windows 8,371, 0 failed (431 tests); the whole suite in render-cache verify mode: 8,372,
  0 failed, 0 stale pictures. Linux with libraries: 8,321, 0 failed. Sanitizer build: 8,243, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures; `gpu` stress clean.

**Review (code reviewer)** found and these were fixed, each with a test: two axis materials naming each
other recursed without end (now cut while resolving, a part naming the parent itself is ignored); the GPU
material table's iterator could be invalidated while appending parts; the GPU projected procedural parts at
a different point than the CPU; the shadow-map (RSM) cache key missed the parts; a NaN normal shaded black
(now the Side part); the views' hash mixes the parts as resolved. Also: a compiler warning in the normal
output's Sobel (64-bit indices).

## 2026-10-11 (round 50): the Textures window (task 0028)

**Asked:** a tab dedicated to making procedural textures with a Blender-style modifier stack.

**Added**
- **Window > Textures** (`window Textures`): the project's texture assets as thumbnails (click to edit,
  drag onto an object in the Scene view or a Hierarchy row to use it); **New** from a preset (Bricks, Wood
  Floor, Tatami, Tiles, Noise, Checker, or empty); the size; **Assign to Selection**; a search.
- **The layer stack**, drawn like a camera's filter stack: each layer a foldout with its checkbox and a menu
  (Move Up, Move Down, Reset, Remove Layer), **Add Layer** by category (Generators, Filters), every field
  editable; each edit one undo step.
- **A preview** of an output (Albedo, Normal, Roughness, Height), 1× or tiled 2 × 2 to check the seams; it
  evaluates at 256 while a field is being dragged and at the texture's size on release.
- Double-clicking a `.ptex` in the Project window opens it in the Textures window. The window opens beside
  the Materials window, so the Scene view stays visible to drag onto.
- From the code review (Opus): the window first opened over the Scene view, and a drop then landed on an
  object in the hidden view's last picture (drops onto the Scene view now need this frame's picture);
  dragging a field re-evaluated the whole texture every frame for every material using it (they now
  re-resolve once, on release, while the preview follows at 256); the preview repacked the full image on
  every frame, idle ones included (it is cached and drawn from the smallest fitting mip); a deleted texture
  stayed selected; the name is the file's, not a field; the drag highlight now matches where a texture
  asset can land; the tests clean up the scratch project's textures.

**Checked**
- New tests (round 50), by real clicks: `window Textures` opens it and the layout round-trips with it;
  New > Bricks creates and selects the asset; Add Layer > Blur appends a Blur, its menu's Move Up moves
  it, its checkbox disables it, and undo takes each back; a value typed into a layer field changes the
  texture and the preview, and undo / redo restore both; a thumbnail dragged onto the Cube gives it the
  texture's maps; the preview is 256 while a field is dragged and 1024 after.
- Unit checks: Windows 8,343, 0 failed (423 tests); the whole suite in render-cache verify mode: 8,344,
  0 failed, 0 stale pictures. Linux with libraries: 8,295, 0 failed. Sanitizer build: 8,217, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures; `editor_render` idle
  1.39 ms (unchanged).

## 2026-10-11 (round 49): procedural texture assets (task 0027, ADR 0017)

**Asked:** a tab for making procedural textures (bricks, wood floors, tatami mats...) with a Blender-style
modifier stack. This round is the texture assets themselves; the Textures window is the next (0028).

**Added**
- **Texture assets, `Assets/Textures/<name>.ptex`:** a size (64–4096), a physical size (metres per tile),
  a normal strength, a seed, and **a stack of layers** evaluated in order to four images: **albedo**,
  **normal** (from the height), **roughness** and **height**.
- **21 layers.** Generators, all tiling seamlessly: Brick, Wood Floor, Tatami, Tiles (square, hexagon,
  herringbone), Voronoi, Noise, Wave, Checker, Stripes, Gradient, Image. Filters: Blur, Levels,
  Hue / Saturation, Invert, Warp, Tile, Edge Wear, Height AO, Color Ramp, Mask by Height. Each layer has
  a blend mode (Normal, Multiply, Add, Screen, Overlay, Height Max / Min), an opacity, the planes it
  writes, and can fade by the mask a Mask by Height layer below sets.
- **Materials use them by output:** `Assets/Textures/Bricks.ptex#albedo` (`#normal`, `#roughness`,
  `#height`); Assign to Selection (Project window, or `newtex` then the console) sets all three maps and
  the tiling from the physical size. Editing a texture redraws every view that shows it.
- **Exports carry them:** the outputs a material uses are written as PNGs beside an exported OBJ or glTF
  and referenced (by name in the MTL, so the folder can be moved). USD and FBX don't reference textures
  yet, so no images are written for them.
- **Assets behave like material assets:** saved when an edit settles and with the scene, renamed and moved
  with their references (materials and scene files), forgotten when deleted.
- Console: `newtex <name> [preset]` (Bricks, Wood Floor, Tatami, Tiles, Noise, Checker), `texlist`,
  `texlayer <texture> add|remove|move|reset|list`, `settex <texture> <Field> <value>` (a layer's field
  as `L0Rows`). **Texture edits undo and redo** (the asset's text travels with the undo step).
- From the code review (Opus): Hexagon and Herringbone tiles are real tilings now (the first draft's
  hexagons didn't repeat vertically and its herringbone was a square grid), Tatami lays true 1:2 mats
  (Basket, Running Bond, Pinwheel), and every count that decides a pattern's parity is rounded so the
  pattern repeats exactly; the seam test now checks sample(u, v) = sample(u + 1, v) = sample(u, v + 1)
  on 20 configurations instead of a step comparison that could not fail. Blur, Height AO and Edge Wear
  are separable (O(r) or O(1) per texel instead of O(r²): radius 64 at 4096² went from minutes to
  seconds) and copy only the planes they touch; evaluation runs outside the cache's lock; a texture's
  first load no longer bumps the texture generation in the middle of a render's resolve; NaN guards use
  `finite_bits` (a `v == v` test can be folded away under /fp:fast); integer fields are clamped where
  used; an unknown layer type (a newer file) is kept so the layers after it keep their fields; exported
  images get unique names per asset and each asset is evaluated once; assigning a texture to an object
  using a `.mat` asset gives that object its own copy instead of editing the shared asset.

**Checked**
- New tests (round 49): every generator tiles seamlessly and is deterministic; a Brick's mortar fraction
  follows its parameters, Tatami has its border, a Wood Floor its gaps; the stack order matters; a
  `.ptex` round-trips; a material with `Bricks.ptex#albedo` renders coloured, and a layer field and the
  normal strength redraw the view; OBJ and glTF exports write and reference the PNGs (by name in the MTL;
  the glTF read back by the importer) while the scene's material keeps the asset, and an STL export writes
  none; renaming the `.ptex` retargets the material; `texlayer` / `settex` edit the stack, the file is
  written when the edit settles, and undo / redo restore a field edit and an added layer.
- Unit checks: Windows 8,308, 0 failed (417 tests); the whole suite in render-cache verify mode: 8,309,
  0 failed, 0 stale pictures. Linux with libraries: 8,260, 0 failed. Sanitizer build: 8,182, 0 failed, no reports.
- Stress `procedural_textures` (new): 1024 x 1024 with 8 layers 108 ms on 32 threads (target under 150 ms);
  147 fuzzed runs (NaN / infinite / huge / negative fields on every layer type), 0 problems.
  `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

## 2026-10-11 (round 48): filters look the same at every resolution and during traces (task 0026, ADR 0016 part 2)

**Asked:** the camera-filter round, second part: a camera's filters should look the same in the Camera
Preview, the Game view and a render, and a filtered progressive trace shouldn't change its look as
samples accumulate.

**Changed**
- **Pixel-sized filter parameters scale with the picture's height** (1080p = 1x): Pixelate's cell, Edge
  Outline's thickness, Film Grain's size, CRT's scanline and phosphor pitch, Sharpen's radius. A 540p
  Preview and a 1080p render of the same camera now match (an outline covers 0.57 % of both).
- **A small view renders at its stack's resolution** (a 240-row retro stack in a 120-row inset) and is
  box-averaged into the view, instead of rendering at the inset's own rows.
- **Bloom on a progressive trace** reads the linear light at every resolve, so the glow doesn't grow
  with the samples; Color Grading's exposure applies in linear light when bloom's HDR plane is there.
- **The views' cache keys hash only the raster-relevant render settings:** changing the path tracer's
  Samples no longer redraws the Game view (changing Exposure still does).
- **The Game view's background is the world's gradient** (or sky / HDRI) when the scene's lighting is on;
  in Gradient mode it used the editor's fixed sky colours.
- A paused Game view keeps the grain's and the flicker's last pattern.

**Checked**
- New tests (round 48): an Edge Outline's covered fraction is equal at 540p and 1080p (0.0057 both); a
  Pixelate row has the same cell count at both sizes (54 / 55); Samples doesn't redraw the Game view,
  Exposure does (0 / 2 view renders); a Rendered Preview with bloom keeps its mean brightness from the
  first frames to the finished trace (152.6 / 152.6); a red world gradient reaches the Game view's sky;
  a paused Game view with Film Grain holds still. The `filter_layout` test now expects the stack's
  resolution for a small view.
- From the code review (Opus), three majors fixed: the Live Preview and the Rendered Camera Preview
  keyed on the raster-only settings too, so changing Samples, Max Bounces or the denoiser no longer
  restarted them (a trace-settings hash is mixed into their keys; a test changes Max Bounces); Color
  Grading's new HDR path re-encoded every pixel from the HDR plane, wiping a glow, an outline, depth of
  field or a Solid Color clear drawn after it (it now re-encodes only pixels whose colour still is the
  plane's encoding; a test checks a glow and a solid clear survive); the Fill layout's width was
  unbounded for a squeezed view (both sides are capped, and a letterboxed stack scales by one factor so
  its pixels stay square). Also: the linear buffer is taken once per progressive resolve, a stepped
  paused Game view keys on its time, and the preview's header is kept out of the bloom measurement.
  Note: a view smaller than its stack's resolution now renders and traces up to 1080p rows for it (a
  360-row Rendered Preview of a 1080-row retro stack traces 1920 x 1080).
- Unit checks: Windows 8,205, 0 failed (408 tests); the whole suite in render-cache verify mode: 8,206,
  0 failed, 0 stale pictures. Linux with libraries: 8,156, 0 failed. Sanitizer build: the
  round's tests 21 checks, 0 failed (the full run before the review fixes failed only in the two tests
  those fixes changed; CI's job covers the final code). Stress `filters`:
  300 fuzzed stacks, 0 problems; `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

## 2026-10-11 (round 47): the Active Camera, and filters wherever a camera's picture shows (task 0025, ADR 0016)

**Asked:** camera filters were missing from the Game view, the Camera Preview and the Render window.

**Found:** which camera "the" camera is was decided by name ("Main Camera", else the first), while the
Camera Preview showed the selected one: a filter on the selected camera showed in the Preview and not
in the Game view. The Scene view's filters ran with no camera (no exposure, clear colour or fog). Path-
traced pictures carried no depth or ids, so Edge Outline and the Retro fog were skipped in the Rendered
Preview and a path-traced F12, and Solid Color was ignored there. F12 raster without filters skipped
depth of field.

**Added / fixed**
- **An explicit Active Camera** per scene (Blender's scene camera, Unity's MainCamera tag): "Set as
  Active Camera" in the Camera inspector and the Hierarchy menu, a camera dropdown in the Game view's
  bar (replacing "Display 1"), `activecamera <name>|none` in the console, Ctrl+0 (Ctrl+Numpad0) in the
  Blender keymap; saved with the scene, one undo step, cleared when the camera is deleted. Without one
  the old name rule applies, so old scenes behave as before.
- **The path tracers (CPU and GPU) record the first hit's distance and object id,** and a traced picture
  gets a real `FilterFrame` with depth (as the rasterizer's z / w) and ids: Edge Outline and the Retro
  fog now work in a path-traced F12 and the Rendered Camera Preview. A camera's Solid Color fills the
  traced background (camera rays only; the lighting is unchanged).
- **The Scene view's filters look through the active camera** for exposure, Solid Color clear and far
  clip (the editor's projection stays; no depth of field while working). **F12 raster without filters**
  applies depth of field before the supersampling downsample.

**Checked**
- New tests (round 47): a filter on a second camera shows in the Game view only once it is the Active
  Camera, and undo restores the name rule; the Camera Preview and the Game view both follow the active
  camera's filters; the Scene view with filters takes the camera's exposure (+18 % mean brightness at
  +4 EV under Filmic) and Solid Color (a red sky pixel); a path-traced F12 with Edge Outline has 591
  near-black pixels against 1 without, and Solid Color fills its background; F12 raster depth of field
  lowers the neighbour difference 0.47 → 0.36; the active camera round-trips through a scene file and is
  cleared by deleting the camera; the console command reports, clears and refuses a non-camera.
- From the code review (Opus): the Game view's camera dropdown kept its labels in locals that died
  before the popup read them (a use after free the first time it was opened); they live in the editor
  now, the list shows only cameras the view could show, and a test opens it with mouse clicks and picks
  a camera. On a GPU the traced depth plane was a mean divided by a sample count that lags the sums
  (inflated fog between merges) and a mean that counted sky samples at 1e6 (an unfogged ring around
  distant objects): the plane now holds the last hitting sample's distance, consistent with the id. The
  hit point is rebuilt from the near plane as the tracer traces it (orthographic cameras too); the
  traced Solid Color is divided by the camera's exposure so it matches the raster clear; the far clip
  is kept with the render; `Scene::clear` resets the active camera and a dangling id in a file is
  dropped at load. A test checks outlines in the Rendered Camera Preview too. Note: F12 raster depth of
  field runs on the supersampled image (33 million pixels at 4x 1080p), a second or two.
- Unit checks: Windows 8,179, 0 failed (399 tests); the whole suite in render-cache verify mode: 8,180,
  0 failed, 0 stale pictures. Linux with libraries: 8,131, 0 failed. Sanitizer build (before the review
  fixes): 8,045, 0 failed, no reports. Stress `filters`: 300 fuzzed stacks, 0 problems; `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures; `editor_render` unchanged within noise.

## 2026-10-11 (round 46): render-frame waste (task 0024)

**Asked:** the editor-performance round, second part: the frames that really render (orbiting, dragging,
editing a material, playing) did work that isn't rendering.

**Changed**
- **One filter stack per camera,** rebuilt only when one of its filters changes (a stack is a
  `std::function` per pass, and every view asked for a new one on every render). The Rendered Camera
  Preview and F12 use it too.
- **One filter target per view** (Scene, Game, Camera Preview, F12): two filtered views of different
  sizes used to reallocate a shared one every frame. The Profiler counts resizes and stack builds.
- **The rasterizer resolves each material's textures once per flush** and shades through them,
  instead of a version check and an atomic read per pixel; its per-item scratch vectors are kept
  between flushes and the per-chunk tile table is thread-local.
- **The sun's shadow casters are copied only when the map is rendered** (pointers until the cache
  check). The RSM already did.
- Not built: early-outs for the Scene view's icons and z-fight outlines on a cache hit (they draw
  after the cached picture, so they must draw every frame); recorded in the spec.

**Checked**
- New tests (round 46): the resolved-texture shading equals the per-pixel path on 64 points of a
  UV-grid material; three filtered views keep their own targets (2 resizes to warm up, 0 afterwards);
  a camera's stack is built once and once more after a filter field changes; the shadow map still
  renders once per change.
- Unit checks: Windows 8,148, 0 failed (390 tests); the whole suite in render-cache verify mode: 8,149,
  0 failed, 0 stale pictures. Linux with libraries: 8,100, 0 failed. Sanitizer build: 8,021, 0 failed,
  no reports.
- Stress `editor_render` on the final code, quiet machine: idle 1.31 ms, orbit 7.38, moving an object
  18.13, editing a material 18.13, playing 11.88 (task 0023's 1.30 / 7.32 / 18.44 / 18.33 / 12.13: the
  same within noise, the waste being allocation and copies that were small next to the rendering);
  shadow renders per row unchanged (1 for a move and a material edit, 0 otherwise). `shading`, `raster`
  and `filters` within their usual numbers (deferred 1080p 10.5 ms, 5.2M triangles at 60 FPS, the PS1
  look 2.5 ms). `editor_fuzz` in verify mode: no crash, 0 stale pictures.
- From the code review (Opus): the filter-target and stack-build tests measured idle frames, which are
  cache hits that render nothing; they move an object every frame now and check that the views really
  rendered. The Scene view no longer copies the cached stack to change its fit (`filter_layout` takes
  the fit apart from the stack); the cache is trimmed between frames, never during a lookup.

## 2026-10-11 (round 45): idle editor frames (task 0023, ADR 0015)

**Asked:** improve the editor's performance, the Scene view first. This round is the idle frame
(nothing changed, the mouse over a panel); the next (task 0024) is the frames that really render.

**Found:** pass 6 had called the 2.1 ms idle frame "almost all hash". A per-phase profile said the
scene hash was about 0.3 ms of it; the rest was painting the UI: every icon rasterized from its
primitives on every draw (the tab bars alone 0.38 ms), 2.5 million pixels of fills the views painted
over anyway, the Project window listing folders from disk every frame, the material library copied and
sorted twice a frame.

**Changed**
- **The scene hash's per-object memo (ADR 0015):** a component says what the views read of it
  (`render_role()`: one virtual call instead of a `dynamic_cast` per type, which was most of the cost),
  each object remembers the hash of its world matrix, flags and materials keyed by a transform version
  (bumped wherever the world matrix goes stale, reparenting included) and the material versions, the
  three evaluated-mesh keys come from one pass, and the Scene view's filter camera takes the frame's
  scene hash. Lights, probe volumes and modifiers are hashed every time, so the memo can't go stale
  on them. The hash at 10,000 meshes: 0.46 ms (the idle frame there 4.8 → 2.7 ms).
- **Icons** are rasterized once per size into a coverage mask and blended through it.
- **Fills:** the frame fills only the border strips around the dock area (the dock fills its
  splitters), and no panel is painted behind the Scene and Game views, which paint every pixel.
- **The Project window** keeps its folder listings with the file list (refreshed every 2 s or on a
  change); `loaded_material_assets()` is rebuilt only when the library changes; the Materials window
  counts users in one pass; `Canvas::circle` walks the ring, not its bounding box; the Statistics panel
  shows the rasterizer's binning time (`ms_bin`, the binning part of the setup tasks over every chunk).
- **Not changed, on purpose:** the hasher's byte-at-a-time mixing (its values are in every saved
  bake's key); no per-frame memo for `main_camera()` or "Lighting out of date" (the serial they would
  key on doesn't change on edits). The spec's 1.0 ms idle target is not met: 1.3 ms, the rest being
  legitimate drawing (`docs/PERFORMANCE.md`, pass 7, lists it); recorded in the spec for sign-off.

**Checked**
- New tests (round 45): `evaluated_keys` equals the three `evaluated_key` calls on 300 modifier
  combinations; the transform version bumps on a move, a parent's move and reparenting; the memoised
  hash equals a from-scratch hash and changes after 23 kinds of change (a gizmo drag, the position
  field, undo, redo, parenting, a parent's move, unparenting, material, light, modifier added / changed
  / disabled / Show in Renders, renderer disabled / enabled / flag, a material slot, an object
  deactivated / reactivated / deleted / created, a probe volume added / changed), in verify mode with 0
  stale pictures; a camera filter field re-renders the Game view; the main camera follows a rename, a
  disabled Camera and a deactivated object; Statistics shows the binning time.
- From the code review (Opus): a small ring in `Canvas::circle` blended a few pixels twice (one run
  per row when the hole is narrow); an empty root dock leaf went unpainted without the frame's clear
  (filled now); an icon's anti-aliased edge past its rect was lost by the mask (the mask is padded by
  3 px); pixel-equality tests for the icon masks (within 2 of a direct draw) and `Canvas::circle`
  (identical to the bounding-box walk).
- Unit checks: Windows 8,139, 0 failed (386 tests); the whole suite in render-cache verify mode:
  8,140, 0 failed, 0 stale pictures. Linux with libraries: 8,091, 0 failed. Sanitizer build: 8,009,
  0 failed, no reports.
- Stress `editor_render`: idle 1.98 → 1.27 ms, mouse over the Hierarchy 2.01 → 1.25, orbit 8.49 →
  7.40, moving an object 20.99 → 18.42, editing a material 19.22 → 18.19. `editor_ui`: idle at 1,000
  meshes 1.48 → 1.11 ms, at 10,000 4.82 → 2.74 ms, scene hash at 10,000 meshes 0.46 ms (0.56 from
  scratch). `editor_fuzz` in verify mode: 6,000 frames, 10,621 events, no crash, 0 stale pictures.

## 2026-10-11 (round 44): area lights bake smoothly; Preview Lighting isn't blocky on small objects (task 0022)

**Asked:** adjusting an area light's Width and Height made objects look pixel-like.

**Found:** every change restarts Preview Lighting at its roughest pass: a quarter of the resolution (a
small sphere got a few texels across), 16 samples, and the area light sampled with at most 8 random
points. It looked blotchy for about a second before refining. The real bake sampled area lights
randomly too.

**Fixed**
- **Area lights are sampled on a jittered grid** over their surface in the baker (stratified, with a
  per-texel offset; a concentric mapping for disks). At 16 samples a floor under a 2 x 1 m light is
  within 3.7% of a converged bake on average, against 5.8% with random points.
- **Preview Lighting** gives every object a chart at least 32 texels across, and samples area lights
  32 to 64 times instead of at most 8. That is most of the fix: a small sphere had a few texels across
  and 8 noisy samples.
- **From the code review:** the preview's 32-texel minimum chart was bigger than a 16-texel page (Max
  Lightmap Size allows 16) and wrote past it. A layout now cuts its minimum chart down to the page
  size, for every caller, and a test lays out and previews with 16-texel pages. The baker also takes
  exactly the Direct Samples the user set (not rounded up to a grid).

**Checked**
- Unit checks: Windows 8,108, 0 failed (378 tests), twice. Linux with libraries: 8,060, 0 failed. Sanitizer build: 7,978, 0 failed, no reports (run before the review fix; the final code's run is CI's). Windows dependency-free: 7,980, 0 failed.

## 2026-10-11 (round 43): Baked lights exist only through the bake (task 0021)

**Asked:** don't rasterize lights whose Mode is Baked; the bake represents them. Mixed and Realtime
stay live. Before a bake, Preview Lighting is what shows them, as Bakery's Realtime Preview does.

**Changed**
- **A Baked light is left out of the rasterized views:** no direct light or highlight, no shadow map,
  no reflective shadow map (so no live bounce from it). Until now it was skipped only on lightmapped
  or baked-probe surfaces, and still lit everything else.
- **Before a bake it lights nothing;** turn on Preview Lighting or press Generate Lighting.
- **Unchanged:** its icon and gizmo, the bake itself, and the path tracer (F12 path traced, the Rendered
  Camera Preview), which computes true GI.
- **Solid shading** leaves Baked lights out too, and live GI picks its sun the way the shadow map does
  (the first directional light that isn't Baked).

**Fixed (found by the code review)**
- **The first version also dropped Baked lights from the path tracer,** which builds its lights the
  same way. A path-traced F12 render of a Baked-light scene came out sky-lit. The rasterizer and the
  path tracer now take different light lists, and a test renders F12 through the editor to check.
- **The light's Mode help text** said Baked lights still drew on unmapped objects.

**Checked**
- Unit checks: Windows 8,092, 0 failed (375 tests), twice. Render cache verify mode: 0 stale pictures.
- Linux with libraries: 8,044, 0 failed. Sanitizer build: 7,966, 0 failed, no reports. Windows dependency-free: 7,964, 0 failed.

## 2026-10-11 (round 42): the sanitizer CI job's flaky pilot test

**Asked:** the Linux sanitizer job was failing on the PR that brings tasks 0011-0020 to main.

**Fixed**
- **"Stopping the pilot brings back the editor view" failed only on the sanitizer runner:** 7,045
  pixels differed, all in one 294 x 24 box at the top of the Scene view. The test's click helper left
  the mouse resting on the Statistics toolbar button, and on a slow run the button's tooltip delay
  elapsed, so the tooltip drew over the view. The helper now parks the mouse outside the window after a
  click. Reproduced and fixed under the sanitizer build on 3 threads (CI's count).

**Checked**
- The pilot tests pass under the sanitizer build on 3 threads (twice), and on Windows.

## 2026-10-10 (round 41): manual lighting updates and a rough preview, like Bakery's (task 0020)

**Asked:** stop recomputing lighting on every change while moving things around, update it only when
asked, and add an optional very rough preview like Bakery's Realtime Preview.

**Added**
- **Lighting Updates** (Lighting window): Manual, the default, or Automatic (the behaviour until now).
  - **In Manual,** Realtime GI, realtime lightmaps, live probes and Auto Generate don't react to changes.
    Direct light and shadows stay live.
  - **The last lighting stays up:** a baked object that moved keeps its lightmap, and the last realtime
    lightmaps and live probes stay shown. "Lighting out of date" says when it's stale.
- **Update Lighting** (button, Ctrl+Shift+L, `lighting update`) runs Realtime GI once (the grid, the
  RSM, the realtime lightmaps, the live probes) and freezes again. Generate Lighting still bakes.
- **Preview Lighting** (a toggle, `lighting preview on|off`):
  - **What:** a rough progressive bake at a quarter of the Lightmap Resolution, 16, then 64, then 256
    samples, denoised, on the GPU when Render > Device is GPU Compute.
  - **When:** it starts half a second after changes stop and runs about 8 ms a frame.
  - **Shown, not saved:** it appears in place of the bake and is never saved.
- **`lighting mode manual|automatic`**, and the Learn lesson and guide explain both modes.
- **In Manual, one automatic update** when Realtime GI is turned on or a scene opens, so it never starts
  empty.
- **"Lighting out of date (Update Lighting)"** in the status bar when frozen Realtime GI is stale (with
  or without a bake).

**Fixed (found by the code review)**
- **A fresh Generate Lighting was covered by the preview again.** The preview now stands down while
  the real bake is current.
- **Frozen frames dropped a Realtime sun's bounce on lightmaps** (and the frozen live probes, and
  specular occlusion). A frozen frame is now the last updated picture, bit for bit.
- **Undo and redo dropped the frozen realtime lightmaps** (a clone gets a new scene serial). They're now
  keyed by a scene id that undo keeps.
- **In Automatic with a stale bake, the preview was never attached.** Staleness is now judged on what's
  shown.
- **Smaller fixes:**
  - A mesh edit in Manual drops the object's old map (only a move keeps it).
  - Opening a scene clears the last one's preview.
  - The preview's sample count resets on a change.
  - The Lighting window's out-of-date text fits Manual.
  - `lighting mode auto` works.
  - The stress section puts its scene back after the drag rows.
  - Two tests that couldn't fail now can: Auto Generate gets its real second, and "never saved" checks
    a real folder.
- **Not fixed:** each preview stage lays out and builds its scene again in one frame. That's a hitch
  on big scenes while orbiting during a preview.

**Checked**
- Unit checks: Windows 8,066, 0 failed (369 tests), with Automatic and with Manual as the default
  (`BLENDITY_LIGHTING_UPDATES`). Render cache verify mode: 0 stale pictures.
- Linux with libraries: 8,018, 0 failed. Sanitizer build: 7,940, 0 failed, no reports.
- Stress `editor_render`, 1600 × 1000, Realtime GI on, dragging a static object:

  | Mode | ms a frame |
  |---|---|
  | Automatic | 84–108 |
  | Manual | 22–27 (the same as with no GI) |
  | Manual + Preview Lighting | 22–23 (it waits for the drag to stop) |

  After the drag, the GPU preview shows its first pass in 1.4 s and reaches 256 samples in 3.4 s. The
  first run after a kernel change compiles the pipeline once (11 s).

## 2026-10-10 (round 40): lighting without lag: baked GI that costs nothing live, realtime lightmaps, GI and baking on the GPU, no path-traced Scene view (tasks 0016-0019)

**Asked:**
- remove the Scene view's path-traced Rendered mode (too slow);
- the approved tasks 0016–0018: realtime lightmaps first with a fallback, baked lighting with no lag,
  and GI, probes and baking on the GPU where it's faster.

**Added**
- **Realtime lightmaps (0016):** Contribute GI objects get a low-resolution map (Realtime Resolution, 2
  texels per metre by default).
  - Voxel GI lights it a slice at a time when the lighting changes, and it's read like a baked map.
  - **Orbit cost:** +1–3 ms at 1080p over no GI, against +12–14 ms traced per pixel.
  - **Look:** the same at any zoom (the Scene view's blocks are gone on mapped objects).
  - **Fallback:** objects without a map keep the per-pixel pass.
- **No live work once baked (0017):** Realtime GI traces only what a bake doesn't hold.
  - **Cases:** nothing baked, the scene changed since the bake, or a Realtime Directional Light.
  - **Otherwise idle:** it traces nothing, and the views equal Realtime GI off.
  - **Status:** the Lighting window's "Live GI:" line says which case applies, and the per-pixel cost.
- **Cheaper probe sampling (0017):** +4 ms at 1080p with every object probe-lit, from +15 ms (a brick
  hint, the cube's axes once per sample, one sample per 2 × 2 pixel quad).
- **GI on the GPU (0018, ADR 0014):** a Vulkan compute kernel for voxel GI's gather.
  - **What uses it:** realtime lightmaps and live probes. **GI Device** (Auto / CPU / GPU) chooses.
  - **Speed:** 4–8.6× faster on the RTX 4070 SUPER, 3.2–6.2× on the 3060 Ti. A whole pass fits in a
    frame.
- **Baking on the GPU (0018):** with Render > Device = GPU Compute, Generate Lighting traces lightmap
  texels and probe rays on the path tracer's kernel.
  - **Speed:** 8.6× (4070 SUPER) and 6× (3060 Ti) on the tracing.
  - **Agreement:** totals match the CPU's within 0.01%.
- **A lighting guide** (`docs/guides/lighting.md`) and a Learn lesson, "Baked and Realtime Lighting".

**Removed**
- **The Scene view's Rendered (path-traced) mode (0019).** `shading rendered` warns and shows Shaded.
  Path tracing stays in F12, the Render window and the Camera Preview's Rendered toggle.

**Fixed (found while building it)**
- **A small object's realtime lightmap had a black patch:** big texels gathering from inside the floor
  saw nothing. They now take their chart neighbours' light, as invalid probes do. Realtime charts are
  also at least 24 texels across.
- **Shared probe samples depended on thread scheduling** (row pairs split between threads). Rows now go
  out in pairs.
- **Changing GI Device didn't gather again;** it's in the live keys now.
- **From the code review:**
  - **A Sky world drawn from the sun was missing from the GI keys,** so turning the sun didn't re-gather
    with Bounce off, and the GPU kept the old sky. The keys now include the sky's own key.
  - **Probe bakes on the GPU could allocate gigabytes** at high sample counts. Slices are now sized by
    rays (about 8M).
  - **Test limits had been tuned to one build** (the probe quad's worst difference, the small ball's
    sky share). They're now principled bounds, with a comment.
  - **Smaller fixes:**
    - The GPU kernel's read-back buffer is freed.
    - Choosing a GI Device again retries the GPU after an error.
    - Unwrapped realtime charts keep their size when an object moves.
    - Another scene's published maps are never shown.
    - The GPU bake's random sequences follow each texel, not its place in a batch.
    - Realtime lightmaps are capped at about 2M texels.
    - Stale "Rendered" tips are gone.

**Not as the approved specs said (recorded in each spec's "As built"; all signed off by the user on 2026-10-10):**
- **0016:** no voxel ray jitter or occlusion fade; the maps' blur and bilinear filter smooth mapped
  surfaces, and the fallback keeps its look.
- **0017:** shared probe samples differ from per-pixel ones by up to 8 levels on 0.03% of pixels.
- **0018:**
  - GPU and CPU gathers can differ by one or two grazing rays per point (means within 0.002).
  - The per-pixel pass stays on the CPU.

**Checked**
- Unit checks:
  - Windows 7,994, 0 failed (358 tests), and the same with GI Device Auto on the GPU
    (`BLENDITY_GI_DEVICE=auto`);
  - Windows dependency-free (CI's build): 7,866, 0 failed.
  - Render cache verify mode: 0 stale pictures.
- The code review's open minors, not done:
  - a GPU failure mid-pass makes the CPU finish that pass in one frame (a one-off hitch);
  - criterion 5 of task 0016 (fallback pictures bit-identical to the old pass) is checked by traced
    receiver counts, not pixels.
- Linux with libraries: 7,946, 0 failed. Sanitizer build: 7,868, 0 failed, no reports.
- Stress:
  - `vgi`: a 1080p orbit reading realtime lightmaps costs +0.9–3.1 ms over no GI, against +12–14 ms
    traced. A pass of 9,377 texels takes 4.8 ms on the CPU.
  - `probes`: deferred shading at 1080p is 13.7 ms without probes and 17.7 ms with every object
    probe-lit (29.1 ms before).
  - `gpu_gi`: gathers and bakes as above (ADR 0014's table).

## 2026-10-10 (round 39): realtime lightmaps, baked lighting without lag, GI on the GPU (specs only)

**Asked:**
- realtime lightmaps, the faster option first with a fallback;
- GI and probes on the GPU if faster;
- baked lighting that doesn't lag the Scene view;
- why realtime GI looks pixel-like in the Scene view but not in the Game view.

**Found:**
- **The Scene view's blocks** come from the per-pixel GI pass tracing at ¼ of the screen resolution.
  Objects are small on screen there, so each 4 × 4-pixel GI cell shows.
  - GI Resolution = Half makes them smaller; a 256 voxel grid doesn't.
  - A dark band where objects meet the floor comes from the voxel steps.
- **Per-frame costs at 1080p:**
  - a baked lightmap: about 1 ms;
  - live voxel GI: about 12 ms, even where a bake covers the scene;
  - probe sampling: up to 15 ms.

**Drafted (for approval, no code yet):**
- **Task 0016:** realtime lightmaps per texel, with the per-pixel pass as the fallback.
- **Task 0017:** no live GI work once the bake covers the scene, cheaper probe sampling, and a lighting
  guide.
- **Task 0018:** GI, probes and baking on Vulkan compute, each behind a measured spike.

## 2026-10-10 (round 38): GPU viewport, a measured go / no-go spike (task 0015)

**Asked:** run the editor's views on the GPU, after the CPU wins, if it pays off.

**Added**
- **Stress section `gpu_viewport`:** a 1080p view on the CPU (deferred and the cached shadow map)
  against the same picture's work on each GPU. The GPU side is the Vulkan path tracer at 1 sample with
  no bounces, split into clears, render plus read-back, and resolve.
- **ADR 0013** (proposed) and **task 0015's spec** (no viewport code).

**Result: no-go.**
- **The bar:** two views at ≤ 0.50× the CPU's time.
- **The CPU frame:** 19.6 ms.
- **The GPU's render plus read-back alone:**
  - RTX 4070 SUPER: 10.6–11.8 ms (0.54–0.60×);
  - RTX 3060 Ti: 15.4–17.5 ms (0.79–0.89×).
- **End to end through the path tracer:** 31–39 ms.
- **Recommendation:** keep the views on the CPU and look next at voxel GI and probe sampling (about
  12–15 ms each at 1080p).

## 2026-10-10 (round 37): probe volumes, like Unity 6's Adaptive Probe Volumes (task 0014)

**Asked:** add probe volumes, kept live by the voxel GI.

**Added**
- **Probe Volume component:** Global (around everything that contributes GI) or a box that follows the
  object's rotation and scale.
- **Placement:** bricks of 4 × 4 × 4 probes, fine near geometry and coarse in open space. Probes inside
  geometry move out (virtual offset) or are marked invalid and filled from their neighbours.
- **Each probe is an ambient cube:**
  - the light from geometry per axis;
  - the share of the sky each axis sees, so a new sky colour relights them with no re-bake.
- **Baked** by Generate Lighting after the lightmaps, in slices so the editor stays responsive.
- **Live:** Realtime GI keeps the probes current, 4,096 probes a frame, with no bake needed.
- **Shading:** per pixel, with normal bias, view bias and leak reduction.
  - **Receive GI** (MeshRenderer) picks Lightmaps or Light Probes.
  - Objects that don't contribute GI always use the probes.
- **Settings:** min and max probe spacing, the biases, leak reduction.
- **Debug display:** `probes show` / `probes hide`, or Show Probes in the Lighting window.

**Fixed (found by the tests and the review)**
- **Enormous volumes and NaN settings overflowed float-to-int casts** (the sanitizer caught them): in
  placement, in the live update's voxel lookup and in the volume box's key. The probe cap could also be
  broken. Counts are now in double, cells are clamped before the cast, the key hashes the setting's
  bits, and the cap is strict.
- **Live probes never showed with Realtime GI on**, yet they re-rendered the views. Probe-lit objects
  now keep the probes (as in APV), and the live probes are what follow lighting changes.
- **Light counted twice with Baked GI off:** baked probes are now used only with Baked GI on. Probes
  from another bake are refused on open.
- **A re-bake without a volume** left the old `ProbeVolume.bin` to come back on open; it is removed.
  Probes are saved even when there are no lightmaps.
- **The probe bake ran on the UI thread in one go:** it now runs in slices within the bake's frame
  budget, and the bake's mesh copies are freed after it.
- **Global volumes put probes on the floor plane:** the pad is now 1.5 spacings.
- **Thin walls leaked light:** APV's normal-based leak reduction was added (on by default).
- **Live probes near a floor saw through it:** they gather from the nearest free voxel.
- **Receive GI didn't redraw the views.**
- **Smaller fixes:**
  - Indirect Intensity applies to the probes.
  - A cell with every valid probe behind the surface falls back to their plain average.
  - The virtual offset is limited to ¾ of a spacing.
  - Receive GI = Light Probes objects get no lightmap chart.
  - The live key covers both views' voxel GI and only the settings that change the gathers.
  - The volume box is remembered while nothing changes.
- **Tests:**
  - **A NaN volume was a valid point box on GCC:** `AABB::add` drops NaN there, so the placement test
    sets the box directly.
  - **"Stopping the pilot brings back the editor view" failed under the sanitizer:** it compared the
    Statistics overlay, whose frame time differs on a slow run. Statistics is now off for that test,
    and the view comes back bit for bit.

**Not as the approved spec said (awaits sign-off; recorded in the spec and ADR 0012):**
- Each probe is an ambient cube, not SH L1 / L2.
- Probes win over per-pixel voxel GI for the objects they light.
- Live probes restart as a whole, not only near a change.
- The debug display draws dots.

**Checked**
- Unit checks: Windows 7,880, 0 failed (344 tests; round 37 has 29). Render cache verify mode: 0 stale
  pictures.
- Linux with libraries: 7,856, 0 failed. Sanitizer build (ASan + UBSan + float-cast-overflow): 7,778, 0 failed, no reports.
- Stress `probes` (32 threads):
  - **Scene:** 38,144 probes in 596 bricks, 2,704 inside geometry;
  - **Time:** place 9.2 ms, validate 11.1 ms, bake 24.2 ms (64 rays);
  - **Memory:** 206 bytes per probe;
  - **Live:** 1.48 ms per 4,096-probe slice;
  - **Shading at 1080p:** deferred shading 13.6 ms without probes and 29.1 ms with every object
    probe-lit (a follow-up);
  - **Fuzz:** 100 odd volumes, 0 problems.

## 2026-10-10 (round 36): realtime GI from the research paper: voxel-based global illumination (task 0013)

**Asked:** implement the first paper in `research/papers/`, Thiedemann et al., *Voxel-based Global
Illumination* (I3D 2011), working with the baking so that changing the lighting never needs a manual
re-bake.

**Added**
- **Realtime GI** (Lighting window): one bounce of the sun's light and the sky's occlusion, live in
  every rasterized view.
- **The paper's pieces, on the CPU:**
  - **Voxel grid (section 3):** the scene voxelized into columns of 128 bits, with an exact conservative
    span voxelizer.
  - **Moving objects:** each object's voxels are kept, so a moved object is voxelized alone.
  - **Ray test (section 4):** a mip-map hierarchy and its ray walk.
  - **Bounce (section 5.1):** gathered through the sun's reflective shadow map.
  - **Sky:** the share of the sky that gets through gives directional occlusion; open ground keeps
    exactly today's light.
  - **Per pixel:** traced at ½ or ¼ resolution with 16 interleaved ray sets, a geometry-aware blur and
    an upsample.
- **With the baking:**
  - Lightmapped surfaces add the live bounce of lights the bake doesn't hold.
  - Objects changed since a bake, or a whole bake made stale by a light or world change, switch to live
    GI instead of flat ambient.
  - **Auto Generate** re-bakes a second after changes stop.
- **Settings:** voxel resolution, radius, rays, intensity, sky occlusion, bounce, GI resolution, RSM
  resolution, specular occlusion.

**Fixed (found by the tests and the review)**
- **Bounce depended on where the scene sat:** the sun's reflective shadow map flipped normals toward a
  camera at the world origin. It now faces the sun.
- **GI settings didn't redraw the views,** and they made the bake look out of date (which would have
  started Auto Generate). Only the settings a bake depends on count now.
- **Lightmapped surfaces:** they keep their baked specular. Receivers that take everything from a
  lightmap trace nothing.
- **Zero-area triangles** add no voxels.
- **Auto Generate's wake-up** was cleared in the same frame, so an idle editor never re-baked.
- **The Scene view and Game view shared one voxel / RSM cache** and rebuilt each other's every frame.
  Each kind of view now has its own.
- **The ray walks stall or skip far from the grid's corner:** they re-derived cells from rounded
  positions. They now step cell indices and pick children by crossing time. A test checks 20,000 rays
  over 256 voxels long on a 256 grid.
- **A pre-existing rasterizer bug:** a vertex far beyond float range cast infinity to int in tile
  binning, which the sanitizer caught.
- **Smaller fixes:**
  - The RSM holds the sun's light only.
  - The self-occlusion offset clears a voxel's diagonal.
  - Ray sets differ in elevation too.
  - The grid stays put while the scene fits it.
  - Objects that are gone are forgotten.
  - Non-casting objects don't occlude.
  - The lightmaps' out-of-date key covers bake settings only (a 0012 bake would show as out of date
    once).

**Not as the approved spec said (awaits sign-off; recorded in the spec and ADR 0011):**
- Realtime lightmaps are per pixel, not per texel: the per-pixel pass covers the same cases.
- The settings live in the Lighting window (`LightingSettings`).

**Checked**
- **Unit checks:** Windows 4,077, Linux 4,053, sanitizer build 3,975; 0 failed. 315 tests. The
  test-engineer wrote 17 tests and the review added 2:
  - **Voxelizer:** matches a double-precision triangle–box test, exactly.
  - **Hierarchy and ray walks:**
    - mips are the OR of the level below;
    - the hierarchical walk equals the column walk exactly on 100k rays, and on 20k rays longer than
      256 voxels;
    - it agrees with an independent 3D DDA on all of them;
    - the first hit is right going up and down.
  - **Gather:** RSM back-projection gathers lit points and not shadowed or back-facing ones.
  - **Pictures:**
    - a red wall bleeds onto the floor;
    - a box darkens the floor beside it, and far floor is bit-identical;
    - GI off is bit-identical;
    - deterministic, and 1 thread equals all threads.
  - **Caching:** orbiting re-voxelizes nothing; moving one object re-voxelizes only it.
  - **With the baking:**
    - a Realtime sun's bounce follows it on a lightmapped floor without a bake;
    - Auto Generate waits, re-bakes and sets its wake-up time.
  - **Settings:** save, load, undo; odd values are safe.
- **Verify mode:** 0 stale pictures.
- **Stress `vgi` (Windows):**
  - **Voxelizing:** 2.2 ms for 257 objects at 128, and 5 ms at 256.
  - **Ray walks, short rays:** the hierarchical walk equals the column walk (6.8 vs 7.4 Mrays/s on one
    thread).
  - **Ray walks, 40 m rays:** the hierarchical walk is 5x faster (5.4 vs 1.1 Mrays/s).
  - **RSM 256²:** 3 ms, only when the casters or the sun change.
  - **The GI pass adds:**
    - 720p, 8 rays, Quarter: 5.8 ms;
    - 1080p, 8 rays, Quarter: 11.8 ms (over the spec's ~10.5 ms budget);
    - 1080p, 4 rays, Quarter: 8.5 ms.
  - **Odd settings:** 200 runs, 0 problems.

## 2026-10-10 (round 35): baked lighting like Unity's (task 0012)

**Asked:** "a similar light baking system like Unity3D": lightmaps and light modes, the approved task 0012.
Voxel GI (0013) and probe volumes (0014) work with the same lightmaps, so changing the lighting won't
need a manual re-bake.

**Added**
- **Per object and per light** (all saved, undoable and settable with `set`):
  - **Contribute GI** on MeshRenderer, with **Scale In Lightmap** and **Generate Lightmap UVs**;
  - **Mode** on each Light: Realtime, Mixed or Baked.
- **A Lighting window** (Window menu, or `window Lighting`) holds:
  - **Generate Lighting** and Cancel, with a progress bar;
  - **Clear Baked Data**;
  - the Lightmapping settings: resolution in texels per unit, max size, padding, direct and indirect
    samples, bounces, denoise, indirect intensity, Auto Generate;
  - the Environment.

  Console: `bake start|cancel|clear`.
- **The baker (`Lightmapper`) runs on the path tracer:**
  - **Lightmap UVs:** generated per object (Smart UV Project + Pack Islands), sized so each object
    gets the asked density.
  - **Indirect light per texel:** cosine-weighted rays through the static objects, the Baked and Mixed
    lights and the sky, with every bounce. Realtime lights are left out, as in Unity.
  - **Direct light:** Baked lights only, with shadow rays.
  - **Finish:** a filter within each object's charts, then dilation into the padding.
  - **Runs** in slices per frame, cancellable, on its own copies of the meshes.
- **Shading:**
  - Lightmapped surfaces take their ambient light from the map.
  - Baked lights are skipped there in realtime.
  - Mixed lights keep their realtime light and shadows (Baked Indirect).
- **Files:** the lightmaps are saved next to the scene, as `<scene>/Lightmap-N.hdr` and
  `LightingData.bin`, and load with it.
- **Stale objects:** an object changed since the bake falls back to realtime ambient, and the status
  bar says **"Lighting out of date"** (ADR 0010).

**Fixed (found by the tests)**
- **Lightmaps had 21–64% fewer texels than asked.** The packed charts fill only part of their square.
  Charts are now sized from the area their UVs actually cover. A test checks density within 20% of
  Lightmap Resolution × Scale In Lightmap across shapes and sizes.
- **A NaN vertex gave NaN lightmap UVs.** Fast-math compiles `std::isfinite` away, so the check is now
  by bits.

**Fixed (from the review)**
- **The Window menu read past its shortcut list** once the Lighting window was added: undefined
  behaviour every time the menu opened. A compile-time check now ties the list to the windows.
- **A bake still running when another scene was opened or created** would have written its lightmaps
  into that scene's folder. Switching scenes now cancels it, and a new scene starts with no baked data.
- **The lighting folder came from the name stored in the scene**, not the file name. A renamed scene
  could share or delete another scene's lightmaps, and a name of "." pointed at the Scenes folder
  itself. It is now the file's name, and anything that isn't a plain name gives no folder.
- **A bake made before the scene had a file, or before Save As, was lost on save.** Saving now writes
  it next to the file, and so does the auto-save on exit.
- **Dragging a lightmapped object re-hashed its whole mesh every frame**, several times. The mesh part
  of the hash is now remembered by render-mesh serial (only the matrix is rehashed), and "Lighting out
  of date" is recomputed only when the scene or the lighting settings change. This also fixes a hash
  that could go stale after opening another scene.
- **Light skipping:** lightmapped surfaces skip the lights the maps were baked with, as they were
  baked. A light baked as Baked and switched to Realtime no longer shines twice, and one added since
  is no longer dropped.
- **A light on an object with its own non-contributing mesh was missing from the out-of-date check.**
- **A damaged `LightingData.bin` could ask for gigabytes.** Counts are now checked against the file's
  size.
- **Bounces were one too many:** the gather's own hit is the first bounce. With 0 bounces you get only
  the sky and direct light.
- **Smaller fixes:**
  - The content hash now includes per-face materials, sharp edges and smoothing.
  - Batches are sized by sample count, so a frame's budget holds at high sample counts.
  - A cancelled bake frees its memory.
  - Lightmaps are not used under the Scene view's headlight.
  - The stress section reports the filter + dilation time.
- **Five tests from the review:**
  - baking before saving;
  - a new scene during a bake;
  - a damaged file;
  - a lamp mesh with a Baked light;
  - a mode switched after the bake.

**Not done, from the review:**
- The texel rasterizer still lives inside the baker; task 0013 will expose it for realtime lightmaps.
- Texels are taken at texel centres, not conservatively, so slivers thinner than a texel get none;
  dilation covers them.
- The Scene view uses a lightmap only when its viewport mesh matches what was baked. With modifiers
  that differ between the viewport and renders it shows realtime light while the status says up to
  date.
- Laying out a very large bake (the UVs, the atlas) still runs on the main thread before the first
  slice.

**Not as the spec said:** the lightmaps are Radiance `.hdr` files, not `.exr`. Blendity can write EXR
only with Blender's libraries and has no EXR reader; `.hdr` is written and read without them.

**Checked**
- **Unit checks:** Windows 3,540, Linux 3,516, sanitizer build 3,438; 0 failed. 293 tests. The
  test-engineer wrote 19 tests, and the review added 5:
  - **UVs:** inside 0..1, no overlaps, padding, density.
  - **Colour bleeding:** a red wall tints the floor with a Baked or a Mixed sun, and a white wall
    doesn't.
  - **Accuracy:**
    - open sky exact;
    - under a covering slab, dark;
    - at a slab's edge within 0.013 of an 8,192-sample path-traced reference;
    - a Baked sun's direct light exact (colour × intensity × N·L), 0 in shadow.
  - **Light modes:**
    - a Baked sun alone lights the floor;
    - a Mixed sun keeps the realtime shadow (90% of the lit region in common with an unbaked scene);
    - a Realtime sun leaves the maps bit-identical.
  - **Lifecycle:**
    - save and reopen;
    - a moved box falls back, then comes back bit-identical;
    - `bake clear` restores the picture bit-identically;
    - dynamic objects untouched;
    - path-traced F12 unchanged.
  - **Robustness and undo:** cancel, undo, NaN and odd settings.
- **Verify mode:** the suite has 0 stale pictures (the view caches re-render when a bake finishes,
  clears or loads).
- **Stress `bake` (Windows):**

| Static objects | Texels | Bake time |
|---|---|---|
| 10 | 12k | 0.03 s |
| 100 | 122k | 0.25 s |
| 1,000 | 1.24M | 2.2 s (3 lightmaps) |

  - That is about 500k texels/s and 42–56 Mrays/s, of which filtering and dilation take 3–140 ms.
  - Shading a lightmap costs 1–1.4 ms more at 1080p (10.1 → 11.0–11.5 ms).
  - Odd settings: 30 runs, 0 problems.

## 2026-10-10 (round 34): a faster editor: views and shadows reused until something changes (task 0011)

**Asked:** improve performance for the Scene view and the editor (task 0011 of the approved plan).

**Found**
- **The sun's shadow map was rendered on every shaded render**, up to three times a frame, at about
  8 ms each. `docs/PERFORMANCE.md` and a stress note both said it was cached.
- **Any input re-rendered every visible 3D view from scratch**: a mouse move over a panel, a caret blink,
  a tooltip timer. In a working scene (1600×1000, Scene and Game views, a Camera Preview), an idle frame
  cost about 46 ms.

**Changed**
- **Views reuse their picture while nothing they show has changed (ADR 0009).**
  - **What is cached:** the Scene view, the Game view and the Shaded Camera Preview.
  - **The key:** a content hash of the scene (geometry, transforms, the hierarchy, MeshRenderer flags,
    materials, lights, world, render settings), plus each view's camera, size, toggles and selection.
  - **Never cached:** Rendered shading, piloting, Play while running.
  - **Verify mode** (`BLENDITY_RENDER_CACHE_VERIFY=1`) renders anyway and compares. The unit suite and
    the random-input fuzz both run clean in it.
- **The shadow map is kept until its casters, their materials or the light change.** There are two
  entries, so the Scene and Game views don't evict each other. Each render mesh gets a `serial` the
  caches key on.
- **Clears run in one parallel pass**, and the visibility plane exists only for deferred shading.
- **Overlay lines and points are drawn in parallel bands of rows**, bit-identical to drawing them one by
  one. The F12 supersampling downsample is parallel too.
- **The scene hash no longer evaluates modifier stacks:** `GameObject::evaluated_key` gives the key
  without the mesh.
- **The Profiler window shows where the frame went:**
  - time in the Scene view and its overlays, the Game view, the preview, the shadow map, the UI and
    present;
  - how many views were rendered or reused.

**Fixed (from the review)**
- **The Game view and Camera Preview missed MeshRenderer flags.** Unticking Show in Renders or Receive
  Shadows left them stale. The flags are now in the shared scene hash, which also fixes the
  path-traced preview's key.
- **A modifier's "Show in Edit Mode" didn't redraw the Edit Mode cage.** The cage's evaluated mesh is
  now in the key.
- **Re-parenting a selected object's child** (for example by undo) left its child outline. The
  hierarchy is now in the key.
- **Smaller gaps:**
  - Reopening a scene file now starts fresh caches (each Scene has a `serial`).
  - The raster options are in the Game view and Camera Preview keys.
  - The grid's orbit distance and pivot are in the Scene view key.
  - A batch can't stay open across frames.
- **One test** checks the first three cases in verify mode. Without the fix it finds 3 stale pictures.

**Checked**
- **Linux, dependency-free build, `editor_render`, before → after:**

| Frame | Before (ms) | After (ms) |
|---|---|---|
| Idle | 47.3 | 2.1 |
| Mouse over the Hierarchy | 44.2 | 2.1 |
| Orbiting the Scene view | 43.6 | 9.9 (no shadow pass) |
| Moving an object | 45.1 | 24.7 |
| Editing a material colour | 45.4 | 27.0 |
| Playing | 32.5 | 14.8 |

- **Idle frames with many objects:** 1.9 ms at 1,000 meshes and 5.1 ms at 10,000 (the hash). An idle
  frame with 1,000 empties went from 10.6 to 1.5 ms.
- **Overlays:** 1.15 → 0.77 ms in a normal frame, and 17.5 → 12.4 ms with a 50k-face Edit Mode cage.
- **Edit Mode on a 50k-face mesh is still about 280 ms a change.**
  - The render mesh costs 13–19 ms and the tangents 28–37 ms.
  - Most of the rest is the overlapping-vertex check. It runs on changes made outside a drag; a real
    gizmo drag skips it.
  - Speeding this up is the follow-up (a positions-only mesh update; it needs an ADR).
- **Unit checks:** Windows 3,203, Linux 3,179, sanitizer build 3,101; 0 failed. 270 tests. The
  test-engineer wrote 26 tests and the review added 1:
  - idle and panel-only frames render nothing;
  - fourteen kinds of change each redraw, including a real gizmo drag, an Edit Mode vertex drag, undo
    (bit-identical to before), Play, shading, the World and Exposure;
  - orbiting renders the shadow map at most once;
  - about 55 mixed actions in verify mode leave no stale picture;
  - the overlay batch is bit-identical on 450 random lines and points;
  - the parallel clears.
- **Stress:**
  - `editor_fuzz` (6,000 frames of random input) in verify mode: no crash, 0 stale pictures.
  - `filters`: 0 problems.
- **The whole unit suite** runs in 18 s on Windows (21.7 s before this task); the tests reuse views too.

**Not done from the spec**
- **`is_selected` as a set** and **one render snapshot per frame:** the scene walks and the linear
  selection search don't show in the measurements at 1,000–10,000 objects.
- **`outline_ids` in one pass:** it reuses its mask instead; still two passes when a selection has
  children.
- **A test for the caret blink:** it is the same frame as the idle case (no events, a redraw).

## 2026-10-09 (round 33): leaner, faster unit tests (task 0010)

**Asked:** consolidate the tests, removing redundant or unnecessary ones. The same request also asked for
editor performance, Unity-style baked lighting, probe volumes, and the voxel-based global illumination paper
in `research/papers/`. Those are specced as tasks 0011–0014, approved, and come next. A GPU viewport
follows as task 0015, specced after a measured spike.

**Changed**
- **Per-element checks are counted.** A loop that ran `CHECK` on every pixel or table entry now counts the
  failures and checks once (`count_bad`, which prints the first bad element). Coverage is the same. The
  bloom odd-values test alone had run 53,800 checks.
- **Overlapping tests are merged**:

| Old test | Now covered by |
|---|---|
| filter stack: an empty stack, or one with every effect off, gives the same Game view as no component | The stack part moved into "with no filter the rasterizer's image is unchanged". The Game view part (empty stack, two disabled effects) moved into "PS1 Game view is 15-bit ... with no filter, an empty stack, disabled or neutral effects it is the old picture". |
| camera filters: the component saves, loads (its own values beat the preset), round-trips twice, and clones | "filter stack: a stack saves, loads (every field, its own values beating the preset) and round-trips; clones and Duplicate deep-copy it", which gained a third effect with every field moved off the preset. |
| bloom: a target that is a window into a larger image (row stride) only changes inside the window | "odd sizes: every pass works ... (row stride, Bloom too)" runs Bloom on the same window, with a white patch to spread. |
| Bloom's save / load / set / undo in "bloom: Add Filter > Bloom on the Main Camera ..." | Threshold is Bloom's row in the effect table "every filter effect (Retro Console, Color, Lens, Stylize, Bloom) can be added from the console, set, saved, loaded and undone". The table also gained a Retro Console row, and checks that every registered effect has a row. The bloom test keeps the Game view glow, Intensity and Scatter's save / load / undo (restored after the review), and undoing the add. |
| The PS1 preset fields in "presets: PS1, N64, Saturn and DOS ..." | "Retro Console Filter contributes the PS1 look" (now also checks that there is no screen-door). |
| The two-filter ordering in "Retro Console Filter contributes the PS1 look" | "effects combine in list order". The later snap grid (in both orders) and the "cap of 0 doesn't lift a cap" checks stay. |
| The Camera Preview rebuild in "the component's hash changes with any effect field ..." | "the Camera Preview (Rendered) refreshes when a filter field changes", which now sets the size through the stack's field names, for the first effect and for a second one (E1). The hash test no longer opens an editor. |

- **Checks that tested nothing:**
  - `CHECK(true)` after drawing a window became a check that the window is open.
  - An empty bloom or retro target is checked as untouched.
  - Dead variables are removed, including an unchecked `plain_changes_within_key`, an unused render, and
    an unused `esc` event.
- **Shared helpers:**
  - `kNaN` / `kInf` sit at the top of the file.
  - `cf_draw` is shared by the PS1 helpers and the bloom scene.
  - Each test prints its time.
- **Faster tests.** Sample counts were lowered only with a tolerance worked out from the noise, written in
  the test.
  - **Path tracer:** the MIS reference uses 1024 spp instead of 8192. The ratio of the two means has σ ≈ 0.5%,
    so the 1% check is about 2σ of the estimator. The tracer is seeded per pixel and sample, so it gives
    the same answer on every run; the measured ratio is 0.9965. The path-guiding reference uses 1024 spp
    instead of 4096 (measured agreement 0.4%, bound 4%).
  - **Bloom path-traced F12:** waits for the render to report done, instead of a 6,000-frame sleep loop.
  - **Pilot click:** clicks only where there is geometry. The pilot-frame test reuses the window size it
    found.
  - **Film grain:** 45 ms waits instead of 60 ms. The seed changes every 1/24 s.
  - **Filter-menu tests:** use a wireframe Scene view (only the Inspector is under test), and the
    checkbox sweep steps 4 px.
  - **Bloom odd-values test:** tries every odd value at two sizes, and the other sizes with NaN and
    infinity.
  - **The rotate-shape sweep:** uses a smaller window.

**Checked**
- **Checks:** Windows 3,021 (was 60,624), Linux 2,997 (was 60,600), sanitizer build 2,919. 0 failed.
  243 tests (3 merged away).
- **Time:**
  - Linux wall time (Release, median of 3): **37.5 s → 22.0 s**.
  - Windows, sum of test times: **32.0 s → 21.7 s**.
- **Slowest ten, before → after (Windows, ms):**

| Test | Before | After |
|---|---|---|
| pilot click | 3974 | 194 |
| path guiding | 2264 | 865 |
| rotate a shape on a rectangle | 2249 | 2024 |
| bloom path-traced F12 | 1798 | 223 |
| effect menu clicks | 1283 | 681 |
| pilot frame = Game view | 1218 | 867 |
| film grain in the editor | 1068 | 703 |
| MIS | 1031 | 136 |
| bloom odd values | 1017 | 418 |
| enabled checkbox | 740 | 289 |

- **The spec's 3× target was met by 5 of the 10.** The rest are bound by something the test is about:
  - **rotate-shape:** 20 editors at about 100 ms each to set up;
  - **film grain:** real time has to pass for the grain to move;
  - **pilot frame:** two 1600×1000 editors;
  - **effect menu:** about 25 clicked frames;
  - **path guiding:** the guided render itself.
- **Review fixes:**
  - Bloom's Intensity and Scatter lost their save / load / undo coverage in the merge; it is back in the
    bloom test.
  - The "later snap grid wins" check is back in both orders.
  - The Camera Preview is also checked through a second effect's fields.
  - The statistics comments now give the real margins.
  - Two more per-texel loops are counted.
  - Local NaN constants use `kNaN` / `kInf`.
- **Not done from the spec** (left as they were):
  - **The neutral/identity tests stay separate:** the rasterizer with no filter, the Color / Lens /
    Stylize passes at neutral settings, and Bloom at intensity 0. They check different layers, so one
    table would hide which one broke.
  - **The shared event helpers were not built** (`ev`, `click`, `cmd`, `grab`, `settle`). The copies differ
    in small ways (frames per click, the rows grabbed), and unifying them is a refactor of about 40 tests
    with no change in coverage.
  - **The F12 sequence test (8592), the denoiser test (2088) and the 9833 editor count are not sped up.**
    Each is under 300 ms now, and none is in the slowest ten.
  - **The check count is 3,021, not about 2,500.** The rest are single checks.
- **The slimmer tests still catch bugs.** Three bugs were planted by hand and not committed; each was
  caught:
  - bloom dropping alpha (caught by the counted check);
  - Bloom's Threshold not saving (the new table row);
  - mesh-light NEE biased by +5% (the 1024-spp MIS check: ratio 1.044).

## 2026-10-09 (round 32): Bloom (task 0007)

**Asked:** start task 0007: the Bloom & glow category of the Camera Filters stack.

**Added**
- **Bloom** in Add Filter (Bloom & glow): light brighter than a threshold spills into the pixels around it.
  Settings: intensity, threshold, soft knee, scatter (the glow's spread), tint, and a clamp against
  fireflies. It works from the rasterizer's linear light and from the path tracer's (F12 renders and the
  finished Rendered Camera Preview).
- **An HDR plane in the rasterizer:** linear light before exposure and tone mapping, kept only when a
  camera's stack has a Bloom. The sky and HDRI write their radiance into it too. Without a Bloom nothing is
  allocated and pictures are bit-identical.

**Fixed (from the review)**
- **Bloom undid the effects before it.** Placed after Posterize (or a retro dither, or depth of field), it
  re-encoded the shader's original light and lost their result. It now re-encodes only pixels still as
  shaded, and adds the glow on top of anything an earlier pass changed. A test checks that Posterize's
  levels survive.
- **The glow's size depended on the resolution:** a small Camera Preview glowed far more widely than a 4K
  render. The pyramid now starts from a fixed 540-row base, so the glow covers the same part of the frame
  at any size.
- **A seam at silhouettes against an HDRI or physical sky:** the sky was decoded from 8 bits while the
  object beside it had full linear light. The sky now writes its radiance into the HDR plane.
- **Memory:** the reused buffers are given back when much bigger than needed (after a 4K render).

**Checked**
- 10 tests (9 by the test-engineer, 1 from the review): an emissive quad above the threshold glows, falling
  off with distance, and nothing changes below it; more intensity adds more light and more scatter
  spreads it; no Bloom means no HDR plane and identical pictures; the stack order holds; Posterize's levels
  survive a later Bloom; odd values and 1x1 / 2x2 frames are safe; save, load, undo and Add Filter; a
  path-traced F12 glows. Several check every pixel, which is why the count jumps.
- Unit checks: Windows 60624, Linux 60600, sanitizer build 60522; 0 failed.
- Stress `filters` (1080p, Windows / Linux): Bloom 14.3 / 15.9 ms on decoded colour, 14.0 / 12.9 ms on the
  HDR plane; the HDR plane itself costs 2.5 / 1.6 ms a frame when asked for. Random stacks of every effect type: 300 runs, 0 problems.

## 2026-10-09 (round 31): Color, Lens and Stylize camera filters (task 0006)

**Asked:** start task 0006: the Color, Lens and Stylize categories of the Camera Filters stack. Animated
effects move in Play mode only.

**Added:** twelve effects in Add Filter, each with its own settings, stackable in any order.

| Category | Effects |
|---|---|
| Color | **Color Grading** (in linear light: exposure, contrast through middle grey, saturation, temperature and tint, lift / gamma / gain), **Posterize**, **Grayscale / Sepia**, **Invert** |
| Lens | **Vignette** (intensity, smoothness, roundness, colour), **Chromatic Aberration**, **Film Grain** (strongest in the midtones), **Lens Distortion** (below 0 barrel, above 0 pincushion, with zoom) |
| Stylize | **Pixelate**, **Edge Outline** (lines where objects meet and where the depth creases or steps), **CRT** (scanlines, a curved tube, phosphor stripes, flicker), **Sharpen** |

Film grain and CRT flicker move only in the Game view while playing. F12 renders, camera sequences,
previews and piloting stay reproducible.

**Fixed (from the review)**
- **F12 during Play animated the grain.** Only the Game view animates now; there is a test.
- **CRT flicker vanished at 60 fps.** A 30 Hz sine sampled every 1/60 s sits on its zero crossings. It is a
  random brightness every 1/30 s now; a test checks 60 fps frames.
- **Edge Outline drew a band on floors seen at a grazing angle.** It tests flatness now: the second
  difference of distance and of 1/distance (linear across the screen on a plane, orthographic or
  perspective) instead of a jump in distance. A test checks a grazing floor with an 8 px, very sensitive
  outline.
- **Lift / gamma / gain clamped above 1**, so exposure plus a lower gain flattened the highlights.
- **The grain seed's time** is wrapped to a day, so a huge time can't overflow the cast.
- **Lens Distortion's help** had barrel and pincushion swapped (the test-engineer noticed).

**Checked**
- 19 tests by the test-engineer, plus 2 from the review. For each effect: neutral settings are
  bit-identical, and its behaviour matches hand-computed values (posterize level counts, the sepia matrix,
  exact outline columns, CRT rows and mask, sharpen at a step, +1 stop = x2 linear light). Grain and flicker
  animate only when asked. All 12 effects save, load and undo. NaN, infinite and huge values are safe at
  1x1 and 64x48 and with a row stride.
- Unit checks: Windows 5710, Linux 5686, sanitizer build 5608; 0 failed.
- Stress `filters` (Windows, 1080p, defaults): every effect 0.4-10 ms (Lens Distortion and CRT the slowest).
  300 random stacks of every effect type with every field fuzzed: 0 problems (crash, size or copy-hash).

## 2026-10-09 (round 30): piloting a camera shows its game view (task 0009)

**Asked:** whenever a camera is piloted, its camera filters should show: treat the pilot camera as the game
camera (chosen: the pure game look).

**Changed**
- **Piloting** (Pilot Camera, or `pilot`) shows the camera's Game view picture inside its frame: its filters,
  lens, exposure and depth of field, and the scene's lights, whatever the Shading mode. Around it is a plain
  dark passepartout.
  - The grid, gizmos, icons, overlays and statistics are hidden while piloting.
  - The banner stays, and a thin outline sits just outside the frame.
  - Clicking an object in the frame still selects it.
  - Entering Edit Mode ends piloting, because its cage and selection need the editor's overlays.

**Fixed:** the round-29 Saturn screen-door test failed on macOS (arm64). It checked that every pixel of a
normally blended quad differs from both the veil and the background, and a few didn't. The screen-door checks
themselves are exact and passed; the contrast check now asks for 90% and prints the count.

**Checked**
- Seven new tests (five by the test-engineer, two from the review):
  - The frame equals a second editor's Game view of the same camera at the same size: 0 of 518,870 pixels
    differ, plain and with the PS1 filter.
  - PS1 shows in every Shading mode.
  - With the grid, gizmos and statistics on or off, no pixel changes.
  - The passepartout is uniform.
  - A click selects.
  - Stopping restores the grid.
- Unit checks: Windows 5062, Linux 5038, sanitizer build 4960; 0 failed.
- Review (code-reviewer agent, Opus): changes needed. Fixed:
  - **Depth copied for the editor's tools** used the camera's near / far, so the tools' visibility tests
    disagreed. It is re-projected into the editor's projection now. A test compares it with the editor's own
    render: worst difference 0.0008.
  - **Edit Mode ends piloting.**
  - **No busy redraw** in Rendered mode while piloting.
  - **Documented limit:** with an orthographic camera, lens shift or a letterboxing filter, the editor's
    projection doesn't line up with the picture. Clicks still select what they hit, and the gizmos are hidden.

## 2026-10-09 (round 29): the Camera Filters stack, and the N64, Saturn and DOS looks (task 0005)

**Asked:** make camera filters a general system like Unity's post-processing, where Retro Console is one
category among others (Color, Lens, Stylize, Bloom & glow come in tasks 0006 and 0007), then start task 0005.

**Changed**
- **Camera Filters** replaces the separate Retro Console Filter component.
  - It is one component per camera holding a list of effects (ADR 0008).
  - **Add Filter** lists the effects by category.
  - Each effect has its own toggle, foldout and menu (Move Up, Move Down, Reset, Remove Filter).
  - Order matters: later resolution and texture settings win, the smaller texture cap wins, and colour
    passes run top to bottom.
- **Old scenes and habits still work.**
  - A scene saved with the Retro Console Filter component loads into a stack.
  - `component Retro Console Filter` adds one to the stack.
  - `set RetroConsole.Width 160` reaches the first such effect, and `set CameraFilters.E1Width 160` the
    second.
- **`redo` with no arguments** now redoes. `redo <param> <value>` still adjusts the last operation. The
  plain form didn't work in round 28: an older command of the same name caught it first.

**Added**
- **Retro Console presets:**

  | Console | Resolution | Look |
  |---|---|---|
  | Nintendo 64 | 320x240 | the RDP's 3-point texture filter, 64 px textures with mipmaps, 15-bit colour with a Bayer dither, fog |
  | Sega Saturn | 320x224 | whole-pixel vertices, affine textures, 15-bit colour, see-through surfaces as a checkerboard "mesh" |
  | DOS PC | 320x200 | 256 colours (a 6x6x6 cube plus 40 greys) with an ordered dither, affine textures |

- **The `filter` console command:** `add <name>` (any case), `remove <i>`, `move <i> <j>`, `reset <i>`
  and `list` (which lists the available effects for an empty stack).

**Fixed (found by this round's tests and review)**
- **The DOS palette's nearest-colour table turned 14 palette colours into neighbours**, black among them,
  so a DOS frame could never be pure black. The nearest colour is now computed exactly from the palette's
  structure, and it is faster too.
- **Editing several selected cameras skipped their effects.** Effect edits now carry the same names the
  files use ("E0 Width"), so they reach each selected camera.
- **`set CameraFilters.Effects ...` could replace the whole stack.** The type list can only be changed with
  `filter`.
- **Screen-door pixels** skip shading for the holes and write depth for the drawn pixels (fog and later
  surfaces see them).

**Checked**
- Unit checks: the test-engineer wrote 12 tests (round 29) from the spec, plus a review test that types into
  the Inspector with two cameras selected. Windows 5029, Linux 5005 and the sanitizer build 4927; 0 failed.
- Stress `filters` (Windows, 1080p): every preset is 6.5-8.2x faster than unfiltered; 200 random stacks with
  odd values, 0 problems; every 256-colour frame uses only palette colours.
- Review (code-reviewer agent, Opus): changes needed. All are fixed above: multi-camera editing, `set` on the
  type list, screen-door shading and depth, the stress comment, docs and the console nits.
- Headless screenshots of each preset on the textured scene.

## 2026-10-09 (round 28): component menus work again, steady macOS CI (task 0008)

**Asked:** components couldn't be removed from an object; the macOS build kept failing in CI.

**Fixed**
- **Remove Component, Move Up and the modifier menu did nothing.** A component's or modifier's menu runs
  at the end of the frame, after the Inspector has drawn. Its items wrote to the Inspector's own
  variables, which by then no longer existed, so the click was lost (and wrote to freed stack memory).
  The choice is now kept by the editor and applied on the next frame. This covers Remove Component, Move
  Up and Reset, and the modifier menu's Apply, Duplicate, Copy to Selected, Move and Reset.
- **macOS CI failed now and then.** The Scene view filter test compared frames pixel by pixel, and the
  overlay text changes more pixels on slower machines. It now checks whether the view is filtered (its
  share of 15-bit pixels).

**Added:** console commands `undo` and `redo`.

**Checked**
- Two new real-event tests: they click a component's menu (Move Up, Reset, Remove Component, then undo)
  and a modifier's menu (Duplicate, Apply).
- Unit checks: Windows 4298, Linux 4274, sanitizer build 4196; 0 failed.
- Review (Opus): approved. Its suggestions are in:
  - the Keymap window's Category dropdown read freed strings while open (same kind of bug);
  - a menu choice not applied by the next frame is dropped;
  - Transform > Reset finds the object by id;
  - the test now really picks Remove and then selects another object, and the Cube keeps its
    component.

## 2026-10-09 (round 27): camera filters, with the PS1 look (task 0004)

**Asked:** camera "filters" built from shaders, starting with one that recreates 3D graphics on the
PlayStation 1 and older consoles. Decisions taken with the user:
- filters are components on the camera;
- they show in the Game view and Play mode, the Camera Preview, F12 and sequences, and the Scene view
  through a toggle;
- presets for PS1, N64, Saturn and DOS (N64, Saturn and DOS are task 0005).

**Added**
- **Retro Console Filter** (Add Component > Rendering), with a Console preset (PS1). Its fields:
  - Width / Height and Fit (Fill View with square pixels, or Letterbox to the console's frame);
  - Vertex Snap with a grid;
  - Affine Textures;
  - Texture Filter, Max Texture Size and Mipmaps;
  - Colour Depth (24 / 15-bit) and Dither (PS1 4x4 / Bayer);
  - Fog with start, end and colour.
- **The PS1 preset:** 320x240, vertices on whole pixels, affine textures, nearest 256 px textures with no
  mipmaps, 15-bit colour through the PS1's dither matrix.
- **Where filters render:** everywhere the camera's image shows, through one `Editor::render_camera`.
  - A path-traced render through a filtered camera traces at the low resolution and gets the colour pass.
  - The Scene view's new camera button (or `filters on|off`) previews the main camera's filters there.
- **Speed:** a filtered camera renders fewer pixels: the PS1 look at 1080p took 2.4 ms against 18.3 ms
  unfiltered.
- ADR 0007 (camera filters), requirement R-10.

**Fixed**
- **Components with spaces in their names** couldn't be added from the console (`component Retro Console
  Filter`), and were dropped when a scene was loaded. Both take the whole name now.
- **A texture could be freed while it was being drawn.**
  - When several shading threads met a material whose textures weren't loaded yet, each loaded them and
    replaced the others' (AddressSanitizer: heap-use-after-free).
  - Both renderers now load materials' textures before shading in parallel.
- **NaN checks under `/fp:fast`.** The Windows build is compiled with `/fp:fast`, which lets the compiler
  assume no NaNs. `display::linear_to_srgb8(NaN)` returned 255 or 0 depending on how the binary was inlined
  (an existing test failed once new code changed the inlining). NaN tests now read the bits
  (`nan_bits` / `finite_bits`).

**Checked**
- Unit checks: Windows with libraries 4280, Linux 4256, sanitizer build 4178; 0 failed.
  - The test-engineer agent wrote 14 tests from the spec. One of them found the save/load bug.
  - Two Linux failures were in the tests, not the code, and the tests were fixed:
    - a straight-edged rectangle can't show sub-pixel creep, so the creep check uses a slanted quad;
    - the Scene view's log line needs a 1% tolerance.
- Stress `filters`:
  - the PS1 look at 1080p: 2.4-2.9 ms against 18.3-24.7 ms unfiltered;
  - no 15-bit pixel off the 5-bit grid;
  - 300 runs of odd settings (NaN / infinite snap and fog, 1x1 to 100000 px), 0 problems.
- Same-session A/B of `shading` against `main` (Linux): equal within noise, so the filter code costs
  nothing when unused.
- Headless screenshots of a textured scene: no filter, PS1 Fill, PS1 Letterbox, the Scene view toggle.
- Review (code-reviewer agent, Opus): changes needed. Fixed:
  - the texture warm-up now runs in every shade mode (the see-through pass shades materials in Solid
    view too), and its lock is re-entrant, so a texture load's own parallel_for can't deadlock;
  - the path tracer resolves textures at the start of every render call, not only at build;
  - stats survive a filtered render (overlays rebind the renderer without resetting them);
  - the Scene view always uses Fill, so grid, gizmos and vertex picking match the picture.

## 2026-10-09 (round 26): sanitizer findings fixed, branch protection on (task 0002)

**Asked:** after merging tasks 0001 and 0003, deal with the current issues before the camera
filters: sync, branch protection, and the sanitizer findings.

**Changed**
- `main` is protected:
  - pull requests only;
  - the `windows`, `linux` and `macos` checks must pass, on a branch up to date with `main`;
  - no force pushes or deletions;
  - 0 approvals, since GitHub doesn't count approving your own PR.
- `linux-sanitizers` is blocking now and joins the required checks once this merges. It also checks
  float-to-int casts that overflow (`float-cast-overflow`, which GCC leaves out of `undefined`).
- `CLAUDE.md` says how to call `gh` from the Bash tool and how to read CI.

**Fixed** (both undefined behaviour; neither showed as a wrong result yet, but a compiler is free to
miscompile them):
- **Merge by Distance and proportional editing far from the origin.** Their spatial hash multiplied
  grid cells in `int`. A point at x = -2 with a 0.1 mm distance already overflowed, and far points
  over tiny distances overflowed the cell index itself. One shared helper now uses 64-bit cells and
  unsigned hashing.
- **JPEG decoding** shifted a negative number (`-1 << s`) for every negative coefficient.

**Checked**
- Sanitizer build (ASan + UBSan + float-cast-overflow, dependency-free): 1832 checks, 0 failed, no reports.
- Windows with libraries: 1934 checks, 0 failed. Linux with libraries: 1910, 0 failed.
- The merge stress section: hash and O(n²) reference agree, speed unchanged.

## 2026-10-09 (round 25): CI passes again (task 0003)

**Asked:** continue with the next task, making CI pass on all three platforms so branch protection can
be switched on.

**Fixed**
- **`build.sh` stopped before compiling without Blender's libraries.** The object folder's name was
  built with `[ ... ] && echo _libs`. That command fails when the libraries are absent, and `set -e` ended the
  script without a message. This broke every Linux and macOS CI run.
- **`build.sh` was not executable in git** (mode 644, committed from Windows), so CI's
  `./build.sh` failed with "permission denied" (exit 126) before even reaching that line. It is
  now 755.
- **macOS saved numbers with nine digits.** Apple's C++ library doesn't announce floating-point
  `to_chars`, so scene and material files used `%.9g` there: 0.7 was written as `0.699999988`, and a
  material test looking for "0.7" failed. The fallback now writes the fewest digits that read back as the
  same number, as `to_chars` does, so files are identical on all three platforms.

**Changed:** CI's unit-test steps turn each failing check (and each sanitizer report) into a GitHub
annotation, so failures can be read on the run page without signing in to view the logs.
The quick stress step has hung on CI until GitHub's 6-hour limit on every Windows run so far (and now
on Linux), though it takes under 90 s locally even on one core. It now stops after 15 minutes, and
`blendity_stress` reports each section's start and end on stderr, so the run page shows where it was.

**Fixed: the stress test never ended on 3 or 5 threads.** The job-system section steps through thread
counts and, to end on the machine's count, stepped back to half of it. With 3 or 5 threads (GitHub's
macOS and Linux runners) that step-back repeated forever. Now the counts are listed first (1, 2, 3, 4, 8,
16... then the machine's own). This was the hang that kept every Windows CI run going for 6 hours.
- **Push Through out through several faces in one plane needed Manifold.** Pushing a rectangle
  through a wall whose back already had a circle, or an archway where the back had a rectangle across
  it, was refused in the dependency-free build. Now the outline is cut across the back faces and
  the front outline gets a corner opposite each new back corner, then a tube joins the two. Both builds
  take this path, so they agree. Manifold is used only when the exit faces aren't coplanar.

**Checked**
- Unit checks:
  - Windows, dependency-free: 1822, 0 failed.
  - Linux, dependency-free: 1822, 0 failed.
  - Windows with libraries: 1924, 0 failed.
  - Linux with libraries: 1900, 0 failed.
- Linux, dependency-free: the quick stress pass and the headless screenshot run as CI runs them.
- Push/Pull stress: 0 problems; 394 results with faces newly on top of each other (unchanged).
- Review (code-reviewer agent, Opus): approved. It suggested the cut be checked to form the opening's outline edge for edge before the tube is built, so a face missed inside it can't leave edges with three faces; that check is in.

## 2026-10-09 (round 24): engineering setup for agentic development (task 0001)

**Asked:** set the repository up for research-informed agentic development:
- standards, three agents (implementer, test engineer, code reviewer), requirements, decision records and task specs;
- CI for the real stack;
- a plan / implement / test / review / human-merge workflow.

**Added**
- `CLAUDE.md`: the project map, build and test commands, testing rules, code standards, documentation duties, approval gates and the publishing procedure.
- `.claude/agents/`:
  - `implementer` (Sonnet): one approved task; no architecture, dependencies, commits or pushes.
  - `test-engineer` (Sonnet): tests from the acceptance criteria; edits only `tests/` and `stress/`; reports bugs as failing tests.
  - `code-reviewer` (Opus): read-only; ranked findings and a verdict.
- `.claude/settings.json`: denies force pushes, pushes to `main` / `blendity`, merging PRs and hard resets; asks before commits, pushes and `gh pr create` / `gh api`.
- `docs/requirements.md`: product, quality and constraint requirements with IDs.
- `docs/decisions/`: the index, a template and six ADRs recording decisions already in force:
  1. optional Blender libraries;
  2. licensing;
  3. copy-on-write meshes;
  4. SketchUp Push/Pull semantics;
  5. tool settings in the F9 and drag helpers;
  6. subtree publishing with human-merged PRs.
- `docs/tasks/`: the lifecycle, a template, task 0001 (this) and draft task 0002 (sanitizer findings).
- `docs/ARCHITECTURE.md`: a system overview (layers, one frame, one edit), key invariants and module ownership at the top. The phase tables are kept below as the history.
- CI: a `linux-sanitizers` job (unit tests under ASan + UBSan, non-blocking until task 0002), and cancelling superseded runs.
- `build.sh` honours `EXTRA_FLAGS`.
- `.github/pull_request_template.md`.

**Changed:** work reaches `main` only through a pull request that CI must pass and the human merges. There are no more direct pushes (ADR 0006).

**Found while checking:**
- **CI has failed on every recent push to `main`.**
  - `build.sh` exits silently before compiling when Blender's libraries are absent (always the case in CI).
  - Two Push Through tests rely on the optional Manifold library.
  - Recorded as task 0003, to finish before branch protection is switched on.
- **The sanitizer job's first finding:** a signed overflow in `merge_by_distance`'s spatial hash (task 0002).
- **Local checks:** Windows 1924 and Linux 1900 unit checks pass with Blender's libraries. With `build.sh` corrected, the sanitizer build compiles and runs.

## 2026-10-09 (round 23): arched shapes in one piece, array copies past a surface's edge

**Asked:** arraying a vertical rectangle with an arched top cut some copies improperly.

**Fixed**
- **The arch itself was two faces.** An arc drawn on a rectangle's top crosses an edge of the faces around the rectangle. The pieces inside the arc were merged, except where the crossed edge ran to the arc's own end (the rectangle's corner), so the arch stayed split. The array then copied both pieces. Edges from a crossing to the arc's ends are now dissolved too, so the arch is one face, and so is each copy's.
- **Copies past the surface's edge.** A copy hanging past the edge of the wall used to fall back to a loose duplicate lying over the wall (overlapping faces, an open mesh). Now:
  - The part of it on the surface is cut in: clipped to the surface's outline when that outline is convex, as a wall's is.
  - A copy with nothing under it at all is still a separate copy.
  - A cut that can't be made leaves that copy out rather than half of it.

**Checked:** 1924 unit checks on Windows and 1900 on Linux, 0 failed. New test: an arched doorway on a wall arrayed 2 and 3 times, with 8 and 16 arc segments. The doorway area must be exact, or exact plus the clipped part for the copy past the wall's end. No overlaps, still closed, no bad triangulations, no wire edges.

Stress:
- Push/Pull: 0 problems.
- The modeling section: 0 problems.
- Curved surfaces: 8 problems.
- The fuzzer: no crash.

## 2026-10-09 (round 22): Merge Coplanar on a selection, Array copies cut into the surface

**Asked:** Merge Coplanar on a selected group of faces; Array copies of a face should cut into the mesh like the original instead of lying on top of it.

**Added**
- **Merge Coplanar on the selection.**
  - With faces selected (in any mode, a face counts when all its corners are selected), only the edges between two selected coplanar faces dissolve, and only corners that no unselected face uses go. The rest of the mesh keeps its edges, and the merged faces stay selected.
  - A 3 x 3 block of the Plane becomes one 12-cornered face (it keeps the corners its neighbours share, so there are no T-junctions); two separate blocks become two faces.
  - With nothing selected it works on the whole mesh, as before.

**Changed**
- **Array copies are drawn onto the surface.**
  - Each copy of a shape that sits on a face is cut into the faces it lands on, as drawing the shape there would, instead of a duplicate lying on top (z-fighting). A copy crossing the faces round an earlier drawing becomes one face.
  - Only a copy that lies wholly on the surface is cut in; one hanging off it, or a face with nothing flat under its copy (a box's whole top), is still a separate copy.
  - The originals and all copies end up selected, ready for Push/Pull or Recess together.

**Checked:** 1916 unit checks on Windows and 1892 on Linux, 0 failed. New tests:
- Merge Coplanar on a 3 x 3 block, two blocks, and nothing selected.
- A drawn rectangle arrayed three times across a cube's top: cut in, no overlaps, still closed, no bad triangulations. The three pushed in together stay closed; a copy with nothing under it stays a separate copy.

Stress:
- Push/Pull: 0 problems.
- The modeling section: 0 problems.
- Curved surfaces: 8 problems.
- The fuzzer: no crash.

## 2026-10-09 (round 21): tool helpers, settings out of the Inspector, Push/Pull on non-manifold meshes, overlap marking

**Asked:**
- Drop "Fluent" from Hard Surface, and add a helper for the tools such as Array.
- Make Push/Pull work on non-manifold meshes.
- Edges were marked as overlapping when they weren't: only the smaller edge should be marked.
- Thickness, draft angle, extrude distance and the like belong in each tool's helper, not in the general area under Draw.

**Changed**
- **Hard Surface** (no "Fluent" in the interface). Grid, Pipe, Array, Taper Extrude and Recess / Plate:
  - Clicked in the menu or the Inspector, each opens a **drag helper** like Ctrl+B's. The mouse sets the spacing, radius, distance or depth, or you type it. The wheel steps the count, columns (Shift: rows), sides, end scale or border. X / Y / Z set Array's axis. Click or Enter confirms, Esc cancels.
  - Afterwards each one is in **Adjust Last Operation (F9)**.
- **Tool settings live with the tools.** The Inspector's Edit Mode area keeps only the switches that change how tools behave: Push/Pull Each Face, Inset Individual, Auto Fuse, Auto Smooth and Sharp Angle.
  - Each tool's numbers (thickness, angle, distance, segments, profile, count...) are in its F9 panel or drag helper.
  - Shell, Thicken and Draft are now adjustable (F9) operations too.
  - Whatever you set there is kept for that tool's next use.
- **Overlapping edges:** of two edges along each other, only the shorter one is marked and selected. That's the piece lying on a longer edge, or one of two doubled edges; the long edge it lies on is left alone.

**Fixed**
- **Push/Pull on non-manifold meshes.** Pushing a face in next to a fin, or next to a second solid sharing the edge, was refused as "the faces around the selection lean over it". The fin or the other solid counted as a wall of this one. Only the faces that carry the surface on across the edge count now. Tested on:
  - a fin (pushing and pulling the top, the front and the fin itself);
  - two cubes sharing an edge, and two sharing a corner;
  - an open box;
  - a cube with a stray wire edge and a loose point;
  - two faces at once.

**Checked:** 1906 unit checks on Windows and 1882 on Linux, 0 failed. New tests:
- The Array helper from the menu: wheel, Z, a typed spacing, Enter. Grid, Taper and Recess open their helpers and Esc leaves the mesh as it was.
- The non-manifold Push/Pull cases.
- No overlap alarms on the primitives, the teapot, a bevelled, a subdivided and a drawn-on cube; only the shorter edge selected.
- Shell, Thicken and Draft as F9 operations.

Stress:
- Push/Pull: 0 problems, and 394 results with faces newly on top of each other. That's 4 more than before, from pushes beside fins that used to be refused.
- The modeling section: 0 problems.
- Curved surfaces: 8 problems, the same as last round.
- The fuzzer: no crash.

## 2026-10-09 (round 20): bevel profile, arcs on drawn shapes, smooth region inset

**Asked:** a profile for the bevel, to make it concave or convex; an arc drawn on a rectangle drawn on a surface didn't make a face inside the arc; insetting several faces "stepped" instead of changing smoothly.

**Added**
- **Bevel Profile** (Blender's superellipse), 0 to 1:
  - 0.5 is a round quarter circle, 0.25 a flat chamfer.
  - Toward 1 it bulges out to a square corner (convex); toward 0 it scoops in (concave).
  - In the Inspector (Bevel Profile), the F9 panel, the Ctrl+B helper (Alt+wheel, shown in its readout), the Bevel modifier (saved with the scene), and the console (`bevelprofile 0.8`).
  - Profile 0.5 is now an exact quarter circle; before, it was a quadratic curve slightly flatter than a circle.

**Fixed**
- **Arcs (and other open lines) across the faces round a drawn shape.**
  - A shape drawn on a face sits in a ring of several faces. A line crossing their edges only cut a face that held all of it, so elsewhere it fell back to loose wire edges.
  - The line is now split where it crosses edges, and each face it runs through is cut. The pieces inside an arc (between it and the chord joining its ends) become one face again.
- **Dissolve Edges** could drop a corner that lay on a straight line in the merged face while a neighbouring face still used it, opening the mesh. Such corners now stay.
- **Inset Region no longer steps.** When an inset would fold over (wider than the region), it used to halve the thickness, so dragging jumped between values. It now finds the furthest thickness that doesn't fold (the middle face keeps at least 2% of its area, and no ring quad crosses into a bow-tie), so dragging slides smoothly to that limit and holds there. A region that folds at any thickness is left as it was.
- **Scripted drawing points** on an edge shared by two faces belong to the face that faces the camera, as a click would.

**Checked:** 1838 unit checks on Windows and 1814 on Linux, 0 failed. New tests:
- Profiles 0 to 1 give a rising volume; flat equals a one-segment chamfer; round equals a quarter circle; the editor setting reaches Ctrl+B and the modifier saves its own.
- Arcs on a drawn rectangle: side to side out and in, corner to corner, across a corner, out to the cube's edge, and on the cube's own edge. No wire edges, still closed, and one face inside the arc where it encloses one.
- Region inset swept from 0 to 0.8 in 0.01 steps: no jumps, never growing back, holding at the limit.

Stress:
- Push/Pull: 0 problems.
- The modeling section: 0 problems.
- Curved surfaces: 8 problems. 4 are new and come from Recess on regions that touch themselves at a sphere's pole, which can leave two corners in one place.
- The fuzzer: no crash.

## 2026-10-09 (round 19): Follow along the edges themselves, no Open Space Plane, Push/Pull on several faces

**Asked:** Follow with picked edges didn't follow the edges (especially several of them); remove the Open Space Plane, since guides do that work; Push/Pull on several faces at once, each along its own normal.

**Fixed**
- **Follow along picked edges** sweeps the face along the edges where they are, from the path's end nearest the face (SketchUp's Follow Me). Before, it carried a copy of the path's shape over to start at the face's centre, so the result ran beside the edges instead of along them. The face's own sides, still selected after switching from face to edge mode, no longer join the path and break it.

**Changed**
- **No Open Space Plane.** The plane chooser (front, side, view, last face), its offset and Draw in Open Space Only are gone. A click off the mesh lands on the ground, and guides (X / Y / Z, Ctrl+click an edge) take a shape anywhere else.
- **A new drawing's turn** follows the shape's first side, so a rectangle drawn at an angle gives its object that angle.

**Added**
- **Push/Pull on several faces.** Each connected group of selected faces moves along its own normal; separate groups move together (top and bottom of a box both out, say).
  - **Push/Pull Each Face** (Inspector, F9's Each Face, `pushpull individual on`) moves every selected face along its own normal.

**Checked:** 1716 unit checks on Windows and 1692 on Linux, 0 failed. New tests:
- Follow along a bent rail of quads, directly and through the editor with a stray side of the face selected; the end cap must sit at the path's end.
- Clicks off the mesh land on the ground.
- A 3-point rectangle at 30 degrees: the object's origin and X.
- Push/Pull on top and bottom, on every face of a cube, and with P on two sides in the editor.

Stress:
- Curved surfaces now include Push/Pull Each Face on curved regions: 5,036 operations, 4 problems. Pushing neighbouring faces of a convex surface in, each along its own normal, can make their walls meet (8 results), as Blender's Extrude Individual does.
- Push/Pull: 0 problems and 390 overlaps.
- The modeling section: 0 problems.
- The fuzzer: no crash.

## 2026-10-08 (round 18): Plasticity guides and perpendicular snaps, one Make Face, overlapping elements, Fluent-style tools, bevel and rotation fixes

**Asked:** commit and push round 17; rotating a face drawn on a rectangular plane still left artifacts; a 90-degree edge snap for polylines like Plasticity's; Ctrl+B sometimes bevelled at once without the helper, and the helper wouldn't go past two segments; guides like Plasticity's (from an axis, local or global, and from edges with Ctrl) instead of axis planes; the Make Face shortcut didn't always work; shapes drawn at an angle kept a global transform (pivot / centre, local / global); a way to see overlapping vertices and edges; Make Face and Smart Fill as one operation; keep Follow like UModeler's; tools from Fluent 4 that fit the current ones.

**Done first:** round 17 committed and pushed.

**Fixed**
- **Bevel's helper (Ctrl+B).** Auto Smooth shaded the bevel after it was recorded, and that change made the helper think the bevel had been undone, so it quit and left the bevel as it was. That happened as soon as there were 2+ segments: a second Ctrl+B bevelled at once, and the wheel stopped at two. Shading is now part of the operation. The helper stays, and the wheel goes up to 64 segments.
- **Turning a shape drawn across the Plane's grid.** When the turned shape reached past the cells it was cut into, the ring around it couldn't be rebuilt. The flat faces around it now join the ring as needed.
- **Concave quads were drawn wrongly.** A dart-shaped quad was split along the diagonal that runs outside it, so one triangle faced backwards. The other diagonal is used now. This also removed the overlaps from drawing circles across curved faces (35 to 0 in the curved stress).
- **Make Face** works in vertex, edge and face mode, and its key works anywhere in Edit Mode, not only with the mouse over the Scene view.
- **A new drawing takes its shape's place.** The first shape drawn in a new Drawing object moves the object's origin to the shape's centre and turns the object with the shape's plane, so Pivot, Local and Global behave.
- **Local in Edit Mode** follows the selection: Y along the selected faces' normal, X along their longest edge. The gizmo and G/R/S now move, turn and scale a shape on a slope, or turned on its face, along its own sides.

**Changed**
- **Guides instead of axis planes** (Plasticity's construction lines):
  - X, Y or Z while drawing lays a guide along that axis, Local or Global as Snap Axes says, through the last point (or the point under the mouse before the first click).
  - Ctrl+click on an edge lays a guide along it.
  - A polyline can follow a guide off its plane, which makes a 3D path for Follow.
- **Make Face and Smart Fill are one operation**, Make Face (F in the Blender keymap, Alt+F in Unity's):
  - Holes and wire loops in the selection close (every hole when nothing is selected): flat ones with a face, bent ones with a fan, cracks welded shut.
  - Selected vertices that form no loop get one face through them.

**Added**
- **Perpendicular Snap** (Plasticity): a point on an edge where the line from the last point meets it at 90 degrees. On by default; switch it in the Inspector or Modeling Tools, or with `drawmode perp on|off`.
- **Overlapping vertices and edges:**
  - Vertices lying on top of each other get yellow rings, and edges running along each other get magenta lines, live in Edit Mode.
  - Modeling Tools > Check: Overlapping Vertices & Edges counts them and has Show, Select and Merge.
  - Also as Edit Mode tools (Select Overlapping, Merge Overlapping) and the console: `overlaps [select|merge|show on|off]`.
- **Follow, UModeler's way.** Select the face and press Follow; with no wire path drawn from it, the face is remembered. Then select the path's edges in edge mode and press Follow again. The path starts at the face, from whichever end leaves it along its normal, and the picked edges stay.
- **Hard-surface tools after Fluent 4** (a new Hard Surface group in Edit Mode; settings in the Inspector; console `hs ...`):
  - **Grid:** cuts quads into columns x rows, splitting neighbours to match.
  - **Pipe:** a capped tube along selected edges or a drawn polyline.
  - **Array:** repeats the selected faces along the selection's own X, Y or Z.
  - **Taper Extrude:** an extrusion with its end scaled.
  - **Recess / Plate:** an inset panel pushed in, or pulled out when the depth is negative.

**Checked:** 1718 unit checks on Windows and 1694 on Linux, 0 failed. New tests:
- Ctrl+B three ways (mouse over the Scene view, elsewhere, and a second bevel starting at 7 segments) with six wheel steps.
- Shapes turned 15 to 90 degrees on a quad, on the 10 x 10 Plane across and inside cells, and on long faces (where a shape turned past its face's edge has no surface left around it, only validity is required).
- The perpendicular snap, on and off.
- Ctrl+click and X guides on a turned cube, Local and Global.
- A 3D polyline along guides, then Follow.
- Make Face with both keymaps, in each mode, with the mouse away from the Scene view, plus loose vertices.
- Overlap finding, selecting and merging.
- Follow along picked edges.
- A rectangle drawn on the view plane (the new object's origin and up) and the Edit Mode frame.
- Each Fluent tool, closed where it should be.

Stress:
- Curved surfaces: 4,876 operations including the new tools, 4 problems, and 69 results with faces on top of each other (91 before).
- Push/Pull: 0 problems, and 390 results with faces newly on top of each other.
- The modeling section: 6,080 operations with 0 problems.
- The editor fuzzer: 6,000 frames with no crash.

## 2026-10-08 (round 17): drawing on any axis plane, turning drawn shapes cleanly, the keymap dropdown, the Utah teapot, deleting materials, curved surfaces

**Asked:** commit and push round 16; draw polylines on the YX and YZ planes as well as XZ, with an axis-lock workflow like Extrude's, usable with Follow; rotating a face just drawn onto a face left artifacts; the keymap preset dropdown in Preferences disappeared when clicked; the Utah teapot with a camera and a point light as the scene you start with; deleting a material from the Materials window; more edge-case testing on curved surfaces for Push/Pull and the other tools.

**Done first:** round 16 committed and pushed.

**Fixed**
- **Dropdowns inside dialogs.** A dropdown opened inside Preferences (the keymap preset, the frame-rate cap) closed every popup, the dialog included, so the list vanished. It now opens on top of the dialog. Picking an item, or clicking the field again, closes only the list.
- **Turning a shape drawn on a face.** Its ring of faces twisted over itself because the inner corners turned while the outer ones stayed. The ring is now rebuilt between the two outlines whenever a move folds it, live during gizmo drags and Blender's G / R / S. The selection follows.
- **Follow on a face of a solid** no longer leaves the old face inside as a start cap. That cap put three faces on each of its edges. The solid now grows the way Extrude grows it.
- **Regions that touch themselves at a vertex** (a cone's apex between two arms of a selection, a pinch on a torus) can now be extruded, inset and refilled without an edge ending up in four faces. Each such vertex gets one copy per side.
- **Smart Fill** fills holes whose outline touches itself one lobe at a time. Before, it made one face that ran through the shared vertex twice.
- **Inset Region** asked to go further than the region is wide now backs off until no face turns over, instead of folding the ring onto the inner faces.
- **Bevel** on curved surfaces no longer leaves zero-area patches where two bevels meet. The twin arcs are welded instead.
- **Push/Pull through a curved region** no longer leaves faces lying on the opening. When the cut can't open the far side cleanly, it stops at the far side, as it already does for other unclean holes.

**Added**
- **Axis planes while drawing.** X, Y and Z put the shape on the YZ, XZ or XY plane, like an axis lock on Extrude:
  - Before the first click, the plane goes through the first point. Pressing the same key again goes back to the surface's own plane.
  - Mid-polyline, the plane turns through the last point, so one line can climb a wall and then run across a floor. Enter keeps it as wire edges: a 3D path for Follow.
  - A closed loop that turned planes stays a loop of wire edges, since it isn't flat.
  - Also in the Inspector and Modeling Tools (Axis Plane), and the console: `drawmode axis x|y|z|none`.
- **The Utah teapot.** GameObject > 3D Object > Teapot is Martin Newell's 32 Bezier patches, tessellated and welded, with UVs. A new project, and File > New Scene, now start with it on a ground plane, along with a Main Camera and a Point Light (Blender's start-up cube, camera and light). Your existing SampleScene is unchanged.
- **Delete Material** in the Materials window, from the right-click menu or the Delete key over the window:
  - It asks first and shows how many slots use the material.
  - Those slots are emptied, so their faces get the default material.
  - A material asset's .mat file goes to the Recycle Bin.
  - Undo puts the slots back.
  - Console: `deletemat <name | Assets/...mat>`.
- **A curved-surfaces stress section** (`blendity_stress --only curved`). It runs 4,396 operations on 10 curved meshes: UV and ico spheres, a torus, a cylinder, a cone, Catmull-Clark and bevelled boxes, a bumpy blob, the teapot and an open hemisphere. The operations:
  - Push/Pull on single faces and on curved regions, in, out, through and onto.
  - Inset (individual and region), Extrude, Bevel, Poke, Triangulate / Tris to Quads.
  - Shapes drawn on and across curved faces, then turned.
  - Follow, Smart Fill, Bridge, Push Through, Delete Loose, Merge by Distance and Auto Smooth.
- Console: `newscene`.

**Checked:** 1643 unit checks on Windows and 1619 on Linux, 0 failed. New tests:
- The preset dropdown in Preferences opened, closed and picked with real clicks while the dialog stays open.
- The teapot's welding, facing, UVs and size.
- The starter scene, including save/load and New Scene.
- Drawing up and over a cube with Z then X, then Follow along that path into a closed solid.
- A bent closed loop staying wire edges.
- A drawn rectangle turned 30, 45, 90 and 170 degrees with R (no bad triangulations, no overlaps, still closed, still selected), and the ring repair on its own.
- Delete Material with its confirmation, a scene material, an asset going to the (scratch) trash, and undo.
- A cone region pinched at its apex extruded, inset, and refilled with Smart Fill.

Stress:
- Curved surfaces went from 59 problems to 4 (zero-area faces in rare corners) and from 283 results with faces on top of each other to 91.
- Push/Pull: 0 problems, and the results with faces newly on top of each other fell from 676 to 390.
- The modeling section: 6,080 operations with 0 problems.
- The editor fuzzer: 6,000 frames with no crash.

## 2026-10-08 (round 16): drawing in open space, a UV gizmo, Merge by Distance in Edit Mode, Z-fighting highlights, Delete in the Hierarchy

**Asked:** commit and push round 15; draw outside of a face (UModeler) or in open space (Plasticity); the Scene view's gizmo in the UV editor for moving, scaling and rotating; Blender's Merge by Distance; Z-fighting faces stayed highlighted in Object Mode and after the object was deleted; the Delete key for objects in the Hierarchy.

**Done first:** round 15 committed and pushed.

**Fixed**
- **Delete in the Hierarchy.** In Edit Mode, Delete removed the selected faces of the mesh being edited even with the Hierarchy focused. It now removes the selected objects there (leaving Edit Mode first), as in Unity.
- **The Project window's Delete key** now also needs the mouse over the Project window, so a Delete meant for the Hierarchy or the Scene view can never reach a file selected there earlier. (Several files went to the Recycle Bin this afternoon while the editor was open, which is what this guards against.)
- **Z-fighting highlights** are dropped once their object is deleted or its mesh changes. They only show while you edit one of the objects involved, or while the Z-Fighting panel is open after checking from it, so nothing looks selected in Object Mode.
- **Drawing rectangles:** the opposite corner no longer snaps onto the first corner's axes, which could flatten the rectangle into a line when seen at a shallow angle.

**Added**
- **Drawing in open space** (Inspector and Modeling Tools, while drawing):
  - Clicks off the mesh go onto an **Open Space Plane**: the ground, a **front** (XY) or **side** (YZ) plane, a plane **facing you** through the point you orbit, or the **last face's plane**, to keep drawing past a face.
  - A **Plane Offset** moves the plane along its normal.
  - **Draw in Open Space Only** ignores surfaces like Plasticity's construction plane. Corners and edges of the mesh still guide the point, which stays on the plane.
  - A faint grid shows the plane under the cursor. A closed shape there becomes a new face; an open one becomes wire edges.
- **The UV editor's gizmo**, like the Scene view's, at the centre of the UV selection:
  - **Move** has U (red) and V (green) arrows and a centre square for free movement. **Rotate** has a ring. **Scale** has U and V handles and a centre box for uniform scaling. **Transform** (Y) shows all three.
  - W, E, R and Y pick them in the UV editor too, and Ctrl snaps.
- **Merge by Distance in Edit Mode** (Merge & Clean Up; Blender preset: Alt+M):
  - Selected vertices closer than the distance weld (all of them when nothing is selected), and it reports how many were removed.
  - F9 adjusts the **Merge Distance** and **Unselected**, which lets selected vertices weld onto unselected ones, as in Blender.
- Console: `drawmode plane ground|front|side|view|face [offset]`, `drawmode space on|off`, `zfight clear`, `delete`, `editop merge_distance`.

**Checked:** 1573 unit checks on Windows and 1549 on Linux, 0 failed. New tests:
- Clicking a Hierarchy row and pressing Delete or Backspace, also from Edit Mode, with a Project file selected and focused earlier that must survive.
- Drawing with real clicks on the front plane at a depth, on the view plane, and "open space only" over a cube.
- Merge by Distance on an unwelded cube (24 to 8 vertices, closed), its F9 distance, selected only, and Unselected.
- Dragging the UV gizmo's U arrow, V scale handle and rotate ring with real mouse events.
- Z-fighting outlines gone in Object Mode, and pruned when the objects are deleted.

Stress: Push/Pull 0 problems; the modeling section 6,080 operations with 0 problems; the editor fuzzer 6,000 frames with no crash.

## 2026-10-08 (round 15): rebindable keymaps with presets, draw axes, a Z-fighting check, UV editing tools, grouped Edit Mode tools

**Asked:** commit and push round 14; local or global snap axes for drawing; put the selection tools in one place and combine other tools where they can be; a Z-fighting check that says which faces are a problem and can be removed; more UV tools (selecting faces, rotating, scaling, selecting and moving whole islands); re-render the Camera Preview whenever a light's colour temperature changes; rebindable shortcuts in Preferences, with keymaps for other programs (Blender, Maya and more).

**Done first:** round 14 committed and pushed.

**Fixed**
- **Colour temperature didn't refresh the Camera Preview.** The scene's render fingerprint, which decides when the preview, the Rendered view and the live preview re-render, only included a light's type, colour, intensity and range. It now includes every light setting: temperature, spot angles, area size, shadows.

**Added**
- **Keymap** (Preferences > Keymap):
  - Every shortcut is a named action (about 75) with a context: anywhere, in Edit Mode, over the Scene view, or the Scene view in Edit Mode. So one key can mean different things in different places, as before.
  - **Presets:** **Unity** (Blendity's own), **Blender** (G/R/S, E, X, M, F, Ctrl+R...), **Maya** (Q/W/E/R, F8-F11 for component modes, B for soft select...), **3ds Max** (W/E/R, Z to frame, 1/2/4 sub-objects, Shift+E...) and **SketchUp** (P, R, C, A, L, M, Q, Space...).
  - Click a binding and press the new key (Esc cancels, Backspace clears). Actions are found by name or by key, and clashes within overlapping contexts are shown.
  - Changes are saved with the preferences and can be reset. The old "Blender Transform Keys" option still works on top of Unity.
- **Snap Axes: Local or Global** for drawing (Inspector and Modeling Tools). Local follows the face's edges or the object's rotation; Global uses the world's axes. Parallel and perpendicular edge snaps and guides work either way.
- **Z-fighting check** (Modeling Tools > Check: Z-Fighting; "Select Z-Fighting" in Edit Mode):
  - Finds faces lying on top of each other, within objects and between objects (in world space).
  - Each pair says what it is and what can go: a duplicate (remove one), both sides of an inner wall (remove both), a face hidden under another (remove it), or a partial overlap (move one).
  - Pairs have Select and Remove buttons, there's **Remove Suggested** for all of them, and the faces are outlined in the Scene view (red: can go).
  - Objects merely resting on each other aren't flagged.
- **UV editor:**
  - **Vertex / Face / Island** select modes (1/2/3). Click inside a face, box-select by face centre, or click or drag a whole island.
  - **Sync** with Edit Mode's face selection.
  - **All / None / Invert / Grow to Islands.**
  - **Rotate ±90** or by an angle, **Scale** by a factor, **Move** by an offset, **Flip U / V**, **Fit** to 0-1, **Center**, and **Align** (left, right, top, bottom, straight U / V).
  - Arrow keys nudge the selection (Shift: further).
- **Edit Mode tools in groups:**
  - **Select** first: every way of selecting in one place, with All and None. Then Create & Extrude, Cut & Divide, Merge & Clean Up, Deform, and Shading & UV.
  - Each group folds away; the Mesh menu has the same groups.
  - Mark / Clear Seam, Mark / Clear Sharp, Shade Smooth / Flat, To Tris / Quads and Grow More / Less each share one button.
  - Extrude Individual is now a switch on Extrude, like Inset's.
  - Select Overlapping became the Z-fighting check.
  - The doubled Knife and Merge Coplanar buttons are gone.
- Console: `keymap <preset>`, `keymap bind <action> <keys>`, `keymap clear <action>`, `zfight [scene] / zfight fix`, `uvsel ...`, `uvxf ...`, `drawmode axes local|global`, `duplicate`.

**Checked:** 1527 unit checks on Windows and 1503 on Linux, 0 failed. New tests:
- Light settings change the render fingerprint.
- Every preset's key layout, with no clashes in any preset, and chords that print and parse back.
- Real key presses under Unity and Maya, a rebind, clash detection, and overrides saved and loaded.
- Local and global draw axes on a rotated cube.
- Z-fighting: a duplicated cube fixed with one copy's faces removed, the inner wall of two joined boxes, and a hidden face versus a partial overlap.
- UV face and island selection and every transform, synced with Edit Mode.
- Every operator in a group, all selection tools in Select, and the Extrude Individual switch.

Stress: Push/Pull 0 problems; the modeling section 6,080 operations with 0 problems; the editor fuzzer 6,000 frames with no crash, with keys going through the new keymap.

## 2026-10-08 (round 14): archways pushed through cleanly, drawing over earlier edges, Smart Fill, Auto Smooth, camera sequences

**Asked:** commit and push round 13; fix the face artifacts in the half circle of an archway pushed through; a Smart Fill that patches open edges into a surface, tested where the edges converge to no area; drawing on a face and pushing or pulling through should work smoothly with edges already on either face (rectangles, circles, arcs); an option to shade bevels and other round surfaces smooth automatically; render the scene from a sequence of cameras.

**Done first:** round 13 committed and pushed.

**Fixed**
- **Archway artifacts.** Where a hole comes out of a face, the face around it was built by zipping its outline to the hole's outline by angle. Seen from the wall's far corner, an arch's curve runs "backwards", so some faces came out flipped, on top of their neighbours. That's the flicker in the half circle. Now every zipped result is checked: all faces must face the right way and add up to exactly the ring's area. When they don't, the ring is made of two faces joined by bridges, which works for any hole shape. Archways with 4-, 8- and 16-segment arcs, pushed through with Push/Pull or Push Through, now give a closed wall with the exact opening, no overlapping faces and every face triangulated exactly.
- **A selection reaching the wall's edge** (the doorway, the arch and the wall above it) pushed through left a folded face. Push Through now welds its result like Push/Pull does.
- **Scripted drawing on non-convex faces.** `drawpoint` found the face under a point with a test that only worked for convex faces, so a point on the ring around an earlier circle counted as "on the ground". Mouse drawing was not affected.

**Added**
- **Smart Fill** (Edit Mode tools, Mesh menu, Modeling Tools for whole objects). Open edges, whether sides of faces with nothing beyond or wire edges, are chained into loops, inside the selection or across the whole mesh. Each loop is closed the way that suits it:
  - A flat loop becomes one face, wound like the faces around it, so a box with a missing side is a closed solid again.
  - A bent loop becomes a fan from its centre.
  - A loop with no area is welded shut, so no zero-area face is ever made: a crack between unwelded copies of an edge, a slit, or a rim converging on a point (welded into one vertex).
  - A loop lying on a line is left alone and reported.
  - A face's own outline is never filled a second time.
- **Auto Smooth:**
  - Operators that make curved surfaces (a bevel with 2+ segments, a pulled circle) turn on smooth shading by angle (30 degrees by default) on flat-shaded meshes. Round parts look round and corners stay crisp. Meshes you shaded per face are left alone.
  - Toggle and angle are in Edit Mode's bevel settings and the Mesh menu, and saved with the preferences.
  - **Shade Auto Smooth** applies it to whole objects (Mesh menu, Modeling Tools).
- **Render Camera Sequence** (Render window):
  - Every camera with **Render in Sequence** on (new in the Camera Inspector) is rendered in **Sequence Order**, using the current settings and engine.
  - Each image is saved to `Renders/<scene>_sequence_<time>/NN_<camera>.<format>`.
  - It shows progress and can be stopped. Renders go back to the Main Camera afterwards.
- Console: `autosmooth`, `rendersequence [folder|stop]`, `editop smart_fill`, `meshop smart_fill`, `meshop shade_auto_smooth`.

**Checked:** 1447 unit checks on Windows and 1423 on Linux, 0 failed. New tests:
- Archways (three arc sizes) pushed through two ways, plus the notch case.
- Five "prior edges" cases, each pushed through or pulled out, each a closed solid with the exact volume, no overlaps and clean triangulation:
  - A rectangle drawn over an earlier circle's edge.
  - A circle drawn across an earlier rectangle.
  - A rectangle pushed through where the back has a circle.
  - An archway pushed through where the back has a rectangle across it.
  - A rectangle over a circle, pulled out.
- Smart Fill: one and two holes, a selection, a bent hole, a crack, a pinched rim, a line, a wire triangle, a slit, and the editor operator.
- Auto Smooth on 1- and 4-segment bevels, per-face shading, off, and by hand.
- A three-camera sequence in order with one excluded, three different images, and a stopped path-traced sequence.

Stress results:
- The modeling section, with Smart Fill on holes, cracks and wire loops in all 26 awkward meshes: 6,080 operations, 0 problems.
- Push/Pull: 0 problems, overlapping results still 676.
- The editor fuzzer: 6,000 frames, no crash.

## 2026-10-08 (round 13): Push/Pull without overlapping faces, Delete Loose, region inset, depth of field everywhere

**Asked:** commit and push round 12; check Push/Pull and similar tools for the overlapping-face artifacts still showing up; Blender's Delete Loose; inset several faces individually or as one; clearing a number field and pressing Enter should give 0, as in Unity; a New Material made in the Inspector should appear in the Materials folder; draw polygons from a face's exact centre, as in Plasticity; let depth of field blur the foreground while the background stays sharp.

**Done first:** round 12 committed and pushed.

**Fixed: Push/Pull's overlapping faces.** A new check finds faces lying on top of each other, or a hair apart (they z-fight). On the stress suite's 26,620 Push/Pull operations the old code produced 1,502 such results, plus 2 broken (open) meshes. Now there are 676 and none broken: 0 problems. What's left is mostly holes through curved surfaces and deliberately broken inputs (unwelded faces, inside-out normals). The causes, each fixed:
- **Stopping 2 mm short.** Push/Pull stopped just short of any surface in its way. A block pushed back down hovered a hair above the surface, with paper-thin walls, and two halves pulled to the same height left a sliver between them. Now, where the walls beside the selection end (flush), it goes exactly there: the walls vanish and the faces merge back, as in SketchUp. Asked for more, it carries on from there as a second step, so pushing a pulled block past flush makes a pocket, and pulling one half past the other works in one go.
- **Sliding past a neighbour counted as hitting it.** Edges lying on the selection's outline, such as a neighbouring block's top edge, stopped the move. Only geometry inside the swept area does now.
- **New walls in a neighbour's plane.** With Merge Coplanar off, pushing along a coplanar side built a wall inside that side. And when a neighbour stretched along one edge but got a wall along another, the wall covered the strip it grew into. Now a neighbour that stretches takes every side lying in its plane.
- **Folds at corners.** Faces touching the outline only at a corner kept the old corner, making a fold outside the solid. When every face around the outline contains the move's direction, the corners now simply slide (Blender's move along the normal).
- **Faces flattened to nothing** are dissolved into their neighbour, the way Blender's Dissolve Degenerate does it, instead of being left to flicker or turn inside out. Back-and-forth spikes left by welding are removed too.

**Added**
- **Delete Loose** (Blender: Mesh > Clean Up > Delete Loose): removes vertices and wire edges that no face uses, within the selection, or everywhere when nothing is selected. It's in the Edit Mode tools, the Mesh menu and the Modeling Tools window, and also works on whole objects.
- **Select Overlapping:** selects faces lying on top of each other, so leftovers (or imported ones) are easy to find and delete.
- **Inset Individual** switch (Blender's Inset > Individual), also in the F9 panel:
  - On: each face is inset by its own amount.
  - Off: the whole selection is inset as one face. Only its outline moves in, by a thickness, and the edges between the selected faces stay.
- **Empty number fields give 0**, as in Unity, within the field's limits (a field with a minimum of 1 gives 1).
- **New Material in an Inspector slot** (and New Material of Type) now saves a `.mat` in `Assets/Materials`, so it shows up in the Project and Materials windows like any other.
- **Face centre (Plasticity):**
  - When drawing, a face's exact centre (its area centre, not the average of its corners) snaps, shown in orange.
  - **Start at Face Center** makes circles, polygons and centre rectangles begin there wherever you click on the face.
- **Depth of field everywhere:**
  - The Game view, the Shaded Camera Preview and a piloted Scene view now show the camera's depth of field, not only path-traced renders.
  - Nearer things blur as well as farther ones. A blurred foreground spills softly over a sharp background, and a blurred background never spills over a sharp foreground (a "scatter as gather" pass, as in EEVEE).
- **Keep Focus on Point:** Pick Focus Point now keeps that point in focus while the camera moves (Blender's Focus Object). Piloting no longer moves the focus to the orbit pivot.
- Console: `insetmode`, `newslotmat`, `focus`, `position`, `drawmode facecenter`, `editop delete_loose`, `editop select_overlapping`.

**Checked:** 1309 unit checks on Windows and 1285 on Linux, 0 failed. New tests:
- Push/Pull sequences:
  - A square pulled up then pushed back flush, then pushed past it.
  - A pocket pulled back flush.
  - Two halves pulled to the same height, to different heights and past each other.
  - One half pulled and the other pushed.
  - Inset, push in and pull back.

  Each must stay closed, keep the exact volume and have no overlapping faces.
- Delete Loose, with and without a selection.
- Region and individual inset: face counts, a closed result, the inner area.
- Clearing float and int fields (with a minimum).
- New Material assets from the Inspector.
- Face-centre drawing with real clicks, and the area centre of an L-shaped face.
- Rasterized depth of field: the in-focus background untouched, the foreground's blur spilling over it, and the reverse.
- Focus tracking while moving and piloting.

Stress: the modeling section ran 5,716 operations with 0 problems, and the editor fuzzer ran 6,000 frames with no crash.

**Note:** your two material files in `Assets/Materials` (`New Material.mat`, `Solid Material.mat`) were moved to the Recycle Bin at 1:34 PM, while your own Blendity session was running. They can be restored from there.

## 2026-10-08 (round 12): drawing across faces, camera piloting fixes, asset folders, face material picker, drawing modes, guide lines, frame rate

**Asked:** commit and push round 11; make the camera actually adjust while piloting; fix drawing a circle onto a surface whose faces overlap inside the circle; renaming a material should rename its file; organise assets into folders like Unity; pick the material itself when assigning faces; draw shapes uniformly (from the centre), not only corner to corner, as in Plasticity; set the editor's frame rate and explain the performance cost; delete things in the Project window; construction lines to line drawings up with, as in Plasticity.

**Done first:** round 11 committed and pushed.

**Fixed**
- **A circle (or any closed shape) drawn over several faces** used to be added as a separate face lying over them. It is now cut into every face it covers, with a vertex wherever it crosses an edge, and all the pieces inside come out selected for Push/Pull. Parts hanging off the surface become wire edges.
- **Piloting a camera:**
  - When the camera's frame was narrower than the view, the frame didn't match what the camera saw, and starting to pilot changed the camera's field of view. The frame is now exactly the camera's view, and starting changes nothing.
  - A physical camera's lens was worked out from the wrong side of the sensor; it now follows the camera's Sensor Fit.
  - Moving the camera any other way while piloting (gizmo, Inspector, undo) now moves the view with it, instead of being overwritten.
- **Copied materials kept the wrong name.** A `.mat` copied outside Blendity kept the old name inside it. The file name now wins, as in Unity, so nothing gets renamed by surprise.

**Added**
- **Project window like Unity's:**
  - A nested folder tree, and a **+** menu or right-click for **Create > Folder / Material** in the current folder.
  - Drag files or folders onto a folder, a breadcrumb or `..` to move them. Dragging a scene material from the Materials window onto a folder saves it there as an asset.
  - **F2 / Rename** renames in place.
  - **Delete / Del** asks first, then sends the item to the **Recycle Bin** (Trash on Linux and macOS), where it can be restored. Only things inside Assets, research papers or screenshots can be deleted.
  - Moving or renaming keeps everything working: the shared material, texture paths, `.scene` files that mention it, and undo. A deleted material stays on its objects as a scene material.
- **Material name = file name**, both ways: renaming a material renames its `.mat`, and renaming the file renames the material.
- **Face Material** field in Edit Mode (Inspector and Modeling Tools): a grid of preview spheres with every material asset (from any folder), the object's slots and the scene's materials. Pick one and the selected faces use it; its slot is found or added for you.
- **Drawing modes from Plasticity:**
  - Rectangles from a **Corner**, the **Center**, or **3 Points** (one side at any angle, then the width), with **Square** to keep the sides equal.
  - Circles and polygons from the **Center**, **2 Points** (across) or **3 Points** (on the circle).
- **Guide lines**, Plasticity's lines and SketchUp's guides:
  - The new **Guide** draw shape, or **Guides from Edges**. They show as dashed lines and are saved with the scene, and adding them can be undone.
  - Drawing snaps to where two guides cross (or where one pierces the drawing plane), along a guide, and parallel to it.
- **Editor frame rate** (Preferences > Performance, and the Profiler):
  - A cap from 30 to 240 fps or Unlimited, now applied to mouse movement too (before, every mouse move drew a frame).
  - Redraw **only when something changes** (the default) or **always**.
  - It shows the measured frame cost and what that reaches on this machine, warns when the cap is over budget, and explains what each setting costs (CPU, battery, input lag, path-tracing samples). The stats overlay shows the live rate.
- Console: `drawmode`, `guide`, `clearguides`, `guidesfromedges`, `fps`, `redraw`, `preferences`, `facemat`, `mkfolder`, `moveasset`, `renameasset`, `deleteasset`.

**Checked:**
- 1209 unit checks on Windows and 1185 on Linux, 0 failed. New tests cover:
  - Circles across a 4 x 4 grid: the pieces' areas add up, and pulling them makes a solid.
  - The circle through grid corners, hanging off the edge, and along existing edges.
  - A circle overlapping a drawn rectangle on a cube, then Push/Pull, which stays closed.
  - Piloting in a tall view, moving the camera from the Inspector, and a physical camera's lens.
  - Folder move and rename, scene-file rewrites, undo after a move, a copied `.mat`, delete to a scratch trash, and refusing to delete outside Assets.
  - Face material picking.
  - Every drawing mode.
  - Guide snapping with real mouse clicks, save and load, and undo.
  - The frame-cap timing.
- Stress: the modeling section now also draws random circles and squares across faces on all 26 awkward meshes, plus degenerate loops: 5,716 operations, 0 problems. Push/Pull still has only its 2 long-standing problems. The editor fuzzer ran 6,000 frames with no crash.
- **Safety:** the tests and the fuzzer now work in a scratch project and a scratch trash, because the Project window can move and delete files. Your `Assets` folder was checked to be unchanged after full runs.

## 2026-10-08 (round 11): snapping, a Materials window, Modeling Tools, camera piloting, Plasticity tools

**Asked:** commit and push round 10 and remove old Blendity builds; drawing that snaps to edges parallel to a rotated object, not only world axes; test edge cases (a rectangle, an arc on its side, then Push/Pull); materials as objects you can see and adjust outside any object (Unity); drag a mesh onto another and snap it to the surface, and Unity's vertex snapping; move and frame a camera through the Scene view, including its FOV; useful tools from Plasticity; a separate tools window like UModeler's; sanity and edge-case testing.

**Done first:** round 10 committed and pushed; `Blendity.old.exe` and the stale `build\dev.exe` deleted, leaving only `dist\windows\Blendity.exe`.

**Fixed (found by the new edge-case tests)**
- An arc drawn from corner to corner of a rectangle, bulging outside it, used to "split" the rectangle with points outside it (a broken face). It now becomes a second face sharing that side, as in SketchUp.
- Push/Pull on a free-standing face (nothing attached along its outline, like a shape drawn on the ground) left the bottom open. It now makes a closed solid, keeping the original face as the floor (or lid when pushed).
- Shapes drawn on a rotated object's face came out lined up with the world. The drawing plane now takes its axes from the face's edges (or the object's own rotation).
- Imprinting a shape that lines up exactly with the face's outline could make extra triangles in the ring (a floating-point tie); the ring is now clean quads.

**Added**
- **Drawing inference** (SketchUp-style), from the last point: the plane's own red and green axes, any mesh edge's direction (parallel) and its perpendicular, and square to the previous segment, each shown with a dotted guide; Shift locks to the nearest. **Corner Radius** rounds rectangles, polygons and closed polylines (Plasticity's curve fillet).
- **Materials window:** every material in the scene and every material asset as a preview sphere. Click one to edit it there, drag it onto objects, faces, Hierarchy rows or slots, or right-click to assign, save as an asset, duplicate, or select the objects using it.
- **Modeling Tools window** (UModeler's tool panel): Create (primitives and parametric shapes), Draw, Object tools (Join, Boolean, Reset XForm, Mirror, origin, camera), Edit Mode's tools, UV and snapping, always available. It shares the Edit Mode tool code with the Inspector.
- **Surface snapping:** Ctrl+Shift while moving drops objects onto the surface under the mouse, resting on their lowest point, and optionally aligned to its normal. Dragging objects from the Hierarchy into the Scene view does the same.
- **Vertex snapping** (Unity's V): hold V and drag; the selection's nearest vertex lands on the vertex under the mouse (objects, and selected vertices in Edit Mode), with Unity's square marker.
- **Pilot Camera:** look through a camera and frame the shot by navigating. The camera follows the view, the wheel dollies, Ctrl+wheel changes its FOV (or focal length on a physical camera), and a frame with thirds guides shows the shot. **Align to View** is there too. Both are in the Camera Inspector and the Modeling Tools window.
- **From Plasticity:** **Shell** (hollow a solid, opening the selected faces), **Thicken**, **Draft** (taper side faces by an angle) and a **Radial** mode on the Array modifier.

**Checked:** 1049 unit checks on Windows and 1025 on Linux, 0 failed. New tests cover: drawing on a rotated object and pushing the result through; the rectangle + arc + Push/Pull case; degenerate shapes and lines off the mesh; fillets and drawing into a drawn face; Shell, Draft, Thicken and Radial Array; dropping onto a surface; V vertex snapping with real mouse events; piloting with the wheel; both new windows. Stress results:
- Push/Pull ran 26,620 operations and still has only its 2 long-standing problems. A first run found 36 new zero-area faces from the new floor; fixed.
- The modeling section ran 5,066 operations with 0 problems.
- The editor fuzzer ran 6,000 frames with no crash.

## 2026-10-08 (round 10): materials as assets, drawing tools, UModeler tools, origin fix

**Asked:** push the previous round; keep a changelog; Shift + scale on a face makes a new face, as in Blender; automatic seams from sharp edges; a light "focus" like the camera's; UModeler features Blendity lacks; Unity-style material assets you drag onto objects and slots; removing material slots; a polyline tool for circles, arcs and so on, drawn onto a mesh with snapping; fix the origin not being used as the transform point.

**Fixed**
- **Origin as the transform point.** The toolbar's handle position defaulted to *Center*: the gizmo sat at the bounds centre and rotations turned about it, while scaling still used the origin, so a moved origin seemed to do nothing. It now defaults to *Pivot* (the origin, as Blender transforms one object), the choice is saved, and Center scales about the same point it rotates about; G / R / S follow the same setting.
- The "Blender Transform Keys" preference was never saved (it was missing from the preferences file).

**Added**
- **Shift + drag** a gizmo handle in Edit Mode extrudes first: faces get walls and a new cap, so Shift + scale makes an inset face and Shift + move an extrusion (Blender's E then S, ProBuilder / UModeler's Shift-drag). In vertex / edge mode the edges grow faces.
- **Seams from Sharp Edges** (UV menu, UV Editor, edge-mode tools): a seam on every edge whose faces meet at more than the Sharp Angle (30 degrees, Blender's default) and on edges marked sharp; *+ Unwrap* does both. **Select Sharp Edges** too.
- **Light aiming**: Light > *Aim at Point* is an eyedropper like the camera's focus picker. Click a surface and spot, area and directional lights turn to shine at it (X kept level, Unity's LookAt); the range grows to reach it. `pickfocus` and `aimlight x y z` in the console.
- **Material assets** (Unity): Assets > Create Material, or a slot's *Save as Material Asset*, writes `Assets/Materials/<name>.mat`. Drag one from the Project window onto an object in the Scene view (the face under the mouse picks the slot), onto a face in Edit Mode (all selected faces if it is one of them), onto a Hierarchy row or onto an Inspector slot. Images drag onto a material's texture fields. Selecting a `.mat` shows its full material editor; every renderer using it shares one instance, edits are written back to the file, and scenes refer to the file (the values are kept as a fallback).
- **Material slots**: an x on each slot removes it (its faces move to the slot before, as in Blender), plus **Remove Unused Slots**.
- **Drawing tools** (UModeler / SketchUp): Polyline, Rectangle, Circle, Arc and Polygon, drawn onto the edited mesh or the ground (Inspector > Draw, Mesh > Draw, `draw <shape>`). Snapping to corners (green), midpoints (cyan), edges (red), faces (blue) and the ground, with Ctrl for the grid and Shift for the plane's axes, plus a live length. A closed shape inside a face is cut into it, ready for Push/Pull; a line between points on a face's edges splits it through every bend; anything else becomes a new face or wire edges. With nothing selected it starts a new mesh.
- **From UModeler**: **Follow** (sweep a face along a drawn path, SketchUp's Follow Me, with mitred corners), **Spin / Lathe** (edges or vertices turned round an axis), **Slice** (cut by an X / Y / Z or view plane through the selection, optionally removing a side; Blender's Bisect), **Reset XForm** (bake rotation and scale into the mesh), **Mirror** (applied, welded), **Select Similar** (normal, area) and **Select by Material**.

**Checked:** 1005 unit checks on Windows and 981 on Linux, 0 failed (new tests: Shift-drag extrude, seams, light aiming, material assets with save / load and slot removal, drawing on a face and on the ground, Follow / Spin / Slice, Reset XForm and the pivot). The stress suite ran 4,728 operations of the new and existing tools on 26 awkward meshes, plus 390 random shapes, with 0 problems; the editor fuzzer ran 1,500 frames with no crash.

## 2026-10-08 (round 9): every Blender export format, file dialogs, Boolean, modifier stack, shapes, n-gons

**Asked:** Blender's export options and formats (tested against an installed Blender); Save As dialogs for objects and screenshots; origin handles; Boolean operations; combining meshes; parametric objects; SketchUp-style n-gon editing; extruding vertices and edges; a Blender-style modifier stack.

**Added:** export to OBJ, binary / ASCII FBX, glTF (.glb / .gltf), STL, PLY and USD (.usda) with Blender's options, and import of the same; every format checked both ways in Blender 5.2 (this found that the earlier ASCII FBX did not open in Blender). Native Save / Open dialogs. Join, Separate, Boolean (applied or a live modifier with a wire cutter). Wire edges and vertex / edge extrusion. Edit Origin with handles and click snapping. A modifier stack with Edit Mode / Viewport / Render toggles and Bevel, Weld, Triangulate, Wireframe, Displace, Simple Deform, Cast and Screw. Thirteen parametric shapes. N-gon Mode, Merge Coplanar and the Knife (K).

**Checked:** 963 checks on Windows, 939 on Linux; a new stress section with 0 problems.

## 2026-10-08 (round 8): lights, export, selection fixes

Spot and Area lights with colour temperature (rasterizer and CPU / GPU path tracing); OBJ and FBX export; editing another mesh by clicking it in Edit Mode; the Hierarchy's create menu; Alt+click loops and Ctrl+Alt+click rings; the scene gizmo's views; origin dots; a lockable Camera Preview; a camera focus eyedropper; old app builds removed.

## 2026-10-08 (round 7): Blender's modal editing, edge selection, per-face shading

G / R / S modal transforms with axis locks and typed values; edge mode keeps the picked edges; per-face Shade Smooth / Flat and Mark Sharp; Bevel Clamp Overlap and a dozen more Blender operators; Unity-style colour fields; render resolution presets; docs/USABILITY.md.

## 2026-10-07 (rounds 4-6): GPU rendering, Push/Pull, physical cameras

Vulkan GPU path tracing with ray tracing hardware and combined CPU + GPU rendering; SketchUp-style Push/Pull as a confirmed operator, hardened by a stress test on awkward meshes; tools split by selection mode; Set Origin; physical cameras with depth of field and exposure; edge tools (Bevel, Bridge, Push Through); Adjust Last Operation (F9); Inspector expressions and multi-object editing; surface types (opaque, cutout, transparent, glass).

## 2026-10-06 / 07 (rounds 1-3): Blendity

The editor itself: Blender's mesh model and Edit Mode behind a Unity-style editor; UV unwrapping, materials and textures; a deferred rasterizer and a path tracer; Blender's bundled libraries (Embree, OpenImageDenoise, OpenSubdiv, OpenColorIO, Manifold, Jolt and others), each optional; SIMD kernels; the Learn tab; stress tests.
