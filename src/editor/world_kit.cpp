// SPDX-License-Identifier: GPL-2.0-or-later
// The VRChat world kit in the editor (round 362, task 0339): colliders fitted and drawn, and Generate Lighting's Fix
// that marks the still meshes Contribute GI - the shapes are scene/colliders'.
#include "editor.h"

#include "../scene/colliders.h"
#include "../scene/lod.h"
#include "../scene/export.h"
#include "../core/core.h"
#include "../image/image.h"

namespace bl {

/* An LOD Group's mesh for the Game view: its height on screen (its bounds' height over the distance, by the focal term)
 * picks the level; nullptr when the group culls it. Every other view, and an object without a group, draws `m`. */
const Mesh *Editor::lod_for_view(const GameObject &g, const Mesh &m) {
  const LODGroupComponent *lg = g.get<LODGroupComponent>();
  if (!lg || !lg->enabled || !lod_view_.on || lod_deforms(g)) return &m;
  const AABB b = m.render_mesh().bounds.transformed(g.world_matrix());  // (the mesh this view draws - review 363: not another evaluation)
  const float height = b.valid() ? std::max(b.max.y - b.min.y, std::max(b.max.x - b.min.x, b.max.z - b.min.z)) : 0.0f;
  const float dist = std::max(1e-3f, length(b.center() - lod_view_.eye));
  const float screen = height * lod_view_.half_focal / (lod_view_.ortho ? 1.0f : dist);
  const int level = lod_level(*lg, screen);
  return level < 0 ? nullptr : lod_mesh(*lg, m, level);
}

/* Fit to Mesh ticked (or a collider just added): the collider sized to its mesh, the tick cleared - part of the step
 * that ticked it, never a step of its own (a loaded or undone state asking for it is fitted quietly). */
void Editor::update_collider_fits() {
  bool any = false;
  scene_->for_each([&](GameObject &g) {
    ColliderComponent *c = g.get<ColliderComponent>();
    if (!c || !c->fit) return;
    c->fit = false;
    const Mesh *m = g.evaluated_mesh();
    if (!m) return;
    fit_collider(*c, *m);
    any = true;
  });
  if (any) scene_dirty_ = true;
}

/* A collider's wire mesh, kept while its fields and its mesh stay the same (review 362: one made a frame each). */
const Mesh *Editor::collider_wire(const GameObject &g, const ColliderComponent &c) {
  static const Mesh kEmpty;
  const Mesh *m = g.evaluated_mesh();
  const Mesh &src = m ? *m : kEmpty;  // (an empty's Box, Sphere or Capsule: an invisible wall)
  uint64_t key = (uint64_t)c.shape * 1000003ull + src.version;
  for (float f : {c.center.x, c.center.y, c.center.z, c.size.x, c.size.y, c.size.z, c.radius, c.height}) key = key * 31 + (uint64_t)(f * 10000.0f);
  ColliderWire &w = collider_wires_[g.id];
  if (w.mesh && w.key == key) return w.mesh.get();
  w = {key, collider_mesh(c, src)};
  return w.mesh.get();
}

/* Each collider as a green wire (Unity's colour for them), over the mesh. */
void Editor::draw_colliders() {
  scene_->for_each([&](GameObject &g) {
    const ColliderComponent *c = g.get<ColliderComponent>();
    if (!c || !c->enabled || !g.active_in_hierarchy() || c->shape == (int)ColliderShape::Mesh) return;
    const Mesh *shape = collider_wire(g, *c);
    if (!shape) return;
    const Mat4 &w = g.world_matrix();
    const uint32_t col = Color::hex(0x7CFC6C, is_selected(g.id) ? 255 : 160);
    for (const auto &e : shape->edge_cache()) scene_r3d_.line(w.point(shape->positions[e.first]), w.point(shape->positions[e.second]), col, true, 3e-4f);
  });
}

/* Round 397: each VRC Light Volume as its box (amber, as the package draws it) and, selected, its voxel grid's centres. */
void Editor::draw_light_volumes() {
  scene_->for_each([&](GameObject &g) {
    const LightVolumeComponent *v = g.get<LightVolumeComponent>();
    if (!v || !v->enabled || !g.active_in_hierarchy()) return;
    const Mat4 &w = g.world_matrix();
    const bool sel = is_selected(g.id);
    const uint32_t col = Color::hex(0xFFB020, sel ? 255 : 150);
    for (int e = 0; e < 12; e++) {  // (the unit cube's edges: two corners differing in one axis)
      const int axis = e / 4, a = (axis + 1) % 3, b = (axis + 2) % 3;
      Vec3 p0(-0.5f), p1(-0.5f);
      p0[a] = p1[a] = (e & 1) ? 0.5f : -0.5f;
      p0[b] = p1[b] = (e & 2) ? 0.5f : -0.5f;
      p1[axis] = 0.5f;
      scene_r3d_.line(w.point(p0), w.point(p1), col, true, 3e-4f);
    }
    if (sel) draw_light_volume_grid(g, *v);
  });
}

/* A selected volume's voxel centres as dots - each axis at most 32 shown (a fine grid would hide the scene); with a bake
 * (Blendity's or Unity's read back, round 398), each dot its voxel's light (L0, clamped) - the bake previewed. */
void Editor::draw_light_volume_grid(const GameObject &g, const LightVolumeComponent &v) {
  int res[3];
  light_volume_resolution(g, v, res);
  const Mat4 &w = g.world_matrix();
  const auto baked = light_volume_bakes_.find(g.id);
  const LightVolumeSH *sh = baked != light_volume_bakes_.end() && std::memcmp(baked->second.res, res, sizeof(res)) == 0 ? &baked->second : nullptr;
  auto dot_color = [&](int x, int y, int z) {
    if (!sh) return Color::hex(0xFFB020, 140);
    const Vec3 c = sh->l0[(size_t)x + (size_t)res[0] * ((size_t)y + (size_t)res[1] * (size_t)z)];
    auto ch = [](float f) { return std::clamp(linear_to_srgb(std::max(0.0f, f)), 0.0f, 1.0f); };
    return Color::rgba(ch(c.x), ch(c.y), ch(c.z), 0.9f);
  };
  const int step[3] = {(res[0] + 31) / 32, (res[1] + 31) / 32, (res[2] + 31) / 32};  // (review 397: res / 32 had shown up to 63 an axis)
  for (int z = 0; z < res[2]; z += step[2])
    for (int y = 0; y < res[1]; y += step[1])
      for (int x = 0; x < res[0]; x += step[0])
        scene_r3d_.point(w.point(Vec3((x + 0.5f) / res[0] - 0.5f, (y + 0.5f) / res[1] - 0.5f, (z + 0.5f) / res[2] - 0.5f)), sh ? 3.0f : 2.0f, dot_color(x, y, z));
}

/* An export's collider: the object's ColliderComponent as a mesh named as `o`'s engine reads it (no materials, no
 * skin: engines hide and collide it). */
void Editor::add_collider_item(GameObject &g, const Mesh &m, const ExportItem &it, const ExportOptions &o, std::vector<ExportItem> &items, std::vector<std::shared_ptr<Mesh>> &keep) {
  const ColliderComponent *c = g.get<ColliderComponent>();
  /* (review 363: with its LODs exported, the collider follows LOD0's name - Unreal's UCX_ names the render mesh) */
  const std::string object = exports_lods(g, o) ? lod_export_name(g.name, 0) : g.name;
  const std::string name = c && c->enabled ? collider_export_name(o.collider_names, c->shape, object) : "";
  if (name.empty()) return;
  std::shared_ptr<Mesh> shape = collider_mesh(*c, m);
  if (!shape) return;
  keep.push_back(shape);
  ExportItem ci;
  ci.name = name;
  ci.mesh = shape.get();
  ci.world = it.world;
  ci.outline = true;  // (no lightmap of its own, as an Outline's shell)
  ci.collider = true;
  items.push_back(ci);
}

/* Generate Lighting with nothing to bake: why, and its Fix - once for a lighting key under Auto Generate (review 362: a
 * refusal a second); "Contribute GI on but hidden" said as itself (review 362: Mark Static had no answer for it). */
void Editor::bake_refuse_empty() {
  const uint64_t key = lightmap_scene_key();
  if (scene_->lighting.auto_generate && key == bake_refused_key_) return;
  bake_refused_key_ = key;
  int hidden = 0;
  scene_->for_each([&](GameObject &g) {
    const MeshRenderer *mr = g.get<MeshRenderer>();
    hidden += mr && mr->contribute_gi && (!mr->enabled || !mr->show_in_renders || !g.active_in_hierarchy()) ? 1 : 0;
  });
  if (hidden) return refuse(strprintf("Generate Lighting: the %d mesh(es) with Contribute GI on are hidden from renders or inactive - show them to bake them.", hidden), "", "");
  refuse("Generate Lighting: no object has Contribute GI on (Unity's Static) - mark the still meshes, or tick it in their MeshRenderer.", "lighting markstatic",
         "Mark Static Meshes");
}

/* Something above `g` (or itself) moves it: a Rigidbody, an Animation, a moving script (review 362: a windmill's blades
 * and a door had been marked still). */
static bool moves(const GameObject &g) {
  for (const GameObject *a = &g; a; a = a->parent)
    if (a->get<Rigidbody>() || a->get<Animation>() || a->get<Oscillator>() || a->get<PlayerController>()) return true;
  return false;
}

/* An export's LOD levels: the object's own item renamed <name>_LOD0, each next level a decimated copy beside it (its
 * skin made again for its vertices) - Unity's and Unreal's import read the suffix. Nothing without an LOD Group. */
bool Editor::exports_lods(const GameObject &g, const ExportOptions &o) {
  const LODGroupComponent *lg = g.get<LODGroupComponent>();
  return o.lods && lg && lg->enabled && lg->levels >= 2 && meshops::decimate_available();  // (review 363: no full copies where nothing decimates)
}

void Editor::add_lod_items(GameObject &g, const Mesh &m, std::vector<ExportItem> &items, size_t index, const ExportOptions &o, GameObject *arm, const SkinModifier *skm) {
  if (!exports_lods(g, o) || index >= items.size()) return;
  const LODGroupComponent *lg = g.get<LODGroupComponent>();
  ExportItem &base = items[index];
  base.name = lod_export_name(g.name, 0);
  const ExportItem copy = base;
  for (int level = 1; level < std::clamp(lg->levels, 1, LODGroupComponent::kMax); level++) {
    ExportItem li = copy;
    li.name = lod_export_name(g.name, level);
    li.mesh = lod_mesh(*lg, m, level);
    if (arm && skm) li.skin = export_skin_of(*arm, *li.mesh, g, *skm);
    items.push_back(li);
  }
}

/* lighting markstatic: Contribute GI on every mesh that stays still - no Rigidbody, no Skin, no Animation of its own
 * (Unity's Static flag; what Generate Lighting bakes). */
void Editor::mark_static_meshes() {
  int n = 0;
  scene_->for_each([&](GameObject &g) {
    MeshRenderer *mr = g.get<MeshRenderer>();
    if (!mr || mr->contribute_gi || !g.active_in_hierarchy() || g.get<SkinModifier>() || !g.get<MeshFilter>() || moves(g)) return;
    mr->contribute_gi = true;
    n++;
  });
  if (!n) return Log::info("Mark Static: every still mesh contributes GI already (or there are none)");
  mark_changed("Mark Static");
  Log::info("Mark Static: %d still mesh(es) now Contribute GI - Generate Lighting bakes them", n);
}

}  // namespace bl
