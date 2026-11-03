# Task 0377: Unity's baked lightmaps in Blendity (round 400)

## Asked
"Let's implement a way to add Unity baked light maps into Blendity" (plan cozy-fluttering-corbato, round 400; the user
chose to show Unity's own lightmaps - nothing written back into Unity's lighting data). Then, while it was under way:
"The materials in Blendity are not the same in the Unity Project. I can also see an error with emission based
materials" - "the texture is not the same for the world projects".

## Built
- **Asked of Unity** (`editor/unity_lighting_ui.cpp`, console `unity lightmaps [wait]`, the Lighting window's Use Unity's
  Lightmaps, a toast when a Unity scene with baked lightmaps opens): the installed Unity opens the scene in a scratch
  project rebuilt from the package and writes `<scene>.unity-lighting` beside it - each lightmapped renderer, its mesh's
  lightmap UVs, the lightmaps' and directional maps' texels, the lights' bake modes, the baked probes as Unity's shading
  reads them. In the background, polled each frame, as `unity modelids`. ADR 0098.
- **Read and matched** (`render/unity_lightmaps.*`): the file checked (magic, version, FNV-1a 32, every count against what
  is left); each renderer found by its path, each of its object's render triangles matched corner by corner to Unity's
  in world space; UVs scaled, offset and turned to Blendity's v.
- **Shown as the scene's baked lighting:** pages from Unity's texels; Unity's Directional lightmaps' shading folded into
  each texel by the surface's normal there; each object's entry keyed as it is now; the lights' modes; Unity's probes as
  the probe volume; Baked GI on, Realtime GI off. A file already beside the scene shows when it opens.
- **Review fixes:** the file records a hash of the scene's LightingData.asset and is refused once Unity has baked again;
  values that aren't finite or out of range refused even under a good checksum; an imported scene matched through its
  root wherever the pivot put it (probes too); the job refused while Blendity bakes or plays, and Generate Lighting
  while it runs; the file written beside and renamed over; the scratch project forced to Linear colour space (a new
  project is Gamma, and Unity then hands HDR lightmaps over gamma-encoded - SideRail's came out 4.6 times too bright);
  renderers past the lightmap count skipped; directional triangles leaving the page skipped; normals by the
  inverse-transpose; the triangle search by a k-d tree (the test had taken 112 s).
- **Re-check fixes:** the Properties block read with comments and strings blanked, the word on its own in any case,
  every `name (` at the block's depth (two a line, a name before its parenthesis on the next), and nothing dropped
  when the block doesn't close or none of the .mat's names are in it; only lights under the imported root take
  Unity's modes (the scene's own "Directional Light" had been switched to Baked); nothing matched - refused, the
  scene's own bake kept; a root turned after the import turns the directional normals and the probe cubes back into
  Unity's world; an earlier run's error file not quoted; the matching built once a renderer and run on every thread
  (SideRail's apply 0.71 s on the main thread, now 0.34 s: reading the file 0.09, matching 0.09, directions 0.08);
  the C# reimport in one helper.
- **Materials as Unity draws them** (`scene/unity_material.*`): `unity_shader_properties` reads a shader's Properties
  block, and only those properties of a .mat are read (a .mat keeps every earlier shader's - a lamp had taken a stale
  red `_EmissionColor`, a sign a stale `_MainTex`); normal, metallic and roughness maps marked as data (they had been
  read as sRGB colour); uniuni's FakeInnerLight (SideRail's lamps) mapped - albedo `_Albedo` x `_Color`, emission
  `_Emission` x (`_EmissionColorBase` x `_BaseBrightness` + `_EmissionColorLight` x `_LightBrightness` x 0.5 for its
  view-dependent glow), times `customColorMultiplier`.

## Checked
- **SideRail** (the user's world, local): `unity lightmaps wait` - Unity read it in 17 s (the scratch project made
  earlier); 1 page (2048 x 2048, directional), **180 of 180 lightmapped renderers, 100% of their 162,933 triangles
  matched**, 118 probes. From Unity's spawn, Blendity's view against Unity's render of the same view (16 x 9 regions):

  | | Mean luminance (Unity 14.9) | Mean difference | Correlation |
  |---|---|---|---|
  | Without Unity's lightmaps | 9.0 (0.60) | 12.9 | -0.14 |
  | With them | 14.1 (0.94) | 9.0 | 0.23 |

  What still differs: Unity's exponential fog (linear in Blendity), the lamps' emissive glow, and overlay lines.
- **SideRail's materials** dumped from Unity (`MaterialDump.cs`, local) against Blendity's: 8 of 42 differed, 4 after -
  a candle whose emission a script flickers, and three particle / Light Volume materials Blendity doesn't draw.
- **Submesh order** (`unity_drop_unused_materials`): every shown renderer's material and triangle count per submesh
  dumped from Unity and Blendity (local scripts) - 9 of 347 differed, every one a model whose polygons first use a
  material other than the file's first: Unity numbers submeshes by first use. Now 0; left: 12 lamp filaments' second
  part (no material in Unity, not drawn there; drawn with the first in Blendity) and 4 objects' n-gon splits.
- Tests (`tests_round400`): the file written and read equal, a changed byte and a cut file refused, a page's rows turned;
  a Unity scene opened with a hand-written file beside it - the cube's UVs exact after scale, offset and v, the
  directional shading (the lit face 1.99, the far face 0), the lamp's mode, the probes, Baked GI on and Realtime off.
  The review's: imported under a root at (10, 0, -4) - lit, the probes moved with it; saved and opened again - kept; a
  Light Volume's Unity bake kept beside them; a LightingData.asset changed since - refused; an index past the mesh, a
  NaN, a lightmap past the pages under good checksums - refused. Materials: a shader's declared properties (an
  attribute's parentheses, a comment), FakeInnerLight's albedo, tiling, normal as data and blue emission 1.5 over a
  stale red, and a built-in shader's .mat read as before. The re-check's: a shader with braces in a comment and a
  display name, two properties a line, a name on its own line, lowercase `properties`, a `SurfaceProperties` struct
  before - all four found; an unclosed block - none; nothing matched - refused; the scene's own lamp keeps its light;
  a root turned a quarter about Y - Unity's +X probe light along the scene's -Z.

## Not done
- Unity's per-pixel normal maps in the directional decode, its tetrahedral probe interpolation, reflection probes,
  Enlighten realtime GI, terrain.
- Remembering across save and open that a bake came from Unity.
