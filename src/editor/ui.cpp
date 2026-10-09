// SPDX-License-Identifier: GPL-2.0-or-later
#include "ui.h"

#include "../core/core.h"

#include <cmath>
#include <cctype>
#include <cstring>
#include <ctime>
#include <vector>

namespace bl::ui {

using namespace platform;

/* ===================================================================== */
/* Frame                                                                  */
/* ===================================================================== */

void Context::begin_frame(Image *target, double now) {
  canvas.begin(target);
  frame_++;
  order_last_ = 0;
  time = now;
  cursor = Cursor::Arrow;
  overlays_.clear();
  current_layer_ = 0;
  tooltip_text_.clear();
  last_hovered_ = false;
  edit_seen_ = false;
  redraw = false;
  next_wakeup = 0;
  id_stack_.clear();
  /* Escape or a click outside closes popups; the click is consumed. */
  if (!popups_.empty()) {
    bool inside = false;
    for (const Recti &r : popup_rects_prev_)
      if (r.contains(in.mx, in.my)) inside = true;
    if ((in.pressed[0] || in.pressed[1] || in.pressed[2]) && !inside) {
      close_popups();
      consume_click();
    }
    else if (in.key_pressed[KEY_ESCAPE] && !wants_keyboard()) {
      close_popups();
      in.key_pressed[KEY_ESCAPE] = false;
    }
  }
  if (!in.down[0] && !in.down[1] && !in.down[2] && !in.released[0] && !in.released[1] && !in.released[2])
    active_ = 0;  // safety: never stay captured without a button held
}

void Context::end_frame() {
  draw_popups();
  for (auto &fn : overlays_) fn();
  draw_tooltip();
  if (edit_id_ && !edit_seen_) edit_id_ = 0;
  if (edit_id_) {
    double blink = (std::floor(time * 2.0) + 1.0) / 2.0;
    if (next_wakeup == 0 || blink < next_wakeup) next_wakeup = blink;
  }
}

Id Context::id(const char *label) const {
  uint64_t h = id_stack_.empty() ? 1469598103934665603ull : id_stack_.back();
  for (const char *p = label; *p; p++) { h ^= (uint8_t)*p; h *= 1099511628211ull; }
  return h ? h : 1;
}

Id Context::id(uint64_t n) const {
  uint64_t h = id_stack_.empty() ? 1469598103934665603ull : id_stack_.back();
  for (int i = 0; i < 8; i++) { h ^= (n >> (i * 8)) & 255; h *= 1099511628211ull; }
  return h ? h : 1;
}

bool Context::covered(int x, int y) const {
  for (size_t k = 0; k < popup_rects_prev_.size(); k++)
    if ((int)k + 1 > current_layer_ && popup_rects_prev_[k].contains(x, y)) return true;
  return false;
}

bool Context::hovered(const Recti &r) const {
  return r.contains(in.mx, in.my) && canvas.clip().contains(in.mx, in.my) && !covered(in.mx, in.my) &&
         (active_ == 0 || current_layer_ > 0 || true);
}

void Context::frame(const Recti &r, uint32_t fill, uint32_t border, int radius) {
  canvas.fill_round_rect(r, radius, fill);
  if (border) canvas.round_rect_outline(r, radius, border);
}

void Context::separator(const Recti &r) { canvas.hline(r.x, r.right(), r.y + r.h / 2, theme.separator); }

/* ===================================================================== */
/* Basic widgets                                                          */
/* ===================================================================== */

void Context::label(const Recti &r, const std::string &text, uint32_t color, Align align) {
  if (!color) color = theme.text;
  int ty = r.y + (r.h - font.line_height()) / 2;
  int tw = font.text_width(text);
  int tx = r.x;
  if (align == Align::Center) tx = r.x + (r.w - tw) / 2;
  else if (align == Align::Right) tx = r.right() - tw;
  canvas.push_clip(r);
  if (align == Align::Left) canvas.text_ellipsis(font, tx, ty, r.w, text, color);
  else canvas.text(font, tx, ty, text, color);
  canvas.pop_clip();
  last_widget_ = r;
  last_hovered_ = hovered(r);
}

static uint64_t rect_key(const Recti &r) { return ((uint64_t)(uint16_t)r.x << 48) ^ ((uint64_t)(uint16_t)r.y << 32) ^ ((uint64_t)r.w << 16) ^ r.h; }

bool Context::button(const Recti &r, const std::string &text, bool selected, Icon icon) {
  Id i = id(text) ^ (rect_key(r) * 0x9E3779B97F4A7C15ull);
  bool hot = hovered(r);
  bool clicked = false;
  if (hot && in.pressed[0]) active_ = i;
  if (active_ == i && in.released[0]) {
    clicked = hot;
    active_ = 0;
  }
  uint32_t bg = selected || (active_ == i && hot) ? theme.button_active : (hot ? theme.button_hover : theme.button);
  frame(r, bg, theme.border, px(3));
  int iw = icon != Icon::None ? r.h - px(6) : 0;
  int tw = font.text_width(text);
  int total = tw + (iw ? iw + (text.empty() ? 0 : px(4)) : 0);
  int x = r.x + std::max(px(4), (r.w - total) / 2);
  if (iw) {
    draw_icon(icon, {x, r.y + px(3), iw, iw}, theme.text_bright);
    x += iw + px(4);
  }
  if (!text.empty()) {
    canvas.push_clip(r);
    canvas.text(font, x, r.y + (r.h - font.line_height()) / 2, text, theme.text_bright);
    canvas.pop_clip();
  }
  last_widget_ = r;
  last_hovered_ = hot;
  return clicked;
}

bool Context::icon_button(const Recti &r, Icon icon, bool selected, const char *tip) {
  Id i = id((uint64_t)icon) ^ (rect_key(r) * 0x9E3779B97F4A7C15ull);
  bool hot = hovered(r);
  bool clicked = false;
  if (hot && in.pressed[0]) active_ = i;
  if (active_ == i && in.released[0]) {
    clicked = hot;
    active_ = 0;
  }
  if (selected) frame(r, theme.button_active, 0, px(3));
  else if (hot) frame(r, theme.button_hover, 0, px(3));
  int s = std::min(r.w, r.h) - px(6);
  draw_icon(icon, {r.x + (r.w - s) / 2, r.y + (r.h - s) / 2, s, s}, selected ? theme.text_bright : theme.text);
  last_widget_ = r;
  last_hovered_ = hot;
  if (tip) tooltip(tip);
  return clicked;
}

bool Context::checkbox(const Recti &r, bool &v) {
  int s = std::min(r.h, font.line_height()) - px(1);
  Recti box{r.x, r.y + (r.h - s) / 2, s, s};
  bool hot = hovered(box.intersect(r).empty() ? r : Recti{r.x, r.y, s + px(2), r.h});
  frame(box, theme.field, hot ? theme.field_hover : theme.field_border, px(2));
  if (v) draw_icon(Icon::Check, box.shrink(px(2)), theme.text_bright);
  bool changed = false;
  if (hot && in.pressed[0]) {
    v = !v;
    changed = true;
  }
  last_widget_ = box;
  last_hovered_ = hot;
  return changed;
}

bool Context::toggle_row(const Recti &r, const std::string &text, bool &v) {
  bool changed = checkbox(r, v);
  int s = std::min(r.h, font.line_height());
  label({r.x + s + px(5), r.y, r.w - s - px(5), r.h}, text);
  if (!changed && hovered(r) && in.pressed[0]) {
    v = !v;
    changed = true;
  }
  return changed;
}

bool Context::foldout(const Recti &r, const std::string &text, bool &open, Icon icon) {
  bool hot = hovered(r);
  int a = font.line_height() - px(4);
  draw_icon(open ? Icon::ArrowDown : Icon::ArrowRight, {r.x, r.y + (r.h - a) / 2, a, a}, theme.text);
  int x = r.x + a + px(3);
  if (icon != Icon::None) {
    draw_icon(icon, {x, r.y + (r.h - a) / 2, a, a}, theme.text);
    x += a + px(4);
  }
  label({x, r.y, r.right() - x, r.h}, text, theme.text_bright);
  if (hot && in.pressed[0]) open = !open;
  last_widget_ = r;
  last_hovered_ = hot;
  return open;
}

void Context::tooltip(const std::string &text) {
  if (!last_hovered_ || active_ != 0 || text.empty()) return;
  Id key = rect_key(last_widget_) ^ (id(text) << 1);
  if (key != hover_id_) {
    hover_id_ = key;
    hover_start_ = time;
  }
  double show_at = hover_start_ + 0.5;
  if (time >= show_at) {
    tooltip_text_ = text;
    tooltip_anchor_ = last_widget_;
  }
  else if (next_wakeup == 0 || show_at < next_wakeup) {
    next_wakeup = show_at;
  }
}

void Context::draw_tooltip() {
  if (tooltip_text_.empty()) return;
  /* Word-wrap to a comfortable width. */
  int maxw = px(380);
  std::vector<std::string> lines;
  std::string cur;
  for (const std::string &para : [&] {
         std::vector<std::string> ps;
         size_t s = 0, e;
         while ((e = tooltip_text_.find('\n', s)) != std::string::npos) { ps.push_back(tooltip_text_.substr(s, e - s)); s = e + 1; }
         ps.push_back(tooltip_text_.substr(s));
         return ps;
       }()) {
    cur.clear();
    for (const std::string &w : split_ws(para)) {
      std::string t = cur.empty() ? w : cur + " " + w;
      if (font.text_width(t) > maxw && !cur.empty()) {
        lines.push_back(cur);
        cur = w;
      }
      else cur = t;
    }
    lines.push_back(cur);
  }
  int w = 0;
  for (auto &l : lines) w = std::max(w, font.text_width(l));
  int lh = font.line_height();
  Recti r{in.mx + px(12), in.my + px(18), w + px(12), (int)lines.size() * lh + px(8)};
  if (r.right() > canvas.width()) r.x = canvas.width() - r.w - px(2);
  if (r.bottom() > canvas.height()) r.y = in.my - r.h - px(6);
  canvas.push_clip({0, 0, canvas.width(), canvas.height()});
  canvas.fill_rect({r.x + 2, r.y + 2, r.w, r.h}, Color::hex(0x000000, 90));
  frame(r, Color::hex(0x1E1E1E), Color::hex(0x5A5A5A), px(3));
  for (size_t i = 0; i < lines.size(); i++) canvas.text(font, r.x + px(6), r.y + px(4) + (int)i * lh, lines[i], theme.text_bright);
  canvas.pop_clip();
}

/* ===================================================================== */
/* Text editing                                                           */
/* ===================================================================== */

static size_t utf8_prev(const std::string &s, size_t i) {
  if (i == 0) return 0;
  i--;
  while (i > 0 && ((unsigned char)s[i] & 0xC0) == 0x80) i--;
  return i;
}
static size_t utf8_after(const std::string &s, size_t i) {
  if (i >= s.size()) return s.size();
  i++;
  while (i < s.size() && ((unsigned char)s[i] & 0xC0) == 0x80) i++;
  return i;
}

int Context::edit_text(Id i, const Recti &r, std::string &buf) {
  edit_seen_ = true;
  int result = 0;
  if (in.pressed[0] && !r.contains(in.mx, in.my)) return 2;  // blur commits
  if (in.pressed[0] && r.contains(in.mx, in.my)) {
    edit_select_all_ = false;
    edit_cursor_ = font.hit_index(buf, in.mx - (r.x + px(4)) + edit_scroll_);
  }
  if (in.double_clicked[0] && r.contains(in.mx, in.my)) edit_select_all_ = true;
  auto erase_selection = [&]() {
    if (edit_select_all_) {
      buf.clear();
      edit_cursor_ = 0;
      edit_select_all_ = false;
      return true;
    }
    return false;
  };
  if (!in.text.empty()) {
    erase_selection();
    buf.insert(edit_cursor_, in.text);
    edit_cursor_ += in.text.size();
    result = 1;
  }
  bool ctrl = in.ctrl();
  if (in.key_pressed[KEY_BACKSPACE]) {
    if (!erase_selection() && edit_cursor_ > 0) {
      size_t p = utf8_prev(buf, edit_cursor_);
      buf.erase(p, edit_cursor_ - p);
      edit_cursor_ = p;
    }
    result = 1;
  }
  if (in.key_pressed[KEY_DELETE]) {
    if (!erase_selection() && edit_cursor_ < buf.size()) buf.erase(edit_cursor_, utf8_after(buf, edit_cursor_) - edit_cursor_);
    result = 1;
  }
  if (in.key_pressed[KEY_LEFT]) { edit_select_all_ = false; edit_cursor_ = utf8_prev(buf, edit_cursor_); }
  if (in.key_pressed[KEY_RIGHT]) { edit_select_all_ = false; edit_cursor_ = utf8_after(buf, edit_cursor_); }
  if (in.key_pressed[KEY_HOME]) { edit_select_all_ = false; edit_cursor_ = 0; }
  if (in.key_pressed[KEY_END]) { edit_select_all_ = false; edit_cursor_ = buf.size(); }
  if (ctrl && in.key_pressed[KEY_A]) edit_select_all_ = true;
  if (ctrl && in.key_pressed[KEY_C] && clipboard_set) clipboard_set(buf);
  if (ctrl && in.key_pressed[KEY_X] && clipboard_set) {
    clipboard_set(buf);
    buf.clear();
    edit_cursor_ = 0;
    result = 1;
  }
  if (ctrl && in.key_pressed[KEY_V] && clipboard_get) {
    std::string clip = clipboard_get();
    for (char &c : clip)
      if (c == '\n' || c == '\r') c = ' ';
    erase_selection();
    buf.insert(edit_cursor_, clip);
    edit_cursor_ += clip.size();
    result = 1;
  }
  if (in.key_pressed[KEY_ENTER] || in.key_pressed[KEY_TAB]) result = 2;
  if (in.key_pressed[KEY_ESCAPE]) result = 3;
  edit_cursor_ = std::min(edit_cursor_, buf.size());

  /* Draw: field, selection, text (scrolled to keep the caret visible), caret. */
  frame(r, theme.field, theme.focus, px(3));
  int inner_w = r.w - px(8);
  int caret_px = font.text_width(buf.c_str(), edit_cursor_);
  if (caret_px - edit_scroll_ > inner_w) edit_scroll_ = caret_px - inner_w;
  if (caret_px - edit_scroll_ < 0) edit_scroll_ = caret_px;
  int tx = r.x + px(4) - edit_scroll_, ty = r.y + (r.h - font.line_height()) / 2;
  canvas.push_clip(r.shrink(px(1)));
  if (edit_select_all_ && !buf.empty())
    canvas.fill_rect({tx, ty, font.text_width(buf), font.line_height()}, theme.selection);
  canvas.text(font, tx, ty, buf, theme.text_bright);
  if (std::fmod(time, 1.0) < 0.5 || result) canvas.fill_rect({tx + caret_px, ty, std::max(1, px(1)), font.line_height()}, theme.text_bright);
  canvas.pop_clip();
  cursor = Cursor::IBeam;
  (void)i;
  return result;
}

bool Context::text_field(Id i, const Recti &r, std::string &s, bool *committed, const char *placeholder) {
  if (committed) *committed = false;
  bool hot = hovered(r);
  last_widget_ = r;
  last_hovered_ = hot;
  const Id before = order_last_;
  order_last_ = i;
  if (edit_id_ != i && (focus_next_ || focus_request_ == i)) {
    focus_next_ = false;
    focus_request_ = 0;
    begin_edit(i, s, true);
  }
  if (edit_id_ != i) {
    if (hot) cursor = Cursor::IBeam;
    frame(r, theme.field, hot ? theme.field_hover : theme.field_border, px(3));
    canvas.push_clip(r.shrink(px(1)));
    int ty = r.y + (r.h - font.line_height()) / 2;
    if (s.empty() && placeholder) canvas.text(font, r.x + px(4), ty, placeholder, theme.text_dim);
    else canvas.text_ellipsis(font, r.x + px(4), ty, r.w - px(8), s, theme.text);
    canvas.pop_clip();
    if (hot && in.pressed[0]) {
      edit_id_ = i;
      edit_seen_ = true;
      edit_buf_ = s;
      edit_original_ = s;
      edit_scroll_ = 0;
      edit_select_all_ = false;
      edit_cursor_ = font.hit_index(s, in.mx - (r.x + px(4)));
      redraw = true;
    }
    return false;
  }
  int res = edit_text(i, r, edit_buf_);
  bool changed = false;
  if (res == 2) tab_to(before);
  if (res == 1) {
    s = edit_buf_;
    changed = true;
  }
  else if (res == 2) {
    s = edit_buf_;
    edit_id_ = 0;
    changed = true;
    if (committed) *committed = true;
  }
  else if (res == 3) {
    changed = s != edit_original_;
    s = edit_original_;
    edit_id_ = 0;
  }
  return changed;
}

/* Tab moves to the next number or text field, Shift+Tab to the previous one
 * (Unity). The key is consumed so the next field doesn't commit at once. */
void Context::tab_to(Id previous) {
  if (!in.key_pressed[KEY_TAB]) return;
  in.key_pressed[KEY_TAB] = false;
  in.text.clear();
  if (in.shift()) focus_request_ = previous;
  else focus_next_ = true;
  redraw = true;
}

void Context::begin_edit(Id i, const std::string &text, bool select_all) {
  edit_id_ = i;
  edit_seen_ = true;  // started after the field was drawn this frame: don't let end_frame() cancel it
  edit_buf_ = text;
  edit_original_ = text;
  edit_cursor_ = text.size();
  edit_scroll_ = 0;
  edit_select_all_ = select_all;
  redraw = true;
}

/* Number fields accept expressions, like Unity's and Blender's numeric inputs:
 *   2*3+1   (1+2)^2   10 % 3   sqrt(2)   sin(pi/4)   max(1, 2, 3)
 *   +=5  -=1  *=2  /=4      relative to the field's current value
 *   L(0, 10)                Unity: spread linearly across the selected objects
 *   R(-1, 1)                Unity: a random value per selected object
 * Functions: sin cos tan asin acos atan atan2 sqrt abs floor ceil round
 * min max pow exp log log10 clamp lerp deg rad; constants pi tau e. */
namespace {
struct Calc {
  const char *p;
  double current = 0;
  int index = 0, count = 1;
  bool ok = true;
  void ws() {
    while (*p == ' ' || *p == '\t') p++;
  }
  bool eat(char c) {
    ws();
    if (*p != c) return false;
    p++;
    return true;
  }
  double call(const std::string &f, const std::vector<double> &a) {
    auto need = [&](size_t n) {
      if (a.size() != n) ok = false;
      return a.size() == n;
    };
    if (f == "sin" && need(1)) return std::sin(a[0]);
    if (f == "cos" && need(1)) return std::cos(a[0]);
    if (f == "tan" && need(1)) return std::tan(a[0]);
    if (f == "asin" && need(1)) return std::asin(a[0]);
    if (f == "acos" && need(1)) return std::acos(a[0]);
    if (f == "atan" && need(1)) return std::atan(a[0]);
    if (f == "atan2" && need(2)) return std::atan2(a[0], a[1]);
    if (f == "sqrt" && need(1)) return std::sqrt(a[0]);
    if (f == "abs" && need(1)) return std::fabs(a[0]);
    if (f == "floor" && need(1)) return std::floor(a[0]);
    if (f == "ceil" && need(1)) return std::ceil(a[0]);
    if (f == "round" && need(1)) return std::round(a[0]);
    if (f == "exp" && need(1)) return std::exp(a[0]);
    if (f == "log" && need(1)) return std::log(a[0]);
    if (f == "log10" && need(1)) return std::log10(a[0]);
    if (f == "pow" && need(2)) return std::pow(a[0], a[1]);
    if (f == "deg" && need(1)) return a[0] * 180.0 / 3.14159265358979323846;
    if (f == "rad" && need(1)) return a[0] * 3.14159265358979323846 / 180.0;
    if (f == "lerp" && need(3)) return a[0] + (a[1] - a[0]) * a[2];
    if (f == "clamp" && need(3)) return std::max(a[1], std::min(a[2], a[0]));
    if ((f == "min" || f == "max") && !a.empty()) {
      double v = a[0];
      for (double x : a) v = f == "min" ? std::min(v, x) : std::max(v, x);
      return v;
    }
    if (f == "l" && need(2)) return count > 1 ? a[0] + (a[1] - a[0]) * index / (double)(count - 1) : a[0];
    if (f == "r" && need(2)) {
      static uint32_t seed = 0x9E3779B9u ^ (uint32_t)std::time(nullptr);
      seed = seed * 1664525u + 1013904223u;
      return a[0] + (a[1] - a[0]) * ((seed >> 8) / 16777216.0);
    }
    ok = false;
    return 0;
  }
  double prim() {
    ws();
    if (eat('(')) {
      double v = expr();
      if (!eat(')')) ok = false;
      return v;
    }
    if (std::isalpha((unsigned char)*p)) {
      std::string name;
      while (std::isalnum((unsigned char)*p) || *p == '_') name += (char)std::tolower((unsigned char)*p++);
      if (eat('(')) {
        std::vector<double> args;
        if (!eat(')')) {
          do args.push_back(expr());
          while (eat(','));
          if (!eat(')')) ok = false;
        }
        return call(name, args);
      }
      if (name == "pi") return 3.14159265358979323846;
      if (name == "tau") return 6.28318530717958647692;
      if (name == "e") return 2.71828182845904523536;
      ok = false;
      return 0;
    }
    char *end;
    double v = std::strtod(p, &end);
    if (end == p) ok = false;
    p = end;
    return v;
  }
  double unary() {
    ws();
    if (*p == '-') { p++; return -unary(); }
    if (*p == '+') { p++; return unary(); }
    return power();
  }
  double power() {
    double v = prim();
    ws();
    if (p[0] == '^' || (p[0] == '*' && p[1] == '*')) {
      p += p[0] == '^' ? 1 : 2;
      return std::pow(v, unary());  // right associative, binds tighter than unary minus on its left
    }
    return v;
  }
  double term() {
    double v = unary();
    for (;;) {
      ws();
      if (*p == '*' && p[1] != '*') { p++; v *= unary(); }
      else if (*p == '/') { p++; double d = unary(); v = d != 0 ? v / d : 0; }
      else if (*p == '%') { p++; double d = unary(); v = d != 0 ? std::fmod(v, d) : 0; }
      else return v;
    }
  }
  double expr() {
    double v = term();
    for (;;) {
      ws();
      if (*p == '+') { p++; v += term(); }
      else if (*p == '-') { p++; v -= term(); }
      else return v;
    }
  }
};
}  // namespace

bool eval_number(const std::string &s, double &out, double current, int index, int count) {
  Calc c{s.c_str()};
  c.current = current;
  c.index = index;
  c.count = std::max(1, count);
  c.ws();
  /* Cleared and confirmed: zero, as in Unity (the field's limits still apply). */
  if (!*c.p) {
    out = 0.0;
    return true;
  }
  /* Relative input: "+=2", "-=2", "*=2", "/=2" (Unity, Blender). */
  char rel = 0;
  if ((c.p[0] == '+' || c.p[0] == '-' || c.p[0] == '*' || c.p[0] == '/') && c.p[1] == '=') {
    rel = c.p[0];
    c.p += 2;
  }
  double v = c.expr();
  c.ws();
  if (!c.ok || *c.p || !std::isfinite(v)) return false;
  switch (rel) {
    case '+': v = current + v; break;
    case '-': v = current - v; break;
    case '*': v = current * v; break;
    case '/': v = v != 0 ? current / v : current; break;
    default: break;
  }
  out = v;
  return true;
}

bool Context::scrub(Id i, const Recti &r, float &v, float speed, float mn, float mx, bool *clicked) {
  bool hot = hovered(r);
  if (clicked) *clicked = false;
  if (hot) cursor = Cursor::ResizeH;
  if (hot && in.pressed[0] && active_ == 0) {
    active_ = i;
    dragging_value_ = false;
    drag_start_value_ = v;
    drag_press_x_ = in.mx;
  }
  bool changed = false;
  if (active_ == i) {
    cursor = Cursor::ResizeH;
    if (std::abs(in.mx - drag_press_x_) > px(3)) dragging_value_ = true;
    if (dragging_value_) {
      float mod = in.shift() ? 4.0f : (in.alt() ? 0.25f : 1.0f);
      float nv = clampf(drag_start_value_ + (in.mx - drag_press_x_) * speed * mod, mn, mx);
      if (nv != v) {
        v = nv;
        changed = true;
      }
    }
    if (in.released[0]) {
      if (!dragging_value_ && clicked) *clicked = true;
      active_ = 0;
    }
  }
  return changed;
}

bool Context::float_field(Id i, const Recti &r, float &v, float speed, float mn, float mx, const char *fmt) {
  last_widget_ = r;
  last_hovered_ = hovered(r);
  const Id before = order_last_;  // the field drawn before this one (Shift+Tab)
  order_last_ = i;
  if (edit_id_ != i && (focus_next_ || focus_request_ == i)) {
    focus_next_ = false;
    focus_request_ = 0;
    begin_edit(i, strprintf("%g", v), true);
  }
  if (edit_id_ == i) {
    int res = edit_text(i, r, edit_buf_);
    if (res == 2 || res == 3) {
      edit_id_ = 0;
      double d;
      tab_to(before);
      if (res == 2 && eval_number(edit_buf_, d, v)) {
        commit_ = {i, edit_buf_, -1, frame_};
        float nv = clampf((float)d, mn, mx);
        bool changed = nv != v;
        v = nv;
        return changed;
      }
    }
    return false;
  }
  bool clicked = false;
  bool changed = scrub(i, r, v, speed, mn, mx, &clicked);
  frame(r, theme.field, last_hovered_ || active_ == i ? theme.field_hover : theme.field_border, px(3));
  /* %.3g would show 2000 as "2e+03": large values get their digits. */
  std::string s = strprintf(std::fabs(v) >= 1000.0f && std::fabs(v) < 1e9f && std::strcmp(fmt, "%.3g") == 0 ? "%.7g" : fmt, v);
  canvas.push_clip(r.shrink(px(1)));
  canvas.text(font, r.x + px(4), r.y + (r.h - font.line_height()) / 2, s, theme.text);
  canvas.pop_clip();
  if (clicked) begin_edit(i, strprintf("%g", v), true);
  return changed;
}

bool Context::int_field(Id i, const Recti &r, int &v, int mn, int mx) {
  float f = (float)v;
  bool changed = float_field(i, r, f, 0.1f, (float)mn, (float)mx, "%.0f");
  int nv = (int)std::lround(f);
  if (nv != v) {
    v = std::max(mn, std::min(mx, nv));
    return true;
  }
  return changed && false;
}

bool Context::drag_label(Id i, const Recti &r, const std::string &text, float &v, float speed, uint32_t color) {
  bool changed = scrub(i, r, v, speed, -1e30f, 1e30f, nullptr);
  label(r, text, color ? color : theme.text);
  return changed;
}

bool Context::vec3_field(Id i, const Recti &r, Vec3 &v, float speed) {
  bool changed = false;
  int gap = px(4);
  int cw = (r.w - 2 * gap) / 3;
  const char *names[3] = {"X", "Y", "Z"};
  uint32_t cols[3] = {theme.axis_x, theme.axis_y, theme.axis_z};
  for (int k = 0; k < 3; k++) {
    Recti c{r.x + k * (cw + gap), r.y, cw, r.h};
    int lw = font.text_width(names[k]) + px(6);
    Id ki = i * 31 + k + 1;
    changed |= drag_label(ki ^ 0x55, {c.x, c.y, lw, c.h}, names[k], v[k], speed, Color::mix(theme.text, cols[k], 0.35f));
    changed |= float_field(ki, {c.x + lw, c.y, c.w - lw, c.h}, v[k], speed);
    if (commit_.frame == frame_ && commit_.id == ki) commit_ = {i, commit_.expr, k, frame_};  // report as the vec3 field
  }
  return changed;
}

bool Context::number_committed(Id i, std::string *expr, int *component) const {
  if (commit_.frame != frame_ || commit_.id != i) return false;
  if (expr) *expr = commit_.expr;
  if (component) *component = commit_.component;
  return true;
}

bool Context::slider(Id i, const Recti &r, float &v, float mn, float mx) {
  bool hot = hovered(r);
  if (hot && in.pressed[0]) active_ = i;
  bool changed = false;
  if (active_ == i) {
    float t = saturate((in.mx - r.x) / (float)std::max(1, r.w));
    float nv = mn + (mx - mn) * t;
    if (nv != v) { v = nv; changed = true; }
    if (in.released[0]) active_ = 0;
  }
  int cy = r.y + r.h / 2;
  canvas.fill_round_rect({r.x, cy - px(2), r.w, px(4)}, px(2), theme.field);
  float t = mx > mn ? saturate((v - mn) / (mx - mn)) : 0;
  canvas.fill_round_rect({r.x, cy - px(2), (int)(r.w * t), px(4)}, px(2), theme.focus);
  canvas.fill_circle(r.x + r.w * t, (float)cy, (float)px(6), hot || active_ == i ? theme.text_bright : theme.text);
  last_widget_ = r;
  last_hovered_ = hot;
  return changed;
}

bool Context::combo(Id i, const Recti &r, int &v, const char *const *options, int count) {
  bool changed = false;
  auto it = int_results_.find(i);
  if (it != int_results_.end()) {
    changed = v != it->second;
    v = it->second;
    int_results_.erase(it);
  }
  bool hot = hovered(r);
  combo_rects_[i] = r;
  frame(r, theme.button, hot ? theme.field_hover : theme.border, px(3));
  std::string cur = (v >= 0 && v < count) ? options[v] : "";
  label({r.x + px(5), r.y, r.w - r.h - px(5), r.h}, cur, theme.text_bright);
  int a = font.line_height() - px(6);
  draw_icon(Icon::ArrowDown, {r.right() - a - px(5), r.y + (r.h - a) / 2, a, a}, theme.text);
  if (hot && in.pressed[0]) {
    if (popup_open(i)) {
      /* Closing it again: only it when it sits on a dialog. */
      if (current_layer_ > 0) {
        for (size_t k = 0; k < popups_.size(); k++)
          if (popups_[k].id == i) {
            popups_.resize(k);
            break;
          }
        redraw = true;
      }
      else close_popups();
    }
    else {
      /* Inside a popup (a dialog such as Preferences): stack the list on it instead of closing it. */
      const bool nested = current_layer_ > 0;
      open_popup(i, r, nested);
      if (nested) {
        popups_.back().submenu = false;  // still drops down below the field
        popups_.back().dropdown = true;
      }
    }
    consume_click();
  }
  popup(i, std::max(r.w, px(120)), [this, i, v, options, count] {
    for (int k = 0; k < count; k++)
      if (menu_item(options[k], nullptr, k == v)) {
        int_results_[i] = k;
        redraw = true;
      }
  });
  last_widget_ = r;
  last_hovered_ = hot;
  return changed;
}

/* HSV <-> RGB for the colour picker (h, s, v in 0..1). */
static Vec3 hsv_to_rgb(float h, float s, float v) {
  h = (h - std::floor(h)) * 6.0f;
  const int i = (int)h % 6;
  const float f = h - std::floor(h), p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
  switch (i) {
    case 0: return {v, t, p};
    case 1: return {q, v, p};
    case 2: return {p, v, t};
    case 3: return {p, q, v};
    case 4: return {t, p, v};
    default: return {v, p, q};
  }
}
static Vec3 rgb_to_hsv(Vec3 c) {
  const float mx = std::max({c.x, c.y, c.z}), mn = std::min({c.x, c.y, c.z}), d = mx - mn;
  float h = 0;
  if (d > 1e-6f) {
    if (mx == c.x) h = (c.y - c.z) / d + (c.y < c.z ? 6.0f : 0.0f);
    else if (mx == c.y) h = (c.z - c.x) / d + 2.0f;
    else h = (c.x - c.y) / d + 4.0f;
    h /= 6.0f;
  }
  return {h, mx > 0 ? d / mx : 0.0f, mx};
}

/* A transparency checkerboard behind a colour with alpha. */
static void checker(Canvas &c, const Recti &r, int cell) {
  c.fill_rect(r, Color::hex(0xCCCCCC));
  c.push_clip(r);
  for (int y = r.y; y < r.bottom(); y += cell)
    for (int x = r.x + (((y - r.y) / cell) % 2) * cell; x < r.right(); x += cell * 2) c.fill_rect({x, y, cell, cell}, Color::hex(0x8C8C8C));
  c.pop_clip();
}

/* Unity's colour field: the colour, with the alpha as a white bar along the
 * bottom (black for the transparent part). Clicking opens a Color window: a
 * saturation / value square with a hue strip, RGB 0-255 / RGB 0-1 / HSV
 * sliders, Alpha, Hex (RRGGBB or RRGGBBAA), and a palette of presets and
 * recently used colours. alpha may be null (no alpha channel). */
bool Context::color_field(Id i, const Recti &r, Vec3 &c, float *alpha) {
  bool changed = false;
  auto it = color_results_.find(i);
  if (it != color_results_.end()) {
    const Vec4 res = it->second;
    changed = c != res.xyz() || (alpha && *alpha != res.w);
    c = res.xyz();
    if (alpha) *alpha = res.w;
    color_results_.erase(it);
  }
  bool hot = hovered(r);
  const Recti inner = r.shrink(px(1));
  canvas.fill_rect(inner, Color::from(c));
  if (alpha) {
    const int bh = std::max(2, px(3));
    Recti bar{inner.x, inner.bottom() - bh, inner.w, bh};
    canvas.fill_rect(bar, 0xFF000000);
    canvas.fill_rect({bar.x, bar.y, (int)(bar.w * saturate(*alpha) + 0.5f), bar.h}, 0xFFFFFFFF);
  }
  canvas.rect_outline(r, hot ? theme.text_bright : theme.border);
  if (hot && in.pressed[0]) {
    color_edit_[i] = Vec4(c.x, c.y, c.z, alpha ? *alpha : 1.0f);
    color_has_alpha_[i] = alpha != nullptr;
    color_before_[i] = color_edit_[i];
    open_popup(i, r);
    consume_click();
  }
  popup(i, px(270), [this, i] {
    Vec4 &e = color_edit_[i];
    const bool has_alpha = color_has_alpha_[i];
    Vec3 hsv = rgb_to_hsv(e.xyz());
    if (color_hue_.count(i) && hsv.y < 1e-4f) hsv.x = color_hue_[i];  // keep the hue of a grey
    bool ch = false;
    /* New colour beside the one it started as (click the old one to go back). */
    Recti sw = popup_row(px(28)).shrink(px(4));
    Recti now{sw.x, sw.y, sw.w / 2, sw.h}, was{sw.x + sw.w / 2, sw.y, sw.w - sw.w / 2, sw.h};
    checker(canvas, sw, px(5));
    canvas.fill_rect(now, Color::from(e.xyz(), has_alpha ? e.w : 1.0f));
    const Vec4 old = color_before_[i];
    canvas.fill_rect(was, Color::from(old.xyz(), has_alpha ? old.w : 1.0f));
    canvas.rect_outline(sw, theme.border);
    if (hovered(was) && in.pressed[0]) {
      e = old;
      ch = true;
      consume_click();
    }
    /* Saturation (across) and value (up) for the current hue, and the hue strip. */
    Recti area = popup_row(px(150)).shrink(px(4));
    const int strip = px(18);
    Recti sq{area.x, area.y, area.w - strip - px(6), area.h}, hue{area.right() - strip, area.y, strip, area.h};
    Image &img = color_square_;
    if (img.width != sq.w || img.height != sq.h || color_square_hue_ != hsv.x) {
      img.resize(sq.w, sq.h);
      for (int y = 0; y < sq.h; y++)
        for (int x = 0; x < sq.w; x++)
          img.row(y)[x] = Color::from(hsv_to_rgb(hsv.x, x / (float)std::max(1, sq.w - 1), 1.0f - y / (float)std::max(1, sq.h - 1)));
      color_square_hue_ = hsv.x;
    }
    canvas.blit(img, sq.x, sq.y);
    for (int y = 0; y < hue.h; y++) canvas.hline(hue.x, hue.right(), hue.y + y, Color::from(hsv_to_rgb(1.0f - y / (float)hue.h, 1, 1)));
    const Id sq_id = i ^ 0x5A5A1ull, hue_id = i ^ 0x5A5A2ull;
    if (hovered(sq) && in.pressed[0]) active_ = sq_id;
    if (hovered(hue) && in.pressed[0]) active_ = hue_id;
    if (active_ == sq_id || active_ == hue_id) {
      if (active_ == sq_id) {
        hsv.y = saturate((in.mx - sq.x) / (float)std::max(1, sq.w - 1));
        hsv.z = saturate(1.0f - (in.my - sq.y) / (float)std::max(1, sq.h - 1));
      }
      else hsv.x = saturate(1.0f - (in.my - hue.y) / (float)std::max(1, hue.h));
      color_hue_[i] = hsv.x;
      const Vec3 rgb = hsv_to_rgb(hsv.x, hsv.y, hsv.z);
      e = Vec4(rgb.x, rgb.y, rgb.z, e.w);
      ch = true;
      if (in.released[0] || !in.down[0]) active_ = 0;
    }
    /* Markers: a ring at the colour, a bar on the hue. */
    const float mx = sq.x + hsv.y * (sq.w - 1), my = sq.y + (1.0f - hsv.z) * (sq.h - 1);
    canvas.circle(mx, my, (float)px(5), hsv.z > 0.5f ? 0xFF000000 : 0xFFFFFFFF, (float)px(1.5f));
    const int hy = hue.y + (int)((1.0f - hsv.x) * hue.h);
    canvas.rect_outline({hue.x - px(2), hy - px(2), hue.w + px(4), px(4)}, 0xFFFFFFFF);
    /* Channels: RGB 0-255 (Unity's default), RGB 0-1, or HSV. */
    static const char *modes[] = {"RGB 0-255", "RGB 0-1.0", "HSV"};
    Recti mr = popup_row(row_h() + px(2));
    combo(i * 31 + 5, {mr.x + px(8), mr.y + px(1), mr.w - px(16), mr.h - px(2)}, color_mode_, modes, 3);
    const char *names[3] = {color_mode_ == 2 ? "H" : "R", color_mode_ == 2 ? "S" : "G", color_mode_ == 2 ? "V" : "B"};
    for (int k = 0; k < 3; k++) {
      Recti row = popup_row(row_h());
      label({row.x + px(8), row.y, px(16), row.h}, names[k]);
      Recti sl{row.x + px(26), row.y, row.w - px(90), row.h};
      /* The slider's track shows where that channel takes the colour. */
      Recti track = sl.shrink(px(5));
      for (int x = 0; x < track.w; x++) {
        const float t = x / (float)std::max(1, track.w - 1);
        Vec3 cc;
        if (color_mode_ == 2) {
          Vec3 h2 = hsv;
          h2[k] = t;
          cc = hsv_to_rgb(h2.x, h2.y, h2.z);
        }
        else {
          cc = e.xyz();
          cc[k] = t;
        }
        canvas.vline(track.x + x, track.y, track.bottom(), Color::from(cc));
      }
      float v = color_mode_ == 2 ? hsv[k] : e[k];
      bool cv = slider(i * 7 + k, sl, v, 0, 1);
      if (color_mode_ == 0) {
        int b = (int)(v * 255.0f + 0.5f);
        if (int_field(i * 13 + k, {row.right() - px(58), row.y + px(1), px(50), row.h - px(2)}, b, 0, 255)) {
          v = b / 255.0f;
          cv = true;
        }
      }
      else cv |= float_field(i * 13 + k, {row.right() - px(58), row.y + px(1), px(50), row.h - px(2)}, v, 0.005f, 0, 1, "%.3f");
      if (cv) {
        if (color_mode_ == 2) {
          hsv[k] = v;
          color_hue_[i] = hsv.x;
          const Vec3 rgb = hsv_to_rgb(hsv.x, hsv.y, hsv.z);
          e = Vec4(rgb.x, rgb.y, rgb.z, e.w);
        }
        else e[k] = v;
        ch = true;
      }
    }
    if (has_alpha) {
      Recti row = popup_row(row_h());
      label({row.x + px(8), row.y, px(16), row.h}, "A");
      Recti sl{row.x + px(26), row.y, row.w - px(90), row.h};
      Recti track = sl.shrink(px(5));
      checker(canvas, track, px(4));
      for (int x = 0; x < track.w; x++) canvas.vline(track.x + x, track.y, track.bottom(), Color::from(e.xyz(), x / (float)std::max(1, track.w - 1)));
      float a = e.w;
      bool ca = slider(i * 7 + 3, sl, a, 0, 1);
      if (color_mode_ == 0) {
        int b = (int)(a * 255.0f + 0.5f);
        if (int_field(i * 13 + 3, {row.right() - px(58), row.y + px(1), px(50), row.h - px(2)}, b, 0, 255)) {
          a = b / 255.0f;
          ca = true;
        }
      }
      else ca |= float_field(i * 13 + 3, {row.right() - px(58), row.y + px(1), px(50), row.h - px(2)}, a, 0.005f, 0, 1, "%.3f");
      if (ca) {
        e.w = a;
        ch = true;
      }
    }
    /* Hex, as Unity shows it: RRGGBB, plus AA when there is an alpha. */
    Recti hr = popup_row(row_h() + px(4));
    auto b8 = [](float v) { return (int)(saturate(v) * 255 + 0.5f); };
    std::string hex = has_alpha ? strprintf("%02X%02X%02X%02X", b8(e.x), b8(e.y), b8(e.z), b8(e.w))
                                : strprintf("%02X%02X%02X", b8(e.x), b8(e.y), b8(e.z));
    label({hr.x + px(8), hr.y, px(70), hr.h}, "Hexadecimal");
    bool done = false;
    if (text_field(i * 17, {hr.x + px(84), hr.y + px(2), hr.w - px(92), hr.h - px(4)}, hex, &done) && done) {
      std::string h = hex[0] == '#' ? hex.substr(1) : hex;
      unsigned v = 0;
      if ((h.size() == 6 || h.size() == 8) && std::sscanf(h.c_str(), "%x", &v) == 1) {
        if (h.size() == 8) {
          e = Vec4(((v >> 24) & 255) / 255.0f, ((v >> 16) & 255) / 255.0f, ((v >> 8) & 255) / 255.0f, (v & 255) / 255.0f);
          if (!has_alpha) e.w = 1.0f;
        }
        else e = Vec4(((v >> 16) & 255) / 255.0f, ((v >> 8) & 255) / 255.0f, (v & 255) / 255.0f, e.w);
        ch = true;
      }
    }
    /* Swatches: presets, then the colours used most recently. */
    static const uint32_t presets[] = {0xFFFFFF, 0x808080, 0x000000, 0xE53935, 0xFB8C00, 0xFDD835,
                                       0x43A047, 0x00ACC1, 0x1E88E5, 0x8E24AA, 0xD81B60, 0x795548};
    std::vector<Vec4> swatches;
    for (uint32_t p : presets) {
      Vec3 v = Color::to_vec(p);
      swatches.push_back(Vec4(v.x, v.y, v.z, 1.0f));
    }
    for (const Vec4 &rc : color_recent_) swatches.push_back(rc);
    const int cell = px(18), per_row = std::max(1, (px(270) - px(16)) / (cell + px(3)));
    for (size_t s = 0; s < swatches.size(); s += (size_t)per_row) {
      Recti row = popup_row(cell + px(3));
      for (size_t k = s; k < std::min(swatches.size(), s + (size_t)per_row); k++) {
        Recti b{row.x + px(8) + (int)(k - s) * (cell + px(3)), row.y + px(1), cell, cell};
        checker(canvas, b, px(3));
        canvas.fill_rect(b, Color::from(swatches[k].xyz(), has_alpha ? swatches[k].w : 1.0f));
        canvas.rect_outline(b, hovered(b) ? theme.text_bright : theme.border);
        if (hovered(b) && in.pressed[0]) {
          e = Vec4(swatches[k].x, swatches[k].y, swatches[k].z, has_alpha ? swatches[k].w : e.w);
          ch = true;
          consume_click();
        }
      }
    }
    if (ch) {
      color_results_[i] = e;
      /* Remember it among the recent colours (most recent first, 12 kept). */
      auto same = [&](const Vec4 &a) { return length(a.xyz() - e.xyz()) < 1e-3f && std::fabs(a.w - e.w) < 1e-3f; };
      color_recent_.erase(std::remove_if(color_recent_.begin(), color_recent_.end(), same), color_recent_.end());
      if (active_ != (i ^ 0x5A5A1ull) && active_ != (i ^ 0x5A5A2ull)) {  // not while dragging in the square
        color_recent_.insert(color_recent_.begin(), e);
        if (color_recent_.size() > 12) color_recent_.resize(12);
      }
      redraw = true;
    }
  });
  last_widget_ = r;
  last_hovered_ = hot;
  return changed;
}

/* ===================================================================== */
/* Scrolling                                                              */
/* ===================================================================== */

int Context::begin_scroll(Id i, const Recti &r, int content_h) {
  int &off = scroll_[i];
  int maxoff = std::max(0, content_h - r.h);
  if (hovered(r) && in.wheel_y != 0 && !in.ctrl()) {
    off -= (int)(in.wheel_y * row_h() * 3);
    in.wheel_y = 0;  // consumed
  }
  bool bar = content_h > r.h;
  int bw = px(10);
  if (bar) {
    Recti track{r.right() - bw, r.y, bw, r.h};
    int th = std::max(px(24), (int)((int64_t)r.h * r.h / std::max(1, content_h)));
    int ty = r.y + (int)((int64_t)(r.h - th) * off / std::max(1, maxoff));
    Recti thumb{track.x + px(2), ty, bw - px(4), th};
    Id sid = i ^ 0x5C011ull;
    if (hovered(track) && in.pressed[0]) {
      active_ = sid;
      scroll_drag_offset_ = thumb.contains(in.mx, in.my) ? in.my - ty : th / 2;
    }
    if (active_ == sid) {
      int nty = in.my - scroll_drag_offset_;
      off = (int)((int64_t)(nty - r.y) * maxoff / std::max(1, r.h - th));
      if (in.released[0]) active_ = 0;
    }
  }
  off = std::max(0, std::min(off, maxoff));
  scroll_stack_.push_back({i, r, content_h, off});
  canvas.push_clip({r.x, r.y, r.w - (bar ? bw : 0), r.h});
  return off;
}

void Context::end_scroll() {
  canvas.pop_clip();
  if (scroll_stack_.empty()) return;
  ScrollState s = scroll_stack_.back();
  scroll_stack_.pop_back();
  if (s.content_h <= s.r.h) return;
  int bw = px(10);
  int maxoff = s.content_h - s.r.h;
  int th = std::max(px(24), (int)((int64_t)s.r.h * s.r.h / s.content_h));
  int ty = s.r.y + (int)((int64_t)(s.r.h - th) * s.offset / std::max(1, maxoff));
  canvas.fill_rect({s.r.right() - bw, s.r.y, bw, s.r.h}, Color::hex(0x2E2E2E));
  canvas.fill_round_rect({s.r.right() - bw + px(2), ty, bw - px(4), th}, px(3), active_ == (s.id ^ 0x5C011ull) ? theme.button_hover : Color::hex(0x5E5E5E));
}

/* ===================================================================== */
/* Popups & menus                                                         */
/* ===================================================================== */

void Context::open_popup(Id i, const Recti &anchor, bool submenu_of_top) {
  if (!submenu_of_top) popups_.clear();
  else if ((int)popups_.size() > current_layer_) popups_.resize(current_layer_);
  Popup p;
  p.id = i;
  p.anchor = anchor;
  p.submenu = submenu_of_top;
  /* Survives this frame even when its owner already declared its menus
   * (opened after the popup() call, e.g. a right-click on empty space). */
  p.declared = true;
  popups_.push_back(p);
  redraw = true;
}

bool Context::popup_open(Id i) const {
  for (auto &p : popups_)
    if (p.id == i) return true;
  return false;
}

void Context::close_popups() {
  popups_.clear();
  popup_rects_prev_.clear();
  redraw = true;
}

void Context::close_popups_above(Id i) {
  for (size_t k = 0; k < popups_.size(); k++)
    if (popups_[k].id == i) {
      popups_.resize(k + 1);
      return;
    }
}

void Context::popup(Id i, int width, std::function<void()> body) {
  for (auto &p : popups_)
    if (p.id == i) {
      p.body = std::move(body);
      p.width = width;
      p.declared = true;
    }
}

Recti Context::popup_row(int h) {
  if (h < 0) h = row_h();
  Recti r{popup_area_.x, popup_cursor_y_, popup_area_.w, h};
  popup_cursor_y_ += h;
  return r;
}

int Context::popup_width() const { return popup_area_.w; }

void Context::draw_popups() {
  std::vector<Recti> rects;
  for (size_t k = 0; k < popups_.size(); k++) {
    if (!popups_[k].declared) {  // owner stopped declaring it: close
      popups_.resize(k);
      break;
    }
    Popup p = popups_[k];
    int h = std::max(p.content_h, row_h());
    Recti r = p.submenu ? Recti{p.anchor.right(), p.anchor.y - px(4), p.width, h} : Recti{p.anchor.x, p.anchor.bottom(), p.width, h};
    if (r.right() > canvas.width()) r.x = p.submenu ? p.anchor.x - p.width : canvas.width() - r.w;
    if (r.bottom() > canvas.height()) r.y = p.submenu ? canvas.height() - r.h : std::max(0, p.anchor.y - r.h);
    r.x = std::max(0, r.x);
    r.y = std::max(0, r.y);
    canvas.push_clip({0, 0, canvas.width(), canvas.height()});
    canvas.fill_rect({r.x + px(3), r.y + px(3), r.w, r.h}, Color::hex(0x000000, 80));
    canvas.fill_rect(r, theme.popup);
    canvas.rect_outline(r, Color::hex(0x161616));
    current_layer_ = (int)k + 1;
    popup_area_ = {r.x + 1, r.y + px(4), r.w - 2, r.h - px(8)};
    popup_cursor_y_ = r.y + px(4);
    canvas.push_clip(r.shrink(1));
    if (p.body) p.body();
    canvas.pop_clip();
    canvas.pop_clip();
    if (k >= popups_.size() || popups_[k].id != p.id) break;  // closed by a click inside
    int content = popup_cursor_y_ - r.y + px(4);
    if (content != popups_[k].content_h) {
      popups_[k].content_h = content;
      redraw = true;
    }
    popups_[k].rect = r;
    rects.push_back(r);
  }
  for (auto &p : popups_) p.declared = false;
  current_layer_ = 0;
  popup_rects_prev_ = rects;
  if (popups_.empty()) popup_rects_prev_.clear();
}

bool Context::menu_item(const std::string &text, const char *shortcut, bool checked, bool enabled, Icon icon) {
  Recti r = popup_row();
  bool hot = hovered(r);
  if (hot && enabled) {
    canvas.fill_rect(r, theme.selection);
    if (current_layer_ > 0 && current_layer_ - 1 < (int)popups_.size()) close_popups_above(popups_[current_layer_ - 1].id);
  }
  int a = font.line_height() - px(4);
  uint32_t col = enabled ? theme.text_bright : theme.text_dim;
  if (checked) draw_icon(Icon::Check, {r.x + px(6), r.y + (r.h - a) / 2, a, a}, col);
  else if (icon != Icon::None) draw_icon(icon, {r.x + px(6), r.y + (r.h - a) / 2, a, a}, col);
  canvas.text(font, r.x + px(10) + a, r.y + (r.h - font.line_height()) / 2, text, col);
  if (shortcut) {
    int sw = font.text_width(shortcut);
    canvas.text(font, r.right() - sw - px(10), r.y + (r.h - font.line_height()) / 2, shortcut, theme.text_dim);
  }
  last_widget_ = r;
  last_hovered_ = hot;
  if (hot && enabled && in.released[0]) {
    /* A dropdown inside a dialog closes alone; anything else closes every popup. */
    if (current_layer_ > 0 && current_layer_ - 1 < (int)popups_.size() && popups_[current_layer_ - 1].dropdown) {
      popups_.resize(current_layer_ - 1);
      redraw = true;
    }
    else close_popups();
    return true;
  }
  return false;
}

bool Context::submenu(const std::string &text, int width, std::function<void()> body) {
  Recti r = popup_row();
  Id sid = id(text) ^ (current_layer_ * 0x1000193ull);
  bool open = popup_open(sid);
  bool hot = hovered(r);
  if (hot || open) canvas.fill_rect(r, hot ? theme.selection : theme.selection_dim);
  int a = font.line_height() - px(4);
  canvas.text(font, r.x + px(10) + a, r.y + (r.h - font.line_height()) / 2, text, theme.text_bright);
  draw_icon(Icon::ArrowRight, {r.right() - a - px(6), r.y + (r.h - a) / 2, a, a}, theme.text);
  if (hot && !open) {
    open_popup(sid, r, true);
    open = true;
  }
  if (open) popup(sid, width, std::move(body));
  return false;
}

void Context::menu_separator() {
  Recti r = popup_row(px(7));
  canvas.hline(r.x + px(6), r.right() - px(6), r.y + r.h / 2, Color::hex(0x454545));
}

void Context::menu_label(const std::string &text) {
  Recti r = popup_row();
  canvas.text(font, r.x + px(8), r.y + (r.h - font.line_height()) / 2, text, theme.text_dim);
}

/* ===================================================================== */
/* Icons (procedural, 16x16 design grid)                                  */
/* ===================================================================== */

void Context::draw_icon(Icon icon, const Recti &r, uint32_t c) {
  float s = std::min(r.w, r.h) / 16.0f;
  float ox = r.x + (r.w - 16 * s) / 2, oy = r.y + (r.h - 16 * s) / 2;
  auto P = [&](float x, float y) { return Vec2(ox + x * s, oy + y * s); };
  auto L = [&](float x0, float y0, float x1, float y1, float t = 1.5f) {
    Vec2 a = P(x0, y0), b = P(x1, y1);
    canvas.line(a.x, a.y, b.x, b.y, c, std::max(1.0f, t * s * 0.75f));
  };
  auto R = [&](float x, float y, float w, float h) {
    canvas.fill_rect({(int)(ox + x * s), (int)(oy + y * s), std::max(1, (int)(w * s)), std::max(1, (int)(h * s))}, c);
  };
  auto T = [&](float x0, float y0, float x1, float y1, float x2, float y2) { canvas.fill_triangle(P(x0, y0), P(x1, y1), P(x2, y2), c); };
  auto C = [&](float x, float y, float rad, bool fill, float t = 1.4f) {
    Vec2 p = P(x, y);
    if (fill) canvas.fill_circle(p.x, p.y, rad * s, c);
    else canvas.circle(p.x, p.y, rad * s, c, std::max(1.0f, t * s * 0.75f));
  };
  switch (icon) {
    case Icon::None: break;
    case Icon::Hand:
      L(5, 14, 4, 8); L(4, 8, 4.5f, 6.5f); L(6, 8, 6, 3); L(8.5f, 8, 8.5f, 2.5f); L(11, 8, 11, 3.5f); L(13, 9, 13, 5.5f);
      L(5, 14, 12, 14); L(13, 9, 12, 14);
      break;
    case Icon::Move:
      L(8, 2, 8, 14); L(2, 8, 14, 8);
      T(8, 0.5f, 5.5f, 3.5f, 10.5f, 3.5f); T(8, 15.5f, 5.5f, 12.5f, 10.5f, 12.5f);
      T(0.5f, 8, 3.5f, 5.5f, 3.5f, 10.5f); T(15.5f, 8, 12.5f, 5.5f, 12.5f, 10.5f);
      break;
    case Icon::Rotate:
      C(8, 8, 5.5f, false);
      T(13.5f, 4, 10.5f, 4.5f, 13, 7.5f);
      break;
    case Icon::Scale:
      canvas.rect_outline({(int)P(2, 6).x, (int)P(2, 6).y, (int)(8 * s), (int)(8 * s)}, c, std::max(1, (int)s));
      L(8, 8, 14, 2); T(14.5f, 1.5f, 10, 2, 14, 6);
      break;
    case Icon::Transform:
      C(8, 8, 6, false); L(8, 3, 8, 13); L(3, 8, 13, 8); R(6.5f, 6.5f, 3, 3);
      break;
    case Icon::Play: T(4, 2, 4, 14, 14, 8); break;
    case Icon::Pause: R(3.5f, 2.5f, 3, 11); R(9.5f, 2.5f, 3, 11); break;
    case Icon::Step: T(2, 2, 2, 14, 11, 8); R(11.5f, 2.5f, 2.5f, 11); break;
    case Icon::Cube:
    case Icon::Mesh:
      L(8, 1.5f, 14, 4.5f); L(8, 1.5f, 2, 4.5f); L(2, 4.5f, 8, 7.5f); L(14, 4.5f, 8, 7.5f);
      L(2, 4.5f, 2, 11.5f); L(14, 4.5f, 14, 11.5f); L(8, 7.5f, 8, 14.5f); L(2, 11.5f, 8, 14.5f); L(14, 11.5f, 8, 14.5f);
      break;
    case Icon::Empty:
      L(8, 2, 8, 14, 1.2f); L(2, 8, 14, 8, 1.2f); C(8, 8, 2.5f, false, 1.2f);
      break;
    case Icon::Camera:
      canvas.rect_outline({(int)P(1, 5).x, (int)P(1, 5).y, (int)(10 * s), (int)(7 * s)}, c, std::max(1, (int)(s * 1.2f)));
      T(11, 8.5f, 15, 5.5f, 15, 11.5f);
      C(4, 3, 1.8f, false, 1.1f); C(8, 3, 1.8f, false, 1.1f);
      break;
    case Icon::Light:
    case Icon::Lightbulb:
      C(8, 6.5f, 4.5f, false); L(6, 11, 6, 13.5f); L(10, 11, 10, 13.5f); L(6, 14.5f, 10, 14.5f);
      break;
    case Icon::Folder:
      R(1, 4, 14, 10); R(1, 2.5f, 6, 2.5f);
      break;
    case Icon::File:
    case Icon::Paper:
      L(3, 1.5f, 10, 1.5f); L(3, 1.5f, 3, 14.5f); L(3, 14.5f, 13, 14.5f); L(13, 14.5f, 13, 4.5f); L(10, 1.5f, 13, 4.5f);
      if (icon == Icon::Paper) { L(5.5f, 6, 10.5f, 6, 1.1f); L(5.5f, 8.5f, 10.5f, 8.5f, 1.1f); L(5.5f, 11, 9, 11, 1.1f); }
      break;
    case Icon::Scene:
      L(2, 3, 14, 3); L(2, 3, 2, 13); L(14, 3, 14, 13); L(2, 13, 14, 13); T(4, 11, 7.5f, 6, 11, 11); C(11, 6, 1.3f, true);
      break;
    case Icon::Eye:
      L(1, 8, 5, 4); L(5, 4, 11, 4); L(11, 4, 15, 8); L(1, 8, 5, 12); L(5, 12, 11, 12); L(11, 12, 15, 8); C(8, 8, 2.2f, true);
      break;
    case Icon::Plus: L(8, 3, 8, 13, 2); L(3, 8, 13, 8, 2); break;
    case Icon::Gear:
      C(8, 8, 4.5f, false, 2.2f);
      for (int k = 0; k < 8; k++) {
        float a = k * kPi / 4;
        L(8 + std::cos(a) * 5, 8 + std::sin(a) * 5, 8 + std::cos(a) * 7, 8 + std::sin(a) * 7, 2.2f);
      }
      break;
    case Icon::Book:
      L(8, 3, 8, 14); L(8, 3, 2, 2); L(2, 2, 2, 13); L(2, 13, 8, 14); L(8, 3, 14, 2); L(14, 2, 14, 13); L(14, 13, 8, 14);
      break;
    case Icon::Chart:
      R(2, 9, 3, 5); R(6.5f, 5, 3, 9); R(11, 2, 3, 12);
      break;
    case Icon::Info: C(8, 8, 6.5f, false); R(7.2f, 7, 1.8f, 5); R(7.2f, 4, 1.8f, 1.8f); break;
    case Icon::Warning: T(8, 1.5f, 15, 14.5f, 1, 14.5f); break;
    case Icon::Error: C(8, 8, 6.5f, true); break;
    case Icon::ArrowRight: T(5, 3, 5, 13, 12, 8); break;
    case Icon::ArrowDown: T(3, 5, 13, 5, 8, 12); break;
    case Icon::Check: L(3, 8.5f, 6.5f, 12, 2); L(6.5f, 12, 13, 4, 2); break;
    case Icon::Close: L(4, 4, 12, 12, 1.8f); L(12, 4, 4, 12, 1.8f); break;
    case Icon::Grid:
      for (float k = 2; k <= 14; k += 4) { L(k, 2, k, 14, 1.1f); L(2, k, 14, k, 1.1f); }
      break;
    case Icon::Vertex: L(3, 13, 8, 3, 1.1f); L(8, 3, 13, 13, 1.1f); L(3, 13, 13, 13, 1.1f); C(8, 3, 2.2f, true); C(3, 13, 2.2f, true); C(13, 13, 2.2f, true); break;
    case Icon::Face: T(3, 13, 8, 3, 13, 13); break;
    case Icon::Magnet: L(4, 3, 4, 9); L(12, 3, 12, 9); C(8, 9, 4, false); R(2.5f, 2, 3, 2.5f); R(10.5f, 2, 3, 2.5f); break;
    case Icon::Globe: C(8, 8, 6, false); L(2, 8, 14, 8, 1.1f); L(8, 2, 8, 14, 1.1f); C(8, 8, 3, false, 1.1f); break;
    case Icon::Local: L(4, 12, 4, 3); L(4, 12, 13, 12); T(4, 1, 2, 4, 6, 4); T(15, 12, 12, 10, 12, 14); break;
    case Icon::Pivot: C(8, 8, 2.5f, true); C(8, 8, 6, false, 1.1f); break;
    case Icon::Center: R(3, 3, 10, 10); break;
    case Icon::Terminal: L(2, 3, 14, 3); L(2, 3, 2, 13); L(14, 3, 14, 13); L(2, 13, 14, 13); L(4, 6, 7, 8); L(7, 8, 4, 10); L(8, 11, 12, 11); break;
    case Icon::Flask: L(6, 2, 10, 2); L(6.5f, 2, 6.5f, 7); L(9.5f, 2, 9.5f, 7); L(6.5f, 7, 2, 14); L(9.5f, 7, 14, 14); L(2, 14, 14, 14); T(4, 11, 12, 11, 13.5f, 13.5f); T(4, 11, 13.5f, 13.5f, 2.5f, 13.5f); break;
    case Icon::Search: C(6.5f, 6.5f, 4.5f, false); L(10, 10, 14, 14, 2); break;
    case Icon::Menu: R(2, 3, 12, 2); R(2, 7, 12, 2); R(2, 11, 12, 2); break;
    case Icon::PushPull:  /* a slab with a face lifted off it (SketchUp's Push/Pull) */
      L(2, 11, 9, 14); L(9, 14, 14, 11); L(2, 11, 2, 13); L(14, 11, 14, 13); L(2, 13, 9, 15.5f); L(9, 15.5f, 14, 13);
      L(2, 7, 9, 10); L(9, 10, 14, 7); L(14, 7, 7, 4); L(7, 4, 2, 7);
      L(8, 1, 8, 5, 1.4f); T(8, 0, 5.5f, 2.5f, 10.5f, 2.5f);
      break;
  }
}

}  // namespace bl::ui
