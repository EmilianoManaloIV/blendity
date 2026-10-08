// SPDX-License-Identifier: GPL-2.0-or-later
// Hierarchy (Blender: Outliner), Inspector (Blender: Properties editor),
// Project (Blender: File/Asset Browser), Console (Blender: Info editor),
// Profiler (Blender: Statistics overlay / --debug timings).
#include "editor.h"

#include "../core/core.h"
#include "../core/jobs.h"
#include "../research/research.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <functional>

namespace bl {

using namespace platform;
using ui::Icon;

static Icon object_icon(const GameObject &g) {
  if (g.get<Camera>()) return Icon::Camera;
  if (g.get<Light>()) return Icon::Light;
  if (g.get<MeshFilter>()) return Icon::Cube;
  return Icon::Empty;
}

/* ===================================================================== */
/* Hierarchy                                                              */
/* ===================================================================== */

void Editor::draw_hierarchy(const Recti &r) {
  auto &u = ui_;
  auto &in = u.in;
  int bh = u.row_h() + u.px(6);
  Recti bar{r.x, r.y, r.w, bh};
  u.canvas.fill_rect(bar, Color::hex(0x2F2F2F));
  int h = bh - u.px(8);
  Recti plus{bar.x + u.px(5), bar.y + u.px(4), h + u.px(16), h};
  bool plus_hot = u.hovered(plus);
  u.frame(plus, plus_hot ? u.theme.button_hover : u.theme.button, u.theme.border, u.px(3));
  u.draw_icon(Icon::Plus, {plus.x + u.px(3), plus.y + u.px(3), h - u.px(6), h - u.px(6)}, u.theme.text_bright);
  u.draw_icon(Icon::ArrowDown, {plus.x + h, plus.y + u.px(5), u.px(9), u.px(9)}, u.theme.text);
  ui::Id create_menu = u.id("hier_create");
  if (plus_hot && in.pressed[0]) { u.open_popup(create_menu, plus); u.consume_click(); }
  auto create_body = [this](bool child) {
    auto &u = ui_;
    if (u.menu_item("Create Empty")) create_object("Empty", child);
    u.submenu("3D Object", u.px(170), [this, child] {
      for (const char *k : {"Cube", "Sphere", "Icosphere", "Cylinder", "Cone", "Torus", "Plane", "Quad"})
        if (ui_.menu_item(k, nullptr, false, true, Icon::Cube)) create_object(k, child);
    });
    u.submenu("Shapes (Parametric)", u.px(190), [this, child] {
      for (int k = 0; k < kShapeCount; k++)
        if (ui_.menu_item(kShapeNames[k], nullptr, false, true, Icon::Cube)) create_object(std::string("Shape: ") + kShapeNames[k], child);
    });
    u.submenu("Light", u.px(190), [this, child] {
      if (ui_.menu_item("Directional Light", nullptr, false, true, Icon::Light)) create_object("Directional Light", child);
      if (ui_.menu_item("Point Light", nullptr, false, true, Icon::Light)) create_object("Point Light", child);
      if (ui_.menu_item("Spot Light", nullptr, false, true, Icon::Light)) create_object("Spot Light", child);
      if (ui_.menu_item("Area Light", nullptr, false, true, Icon::Light)) create_object("Area Light", child);
    });
    if (u.menu_item("Camera", nullptr, false, true, Icon::Camera)) create_object("Camera", child);
  };
  u.popup(create_menu, u.px(200), [create_body] { create_body(false); });
  u.text_field(u.id("hier_search"), {plus.right() + u.px(6), bar.y + u.px(4), bar.right() - plus.right() - u.px(12), h},
               hierarchy_search_, nullptr, "Search...");

  /* Flatten the visible tree (only expanded branches; virtualised drawing). */
  struct Row { GameObject *g; int depth; };
  std::vector<Row> rows;
  std::string filter = to_lower(hierarchy_search_);
  if (!filter.empty()) {
    scene_->for_each_ordered([&](GameObject &g, int) {
      if (to_lower(g.name).find(filter) != std::string::npos) rows.push_back({&g, 0});
    });
  }
  else {
    std::function<void(GameObject *, int)> walk = [&](GameObject *g, int d) {
      rows.push_back({g, d});
      if (expanded_.count(g->id))
        for (GameObject *c : g->children) walk(c, d + 1);
    };
    for (GameObject *g : scene_->roots) walk(g, 0);
  }

  int rh = u.row_h();
  Recti list{r.x, bar.bottom(), r.w, r.h - bh};
  /* Scene header row like Unity ("SampleScene" with its own foldout). */
  Recti head{list.x, list.y, list.w, rh + u.px(2)};
  u.canvas.fill_rect(head, Color::hex(0x323232));
  int a = u.font.line_height() - u.px(2);
  u.draw_icon(Icon::Scene, {head.x + u.px(8), head.y + (head.h - a) / 2, a, a}, u.theme.text);
  u.label({head.x + u.px(14) + a, head.y, head.w - a - u.px(20), head.h}, scene_->name + (scene_dirty_ ? "*" : ""), u.theme.text_bright);
  Recti body{list.x, head.bottom(), list.w, list.bottom() - head.bottom()};

  int content_h = (int)rows.size() * rh + rh * 2;
  ui::Id sid = u.id("hier_scroll");
  if (scroll_to_active_) {
    for (size_t i = 0; i < rows.size(); i++)
      if (rows[i].g->id == active_) {
        int cur = u.scroll_offset(sid), y = (int)i * rh;
        if (y < cur || y + rh > cur + body.h) u.scroll_to(sid, std::max(0, y - body.h / 2));
        break;
      }
    scroll_to_active_ = false;
  }
  int off = u.begin_scroll(sid, body, content_h);
  int first = std::max(0, off / rh), last = std::min((int)rows.size(), (off + body.h) / rh + 2);
  GameObject *drop_target = nullptr;
  int drop_mode = -1;  // 0 into, 1 before, 2 after
  bool hovered_row = false;
  for (int i = first; i < last; i++) {
    GameObject *g = rows[i].g;
    Recti row{body.x, body.y + i * rh - off, body.w, rh};
    bool sel = is_selected(g->id);
    bool hot = u.hovered(row);
    if (sel) u.canvas.fill_rect(row, focused_ == WindowKind::Hierarchy ? u.theme.selection : u.theme.selection_dim);
    else if (hot) u.canvas.fill_rect(row, Color::hex(0x444444));
    int x = row.x + u.px(6) + rows[i].depth * u.px(14);
    int arrow = u.font.line_height() - u.px(5);
    if (!g->children.empty() && filter.empty()) {
      Recti ar{x, row.y, arrow + u.px(4), rh};
      u.draw_icon(expanded_.count(g->id) ? Icon::ArrowDown : Icon::ArrowRight, {x, row.y + (rh - arrow) / 2, arrow, arrow}, u.theme.text_dim);
      if (u.hovered(ar) && in.pressed[0]) {
        if (expanded_.count(g->id)) expanded_.erase(g->id);
        else expanded_.insert(g->id);
        u.consume_click();
      }
    }
    x += arrow + u.px(4);
    int ic = u.font.line_height() - u.px(2);
    bool on = g->active_in_hierarchy();
    u.draw_icon(object_icon(*g), {x, row.y + (rh - ic) / 2, ic, ic}, on ? u.theme.text : u.theme.text_dim);
    x += ic + u.px(5);
    if (rename_id_ == g->id) {
      ui::Id rid = u.id("rename") ^ g->id;
      if (!u.editing(rid)) u.begin_edit(rid, rename_buf_, true);
      bool done = false;
      u.text_field(rid, {x, row.y + 1, row.right() - x - u.px(4), rh - 2}, rename_buf_, &done);
      if (done || !u.editing(rid)) {
        if (!rename_buf_.empty() && rename_buf_ != g->name) {
          g->name = rename_buf_;
          mark_changed("Rename");
        }
        rename_id_ = 0;
      }
    }
    else {
      u.label({x, row.y, row.right() - x, rh}, g->name, on ? (sel ? u.theme.text_bright : u.theme.text) : u.theme.text_dim);
    }
    if (hot) {
      hovered_row = true;
      if (in.pressed[0]) {
        if (in.shift() && last_clicked_) {
          /* Range select in visible order. */
          int a0 = -1, a1 = i;
          for (int k = 0; k < (int)rows.size(); k++)
            if (rows[k].g->id == last_clicked_) a0 = k;
          if (a0 >= 0) {
            if (!in.ctrl()) selection_.clear();
            for (int k = std::min(a0, a1); k <= std::max(a0, a1); k++) select(rows[k].g->id, SEL_ADD);
          }
        }
        else if (in.ctrl()) select(g->id, SEL_TOGGLE);
        else if (!sel) select(g->id);
        else active_ = g->id;
        last_clicked_ = g->id;
        hier_drag_ = g->id;
        hier_dragging_ = false;
        hier_press_y_ = in.my;
      }
      if (in.released[0] && !hier_dragging_ && sel && !in.ctrl() && !in.shift() && hier_drag_ == g->id && selection_.size() > 1) select(g->id);
      if (in.double_clicked[0]) frame_selected();
      if (in.pressed[1]) {
        if (!sel) select(g->id);
        u.open_popup(u.id("hier_ctx"), {in.mx, in.my, 0, 0});
      }
      if (hier_dragging_ && hier_drag_ != g->id) {
        int ry = in.my - row.y;
        drop_target = g;
        drop_mode = ry < rh / 4 ? 1 : (ry > rh * 3 / 4 ? 2 : 0);
      }
    }
  }
  if (hier_drag_ && in.down[0] && std::abs(in.my - hier_press_y_) > u.px(5)) hier_dragging_ = true;
  if (hier_dragging_) {
    u.cursor = Cursor::Hand;
    if (drop_target) {
      int i = 0;
      for (int k = first; k < last; k++)
        if (rows[k].g == drop_target) i = k;
      Recti row{body.x, body.y + i * rh - off, body.w, rh};
      if (drop_mode == 0) u.canvas.rect_outline(row, u.theme.focus, u.px(2));
      else u.canvas.fill_rect({row.x + u.px(20), drop_mode == 1 ? row.y : row.bottom() - u.px(2), row.w - u.px(20), u.px(2)}, u.theme.focus);
    }
    if (in.released[0]) {
      auto moving = selected_objects(true);
      if (drop_target) {
        bool valid = true;
        for (GameObject *mv : moving)
          if (scene_->is_ancestor(mv, drop_target)) valid = false;
        if (valid) {
          for (GameObject *mv : moving) {
            if (drop_mode == 0) {
              scene_->set_parent(mv, drop_target);
              expanded_.insert(drop_target->id);
            }
            else {
              GameObject *p = drop_target->parent;
              auto &sib = p ? p->children : scene_->roots;
              if (mv->parent == p) sib.erase(std::remove(sib.begin(), sib.end(), mv), sib.end());
              int idx = (int)(std::find(sib.begin(), sib.end(), drop_target) - sib.begin()) + (drop_mode == 2 ? 1 : 0);
              if (mv->parent == p) {
                sib.insert(sib.begin() + std::min(idx, (int)sib.size()), mv);
              }
              else scene_->set_parent(mv, p, idx);
            }
          }
          mark_changed("Reparent");
        }
      }
      else if (!hovered_row && u.hovered(body)) {
        for (GameObject *mv : moving) scene_->set_parent(mv, nullptr);  // drop on empty space = unparent
        mark_changed("Unparent");
      }
    }
  }
  if (!in.down[0]) {
    hier_drag_ = 0;
    hier_dragging_ = false;
  }
  /* Click on empty space clears selection; right-click opens create menu. */
  if (!hovered_row && u.hovered(body) && in.pressed[0] && !edit_mode_) clear_selection();
  if (!hovered_row && u.hovered(body) && in.pressed[1]) u.open_popup(create_menu, {in.mx, in.my, 0, 0});
  u.end_scroll();

  bool has_sel = !selection_.empty();
  u.popup(u.id("hier_ctx"), u.px(220), [this, has_sel, create_body] {
    auto &u = ui_;
    if (u.menu_item("Rename", "F2", false, has_sel)) {
      rename_id_ = active_;
      if (GameObject *g = scene_->find(active_)) rename_buf_ = g->name;
    }
    if (u.menu_item("Duplicate", "Ctrl+D", false, has_sel)) duplicate_selected();
    if (u.menu_item("Delete", "Del", false, has_sel)) delete_selected();
    if (u.menu_item("Frame Selected", "F", false, has_sel)) frame_selected();
    if (u.menu_item("Select Children", nullptr, false, has_sel)) {
      /* Unity: Select Children - the selection and everything under it. */
      std::vector<GameObject *> stack = selected_objects(false);
      while (!stack.empty()) {
        GameObject *g = stack.back();
        stack.pop_back();
        for (GameObject *c : g->children) {
          if (!is_selected(c->id)) selection_.push_back(c->id);
          stack.push_back(c);
          expanded_.insert(g->id);
        }
      }
    }
    if (u.menu_item("Clear Parent", nullptr, false, has_sel)) {
      for (GameObject *g : selected_objects(true)) scene_->set_parent(g, nullptr);
      mark_changed("Clear Parent");
    }
    if (u.menu_item("Join", "Ctrl+J", false, selection_.size() > 1)) join_selected();
    u.submenu("Boolean", u.px(220), [this] {
      static const char *kOps[] = {"Difference", "Union", "Intersect"};
      for (int op : {1, 0, 2})
        if (ui_.menu_item(kOps[op], nullptr, false, selection_.size() > 1)) boolean_selected(op, true);
      ui_.menu_separator();
      for (int op : {1, 0, 2})
        if (ui_.menu_item(std::string(kOps[op]) + " Modifier", nullptr, false, selection_.size() > 1)) boolean_selected(op, false);
    });
    u.tooltip("The active object (selected last) is cut by the others. Modifier: the cut stays live.");
    if (u.menu_item("Separate By Loose Parts", nullptr, false, has_sel)) separate("loose");
    if (u.menu_item("Export Selection...", nullptr, false, has_sel)) open_export_dialog(export_opts_.format);
    u.menu_separator();
    u.menu_label("Create Child");
    create_body(true);
  });

  /* Keyboard navigation (Unity: arrows move selection, left/right fold). */
  if (focused_ == WindowKind::Hierarchy && !u.wants_keyboard() && !rows.empty()) {
    int cur = -1;
    for (int i = 0; i < (int)rows.size(); i++)
      if (rows[i].g->id == active_) cur = i;
    if (in.key_pressed[KEY_DOWN]) select(rows[std::min((int)rows.size() - 1, cur + 1)].g->id);
    if (in.key_pressed[KEY_UP] && cur > 0) select(rows[cur - 1].g->id);
    if (in.key_pressed[KEY_RIGHT] && cur >= 0) expanded_.insert(rows[cur].g->id);
    if (in.key_pressed[KEY_LEFT] && cur >= 0) expanded_.erase(rows[cur].g->id);
  }
}

/* ===================================================================== */
/* Inspector                                                              */
/* ===================================================================== */

/* One edited Inspector field, so the same edit can be applied to every
 * selected object (Unity's multi-object editing). A typed expression is kept
 * and re-evaluated per object: "+=1" adds 1 to each object's own value. */
struct FieldEdit {
  enum Kind { Float, Int, Bool, Vec, Color, Enum, Text } kind = Float;
  std::string name;
  float f_old = 0, f_new = 0;
  int i_new = 0;
  bool b_new = false;
  Vec3 v_old, v_new;
  std::string s_new;
  std::string expr;    // typed text, empty when dragged / picked
  int component = -1;  // vec3: which axis was edited (-1: unknown / all)
};

/* Applies a FieldEdit to another object's matching field. */
struct ApplyEditReflector : Reflector {
  const FieldEdit &e;
  int index, count;
  bool use_old;  // evaluate from the recorded old value (the object the edit was made on)
  bool applied = false;
  ApplyEditReflector(const FieldEdit &edit, int i, int n, bool from_old) : e(edit), index(i), count(n), use_old(from_old) {}
  bool match(const char *n, FieldEdit::Kind k) const { return e.kind == k && e.name == n; }
  float eval(float current, float fallback) const {
    double d;
    if (!e.expr.empty() && ui::eval_number(e.expr, d, current, index, count)) return (float)d;
    return fallback;
  }
  void field(const char *n, float &v, float, float mn, float mx) override {
    if (!match(n, FieldEdit::Float)) return;
    v = clampf(eval(use_old ? e.f_old : v, e.f_new), mn, mx);
    applied = true;
  }
  void field(const char *n, int &v, int mn, int mx) override {
    if (!match(n, FieldEdit::Int)) return;
    v = std::max(mn, std::min(mx, (int)std::lround(eval(use_old ? e.f_old : (float)v, (float)e.i_new))));
    applied = true;
  }
  void field(const char *n, bool &v) override {
    if (match(n, FieldEdit::Bool)) { v = e.b_new; applied = true; }
  }
  void field(const char *n, Vec3 &v) override {
    if (!match(n, FieldEdit::Vec)) return;
    Vec3 cur = use_old ? e.v_old : v;
    for (int k = 0; k < 3; k++)
      if (e.component < 0 ? e.v_new[k] != e.v_old[k] : k == e.component) v[k] = eval(cur[k], e.v_new[k]);
    applied = true;
  }
  void color(const char *n, Vec3 &v) override {
    if (match(n, FieldEdit::Color)) { v = e.v_new; applied = true; }
  }
  void enumeration(const char *n, int &v, const char *const *, int) override {
    if (match(n, FieldEdit::Enum)) { v = e.i_new; applied = true; }
  }
  void text(const char *n, std::string &v) override {
    if (match(n, FieldEdit::Text)) { v = e.s_new; applied = true; }
  }
  void mesh(const char *, MeshPtr &) override {}
  void texture(const char *, TextureRef &) override {}
};

/* Draws any component through the Reflector interface (Unity's default
 * inspector / Blender's RNA-driven property panels). */
struct InspectorReflector : Reflector {
  Editor &ed;
  ui::Context &u;
  ui::Layout &lay;
  bool changed = false;
  std::vector<FieldEdit> edits;  // what changed this frame (for multi-object editing)
  int label_w;
  InspectorReflector(Editor &e, ui::Context &c, ui::Layout &l) : ed(e), u(c), lay(l) { label_w = std::max(u.px(110), l.area.w * 2 / 5); }
  ui::Id fid(const char *n) { return u.id(n); }
  Recti field_rect(Recti row) { return {row.x + label_w, row.y, row.w - label_w - u.px(6), row.h}; }
  Recti label_rect(Recti row) { return {row.x + u.px(4), row.y, label_w - u.px(8), row.h}; }
  FieldEdit &record(FieldEdit::Kind k, const char *n) {
    edits.emplace_back();
    edits.back().kind = k;
    edits.back().name = n;
    return edits.back();
  }
  void field(const char *n, float &v, float speed, float mn, float mx) override {
    Recti row = lay.row();
    float old = v;
    bool ch = u.drag_label(fid(n) ^ 0xAB, label_rect(row), n, v, speed);
    v = clampf(v, mn, mx);
    ch |= u.float_field(fid(n), field_rect(row), v, speed, mn, mx);
    if (ch) {
      FieldEdit &e = record(FieldEdit::Float, n);
      e.f_old = old;
      e.f_new = v;
      u.number_committed(fid(n), &e.expr);
    }
    changed |= ch;
  }
  void field(const char *n, int &v, int mn, int mx) override {
    Recti row = lay.row();
    u.label(label_rect(row), n);
    int old = v;
    bool ch = u.int_field(fid(n), field_rect(row), v, mn, mx);
    if (ch) {
      FieldEdit &e = record(FieldEdit::Int, n);
      e.f_old = (float)old;
      e.i_new = v;
      u.number_committed(fid(n), &e.expr);
    }
    changed |= ch;
  }
  /* Sample counts: type any number, halve / double it, or pick a power of two
   * (16, 32, 64 ... like Cycles' presets). Noise falls as 1/sqrt(samples). */
  void samples(const char *n, int &v, int mn, int mx) override {
    Recti row = lay.row();
    u.label(label_rect(row), n);
    Recti f = field_rect(row);
    const int bw = u.px(24), gap = u.px(2), cw = std::max(u.px(64), f.w * 2 / 5);
    int old = v;
    bool ch = u.int_field(fid(n), {f.x, f.y, f.w - cw - 2 * bw - 3 * gap, f.h}, v, mn, mx);
    Recti half{f.right() - cw - 2 * bw - 2 * gap, f.y, bw, f.h}, dbl{half.right() + gap, f.y, bw, f.h};
    if (u.button(half, "/2")) { v = std::max(mn, v / 2); ch = true; }
    u.tooltip("Halve the samples (about 1.4x more noise, twice as fast).");
    if (u.button(dbl, "x2")) { v = std::min(mx, v * 2); ch = true; }
    u.tooltip("Double the samples (about 30% less noise, twice as long).");
    static const char *const presets[] = {"1",   "2",   "4",    "8",    "16",   "32",    "64",    "128",  "256",
                                          "512", "1024", "2048", "4096", "8192", "16384", "32768", "65536"};
    int pi = -1;
    for (int k = 0; k < 17; k++)
      if (v == (1 << k)) pi = k;
    int sel = pi < 0 ? 0 : pi;
    ui::Id cid = fid(n) ^ 0x5A5Aull;
    Recti cr{dbl.right() + gap, f.y, f.right() - dbl.right() - gap, f.h};
    if (pi < 0) {
      /* Not a power of two: show "Custom" over the dropdown. */
      if (u.combo(cid, cr, sel, presets, 17)) { v = 1 << sel; ch = true; }
      u.canvas.fill_rect(cr.shrink(u.px(2)), u.theme.field);
      u.label({cr.x + u.px(5), cr.y, cr.w - u.px(16), cr.h}, "Custom", u.theme.text_dim);
    }
    else if (u.combo(cid, cr, sel, presets, 17)) { v = 1 << sel; ch = true; }
    u.tooltip("Power-of-two presets: each step doubles the samples.");
    v = std::max(mn, std::min(mx, v));
    if (ch && v != old) {
      FieldEdit &e = record(FieldEdit::Int, n);
      e.f_old = (float)old;
      e.i_new = v;
      u.number_committed(fid(n), &e.expr);
      changed = true;
    }
  }
  void field(const char *n, bool &v) override {
    Recti row = lay.row();
    u.label(label_rect(row), n);
    if (u.checkbox(field_rect(row), v)) {
      record(FieldEdit::Bool, n).b_new = v;
      changed = true;
    }
  }
  void field(const char *n, Vec3 &v) override {
    Recti row = lay.row();
    u.label(label_rect(row), n);
    Vec3 old = v;
    if (u.vec3_field(fid(n), field_rect(row), v, 0.05f)) {
      FieldEdit &e = record(FieldEdit::Vec, n);
      e.v_old = old;
      e.v_new = v;
      u.number_committed(fid(n), &e.expr, &e.component);
      changed = true;
    }
  }
  void color(const char *n, Vec3 &v) override {
    Recti row = lay.row();
    u.label(label_rect(row), n);
    if (u.color_field(fid(n), field_rect(row), v)) {
      record(FieldEdit::Color, n).v_new = v;
      changed = true;
    }
  }
  void color_alpha(const char *n, Vec3 &v, const char *alpha_name, float &a) override {
    Recti row = lay.row();
    u.label(label_rect(row), n);
    const Vec3 old_rgb = v;
    const float old_a = a;
    if (u.color_field(fid(n), field_rect(row), v, &a)) {
      /* Recorded as the two fields they are saved as, so multi-object editing works too. */
      if (v != old_rgb) record(FieldEdit::Color, n).v_new = v;
      if (a != old_a) {
        FieldEdit &e = record(FieldEdit::Float, alpha_name);
        e.f_old = old_a;
        e.f_new = a;
      }
      changed = true;
    }
  }
  void enumeration(const char *n, int &v, const char *const *opts, int count) override {
    Recti row = lay.row();
    u.label(label_rect(row), n);
    if (u.combo(fid(n), field_rect(row), v, opts, count)) {
      record(FieldEdit::Enum, n).i_new = v;
      changed = true;
    }
  }
  void text(const char *n, std::string &v) override {
    Recti row = lay.row();
    u.label(label_rect(row), n);
    if (u.text_field(fid(n), field_rect(row), v)) {
      record(FieldEdit::Text, n).s_new = v;
      changed = true;
    }
  }
  void mesh(const char *n, MeshPtr &m) override {
    Recti row = lay.row();
    u.label(label_rect(row), n);
    Recti f = field_rect(row);
    u.frame(f, u.theme.field, u.theme.field_border, u.px(3));
    int a = u.font.line_height() - u.px(4);
    u.draw_icon(Icon::Mesh, {f.x + u.px(4), f.y + (f.h - a) / 2, a, a}, u.theme.text);
    u.label({f.x + a + u.px(8), f.y, f.w - a - u.px(10), f.h}, m ? m->name : "None (Mesh)");
    if (m) {
      Recti info = lay.row();
      u.label({info.x + label_w, info.y, info.w - label_w, info.h},
              strprintf("%zu verts, %zu faces, %zu tris", m->vert_count(), m->face_count(), m->render_mesh().tri_count()), u.theme.text_dim);
    }
  }
  void help(const char *t) override { u.tooltip(t); }

