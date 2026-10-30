# 0271: pickers, a Report dialog, refusals with a Fix, messages that name the interface

- **Status:** done (round 291)
- **Requirements:** R-14
- **Decisions:** ADR 0085 (console round)
- **Model:** main session
- **PR:** #228

## Asked
The user (2026-10-29): many editor features can only be reached by typing console commands; give them a way through
the interface, and look for related HCI problems. Two audits of the tree found about 75 console-only verbs, 30 messages
naming a command (23 where a button exists), 7 results shown only in the Console, and 34 refusals without a Fix.

## What changed
- **Pickers** shared by the later rounds (`src/editor/ui_pickers.cpp`): an image row (None, Browse..., the project's
  images sorted) and a path row (a typed field and a `...` button opening the system's file, save or folder dialog).
  A **folder dialog** joins the platform layer: `SHBrowseForFolderW` on Windows, `zenity --directory` or
  `kdialog --getexistingdirectory` on Linux, `NSOpenPanel` on macOS. With no dialog the row says so and keeps the field.
- **A Report dialog** shows what an operation printed, with Copy and Close. `report <command>` runs a command and opens
  it; the palette's UV Report and Scene Statistics use it instead of sending the artist to the Console.
- **Refusals with a Fix**: the nineteen "stop Play first" refusals get Fix: Stop Play; a busy Mesh Maps bake, a remesh
  and an AI fine-tune get their Cancel; missing prerequisites get the window or setting that provides them (the
  assistant on, Preferences, Bake Mesh Maps, Apply Modifiers, the Lighting, Rigging, Modeling Tools, Textures and
  Animation windows, realtime GI on).
- **Messages name the interface**: the messages and hints that named a command name the menu, window or button instead.
- **Toasts outside the Scene view**: when a layout hides the Scene view (Play in a full Game view, the Texturing layout
  on a small window), toasts and their Fix buttons are drawn and clicked over the dock area.
- **`tools/hci_console_audit.py`** lists interface strings that name a console command; `--check` fails above
  `tools/hci_console_baseline.txt` (26 at round 291, 19 after rounds 292-293).

- **Export Textures' folder** is the first path field with its `...` button (the folder dialog); a click away from the
  field keeps the typed text without exporting (before, a click on Cancel while typing exported).
- The folder dialog is Windows' `IFileOpenDialog` with `FOS_PICKFOLDERS`, the older chooser only when that one can't be
  made.

## Tests (`tests_round291`, `tests_round293`'s second test)
`report uv report` opens the dialog with the report's lines and Copy fills the clipboard; a refusal in Play shows Fix:
Stop Play and the click stops Play; Fix: Turn Realtime GI on and Fix: Cancel the bake do what they say; Browse... with no
file dialog says how else. After the review: Export Textures' folder typed by keys, its `...` saying there is no dialog
here (headless) with the dialog left open; Fix: Stop Play clicked while the Game tab hides the Scene view.

## Not done
- The audit's baseline is lowered by the later rounds; strings left are the Console's own replies and developer hooks.
