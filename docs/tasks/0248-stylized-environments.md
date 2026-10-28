# 0248: stylized environments - strokes in 3D, foliage and scatter, a stylized sky and fog

- **Status:** done (round 268)
- **Requirements:** R-07, R-14
- **Decisions:** ADR 0084
- **Model:** main session
- **PR:** #214

## Asked
The user (2026-10-27): the stylized side after the hand-painted look - environments in the Ghibli / oil-paint 3D
style: brush strokes as geometry, hand-placed foliage, scattered props, a painted sky.

## Goal
A brush that paints **geometry** - each dab a camera-facing card or the drag a ribbon - as one Stroke object, its
cards' normals set to the surface's or a canopy centre's (the hand-painted foliage trick); a **scatter** brush placing
instances of an object onto a surface with density, random scale / rotation / tint and a mask; **foliage from
strokes** (a canopy of cards round a centre, grass within a painted area); a **stylized sky** (the gradient sky with a
painted sun disc and a horizon band) and **distance fog** with a colour ramp in every renderer; a Stylized workspace.

## What changed
- **Stroke objects** (`src/scene/stroke_object.cpp`, ADR 0084): a `Strokes` component keeps the strokes - a **card**
  (one quad of `size` facing the camera at its creation, the surface's normal under the dab kept) per dab, or a
  **ribbon** (the drag's points swept into a strip `width` wide, the strip facing the camera at its first point) - each
  with a UV square for a painted alpha image (the material's base map, Cutout); the object's mesh is rebuilt from the
  list on every edit with **custom normals** by the component's Normals: Own (each card's facing), Surface (the
  normal under the dab) or Canopy (away from the Canopy centre - a bush of flat cards shades as one round volume). The
  list is saved as text in the scene file (one stroke a line). In the editor (`src/editor/strokes_tool.cpp`, Modeling
  Tools > Strokes, `strokeobj on|off`): a click on a surface adds a card, a drag with Ribbon a strip at the release (a
  drag with Card a card every size's worth of pixels); each stroke one undo step (the list changes, the mesh is
  rebuilt; copy on write stores the small mesh with the step). One Stroke object a session (`strokeobj new` starts
  another); `strokeobj card|ribbon`, `size`, `width`, `normals own|surface|canopy`, `canopy [x y z]` (or the strokes'
  centre), `alpha <image>|off`, `dab x y`, `stroke x0 y0 x1 y1 [n]`, `count`.
- **Foliage from strokes:** `strokeobj canopyfill [radius] [count] [x y z]` fills a shell round the canopy centre (or
  the point given) with `count` cards on a Fibonacci sphere, each facing out, the Normals set to Canopy - a bush or a
  crown; `strokeobj grass [density] [height] [radius] [x y]` drops upright cards of `height` onto the surface under a
  view point, `density` a square metre of the disc, kept off a Paint layer's painted texels when the scatter mask is
  set, the canopy centre put far below so every blade's normal points up. (The plan's canopy from a sketch outline
  is not this round: the shell round a centre is the hand-painter's bush; an outline-fitted cloud would follow.)
- **Scatter** (`src/editor/scatter.cpp`, Modeling Tools > Scatter): `scatter <object>` names the source, then dabs
  (`scatter on`, or `scatter dab x y`) place instances - GameObjects sharing the source's mesh and materials (copy on
  write), a tinted material copy when Tint is on - as children of one *Scatter* object, `density` a square metre of
  the dab's disc of `radius`, spread on a golden-angle spiral jittered from the seed and the hit point (the same brush
  at the same spot places the same instances), each
  dropped onto the surface by a ray along the normal and kept when it lands on the same object; aligned to the surface
  normal or world up, scaled between `scale a b`, turned by a random yaw up to `rotate`; **a mask** (`scatter mask
  <layer>`): a Paint layer of the surface's stack whose coverage keeps instances off its painted texels (the point's
  UV from the triangle under it). The pen's eraser end (or `scatter erase x y`) removes the instances within the disc;
  `scatter merge` joins them into one mesh on a new object for export (the instances kept).