  /* Texture slot: thumbnail + name; click for a picker (Unity's object field /
   * Blender's image browse menu). Popup results arrive on the next frame. */
  void texture(const char *n, TextureRef &t) override {
    static std::unordered_map<ui::Id, TextureRef> results;
    ui::Id pid = u.id(n) ^ 0x7E7E7ull;
    auto res = results.find(pid);
    if (res != results.end()) {
      t = res->second;
      results.erase(res);
      changed = true;
      invalidate_material_textures();
    }
    Recti row = lay.row(u.row_h() + u.px(4));
    u.label(label_rect(row), n);
    Recti f = field_rect(row);
    Recti thumb{f.x, f.y, f.h, f.h};
    TexturePtr tex;
    if (t.path == "generated:UV Grid") tex = texture_uv_grid();
    else if (t.path == "generated:Color Grid") tex = texture_color_grid();
    else if (!t.empty()) tex = texture_load(resolve_asset_path(t.path), !t.non_color);
    draw_texture_thumb(u, tex, thumb);
    Recti name{f.x + f.h + u.px(4), f.y, f.w - f.h - u.px(4), f.h};
    bool hot = u.hovered(name) || u.hovered(thumb);
    u.frame(name, u.theme.field, hot ? u.theme.field_hover : u.theme.field_border, u.px(3));
    std::string label = t.empty() ? "None (Texture)" : (starts_with(t.path, "generated:") ? t.path.substr(10) : fs::filename(t.path));
    if (!t.empty() && !tex) label += "  (missing)";
    if (t.non_color && !t.empty()) label += "  [Non-Color]";
    u.label({name.x + u.px(5), name.y, name.w - u.px(8), name.h}, label, tex || t.empty() ? u.theme.text : u.theme.error);
    u.tooltip(t.empty() ? std::string("Click to pick an image from Assets, or drop an image file onto the window.") : t.path);
    if (hot && u.in.pressed[0]) {
      u.open_popup(pid, name);
      u.consume_click();
    }
    TextureRef cur = t;
    Editor *e = &ed;
    u.popup(pid, std::max(name.w, u.px(260)), [uc = &u, pid, cur, e] {
      auto pick = [&](const std::string &path, bool non_color) {
        results[pid] = TextureRef{path, non_color};
        uc->redraw = true;
      };
      if (uc->menu_item("None")) pick("", cur.non_color);
      if (uc->menu_item("UV Grid (generated)", nullptr, cur.path == "generated:UV Grid")) pick("generated:UV Grid", false);
      if (uc->menu_item("Color Grid (generated)", nullptr, cur.path == "generated:Color Grid")) pick("generated:Color Grid", false);
      if (!cur.empty() && uc->menu_item(cur.non_color ? "Color Space: Non-Color (click for sRGB)" : "Color Space: sRGB (click for Non-Color)"))
        pick(cur.path, !cur.non_color);
      uc->menu_separator();
      const auto &imgs = e->image_assets();
      if (imgs.empty()) uc->menu_label("No images in Assets yet - drop some onto the window");
      for (const std::string &p : imgs)
        if (uc->menu_item(p, nullptr, p == cur.path, true, Icon::Eye)) pick(p, cur.non_color);
    });
  }

