# 0501: Skinning on the graphics card (Pass 26, rounds 518-523)

- **Status:** approved by the user 2026-11-15 (relayed by the hub: "Approve, Unity normals"): normals skinned as
  Unity does, and the CPU views use the same rule (ADR 0146); round 518 in progress
- **Requirements:** Q-04 (interactive editor), Q-06 (responsiveness)
- **Decisions:** ADR 0146 (new); builds on ADR 0034, 0036, 0106, 0108, 0112, 0124, 0126
- **Model:** main session (Opus) for rounds 518-519 (the data model and the card's pass); implementer for 520-521;
  test-engineer and code-reviewer on every round
- **PR:** (one per round, stacked on main)

## Goal
Posing, scrubbing and playing a rigged or humanoid character stays at 60 frames a second in a view on the card: the
card moves the vertices, the CPU sends only the bones (about 20 KB a frame for 292 bones, where it sends 5-6 MB of
meshes today). A machine without a card, the dependency-free build and CI get the same picture from the CPU.

## Where the time goes now (why this pass)
Pass 22 (rounds 468-474) cut the posed frame from 48-123 ms to 10-98 ms, but the parts it left are all "the CPU makes
the posed mesh, then sends it": round 474's own tables (docs/PERFORMANCE.md, "Pass 22 in one table") for the chest's
FK drag alone (35.3 ms) give mesh uploads 11.1 ms, moving the render meshes in place (positions, normals, tangents)
8.3 ms, render meshes built whole (the toon outline shells) 7.9 ms; a Puppet drag (52.6 ms) 17.6 / 11.1 / 10.0 ms; a
scrub 17.6 / 8.6 / 8.0 ms; Play with the springs 17.0 / 8.2 / 7.5 ms. Round 511's verify (docs/perf/verify-511.md)
still lists "posing and scrubbing an avatar, 17-89 ms" as Pass 22's open items. Every one of those parts goes away
when the card skins: what stays on the CPU is the pose (constraints, IK, the full-body solver: under 2 ms) and a
matrix per bone.

## What Blender has to port (ADR 0112's 1.3x bar)
- Blender skins on the CPU (`blenkernel/intern/armature_deform.cc`, `modifiers/intern/MOD_armature.cc`); its draw
  module has no GPU armature deform. Blendity already has the same linear and dual-quaternion math (`skin.cpp`). No
  code to port.
- The pattern worth taking is Blender's **GPU subdivision** (`blenkernel/intern/subdiv_modifier.cc`
  `BKE_subsurf_modifier_can_do_gpu_subdiv`, `mesh_wrapper.cc` `BKE_mesh_wrapper_ensure_subdivision`): the card
  evaluates the modifier only when it is the last in the stack, and the CPU result is made lazily, the first time
  something asks for it. ADR 0146 takes that rule, not its code.

## Scope
- In:
  - the skin's render data (per render vertex, up to 8 bones and their weights, normalised once) and a per-frame
    palette (a matrix, a dual quaternion and a rigid flag per bound bone), built from Pass 22's binding (ADR 0124);
  - one plain-C++ kernel that skins render vertices from them - the reference, and the CPU views' path;
  - a compute pass in the card's raster (`VulkanRaster`) running the same kernel in GLSL once a frame for every
    skinned instance, into a posed vertex buffer every pass and every view of that frame reads;
  - the CPU positions for what still needs them, made lazily and never read back from the card;
  - the toon outline shells and shape keys on the same path;
  - the stress rows, the tolerance tests, the timed check and the write-up.
- Out (not in this pass):
  - blend shapes computed on the card, unless round 521 measures them over ADR 0112's bar;
  - the path-traced view (829 ms a frame while posing: it re-traces the posed body by design), Realtime GI with
    Lighting Updates Automatic (70 ms: it re-lights the posed body), AutoPhysics (58.5 ms of a 50 ms budget) and the
    first Puppet drag's body model (103 ms of 100): none is skinning; each must not get slower, and each is named in
    the write-up as left;
  - the Scene and Game views' shared instance buffer (verify-511: "each view's frame writes the other's rows") -
    round 522 measures whether it holds a two-view row over 16.7 ms and names it if so; its fix is the Speed stream's;
  - Edit Mode, Sculpt and Weight Paint on a skinned mesh (they stay on the CPU path for that mesh, as today).

