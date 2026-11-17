# 0531: The card's pictures against the CPU's - three tests that fail on a real card

- **Status:** in progress
- **Requirements:** the hub's round 551 (Pass 27); found by the HCI worker and round 542's card runs
- **Decisions:** none needed (a test's bound or a renderer's fix, each with its measured reason)
- **Model:** main session (Opus)
- **PR:** (link when opened)

## Goal
Three `gpu` tests fail on this machine's card (an RTX 4070 SUPER, its driver) and never on CI, which has no card - the
same way on round 476's build, in fresh folders, run alone:
1. `test_gpu.cpp` "textured materials draw on the card as the CPU draws them": a plane at a grazing angle (mips) mean
   1.025 / 255 against < 1.0, and a normal map 98.01 % within 3 against 99 %.
2. "the Scene view's overlays": the lines mean 1.055, 96.65 % within 3 (bound < 1.0, 99 %).
3. "a probe-lit ball beside a bright Baked lamp under Lanczos": mean 0.100 against <= 0.1.

For each: find where the pixels differ and why. When the card is wrong, fix the card's shader; when the difference
is legitimate (floating-point order, a rounding the two can't share), set the bound from the measurement with the
reason written in the test. Never a looser bound without a measured reason (CLAUDE.md).

## Scope
- In: the three tests, and the shader code their differences point at (`src/render/gpu_raster.cpp` texture
  sampling, normal mapping, overlay lines; the Lanczos resolve).
- Out: other card tests; the CPU renderer's own behaviour unless the CPU is the wrong one.

## Acceptance criteria
1. Each of the three tests passes on this card, run alone and in the `gpu` group.
2. Each change is either a product fix (the card's picture nearer the CPU's, measured before and after) or a bound
   whose comment names the measured cause and size.
3. The `gpu` group shows no new failure; the tests still skip cleanly without a card (CI).

## Tests and checks to run
- `--group gpu` in a CARD window from the hub, before and after; the full suite on Windows (libraries) and Linux.

## Risks
- A bound set from one card may still fail on another: bounds carry headroom stated in the comment.

## Documentation to update
- `changelog.d/551-card-cpu-picture-tolerances.md`.
