## 2026-11-19 (round 597): Measure distant toon detail before editor notifications

**Fixed (task 0579):** The hatched plane, face-shadow sphere and box-mapped cube now use the cached Scene view
for both captures in the near and far comparisons. A warning toast could previously change the sampled colors
and extend the object mask.

**Checked:** The regression compares the composited and cached captures with a visible toast at both distances
and checks that the measured energy responds to a rendered checker. Platform verification is pending.
