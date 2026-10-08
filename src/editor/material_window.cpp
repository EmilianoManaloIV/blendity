// SPDX-License-Identifier: GPL-2.0-or-later
// The Materials window: every material in the scene and every material asset
// as a thing of its own, the way Unity treats materials (Project window
// previews + their own Inspector) and Blender's Material browser / Asset
// Browser do. Each tile is a shaded preview sphere; click one to edit it
// right here, drag it onto an object, a face, a Hierarchy row or a slot.
#include "editor.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace bl {

using ui::Icon;

namespace {

struct Preview {
  uint64_t version = 0;
  std::vector<uint32_t> px;
  int size = 0;
};

/* A small software-shaded sphere: Lambert + Blinn-Phong from the material's
 * colour, metallic, roughness and emission, with its base texture wrapped on. */
const Preview &material_preview(const Material &m, int size) {
  static std::unordered_map<const Material *, Preview> cache;
  Preview &p = cache[&m];
  if (p.size == size && p.version == m.version && !p.px.empty()) return p;
  p.size = size;
  p.version = m.version;
  p.px.assign((size_t)size * size, 0);
  const TexturePtr tex = m.procedural ? nullptr : m.textures().base;
  const Vec3 L = normalize(Vec3(-0.45f, 0.65f, 0.6f)), V(0, 0, 1);
  const Vec3 H = normalize(L + V);
  const float rough = clampf(m.roughness, 0.03f, 1.0f);
  const float shin = std::max(2.0f, 2.0f / (rough * rough * rough * rough) - 2.0f);
  for (int y = 0; y < size; y++)
    for (int x = 0; x < size; x++) {
      const float nx = (x + 0.5f) / size * 2 - 1, ny = 1 - (y + 0.5f) / size * 2;
      const float r2 = nx * nx + ny * ny;
      /* Behind the sphere: a checker, so transparency shows. */
      const bool chk = ((x / std::max(1, size / 8)) + (y / std::max(1, size / 8))) & 1;
      Vec3 bg = chk ? Vec3(0.20f) : Vec3(0.12f);
      if (r2 > 1.0f) {
        p.px[(size_t)y * size + x] = Color::from(Vec3(linear_to_srgb(bg.x), linear_to_srgb(bg.y), linear_to_srgb(bg.z)));
        continue;
      }
      const Vec3 n(nx, ny, std::sqrt(1 - r2));
      Vec3 albedo = m.base_color;
      if (tex) {
        const Vec2 uv(0.5f + std::atan2(n.x, n.z) / (2 * kPi), 0.5f - std::asin(clampf(n.y, -1.0f, 1.0f)) / kPi);
        const Vec4 t = tex->sample(Vec2(uv.x * m.tiling.x + m.offset.x, uv.y * m.tiling.y + m.offset.y), 0.0f);
        albedo = Vec3(albedo.x * t.x, albedo.y * t.y, albedo.z * t.z);
      }
      else if (m.procedural == 1) {  // checker
        const bool c = ((int)std::floor((n.x + 1) * m.procedural_scale * 0.5f) + (int)std::floor((n.y + 1) * m.procedural_scale * 0.5f)) & 1;
        if (c) albedo = m.procedural_color2;
      }
      const float ndl = std::max(0.0f, dot(n, L));
      const Vec3 f0 = Vec3(0.04f) * (1 - m.metallic) + albedo * m.metallic;
      const float spec = std::pow(std::max(0.0f, dot(n, H)), shin) * (shin + 8) / (8 * kPi) * ndl;
      /* A sky-ish ambient for the shadowed side, and a fake reflection for metals. */
      const Vec3 sky = Vec3(0.35f, 0.40f, 0.48f) * (0.5f + 0.5f * n.y);
      Vec3 c = m.unlit ? albedo : albedo * (1 - m.metallic) * (ndl * 0.9f + 0.12f) + f0 * spec + f0 * sky * (0.3f + 0.7f * m.metallic);
      c += m.emission * m.emission_strength;
      if (m.alpha < 1.0f) c = c * m.alpha + bg * (1 - m.alpha);
      /* Soft edge. */
      const float edge = clampf((1 - std::sqrt(r2)) * size * 0.5f, 0.0f, 1.0f);
      c = c * edge + bg * (1 - edge);
      p.px[(size_t)y * size + x] = Color::from(Vec3(linear_to_srgb(std::min(c.x, 1.0f)), linear_to_srgb(std::min(c.y, 1.0f)), linear_to_srgb(std::min(c.z, 1.0f))));
    }
  return p;
}

void blit(ui::Context &u, const Preview &p, Recti r) {
  Image *img = u.canvas.target();
  const Recti c = r.intersect(u.canvas.clip());
  for (int y = c.y; y < c.bottom(); y++)
    for (int x = c.x; x < c.right(); x++) img->row(y)[x] = p.px[(size_t)((y - r.y) * p.size / std::max(1, r.h)) * p.size + (size_t)((x - r.x) * p.size / std::max(1, r.w))];
}

}  // namespace

