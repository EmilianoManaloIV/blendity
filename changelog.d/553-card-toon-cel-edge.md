## 2026-11-17 (round 553): Toon at a distance counts the sphere, not its shadow

**Asked:** round 551 held "toon at a distance"'s card half to a 40th: the card's cel edge at 20 m looked narrower than
the CPU's (9 antialiased pixels against 17). Make the card's edge the CPU's and put the 20th back (task 0534).

**Found (task 0534)**
- The card's edge was never narrower. The 20 m pictures, dumped from both renderers, show the sphere's cel edge as the
  same 9 mid-tone pixels in the same places. The test's mask is every pixel that changes when the sphere moves away, and
  that includes its shadow on the floor at the view's bottom: 8 of the CPU's 17 "between" pixels were its shadow's edge,
  mid-grey on the CPU where the card's is dark. The 20th never held for the sphere on either renderer (9 of about 270,
  a 30th); the CPU met it only through its shadow.

**Changed**
- The test counts the sphere's own pixels (the largest 4-connected part of the mask), holds both renderers to a 40th
  (8 edge pixels or fewer fail) and the card's edge within 3 pixels of the CPU's. No product change: a card-side change
  tried first (the neighbour before where the next pixel is off the object) moved nothing and was reverted.
- `BLENDITY_GPU_DIFF_DIR` also writes this test's 20 m pictures and masks.

**Follow-up:** round 556 (task 0538), the card's contact-shadow edge on the floor at 20 m, darker and harder than the CPU's.

**Checked:** CHECKS_W
