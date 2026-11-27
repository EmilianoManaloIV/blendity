## 2026-11-27 (round 670): Visible lighting and sky controls

**Asked:** Exercise the visible controls that set World lighting, lightmapping and individual lights, including both new Sky sun angles.

**Added (task 0652)**
- A stock Windows replay checks the Lighting window and Light Inspector with mouse presses, drags and contrasting missed hits. It saves the scene, verifies Undo, reopens the edited file and checks applicable rendered effects.

**Checked:** Across 87 keys, edits and Undo restored the expected property values. Opening each disclosure exposed an editable child, and closing it restored the original label pixels. Both physical Sky angles matched the explicit reference render at each orientation and gave an unchanged picture under a directional lamp. A separate driver checked both angles and Spot Range with opposite gestures and fresh-process reopen.

<!-- docs/PERFORMANCE.md -->
append:
Task 0652 records [visible Lighting and World field checks](perf/0652-lighting-inspector.md), including the generated HDRI picker failure and its working disk control.
