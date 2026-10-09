# 0008: Inspector component menus act, and the Scene view filter test is steady on macOS

- **Status:** done (merged as PR #5)
- **Requirements:** R-07, Q-03
- **Decisions:** none needed
- **Model:** main session (Opus)
- **PR:** (link when opened)

## Goal
Remove Component, Move Up and Reset in a component's menu, and Apply / Duplicate / Copy to Selected /
Move / Reset in a modifier's menu, do what they say. CI passes on macOS every time.

## Findings
1. **Menu items did nothing.** Popup menus run at the end of the frame (`ui::Context::draw_popups`), after
   `Editor::draw_inspector` has returned. Two kinds of item were broken:
   - The component menu's items set `draw_inspector`'s locals `remove_idx` / `move_up` through a
     reference that no longer pointed anywhere, so the component stayed. That is also undefined
     behaviour.
   - The modifier menu did the same with `act` / `act_ci`. Its Reset item and the X button worked,
     because they act directly.
2. **macOS CI failed now and then** in "camera filters: `filters on` ... `filters off` restores it". It
   compared Scene view frames pixel by pixel, and overlay text (frame times, the log line) redraws more
   pixels on slower runners.

## Changes
- **Menu items now record the choice and the next draw applies it.**
  - Each item stores its choice in `Editor::inspector_action_`: the object id, the component pointer and
    the action. The lambdas capture by value and request a redraw.
  - The next `draw_inspector` applies the choice before drawing. A choice made on another object is
    dropped.
- **Console commands `undo` and `redo`.**
- **The flaky test** now asks whether the view is filtered (the share of 15-bit pixels) instead of
  comparing pixels.
- **Test accessors:** `Editor::inspector_menu_rect_for_test` and `ui::Context::popup_rects`.

## Acceptance criteria
1. Clicking Remove Component, Move Up or Reset in the menu changes the component list, and the change can
   be undone. A real-event test covers this.
2. Duplicate and Apply in a modifier's menu work (a real-event test).
3. The Scene view filter test no longer depends on overlay pixels.