## Every device Blendity has, and what each does with a skinned mesh

| device (`src/render`) | after this pass |
|---|---|
| `VulkanRaster` (`gpu_raster.cpp`): Scene, Game, Camera Preview and XR views on the card | skins on the card (compute pass), round 519 |
| CPU raster (`raster.cpp`): the views without a card, the dependency-free build, CI | skins render vertices with the reference kernel (same math, same normals rule), round 518 |
| CPU path tracer (`pathtracer.cpp`), F12 and bakes | the lazy CPU positions, on the render's gather worker (ADR 0106), round 520 |
| Vulkan path tracer (`gpu_device.cpp`) and OptiX (`optix_device.cpp`) | the same flattened scene as the CPU path tracer: the lazy CPU positions, round 520 |
| MoonRay (`moonray_export.cpp`) | the lazy CPU positions in its export, round 520 |
| Realtime GI / voxel GI (`realtime_lightmap.cpp`, `voxel_gi_gpu.cpp`, gathered in `lighting.cpp`) and the lightmapper | the lazy CPU positions, round 520 |

A card without what the pass needs (storage buffers written from compute and read as vertices), a skin over the card's
limits, or a stack where the skin is not last: that mesh stays on the CPU path and the view says why (ADR 0108's
per-view list), the rest of the view stays on the card.

## Rounds

### Round 518 (this spec): the baseline, the render data and the CPU reference
- A timed window from the hub (about 30 minutes): `blendity_stress --only posing` and `--only animation` on main,
  `tools/bench_world.py --posing` on the owner's avatar scratch copies if they are still there; saved as the pass's
  baseline (Pass 25 changed the views since round 474).
- `src/scene/skin_render.{h,cpp}` (new): `SkinRenderData` (per render vertex: bone slots and weights, from
  `SkinBinding` and the render mesh's render-vertex -> mesh-vertex map), `SkinPalette` (from the final pose, after
  the one solve of ADR 0126), `skin_render_vertices(data, palette, rest, out, from, to)` - positions, normals and
  tangents by the rule ADR 0146 settles.
