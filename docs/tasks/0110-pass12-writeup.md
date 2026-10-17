# 0110: Performance pass 12 written up

- **Status:** done
- **Requirements:** Q-04
- **Decisions:** none (ADR 0040 covers the pass)
- **Model:** main session (Opus)
- **PR:** (this task's)

## Goal
Pass 12 (tasks 0107-0109) answered "Look for performance gains in edit mode; compare to how blender implements their
edit mode." Write it up: the targets met and missed, what it cost, which of Blender's mechanisms were adopted and
which are left, and what comes next.

## Scope
- In: PERFORMANCE's pass 12 (one table, before and after each task, both paths; targets; costs; Blender's mechanisms;
  next candidates); README's modeling paragraph; CHANGELOG.
- Out: code.

## Acceptance criteria
1. Every number in the write-up comes from the measured rows (`--laptop --quality low --only editor_render`).
2. Each plan target is named as met or missed.

## As built
- Both targets met: no overlap-check stall on 50k faces (0.2-0.6 ms of main-thread time a row); entering Edit Mode in
  20 ms with the views on the GPU and in about one frame on the CPU (39 ms against the 32 ms of entering again).
- Left for later, with numbers: operators' own cost (the extrude's ~95 ms), selection drawing the view again
  (55-68 ms), picking Edit Mode elements on the GPU, chunk-deduplicated undo.
