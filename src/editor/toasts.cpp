// SPDX-License-Identifier: GPL-2.0-or-later
// Toasts (task 0252, ADR 0085 F1): every new warning or error of the log shown over the Scene view for three seconds,
// the newest on top, three at most; a refusal that knows its remedy carries a Fix button that runs a console command
// (Nielsen H9: help the user recognise, diagnose and recover). The log line stays, so the Console and the tests reading
// the log see nothing new. The HCI study's instrument writes the same events to a file when BLENDITY_HCI_LOG names one.
#include "editor.h"

#include "../core/core.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>

namespace bl {

void Editor::toast(LogLevel level, const std::string &text, const std::string &fix_cmd, const std::string &fix_label) {
  for (Toast &t : toasts_)
    if (t.text == text) {  // (the same message again: one toast, a count)
      t.count++, t.born = clock_now();
      if (t.fix_cmd.empty()) t.fix_cmd = fix_cmd, t.fix_label = fix_label;
      return;
    }
  if (level == LogLevel::Info && toasts_.size() >= 3) {  // (an Info toast never pushes out a warning or an error: the oldest Info goes, or none)
    const auto victim = std::find_if(toasts_.rbegin(), toasts_.rend(), [](const Toast &o) { return o.level == LogLevel::Info; });
    if (victim == toasts_.rend()) return;
    toasts_.erase(std::next(victim).base());
  }
  Toast t;
  t.text = text, t.fix_cmd = fix_cmd, t.fix_label = fix_label, t.level = level, t.born = clock_now();
  toasts_.insert(toasts_.begin(), t);
  while (toasts_.size() > 3) toasts_.pop_back();
  hci_log("toast\t" + text + (fix_cmd.empty() ? "" : "\tfix: " + fix_cmd));
  ui_.redraw = true;
}

/* A refusal that knows its remedy: the warning is logged as before, and the toast it becomes gets the Fix button. */
void Editor::refuse(const std::string &text, const std::string &fix_cmd, const std::string &fix_label) {
  refuse_pending_.push_back({text, fix_cmd, fix_label});
  Log::warn("%s", text.c_str());
}

/* Each frame after the log is fetched: the new warnings and errors become toasts; the timers run; old toasts leave. */
void Editor::toasts_poll() {
  if (!toast_started_) {  // (the log the editor opened with - earlier tests' and sessions' lines - is history, not news)
    toast_started_ = true, toast_seen_ = log_.size();
    refuse_pending_.clear();
    return;
  }
  for (size_t i = std::min(toast_seen_, log_.size()); i < log_.size(); i++) {
    const LogEntry &e = log_[i];
    if (e.level == LogLevel::Info) continue;
    const auto fix = std::find_if(refuse_pending_.begin(), refuse_pending_.end(), [&](const PendingFix &f) { return f.text == e.text; });
    if (fix == refuse_pending_.end()) toast(e.level, e.text);
    else toast(e.level, e.text, fix->cmd, fix->label), refuse_pending_.erase(fix);
  }
  refuse_pending_.clear();  // (a refusal whose line was filtered or cleared keeps no fix for a later line)
  toast_seen_ = log_.size();
  const double now = clock_now();
  if (toasts_.empty()) return;
  const size_t before = toasts_.size();
  toasts_.erase(std::remove_if(toasts_.begin(), toasts_.end(), [now](const Toast &t) { return now - t.born > 3.0; }), toasts_.end());
  if (toasts_.size() != before) ui_.redraw = true;
  if (!toasts_.empty()) ui_.redraw = true;  // (the timers draw)
}

/* The clicks on the toasts, taken before the Scene view's own handling: the text opens the Console, the Fix button runs
 * its command (deferred a frame, as a palette command is). The rectangles are the last draw's. */
bool Editor::toasts_update(const Recti &) {
  if (toasts_.empty() || toast_rects_.empty()) return false;
  auto &u = ui_;
  bool over = false;
  for (const ToastRects &tr : toast_rects_) {
    if (!u.hovered(tr.frame)) continue;
    over = true;
    const auto it = std::find_if(toasts_.begin(), toasts_.end(), [&](const Toast &t) { return t.text == tr.key; });
    if (it != toasts_.end()) it->born = clock_now() - 0.5;  // (the timer waits while the pointer is on it: 2.5 s left once it leaves)
    if (!u.in.pressed[0]) continue;
    u.consume_click();
    if (!tr.fix_cmd.empty() && u.hovered(tr.fix)) {
      hci_log("fix\t" + tr.fix_cmd);
      const std::string cmd = tr.fix_cmd;
      if (it != toasts_.end()) toasts_.erase(it);
      toast_rects_.clear();
      defer([this, cmd] { command(cmd); });
      return true;
    }
    dock_open(WindowKind::Console);
    return true;
  }
  return over;
}

void Editor::draw_toasts(const Recti &view) {
  toast_rects_.clear();
  if (toasts_.empty()) return;
  auto &u = ui_;
  const int lh = u.font.line_height(), pad = u.px(8), gap = u.px(6);
  int y = view.y + u.px(10);
  for (size_t i = 0; i < toasts_.size(); i++) {
    Toast &t = toasts_[i];
    std::string text = t.text;
    if (t.count > 1) text += strprintf("  x%d", t.count);
    const int bw = t.fix_cmd.empty() ? 0 : u.font.text_width("Fix: " + t.fix_label) + u.px(28);
    const int w = std::min(view.w - u.px(40), u.font.text_width(text) + 2 * pad + lh + (bw ? bw + gap : 0));
    const Recti r{view.x + (view.w - w) / 2, y, w, tablet_ ? std::max(lh + 2 * pad, u.px(34)) : lh + 2 * pad};  // (task 0270: 7 mm in Tablet Mode)
    const uint32_t accent = t.level == LogLevel::Error ? u.theme.error : t.level == LogLevel::Warning ? u.theme.warning : u.theme.accent;
    u.frame(r, u.theme.button, accent, u.px(4));
    const int a = lh - u.px(4);
    const ui::Icon icon = t.level == LogLevel::Error ? ui::Icon::Error : t.level == LogLevel::Warning ? ui::Icon::Warning : ui::Icon::Info;
    u.draw_icon(icon, {r.x + pad, r.y + (r.h - a) / 2, a, a}, accent);
    const Recti tr{r.x + pad + lh, r.y, std::max(0, w - 2 * pad - lh - (bw ? bw + gap : 0)), r.h};
    u.canvas.text_ellipsis(u.font, tr.x, tr.y + (tr.h - lh) / 2, tr.w, text, u.theme.text_bright);
    ToastRects rects;
    rects.frame = r, rects.text = tr, rects.key = t.text, rects.fix_cmd = t.fix_cmd;
    if (u.record_buttons) u.button_rects["toast:" + t.text] = r;  // (for the tests: a toast hovered or clicked by its text)
    if (bw) {
      const Recti fb{r.right() - pad - bw, r.y + u.px(3), bw, r.h - u.px(6)};
      u.button(fb, "Fix: " + t.fix_label, false, ui::Icon::Play);  // (drawn and recorded here; the click is toasts_update's)
      rects.fix = fb;
    }
    toast_rects_.push_back(rects);
    y += r.h + gap;
  }
}

/* The HCI study's instrument: a line a toast, a fix clicked, a hint change, an undo step, a layout change. */
void Editor::hci_log(const std::string &line) {
  if (hci_log_path_.empty()) {
    const char *e = std::getenv("BLENDITY_HCI_LOG");
    hci_log_path_ = e && *e ? e : "-";
  }
  if (hci_log_path_ == "-") return;
  std::ofstream f(hci_log_path_, std::ios::app);
  f << strprintf("%.3f\t", clock_now()) << line << "\n";
}

void Editor::hci_log_poll() {
  if (!hci_started_) hci_started_ = true, hci_log("start");
  if (hci_log_path_ == "-") return;
  const std::string hint = statusbar_hint();
  if (hint != hci_last_hint_) hci_last_hint_ = hint, hci_log("hint\t" + hint);
  if (workspace_ != hci_last_layout_) hci_last_layout_ = workspace_, hci_log("layout\t" + workspace_);
  if (undo_serial_ != hci_last_undo_) {
    hci_last_undo_ = undo_serial_;
    if (!undo_.empty()) hci_log("undo\t" + undo_.back().label);
  }
}

}  // namespace bl
