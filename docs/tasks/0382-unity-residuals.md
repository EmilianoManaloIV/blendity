# Task 0382: the residuals fixed - Unity's ACES, Unity's indirect specular, three shaders read (round 405)

- **Status:** built
- **Requirements:** R-unity-parity (the 2026-11-03 plan: rounds 401-405), Q-tests, Q-docs
- **Decisions:** none needed (ADR 0099's look, refined)
- **Model:** main session (Fable)
- **PR:** stacked on #303 (round 404)

## Asked
"Lighting, Emissions, and other scene properties should be nearly the same and almost indistinguishable." The fifth
round: the residuals round 404's report ranked, fixed in order of their weight, and the stations measured again.

## Built
- **Unity's ACES as the view transform** (`ViewTransform::AcesFull`, "ACES (full fit, Unity's)"): Hill's fit of the ACES
  RRT + ODT - its curve, per channel, without the fit's input and output matrices (exact on greys, where the matrices'
  rows sum to 1; a saturated colour comes out a little more saturated than Unity's), with no exposure scale - the curve
  Unity's post-processing applies; the PPv2 import picks it (round 401 had picked Blendity's own ACES, a different curve,
  0.6 x). In every view: the CPU views, F12, the display's SIMD paths and the GPU views (their frame carries it as code 4;
  3 is an OpenColorIO table). Listed after the OpenColorIO views so a saved OpenColorIO setting keeps its view; its own
  saved index therefore depends on the build's OpenColorIO views (the dependency-free build saves 3, the full build 3 +
  the views' count) - a known limit, noted in ADR 0099.
- **Unity's indirect specular** (`unity_indirect_specular`: BRDF1's Fresnel from f0 to the grazing term - smoothness +
  reflectivity, the metallic workflow's lerp(0.04, 1, metallic) - by (1 - N.V)^5, times 1 / (roughness^2 + 1)) weighs
  the reflection probes' and the sky's reflection on a scene from Unity (`LightingEnv::unity_brdf`, set for any scene
  with a Unity link, lights or none: the Scene view, F12's rasterizer and the GPU views); Blendity's own look keeps
  Karis's approximation. The two differ most at grazing angles on rough surfaces: a rough concrete wall (roughness 0.8)
  reflects a probe at 0.17 of it in Unity's model against 0.03 in the approximation - Unity's walls catch more of the
  probes at the edges of the picture, as SideRail's do.
- **Three shaders read as what they draw** (`unity_shader_family`, by the shader's own name - the path's last part - so
  a lit shader that mentions glass or a mirror isn't one): a glass shader (Glass_NoFog, GlassShader) as Blendity's Glass
  surface; a candle flicker shader as an emissive flame (its emission colour, times ten times its power multiplier, at least
  once); a mirror shader (VRChat's
  players-only mirror, FX/MirrorReflection) as a mirror (white, metallic, smooth - the probes' reflection; Unity's batch
  render draws the mirror without its camera, so what the two sides show there isn't checked).

## Measured (local, the user's packages)
SideRail's four stations, round 404 → 405: spawn MAE 7.03 → 3.46 (pass), spawn turned round 2.40 → 1.61 (pass: a nearly
black picture - image_compare.py's dark-frame rule: the MAE and the grids' means decide, 1.63 against 0.37), floor 4.71 →
2.85 (passes on error, misses on SSIM: 0.937 → 0.870 - the darker toe of Unity's curve on a dark frame moves its fine
texture's contrast; not pinned down), ahead 6.25 → 3.00 (pass). Unity's ACES alone: 3.44 / 2.77 / 2.98; the BRDF weight
adds 0.02 on these stations (`BLENDITY_UNITY_BRDF=0` turns it off, a diagnostic) - measured before the review's fixes
(the weight set without a rasterized light, the metallic reflectivity), which touch the stations' pictures where the
walls' metal trim and the sky-lit floor meet the probes, not the figures' cause. The report has the table.

## Acceptance criteria
1. The full-fit curve's values and monotony, the SIMD display path equal to the scalar, the setting's index round trip.
2. The indirect specular's values at grazing and facing angles for a rough dielectric, a dark rough metal and a smooth
   metal; the weight on for a Unity scene without lights; the GPU frame's code for the full fit; the curve in every
   display kernel.
3. The three families named and read.
4. The suites, the audits.

## Not done
- The SDK's control-panel checks on the world and the HCI re-run (the plan's round 404 items deferred here): with the
  parity rounds done, they go to a round of their own, so the user's review of 401-405 isn't held by them.
- Particle smoke and UI canvases (the user's choice: not drawn); the players-only mirror's players; Udon-driven state.
- The ACES fit's input and output matrices (the RRT's saturation and the gamut change on saturated colours: the fog's
  blue a little more saturated than Unity's); the full-fit view's saved index across builds with different OpenColorIO
  views (save the view by name).
