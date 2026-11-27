# 0648: Set the Sky world's sun elevation and rotation

- **Status:** in review
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
The initial review found that the new reflected fields changed saved lightmap keys for earlier scenes. The correction includes the fields in the persisted key only when the physical Sky uses valid custom angles without an overriding lamp. The regression opens Gradient and Sky scenes and lightmaps produced by the full-library Windows baseline at a3d6cc8c (same product and test sources as d7503d4e), verifies their saved keys, and exercises active and overridden angles. The baseline fixtures are under tests/fixtures/sky_sun_bakes. See the [generation receipt](C:/blab/pass37-hub/timing-queue/0648-functional-legacy-bake.result.json). Native candidate tests, final independent review and integration are pending.
