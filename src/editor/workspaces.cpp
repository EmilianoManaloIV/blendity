// SPDX-License-Identifier: GPL-2.0-or-later
// Layouts for each stage of the work (task 0055, ADR 0027): a window arrangement plus a focus, the tool groups that
// stage shows (the rest hidden, never removed: keys and the console reach everything, and Show All Tools brings them
// back). Unity: the Layout dropdown (Save / Delete Layout). Blender: Workspaces (Modeling, UV Editing, Texture Paint,
// Shading, Animation, Rendering ...). Also the Reference Image object (Blender: Add > Image > Reference).
#include "editor.h"

#include "../image/image.h"
#include "../scene/scene.h"

#include <algorithm>

namespace bl {

namespace {

/* Window ids in dock strings: 0 Scene 1 Game 2 Hierarchy 3 Inspector 4 Project 5 Console 6 Learn 8 Profiler 9 Render
 * 10 UV Editor 11 Materials 12 Modeling Tools 13 Lighting 14 Textures 15 Rigging 16 Animation 17 Sequencer 18 Live Link
 * 19 Editing 20 My Tools. */
const Editor::Workspace kWorkspaces[] = {
    {"Default", "H(0.78,V(0.68,H(0.21,L(2:0),L(0,1,6,10:0)),L(4,11,5,8,9:0)),L(3,12:0))", Editor::FocusAll, true},
    {"Reference & Planning", "H(0.8,V(0.72,H(0.2,L(2:0),L(0,1:0)),L(4,5:0)),L(3:0))",
     Editor::FocusCreate | Editor::FocusObject | Editor::FocusSnap | Editor::FocusReference | Editor::FocusLight, true},
    {"Blocking Out", "H(0.76,V(0.74,H(0.2,L(2:0),L(0,1:0)),L(4,5:0)),L(12,3:0))",
     Editor::FocusCreate | Editor::FocusDraw | Editor::FocusObject | Editor::FocusSnap | Editor::FocusReference, true},
    {"Modeling & Topology", "H(0.74,V(0.76,H(0.18,L(2:0),L(0:0)),L(4,5:0)),L(12,3:0))",
     Editor::FocusCreate | Editor::FocusDraw | Editor::FocusObject | Editor::FocusEdit | Editor::FocusCheck | Editor::FocusSnap |
         Editor::FocusDeform,
     true},
    {"UV Unwrapping", "H(0.78,V(0.74,H(0.5,L(0:0),L(10:0)),L(4,5:0)),L(12,3:0))",
     Editor::FocusObject | Editor::FocusEdit | Editor::FocusUV | Editor::FocusMaterial, true},
    {"Texturing & Materials", "H(0.68,V(0.62,H(0.55,L(0:0),L(1:0)),H(0.5,L(11:0),L(14:0))),L(3,2:0))",
     Editor::FocusObject | Editor::FocusMaterial, true},
    {"Lighting & Rendering", "H(0.74,V(0.62,H(0.55,L(0:0),L(1:0)),L(9,4,5:0)),V(0.55,L(13:0),L(3,2:0)))",
     Editor::FocusObject | Editor::FocusLight | Editor::FocusMaterial, true},
    {"Editing", "H(0.8,V(0.8,L(19,9:0),L(4,5:0)),L(3,2:0))", Editor::FocusObject | Editor::FocusMaterial, true},
    {"Rigging", "H(0.72,V(0.74,H(0.22,L(2:0),L(0,1:0)),L(4,5:0)),L(15,3:0))",
     Editor::FocusCreate | Editor::FocusObject | Editor::FocusRig | Editor::FocusDeform, true},
    {"Skinning & Weight Painting", "H(0.7,V(0.78,H(0.22,L(2:0),L(0:0)),L(4,5:0)),L(15,3:0))", Editor::FocusObject | Editor::FocusRig, true},
    {"Animation", "H(0.74,V(0.6,H(0.5,L(0:0),L(1:0)),L(16,17,4,5:0)),L(3,15,2:0))", Editor::FocusObject | Editor::FocusRig | Editor::FocusLight, true},
    /* The arrangements: every tool. */
    {"2 by 3", "H(0.55,V(0.5,L(0,6,10:0),L(1,9:0)),H(0.4,L(2:0),H(0.5,L(4,11,5,8:0),L(3,12:0))))", Editor::FocusAll, false},
    {"Tall", "H(0.76,H(0.62,L(0,1,6,10:0),V(0.5,L(2:0),L(4,11,5,8,9:0))),L(3,12:0))", Editor::FocusAll, false},
    {"Wide", "V(0.7,H(0.8,L(0,1,6,10:0),L(3,12:0)),H(0.3,L(2:0),L(4,11,5,8,9:0)))", Editor::FocusAll, false},
    {"Learning", "H(0.40,L(6:0),V(0.64,L(0,1,10:0),H(0.45,L(2:0),L(3,12,4,11,5,8,9:0))))", Editor::FocusAll, false},
};

}  // namespace

const Editor::Workspace *Editor::builtin_workspaces(int &count) {
  count = (int)(sizeof(kWorkspaces) / sizeof(kWorkspaces[0]));
  return kWorkspaces;
}

const Editor::Workspace *Editor::find_workspace(const std::string &name) const {
  int n = 0;
  const Workspace *w = builtin_workspaces(n);
  for (int i = 0; i < n; i++)
    if (to_lower(name) == to_lower(w[i].name)) return &w[i];
  for (const UserLayout &l : user_layouts_)
    if (to_lower(name) == to_lower(l.name)) return &l.ws;
  return nullptr;
}

/* The names the Layout dropdown and Window > Layouts show, in order. */
std::vector<std::string> Editor::workspace_names() const {
  std::vector<std::string> out;
  int n = 0;
  const Workspace *w = builtin_workspaces(n);
  for (int i = 0; i < n; i++) out.push_back(w[i].name);
  for (const UserLayout &l : user_layouts_) out.push_back(l.name);
  return out;
}

bool Editor::apply_workspace(const std::string &name) {
  const Workspace *w = find_workspace(name);
  if (!w) return false;
  if (active_window_ != 0) {  // (another window is drawing: a layout arranges the main window, task 0187)
    Log::warn("Layouts arrange the main window: choose one there");
    return false;
  }
  dock_reset(w->dock);
  focus_ = w->focus;
  workspace_ = w->name;
  ui_.redraw = true;
  return true;
}

bool Editor::shows(uint32_t groups) const { return show_all_tools_ || (focus_ & groups) != 0; }

bool Editor::save_user_layout(const std::string &raw) {
  std::string name;
  for (char c : raw)
    if (c != ';' && c != '\n' && c != '\r' && c != '=') name += c;  // (the preferences line's separators)
  while (!name.empty() && name.front() == ' ') name.erase(name.begin());
  while (!name.empty() && name.back() == ' ') name.pop_back();
  int n = 0;
  const Workspace *w = builtin_workspaces(n);
  for (int i = 0; i < n; i++)
    if (to_lower(name) == to_lower(w[i].name)) {
      Log::warn("Save Layout: '%s' is a built-in layout's name", name.c_str());
      return false;
    }
  if (name.empty()) return false;
  UserLayout l;
  l.name = name;
  l.dock = dock_serialize(dock_.get());
  l.ws = {nullptr, nullptr, focus_, true};
  user_layouts_.erase(std::remove_if(user_layouts_.begin(), user_layouts_.end(), [&](const UserLayout &u) { return to_lower(u.name) == to_lower(name); }),
                      user_layouts_.end());
  user_layouts_.push_back(l);
  fix_user_layout_pointers();
  workspace_ = name;
  save_prefs();
  Log::info("Layout '%s' saved", name.c_str());
  return true;
}

bool Editor::delete_user_layout(const std::string &name) {
  const size_t before = user_layouts_.size();
  user_layouts_.erase(std::remove_if(user_layouts_.begin(), user_layouts_.end(), [&](const UserLayout &u) { return to_lower(u.name) == to_lower(name); }),
                      user_layouts_.end());
  if (user_layouts_.size() == before) {
    Log::warn("Delete Layout: no layout of yours named '%s'", name.c_str());
    return false;
  }
  fix_user_layout_pointers();
  save_prefs();
  return true;
}

void Editor::fix_user_layout_pointers() {  // (the Workspace inside points at the strings beside it)
  for (UserLayout &l : user_layouts_) l.ws.name = l.name.c_str(), l.ws.dock = l.dock.c_str();
}

/* The Layout dropdown's own menu items (Unity's): Save Layout..., Delete Layout..., Show All Tools. */
void Editor::draw_layout_dialogs() {
  auto &u = ui_;
  const ui::Id sid = u.id("layout_save");
  if (layout_save_open_) {
    if (!u.popup_open(sid)) u.open_popup(sid, {fb_.width / 2 - u.px(160), u.px(80), 0, 0});
    u.popup(sid, u.px(320), [this] {
      auto &u = ui_;
      Recti t = u.popup_row(u.row_h() + u.px(6));
      u.label({t.x + u.px(10), t.y, t.w - u.px(20), t.h}, "Save Layout", u.theme.text_bright);
      Recti f = u.popup_row(u.row_h() + u.px(6));
      const ui::Id fid = u.id("layout_save_name");
      if (!u.editing(fid) && layout_save_name_.empty()) u.begin_edit(fid, "", false);
      bool done = false;
      u.text_field(fid, f.shrink(u.px(4)), layout_save_name_, &done, "Name");
      Recti b = u.popup_row(u.row_h() + u.px(10));
      if (u.button({b.right() - u.px(180), b.y + u.px(4), u.px(80), b.h - u.px(8)}, "Save") || (done && !layout_save_name_.empty())) {
        save_user_layout(layout_save_name_);
        layout_save_name_.clear();
        layout_save_open_ = false;
        u.close_popups();
      }
      if (u.button({b.right() - u.px(92), b.y + u.px(4), u.px(80), b.h - u.px(8)}, "Cancel")) {
        layout_save_open_ = false;
        u.close_popups();
      }
    });
    if (!u.popup_open(sid)) layout_save_open_ = false;
  }
  const ui::Id did = u.id("layout_delete");
  if (layout_delete_open_) {
    if (!u.popup_open(did)) u.open_popup(did, {fb_.width / 2 - u.px(160), u.px(80), 0, 0});
    u.popup(did, u.px(320), [this] {
      auto &u = ui_;
      Recti t = u.popup_row(u.row_h() + u.px(6));
      u.label({t.x + u.px(10), t.y, t.w - u.px(20), t.h}, user_layouts_.empty() ? "No layouts of yours to delete" : "Delete Layout", u.theme.text_bright);
      std::string gone;
      for (const UserLayout &l : user_layouts_) {
        Recti r = u.popup_row(u.row_h() + u.px(4));
        u.label({r.x + u.px(10), r.y, r.w - u.px(110), r.h}, l.name);
        if (u.button({r.right() - u.px(90), r.y + u.px(2), u.px(80), r.h - u.px(4)}, "Delete " + l.name)) gone = l.name;
      }
      if (!gone.empty()) delete_user_layout(gone);
      Recti b = u.popup_row(u.row_h() + u.px(10));
      if (u.button({b.right() - u.px(92), b.y + u.px(4), u.px(80), b.h - u.px(8)}, "Close")) {
        layout_delete_open_ = false;
        u.close_popups();
      }
    });
    if (!u.popup_open(did)) layout_delete_open_ = false;
  }
}

/* A picture to model from (Blender: Add > Image > Reference): an unlit quad the image's shape, a metre tall, facing
 * the view asked for (front: seen from the front, along -Z; side: from +X; top: from above); no shadows, not in
 * renders (Display As: Solid in the views only). */
GameObject *Editor::create_reference_image(const std::string &path, const std::string &view) {
  std::string err;
  const std::string abs = resolve_asset_path(path);
  TexturePtr tex = texture_load(abs, true, &err);
  if (!tex || tex->levels.empty()) {
    Log::warn("Reference Image: can't read %s%s", path.c_str(), err.empty() ? "" : (": " + err).c_str());
    return nullptr;
  }
  const float aspect = tex->levels[0].h > 0 ? tex->levels[0].w / (float)tex->levels[0].h : 1.0f;
  std::string name = "Reference " + fs::stem(path);
  for (int k = 1; scene_->find_by_name(name); k++) name = strprintf("Reference %s.%03d", fs::stem(path).c_str(), k);
  GameObject *g = scene_->create(name);
  g->add<MeshFilter>()->mesh = primitives::quad(1.0f);
  MeshRenderer *mr = g->add<MeshRenderer>();
  MaterialPtr m = make_material(name, {1, 1, 1});
  m->unlit = true;
  /* Kept with the project: an image from outside Assets is copied into Assets/Textures/References (a scene that names
   * a path on this machine would lose it elsewhere). */
  std::string src = abs;
  if (make_asset_relative(abs) == abs && !assets_dir_.empty()) {
    const std::string dir = fs::join(assets_dir_, "Textures/References");
    fs::make_dirs(dir);
    std::string dst = fs::join(dir, fs::filename(abs));
    if (!fs::exists(dst) && fs::copy_file(abs, dst)) Log::info("Reference Image: copied %s into Assets/Textures/References", fs::filename(abs).c_str());
    if (fs::exists(dst)) src = dst;
  }
  m->base_map.path = make_asset_relative(src);
  m->cast_shadows = false;
  mr->materials = {m};
  mr->cast_shadows = mr->receive_shadows = false;
  mr->show_in_renders = false;  // a guide, not part of the picture
  g->set_local_scale({aspect, 1.0f, 1.0f});
  const std::string v = to_lower(view);
  /* The quad faces -Z (Unity): front is seen from the front camera, side turned to face +X, top laid flat facing up. */
  if (v == "side") g->set_local_rotation(Quat::axis_angle({0, 1, 0}, -kPi * 0.5f));
  else if (v == "top") g->set_local_rotation(Quat::axis_angle({1, 0, 0}, kPi * 0.5f));
  g->set_world_position(cam_.pivot);
  select(g->id);
  mark_changed("Add Reference Image");
  Log::info("Reference image '%s' (%s view): a guide, kept out of renders", name.c_str(), v.empty() ? "front" : v.c_str());
  return g;
}

}  // namespace bl
