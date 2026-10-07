// SPDX-License-Identifier: GPL-2.0-or-later
// Immediate-mode GUI toolkit.
// Unity's editor is itself built on an immediate-mode GUI (IMGUI: GUILayout /
// EditorGUILayout), so the editor windows here are written the same way.
// Blender's retained-mode equivalent lives in blender/source/blender/editors/interface.
// Theory: Game Engine Architecture Vol. II ch. 12.8 "Overlays and User Interfaces".
#pragma once

#include "../platform/platform.h"
#include "../render/canvas.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace bl::ui {

using Id = uint64_t;

struct Theme {
  uint32_t window = Color::hex(0x383838);
  uint32_t panel = Color::hex(0x383838);
  uint32_t panel_alt = Color::hex(0x3B3B3B);
  uint32_t toolbar = Color::hex(0x191919);
  uint32_t menubar = Color::hex(0x202020);
  uint32_t tabbar = Color::hex(0x282828);
  uint32_t tab_active = Color::hex(0x3C3C3C);
  uint32_t tab_hover = Color::hex(0x323232);
  uint32_t border = Color::hex(0x191919);
  uint32_t separator = Color::hex(0x232323);
  uint32_t text = Color::hex(0xD2D2D2);
  uint32_t text_dim = Color::hex(0x8C8C8C);
  uint32_t text_bright = Color::hex(0xF0F0F0);
  uint32_t field = Color::hex(0x2A2A2A);
  uint32_t field_border = Color::hex(0x212121);
  uint32_t field_hover = Color::hex(0x656565);
  uint32_t focus = Color::hex(0x3A79BB);
  uint32_t button = Color::hex(0x585858);
  uint32_t button_hover = Color::hex(0x676767);
  uint32_t button_active = Color::hex(0x46607C);
  uint32_t selection = Color::hex(0x2C5D87);
  uint32_t selection_dim = Color::hex(0x4D4D4D);
  uint32_t header = Color::hex(0x3E3E3E);
  uint32_t popup = Color::hex(0x2B2B2B);
  uint32_t accent = Color::hex(0xE87D0D);  // Blender orange, used for Learn / research highlights
  uint32_t warning = Color::hex(0xF4BC02);
  uint32_t error = Color::hex(0xE0453A);
  uint32_t ok = Color::hex(0x6CC24A);
  uint32_t axis_x = Color::hex(0xDB3E1D);
  uint32_t axis_y = Color::hex(0x9AF348);
  uint32_t axis_z = Color::hex(0x3A7AF8);
  uint32_t axis_hot = Color::hex(0xF6F232);
};

enum class Icon {
  None, Hand, Move, Rotate, Scale, Transform, Play, Pause, Step, Cube, Camera, Light, Folder, File, Scene, Eye,
  Plus, Gear, Book, Paper, Chart, Info, Warning, Error, ArrowRight, ArrowDown, Check, Close, Grid, Vertex, Face,
  Magnet, Globe, Local, Pivot, Center, Empty, Mesh, Terminal, Flask, Lightbulb, Search, Menu, PushPull
};

struct Input {
  int mx = 0, my = 0, pmx = 0, pmy = 0;
  bool down[3] = {}, pressed[3] = {}, released[3] = {}, double_clicked[3] = {};
  float wheel_x = 0, wheel_y = 0;
  bool key_down[platform::KEY_COUNT] = {};
  bool key_pressed[platform::KEY_COUNT] = {};  // includes auto-repeat
  int mods = 0;
  std::string text;
  std::vector<std::string> dropped;
  int drop_x = 0, drop_y = 0;
  bool shift() const { return mods & platform::MOD_SHIFT; }
  bool ctrl() const { return mods & platform::MOD_CTRL; }
  bool alt() const { return mods & platform::MOD_ALT; }
  int dx() const { return mx - pmx; }
  int dy() const { return my - pmy; }
};

enum class Align { Left, Center, Right };

/* Simple vertical layout used by Inspector-like panels. */
struct Layout {
  Recti area;
  int y = 0;
  int indent = 0;
  int row_h = 20;
  int spacing = 2;
  Recti row(int h = -1) {
    if (h < 0) h = row_h;
    Recti r{area.x + indent, y, area.w - indent, h};
    y += h + spacing;
    return r;
  }
  void space(int px) { y += px; }
};

