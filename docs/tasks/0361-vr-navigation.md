# Task 0361: moving around in VR, and scale (round 384)

## Asked
Phase C's second round (plan cozy-fluttering-corbato):
- **Moving:** teleport with an arc, smooth move, and grabbing the world with both hands to move, turn and scale it.
- **Scale:** a scale readout that snaps to 1:1, and a tape measure between the controllers.
- **What you see:** the active camera's frame and simple controller models.

## Built
- **`render/xr_nav.h`** (pure, so recorded hand poses drive it in tests):
  - **`Stage`:** where the headset's floor stands in the scene - an origin, a yaw, a scale and the lift that raises a
    seated runtime's origin to a standing height. Every path - the eyes, the hands, the arc, the grabs, the turns and
    the teleport - goes through the same lift.
  - **`teleport_arc`:** a throw from the hand's aim (7 m/s, the world's gravity, both times the scale), landing on
    whatever a hit test finds along a segment, or on the ground plane. It stops on a wall or a slope steeper than
    about 45 degrees (normal y under 0.7) without landing.
  - **`SegmentBvh`:** each mesh's triangles in a bounding-volume hierarchy, for the arc's segments. The editor makes one
    per mesh on a worker at its first aim, two at a time, and again when the mesh changes - its topology or only its
    positions (a sculpt, a deform: `deform_version`). Until it lands, the arc passes through that mesh, or uses its last
    one. The meshes are gathered once per arc; BVHs no mesh asks for are let go.
  - **`teleport`:** the stage moved so the head stands over the landing, the feet at its height.
  - **`smooth_move`:** walking where the head looks, level.
  - **`snap_turn`:** a turn about the head, which stays put.
  - **`grab_begin` / `grab_update`:**
    - one hand moves the world with it;
    - two also turn it about the vertical by the angle between the hands, and scale it by how far apart they come
      (apart: the world grows), about their middle, which stays where the hands hold it;
    - the scale snaps to 1:1 within 5%, and stays between 1:0.01 and 1:100;
    - hands that start within 5 cm grab as one, and a turn needs them 5 cm apart across the floor (one hand above the
      other turns nothing); when that changes mid-grab, the grab starts again from where the world is, so the world
      never jumps back to the start's turn.
  - **`measure`:** the world distance between two stage points.
- **The runtime's hands** (`Runtime::locate_hands`, `Session::hand`, `Session::head`). In OpenXR:
  - an action set of aim and grip poses, trigger, squeeze, stick and primary button;
  - bindings suggested for the simple controller, Oculus Touch (A on the right, X on the left), Valve Index, HTC Vive and
    Windows Mixed Reality controllers; a profile the runtime refuses is logged;
  - actions synced each frame, and each hand's poses located at the display time.
  - A runtime without actions still shows the scene, only without hands.
- **The editor's gestures** (`Editor::vr_hands`), run inside the session's frame once the hands are found, so the world
  moves on the frame the hands moved:

  | Control | What it does |
  |---|---|
  | Squeeze, one hand | moves the world with it |
  | Squeeze, both hands | move, turn and scale the world |
  | Right trigger, held | shows the arc |
  | Right trigger, let go | teleports to where the arc lands (a squeeze or lost tracking cancels it) |
  | Left stick | walks at 2 m/s times the scale |
  | Right stick, left or right | 30-degree snap turn |
  | A (Touch: A or X) / primary | the tape measure on and off |
- **What the eyes show** (`Editor::vr_overlay`):
  - each hand's controller as a box and its aim ray;
  - the arc, and a ring where it lands;
  - the render camera's frame at 1 m in front of it, at the final render's aspect (an orthographic camera's without the
    lines from its eye);
  - the tape and its length;
  - the scale (1:s) when it isn't 1:1.

## Checked
- **Two-hand grab:** two hands pulled to half their distance make the world 1:2, the grabbed middle moving less than
  0.1 mm. Turned a quarter about the vertical, the world turns 90 degrees.
- **One-hand grab:** one hand moved 0.5 m moves the world with it.
- **Snapping:** 1:1.03 snaps to 1:1, 1:1.2 doesn't.
- **Walking:** a second of stick walks 2 m where the head looks.
- **Snap turn:** the head stays put through it.
- **Tape:** 0.4 m between the hands measures 0.4 m, and 0.8 m at 1:2.
- **The arc:**
  - from 1.2 m, 30 degrees down at 7 m/s, it lands 1.541 m ahead (a throw: 1.535, the difference the steps' size);
  - a box top in the way stops it on the box;
  - teleporting puts the head over the landing, the feet at the box's height.
- **In the editor, on a stub runtime:**
  - both squeezes pulled together make the world 1:2;
  - the trigger held then let go teleports 1.06 m;
  - A toggles the tape;
  - the hands drew 200 of an eye's 4,096 pixels.
- **The review's fixes, checked:**
  - with a 1.6 m lift at 1:2, the eyes, both grabs, a snap turn and a teleport agree;
  - one hand then both, hands started 1 cm apart, one hand above the other, and the scale's limits;
  - a wall stops the arc without landing;
  - on 180,000 triangles the BVH finds the same first hits as every triangle tried (40 of 40), in 0.004 ms a segment
    against 1.2 ms; it takes 80 ms to make, on a worker;
  - in the editor: a squeeze while aiming and lost tracking both cancel the teleport; the world is 1:2 on the frame the
    hands came together; the head stands over the landing; the arc stands on a box's top once its BVH lands (25 ms).
- **The re-check's fixes, checked:**
  - the drawn eye's view on the frame the hands came together is the 1:2 stage's (off by 0);
  - the box's mesh deformed 0.2 m up in place: the arc follows it;
  - a 40-degree ramp lands; stretched 1.5 times upright (51.5 degrees) the arc stops on it without landing - the
    normal through the inverse transpose;
  - turned a quarter, then one hand over the other, then spread again: -90, -90, -90 degrees (no jump);
  - `vr scale` keeps the grab's range, 1:0.01 to 1:100.
- The live check is task 0360's, with the controllers: squeeze, trigger, sticks, A.

## Limits
- **Two-hand turns** are only about the vertical (no tilting the world).
- **The arc** lands on y = 0 or a mesh. After a scale moved the floor, `vr here` stands you back on the ground.
- **Controllers** are boxes, not their own models.
- **HTC Vive's trackpad** is bound as the stick: a touch walks or turns; there is no click-to-move.
