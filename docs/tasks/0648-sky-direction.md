# 0648: Set the Sky world's sun elevation and rotation

- **Status:** approved for implementation
- **Authorization:** Owner-assigned Pass 37 Hub follow-up, round 666 on d7503d4e.
- **Requirements:** World lighting, rendering parity and editor accessibility.
- **Decisions:** preserve task 0522 and existing directional-light precedence.
- **Implementation:** project implementer.
- **PR:** Hub integration pending.

## Goal
Set the physical Sky world's sun direction from scene settings and the console and learn it in the editor.

## Scope
Physical Sky only, when no enabled directional light drives the sun. Preserve the default appearance of older scenes. Apply the same direction to the sky and its light.

## Likely files
EnvironmentSettings and reflection in src/scene, src/render/sky_sun.*, src/editor/render_view.cpp, Learn, affected tests and round fragment.

## Acceptance criteria
1. The Sky world exposes elevation and rotation, in degrees, through the Inspector and normal console entry point. They persist through save/reopen.
2. Existing scenes keep their old default direction. An active Directional Light keeps precedence. The derived direction reaches lighting and sky caches on edits.
3. A Learn example explains orientation and precedence. Tests cover default, edited angles, cache invalidation, saved settings and precedence.

## Tests and checks to run
Affected rendering/scene/editor groups in scratch, independently verified as part of the Hub chain.

## Risks and edge cases
Below-horizon elevation, wrapped azimuth, old settings with missing fields, a disabled light, Scene view and renderer reuse.

## Documentation to update
Round 666 changelog fragment and Learn lesson.

## Outcome
Pending.
