# 0005: Tool settings live in the F9 panel and drag helpers

- **Status:** accepted (2026-10-09, round 21)
- **Requirements:** R-07, R-08

## Context
The Inspector's Edit Mode area had grown a field for every tool's numbers (shell thickness, draft angle,
extrude distance, bevel width, array count...). The user asked for each tool's settings to appear when
the tool is used, as Blender's Adjust Last Operation and modal helpers do.

## Decision
- Tools that take numbers are adjustable operations: `edit_op_redoable` + `LastOp` + `run_last_op`, with
  fields in the F9 panel. Tools that benefit from it also get a drag helper (`modal_begin`): mouse for the
  main amount, wheel for a count, keys for axes, typed numbers, Enter / Esc.
- Values changed in F9 or the helper become the tool's defaults (`remember_last_op_settings`).
- The Inspector keeps only switches that change how tools behave (Each Face, Individual, Auto Fuse, Auto
  Smooth, Sharp Angle).

## Consequences
- New tools with numbers must follow this pattern; reviewers reject new tool fields in the Inspector.
- Scripts and tests set values through the console (`hs ...`, `bevelprofile`, `editop`) rather than the UI.