- **A stylized sky and fog** (`EnvironmentSettings`, the Lighting window's World): the Gradient world gains a
  **Stylized Sky** group - Sun Disc (its size in degrees, toward the first directional light; a softness and a colour)
  and Horizon Band (its half-width and colour) - and every world a **Fog** group (on, start, end, a near and a far
  colour), applied by the view distance as a mix toward the ramp: in the CPU raster (`Renderer3D`'s lit pixels), the
  GPU view (`light_pixel`), the path tracer (the camera ray's first hit: the fog's share added, the path's weight
  scaled) and its GPU kernel; the sky's disc and band in `Environment::radiance`, the GPU view's and the kernel's
  gradient alike. The retro camera filter's fog stays a display effect of its own. Set from the console through the
  world's fields (`set world.Fog 1`, `set world.FogEnd 40`, `set world.SunDisc 8`, `set world.HorizonBand 0.15`: a
  field's name with its spaces dropped).
- **The Stylized workspace** (`layout Stylized`: the Scene view, the Modeling Tools with the Strokes and Scatter
  sections, the Lighting window for the sky and fog, the Inspector) and its guide.

## Tests (`tests_round268`)
- Editor: a dab on the 4x cube makes a Stroke object with one card (4 vertices, 1 face) whose normal is within a
  degree of the view direction, one step; Surface normals give the face's axis normal, Canopy normals the direction
  from the centre to each corner; a ribbon of 10 points is a strip of 20 vertices and 9 quads; the list round-trips
  through its text; `canopyfill 1 100` puts 100 cards on the shell within 1e-3 facing out; `grass 40 0.25 1` on the
  sample scene's plane makes 126 upright blades (40 pi wanted) with Canopy normals; undo brings the list and the mesh
  back. A scatter dab of density 4 in a 1 m disc on the plane places 13 instances (4 pi wanted) as one step, all on
  the plane within the disc; the eraser removes them; `scatter merge` makes one mesh of 13 x 384 faces; with the
  plane's surface painted over and `scatter mask 1`, 0 instances.
- Render: on the gradient world the horizon band colours the horizon and the sun disc its centre with the zenith
  untouched and a direction well outside the disc unchanged; the fog leaves a surface at Fog Start its own colour,
  gives one at Fog End (and beyond) the far colour, and is off when off.
- Not covered: the GPU view's and the kernel's sky and fog against the CPU's (the gpu group runs on the card in the
  chain, where a GLSL error would show; the pixels are not compared), the Strokes and Scatter sections by clicks, the
  alpha image's cutout on the cards.

## Not done
- A canopy fitted to a sketch outline (a shell round a centre instead); sway through spring bones.
- Scatter on several objects in one dab (the drops keep to the object the dab hit).
- Fog in the retro camera filter's path (its own fog stays).

## Review (round 268)
Opus's findings taken: fog reaches the CPU raster's 8-wide shading path too (plain materials had stayed clear
inside each run of eight pixels); the path tracer's and the kernel's fog distance runs from the camera through
see-through passes (a hill seen through a leaf card's holes had less fog than the frame); every card and ribbon
segment is two single-sided faces back to back sharing the custom normal, so a renderer's back-side flip never turns
the Canopy normal (half the grass had shaded as facing the ground); the scatter spread is the golden angle jittered
(it had been one spiral arm); the Scatter parent is validated by name and shape (a freed id could adopt another object
and the eraser destroy its children); the tint is one copy a dab with no asset path (relinking had dropped it, and
every instance had carried a copy); the counts are capped at 5,000 instances and 20,000 blades a dab with a reason
(density x radius could overflow an int); the mask's triangle search rejects by bounds first; a dab is repeatable by
the seed and the hit point (the session counter had made it not so); a canopy or grass goes into a Stroke object of
its own when the current one holds strokes of another kind (they had moved the object's canopy centre); instances
are named "<source> Instance"; the spec's console forms are the field names without spaces (`set world.FogEnd 40`;
`set world.Fog End 40` had turned fog off); `gpu_device.h` is back to LF. The OptiX device's committed PTX was regenerated (tools/optix_ptx_embed.bat) so it carries the sky disc, band and fog too. Open: the Inspector's Normals edit does not rebuild until the next stroke; the mask reads the base mesh (a modifier's
geometry is not matched); the parent's own transform is assumed identity; glass adds fog over a fogged background.
