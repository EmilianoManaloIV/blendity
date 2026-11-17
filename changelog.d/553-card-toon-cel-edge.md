## 2026-11-17 (round 553): Toon at a distance counts the sphere, not its shadow

**Asked:** round 551 held "toon at a distance"'s card half to a 40th: the card's cel edge at 20 m looked narrower than
the CPU's (9 antialiased pixels against 17). Make the card's edge the CPU's and put the 20th back (task 0534).

**Found (task 0534)**
- The card's edge was never narrower. The 20 m pictures, dumped from both renderers, show the sphere's cel edge in the
  same places on both: 8 mid-tone pixels on the CPU, 7 on the card. The test's mask is every pixel that changes when the sphere moves away, and
  that includes its shadow on the floor at the view's bottom: 8 of the CPU's 17 "between" pixels were its shadow's edge,
  mid-grey on the CPU where the card's is dark. The 20th never held for the sphere on either renderer (8 of 243 on the
  CPU, 7 of 242 on the card - a 30th); the CPU met it only through its shadow.

**Changed**
- The test counts the sphere's own pixels (the largest 4-connected part of the mask) and measures the edge against
  the sphere's height in rows, not its area (an edge's length goes with the diameter; the sphere is 17 rows at 20 m): at least a fifth of the
  rows (4; measured 6-8), and the card's edge within 3 pixels of the CPU's. No product change: a card-side change
  tried first (the neighbour before where the next pixel is off the object) moved nothing and was reverted.
- `BLENDITY_GPU_DIFF_DIR` also writes this test's 20 m pictures and masks.

**Follow-up:** round 556 (task 0538), the card's contact-shadow edge on the floor at 20 m, darker and harder than the CPU's.

**Checked:** on the card (RTX 4070 SUPER), on #442's head: toon at a distance passes on both renderers (the sphere 17 rows at 20 m, its edge 8 pixels on the CPU, 6 on the card); the `gpu` group 1,155 checks with only round 508's Generate Lighting failing (a slow-machine flake round 555 fixes; it passes alone). Linux (dependency-free): render and gpu, 40,285 checks, 0 failed.
