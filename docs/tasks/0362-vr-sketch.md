# Task 0362: sketching in VR (round 385)

## Asked
Phase C's third round (plan cozy-fluttering-corbato):
- **What it does:** controller strokes go into the existing Strokes system as 3D ribbons and tubes, like Quill or
  Gravity Sketch, with pressure from the trigger.
- **Tools:** a colour palette on the off hand, and undo on a button.
- **Tests:** recorded controller paths make the expected stroke geometry.

## Built
- **`StrokeRibbon`** (`scene/stroke_object.h`, ADR 0084) gains four fields, each optional, so old scenes load
  unchanged:
  - `widths`: a width per point;
  - `ups`: the strip's normal per point;
  - `tube`;
  - `material`: a slot.
- **The mesh:**
  - **Each point's normal** is square to the path:
    - a ribbon follows the hand's up made square to the path, turned over when it would fold the strip (a half roll
      doesn't make a bow-tie), with the last normal carried where the up lies along the path (a vertical stroke drawn
      with the hand level);
    - a tube carries its first normal along the path, turned by the smallest turn that takes each direction to the
      next (parallel transport), so its roll never twists it, even round a sharp corner.
    - A desk ribbon (no ups) keeps its facing as before.
  - **A ribbon** is a strip across the path, square to its normals, its width there. The custom normals are those
    normals.
  - **A tube** is a ring of 8 vertices per point (`kTubeSides`, plus a seam vertex for its UVs), the width its
    diameter, normals pointing out from the path, faces wound outward. Both ends are capped with their own vertices,
    normals along the path.
  - **The faces** are on the ribbon's slot, at most 255 (`kMaxStrokeSlot`).
  - **`Strokes::with_last_ribbon`:** the mesh with only the newest stroke added (a copy without the old mesh's render
    caches), the same as building every stroke again. ADR 0084 is amended to say so.
- **The scene file:** a ribbon's line takes the optional groups `W` (widths), `U` (ups), `T` (tube) and `M` (slot)
  after its points. A group that is broken or cut short leaves the plain ribbon, and the slot is held at 255.
- **Sketching in the editor** (`editor/vr_sketch.cpp`), with `vr sketch on`:

  | Control | What it does |
  |---|---|
  | Right trigger | draws: a point when the tip has moved a quarter of the width (2 mm at least); the width is the brush times 0.15 to 1 by pressure; the up is the controller's +Y (the brush turns with the hand) |
  | Squeeze | ends the stroke; the grip still grabs the world |
  | A (the right primary) | undoes the last stroke, only when the newest undo step is a VR stroke (a desktop edit is not undone unseen) |
  | X (the left primary: A on Index, the menu button on Vive and WMR) | switches between ribbon and tube |
  | Left stick, sideways | flicks through the palette of 8 colours (black, white, red, orange, yellow, green, blue, purple) |
  | Left stick, up and down | walks |
  | Right stick, up and down | brush size, 1.5 times a flick (1 mm to 50 cm) |
  | Right stick, sideways | snap-turns |

  - **The tip** is 5 cm ahead of the aim. The trigger starts a stroke past 0.1 and ends it under 0.05.
  - **A finished stroke** goes into VR's own Stroke object, "VR Sketch" (made at the origin on the first stroke, Own
    normals; the desktop tool's Stroke object stays its own; found by its id and its name, so an id freed by an undo
    and taken by another object is never used), in its space. A desk change still pending becomes its own undo step
    first. Its geometry is added to the mesh, not
    every stroke built again. One undo step ("VR Stroke" or "VR Tube").
  - **The tape** turns off when sketching starts (its buttons undo and switch the tube).
  - **Each colour** is a material slot, "Sketch <n>", made on first use.
  - **The brush** draws at the world's scale: at 1:2 a stroke is twice as wide in the world.
  - **The teleport arc** is off while sketching (the trigger draws).
- **What the eyes show:** the stroke under way in its colour, the brush's width at the tip (thicker for a tube), and
  the palette as a ring of colours round the left hand, with the chosen one larger.
- **Console:** `vr sketch [on|off]`, `vr tube [on|off]`, `vr colour <1-8>`, `vr brush <cm>`; anything else is
  refused with what it wants. The lesson
  "VR View: sketching in the air".

## Checked
- **A recorded stroke** (7 points 5 cm apart, the pressure rising, the hand turning a quarter about the path):
  - as a ribbon: each point's width across the path, centred on it, square to its up, the normals its ups, every face
    on slot 2;
  - as a tube: rings at half the width from the path and square to it, normals out from the path, caps facing along it,
    64 faces;
  - the scene file round-trips both, and a line from before round 385 still loads.
- **In the editor, on a stub runtime:**
  - the right hand drawn 30 cm with the trigger from 0.2 to 1 makes one ribbon of 31 points from the tip's first place
    to its last, widths rising from 3.2 mm to 1 cm, on "Sketch 3", and the stage never moved;
  - A undoes it;
  - three flicks on the left stick move the colour one on; the right stick up makes the brush 1.5 cm;
  - X makes the next stroke a tube.
- **The review's fixes, checked:**
  - every face of a tube is wound outward (0 of 64 inward);
  - a vertical stroke drawn with the hand level, its up jittering and a half roll between two points: neighbouring
    frames at least 0.994 alike for the ribbon and 1.000 for the tube, and the ribbon's normals square to the path;
  - three strokes added one at a time give the whole list's mesh;
  - 500 tubes of 100 points (459,000 vertices): building them all 46.6 ms, adding the last 11.9 ms;
  - a group cut short loads as the plain ribbon, and slot 2,000,000,000 loads as 255;
  - A after a desktop edit (a cube made) undoes nothing;
  - the strokes are on "VR Sketch", with Own normals, apart from the desktop tool's object;
- **The re-check's fixes, checked:**
  - a V turning 30 degrees a step with the up out of its plane: neighbouring rings at least 0.866 alike (cos 30);
  - "VR Sketch"'s id taken by an object of another name: the next stroke makes its own again;
  - "VR Sketch" past its vertices (50 in the test): the next stroke starts "VR Sketch 2".
- **Live:** task 0360's headset check, with `vr sketch on`.

## Limits
- **Strokes are geometry:** a stroke is not editable afterwards beyond Undo and the Stroke object's own tools.
- **Tubes are round:** 8 sides, no flat brushes, and no taper beyond the pressure. They shade with their own normals
  whatever the Stroke object's Normals mode (Surface and Canopy are for cards and ribbons).
- **The palette** is fixed at 8 colours. Other colours come from the Stroke object's materials in the Inspector.
- **The Stroke object** is drawn as the scene draws it, so a new stroke appears once it ends (the stroke under way is
  shown as lines).
- **Adding a stroke** copies its object's mesh (Windows: 11.9 ms at 459,000 vertices against 46.6 ms to build them
  all; Linux under WSL: about 30-47 ms against 25-65 ms - the copy is the cost). So VR's object is full at 100,000
  vertices and the next stroke starts another ("VR Sketch 2", ...): a stroke's end costs a 100,000-vertex copy at
  most, however much is drawn.
