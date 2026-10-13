# 0053: Multi-select and delete many assets at once (Materials, Textures, Project)

- **Status:** done (as built below)
- **Requirements:** R-01
- **Decisions:** none needed
- **Model:** main session (Opus)
- **PR:** (link when opened)

## Goal
The user asked (2026-10-12): "I want to be able to multiselect textures and materials in their tabs so I can en
masse delete assets." Ctrl / Shift click selects several thumbnails, as in the Hierarchy and Unity's Project
window. Delete removes them all after one question.

## Scope
- In:
  - **Materials and Textures windows (and the Project window).**
    - Ctrl+click toggles an item.
    - Shift+click selects the range from the last clicked item, in the order shown.
    - Ctrl+A selects all shown (the search filter applies).
    - A plain click selects one.
    - Every selected thumbnail shows the selection highlight; the editing panel shows the last clicked one.
  - **Delete** (key, toolbar button or context menu) on a selection of several asks once ("Delete 7 materials?",
    listing a few names). It deletes them all: asset files go to the Trash, scene materials are emptied from their
    slots. The scene side is one undo step.
  - Console: `matdelete <name>...`, `texdelete <name>...` (also used by the tests).
- Out: multi-object drag (a drag still assigns the item under the mouse).

## Acceptance criteria
1. Ctrl+click toggles one, Shift+click selects the range from the last click (Ctrl+Shift adds the range): the selection
   is exactly what those clicks name.
2. Delete with three selected asks once; confirming deletes the three files (Trash) and empties their slots, as one
   undo step for the slots.
3. The same in the Textures window, and in the Project window for files.
4. Ctrl+A with a search selects only the shown ones.

## As built
- **Shift+click selects the range** from the last click, replacing the selection; Ctrl+Shift adds the range. This
  follows the Hierarchy and Unity's Project window.
- **The selection is what's shown.** A search drops hidden items from the selection, so Delete never removes what the
  user can't see.
- **One question at a time.** While a delete question is up, clicks and Delete don't start another.
- **Ctrl+A over a window** never falls through to the scene's Select All.
- **Console names are comma-separated** so they may hold spaces: `matdelete Bricks, Wood Floor`. The older
  `deletemat` stays for one material.
- **The Project window's delete** keeps a deleted `.mat` on objects as a scene material, as it always did. The
  Materials window empties its slots.
- A Learn section, "Many at once", is in the Assets lesson.

## Tests and checks to run
- Unit tests (round 75) by clicks and keys; full suite, verify mode, Linux, sanitizer.
