// SPDX-License-Identifier: GPL-2.0-or-later
// UV Editor window (Blender: UV Editor, blender/source/blender/editors/uvedit
// and space_image). Unity has no UV editor, so this window keeps Unity's
// navigation (MMB pan, wheel zoom, W/E/R tools) around Blender's UV tools.
#include "editor.h"

#include "../core/core.h"
#include "../scene/uv.h"

#include <algorithm>
#include <cmath>

namespace bl {

using ui::Icon;

MeshPtr *Editor::uv_target(GameObject **owner) {
  GameObject *g = edit_mode_ ? edit_object() : active_object();
  if (owner) *owner = g;
  if (!g) return nullptr;
  auto *mf = g->get<MeshFilter>();
  return mf && mf->mesh ? &mf->mesh : nullptr;
}

void Editor::uv_op(const std::string &op) {
  GameObject *g = nullptr;
  MeshPtr *mp = uv_target(&g);
  if (!mp) {
    Log::warn("UV tools need an object with a MeshFilter (select one, or Tab into Edit Mode)");
    return;
  }
  Mesh &m = *mesh_make_mutable(*mp);
  /* Operate on the selected faces in Edit Mode (Blender behaviour), else on everything. */
  std::vector<uint8_t> mask;
  const uvops::Mask *pm = nullptr;
  if (edit_mode_ && std::find(face_sel_.begin(), face_sel_.end(), 1) != face_sel_.end()) {
    mask = face_sel_;
    mask.resize(m.face_count(), 0);
    pm = &mask;
  }
  ScopedTimer t;
  int islands = -1;
  if (op == "unwrap") islands = uvops::unwrap_lscm(m, pm);
  else if (op == "smart") islands = uvops::smart_project(m, pm);
  else if (op == "cube") uvops::project_cube(m, pm, std::max(0.001f, length(m.bounds().extent()) * 2.0f / std::sqrt(3.0f)));
  else if (op == "cylinder") uvops::project_cylinder(m, pm);
  else if (op == "sphere") uvops::project_sphere(m, pm);
  else if (op == "view") {
    float aspect = scene_rect_.h > 0 ? scene_rect_.w / (float)scene_rect_.h : 1.0f;
    uvops::project_view(m, pm, cam_.proj(aspect) * cam_.view() * g->world_matrix());
  }
  else if (op == "reset") uvops::reset(m, pm);
  else if (op == "pack") uvops::pack_islands(m, pm);
  else if (op == "average") uvops::average_island_scale(m, pm);
  else if (op == "mark_seam" || op == "clear_seam") {
    if (!edit_mode_) {
      Log::warn("Mark Seam works on selected edges in Edit Mode (Tab, then select vertices along the cut)");
      return;
    }
    /* The selected edges themselves: in edge mode two opposite sides of a quad
     * are two seams, not the four edges between their corners. */
    int n = 0;
    std::vector<std::pair<uint32_t, uint32_t>> edges(m.edge_cache().begin(), m.edge_cache().end());
    for (auto &e : edges)
      if (edge_is_selected(e.first, e.second)) {
        m.set_seam(e.first, e.second, op == "mark_seam");
        n++;
      }
    Log::info("%s %d seam edge(s)", op == "mark_seam" ? "Marked" : "Cleared", n);
  }
  else if (op == "seams_from_sharp" || op == "seams_from_sharp_unwrap") {
    /* Blender: Select Sharp Edges (30 degrees by default) then Mark Seam. In Edit
     * Mode with faces selected, only their edges; otherwise the whole mesh. */
    const size_t n = meshops::seams_from_sharp(m, seam_angle_, pm);
    Log::info("Seams from sharp edges (> %.0f deg): %zu new seam(s)", seam_angle_, n);
    if (op == "seams_from_sharp_unwrap") islands = uvops::unwrap_lscm(m, pm);
  }
  else {
    Log::warn("Unknown UV operation '%s'", op.c_str());
    return;
  }
  m.touch();
  mark_changed("UV: " + op);
  if (islands >= 0) Log::info("UV %s: %d island(s) in %.1f ms", op.c_str(), islands, t.ms());
  else Log::info("UV %s done in %.1f ms", op.c_str(), t.ms());
}

void Editor::draw_uv_editor(const Recti &r) {
  auto &u = ui_;
  auto &in = u.in;
  /* ---- toolbar ---- */
  int bh = u.row_h() + u.px(6);
  Recti bar{r.x, r.y, r.w, bh};
  u.canvas.fill_rect(bar, Color::hex(0x2F2F2F));
  int x = bar.x + u.px(6), y = bar.y + u.px(3), h = bh - u.px(6);
  struct B { const char *label, *op, *tip; };
  const B buttons[] = {
      {"Unwrap", "unwrap", "Unwrap (U): Least Squares Conformal Maps per seam-bounded island, then pack.\nBlender: UV > Unwrap (Conformal)."},
      {"Smart UV", "smart", "Smart UV Project: group faces by normal, project and pack. No seams needed."},
      {"Cube", "cube", "Cube Projection: each face projected along its main axis."},
      {"Cylinder", "cylinder", "Cylinder Projection around the object's Y axis."},
      {"Sphere", "sphere", "Sphere Projection (latitude / longitude)."},
      {"View", "view", "Project From View: UVs = what the Scene camera sees."},
      {"Reset", "reset", "Reset: every face fills the 0-1 square."},
      {"Pack", "pack", "Pack Islands into 0-1 with margin and rotation."},
      {"Avg Scale", "average", "Average Islands Scale: equal texel density."},
      {"Mark Seam", "mark_seam", "Mark the edges between selected vertices as seams (Edit Mode).\nBlender: Edge > Mark Seam."},
      {"Clear Seam", "clear_seam", "Clear seams between selected vertices."},
      {"Seams from Sharp", "seams_from_sharp_unwrap", "Mark a seam on every sharp edge (faces meeting at more than the Seam Angle, or marked sharp),\n"
       "then unwrap. Blender: Select Sharp Edges, Mark Seam, Unwrap."}};
  for (const B &b : buttons) {
    int w = u.font.text_width(b.label) + u.px(14);
    if (x + w > bar.right() - u.px(260)) break;
    if (u.button({x, y, w, h}, b.label)) uv_op(b.op);
    u.tooltip(b.tip);
    x += w + u.px(3);
  }
  static const char *bgs[] = {"Material Texture", "UV Grid", "Color Grid", "None"};
  if (u.combo(u.id("uv_bg"), {bar.right() - u.px(250), y, u.px(150), h}, uv_bg_, bgs, 4)) {
  }
  if (u.button({bar.right() - u.px(94), y, u.px(88), h}, "Stretch", uv_stretch_)) uv_stretch_ = !uv_stretch_;
  u.tooltip("Color faces by area distortion: blue = compressed, green = even, red = stretched.");

  Recti bar2{r.x, r.y + bh, r.w, bh};
  draw_uv_tool_row(bar2);
  Recti view{r.x, r.y + 2 * bh, r.w, r.h - 2 * bh};
  u.canvas.fill_rect(view, Color::hex(0x262626));
  u.canvas.push_clip(view);
  GameObject *g = nullptr;
  MeshPtr *mp = uv_target(&g);
  float size = std::min(view.w, view.h) * 0.85f * uv_zoom_;
  float cx = view.x + view.w * 0.5f + uv_pan_.x, cy = view.y + view.h * 0.5f + uv_pan_.y;
  auto to_screen = [&](Vec2 t) { return Vec2(cx + (t.x - 0.5f) * size, cy - (t.y - 0.5f) * size); };
  auto to_uv = [&](float sx, float sy) { return Vec2((sx - cx) / size + 0.5f, -(sy - cy) / size + 0.5f); };

  /* ---- background image in the 0-1 square ---- */
  TexturePtr bg;
  if (uv_bg_ == 1) bg = texture_uv_grid();
  else if (uv_bg_ == 2) bg = texture_color_grid();
  else if (uv_bg_ == 0 && g)
    if (auto *mr = g->get<MeshRenderer>()) bg = mr->material(0)->textures().base;
  Vec2 s0 = to_screen({0, 1}), s1 = to_screen({1, 0});
  Recti sq{(int)s0.x, (int)s0.y, (int)(s1.x - s0.x), (int)(s1.y - s0.y)};
  if (bg && !bg->levels.empty()) {
    int lv = 0;
    while (lv + 1 < (int)bg->levels.size() && bg->levels[lv + 1].w >= sq.w) lv++;
    const Texture::Level &L = bg->levels[lv];
    Recti c = sq.intersect(u.canvas.clip());
    Image *img = u.canvas.target();
    for (int yy = c.y; yy < c.bottom(); yy++)
      for (int xx = c.x; xx < c.right(); xx++) {
        Vec4 v = bg->fetch(lv, (xx - sq.x) * L.w / std::max(1, sq.w), (yy - sq.y) * L.h / std::max(1, sq.h), TexWrap::Extend);
        img->row(yy)[xx] = to_display_pixel(v.xyz() * 0.75f, ViewTransform::Standard, 0.0f);
      }
  }
  else {
    u.canvas.fill_rect(sq, Color::hex(0x303030));
  }
  for (int k = 0; k <= 10; k++) {
    Vec2 a = to_screen({k / 10.0f, 0}), b = to_screen({k / 10.0f, 1});
    u.canvas.line(a.x, a.y, b.x, b.y, Color::hex(0x5A5A5A, k % 5 == 0 ? 160 : 70), 1.0f);
    a = to_screen({0, k / 10.0f});
    b = to_screen({1, k / 10.0f});
    u.canvas.line(a.x, a.y, b.x, b.y, Color::hex(0x5A5A5A, k % 5 == 0 ? 160 : 70), 1.0f);
  }
  u.canvas.rect_outline(sq, Color::hex(0x8A8A8A));

  if (!mp) {
    u.label({view.x, view.y + u.px(20), view.w, u.row_h()}, "Select an object with a mesh (or Tab into Edit Mode) to edit its UVs", u.theme.text_dim, ui::Align::Center);
    u.canvas.pop_clip();
    return;
  }
  const Mesh &m = **mp;
  if (!m.has_uvs()) {
    u.label({view.x, view.y + u.px(20), view.w, u.row_h()}, "This mesh has no UV map yet - press Unwrap or Smart UV", u.theme.warning, ui::Align::Center);
    u.canvas.pop_clip();
    return;
  }
  if (uv_sel_.size() != m.corner_count()) uv_sel_.assign(m.corner_count(), 0);
  /* Faces shown: Edit Mode selection if any (Blender), else all. */
  bool use_sel = !uv_sync_ && edit_mode_ && g && g->id == edit_obj_ && std::find(face_sel_.begin(), face_sel_.end(), 1) != face_sel_.end();  // syncing: every face shows
  auto shown = [&](size_t f) { return !use_sel || (f < face_sel_.size() && face_sel_[f]); };
  std::vector<float> stretch;
  if (uv_stretch_) stretch = uvops::face_area_stretch(m);
  for (size_t f = 0; f < m.face_count(); f++) {
    if (!shown(f)) continue;
    uint32_t b = m.face_offsets[f], n = m.face_size(f);
    std::vector<Vec2> pts(n);
    for (uint32_t i = 0; i < n; i++) pts[i] = to_screen(m.uvs[b + i]);
    uint32_t fill = Color::hex(0x7A7A7A, 40);
    if (uv_stretch_) {
      float d = std::fabs(std::log2(std::max(stretch[f], 1e-4f)));
      Vec3 c = d < 0.5f ? lerp(Vec3(0.2f, 0.4f, 1.0f), Vec3(0.2f, 0.9f, 0.3f), d * 2) : lerp(Vec3(0.2f, 0.9f, 0.3f), Vec3(1.0f, 0.2f, 0.1f), saturate(d - 0.5f));
      fill = Color::from(c, 0.55f);
    }
    else if (edit_mode_ && f < face_sel_.size() && face_sel_[f]) fill = Color::hex(0xFF8C1A, 45);
    for (uint32_t i = 1; i + 1 < n; i++) u.canvas.fill_triangle(pts[0], pts[i], pts[i + 1], fill);
    for (uint32_t i = 0; i < n; i++) {
      uint32_t j = (i + 1) % n;
      bool seam = m.is_seam(m.corner_verts[b + i], m.corner_verts[b + j]);
      bool sel = uv_sel_[b + i] && uv_sel_[b + j];
      u.canvas.line(pts[i].x, pts[i].y, pts[j].x, pts[j].y, seam ? Color::hex(0xFF3030) : (sel ? Color::hex(0xFFA733) : Color::hex(0xD0D0D0, 200)), seam ? 2.0f : 1.0f);
    }
  }
  /* Unselected UV vertices only when they will not swamp the layout. */
  bool all_dots = m.corner_count() * 40.0f < size * size * 0.02f || m.corner_count() < 600;
  for (size_t f = 0; f < m.face_count(); f++) {
    if (!shown(f)) continue;
    for (uint32_t c = m.face_offsets[f]; c < m.face_offsets[f + 1]; c++) {
      if (!uv_sel_[c] && !all_dots) continue;
      Vec2 p = to_screen(m.uvs[c]);
      u.canvas.fill_circle(p.x, p.y, (float)u.px(uv_sel_[c] ? 2.5f : 1.5f), uv_sel_[c] ? Color::hex(0xFFA733) : Color::hex(0x101010));
    }
  }

  /* ---- interaction ---- */
  bool hot = u.hovered(view) && !u.any_popup_open();
  if (hot && in.pressed[0]) focused_ = WindowKind::UVEditor;
  if (hot && in.wheel_y != 0) {
    Vec2 before = to_uv((float)in.mx, (float)in.my);
    uv_zoom_ = clampf(uv_zoom_ * std::pow(1.15f, in.wheel_y), 0.05f, 50.0f);
    size = std::min(view.w, view.h) * 0.85f * uv_zoom_;
    Vec2 after = to_uv((float)in.mx, (float)in.my);
    uv_pan_ += Vec2((after.x - before.x) * size, -(after.y - before.y) * size);
  }
  auto nearest_corner = [&](float mx, float my) {
    int best = -1;
    float bd = (float)u.px(10);
    for (size_t f = 0; f < m.face_count(); f++) {
      if (!shown(f)) continue;
      for (uint32_t c = m.face_offsets[f]; c < m.face_offsets[f + 1]; c++) {
        Vec2 p = to_screen(m.uvs[c]);
        float d = length(p - Vec2(mx, my));
        if (d < bd) { bd = d; best = (int)c; }
      }
    }
    return best;
  };
  /* The face whose UV polygon contains a screen point (the last drawn, i.e. on top). */
  auto face_under = [&](float mx, float my) {
    const Vec2 p = to_uv(mx, my);
    int hit = -1;
    for (size_t f = 0; f < m.face_count(); f++) {
      if (!shown(f)) continue;
      bool in_poly = false;
      const uint32_t b0 = m.face_offsets[f], n = m.face_size(f);
      for (uint32_t i = 0, j = n - 1; i < n; j = i++) {
        const Vec2 a = m.uvs[b0 + i], c2 = m.uvs[b0 + j];
        if ((a.y > p.y) != (c2.y > p.y) && p.x < (c2.x - a.x) * (p.y - a.y) / (c2.y - a.y) + a.x) in_poly = !in_poly;
      }
      if (in_poly) hit = (int)f;
    }
    return hit;
  };
  /* Sticky selection: corners of the same vertex at the same UV move together. */
  auto select_shared = [&](uint32_t c, uint8_t value) {
    uint32_t v = m.corner_verts[c];
    Vec2 t = m.uvs[c];
    for (size_t k = 0; k < m.corner_count(); k++)
      if (m.corner_verts[k] == v && std::fabs(m.uvs[k].x - t.x) + std::fabs(m.uvs[k].y - t.y) < 1e-5f) uv_sel_[k] = value;
  };
  if (uv_drag_ == UvDrag::None && hot) {
    if (in.pressed[2]) uv_drag_ = UvDrag::Pan;
    else if (in.pressed[0]) {
      uv_press_x_ = in.mx;
      uv_press_y_ = in.my;
      int c = nearest_corner((float)in.mx, (float)in.my);
      bool on_sel = c >= 0 && uv_sel_[c];
      const int under = uv_select_mode_ ? face_under((float)in.mx, (float)in.my) : -1;
      if (under >= 0) {
        /* Face / Island mode: inside a selected face grabs the selection; an unselected one is picked first. */
        bool all = true;
        for (uint32_t k = m.face_offsets[(size_t)under]; k < m.face_offsets[(size_t)under + 1]; k++) all = all && uv_sel_[k];
        if (all) on_sel = true;
        else if (tool_ == Tool::Move && !in.shift() && !in.ctrl()) {
          uv_select(uv_select_mode_ == 2 ? "island" : "face", under);
          on_sel = true;
        }
        if (c < 0) c = (int)m.face_offsets[(size_t)under];
      }
      bool any_sel = std::find(uv_sel_.begin(), uv_sel_.end(), 1) != uv_sel_.end();
      UvDrag mode = UvDrag::Box;
      if (tool_ == Tool::Move && on_sel) mode = UvDrag::Move;
      else if ((tool_ == Tool::Rotate || tool_ == Tool::Scale) && any_sel) mode = tool_ == Tool::Rotate ? UvDrag::Rotate : UvDrag::Scale;
      else if (tool_ == Tool::Move && c >= 0 && !in.shift() && !in.ctrl() && uv_select_mode_ == 0) {
        /* Click-drag an unselected vertex: select it and move straight away. */
        std::fill(uv_sel_.begin(), uv_sel_.end(), 0);
        select_shared((uint32_t)c, 1);
        mode = UvDrag::Move;
      }
      uv_drag_ = mode;
      if (mode != UvDrag::Box) {
        uv_drag_start_.clear();
        Vec2 sum(0, 0);
        for (size_t k = 0; k < uv_sel_.size(); k++)
          if (uv_sel_[k]) {
            uv_drag_start_.push_back({(uint32_t)k, m.uvs[k]});
            sum += m.uvs[k];
          }
        uv_pivot_ = uv_drag_start_.empty() ? Vec2(0.5f, 0.5f) : sum / (float)uv_drag_start_.size();
      }
    }
  }
  switch (uv_drag_) {
    case UvDrag::Pan:
      uv_pan_ += Vec2((float)in.dx(), (float)in.dy());
      u.cursor = platform::Cursor::Hand;
      if (!in.down[2]) uv_drag_ = UvDrag::None;
      break;
    case UvDrag::Box: {
      Recti box{std::min(uv_press_x_, in.mx), std::min(uv_press_y_, in.my), std::abs(in.mx - uv_press_x_), std::abs(in.my - uv_press_y_)};
      bool moved = box.w > u.px(4) || box.h > u.px(4);
      if (moved) {
        u.canvas.fill_rect(box, Color::hex(0x3A79BB, 40));
        u.canvas.rect_outline(box, Color::hex(0x6FA3DD));
      }
      if (!in.down[0]) {
        bool add = in.shift() || in.ctrl();
        if (!add) std::fill(uv_sel_.begin(), uv_sel_.end(), 0);
        if (moved && uv_select_mode_ == 0) {
          for (size_t f = 0; f < m.face_count(); f++) {
            if (!shown(f)) continue;
            for (uint32_t c = m.face_offsets[f]; c < m.face_offsets[f + 1]; c++) {
              Vec2 p = to_screen(m.uvs[c]);
              if (box.contains((int)p.x, (int)p.y)) uv_sel_[c] = 1;
            }
          }
        }
        else if (moved) {
          /* Faces (or their islands) whose centre is in the box. */
          for (size_t f = 0; f < m.face_count(); f++) {
            if (!shown(f)) continue;
            Vec2 cen(0, 0);
            for (uint32_t c = m.face_offsets[f]; c < m.face_offsets[f + 1]; c++) cen += m.uvs[c];
            const Vec2 p = to_screen(cen / (float)m.face_size(f));
            if (box.contains((int)p.x, (int)p.y)) {
              if (uv_select_mode_ == 2) {
                std::vector<uint8_t> keep = uv_sel_;
                uv_select("island", (int)f);
                for (size_t k = 0; k < keep.size() && k < uv_sel_.size(); k++) uv_sel_[k] |= keep[k];
              }
              else
                for (uint32_t c = m.face_offsets[f]; c < m.face_offsets[f + 1]; c++) uv_sel_[c] = 1;
            }
          }
        }
        else if (uv_select_mode_ != 0) {
          const int f = face_under((float)in.mx, (float)in.my);
          if (f >= 0) {
            bool was = true;
            for (uint32_t k = m.face_offsets[(size_t)f]; k < m.face_offsets[(size_t)f + 1]; k++) was = was && uv_sel_[k];
            std::vector<uint8_t> keep = uv_sel_;
            uv_select(uv_select_mode_ == 2 ? "island" : "face", f);
            if (add) {
              std::vector<uint8_t> picked = uv_sel_;
              uv_sel_ = keep;
              for (size_t k = 0; k < picked.size(); k++)
                if (picked[k]) uv_sel_[k] = was ? 0 : 1;  // Shift toggles
            }
          }
        }
        else {
          int c = nearest_corner((float)in.mx, (float)in.my);
          if (c >= 0) select_shared((uint32_t)c, add ? !uv_sel_[c] : 1);
        }
        uv_sync_to_faces(m);
        uv_drag_ = UvDrag::None;
      }
      break;
    }
    case UvDrag::Move:
    case UvDrag::Rotate:
    case UvDrag::Scale: {
      Mesh &mm = *mesh_make_mutable(*mp);
      Vec2 ps = to_screen(uv_pivot_);
      Vec2 a{(float)uv_press_x_, (float)uv_press_y_}, b{(float)in.mx, (float)in.my};
      bool snap = snap_ || in.ctrl();
      for (auto &[c, start] : uv_drag_start_) {
        Vec2 t = start;
        if (uv_drag_ == UvDrag::Move) {
          Vec2 d{(b.x - a.x) / size, -(b.y - a.y) / size};
          if (snap) d = {std::round(d.x * 64) / 64, std::round(d.y * 64) / 64};
          t = start + d;
        }
        else if (uv_drag_ == UvDrag::Rotate) {
          float ang = std::atan2(-(b.y - ps.y), b.x - ps.x) - std::atan2(-(a.y - ps.y), a.x - ps.x);
          if (snap) ang = std::round(ang * kRad2Deg / 15.0f) * 15.0f * kDeg2Rad;
          Vec2 d = start - uv_pivot_;
          t = uv_pivot_ + Vec2(d.x * std::cos(ang) - d.y * std::sin(ang), d.x * std::sin(ang) + d.y * std::cos(ang));
        }
        else {
          float k = length(b - ps) / std::max(1.0f, length(a - ps));
          if (snap) k = std::round(k * 10) / 10;
          t = uv_pivot_ + (start - uv_pivot_) * k;
        }
        if (c < mm.uvs.size()) mm.uvs[c] = t;
      }
      mm.touch();
      if (!in.down[0]) {
        uv_drag_ = UvDrag::None;
        mark_changed("UV Transform");
      }
      break;
    }
    default: break;
  }
  /* Keyboard (when the UV editor has focus). */
  if (focused_ == WindowKind::UVEditor && !u.wants_keyboard()) {
    if (in.ctrl() && in.key_pressed[platform::KEY_A]) std::fill(uv_sel_.begin(), uv_sel_.end(), in.shift() ? 0 : 1);
    if (in.key_pressed[platform::KEY_U]) uv_op("unwrap");
    if (in.key_pressed[platform::KEY_L] && hot) {
      /* Select linked: the UV island under the mouse. */
      int c = nearest_corner((float)in.mx, (float)in.my);
      if (c >= 0) {
        std::vector<int> fi;
        uvops::compute_islands(m, nullptr, false, fi);
        uint32_t f0 = 0;
        for (size_t f = 0; f < m.face_count(); f++)
          if ((uint32_t)c >= m.face_offsets[f] && (uint32_t)c < m.face_offsets[f + 1]) f0 = (uint32_t)f;
        for (size_t f = 0; f < m.face_count(); f++)
          if (fi[f] == fi[f0])
            for (uint32_t k = m.face_offsets[f]; k < m.face_offsets[f + 1]; k++) uv_sel_[k] = 1;
      }
    }
    if (in.key_pressed[platform::KEY_F] || in.key_pressed[platform::KEY_HOME]) {
      uv_zoom_ = 1.0f;
      uv_pan_ = Vec2(0, 0);
    }
    /* Arrows nudge the selection by 1/64 (Shift: 1/8), like Blender's UV editor with snapping. */
    const float step = in.shift() ? 0.125f : 1.0f / 64.0f;
    if (in.key_pressed[platform::KEY_LEFT]) uv_transform("move", -step, 0);
    if (in.key_pressed[platform::KEY_RIGHT]) uv_transform("move", step, 0);
    if (in.key_pressed[platform::KEY_UP]) uv_transform("move", 0, step);
    if (in.key_pressed[platform::KEY_DOWN]) uv_transform("move", 0, -step);
    if (in.ctrl() && in.key_pressed[platform::KEY_I]) uv_select("invert");
    if (in.key_pressed[platform::KEY_1] && !in.ctrl()) uv_select_mode_ = 0;
    if (in.key_pressed[platform::KEY_2] && !in.ctrl()) uv_select_mode_ = 1;
    if (in.key_pressed[platform::KEY_3] && !in.ctrl()) uv_select_mode_ = 2;
  }
  /* Island count only changes with the mesh: cache it by mesh + version. */
  static const Mesh *islands_mesh = nullptr;
  static uint64_t islands_version = 0;
  static int islands = 0;
  if (islands_mesh != &m || islands_version != m.version) {
    std::vector<int> fi;
    islands = uvops::compute_islands(m, nullptr, false, fi);
    islands_mesh = &m;
    islands_version = m.version;
  }
  size_t nsel = std::count(uv_sel_.begin(), uv_sel_.end(), 1);
  u.label({view.x + u.px(8), view.bottom() - u.row_h() - u.px(4), view.w, u.row_h()},
          strprintf("%s  |  %zu faces  |  %d UV islands  |  %zu corners selected  |  1/2/3 vertex/face/island, W/E/R tools, arrows nudge, L linked, U unwrap",
                    g ? g->name.c_str() : "", m.face_count(), islands, nsel),
          u.theme.text_dim);
  u.canvas.pop_clip();
}

/* ---------------------------------------------------- selection and transforms */

/* UV Sync Selection (Edit Mode): faces whose corners are all selected in UV are the selected faces. */
void Editor::uv_sync_to_faces(const Mesh &m) {
  if (!uv_sync_ || !edit_mode_ || !edit_object() || &m != edit_mesh_ptr()->get() || uv_sel_.size() != m.corner_count()) return;
  if (uv_select_mode_ == 0) return;  // vertex picks don't decide faces
  face_sel_.assign(m.face_count(), 0);
  for (size_t f = 0; f < m.face_count(); f++) {
    bool all = true;
    for (uint32_t k = m.face_offsets[f]; k < m.face_offsets[f + 1]; k++) all = all && uv_sel_[k];
    face_sel_[f] = all;
  }
  /* Keep the UV view showing every face (not only the selected ones) while syncing. */
  sync_vert_face_selection(true);
}

bool Editor::uv_select(const std::string &what, int face) {
  MeshPtr *mp = uv_target(nullptr);
  if (!mp || !*mp || !(*mp)->has_uvs()) return false;
  const Mesh &m = **mp;
  if (uv_sel_.size() != m.corner_count()) uv_sel_.assign(m.corner_count(), 0);
  auto face_corners = [&](size_t f, uint8_t v) {
    for (uint32_t k = m.face_offsets[f]; k < m.face_offsets[f + 1]; k++) uv_sel_[k] = v;
  };
  if (what == "all") std::fill(uv_sel_.begin(), uv_sel_.end(), 1);
  else if (what == "none") std::fill(uv_sel_.begin(), uv_sel_.end(), 0);
  else if (what == "invert")
    for (uint8_t &v : uv_sel_) v = !v;
  else if (what == "face") {
    if (face < 0 || (size_t)face >= m.face_count()) return false;
    std::fill(uv_sel_.begin(), uv_sel_.end(), 0);
    face_corners((size_t)face, 1);
  }
  else if (what == "island" || what == "islands") {
    std::vector<int> fi;
    uvops::compute_islands(m, nullptr, false, fi);
    std::vector<uint8_t> want(fi.empty() ? 0 : (size_t)*std::max_element(fi.begin(), fi.end()) + 1, 0);
    if (what == "island") {
      if (face < 0 || (size_t)face >= m.face_count()) return false;
      want[(size_t)fi[(size_t)face]] = 1;
      std::fill(uv_sel_.begin(), uv_sel_.end(), 0);
    }
    else  // grow: every island with anything selected
      for (size_t f = 0; f < m.face_count(); f++)
        for (uint32_t k = m.face_offsets[f]; k < m.face_offsets[f + 1]; k++)
          if (uv_sel_[k]) want[(size_t)fi[f]] = 1;
    for (size_t f = 0; f < m.face_count(); f++)
      if (want[(size_t)fi[f]]) face_corners(f, 1);
  }
  else return false;
  uv_sync_to_faces(m);
  return true;
}

/* Transforms of the selected UV corners about the selection's bounds centre. */
bool Editor::uv_transform(const std::string &op, float a, float b) {
  MeshPtr *mp = uv_target(nullptr);
  if (!mp || !*mp || !(*mp)->has_uvs()) return false;
  Mesh &m = *mesh_make_mutable(*mp);
  if (uv_sel_.size() != m.corner_count()) return false;
  Vec2 lo(1e30f, 1e30f), hi(-1e30f, -1e30f);
  size_t n = 0;
  for (size_t k = 0; k < uv_sel_.size(); k++)
    if (uv_sel_[k]) {
      lo = Vec2(std::min(lo.x, m.uvs[k].x), std::min(lo.y, m.uvs[k].y));
      hi = Vec2(std::max(hi.x, m.uvs[k].x), std::max(hi.y, m.uvs[k].y));
      n++;
    }
  if (!n) {
    Log::warn("UV: select something first (1 / 2 / 3: vertices, faces, islands)");
    return false;
  }
  const Vec2 c = (lo + hi) * 0.5f, size = hi - lo;
  auto apply = [&](const std::function<Vec2(Vec2)> &fn) {
    for (size_t k = 0; k < uv_sel_.size(); k++)
      if (uv_sel_[k]) m.uvs[k] = fn(m.uvs[k]);
  };
  if (op == "rotate") {
    const float r = a * kDeg2Rad, cs = std::cos(r), sn = std::sin(r);
    apply([&](Vec2 t) {
      const Vec2 d = t - c;
      return c + Vec2(d.x * cs - d.y * sn, d.x * sn + d.y * cs);
    });
  }
  else if (op == "scale") {
    const float sx = a, sy = b != 0.0f ? b : a;
    apply([&](Vec2 t) { return c + Vec2((t.x - c.x) * sx, (t.y - c.y) * sy); });
  }
  else if (op == "move") apply([&](Vec2 t) { return t + Vec2(a, b); });
  else if (op == "flip_u") apply([&](Vec2 t) { return Vec2(2.0f * c.x - t.x, t.y); });
  else if (op == "flip_v") apply([&](Vec2 t) { return Vec2(t.x, 2.0f * c.y - t.y); });
  else if (op == "center") apply([&](Vec2 t) { return t + Vec2(0.5f, 0.5f) - c; });
  else if (op == "fit") {
    /* Into the 0-1 square, keeping its proportions. */
    const float k = 1.0f / std::max(1e-6f, std::max(size.x, size.y));
    apply([&](Vec2 t) { return Vec2(0.5f, 0.5f) + (t - c) * k; });
  }
  else if (op == "align_left") apply([&](Vec2 t) { return Vec2(lo.x, t.y); });
  else if (op == "align_right") apply([&](Vec2 t) { return Vec2(hi.x, t.y); });
  else if (op == "align_top") apply([&](Vec2 t) { return Vec2(t.x, hi.y); });
  else if (op == "align_bottom") apply([&](Vec2 t) { return Vec2(t.x, lo.y); });
  else if (op == "align_u") apply([&](Vec2 t) { return Vec2(c.x, t.y); });   // a straight vertical line
  else if (op == "align_v") apply([&](Vec2 t) { return Vec2(t.x, c.y); });   // a straight horizontal line
  else return false;
  m.touch();
  mark_changed("UV " + op);
  return true;
}

/* The second toolbar row: selection mode and sync, selection, quick transforms. */
void Editor::draw_uv_tool_row(const Recti &bar) {
  auto &u = ui_;
  u.canvas.fill_rect(bar, Color::hex(0x2A2A2A));
  int x = bar.x + u.px(6);
  const int y = bar.y + u.px(3), h = bar.h - u.px(6);
  static const char *kModes[] = {"Vertex", "Face", "Island"};
  for (int k = 0; k < 3; k++) {
    const int w = u.font.text_width(kModes[k]) + u.px(14);
    if (u.button({x, y, w, h}, kModes[k], uv_select_mode_ == k)) uv_select_mode_ = k;
    u.tooltip(k == 0 ? "Pick UV vertices (1)." : k == 1 ? "Pick whole faces: click inside one, box by centre (2)." : "Pick whole islands: click any face of one; drag to move it (3).");
    x += w + u.px(2);
  }
  {
    const int w = u.font.text_width("Sync") + u.px(14);
    if (u.button({x + u.px(4), y, w, h}, "Sync", uv_sync_)) uv_sync_ = !uv_sync_;
    u.tooltip("Faces picked here are selected in Edit Mode too (Blender: UV Sync Selection).");
    x += w + u.px(10);
  }
  struct B {
    const char *label;
    std::function<void()> fn;
    const char *tip;
  };
  const B buttons[] = {
      {"All", [this] { uv_select("all"); }, "Select everything (Ctrl+A)."},
      {"None", [this] { uv_select("none"); }, "Deselect (Ctrl+Shift+A)."},
      {"Invert", [this] { uv_select("invert"); }, "Invert the selection (Ctrl+I)."},
      {"Islands", [this] { uv_select("islands"); }, "Grow the selection to whole islands (Blender: Select Linked)."},
      {"-90", [this] { uv_transform("rotate", -90); }, "Rotate the selection 90 degrees clockwise."},
      {"+90", [this] { uv_transform("rotate", 90); }, "Rotate the selection 90 degrees counter-clockwise."},
      {"Flip U", [this] { uv_transform("flip_u"); }, "Mirror the selection left to right."},
      {"Flip V", [this] { uv_transform("flip_v"); }, "Mirror the selection top to bottom."},
      {"Fit", [this] { uv_transform("fit"); }, "Scale the selection to fill the 0-1 square (keeps its proportions)."},
      {"Center", [this] { uv_transform("center"); }, "Move the selection to the middle of the 0-1 square."},
  };
  for (const B &b : buttons) {
    const int w = u.font.text_width(b.label) + u.px(12);
    if (x + w > bar.right() - u.px(330)) break;
    if (u.button({x, y, w, h}, b.label)) b.fn();
    u.tooltip(b.tip);
    x += w + u.px(2);
  }
  /* Align: one dropdown instead of six buttons. */
  if (x + u.px(84) <= bar.right() - u.px(330)) {
    static const char *kAlign[] = {"Align...", "Left", "Right", "Top", "Bottom", "Straight U", "Straight V"};
    static const char *kOps[] = {"", "align_left", "align_right", "align_top", "align_bottom", "align_u", "align_v"};
    int pick = 0;
    if (u.combo(u.id("uv_align"), {x, y, u.px(80), h}, pick, kAlign, 7) && pick > 0) uv_transform(kOps[pick]);
    u.tooltip("Line the selected UVs up on an edge of their bounds, or straighten them into one vertical / horizontal line.");
  }
  /* Exact values: rotate by, scale by, move by. */
  int rx = bar.right() - u.px(326);
  auto num = [&](const char *id, float &v, float speed, const char *tip) {
    u.float_field(u.id(id), {rx, y, u.px(46), h}, v, speed, -10000.0f, 10000.0f, "%.3g");
    u.tooltip(tip);
    rx += u.px(48);
  };
  num("uv_rot", uv_rot_field_, 1.0f, "Angle (degrees) for Rotate.");
  if (u.button({rx, y, u.px(50), h}, "Rotate")) uv_transform("rotate", uv_rot_field_);
  rx += u.px(54);
  num("uv_scl", uv_scale_field_, 0.01f, "Factor for Scale.");
  if (u.button({rx, y, u.px(44), h}, "Scale")) uv_transform("scale", uv_scale_field_);
  rx += u.px(48);
  num("uv_mu", uv_move_u_, 0.005f, "U offset for Move.");
  num("uv_mv", uv_move_v_, 0.005f, "V offset for Move.");
  if (u.button({rx, y, u.px(40), h}, "Move")) uv_transform("move", uv_move_u_, uv_move_v_);
}

}  // namespace bl
