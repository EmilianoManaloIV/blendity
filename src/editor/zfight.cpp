// SPDX-License-Identifier: GPL-2.0-or-later
// Z-fighting check: faces lying in one plane and covering the same area flicker
// (the depth buffer can't tell which is in front). Scans the selection (or the
// whole scene, across objects, in world space), lists each pair with what can
// go - a duplicate's copy, both sides of an inner wall, a face hidden under
// another - outlines them in the Scene view, and removes the suggested faces.
#include "editor.h"

#include <algorithm>

namespace bl {

size_t Editor::zfight_scan(bool scene_wide) {
  zfight_.clear();
  zfight_scene_ = scene_wide;
  std::vector<GameObject *> objs;
  if (scene_wide) {
    scene_->for_each([&](GameObject &g) {
      if (g.active_in_hierarchy() && g.get<MeshFilter>() && g.get<MeshFilter>()->mesh) objs.push_back(&g);
    });
  }
  else if (edit_mode_ && edit_object()) objs.push_back(edit_object());
  else
    for (GameObject *g : selected_objects(false))
      if (g->get<MeshFilter>() && g->get<MeshFilter>()->mesh) objs.push_back(g);
  /* One mesh in world space; each face remembers its object and own index. */
  Mesh all;
  std::vector<std::pair<uint64_t, uint32_t>> owner;
  for (GameObject *g : objs) {
    const Mesh &m = *g->get<MeshFilter>()->mesh;
    const size_t f0 = all.face_count();
    meshops::append_mesh(all, m, g->world_matrix());
    for (size_t f = f0; f < all.face_count(); f++) owner.push_back({g->id, (uint32_t)(f - f0)});
  }
  if (!all.face_count()) {
    Log::info("Z-Fighting: nothing to check (select meshes, or check the scene)");
    return 0;
  }
  const std::vector<meshops::ZFightPair> pairs = meshops::zfight_pairs(all, 1e-4f * std::max(1.0f, length(all.bounds().extent())));
  for (const meshops::ZFightPair &p : pairs) {
    if (p.a >= owner.size() || p.b >= owner.size()) continue;
    /* Two objects touching back to back (a box standing on the ground) is contact, not z-fighting:
     * only the same polygon twice counts between objects then. */
    if (owner[p.a].first != owner[p.b].first && !p.same_direction && !p.identical) continue;
    ZFightIssue z;
    z.obj_a = owner[p.a].first;
    z.face_a = owner[p.a].second;
    z.obj_b = owner[p.b].first;
    z.face_b = owner[p.b].second;
    z.pair = p;
    if (GameObject *ga = scene_->find(z.obj_a)) z.version_a = ga->get<MeshFilter>()->mesh->version;
    if (GameObject *gb = scene_->find(z.obj_b)) z.version_b = gb->get<MeshFilter>()->mesh->version;
    /* World-space outlines, for drawing them. */
    for (int k = 0; k < 2; k++) {
      const uint32_t f = k ? p.b : p.a;
      for (uint32_t c = 0; c < all.face_size(f); c++) z.outline[k].push_back(all.positions[all.face_verts(f)[c]]);
    }
    zfight_.push_back(std::move(z));
  }
  size_t removable = 0;
  for (const ZFightIssue &z : zfight_) removable += z.pair.remove_a || z.pair.remove_b;
  Log::info("Z-Fighting: %zu pair(s) of faces on top of each other in %zu object(s); %zu can be fixed by removing faces", zfight_.size(), objs.size(),
            removable);
  return zfight_.size();
}

std::string Editor::zfight_describe(const ZFightIssue &z) const {
  const GameObject *ga = scene_->find(z.obj_a), *gb = scene_->find(z.obj_b);
  const std::string na = ga ? ga->name : "?", nb = gb ? gb->name : "?";
  const std::string where = z.obj_a == z.obj_b ? strprintf("%s: faces %u and %u", na.c_str(), z.face_a, z.face_b)
                                               : strprintf("%s face %u / %s face %u", na.c_str(), z.face_a, nb.c_str(), z.face_b);
  std::string what;
  if (z.pair.identical && !z.pair.same_direction) what = "back to back (an inner wall): remove both";
  else if (z.pair.identical) what = "a duplicate: remove one";
  else if (z.pair.remove_a || z.pair.remove_b) what = "one is hidden under the other: remove it";
  else what = strprintf("partly overlapping (%.0f%% / %.0f%%): move one of them", z.pair.covered_a * 100.0f, z.pair.covered_b * 100.0f);
  return where + " - " + what;
}

/* Remove the suggested faces of issue `index` (-1: every issue). */
size_t Editor::zfight_fix(int index) {
  std::unordered_map<uint64_t, std::vector<uint8_t>> drop;  // object -> faces to delete
  for (size_t i = 0; i < zfight_.size(); i++) {
    if (index >= 0 && (int)i != index) continue;
    const ZFightIssue &z = zfight_[i];
    for (int k = 0; k < 2; k++) {
      if (!(k ? z.pair.remove_b : z.pair.remove_a)) continue;
      const uint64_t id = k ? z.obj_b : z.obj_a;
      const uint32_t f = k ? z.face_b : z.face_a;
      GameObject *g = scene_->find(id);
      if (!g || !g->get<MeshFilter>() || !g->get<MeshFilter>()->mesh) continue;
      auto &d = drop[id];
      d.resize(g->get<MeshFilter>()->mesh->face_count(), 0);
      if (f < d.size()) d[f] = 1;
    }
  }
  size_t removed = 0;
  for (auto &kv : drop) {
    GameObject *g = scene_->find(kv.first);
    MeshPtr &mp = (edit_mode_ && edit_obj_ == kv.first) ? *edit_mesh_ptr() : g->get<MeshFilter>()->mesh;
    Mesh &m = *mesh_make_mutable(mp);
    removed += (size_t)std::count(kv.second.begin(), kv.second.end(), 1);
    meshops::delete_faces(m, kv.second);
    m.touch();
    if (edit_mode_ && edit_obj_ == kv.first) {
      face_sel_.assign(m.face_count(), 0);
      vert_sel_.assign(m.vert_count(), 0);
    }
  }
  if (removed) {
    mark_changed("Fix Z-Fighting");
    Log::info("Z-Fighting: removed %zu face(s)", removed);
  }
  zfight_scan(zfight_scene_);  // what is left
  return removed;
}

void Editor::zfight_select(int index) {
  if (index < 0 || index >= (int)zfight_.size()) return;
  const ZFightIssue &z = zfight_[(size_t)index];
  /* Edit the first object of the pair with both faces (of it) selected. */
  select(z.obj_a);
  if (!edit_mode_ || edit_obj_ != z.obj_a) {
    if (edit_mode_) exit_edit_mode();
    enter_edit_mode();
  }
  if (!edit_object()) return;
  const Mesh &m = **edit_mesh_ptr();
  set_edit_element(EditElement::Face);
  face_sel_.assign(m.face_count(), 0);
  if (z.face_a < face_sel_.size()) face_sel_[z.face_a] = 1;
  if (z.obj_b == z.obj_a && z.face_b < face_sel_.size()) face_sel_[z.face_b] = 1;
  sync_vert_face_selection(true);
}

/* An issue lasts while both objects exist with the meshes it was found on. */
void Editor::zfight_prune() {
  auto current = [&](uint64_t id, uint64_t version) {
    GameObject *g = scene_->find(id);
    const MeshFilter *mf = g ? g->get<MeshFilter>() : nullptr;
    if (!mf || !mf->mesh) return false;
    const Mesh *m = edit_mode_ && edit_obj_ == id ? edit_mesh_ptr()->get() : mf->mesh.get();
    return m && m->version == version;
  };
  zfight_.erase(std::remove_if(zfight_.begin(), zfight_.end(),
                               [&](const ZFightIssue &z) { return !current(z.obj_a, z.version_a) || !current(z.obj_b, z.version_b); }),
                zfight_.end());
}

/* Outlines only while they mean something: editing one of the objects involved, or with the
 * Z-Fighting panel open after checking from it - not in Object Mode left over from a selection. */
bool Editor::zfight_outlines_visible() const {
  if (zfight_.empty() || !zfight_show_) return false;
  if (zfight_from_panel_ && frames_ <= zfight_panel_frame_ + 1) return true;
  if (edit_mode_)
    for (const ZFightIssue &z : zfight_)
      if (z.obj_a == edit_obj_ || z.obj_b == edit_obj_) return true;
  return false;
}

/* The flagged faces outlined in the Scene view: orange, red for the ones that can go. */
void Editor::draw_zfight_overlay(const Recti &view) {
  zfight_prune();
  if (!zfight_outlines_visible()) return;
  auto &u = ui_;
  u.canvas.push_clip(view);
  for (size_t i = 0; i < zfight_.size() && i < 500; i++) {
    const ZFightIssue &z = zfight_[i];
    for (int k = 0; k < 2; k++) {
      const auto &loop = z.outline[k];
      const bool goes = k ? z.pair.remove_b : z.pair.remove_a;
      const uint32_t col = goes ? Color::hex(0xFF3030) : Color::hex(0xFFA020);
      for (size_t c = 0; c < loop.size(); c++) {
        Vec2 a, b;
        float za, zb;
        if (!scene_r3d_.project(loop[c], a, za) || !scene_r3d_.project(loop[(c + 1) % loop.size()], b, zb)) continue;
        u.canvas.line(view.x + a.x, view.y + a.y, view.x + b.x, view.y + b.y, col, goes ? 2.0f : 1.5f);
      }
    }
  }
  u.canvas.pop_clip();
}

/* The panel (Modeling Tools): scan, the list, fix. */
void Editor::draw_zfight_panel(ui::Layout &lay) {
  auto &u = ui_;
  zfight_panel_frame_ = frames_;
  zfight_prune();
  Recti r = lay.row(u.row_h() + u.px(2));
  const int bw = (r.w - u.px(12)) / 3;
  if (u.button({r.x + u.px(4), r.y, bw, r.h}, "Check Selection")) {
    zfight_scan(false);
    zfight_from_panel_ = true;
  }
  u.tooltip("Find faces on top of each other (they flicker: z-fighting) in the selected objects, or the mesh being edited.");
  if (u.button({r.x + u.px(6) + bw, r.y, bw, r.h}, "Check Scene")) {
    zfight_scan(true);
    zfight_from_panel_ = true;
  }
  u.tooltip("The whole scene, including faces of different objects lying on top of each other.");
  size_t removable = 0;
  for (const ZFightIssue &z : zfight_) removable += z.pair.remove_a || z.pair.remove_b;
  if (u.button({r.x + u.px(8) + 2 * bw, r.y, bw, r.h}, strprintf("Remove Suggested (%zu)", removable)) && removable) zfight_fix(-1);
  u.tooltip("Remove every face the check suggests: duplicates' copies, both sides of inner walls, faces hidden under others.");
  if (zfight_.empty()) {
    u.label(lay.row(), "No z-fighting found (or not checked yet).", u.theme.text_dim);
    return;
  }
  Recti tr = lay.row(u.row_h() + u.px(2));
  if (u.button({tr.x + u.px(4), tr.y, tr.w - u.px(8), tr.h}, zfight_show_ ? "Outlines in the Scene View (on)" : "Outlines in the Scene View (off)",
               zfight_show_))
    zfight_show_ = !zfight_show_;
  for (size_t i = 0; i < zfight_.size() && i < 30; i++) {
    const ZFightIssue &z = zfight_[i];
    Recti row = lay.row(u.row_h() + u.px(2));
    const bool fixable = z.pair.remove_a || z.pair.remove_b;
    u.label({row.x + u.px(4), row.y, row.w - u.px(150), row.h}, zfight_describe(z), fixable ? u.theme.text : u.theme.text_dim);
    if (u.button({row.right() - u.px(144), row.y, u.px(68), row.h}, "Select")) zfight_select((int)i);
    if (fixable && u.button({row.right() - u.px(72), row.y, u.px(68), row.h}, "Remove")) {
      zfight_fix((int)i);
      break;  // the list changed
    }
  }
  if (zfight_.size() > 30) u.label(lay.row(), strprintf("... and %zu more", zfight_.size() - 30), u.theme.text_dim);
}

}  // namespace bl
