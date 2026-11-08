# Task 0400: Soft baked shadows (round 424)

- **Status:** built
- **Requirements:** R-03 (rendering), R-08 (VRChat worlds and avatars)
- **Decisions:** none new (ADR 0113's target)
- **Model:** main session (Opus 5.5)
- **PR:** stacked on #321 (round 423)

## Asked
Pass 17's plan, round 424: soft baked shadows - Unity's Baked Shadow Radius (point and spot lights) and Baked Shadow
Angle (directional) read from a scene's lights, shown in the Inspector, and baked: a light's direct shadow rays
spread over its size, in the lightmaps and the probes. Round 423 also left the per-light diagnosis of SideRail's dim
stations to this round.

## Built
- **The Light's fields** (`Light::shadow_radius`, `shadow_angle`, src/scene/scene.h): Baked Shadow Radius (metres,
  0 - 10) on point and spot lights and Baked Shadow Angle (degrees, 0 - 90) on a directional one, shown when the light
  casts shadows, with help saying what they do and that Direct Samples sets the rays. Read from Unity's Light
  (`m_ShadowRadius`, `m_ShadowAngle`) on import and from prefab overrides (clamped), written back by the Unity export;
  `to_render_light` passes them to the bake's `RenderLight` for a light with Soft Shadows only - as Unity's
  LightmapperUtils does (a light switched to Hard Shadows keeps its radius in the file and bakes sharp) - and the
  Inspector shows them only then.
- **The shadow rays spread over the light** (`baked_light_visibility`, src/render/lightmapper.cpp): the share of
  Direct Samples stratified rays into the cone the light fills seen from the texel - a ball of the radius round a
  point or spot light, a disc of the angle round a sun's direction - that nothing blocks; uniform over the cone's
  solid angle on a jittered grid with a random shift per texel (the area lights' points share the helper). Each ray
  toward a ball stops at the ball's near side along that ray, so the bulb's own socket inside it casts nothing while a
  fixture's wall outside it is tested; a texel inside the ball takes the one centre ray. Each ray is weighted by its
  cosine to the texel's normal - a ray below the surface's horizon isn't a shadow (a bulb near a wall would have
  darkened it). One ray, as before, for a light with no size.
- **The probes the same** (`probe_bake`, src/render/probe_volume.cpp): 16 rays for a light with a size.
- **No Shadows is baked as Unity bakes it:** a light whose Shadow Type is No Shadows casts no baked shadow (the bake had
  ignored the setting - only the views and the path tracer honoured it), area lights included.

## The per-light diagnosis (SideRail)
A temporary hook cast 512 rays out of every Baked light in SideRail's bake (not committed). **42 of its 70 lights are
boxed in:** every ray from them hits something within 5 cm - the bulbs of the FootLight and BracketLamp prefabs sit
inside their fixtures' closed meshes. Blendity's bake blocks a shadow ray at any surface, so those lights add nothing.
Letting the shadow rays through back faces (a second temporary hook) put the light where Unity's is - the stations'
grid correlation 0.49 -> 0.91 (spawn), 0.84 -> 0.89 (floor), 0.47 -> 0.80 (ahead), at 10 texels per unit and 64
samples - but 1.7 to 8 times too bright: every surface had become one-sided, the fixtures' own walls included.
How Unity's lightmapper treats single-sided geometry for shadow rays (and its backface tolerance) is round 425's
subject (texel validity), with this as its first measurement. None of SideRail's lights has a Baked Shadow Radius or
uses No Shadows, so this round doesn't change its stations.

## Changed behaviour
- A light with No Shadows lights through everything in a bake too.
- The Light has two new fields: a scene's lighting key includes them, so bakes saved before this round show as out
  of date once.
- The review's fixes: each ray's own reach to the ball (one length for all had stopped the cone's edge rays short of
  a fixture's wall and sent near rays into the ball), Soft Shadows only (as Unity), rays below the horizon left out,
  the stratified grid shared with the area lights, the probes' visibility extracted, the Shadow Type's help saying
  the bake honours it, a tighter energy check and new tests.

## Tests
`tests_round424` (tests/test_lighting.cpp): a card 1 m over a floor - a point light with a 0.4 m radius gives 393
penumbra texels where a sharp one gives none, the floor's share of the light about the same (0.924 / 0.919); a sun at
20 degrees 206 where a sharp one gives none; No Shadows leaves the floor's darkest texel as lit as with no card; a
prefab's Light read with its radius and angle (500 degrees clamped to 90) and the radius passed to the bake's light;
a 0.3 m bulb 0.1 m off a bare floor lights it exactly as a sharp one (no ray below the horizon counted as a shadow); a
5 cm bulb inside a closed 20 cm box lights nothing outside it; a Hard Shadows light bakes with no radius.
