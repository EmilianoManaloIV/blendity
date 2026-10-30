# The screenshot review (task 0293, round 313)

The live sweep (`tests/test_hci.cpp`, `hci: the live sweep`) saved a picture of each screen at 1366 x 768, Dark, UI
scale 1: every layout, every window opened in the Default layout, every top menu, every dialog. That is 66 pictures
(the submenus and the component Inspectors were checked by the sweep but not pictured). Each was looked at against
Nielsen's ten heuristics and the conventions of task 0292.

Severity follows Nielsen's scale: 0 not a problem, 1 cosmetic, 2 minor, 3 major, 4 catastrophe. Every finding of 2 or
more is fixed, each with a check in `tests/test_hci.cpp`'s second test.

## Findings

| # | Screen | Heuristic | Sev. | Finding | Fix |
|---|---|---|---|---|---|
| 1 | Editing window in the side pane | Aesthetic and minimalist design; visibility | 3 | The picture and the panel were side by side even at 222 px: the hint was cut on both sides ("der, Open Image, or drop an in") and the panel's four buttons were 4 px slivers. | Narrower than 460 px the picture goes above the panel; the buttons go two by two; the hint has a short form ("Open or drop an image"). |
| 2 | Live Link in the side pane | Visibility; recognition | 2 | Install a Bridge's four buttons cut mid-word ("Unity Pro", "Godot Pr"). | Two by two in a side pane, each with a tooltip saying what it installs where. |
| 3 | Lighting, Modeling Tools, Layers, the Inspector at 1024 px or x1.5 | Visibility; consistency | 2 | Any button whose label didn't fit was cut mid-letter, or drawn past its edge when disabled. | Every button's label that doesn't fit ends in "..." and its tooltip begins with the whole label (`ui::Context::elide`). The disabled path clips too. |
| 4 | Profiler | Aesthetic design | 2 | The "30 FPS" grid label was drawn over the graph's title. | The labels stay inside the graph. |
| 5 | Preferences | Help users recognize; visibility | 2 | "Running without them: ." when there was no reason to give; the AI row said "not installed (ai install)", a console command. | "Running without them."; "not installed (Install...)". |
| 6 | Every screen, Dark theme | Accessibility (WCAG 2.1 1.4.3) | 2 | Dim text (hints, secondary labels) was 3.5:1 on the panel. | 0xA3A3A3: 4.6:1. Every theme's dim text is now checked at 4.5:1 or more. |
| 7 | UV editor at 1024 x 640 and at x1.5 | Consistency; error prevention | 3 | The toolbar's right-hand half (Parts, materials, background, Stretch) sat at fixed offsets from the right. In a narrow window it ran over the tools on its left and past the window over the Scene view's Overlays button, so a click could land on the wrong one. The second row's exact values did the same over Sync and Island. | Narrower than the two halves, each right-hand half gets a row of its own, wrapping again if needed; the picture starts below them. |
| 8 | Sequencer in the side pane | Visibility | 2 | The sequence menu ran past the window ("(no s"). | It narrows to the room left (80 px at least). |
| 9 | Learn | Visibility | 1 | A long Try It button's text was cut ("...smooth it with Taubin's fil"). | Elided by fix 3; the whole text is its tooltip. |
| 10 | Buttons the sweep found without a word of help | Help and documentation | 1 | Enter / Exit Edit Mode (the Inspector), the UV editor's Rotate / Scale / Move, Hide Guides and Clear (both places), Live Link's Start, the Sequencer's Create, Scatter's Merge Instances, the Shape Keys' Remove..., a layout guide's window steps. | Tooltips. |
| 11 | Layers window | Visibility of state | 1 | The layer's opacity slider shows no number. | Left: the slider's position reads well enough, and a number fits poorly at 222 px. |
| 13 | Preferences on Linux | Visibility of state | 1 | Where MoonRay is found, its "Your own MoonRay" row appears a moment after Preferences opens, and every row below moves down one (a click aimed a moment before lands on the row above). Found by the Linux run of task 0288's test. | Left (cosmetic, once a session); the tests wait for a control to stay put. |
| 12 | Every screen at UI scale 1 | Fitts's law | 1 | About 95 % of targets are under 7 mm at scale 1, and about 0.3 % at 1.5. This is the Fitts audit's known finding (task 0262). | Left: Tablet Mode and UI Scale 1.5 are the answer for touch and pen. The sweep prints the count per configuration so it stays visible. |

## Left, with reasons (the sweep's baseline, `tools/hci_sweep_baseline.json`)
- **15 elided labels:** in narrow panes or at x1.5, such as "Edit Origin with Handles..." in an 82 px Inspector column. They end in "..." with the whole label as their tooltip.
- **16 "no tooltip"** entries are rows, not buttons:
  - the Profiler's stat rows ("Vertex stage", "Total 3D") and the Console's line numbers;
  - two buttons whose tooltip shows only on hover, by design: Render Animation looks up FFmpeg only when hovered;
  - the Textures window's Materials / Textures tabs.
- **3 overlaps** are a control on its own row: a help "?" a pixel past its row's edge, and a "+1" on a section header at x1.5.

## Numbers
- **The sweep:** 826 screens in 6 configurations, about 4 seconds:
  - before the fixes, 387 clipped labels, 369 overlaps and 125 controls without help, most of them the sweep's own false positives (internal ids, one control under two names);
  - after, 15 elided labels, 16 controls without a tooltip and 3 overlaps, none of them a fault (listed above);
  - 0 popups outside the window, 0 disabled buttons without a reason.
- **Contrast (text / dim text on the panel):** Dark 7.8 / 4.6 (dim was 3.5), Light 11.4 / 4.8, Emiliano Light 12.6 / 4.6, Emiliano Dark 12.6 / 5.1, High Contrast 19.0 / 11.4.
- **The other audits:** console 1, consistency 0, labels 0.
