# Task 0431: SH deringing and Light Probe Groups as objects (round 455)

- **Status:** done
- **Requirements:** R-03 (rendering), R-08 (VRChat worlds and avatars)
- **Decisions:** ADR 0119 (new; amends ADR 0116's consequences, builds on ADR 0012)
- **Model:** implementer (Sonnet), reviewed by code-reviewer (Opus)
- **PR:** (link when opened)

## Goal
Baked probes can be windowed (Hanning or Lanczos sigma, as in Sloan's "Stupid SH Tricks") so a bright lamp no longer
leaves a dark ring and a light spot on the far side of probe-lit objects. Light Probe Groups become scene objects: a
component holding probe positions, drawn in the Scene view, moved with the gizmo, made by Unity import. When a scene has
one, Generate Lighting bakes at the groups' probes and fills the volume from their tetrahedra, as Unity does.

## Found (file:line with round 454's changes in)
- `ProbeSH::eval` adds all nine coefficients and clamps at 0 (`src/render/probe_sh.h:42-48`); no window (ADR 0116
  Consequences, `docs/decisions/0116-l2-sh-probes.md:33`). For one lamp the stored L2 irradiance is
  (1/4)(3/8 + 2t + 15/8 t^2), t = n.l: 1.06 at the peak, -0.040 at 122 degrees, +0.0625 straight behind.
- CPU: `bounce_of` evaluates `sh[i].eval(n)` per corner (`src/render/probe_volume.cpp:212-216`). Card: `probe_cube` adds
  the seven packed rows (`src/render/gpu_raster.cpp:1914-1934`) that `probe_pack` writes from `d.sh`
  (`src/editor/gpu_views.cpp:278-319`; `pv[3].w` unused); upload key `gpu_views.cpp:1356-1361`. The overlay dots evaluate
  the SH too (`src/editor/scene_overlays.cpp:376-386`).
- Blendity's own probes are always the automatic grid: `probe_place` over `probe_volume_box` (`src/editor/lighting.cpp:393`,
  `:461-503`, `:683-732`); nothing bakes without a `ProbeVolume` component.
- Tetrahedra are used only for Unity's probes: `unity_probe_volume` grids the points, each grid probe takes its
  tetrahedron's four by barycentric weight, or the nearest four outside the hull (`src/render/unity_lightmaps.cpp:714-775`,
  `src/render/tetrahedra.h`).
- Unity's LightProbeGroup (class 220) falls through to `add_engine` as an Engine Component (`src/scene/unity_scene.cpp:874-890`);
  the import only counts them in a note (`src/editor/unity_scene_ui.cpp:395-397`).
- Edit-in-the-view model: `Component::control_point_count / control_point / set_control_point` (`src/scene/scene.h:184-188`);
  `GameObject::point_source` accepts Curve and Lattice only (`src/scene/scene.cpp:3266-3270`); drawing
  (`scene_overlays.cpp:561-571`, `:638-642`), picking (`src/editor/scene_pick.cpp:20-45`), the gizmo
  (`src/editor/scene_view.cpp:1042`, `:1068`), the `points` command (`src/editor/editor.cpp:5331-5400`).
- Out of date: `lightmap_scene_key` (`lighting.cpp:44-88`) is saved with every bake; `lighting_out_of_date` re-checks only
  when `scene_render_hash` moves (`lighting.cpp:103-115`), which reads components by `render_role`
  (`src/editor/render_view.cpp:793-812`).
- Probe settings live in `LightingSettings` (`scene.h:1947-1953`), Inspector rows at `scene.cpp:390-401`.

