# Task 0293 - The HCI pass over every live feature (round 313)

The user (2026-10-29, /plan): "Do another HCI pass to test all current live features in the program; update anything
that we may have missed." Their choice: an automated sweep, then a review of a picture of every screen.

## What
- **The sweep:**
  - `tests/test_hci.cpp`, a new test group (`--group hci`), opens every place the artist can reach: every layout,
    every window (in the Default layout, as `Window >` opens it), every top menu and its submenus, every dialog kind,
    and the Inspector of each component on a fresh cube.
  - On each screen it reads the controls the frame drew (`record_buttons`) and checks:
    - a button's label wider than the button;
    - overlapping controls on the same layer (not one inside another, not one control under two names);
    - a popup or dialog outside the window;
    - a button without a tooltip, unless its label says it all (OK, Cancel, Close...);
    - a disabled button without a reason;
    - targets under 7 mm, counted and not judged.
  - It also checks every theme's text and dim text contrast on its panel.
  - The suite runs one configuration (1366 x 768, Dark, scale 1). `BLENDITY_SWEEP=full` runs six: three sizes, both
    themes, scales 1 and 1.5 - 826 screens in about 4 s.
  - `BLENDITY_SWEEP_OUT` writes the findings as JSON, and `BLENDITY_SWEEP_SHOTS` a PNG of each screen.
- **`tools/hci_sweep.py`:** the JSON as a report (`docs/hci/sweep-313.md`), with `--check` against
  `tools/hci_sweep_baseline.json` (distinct controls per kind).
- **The screenshot review:** `docs/hci/review-313.md`, with 12 findings rated on Nielsen's severity scale.
- **The fixes:**
  - **An elided label:** a button's label that doesn't fit ends in "..." and its tooltip begins with the whole label
    (`ui::Context::elide`; the caller's own tooltip follows it on one timer). Disabled buttons clip too.
  - **The Editing window:** in a narrow pane it stacks the picture above its panel, its buttons two by two, with a
    short hint.
  - **Install a Bridge:** two by two in a side pane, with a tooltip each.
  - **The UV editor:** its toolbar's right-hand halves get rows of their own when the window is narrow, wrapping
    again if needed. They had run over the tools and past the window.
  - **The Sequencer:** the sequence menu narrows to the room left.
  - **The Profiler:** the FPS labels sit inside the graph.
  - **Preferences:** "Running without them." with no reason; "not installed (Install...)" in place of a command name.
  - **The Dark theme:** dim text 0xA3A3A3, 4.6:1 (was 3.5).
  - **Tooltips** on the controls the sweep found without one.
- **Test hooks:**
  - `ui::Context::tooltip_texts`: each widget's tooltip by its rect, while recording;
  - `Editor::open_dialog_for_test`, `dialog_count_for_test`, `set_ui_scale_for_test`, `workspace_names_for_test`,
    `fb_width_for_test` / `fb_height_for_test` and `text_width_for_test`.

## Acceptance
- **`hci: the live sweep`:**
  - 0 popups outside the window and 0 disabled buttons without a reason;
  - every theme's text at 4.5:1 or more;
  - `py tools/hci_sweep.py <json> --check` at the baseline: 15 elided, 16 without a tooltip, 3 overlaps, all listed
    with their reason in review-313.md.
- **`hci: the sweep's fixes`:**
  - an elided button's tooltip carries its whole label;
  - the Editing window's buttons are whole and two by two in the side pane;
  - Install a Bridge's labels are whole;
  - the UV editor's right-hand controls stay inside a 395 px window;
  - every theme's dim text is at 4.5:1.
- The whole suite in the dependency-free non-LTCG build.
