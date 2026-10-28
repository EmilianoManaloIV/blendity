// SPDX-License-Identifier: GPL-2.0-or-later
// The History window and the unsaved-changes prompt (task 0254, ADR 0085 F5 and F6).
//
// History: the undo steps as a list, the current state marked, a click jumping to any of them (undo or redo as many
// steps as it takes), the steps that can be redone dim below; the Editing window's own stack as a second column when
// an image is open, so Ctrl+Z's target is visible (Nielsen H1, H3; Blender's Edit > Undo History; Photoshop's panel).
//
// Unsaved changes: New Scene, Open Scene and a scene opened from the Project window or a drop ask "Save changes?" -
// Save, Don't Save, Cancel - when the scene has changes since its last save (H5: prevent the loss). Save runs the
// save, then the action, once the save has really happened (a Save As that is cancelled drops the action).
#include "editor.h"

#include "../core/core.h"

#include <algorithm>

namespace bl {

/* ---- the History window ---- */

Editor::HistoryRows Editor::history_rows_for_test() const {
  HistoryRows rows;
  for (const UndoState &s : undo_) rows.past.push_back(s.label);
  for (auto it = redo_.rbegin(); it != redo_.rend(); ++it) rows.future.push_back(it->label);
  return rows;
}

/* The state after `steps` of the steps taken (0: before the first): undo or redo until the past has that length. */
void Editor::history_jump(size_t steps) {
  if (playing_) return;
  for (int guard = 0; guard < 4096 && undo_.size() > steps; guard++) undo();
  for (int guard = 0; guard < 4096 && undo_.size() < steps && !redo_.empty(); guard++) redo();
  ui_.redraw = true;
}

static void history_row(ui::Context &u, const Recti &row, const std::string &label, bool current, bool future, const std::string &key) {
  const int lh = u.font.line_height();
  if (current) u.canvas.fill_rect(row, u.theme.selection);
  const Recti dot{row.x + u.px(8), row.y + (row.h - u.px(6)) / 2, u.px(6), u.px(6)};
  u.canvas.fill_round_rect(dot, u.px(3), future ? u.theme.text_dim : u.theme.accent);
  u.canvas.text_ellipsis(u.font, row.x + u.px(22), row.y + (row.h - lh) / 2, row.w - u.px(30), label, future ? u.theme.text_dim : u.theme.text);
  if (u.record_buttons) u.button_rects[key] = row;
}

void Editor::draw_history_window(const Recti &r) {
  auto &u = ui_;
  ui::Layout lay{{r.x, r.y + u.px(4), r.w, r.h - u.px(8)}, r.y + u.px(4)};
  lay.row_h = u.row_h();
  const int rh = u.row_h() + u.px(2);
  if (undo_.empty() && redo_.empty()) {
    const Recti row = lay.row(rh);
    u.label({row.x + u.px(8), row.y, row.w - u.px(16), row.h}, "No steps yet: every change lands here, and a click goes back to it.", u.theme.text_dim);
    return;
  }
  /* The steps taken, oldest first; a row is the state after that step; the last is where we are. */
  const size_t n = undo_.size();
  size_t jump = (size_t)-1;  // (a click's target: taken after the rows are drawn, so no row reads a state that is gone)
  {
    const Recti row = lay.row(rh);
    history_row(u, row, "Start (before the first step)", n == 0, false, "history:start");
    if (u.hovered(row) && u.in.pressed[0] && n > 0) jump = 0;
  }
  for (size_t i = 0; i < n; i++) {
    const Recti row = lay.row(rh);
    history_row(u, row, undo_[i].label, i + 1 == n, false, strprintf("history:%zu", i));
    if (u.hovered(row) && u.in.pressed[0] && i + 1 != n) jump = i + 1;
  }
  /* The steps undone, the next one first: a click redoes up to it. */
  for (size_t j = 0; j < redo_.size(); j++) {
    const UndoState &s = redo_[redo_.size() - 1 - j];
    const Recti row = lay.row(rh);
    history_row(u, row, s.label, false, true, strprintf("history:redo:%zu", j));
    if (u.hovered(row) && u.in.pressed[0]) jump = n + j + 1;
  }
  if (jump != (size_t)-1) {
    u.consume_click();
    history_jump(jump);
    return;  // (the rows are drawn again next frame, from the new state)
  }
  if (!edit_doc_.is_open()) return;
  /* The Editing window's own stack: a second list, so a Ctrl+Z over that window is seen for what it undoes. */
  lay.row(u.px(8));
  const Recti head = lay.row(rh);
  u.label({head.x + u.px(8), head.y, head.w - u.px(16), head.h}, "Editing: " + edit_doc_.label(), u.theme.text_bright);
  const Recti row = lay.row(rh);
  const int bw = u.px(70);
  u.label({row.x + u.px(8), row.y, row.w - 2 * bw - u.px(24), row.h},
          strprintf("%zu step%s back, %zu forward (%s over the Editing window)", edit_doc_.undo_count(), edit_doc_.undo_count() == 1 ? "" : "s",
                    edit_doc_.redo_count(), key("edit.undo").c_str()),
          u.theme.text_dim);
  if (u.button_enabled({row.right() - 2 * bw - u.px(12), row.y, bw, row.h}, "Undo", edit_doc_.can_undo(), "Nothing to undo in the Editing window.")) edit_doc_.undo();
  if (u.button_enabled({row.right() - bw - u.px(8), row.y, bw, row.h}, "Redo", edit_doc_.can_redo(), "Nothing to redo in the Editing window.")) edit_doc_.redo();
}

/* ---- unsaved changes ---- */

/* Ask before `then` replaces the scene, when it has changes since the last save; otherwise do it now. */
void Editor::ask_unsaved(const std::string &what, std::function<void()> then) {
  after_save_ = nullptr;
  if (playing_) exit_play();  // (the scene at stake is the one Play restores: leave Play, then ask - as Exit does)
  if (!scene_dirty_) {
    then();
    return;
  }
  unsaved_what_ = what;
  unsaved_then_ = std::move(then);
  dialog_ = Dialog::UnsavedChanges;
  ui_.redraw = true;
}

void Editor::unsaved_save_then() {
  after_save_ = std::move(unsaved_then_);
  unsaved_then_ = nullptr;
  save_scene_cmd(false);  // (a scene without a file: Save As; the action follows the save, or is dropped with it)
}

void Editor::unsaved_discard() {
  std::function<void()> then = std::move(unsaved_then_);
  unsaved_then_ = nullptr;
  if (then) then();
}

void Editor::unsaved_cancel() {
  unsaved_then_ = nullptr;
  after_save_ = nullptr;
}

/* After a save that really happened: the action that waited for it. */
void Editor::run_after_save() {
  if (!after_save_) return;
  std::function<void()> then = std::move(after_save_);
  after_save_ = nullptr;
  then();
}

void Editor::new_scene_asked() {
  ask_unsaved("New Scene", [this] { new_scene(); });
}

void Editor::open_scene_asked(const std::string &path) {
  ask_unsaved("Opening " + fs::filename(path), [this, path] { open_scene(path); });
}

void Editor::open_scene_dialog_asked() {
  ask_unsaved("Open Scene", [this] { open_scene_dialog(); });
}

}  // namespace bl
