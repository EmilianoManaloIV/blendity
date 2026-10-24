# 0218: Tablet Mode follows the laptop

- **Status:** done (round 238)
- **Requirements:** R-14
- **Decisions:** ADR 0062 (task 0213)
- **Model:** main session
- **PR:** #184

## Asked
The plan for a 2-in-1, the user's choice: "Tablet Mode turns itself on when the laptop folds into tablet posture, with
an override."

## What changed
- **The posture:** `platform::tablet_posture()` reads Windows' slate mode (the keyboard folded back or detached). It
  trusts it only where it means something: a touch screen, not docked, on a mobile or slate power role. Linux and
  macOS report none. A fold sends Windows' settings change, which wakes a frame at once.
- **The preference:** Preferences > Pen and touch > Tablet Mode, or `tabletmode auto|on|off`.
  - **Auto** (the default) turns the main window's Tablet Mode on when the laptop folds and off when the keyboard is
    back. It acts on a change only, never on the state the editor starts in, so a machine that never folds - or only
    reads as folded - is never touched.
  - **On / Off** set it when chosen and at the start.
- **The tab menu's Tablet Mode** still switches it by hand, and that choice holds until the posture changes again.
- **The status bar** says when it follows a fold: "Tablet Mode on: the keyboard folded away" / "off: the keyboard is
  back".

## Checked
- tests/test_input.cpp `tests_round238`:
  - started folded, Auto changes nothing;
  - a fold turns Tablet Mode on and an unfold off;
  - the tab menu's Off holds until the next fold;
  - Off ignores a fold, On turns it on with the keyboard there, and the choice is saved.
- Not tried on a convertible; task 0222's check list covers it.
- An Opus review found two things, both fixed:
  - Windows' slate reading is wrong on many machines without a convertible's hinge, so Auto would have turned Tablet
    Mode on for ordinary desktops and laptops at startup. The posture is now trusted only with a touch screen,
    undocked, on a mobile or slate power role (as Chromium checks it), and Auto acts only on a change, never on the
    state it starts in;
  - a posture change took effect after the frame was drawn, leaving the old size on screen against the new targets;
    the window is drawn again at once.