class Context {
 public:
  Theme theme;
  Font font;
  Canvas canvas;
  Input in;
  float scale = 1.0f;
  double time = 0.0;
  platform::Cursor cursor = platform::Cursor::Arrow;
  /* Set by widgets that need another frame right away (popup results, layout). */
  bool redraw = false;
  /* Absolute time at which the UI wants to be woken (tooltips, caret blink); 0 = never. */
  double next_wakeup = 0;
  std::function<std::string()> clipboard_get;
  std::function<void(const std::string &)> clipboard_set;

  void begin_frame(Image *target, double now);
  void end_frame();

  int px(float v) const { return (int)std::lround(v * scale); }
  int row_h() const { return font.line_height() + px(6); }

  /* --- IDs --- */
  Id id(const char *label) const;
  Id id(const std::string &label) const { return id(label.c_str()); }
  Id id(uint64_t n) const;
  void push_id(Id v) { id_stack_.push_back(v); }
  void push_id(const char *s) { id_stack_.push_back(id(s)); }
  void pop_id() { id_stack_.pop_back(); }

  /* --- interaction state --- */
  bool hovered(const Recti &r) const;  // inside rect, inside clip, not covered by a popup
  bool mouse_in(const Recti &r) const { return r.contains(in.mx, in.my); }
  bool is_active(Id i) const { return active_ == i; }
  bool any_active() const { return active_ != 0; }
  void set_active(Id i) { active_ = i; }
  void clear_active() { active_ = 0; }
  bool wants_keyboard() const { return edit_id_ != 0; }
  bool editing(Id i) const { return edit_id_ == i; }
  void stop_editing() { edit_id_ = 0; }
  /* Programmatically start editing a text/number field (e.g. F2 rename). */
  void begin_edit(Id i, const std::string &text, bool select_all);
  bool mouse_captured() const { return active_ != 0; }
  /* Consumes the click so no other widget reacts this frame. */
  void consume_click() { for (bool &p : in.pressed) p = false; }

  /* --- widgets --- */
  void label(const Recti &r, const std::string &text, uint32_t color = 0, Align align = Align::Left);
  bool button(const Recti &r, const std::string &label, bool selected = false, Icon icon = Icon::None);
  bool icon_button(const Recti &r, Icon icon, bool selected = false, const char *tip = nullptr);
  bool checkbox(const Recti &r, bool &v);
  bool toggle_row(const Recti &r, const std::string &label, bool &v);
  /* Text field; returns true when the value changed. committed set on Enter/blur. */
  bool text_field(Id i, const Recti &r, std::string &s, bool *committed = nullptr, const char *placeholder = nullptr);
  /* Unity-style number field: drag to scrub, click to type. */
  bool float_field(Id i, const Recti &r, float &v, float speed = 0.05f, float mn = -1e30f, float mx = 1e30f,
                   const char *fmt = "%.3g");
  bool int_field(Id i, const Recti &r, int &v, int mn = INT32_MIN, int mx = INT32_MAX);
  /* A label that scrubs a value when dragged (Unity's prefix labels). */
  bool drag_label(Id i, const Recti &r, const std::string &text, float &v, float speed, uint32_t color = 0);
  bool vec3_field(Id i, const Recti &r, Vec3 &v, float speed = 0.05f);
  /* True when number field i (or vec3 field i, component set) was committed
   * by typing this frame; expr is the text, so multi-object editing can
   * evaluate "+=1" or L(0,10) against each object's own value. */
  bool number_committed(Id i, std::string *expr = nullptr, int *component = nullptr) const;
  bool slider(Id i, const Recti &r, float &v, float mn, float mx);
  bool combo(Id i, const Recti &r, int &v, const char *const *options, int count);
  bool color_field(Id i, const Recti &r, Vec3 &c);
  bool foldout(const Recti &r, const std::string &label, bool &open, Icon icon = Icon::None);
  void tooltip(const std::string &text);  // for the most recent hovered widget
  void separator(const Recti &r);

