# 0071: Dragging stacks into order

- **Status:** done
- **Requirements:** R-01, R-10
- **Decisions:** none (a UI helper)
- **Model:** main session (Opus)
- **PR:** https://github.com/EmilianoManaloIV/blendity/pull/64

## Goal
The user (2026-10-14): drag-and-drop reordering of the camera filter stack, the modifier stack and other stacks.

## Scope
- In: one helper (`stack_drag_item` / `stack_drag_end`, `src/editor/stack_drag.cpp`): press an item's name in its
  header and drag; a line shows where it lands (as the Hierarchy's drop line), the item is outlined; let go and the
  list's own move runs, one undo step. Used by the modifier stack, a camera's filters, a texture's layers and the
  Sequencer's tracks (`seq trackmove <from> <to>` too).
- A press that doesn't move is the header's usual click (it folds or unfolds); a drag doesn't fold it.
- The menus' Move Up / Down stay.
- Out: dragging non-modifier components (their menu's Move Up / Down stays).

## Acceptance criteria
1. A modifier dragged below another moves there; undo puts it back in one step; a click still folds.
2. A camera's filter and a texture's layer move the same way.
3. A Sequencer track dragged moves with its clips.

## As built
- The drop slot is above the first header whose middle is below the mouse; the line is drawn over the lists' headers.
- A drag is dropped by Esc, an undo or redo, or its list no longer showing. Tracks don't drag in Play.
- Texture layers use the same calls as filters; their drag has no test of its own.
- Tests: round 97.
