// SPDX-License-Identifier: GPL-2.0-or-later
// Hierarchy (Blender: Outliner), Inspector (Blender: Properties editor),
// Project (Blender: File/Asset Browser), Console (Blender: Info editor),
// Profiler (Blender: Statistics overlay / --debug timings).
#include "editor.h"
#include "../texture/procedural_texture.h"

#include "../core/core.h"
#include <cstring>
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
  u.canvas.fill_rect(bar, u.theme.bar);
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
      for (const char *k : {"Cube", "Sphere", "Icosphere", "Cylinder", "Cone", "Torus", "Plane", "Quad", "Teapot"})
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
    u.submenu("Curve", u.px(170), [this, child] {
      for (const char *k : {"Bezier", "Circle", "Path"})
        if (ui_.menu_item(k)) create_object(std::string("Curve: ") + k, child);
    });
    if (u.menu_item("Lattice")) create_object("Lattice", child);
    if (u.menu_item("Armature")) create_object("Armature", child);
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
  u.canvas.fill_rect(head, u.theme.pane);
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
    if (row.intersect(body).h > 0) drop_rows_.push_back({row.intersect(body), g->id});  // material assets drop here
    if (headless_) hierarchy_rows_[g->name] = row;
    if (sel) u.canvas.fill_rect(row, focused_ == WindowKind::Hierarchy ? u.theme.selection : u.theme.selection_dim);
    else if (hot) u.canvas.fill_rect(row, u.theme.row_hover);
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
        else if (!sel) hier_pending_select_ = g->id;  // selected when the button comes up (finish_hierarchy_drag), unless an
                                                      // object field took it: the Inspector then stays on the field's object
                                                      // (Unity selects on press, so its users lock the Inspector first)
        else active_ = g->id;
        last_clicked_ = g->id;
        hier_drag_ = g->id;
        hier_dragging_ = false;
        hier_press_x_ = in.mx;
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
  if (hier_drag_ && in.down[0] && std::max(std::abs(in.mx - hier_press_x_), std::abs(in.my - hier_press_y_)) > u.px(5)) hier_dragging_ = true;
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
      if (hier_pending_select_)  // dragged a row that wasn't selected: just it moves (the selection stays as it was)
        if (GameObject *dragged = scene_->find(hier_pending_select_)) moving = {dragged};
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
      else if (scene_rect_.contains(in.mx, in.my)) {
        /* Dragged into the Scene view: drop it onto the surface under the mouse (Unity). */
        drop_objects_on_surface(moving, in.mx, in.my);
      }
    }
  }
  /* (The drag ends in finish_hierarchy_drag, after every window drew: a field drawn after this one takes the drop.) */
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
    if (GameObject *ag = scene_->find(active_); ag && ag->camera() && ag->id != scene_->active_camera)
      if (u.menu_item("Set as Active Camera", nullptr, false, true)) set_active_camera(ag);
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
    /* As GameObject > Bake Textures, for the object right-clicked (the user asked for it here too). */
    const GameObject *bg = scene_->find(active_);
    const bool bakeable = bg && bg->mesh_filter() && bg->mesh_filter()->mesh;
    u.submenu("Bake Textures", u.px(200), [this, bakeable] {
      for (int size : {512, 1024, 2048, 4096})
        if (ui_.menu_item(strprintf("%d x %d", size, size), nullptr, false, bakeable)) bake_object_textures(size);
    });
    u.tooltip("Bake this object's look (procedural textures, axis materials, every material slot) into one set of images\n"
              "on new UVs, with a material that uses them. Undo restores it.");
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
  Material *material = nullptr;  // the material being edited (texture drops touch it)
  ProceduralTexture *edited_texture = nullptr;  // a texture asset being edited (the Textures window)
  std::vector<FieldEdit> edits;  // what changed this frame (for multi-object editing)
  std::string record_prefix;     // a filter effect's "E<i> ": edits carry the name other objects' reflectors see
  const std::set<std::string> *mixed = nullptr;  // fields whose values differ across the materials edited at once (task 0147): drawn as a dash
  int label_w;
  InspectorReflector(Editor &e, ui::Context &c, ui::Layout &l) : ed(e), u(c), lay(l) { label_w = std::max(u.px(110), l.area.w * 2 / 5); }
  ui::Id fid(const char *n) { return u.id(n); }
  Recti field_rect(Recti row) { return {row.x + label_w, row.y, row.w - label_w - u.px(6), row.h}; }
  Recti label_rect(Recti row) { return {row.x + u.px(4), row.y, label_w - u.px(8), row.h}; }
  FieldEdit &record(FieldEdit::Kind k, const char *n) {
    edits.emplace_back();
    edits.back().kind = k;
    edits.back().name = record_prefix + n;
    return edits.back();
  }
  /* Unity's mixed-value look: a dash over a field whose materials disagree, until it is hovered or edited (then the
   * active one's value shows); a value typed into it lands on all of them. */
  void dash_if_mixed(const char *n, Recti f) {
    if (!mixed || !mixed->count(n) || u.hovered(f) || u.is_active(fid(n)) || u.editing(fid(n))) return;
    u.canvas.fill_rect(f.shrink(u.px(2)), u.theme.field);
    u.label({f.x + u.px(5), f.y, f.w - u.px(8), f.h}, "-", u.theme.text_dim);
  }
  /* A number typed into a mixed field that the active material already holds: no change on it, but the others take
   * it (and an expression at the clamp still reaches them), so the edit is recorded anyway. */
  bool mixed_commit(const char *n, FieldEdit::Kind k, float v) {
    std::string expr;
    if (!mixed || !mixed->count(n) || !u.number_committed(fid(n), &expr)) return false;
    FieldEdit &e = record(k, n);
    e.f_old = e.f_new = v;
    e.i_new = (int)std::lround(v);
    e.expr = expr;
    return true;
  }
  void field(const char *n, float &v, float speed, float mn, float mx) override {
    Recti row = lay.row();
    float old = v;
    bool ch = u.drag_label(fid(n) ^ 0xAB, label_rect(row), n, v, speed);
    v = clampf(v, mn, mx);
    ch |= u.float_field(fid(n), field_rect(row), v, speed, mn, mx);
    dash_if_mixed(n, field_rect(row));
    if (ch) {
      FieldEdit &e = record(FieldEdit::Float, n);
      e.f_old = old;
      e.f_new = v;
      u.number_committed(fid(n), &e.expr);
    }
    else ch = mixed_commit(n, FieldEdit::Float, v);
    changed |= ch;
  }
  void field(const char *n, int &v, int mn, int mx) override {
    Recti row = lay.row();
    u.label(label_rect(row), n);
    int old = v;
    (edited_texture ? ed.texture_ui_rects_ : ed.inspector_menu_rects_)["field:" + record_prefix + n] = field_rect(row);  // tests type into it
    if (u.record_buttons) u.button_rects["field:" + record_prefix + n] = field_rect(row);  // (and find it outside the Inspector: the Materials window)
    bool ch = u.int_field(fid(n), field_rect(row), v, mn, mx);
    dash_if_mixed(n, field_rect(row));
    if (ch) {
      FieldEdit &e = record(FieldEdit::Int, n);
      e.f_old = (float)old;
      e.i_new = v;
      u.number_committed(fid(n), &e.expr);
    }
    else ch = mixed_commit(n, FieldEdit::Int, (float)v);
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
    if (mixed && mixed->count(n) && !u.hovered(field_rect(row))) {  // the box as checkbox draws it, with a dash instead of a tick
      const int sz = std::min(row.h, u.font.line_height()) - u.px(1);
      const Recti box{field_rect(row).x, row.y + (row.h - sz) / 2, sz, sz};
      u.frame(box, u.theme.field, u.theme.field_border, u.px(2));
      u.label(box, "-", u.theme.text_dim, ui::Align::Center);
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
    dash_if_mixed(n, field_rect(row));
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
    if (mixed && (mixed->count(n) || mixed->count(alpha_name))) dash_if_mixed(mixed->count(n) ? n : alpha_name, field_rect(row));
  }
  void enumeration(const char *n, int &v, const char *const *opts, int count) override {
    Recti row = lay.row();
    u.label(label_rect(row), n);
    if (u.combo(fid(n), field_rect(row), v, opts, count)) {
      record(FieldEdit::Enum, n).i_new = v;
      if (material && std::strcmp(n, "Toon Style") == 0) apply_toon_style(*material, v);  // (task 0148: the preset's fields, picked again too)
      changed = true;
    }
  }
  /* An axis material's part: a button with the material's name that opens the material picker, and a clear button.
   * The pick lands at the end of the frame (the popup), through Editor::set_material_ref. */
  void material_ref(const char *n, std::string &v) override {
    if (!material) {
      text(n, v);
      return;
    }
    Recti row = lay.row();
    u.label(label_rect(row), n);
    const Recti f = field_rect(row);
    const Recti b{f.x, f.y, f.w - f.h - u.px(2), f.h};
    MaterialPtr cur = v.empty() ? nullptr : material_asset(v);
    const std::string label = v.empty() ? std::string("(this material's own maps)") : cur ? cur->name : "Missing: " + v;
    const ui::Id pid = fid(n) ^ 0x5A5Aull;
    ed.inspector_menu_rects_[std::string("matref:") + n] = b;
    if (u.button(b, label, false)) u.open_popup(pid, b);
    u.tooltip(v.empty() ? "Pick a material asset for this part." : v);
    Editor *edp = &ed;
    Material *mat = material;
    const std::string field = n;
    ed.material_picker_popup(pid, nullptr, [edp, mat, field](const MaterialPtr &m) { edp->set_material_ref(mat, field, m); });  // (the popup runs this frame)
    const Recti xb{b.right() + u.px(2), f.y, f.h, f.h};
    if (!v.empty() && u.icon_button(xb, Icon::Close, false, "Use this material's own maps for this part")) {
      v.clear();
      record(FieldEdit::Text, n).s_new = v;
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
  /* Unity's object field: type the name, or drag the object here from the Hierarchy (of the field's type). */
  void object(const char *n, std::string &v, const char *type) override {
    Recti row = lay.row();
    u.label(label_rect(row), n);
    if (!ed.object_field(n, field_rect(row), v, type)) return;
    record(FieldEdit::Text, n).s_new = v;
    changed = true;
  }
  /* An asset field: the file dragged here from the Project window, or picked. */
  void asset(const char *n, std::string &p, const char *ext, const char *kind) override {
    Recti row = lay.row();
    u.label(label_rect(row), n);
    if (!ed.asset_field(n, field_rect(row), p, ext, kind)) return;
    record(FieldEdit::Text, n).s_new = p;
    changed = true;
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

  /* A collapsible group (task 0145): a foldout row, its fields indented under it and skipped while it is closed. The
   * state is kept by the group's name - per material for a material's groups (an axis material's Axis Material
   * group opens by its own default, a plain one's stays closed), by name alone for a component's. */
  std::vector<bool> groups_open;
  std::string group_key(const char *n) const { return "grp_" + (material ? std::to_string((uintptr_t)material) : std::string()) + n; }
  bool group(const char *n, bool open_by_default) override {
    lay.space(u.px(2));
    Recti row = lay.row(u.row_h() + u.px(2));
    bool &open = ed.foldouts_.emplace(group_key(n), open_by_default).first->second;
    u.foldout({row.x + u.px(4), row.y, row.w - u.px(8), row.h}, n, open);
    (edited_texture ? ed.texture_ui_rects_ : ed.inspector_menu_rects_)["group:" + std::string(n)] = row;  // tests click it
    if (open) lay.indent += u.px(10);
    groups_open.push_back(open);
    return open;
  }
  void group_end() override {
    if (groups_open.empty()) return;
    if (groups_open.back()) lay.indent -= u.px(10);
    groups_open.pop_back();
  }

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
    /* What the renderers use: an image file, a generated image, or a procedural texture's output (a .ptex
     * reference such as Bricks.ptex#albedo is no file: loading it as one showed it missing). */
    const TexturePtr tex = resolve_texture_ref(t);
    const bool procedural = texture_ref_is_procedural(t);
    draw_texture_thumb(u, tex, thumb);
    Recti name{f.x + f.h + u.px(4), f.y, f.w - f.h - u.px(4), f.h};
    ed.drop_textures_.push_back({f, &t, material});  // an image dragged from the Project window lands here
    bool hot = u.hovered(name) || u.hovered(thumb);
    u.frame(name, u.theme.field, hot ? u.theme.field_hover : u.theme.field_border, u.px(3));
    std::string label = texture_ref_label(t);
    if (!t.empty() && !tex) label += "  (missing)";
    if (t.non_color && !t.empty() && !procedural) label += "  [Non-Color]";  // a .ptex output's colour space is its own
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
      if (!cur.empty() && !texture_ref_is_procedural(cur) &&
          uc->menu_item(cur.non_color ? "Color Space: Non-Color (click for sRGB)" : "Color Space: sRGB (click for Non-Color)"))
        pick(cur.path, !cur.non_color);
      /* The procedural textures (Window > Textures) and their outputs. */
      const auto &ptex = e->texture_asset_paths();
      if (!ptex.empty()) {
        uc->menu_separator();
        uc->menu_label("Procedural textures");
        for (const std::string &p : ptex)
          for (int o = 0; o < (int)TextureOutput::Count; o++) {
            const std::string ref = p + "#" + texture_output_name(o);
            if (uc->menu_item(fs::stem(p) + " (" + texture_output_name(o) + ")", nullptr, ref == cur.path)) pick(ref, o != (int)TextureOutput::Albedo);
          }
      }
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
    u.tooltip("Remove the last slot. Each slot's x removes that one; faces on it move to the slot before.");
    GameObject *owner = ed.active_object();
    int remove_slot = -1;
    if (owner && mats.size() > 1) {
      Recti ur = lay.row();
      if (u.button({ur.x + label_w, ur.y, ur.w - label_w - u.px(6), ur.h}, "Remove Unused Slots")) {
        ed.remove_unused_material_slots(owner);
        return;
      }
      u.tooltip("Remove every slot no face uses (Blender: Material Specials > Remove Unused Slots).");
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
      if (owner) {
        /* An x per slot (Blender's "-" on the selected slot), and a drop target for material assets. */
        Recti xr{fr.right() - fr.h, fr.y, fr.h, fr.h};
        if (u.icon_button(xr, Icon::Close, false, "Remove this material slot")) remove_slot = (int)i;
        fr.w -= fr.h + u.px(2);
        ed.drop_slots_.push_back({fr, owner->id, (int)i});
      }
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
        /* Unity: a new material is an asset (Assets/Materials/<name>.mat) from the start. */
        if (uc->menu_item("New Material", nullptr, false, true, Icon::Plus)) {
          results[pid] = {e->new_slot_material(cur, ""), false};
          uc->redraw = true;
        }
        uc->tooltip("A new material, saved as an asset in Assets/Materials (a copy of this slot's material, if it has one).");
        uc->submenu("New Material of Type", uc->px(200), [uc, pid, e] {
          for (const std::string &p : material_presets())
            if (uc->menu_item(p)) {
              results[pid] = {e->new_slot_material(nullptr, p), false};
              uc->redraw = true;
            }
        });
        if (uc->menu_item("None")) {
          results[pid] = {nullptr, true};
          uc->redraw = true;
        }
        if (cur && cur->asset_path.empty() && uc->menu_item("Save as Material Asset", nullptr, false, true, Icon::File)) {
          /* Unity: drag a material into the Project window. The slot (and every
           * other slot sharing it) now uses the asset. */
          if (MaterialPtr a = e->new_material_asset(cur, true)) results[pid] = {a, false};
          uc->redraw = true;
        }
        uc->tooltip("Write this material to Assets/Materials as a .mat you can drag onto other objects.");
        uc->menu_separator();
        uc->menu_label("Material assets");
        for (const std::string &path : e->material_asset_paths()) {
          /* Folders show as a prefix: "Bricks/Red" for Assets/Materials/Bricks/Red.mat. */
          std::string shown = path.substr(0, path.size() - 4);
          if (starts_with(shown, "Assets/Materials/")) shown = shown.substr(17);
          else if (starts_with(shown, "Assets/")) shown = shown.substr(7);
          if (uc->menu_item(shown, nullptr, cur && cur->asset_path == path)) {
            if (MaterialPtr a = material_asset(path)) results[pid] = {a, false};
            uc->redraw = true;
          }
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
    if (remove_slot >= 0 && owner) {
      ed.remove_material_slot(owner, remove_slot);
      return;
    }
    /* Inline editors, one per distinct material (shared slots edit once). With several objects selected, an edit
     * reaches the others' materials in the same slot that show the field (task 0147), as component edits do. */
    std::vector<Material *> seen;
    const std::vector<GameObject *> selected = ed.selected_objects(false);
    for (size_t slot = 0; slot < mats.size(); slot++) {
      const MaterialPtr &m = mats[slot];
      if (!m || std::find(seen.begin(), seen.end(), m.get()) != seen.end()) continue;
      seen.push_back(m.get());
      lay.space(u.px(4));
      Recti h = lay.row(u.row_h() + u.px(4));
      u.canvas.fill_rect(h, Color::hex(0x333740));
      bool &open = ed.foldouts_.emplace("mat_" + std::to_string((uintptr_t)m.get()), true).first->second;
      u.foldout({h.x + u.px(4), h.y, h.w - u.px(8), h.h}, "Material  " + m->name, open, Icon::Eye);
      u.tooltip("Blender: Material Properties (Principled BSDF). Unity: the material inspector at the bottom of the Inspector.");
      if (!open) continue;
      /* The other selected objects' materials in this slot (a material in two slots is gathered from its first). */
      std::vector<MaterialPtr> others;
      if (selected.size() > 1)
        for (GameObject *o : selected)
          if (const MeshRenderer *omr = o->get<MeshRenderer>(); omr && slot < omr->materials.size() && omr->materials[slot] &&
              std::find(others.begin(), others.end(), omr->materials[slot]) == others.end())
            others.push_back(omr->materials[slot]);
      const std::set<std::string> mixed = others.size() > 1 ? ed.mixed_material_fields(m.get(), others) : std::set<std::string>();
      if (others.size() > 1) {
        u.label(lay.row(), strprintf("Editing %zu materials (slot %zu of the selected objects): a change reaches the ones that have the field", others.size(), slot + 1),
                u.theme.accent);
        u.tooltip("A dash: their values differ; what you type lands on all (+=0.1 moves each from its own value).");
      }
      u.push_id((uint64_t)(uintptr_t)m.get());
      InspectorReflector sub(ed, u, lay);
      sub.label_w = label_w;
      sub.material = m.get();
      if (others.size() > 1) sub.mixed = &mixed;
      m->reflect(sub);
      u.pop_id();
      if (sub.changed) {
        m->touch();
        if (others.size() > 1) ed.apply_material_edits(sub.edits, m.get(), others);
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
  /* A camera's filter stack (ADR 0008), like Unity's post-processing profile: each effect a
   * foldout with its toggle and a menu (Move Up / Down, Reset, Remove), then Add Filter by
   * category. Menus run at the end of the frame: they edit the stack through filter_stack_op,
   * by object id. */
  /* A texture's layer stack (task 0028), like a camera's filter stack: each layer a foldout with its checkbox and a
   * menu (Move Up / Down, Reset, Remove), then Add Layer by category. Menus edit through texture_stack_op at the
   * end of the frame, by the asset's path. */
  void layer_list(const char *, TextureLayerList &layers) override {
    if (!edited_texture) return;
    const std::string path = edited_texture->asset_path;
    const int n = (int)layers.size();
    if (layers.empty()) {
      Recti er = lay.row();
      u.label({er.x + u.px(8), er.y, er.w - u.px(16), er.h}, "No layers. Add Layer starts the stack (a generator first).", u.theme.text_dim);
    }
    for (int i = 0; i < n; i++) {
      TextureLayer &l = *layers[(size_t)i];
      u.push_id((uint64_t)i + 9000);
      lay.space(u.px(2));
      Recti h = lay.row(u.row_h() + u.px(4));
      h.x += u.px(4);
      h.w -= u.px(8);
      u.canvas.fill_round_rect(h, u.px(3), u.theme.header);
      const int a = u.font.line_height() - u.px(4);
      Recti fr{h.x, h.y, a + u.px(6), h.h};
      if (u.hovered(fr) && u.in.pressed[0]) l.ui_expanded = !l.ui_expanded;
      u.draw_icon(l.ui_expanded ? Icon::ArrowDown : Icon::ArrowRight, {h.x + u.px(2), h.y + (h.h - a) / 2, a, a}, u.theme.text);
      const Recti cb{fr.right(), h.y, a + u.px(4), h.h};
      ed.texture_ui_rects_["layer_enabled" + std::to_string(i)] = cb;
      if (u.checkbox(cb, l.enabled)) {
        record(FieldEdit::Bool, ("L" + std::to_string(i) + " Enabled").c_str()).b_new = l.enabled;
        changed = true;
      }
      const TextureLayerInfo *info = find_texture_layer_info(l.type_name());
      Recti tr{fr.right() + a + u.px(8), h.y, h.w - (fr.right() - h.x) - a - u.px(36), h.h};
      u.label(tr, l.type_name(), l.enabled ? u.theme.text_bright : u.theme.text_dim);
      if (u.hovered(tr) && u.in.pressed[0]) l.ui_expanded = !l.ui_expanded;
      if (!path.empty()) ed.stack_drag_item("layers:" + path, i, tr, h, &l.ui_expanded);  // (an unsaved texture can't move)
      if (u.record_buttons) u.button_rects["layer_head" + std::to_string(i)] = tr;  // (tests)
      if (info) u.tooltip(info->category + " > " + info->name + "\n" + info->help);
      const int bs = h.h - u.px(4);
      Recti mr{h.right() - u.px(2) - bs, h.y + u.px(2), bs, bs};
      ui::Id mid = u.id("layer_menu");
      ed.texture_ui_rects_["layer" + std::to_string(i)] = mr;
      if (u.icon_button(mr, Icon::Menu, false, "Move, reset or remove this layer")) u.open_popup(mid, mr);
      Editor *edp = &ed;
      u.popup(mid, u.px(200), [edp, path, i, n] {
        auto &uu = edp->ui_;
        if (uu.menu_item("Move Up", nullptr, false, i > 0)) edp->texture_stack_op(path, "move", i, i - 1);
        if (uu.menu_item("Move Down", nullptr, false, i + 1 < n)) edp->texture_stack_op(path, "move", i, i + 1);
        if (uu.menu_item("Reset")) edp->texture_stack_op(path, "reset", i);
        uu.menu_separator();
        if (uu.menu_item("Remove Layer")) edp->texture_stack_op(path, "remove", i);
      });
      if (l.ui_expanded) {
        lay.indent += u.px(10);
        record_prefix = "L" + std::to_string(i) + " ";
        l.reflect_common(*this, false);
        l.reflect(*this);
        record_prefix.clear();
        lay.indent -= u.px(10);
      }
      u.pop_id();
    }
    int from = 0, to = 0;
    if (ed.stack_drag_end("layers:" + path, from, to)) ed.texture_stack_op(path, "move", from, to);
    lay.space(u.px(4));
    Recti ar = lay.row(u.row_h() + u.px(4));
    Recti ab{ar.x + u.px(4), ar.y, std::min(ar.w - u.px(8), u.px(200)), ar.h};
    ui::Id add = u.id("add_layer");
    ed.texture_ui_rects_["add_layer"] = ab;
    if (u.button(ab, "Add Layer", false, Icon::Plus)) u.open_popup(add, ab);
    u.tooltip("Add a generator (a pattern) or a filter (changes what is below) to the top of the stack.");
    Editor *edp = &ed;
    u.popup(add, u.px(240), [edp, path] {
      auto &uu = edp->ui_;
      std::string category;
      for (const TextureLayerInfo &li : texture_layer_infos()) {
        if (li.category != category) {
          category = li.category;
          uu.menu_label(category);
        }
        if (uu.menu_item(li.name)) edp->texture_stack_op(path, "add", 0, 0, li.name);
        uu.tooltip(li.help);
      }
    });
  }
  void filter_effects(const char *, FilterEffectList &effects) override {
    GameObject *g = ed.active_object();
    if (!g) return;
    const uint64_t gid = g->id;
    const int n = (int)effects.size();
    if (effects.empty()) {
      Recti er = lay.row();
      u.label({er.x + u.px(8), er.y, er.w - u.px(16), er.h}, "No filters. Add Filter to start a stack.", u.theme.text_dim);
    }
    for (int i = 0; i < n; i++) {
      FilterEffect &e = *effects[(size_t)i];
      u.push_id((uint64_t)i + 7000);
      lay.space(u.px(2));
      Recti h = lay.row(u.row_h() + u.px(4));
      h.x += u.px(4);
      h.w -= u.px(8);
      u.canvas.fill_round_rect(h, u.px(3), u.theme.header);
      const int a = u.font.line_height() - u.px(4);
      Recti fr{h.x, h.y, a + u.px(6), h.h};
      if (u.hovered(fr) && u.in.pressed[0]) e.ui_expanded = !e.ui_expanded;
      u.draw_icon(e.ui_expanded ? Icon::ArrowDown : Icon::ArrowRight, {h.x + u.px(2), h.y + (h.h - a) / 2, a, a}, u.theme.text);
      if (u.checkbox({fr.right(), h.y, a + u.px(4), h.h}, e.enabled)) {
        record(FieldEdit::Bool, ("E" + std::to_string(i) + " Enabled").c_str()).b_new = e.enabled;
        changed = true;
      }
      const FilterEffectInfo *info = find_filter_effect_info(e.type_name());
      Recti tr{fr.right() + a + u.px(8), h.y, h.w - (fr.right() - h.x) - a - u.px(36), h.h};
      u.label(tr, e.type_name(), e.enabled ? u.theme.text_bright : u.theme.text_dim);
      if (u.hovered(tr) && u.in.pressed[0]) e.ui_expanded = !e.ui_expanded;
      ed.stack_drag_item("filters:" + std::to_string(gid), i, tr, h, &e.ui_expanded);
      if (u.record_buttons) u.button_rects["filter_head" + std::to_string(i)] = tr;  // (tests)
      if (info) u.tooltip(info->category + " > " + info->name + "\n" + info->help);
      const int bs = h.h - u.px(4);
      Recti mr{h.right() - u.px(2) - bs, h.y + u.px(2), bs, bs};
      ui::Id mid = u.id("filter_menu");
      ed.inspector_menu_rects_["filter" + std::to_string(i)] = mr;
      if (u.icon_button(mr, Icon::Menu, false, "Move, reset or remove this filter")) u.open_popup(mid, mr);
      Editor *edp = &ed;
      u.popup(mid, u.px(200), [edp, gid, i, n] {
        auto &uu = edp->ui_;
        if (uu.menu_item("Move Up", nullptr, false, i > 0)) edp->filter_stack_op(gid, "move", i, i - 1);
        if (uu.menu_item("Move Down", nullptr, false, i + 1 < n)) edp->filter_stack_op(gid, "move", i, i + 1);
        if (uu.menu_item("Reset")) edp->filter_stack_op(gid, "reset", i);
        uu.menu_separator();
        if (uu.menu_item("Remove Filter")) edp->filter_stack_op(gid, "remove", i);
      });
      if (e.ui_expanded) {
        lay.indent += u.px(10);
        record_prefix = "E" + std::to_string(i) + " ";  // shown as "Width", applied to other cameras as "E0 Width"
        e.reflect(*this);
        record_prefix.clear();
        lay.indent -= u.px(10);
      }
      u.pop_id();
    }
    int from = 0, to = 0;
    if (ed.stack_drag_end("filters:" + std::to_string(gid), from, to)) ed.filter_stack_op(gid, "move", from, to);
    /* Add Filter, by category. */
    lay.space(u.px(4));
    Recti ar = lay.row(u.row_h() + u.px(4));
    Recti ab{ar.x + u.px(4), ar.y, std::min(ar.w - u.px(8), u.px(200)), ar.h};
    ui::Id add = u.id("add_filter");
    ed.inspector_menu_rects_["add_filter"] = ab;
    if (u.button(ab, "Add Filter", false, Icon::Plus)) u.open_popup(add, ab);
    u.tooltip("Add an effect to this camera's stack (Unity: Add Effect in a post-processing profile).");
    Editor *edp = &ed;
    std::vector<uint64_t> targets{gid};
    for (GameObject *o : ed.selected_objects(false))
      if (o->id != gid && o->get<CameraFilters>()) targets.push_back(o->id);
    u.popup(add, u.px(240), [edp, targets] {
      auto &uu = edp->ui_;
      std::string category;
      for (const FilterEffectInfo &fi : filter_effect_infos()) {
        if (fi.category != category) {
          category = fi.category;
          uu.menu_label(category);
        }
        if (uu.menu_item(fi.name))
          for (uint64_t id : targets) edp->filter_stack_op(id, "add", 0, 0, fi.name);
        uu.tooltip(fi.help);
      }
      uu.menu_label("Looks");
      for (const std::string &look : filter_looks()) {
        if (uu.menu_item(look))
          for (uint64_t id : targets) edp->filter_stack_op(id, "look", 0, 0, look);
        uu.tooltip(look == "Anime" ? "One effect: ink lines, a little more saturation and contrast, a soft bloom."
                                   : "One effect: greys, halftone dots, ink lines and paper - a printed manga page.");
      }
    });
  }
};

void Editor::draw_inspector(const Recti &r) {
  auto &u = ui_;
  inspector_menu_rects_.clear();
  /* A menu choice not applied by the next frame (nothing selected, a folded section, an undo in
   * between) is dropped rather than kept for later. */
  if (inspector_action_.obj && u.frame_number() > inspector_action_.frame + 2) inspector_action_ = {};
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
      if (ext == ".mat") {
        /* Unity's material asset inspector: edit the shared material itself. */
        if (MaterialPtr m = material_asset(make_asset_relative(project_selected_))) {
          lay.space(u.px(6));
          u.label(lay.row(), "Material asset - drag it from the Project window onto an object, a face or a slot", u.theme.accent);
          ui::Id sid = u.id("mat_asset_scroll");
          static int content_h = 0;
          Recti area{r.x, lay.y, r.w, r.bottom() - lay.y};
          const int off = u.begin_scroll(sid, area, content_h);
          ui::Layout ml{{r.x + u.px(6), area.y, r.w - u.px(18), area.h}, area.y + u.px(4) - off};
          ml.row_h = u.row_h();
          /* Several .mat files selected (task 0147): the edit reaches each of them that shows the field. */
          std::vector<MaterialPtr> others;
          for (const std::string &p : project_multi_.items)
            if (fs::extension(p) == ".mat")
              if (MaterialPtr om = material_asset(make_asset_relative(p)); om && std::find(others.begin(), others.end(), om) == others.end()) others.push_back(om);
          u.push_id((uint64_t)(uintptr_t)m.get());
          draw_material_fields(ml, m, others.size() > 1 ? &others : nullptr);
          u.pop_id();
          content_h = ml.y + off - area.y + u.px(20);
          u.end_scroll();
          return;
        }
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
      inspector_menu_rects_[key] = kr;
      if (u.icon_button(kr, Icon::Menu, false, "Component menu")) u.open_popup(mid, kr);
      u.popup(mid, u.px(200), menu);
    }
    return open;
  };

  /* Transform (always first, like Unity) */
  if (section("Transform", "Transform", nullptr, Icon::Move,
              "Position / Rotation / Scale relative to the parent.\nBlender: Object Properties > Transform (Location, Rotation, Scale).\nTheory: FoCG ch. 7, GEA Vol. I ch. 5.3.",
              [this, gid = g->id] {
                if (ui_.menu_item("Reset"))
                  if (GameObject *o = scene_->find(gid)) {  // by id: the object may be gone by the end of the frame
                    o->set_local(Transform());
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

  /* Components. A menu choice made last frame (see InspectorAction) is applied first. */
  int remove_idx = -1, move_up = -1, reset_idx = -1;
  if (inspector_action_.obj && inspector_action_.obj != g->id) inspector_action_ = {};  // chosen on another object
  if (inspector_action_.obj == g->id && inspector_action_.act >= kCompRemove) {
    for (size_t ci = 0; ci < g->components.size(); ci++)
      if (g->components[ci].get() == inspector_action_.comp) {
        if (inspector_action_.act == kCompRemove) remove_idx = (int)ci;
        if (inspector_action_.act == kCompMoveUp) move_up = (int)ci;
        if (inspector_action_.act == kCompReset) reset_idx = (int)ci;
      }
    inspector_action_ = {};
  }
  if (reset_idx >= 0)
    if (auto fresh = create_component(g->components[reset_idx]->type_name())) {
      fresh->owner = g;
      if (auto *mf = dynamic_cast<MeshFilter *>(g->components[reset_idx].get())) static_cast<MeshFilter *>(fresh.get())->mesh = mf->mesh;
      g->components[reset_idx] = std::move(fresh);
      mark_changed("Reset Component");
    }
  if (remove_idx >= 0 && !std::strcmp(g->components[remove_idx]->type_name(), IKConstraint::kName)) {
    remove_ik(g);  // (its target and pole go with it, task 0067)
    remove_idx = -1;
  }
  if (remove_idx >= 0) {
    g->components.erase(g->components.begin() + remove_idx);
    mark_changed("Remove Component");
    remove_idx = -1;
  }
  if (move_up > 0) {
    std::swap(g->components[move_up], g->components[move_up - 1]);
    mark_changed("Reorder Components");
    move_up = -1;
  }
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
    bool open = section(c->type_name(), std::string(c->type_name()) + std::to_string(ci), &en, icon, tip, [this, gid = g->id, c, idx] {
      auto choose = [&](int act) {
        inspector_action_ = {gid, c, act, ui_.frame_number()};
        ui_.redraw = true;
      };
      if (ui_.menu_item("Remove Component")) choose(kCompRemove);
      if (ui_.menu_item("Move Up", nullptr, false, idx > 0)) choose(kCompMoveUp);
      if (ui_.menu_item("Reset")) choose(kCompReset);
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
        row = lay.row(u.row_h() + u.px(2));
        const int hw = (row.w - u.px(12)) / 2;
        const bool piloting = pilot_cam_ == g->id;
        if (u.button({row.x + u.px(4), row.y, hw, row.h}, piloting ? "Stop Piloting (Esc)" : "Pilot Camera", piloting, Icon::Camera)) toggle_pilot_camera();
        u.tooltip("Look through this camera in the Scene view and frame the shot by navigating: orbit, pan, fly and the wheel\n"
                  "move the camera itself, Ctrl + wheel changes its field of view (its lens). Blender: Lock Camera to View.");
        if (u.button({row.x + u.px(8) + hw, row.y, hw, row.h}, "Align to View")) align_camera_to_view();
        u.tooltip("Move this camera to where the Scene view is looking from (Unity: GameObject > Align With View).");
        row = lay.row(u.row_h() + u.px(2));
        const bool is_active = scene_->active_camera == g->id;
        if (u.button({row.x + u.px(4), row.y, row.w - u.px(8), row.h}, is_active ? "Active Camera (the Game view and renders use it)" : "Set as Active Camera",
                     is_active, Icon::Camera) &&
            !is_active)
          set_active_camera(g);
        u.tooltip("The Game view, F12 and the Scene view's filters use the scene's Active Camera (Blender: Ctrl+Numpad0, the\n"
                  "scene camera; Unity: the MainCamera tag). Without one, \"Main Camera\" by name, else the first camera.");
      }
      /* Light: the same eyedropper aims it (Blender: Track To / Point At; Unity: LookAt). */
      if (auto *lc = dynamic_cast<Light *>(c)) {
        Recti row = lay.row(u.row_h() + u.px(2));
        const bool picking = focus_pick_cam_ == g->id;
        const char *label = lc->type == 1 ? "Pick Point to Reach (sets Range)" : "Aim at Point";
        if (u.button({row.x + u.px(4), row.y, row.w - u.px(8), row.h}, picking ? "Click a point in the Scene view... (Esc cancels)" : label,
                     picking, Icon::Light))
          focus_pick_cam_ = picking ? 0 : g->id;
        u.tooltip("Eyedropper: click a surface in the Scene view and the light turns to shine at that point\n"
                  "(its range grows to reach it if needed). Like the camera's Pick Focus Point.");
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
        if (mf->mesh) {
          /* Vertex groups and Weight Paint live in the Rigging window (task 0052): one row leads there. */
          Recti wrow = lay.row(u.row_h() + u.px(2));
          const size_t ng = mf->mesh->vertex_groups.size();
          const std::string label = ng ? strprintf("Vertex Groups (%zu), Weight Paint: Rigging...", ng) : std::string("Vertex Groups, Weight Paint: Rigging...");
          if (u.button({wrow.x + u.px(4), wrow.y, wrow.w - u.px(8), wrow.h}, label, weight_paint_ && wp_obj_ == g->id, Icon::Vertex))
            dock_open(WindowKind::Rigging);
          u.tooltip("Opens the Rigging window: skeletons, skins, vertex groups, Weight Paint and IK (Window > Rigging).");
        }
        Recti row = lay.row(u.row_h() + u.px(2));
        if (u.button({row.x + u.px(4), row.y, row.w - u.px(8), row.h}, edit_mode_ ? "Exit Edit Mode (Tab)" : "Enter Edit Mode (Tab)", edit_mode_, Icon::Vertex)) {
          if (edit_mode_) exit_edit_mode(); else enter_edit_mode();
        }
        if (edit_mode_ && g->id == edit_obj_) {
          draw_edit_tools(lay);
        }
        auto &feats = research::features();
        if (!feats.empty()) {
          lay.space(u.px(2));
          u.label(lay.row(), "From Papers (Learn > References)", u.theme.accent);
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
        static const Item kDeform[] = {{"Cast", "CastModifier"}, {"Curve", "CurveModifier"}, {"Displace", "DisplaceModifier"}, {"Lattice", "LatticeModifier"}, {"Mesh Deform", "MeshDeformModifier"}, {"Armature (Skin)", "SkinModifier"},
                                       {"Simple Deform", "SimpleDeformModifier"}, {"Smooth", "SmoothModifier"},
                                       {"Smooth Normals", "SmoothNormalsModifier"}, {"Normal Transfer", "NormalTransferModifier"}};
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
      enum Act { None, Apply, Duplicate, CopyToSelected, Up, Down, First, Last, Remove, Reset, DragTo };
      Act act = None;
      size_t act_ci = 0, act_to = 0;
      if (inspector_action_.obj == g->id && inspector_action_.act > None && inspector_action_.act < kCompRemove) {
        for (size_t ci = 0; ci < g->components.size(); ci++)
          if (g->components[ci].get() == inspector_action_.comp) act = (Act)inspector_action_.act, act_ci = ci;
        inspector_action_ = {};
      }
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
        inspector_menu_rects_["mod" + std::to_string(ci)] = mr;
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
        stack_drag_item("mods:" + std::to_string(g->id), (int)k, tr, h, &c->ui_expanded);
        if (u.record_buttons) u.button_rects["mod_head" + std::to_string(k)] = tr;  // (tests)
        if (const ComponentInfo *info = find_component_info(c->type_name())) u.tooltip(info->help + "\nBlender: " + info->blender);
        const size_t kk = k;
        u.popup(mid, u.px(210), [this, gid = g->id, c, kk, nmods = mods.size()] {
          auto &u = ui_;
          auto choose = [&](Act a) {
            inspector_action_ = {gid, c, (int)a, u.frame_number()};
            u.redraw = true;
          };
          if (u.menu_item("Apply", "Ctrl+A")) choose(Apply);
          u.tooltip("Bake this modifier into the mesh and remove it (Blender: Apply).");
          if (u.menu_item("Duplicate", "Shift+D")) choose(Duplicate);
          if (u.menu_item("Copy to Selected", nullptr, false, selection_.size() > 1)) choose(CopyToSelected);
          u.menu_separator();
          if (u.menu_item("Move Up", nullptr, false, kk > 0)) choose(Up);
          if (u.menu_item("Move Down", nullptr, false, kk + 1 < nmods)) choose(Down);
          if (u.menu_item("Move to First", nullptr, false, kk > 0)) choose(First);
          if (u.menu_item("Move to Last", nullptr, false, kk + 1 < nmods)) choose(Last);
          u.menu_separator();
          if (u.menu_item("Reset")) choose(Reset);
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
          if (dynamic_cast<SkinModifier *>(c)) draw_skin_bind_buttons(g, lay);  // (task 0047; inpainted, task 0057)
          if (auto *md = dynamic_cast<MeshDeformModifier *>(c)) {
            /* Bind / Unbind (Blender's button), and why it does nothing when it can't. */
            Recti br = lay.row(u.row_h() + u.px(2));
            if (u.button({br.x + u.px(14), br.y, br.w - u.px(18), br.h}, md->bound() ? "Unbind" : "Bind", md->bound()))
              mesh_deform_bind(g, md, !md->bound());
            u.tooltip(md->bound() ? "Forget the cage's shape: the mesh goes back to its own shape."
                                  : "Remember the cage's shape now: moving its vertices afterwards moves this mesh.");
            if (!md->last_error.empty()) {
              Recti er = lay.row();
              u.label({er.x + u.px(14), er.y, er.w - u.px(18), er.h}, md->last_error, u.theme.warning);
            }
          }
        }
        u.pop_id();
      }
      if (mods.empty()) {
        Recti er = lay.row();
        u.label({er.x + u.px(8), er.y, er.w - u.px(16), er.h}, "No modifiers. Add Modifier to start a stack.", u.theme.text_dim);
      }
      int drag_from = 0, drag_to = 0;  // a header dragged to another place (task 0071)
      if (stack_drag_end("mods:" + std::to_string(g->id), drag_from, drag_to) && (size_t)drag_from < mods.size())
        act = DragTo, act_ci = mods[(size_t)drag_from], act_to = (size_t)drag_to;
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
              run_modifier(*g->components[act_ci], m);  // its vertex group, and the groups carried (ADR 0024)
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
          case DragTo: move_to(act_to); mark_changed("Move Modifier"); break;
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
      /* Another stage's components (task 0055) show only when searched for by name. */
      if (f.empty() && ((ci.category == "Rigging" && !shows(FocusRig)) || (ci.category == "Rendering" && !shows(FocusLight | FocusMaterial | FocusReference))))
        continue;
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

/* ===================================================================== */
/* Console                                                                */
/* ===================================================================== */

void Editor::draw_console(const Recti &r) {
  auto &u = ui_;
  auto &in = u.in;
  int bh = u.row_h() + u.px(6);
  Recti bar{r.x, r.y, r.w, bh};
  u.canvas.fill_rect(bar, u.theme.bar);
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
    else if (i % 2) u.canvas.fill_rect(row, u.theme.stripe);
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

/* Task 0159: what the card itself did in the frame, against the time the frame spent in the GPU path. The views'
 * pass times come from the raster device's timestamp queries, F12's and the Camera Preview's from the path tracer's;
 * the read-back is the CPU's timer around the copy. A card without timestamps shows "n/a" beside the wall time. */
void Editor::draw_profiler_gpu(const FrameProfile &fp, const std::function<void(const std::string &, const std::string &)> &stat) {
  const bool views_on_card = fp.gpu_views > 0 || scene_on_gpu_;  // (a frame whose views were all reused still shows the block)
  if (!views_on_card && !rendering_ && !cam_preview_rendered_) return;
  if (views_on_card) draw_profiler_gpu_views(fp, stat);
  if (rendering_) draw_profiler_gpu_tracer("F12 render", final_pt_.stats(), render_time_ * 1000.0, "the render so far", stat);
  if (cam_preview_rendered_) draw_profiler_gpu_tracer("Camera Preview", cam_preview_pt_.stats(), frame_ms_, "the frame", stat);
}

void Editor::draw_profiler_gpu_views(const FrameProfile &fp, const std::function<void(const std::string &, const std::string &)> &stat) {
  const double busy = fp.gpu_busy_ms(), wait = std::max(0.0, fp.ms_gpu - busy - fp.ms_gpu_readback);
  const bool stamps = busy > 0.0 || fp.ms_gpu == 0.0;
  stat("GPU views: the GPU path (wall)", strprintf("%.2f ms", fp.ms_gpu));
  stat("GPU views: busy (shadow / g-buffer / lighting)", stamps ? strprintf("%.2f ms (%.2f / %.2f / %.2f)", busy, fp.ms_gpu_shadow, fp.ms_gpu_gbuffer, fp.ms_gpu_lighting) : std::string("n/a (no timestamps)"));
  stat("GPU views: copies out + fence / wait", strprintf("%.2f ms / %.2f ms", fp.ms_gpu_readback, wait));  // (the wait: the GPU path's rest - gathering, uploads, the CPU's recording)
  stat("GPU busy in the frame", stamps && frame_ms_ > 0.0 ? strprintf("%.0f %% of %.2f ms", 100.0 * busy / frame_ms_, frame_ms_) : std::string("n/a"));
  for (int k = 0; k < gpu::Raster::kSlots; k++) {  // (task 0163: why a view drew on the CPU instead; a slot's last view keeps its reason until the device is chosen again)
    if (gpu_slot_reason_[k].empty()) continue;
    const RenderTarget *rt = gpu_slot_rt_[k];
    const char *view = rt == &scene_rt_ ? "Scene view" : rt == &game_rt_ ? "Game view" : rt == &cam_preview_rt_ ? "Camera Preview" : "A view";
    stat(strprintf("%s on the CPU", view), gpu_slot_reason_[k]);
  }
}

/* A path tracer's card time: busy and wait inside its submissions, and the busy share of `wall_ms` (the whole render
 * for F12, the frame for the Camera Preview) - the card sits idle between submissions, which the submissions' own
 * wait never shows. */
void Editor::draw_profiler_gpu_tracer(const char *what, const PTStats &s, double wall_ms, const char *of_what, const std::function<void(const std::string &, const std::string &)> &stat) {
  const std::string key = strprintf("%s: GPU busy / wait in its submissions", what);
  if (s.gpu_busy_ms < 0.0) {
    stat(key, s.render_ms > 0.0 ? "n/a (the CPU, or no timestamps)" : "-");
    return;
  }
  stat(key, strprintf("%.0f ms / %.0f ms", s.gpu_busy_ms, s.gpu_wait_ms));
  const double cards = (double)std::max<size_t>(1, s.gpu_samples.size());  // (the busy is every card's, summed: a share per card)
  if (wall_ms > 0.0) stat(strprintf("%s: GPU busy over %s%s", what, of_what, cards > 1 ? " (a card's average)" : ""), strprintf("%.0f %% of %.0f ms", 100.0 * s.gpu_busy_ms / (wall_ms * cards), wall_ms));
}

void Editor::draw_profiler(const Recti &r) {
  auto &u = ui_;
  ui::Id sid = u.id("prof_scroll");
  static int content_h = 0;
  int off = u.begin_scroll(sid, r, content_h);
  ui::Layout lay{{r.x + u.px(10), r.y, r.w - u.px(30), r.h}, r.y + u.px(8) - off};
  lay.row_h = u.row_h();
  u.label(lay.row(), "CPU frame time (ms) - editor frame (blue) and scene render (orange)", u.theme.text_bright);
  Recti graph = lay.row(u.px(120));
  u.frame(graph, u.theme.canvas_bg, u.theme.border, u.px(3));
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
    if (u.record_buttons) u.button_rects[k] = row;  // (tests find a row by its name, as a button by its label)
    u.label({row.x, row.y, row.w / 2, row.h}, k, u.theme.text_dim);
    u.label({row.x + row.w / 2, row.y, row.w / 2, row.h}, v);
  };
  stat("Vertex stage", strprintf("%.2f ms", s.ms_vertex));
  stat("Clip / cull / bin", strprintf("%.2f ms", s.ms_setup));
  stat("  of it binning (CPU, all threads)", strprintf("%.2f ms", s.ms_bin));
  stat("Raster (tiles)", strprintf("%.2f ms", s.ms_raster));
  stat("Total 3D", strprintf("%.2f ms", s.ms_total));
  stat("Editor frame (incl. UI)", strprintf("%.2f ms", frame_ms_));
  stat("Triangles submitted / rasterized", strprintf("%zu / %zu", s.tris_submitted, s.tris_rasterized));
  stat("Objects submitted / frustum-culled", strprintf("%d / %d", s.objects_submitted, s.objects_culled));
  stat("Worker threads", strprintf("%d", JobSystem::global().thread_count()));
  stat("Scene memory (approx.)", format_bytes(scene_->memory_bytes()));
  stat("Undo steps", strprintf("%zu", undo_.size()));

  /* Where the last frame went, and what the render caches saved (ADR 0009). */
  lay.space(u.px(6));
  u.label(lay.row(), "Editor frame breakdown (last frame)", u.theme.text_bright);
  const FrameProfile &fp = prof_last_;
  stat("Scene view 3D (of it overlays)", strprintf("%.2f ms (%.2f ms)", fp.ms_scene3d, fp.ms_overlays));
  stat("Game view / Camera Preview", strprintf("%.2f ms / %.2f ms", fp.ms_game, fp.ms_preview));
  stat("Shadow map", fp.shadow_renders ? strprintf("%.2f ms (rendered)", fp.ms_shadow) : std::string("reused"));
  stat("UI / present", strprintf("%.2f ms / %.2f ms", fp.ms_ui, fp.ms_present));
  stat("Views rendered / reused", strprintf("%llu / %llu", (unsigned long long)fp.view_renders, (unsigned long long)fp.view_cache_hits));
  draw_profiler_gpu(fp, stat);
  /* Task 0083: what the frames are really apart (the rate the screen sees, cap and timer included), and the parts
   * an Edit Mode drag pays for. */
  if (frame_stamps_.size() >= 2) {
    std::vector<double> gaps;
    for (size_t k = 1; k < frame_stamps_.size(); k++) gaps.push_back((frame_stamps_[k] - frame_stamps_[k - 1]) * 1000.0);
    double mean = 0;
    for (double g : gaps) mean += g;
    mean /= (double)gaps.size();
    std::sort(gaps.begin(), gaps.end());
    const size_t p95 = std::min(gaps.size() - 1, (size_t)std::ceil(0.95 * (double)gaps.size()) - 1);  // (nearest rank)
    stat("Between frames (last second)", strprintf("%.1f ms on average, 95%% within %.1f ms", mean, gaps[p95]));
  }
  stat("Draw items / scene hashes", strprintf("%.2f ms / %.2f ms (%llu)", fp.ms_collect, fp.ms_hash, (unsigned long long)fp.scene_hashes));
  stat("Edit Mode: operator / mesh writes / overlap check", strprintf("%.2f ms / %.2f ms / %.2f ms", fp.ms_edit_op, fp.ms_edit_apply, fp.ms_overlap));
  stat("Lighting key / snapping", strprintf("%.2f ms (%llu) / %.2f ms", fp.ms_lighting_key, (unsigned long long)fp.lightmap_key_hashes, fp.ms_snap_pick));
  const MeshCounts &mc = mesh_last_;
  stat("Mesh copies / render meshes built / moved in place",
       strprintf("%llu (%.1f ms) / %llu (%.1f ms) / %llu (%.1f ms)", (unsigned long long)mc.copies, mc.ms_copies, (unsigned long long)mc.full_builds,
                 mc.ms_full_builds, (unsigned long long)mc.in_place, mc.ms_in_place));
  stat("Tangents / edge tables built", strprintf("%llu (%.1f ms) / %llu (%.1f ms)", (unsigned long long)mc.tangent_builds, mc.ms_tangents,
                                                   (unsigned long long)mc.edge_builds, mc.ms_edges));

  lay.space(u.px(6));
  draw_performance_settings(&lay);

  lay.space(u.px(6));
  u.label(lay.row(), "Renderer options (toggle to see the cost of each optimization)", u.theme.text_bright);
  u.toggle_row(lay.row(), "Multithreaded tiles (GEA Vol. I ch. 4)", raster_opt_.multithreaded);
  u.toggle_row(lay.row(), "Back-face culling (FoCG ch. 9.4)", raster_opt_.backface_culling);
  u.toggle_row(lay.row(), "Frustum culling (FoCG ch. 9.4 / GEA Vol. II 11.5)", raster_opt_.frustum_culling);
  u.toggle_row(lay.row(), "Uniform materials once per triangle", raster_opt_.uniform_shading);
  u.toggle_row(lay.row(), "8 pixels at a time (AVX2)", raster_opt_.simd_shading);
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
/* Render window (Blender: Render Result + Render/Output/World properties;
 * Unity: Lighting window + Recorder)                                     */
/* ===================================================================== */

/* Unity's Lighting window (Window > Rendering > Lighting): Generate Lighting, the baked data, and the
 * settings it bakes with (task 0012). */
void Editor::draw_lighting_window(const Recti &r) {
  auto &u = ui_;
  u.canvas.fill_rect(r, u.theme.pane);
  static int content_h = 0;
  int off = u.begin_scroll(u.id("lighting_window"), r, content_h);
  ui::Layout lay{{r.x + u.px(8), r.y, r.w - u.px(20), r.h}, r.y + u.px(8) - off};
  lay.row_h = u.row_h();
  auto header = [&](const char *t) {
    Recti h = lay.row(u.row_h() + u.px(4));
    u.canvas.fill_rect({r.x, h.y, r.w, h.h}, u.theme.header);
    u.label({h.x + u.px(4), h.y, h.w, h.h}, t, u.theme.text_bright);
  };
  Recti b = lay.row(u.row_h() + u.px(8));
  const int bw = (b.w - u.px(4)) / 2;
  if (baking_) {
    if (u.button({b.x, b.y, bw, b.h}, "Cancel", true, Icon::Pause)) bake_cancel();
  }
  else if (u.button({b.x, b.y, bw, b.h}, "Generate Lighting", false, Icon::Lightbulb))
    bake_start();
  u.tooltip("Bakes the Baked and Mixed lights, the sky and their bounces into lightmaps for objects marked\n"
            "Contribute GI (MeshRenderer). Saved next to the scene, in a folder with the scene's name.");
  if (u.button({b.x + bw + u.px(4), b.y, bw, b.h}, "Clear Baked Data", false, Icon::Close)) bake_clear();
  u.tooltip("Removes the lightmaps (to the trash) and goes back to realtime ambient light.");
  if (baking_) {
    Recti pr = lay.row(u.px(6));
    u.canvas.fill_round_rect(pr, u.px(3), u.theme.well);
    u.canvas.fill_round_rect({pr.x, pr.y, (int)(pr.w * baker_.progress()), pr.h}, u.px(3), u.theme.accent);
    u.redraw = true;
  }
  {
    /* Task 0020: Update Lighting and the rough preview. */
    Recti r2 = lay.row(u.row_h() + u.px(4));
    if (u.button({r2.x, r2.y, bw, r2.h}, lighting_update_requested_ ? "Updating..." : "Update Lighting  (Ctrl+Shift+L)", lighting_update_requested_,
                 Icon::Lightbulb))
      lighting_update();
    u.tooltip("Runs Realtime GI once now: realtime lightmaps and live probes catch up, then stay as they are\n"
              "(Lighting Updates: Manual). Generate Lighting makes the real bake.");
    const bool pv = scene_->lighting.preview;
    if (u.button({r2.x + bw + u.px(4), r2.y, bw, r2.h}, pv ? "Preview Lighting: On" : "Preview Lighting: Off", pv, Icon::Lightbulb))
      command(pv ? "lighting preview off" : "lighting preview on");
    u.tooltip("A rough bake while you edit, refining 32 -> 64 -> 256 samples half a second after changes stop;\n"
              "shown instead of the bake and never saved (like Bakery's Realtime Preview).");
    std::string mode = lighting_manual() ? "Lighting Updates: Manual (lighting stays as last computed until you update)"
                                         : "Lighting Updates: Automatic (Realtime GI follows every change)";
    if (pv) mode += preview_running_ ? strprintf("  -  Preview: baking %d samples (%.0f%%)", preview_stage_ <= 0 ? 32 : preview_stage_ == 1 ? 64 : 256,
                                                  preview_baker_.progress() * 100.0)
                                     : preview_samples_ ? strprintf("  -  Preview: %d samples", preview_samples_) : std::string("  -  Preview: waiting");
    u.label(lay.row(), mode, u.theme.text_dim);
  }
  if (!bake_status_.empty()) u.label(lay.row(), bake_status_, baking_ ? u.theme.accent : u.theme.text_dim);
  if (!baking_ && lighting_out_of_date())
    u.label(lay.row(), lighting_manual() ? "Lighting out of date: the last lighting stays up until Generate Lighting (moved objects keep their maps)."
                                         : "Lighting out of date: objects changed since the bake keep realtime ambient.",
            u.theme.warning);
  else if (!baking_ && frozen_out_of_date())
    u.label(lay.row(), "Realtime GI out of date: Update Lighting (Ctrl+Shift+L) brings it up to date.", u.theme.warning);
  {
    std::string why;
    const bool live = live_gi_needed(&why);
    if (live && scene_stats_.ms_gi > 0.0) why += strprintf("  -  %.1f ms a Scene view render", scene_stats_.ms_gi);
    if (live && !gi_gpu_name_.empty() && !gi_gpu_failed_) why += "  -  gathers on " + gi_gpu_name_;
    u.label(lay.row(), why, live ? u.theme.text : u.theme.text_dim);
    u.tooltip("Realtime GI only traces what the bake doesn't hold: no bake, objects or lights changed since it\n"
              "(Auto Generate re-bakes), or a Realtime Directional Light. Otherwise the views cost what a bake does.");
  }
  {
    size_t contributing = 0;
    scene_->for_each([&](GameObject &g) {
      if (auto *mr = g.get<MeshRenderer>())
        if (mr->enabled && mr->contribute_gi && g.active_in_hierarchy()) contributing++;
    });
    int side = lighting_data_.pages.empty() ? 0 : lighting_data_.pages[0].width;
    u.label(lay.row(), strprintf("Contribute GI: %zu objects   Lightmaps: %zu x %d px   Baked: %zu objects", contributing,
                                 lighting_data_.pages.size(), side, lighting_data_.entries.size()),
            u.theme.text_dim);
  }
  lay.space(u.px(6));
  header("Lightmapping Settings");
  {
    InspectorReflector ir(*this, u, lay);
    u.push_id("lighting");
    scene_->lighting.reflect(ir);
    u.pop_id();
    if (ir.changed) mark_changed("Lighting Settings");
    u.label(lay.row(), "Emissive meshes: sampled at every texel, like lights (task 0157)", u.theme.text_dim);
  }
  lay.space(u.px(6));
  header("Environment");
  {
    InspectorReflector ir(*this, u, lay);
    u.push_id("lighting_world");
    scene_->environment.reflect(ir);
    u.pop_id();
    if (ir.changed) {
      mark_changed("World Settings");
    }
  }
  lay.space(u.px(6));
  u.label(lay.row(), "Light Mode is on each Light; Contribute GI on each MeshRenderer.", u.theme.text_dim);
  lay.space(u.px(16));
  content_h = lay.y + off - r.y;
  u.end_scroll();
}

void Editor::draw_render_window(const Recti &r) {
  auto &u = ui_;
  int lw = std::min(u.px(340), r.w * 2 / 5);
  Recti left{r.x, r.y, lw, r.h};
  Recti right{r.x + lw + 1, r.y, r.w - lw - 1, r.h};
  u.canvas.fill_rect(left, u.theme.pane);
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
    final_pt_.pause();  // (task 0161: the cards stop too)
    resolve_final_render(true);
    render_status_ += "  (stopped)";
  }
  if (u.button({b.x + 3 * (bw + u.px(4)), b.y, bw, b.h}, "Save...", false, Icon::File)) defer([this] { save_render(); });
  u.tooltip("Saves to Renders/ in the chosen File Format.");
  {
    /* Camera sequence: a shot list rendered one camera after another. */
    const std::vector<GameObject *> cams = sequence_cameras();
    Recti sr = lay.row(u.row_h() + u.px(6));
    if (seq_.active) {
      if (u.button(sr, strprintf("Stop Camera Sequence (%zu / %zu)", std::min(seq_.index + 1, seq_.cams.size()), seq_.cams.size()), true, Icon::Pause))
        stop_render_sequence();
    }
    else if (u.button(sr, strprintf("Render Camera Sequence (%zu camera%s)", cams.size(), cams.size() == 1 ? "" : "s"), false, Icon::Camera))
      start_render_sequence();
    u.tooltip("Render the scene from every camera with Render in Sequence on (Camera Inspector), in Sequence Order,\n"
              "each saved to Renders/<scene>_sequence_<time>/NN_<camera>.<format> with these settings.");
    draw_render_animation_button(lay);
    if (!cams.empty() && !seq_.active) {
      std::string names;
      for (size_t k = 0; k < cams.size() && k < 6; k++) names += (k ? ", " : "") + cams[k]->name;
      if (cams.size() > 6) names += strprintf(" and %zu more", cams.size() - 6);
      u.label(lay.row(), "Order: " + names, u.theme.text_dim);
    }
  }
  if (!render_status_.empty()) {
    for (const std::string &part : {render_status_})
      u.label(lay.row(), part, rendering_ ? u.theme.accent : u.theme.text_dim);
  }
  if (rendering_) {
    Recti pr = lay.row(u.px(6));
    float t = std::min(1.0f, final_pt_.samples() / (float)std::max(1, scene_->render.samples));
    u.canvas.fill_round_rect(pr, u.px(3), u.theme.well);
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
    const bool gpu_mode = rs.device >= 1;
    auto device_row = [&](const std::string &title, const std::string &detail, bool on, bool enabled) {
      Recti row = lay.row(u.row_h() * 2);
      bool v = on;
      bool clicked = enabled && u.checkbox({row.x + u.px(4), row.y + u.px(2), u.row_h() - u.px(4), u.row_h() - u.px(4)}, v);
      u.label({row.x + u.row_h() + u.px(6), row.y, row.w - u.row_h() - u.px(6), u.row_h()}, title, enabled ? u.theme.text_bright : u.theme.text_dim);
      u.label({row.x + u.row_h() + u.px(6), row.y + u.row_h() - u.px(3), row.w - u.row_h() - u.px(6), u.row_h()}, detail, u.theme.text_dim);
      return clicked;
    };
    if (device_row("CPU", strprintf("%d threads, %s", JobSystem::global().thread_count(), PathTracer::embree_available() && rs.use_embree ? "Embree" : "Blendity BVH"),
                   !gpu_mode || rs.gpu_with_cpu || rs.device == 2, rs.device == 1)) {  // (Auto always uses it)
      rs.gpu_with_cpu = !rs.gpu_with_cpu;
      mark_changed("Render Devices");
    }
    u.tooltip(rs.device == 2 ? "GPU + CPU (Auto): the CPU always renders alongside the GPUs, each as fast as it can."
              : gpu_mode     ? "Tick to render on the CPU alongside the GPUs (combined rendering)."
                             : "Set Device to GPU Compute to use the GPUs.");
    for (const gpu::DeviceInfo &d : devs) {
      bool on = !render_devices_off_.count(d.name);
      std::string detail = strprintf("%s, %s, %llu MB, %s", d.vendor.c_str(), d.type.c_str(), (unsigned long long)d.memory_mb,
                                     d.hardware_rt ? (rs.hardware_rt ? "ray tracing hardware" : "ray tracing hardware (off)") : "compute (no RT hardware)");
      if (device_row(d.name, detail, on && gpu_mode, gpu_mode)) {
        if (on) render_devices_off_.insert(d.name);
        else render_devices_off_.erase(d.name);
        save_prefs();
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
    }
  }
  lay.space(u.px(6));
  GameObject *owner = nullptr;
  main_camera(*scene_, &owner);
  u.label(lay.row(), owner ? "Camera: " + owner->name : "No camera - rendering from the Scene view", u.theme.text_dim);
  u.label(lay.row(), "Tip: select a camera and switch its Camera Preview to Rendered to preview the path tracer live.", u.theme.text_dim);
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
    if (!render_transparent_)
      for (int x = dst.x; x < dst.right(); x++) drow[x] = src[std::min(iw - 1, (int)((x - ox) / sc))];
    else
      for (int x = dst.x; x < dst.right(); x++) {  // Film > Transparent: over a checkerboard, as Blender's Image Editor
        const uint32_t c = src[std::min(iw - 1, (int)((x - ox) / sc))], a = c >> 24;
        const uint32_t chk = (((x - ox) / u.px(8)) + ((y - oy) / u.px(8))) & 1 ? 0x99u : 0x66u;
        auto mix = [&](uint32_t v) { return (v * a + chk * (255 - a) + 127) / 255; };
        drow[x] = 0xFF000000u | mix(c >> 16 & 255) << 16 | mix(c >> 8 & 255) << 8 | mix(c & 255);
      }
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


/* Edit Mode's tools: selection mode, n-gon / knife / drawing, the mode's settings and
 * operators. Shared by the Inspector and the Modeling Tools window. */
void Editor::draw_edit_tools(ui::Layout &lay) {
  auto &u = ui_;
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
        if (u.button({nr.x + u.px(4), nr.y, nw * 2 + u.px(4), nr.h}, ngon_mode_ ? "N-gon Mode: On" : "N-gon Mode: Off", ngon_mode_, Icon::Face))
          ngon_mode_ = !ngon_mode_;
        u.tooltip("SketchUp-style faces: a flat region of faces acts as one face (inner edges hidden,\n"
                  "one click selects it all, Push/Pull moves it all).");
        if (u.button({nr.x + u.px(12) + 2 * nw, nr.y, nw, nr.h}, knife_.active ? "Knife (on)" : "Knife (K)", knife_.active))
          edit_tool("knife");
        u.tooltip("Split a face along a line between two points on its edges or corners.\nSketchUp: Line tool. Blender: Knife (K).");
      }
      {
        /* Drawing tools (UModeler / SketchUp): shapes drawn onto the mesh with snapping. */
        Recti dr = lay.row(u.row_h() + u.px(2));
        u.label({dr.x + u.px(4), dr.y, u.px(40), dr.h}, "Draw", u.theme.accent);
        /* Two rows of three, so the names fit. */
        const int per = 3, x0 = dr.x + u.px(44), dw = (dr.right() - x0 - u.px(4) - (per - 1) * u.px(3)) / per;
        for (int k = 0; k < kDrawShapeCount; k++) {
          if (k == per) dr = lay.row(u.row_h() + u.px(2));
          const bool on = draw_.active && draw_.shape == k;
          if (u.button({x0 + (k % per) * (dw + u.px(3)), dr.y, dw, dr.h}, kDrawShapes[k], on)) {
            if (on) draw_.active = false;
            else draw_begin(k);
          }
          u.tooltip("Draw onto the mesh (or the ground) with snapping to corners, midpoints, edges, faces and the grid.\n"
                    "A closed shape inside a face is cut into it, ready for Push/Pull; a line between edges splits the face;\n"
                    "anything else becomes new faces or wire edges. UModeler: drawing tools. SketchUp: Line, Rectangle, Circle, Arc, Polygon.");
        }
        if (draw_.active) {
          static const char *kAxes[] = {"Local (face / object)", "Global (world)"};
          int ax = draw_global_axes_ ? 1 : 0;
          er.enumeration("Snap Axes", ax, kAxes, 2);
          if (ax != (draw_global_axes_ ? 1 : 0)) {
            draw_global_axes_ = ax == 1;
          }
          u.tooltip("The red / green axes drawing snaps to (and rectangles line up with):\n"
                    "Local follows the face's edges or the object's rotation; Global uses the world's X / Y / Z.\n"
                    "Edges of the mesh (parallel / perpendicular) and guides snap either way.");
        }
        if (draw_.active) {
          er.field("Perpendicular Snap", draw_perp_snap_);
          u.tooltip("Snap onto an edge where the line from the last point meets it at 90 degrees (Plasticity).");
          {
            Recti gr = lay.row();
            u.label({gr.x + u.px(4), gr.y, gr.w - u.px(8), gr.h}, "Guides: X / Y / Z along that axis, Ctrl+click along an edge", u.theme.text_dim);
          }
          u.tooltip("Construction lines like Plasticity's: X, Y or Z lays one along that axis (Local or Global, as Snap Axes\n"
                    "says) through the last point, or through the point under the mouse before the first click. Ctrl+click\n"
                    "on an edge lays one along it. Drawing snaps to guides, where they cross and where they pierce the plane;\n"
                    "a polyline can follow one off the plane, which makes a 3D path for Follow.");
        }
        if (draw_.active && draw_.shape == 1) {
          er.enumeration("Rectangle From", draw_rect_mode_, kRectModes, 3);
          u.tooltip("Corner: two opposite corners. Center: the middle, then a corner (grows evenly both ways).\n"
                    "3 Points: one side at any angle, then the width (Plasticity's rectangle modes).");
          er.field("Square", draw_uniform_);
          u.tooltip("Keep the sides equal (Plasticity: hold for a square).");
        }
        if (draw_.active && (draw_.shape == 1 || draw_.shape == 2 || draw_.shape == 4)) {
          er.field("Start at Face Center", draw_face_center_);
          u.tooltip("Centre-based shapes (Center rectangles, circles and polygons from their centre) start at the exact\n"
                    "centre of the face you click, wherever on it you click (Plasticity). The centre also snaps (orange) when the\n"
                    "mouse is near it, with this off.");
        }
        if (draw_.active && (draw_.shape == 2 || draw_.shape == 4)) {
          er.enumeration("Circle From", draw_circle_mode_, kCircleModes, 3);
          u.tooltip("Center: the middle, then the radius. 2 Points: across its diameter. 3 Points: any three points on it\n"
                    "(Plasticity's circle modes).");
        }
        if (draw_.active && (draw_.shape == 2 || draw_.shape == 3)) er.field("Circle / Arc Segments", draw_segments_, 3, 256);
        if (draw_.active && draw_.shape == 4) er.field("Polygon Sides", draw_sides_, 3, 64);
        {
          /* Construction lines (Plasticity's lines / SketchUp's guides) to line drawings up with. */
          Recti gr = lay.row(u.row_h() + u.px(2));
          const int gw = (gr.w - u.px(12)) / 3;
          if (u.button({gr.x + u.px(4), gr.y, gw, gr.h}, "Guides from Edges")) guides_from_selected_edges();
          u.tooltip("A construction line along each selected edge, to draw against.");
          if (u.button({gr.x + u.px(6) + gw, gr.y, gw, gr.h}, show_guides_ ? "Hide Guides" : "Show Guides", show_guides_)) show_guides_ = !show_guides_;
          if (u.button({gr.x + u.px(8) + 2 * gw, gr.y, gw, gr.h}, strprintf("Clear Guides (%zu)", scene_->guides.size()))) {
            scene_->guides.clear();
            mark_changed("Clear Guides");
          }
        }
        if (draw_.active && draw_.shape != 2 && draw_.shape != 3 && draw_.shape != 5) {
          er.field("Corner Radius", draw_fillet_, 0.005f, 0.0f, 1000.0f);
          u.tooltip("Round the corners of rectangles, polygons and closed polylines (Plasticity: Fillet Curve). 0 keeps them sharp.");
        }
      }
      /* Only switches that change how tools behave live here; each tool's own numbers (thickness,
       * angle, distance, segments...) are in its helper - Adjust Last Operation (F9) at the bottom
       * left of the Scene view, or the drag helper - and are kept for next time. */
      if (elem_ == EditElement::Face) {
        er.field("Push/Pull Each Face", pp_individual_);
        u.tooltip("On: Push/Pull (P) moves every selected face along its own normal (a box's sides all out at once).\n"
                  "Off: each connected group of selected faces moves along its own normal; separate groups move together.");
        er.field("Inset Individual", inset_individual_);
        u.tooltip("On: each selected face is inset on its own. Off: the selection is inset as one face\n"
                  "(its outline moves in, edges between selected faces stay). Blender: Inset > Individual.");
        er.field("Auto Fuse on Contact", auto_fuse_);
        u.tooltip("Faces extruded or moved onto another face of the mesh merge into it\n(the touching area becomes an opening): bridging by extrusion.");
      }
      else if (elem_ == EditElement::Edge) {
        er.field("Auto Smooth", auto_smooth_);
        u.tooltip("Rounded results (bevels with 2+ segments, pulled circles...) are shaded smooth, with edges sharper than\n"
                  "the angle kept hard (Blender: Shade Auto Smooth). Only on meshes not shaded per face.");
        if (auto_smooth_) er.field("Auto Smooth Angle", auto_smooth_angle_, 0.5f, 1.0f, 180.0f);
        er.field("Sharp Angle", seam_angle_, 0.5f, 0.0f, 180.0f);
        u.tooltip("Seams from Sharp and Select Sharp Edges use edges whose faces meet at more than this (Blender: 30).");
      }
      {
        Recti nr = lay.row();
        u.label({nr.x + u.px(4), nr.y, nr.w - u.px(8), nr.h}, "Each tool's settings are in its helper (F9) when you use it.", u.theme.text_dim);
        u.tooltip("Thickness, angle, distance, segments, count and the like show in Adjust Last Operation (F9, bottom left\n"
                  "of the Scene view) after a tool runs, and in the drag helper while Bevel, Inset, Array, Grid, Pipe, Taper\n"
                  "or Recess is dragged. Changing them re-runs the tool, and they are kept for its next use.");
      }
      {
        /* The tools in groups, Select first (one place for every way of selecting); each
         * group shows what this selection mode can use. Mark / Clear and similar pairs
         * share one cell. */
        std::vector<std::vector<const EditOpInfo *>> groups(kEditGroupCount);
        for (const EditOpInfo &op : edit_op_table()) {
          if (std::string(op.op) == "extrude_individual") continue;  // Extrude's Individual switch
          if (!edit_op_available(op.op)) continue;
          groups[(size_t)edit_op_group(op.op)].push_back(&op);
        }
        for (int gi = 0; gi < kEditGroupCount; gi++) {
          auto &list = groups[(size_t)gi];
          if (list.empty() && gi != 0) continue;
          Recti hr = lay.row(u.row_h() + u.px(2));
          bool &open = foldouts_.emplace(std::string("edit_grp_") + kEditGroups[gi], true).first->second;
          const int ah = u.font.line_height() - u.px(4);
          u.draw_icon(open ? Icon::ArrowDown : Icon::ArrowRight, {hr.x + u.px(2), hr.y + (hr.h - ah) / 2, ah, ah}, u.theme.text_dim);
          u.label({hr.x + ah + u.px(6), hr.y, hr.w, hr.h}, kEditGroups[gi], u.theme.accent);
          if (u.hovered(hr) && u.in.pressed[0]) {
            open = !open;
            u.consume_click();
          }
          if (!open) continue;
          /* Cells: single operators, or a pair as two half buttons. */
          struct Cell {
            const EditOpInfo *a = nullptr, *b = nullptr;
            const EditOpPair *pair = nullptr;
          };
          std::vector<Cell> cells;
          std::vector<const EditOpInfo *> done;
          if (gi == 0) cells.push_back(Cell{});  // All / None
          for (const EditOpInfo *o : list) {
            if (std::find(done.begin(), done.end(), o) != done.end()) continue;
            Cell c;
            c.a = o;
            for (const EditOpPair &p : edit_op_pairs())
              if (std::string(p.a) == o->op || std::string(p.b) == o->op) {
                const EditOpInfo *other = nullptr;
                for (const EditOpInfo *q : list)
                  if (q != o && (std::string(q->op) == p.a || std::string(q->op) == p.b)) other = q;
                if (other) {
                  c.pair = &p;
                  c.a = std::string(o->op) == p.a ? o : other;
                  c.b = std::string(o->op) == p.a ? other : o;
                  done.push_back(other);
                }
              }
            done.push_back(o);
            cells.push_back(c);
          }
          for (size_t i = 0; i < cells.size(); i += 3) {
            Recti row = lay.row(u.row_h() + u.px(2));
            const int bw3 = (row.w - u.px(16)) / 3;
            for (size_t k = i; k < std::min(cells.size(), i + 3); k++) {
              const Cell &c = cells[k];
              const int x = row.x + u.px(4) + (int)(k - i) * (bw3 + u.px(4));
              if (!c.a) {  // Select All / None
                const int hw = (bw3 - u.px(2)) / 2;
                if (u.button({x, row.y, hw, row.h}, "All")) edit_select_all(true);
                u.tooltip("Select everything (Ctrl+A).");
                if (u.button({x + hw + u.px(2), row.y, bw3 - hw - u.px(2), row.h}, "None")) edit_select_all(false);
                u.tooltip("Deselect everything (Ctrl+Shift+A).");
                continue;
              }
              if (c.pair) {
                const int lw = u.font.text_width(c.pair->label) + u.px(8);
                u.label({x, row.y, lw, row.h}, c.pair->label, u.theme.text_dim);
                const int hw = (bw3 - lw - u.px(2)) / 2;
                if (u.button({x + lw, row.y, hw, row.h}, c.pair->label_a)) edit_tool(c.a->op);
                u.tooltip(c.a->keys[0] ? strprintf("%s (%s)", c.a->tip, c.a->keys) : std::string(c.a->tip));
                if (u.button({x + lw + hw + u.px(2), row.y, bw3 - lw - hw - u.px(2), row.h}, c.pair->label_b)) edit_tool(c.b->op);
                u.tooltip(c.b->keys[0] ? strprintf("%s (%s)", c.b->tip, c.b->keys) : std::string(c.b->tip));
                continue;
              }
              std::string label = c.a->label;
              if (gi == 0 && starts_with(label, "Select ")) label = label.substr(7);  // in the Select group the word is implied
              if (u.button({x, row.y, bw3, row.h}, label)) edit_tool(c.a->op);
              u.tooltip(c.a->keys[0] ? strprintf("%s (%s)", c.a->tip, c.a->keys) : std::string(c.a->tip));
            }
          }
          if (gi == 1 && elem_ == EditElement::Face) {
            Recti tr = lay.row(u.row_h() + u.px(2));
            if (u.button({tr.x + u.px(4), tr.y, tr.w - u.px(8), tr.h}, extrude_individual_ ? "Extrude: Individual Faces" : "Extrude: Region",
                         extrude_individual_))
              extrude_individual_ = !extrude_individual_;
            u.tooltip("Region: the selected faces extrude together (shared sides stay inside).\n"
                      "Individual: each face on its own, with its own walls. Blender: Extrude Individual Faces.");
          }
        }
      }
      static const char *kAxesXYZ[] = {"X", "Y", "Z", "View"};
      if (elem_ != EditElement::Face) {
        er.field("Spin Angle", spin_angle_, 1.0f, -3600.0f, 3600.0f);
        er.field("Spin Steps", spin_steps_, 1, 512);
        er.enumeration("Spin Axis", spin_axis_, kAxesXYZ, 3);
        u.tooltip("Spin / Lathe turns round this axis of the object, through its origin.");
      }
      er.enumeration("Slice Axis", slice_axis_, kAxesXYZ, 4);
      static const char *kClear[] = {"Keep Both Sides", "Remove Above", "Remove Below"};
      er.enumeration("Slice Keeps", slice_clear_, kClear, 3);
      er.field("Proportional Editing", proportional_);
      if (proportional_) {
        er.field("Proportional Radius", prop_radius_, 0.01f, 0.001f, 10000.0f);
        static const char *kFalloff[] = {"Smooth", "Sphere", "Root", "Sharp", "Linear", "Constant"};
        er.enumeration("Falloff", prop_falloff_, kFalloff, 6);
      }
      if (elem_ == EditElement::Face) {  // materials go on faces
        /* The material itself (Unity's object picker): the selected faces' material, click for every material. */
        Recti rm = lay.row(u.row_h() + u.px(2));
        u.label({rm.x + u.px(4), rm.y, er.label_w - u.px(8), rm.h}, "Face Material");
        Recti fr{rm.x + er.label_w, rm.y, rm.w - er.label_w - u.px(4), rm.h};
        MaterialPtr cur;
        bool any_face = false;
        {
          GameObject *eo = edit_object();
          const Mesh &em = **edit_mesh_ptr();
          auto *mr = eo ? eo->get<MeshRenderer>() : nullptr;
          for (size_t f = 0; f < face_sel_.size() && f < em.face_count() && !cur; f++)
            if (face_sel_[f] && mr) {
              any_face = true;
              const int s = em.material_of(f);
              if (s >= 0 && s < (int)mr->materials.size()) cur = mr->materials[(size_t)s];
              if (!cur) break;
            }
        }
        const bool hot = u.hovered(fr);
        u.frame(fr, u.theme.field, hot ? u.theme.field_hover : u.theme.field_border, u.px(3));
        const int sw = u.font.line_height() - u.px(4);
        const Vec3 c = cur ? cur->base_color : Vec3(0.8f);
        u.canvas.fill_circle((float)(fr.x + u.px(4) + sw / 2), (float)(fr.y + fr.h / 2), sw * 0.5f, Color::from(Vec3(linear_to_srgb(c.x), linear_to_srgb(c.y), linear_to_srgb(c.z))));
        u.label({fr.x + sw + u.px(10), fr.y, fr.w - sw - u.px(14), fr.h}, cur ? cur->name : any_face ? "None (Default-Material)" : "Select faces, then pick a material");
        const ui::Id pid = u.id("face_mat_pick");
        if (hot && u.in.pressed[0]) {
          material_pick_search_.clear();
          u.open_popup(pid, fr);
          u.consume_click();
        }
        u.tooltip("Pick the material itself for the selected faces: its slot is found or added for you.\n"
                  "You can also drag a material from the Materials or Project window onto a face.");
        material_picker_popup(pid, edit_object(), [this](const MaterialPtr &m) { assign_material_to_selected_faces(m); });
        Recti r3 = lay.row(u.row_h() + u.px(2));
        u.label({r3.x + u.px(4), r3.y, er.label_w - u.px(8), r3.h}, "Material Slot");
        u.int_field(u.id("assign_slot"), {r3.x + er.label_w, r3.y, u.px(60), r3.h}, assign_slot_, 0, 63);
        if (u.button({r3.x + er.label_w + u.px(66), r3.y, r3.w - er.label_w - u.px(70), r3.h}, "Assign to Faces")) assign_material_to_faces(assign_slot_);
        u.tooltip("Blender: Material Properties > Assign. Unity: sub-mesh per material.");
      }
}

/* A material's fields anywhere (the Materials window, a .mat's inspector). */
bool Editor::draw_texture_fields(ui::Layout &lay, const ProceduralTexturePtr &t) {
  if (!t) return false;
  const std::string before = save_texture_text(*t);  // (small: the undo step keeps it if a field changes)
  const float size_before = t->physical_size;
  InspectorReflector ir(*this, ui_, lay);
  ir.edited_texture = t.get();
  t->reflect(ir);
  if (ir.changed) {
    if (t->physical_size != size_before) retile_texture_users(t->asset_path, size_before, t->physical_size);  // (task 0072)
    bool noted = false;
    for (const auto &kv : texture_before_) noted = noted || kv.first == t->asset_path;
    if (!noted && !t->asset_path.empty()) texture_before_.push_back({t->asset_path, before});
    if (ui_.in.down[0]) {
      /* A drag: the objects using the texture follow every frame (task 0031), its images evaluated at 256 at
       * most until the release brings back the set size. */
      procedural_texture_set_preview(t->asset_path, true);
      t->touch();
      texture_touch_pending_ = true;
    }
    else t->touch();
    mark_changed("Edit Texture " + t->name);
  }
  return ir.changed;
}

void Editor::draw_material_fields(ui::Layout &lay, const MaterialPtr &m, const std::vector<MaterialPtr> *others) {
  if (!m) return;
  auto &u = ui_;
  const bool many = others && others->size() > 1;
  std::set<std::string> mixed;
  if (many) {
    mixed = mixed_material_fields(m.get(), *others);
    u.label(lay.row(), strprintf("Editing %zu materials: a change reaches the ones that have the field", others->size()), u.theme.accent);
    u.tooltip("Unity's multi-object editing for materials (task 0147): a toon field reaches the toon materials among them, an\n"
              "axis field the axis ones, Shading Model all of them. A dash: their values differ; what you type lands on all.\n"
              "+=0.1 or *=2 changes each from its own value. Console: matset <Field> <value>.");
  }
  InspectorReflector ir(*this, ui_, lay);
  ir.material = m.get();
  if (many) ir.mixed = &mixed;
  m->reflect(ir);
  if (ir.changed) {
    m->touch();
    const size_t n = many ? apply_material_edits(ir.edits, m.get(), *others) : 0;
    mark_changed(n ? strprintf("Edit %zu Materials", n + 1) : "Edit Material " + m->name);  // an asset is written back when the edit finishes
  }
}

/* The edits made on `source`, applied to every other target that shows the field: Material::reflect's own
 * conditions (a toon field only on a Toon material, the axis fields only with an Axis Mode) decide what applies.
 * A typed expression is evaluated from each target's own value (index / count over the targets). */
size_t Editor::apply_material_edits(const std::vector<FieldEdit> &edits, const Material *source, const std::vector<MaterialPtr> &targets) {
  size_t n = 0;
  for (size_t i = 0; i < targets.size(); i++) {
    const MaterialPtr &t = targets[i];
    if (!t || t.get() == source) continue;
    bool took = false;
    for (const FieldEdit &fe : edits) {
      if (fe.kind == FieldEdit::Text && fe.name == "Name") continue;  // a name is the material's own (its file's): renaming all would collide
      ApplyEditReflector a(fe, (int)i, (int)targets.size(), false);
      t->reflect(a);
      if (a.applied && fe.kind == FieldEdit::Enum && fe.name == "Toon Style") apply_toon_style(*t, fe.i_new);  // the preset's fields on each (task 0148)
      took = took || a.applied;
    }
    if (!took) continue;
    t->touch();
    n++;
  }
  return n;
}

/* Every shown field as text, by name: the materials are compared through it. */
namespace {
struct ValuesReflector : Reflector {
  std::map<std::string, std::string> values;
  void field(const char *n, float &v, float, float, float) override { values[n] = strprintf("%g", v); }
  void field(const char *n, int &v, int, int) override { values[n] = std::to_string(v); }
  void field(const char *n, bool &v) override { values[n] = v ? "1" : "0"; }
  void field(const char *n, Vec3 &v) override { values[n] = strprintf("%g %g %g", v.x, v.y, v.z); }
  void color(const char *n, Vec3 &v) override { values[n] = strprintf("%g %g %g", v.x, v.y, v.z); }
  void enumeration(const char *n, int &v, const char *const *, int) override { values[n] = std::to_string(v); }
  void text(const char *n, std::string &v) override { values[n] = v; }
  void mesh(const char *, MeshPtr &) override {}
  void texture(const char *n, TextureRef &t) override { values[n] = t.path; }
};
}  // namespace

std::set<std::string> Editor::mixed_material_fields(const Material *source, const std::vector<MaterialPtr> &targets) {
  std::set<std::string> mixed;
  if (!source) return mixed;
  /* Once per change: the same materials at the same versions give the cached answer (a Ctrl+A over hundreds of
   * imported materials would otherwise reflect them all every frame). */
  uint64_t key = (uint64_t)(uintptr_t)source * 1099511628211ull + source->version;
  for (const MaterialPtr &t : targets) key = (key ^ (uint64_t)(uintptr_t)t.get()) * 1099511628211ull + (t ? t->version : 0);
  if (key == mixed_cache_.key && mixed_cache_.key) return mixed_cache_.fields;
  ValuesReflector mine;
  const_cast<Material *>(source)->reflect(mine);  // (reflect isn't const; ValuesReflector only reads)
  for (const MaterialPtr &t : targets) {
    if (!t || t.get() == source) continue;
    ValuesReflector theirs;
    t->reflect(theirs);
    for (const auto &kv : mine.values) {
      auto it = theirs.values.find(kv.first);
      if (it != theirs.values.end() && it->second != kv.second) mixed.insert(kv.first);
    }
  }
  mixed_cache_.key = key;
  mixed_cache_.fields = mixed;
  return mixed;
}

/* The editor's frame rate cap, with what it costs measured on this machine.
 * Blendity draws on the CPU, so frames are not free: the notes say what each
 * setting trades (smoothness, CPU and battery, path-tracing samples). */
void Editor::draw_performance_settings(ui::Layout *lay) {
  auto &u = ui_;
  auto row = [&](int h = -1) {
    if (lay) return lay->row(h);
    Recti r = u.popup_row(h);
    return Recti{r.x + u.px(12), r.y, r.w - u.px(24), r.h};
  };
  auto text = [&](const std::string &t, uint32_t c) {
    /* Word-wrapped to the row's width (continuation lines indented under a "- " bullet). */
    Recti r = row();
    const bool bullet = t.size() > 2 && t[0] == '-' && t[1] == ' ';
    const int indent = bullet ? u.font.text_width("- ") : 0;
    std::string line, word;
    bool first = true;
    auto flush = [&] {
      u.label({r.x + (first ? 0 : indent), r.y, r.w - (first ? 0 : indent), r.h}, line, c);
      first = false;
      line.clear();
    };
    for (size_t i = 0; i <= t.size(); i++) {
      if (i < t.size() && t[i] != ' ') {
        word += t[i];
        continue;
      }
      const std::string next = line.empty() ? word : line + " " + word;
      if (!line.empty() && u.font.text_width(next) > r.w - (first ? 0 : indent)) {
        flush();
        r = row();
        line = word;
      }
      else line = next;
      word.clear();
    }
    if (!line.empty()) flush();
  };
  /* Quality tier, viewport scale and CPU threads (task 0050): what the minimum machine (4 cores / 8 threads, a GTX
   * 1050 Ti, 8 GB) needs, one choice away. */
  {
    Recti qh = row(u.row_h() + u.px(4));
    u.label(qh, "Quality", u.theme.text_bright);
    static const char *kTiers[] = {"Low (4 cores, GTX 1050 Ti)", "Medium", "High", "Custom"};
    int ti = std::max(0, std::min(3, quality_tier_ < 0 ? (int)TierCustom : quality_tier_));
    Recti q1 = row(u.row_h() + u.px(4));
    u.label({q1.x, q1.y, u.px(150), q1.h}, "Quality tier");
    if (u.combo(u.id("pref_tier"), {q1.x + u.px(150), q1.y + u.px(2), u.px(220), q1.h - u.px(4)}, ti, kTiers, 4)) set_quality_tier(ti);
    u.tooltip("Low: the Scene and Game views drawn at 67 % and scaled up, a 1024 shadow map, lighter Realtime GI, 60 fps.\n"
              "Medium: views at 85 %, a 2048 shadow map. High: no limits. Renders (F12) always use the scene's own settings.\n"
              "Recommended here: " + std::string(recommended_tier() == TierLow ? "Low" : "High") + ".");
    Recti q2 = row(u.row_h() + u.px(4));
    u.label({q2.x, q2.y, u.px(150), q2.h}, "Viewport resolution");
    static const char *kScales[] = {"50 %", "67 %", "75 %", "85 %", "100 %"};
    static const float kScaleValues[] = {0.5f, 0.67f, 0.75f, 0.85f, 1.0f};
    int si = 4;
    for (int k = 0; k < 5; k++)
      if (std::fabs(kScaleValues[k] - view_scale_) < 0.01f) si = k;
    if (u.combo(u.id("pref_vscale"), {q2.x + u.px(150), q2.y + u.px(2), u.px(120), q2.h - u.px(4)}, si, kScales, 5)) {
      set_view_scale(kScaleValues[si]);
      quality_tier_ = TierCustom;
    }
    u.tooltip("The Scene and Game views draw their 3D this much smaller and scale it up: 75 % draws about half the pixels.\n"
              "Lines, gizmos and text stay sharp, and clicks still pick exactly.");
    Recti q3 = row(u.row_h() + u.px(4));
    u.label({q3.x, q3.y, u.px(150), q3.h}, "CPU threads");
    std::vector<std::string> opts{"All (" + std::to_string(sys::hardware_threads()) + ")"};
    std::vector<int> vals{0};
    for (int n : {2, 4, 6, 8, 12, 16, 24, 32})
      if (n < sys::hardware_threads()) opts.push_back(std::to_string(n)), vals.push_back(n);
    std::vector<const char *> cs;
    for (const std::string &o : opts) cs.push_back(o.c_str());
    int ci0 = 0;
    for (size_t k = 0; k < vals.size(); k++)
      if (vals[k] == cpu_threads_) ci0 = (int)k;
    if (u.combo(u.id("pref_threads"), {q3.x + u.px(150), q3.y + u.px(2), u.px(120), q3.h - u.px(4)}, ci0, cs.data(), (int)cs.size()))
      set_cpu_threads(vals[(size_t)ci0]);
    u.tooltip("How many threads Blendity works on (rendering, modeling, baking). Fewer leaves room for other programs.");
  }
  Recti head = row(u.row_h() + u.px(4));
  u.label(head, "Editor frame rate", u.theme.text_bright);
  static const char *kCaps[] = {"30 fps", "60 fps", "90 fps", "120 fps", "144 fps", "165 fps", "240 fps", "Unlimited"};
  static const int kCapValues[] = {30, 60, 90, 120, 144, 165, 240, 0};
  int ci = 3;
  for (int k = 0; k < 8; k++)
    if (kCapValues[k] == max_fps_) ci = k;
  Recti r1 = row(u.row_h() + u.px(4));
  u.label({r1.x, r1.y, u.px(150), r1.h}, "Frame rate cap");
  if (u.combo(u.id("pref_fps"), {r1.x + u.px(150), r1.y + u.px(2), u.px(120), r1.h - u.px(4)}, ci, kCaps, 8)) max_fps_ = kCapValues[ci];
  u.tooltip("The most frames per second the editor draws (Unity: Preferences > General > Interaction Mode).");
  static const char *kRedraw[] = {"When something changes", "Always"};
  int ri = always_redraw_ ? 1 : 0;
  Recti r2 = row(u.row_h() + u.px(4));
  u.label({r2.x, r2.y, u.px(150), r2.h}, "Redraw");
  if (u.combo(u.id("pref_redraw"), {r2.x + u.px(150), r2.y + u.px(2), u.px(200), r2.h - u.px(4)}, ri, kRedraw, 2)) always_redraw_ = ri == 1;
  u.tooltip("When something changes: idle costs nothing (the default). Always: a steady rate at the cap even when idle.");
  /* Task 0096: where the views' 3D is drawn. */
  static const char *kDevices[] = {"Auto (GPU when there is one)", "CPU", "GPU"};
  int di = viewport_device_;
  Recti r3 = row(u.row_h() + u.px(4));
  u.label({r3.x, r3.y, u.px(150), r3.h}, "Viewport Device");
  if (u.combo(u.id("pref_viewdev"), {r3.x + u.px(150), r3.y + u.px(2), u.px(220), r3.h - u.px(4)}, di, kDevices, 3)) {
    set_viewport_device(di);
    save_prefs();
  }
  u.tooltip("Where the Scene view, Game view and Camera Preview draw their 3D. The GPU is many times faster; anything it doesn't draw\n"
            "yet (GI and probes, axis or see-through materials, camera filters...) draws on the CPU, as before. F12 renders stay on the CPU.");
  text("Now: " + gpu_view_status(), u.theme.text_dim);
  /* Task 0103: the other views during a drag. */
  if (u.toggle_row(row(u.row_h()), "Other Views Follow Drags", other_views_follow_)) save_prefs();
  u.tooltip("Off (the default): while you drag in the Scene view, the Game view and the Camera Preview keep their last picture and\n"
            "draw once when you let go, so the drag itself stays fast (Blender's sculpt mode does the same). On: they follow\n"
            "every frame of the drag. Play mode always draws every frame.");
  /* Task 0104: cheaper CPU frames while moving. */
  if (u.toggle_row(row(u.row_h()), "Simpler While Moving", simpler_while_moving_)) save_prefs();
  u.tooltip("On (the default): while you drag or orbit, the views drawn on the CPU render the sun's shadow map at half size with a\n"
            "softer 2 x 2 filter, and draw exactly again the moment you stop (Blender lowers its viewport's quality while you\n"
            "navigate the same way). Off: every frame exact.");
  /* Measured: the last 60 frames' cost. */
  float avg = 0, worst = 0;
  const size_t n = std::min<size_t>(60, frame_history_.size());
  for (size_t k = frame_history_.size() - n; k < frame_history_.size(); k++) {
    avg += frame_history_[k];
    worst = std::max(worst, frame_history_[k]);
  }
  avg = n ? avg / (float)n : 0.0f;
  const float reachable = avg > 0.01f ? 1000.0f / avg : 0.0f;
  text(strprintf("Now: %.0f fps; a frame costs %.1f ms on average (worst %.1f ms) at %d x %d", measured_fps_, avg, worst, fb_.width, fb_.height),
       u.theme.text);
  const float budget = max_fps_ > 0 ? 1000.0f / (float)max_fps_ : 0.0f;
  if (max_fps_ > 0 && avg > budget)
    text(strprintf("Over budget: %d fps leaves %.1f ms a frame, this machine needs ~%.1f ms here, so it tops out near %.0f fps.", max_fps_, budget, avg,
                   reachable),
         u.theme.warning);
  else if (max_fps_ > 0 && avg > 0.01f)
    text(strprintf("Within budget: %d fps leaves %.1f ms a frame; this machine could draw about %.0f fps here.", max_fps_, budget, reachable),
         u.theme.text_dim);
  const uint32_t dim = u.theme.text_dim;
  text("What it costs:", u.theme.text);
  text(gpu_view_ ? "- The views' 3D is drawn on the GPU; the UI, overlays and picking on the CPU."
                 : "- Every frame is drawn on the CPU, so a bigger window, more triangles or Shaded views cost more per frame.",
       dim);
  if (max_fps_ == 0)
    text("- Unlimited: a CPU core runs flat out while anything moves; fans and battery suffer, and frames beyond the monitor's refresh rate are never seen.", dim);
  else if (max_fps_ > 144)
    text("- Above 144 fps: smoother only on a high-refresh monitor; elsewhere it is CPU and battery for frames you can't see.", dim);
  else if (max_fps_ <= 30)
    text("- 30 fps: about half the CPU of 60 while you drag, but the mouse feels up to 33 ms behind and orbiting looks steppy.", dim);
  else
    text(strprintf("- %d fps: dragging and orbiting draw up to %d frames a second; idle costs nothing unless Redraw is Always.", max_fps_, max_fps_), dim);
  text("- A Rendered Camera Preview: editor frames share the CPU with sampling; a lower cap leaves more for samples.", dim);
  if (always_redraw_) text("- Always redraw: the cap's CPU cost continues while idle (handy with the Profiler; otherwise wasted power).", u.theme.warning);
}

namespace {

/* Whether `g` is of the field's kind: has a component of that type name (none: any object). */
bool has_type(const GameObject &g, const char *type) {
  if (!type) return true;
  for (const auto &c : g.components)
    if (!std::strcmp(c->type_name(), type)) return true;
  return false;
}

/* What the field says it holds: "Transform" for any object (Unity's), "Mesh" for a MeshFilter (plainer than Unity's
 * "Mesh Filter"), else the component's name. */
std::string type_label(const char *type) {
  if (!type) return "Transform";
  if (!std::strcmp(type, "MeshFilter")) return "Mesh";
  return type;
}

/* The kind's icon at the field's left. */
Icon type_icon(const char *type) {
  if (!type) return Icon::Transform;
  if (!std::strcmp(type, "Camera")) return Icon::Camera;
  if (!std::strcmp(type, "Light")) return Icon::Light;
  if (!std::strcmp(type, "Armature") || !std::strcmp(type, "Bone")) return Icon::Move;
  return Icon::Cube;
}

}  // namespace

/* Unity's object picker button: a small target (a ring and a dot) at a field's right. */
bool Editor::picker_button(const Recti &r) {
  auto &u = ui_;
  const bool hit = u.button(r, "");
  const float cx = (float)r.x + (float)r.w * 0.5f, cy = (float)r.y + (float)r.h * 0.5f, rad = (float)std::min(r.w, r.h) * 0.3f;
  const uint32_t col = u.hovered(r) ? u.theme.text_bright : u.theme.text;
  u.canvas.fill_circle(cx, cy, rad, col);
  u.canvas.fill_circle(cx, cy, rad - (float)u.px(1) - 0.5f, u.theme.button);
  u.canvas.fill_circle(cx, cy, rad * 0.35f, col);
  return hit;
}

/* The field's face (Unity's ObjectField): a box with what it holds ("Robot (Armature)", "None (Armature)"; yellow when
 * missing or of the wrong kind). A click, or typing, edits the name as text. True when the text changed. */
bool Editor::object_field_box(ui::Id id, const Recti &box, std::string &v, const std::string &shown, const std::string &none, bool bad,
                              Icon icon) {
  auto &u = ui_;
  if (u.editing(id) || (u.hovered(box) && u.in.pressed[0])) return u.text_field(id, box, v, nullptr, none.c_str());
  u.frame(box, u.theme.field, u.theme.field_border, u.px(3));
  const uint32_t col = v.empty() ? u.theme.text_dim : bad ? u.theme.warning : u.theme.text;
  const int is = box.h - u.px(6);
  u.draw_icon(icon, {box.x + u.px(4), box.y + u.px(3), is, is}, col);
  const std::string text = v.empty() ? none : shown;
  u.label({box.x + is + u.px(8), box.y, box.w - is - u.px(10), box.h}, text, col);
  if (u.record_buttons) u.button_rects["shown:" + text] = box;  // (tests read what a field says)
  return false;
}

bool Editor::object_field_picked(ui::Id id, std::string &v) {
  if (field_pick_.field != id || field_pick_.age > 1) return false;
  field_pick_.field = 0;
  if (v == field_pick_.value) return false;
  v = field_pick_.value;
  return true;
}

/* An object field (Unity's ObjectField): a name typed in, an object of its kind dropped on it from the Hierarchy (one
 * of another kind is refused: the field turns red), or one chosen with the picker at its right. True when the name
 * changed. `name` keys it among the fields drawn this frame. */
bool Editor::object_field(const char *name, const Recti &f, std::string &v, const char *type) {
  auto &u = ui_;
  const ui::Id id = u.id(name);
  if (u.record_buttons) u.button_rects[std::string("field:") + name] = f;  // (tests drop onto a field by its name)
  const std::string label = type_label(type);
  const Recti box{f.x, f.y, f.w - f.h, f.h}, pick{box.right(), f.y, f.h, f.h};
  const GameObject *held = v.empty() ? nullptr : scene_->find_by_name(v);
  bool changed = object_field_picked(id, v);
  /* The picker: None and the scene's objects of the kind. */
  const ui::Id pid = u.id(std::string("pick:") + name);
  if (picker_button(pick)) u.open_popup(pid, {pick.x, pick.bottom(), 0, 0});
  u.tooltip("Select " + label + ": the scene's objects that can go here");
  u.popup(pid, u.px(220), [this, id, type] {
    if (ui_.menu_item("None")) field_pick_ = {id, ""};
    scene_->for_each_ordered([&](GameObject &g, int) {
      if (has_type(g, type) && ui_.menu_item(g.name)) field_pick_ = {id, g.name};
    });
  });
  changed = object_field_box(id, box, v, v + " (" + label + ")", "None (" + label + ")", !held || !has_type(*held, type), type_icon(type)) || changed;
  u.mark_widget(box);  // (the field's own help tooltip goes on the box)
  const GameObject *g = hierarchy_drag_object();
  if (!g || !u.hovered(f)) return changed;
  const bool fits = has_type(*g, type);
  u.canvas.rect_outline(f, fits ? u.theme.focus : u.theme.error, u.px(2));
  if (!u.in.released[0]) return changed;
  object_field_took_drop();  // (the Hierarchy then leaves the selection as it was)
  if (u.editing(id)) u.stop_editing();  // (the edit buffer would write the old text back)
  if (!fits) {
    Log::warn("'%s' isn't a %s: the %s field takes an object with a %s component", g->name.c_str(), label.c_str(), name, type);
    return changed;
  }
  if (scene_->find_by_name(g->name) != g) Log::warn("Two objects are named '%s': the field finds the other one first. Rename one of them.", g->name.c_str());
  if (v == g->name) return changed;
  v = g->name;
  return true;
}

/* Whether an asset field's file is there, looked at once a second (not a disk check every frame). */
bool Editor::asset_file_exists(const std::string &path) {
  auto it = asset_exists_cache_.find(path);
  if (it != asset_exists_cache_.end() && now_seconds() - it->second.second < 1.0) return it->second.first;
  const bool there = fs::exists(resolve_asset_path(path));
  if (asset_exists_cache_.size() > 256) asset_exists_cache_.clear();
  asset_exists_cache_[path] = {there, now_seconds()};
  return there;
}

/* The project's files of one kind (".anim"), project-relative with '/' (the asset fields' pickers). */
std::vector<std::string> Editor::asset_paths_with_ext(const std::string &ext) {
  std::vector<std::string> out, dirs{assets_dir_};
  while (!dirs.empty()) {
    const std::string d = dirs.back();
    dirs.pop_back();
    for (const DirEntry &de : fs::list(d)) {
      const std::string p = fs::join(d, de.name);
      if (de.is_dir) dirs.push_back(p);
      else if (to_lower(fs::extension(de.name)) == ext) out.push_back(make_asset_relative(p));
    }
  }
  for (std::string &p : out) std::replace(p.begin(), p.end(), '\\', '/');
  std::sort(out.begin(), out.end());
  return out;
}

/* An asset field: "Walk (Animation Clip)", a file of its kind dragged from the Project window (another kind is
 * refused), or one chosen with the picker. True when the path changed. */
bool Editor::asset_field(const char *name, const Recti &f, std::string &path, const char *ext, const char *kind) {
  auto &u = ui_;
  const ui::Id id = u.id(name);
  if (u.record_buttons) u.button_rects[std::string("field:") + name] = f;
  const Recti box{f.x, f.y, f.w - f.h, f.h}, pick{box.right(), f.y, f.h, f.h};
  const bool there = !path.empty() && asset_file_exists(path);
  bool changed = object_field_picked(id, path);
  const ui::Id pid = u.id(std::string("pick:") + name);
  if (picker_button(pick)) {
    asset_pick_files_ = asset_paths_with_ext(ext);  // (the disk walked once, as the list opens)
    u.open_popup(pid, {pick.x, pick.bottom(), 0, 0});
  }
  u.tooltip(std::string("Select ") + kind + ": the project's " + ext + " files");
  u.popup(pid, u.px(260), [this, id] {
    if (ui_.menu_item("None")) field_pick_ = {id, ""};
    for (const std::string &p : asset_pick_files_)
      if (ui_.menu_item(fs::stem(p))) field_pick_ = {id, p};
  });
  changed = object_field_box(id, box, path, fs::stem(path) + " (" + kind + ")", std::string("None (") + kind + ")", !there, Icon::File) || changed;
  u.mark_widget(box);
  AssetDrag &d = asset_drag_;
  if (!d.dragging || d.mat || !u.hovered(f)) return changed;
  const bool fits = to_lower(fs::extension(d.path)) == ext;
  u.canvas.rect_outline(f, fits ? u.theme.focus : u.theme.error, u.px(2));
  if (!u.in.released[0]) return changed;
  const std::string dropped = d.path;
  d = AssetDrag{};  // (taken here: nothing else acts on the drop)
  if (u.editing(id)) u.stop_editing();
  if (!fits) {
    Log::warn("%s isn't a %s: the %s field takes %s files", fs::filename(dropped).c_str(), kind, name, ext);
    return changed;
  }
  std::string rel = make_asset_relative(dropped);
  std::replace(rel.begin(), rel.end(), '\\', '/');
  if (rel == path) return changed;
  path = rel;
  return true;
}

/* The end of a Hierarchy press, once a frame after every window drew (so a field drawn after the Hierarchy took its
 * drop first). A row pressed while unselected is selected now - a click, a wobbly click, a reparent, a drop in the
 * Scene view - unless an object field took it; then the drag state goes. */
void Editor::finish_hierarchy_drag() {
  const ui::Input &in = ui_.in;
  if (in.released[0] && hier_pending_select_ && !hier_field_took_drop_ && scene_->find(hier_pending_select_)) select(hier_pending_select_);
  if (!in.down[0]) {
    hier_drag_ = 0;
    hier_dragging_ = false;
    hier_pending_select_ = 0;
    hier_field_took_drop_ = false;
  }
}

/* A component's (or any reflected) fields with the Inspector's widgets, outside the Inspector (task 0052: the Rigging
 * window). True when one changed this frame. */
bool Editor::draw_reflected(const std::function<void(Reflector &)> &fields, ui::Layout &lay) {
  InspectorReflector ir(*this, ui_, lay);
  fields(ir);
  return ir.changed;
}

/* Weight Paint's toggle and, while painting `g`, the brush (task 0046; in the Rigging window since task 0052). */
void Editor::draw_weight_paint_controls(GameObject *g, MeshFilter *mf, ui::Layout &lay) {
  auto &u = ui_;
  if (!mf || !mf->mesh) return;
  Recti wrow = lay.row(u.row_h() + u.px(2));
  const bool painting = weight_paint_ && wp_obj_ == g->id;
  if (u.button({wrow.x + u.px(4), wrow.y, wrow.w - u.px(8), wrow.h}, painting ? "Exit Weight Paint" : "Weight Paint", painting, Icon::Vertex))
    set_weight_paint(!painting);
  u.tooltip("Paint the chosen group's weights with a brush (Blender: Weight Paint mode). Blue is 0, green 0.5, red 1.");
  if (!painting) return;
  InspectorReflector br(*this, u, lay);
  br.enumeration("Brush", wp_brush_.mode, kWeightBrushModes, 4);
  br.help("Add raises, Subtract lowers, Draw moves towards Weight, Blur evens out with the neighbours.");
  br.field("Radius", wp_brush_.radius, 1.0f, 1.0f, 500.0f);
  br.field("Strength", wp_brush_.strength, 0.01f, 0.0f, 1.0f);
  br.field("Weight", wp_brush_.weight, 0.01f, 0.0f, 1.0f);
  br.enumeration("Falloff", wp_brush_.falloff, kWeightFalloffs, 6);
  br.field("Mirror X", wp_brush_.mirror_x);
  br.field("Front Faces Only", wp_brush_.front_only);
  const int hw = (lay.area.w - u.px(16)) / 2;
  Recti brow = lay.row(u.row_h() + u.px(2));
  if (u.button({brow.x + u.px(4), brow.y, hw, brow.h}, "Normalize All")) command("wpnormalize");
  u.tooltip("Each vertex's weights over all groups add up to 1 (Blender: Normalize All).");
  if (u.button({brow.x + u.px(8) + hw, brow.y, hw, brow.h}, "Clean")) command("wpclean 0.01");
  u.tooltip("Weights under 0.01 become 0 (Blender: Clean).");
}

}  // namespace bl