  /* Unity's MeshRenderer > Materials array + inline material inspectors. */
  void material_list(const char *n, std::vector<MaterialPtr> &mats) override {
    struct Pick {
      MaterialPtr m;
      bool none = false;
    };
    static std::unordered_map<ui::Id, Pick> results;
    Recti row = lay.row();
    u.label(label_rect(row), strprintf("%s  (%zu slot%s)", n, mats.size(), mats.size() == 1 ? "" : "s"));
    Recti f = field_rect(row);
    if (u.button({f.right() - u.px(50), f.y, u.px(24), f.h}, "+")) {
      mats.push_back(mats.empty() || !mats.back() ? make_material("Material", Vec3(0.8f)) : mats.back());
      changed = true;
    }
    u.tooltip("Add a material slot. Faces choose slots by material index (Edit Mode > Assign).");
    if (!mats.empty() && u.button({f.right() - u.px(24), f.y, u.px(24), f.h}, "-")) {
      mats.pop_back();
      changed = true;
    }
    for (size_t i = 0; i < mats.size(); i++) {
      ui::Id pid = u.id((uint64_t)i * 977 + 13) ^ 0x3A7Eull;
      auto res = results.find(pid);
      if (res != results.end()) {
        mats[i] = res->second.none ? nullptr : res->second.m;
        results.erase(res);
        changed = true;
      }
      Recti r = lay.row();
      u.label(label_rect(r), strprintf("  Element %zu", i), u.theme.text_dim);
      Recti fr = field_rect(r);
      bool hot = u.hovered(fr);
      u.frame(fr, u.theme.field, hot ? u.theme.field_hover : u.theme.field_border, u.px(3));
      int a = u.font.line_height() - u.px(4);
      Vec3 sw = mats[i] ? mats[i]->base_color : Vec3(0.8f);
      u.canvas.fill_round_rect({fr.x + u.px(4), fr.y + (fr.h - a) / 2, a, a}, u.px(2), Color::from(Vec3(linear_to_srgb(sw.x), linear_to_srgb(sw.y), linear_to_srgb(sw.z))));
      u.label({fr.x + a + u.px(10), fr.y, fr.w - a - u.px(14), fr.h}, mats[i] ? mats[i]->name : "None (Default-Material)");
      if (hot && u.in.pressed[0]) {
        u.open_popup(pid, fr);
        u.consume_click();
      }
      Editor *e = &ed;
      MaterialPtr cur = mats[i];
      u.popup(pid, std::max(fr.w, u.px(240)), [uc = &u, pid, e, cur] {
        if (uc->menu_item("New Material", nullptr, false, true, Icon::Plus)) {
          auto m = cur ? std::make_shared<Material>(*cur) : make_material("Material", Vec3(0.8f));
          m->name = cur ? cur->name + " (copy)" : "Material";
          results[pid] = {m, false};
          uc->redraw = true;
        }
        uc->submenu("New Material of Type", uc->px(200), [uc, pid] {
          for (const std::string &p : material_presets())
            if (uc->menu_item(p)) {
              results[pid] = {make_material_preset(p), false};
              uc->redraw = true;
            }
        });
        if (uc->menu_item("None")) {
          results[pid] = {nullptr, true};
          uc->redraw = true;
        }
        uc->menu_separator();
        uc->menu_label("Materials in this scene");
        for (const MaterialPtr &m : e->scene_materials())
          if (uc->menu_item(m->name, nullptr, m == cur)) {
            results[pid] = {m, false};
            uc->redraw = true;
          }
      });
    }
    /* Inline editors, one per distinct material (shared slots edit once). */
    std::vector<Material *> seen;
    for (auto &m : mats) {
      if (!m || std::find(seen.begin(), seen.end(), m.get()) != seen.end()) continue;
      seen.push_back(m.get());
      lay.space(u.px(4));
      Recti h = lay.row(u.row_h() + u.px(4));
      u.canvas.fill_rect(h, Color::hex(0x333740));
      bool &open = ed.foldouts_.emplace("mat_" + std::to_string((uintptr_t)m.get()), true).first->second;
      u.foldout({h.x + u.px(4), h.y, h.w - u.px(8), h.h}, "Material  " + m->name, open, Icon::Eye);
      u.tooltip("Blender: Material Properties (Principled BSDF). Unity: the material inspector at the bottom of the Inspector.");
      if (!open) continue;
      u.push_id((uint64_t)(uintptr_t)m.get());
      InspectorReflector sub(ed, u, lay);
      sub.label_w = label_w;
      m->reflect(sub);
      u.pop_id();
      if (sub.changed) {
        m->touch();
        changed = true;
      }
    }
  }