void Editor::draw_materials_window(const Recti &r) {
  auto &u = ui_;
  auto &in = u.in;
  if (u.hovered(r) && in.pressed[0]) focused_ = WindowKind::Materials;
  /* Everything: the scene's materials and the asset files (assets first). */
  struct Entry {
    MaterialPtr m;
    int users = 0;
  };
  std::vector<Entry> list;
  std::unordered_set<const Material *> seen;
  for (const std::string &path : material_asset_paths())
    if (MaterialPtr m = material_asset(path))
      if (seen.insert(m.get()).second) list.push_back({m, 0});
  for (const MaterialPtr &m : scene_materials())
    if (seen.insert(m.get()).second) list.push_back({m, 0});
  scene_->for_each([&](GameObject &g) {
    if (auto *mr = g.get<MeshRenderer>())
      for (auto &s : mr->materials)
        for (Entry &e : list)
          if (s == e.m) e.users++;
  });
  const std::string f = to_lower(material_search_);
  list.erase(std::remove_if(list.begin(), list.end(), [&](const Entry &e) { return !f.empty() && to_lower(e.m->name).find(f) == std::string::npos; }),
             list.end());
  /* Toolbar */
  const int bh = u.row_h() + u.px(6);
  Recti bar{r.x, r.y, r.w, bh};
  u.canvas.fill_rect(bar, Color::hex(0x2F2F2F));
  if (u.button({bar.x + u.px(6), bar.y + u.px(3), u.px(150), bh - u.px(6)}, "New Material Asset", false, Icon::Plus))
    material_selected_ = new_material_asset(nullptr, false);
  u.tooltip("A new .mat in Assets/Materials (Unity: Create > Material).");
  u.text_field(u.id("mat_search"), {bar.x + u.px(162), bar.y + u.px(4), std::min(u.px(200), bar.w - u.px(170)), bh - u.px(8)}, material_search_, nullptr,
               "Search materials");
  /* Grid of previews on top, the selected material's editor below. */
  const bool editing = material_selected_ != nullptr;
  const int grid_h = editing ? std::max(u.px(120), (r.h - bh) * 2 / 5) : r.h - bh;
  Recti grid{r.x, bar.bottom(), r.w, grid_h};
  const int tile = u.px(84), pad = u.px(6);
  const int cols = std::max(1, (grid.w - pad) / (tile + pad));
  const int rows = ((int)list.size() + cols - 1) / cols;
  ui::Id sid = u.id("mat_grid");
  const int off = u.begin_scroll(sid, grid, rows * (tile + u.row_h() + pad) + pad);
  for (size_t i = 0; i < list.size(); i++) {
    const int cx = grid.x + pad + (int)(i % (size_t)cols) * (tile + pad);
    const int cy = grid.y + pad + (int)(i / (size_t)cols) * (tile + u.row_h() + pad) - off;
    Recti cell{cx, cy, tile, tile + u.row_h()};
    if (cell.bottom() < grid.y || cell.y > grid.bottom()) continue;
    const MaterialPtr &m = list[i].m;
    const bool sel = m == material_selected_;
    const bool hot = u.hovered(cell);
    if (sel || hot) u.canvas.fill_round_rect(cell, u.px(4), sel ? u.theme.selection : Color::hex(0x444444));
    const Preview &p = material_preview(*m, tile - u.px(8));
    blit(u, p, {cx + u.px(4), cy + u.px(4), tile - u.px(8), tile - u.px(8)});
    u.label({cx, cy + tile, tile, u.row_h()}, m->name, u.theme.text, ui::Align::Center);
    if (!m->asset_path.empty()) u.draw_icon(Icon::File, {cx + u.px(4), cy + u.px(4), u.px(12), u.px(12)}, u.theme.accent);
    u.tooltip(m->name + (m->asset_path.empty() ? "  (scene material)" : "  (" + m->asset_path + ")") +
              strprintf("\nUsed by %d slot(s). Click to edit; drag onto an object, a face, a Hierarchy row or a slot.", list[i].users));
    if (hot && in.pressed[0]) {
      material_selected_ = m;
      asset_drag_ = {true, false, m->asset_path, m, in.mx, in.my};
    }
    if (hot && in.pressed[1]) {
      material_selected_ = m;
      u.open_popup(u.id("mat_ctx"), {in.mx, in.my, 0, 0});
    }
  }
  if (list.empty()) u.label({grid.x, grid.y + u.px(20), grid.w, u.row_h()}, "No materials yet. New Material Asset makes one.", u.theme.text_dim, ui::Align::Center);
  u.end_scroll();
  MaterialPtr selm = material_selected_;
  u.popup(u.id("mat_ctx"), u.px(230), [this, selm] {
    auto &u = ui_;
    if (!selm) return;
    if (u.menu_item("Assign to Selection", nullptr, false, !selection_.empty()))
      for (GameObject *g : selected_objects(false)) assign_material(g, 0, selm);
    if (selm->asset_path.empty() && u.menu_item("Save as Material Asset")) material_selected_ = new_material_asset(selm, true);
    if (u.menu_item("Duplicate as Asset")) {
      auto copy = std::make_shared<Material>(*selm);
      copy->asset_path.clear();
      copy->name = selm->name + " Copy";
      material_selected_ = new_material_asset(copy, false);
    }
    if (u.menu_item("Select Objects Using It")) {
      selection_.clear();
      scene_->for_each([&](GameObject &g) {
        if (auto *mr = g.get<MeshRenderer>())
          for (auto &s : mr->materials)
            if (s == selm && std::find(selection_.begin(), selection_.end(), g.id) == selection_.end()) selection_.push_back(g.id);
      });
      if (!selection_.empty()) active_ = selection_.back();
    }
  });
  if (!editing) return;
  /* The material's own inspector, outside any object. */
  Recti ed{r.x, grid.bottom() + 1, r.w, r.bottom() - grid.bottom() - 1};
  u.canvas.hline(ed.x, ed.right(), ed.y - 1, u.theme.border);
  ui::Id eid = u.id("mat_edit_scroll");
  static int content_h = 0;
  const int eoff = u.begin_scroll(eid, ed, content_h);
  ui::Layout lay{{ed.x + u.px(6), ed.y, ed.w - u.px(18), ed.h}, ed.y + u.px(6) - eoff};
  lay.row_h = u.row_h();
  Recti head = lay.row(u.row_h() + u.px(4));
  const Preview &p = material_preview(*material_selected_, u.px(64));
  (void)p;
  u.label(head, material_selected_->name + (material_selected_->asset_path.empty() ? "  (scene material)" : "  (" + material_selected_->asset_path + ")"),
          u.theme.text_bright);
  u.push_id((uint64_t)(uintptr_t)material_selected_.get());
  draw_material_fields(lay, material_selected_);
  u.pop_id();
  content_h = lay.y + eoff - ed.y + u.px(20);
  u.end_scroll();
}

