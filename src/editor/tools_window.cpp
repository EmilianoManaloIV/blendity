// SPDX-License-Identifier: GPL-2.0-or-later
// The Modeling Tools window: UModeler's tool panel (and ProBuilder's) as a
// window of its own, so the modeling tools are always at hand - creating
// shapes, drawing, object operations, Edit Mode's operators and UV tools -
// without going through an object's Inspector.
#include "editor.h"

#include <algorithm>

namespace bl {

using ui::Icon;

void Editor::draw_tools_window(const Recti &r) {
  auto &u = ui_;
  if (u.hovered(r) && u.in.pressed[0]) focused_ = WindowKind::Tools;
  ui::Id sid = u.id("tools_scroll");
  static int content_h = 0;
  const int off = u.begin_scroll(sid, r, content_h);
  ui::Layout lay{{r.x + u.px(6), r.y, r.w - u.px(18), r.h}, r.y + u.px(6) - off};
  lay.row_h = u.row_h();
  GameObject *a = active_object();
  const bool mesh = a && a->get<MeshFilter>() && a->get<MeshFilter>()->mesh;
  const bool has_sel = !selection_.empty();
  /* Who the tools act on. */
  {
    Recti row = lay.row();
    const std::string who = edit_mode_ && edit_object() ? "Editing: " + edit_object()->name
                            : a                          ? "Selected: " + a->name + (selection_.size() > 1 ? strprintf(" (+%zu)", selection_.size() - 1) : "")
                                                         : std::string("Nothing selected - create or draw something");
    u.label(row, who, u.theme.text_bright);
  }
  auto section = [&](const char *title) {
    lay.space(u.px(4));
    Recti h = lay.row(u.row_h() + u.px(2));
    u.canvas.fill_rect({r.x, h.y, r.w, h.h}, u.theme.header);
    bool &open = foldouts_.emplace(std::string("tools_") + title, true).first->second;
    const int a2 = u.font.line_height() - u.px(4);
    u.draw_icon(open ? Icon::ArrowDown : Icon::ArrowRight, {h.x, h.y + (h.h - a2) / 2, a2, a2}, u.theme.text);
    u.label({h.x + a2 + u.px(6), h.y, h.w, h.h}, title, u.theme.text_bright);
    if (u.hovered(h) && u.in.pressed[0]) open = !open;
    return open;
  };
  /* A grid of buttons, `per` to a row. */
  struct B {
    std::string label;
    std::string tip;
    bool enabled;
    std::function<void()> fn;
  };
  auto grid = [&](const std::vector<B> &bs, int per) {
    for (size_t i = 0; i < bs.size(); i += (size_t)per) {
      Recti row = lay.row(u.row_h() + u.px(2));
      const int bw = (row.w - u.px(4) * (per + 1)) / per;
      for (size_t k = i; k < std::min(bs.size(), i + (size_t)per); k++) {
        Recti br{row.x + u.px(4) + (int)(k - i) * (bw + u.px(4)), row.y, bw, row.h};
        if (!bs[k].enabled) {
          u.frame(br, Color::hex(0x2A2A2A), u.theme.border, u.px(3));
          u.label(br, bs[k].label, u.theme.text_dim, ui::Align::Center);
        }
        else if (u.button(br, bs[k].label)) bs[k].fn();
        u.tooltip(bs[k].tip);
      }
    }
  };
  if (section("Create")) {
    std::vector<B> prims, shapes;
    for (const char *k : {"Cube", "Sphere", "Cylinder", "Plane"})
      prims.push_back({k, std::string("GameObject > 3D Object > ") + k, true, [this, k] { create_object(k); }});
    for (int k = 0; k < kShapeCount; k++)
      shapes.push_back({kShapeNames[k], "A parametric shape: its settings stay in the Inspector until you edit the mesh.", true,
                        [this, k] { create_object(std::string("Shape: ") + kShapeNames[k]); }});
    u.label(lay.row(), "Primitives (plain meshes)", u.theme.text_dim);
    grid(prims, 4);
    u.label(lay.row(), "Parametric shapes (keep their settings)", u.theme.text_dim);
    grid(shapes, 4);
  }
  if (section("Draw")) {
    std::vector<B> bs;
    for (int k = 0; k < 5; k++)
      bs.push_back({kDrawShapes[k], "Draw onto the mesh or the ground, with snapping (corners, midpoints, edges, faces, axes,\n"
                                    "parallel and perpendicular edges). With nothing selected it starts a new mesh.",
                    true, [this, k] {
                      if (draw_.active && draw_.shape == k) draw_.active = false;
                      else draw_begin(k);
                    }});
    grid(bs, 5);
    Recti row = lay.row();
    u.label({row.x + u.px(4), row.y, row.w / 2, row.h}, "Corner Radius");
    u.float_field(u.id("tools_fillet"), {row.x + row.w / 2, row.y, row.w / 2 - u.px(6), row.h}, draw_fillet_, 0.005f, 0.0f, 1000.0f);
    u.tooltip("Round the corners of drawn rectangles, polygons and closed polylines (Plasticity: Fillet Curve).");
    row = lay.row();
    u.label({row.x + u.px(4), row.y, row.w / 2, row.h}, "Circle / Arc Segments");
    u.int_field(u.id("tools_segs"), {row.x + row.w / 2, row.y, row.w / 2 - u.px(6), row.h}, draw_segments_, 3, 256);
    row = lay.row();
    u.label({row.x + u.px(4), row.y, row.w / 2, row.h}, "Polygon Sides");
    u.int_field(u.id("tools_sides"), {row.x + row.w / 2, row.y, row.w / 2 - u.px(6), row.h}, draw_sides_, 3, 64);
  }
  if (section("Object")) {
    const bool two = selection_.size() > 1;
    std::vector<B> bs = {
        {edit_mode_ ? "Exit Edit Mode" : "Edit Mode", "Tab", mesh || edit_mode_, [this] {
           if (edit_mode_) exit_edit_mode();
           else enter_edit_mode();
         }},
        {"Join", "Combine the selected meshes into the active one (Ctrl+J).", two && !edit_mode_, [this] { join_selected(); }},
        {"Separate Parts", "Every loose piece becomes its own object.", mesh, [this] { separate("loose"); }},
        {"Union", "Boolean: merge the selected solids into the active one.", two && !edit_mode_, [this] { boolean_selected(1, true); }},
        {"Difference", "Boolean: cut the other selected objects out of the active one.", two && !edit_mode_, [this] { boolean_selected(0, true); }},
        {"Intersect", "Boolean: keep only the overlap.", two && !edit_mode_, [this] { boolean_selected(2, true); }},
        {"Reset XForm", "Bake rotation and scale into the mesh (UModeler: Reset XForm; Blender: Ctrl+A).", has_sel, [this] { mesh_op("apply_transform"); }},
        {"Mirror X", "Add the mirrored half for good.", has_sel, [this] { mesh_op("mirror_x"); }},
        {"Mirror Z", "Add the mirrored half for good.", has_sel, [this] { mesh_op("mirror_z"); }},
        {"Apply Modifiers", "Bake the modifier stack into the mesh.", has_sel, [this] { mesh_op("apply_modifiers"); }},
        {"Shade Smooth", "Smooth shading.", has_sel, [this] { mesh_op("shade_smooth"); }},
        {"Shade Flat", "Flat shading.", has_sel, [this] { mesh_op("shade_flat"); }},
        {"Subdivide", "Catmull-Clark, one level.", has_sel, [this] { mesh_op("subdivide"); }},
        {"Merge by Distance", "Weld close vertices.", has_sel, [this] { mesh_op("merge"); }},
        {"Origin to Bottom", "Set Origin to the bottom centre.", has_sel, [this] { set_origin(4); }},
        {origin_edit_ ? "Done (Origin)" : "Edit Origin", "Move the origin with the gizmo, or click a vertex / edge / face to snap it there.", has_sel && !edit_mode_,
         [this] { origin_edit_ = !origin_edit_; }},
        {pilot_cam_ ? "Stop Piloting" : "Pilot Camera", "Look through the selected camera and move it like the Scene view; the wheel with Ctrl changes its FOV.",
         (a && a->get<Camera>()) || pilot_cam_, [this] { toggle_pilot_camera(); }},
        {"Align Camera to View", "Put the selected camera where the Scene view is looking from (Unity: Align With View).", a && a->get<Camera>(),
         [this] { align_camera_to_view(); }},
    };
    grid(bs, 3);
  }
  if (edit_mode_ && edit_object()) {
    if (section("Edit Mode")) draw_edit_tools(lay);
  }
  else if (section("Edit Mode")) {
    Recti row = lay.row();
    u.label(row, mesh ? "Press Tab (or Edit Mode above) to edit the mesh." : "Select a mesh to edit it.", u.theme.text_dim);
  }
  if (section("UV")) {
    std::vector<B> bs = {
        {"Unwrap", "Unwrap along the seams (U).", mesh, [this] { uv_op("unwrap"); }},
        {"Smart UV", "Smart UV Project: no seams needed.", mesh, [this] { uv_op("smart"); }},
        {"Cube Project", "Each face projected along its main axis.", mesh, [this] { uv_op("cube"); }},
        {"Seams from Sharp", "A seam on every sharp edge (and marked sharp edges).", mesh, [this] { uv_op("seams_from_sharp"); }},
        {"Sharp + Unwrap", "Seams from sharp edges, then unwrap.", mesh, [this] { uv_op("seams_from_sharp_unwrap"); }},
        {"UV Editor", "Open the UV Editor window.", true, [this] { dock_open(WindowKind::UVEditor); }},
    };
    grid(bs, 3);
  }
  if (section("Snapping")) {
    Recti row = lay.row();
    u.label(row, "Move gizmo: hold V for vertex snapping, Ctrl+Shift to drop onto surfaces.", u.theme.text_dim);
    row = lay.row(u.row_h() + u.px(2));
    if (u.button({row.x + u.px(4), row.y, row.w - u.px(8), row.h}, surface_align_ ? "Surface Snap: Align to Normal (on)" : "Surface Snap: Align to Normal (off)",
                 surface_align_))
      surface_align_ = !surface_align_;
    u.tooltip("When an object is dropped onto a surface (Ctrl+Shift while moving), also turn its up axis to the surface's normal.");
  }
  content_h = lay.y + off - r.y + u.px(20);
  u.end_scroll();
}

}  // namespace bl
