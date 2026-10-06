// SPDX-License-Identifier: GPL-2.0-or-later
// The Learn tab: renders lessons.cpp and executes their "Try it" actions.
#include "learn.h"

#include "../core/core.h"
#include "editor.h"

#include <algorithm>
#include <unordered_set>

namespace bl {

using ui::Icon;

static std::vector<std::string> wrap(Font &f, const std::string &text, int width) {
  std::vector<std::string> lines;
  std::string cur;
  for (std::string w : split_ws(text)) {
    /* Hard-break tokens wider than the column (long source paths). */
    while (f.text_width(w) > width && w.size() > 1) {
      size_t cut = w.size() - 1;
      while (cut > 1 && f.text_width(w.substr(0, cut)) > width) cut--;
      if (!cur.empty()) { lines.push_back(cur); cur.clear(); }
      lines.push_back(w.substr(0, cut));
      w = w.substr(cut);
    }
    std::string t = cur.empty() ? w : cur + " " + w;
    if (f.text_width(t) > width && !cur.empty()) {
      lines.push_back(cur);
      cur = w;
    }
    else cur = t;
  }
  if (!cur.empty()) lines.push_back(cur);
  return lines;
}

static std::string trim(const std::string &s) {
  size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
  return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

static std::vector<std::string> split_cells(const std::string &line) {
  std::vector<std::string> cells;
  size_t s = 1;  // skip leading '|'
  while (s <= line.size()) {
    size_t e = line.find('|', s);
    if (e == std::string::npos) e = line.size();
    cells.push_back(trim(line.substr(s, e - s)));
    s = e + 1;
  }
  return cells;
}

void Editor::draw_learn(const Recti &r) {
  auto &u = ui_;
  auto &in = u.in;
  static std::unordered_set<uint64_t> revealed;
  int n = lesson_count();
  lesson_ = std::max(0, std::min(lesson_, n - 1));

  /* ---- lesson list ---- */
  int lw = std::min(u.px(250), r.w * 2 / 5);
  Recti left{r.x, r.y, lw, r.h};
  u.canvas.fill_rect(left, Color::hex(0x313131));
  u.canvas.vline(left.right(), left.y, left.bottom(), u.theme.border);
  Recti head{left.x, left.y, left.w, u.row_h() * 2 + u.px(10)};
  u.draw_icon(Icon::Book, {head.x + u.px(10), head.y + u.px(8), u.row_h(), u.row_h()}, u.theme.accent);
  u.label({head.x + u.px(16) + u.row_h(), head.y + u.px(6), head.w, u.row_h()}, "Learn Blendity", u.theme.text_bright);
  int done = (int)lessons_done_.size();
  Recti pb{head.x + u.px(10), head.y + u.row_h() + u.px(12), head.w - u.px(20), u.px(6)};
  u.canvas.fill_round_rect(pb, u.px(3), Color::hex(0x252525));
  u.canvas.fill_round_rect({pb.x, pb.y, pb.w * std::min(done, n) / n, pb.h}, u.px(3), u.theme.accent);
  u.label({head.x + u.px(10), pb.bottom(), head.w - u.px(20), u.row_h()}, strprintf("%d of %d lessons complete", std::min(done, n), n), u.theme.text_dim);
  Recti list{left.x, head.bottom() + u.px(10), left.w - 1, left.h - head.h - u.px(10)};
  int rh = u.row_h() + u.px(6);
  int loff = u.begin_scroll(u.id("lesson_list"), list, n * rh);
  for (int i = 0; i < n; i++) {
    Recti row{list.x, list.y + i * rh - loff, list.w, rh};
    bool cur = i == lesson_;
    bool hot = u.hovered(row);
    if (cur) u.canvas.fill_rect(row, u.theme.selection);
    else if (hot) u.canvas.fill_rect(row, Color::hex(0x404040));
    int a = u.font.line_height() - u.px(3);
    if (lessons_done_.count(i)) u.draw_icon(Icon::Check, {row.x + u.px(8), row.y + (rh - a) / 2, a, a}, u.theme.ok);
    else u.label({row.x + u.px(4), row.y, a + u.px(10), rh}, std::to_string(i), u.theme.text_dim, ui::Align::Center);
    u.label({row.x + u.px(14) + a, row.y, row.w - a - u.px(18), rh}, lesson(i).title, cur ? u.theme.text_bright : u.theme.text);
    if (hot && in.pressed[0]) lesson_ = i;
  }
  u.end_scroll();

  /* ---- lesson body ---- */
  const Lesson &L = lesson(lesson_);
  Recti body{left.right() + 1, r.y, r.right() - left.right() - 1, r.h};
  ui::Id sid = u.id("lesson_body") ^ (uint64_t)lesson_;
  static int content_h = 0;
  int off = u.begin_scroll(sid, body, content_h);
  int margin = u.px(22);
  int x0 = body.x + margin, w = std::max(u.px(200), body.w - margin * 2 - u.px(10));
  int y = body.y + u.px(16) - off;
  int lh = u.font.line_height();
  auto text_lines = [&](const std::vector<std::string> &lines, int x, uint32_t col) {
    for (auto &l : lines) {
      u.canvas.text(u.font, x, y, l, col);
      y += lh;
    }
  };
  /* Title block */
  u.canvas.text(u.font, x0, y, strprintf("LESSON %d", lesson_), u.theme.accent);
  y += lh + u.px(2);
  {
    /* Poor man's bold: draw twice, 1px apart. */
    for (auto &l : wrap(u.font, L.title, w)) {
      u.canvas.text(u.font, x0, y, l, u.theme.text_bright);
      u.canvas.text(u.font, x0 + 1, y, l, u.theme.text_bright);
      y += lh;
    }
  }
  text_lines(wrap(u.font, L.subtitle, w), x0, u.theme.text_dim);
  y += u.px(10);
  u.canvas.hline(x0, x0 + w, y, u.theme.separator);
  y += u.px(12);

  /* Parse & render the markup. */
  std::vector<std::string> src;
  {
    std::string s = L.body;
    size_t p = 0, e;
    while ((e = s.find('\n', p)) != std::string::npos) { src.push_back(s.substr(p, e - p)); p = e + 1; }
    src.push_back(s.substr(p));
  }
  bool table_header = true;
  for (size_t li = 0; li < src.size(); li++) {
    std::string line = trim(src[li]);
    if (line.empty()) { table_header = true; y += u.px(6); continue; }
    char k = line[0];
    std::string rest = trim(line.size() > 1 ? line.substr(1) : "");
    if (k != '|') table_header = true;
    if (k == '#' ) {
      y += u.px(8);
      for (auto &l : wrap(u.font, rest, w)) {
        u.canvas.text(u.font, x0, y, l, u.theme.text_bright);
        u.canvas.text(u.font, x0 + 1, y, l, u.theme.text_bright);
        y += lh;
      }
      u.canvas.fill_rect({x0, y + u.px(2), u.px(28), u.px(2)}, u.theme.accent);
      y += u.px(10);
    }
    else if (k == '-') {
      auto lines = wrap(u.font, rest, w - u.px(18));
      u.canvas.fill_circle((float)x0 + u.px(5), (float)y + lh * 0.55f, (float)u.px(2.5f), u.theme.accent);
      text_lines(lines, x0 + u.px(16), u.theme.text);
      y += u.px(3);
    }
    else if (k == '|') {
      auto cells = split_cells(line);
      int nc = std::max(1, (int)cells.size());
      int cw = w / nc;
      std::vector<std::vector<std::string>> wrapped;
      size_t maxl = 1;
      for (auto &c : cells) {
        wrapped.push_back(wrap(u.font, c, cw - u.px(12)));
        maxl = std::max(maxl, wrapped.back().size());
      }
      int hgt = (int)maxl * lh + u.px(8);
      uint32_t bg = table_header ? Color::hex(0x2A2A2A) : ((li % 2) ? Color::hex(0x3E3E3E) : Color::hex(0x393939));
      u.canvas.fill_rect({x0, y, w, hgt}, bg);
      for (int c = 0; c < nc && c < (int)wrapped.size(); c++) {
        int yy = y + u.px(4);
        for (auto &l : wrapped[c]) {
          u.canvas.text(u.font, x0 + c * cw + u.px(6), yy, l, table_header ? u.theme.accent : (c == 0 ? u.theme.text_bright : u.theme.text));
          yy += lh;
        }
        if (c > 0) u.canvas.vline(x0 + c * cw, y, y + hgt, Color::hex(0x2C2C2C));
      }
      y += hgt;
      table_header = false;
    }
    else if (k == '>') {
      auto lines = wrap(u.font, rest, w - u.px(40));
      int hgt = (int)lines.size() * lh + u.px(10);
      u.canvas.fill_rect({x0, y, w, hgt}, Color::hex(0x3A322A));
      u.canvas.fill_rect({x0, y, u.px(3), hgt}, u.theme.accent);
      int a = lh - u.px(3);
      u.draw_icon(Icon::Book, {x0 + u.px(10), y + u.px(6), a, a}, u.theme.accent);
      int yy = y + u.px(5);
      for (auto &l : lines) { u.canvas.text(u.font, x0 + u.px(16) + a, yy, l, Color::hex(0xE9D8C4)); yy += lh; }
      y += hgt + u.px(4);
    }
    else if (k == '@') {
      auto lines = wrap(u.font, rest, w - u.px(40));
      int hgt = (int)lines.size() * lh + u.px(8);
      Recti br{x0, y, w, hgt};
      bool hot = u.hovered(br);
      u.canvas.fill_rect(br, hot ? Color::hex(0x2C3540) : Color::hex(0x2A2F36));
      int a = lh - u.px(3);
      u.draw_icon(Icon::Terminal, {x0 + u.px(8), y + u.px(5), a, a}, Color::hex(0x7FB2E6));
      int yy = y + u.px(4);
      for (auto &l : lines) { u.canvas.text(u.font, x0 + u.px(14) + a, yy, l, Color::hex(0xA9C9EA)); yy += lh; }
      if (hot) {
        u.tooltip("Click to open this location from the Blender source tree next to Blendity.");
        if (in.pressed[0]) {
          std::string rel = rest.substr(0, rest.find("  "));
          std::string path = fs::join(fs::parent(project_root_), rel);
          size_t comma = path.find(',');
          if (comma != std::string::npos) path = path.substr(0, path.rfind('/', comma));
          if (fs::exists(path)) fs::open_external(fs::is_dir(path) ? path : fs::parent(path));
          else Log::warn("Blender source not found at %s (place the blender folder next to blendity)", path.c_str());
        }
      }
      y += hgt + u.px(4);
    }
    else if (k == '?') {
      size_t bar = rest.find('|');
      std::string q = trim(rest.substr(0, bar)), a = bar == std::string::npos ? "" : trim(rest.substr(bar + 1));
      uint64_t key = ((uint64_t)lesson_ << 32) | li;
      bool shown = revealed.count(key) > 0;
      auto ql = wrap(u.font, "Check yourself: " + q, w - u.px(40));
      auto al = shown ? wrap(u.font, a, w - u.px(40)) : std::vector<std::string>{"Click to reveal the answer"};
      int hgt = (int)(ql.size() + al.size()) * lh + u.px(14);
      Recti br{x0, y, w, hgt};
      bool hot = u.hovered(br);
      u.frame(br, hot ? Color::hex(0x34393F) : Color::hex(0x30353A), Color::hex(0x47505A), u.px(4));
      int a2 = lh - u.px(3);
      u.draw_icon(Icon::Lightbulb, {x0 + u.px(8), y + u.px(6), a2, a2}, u.theme.warning);
      int yy = y + u.px(5);
      for (auto &l : ql) { u.canvas.text(u.font, x0 + u.px(14) + a2, yy, l, u.theme.text_bright); yy += lh; }
      yy += u.px(4);
      for (auto &l : al) { u.canvas.text(u.font, x0 + u.px(14) + a2, yy, l, shown ? u.theme.ok : u.theme.text_dim); yy += lh; }
      if (hot && in.pressed[0]) {
        if (shown) revealed.erase(key); else revealed.insert(key);
      }
      y += hgt + u.px(6);
    }
    else if (k == '!') {
      size_t bar = rest.find('|');
      std::string action = trim(rest.substr(0, bar)), label = bar == std::string::npos ? action : trim(rest.substr(bar + 1));
      int bw = std::min(w, u.font.text_width(label) + u.px(44));
      Recti br{x0, y, bw, u.row_h() + u.px(8)};
      if (u.button(br, "Try it: " + label, false, Icon::Play)) {
        std::string verb = action.substr(0, action.find(':'));
        std::string arg = action.find(':') == std::string::npos ? "" : action.substr(action.find(':') + 1);
        if (verb == "layout") dock_reset(arg);
        else if (verb == "lesson") lesson_ = std::atoi(arg.c_str());
        else if (verb == "tool") {
          tool_ = arg == "View" ? Tool::View : arg == "Rotate" ? Tool::Rotate : arg == "Scale" ? Tool::Scale : arg == "Transform" ? Tool::Transform : Tool::Move;
          dock_open(WindowKind::Scene, false);
        }
        else if (verb == "create") create_object(arg);
        else if (verb == "component") {
          if (selection_.empty()) Log::warn("Select an object first (click it in the Scene view or Hierarchy)");
          else add_component_to_selection(arg);
        }
        else if (verb == "window") {
          for (int k2 = 0; k2 < (int)WindowKind::Count; k2++)
            if (arg == window_title((WindowKind)k2)) dock_open((WindowKind)k2, false);
        }
        else if (verb == "frame") frame_selected();
        else if (verb == "edit") enter_edit_mode();
        else if (verb == "play") enter_play();
        else if (verb == "spawn") spawn_stress_grid(std::atoi(arg.c_str()), "Sphere");
        else if (verb == "research") dock_open(WindowKind::Research);
        else if (verb == "cmd") {
          /* Console commands, ';'-separated (see `help` in the Console). */
          size_t s = 0;
          while (s <= arg.size()) {
            size_t e = arg.find(';', s);
            std::string c = trim(arg.substr(s, e == std::string::npos ? std::string::npos : e - s));
            if (!c.empty()) command(c);
            if (e == std::string::npos) break;
            s = e + 1;
          }
        }
      }
      y += br.h + u.px(6);
    }
    else {
      text_lines(wrap(u.font, line, w), x0, u.theme.text);
      y += u.px(4);
    }
  }

  /* Footer: complete & navigate */
  y += u.px(16);
  u.canvas.hline(x0, x0 + w, y, u.theme.separator);
  y += u.px(12);
  bool is_done = lessons_done_.count(lesson_) > 0;
  int bh = u.row_h() + u.px(8);
  if (lesson_ > 0 && u.button({x0, y, u.px(110), bh}, "< Previous")) lesson_--;
  Recti cb{x0 + w - u.px(240), y, u.px(240), bh};
  if (is_done) {
    if (u.button(cb, lesson_ + 1 < n ? "Completed - Next lesson >" : "Completed!", true, Icon::Check) && lesson_ + 1 < n) lesson_++;
  }
  else if (u.button(cb, "Mark complete & continue", false, Icon::Check)) {
    lessons_done_.insert(lesson_);
    if (lesson_ + 1 < n) lesson_++;
    save_prefs();
  }
  y += bh + u.px(30);
  content_h = y + off - body.y;
  u.end_scroll();
}

}  // namespace bl
