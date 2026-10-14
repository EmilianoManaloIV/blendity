# 0061: The animation curve editor (Unity's Curves view)

- **Status:** done
- **Requirements:** R-06
- **Decisions:** ADR 0029 (amended: tangents on keys)
- **Model:** main session (Opus)
- **PR:** (link when opened)

## Goal
The user's later list (2026-10-12): "a curve editor" for the keyframe animation of task 0059. Unity's Animation
window has two views, Dopesheet and Curves; the Curves view draws each property's value over time and lets the keys'
values and tangents be shaped by hand.

## Scope
- In:
  - **Tangents on keys** (Unity's key tangent modes): **Clamped Auto** (today's Auto), **Free Smooth** (one slope,
    both sides, set by dragging a handle), **Flat** (slope 0), **Broken** (separate in and out slopes); the span modes
    Linear and Constant stay. Each key keeps its in and out slopes; the file stores them (older files still load).
  - **Curves view**: a Dopesheet | Curves switch in the Animation window. The selected tracks' curves (all when none
    is selected) in their own colours over a value axis with grid lines and numbers; keys as points.
  - **Editing**: drag keys in time and value (Shift: one direction only), the selected key's tangent handles (Free
    Smooth: both together; Broken: each alone), right-click a key for the tangent modes and the span modes, Delete,
    double-click on a curve adds a key on it. **F** frames the curves; the wheel zooms the value axis, Ctrl+wheel the
    time axis (not built: see As built).
  - Edits are undo steps (as the dopesheet's).
  - Console: `anim tangent <object|.> <property> <axis> <frame> auto|free|flat|broken [in] [out]`.
  - A Learn section.
- Out: Bezier weights (Unity's weighted tangents), the clip sequencer (task 0062).

## Acceptance criteria
1. A Free Smooth key's curve leaves it at the handle's slope on both sides; Broken uses each side's own; Flat is 0;
   Clamped Auto is as before (no overshoot between two keys).
2. Files round-trip the tangents; a task-0059 file still loads (its keys Clamped Auto).
3. In the Curves view, dragging a key changes its value; dragging a handle sets the tangent (and makes it Free Smooth).
4. Edits undo in one step each.

## Tests and checks to run
- Unit tests (round 85); full suite, verify mode, Linux, sanitizer; stress `animation`.

## As built
- **Not built: Ctrl+wheel time zoom.** The Curves view shares the dopesheet's time axis (the clip's length fills the
  width), so the two views line up; zooming time would need a time scroll for both views. Left for later.
- **Double-click** adds a Free Smooth key at the curve's own slope there, so adding a key doesn't change the shape
  (Unity does the same).
- **Preview follows a drag**: while a key or handle is dragged with Preview on, the scene shows the edited clip.
- **Picking curves** drops the selected keys of the curves no longer shown, so a drag can't move keys you can't see.
- Slopes are kept to +-1e6 (a steeper slope draws as a vertical line anyway); the value axis zooms between about
  1e-4 (or 1e-5 of the value) and 1e7.
- Tests: round 85 (four tests).
