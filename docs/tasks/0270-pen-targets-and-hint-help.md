# 0270: the pen's targets at 7 mm in Tablet Mode, asserted; the hint's help

- **Status:** done (round 290)
- **Requirements:** R-14
- **Decisions:** ADR 0085 (F8)
- **Model:** main session
- **PR:** #227

## Asked
The follow-ups of round 282: the plan's assertion "no pen-primary target under 7 mm in Tablet Mode" was never written,
and the status hint had no way to the documentation of what is under way.

## What changed
- **The pen's targets** - the Scene view's navigation buttons, a modal's step buttons, the job tray's Cancel and a
  toast's Fix - are 7 mm or more in Tablet Mode: the status bar and the toasts are at least 34 px (UI scale 1) tall
  there.
- **The hint's ?**: a button beside the status hint opens the Learn lesson for the hint row under way (Sculpting while
  sculpting, UV Mapping in the UV editor, the shortcut sheet otherwise).

## Tests (`tests_round290`)
In Tablet Mode at 1.5x the six targets are printed with their sizes and asserted at 39.7 px or more; the ? while
sculpting opens Sculpting.

## Not done
- The rest of the interface (panel rows, toolbar buttons) stays under 7 mm at 1.5x (`docs/hci/fitts-282.md`): the pen's
  targets in the Scene view are the ones asserted.
