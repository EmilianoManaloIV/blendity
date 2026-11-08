# ADR 0119: Windowed probe SH, and Light Probe Groups as objects

- **Status:** accepted (round 455, task 0431)
- **Builds on:** ADR 0116 (L2 SH probes), ADR 0012 (probe volumes); amends ADR 0116's consequences
- **Requirements:** R-03 (rendering), R-08 (VRChat worlds and avatars)

## Context
ADR 0116 stores a probe's light as L2 spherical harmonics, convolved with the cosine lobe, and says "L2 rings: no
windowing". For one lamp the stored irradiance / pi is (1/4)(3/8 + 2t + 15/8 t^2), t = n.l: 1.06 at the peak, -0.040 at
122 degrees, +0.0625 straight behind. A bright lamp therefore leaves a dark ring and a light spot on the far side of a
probe-lit object (a lamp of 20 over a sky of 1: the sphere is 0.21 at its darkest and 2.25 behind, where the truth is
1.0). Unity's Light Probe Groups have a Remove Ringing option (Sloan, "Stupid Spherical Harmonics Tricks": a window on
the bands) that is off by default.

Light Probe Groups were not objects in Blendity: their probes arrived with Unity's lighting file, and moving a probe
needed Unity. Blendity's own probes were always the automatic grid over a `ProbeVolume`.

## Decision
1. **The window is applied at evaluation, not at bake.** Band 0 is kept; bands 1 and 2 are multiplied by h1 and h2 of
   `window_factors(kind, w)`: Hanning h_l = (1 + cos(pi l / w)) / 2, Lanczos h_l = sin(pi l / w) / (pi l / w). At w = 4:
   Hanning 1, 0.854, 0.5; Lanczos 1, 0.900, 0.637. `ProbeFrame::sh_window` (three floats, not saved, carried by value into a render job so a
   worker never reads what the main thread is rewriting; the Editor keeps the current one in `probe_window_`) is set every
   frame from the settings and passed to `ProbeVolumeData::sample` and `ProbeSH::eval`; the overlay dots read `probe_window_`. `probe_pack`
   multiplies the packed SH rows by the same factors, so the card's shader is unchanged and CPU and card share one set
   of numbers. The window joins the card's upload key and the view cache key. Because it is a view setting it needs no
   rebake, is not part of `lightmap_scene_key`, and the probe files are unchanged.
2. **Settings:** `LightingSettings::probe_dering` (0 Off, 1 Hanning, 2 Lanczos) and `probe_dering_width` (w, range
   3-16, default 4), as `set Lighting.ProbeRemoveRinging` and `Lighting.ProbeRingingWidth`. Default **Off** (Unity's
   default; every picture unchanged). With one lamp of 20 over a uniform sky of 1 (unclamped, algebra confirmed by
   `tests_round455`): the darkest value 0.208 Off, 0.552 Hanning, 0.615 Lanczos; straight behind (truth 1.0) 2.25 Off,
   0.976 Lanczos; the lamp's peak (truth 21) 22.25 Off, 17.66 Hanning, 18.98 Lanczos - the price is a lamp a seventh
   weaker. Hanning at w = 3 makes the back worse (-0.047), hence w >= 3 and the default 4.
3. **Unity's probes are unwindowed.** A volume from Unity's lighting file (`lighting_from_unity_`) is read with the
   window 1, 1, 1: Unity applied its own Remove Ringing when it baked, and a second window would soften what the
   user compares against Unity's picture.
4. **`LightProbeGroupComponent` is a serialized component** (kName "LightProbeGroup", Rendering, `RenderRole::ProbeGroup`)
   holding `std::vector<Vec3> positions` in object space (Unity's m_SourcePositions). A new group has Unity's default
   eight probes at a 1 m cube's corners. It implements the control-point interface, so the Scene view that edits Curves
   and Lattices draws, picks and moves its probes without a second path (`GameObject::point_source` accepts the new
   role). Files, undo and hashes carry the positions as **one text field** ("x y z; x y z; ...", the shortest text that
   reads back the same float) read in `reflect` only when `serializing()`; the Inspector shows a count and buttons, not a
   row a probe (a group of 10,000 would be 30,000 rows). `object_render_hash` hashes the positions straight from the
   vector, so moving a probe marks the lighting out of date and undo makes it current.
5. **Groups replace the automatic grid in the bake.** With one or more enabled groups in active objects, `probe_bake`
   runs over all their world positions (no `probe_validate`: Unity does not move a hand-placed probe), then
   `probe_volume_from_points(points, sh, cubes, out)` - the second half of `unity_probe_volume`, moved into
   `probe_volume.cpp` and shared (`KdTree` moved to `tetrahedra.h` with it) - places a grid round the points and gives
   each grid probe its tetrahedron's four by barycentric weights, or the nearest four outside the hull. The sky fields
   (`sky_in_sh`, the baked sky) come from the bake. A `ProbeVolume` component is then ignored, with a log line.
6. **The scene key only changes when a group exists.** `lightmap_scene_key` mixes in the groups' world positions only if
   there are some; a scene without one hashes exactly as before, so a round 454 bake still loads as current.
7. **Unity import:** a LightProbeGroup (class 220) becomes the component; its document is kept as a read-only Engine
   Component (as the avatar components' are), and a scene file saved before this round, holding only the Engine
   Component, gets the component on load (parsed from the kept text). The import note says "N light probe group(s) came
   in as objects". Probes are not written back to Unity.

## Consequences
- Windowing trades sharpness for less ringing; a lamp's peak on a probe-lit surface drops by 15 % (Lanczos) to 20 %
  (Hanning) at w = 4. Off is the default, so nothing changes until the user asks.
- The window costs nothing at bake and nothing on the card (the rows are multiplied once at upload); changing it
  re-uploads the probe buffer once.
- Moving a probe changes nothing in the Scene view until Generate Lighting runs again (after Use Unity's Lightmaps too:
  the Inspector says so); the out-of-date flag shows it.
- Group probes inside a wall bake dark (as in Unity). Duplicate or coplanar points go to `Tetrahedra`'s nudge; past its
  4,000-point cap the grid takes the nearest four, with no tetrahedra (said in the log).
- Export writes a group's Unity document back unchanged; probes moved in Blendity are not written back (the export logs
  a line when a group's probes differ from the imported ones). Writing them back is on the open list.
- A bake with groups has no automatic grid: a scene with a group and geometry far from it is lit by the probes that
  exist (the nearest four outside the hull).

## Alternatives
- **Window at bake time:** the window would be baked into the SH, so changing it (or Off) needs a rebake and old probe
  files carry a choice nobody can undo; the sky relight and the Light Volume bake would need to know. Rejected: a view
  setting costs nothing at the bake and is undone by a slider.
- **Per-group dering (Unity's m_Dering):** one window for all probes is one number set on the card; a per-probe window
  would add a flag per probe to the buffer and a branch to the shader. The Unity import ignores m_Dering (listed as
  not built).
- **Mixing groups with the automatic grid:** grid probes inside a group's hull would have two sources and need a blend
  rule; Unity has no automatic grid at all, so groups win outright. A scene that wants both can use a Probe Volume
  alone, or groups alone.
