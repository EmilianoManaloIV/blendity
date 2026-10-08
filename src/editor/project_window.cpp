// SPDX-License-Identifier: GPL-2.0-or-later
// The Project window, Unity's way: a folder tree of Assets on the left (each
// folder a drop target), the current folder's files on the right. Assets are
// organised by dragging them onto folders, renamed with F2 (a material's
// file name is its name, both ways) and deleted to the Recycle Bin / Trash
// with Delete. Moving or renaming a material or image keeps every reference
// to it working: the shared material instances, texture paths and the
// .scene files that mention it.
#include "editor.h"

#include <algorithm>

namespace bl {

using ui::Icon;

static std::string clean_name(std::string s) {
  for (char &c : s)
    if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') c = '_';
  while (!s.empty() && (s.back() == ' ' || s.back() == '.')) s.pop_back();
  while (!s.empty() && s.front() == ' ') s.erase(s.begin());
  return s;
}

/* `path` is `dir` or inside it (absolute paths, '/' or '\' separators). */
static bool path_inside(std::string path, std::string dir) {
  std::replace(path.begin(), path.end(), '\\', '/');
  std::replace(dir.begin(), dir.end(), '\\', '/');
  return path == dir || (path.size() > dir.size() && path.compare(0, dir.size(), dir) == 0 && path[dir.size()] == '/');
}

const std::vector<std::string> &Editor::material_asset_paths() {
  if (ui_.time - material_paths_listed_ < 2.0 && material_paths_listed_ > 0) return material_paths_;
  material_paths_listed_ = std::max(1e-3, ui_.time);
  material_paths_.clear();
  std::function<void(const std::string &, int)> walk = [&](const std::string &dir, int depth) {
    if (depth > 12) return;
    for (const DirEntry &de : fs::list(dir)) {
      const std::string p = fs::join(dir, de.name);
      if (de.is_dir) walk(p, depth + 1);
      else if (fs::extension(de.name) == ".mat") material_paths_.push_back(make_asset_relative(p));
    }
  };
  walk(assets_dir_, 0);
  for (std::string &p : material_paths_) std::replace(p.begin(), p.end(), '\\', '/');
  std::sort(material_paths_.begin(), material_paths_.end());
  return material_paths_;
}

void Editor::invalidate_project_listing() {
  project_listed_ = -100;
  material_paths_listed_ = -100;
}

/* Every reference to `from` (a file or a folder, project-relative) now points at `to`. */
void Editor::retarget_asset_references(const std::string &from, const std::string &to) {
  retarget_material_assets(from, to);
  auto fix = [&](Material &m) {
    for (TextureRef *t : {&m.base_map, &m.metallic_map, &m.roughness_map, &m.normal_map, &m.emission_map}) {
      std::string p = t->path;
      std::replace(p.begin(), p.end(), '\\', '/');
      if (p == from || (p.size() > from.size() && p.compare(0, from.size(), from) == 0 && p[from.size()] == '/')) {
        t->path = to + p.substr(from.size());
        m.touch();
      }
    }
  };
  for (const MaterialPtr &m : scene_materials()) fix(*m);
  for (auto &kv : loaded_material_assets()) fix(*kv.second);
  invalidate_material_textures();
  const size_t files = retarget_scene_files(assets_dir_, from, to);
  if (files) Log::info("Updated %zu scene file(s) that referred to %s", files, from.c_str());
  save_dirty_material_assets();
}

bool Editor::move_project_entry(const std::string &from, const std::string &to_dir, const std::string &new_name) {
  if (from.empty() || !fs::exists(from)) return false;
  const std::string name = new_name.empty() ? fs::filename(from) : new_name;
  const std::string to = fs::join(to_dir, name);
  if (to == from || fs::normalize(to) == fs::normalize(from)) return false;
  if (fs::is_dir(from) && path_inside(fs::normalize(to_dir), fs::normalize(from))) {
    Log::warn("Can't move a folder into itself");
    return false;
  }
  if (!path_inside(fs::normalize(to_dir), fs::normalize(project_root_))) {
    Log::warn("Assets stay inside the project");
    return false;
  }
  if (fs::exists(to)) {
    Log::warn("'%s' already exists there", name.c_str());
    return false;
  }
  save_dirty_material_assets();  // the file on disk is up to date before it moves
  if (!fs::move(from, to)) {
    Log::error("Could not move %s", from.c_str());
    return false;
  }
  std::string rf = make_asset_relative(from), rt = make_asset_relative(to);
  std::replace(rf.begin(), rf.end(), '\\', '/');
  std::replace(rt.begin(), rt.end(), '\\', '/');
  retarget_asset_references(rf, rt);
  if (path_inside(project_selected_, from)) project_selected_ = to + project_selected_.substr(from.size());
  if (path_inside(project_dir_, from)) project_dir_ = to + project_dir_.substr(from.size());
  invalidate_project_listing();
  Log::info("%s -> %s", rf.c_str(), rt.c_str());
  return true;
}

bool Editor::rename_project_entry(const std::string &path, const std::string &new_name_in) {
  std::string base = clean_name(new_name_in);
  if (base.empty()) return false;
  const bool dir = fs::is_dir(path);
  const std::string ext = dir ? "" : fs::extension(path);
  /* Typing the extension again is fine; leaving it off keeps it (Unity hides it). */
  if (!ext.empty() && to_lower(fs::extension(base)) == ext) base = base.substr(0, base.size() - ext.size());
  const std::string name = base + (dir ? "" : fs::filename(path).substr(fs::filename(path).size() - ext.size()));
  if (name == fs::filename(path)) return true;
  return move_project_entry(path, fs::parent(path), name);
}

std::string Editor::create_project_folder(const std::string &dir_in) {
  const std::string dir = dir_in.empty() || !path_inside(fs::normalize(dir_in), fs::normalize(assets_dir_)) ? assets_dir_ : dir_in;
  std::string p = fs::join(dir, "New Folder");
  for (int i = 1; fs::exists(p); i++) p = fs::join(dir, strprintf("New Folder %d", i));
  if (!fs::make_dirs(p)) {
    Log::error("Could not create %s", p.c_str());
    return std::string();
  }
  invalidate_project_listing();
  return p;
}

bool Editor::delete_project_entry(const std::string &path) {
  if (path.empty() || !fs::exists(path)) return false;
  /* Only what the Project window shows: inside Assets, the research papers or the screenshots (not those folders themselves). */
  const std::string np = fs::normalize(path);
  bool deletable = false;
  for (const std::string &root : {assets_dir_, papers_dir_, screenshots_dir_})
    deletable = deletable || (path_inside(np, fs::normalize(root)) && np != fs::normalize(root));
  if (!deletable) {
    Log::warn("Only things inside Assets, research papers or screenshots can be deleted here");
    return false;
  }
  save_dirty_material_assets();
  std::string rel = make_asset_relative(path);
  std::replace(rel.begin(), rel.end(), '\\', '/');
  std::string err;
  if (!fs::move_to_trash(path, &err)) {
    Log::error("Could not delete %s: %s", rel.c_str(), err.c_str());
    return false;
  }
  /* Materials from it stay on the objects that use them, as scene materials. */
  const size_t n = forget_material_assets(rel);
  if (path_inside(project_selected_, path)) project_selected_.clear();
  if (path_inside(project_dir_, path)) project_dir_ = fs::parent(path);
  invalidate_project_listing();
  Log::info("Deleted %s (it is in the %s)%s", rel.c_str(),
#ifdef _WIN32
            "Recycle Bin",
#else
            "Trash",
#endif
            n ? strprintf("; %zu material(s) stay on their objects as scene materials", n).c_str() : "");
  return true;
}

/* Unity: renaming a material renames its file (and the other way round). */
void Editor::sync_material_asset_names() {
  if (ui_.any_editing() || playing_) return;  // not while the name is being typed
  for (auto &kv : loaded_material_assets()) {
    const MaterialPtr &m = kv.second;
    if (!m || m->asset_path.empty() || m->name.empty()) continue;
    if (m->name == fs::stem(kv.first)) continue;
    const std::string abs = resolve_asset_path(kv.first);
    if (!fs::exists(abs)) continue;
    std::string want = clean_name(m->name);
    if (want.empty()) {
      m->name = fs::stem(kv.first);
      continue;
    }
    std::string target = fs::join(fs::parent(abs), want + ".mat");
    for (int i = 1; fs::exists(target); i++) target = fs::join(fs::parent(abs), strprintf("%s %d.mat", want.c_str(), i));
    if (!move_project_entry(abs, fs::parent(abs), fs::filename(target))) m->name = fs::stem(kv.first);
    return;  // the library changed; the rest next frame
  }
}

void Editor::draw_project(const Recti &r) {
  auto &u = ui_;
  auto &in = u.in;
  if (u.hovered(r) && (in.pressed[0] || in.pressed[1])) focused_ = WindowKind::Project;
  if (u.time - project_listed_ > 2.0) {
    project_entries_ = fs::list(project_dir_);
    project_listed_ = u.time;
  }
  const bool dragging_asset = asset_drag_.pending && asset_drag_.dragging;
  auto folder_target = [&](const Recti &rr, const std::string &dir) {
    drop_folders_.push_back({rr, dir});
    if (dragging_asset && rr.contains(in.mx, in.my)) u.canvas.rect_outline(rr, Color::hex(0x3A79BB), u.px(2));
  };
  int bh = u.row_h() + u.px(6);
  Recti bar{r.x, r.y, r.w, bh};
  u.canvas.fill_rect(bar, Color::hex(0x2F2F2F));
  /* Unity's "+" (Create) menu. */
  Recti plus{bar.x + u.px(4), bar.y + u.px(3), u.px(44), bh - u.px(6)};
  if (u.button(plus, "", false, Icon::Plus)) u.open_popup(u.id("proj_create"), plus);
  u.tooltip("Create a folder or a material in this folder (Unity: Assets > Create).");
  /* Breadcrumb relative to the project root; each part is a drop target too. */
  std::string rel = project_dir_.size() > project_root_.size() ? project_dir_.substr(project_root_.size() + 1) : "";
  std::replace(rel.begin(), rel.end(), '\\', '/');
  int x = plus.right() + u.px(8);
  std::string acc = project_root_;
  std::vector<std::string> parts;
  size_t s = 0, e;
  while ((e = rel.find('/', s)) != std::string::npos) {
    parts.push_back(rel.substr(s, e - s));
    s = e + 1;
  }
  if (s < rel.size()) parts.push_back(rel.substr(s));
  for (size_t i = 0; i < parts.size(); i++) {
    acc = fs::join(acc, parts[i]);
    int w = u.font.text_width(parts[i]) + u.px(10);
    Recti pr{x, bar.y, w, bar.h};
    bool hot = u.hovered(pr);
    u.label(pr, parts[i], hot ? u.theme.text_bright : u.theme.text, ui::Align::Center);
    if (hot && in.pressed[0]) {
      project_dir_ = acc;
      project_listed_ = -100;
    }
    if (path_inside(acc, assets_dir_)) folder_target(pr, acc);
    x += w;
    if (i + 1 < parts.size()) {
      u.draw_icon(Icon::ArrowRight, {x, bar.y + bh / 2 - u.px(4), u.px(8), u.px(8)}, u.theme.text_dim);
      x += u.px(10);
    }
  }
  int sw = std::min(u.px(200), r.w / 3);
  u.text_field(u.id("proj_search"), {bar.right() - sw - u.px(6), bar.y + u.px(4), sw, bh - u.px(8)}, project_search_, nullptr, "Search");

  /* Left: favourites, then the folder tree of Assets. */
  int lw = std::min(u.px(200), r.w / 3);
  Recti left{r.x, bar.bottom(), lw, r.h - bh};
  Recti right{r.x + lw + 1, bar.bottom(), r.w - lw - 1, r.h - bh};
  u.canvas.fill_rect(left, Color::hex(0x333333));
  u.canvas.vline(left.right(), left.y, left.bottom(), u.theme.border);
  int rh = u.row_h();
  struct Fav {
    const char *label;
    std::string path;
    Icon icon;
  };
  Fav favs[] = {{"Assets", assets_dir_, Icon::Folder},
                {"Scenes", fs::join(assets_dir_, "Scenes"), Icon::Scene},
                {"Research Papers", papers_dir_, Icon::Paper},
                {"Screenshots", screenshots_dir_, Icon::Folder}};
  ui::Id tid = u.id("proj_tree");
  static int tree_h = 0;
  const int toff = u.begin_scroll(tid, left, tree_h);
  int y = left.y + u.px(4) - toff;
  const int a = u.font.line_height() - u.px(3);
  for (auto &f : favs) {
    Recti fr{left.x, y, left.w, rh};
    bool cur = project_dir_ == f.path;
    bool hot = u.hovered(fr);
    if (cur) u.canvas.fill_rect(fr, u.theme.selection_dim);
    else if (hot) u.canvas.fill_rect(fr, Color::hex(0x404040));
    u.draw_icon(f.icon, {fr.x + u.px(10), fr.y + (rh - a) / 2, a, a}, f.icon == Icon::Paper ? u.theme.accent : u.theme.text);
    u.label({fr.x + u.px(16) + a, fr.y, fr.w - a - u.px(20), rh}, f.label);
    if (hot && in.pressed[0]) {
      fs::make_dirs(f.path);
      project_dir_ = f.path;
      project_listed_ = -100;
    }
    if (path_inside(f.path, assets_dir_)) folder_target(fr, f.path);
    y += rh;
  }
  y += u.px(6);
  u.label({left.x + u.px(10), y, left.w, rh}, "Folders", u.theme.text_dim);
  y += rh;
  /* Nested folders, each with an arrow to fold it (Unity's two-column layout). */
  std::function<void(const std::string &, int)> tree = [&](const std::string &dir, int depth) {
    if (depth > 10) return;
    for (const DirEntry &d : fs::list(dir)) {
      if (!d.is_dir) continue;
      const std::string p = fs::join(dir, d.name);
      Recti fr{left.x, y, left.w, rh};
      y += rh;
      const int ind = u.px(10) + depth * u.px(12);
      bool has_sub = false;
      for (const DirEntry &c : fs::list(p)) has_sub = has_sub || c.is_dir;
      bool &open = foldouts_.emplace("proj_" + p, false).first->second;
      if (fr.bottom() >= left.y && fr.y <= left.bottom()) {
        const bool hot = u.hovered(fr);
        if (project_dir_ == p) u.canvas.fill_rect(fr, u.theme.selection_dim);
        else if (hot) u.canvas.fill_rect(fr, Color::hex(0x404040));
        Recti arrow{fr.x + ind, fr.y + (rh - a) / 2, a, a};
        if (has_sub) {
          u.draw_icon(open ? Icon::ArrowDown : Icon::ArrowRight, arrow, u.theme.text_dim);
          if (u.hovered(arrow) && in.pressed[0]) {
            open = !open;
            u.consume_click();
          }
        }
        u.draw_icon(Icon::Folder, {arrow.right() + u.px(2), fr.y + (rh - a) / 2, a, a}, u.theme.text);
        u.label({arrow.right() + a + u.px(8), fr.y, fr.right() - arrow.right() - a - u.px(10), rh}, d.name);
        if (hot && in.pressed[0]) {
          project_dir_ = p;
          project_listed_ = -100;
        }
        if (hot && in.pressed[1]) {
          project_selected_ = p;
          u.open_popup(u.id("proj_ctx"), {in.mx, in.my, 0, 0});
        }
        folder_target(fr.intersect(left), p);
      }
      if (open || (path_inside(project_dir_, p) && project_dir_ != p)) tree(p, depth + 1);
    }
  };
  tree(assets_dir_, 0);
  tree_h = y + toff - left.y + rh;
  u.end_scroll();

  /* Right: the folder's files */
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
  bool row_clicked = false;
  if (can_up) {
    Recti rr = row_rect(i++);
    bool hot = u.hovered(rr);
    if (hot) u.canvas.fill_rect(rr, Color::hex(0x444444));
    u.label({rr.x + u.px(10), rr.y, rr.w, rr.h}, "..", u.theme.text_dim);
    if (hot && in.double_clicked[0]) {
      project_dir_ = fs::parent(project_dir_);
      project_listed_ = -100;
    }
    if (path_inside(fs::parent(project_dir_), assets_dir_)) folder_target(rr.intersect(right), fs::parent(project_dir_));
    row_clicked = row_clicked || (hot && (in.pressed[0] || in.pressed[1]));
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
    Icon ic = en->is_dir ? Icon::Folder
              : ext == ".mat" ? Icon::None
              : ext == ".scene" ? Icon::Scene
              : model_extension_supported(ext) ? Icon::Mesh
              : ext == ".pdf" ? Icon::Paper
              : image_extension_supported(ext) ? Icon::Eye
                                               : Icon::File;
    int ia = u.font.line_height() - u.px(2);
    u.draw_icon(ic, {rr.x + u.px(10), rr.y + (rh - ia) / 2, ia, ia}, ext == ".pdf" ? u.theme.accent : u.theme.text);
    if (ext == ".mat") {
      /* A material asset: its colour as a swatch, like Unity's preview ball. */
      if (MaterialPtr mm = material_asset(make_asset_relative(full))) {
        const int sw2 = u.font.line_height() - u.px(4);
        const Vec3 c = mm->base_color;
        u.canvas.fill_circle((float)(rr.x + u.px(10) + sw2 / 2), (float)(rr.y + rh / 2), sw2 * 0.5f,
                             Color::from(Vec3(linear_to_srgb(c.x), linear_to_srgb(c.y), linear_to_srgb(c.z))));
      }
    }
    const Recti name_r{rr.x + u.px(16) + ia, rr.y, rr.w / 2, rh};
    if (project_rename_ == full) {
      /* F2 / Rename: the name in place (the extension stays hidden for files, as in Unity). */
      ui::Id rid = u.id("proj_rename");
      if (!u.editing(rid)) u.begin_edit(rid, project_rename_buf_, true);
      bool done = false;
      u.text_field(rid, {name_r.x, rr.y + 1, std::max(u.px(120), rr.w / 2), rh - 2}, project_rename_buf_, &done);
      if (done || !u.editing(rid)) {
        if (done && !project_rename_buf_.empty()) rename_project_entry(full, project_rename_buf_);
        project_rename_.clear();
      }
    }
    else u.label(name_r, en->name);
    if (!en->is_dir) u.label({rr.x, rr.y, rr.w - u.px(12), rh}, format_bytes(en->size), u.theme.text_dim, ui::Align::Right);
    if (en->is_dir) folder_target(rr.intersect(right), full);
    if (hot && in.pressed[0] && project_rename_ != full) {
      project_selected_ = full;
      clear_selection();
      /* Anything drags onto folders; materials and images onto objects, faces and slots too (Unity). */
      asset_drag_ = {true, false, full, nullptr, in.mx, in.my};
    }
    if (hot && in.double_clicked[0]) {
      asset_drag_ = AssetDrag{};
      if (en->is_dir) {
        project_dir_ = full;
        project_listed_ = -100;
      }
      else if (ext == ".scene") open_scene(full);
      else if (model_extension_supported(ext)) import_model_file(full, false);
      else if (ext != ".mat") fs::open_external(full);
    }
    if (hot && in.pressed[1]) {
      project_selected_ = full;
      u.open_popup(u.id("proj_ctx"), {in.mx, in.my, 0, 0});
    }
    row_clicked = row_clicked || (hot && (in.pressed[0] || in.pressed[1]));
  }
  if (shown.empty()) {
    bool papers = project_dir_ == papers_dir_;
    u.label({right.x, right.y + rh, right.w, rh},
            papers ? "Drop research papers (PDF, TXT, MD...) onto this window." : "This folder is empty. Right-click or + to create a folder or material.",
            u.theme.text_dim, ui::Align::Center);
  }
  u.end_scroll();
  /* Empty space: the current folder is the drop target, and right-click creates. */
  if (path_inside(project_dir_, assets_dir_) && !row_clicked) {
    const Recti rest{right.x, right.y + std::max(0, rows * rh - off), right.w, std::max(0, right.h - std::max(0, rows * rh - off))};
    if (rest.h > 0) drop_folders_.push_back({rest, project_dir_});
    if (u.hovered(right) && in.pressed[1]) {
      project_selected_.clear();
      u.open_popup(u.id("proj_create"), {in.mx, in.my, 0, 0});
    }
    if (u.hovered(right) && in.pressed[0] && !row_clicked) project_selected_.clear();
  }
  /* Keys while the Project window has focus: F2 renames, Delete deletes (after asking). */
  if (focused_ == WindowKind::Project && !u.any_editing() && !project_selected_.empty() && fs::exists(project_selected_)) {
    if (in.key_pressed[platform::KEY_F2]) {
      project_rename_ = project_selected_;
      project_rename_buf_ = fs::is_dir(project_selected_) ? fs::filename(project_selected_) : fs::stem(project_selected_);
      in.key_pressed[platform::KEY_F2] = false;
    }
    if (in.key_pressed[platform::KEY_DELETE] || in.key_pressed[platform::KEY_BACKSPACE]) {
      project_delete_ = project_selected_;
      in.key_pressed[platform::KEY_DELETE] = in.key_pressed[platform::KEY_BACKSPACE] = false;
    }
  }
  const std::string dir_now = path_inside(project_dir_, assets_dir_) ? project_dir_ : assets_dir_;
  u.popup(u.id("proj_create"), u.px(220), [this, dir_now] {
    auto &u = ui_;
    if (u.menu_item("Folder", nullptr, false, true, Icon::Folder)) {
      const std::string p = create_project_folder(dir_now);
      if (!p.empty()) {
        project_selected_ = p;
        project_rename_ = p;
        project_rename_buf_ = fs::filename(p);
      }
    }
    if (u.menu_item("Material", nullptr, false, true, Icon::Plus)) {
      if (MaterialPtr m = new_material_asset(nullptr, false, dir_now)) {
        project_selected_ = resolve_asset_path(m->asset_path);
        project_rename_ = project_selected_;
        project_rename_buf_ = m->name;
        material_selected_ = m;
      }
    }
    u.tooltip("A new Material asset here. Drag it onto objects, faces or Inspector slots (Unity: Create > Material).");
  });
  std::string sel = project_selected_;
  u.popup(u.id("proj_ctx"), u.px(230), [this, sel, dir_now] {
    auto &u = ui_;
    std::string ext = fs::extension(sel);
    const bool in_assets = path_inside(sel, assets_dir_) && sel != assets_dir_;
    if (ext == ".scene" && u.menu_item("Open Scene")) open_scene(sel);
    if (model_extension_supported(ext) && u.menu_item("Import into Scene")) import_obj_file(sel);
    if (ext == ".mat" && u.menu_item("Assign to Selection", nullptr, false, !selection_.empty()))
      if (MaterialPtr m = material_asset(make_asset_relative(sel)))
        for (GameObject *g : selected_objects(false)) assign_material(g, 0, m);
    if (ext == ".mat" && u.menu_item("Edit in Materials Window"))
      if (MaterialPtr m = material_asset(make_asset_relative(sel))) {
        material_selected_ = m;
        dock_open(WindowKind::Materials);
      }
    u.menu_separator();
    if (u.menu_item("Create Folder", nullptr, false, true, Icon::Folder)) {
      const std::string p = create_project_folder(fs::is_dir(sel) && in_assets ? sel : dir_now);
      if (!p.empty()) {
        project_selected_ = p;
        project_rename_ = p;
        project_rename_buf_ = fs::filename(p);
      }
    }
    if (u.menu_item("Create Material", nullptr, false, true, Icon::Plus)) new_material_asset(nullptr, false, fs::is_dir(sel) && in_assets ? sel : dir_now);
    u.menu_separator();
    if (u.menu_item("Rename", "F2", false, in_assets)) {
      project_rename_ = sel;
      project_rename_buf_ = fs::is_dir(sel) ? fs::filename(sel) : fs::stem(sel);
    }
    if (u.menu_item("Delete", "Del", false, in_assets)) project_delete_ = sel;
    u.menu_separator();
    if (u.menu_item("Open with system viewer")) fs::open_external(sel);
    if (u.menu_item("Show in Explorer / Finder")) fs::open_external(fs::is_dir(sel) ? sel : fs::parent(sel));
    if (u.menu_item("Refresh")) invalidate_project_listing();
  });
  /* Delete asks first (Unity: "Delete selected asset?"); it goes to the Recycle Bin / Trash. */
  if (!project_delete_.empty()) {
    ui::Id did = u.id("proj_delete");
    if (!u.popup_open(did)) u.open_popup(did, {r.x + r.w / 2 - u.px(170), r.y + r.h / 3, 0, 0});
    std::string target = project_delete_;
    u.popup(did, u.px(340), [this, target] {
      auto &u = ui_;
      std::string rel = make_asset_relative(target);
      size_t users = 0;
      if (fs::extension(target) == ".mat")
        if (MaterialPtr m = material_asset(rel))
          scene_->for_each([&](GameObject &g) {
            if (auto *mr = g.get<MeshRenderer>())
              for (auto &s : mr->materials) users += s == m;
          });
      Recti t = u.popup_row(u.row_h() + u.px(6));
      u.label({t.x + u.px(10), t.y, t.w - u.px(20), t.h}, "Delete " + fs::filename(target) + (fs::is_dir(target) ? " and everything in it?" : "?"),
              u.theme.text_bright);
      Recti h1 = u.popup_row();
#ifdef _WIN32
      u.label({h1.x + u.px(10), h1.y, h1.w - u.px(20), h1.h}, "It goes to the Recycle Bin, where you can restore it.", u.theme.text_dim);
#else
      u.label({h1.x + u.px(10), h1.y, h1.w - u.px(20), h1.h}, "It goes to the Trash, where you can restore it.", u.theme.text_dim);
#endif
      if (users) {
        Recti h2 = u.popup_row();
        u.label({h2.x + u.px(10), h2.y, h2.w - u.px(20), h2.h}, strprintf("%zu slot(s) use it; they keep it as a scene material.", users), u.theme.text_dim);
      }
      Recti b = u.popup_row(u.row_h() + u.px(10));
      if (u.button({b.right() - u.px(180), b.y + u.px(4), u.px(80), b.h - u.px(8)}, "Delete")) {
        delete_project_entry(target);
        project_delete_.clear();
        u.close_popups();
      }
      if (u.button({b.right() - u.px(92), b.y + u.px(4), u.px(80), b.h - u.px(8)}, "Cancel")) {
        project_delete_.clear();
        u.close_popups();
      }
    });
    if (!u.popup_open(did)) project_delete_.clear();  // clicked away
  }
}

}  // namespace bl