/* Everything a face or slot can use: the object's own slots first, then the
 * material assets (every folder), then the scene's other materials. */
std::vector<MaterialPtr> Editor::pickable_materials(GameObject *g) {
  std::vector<MaterialPtr> out;
  std::unordered_set<const Material *> seen;
  auto add = [&](const MaterialPtr &m) {
    if (m && seen.insert(m.get()).second) out.push_back(m);
  };
  if (g)
    if (auto *mr = g->get<MeshRenderer>())
      for (auto &m : mr->materials) add(m);
  for (const std::string &path : material_asset_paths()) add(material_asset(path));
  for (const MaterialPtr &m : scene_materials()) add(m);
  return out;
}

bool Editor::assign_material_to_selected_faces(const MaterialPtr &m) {
  GameObject *g = edit_object();
  if (!g || !m) return false;
  for (size_t f = 0; f < face_sel_.size(); f++)
    if (face_sel_[f]) {
      assign_material_to_face(g, (uint32_t)f, m);  // all selected faces, through a selected one
      return true;
    }
  Log::warn("Select faces first (press 3 for face mode)");
  return false;
}

/* A popup grid of material previews (Unity's object picker for materials):
 * the materials themselves, not slot numbers. */
void Editor::material_picker_popup(ui::Id pid, GameObject *g, std::function<void(const MaterialPtr &)> pick) {
  auto &u = ui_;
  u.popup(pid, u.px(380), [this, g, pick] {
    auto &u = ui_;
    Recti sr = u.popup_row(u.row_h() + u.px(6));
    u.text_field(u.id("matpick_search"), {sr.x + u.px(6), sr.y + u.px(3), sr.w - u.px(12), sr.h - u.px(6)}, material_pick_search_, nullptr,
                 "Search materials");
    const std::string f = to_lower(material_pick_search_);
    std::vector<MaterialPtr> list = pickable_materials(g);
    list.erase(std::remove_if(list.begin(), list.end(), [&](const MaterialPtr &m) { return !f.empty() && to_lower(m->name).find(f) == std::string::npos; }),
               list.end());
    std::vector<MaterialPtr> slots;
    if (g)
      if (auto *mr = g->get<MeshRenderer>()) slots = mr->materials;
    const int tile = u.px(64), pad = u.px(6);
    const int cols = 5;
    for (size_t i = 0; i < list.size(); i += cols) {
      Recti row = u.popup_row(tile + u.row_h() + pad);
      for (size_t k = i; k < std::min(list.size(), i + cols); k++) {
        const MaterialPtr &m = list[k];
        Recti cell{row.x + pad + (int)(k - i) * (tile + pad), row.y + pad / 2, tile, tile + u.row_h()};
        const bool hot = u.hovered(cell);
        if (hot) u.canvas.fill_round_rect(cell, u.px(4), Color::hex(0x4A4A4A));
        blit(u, material_preview(*m, tile - u.px(6)), {cell.x + u.px(3), cell.y + u.px(3), tile - u.px(6), tile - u.px(6)});
        u.label({cell.x, cell.y + tile, cell.w, u.row_h()}, m->name, u.theme.text, ui::Align::Center);
        int slot = -1;
        for (size_t s = 0; s < slots.size(); s++)
          if (slots[s] == m && slot < 0) slot = (int)s;
        if (slot >= 0) u.canvas.text(u.font, cell.x + u.px(4), cell.y + u.px(2), strprintf("%d", slot), u.theme.text_bright);
        else if (!m->asset_path.empty()) u.draw_icon(Icon::File, {cell.x + u.px(4), cell.y + u.px(4), u.px(12), u.px(12)}, u.theme.accent);
        u.tooltip(m->name + (slot >= 0 ? strprintf("  (slot %d of this object)", slot) : m->asset_path.empty() ? "  (scene material)" : "  (" + m->asset_path + ")"));
        if (hot && u.in.pressed[0]) {
          pick(m);
          u.close_popups();
          u.consume_click();
          return;
        }
      }
    }
    if (list.empty()) {
      Recti r = u.popup_row();
      u.label(r, "No materials match.", u.theme.text_dim, ui::Align::Center);
    }
    Recti b = u.popup_row(u.row_h() + u.px(6));
    if (u.button({b.x + u.px(6), b.y + u.px(3), b.w - u.px(12), b.h - u.px(6)}, "New Material Asset", false, Icon::Plus)) {
      if (MaterialPtr m = new_material_asset(nullptr, false)) pick(m);
      u.close_popups();
    }
  });
}

}  // namespace bl