  /* --- scrolling --- */
  /* Returns the scroll offset. Content is clipped to r (minus scrollbar). */
  int begin_scroll(Id i, const Recti &r, int content_h);
  void end_scroll();
  void scroll_to(Id i, int offset) { scroll_[i] = offset; }
  int scroll_offset(Id i) { return scroll_[i]; }

  /* --- popups & menus (drawn on top at end_frame) --- */
  void open_popup(Id i, const Recti &anchor, bool submenu_of_top = false);
  bool popup_open(Id i) const;
  void close_popups();
  void close_popups_above(Id i);
  /* Declares the popup body for this frame; only runs if open. */
  void popup(Id i, int width, std::function<void()> body);
  bool any_popup_open() const { return !popups_.empty(); }
  /* Rows inside popups/menus. */
  bool menu_item(const std::string &label, const char *shortcut = nullptr, bool checked = false, bool enabled = true,
                 Icon icon = Icon::None);
  bool submenu(const std::string &label, int width, std::function<void()> body);
  void menu_separator();
  void menu_label(const std::string &text);
  Recti popup_row(int h = -1);  // reserve a row in the current popup
  int popup_width() const;

  /* --- drawing helpers --- */
  void draw_icon(Icon icon, const Recti &r, uint32_t color);
  void panel_bg(const Recti &r, uint32_t color) { canvas.fill_rect(r, color); }
  void frame(const Recti &r, uint32_t fill, uint32_t border, int radius = 3);

  /* Deferred draw on top of everything (drag previews, overlays). */
  void overlay(std::function<void()> fn) { overlays_.push_back(std::move(fn)); }

 private:
  struct Popup {
    Id id = 0;
    Recti anchor;
    Recti rect;
    bool submenu = false;
    int width = 200;
    int content_h = 0;
    bool declared = false;
    std::function<void()> body;
  };
  struct ScrollState {
    Id id;
    Recti r;
    int content_h;
    int offset;
  };
  bool covered(int x, int y) const;
  void draw_popups();
  void draw_tooltip();
  /* 0 none, 1 changed, 2 committed, 3 cancelled */
  int edit_text(Id i, const Recti &r, std::string &buf);
  bool scrub(Id i, const Recti &r, float &v, float speed, float mn, float mx, bool *clicked);

  std::vector<Id> id_stack_;
  Id active_ = 0;
  Id edit_id_ = 0;
  std::string edit_buf_, edit_original_;
  size_t edit_cursor_ = 0;
  int edit_scroll_ = 0;
  bool edit_select_all_ = false;
  bool edit_seen_ = false;
  bool dragging_value_ = false;
  uint64_t frame_ = 0;
  Id order_last_ = 0;      // last number / text field drawn this frame (Tab order)
  bool focus_next_ = false;  // Tab: the next field drawn starts editing
  Id focus_request_ = 0;   // Shift+Tab: this field starts editing
  void tab_to(Id previous);
  struct NumberCommit {
    Id id = 0;
    std::string expr;
    int component = -1;
    uint64_t frame = ~0ull;
  } commit_;
  float drag_start_value_ = 0;
  int drag_press_x_ = 0;
  std::unordered_map<Id, int> scroll_;
  std::vector<ScrollState> scroll_stack_;
  Id scroll_drag_ = 0;
  int scroll_drag_offset_ = 0;
  std::vector<Popup> popups_;
  std::vector<Recti> popup_rects_prev_;
  int current_layer_ = 0;  // 0 = base UI, k = inside popup k-1
  int popup_cursor_y_ = 0;
  Recti popup_area_;
  Id hover_id_ = 0;
  double hover_start_ = 0;
  std::string tooltip_text_;
  Recti tooltip_anchor_;
  Recti last_widget_;
  bool last_hovered_ = false;
  std::vector<std::function<void()>> overlays_;
  std::unordered_map<Id, int> int_results_;
  std::unordered_map<Id, Vec3> color_results_;
  std::unordered_map<Id, Vec3> color_edit_;
};

/* Evaluates number-field input: "2*3+1", "sqrt(2)", "+=1" / "*=2" (relative to
 * current), Unity's L(a,b) / R(a,b) across `count` selected objects (this one is `index`). */
bool eval_number(const std::string &s, double &out, double current = 0, int index = 0, int count = 1);

}  // namespace bl::ui
