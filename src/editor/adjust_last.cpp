// SPDX-License-Identifier: GPL-2.0-or-later
// Adjust Last outside Edit Mode's operators (task 0266, ADR 0085's follow-up): a primitive just added (its size and
// segments), a UV operator with numbers (Unwrap's and Pack's margin, Smart UV's angle) and a Draw shape with a count
// (a circle's or an arc's segments, a polygon's sides, a rectangle's fillet) get Blender's redo panel. Changing a field
// runs the operation again from the state before it and amends the same undo step: the step holds the scene before,
// so only the editor's after-snapshot is taken again - no new step, Ctrl+Z still goes back to before the operation.
// The panel lasts while that step is the newest; anything else committed ends it, as Blender's does.
#include "editor.h"
#include "../scene/uv.h"

namespace bl {

static bool primitive_has_numbers(const std::string &kind) {
  return kind == "Cube" || kind == "Sphere" || kind == "Icosphere" || kind == "Cylinder" || kind == "Cone" || kind == "Torus" || kind == "Plane";
}

/* The primitive's mesh from the panel's numbers (the same defaults create_primitive uses). */
static MeshPtr primitive_mesh(const Editor::ObjLastOp &o) {
  const std::string &k = o.what;
  if (k == "Cube") return primitives::cube(o.a);
  if (k == "Sphere") return primitives::uv_sphere(o.a, o.n, o.m);
  if (k == "Icosphere") return primitives::ico_sphere(o.a, o.n);
  if (k == "Cylinder") return primitives::cylinder(o.a, o.b, o.n);
  if (k == "Cone") return primitives::cone(o.a, o.b, o.n);
  if (k == "Torus") return primitives::torus(o.a, o.b, o.n, o.m);
  if (k == "Plane") return primitives::plane(o.a, o.n);
  return nullptr;
}

/* The serial the step will have once committed: the operation marked a change this frame (pending) or already did. */
uint64_t Editor::obj_last_serial_now() const { return undo_serial_ + (pending_change_ ? 1 : 0); }

void Editor::record_primitive_last(GameObject *g, const std::string &kind) {
  if (!g || !primitive_has_numbers(kind)) return;
  ObjLastOp o;
  o.kind = "primitive", o.what = kind, o.obj = g->id;
  if (kind == "Cube") o.a = 1.0f;
  else if (kind == "Sphere") o.a = 0.5f, o.n = 24, o.m = 16;
  else if (kind == "Icosphere") o.a = 0.5f, o.n = 2;
  else if (kind == "Cylinder") o.a = 0.5f, o.b = 2.0f, o.n = 24;
  else if (kind == "Cone") o.a = 0.5f, o.b = 1.0f, o.n = 24;
  else if (kind == "Torus") o.a = 0.5f, o.b = 0.2f, o.n = 32, o.m = 16;
  else if (kind == "Plane") o.a = 10.0f, o.n = 10;
  o.serial = obj_last_serial_now();
  obj_last_ = std::move(o);
  obj_last_open_ = true;
  obj_last_note_result();
}

void Editor::record_uv_last(uint64_t obj, const std::string &op, MeshPtr before, const std::vector<uint8_t> *mask) {
  if (op != "unwrap" && op != "smart" && op != "pack") return;
  ObjLastOp o;
  o.kind = "uv", o.what = op, o.obj = obj, o.before = std::move(before);
  o.a = 0.02f;   // margin
  o.b = 66.0f;   // Smart UV's angle limit
  o.flag = true; // Pack's rotation
  if (mask) o.mask = *mask, o.has_mask = true;
  o.serial = obj_last_serial_now();
  obj_last_ = std::move(o);
  obj_last_open_ = true;
  obj_last_note_result();
}

void Editor::record_draw_last(uint64_t obj, MeshPtr before, const Mat4 &before_xf) {
  const int shape = draw_.shape;
  if (shape != 1 && shape != 2 && shape != 3 && shape != 4) return;  // (a rectangle's fillet, a circle's or an arc's segments, a polygon's sides)
  ObjLastOp o;
  o.kind = "draw", o.what = kDrawShapes[shape], o.obj = obj, o.before = std::move(before), o.before_xf = before_xf;
  o.draw_pts = draw_.pts;
  o.draw_snaps = draw_.snaps;
  o.draw_plane_p = draw_.plane_p, o.draw_plane_n = draw_.plane_n, o.draw_axis_u = draw_.axis_u;
  o.draw_has_plane = draw_.has_plane, o.draw_face = draw_.face, o.draw_shape = shape;
  o.n = shape == 4 ? draw_sides_ : draw_segments_;
  o.a = draw_fillet_;
  o.serial = obj_last_serial_now();
  obj_last_ = std::move(o);
  obj_last_open_ = true;
  obj_last_note_result();
}

bool Editor::obj_last_valid() const {
  if (obj_last_.kind.empty() || playing_ || obj_last_.serial != undo_serial_ || pending_change_) return false;
  const GameObject *g = scene_->find(obj_last_.obj);
  const MeshFilter *mf = g ? g->get<MeshFilter>() : nullptr;
  if (!mf || !mf->mesh || mf->mesh.get() != obj_last_.result || mf->mesh->version != obj_last_.result_version) return false;  // (undone, replaced, edited since)
  return obj_last_.kind != "draw" || (edit_mode_ && edit_obj_ == g->id);  // (a Draw shape is adjusted in the Edit Mode it was drawn in)
}

/* The mesh the operation (or its re-run) left on the object: the panel lasts while it is still there, unedited. */
void Editor::obj_last_note_result() {
  const GameObject *g = scene_->find(obj_last_.obj);
  const MeshFilter *mf = g ? g->get<MeshFilter>() : nullptr;
  obj_last_.result = mf && mf->mesh ? mf->mesh.get() : nullptr;
  obj_last_.result_version = mf && mf->mesh ? mf->mesh->version : 0;
}

/* The step's after-snapshot taken again: the change belongs to the step the operation made. */
void Editor::amend_last_step() {
  pending_change_ = false;
  settle_meshes();
  obj_last_note_result();
  stable_ = scene_->clone();
  scene_dirty_ = true;
  ui_.redraw = true;
}

/* Run the recorded operation again with the panel's numbers, from the state before it. */
void Editor::obj_last_rerun() {
  ObjLastOp &o = obj_last_;
  GameObject *g = scene_->find(o.obj);
  MeshFilter *mf = g ? g->get<MeshFilter>() : nullptr;
  if (!mf) return;
  if (o.kind == "primitive") {
    if (MeshPtr m = primitive_mesh(o)) mf->mesh = m;
    return amend_last_step();
  }
  if (o.kind == "uv" && o.before) {
    mf->mesh = o.before;
    Mesh &m = *mesh_make_mutable(mf->mesh);
    const uvops::Mask *pm = o.has_mask ? &o.mask : nullptr;
    const float margin = std::max(0.0f, std::min(0.25f, o.a));
    if (o.what == "unwrap") uvops::unwrap_lscm(m, pm, margin);
    else if (o.what == "smart") uvops::smart_project(m, pm, std::max(1.0f, std::min(89.0f, o.b)), margin);
    else uvops::pack_islands(m, pm, margin, o.flag);
    m.touch();
    return amend_last_step();
  }
  if (o.kind == "draw" && o.before && edit_mode_ && edit_object() == g) {
    mf->mesh = o.before;
    g->set_world_matrix(o.before_xf);
    const DrawTool keep = draw_;
    const int keep_segments = draw_segments_, keep_sides = draw_sides_;
    const float keep_fillet = draw_fillet_;
    draw_.pts = o.draw_pts, draw_.snaps = o.draw_snaps, draw_.shape = o.draw_shape;
    draw_.plane_p = o.draw_plane_p, draw_.plane_n = o.draw_plane_n, draw_.axis_u = o.draw_axis_u;
    draw_.has_plane = o.draw_has_plane, draw_.face = o.draw_face, draw_.bent = false;
    if (o.draw_shape == 4) draw_sides_ = std::max(3, std::min(256, o.n));
    else draw_segments_ = std::max(3, std::min(256, o.n));
    draw_fillet_ = std::max(0.0f, o.a);
    if (!draw_.pts.empty()) {
      const Vec3 last = draw_.pts.back();
      draw_.pts.pop_back();
      bool closed = false;
      const std::vector<Vec3> outline = draw_outline(last, true, closed);
      draw_commit(outline, closed);
    }
    draw_ = keep;
    draw_segments_ = keep_segments, draw_sides_ = keep_sides, draw_fillet_ = keep_fillet;  // (the tool's own settings stay as they were)
    return amend_last_step();
  }
}

/* The panel, at the bottom left of the Scene view as Edit Mode's is: a title, then the operation's numbers. */
void Editor::draw_obj_last_panel(const Recti &view) {
  auto &u = ui_;
  ObjLastOp &o = obj_last_;
  struct Row { const char *label; float *f; int *i; bool *b; float speed, mn, mx; int imn, imx; };
  std::vector<Row> rows;
  std::string title;
  if (o.kind == "primitive") {
    title = "Add " + o.what;
    const bool radial = o.what == "Sphere" || o.what == "Icosphere" || o.what == "Cylinder" || o.what == "Cone";
    if (o.what == "Torus") rows.push_back({"Major Radius", &o.a, nullptr, nullptr, 0.01f, 0.001f, 1000.0f, 0, 0}), rows.push_back({"Minor Radius", &o.b, nullptr, nullptr, 0.01f, 0.001f, 1000.0f, 0, 0});
    else rows.push_back({radial ? "Radius" : "Size", &o.a, nullptr, nullptr, 0.01f, 0.001f, 10000.0f, 0, 0});
    if (o.what == "Cylinder" || o.what == "Cone") rows.push_back({"Height", &o.b, nullptr, nullptr, 0.01f, 0.001f, 10000.0f, 0, 0});
    if (o.what == "Sphere" || o.what == "Cylinder" || o.what == "Cone" || o.what == "Torus") rows.push_back({"Segments", nullptr, &o.n, nullptr, 0, 0, 0, 3, 256});
    if (o.what == "Sphere") rows.push_back({"Rings", nullptr, &o.m, nullptr, 0, 0, 0, 2, 256});
    if (o.what == "Torus") rows.push_back({"Ring Segments", nullptr, &o.m, nullptr, 0, 0, 0, 3, 256});
    if (o.what == "Icosphere") rows.push_back({"Subdivisions", nullptr, &o.n, nullptr, 0, 0, 0, 0, 6});
    if (o.what == "Plane") rows.push_back({"Subdivisions", nullptr, &o.n, nullptr, 0, 0, 0, 1, 256});
  }
  else if (o.kind == "uv") {
    title = o.what == "unwrap" ? "Unwrap" : o.what == "smart" ? "Smart UV Project" : "Pack Islands";
    if (o.what == "smart") rows.push_back({"Angle Limit", &o.b, nullptr, nullptr, 0.5f, 1.0f, 89.0f, 0, 0});
    rows.push_back({"Margin", &o.a, nullptr, nullptr, 0.002f, 0.0f, 0.25f, 0, 0});
    if (o.what == "pack") rows.push_back({"Rotate", nullptr, nullptr, &o.flag, 0, 0, 0, 0, 0});
  }
  else {
    title = "Draw " + o.what;
    if (o.draw_shape == 1) rows.push_back({"Fillet", &o.a, nullptr, nullptr, 0.005f, 0.0f, 100.0f, 0, 0});
    else rows.push_back({o.draw_shape == 4 ? "Sides" : "Segments", nullptr, &o.n, nullptr, 0, 0, 0, 3, 256});
  }
  const int rh = u.row_h(), pad = u.px(6), w = std::min(u.px(270), view.w - u.px(20));
  const int nrows = obj_last_open_ ? (int)rows.size() : 0;
  const int h = rh + u.px(6) + nrows * (rh + u.px(2)) + (nrows ? pad : 0);
  const Recti r{view.x + u.px(10), view.bottom() - h - u.px(10), w, h};
  obj_last_rect_ = r;
  u.frame(r, (u.theme.popup & 0x00FFFFFFu) | (235u << 24), u.theme.border, u.px(4));
  const Recti hr{r.x + pad, r.y + u.px(3), r.w - pad * 2, rh};
  const int a = u.font.line_height() - u.px(4);
  u.draw_icon(obj_last_open_ ? ui::Icon::ArrowDown : ui::Icon::ArrowRight, {hr.x, hr.y + (rh - a) / 2, a, a}, u.theme.text);
  u.label({hr.x + a + u.px(6), hr.y, hr.w - a - rh, rh}, title, u.theme.text_bright);
  if (u.record_buttons) u.button_rects["adjust:title"] = hr;
  if (u.hovered(hr) && u.in.pressed[0]) {
    obj_last_open_ = !obj_last_open_;
    u.consume_click();
  }
  bool changed = false;
  int y = hr.bottom() + u.px(2);
  for (size_t k = 0; k < (size_t)nrows; k++, y += rh + u.px(2)) {
    const Row &row = rows[k];
    const Recti lr{r.x + pad, y, u.px(110), rh}, fr{r.x + pad + u.px(112), y, r.w - pad * 2 - u.px(112), rh};
    u.label(lr, row.label);
    if (u.record_buttons) u.button_rects[std::string("adjust:") + row.label] = fr;
    const ui::Id id = u.id(std::string("objlast_") + row.label);
    if (row.f) changed |= u.float_field(id, fr, *row.f, row.speed, row.mn, row.mx, "%.3g");
    else if (row.i) changed |= u.int_field(id, fr, *row.i, row.imn, row.imx);
    else if (row.b) changed |= u.checkbox({fr.x, fr.y + u.px(2), rh - u.px(4), rh - u.px(4)}, *row.b);
  }
  if (changed) obj_last_rerun();
}

}  // namespace bl
