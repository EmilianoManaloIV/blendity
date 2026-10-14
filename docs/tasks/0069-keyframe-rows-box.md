# 0069: Keyframes - x/y/z grouped rows and box select that works

- **Status:** done
- **Requirements:** R-06 (rigging and animation)
- **Model:** main session (Opus)
- **PR:** https://github.com/EmilianoManaloIV/blendity/pull/62

## Goal
The user (2026-10-14): "Add the ability to select multiple keyframes in the animation block (drag select). Feel free
to simplify keyframes into tuples such as having x,y,z in the same frame and then expand as needed."

## Scope
- In: the dopesheet shows a vector property's axes as one row ("Lid : Position", Unity's) with a fold arrow; its
  diamond stands for all its axes' keys on that frame (select, move, delete, copy act on all); unfolded, each axis has
  a row; box select takes every diamond the box touches (not only rows whose middle it crosses), lights them while
  dragging, Shift adds and Ctrl takes away; selected keys show where they will land while dragged.
- Out: edge scrolling while box selecting.

## As built
- Groups are a display mapping: the file still holds one track per axis. The Curves view lists every track.
- Double-click on a group row keys all its axes.
- Tests: round 93.
