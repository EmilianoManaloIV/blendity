# Task 0320: a lattice turned to face the camera

- **Status:** done (round 342, 2026-10-31)
- **Asked:** the 2D-look methods ("Add those components onto the plan."): a camera-aligned lattice to shape the
  silhouette as the camera sees it, shot by shot.

## What
- **Lattice > Align to Camera:** in the camera views (the Game view, Camera Preview, F12) the box turns to face the
  camera about its own centre - X the screen's right, Y its up, Z toward the camera - keeping its size; its points then
  move the mesh as the camera sees it (pull the far eye forward, widen the jaw in profile). Out of the camera views
  (the Scene view, exports, bakes, physics) an aligned box moves nothing, so you model as usual.
- Keying its points (round 335: any field) makes a correction for one shot only.

## How (ADR 0094)
- `Lattice::frame` gives the box's frame: its object's, or turned to the camera the views told it about (`view_on`,
  the eye); the Lattice modifier's dependency hash mixes that frame, so each camera evaluates its own result.
- The views set the camera on every aligned lattice for their collect and take it away after
  (`Editor::set_view_lattices`); a mesh through an aligned lattice is evaluated in its camera view's own slots
  (round 339's kinds 3-8), so the Scene view keeps the model and no traced view's mesh is rewritten by another.

## The review's fixes
- The camera reached every evaluation of the collect, not only the camera view's own: a plain mesh evaluated during it
  (a lightmap lookup, the lighting's out-of-date key, a Boolean reading the deformed object) took the camera's shape
  into the shared slots and keys - lightmaps dropped, the whole scene rehashed each frame, and a mesh a traced view
  held rewritten. The box faces the camera only while a camera view's own kind is evaluated (`evaluating_camera_view`,
  thread-local, cleared for a plain kind nested in one).
- A Lattice modifier shown in renders only (the kind the camera views evaluate) went the plain way: the kind's own
  switch decides; a lattice at rest or a Strength of 0 keeps the camera out of the key.
- X is the screen's right from the camera's own up (a rolled camera; straight down no longer spins it).
- The Scene view's cage and the box's rotation not counting are said in the help and the lesson; a Try-it.

## Acceptance (`tests_round342`)
- A lattice's +X points pulled out widen a Cube toward world +X through a camera in front, toward world -Z through a
  camera on its right, and not in the Scene view; with Align to Camera off it widens to +X from every camera.
