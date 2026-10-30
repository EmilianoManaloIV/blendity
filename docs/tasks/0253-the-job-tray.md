# 0253: the job tray - every long job with its progress, its time and a Cancel (F2)

- **Status:** in progress (round 273)
- **Requirements:** R-14
- **Decisions:** ADR 0085 (F2)
- **Model:** main session
- **PR:** #219

## Asked
The HCI pass (ADR 0085): the long jobs - Quad Remesh, Mesh Maps, a baked-light layer, a surface's evaluation, a skin
solve, the lightmap and probe bake, the final render, MoonRay, the animation's video - each showed its progress in a
place of its own (the Rigging window's bar, the status bar's lighting text, the Render window) or not at all (Remesh,
Mesh Maps, the surface, baked light: a log line at the start and one at the end), none said how long it had run or
had left, and only some could be cancelled, each in a way of its own (Esc over the view, a console verb, a button).
Nielsen's H1 (visibility of system status) and H3 (user control and freedom).

## What changed
- **One tray** (`src/editor/job_tray.cpp`): `Editor::jobs_running()` lists the live jobs each frame from the state that
  already exists - the remesh, mesh-maps, baked-light, surface and skin jobs, the lightmap / probe bake, the final
  render (samples over the target; while an animation or a camera sequence renders, that is the one job listed, not
  a Render a frame), MoonRay, the video encode, and a *busy* label for the import that runs on the main thread - each with a label, a progress (or none: a moving stripe), when it started and how to cancel it.
- **Drawn in the status bar's right slot** (`draw_job_tray`): the newest job's label, its percentage, the time it has
  run and, past 2 % with a progress, the estimate left (elapsed x remaining / done), a bar behind the text, "+N" for
  the others and a tooltip listing them all, and a **Cancel** button for a job that can stop. The lighting bake's own
  status text gives way to the tray while a job runs.
- **Cancel** reaches the job's own flag: the remesh, mesh-maps and skin workers already stopped at the next row; the
  baked-light worker gains a cancel (and a progress) checked every 4,096 texels, its file removed if it was written
  before the cancel came; the lightmap bake cancels as its own Cancel does; the final render stops as the Render
  window's Stop does (`render_stop`, shared), the animation and the camera sequence as their own Stop do; MoonRay's
  process is told to stop. A surface's evaluation and the video encode offer no Cancel (the one would start again at
  once, the other's FFmpeg cannot be told). A cancel made another way (Esc over the view, `meshmaps cancel`) ends the
  job as cancelled too, and Mesh Maps' own line for it is information, not a warning.
- **Progress** where it was free: the mesh-maps baker counts its rows (`bake_mesh_maps` takes a progress pointer);
  the baked-light worker its texels; the skin job, the lightmapper, the final render and MoonRay reported one
  already (a bake's probe phase shows the stripe; MoonRay's checkpoints at 0 % too).
- **Done** (`jobs_poll`): a job seen last frame and gone this frame raises an Info toast - "Mesh Maps done in 3.2 s"
  (or "cancelled") - for a cancel, or a job that ran a second or more, and never for the Live Preview's renders; an
  Info toast never pushes out a warning or an error - so the end is seen from wherever the user is; the HCI log gets `job start <label>` and
  `job done <label> <seconds>` lines.
- **A busy state for the main-thread import**: the Import Preview's Import button sets the tray's label and runs the
  import two frames later, so the frame with "Importing <file>" is on screen while the import blocks.
- **Console:** `jobs` lists the running jobs with their progress and time; `jobs cancel [label]` cancels the first
  (or the named) one.
- Toasts of level Info draw with the accent colour and the Info icon (round 272 drew warnings and errors only).

## Tests (`tests_round273`)
- `meshmaps 1024 16` on the Sphere: the tray lists "Mesh Maps" from the frame it starts, with the test clock as its
  start and a Cancel; the frames until its progress first moves are printed and a value strictly between 0 and 1
  is seen; when it lands the tray is empty and an Info toast
  says it is done, with the time; the HCI log has the start and done lines.
- A second, larger bake cancelled through the tray's Cancel button (a real click on its recorded rectangle, re-aimed
  each try while frames swing under the bake's load): the job leaves, nothing lands, the toast says cancelled. The
  status bar's right text is measured at its widest frame time so the tray does not move between a press and its
  release.
- `jobs` prints the running jobs.

## Not done
- A skin solve finished during an Edit Mode session waits in the tray at 100 % with a Cancel that does nothing until
  the session ends (the result lands then).
- A MoonRay render still waiting for the program lists as a render whose Cancel does not clear the wait.
- Bake Textures on export stays on the main thread without a busy state (the export dialog is native; round 0260,
  files and the shell, takes it with import progress).
- Progress for Quad Remesh (QuadriFlow gives none) and a surface's evaluation (its layers are not of a size): the
  stripe.
- The busy state for the import is not driven by a test (the preview dialog needs a model file).
- Toasts for jobs that end inside a second (a surface's evaluation on a small texture) are shown too: a floor could
  come with the hint table.
