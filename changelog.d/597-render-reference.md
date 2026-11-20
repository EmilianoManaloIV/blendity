## 2026-11-19 (round 597): Measure distant toon detail before editor notifications

**Fixed (task 0579):** The hatched plane, face-shadow sphere and box-mapped cube now use the cached Scene view
for both captures in the near and far comparisons. A warning toast could previously change the sampled colors
and extend the object mask.

**Checked:** The regression compares the composited and cached captures with a visible toast at both distances
and checks that the measured energy responds to a rendered checker. Sanitizer shard 4 passed all 49,030 checks. The toast added 10,586 pixels to each composited mask,
while the cached colors and coverage stayed identical. Both Windows configurations passed render and editor.
