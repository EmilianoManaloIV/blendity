# 0106: Performance pass 11 written up

- **Status:** done
- **Requirements:** R-05, Q-01
- **Decisions:** ADR 0026 (the minimum machine) gains a note
- **Model:** main session (Opus)
- **PR:** (link when opened)

## Goal
Pass 11 (tasks 0098-0105) answered "What should be more optimization gains?" in the four areas the user chose. Write it
up: before and after on both paths, the targets met and missed, and what comes next.

## Scope
- In: PERFORMANCE's pass 11 summary; ADR 0026's note on the laptop setting's Edit Mode target; README's feature
  paragraph; CHANGELOG.
- Out: new measurements beyond the tasks' own (each task's numbers stand as measured).

## As built
- The summary table opens PERFORMANCE's pass 11 section; each task's section follows it.
- ADR 0026: the 50k-face Edit Mode drag on the laptop setting now meets its 30 fps target on the CPU path (21.5 ms,
  about 30 ms at the laptop's sustained clock) and on the GPU path (11.5-12.3 ms).
