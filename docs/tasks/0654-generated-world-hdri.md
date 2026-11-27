# 0654: Render generated grids selected for the World HDRI

- **Status:** approved
- **Authorization:** The Hub assigned this owner-approved Pass 38 follow-up as round 672 on `a0cd6307`, conditional on independent confirmation of task 0652's disk control. The verifier confirmed the generated-image defect and working disk control on `d79d27a7`.
- **Requirements:** R-05, Q-06.
- **Decisions:** none needed. Retain the established World environment and file color-space conventions.
- **Implementation:** project implementer with independent test and review.
- **PR:** local Pass 38 stack, pending publication.

## Goal
Selecting a generated UV Grid or Color Grid in the World HDRI picker should light and render the scene using that image. Rotating the environment should rotate the image.

## Scope
Use the existing generated-image evaluation path for the World environment. Check consumers of the shared environment, including views and final rendering. Preserve the existing behavior and cost of ordinary disk HDRIs, missing files, empty slots and previously saved scenes. Record any other distinct reference types found in review.

## Likely files
`src/editor/render_view.cpp`, the established texture reference resolver if needed, and rendering regressions.

## Acceptance criteria
1. Both offered generated grids produce meaningful, distinguishable images. Native editor save and reopen retain the selected reference. Rotation changes the rendered orientation.
2. Compare ordinary disk HDRI, its rotation and invalid-file and empty selections with the stock control. Verify disk render parity and measure comparable disk workload cases under the Hub's timing lock.
3. Independently verify native behavior and review the corrected source. Include the test and review receipts before handoff.

## Tests and checks to run
Use the visible 0652 picker, the asymmetric disk image and the same isolated project layout. Cover generated references, saved legacy selections, valid and missing files, both raster and traced environment consumption, and the affected rendering and editor tests through the Hub.

## Risks and edge cases
The material resolver normally interprets color textures as sRGB, while a disk World HDRI currently uses linear input. Generated grids have no backing file. A cached failed environment must not conceal a later valid selection.

## Documentation to update
Round 672 changelog fragment, Learn help if needed and this outcome.

## Outcome
The independent baseline showed zero changed pixels and a file-read warning for the offered UV Grid. A disk image changed all 57,600 pixels and rotating it by 45 degrees changed 51,233. [Baseline audit](D:/Blendity-agent-scratch/pass38/0652-independent/hdri-audit.json)
