# Task 0370: the HCI rerun after the fixes (round 393)

## Asked
Phase D's last round (plan cozy-fluttering-corbato): all seven productions (A-C and the four Adobe ones) measured again
after the fixes, with before and after columns; the comparison document, its page (the same link) and the KLM table
updated; any finding still open goes to the next plan.

## Built
- **The rerun** (`docs/hci/compare-2026-10.md`, "After the fixes"): Blendity built on 2026-11-02 from rounds 351-392
  with Blender's libraries, each stage from an empty scratch project. A, B and C ran their round-350 command lists
  unchanged; E-H ran lists rewritten with the verbs rounds 388-392 added (`scratchpad/hci_D_after.json` to
  `hci_G_after.json`); the editing production read a short MP4 and WAV made for it.
- **What came out:** refusals per production before and after; the avatar measured again (10 shape keys and 3
  textures in the file, where there were none; the export waited for the remesh); every finding's status; one new
  finding, 30 (`set Render.Width` / `Height` can't reach the render size: severity 1, for the next plan).
- **The KLM table:** Blendity's cells for rows 11, 13, 14, 15, 18, 19 and 20 (tasks it couldn't do) and row 16 (Bake All
  Maps); `tools/hci_klm.py --compare --check` recounts them. Blendity now does all twenty tasks.
- **The page** at the same link gains the rerun as its first section.
- **Two bugs the rerun found, fixed:**
  - a new paper lists no layer (its strokes are layer 0's) and its SVG export wrote none - it exports that implicit
    layer now;
  - console coordinates on a paper before the Draw tab's pad had been drawn read each pixel as a whole view height (an
    artboard a thousand times too large, Restyle's selection missing) - `sketch_command_view` uses the Scene view's size
    until the pad has one, and Select and Free Transform's console verbs share it.

## Checked
- The seven productions' logs: A 2 refusals (clicks the console can't place), B 5 (one by design with its Fix, one new
  finding, one click), C 0, E 0, F 1 (an ID mask before a background bake landed, headless only), G 0, H 0.
- The avatar's file by `hci_glb.py`; the SVG exported by G read back in.
- The KLM check; tests_sketch: a paper's implicit layer exported and read back, a console shape on a fresh paper inside
  the view.
