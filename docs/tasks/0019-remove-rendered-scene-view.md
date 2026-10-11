# 0019: Remove the Scene view's path-traced Rendered mode

- **Status:** in review (approved 2026-10-10)
- **Requirements:** Q-01
- **Decisions:** none needed (a feature removed; no data-model change)
- **Model:** main session (Opus)
- **PR:** (link when opened)

## Goal
The user found the Scene view's Rendered mode (progressive path tracing) too costly for an editing view and
asked for it to go.

Path tracing stays where it belongs:
- **F12** and the **Render** window;
- the **Camera Preview's Rendered** toggle (a small preview of one camera).

## Scope
- In:
  - **The Scene view dropdown** is Wireframe, Solid, Shaded and Shaded Wireframe.
  - **`shading rendered`** warns and shows Shaded, so old scripts and lessons don't fail.
  - **The Scene view's path tracer is removed,** with its state and its continuous redraw.
  - **Viewport Samples** is kept in scene files so they still round-trip, but is no longer shown.
  - **The lesson step** "Switch the Scene view to Rendered" becomes "Path trace the selected camera's
    preview".
- Out: the Camera Preview's Rendered toggle and F12 are unchanged.

## Acceptance criteria
1. `shading rendered` gives the Shaded picture, bit for bit, and the editor doesn't keep redrawing.
2. The other modes still switch.
3. Existing tests pass: the PS1 pilot test loops over the remaining modes.

## Documentation to update
- README (render paragraph), USABILITY, CHANGELOG.
