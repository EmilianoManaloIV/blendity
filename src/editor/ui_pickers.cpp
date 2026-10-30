// SPDX-License-Identifier: GPL-2.0-or-later
// Pickers and the Report dialog (task 0271, ADR 0085's console round): rows the tool sections and dialogs share so a
// path, an image or a folder is chosen from a list or the system's dialog instead of typed into the Console (Nielsen H6,
// recognition rather than recall), and a dialog that shows what an operation printed instead of sending the artist to
// the Console to read it.
#include "editor.h"

#include "../core/core.h"
#include "../image/image.h"

#include <algorithm>

namespace bl {

/* The system's folder chooser, outside the frame like the file dialogs (the caller defers it). */
Editor::PathPick Editor::pick_folder(const std::string &title, const std::string &dir, std::string &out) {
  if (headless_ || !window_) return PathPick::Unavailable;
  if (!platform::file_dialogs_available()) return PathPick::Unavailable;
  const bool ok = platform::folder_dialog(window_, title, dir, out);
  const int mx = ui_.in.mx, my = ui_.in.my;
  ui_.in = ui::Input{};
  ui_.in.mx = ui_.in.pmx = mx;
  ui_.in.my = ui_.in.pmy = my;
  ui_.redraw = true;
  return ok ? PathPick::Chosen : PathPick::Cancelled;
}

/* No system dialog here (headless, or Linux without zenity / kdialog): said, with the way round. */
void Editor::picker_unavailable(const std::string &what) {
  toast(LogLevel::Warning, what + ": no file dialog here (Linux needs zenity or kdialog) - put the file under the project's Assets and pick it from the list, or type its path");
}

/* An image: None, the project's images (sorted, the list scrolls when long), then Browse... for one elsewhere. */
bool Editor::image_picker_row(ui::Layout &lay, const char *label, const char *key, const std::string &current, const std::function<void(const std::string &)> &apply) {
  std::vector<std::string> items{"None", "Browse..."};  // (Browse at the top: a long project's list scrolls)
  const int browse = 1;
  int cur = 0;
  for (const std::string &p : image_assets()) {
    if (p == current) cur = (int)items.size();
    items.push_back(p);
  }
  if (!current.empty() && cur == 0) cur = (int)items.size(), items.push_back(current);  // (one from outside the project: shown as it is)
  if (!tool_combo_row(lay, label, key, cur, items)) return false;
  if (cur == 0) return apply(""), true;
  if (cur != browse) return apply(items[(size_t)cur]), true;
  const std::string title = std::string("Choose ") + label;
  defer([this, title, apply] {
    std::string path;
    const std::vector<platform::FileFilter> filters{{"Images", {".png", ".jpg", ".jpeg", ".tga", ".bmp", ".exr", ".hdr"}}};
    const PathPick r = pick_open_path(title, assets_dir_, filters, path);
    if (r == PathPick::Chosen) apply(path);
    if (r == PathPick::Unavailable) picker_unavailable(title);
  });
  return false;
}

/* A path's field and its ... button in `r`: true when Enter was pressed in the field (the caller applies `text`); the
 * button opens the system's dialog after the frame and `picked` gets what was chosen. */
bool Editor::path_picker_field(const Recti &r, ui::Id fid, const std::string &key, std::string &text, PickKind kind,
                               const std::vector<platform::FileFilter> &filters, const std::function<void(const std::string &)> &picked) {
  auto &u = ui_;
  const int bw = u.px(28);
  const Recti fr{r.x, r.y, r.w - bw - u.px(4), r.h};
  const Recti br{fr.right() + u.px(4), r.y, bw, r.h};
  bool done = false;
  u.text_field(fid, fr, text, &done);
  done = done && !u.in.pressed[0] && !u.in.pressed[1];  // (a click elsewhere keeps the text but is not Enter: the ... button must not export)
  if (u.record_buttons) u.button_rects[key + ":field"] = fr, u.button_rects[key + ":..."] = br;
  const bool pressed = u.button(br, "...");
  u.tooltip(kind == PickKind::Folder ? "Choose the folder" : "Choose the file");
  if (!pressed) return done;
  const std::string title = kind == PickKind::Folder ? "Choose a Folder" : "Choose a File";
  const std::string start = text.empty() ? project_root_ : resolve_asset_path(text);
  defer([this, title, start, kind, filters, picked] {
    std::string path;
    const PathPick res = kind == PickKind::Folder ? pick_folder(title, start, path)
                         : kind == PickKind::Save ? pick_save_path(title, start, filters, path)
                                                  : pick_open_path(title, fs::parent(start), filters, path);
    if (res == PathPick::Chosen) picked(path);
    if (res == PathPick::Unavailable) picker_unavailable(title);
  });
  return done;
}

/* A path as a row: its label, the field (typed, Enter applies) and the ... button. */
bool Editor::path_picker_row(ui::Layout &lay, const char *label, const char *key, const std::string &current, PickKind kind,
                             const std::vector<platform::FileFilter> &filters, const std::function<void(const std::string &)> &apply) {
  auto &u = ui_;
  const Recti r = lay.row(u.row_h() + u.px(2));
  u.label({r.x + u.px(4), r.y, u.px(92), r.h}, label);
  std::string &text = picker_text_[key];
  const ui::Id fid = u.id(std::string(key) + "#field");
  if (!u.editing(fid)) text = current;
  const Recti fr{r.x + u.px(98), r.y + u.px(1), r.w - u.px(102), r.h - u.px(2)};
  const bool done = path_picker_field(fr, fid, key, text, kind, filters, apply);
  if (done && text != current) return apply(text), true;
  return false;
}

/* ---------------------------------------------------------------- the Report dialog */

void Editor::show_report(const std::string &title, const std::vector<std::string> &lines) {
  report_actions_.clear();  // (a caller with actions sets them after: task 0284)
  report_title_ = title;
  report_lines_ = lines;
  if (report_lines_.empty()) report_lines_.push_back("(nothing to report)");
  dialog_ = Dialog::Report;
  ui_.redraw = true;
}

/* `report <command line>`: the command run, everything it printed shown in the Report dialog. */
void Editor::report_command(const std::string &line) {
  std::vector<LogEntry> got;
  const size_t from = Log::fetch(SIZE_MAX, got);
  run_console_command(line);
  got.clear();
  Log::fetch(from, got);
  std::vector<std::string> lines;
  for (const LogEntry &e : got) {
    if (e.text.rfind("> ", 0) == 0) continue;  // (the console's echo of the command)
    lines.push_back(e.text);
  }
  show_report(line, lines);
}

void Editor::draw_report_dialog(const std::function<void()> &close) {
  auto &u = ui_;
  const size_t shown = std::min<size_t>(report_lines_.size(), 30);
  for (size_t i = 0; i < shown; i++) {
    const Recti r = u.popup_row();
    if (i < report_actions_.size()) {  // (task 0284: the row goes to its frame; Fix applies its remedy)
      const Recti go{r.x + u.px(8), r.y, r.w - u.px(84), r.h}, fix{r.right() - u.px(70), r.y + u.px(1), u.px(60), r.h - u.px(2)};
      if (u.record_buttons) u.button_rects["report:row " + std::to_string(i)] = go, u.button_rects["report:fix " + std::to_string(i)] = fix;
      if (u.button(go, report_lines_[i], false)) command(report_actions_[i].first);
      u.tooltip(report_lines_[i]);
      if (u.button(fix, "Fix")) defer([this, cmd = report_actions_[i].second] { command(cmd); });
      continue;
    }
    u.label({r.x + u.px(12), r.y, r.w - u.px(20), r.h}, report_lines_[i]);
    u.tooltip(report_lines_[i]);
  }
  if (report_lines_.size() > shown) {
    const Recti r = u.popup_row();
    u.label({r.x + u.px(12), r.y, r.w - u.px(20), r.h}, strprintf("... and %zu more lines (Copy takes them all)", report_lines_.size() - shown), u.theme.text_dim);
  }
  const Recti b = u.popup_row(u.row_h() + u.px(10));
  const Recti copy{b.right() - u.px(184), b.y + u.px(4), u.px(84), b.h - u.px(8)}, shut{b.right() - u.px(92), b.y + u.px(4), u.px(80), b.h - u.px(8)};
  if (u.record_buttons) u.button_rects["report:Copy"] = copy, u.button_rects["report:Close"] = shut;
  if (u.button(copy, "Copy")) {
    std::string all;
    for (const std::string &l : report_lines_) all += l + "\n";
    if (u.clipboard_set) u.clipboard_set(all);
    report_copied_ = all;
  }
  if (u.button(shut, "Close")) close();
}

}  // namespace bl
