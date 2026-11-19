# 0154 Import installed Steam Workshop assets into native projects

- **Status:** accepted
- **Date:** 2026-11-19
- **Requirements:** R-6 (import), R-13 (animation)
- **Task:** [0582 Workshop import pass](../tasks/0582-workshop-import.md)

## Context
The owner wants to download characters and maps through Steam, pose them and render them in Blendity. Model and map dependencies can span an item, its game and other mounted items. Classic Source and Source 2 store their resources differently.

## Decision
Steam handles acquisition. Blendity reads user-selected local content through one ordered resolver for loose files and archives. Separate classic and Source 2 readers produce native project assets. Preserve shared mesh instances, bind poses and project-relative material dependencies. Present unresolved paths to the user so a missing mount can be supplied. Treat importing as an explicit snapshot and preserve user material edits on reimport.

## Consequences
Projects can reopen after the Workshop item moves or updates. Conversion uses project storage and requires per-title compatibility tests. Identify unsupported versions and missing dependencies. Record provenance separately for upstream design references, redistributable fixtures and verified title assets.

## References
- [Steam Workshop installation](https://partner.steamgames.com/doc/features/workshop/implementation#ItemInstallation)
- [SourceIO format coverage](https://github.com/REDxEYE/SourceIO#supported-formats-v530)
- [Blendity imported characters](0047-imported-characters.md) and [native import result](../../src/scene/import.h)
