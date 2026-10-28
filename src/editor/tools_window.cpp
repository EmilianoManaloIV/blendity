// SPDX-License-Identifier: GPL-2.0-or-later
// The Modeling Tools window: UModeler's tool panel (and ProBuilder's) as a
// window of its own, so the modeling tools are always at hand - creating
// shapes, drawing, object operations, Edit Mode's operators and UV tools -
// without going through an object's Inspector.
#include "editor.h"

#include <algorithm>

namespace bl {

using ui::Icon;

/* A grid of buttons, `per` to a row, a disabled one drawn flat with its label dimmed (the Rigging window too). */
void Editor::button_grid(ui::Layout &lay, const std::vector<GridButton> &bs, int per) {
  auto &u = ui_;
  for (size_t i = 0; i < bs.size(); i += (size_t)per) {
    Recti row = lay.row(u.row_h() + u.px(2));
    const int bw = (row.w - u.px(4) * (per + 1)) / per;
    for (size_t k = i; k < std::min(bs.size(), i + (size_t)per); k++) {
      Recti br{row.x + u.px(4) + (int)(k - i) * (bw + u.px(4)), row.y, bw, row.h};
      if (!bs[k].enabled) {
        u.frame(br, u.theme.strip, u.theme.border, u.px(3));
        u.label(br, bs[k].label, u.theme.text_dim, ui::Align::Center);
      }
      else if (u.button(br, bs[k].label)) bs[k].fn();
      u.tooltip(bs[k].tip);
    }
  }
}

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
  /* The current layout's focus (task 0055): a section of another stage isn't shown; the first row says so. */
  {
    int hidden = 0;
    for (uint32_t g : {(uint32_t)FocusCreate, (uint32_t)FocusDraw, (uint32_t)FocusObject, (uint32_t)FocusEdit, (uint32_t)FocusCheck,
                       (uint32_t)FocusCheck /* (two Check sections) */, (uint32_t)FocusUV, (uint32_t)FocusSnap})
      hidden += !shows(g);
    if (hidden > 0) {
      Recti row = lay.row(u.row_h() + u.px(2));
      if (u.button({row.x + u.px(4), row.y, row.w - u.px(8), row.h}, strprintf("%d more for other stages: Show All Tools", hidden)))
        show_all_tools_ = true;
      u.tooltip("The '" + workspace_ + "' layout shows this stage's tools (Window > Layouts). Keys and the console reach every tool.");
    }
    else if (show_all_tools_ && focus_ != FocusAll) {  // and back
      Recti row = lay.row(u.row_h() + u.px(2));
      if (u.button({row.x + u.px(4), row.y, row.w - u.px(8), row.h}, "Showing all tools: only this stage's")) show_all_tools_ = false;
    }
  }
  auto section = [&](const char *title, uint32_t groups = FocusAll) {
    if (!shows(groups)) return false;
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
  auto grid = [&](const std::vector<GridButton> &bs, int per) { button_grid(lay, bs, per); };
  if (section("Create", FocusCreate)) {
    std::vector<GridButton> prims, shapes;
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
  if (section("Draw", FocusDraw)) {
    std::vector<GridButton> bs;
    for (int k = 0; k < kDrawShapeCount; k++)
      bs.push_back({kDrawShapes[k], k == 5 ? "A construction line through two points: drawing snaps to it, to where guides cross and to its\n"
                                             "direction (Plasticity's lines, SketchUp's guides). Not part of the mesh."
                                           : "Draw onto the mesh or the ground, with snapping (corners, midpoints, edges, faces, axes,\n"
                                             "parallel and perpendicular edges, guides). With nothing selected it starts a new mesh.",
                    true, [this, k] {
                      if (draw_.active && draw_.shape == k) draw_.active = false;
                      else draw_begin(k);
                    }});
    grid(bs, 3);
    auto choice = [&](const char *label, int &v, const char *const *opts, const char *id, const char *tip) {
      Recti rr = lay.row(u.row_h() + u.px(2));
      u.label({rr.x + u.px(4), rr.y, rr.w / 2, rr.h}, label);
      u.combo(u.id(id), {rr.x + rr.w / 2, rr.y, rr.w / 2 - u.px(6), rr.h}, v, opts, 3);
      u.tooltip(tip);
    };
    {
      static const char *kAxes[] = {"Local", "Global", ""};
      int ax = draw_global_axes_ ? 1 : 0;
      Recti rr = lay.row(u.row_h() + u.px(2));
      u.label({rr.x + u.px(4), rr.y, rr.w / 2, rr.h}, "Snap Axes");
      if (u.combo(u.id("tools_axes"), {rr.x + rr.w / 2, rr.y, rr.w / 2 - u.px(6), rr.h}, ax, kAxes, 2)) draw_global_axes_ = ax == 1;
      u.tooltip("Local: the face's / object's own axes (follow a rotated object). Global: the world's X / Y / Z.");
    }
    {
      Recti rr = lay.row(u.row_h() + u.px(2));
      if (u.button({rr.x + u.px(4), rr.y, rr.w - u.px(8), rr.h}, draw_perp_snap_ ? "Perpendicular Snap (on)" : "Perpendicular Snap (off)",
                   draw_perp_snap_))
        draw_perp_snap_ = !draw_perp_snap_;
      u.tooltip("Snap onto an edge where the line from the last point meets it at 90 degrees (Plasticity).\n"
                "Guides: X / Y / Z along that axis (Local / Global as Snap Axes says), Ctrl+click along an edge.");
    }
    choice("Rectangle From", draw_rect_mode_, kRectModes, "tools_rectmode",
           "Corner: two opposite corners. Center: the middle, then a corner. 3 Points: one side at any angle, then the width.");
    choice("Circle / Polygon From", draw_circle_mode_, kCircleModes, "tools_circmode",
           "Center: the middle, then the radius. 2 Points: across it. 3 Points: three points on it.");
    {
      Recti rr = lay.row(u.row_h() + u.px(2));
      if (u.button({rr.x + u.px(4), rr.y, rr.w - u.px(8), rr.h}, draw_uniform_ ? "Square Rectangles (on)" : "Square Rectangles (off)", draw_uniform_))
        draw_uniform_ = !draw_uniform_;
      u.tooltip("Rectangles keep equal sides: a square from its corner, centre or one side.");
    }
    {
      Recti rr = lay.row(u.row_h() + u.px(2));
      if (u.button({rr.x + u.px(4), rr.y, rr.w - u.px(8), rr.h}, draw_face_center_ ? "Start at Face Center (on)" : "Start at Face Center (off)",
                   draw_face_center_))
        draw_face_center_ = !draw_face_center_;
      u.tooltip("Circles, polygons and centre rectangles start at the exact centre of the face you click (Plasticity).\n"
                "With this off, the centre still snaps (orange) when the mouse is near it.");
    }
    {
      Recti rr = lay.row(u.row_h() + u.px(2));
      const int gw = (rr.w - u.px(12)) / 3;
      if (u.button({rr.x + u.px(4), rr.y, gw, rr.h}, "Guides from Edges")) guides_from_selected_edges();
      u.tooltip("Edit Mode: a construction line along each selected edge.");
      if (u.button({rr.x + u.px(6) + gw, rr.y, gw, rr.h}, show_guides_ ? "Hide Guides" : "Show Guides", show_guides_)) show_guides_ = !show_guides_;
      if (u.button({rr.x + u.px(8) + 2 * gw, rr.y, gw, rr.h}, strprintf("Clear (%zu)", scene_->guides.size()))) {
        scene_->guides.clear();
        mark_changed("Clear Guides");
      }
    }
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
  if (section("Object", FocusObject)) {
    const bool two = selection_.size() > 1;
    std::vector<GridButton> bs = {
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
        {"Shade Auto Smooth", "Smooth, but hard where faces meet at more than 30 degrees (Blender: Shade Auto Smooth).", has_sel,
         [this] { mesh_op("shade_auto_smooth"); }},
        {"Subdivide", "Catmull-Clark, one level.", has_sel, [this] { mesh_op("subdivide"); }},
        {"Merge by Distance", "Weld close vertices.", has_sel, [this] { mesh_op("merge"); }},
        {"Delete Loose", "Remove vertices and wire edges no face uses (Blender: Clean Up > Delete Loose).", has_sel && !edit_mode_,
         [this] { mesh_op("delete_loose"); }},
        {"Make Faces (Close Holes)", "Make Face on the whole object: every hole gets a face, cracks with no area are welded shut\n(Blender: Fill Holes). In Edit Mode it is Make Face (F / Alt+F) on the selection.", has_sel && !edit_mode_,
         [this] { mesh_op("smart_fill"); }},
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
    if (section("Edit Mode", FocusEdit)) draw_edit_tools(lay);
  }
  else if (section("Edit Mode", FocusEdit)) {
    Recti row = lay.row();
    u.label(row, mesh ? "Press Tab (or Edit Mode above) to edit the mesh." : "Select a mesh to edit it.", u.theme.text_dim);
  }
  if (section("Sculpt", FocusEdit)) draw_sculpt_tools(lay);  // (task 0236)
  if (section("Check: Z-Fighting", FocusCheck)) draw_zfight_panel(lay);
  if (section("Check: Overlapping Vertices & Edges", FocusCheck)) draw_overlap_panel(lay);
  if (section("UV", FocusUV)) {
    std::vector<GridButton> bs = {
        {"Unwrap", "Unwrap along the seams (U).", mesh, [this] { uv_op("unwrap"); }},
        {"Smart UV", "Smart UV Project: no seams needed.", mesh, [this] { uv_op("smart"); }},
        {"Part Unwrap", "Unwrap by parts: few charts, each under the stretch bound.", mesh, [this] { uv_op("parts"); }},
        {"Cube Project", "Each face projected along its main axis.", mesh, [this] { uv_op("cube"); }},
        {"Seams from Sharp", "A seam on every sharp edge (and marked sharp edges).", mesh, [this] { uv_op("seams_from_sharp"); }},
        {"Sharp + Unwrap", "Seams from sharp edges, then unwrap.", mesh, [this] { uv_op("seams_from_sharp_unwrap"); }},
        {"UV Editor", "Open the UV Editor window.", true, [this] { dock_open(WindowKind::UVEditor); }},
    };
    grid(bs, 3);
  }
  if (section("Snapping", FocusSnap)) {
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
