# 0655: Render procedural texture outputs selected for the World HDRI

- **Status:** in review
- **Authorization:** Owner-approved Pass 38 follow-up, round 673. The Hub reserved 0655 conditional on a stock picker reproduction. It depends on 0654 at `e10a836b`.
- **Requirements:** R-05, Q-06.
- **Decisions:** retain the existing shared procedural texture evaluation and worker contract.
- **Implementation:** project implementer with independent test and review.
- **PR:** local Pass 38 stack, pending publication.

## Goal
Use the selected procedural texture output in the World environment and refresh the rendered background and illumination when that texture changes.

## Scope
Resolve a real project `.ptex#output` reference offered by the World picker. Share resolved material outputs and their publication/invalidation semantics. Preserve ordinary disk, generated grid and legacy references. Keep unrelated registry routes outside this change.

## Likely files
World environment resolution, lighting regression tests, Learn help and the round 673 changelog fragment.

## Acceptance criteria
1. Select a Bricks albedo output in the visible World HDRI picker. Confirm the reference, a meaningful visible render and the same output after reopening.
2. Edit the source asset with the same World reference selected. Verify settled viewport/final output reflects the edit, Undo restores its original appearance and alternate outputs remain distinguishable.
3. Check the shared texture evaluation contract, including worker publication for surfaces, and preserve the disk and generated-grid controls from 0654. Obtain independent verification and source review.

## Tests and checks to run
Replay the native stock case on the candidate and exercise the affected rendering and procedural texture tests with the Hub. Use a separate private project for every run.

## Risks and edge cases
The asset can change without a new reference string. A surface can show its previous output while an asynchronous evaluation is pending. Final jobs must own their environment snapshot. A missing asset or a name containing a literal # can be a disk image.

## Documentation to update
Round 673 changelog, lighting help and verified results in this task.

## Outcome
Stock retained `Assets/Textures/A0655.ptex#albedo` across reopen. Editing the brick rows from 4 to 12 changed the picker thumbnail in 1,161 RGB channels. The selected, edited and reopened F12 renders all matched the empty slot in all 57,600 pixels. The editor logged a file-read failure for the output name. [Stock evidence](D:/Blendity-agent-scratch/pass38/0652/ptex1/results.json)

The final Windows picker rendered the Bricks selection in all 57,600 F12 pixels that previously matched the empty environment. Editing Rows at the same reference changed 56,123 F12 pixels and 687,574 Scene view pixels. Undo, Redo, an unchanged repeat and a fresh-process reopen matched their expected images exactly. The normal output, a disk image with a literal # and the generated UV Grid also rendered. Missing assets and unknown output names retained the empty fallback and a diagnostic. [Native picker receipt](D:/Blendity-agent-scratch/pass38/0655-independent/final-picker/receipt.json)

Product revision `4f6766a38a1fd498e7e0f92cbe2b128ec1a7d7ec` freezes the selected full-resolution output for renders and bakes. Pending final jobs own their procedural source and paint images. The bake key includes painted layer contents across groups and masks, while runtime caches observe registry replacement. A background surface job checks its originating asset before publishing. The source reviewer approved the same-stamp paint and same-path replacement cases. [Source review](C:/blab/pass38-hub/0655-review.md)

Independent native verification passed on Windows and Linux. The original test looked for the saved bake relative to the post-Undo editor location. With the assertion anchored to the named saved scene, the cold and warm final render and bake checks passed 30/30 on each platform. Both platforms also passed the same-path replacement, late worker, painted content and literal disk filename cases. [Independent verification](C:/blab/pass38-hub/0655-test.md)
