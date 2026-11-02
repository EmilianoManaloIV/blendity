# Task 0363: painting in VR (round 386)

## Asked
Phase C's last round (plan cozy-fluttering-corbato):
- **What it does:** the controller's tip or ray paints textures through the existing paint dab pipeline
  (`editor/paint.cpp`).
- **Brush controls:** size and opacity from the thumbstick; erase by the grip.
- **Tests:** a recorded dab on a known surface writes the same texels as a mouse dab there.

## Built
- **A world dab** (`Editor::paint_world_dab`, `paint_triangle_world`):
  - **Why:** the desk's dab is screen-space (the Scene view's projection, its id and depth planes); a controller has
    no screen.
  - **The disc:** a world dab is a disc square to the controller's ray, round where the ray meets the surface, its
    radius in metres. Its right and down are the controller's, and are the stamp's and alpha's axes.
  - **Which texels:**
    - a triangle out of the ball round the centre is skipped;
    - so is a triangle with its back to the ray (the far side is not painted), its winding turned for an object
      whose transform mirrors;
    - a texel more than the radius in front of or behind the disc is skipped (a surface behind, the other side of a
      thin wall);
    - inside the disc a texel takes the brush's stamp, alpha and grain, as the desk's.
  - **Everything else is the desk's:** the brush's colour, channels, tip, wet and jitter, symmetry (each mirror with
    its own frame), the pressure (opacity or size), the erase, the dilation at the stroke's end and one undo step.
  - **One rasterizer:** `paint_raster_uv` is the triangle's UV raster the desk's dab, the fills and the world dab
    share; the screen and world tests are its weights.
  - **Only the triangles near it:** VR passes the object's segment BVH (`SegmentBvh::near`) when it was made for the
    mesh as it is; otherwise every triangle is tried.
- **A stroke VR has under way is VR's:** the desk's Paint mode doesn't end it when the mouse is up, and starts no
  stroke of its own meanwhile; VR starts none while a desk stroke is under way.
- **`vr paint on`** (with Paint mode on for an object: select it, then the Paint tab or `paint on`; refused
  otherwise):

  | Control | What it does |
  |---|---|
  | Right trigger | paints where the right hand's ray meets the painted object (its base mesh, through the arc's segment BVH, up to 20 m times the scale); the pressure through the pen's curve; between frames the rays from last frame's to this one's, each hit again, a dab where one meets the surface, spaced by the brush's spacing, 64 a frame at most - none where they miss, so a stroke that leaves the surface never bridges across it |
  | Right grip | erases (it doesn't grab while painting; the left grip still does); squeezed or let go mid-stroke, a stroke of the other kind starts from there |
  | Right stick, up and down | the brush's radius, 1.5 times a flick (5 mm to 50 cm, at life size; the world's scale on top) |
  | Left stick, sideways | the brush's opacity, 10% a flick (5% to 100%) |
  | A | undoes the last step when it is a paint stroke ("Paint" or "Erase") |

  - **One brush at a time:** sketching and painting turn each other off (a sketch stroke under way is kept), and the
    tape is off while painting.
  - **Paint turned off at the desk** (Esc, another object, another scene) turns VR's painting off too.
  - **Walking:** the left stick's up and down still walks.
- **What the eyes show:** the ray to where it meets the surface (1 m when it meets nothing) and the brush's ring
  there, square to the ray, in the brush's colour (red when the grip erases).
- **Console:** `vr paint [on|off]`, `vr paint size <cm>`, `vr paint opacity <0-1>`. The lesson "VR View: painting
  with your hands".

## Checked
- **A world dab against a mouse dab:** on the 6 m cube's face at the view's centre, a 12 px mouse dab (`paint
  stroke`) and a world dab of the same size there (0.133 m, along the view's ray): the mouse painted 280 texels, the
  world dab 283, 279 of them the same (the rim).
- **Its limits:** the ray turned round (the face's back to it) paints nothing; the disc 1 m off the face (its
  radius 0.3 m) paints nothing; on the face it paints 1,159 texels.
- **In the editor, on a stub runtime:**
  - the right hand 15 m back, aimed at the cube, swept 3 m with the trigger held: 7,830 texels in one "Paint" step
    (a single dab is about 1,100; the review found the desk's Paint mode had ended the stroke after its first dab);
  - the same with the grip: 6,785 erased;
  - A twice: the erase, then the paint undone (no texel differs);
  - the right stick up: radius 0.30 to 0.45 m; the left stick left: opacity 1.00 to 0.90;
  - off the face to the left, round over the cube in the air and back on from the right as one stroke: 19,154
    texels; the two ends as two strokes: 19,124 - no band across the middle;
  - Paint turned off at the desk turns VR's painting off; without Paint mode, `vr paint on` is refused.
- **A mirrored object** (scale -6 on X): the face toward the ray takes the dab (1,159 texels, as unmirrored).
- **The desk's paint tests** (231, 108 and 258 checks in their groups) pass on the shared rasterizer.
- **Live:** task 0360's headset check, with Paint on for an object and `vr paint on`.

## Limits
- **The ray only:** the brush paints where the ray meets the surface, not at the controller's tip touching it.
- **No stencils** (they are screen images) and no 2D canvas painting in VR.
- **One object:** the object in Paint mode; choosing another is at the desk.
- **A dab's cost:** the triangles near it through the BVH; until the BVH is made for the mesh as it is (after an
  edit, on a worker), every triangle. Dabs a frame are capped at 64.
- **Undo at the desk mid-stroke** ends the stroke (it and the step before go, as the desk's Ctrl+Z mid-stroke does);
  with the trigger still held, VR starts a new stroke on the next frame.
- **The base mesh:** the brush projects onto the mesh before its modifiers, as the desk's does - a posed or displaced
  object is painted where its rest shape is.
