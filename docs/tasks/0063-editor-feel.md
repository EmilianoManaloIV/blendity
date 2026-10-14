# 0063: Switch the editor's feel: Unity, Unreal, Blender or Maya keys and navigation

- **Status:** done
- **Requirements:** R-01 (Unity-style editor), R-07 (usability)
- **Decisions:** none new (keymap presets are ADR-less data; navigation styles extend the Scene view's input)
- **Model:** main session (Opus)
- **PR:** https://github.com/EmilianoManaloIV/blendity/pull/60

## Goal
The user's later list (2026-10-12): "switch the editor's feel between Unity / Unreal / Blender / Maya". Asked what a
feel covers (2026-10-13), they chose **keys and mouse navigation only**. Window names, layouts and the up axis (Y) stay Unity's.

## Scope
- In:
  - **Editor Feel** (Edit > Editor Feel, Preferences > Keymap, console `feel unity|unreal|blender|maya`, saved in
    the prefs). It sets the keymap preset and the Scene view's mouse navigation together. The keymap preset dropdown
    stays for keys alone (3ds Max and SketchUp keep Unity's navigation).
  - **A new Unreal keymap preset**:
    - Unity's keys (W/E/R, F, Q, Ctrl+D), with Unreal's own: Alt+P plays (Play In Editor), Ctrl+W duplicates too,
      End drops the selection to the floor, Alt+G / Alt+H / Alt+J / Alt+K for perspective, front, top and side.
    - Where an action doesn't exist, it is left out.
  - **Mouse navigation** in the Scene view:
    - **Unity** (as now): RMB drag looks and WASD/QE fly; Alt+LMB orbits; Alt+Ctrl+LMB or MMB pans; Alt+RMB zooms;
      the wheel dollies.
    - **Unreal**: RMB drag looks and WASD/QE fly; MMB pans; Alt+LMB orbits; Alt+RMB dollies; Alt+MMB pans; LMB+RMB
      held together pans; the wheel dollies.
    - **Blender**: MMB orbits; Shift+MMB pans; Ctrl+MMB zooms; the wheel zooms. RMB doesn't fly and Alt+LMB doesn't
      orbit (in Blender those are menus and loop select).
    - **Maya**: Alt+LMB tumbles; Alt+MMB tracks; Alt+RMB dollies; the wheel dollies. RMB alone doesn't fly (in Maya it
      is the marking menu).
  - The Scene view's navigation hints (tooltips, Learn) name the feel's buttons.
  - A Learn section.
- Out: window names, layouts, the up axis, Unreal's LMB-drag-on-empty camera move (it would replace box selection;
  noted in As built), Blender's fly/walk modes, Maya's marking menus.

## Acceptance criteria
1. `feel blender` binds G/R/S and makes MMB drag orbit, Shift+MMB pan; RMB drag doesn't move the camera.
2. `feel maya` makes Alt+LMB orbit and Alt+RMB dolly; RMB alone doesn't fly.
3. `feel unreal` binds Alt+P to Play and End to drop to floor (if present); RMB flies; LMB+RMB pans.
4. `feel unity` restores today's behaviour exactly; the feel survives a restart (prefs).

## Tests and checks to run
- Unit tests (round 87) with real mouse events per feel; full suite, verify mode, Linux, sanitizer; stress `editor_fuzz`.

## As built
- **Your own key changes stay** when the feel changes (a keymap *preset* choice still starts afresh, as before).
- **Blender**: Alt+click selects a loop and Ctrl+Alt+click a ring on release, without moving the view. RMB does
  nothing in the Scene view: Blendity has no RMB menu there yet. Blender's fly/walk mode (Shift+`) isn't built.
- **Maya**: plain MMB does nothing (Maya's manipulator drag); Alt+Ctrl+LMB orbits (Maya's dolly-to-region isn't
  built).
- **Unreal**: LMB+RMB pans whichever button goes down first; letting go of LMB goes back to looking round. Unreal's
  LMB drag on empty space (move and turn the camera) isn't built: LMB drag stays box selection. End drops the whole
  object onto y = 0, not onto whatever is below it.
- **Hints**: the Preferences tooltip, Learn and the MANUAL describe each feel. The Scene view's own tooltips (and the
  camera Pilot's "fly") still describe Unity's navigation.
- Tests: round 87 (three tests).
