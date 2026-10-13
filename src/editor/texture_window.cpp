// SPDX-License-Identifier: GPL-2.0-or-later
// The Textures window (task 0028): the project's procedural texture assets (ADR 0017) as thumbnails, a new
// one from a preset, the selected texture's settings and layer stack, and a preview of its outputs. The
// Materials window's layout; the stack is drawn by the Inspector's reflector (panels.cpp, layer_list).
#include "editor.h"

#include "../core/core.h"
#include "../texture/procedural_texture.h"

#include <algorithm>
#include <unordered_set>
#include <functional>
#include <unordered_map>

namespace bl {

namespace {

/* A small picture of one output, re-made when the texture's hash changes. */
struct TexThumb {
  uint64_t hash = 0;
  int which = -1, size = 0;
  std::vector<uint32_t> px;
};

/* Packs a Bitmap (RGBA8) into the canvas's 0xAARRGGBB. */
void to_argb(const Bitmap &b, std::vector<uint32_t> &px) {
  px.resize((size_t)b.width * b.height);
  for (size_t i = 0; i < px.size(); i++)
    px[i] = 0xFF000000u | (uint32_t)b.rgba8[i * 4] << 16 | (uint32_t)b.rgba8[i * 4 + 1] << 8 | (uint32_t)b.rgba8[i * 4 + 2];
}

/* Nearest-neighbour draw of a w x h picture into r, `tiles` times across (a 2 x 2 preview shows the seams). */
void draw_pixels(ui::Context &u, const std::vector<uint32_t> &px, int w, int h, Recti r, int tiles = 1) {
  if (px.empty() || w <= 0 || h <= 0 || r.w <= 0 || r.h <= 0) return;
  Image *img = u.canvas.target();
  const Recti c = r.intersect(u.canvas.clip());
  for (int y = c.y; y < c.bottom(); y++) {
    const int sy = (int)((int64_t)(y - r.y) * h * tiles / r.h) % h;
    for (int x = c.x; x < c.right(); x++) {
      const int sx = (int)((int64_t)(x - r.x) * w * tiles / r.w) % w;
      img->row(y)[x] = px[(size_t)sy * w + sx];
    }
  }
}

/* The albedo at 64 x 64 for the grid (a copy of the texture evaluated small: the pattern is the same). */
const TexThumb &texture_thumb(ProceduralTexture &t, int which, int size) {
  static std::unordered_map<std::string, TexThumb> cache;
  const uint64_t h = t.hash();
  TexThumb &th = cache[t.asset_path + "#" + std::to_string(which) + "@" + std::to_string(size)];
  if (th.hash == h && th.size == size && th.which == which && !th.px.empty()) return th;
  ProceduralTexture small = t;
  small.width = small.height = size;
  to_argb(small.output(which), th.px);
  th.hash = h;
  th.size = size;
  th.which = which;
  return th;
}

}  // namespace

const std::vector<std::string> &Editor::texture_asset_paths() {
  if (ui_.time - texture_paths_listed_ < 2.0 && texture_paths_listed_ > 0) return texture_paths_;
  texture_paths_listed_ = std::max(1e-3, ui_.time);
  texture_paths_.clear();
  std::function<void(const std::string &, int)> walk = [&](const std::string &dir, int depth) {
    if (depth > 12) return;
    for (const DirEntry &de : fs::list(dir)) {
      const std::string p = fs::join(dir, de.name);
      if (de.is_dir) walk(p, depth + 1);
      else if (fs::extension(de.name) == ".ptex") texture_paths_.push_back(make_asset_relative(p));
    }
  };
  walk(assets_dir_, 0);
  for (std::string &p : texture_paths_) std::replace(p.begin(), p.end(), '\\', '/');
  std::sort(texture_paths_.begin(), texture_paths_.end());
  return texture_paths_;
}

/* A new texture asset from a preset, under a name not taken yet in Assets/Textures. */
ProceduralTexturePtr Editor::new_texture_asset(const std::string &preset) {
  ProceduralTexture src;
  if (!preset.empty()) apply_texture_preset(src, preset);
  std::string base = preset.empty() ? "Texture" : preset;
  base.erase(std::remove(base.begin(), base.end(), ' '), base.end());
  std::string rel = "Assets/Textures/" + base + ".ptex";
  for (int n = 2; fs::exists(resolve_asset_path(rel)) || texture_asset(rel); n++) rel = "Assets/Textures/" + base + " " + std::to_string(n) + ".ptex";
  src.name = fs::stem(rel);
  ProceduralTexturePtr t = create_texture_asset(src, rel);
  if (!t) {
    Log::error("Could not write %s", rel.c_str());
    return nullptr;
  }
  texture_selected_ = t;
  texture_paths_listed_ = -100;
  project_listed_ = -100;
  Log::info("Created %s (%zu layer%s)", rel.c_str(), t->layers.size(), t->layers.size() == 1 ? "" : "s");
  return t;
}

void Editor::draw_textures_window(const Recti &r) {
  auto &u = ui_;
  auto &in = u.in;
  texture_ui_rects_.clear();
  if (u.hovered(r) && in.pressed[0]) focused_ = WindowKind::Textures;
  /* The list: every .ptex under Assets (loaded on first sight), filtered by the search. */
  std::vector<ProceduralTexturePtr> list;
  const std::string f = to_lower(texture_search_);
  size_t all = 0;
  for (const std::string &path : texture_asset_paths())
    if (ProceduralTexturePtr t = texture_asset(path)) {
      all++;
      if (f.empty() || to_lower(t->name).find(f) != std::string::npos) list.push_back(t);
    }
  if (texture_selected_ && texture_asset(texture_selected_->asset_path) != texture_selected_) texture_selected_ = nullptr;  // deleted or replaced
  /* Toolbar: New (presets), size, Assign to Selection, search. */
  const int bh = u.row_h() + u.px(6);
  Recti bar{r.x, r.y, r.w, bh};
  u.canvas.fill_rect(bar, Color::hex(0x2F2F2F));
  Recti nb{bar.x + u.px(6), bar.y + u.px(3), u.px(90), bh - u.px(6)};
  texture_ui_rects_["new"] = nb;
  if (u.button(nb, "New", false, ui::Icon::Plus)) u.open_popup(u.id("tex_new"), nb);
  u.tooltip("A new texture asset in Assets/Textures from a preset (Bricks, Wood Floor, Tatami...).");
  u.popup(u.id("tex_new"), u.px(180), [this] {
    bool made = false;
    for (const std::string &p : texture_preset_names())
      if (ui_.menu_item(p.c_str())) new_texture_asset(p), made = true;
    ui_.menu_separator();
    if (ui_.menu_item("Empty")) new_texture_asset(""), made = true;
    if (made) {  // the new one in view (a search left from earlier would hide it)
      texture_search_.clear();
      ui_.redraw = true;
    }
  });
  int x = nb.right() + u.px(6);
  if (texture_selected_) {
    static const char *sizes[] = {"256", "512", "1024", "2048"};
    int si = texture_selected_->width >= 2048 ? 3 : texture_selected_->width >= 1024 ? 2 : texture_selected_->width >= 512 ? 1 : 0;
    const Recti sr{x, bar.y + u.px(3), u.px(80), bh - u.px(6)};
    texture_ui_rects_["size"] = sr;
    if (u.combo(u.id("tex_size"), sr, si, sizes, 4)) {
      note_texture_edit(*texture_selected_);
      texture_selected_->width = texture_selected_->height = 256 << si;
      texture_selected_->touch();
      mark_changed("Texture Size " + texture_selected_->name);
    }
    u.tooltip("The images' size in texels.");
    x = sr.right() + u.px(6);
    const Recti ab{x, bar.y + u.px(3), u.px(150), bh - u.px(6)};
    texture_ui_rects_["assign"] = ab;
    if (u.button(ab, "Assign to Selection", false)) assign_texture_asset_to_selection(texture_selected_->asset_path);
    u.tooltip("The selected objects' first material gets this texture's albedo, normal and roughness, tiled by its physical size.");
    x = ab.right() + u.px(6);
    const Recti db{x, bar.y + u.px(3), u.px(80), bh - u.px(6)};
    texture_ui_rects_["delete"] = db;
    if (u.button(db, "Delete", false)) texture_delete_ = texture_selected_;
    u.tooltip("Delete this texture (it goes to the Recycle Bin / Trash). Also: right-click a thumbnail, or Del.");
    x = db.right() + u.px(6);
  }
  const Recti sf{x + u.px(6), bar.y + u.px(4), std::max(u.px(60), std::min(u.px(200), bar.right() - x - u.px(12))), bh - u.px(8)};
  u.text_field(u.id("tex_search"), sf, texture_search_, nullptr, "Search textures");
  if (!f.empty() && list.size() < all)
    u.label({sf.right() + u.px(8), bar.y, std::max(0, bar.right() - sf.right() - u.px(12)), bh}, strprintf("Showing %zu of %zu (search)", list.size(), all),
            u.theme.text_dim);
  /* The grid of thumbnails, the selected texture's editor below. */
  const bool editing = texture_selected_ != nullptr;
  const int grid_h = editing ? std::max(u.px(110), (r.h - bh) / 4) : r.h - bh;
  Recti grid{r.x, bar.bottom(), r.w, grid_h};
  const int tile = u.px(72), pad = u.px(6);
  const int cols = std::max(1, (grid.w - pad) / (tile + pad));
  const int rows = ((int)list.size() + cols - 1) / cols;
  const int off = u.begin_scroll(u.id("tex_grid"), grid, rows * (tile + u.row_h() + pad) + pad);
  for (size_t i = 0; i < list.size(); i++) {
    const int cx = grid.x + pad + (int)(i % (size_t)cols) * (tile + pad);
    const int cy = grid.y + pad + (int)(i / (size_t)cols) * (tile + u.row_h() + pad) - off;
    Recti cell{cx, cy, tile, tile + u.row_h()};
    texture_ui_rects_["thumb:" + list[i]->name] = cell;
    if (cell.bottom() < grid.y || cell.y > grid.bottom()) continue;
    const ProceduralTexturePtr &t = list[i];
    const bool sel = t == texture_selected_, hot = u.hovered(cell);
    if (sel || hot) u.canvas.fill_round_rect(cell, u.px(4), sel ? u.theme.selection : Color::hex(0x444444));
    const TexThumb &th = texture_thumb(*t, 0, 64);
    draw_pixels(u, th.px, th.size, th.size, {cx + u.px(4), cy + u.px(4), tile - u.px(8), tile - u.px(8)});
    u.label({cx, cy + tile, tile, u.row_h()}, t->name, u.theme.text, ui::Align::Center);
    u.tooltip(t->name + "  (" + t->asset_path + strprintf(")\n%d x %d, %zu layer(s). Click to edit; drag onto an object or a Hierarchy row to use it.", t->width, t->height,
                                                          t->layers.size()));
    if (hot && in.pressed[0]) {
      texture_selected_ = t;
      asset_drag_ = {true, false, resolve_asset_path(t->asset_path), nullptr, in.mx, in.my};
    }
    if (hot && in.pressed[1]) {  // right-click: the thumbnail's menu
      texture_selected_ = t;
      u.open_popup(u.id("tex_ctx"), {in.mx, in.my, 0, 0});
    }
  }
  u.popup(u.id("tex_ctx"), u.px(200), [this] {
    if (!texture_selected_) return;
    if (ui_.menu_item("Assign to Selection")) assign_texture_asset_to_selection(texture_selected_->asset_path);
    ui_.menu_separator();
    if (ui_.menu_item("Delete Texture...", "Del")) texture_delete_ = texture_selected_;
  });
  /* Delete (or Backspace) over the window asks to delete the selected texture. */
  if (u.hovered(r) && texture_selected_ && !u.wants_keyboard() && (in.key_pressed[platform::KEY_DELETE] || in.key_pressed[platform::KEY_BACKSPACE])) {
    texture_delete_ = texture_selected_;
    in.key_pressed[platform::KEY_DELETE] = in.key_pressed[platform::KEY_BACKSPACE] = false;
  }
  /* The confirmation (as the Project and Materials windows'): it goes to the Recycle Bin / Trash. */
  if (texture_delete_) {
    const ui::Id did = u.id("tex_delete");
    if (!u.popup_open(did)) u.open_popup(did, {r.x + r.w / 2 - u.px(190), r.y + u.px(40), 0, 0});
    const ProceduralTexturePtr dt = texture_delete_;
    u.popup(did, u.px(380), [this, dt] {
      auto &u = ui_;
      /* The materials whose maps name one of its outputs: the scene's and the assets (an axis material's parts are
       * assets too), each once. */
      const std::string prefix = dt->asset_path + "#";
      std::unordered_set<const Material *> users_set;
      auto check = [&](const Material *m) {
        if (!m) return;
        for (const TextureRef *t : {&m->base_map, &m->metallic_map, &m->roughness_map, &m->normal_map, &m->emission_map})
          if (t->path.compare(0, prefix.size(), prefix) == 0) users_set.insert(m);
      };
      scene_->for_each([&](GameObject &g) {
        if (auto *mr = g.get<MeshRenderer>())
          for (auto &m : mr->materials) check(m.get());
      });
      for (const auto &kv : loaded_material_assets()) check(kv.second.get());
      const size_t users = users_set.size();
      Recti t = u.popup_row(u.row_h() + u.px(6));
      u.label({t.x + u.px(10), t.y, t.w - u.px(20), t.h}, "Delete " + fs::filename(dt->asset_path) + "?", u.theme.text_bright);
      Recti h1 = u.popup_row();
#ifdef _WIN32
      u.label({h1.x + u.px(10), h1.y, h1.w - u.px(20), h1.h}, "It goes to the Recycle Bin, where you can restore it.", u.theme.text_dim);
#else
      u.label({h1.x + u.px(10), h1.y, h1.w - u.px(20), h1.h}, "It goes to the Trash, where you can restore it.", u.theme.text_dim);
#endif
      if (users) {
        Recti h2 = u.popup_row();
        u.label({h2.x + u.px(10), h2.y, h2.w - u.px(20), h2.h}, strprintf("%zu material%s use it: those maps will show it missing.", users, users == 1 ? "" : "s"),
                u.theme.text_dim);
      }
      Recti b = u.popup_row(u.row_h() + u.px(10));
      const Recti del{b.right() - u.px(180), b.y + u.px(4), u.px(80), b.h - u.px(8)};
      texture_ui_rects_["delete_confirm"] = del;
      if (u.button(del, "Delete")) {
        const std::string path = resolve_asset_path(dt->asset_path);
        if (texture_selected_ == dt) texture_selected_ = nullptr;
        texture_delete_ = nullptr;
        /* An undo past this point must not write the deleted texture's text into a new one of the same name. */
        texture_before_.erase(std::remove_if(texture_before_.begin(), texture_before_.end(), [&](const auto &kv) { return kv.first == dt->asset_path; }),
                              texture_before_.end());
        for (UndoState &st : undo_) st.textures.erase(std::remove_if(st.textures.begin(), st.textures.end(), [&](const auto &kv) { return kv.first == dt->asset_path; }), st.textures.end());
        for (UndoState &st : redo_) st.textures.erase(std::remove_if(st.textures.begin(), st.textures.end(), [&](const auto &kv) { return kv.first == dt->asset_path; }), st.textures.end());
        delete_project_entry(path);
        texture_paths_listed_ = -100;  // list again now
        invalidate_material_textures();  // materials naming it re-resolve (and show it missing)
        u.close_popups();
      }
      if (u.button({b.right() - u.px(92), b.y + u.px(4), u.px(80), b.h - u.px(8)}, "Cancel")) {
        texture_delete_ = nullptr;
        u.close_popups();
      }
    });
    if (!u.popup_open(did)) texture_delete_ = nullptr;  // clicked away
  }
  if (list.empty()) u.label({grid.x, grid.y + u.px(20), grid.w, u.row_h()}, "No textures yet. New makes one from a preset.", u.theme.text_dim, ui::Align::Center);
  u.end_scroll();
  if (!editing) return;
  /* The editor: the preview on the right, the settings and the stack on the left. */
  Recti body{r.x, grid.bottom() + 1, r.w, r.bottom() - grid.bottom() - 1};
  u.canvas.hline(body.x, body.right(), body.y - 1, u.theme.border);
  const int pw = std::min(body.w / 2, std::max(u.px(140), std::min(body.h - u.px(40), u.px(300))));
  Recti pane{body.right() - pw - u.px(6), body.y + u.px(6), pw, pw};
  texture_ui_rects_["preview"] = pane;
  Recti left{body.x, body.y, body.w - pw - u.px(12), body.h};
  /* Preview: an output, 1x or 2 x 2. While a field is being dragged it evaluates at 256 at most, and at the
   * texture's size once the mouse is released. */
  {
    static const char *outs[] = {"Albedo", "Normal", "Roughness", "Height"};
    const Recti oc{pane.x, pane.bottom() + u.px(4), pane.w - u.px(56), u.row_h()};
    texture_ui_rects_["output"] = oc;
    u.combo(u.id("tex_out"), oc, texture_preview_output_, outs, 4);
    const Recti tb{oc.right() + u.px(4), oc.y, u.px(52), u.row_h()};
    if (u.button(tb, texture_preview_tiled_ ? "2 x 2" : "1 x", texture_preview_tiled_)) texture_preview_tiled_ = !texture_preview_tiled_;
    u.tooltip("Show the texture tiled 2 x 2: its edges meet in the middle.");
    ProceduralTexture &t = *texture_selected_;
    const int which = std::max(0, std::min(3, texture_preview_output_));
    if (texture_dragging_) {
      const int ps = std::min(256, std::max(64, std::max(t.width, t.height)));
      const TexThumb &th = texture_thumb(t, which, ps);
      draw_pixels(u, th.px, th.size, th.size, pane, texture_preview_tiled_ ? 2 : 1);
      texture_preview_size_ = th.size;
    }
    else if (TexturePtr tex = procedural_texture_output(t, which)) {
      /* From the smallest mip at least as wide as the pane, repacked only when the image or the level changes
       * (not every frame: an idle editor redraws on every mouse move). */
      /* (Held, not just its address: a new image allocated where a freed one was would look unchanged, and the pane
       * would keep the old picture - after an undo, say.) */
      static TexturePtr packed_tex;
      static size_t packed_level = 0;
      static std::vector<uint32_t> px;
      size_t lv = 0;
      while (lv + 1 < tex->levels.size() && tex->levels[lv + 1].w >= pane.w) lv++;
      const Texture::Level &L = tex->levels[lv];
      if (packed_tex != tex || packed_level != lv || px.size() != (size_t)L.w * L.h) {
        px.resize((size_t)L.w * L.h);
        for (size_t i = 0; i < px.size(); i++) {
          const uint32_t c = L.px8[i];
          px[i] = 0xFF000000u | (c & 0xFF) << 16 | (c >> 8 & 0xFF) << 8 | (c >> 16 & 0xFF);
        }
        packed_tex = tex;
        packed_level = lv;
      }
      draw_pixels(u, px, L.w, L.h, pane, texture_preview_tiled_ ? 2 : 1);
      texture_preview_size_ = tex->width;
    }
    u.canvas.rect_outline(pane, u.theme.border);
  }
  const int eoff = u.begin_scroll(u.id("tex_edit_scroll"), left, texture_edit_h_);
  ui::Layout lay{{left.x + u.px(6), left.y, left.w - u.px(18), left.h}, left.y + u.px(6) - eoff};
  lay.row_h = u.row_h();
  u.label(lay.row(u.row_h() + u.px(4)), texture_selected_->name + "  (" + texture_selected_->asset_path + ")", u.theme.text_bright);
  u.push_id((uint64_t)(uintptr_t)texture_selected_.get());
  const bool changed = draw_texture_fields(lay, texture_selected_);
  u.pop_id();
  texture_edit_h_ = lay.y + eoff - left.y + u.px(20);
  u.end_scroll();
  if (changed && in.down[0]) texture_dragging_ = true;  // a drag on a field: the small preview until release
  if (!in.down[0]) {
    texture_dragging_ = false;
    if (texture_touch_pending_) {  // the drag is over: the images at the set size again
      procedural_texture_clear_previews();
      invalidate_material_textures();
      texture_touch_pending_ = false;
    }
  }
}

}  // namespace bl
