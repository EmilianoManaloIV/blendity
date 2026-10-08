// SPDX-License-Identifier: GPL-2.0-or-later
// Materials as assets, the Unity way: a material is a file in the Project
// window (Assets/Materials/*.mat) shared by every renderer that uses it, and
// you drag it onto an object in the Scene view or the Hierarchy, onto one
// face in Edit Mode, or onto a slot in the Inspector. Images drag onto a
// material's texture fields the same way. Blender's equivalents: material
// datablocks, Ctrl+L > Link Materials, the Assign button and Remove Unused
// Slots (editors/render/render_shading.cc).
#include "editor.h"

#include <algorithm>

namespace bl {

static std::string unique_path(const std::string &dir, const std::string &base, const std::string &ext) {
  std::string clean = base.empty() ? "Material" : base;
  for (char &c : clean)
    if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') c = '_';
  std::string p = fs::join(dir, clean + ext);
  for (int i = 1; fs::exists(p); i++) p = fs::join(dir, strprintf("%s %d%s", clean.c_str(), i, ext.c_str()));
  return p;
}

MaterialPtr Editor::new_material_asset(const MaterialPtr &src, bool replace_in_scene, const std::string &dir_in) {
  const std::string dir = dir_in.empty() ? fs::join(assets_dir_, "Materials") : dir_in;
  fs::make_dirs(dir);
  const Material base = src ? *src : *make_material("New Material", Vec3(0.8f));
  const std::string abs = unique_path(dir, base.name == "Material" ? "New Material" : base.name, ".mat");
  MaterialPtr m = create_material_asset(base, make_asset_relative(abs));
  if (!m) {
    Log::error("Could not write %s", abs.c_str());
    return nullptr;
  }
  m->name = fs::stem(abs);
  m->touch();
  save_dirty_material_assets();
  if (replace_in_scene && src) {
    /* "Save as Asset": every slot that used the scene material now uses the asset. */
    scene_->for_each([&](GameObject &g) {
      if (auto *mr = g.get<MeshRenderer>())
        for (auto &s : mr->materials)
          if (s == src) s = m;
    });
    mark_changed("Save Material as Asset");
  }
  invalidate_project_listing();
  Log::info("Material asset: %s", make_asset_relative(abs).c_str());
  return m;
}

/* The Inspector's New Material: an asset in Assets/Materials from the start (Unity),
 * a copy of the slot's material or of a preset. */
MaterialPtr Editor::new_slot_material(const MaterialPtr &cur, const std::string &preset) {
  MaterialPtr m = !preset.empty() ? make_material_preset(preset)
                  : cur           ? std::make_shared<Material>(*cur)
                                  : make_material("New Material", Vec3(0.8f));
  m->name = !preset.empty() ? preset : cur ? cur->name + " Copy" : "New Material";
  m->asset_path.clear();
  MaterialPtr a = new_material_asset(m, false);
  return a ? a : m;
}

void Editor::assign_material(GameObject *g, int slot, const MaterialPtr &m) {
  if (!g || slot < 0) return;
  auto *mr = g->get<MeshRenderer>();
  if (!mr) {
    if (!g->get<MeshFilter>()) return;
    mr = g->add<MeshRenderer>();
  }
  if ((int)mr->materials.size() <= slot) mr->materials.resize((size_t)slot + 1);
  mr->materials[(size_t)slot] = m;
  mark_changed("Assign Material");
  Log::info("'%s' slot %d: %s", g->name.c_str(), slot, m ? m->name.c_str() : "None");
}

void Editor::assign_material_to_face(GameObject *g, uint32_t face, const MaterialPtr &m) {
  auto *mf = g ? g->get<MeshFilter>() : nullptr;
  if (!mf || !mf->mesh || face >= mf->mesh->face_count()) return;
  auto *mr = g->get<MeshRenderer>();
  if (!mr) mr = g->add<MeshRenderer>();
  if (mr->materials.empty()) mr->materials.push_back(nullptr);  // slot 0 stays what the rest of the mesh uses
  int slot = -1;
  for (size_t i = 0; i < mr->materials.size(); i++)
    if (mr->materials[i] == m) slot = (int)i;
  if (slot < 0) {
    mr->materials.push_back(m);
    slot = (int)mr->materials.size() - 1;
  }
  Mesh &mesh = *mesh_make_mutable(mf->mesh);
  /* In Edit Mode a drop on a selected face assigns to all selected faces (Blender's Assign). */
  std::vector<uint32_t> faces = {face};
  if (edit_mode_ && g->id == edit_obj_ && face < face_sel_.size() && face_sel_[face]) {
    faces.clear();
    for (size_t f = 0; f < face_sel_.size() && f < mesh.face_count(); f++)
      if (face_sel_[f]) faces.push_back((uint32_t)f);
  }
  mesh.face_material.resize(mesh.face_count(), 0);
  for (uint32_t f : faces) mesh.face_material[f] = slot;
  mesh.touch();
  mark_changed("Assign Material to Faces");
  Log::info("%zu face(s) of '%s': %s (slot %d)", faces.size(), g->name.c_str(), m ? m->name.c_str() : "None", slot);
}

void Editor::remove_material_slot(GameObject *g, int slot) {
  auto *mr = g ? g->get<MeshRenderer>() : nullptr;
  if (!mr || slot < 0 || slot >= (int)mr->materials.size()) return;
  mr->materials.erase(mr->materials.begin() + slot);
  /* Faces on the removed slot move to the one before it (Blender does the same); later slots shift down. */
  if (auto *mf = g->get<MeshFilter>())
    if (mf->mesh && !mf->mesh->face_material.empty()) {
      Mesh &m = *mesh_make_mutable(mf->mesh);
      for (int32_t &s : m.face_material)
        if (s >= slot) s = std::max(0, s - 1);
      m.touch();
    }
  mark_changed("Remove Material Slot");
}

size_t Editor::remove_unused_material_slots(GameObject *g) {
  auto *mr = g ? g->get<MeshRenderer>() : nullptr;
  auto *mf = g ? g->get<MeshFilter>() : nullptr;
  if (!mr || !mf || !mf->mesh) return 0;
  std::vector<uint8_t> used(mr->materials.size(), 0);
  for (size_t f = 0; f < mf->mesh->face_count(); f++) {
    const int s = mf->mesh->material_of(f);
    if (s >= 0 && s < (int)used.size()) used[(size_t)s] = 1;
  }
  size_t n = 0;
  for (int s = (int)used.size() - 1; s >= 0; s--)
    if (!used[(size_t)s] && mr->materials.size() > 1) {
      remove_material_slot(g, s);
      n++;
    }
  Log::info("Removed %zu unused material slot(s) from '%s'", n, g->name.c_str());
  return n;
}

/* Called each frame after the windows have drawn (and registered their drop targets). */
void Editor::update_asset_drag() {
  auto &in = ui_.in;
  AssetDrag &d = asset_drag_;
  if (!d.pending) return;
  if (!d.dragging && (std::abs(in.mx - d.x) > ui_.px(6) || std::abs(in.my - d.y) > ui_.px(6))) d.dragging = true;
  if (d.dragging) {
    const bool image = !d.mat && image_extension_supported(fs::extension(d.path));
    const bool material = d.mat || fs::extension(d.path) == ".mat";
    const std::string label = (image ? "Texture: " : material ? "Material: " : fs::is_dir(d.path) ? "Folder: " : "") +
                              (d.mat ? d.mat->name : fs::filename(d.path));
    /* Highlight what it would land on. */
    Recti target{0, 0, 0, 0};
    if (!image && !material) {
      for (auto &f : drop_folders_)
        if (f.first.contains(in.mx, in.my)) target = f.first;
    }
    for (auto &s : drop_slots_)
      if (!image && s.r.contains(in.mx, in.my)) target = s.r;
    for (auto &t : drop_textures_)
      if (image && t.r.contains(in.mx, in.my)) target = t.r;
    for (auto &r : drop_rows_)
      if (!image && r.first.contains(in.mx, in.my)) target = r.first;
    ui_.overlay([this, label, target] {
      auto &cv = ui_.canvas;
      if (target.w > 0) cv.rect_outline(target, Color::hex(0x3A79BB), ui_.px(2));
      Recti tr{ui_.in.mx + ui_.px(12), ui_.in.my + ui_.px(8), ui_.font.text_width(label) + ui_.px(16), ui_.row_h()};
      ui_.frame(tr, ui_.theme.tab_active, ui_.theme.focus, ui_.px(3));
      cv.text(ui_.font, tr.x + ui_.px(8), tr.y + ui_.px(3), label, ui_.theme.text_bright);
    });
    ui_.cursor = platform::Cursor::Hand;
  }
  if (!in.down[0]) {
    if (d.dragging) {
      /* Onto a folder in the Project window: move it there (a scene material is saved there as an asset). */
      const std::pair<Recti, std::string> *folder = nullptr;
      for (auto &f : drop_folders_)
        if (f.first.contains(in.mx, in.my)) folder = &f;
      if (folder) {
        if (d.mat && d.mat->asset_path.empty()) new_material_asset(d.mat, true, folder->second);
        else {
          const std::string src = d.mat ? resolve_asset_path(d.mat->asset_path) : d.path;
          if (fs::normalize(fs::parent(src)) != fs::normalize(folder->second)) move_project_entry(src, folder->second);
        }
      }
      else if (d.mat || fs::extension(d.path) == ".mat" || image_extension_supported(fs::extension(d.path)))
        drop_asset(d.path, in.mx, in.my, d.mat);
    }
    d = AssetDrag{};
  }
}

bool Editor::drop_asset(const std::string &path, int mx, int my, MaterialPtr given) {
  const std::string ext = fs::extension(path);
  if (!given && ext != ".mat") {
    /* An image onto a material's texture field. */
    for (auto &t : drop_textures_)
      if (t.r.contains(mx, my)) {
        t.tex->path = make_asset_relative(path);
        invalidate_material_textures();
        if (t.owner) t.owner->touch();
        mark_changed("Assign Texture");
        Log::info("Texture: %s", fs::filename(path).c_str());
        return true;
      }
    Log::warn("Drop an image onto a material's texture field (Base Map, Normal Map...)");
    return false;
  }
  MaterialPtr m = given ? given : material_asset(make_asset_relative(path));
  if (!m) {
    Log::error("Could not read the material %s", path.c_str());
    return false;
  }
  for (auto &s : drop_slots_)
    if (s.r.contains(mx, my)) {
      assign_material(scene_->find(s.object), s.slot, m);
      return true;
    }
  for (auto &r : drop_rows_)
    if (r.first.contains(mx, my)) {
      assign_material(scene_->find(r.second), 0, m);
      return true;
    }
  if (scene_rect_.contains(mx, my)) {
    const uint32_t id = scene_rt_.id_at(mx - scene_rect_.x, my - scene_rect_.y);
    GameObject *g = id ? scene_->find(id) : nullptr;
    if (!g || !g->get<MeshFilter>() || !g->get<MeshFilter>()->mesh) {
      Log::warn("Drop the material onto an object");
      return false;
    }
    /* The face under the mouse decides the slot (Unity replaces the sub-mesh's material). */
    const Mesh &base = *g->get<MeshFilter>()->mesh;
    const Mat4 inv = g->world_matrix().inverse();
    const Ray ray = scene_r3d_.screen_ray((float)(mx - scene_rect_.x), (float)(my - scene_rect_.y));
    const Ray lr{inv.point(ray.origin), normalize(inv.dir(ray.dir))};
    const RenderMesh &rm = base.render_mesh();
    float best = 1e30f;
    int face = -1;
    for (size_t t = 0; t < rm.tri_count(); t++) {
      const float d = ray_triangle(lr, rm.positions[rm.indices[t * 3]], rm.positions[rm.indices[t * 3 + 1]], rm.positions[rm.indices[t * 3 + 2]]);
      if (d > 0 && d < best) best = d, face = (int)rm.tri_face[t];
    }
    if (edit_mode_ && g->id == edit_obj_ && face >= 0) assign_material_to_face(g, (uint32_t)face, m);
    else assign_material(g, face >= 0 ? base.material_of((size_t)face) : 0, m);
    select(g->id);
    return true;
  }
  return false;
}

}  // namespace bl