- The CPU views take it: an eligible skinned mesh keeps its rest render mesh and writes the posed render vertices
  in place each posed frame - no recompute of normals, no corner split that flips with the pose, no MikkTSpace
  rebuild (ADR 0124's "built whole" cause goes).
- Profiler / `pose_counts()`: skins on the card, skins on the CPU, the reason a skin is not on the card.

### Round 519 (spec 0502): the card's skin pass
- `Raster::can_skin()`, `add_skin(rest_mesh, weights) -> mesh id` (its own posed vertex buffer, the rest mesh's
  indices), `set_skin_palettes(...)` (a host-visible ring of three, as `render_async` keeps two frames in flight),
  `remove_skin`. A compute pipeline in the shader cache (`warm_shaders`), one dispatch per skinned instance before the
  first pass, one barrier (shader write -> vertex read). Every pass (g-buffer, shadows, see-through, outlines, ids)
  draws the posed buffer unchanged; both views of a frame share it (no second skin, as ADR 0124 for the CPU).
- `gpu_views.cpp`: a posed skinned mesh sends its palette, not its mesh; its bounds come from per-bone rest boxes
  moved by the palette (conservative, for culling and the shadow fit).
- The Learn tab's posing lesson: what runs on the card, what still needs the CPU, the Profiler lines.

### Round 520 (spec 0503): what still needs CPU positions, without a stall
- `GameObject::posed_mesh_cpu()`: the posed mesh made on demand from the kept pose with the reference kernel (or
  `SkinModifier::modify` where the stack needs the whole mesh), kept by the skin's key (ADR 0124) so a second ask in
  the same pose costs nothing. Never a read-back from the card (it would wait on the frame in flight).
- Callers: picking (`scene_pick.cpp`, `raycast.cpp`: only meshes whose bone-box bounds the ray hits), surface and
  object snapping (`object_snap.cpp`), physics mesh colliders (`physics_jolt.cpp`, at the physics step), export of a
  posed mesh (`export.cpp`; a skinned export still writes rest + weights), the path tracers, MoonRay, the lightmapper
  and Realtime GI gathers (on their workers), F12's snapshot (`render_view.cpp`), Live Link (`livelink.cpp`), the
  modes that edit the mesh. The modifiers after an armature keep the whole stack on the CPU.
- A test per caller that the posed shape is what it sees (a click on the raised arm picks it; a click where the arm
  was at rest does not).

### Round 521 (spec 0504): outline shells and shape keys
- The toon outline shell drawn from the skinned buffer (pushed along the skinned normal by the instance's width), no
  longer built whole each posed frame.
- Shape keys stay CPU-applied to the rest vertices before the skin (as today), and only when their weights change do
  the changed vertex ranges go up (`update_mesh_ranges`); a blink or a viseme while posing is measured. Blend shapes
  on the card only if a row improves 1.3x or more (ADR 0112).

### Round 522 (spec 0505): the timed check
- A timed window from the hub (about 90 minutes): every row of the target table below, the stand-in alone and in its
  world, the owner's avatar; the settings table; before (round 518's baseline) and after. What still misses is fixed
  under ADR 0112's bar or named with its cause.

### Round 523 (spec 0506): the write-up
- docs/PERFORMANCE.md "Pass 26" section (changelog fragment sections), the posing lesson's numbers, README,
  USABILITY, ARCHITECTURE rows, the books (as Pass 25's round 512 did).

## The timed rows that must come under budget
Round 474's numbers (a window on the card, ms a frame; stand-in = `blendity_stress --only posing`, 292 bones,
15 skinned meshes, about 140,000 triangles). Target: 16.7 ms a frame.

| row | alone, 474 | in the world, 474 | must after this pass |
|---|---|---|---|
| the Rotate gizmo on the chest (FK) | 35.3 | 39.7 | alone and in the world |
| the Move gizmo on the left hand's IK target | 10.1 | 17.9 | in the world |
| the Move gizmo on the left elbow's pole | 10.9 | 19.6 | in the world |
| a Puppet handle (the left hand, AutoPosing on) | 52.6 | 62.0 | alone and in the world |
| G on the left hand's IK target | 12.9 | 24.7 | in the world |
| R on the chest (the Blender keymap) | 44.1 | 61.0 | alone and in the world |
| a scrub, Scene view only | 46.4 | 59.9 | alone and in the world |
| the clip playing, Scene view only | 28.1 | 36.0 | alone and in the world |
| Play with the springs | 44.5 | 69.8 | alone and in the world |
| a scrub, Scene + Game view | 70.0 | 90.9 | alone; in the world unless the shared instance buffer holds it (named) |
| the clip playing, Scene + Game view | 36.2 | 39.4 | alone and in the world |

And in the world, the IK-target drag (or the clip) with: Record on, Preview off, the Game view beside, Other Views
Follow Drags, trails / onion skins / zoetrope (the drag; the scrub is named if it misses), Centre of Mass, the
Avatar / Inspector / Rigging windows, Realtime GI Manual, Live Link, surface snapping, an F12 Live Preview: each under
16.7 ms. The once-only rows keep their 100 ms and the operations their 50 ms (the two misses above stay out of scope).
Not targeted, must not get slower: the path-traced view, Realtime GI Automatic.

The CPU views (no card) are not held to 16.7 ms; they must be no slower than round 518's baseline, and a CPU posed
frame that no longer builds render meshes whole should be faster.

## Likely files and ownership
No other pass runs now (the hub, 2026-11-15); the rounds are one stack, one after the other, so a file is held by the
round that edits it until that round is published.

| file | rounds | what |
|---|---|---|
| `src/scene/skin_render.{h,cpp}` (new) | 518 | render data, palette, reference kernel |
| `src/scene/skin.cpp`, `src/scene/scene.h` (SkinModifier, GameObject) | 518, 520 | binding export, `posed_mesh_cpu` |
| `src/scene/mesh.{h,cpp}` (render mesh, `update_in_place`) | 518 | the rest render mesh kept, the posed vertices written |
| `src/scene/pose_counts.{h,cpp}`, `src/editor/editor.cpp` (Profiler lines, outline shells near 3477) | 518, 521 | counters; shells |
| `src/render/gpu_raster.{h,cpp}` | 519, 521 | the skin pass, palettes, the shell push |
| `src/editor/gpu_views.cpp` | 519, 521 | palettes in place of uploads, bounds |
| `src/editor/scene_pick.cpp`, `object_snap.cpp`, `src/scene/raycast.cpp`, `physics_jolt.cpp`, `export.cpp`, `src/editor/render_view.cpp`, `lighting.cpp`, `livelink.cpp` | 520 | the lazy CPU positions |
| `src/editor/lessons.cpp` | 519, 523 | the posing lesson |
| `tests/test_animation.cpp` | 518, 520, 521 | reference kernel, CPU consumers, shells, shape keys |
| `tests/test_gpu.cpp` | 519, 521 | the card against the CPU |
| `stress/stress_posing.inc`, `stress/results/baseline.md` | 518, 522 | rows, baseline |
| `changelog.d/518..523-*.md`, `docs/tasks/0501..0506`, `docs/decisions/0146-*` | each | fragments, specs |

## Acceptance criteria
1. **Exactness, the reference against the deform (CPU, every build):** for linear and dual-quaternion skins, 1 to 8
   influences, weights that sum to under or over 1, a vertex with no bone, NaN / infinite weights, an owner scaled
   unevenly, a mirrored (negative-scale) bone, 292 bones: `skin_render_vertices`' positions equal
   `SkinModifier::modify`'s within 1e-5 of the mesh's bounding diagonal; a vertex with no bone stays at rest; a
   non-finite result keeps the rest position, as the deform does.
2. **Exactness, the card against the CPU (`test_gpu`, where a card is present; skipped with the reason in CI):** the
   card's posed buffer (read back in the test only) equals the reference kernel's within 1e-5 of the diagonal for
   positions and 1e-4 for normals and tangents (unit length, dot >= 1 - 1e-4); a posed stand-in drawn by the card and
   by the CPU agree within ADR 0036's 1 / 255 tolerance (ADR 0108's listed approximations aside).