  static void draw_texture_thumb(ui::Context &u, const TexturePtr &t, const Recti &r) {
    if (!t || t->levels.empty()) {
      int c = std::max(2, r.w / 4);
      for (int y = 0; y < r.h; y += c)
        for (int x = 0; x < r.w; x += c)
          u.canvas.fill_rect({r.x + x, r.y + y, std::min(c, r.w - x), std::min(c, r.h - y)}, ((x + y) / c) & 1 ? Color::hex(0x3A3A3A) : Color::hex(0x2A2A2A));
      u.canvas.rect_outline(r, u.theme.border);
      return;
    }
    int lv = 0;
    while (lv + 1 < (int)t->levels.size() && t->levels[lv + 1].w >= r.w) lv++;
    const Texture::Level &L = t->levels[lv];
    Recti c = r.intersect(u.canvas.clip());
    Image *img = u.canvas.target();
    for (int y = c.y; y < c.bottom(); y++)
      for (int x = c.x; x < c.right(); x++) {
        int sx = (x - r.x) * L.w / std::max(1, r.w), sy = (y - r.y) * L.h / std::max(1, r.h);
        Vec4 v = t->fetch(lv, sx, sy, TexWrap::Extend);
        img->row(y)[x] = to_display_pixel(v.xyz(), ViewTransform::Standard, 0.0f);
      }
    u.canvas.rect_outline(r, u.theme.border);
  }
};

void Editor::draw_inspector(const Recti &r) {
  auto &u = ui_;
  GameObject *g = active_object();
  if (!g) {
    /* Project file selected? Show file info, like Unity's asset inspector. */
    if (!project_selected_.empty()) {
      ui::Layout lay{r.shrink(u.px(8)), r.y + u.px(8)};
      lay.row_h = u.row_h();
      u.label(lay.row(), fs::filename(project_selected_), u.theme.text_bright);
      std::string ext = fs::extension(project_selected_);
      u.label(lay.row(), "Type: " + (ext.empty() ? std::string("folder") : ext), u.theme.text_dim);
      u.label(lay.row(), project_selected_, u.theme.text_dim);
      if (ext == ".pdf" || starts_with(project_selected_, papers_dir_)) {
        lay.space(u.px(6));
        u.label(lay.row(), "Research paper", u.theme.accent);
        u.label(lay.row(), "Ask Claude: \"implement <technique> from research/papers/" + fs::filename(project_selected_) + "\"", u.theme.text);
      }
      if (u.button(lay.row(u.row_h() + u.px(4)), "Open with system viewer")) fs::open_external(project_selected_);
      return;
    }
    u.label({r.x, r.y + u.px(20), r.w, u.row_h()}, "Select a GameObject to inspect it.", u.theme.text_dim, ui::Align::Center);
    u.label({r.x, r.y + u.px(20) + u.row_h(), r.w, u.row_h()}, "(Blender: the Properties editor)", u.theme.text_dim, ui::Align::Center);
    return;
  }
  ui::Id sid = u.id("insp_scroll");
  static int last_content_h = 0;
  int off = u.begin_scroll(sid, r, last_content_h);
  ui::Layout lay{{r.x + u.px(6), r.y, r.w - u.px(18), r.h}, r.y + u.px(6) - off};
  lay.row_h = u.row_h();
  u.push_id(g->id);

  /* Header: active toggle + name (Unity) */
  Recti hr = lay.row(u.row_h() + u.px(6));
  int ic = u.row_h();
  u.draw_icon(object_icon(*g), {hr.x, hr.y + u.px(3), ic, ic}, u.theme.text);
  bool active = g->active;
  if (u.checkbox({hr.x + ic + u.px(6), hr.y, ic, hr.h}, active)) {
    g->active = active;
    mark_changed("Toggle Active");
  }
  u.tooltip("Active: inactive objects are not drawn or updated.\nBlender: Disable in Viewports (monitor icon).");
  std::string name = g->name;
  bool committed = false;
  if (u.text_field(u.id("name"), {hr.x + ic * 2 + u.px(12), hr.y + u.px(2), hr.w - ic * 2 - u.px(14), hr.h - u.px(4)}, name, &committed)) {
    g->name = name;
    if (committed) mark_changed("Rename");
  }
  /* Unity-style multi-object editing: every selected object, in selection order. */
  std::vector<GameObject *> multi;
  for (uint64_t id : selection_)
    if (GameObject *o = scene_->find(id)) multi.push_back(o);
  if (multi.size() > 1) {
    u.label(lay.row(), strprintf("%zu objects selected: edits apply to all of them", multi.size()), u.theme.text_dim);
    u.tooltip("Type +=1, -=1, *=2 or /=2 to change each object relative to its own value,\n"
              "L(0,10) to spread values evenly across the selection, R(0,1) for random values.\n"
              "Any expression works: 2*pi, sqrt(2), max(1,2).");
  }
  lay.space(u.px(4));

  auto section = [&](const std::string &title, const std::string &key, bool *enabled, Icon icon, const std::string &tip,
                     std::function<void()> menu) {
    Recti h = lay.row(u.row_h() + u.px(4));
    u.canvas.fill_rect({r.x, h.y, r.w, h.h}, u.theme.header);
    u.canvas.hline(r.x, r.right(), h.y, u.theme.border);
    bool &open = foldouts_.emplace(key, true).first->second;
    int a = u.font.line_height() - u.px(4);
    Recti fr{h.x, h.y, a + u.px(4), h.h};
    if (u.hovered(fr) && u.in.pressed[0]) open = !open;
    u.draw_icon(open ? Icon::ArrowDown : Icon::ArrowRight, {h.x, h.y + (h.h - a) / 2, a, a}, u.theme.text);
    int x = h.x + a + u.px(6);
    u.draw_icon(icon, {x, h.y + (h.h - a) / 2, a, a}, u.theme.text);
    x += a + u.px(6);
    if (enabled) {
      if (u.checkbox({x, h.y, a + u.px(2), h.h}, *enabled)) mark_changed("Toggle Component");
      x += a + u.px(8);
    }
    Recti tr{x, h.y, h.right() - x - u.px(24), h.h};
    u.label(tr, title, u.theme.text_bright);
    if (u.hovered(tr) && u.in.pressed[0]) open = !open;
    u.tooltip(tip);
    if (menu) {
      Recti kr{h.right() - u.px(20), h.y + u.px(2), u.px(18), h.h - u.px(4)};
      ui::Id mid = u.id(key + "_menu");
      if (u.icon_button(kr, Icon::Menu, false, "Component menu")) u.open_popup(mid, kr);
      u.popup(mid, u.px(200), menu);
    }
    return open;
  };

  /* Transform (always first, like Unity) */
  if (section("Transform", "Transform", nullptr, Icon::Move,
              "Position / Rotation / Scale relative to the parent.\nBlender: Object Properties > Transform (Location, Rotation, Scale).\nTheory: FoCG ch. 7, GEA Vol. I ch. 5.3.",
              [this, g] {
                if (ui_.menu_item("Reset")) {
                  g->set_local(Transform());
                  mark_changed("Reset Transform");
                }
              })) {
    InspectorReflector ir(*this, u, lay);
    Transform t = g->local();
    const Vec3 pos_before = t.position, euler_before = t.euler_hint, scale_before = t.scale;
    Recti row = lay.row();
    u.label(ir.label_rect(row), "Position");
    bool ch = u.vec3_field(u.id("pos"), ir.field_rect(row), t.position, 0.05f);
    u.tooltip("Local position. Unity is Y-up left-handed; Blender is Z-up right-handed.");
    row = lay.row();
    u.label(ir.label_rect(row), "Rotation");
    Vec3 e = t.euler_hint;
    bool rch = u.vec3_field(u.id("rot"), ir.field_rect(row), e, 0.5f);
    u.tooltip("Euler angles in degrees, applied Z, X, then Y (Unity order).\nStored internally as a quaternion (GEA Vol. I 5.4).");
    row = lay.row();
    u.label(ir.label_rect(row), "Scale");
    ch |= u.vec3_field(u.id("scl"), ir.field_rect(row), t.scale, 0.01f);
    if (ch) {
      g->set_local_position(t.position);
      g->set_local_scale(t.scale);
      mark_changed("Transform");
    }
    if (rch) {
      g->set_local_euler(e);
      mark_changed("Rotate");
    }
    /* Set Origin (Blender: Object > Set Origin): pick where the pivot goes. */
    if (g->get<MeshFilter>()) {
      row = lay.row(u.row_h() + u.px(2));
      u.label(ir.label_rect(row), "Origin");
      Recti fr = ir.field_rect(row);
      int bw = u.font.text_width("Apply") + u.px(16);
      u.combo(u.id("origin_mode"), {fr.x, fr.y, fr.w - bw - u.px(4), fr.h}, origin_mode_, kOriginModes, kOriginModeCount);
      origin_hover_ = u.hovered(row) || u.popup_open(u.id("origin_mode"));
      u.tooltip("Where the object's origin (pivot) goes. The mesh moves the other way, so nothing\n"
                "moves on screen. Volume uses the enclosed volume's centre of mass (surface for open meshes).\n"
                "Blender: Object > Set Origin. Unity: ProBuilder's Center Pivot.");
      if (u.button({fr.right() - bw, fr.y, bw, fr.h}, "Apply")) set_origin(origin_mode_, origin_target_);
      if (!edit_mode_) {
        row = lay.row(u.row_h() + u.px(2));
        if (u.button({fr.x, row.y, fr.w, row.h}, origin_edit_ ? "Done Editing Origin (Esc)" : "Edit Origin with Handles...", origin_edit_, Icon::Move))
          origin_edit_ = !origin_edit_;
        u.tooltip("Move or rotate only the origin with the gizmo (the mesh stays put), or click a vertex,\n"
                  "an edge (its midpoint) or a face (its centre) in the Scene view to snap the origin there.\n"
                  "Blender: Options > Affect Only > Origins.");
      }
      else {
        row = lay.row(u.row_h() + u.px(2));
        if (u.button({fr.x, row.y, fr.w, row.h}, "Origin to Selected Elements", false, Icon::Vertex)) set_origin(6);
        u.tooltip("The origin moves to the centre of the selected vertices, edges or faces.\n"
                  "Blender: Shift+S > Cursor to Selected, then Set Origin > Origin to 3D Cursor.");
      }
      if (origin_mode_ == 5) {
        row = lay.row();
        u.label(ir.label_rect(row), "  Point (world)");
        u.vec3_field(u.id("origin_pt"), ir.field_rect(row), origin_target_, 0.05f);
        origin_hover_ = origin_hover_ || u.hovered(row);
        u.tooltip("The world position the origin moves to. Expressions work, e.g. \"+=1\".");
      }
    }
    /* Multi-object editing (Unity): the edited axis goes to every selected
     * object; typed expressions are evaluated per object ("+=1", L(0,10)). */
    if ((ch || rch) && multi.size() > 1) {
      Transform t0 = g->local();
      auto make = [&](const char *name, ui::Id id, Vec3 old, Vec3 now) {
        FieldEdit fe;
        fe.kind = FieldEdit::Vec;
        fe.name = name;
        fe.v_old = old;
        fe.v_new = now;
        u.number_committed(id, &fe.expr, &fe.component);
        return fe;
      };
      FieldEdit fp = make("Position", u.id("pos"), pos_before, t.position), fr = make("Rotation", u.id("rot"), euler_before, e),
                fs = make("Scale", u.id("scl"), scale_before, t.scale);
      for (size_t i = 0; i < multi.size(); i++) {
        GameObject *o = multi[i];
        bool self = o == g;
        if (self && fp.expr.empty() && fr.expr.empty() && fs.expr.empty()) continue;  // already set
        Transform ot = self ? t0 : o->local();
        Vec3 p = self ? pos_before : ot.position, r = self ? euler_before : ot.euler_hint, s = self ? scale_before : ot.scale;
        if (fp.v_old != fp.v_new) { ApplyEditReflector a(fp, (int)i, (int)multi.size(), false); a.field("Position", p); o->set_local_position(p); }
        if (fs.v_old != fs.v_new) { ApplyEditReflector a(fs, (int)i, (int)multi.size(), false); a.field("Scale", s); o->set_local_scale(s); }
        if (fr.v_old != fr.v_new) { ApplyEditReflector a(fr, (int)i, (int)multi.size(), false); a.field("Rotation", r); o->set_local_euler(r); }
      }
      mark_changed("Transform (multiple objects)");
    }
  }

  /* Components */
  int remove_idx = -1, move_up = -1;
  for (size_t ci = 0; ci < g->components.size(); ci++) {
    Component *c = g->components[ci].get();
    if (c->is_modifier()) continue;  // drawn together in the Modifiers stack below
    const ComponentInfo *info = find_component_info(c->type_name());
    std::string tip = info ? info->help + "\nBlender: " + info->blender : std::string();
    Icon icon = std::string(c->type_name()) == "Camera" ? Icon::Camera : (std::string(c->type_name()) == "Light" ? Icon::Light : (c->is_modifier() ? Icon::Gear : Icon::File));
    if (std::string(c->type_name()) == "MeshFilter" || std::string(c->type_name()) == "MeshRenderer") icon = Icon::Mesh;
    u.push_id((uint64_t)ci + 1000);
    bool en = c->enabled;
    size_t idx = ci;
    bool open = section(c->type_name(), std::string(c->type_name()) + std::to_string(ci), &en, icon, tip, [&, idx] {
      if (ui_.menu_item("Remove Component")) remove_idx = (int)idx;
      if (ui_.menu_item("Move Up", nullptr, false, idx > 0)) move_up = (int)idx;
      if (ui_.menu_item("Reset")) {
        if (auto fresh = create_component(g->components[idx]->type_name())) {
          fresh->owner = g;
          if (auto *mf = dynamic_cast<MeshFilter *>(g->components[idx].get())) static_cast<MeshFilter *>(fresh.get())->mesh = mf->mesh;
          g->components[idx] = std::move(fresh);
          mark_changed("Reset Component");
        }
      }
    });
    if (en != c->enabled) c->enabled = en;
    if (open) {
      InspectorReflector ir(*this, u, lay);
      c->reflect(ir);
      if (ir.changed) mark_changed(std::string("Edit ") + c->type_name());
      /* Multi-object editing: the same field of the same component (the nth
       * of its type) on every other selected object. */
      if (ir.changed && multi.size() > 1) {
        int nth = 0;
        for (size_t k = 0; k < ci; k++) nth += std::string(g->components[k]->type_name()) == c->type_name();
        for (const FieldEdit &fe : ir.edits)
          for (size_t i = 0; i < multi.size(); i++) {
            GameObject *o = multi[i];
            bool self = o == g;
            if (self && fe.expr.empty()) continue;  // the active object already has the value
            int seen = 0;
            Component *oc = nullptr;
            for (auto &cc : o->components)
              if (std::string(cc->type_name()) == c->type_name() && seen++ == nth) {
                oc = cc.get();
                break;
              }
            if (!oc) continue;
            ApplyEditReflector a(fe, (int)i, (int)multi.size(), self);
            oc->reflect(a);
          }
      }
      /* Camera: an eyedropper for the focus distance (Blender: the Focus Distance
       * eyedropper; Unity HDRP: Focus Distance). */
      if (dynamic_cast<Camera *>(c)) {
        Recti row = lay.row(u.row_h() + u.px(2));
        const bool picking = focus_pick_cam_ == g->id;
        if (u.button({row.x + u.px(4), row.y, row.w - u.px(8), row.h}, picking ? "Click a point in the Scene view... (Esc cancels)" : "Pick Focus Point",
                     picking, Icon::Eye))
          focus_pick_cam_ = picking ? 0 : g->id;
        u.tooltip("Eyedropper: click a surface in the Scene view and the focus distance becomes that point's\n"
                  "distance from this camera (along its view). Turns Depth of Field on.");
      }
      /* Mesh tools (Blender's Edit Mode operators, object-level). */
      if (auto *mf = dynamic_cast<MeshFilter *>(c)) {
        lay.space(u.px(4));
        u.label(lay.row(), "Mesh Tools (Blender operators)", u.theme.accent);
        int bw = (lay.area.w - u.px(12)) / 2;
        auto two = [&](const char *a, const char *opa, const char *ta, const char *b, const char *opb, const char *tb) {
          Recti row = lay.row(u.row_h() + u.px(2));
          if (u.button({row.x + u.px(4), row.y, bw, row.h}, a)) mesh_op(opa);
          u.tooltip(ta);
          if (b && u.button({row.x + u.px(8) + bw, row.y, bw, row.h}, b)) mesh_op(opb);
          if (b) u.tooltip(tb);
        };
        two("Subdivide", "subdivide", "Catmull-Clark subdivision (each face -> quads).\nBlender: Subdivision Surface / Subdivide Smooth.",
            "Subdivide Simple", "subdivide_simple", "Split faces without smoothing.");
        two("Smooth", "smooth", "Laplacian smoothing, one iteration (FoCG ch. 12).\nBlender: Smooth Vertices.",
            "Triangulate", "triangulate", "Convert n-gons to triangles (ear clipping).\nBlender: Ctrl+T.");
        two("Merge by Distance", "merge", "Weld vertices closer than the threshold (spatial hash).\nBlender: M > By Distance.",
            "Flip Normals", "flip", "Reverse face winding.\nBlender: Alt+N > Flip.");
        two(mf->mesh && mf->mesh->smooth ? "Shade Flat" : "Shade Smooth", mf->mesh && mf->mesh->smooth ? "shade_flat" : "shade_smooth",
            "Per-face vs interpolated vertex normals (Gouraud, FoCG ch. 9).", "Apply Modifiers", "apply_modifiers",
            "Bake the modifier stack into the mesh.\nBlender: Ctrl+A > Apply modifier.");
        if (mf->mesh) {
          /* Hard edges at UV seams: Unity users expect a seam to split the
           * shading; Blender keeps the two apart (Mark Sharp). */
          InspectorReflector sr(*this, u, lay);
          bool hard = mf->mesh->seams_sharp;
          sr.field("Seams Are Hard Edges", hard);
          u.tooltip("Smooth shading stops at UV seams as well as at edges marked sharp.\n"
                    "Blender keeps these separate: seams only cut UVs, Mark Sharp makes hard edges.");
          if (hard != mf->mesh->seams_sharp) {
            Mesh &mm = *mesh_make_mutable(mf->mesh);
            mm.seams_sharp = hard;
            mm.touch();
            mark_changed("Seams Are Hard Edges");
          }
        }
        Recti row = lay.row(u.row_h() + u.px(2));
        if (u.button({row.x + u.px(4), row.y, row.w - u.px(8), row.h}, edit_mode_ ? "Exit Edit Mode (Tab)" : "Enter Edit Mode (Tab)", edit_mode_, Icon::Vertex)) {
          if (edit_mode_) exit_edit_mode(); else enter_edit_mode();
        }
        if (edit_mode_ && g->id == edit_obj_) {
          InspectorReflector er(*this, u, lay);
          /* Selection mode, then only that mode's operators and their settings
           * (ProBuilder's Vertex / Edge / Face actions). */
          {
            Recti mr = lay.row(u.row_h() + u.px(4));
            static const char *kModes[] = {"Vertex (1)", "Edge (2)", "Face (3)"};
            const Icon kIcons[] = {Icon::Vertex, Icon::Grid, Icon::Face};
            int w3m = (mr.w - u.px(16)) / 3;
            for (int k = 0; k < 3; k++) {
              if (u.button({mr.x + u.px(4) + k * (w3m + u.px(4)), mr.y, w3m, mr.h}, kModes[k], (int)elem_ == k, kIcons[k]))
                set_edit_element((EditElement)k);
              u.tooltip(k == 0 ? "Vertex select mode: vertex operations." : k == 1 ? "Edge select mode: edge operations." : "Face select mode: face operations.");
            }
          }
          {
            /* SketchUp-style n-gon editing. */
            Recti nr = lay.row(u.row_h() + u.px(2));
            const int nw = (nr.w - u.px(16)) / 3;
            if (u.button({nr.x + u.px(4), nr.y, nw, nr.h}, ngon_mode_ ? "N-gon Mode: On" : "N-gon Mode: Off", ngon_mode_, Icon::Face))
              ngon_mode_ = !ngon_mode_;
            u.tooltip("SketchUp-style faces: a flat region of faces acts as one face (inner edges hidden,\n"
                      "one click selects it all, Push/Pull moves it all).");
            if (u.button({nr.x + u.px(8) + nw, nr.y, nw, nr.h}, "Merge Coplanar")) edit_tool("dissolve_limited");
            u.tooltip("Make each flat region one real n-gon and drop corners on straight edges.\nBlender: Limited Dissolve.");
            if (u.button({nr.x + u.px(12) + 2 * nw, nr.y, nw, nr.h}, knife_.active ? "Knife (on)" : "Knife / Line (K)", knife_.active))
              edit_tool("knife");
            u.tooltip("Split a face along a line between two points on its edges or corners.\nSketchUp: Line tool. Blender: Knife (K).");
          }
          if (elem_ == EditElement::Face) {
            er.field("Extrude Distance", extrude_dist_, 0.01f, -100.0f, 100.0f);
            er.field("Inset Amount", inset_amount_, 0.005f, 0.0f, 1.0f);
            er.field("Bridge Segments", bridge_segments_, 1, 256);
            er.field("Auto Fuse on Contact", auto_fuse_);
            u.tooltip("Faces extruded or moved onto another face of the mesh merge into it\n(the touching area becomes an opening): bridging by extrusion.");
          }
          else if (elem_ == EditElement::Edge) {
            er.field("Bevel Width", bevel_width_, 0.005f, 0.0001f, 1000.0f);
            er.field("Bevel Segments", bevel_segments_, 1, 64);
            er.field("Bevel Clamp Overlap", bevel_clamp_);
            er.field("Loop Cuts", loop_cuts_, 1, 64);
            er.field("Loop Slide", loop_slide_, 0.01f, 0.0f, 1.0f);
            er.field("Subdivide Cuts", subdivide_cuts_, 1, 100);
            er.field("Bridge Segments", bridge_segments_, 1, 256);
          }
          else {
            er.field("Smooth Factor", smooth_factor_, 0.01f, 0.0f, 1.0f);
          }
          {
            /* The mode's operators, three to a row; then the ones every mode has. */
            std::vector<const EditOpInfo *> mine, common;
            for (const EditOpInfo &op : edit_op_table())
              (op.elements == 7 ? common : mine).push_back(&op);
            mine.erase(std::remove_if(mine.begin(), mine.end(), [&](const EditOpInfo *o) { return !edit_op_available(o->op); }), mine.end());
            auto grid = [&](const std::vector<const EditOpInfo *> &list) {
              for (size_t i = 0; i < list.size(); i += 3) {
                Recti row = lay.row(u.row_h() + u.px(2));
                int bw3 = (row.w - u.px(16)) / 3;
                for (size_t k = i; k < std::min(list.size(), i + 3); k++) {
                  int x = row.x + u.px(4) + (int)(k - i) * (bw3 + u.px(4));
                  if (u.button({x, row.y, bw3, row.h}, list[k]->label)) edit_tool(list[k]->op);
                  u.tooltip(list[k]->keys[0] ? strprintf("%s (%s)", list[k]->tip, list[k]->keys) : std::string(list[k]->tip));
                }
              }
            };
            grid(mine);
            grid(common);
          }
          er.field("Proportional Editing", proportional_);
          if (proportional_) {
            er.field("Proportional Radius", prop_radius_, 0.01f, 0.001f, 10000.0f);
            static const char *kFalloff[] = {"Smooth", "Sphere", "Root", "Sharp", "Linear", "Constant"};
            er.enumeration("Falloff", prop_falloff_, kFalloff, 6);
          }
          if (elem_ == EditElement::Face) {  // materials go on faces
            Recti r3 = lay.row(u.row_h() + u.px(2));
            u.label({r3.x + u.px(4), r3.y, er.label_w - u.px(8), r3.h}, "Material Slot");
            u.int_field(u.id("assign_slot"), {r3.x + er.label_w, r3.y, u.px(60), r3.h}, assign_slot_, 0, 63);
            if (u.button({r3.x + er.label_w + u.px(66), r3.y, r3.w - er.label_w - u.px(70), r3.h}, "Assign to Faces")) assign_material_to_faces(assign_slot_);
            u.tooltip("Blender: Material Properties > Assign. Unity: sub-mesh per material.");
          }
        }
        auto &feats = research::features();
        if (!feats.empty()) {
          lay.space(u.px(2));
          u.label(lay.row(), "Research features", u.theme.accent);
          for (auto &f : feats) {
            Recti fr = lay.row(u.row_h() + u.px(2));
            if (u.button({fr.x + u.px(4), fr.y, fr.w - u.px(8), fr.h}, f.name, false, Icon::Flask)) mesh_op("research:" + f.id);
            u.tooltip(f.description + "\nSource: " + f.citation);
          }
        }
      }
    }
    u.pop_id();
  }
  if (remove_idx >= 0) {
    g->components.erase(g->components.begin() + remove_idx);
    mark_changed("Remove Component");
  }
  if (move_up > 0) {
    std::swap(g->components[move_up], g->components[move_up - 1]);
    mark_changed("Reorder Components");
  }

  /* The modifier stack (Blender's Modifier Properties): applied top to
   * bottom, each with Blender's header toggles and menu. */
  if (g->get<MeshFilter>()) {
    std::vector<size_t> mods;
    for (size_t ci = 0; ci < g->components.size(); ci++)
      if (g->components[ci]->is_modifier()) mods.push_back(ci);
    const std::string title = mods.empty() ? std::string("Modifiers") : strprintf("Modifiers (%zu)", mods.size());
    if (section(title, "ModifierStack", nullptr, Icon::Gear,
                "Non-destructive changes to the mesh, applied from the top down (Blender: Modifier Properties).\n"
                "Edit the mesh underneath any time; Apply makes a modifier's result permanent.",
                nullptr)) {
      /* Add Modifier, by Blender's categories. */
      Recti ar2 = lay.row(u.row_h() + u.px(4));
      const int half = (ar2.w - u.px(12)) / 2;
      Recti ab2{ar2.x + u.px(4), ar2.y, half, ar2.h};
      ui::Id add_mod = u.id("add_modifier");
      if (u.button(ab2, "Add Modifier", false, Icon::Plus)) u.open_popup(add_mod, ab2);
      u.tooltip("Blender: Modifier Properties > Add Modifier (Edit / Generate / Deform).");
      if (u.button({ab2.right() + u.px(4), ar2.y, half, ar2.h}, "Apply All", false, Icon::Check) && !mods.empty()) mesh_op("apply_modifiers");
      u.tooltip("Bake the whole stack into the mesh (Blender: Ctrl+A on each, top first).");
      u.popup(add_mod, u.px(230), [this] {
        auto &u = ui_;
        struct Item { const char *label, *type; };
        static const Item kEdit[] = {{"Weld", "WeldModifier"}};
        static const Item kGenerate[] = {{"Array", "ArrayModifier"},           {"Bevel", "BevelModifier"},
                                         {"Boolean", "BooleanModifier"},       {"Decimate", "DecimateModifier"},
                                         {"Mirror", "MirrorModifier"},         {"Screw", "ScrewModifier"},
                                         {"Solidify", "SolidifyModifier"},     {"Subdivision Surface", "SubdivisionSurface"},
                                         {"Triangulate", "TriangulateModifier"}, {"Wireframe", "WireframeModifier"}};
        static const Item kDeform[] = {{"Cast", "CastModifier"}, {"Displace", "DisplaceModifier"},
                                       {"Simple Deform", "SimpleDeformModifier"}, {"Smooth", "SmoothModifier"}};
        auto group = [&](const char *name, const Item *items, size_t n) {
          u.menu_label(name);
          for (size_t i = 0; i < n; i++) {
            if (u.menu_item(items[i].label, nullptr, false, true, Icon::Gear)) add_component_to_selection(items[i].type);
            if (const ComponentInfo *info = find_component_info(items[i].type)) u.tooltip(info->help + "\nBlender: " + info->blender);
          }
        };
        group("Edit", kEdit, 1);
        group("Generate", kGenerate, sizeof(kGenerate) / sizeof(kGenerate[0]));
        group("Deform", kDeform, sizeof(kDeform) / sizeof(kDeform[0]));
      });
      enum Act { None, Apply, Duplicate, CopyToSelected, Up, Down, First, Last, Remove, Reset };
      Act act = None;
      size_t act_ci = 0;
      for (size_t k = 0; k < mods.size(); k++) {
        const size_t ci = mods[k];
        Component *c = g->components[ci].get();
        u.push_id((uint64_t)ci + 5000);
        /* Header: fold arrow, name, the three toggles, menu, delete. */
        lay.space(u.px(2));
        Recti h = lay.row(u.row_h() + u.px(4));
        h.x += u.px(4);
        h.w -= u.px(8);
        u.canvas.fill_round_rect(h, u.px(3), u.theme.header);
        const int a = u.font.line_height() - u.px(4);
        Recti fr{h.x, h.y, a + u.px(6), h.h};
        if (u.hovered(fr) && u.in.pressed[0]) c->ui_expanded = !c->ui_expanded;
        u.draw_icon(c->ui_expanded ? Icon::ArrowDown : Icon::ArrowRight, {h.x + u.px(2), h.y + (h.h - a) / 2, a, a}, u.theme.text);
        std::string name = c->type_name();
        if (name.size() > 8 && name.compare(name.size() - 8, 8, "Modifier") == 0) name.resize(name.size() - 8);
        std::string pretty;
        for (size_t i = 0; i < name.size(); i++) {
          if (i && std::isupper((unsigned char)name[i]) && !std::isupper((unsigned char)name[i - 1])) pretty += ' ';
          pretty += name[i];
        }
        const int bs = h.h - u.px(4);
        int bx = h.right() - u.px(2) - bs;
        auto icon_at = [&](Icon ic, bool on, const char *tip) {
          Recti br{bx, h.y + u.px(2), bs, bs};
          bx -= bs + u.px(2);
          return u.icon_button(br, ic, on, tip);
        };
        if (icon_at(Icon::Close, false, "Delete (Blender: X, Ctrl+X)")) act = Remove, act_ci = ci;
        Recti mr{bx, h.y + u.px(2), bs, bs};
        bx -= bs + u.px(6);
        ui::Id mid = u.id("mod_menu");
        if (u.icon_button(mr, Icon::Menu, false, "Apply, duplicate, copy to selected, move")) u.open_popup(mid, mr);
        if (icon_at(Icon::Camera, c->show_in_render, "Show in Renders (the Game view and Render Image)")) {
          c->show_in_render = !c->show_in_render;
          mark_changed("Modifier: Render");
        }
        if (icon_at(Icon::Eye, c->enabled, "Show in Viewport")) {
          c->enabled = !c->enabled;
          mark_changed("Modifier: Viewport");
        }
        if (icon_at(Icon::Vertex, c->show_in_editmode, "Show in Edit Mode (the result is drawn while you edit the mesh)")) {
          c->show_in_editmode = !c->show_in_editmode;
          mark_changed("Modifier: Edit Mode");
        }
        Recti tr{fr.right() + u.px(2), h.y, bx + bs - fr.right(), h.h};
        u.label(tr, pretty, c->enabled ? u.theme.text_bright : u.theme.text_dim);
        if (u.hovered(tr) && u.in.pressed[0]) c->ui_expanded = !c->ui_expanded;
        if (const ComponentInfo *info = find_component_info(c->type_name())) u.tooltip(info->help + "\nBlender: " + info->blender);
        const size_t kk = k;
        u.popup(mid, u.px(210), [&, ci, kk] {
          auto &u = ui_;
          if (u.menu_item("Apply", "Ctrl+A")) act = Apply, act_ci = ci;
          u.tooltip("Bake this modifier into the mesh and remove it (Blender: Apply).");
          if (u.menu_item("Duplicate", "Shift+D")) act = Duplicate, act_ci = ci;
          if (u.menu_item("Copy to Selected", nullptr, false, selection_.size() > 1)) act = CopyToSelected, act_ci = ci;
          u.menu_separator();
          if (u.menu_item("Move Up", nullptr, false, kk > 0)) act = Up, act_ci = ci;
          if (u.menu_item("Move Down", nullptr, false, kk + 1 < mods.size())) act = Down, act_ci = ci;
          if (u.menu_item("Move to First", nullptr, false, kk > 0)) act = First, act_ci = ci;
          if (u.menu_item("Move to Last", nullptr, false, kk + 1 < mods.size())) act = Last, act_ci = ci;
          u.menu_separator();
          if (u.menu_item("Reset")) act = Reset, act_ci = ci;
        });
        if (c->ui_expanded) {
          lay.indent += u.px(10);
          InspectorReflector ir(*this, u, lay);
          c->reflect(ir);
          if (ir.changed) mark_changed(std::string("Edit ") + c->type_name());
          lay.indent -= u.px(10);
          if (auto *bm = dynamic_cast<BooleanModifier *>(c); bm && !bm->last_error.empty()) {
            Recti er = lay.row();
            u.label({er.x + u.px(14), er.y, er.w - u.px(18), er.h}, bm->last_error, u.theme.warning);
          }
        }
        u.pop_id();
      }
      if (mods.empty()) {
        Recti er = lay.row();
        u.label({er.x + u.px(8), er.y, er.w - u.px(16), er.h}, "No modifiers. Add Modifier to start a stack.", u.theme.text_dim);
      }
      /* Apply the chosen action after drawing (it changes the component list). */
      if (act != None && act_ci < g->components.size()) {
        auto it = std::find(mods.begin(), mods.end(), act_ci);
        const size_t pos = (size_t)(it - mods.begin());
        auto move_to = [&](size_t target_pos) {
          std::unique_ptr<Component> c = std::move(g->components[act_ci]);
          g->components.erase(g->components.begin() + (long)act_ci);
          /* Recompute where the target modifier sits now. */
          std::vector<size_t> now;
          for (size_t i = 0; i < g->components.size(); i++)
            if (g->components[i]->is_modifier()) now.push_back(i);
          size_t at = target_pos < now.size() ? now[target_pos] : (now.empty() ? g->components.size() : now.back() + 1);
          g->components.insert(g->components.begin() + (long)at, std::move(c));
        };
        switch (act) {
          case Apply: {
            auto *mf = g->get<MeshFilter>();
            if (mf && mf->mesh) {
              bool first = true;
              for (size_t i = 0; i < pos; i++) first = first && !g->components[mods[i]]->enabled;
              if (!first) Log::warn("Applied a modifier that isn't first in the stack: the ones above it are skipped (as in Blender)");
              Mesh m = *mf->mesh;
              g->components[act_ci]->modify(m);
              m.touch();
              mf->mesh = std::make_shared<Mesh>(std::move(m));
              Log::info("Applied %s", g->components[act_ci]->type_name());
              g->components.erase(g->components.begin() + (long)act_ci);
              mark_changed("Apply Modifier");
            }
            break;
          }
          case Duplicate: {
            auto copy = g->components[act_ci]->clone();
            copy->owner = g;
            g->components.insert(g->components.begin() + (long)act_ci + 1, std::move(copy));
            mark_changed("Duplicate Modifier");
            break;
          }
          case CopyToSelected: {
            int n = 0;
            for (GameObject *o : selected_objects(false))
              if (o != g && o->get<MeshFilter>()) {
                o->add_component(g->components[act_ci]->clone());
                n++;
              }
            Log::info("Copied %s to %d object(s)", g->components[act_ci]->type_name(), n);
            mark_changed("Copy Modifier to Selected");
            break;
          }
          case Up: if (pos > 0) move_to(pos - 1); mark_changed("Move Modifier"); break;
          case Down: move_to(pos + 1); mark_changed("Move Modifier"); break;
          case First: move_to(0); mark_changed("Move Modifier"); break;
          case Last: move_to(mods.size()); mark_changed("Move Modifier"); break;
          case Remove:
            g->components.erase(g->components.begin() + (long)act_ci);
            mark_changed("Remove Modifier");
            break;
          case Reset:
            if (auto fresh = create_component(g->components[act_ci]->type_name())) {
              fresh->owner = g;
              g->components[act_ci] = std::move(fresh);
              mark_changed("Reset Modifier");
            }
            break;
          default: break;
        }
      }
    }
  }

  /* Add Component (Unity's searchable menu). */
  lay.space(u.px(10));
  Recti ar = lay.row(u.row_h() + u.px(6));
  Recti ab{ar.x + ar.w / 2 - u.px(110), ar.y, u.px(220), ar.h};
  ui::Id add_id = u.id("add_component");
  if (u.button(ab, "Add Component")) {
    add_component_search_.clear();
    u.open_popup(add_id, ab);
  }
  u.tooltip("Blender equivalent: Add Modifier / Add Constraint / Physics tab.");
  u.popup(add_id, u.px(220), [this, add_id] {
    auto &u = ui_;
    Recti sr = u.popup_row(u.row_h() + u.px(6));
    ui::Id fid = u.id("add_comp_search");
    if (!u.editing(fid) && add_component_search_.empty()) u.begin_edit(fid, "", false);
    u.text_field(fid, sr.shrink(u.px(3)), add_component_search_, nullptr, "Search");
    std::string f = to_lower(add_component_search_);
    std::string last_cat;
    for (auto &ci : component_registry()) {
      if (!f.empty() && to_lower(ci.name).find(f) == std::string::npos) continue;
      if (ci.category != last_cat) {
        u.menu_label(ci.category);
        last_cat = ci.category;
      }
      if (u.menu_item(ci.name)) add_component_to_selection(ci.name);
      u.tooltip(ci.help + "\nBlender: " + ci.blender);
    }
    (void)add_id;
  });
  lay.space(u.px(30));
  u.pop_id();
  last_content_h = lay.y + off - r.y;
  u.end_scroll();
}

/* ===================================================================== */
/* Project                                                                */
/* ===================================================================== */

void Editor::draw_project(const Recti &r) {
  auto &u = ui_;
  auto &in = u.in;
  if (u.time - project_listed_ > 2.0) {
    project_entries_ = fs::list(project_dir_);
    project_listed_ = u.time;
  }
  int bh = u.row_h() + u.px(6);
  Recti bar{r.x, r.y, r.w, bh};
  u.canvas.fill_rect(bar, Color::hex(0x2F2F2F));
  /* Breadcrumb relative to the project root. */
  std::string rel = project_dir_.size() > project_root_.size() ? project_dir_.substr(project_root_.size() + 1) : "";
  int x = bar.x + u.px(6);
  std::string acc = project_root_;
  std::vector<std::string> parts;
  size_t s = 0, e;
  while ((e = rel.find('/', s)) != std::string::npos) { parts.push_back(rel.substr(s, e - s)); s = e + 1; }
  if (s < rel.size()) parts.push_back(rel.substr(s));
  for (size_t i = 0; i < parts.size(); i++) {
    acc = fs::join(acc, parts[i]);
    int w = u.font.text_width(parts[i]) + u.px(10);
    Recti pr{x, bar.y, w, bar.h};
    bool hot = u.hovered(pr);
    u.label(pr, parts[i], hot ? u.theme.text_bright : u.theme.text, ui::Align::Center);
    if (hot && in.pressed[0]) { project_dir_ = acc; project_listed_ = -100; }
    x += w;
    if (i + 1 < parts.size()) {
      u.draw_icon(Icon::ArrowRight, {x, bar.y + bh / 2 - u.px(4), u.px(8), u.px(8)}, u.theme.text_dim);
      x += u.px(10);
    }
  }
  int sw = std::min(u.px(200), r.w / 3);
  u.text_field(u.id("proj_search"), {bar.right() - sw - u.px(6), bar.y + u.px(4), sw, bh - u.px(8)}, project_search_, nullptr, "Search");

  /* Left: favourites / folders tree */
  int lw = std::min(u.px(190), r.w / 3);
  Recti left{r.x, bar.bottom(), lw, r.h - bh};
  Recti right{r.x + lw + 1, bar.bottom(), r.w - lw - 1, r.h - bh};
  u.canvas.fill_rect(left, Color::hex(0x333333));
  u.canvas.vline(left.right(), left.y, left.bottom(), u.theme.border);
  int rh = u.row_h();
  struct Fav { const char *label; std::string path; Icon icon; };
  Fav favs[] = {{"Assets", assets_dir_, Icon::Folder},
                {"Scenes", fs::join(assets_dir_, "Scenes"), Icon::Scene},
                {"Research Papers", papers_dir_, Icon::Paper},
                {"Screenshots", screenshots_dir_, Icon::Folder}};
  int y = left.y + u.px(4);
  for (auto &f : favs) {
    Recti fr{left.x, y, left.w, rh};
    bool cur = project_dir_ == f.path;
    bool hot = u.hovered(fr);
    if (cur) u.canvas.fill_rect(fr, u.theme.selection_dim);
    else if (hot) u.canvas.fill_rect(fr, Color::hex(0x404040));
    int a = u.font.line_height() - u.px(3);
    u.draw_icon(f.icon, {fr.x + u.px(10), fr.y + (rh - a) / 2, a, a}, f.icon == Icon::Paper ? u.theme.accent : u.theme.text);
    u.label({fr.x + u.px(16) + a, fr.y, fr.w - a - u.px(20), rh}, f.label);
    if (hot && in.pressed[0]) {
      fs::make_dirs(f.path);
      project_dir_ = f.path;
      project_listed_ = -100;
    }
    y += rh;
  }
  /* Subfolders of Assets */
  y += u.px(6);
  u.label({left.x + u.px(10), y, left.w, rh}, "Folders", u.theme.text_dim);
  y += rh;
  for (auto &d : fs::list(assets_dir_)) {
    if (!d.is_dir) continue;
    Recti fr{left.x, y, left.w, rh};
    std::string p = fs::join(assets_dir_, d.name);
    bool hot = u.hovered(fr);
    if (project_dir_ == p) u.canvas.fill_rect(fr, u.theme.selection_dim);
    else if (hot) u.canvas.fill_rect(fr, Color::hex(0x404040));
    int a = u.font.line_height() - u.px(3);
    u.draw_icon(Icon::Folder, {fr.x + u.px(18), fr.y + (rh - a) / 2, a, a}, u.theme.text);
    u.label({fr.x + u.px(24) + a, fr.y, fr.w - a - u.px(28), rh}, d.name);
    if (hot && in.pressed[0]) { project_dir_ = p; project_listed_ = -100; }
    y += rh;
    if (y > left.bottom()) break;
  }

  /* Right: file list */
  std::string f = to_lower(project_search_);
  std::vector<const DirEntry *> shown;
  for (auto &en : project_entries_)
    if (f.empty() || to_lower(en.name).find(f) != std::string::npos) shown.push_back(&en);
  bool can_up = project_dir_ != project_root_ && project_dir_.size() > project_root_.size();
  int rows = (int)shown.size() + (can_up ? 1 : 0);
  ui::Id sid = u.id("proj_scroll");
  int off = u.begin_scroll(sid, right, rows * rh + rh);
  int i = 0;
  auto row_rect = [&](int k) { return Recti{right.x, right.y + k * rh - off + u.px(2), right.w, rh}; };
  if (can_up) {
    Recti rr = row_rect(i++);
    bool hot = u.hovered(rr);
    if (hot) u.canvas.fill_rect(rr, Color::hex(0x444444));
    u.label({rr.x + u.px(10), rr.y, rr.w, rr.h}, "..", u.theme.text_dim);
    if (hot && in.double_clicked[0]) { project_dir_ = fs::parent(project_dir_); project_listed_ = -100; }
  }
  for (const DirEntry *en : shown) {
    Recti rr = row_rect(i++);
    if (rr.bottom() < right.y || rr.y > right.bottom()) continue;
    std::string full = fs::join(project_dir_, en->name);
    bool sel = project_selected_ == full;
    bool hot = u.hovered(rr);
    if (sel) u.canvas.fill_rect(rr, u.theme.selection);
    else if (hot) u.canvas.fill_rect(rr, Color::hex(0x444444));
    std::string ext = fs::extension(en->name);
    Icon ic = en->is_dir ? Icon::Folder : (ext == ".scene" ? Icon::Scene : (model_extension_supported(ext) ? Icon::Mesh : (ext == ".pdf" ? Icon::Paper : (ext == ".png" ? Icon::Eye : Icon::File))));
    int a = u.font.line_height() - u.px(2);
    u.draw_icon(ic, {rr.x + u.px(10), rr.y + (rh - a) / 2, a, a}, ext == ".pdf" ? u.theme.accent : u.theme.text);
    u.label({rr.x + u.px(16) + a, rr.y, rr.w / 2, rh}, en->name);
    if (!en->is_dir) u.label({rr.x, rr.y, rr.w - u.px(12), rh}, format_bytes(en->size), u.theme.text_dim, ui::Align::Right);
    if (hot && in.pressed[0]) {
      project_selected_ = full;
      clear_selection();
    }
    if (hot && in.double_clicked[0]) {
      if (en->is_dir) { project_dir_ = full; project_listed_ = -100; }
      else if (ext == ".scene") open_scene(full);
      else if (model_extension_supported(ext)) import_model_file(full, false);
      else fs::open_external(full);
    }
    if (hot && in.pressed[1]) {
      project_selected_ = full;
      u.open_popup(u.id("proj_ctx"), {in.mx, in.my, 0, 0});
    }
  }
  if (shown.empty()) {
    bool papers = project_dir_ == papers_dir_;
    u.label({right.x, right.y + rh, right.w, rh}, papers ? "Drop research papers (PDF, TXT, MD...) onto this window." : "This folder is empty.", u.theme.text_dim, ui::Align::Center);
  }
  u.end_scroll();
  std::string sel = project_selected_;
  u.popup(u.id("proj_ctx"), u.px(230), [this, sel] {
    auto &u = ui_;
    std::string ext = fs::extension(sel);
    if (ext == ".scene" && u.menu_item("Open Scene")) open_scene(sel);
    if (model_extension_supported(ext) && u.menu_item("Import into Scene")) import_obj_file(sel);
    if (u.menu_item("Open with system viewer")) fs::open_external(sel);
    if (u.menu_item("Show in Explorer / Finder")) fs::open_external(fs::parent(sel));
    if (u.menu_item("Refresh")) project_listed_ = -100;
  });
}

/* ===================================================================== */
/* Console                                                                */
/* ===================================================================== */

void Editor::draw_console(const Recti &r) {
  auto &u = ui_;
  auto &in = u.in;
  int bh = u.row_h() + u.px(6);
  Recti bar{r.x, r.y, r.w, bh};
  u.canvas.fill_rect(bar, Color::hex(0x2F2F2F));
  int h = bh - u.px(8), x = bar.x + u.px(6), y = bar.y + u.px(4);
  if (u.button({x, y, u.px(56), h}, "Clear")) Log::clear();
  x += u.px(60);
  if (u.button({x, y, u.px(72), h}, "Collapse", collapse_)) collapse_ = !collapse_;
  u.tooltip("Merge identical consecutive messages (Unity console).");
  size_t ni = 0, nw = 0, ne = 0;
  for (auto &e : log_) (e.level == LogLevel::Info ? ni : e.level == LogLevel::Warning ? nw : ne)++;
  int bw = u.px(64);
  int rx = bar.right() - bw * 3 - u.px(12);
  if (u.button({rx, y, bw, h}, std::to_string(ni), show_info_, Icon::Info)) show_info_ = !show_info_;
  if (u.button({rx + bw + u.px(2), y, bw, h}, std::to_string(nw), show_warn_, Icon::Warning)) show_warn_ = !show_warn_;
  if (u.button({rx + 2 * (bw + u.px(2)), y, bw, h}, std::to_string(ne), show_error_, Icon::Error)) show_error_ = !show_error_;

  /* Command line (Blender has a Python console; this is a tiny command shell). */
  int ch = u.row_h() + u.px(6);
  Recti cmd{r.x + u.px(4), r.bottom() - ch + u.px(2), r.w - u.px(8), ch - u.px(4)};
  Recti list{r.x, bar.bottom(), r.w, r.h - bh - ch};
  struct Line { const LogEntry *e; int count; };
  std::vector<Line> lines;
  for (auto &e : log_) {
    if ((e.level == LogLevel::Info && !show_info_) || (e.level == LogLevel::Warning && !show_warn_) || (e.level == LogLevel::Error && !show_error_)) continue;
    if (collapse_ && !lines.empty() && lines.back().e->text == e.text && lines.back().e->level == e.level) { lines.back().count++; continue; }
    lines.push_back({&e, 1});
  }
  int rh = u.row_h();
  ui::Id sid = u.id("console_scroll");
  int content = (int)lines.size() * rh;
  static size_t last_count = 0;
  if (lines.size() != last_count && console_autoscroll_) u.scroll_to(sid, std::max(0, content - list.h));
  last_count = lines.size();
  int off = u.begin_scroll(sid, list, content);
  console_autoscroll_ = off >= content - list.h - rh;
  int first = std::max(0, off / rh), last = std::min((int)lines.size(), (off + list.h) / rh + 2);
  for (int i = first; i < last; i++) {
    Recti row{list.x, list.y + i * rh - off, list.w, rh};
    const LogEntry &e = *lines[i].e;
    if (console_sel_ == i) u.canvas.fill_rect(row, u.theme.selection);
    else if (i % 2) u.canvas.fill_rect(row, Color::hex(0x3C3C3C));
    Icon ic = e.level == LogLevel::Error ? Icon::Error : (e.level == LogLevel::Warning ? Icon::Warning : Icon::Info);
    uint32_t col = e.level == LogLevel::Error ? u.theme.error : (e.level == LogLevel::Warning ? u.theme.warning : u.theme.text_dim);
    int a = u.font.line_height() - u.px(4);
    u.draw_icon(ic, {row.x + u.px(6), row.y + (rh - a) / 2, a, a}, col);
    std::string ts = strprintf("[%02d:%02d] ", (int)(e.time / 60) % 60, (int)e.time % 60);
    u.label({row.x + u.px(12) + a, row.y, row.w - a - u.px(60), rh}, ts + e.text, e.level == LogLevel::Info ? u.theme.text : col);
    if (lines[i].count > 1) u.label({row.x, row.y, row.w - u.px(10), rh}, std::to_string(lines[i].count), u.theme.text_dim, ui::Align::Right);
    if (u.hovered(row) && in.pressed[0]) console_sel_ = i;
    if (u.hovered(row) && in.double_clicked[0] && u.clipboard_set) {
      u.clipboard_set(e.text);
      Log::info("Copied message to clipboard");
    }
  }
  u.end_scroll();
  u.canvas.hline(r.x, r.right(), cmd.y - u.px(2), u.theme.border);
  bool done = false;
  ui::Id cid = u.id("console_cmd");
  u.text_field(cid, cmd, console_input_, &done, "Type a command (help) and press Enter");
  if (done && !console_input_.empty()) {
    console_history_.push_back(console_input_);
    std::string c = console_input_;
    console_input_.clear();
    run_console_command(c);
    u.begin_edit(cid, "", false);  // keep focus for the next command
  }
}

/* ===================================================================== */
/* Profiler                                                               */
/* ===================================================================== */

void Editor::draw_profiler(const Recti &r) {
  auto &u = ui_;
  ui::Id sid = u.id("prof_scroll");
  static int content_h = 0;
  int off = u.begin_scroll(sid, r, content_h);
  ui::Layout lay{{r.x + u.px(10), r.y, r.w - u.px(30), r.h}, r.y + u.px(8) - off};
  lay.row_h = u.row_h();
  u.label(lay.row(), "CPU frame time (ms) - editor frame (blue) and scene render (orange)", u.theme.text_bright);
  Recti graph = lay.row(u.px(120));
  u.frame(graph, Color::hex(0x242424), u.theme.border, u.px(3));
  float maxv = 33.3f;
  for (float v : frame_history_) maxv = std::max(maxv, v * 1.1f);
  auto plot = [&](const std::vector<float> &hist, uint32_t col) {
    for (size_t i = 1; i < hist.size(); i++) {
      float x0 = graph.x + (i - 1) * graph.w / 240.0f, x1 = graph.x + i * graph.w / 240.0f;
      float y0 = graph.bottom() - hist[i - 1] / maxv * graph.h, y1 = graph.bottom() - hist[i] / maxv * graph.h;
      u.canvas.line(x0, y0, x1, y1, col, 1.5f);
    }
  };
  for (float ref : {16.67f, 33.3f}) {
    int yy = graph.bottom() - (int)(ref / maxv * graph.h);
    u.canvas.hline(graph.x, graph.right(), yy, Color::hex(0x555555));
    u.canvas.text(u.font, graph.x + u.px(4), yy - u.font.line_height(), ref < 20 ? "60 FPS" : "30 FPS", u.theme.text_dim);
  }
  plot(frame_history_, Color::hex(0x5AA0E6));
  plot(raster_history_, u.theme.accent);

  const RasterStats &s = scene_stats_;
  lay.space(u.px(6));
  u.label(lay.row(), "Scene view render breakdown (last frame)", u.theme.text_bright);
  auto stat = [&](const std::string &k, const std::string &v) {
    Recti row = lay.row();
    u.label({row.x, row.y, row.w / 2, row.h}, k, u.theme.text_dim);
    u.label({row.x + row.w / 2, row.y, row.w / 2, row.h}, v);
  };
  stat("Vertex stage", strprintf("%.2f ms", s.ms_vertex));
  stat("Clip / cull / bin", strprintf("%.2f ms", s.ms_setup));
  stat("Raster (tiles)", strprintf("%.2f ms", s.ms_raster));
  stat("Total 3D", strprintf("%.2f ms", s.ms_total));
  stat("Editor frame (incl. UI)", strprintf("%.2f ms", frame_ms_));
  stat("Triangles submitted / rasterized", strprintf("%zu / %zu", s.tris_submitted, s.tris_rasterized));
  stat("Objects submitted / frustum-culled", strprintf("%d / %d", s.objects_submitted, s.objects_culled));
  stat("Worker threads", strprintf("%d", JobSystem::global().thread_count()));
  stat("Scene memory (approx.)", format_bytes(scene_->memory_bytes()));
  stat("Undo steps", strprintf("%zu", undo_.size()));

  lay.space(u.px(6));
  u.label(lay.row(), "Renderer options (toggle to see the cost of each optimization)", u.theme.text_bright);
  u.toggle_row(lay.row(), "Multithreaded tiles (GEA Vol. I ch. 4)", raster_opt_.multithreaded);
  u.toggle_row(lay.row(), "Back-face culling (FoCG ch. 9.4)", raster_opt_.backface_culling);
  u.toggle_row(lay.row(), "Frustum culling (FoCG ch. 9.4 / GEA Vol. II 11.5)", raster_opt_.frustum_culling);
  Recti tr = lay.row();
  u.label({tr.x, tr.y, tr.w / 2, tr.h}, "Tile size (px)");
  static const char *tiles[] = {"16", "32", "64", "128", "256"};
  int ti = raster_opt_.tile_size <= 16 ? 0 : raster_opt_.tile_size <= 32 ? 1 : raster_opt_.tile_size <= 64 ? 2 : raster_opt_.tile_size <= 128 ? 3 : 4;
  if (u.combo(u.id("tile"), {tr.x + tr.w / 2, tr.y, u.px(100), tr.h}, ti, tiles, 5)) raster_opt_.tile_size = std::atoi(tiles[ti]);

  lay.space(u.px(6));
  u.label(lay.row(), "Live stress test", u.theme.text_bright);
  Recti sr = lay.row(u.row_h() + u.px(2));
  u.label({sr.x, sr.y, u.px(70), sr.h}, "Count");
  u.int_field(u.id("stress_n"), {sr.x + u.px(70), sr.y, u.px(90), sr.h}, stress_count_, 1, 200000);
  static const char *kinds[] = {"Sphere", "Cube", "Icosphere", "Torus"};
  u.combo(u.id("stress_kind"), {sr.x + u.px(170), sr.y, u.px(110), sr.h}, stress_kind_, kinds, 4);
  if (u.button({sr.x + u.px(290), sr.y, u.px(90), sr.h}, "Spawn")) spawn_stress_grid(stress_count_, kinds[stress_kind_]);
  u.tooltip("Creates a grid of objects that share one mesh, then watch the graph.\nFull headless suite: blendity_stress (see README).");
  if (u.button({sr.x + u.px(388), sr.y, u.px(110), sr.h}, "Benchmark")) run_console_command("bench 20");
  u.tooltip("Renders the Scene view 20x with each optimization disabled in turn; results go to the Console.");
  lay.space(u.px(20));
  content_h = lay.y + off - r.y;
  u.end_scroll();
}

/* ===================================================================== */
/* Research                                                               */
/* ===================================================================== */

void Editor::draw_research(const Recti &r) {
  auto &u = ui_;
  if (u.time - papers_listed_ > 2.0) {
    papers_ = fs::list(papers_dir_);
    papers_.erase(std::remove_if(papers_.begin(), papers_.end(), [](const DirEntry &e) { return e.is_dir || to_lower(e.name) == "readme.md"; }), papers_.end());
    papers_listed_ = u.time;
  }
  ui::Id sid = u.id("research_scroll");
  static int content_h = 0;
  int off = u.begin_scroll(sid, r, content_h);
  ui::Layout lay{{r.x + u.px(12), r.y, r.w - u.px(34), r.h}, r.y + u.px(10) - off};
  lay.row_h = u.row_h();
  Recti title = lay.row(u.row_h() + u.px(6));
  u.draw_icon(Icon::Flask, {title.x, title.y + u.px(3), u.row_h(), u.row_h()}, u.theme.accent);
  u.label({title.x + u.row_h() + u.px(8), title.y, title.w, title.h}, "Research: turn your papers into editor features", u.theme.text_bright);
  const char *intro[] = {
      "1. Drop papers (PDF / TXT / MD / TeX) onto this window, or copy them into the folder below.",
      "2. Ask Claude Code: \"Read research/papers/<file> and implement <technique> as a Blendity feature.\"",
      "3. New features register in src/research/ and appear here, in the Mesh menu and the Inspector.",
      "4. Track progress in research/FEATURES.md (paper -> feature -> status -> source file)."};
  for (const char *t : intro) u.label(lay.row(), t, u.theme.text);
  Recti br = lay.row(u.row_h() + u.px(4));
  if (u.button({br.x, br.y, u.px(190), br.h}, "Open papers folder", false, Icon::Folder)) fs::open_external(papers_dir_);
  if (u.button({br.x + u.px(198), br.y, u.px(190), br.h}, "Open FEATURES.md", false, Icon::File)) fs::open_external(fs::join(project_root_, "research/FEATURES.md"));
  u.label(lay.row(), papers_dir_, u.theme.text_dim);

  lay.space(u.px(8));
  u.label(lay.row(), strprintf("Papers (%zu)", papers_.size()), u.theme.accent);
  if (papers_.empty()) u.label(lay.row(), "No papers yet - drag & drop one onto the editor.", u.theme.text_dim);
  for (auto &p : papers_) {
    Recti row = lay.row(u.row_h() + u.px(2));
    bool hot = u.hovered(row);
    if (hot) u.canvas.fill_rect(row, Color::hex(0x444444));
    int a = u.font.line_height() - u.px(2);
    u.draw_icon(Icon::Paper, {row.x + u.px(4), row.y + (row.h - a) / 2, a, a}, u.theme.accent);
    u.label({row.x + a + u.px(10), row.y, row.w * 6 / 10, row.h}, p.name);
    u.label({row.x, row.y, row.w - u.px(80), row.h}, format_bytes(p.size), u.theme.text_dim, ui::Align::Right);
    if (u.button({row.right() - u.px(70), row.y + u.px(1), u.px(66), row.h - u.px(2)}, "Open")) fs::open_external(fs::join(papers_dir_, p.name));
    if (hot && u.in.double_clicked[0]) fs::open_external(fs::join(papers_dir_, p.name));
  }

  lay.space(u.px(10));
  auto &feats = research::features();
  u.label(lay.row(), strprintf("Implemented research features (%zu)", feats.size()), u.theme.accent);
  for (auto &f : feats) {
    Recti row = lay.row(u.row_h() + u.px(4));
    u.canvas.fill_rect(row, Color::hex(0x323232));
    u.label({row.x + u.px(6), row.y, row.w - u.px(140), row.h}, f.name, u.theme.text_bright);
    if (u.button({row.right() - u.px(130), row.y + u.px(2), u.px(126), row.h - u.px(4)}, "Apply to selection", false, Icon::Flask)) mesh_op("research:" + f.id);
    u.label(lay.row(), "  " + f.citation, u.theme.text_dim);
    u.label(lay.row(), "  " + f.description, u.theme.text);
    u.label(lay.row(), "  Status: " + f.status + "   |   Source: " + f.source_file, u.theme.text_dim);
    lay.space(u.px(4));
  }
  lay.space(u.px(20));
  content_h = lay.y + off - r.y;
  u.end_scroll();
}


/* ===================================================================== */
/* Render window (Blender: Render Result + Render/Output/World properties;
 * Unity: Lighting window + Recorder)                                     */
/* ===================================================================== */

void Editor::draw_render_window(const Recti &r) {
  auto &u = ui_;
  int lw = std::min(u.px(340), r.w * 2 / 5);
  Recti left{r.x, r.y, lw, r.h};
  Recti right{r.x + lw + 1, r.y, r.w - lw - 1, r.h};
  u.canvas.fill_rect(left, Color::hex(0x333333));
  u.canvas.vline(left.right(), left.y, left.bottom(), u.theme.border);

  /* ---- settings column ---- */
  static int content_h = 0;
  int off = u.begin_scroll(u.id("render_settings"), left, content_h);
  ui::Layout lay{{left.x + u.px(8), left.y, left.w - u.px(20), left.h}, left.y + u.px(8) - off};
  lay.row_h = u.row_h();
  Recti b = lay.row(u.row_h() + u.px(8));
  int bw = (b.w - u.px(12)) / 4;
  if (u.button({b.x, b.y, bw, b.h}, rendering_ && !render_preview_ ? "Restart" : "Render", false, Icon::Camera)) start_final_render();
  u.tooltip("Render Image (F12) from the Main Camera. Blender: Render > Render Image.");
  if (u.button({b.x + bw + u.px(4), b.y, bw, b.h}, "Preview", render_preview_ && render_has_result_, Icon::Eye)) start_final_render(true);
  u.tooltip(strprintf("A quick look before rendering: %d%% of the resolution, %d samples (Preview settings below).\n"
                      "Turn on Live Preview to update it whenever the scene changes.",
                      scene_->render.preview_percent, scene_->render.preview_samples));
  if (u.button({b.x + 2 * (bw + u.px(4)), b.y, bw, b.h}, "Stop", false, Icon::Pause) && rendering_) {
    rendering_ = false;
    render_linear_ = final_pt_.linear_rgb(scene_->render.denoise);
    final_pt_.resolve(render_img_.pixels.data(), render_img_.width, scene_->render.denoise);
    render_status_ += "  (stopped)";
  }
  if (u.button({b.x + 3 * (bw + u.px(4)), b.y, bw, b.h}, "Save...", false, Icon::File)) defer([this] { save_render(); });
  u.tooltip("Saves to Renders/ in the chosen File Format.");
  if (!render_status_.empty()) {
    for (const std::string &part : {render_status_})
      u.label(lay.row(), part, rendering_ ? u.theme.accent : u.theme.text_dim);
  }
  if (rendering_) {
    Recti pr = lay.row(u.px(6));
    float t = std::min(1.0f, final_pt_.samples() / (float)std::max(1, scene_->render.samples));
    u.canvas.fill_round_rect(pr, u.px(3), Color::hex(0x252525));
    u.canvas.fill_round_rect({pr.x, pr.y, (int)(pr.w * t), pr.h}, u.px(3), u.theme.accent);
    u.redraw = true;
  }
  lay.space(u.px(6));
  auto header = [&](const char *t) {
    Recti h = lay.row(u.row_h() + u.px(4));
    u.canvas.fill_rect({left.x, h.y, left.w, h.h}, u.theme.header);
    u.label({h.x + u.px(4), h.y, h.w, h.h}, t, u.theme.text_bright);
  };
  header("Render Settings");
  {
    /* Resolution presets (Blender: Format presets), with the image's size in
     * megapixels so high resolutions are one click away. */
    struct Res { int w, h; const char *name; };
    static const Res kRes[] = {{640, 360, "nHD"},           {1280, 720, "HD 720p"},        {1920, 1080, "Full HD 1080p"},
                               {2560, 1440, "QHD 1440p"},   {3840, 2160, "4K UHD"},        {4096, 2160, "DCI 4K"},
                               {5120, 2880, "5K"},          {7680, 4320, "8K UHD"},        {1080, 1080, "Square 1:1"},
                               {1080, 1350, "Portrait 4:5"}, {1080, 1920, "Vertical 9:16"}, {2048, 2048, "2K texture"},
                               {4096, 4096, "4K texture"},  {3000, 2000, "Photo 3:2"},     {6000, 4000, "24 MP photo"},
                               {3508, 2480, "A4 at 300 dpi"}, {7016, 4961, "A2 at 300 dpi"}};
    constexpr int kResCount = (int)(sizeof(kRes) / sizeof(kRes[0]));
    static std::vector<std::string> labels;
    static std::vector<const char *> ptrs;
    if (labels.empty()) {
      labels.push_back("Presets...");
      for (const Res &r : kRes) labels.push_back(strprintf("%d x %d  %s  (%.1f MP)", r.w, r.h, r.name, r.w * (double)r.h / 1e6));
      for (const std::string &l : labels) ptrs.push_back(l.c_str());
    }
    RenderSettings &rs0 = scene_->render;
    InspectorReflector pr(*this, u, lay);
    Recti row = lay.row(u.row_h() + u.px(2));
    u.label(pr.label_rect(row), "Resolution Preset");
    int pick = 0;
    for (int k = 0; k < kResCount; k++)
      if (kRes[k].w == rs0.width && kRes[k].h == rs0.height) pick = k + 1;
    if (u.combo(u.id("res_preset"), pr.field_rect(row), pick, ptrs.data(), kResCount + 1) && pick > 0) {
      rs0.width = kRes[pick - 1].w;
      rs0.height = kRes[pick - 1].h;
      rs0.percent = 100;
      mark_changed("Resolution Preset");
    }
    u.tooltip("Common render sizes. MP = megapixels (width x height / 1,000,000):\n"
              "memory and render time grow with it.");
    const int fw = rs0.width * rs0.percent / 100, fh = rs0.height * rs0.percent / 100;
    u.label(lay.row(), strprintf("Output %d x %d = %.2f MP (%.0f MB of float pixels)", fw, fh, fw * (double)fh / 1e6,
                                 fw * (double)fh * 12.0 / (1024.0 * 1024.0)),
            u.theme.text_dim);
  }
  {
    InspectorReflector ir(*this, u, lay);
    u.push_id("rs");
    scene_->render.reflect(ir);
    u.pop_id();
    if (ir.changed) mark_changed("Render Settings");
  }
  lay.space(u.px(6));
  /* Blender: Preferences > System > Cycles Render Devices. Machine-specific,
   * so the ticks live in the editor prefs, not the scene. */
  header("Render Devices");
  {
    const auto &devs = gpu::devices();
    RenderSettings &rs = scene_->render;
    const bool gpu_mode = rs.device == 1;
    auto device_row = [&](const std::string &title, const std::string &detail, bool on, bool enabled) {
      Recti row = lay.row(u.row_h() * 2);
      bool v = on;
      bool clicked = enabled && u.checkbox({row.x + u.px(4), row.y + u.px(2), u.row_h() - u.px(4), u.row_h() - u.px(4)}, v);
      u.label({row.x + u.row_h() + u.px(6), row.y, row.w - u.row_h() - u.px(6), u.row_h()}, title, enabled ? u.theme.text_bright : u.theme.text_dim);
      u.label({row.x + u.row_h() + u.px(6), row.y + u.row_h() - u.px(3), row.w - u.row_h() - u.px(6), u.row_h()}, detail, u.theme.text_dim);
      return clicked;
    };
    if (device_row("CPU", strprintf("%d threads, %s", JobSystem::global().thread_count(), PathTracer::embree_available() && rs.use_embree ? "Embree" : "Blendity BVH"),
                   !gpu_mode || rs.gpu_with_cpu, gpu_mode)) {
      rs.gpu_with_cpu = !rs.gpu_with_cpu;
      mark_changed("Render Devices");
    }
    u.tooltip(gpu_mode ? "Tick to render on the CPU alongside the GPUs (combined rendering)." : "Set Device to GPU Compute to use the GPUs.");
    for (const gpu::DeviceInfo &d : devs) {
      bool on = !render_devices_off_.count(d.name);
      std::string detail = strprintf("%s, %s, %llu MB, %s", d.vendor.c_str(), d.type.c_str(), (unsigned long long)d.memory_mb,
                                     d.hardware_rt ? (rs.hardware_rt ? "ray tracing hardware" : "ray tracing hardware (off)") : "compute (no RT hardware)");
      if (device_row(d.name, detail, on && gpu_mode, gpu_mode)) {
        if (on) render_devices_off_.insert(d.name);
        else render_devices_off_.erase(d.name);
        save_prefs();
        vp_pt_hash_ = 0;
      }
    }
    if (devs.empty()) u.label(lay.row(), gpu::compiled_in() ? "No Vulkan GPU found - rendering on the CPU" : "This build has no GPU rendering", u.theme.text_dim);
    else if (!gpu_mode) u.label(lay.row(), "Device is CPU: set it to GPU Compute above to use these", u.theme.text_dim);
    if (!final_pt_.gpu_error().empty()) u.label(lay.row(), final_pt_.gpu_error(), u.theme.warning);
  }
  lay.space(u.px(6));
  header("World / Environment Lighting");
  {
    InspectorReflector ir(*this, u, lay);
    u.push_id("world");
    scene_->environment.reflect(ir);
    u.pop_id();
    if (ir.changed) {
      mark_changed("World Settings");
      vp_pt_hash_ = 0;
    }
  }
  lay.space(u.px(6));
  GameObject *owner = nullptr;
  main_camera(*scene_, &owner);
  u.label(lay.row(), owner ? "Camera: " + owner->name : "No camera - rendering from the Scene view", u.theme.text_dim);
  u.label(lay.row(), "Tip: Scene view > Rendered previews the path tracer live.", u.theme.text_dim);
  lay.space(u.px(16));
  content_h = lay.y + off - left.y;
  u.end_scroll();

  /* ---- image view ---- */
  u.canvas.push_clip(right);
  /* Transparency-style checker behind the image. */
  for (int y = right.y; y < right.bottom(); y += u.px(16))
    for (int x = right.x; x < right.right(); x += u.px(16))
      u.canvas.fill_rect({x, y, u.px(16), u.px(16)}, ((x - right.x) / u.px(16) + (y - right.y) / u.px(16)) & 1 ? Color::hex(0x2A2A2A) : Color::hex(0x303030));
  if (!render_has_result_ || render_img_.width == 0) {
    u.label(right, "Press Render (F12) to render the Main Camera, or Preview for a quick look", u.theme.text_dim, ui::Align::Center);
    u.canvas.pop_clip();
    return;
  }
  const int iw = render_img_.width, ih = render_img_.height;
  float fit = std::min((right.w - u.px(20)) / (float)iw, (right.h - u.px(20)) / (float)ih);
  if (u.hovered(right) && u.in.wheel_y != 0) {
    if (render_zoom_ <= 0) render_zoom_ = fit;
    render_zoom_ = clampf(render_zoom_ * std::pow(1.2f, u.in.wheel_y), 0.05f, 16.0f);
  }
  if (u.hovered(right) && u.in.down[2]) render_pan_ += Vec2((float)u.in.dx(), (float)u.in.dy());
  if (u.hovered(right) && u.in.double_clicked[0]) { render_zoom_ = 0; render_pan_ = Vec2(0, 0); }
  float sc = render_zoom_ > 0 ? render_zoom_ : fit;
  int dw = (int)(iw * sc), dh = (int)(ih * sc);
  int ox = right.x + (right.w - dw) / 2 + (int)render_pan_.x, oy = right.y + (right.h - dh) / 2 + (int)render_pan_.y;
  Recti dst = Recti{ox, oy, dw, dh}.intersect(right);
  Image *fb = u.canvas.target();
  for (int y = dst.y; y < dst.bottom(); y++) {
    int sy = std::min(ih - 1, (int)((y - oy) / sc));
    const uint32_t *src = render_img_.row(sy);
    uint32_t *drow = fb->row(y);
    for (int x = dst.x; x < dst.right(); x++) drow[x] = src[std::min(iw - 1, (int)((x - ox) / sc))];
  }
  u.canvas.rect_outline({ox - 1, oy - 1, dw + 2, dh + 2}, Color::hex(0x101010));
  if (render_preview_) {
    Recti badge{right.x + u.px(8), right.y + u.px(8), u.font.text_width("PREVIEW") + u.px(12), u.row_h()};
    u.canvas.fill_round_rect(badge, u.px(3), Color::hex(0xE87D0D, 220));
    u.label(badge, "PREVIEW", 0xFF000000u, ui::Align::Center);
  }
  u.label({right.x + u.px(8), right.bottom() - u.row_h() - u.px(4), right.w, u.row_h()},
          strprintf("%d x %d  |  %.0f%%  |  wheel zoom, middle-drag pan, double-click fit", iw, ih, sc * 100.0f), u.theme.text_dim);
  u.canvas.pop_clip();
}

/* File > Export's options: the side panel of Blender's export dialogs. Drawn
 * inside the Export popup; its height is measured for the next frame. */
void Editor::draw_export_options() {
  auto &u = ui_;
  Recti area = u.popup_row(std::max(export_panel_h_, u.row_h()));
  ui::Layout lay{{area.x + u.px(6), area.y, area.w - u.px(12), area.h}, area.y + u.px(2)};
  lay.row_h = u.row_h();
  InspectorReflector ir(*this, u, lay);
  const ExportOptions before = export_opts_;
  export_opts_.reflect(ir);
  if (export_opts_.format != before.format) {
    /* A new format starts from its own defaults (Blender remembers these per format). */
    ExportOptions o = export_defaults(export_opts_.format);
    o.selection_only = before.selection_only;
    o.apply_modifiers = before.apply_modifiers;
    o.scale = before.scale;
    export_opts_ = o;
  }
  export_panel_h_ = lay.y - area.y + u.px(2);
}

}  // namespace bl
