# Blendity against Unity and the VRChat SDK: snapshots across the board (round 404, 2026-11-03)

**Asked (2026-11-03):** "Do a full comparison between Blendity and the Unity VCC SDK. Implement any attributes that are
present in Unity but not present in Blendity. Compare snapshots across the board to get accurate readings using Unity
VCC's Shinano and SideRail project. Lighting, Emissions, and other scene properties should be nearly the same and almost
indistinguishable."

Rounds 401-403 implemented the attributes (the scene's look, lilToon's lighting model, the avatar's and the world's
VRChat components, every kept Unity component as data). This round measures: the same stations rendered by Unity
2022.3.22f1 (the VCC projects' copies, VRChat SDK 3.10.6) and by Blendity, scored by `tools/image_compare.py`. The
user's packages stay local: the numbers are here, the pictures in the repo root's `comparison-screens/` (untracked).

## Method

- **The harness** (`tools/unity_compare.py`, `bridges/unity/SdkCheck/BlendityCompare.cs`): Unity opens the scene (or the
  prefab in the studio rig) in batch mode and renders each station through its Main Camera with its post-processing
  (PNG, sRGB: what players see) and without it (EXR, linear); Blendity's headless mode opens the same scene from the
  Blendity project it was imported into, applies Unity's lightmaps (`unity lightmaps wait`), moves the Main Camera to the
  station, renders with the CPU rasterizer (`render wait`) and saves the PNG. Each pair is scored: mean absolute error
  per channel (0-255), PSNR, SSIM (8 x 8 windows), the correlation of a 16 x 9 luminance grid; the target (the plan's
  "almost indistinguishable"): MAE ≤ 4, SSIM ≥ 0.90, correlation ≥ 0.95.
- **The rig's sun on Blendity's side** is aimed along Unity's `Euler(40, 200, 0)` forward, (-0.262, -0.643, -0.720):
  Blendity keeps Unity's axes (round 402).
- **Stations.** SideRail (world): the spawn station (round 401's view), the spawn turned round, the floor under it
  (pitched 50°), the walkway ahead. Shinano (avatar): front, back, the face (FOV 25), the hands and skirt; the studio rig
  of round 399 (one white sun at Euler(40, 200, 0), a flat ambient (0.45, 0.45, 0.5), a background (0.22, 0.22, 0.24),
  FOV 35) on both sides, no post-processing.
- **Unity's scene as it holds it** (scene_look.json): fog Exponential, density 0.02; ambient Flat; skybox UnlitBlack;
  Main Camera FOV 50, HDR; post-processing on; Linear colour space; 71 lights, 4 reflection probes, 365 renderers.

## Results

### Shinano (avatar) - every station passes

| station | MAE | SSIM | grid corr | target | Blendity (open + render) |
|---|---|---|---|---|---|
| front | 1.00 | 0.987 | 1.000 | pass | 9.5 s |
| back | 1.17 | 0.977 | 1.000 | pass | 9.7 s |
| face | 2.61 | 0.944 | 1.000 | pass | 9.9 s |
| hands | 0.91 | 0.989 | 1.000 | pass | 9.9 s |

Unity's side: 0.9 s to open, 0.4 s the first render, 0.1 s the next (timings.json). What remains is at the edges: the
hair cards' alpha test and the eyelashes (sorting), the face station's 2.6 mostly the eyes' highlights (lilToon's
reflection block on the eye material with its own textures, which Live Link doesn't carry - round 405).

### SideRail (world) - round 404's state, then round 405's fixes

| station | MAE (404 → 405) | SSIM (404 → 405) | grid corr (404 → 405) | target after 405 |
|---|---|---|---|---|
| spawn | 7.03 → **3.46** | 0.931 → 0.921 | 0.989 → 0.989 | pass |
| spawn_back | 2.40 → **1.61** | 0.928 → 0.806 | 0.751 → 0.693 | pass (a nearly black picture: the MAE and the means - 1.63 against 0.37 - decide) |
| floor | 4.71 → **2.85** | 0.937 → 0.870 | 0.999 → 0.998 | miss (SSIM fell under the darker toe; not pinned down) |
| ahead | 6.25 → **3.00** | 0.928 → 0.949 | 0.987 → 0.984 | pass |

Round 405 (Unity's own ACES curve, Unity's BRDF weight on the probes' reflection, the glass / candle / mirror shaders read)
cuts the error at every station - by half on the three lit ones: three of four pass; the floor passes on error (2.85)
and misses on SSIM (0.937 → 0.870): the darker toe of Unity's curve lowers a dark frame's contrast, and the two
rasterizers' fine concrete texture agrees less there - the cause isn't pinned down. The spawn turned round, nearly
black, lost SSIM too (0.928 → 0.806): on such a frame SSIM reads noise, so the comparison asks the MAE and the grids'
means of it. Unity's ACES alone gives 3.44 / 2.77 / 2.98 on the three lit stations; the BRDF weight changes them by 0.02
(it matters at grazing walls the stations see little of). These figures predate the review's two fixes to the weight
(set for a Unity scene without a rasterized light; a metal's reflectivity by its metallic). Blendity's stations take
23-24 s each (the import and the lightmaps from the cache).

Unity's side: 1.2 s to open, 0.4 s the first render, 0.13 s the next. Before round 405 the causes, from the difference
maps, were the fog's blue a little lighter under Blendity's own ACES (round 401 had mapped Unity's ACES to it), the
walls' probe reflections, the lamps' glass and candle shaders Blendity didn't read, the particle smoke not drawn (the
user's choice), the players-only mirror. The first three are fixed; the smoke and the mirror's players remain.

Every station opens the scene afresh (the import and `unity lightmaps wait`); the first one also makes Unity write its
lightmap file (round 400: Unity's own bake read through Unity), the later ones find it beside the scene.

## Attributes

`tools/unity_attribute_table.py` lists every component class and script a project copy uses, with Blendity's standing
(native / data / drawn / written back, with the round). SideRail: 45 scene and prefab files; GameObject 239, Transform
179, PrefabInstance 136, MeshFilter 70, MeshRenderer 69, RectTransform 60, CanvasRenderer 36, MeshCollider 23, Light 20,
TextMeshProUGUI 20, Image 16, BoxCollider 15, UdonBehaviour 15, AudioSource 7, Canvas 6, ReflectionProbe 5, VRCUiShape 5,
LightmapSettings / NavMeshSettings / OcclusionCullingSettings / RenderSettings 3 each, ParticleSystem 3,
VRCMirrorReflection 3, LightProbeGroup 2, LightVolumeInstance 2, Camera 2, AudioListener 2, and one each of the rest.
Shinano: VRCPhysBone 102, VRCRotationConstraint 38, VRCPhysBoneCollider 21, VRCAvatarDescriptor 2, PipelineManager 2.
Every class in both projects is either native (rounds 362-403) or kept as editable data (round 403: what Blendity does with any component it has no native side for).
Canvases, TextMeshPro and particle systems are data by the user's choice (not drawn).

## Performance

| | Unity (batch) | Blendity (headless, CPU rasterizer) |
|---|---|---|
| SideRail open | 1.2 s | 147 s the first station (import 634 objects, 315 meshes, 196 nested prefabs; then Unity's lightmap fetch, which runs Unity itself), 24-83 s the next (open the saved scene, lightmaps from the cache) |
| SideRail render 1280 x 720 | 0.13-0.37 s | within the station's time above (the raster F12 is a few seconds) |
| Shinano open + render 900 x 1200 | 0.9 s + 0.1-0.4 s | 9.5-9.9 s (the prefab's import, the lilToon materials, the render) |

Blendity's station times are dominated by import and the lightmap fetch, not by rendering; a comparison that reuses an
imported scene (the Blendity project keeps it) runs at render speed.

**Navigation (round 420, after Pass 16):** a frame of each editor's views on the same assets - Unity's
(`tools/unity_frames.py`: the interval between two of the view's actual draws, Unity 2022.3.22f1 on the project copies,
the scene framed whole) against Blendity's (`tools/bench_world.py`: the whole editor's frame, means).

| View | Unity: median / mean / p95 ms | Blendity: presented by the card / by the system, mean ms |
|---|---|---|
| SideRail, Scene view (the whole world in view) | 10.0 / 25.6 / 99.2 | 10.3 / 19.6 |
| SideRail, Game view (Bloom, Vignette, Color Grading) | 4.5 / 4.8 / 6.6 | 10.4 / 12.5 (with the Scene view) |
| Shinano, Scene view | 2.1 / 2.2 / 2.9 | 7.0 (round 415's build) |
| Shinano, Game view | 2.4 / 2.5 / 3.4 | 5.1 / 7.2 (with the Scene view, round 416's build) |

Unity draws SideRail's Game view about twice as fast and Shinano's views two to three times as fast; SideRail's Scene
view is a draw on the median, with long Unity frames in its mean and p95. docs/PERFORMANCE.md, "Pass 16 in one table",
has the method and the caveats.

## SDK checks and HCI

Not run this round: the SDK's control-panel checks on the world (round 404's fixture `vrchat_sdk_world.json`) and the
HCI re-run of the avatar and world productions (`docs/hci/compare-2026-10.md`'s method) are deferred to round 405 with
the residual fixes, so that they measure the finished state once.

## Verdict

- **For avatar makers:** Shinano shades as in Unity (lilToon's model, round 402) within 0.9-2.6 / 255 at every station
  (round 402's 1.52 for the front was measured with the sun mirrored in X - the review found it; Unity's own direction gives 1.00),
  and her VRChat components are editable in Blendity and go back. The target is met.
- **For world makers:** after round 405 SideRail is within 1.6-3.5 / 255 at every station - three of four stations
  pass the target, the floor misses on SSIM alone (a dark frame's fine texture). What remains is the particle smoke (not
  drawn by the user's choice) and the mirror's players.

## Pass 17 (rounds 421-434, 2026-11-07): Blendity's own bake against Unity's

Every SideRail number above loaded **Unity's** lightmaps. Pass 17 compared Blendity's **own** Generate Lighting, at the
scene's own settings, with Unity's render of each station (`tools/unity_compare.py world ... --blendity-bake`):

| station (MAE / SSIM / grid correlation) | Unity's lightmaps | Blendity's bake, round 421 | Blendity's bake, final |
|---|---|---|---|
| spawn | 3.46 / 0.921 / 0.989 | 5.55 / 0.890 / 0.539 | 3.71 / 0.812 / 0.899 |
| spawn_back (dark) | 1.61 / 0.806 / 0.693 | 2.34 / 0.875 / 0.859 | 1.37 / 0.772 / 0.360 |
| floor | 2.85 / 0.870 / 0.998 | 7.66 / 0.692 / 0.666 | 4.36 / 0.747 / 0.974 |
| ahead | 3.00 / 0.949 / 0.984 | 4.12 / 0.900 / 0.419 | 3.06 / 0.883 / 0.958 |

The bake takes 85 s (686 s at the start; Unity's own was not timed). Brightness and placement now match Unity's (MAE under
4 at three stations, the light where Unity puts it); fine structure doesn't yet (SSIM 0.75-0.88, target 0.90). What
closed the gap: SideRail's lamps shine through see-through "light profile" spheres (Unity's Alpha Source, From Gray Scale)
with Unity's legacy falloff for baked lights; reflection probes had been upside down since round 401; Filamented's
specular occlusion; texel validity. Details and the open items: docs/PERFORMANCE.md, "Pass 17 in one table".

**Navigation, after round 431** (ms a frame on the card): SideRail's Scene view 8.1 (Unity's median 10.0), its Scene +
Game view 11.0 (Unity's Game view 4.8); Shinano's Scene view 7.5 (Unity's 2.2) - of it 2.8 ms re-deriving a scene that
didn't change (a scene-wide change counter, open).

## Residuals, ranked (after round 405)

1. ~~SideRail's fog tint under ACES / +1 EV~~ - fixed in round 405 (Unity's ACES curve as the view transform).
2. ~~The walls' probe reflections at grazing angles~~ - fixed in round 405 (Unity's BRDF weight).
3. ~~Shaders Blendity doesn't read (glass, candle flicker, the players-only mirror)~~ - fixed in round 405.
4. The floor station's SSIM (0.94 → 0.87 with Unity's curve): a dark frame's fine texture under the darker toe - not
   pinned down; the ACES fit's input and output matrices (the fog's blue a little more saturated than Unity's).
5. The eye materials' reflection textures (Shinano's face, 2.6).
6. Particle smoke (not drawn, the user's choice) and the mirror's players - counted, not fixed.
7. (Pass 17) Blendity's own bake: SSIM under the target at every station (directional strength, seams, the exit sign's
   glow); Shinano's views 3x Unity's (the change counter).