3. **Normals:** by ADR 0146's rule; the CPU and the card give the same normals for the same pose; a pose then back to
   rest gives the rest normals again (within 1e-6) on both.
4. **Nothing posed goes up as a mesh:** on the card, a posed frame of the stand-in sends 0 mesh uploads and 0 render
   meshes built whole (Profiler counts), only palettes; a mesh whose stack has a modifier after the armature, or a
   card without the pass, still draws correctly from the CPU with its reason listed.
5. **CPU positions without a stall:** picking, snapping, a mesh collider, a posed export, F12 and the path tracers see
   the posed shape (a test each); none reads from the card; asked twice in one pose, the second costs no skin.
6. **Outlines and shape keys:** a posed toon character's outline follows the body on the card with 0 shells built a
   frame; changing a shape key's weight while posed moves only that key's vertex ranges up.
7. **The timed rows** above meet 16.7 ms on the owner's machine in the hub's window, or the write-up names each one
   that does not with the part of its frame that holds it.
8. The dependency-free build and CI build and pass; the full unit suite passes on Windows and Linux; the stress
   sections `posing`, `animation`, `editor`, `editor_render` show no new problems; `tools/test_coverage_map.py
   --check` finds nothing new untested.

## Tests and checks to run
- Unit: `--group animation`, `--group gpu`, `--group render`, `--group editor` (TEST_GROUPS) per round; the full suite
  on Windows and Linux before each publish; the card's tests in a graphics-card window from the hub.
- Stress: `--only posing | animation | editor | editor_render`; timed rows only in the hub's windows.

## Risks and edge cases
- A mesh drawn by two characters (one mesh asset, two armatures): the posed buffer belongs to the skinned instance,
  not the mesh.
- A character with more bones than one dispatch's palette, or more than 65,535 bones (indices are 16-bit): per-mesh
  CPU fallback with its reason.
- The frame in flight: palettes in a ring of three, never written while the card reads them.
- Undo / redo of a pose, Record, Preview, Play and leaving Play: the palette follows the pose of the frame drawn
  (ADR 0126's "drawn where the solver puts it").
- The rest-position switch (`Armature::rest_position`), a skin turned off, a mesh re-bound (automatic weights): the
  render data rebuilt, never stale (keyed as ADR 0124's binding).
- Device lost / the card's memory full: the skin goes to the CPU path and says so.
- A culled character: the bone-box bounds must contain every posed vertex (a test with stretched bones).

## Documentation to update
- `changelog.d/<round>-<slug>.md` per round (with README, ARCHITECTURE, USABILITY, PERFORMANCE sections; ADR 0123).
- ADR 0146 (new). The Learn tab's posing lesson (rounds 519 and 523).