## Scope
- In: **the window applied at evaluation, not at bake.** Band 0 kept; bands 1 and 2 multiplied by h1, h2. Hanning
  h_l = (1 + cos(pi l / w)) / 2; Lanczos h_l = sin(pi l / w) / (pi l / w). `ProbeFrame::sh_window` (three floats, carried by value into a render job, not
  saved; the Editor holds the current one in `probe_window_`) set beside `probe_frame_` (`render_view.cpp:1226`), applied in `bounce_of` and the dots; `probe_pack` multiplies
  the packed SH by the same factors (the card's shader unchanged, one set of numbers). The window joins the upload key and
  the view cache key.
- In: `LightingSettings::probe_dering` (0 Off, 1 Hanning, 2 Lanczos; default **Off** - Unity's Remove Ringing default,
  pictures unchanged) and `probe_dering_width` (w, default 4, range 3-16); Inspector rows with help text, console fields
  (`set Lighting.ProbeRemoveRinging 2`, `set Lighting.ProbeRingingWidth 4`). View settings: no rebake, not in
  `lightmap_scene_key`; probe files unchanged.
- In: Unity's probes (`lighting_from_unity_`) shown as Unity baked them, no window (Unity applied its own Remove Ringing).
- In: `LightProbeGroupComponent` (kName "LightProbeGroup", Rendering): `std::vector<Vec3> positions` in object space
  (Unity's m_SourcePositions); a new group starts with Unity's default 8 probes at a 1 m cube's corners. Implements the
  control-point interface; `point_source` accepts it via a new `RenderRole::ProbeGroup`. `reflect` shows a Probes count;
  files, undo and hashes carry the positions as one text field ("x y z; ...", 9 significant digits) when `serializing()`
  or `hashing()`.
- In: the Scene view (Points overlay layer): every active group's probes as small yellow dots (cap 50,000); the active
  group's probes clickable (orange when selected), moved with the gizmo, with undo; clicking an unselected group's dot
  selects the group; `points add|delete|select|move <group> ...` works for groups; Inspector buttons Add Probe, Delete
  Selected, Select All; GameObject > Light > Light Probe Group and `create Light Probe Group`.
- In: Unity import: class 220 becomes a `LightProbeGroupComponent` from m_SourcePositions (the document stays as a read-only Engine Component for the export; a scene file saved before this round, holding only the Engine Component, gets the component on load); the note
  says "N light probe group(s) came in as objects".
- In: the bake: with one or more enabled groups, `probe_bake` runs over all groups' world positions (no `probe_validate`:
  Unity doesn't move hand-placed probes), then `probe_volume_from_points(points, sh, cubes, out)` (the second half of
  `unity_probe_volume`, moved into `probe_volume.cpp` and shared) builds the grid; its sky fields come from the bake.
  Groups replace the automatic grid (a `ProbeVolume` component is then ignored, with a log line).
- In: `lightmap_scene_key` mixes in the groups' world positions **only when a group exists** (old bakes keep their key);
  `object_render_hash` hashes the component so moving a probe re-checks out-of-date.
- Out: windowing VRC Light Volumes, the live cubes and the environment's SH; Unity's per-group m_Dering; writing groups
  back to Unity (export / Live Link) - carried to the open list; tetrahedron edges; optimised windows (Sloan 2017);
  mixing groups with an automatic grid in one bake.

## Likely files
- `src/render/probe_sh.h` (`window_factors(kind, w)`), `src/render/probe_volume.h/.cpp` (`sh_window`,
  `probe_volume_from_points`), `src/render/unity_lightmaps.cpp` (calls the shared function).
- `src/editor/gpu_views.cpp` (`probe_pack`, upload key), `src/editor/render_view.cpp` (window, cache key, hash role).
- `src/scene/scene.h/.cpp` (component, role, `point_source`, settings, reflect, registry), `src/scene/unity_scene.cpp`.
- `src/editor/lighting.cpp`, `scene_overlays.cpp`, `scene_pick.cpp`, `editor.cpp` (menu, `known_create_kind`,
  `points_command`), `unity_scene_ui.cpp`, `lessons.cpp`.
- `tests/test_lighting.cpp` (`tests_round455`), `tests/test_gpu.cpp` (`tests_round455_gpu`), `tests/test_editor.cpp` (`tests_round455`).

## Acceptance criteria
1. Factors: Hanning w=4 (1, 0.854, 0.5); Lanczos w=4 (1, 0.900, 0.637); Off (1, 1, 1); band 0 never scaled.
2. Ringing: one lamp of 20 along +Y over a uniform sky of 1 (irradiance / pi), unclamped: the darkest value rises from
   0.21 (Off) to >= 0.5 (Hanning 0.55, Lanczos 0.62); straight behind (truth 1.0) Off 2.25, Lanczos 0.98 +- 0.02; the peak
   drop printed. (Numbers from the algebra: confirm in the first test and correct the spec if they differ.)
3. Off is bit-identical to round 454: the round 442 and 453 tests pass unchanged.
4. CPU vs card, a probe-lit sphere by a bright lamp, Lanczos: mean <= 0.1/255, >= 99.9 % within 3; Off <-> Lanczos
   redraws both views without a rebake.
5. A volume from Unity's lighting file keeps its picture with the window on.
6. A group's positions survive save/load, undo and `clone()` exactly; the Inspector shows a count, not one row a probe.
7. A Unity LightProbeGroup (3 m_SourcePositions under a moved, scaled Transform) imports as a component with those
   local positions and the right world positions, its Unity document kept as a read-only Engine Component, so an export writes it back unchanged (probes moved in Blendity are not written back; a log line says so).
8. A group of 8 probes, a red emissive wall, no Probe Volume: Generate Lighting bakes; a grid probe on a placed probe holds
   that probe's SH and cube exactly (`probe_volume_from_points`, checked directly), and `sample()` at each placed probe
   is its SH within 1e-3 (the views' trilinear read); moving the group 2 m and baking again moves the red light with it.
9. `points move Group 0 x y z` or a real gizmo drag marks the lighting out of date; undo makes it current; a scene with
   no group keeps its `lightmap_scene_key` (a round 454 bake still loads as current).
10. Real events (`step_frame_headless`): select the group, click a probe's dot, drag the gizmo's X arrow - only that probe
    moves, along local X; Ctrl+Z restores; Add Probe / Delete Selected change the count with undo.
11. Existing tests pass; `py tools/test_coverage_map.py --check` clean.

## Tests and checks to run
- `--group lighting`, `--group gpu` (GPU in the libraries clone), `--group editor`, both builds, then the chain.
- `blendity_stress --only lightmap_quality` (window Off: no change expected).

## Risks and edge cases
- Windowing trades sharpness for less ringing; Hanning w=3 makes the back worse (-0.047), so w >= 3, default 4; ADR
  0119 records the numbers.
- Group probes inside walls bake dark (as Unity); duplicate / coplanar points go to `Tetrahedra`'s nudge; > 4,000
  points (`Tetrahedra::kMaxPoints`) fall back to the nearest four, said in the log.
- `object_render_hash` must pick up the new role or a drag never marks the bake out of date; the key must not change for
  scenes without groups.
- After Use Unity's Lightmaps, moving a group changes nothing until Generate Lighting (the Inspector says so).
- The F12 worker reads `probes_` by pointer, so the window travels in `ProbeFrame` (copied by value into the job); the main
  thread only writes `probe_window_`.

## Documentation to update
- CHANGELOG (round 455, CHECKS_455), README (probes paragraph), ARCHITECTURE (`LightProbeGroupComponent`,
  `probe_volume_from_points`, the SH window), USABILITY (moving probes needed Unity: fixed), ADR 0119 (new), a
  "Superseded in part (round 455)" note in ADR 0116, a round 455 section in the Learn lesson (`lessons.cpp:705`): Try-it a
  bright Point Light beside a probe-lit sphere, Remove Ringing Off then Lanczos; question: why does windowing leave the
  sphere's average brightness unchanged?
