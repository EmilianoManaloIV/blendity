# 0030: Large area lights without black or blotchy shading; the gizmo follows a new origin; Select Coplanar

- **Status:** done (as built below)
- **Requirements:** R-03 (rendering), R-01 (editing)
- **Decisions:** none (bug fixes inside ADR 0010's lightmapper and the rasterizer's light model)
- **Model:** main session (Opus)
- **PR:** https://github.com/EmilianoManaloIV/blendity/pull/28

## Goal
The user reported: "the area light when width and height are large (lets say 10 x 10) still creates weird
rendered artifacts (black splotches on the object, even a default cube)", "more like when you use the
lighting generate lighting feature it causes the error", and "when I retarget the origin of any object,
the transform tool and gizmo doesn't automatically apply to the new origin", and asked for "the ability to
select coplanar faces".

Reproduced in the scratch project (a cube under a 10 x 10 area light 3.5 m up, Generate Lighting):
1. **Baked (or Mixed) light:** the lightmaps are blotchy everywhere, even on open floor with nothing
   shadowing it, and denoising turns the noise into splotches. The direct light's estimate takes the
   range falloff from each random point on the light (up to ~8 m away on a 10 m light, so the falloff
   varies 10x across it) and multiplies it with the cosines: a peaked integrand that 32 shadow rays
   can't average. Nothing else in the renderers does that (they take the range from the light's centre).
2. **Realtime light:** the cube's sides are black. The rasterizer (Scene, Game views, F12 raster) treats
   an area light as a point at its centre, so a face whose normal is perpendicular to the light gets no
   light at all, although most of a 10 m light is in front of it.
3. **The origin:** the Inspector's Set Origin > Apply runs after the Scene view has placed the gizmo
   for the frame, and a change doesn't ask for another frame, so the gizmo stays at the old origin until
   the mouse moves over the view.

## Scope
- In:
  - **`area_light_cosine(l, p, n)`** (`raster.h`): an area light's facing-weighted cosine over its
    surface, what the path tracer's next-event estimate converges to without shadows; and
    **`light_range_falloff`** (the range part of `light_falloff`, at the centre).
  - **The rasterizer's area lights** (`light_surface`, the Gouraud `shade`): the diffuse from
    `area_light_cosine`, the specular from a representative point (the point of the light nearest the
    reflection ray, Karis 2013) with the lobe widened by the light's size.
  - **The lightmapper's direct light** (`bake_texel`): the unshadowed light from `area_light_cosine` times
    the fraction of the (stratified) shadow rays that reach the light, weighted by their cosines (a ratio
    estimator: open surfaces are exact and noise-free; only penumbrae are sampled), the range falloff at
    the centre as everywhere else.
  - **Probes' direct light** (`probe_volume.cpp`): an area light's six axis cosines from
    `area_light_cosine` (the visibility ray to the centre as before).
  - **`mark_changed` asks for another frame** (`ui_.redraw`), so anything a panel changes after the
    Scene view drew (Set Origin, a field) shows in the next frame without input.
  - **Select Coplanar** (`meshops::select_coplanar`, Edit Mode's `select_coplanar` in the Mesh menu and
    the console's `editop`): adds every face in the plane of a selected face (normals within 1 degree, the
    plane within a scale-relative 1e-4), joined to it or not (Blender's Select Similar > Coplanar). In vertex
    and edge mode the faces whose corners are all selected start it.
- Out: area-light shadows in the rasterizer (it has shadow maps for the sun only).

## Acceptance criteria
1. A cube's side under a 10 x 10 area light 3 m above it is lit in the rasterizer, within 10 % of the
   path tracer's unshadowed estimate; `area_light_cosine` matches a 1M-point Monte Carlo mean within 1 %.
2. A baked floor under a 10 x 10 Baked area light (no bounces, no denoiser) varies by under 2 % (std / mean)
   over open texels, and its mean matches the path tracer's direct light within 5 %.
3. A small light (0.1 m) gives what a point light at its centre gives, within 1 %. (Larger lights change
   on purpose: they are now lit by their whole surface, as in the path tracer.)
4. After the Inspector's Set Origin > Apply, the editor asks for another frame and the gizmo sits on the
   new origin in it.
5. Select Coplanar adds the faces in the same plane, joined or not, and none raised, tilted 5 degrees or
   facing the other way.

## Tests and checks to run
- Unit (round 52), the full suite (Windows, Linux, sanitizer, verify mode). Stress: `lighting`
  (lightmaps), `editor_render`.

## Documentation to update
- CHANGELOG, README count, USABILITY rows, ARCHITECTURE row for `area_light_cosine`.

## As built
Built as specified. **Deviations:**
1. **How `area_light_cosine` integrates** (settled after the review): on a rectangle, only the part in front
   of the surface's horizon (a line across the light: no kink inside what is integrated), the outer axis
   split where that line meets a corner, each axis through x = foot + h tan(t) (which flattens the
   1 / (h^2 + x^2) falloff from the point under p), 5 x 5 Gauss-Legendre points a piece (8 x 8 within a
   quarter of the light's size from it). On an ellipse, 12 rays from its centre (20 near), each ray's part
   in front of the horizon by Gauss in the area fraction r^2. The atan / tan are polynomials. Thirteen cases
   (under, beside and 0.3 m from a 10 m light, 20 x 0.2 m strips, disks, a 0.1 m light) are within 1 % of a
   million-point reference. Cost per pixel per area light, one thread: about 175 ns for a rectangle (one
   evaluation of a point light is about 10 ns), 210 ns where a wall cuts it, 280 ns 0.3 m below it, 360 ns
   for a disk (645 ns 0.3 m below); a 1080p view fully lit by one large light is 11 to 24 ms more on 32
   threads, and only when the view re-renders (the render cache keeps idle frames free).
2. **The user's later report**, "certain camera filter properties like edge outline don't show up in the
   render view ... I want the filters to show up in the path traced system as well": the Edge Outline was
   checked in path-traced F12 renders at 720p with the denoiser, on the CPU and both GPUs and in the
   Render window's Preview, and shows in all (a regression test now covers it). What the path tracer
   doesn't do is the Retro Console's rasterizer stages (vertex snapping, affine textures, the texture
   override, screen-door transparency, ADR 0008): task 0032 brings those to the path tracers.
3. **The specular of an area light** is GGX at the original roughness toward the representative point, times
   Karis's (a / a')^2: within a factor 0.5 to 1.2 of the path tracer's highlight (an approximation; the
   diffuse is exact).

**Review:** the code reviewer found two major issues and several minor ones, all fixed: the specular was
normalised twice (the lobe widened and also scaled down: highlights at 0.02x to 0.3x the path tracer's); the
per-pixel cost (up to 288 evaluations for a clipped disk; now the clipped region is integrated directly, at
25 to 64 points); the first quadrature was 17 to 62 % off within a metre of large or strip lights (now 1 %);
a held gizmo redrew every frame (the extra frame is asked for outside drags only); Select Coplanar compared
every face with every selected plane (now a grid of planes) and dropped the existing vertex / edge
selection (now it only adds); a zero-size light (an object scaled to 0) lit nothing in the rasterizer and
the bake while the path tracer lit it (now a point at the centre); a grazing texel whose shadow rays all fell
behind it went dark (now one ray to the light's nearest point).
