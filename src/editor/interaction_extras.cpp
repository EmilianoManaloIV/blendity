// SPDX-License-Identifier: GPL-2.0-or-later
// The interaction pass's second round (task 0190, ADR 0060 amended): each layout's first-run guide (what the layout is
// for and its three next steps, dismissed for good - the 2025 study's scaffolding), File > Open Recent, a number typed
// while dragging a gizmo axis (Blender's numeric input). The Inspector's essentials are in panels.cpp.
#include "editor.h"

#include <algorithm>

namespace bl {

namespace {

/* A layout's guide: what it is for, and three next steps (the palette's commands). */
struct Guide {
  const char *layout, *purpose;
  const char *steps[3];
};
const Guide kGuides[] = {
    {"Reference & Planning", "Collect what you'll build from: reference images, a camera framing the shot.", {"Create Camera", "Create Plane", "Learn Window"}},
    {"Blocking Out", "Rough shapes first: primitives, drawing on faces, Push/Pull.", {"Create Cube", "Create Cylinder", "Modeling Tools Window"}},
    {"Modeling & Topology", "Shape the mesh in Edit Mode: extrude, inset, bevel, loop cuts.", {"Create Cube", "Subdivide", "Modeling Tools Window"}},
    {"UV Unwrapping", "Lay the surface flat for textures: seams, unwrap, pack.", {"Smart UV Project", "Part Unwrap", "Pack Islands"}},
    {"Texturing & Materials", "Give surfaces their look: materials, procedural textures.", {"Materials Window", "Textures Window", "New Material Asset"}},
    {"Lighting & Rendering", "Light the scene and render it.", {"Create Area Light", "Generate Lighting", "Render Preview"}},
    {"Editing", "Develop a render or an image: exposure, colour, crop, masks.", {"Develop the Last Render", "Editing Window", "Use as Camera Look"}},
    {"Rigging", "Give a character bones: a rig, IK, hair chains.", {"Humanoid Rig", "Auto IK on Selected Skeleton", "Rig Hair from Mesh"}},
    {"Skinning & Weight Painting", "Bind a mesh to its bones and paint the weights.", {"Rigging Window", "Humanoid Rig", "Inspector Window"}},
    {"Animation", "Pose and key over time: clips, curves, the Sequencer.", {"Animation Window", "Sequencer Window", "Play / Stop"}},
};

const Guide *guide_for(const std::string &layout) {
  for (const Guide &g : kGuides)
    if (layout == g.layout) return &g;
  return nullptr;
}

}  // namespace

/* The guide strip above the dock for a layout seen for the first time; what is left of the area for the dock. */
Recti Editor::draw_layout_guide(const Recti &area) {
  auto &u = ui_;
  const Guide *g = guides_enabled_ ? guide_for(workspace_) : nullptr;
  if (!g || guides_seen_.count(workspace_)) return area;
  const int h = u.row_h() + u.px(10);
  const Recti strip{area.x, area.y, area.w, h};
  u.canvas.fill_rect({area.x, area.y, area.w, h + u.px(2)}, u.theme.border);  // (the gap under the strip too: the frame isn't cleared)
  u.canvas.fill_rect(strip, u.theme.header);
  int x = strip.x + u.px(8);
  const std::string purpose = std::string(g->layout) + ": " + g->purpose;
  const int pw = std::min(u.font.text_width(purpose) + u.px(12), strip.w / 2);
  u.canvas.text_ellipsis(u.font, x, strip.y + (h - u.font.line_height()) / 2, pw, purpose, u.theme.text_bright);
  x += pw;
  for (const char *step : g->steps) {
    const PaletteEntry *e = palette_find(step);
    if (!e) continue;
    const int bw = u.font.text_width(step) + u.px(20);
    if (x + bw > strip.right() - u.px(90)) break;
    if (u.button({x, strip.y + u.px(4), bw, h - u.px(8)}, step)) palette_run(*e);
    if (!e->help.empty()) u.tooltip(e->help);
    x += bw + u.px(6);
  }
  if (u.button({strip.right() - u.px(80), strip.y + u.px(4), u.px(74), h - u.px(8)}, "Got it")) {
    guides_seen_.insert(workspace_);
    save_prefs();
  }
  u.tooltip("Hide this layout's guide for good (Help > Reset Guides shows them again).");
  return {area.x, area.y + h + u.px(2), area.w, std::max(0, area.h - h - u.px(2))};
}

std::vector<std::string> Editor::palette_guide_steps_for_test(const std::string &layout) {
  std::vector<std::string> out;
  if (const Guide *g = guide_for(layout))
    for (const char *step : g->steps)
      if (palette_find(step)) out.push_back(step);
  return out;
}

void Editor::note_recent_scene(const std::string &path) {
  if (path.empty()) return;
  recent_scenes_.erase(std::remove(recent_scenes_.begin(), recent_scenes_.end(), path), recent_scenes_.end());
  recent_scenes_.insert(recent_scenes_.begin(), path);
  if (recent_scenes_.size() > 8) recent_scenes_.resize(8);
}

std::string Editor::prefs_text_0190() const {
  std::string recent, seen;
  for (const std::string &p : recent_scenes_)
    if (p.find(';') == std::string::npos) recent += (recent.empty() ? "" : ";") + p;
  for (const std::string &g : guides_seen_) seen += (seen.empty() ? "" : ";") + g;
  return "recent_scenes=" + recent + "\ninspector_essentials=" + (inspector_essentials_ ? "1" : "0") + "\nguides=" + (guides_enabled_ ? "1" : "0") +
         "\nguides_seen=" + seen + "\nnav_buttons=" + (nav_buttons_always_ ? "always" : "tablet") +
         "\ntablet_mode=" + (tablet_pref_ == 1 ? "on" : tablet_pref_ == 2 ? "off" : "auto") + "\n";
}

/* Digits typed while dragging a gizmo's axis: the move is exactly that far along it (an expression works: 2*1.5).
 * Backspace takes a digit back; with nothing typed the mouse moves it. */
bool Editor::typed_axis_move(Vec3 axis, Vec3 &delta) {
  auto &in = ui_.in;
  for (char c : in.text)
    if ((c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+' || c == '*' || c == '/' || c == '(' || c == ')') gizmo_typed_ += c;
  in.text.clear();
  if (in.key_pressed[platform::KEY_BACKSPACE] && !gizmo_typed_.empty()) gizmo_typed_.pop_back();
  double v = 0.0;
  if (gizmo_typed_.empty() || !ui::eval_number(gizmo_typed_, v)) return false;
  delta = axis * (float)v;
  const std::string shown = "Move " + gizmo_typed_;
  ui_.overlay([this, shown] {
    const Recti r{ui_.in.mx + ui_.px(16), ui_.in.my + ui_.px(16), ui_.font.text_width(shown) + ui_.px(12), ui_.row_h()};
    ui_.frame(r, ui_.theme.tab_active, ui_.theme.focus, ui_.px(3));
    ui_.canvas.text(ui_.font, r.x + ui_.px(6), r.y + ui_.px(3), shown, ui_.theme.text_bright);
  });
  return true;
}

}  // namespace bl
