# 0132: Automatic walking and AI inbetweening written up

- **Status:** done
- **Requirements:** R-13
- **Decisions:** ADRs 0045, 0046
- **Model:** main session (Opus)
- **PR:** (with tasks 0128-0131)

## Goal
The new tools explained where the animator looks: the Learn tab, the README and the design documents.

## Scope
- In: Learn:
  - "Automatic walking: which tool when": Walk Cycle, Footsteps and Locomotion, and when to use each;
  - "AI inbetweening: the motion between two keys": switching it on, its download, a span, its style, Variation,
    Apply, the stale ghost, Fit to Clips;
  - each with a Try-it and questions, and the earlier lessons for Walk Cycle, Footsteps and Locomotion.
  - Each says hand animation is unchanged with the assistants off.
- In: README (features, unit-check count), ARCHITECTURE (the AI module), USABILITY, CHANGELOG.

## Acceptance criteria
1. Both lessons' Try-its run: the walking tools switched on; the AI span asked for (and, without the download,
   refused with how to get it).

## As built
- Tests: round 152.
