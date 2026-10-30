# Task 0292 - Interface conventions, checked (round 312)

The user (2026-10-29): "Finish all open tasks and maintain HCI consistency." The conventions the interface already
mostly followed are written down (docs/USABILITY.md, "Interface conventions") and checked by a new audit,
`tools/hci_consistency_audit.py`. Its baseline (`tools/hci_consistency_baseline.txt`) is 0.

## The conventions
1. **"..." means "asks before it acts".** A menu item or button that opens a dialog asking for a file, a name, numbers
   or a choice ends with "..." (the platforms' rule). Dialogs that only show something (About, a Report) do not.
   Fixed: "Welcome: Where Are You Coming From...".
2. **Title Case on buttons, menu items and submenus.** The short joining words stay lowercase (a, of, the, to, along...).
   Reply words in dialogs ("Not now", "Got it", "Skip this one") stay as said. Labels with a colon, a number or a
   bracket are left alone.
   Fixed:
   - "Apply Now", "Live Modifier";
   - "Mark Complete & Continue";
   - "Open with System Viewer" (two places);
   - "Install This Version".
3. **"Color" in what the artist reads** - Unity's and Blender's word, and the Inspector's fields ("Sky Color", "Fog
   Color"). 38 tooltips, help lines and labels changed, among them "Pen Color" (round 306). The Learn lessons and the
   docs keep the house's British prose.
4. **A tooltip is a sentence**: it ends with ".", "?" or "!" (before a closing bracket if there is one). Fixed: the
   Editing window's Move up / Move down and two sketch layer tooltips.
5. **No command names in visible text** - `tools/hci_console_audit.py`, at 1 (a UI file's shortcuts can run console
   lines: what they are).

## Acceptance
- `py tools/hci_consistency_audit.py --check` at 0. The audit's rules are tuned: one- and two-letter labels are icons,
  and a statement ends at a lambda's brace.
- The tests that named the old labels pass with the new ones: the update dialog's Install This Version, and round 306's
  Pen Color.
- The whole suite in the dependency-free non-LTCG build.
