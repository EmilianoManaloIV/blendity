// SPDX-License-Identifier: GPL-2.0-or-later
// The Scatter brush (ADR 0084, task 0248): a dab on a surface places instances of a source object - GameObjects sharing
// its mesh and materials (copy on write: no copies until one is edited) - as children of one Scatter parent, by a density
// per square metre of the dab's disc, jittered from a seed (a dab is repeatable), aligned to the surface normal or world
// up, scaled and turned at random, tinted through a material copy when asked; a Paint layer's coverage keeps them off
// painted texels (paths); the eraser end removes those within the disc; a merge joins them into one mesh for export.
#include "editor.h"

#include "../core/core.h"
#include "../scene/raycast.h"
#include "../scene/stroke_object.h"
#include "../texture/paint_layer.h"
#include "../texture/surface.h"

#include <cmath>

namespace bl {

namespace {
inline uint32_t scatter_hash(uint32_t x) {
  x ^= x >> 16, x *= 0x7feb352dU, x ^= x >> 15, x *= 0x846ca68bU, x ^= x >> 16;
  return x;
}
inline float scatter_rand(uint32_t seed, uint32_t k) { return (scatter_hash(seed * 0x9E3779B9u + k) & 0xFFFFFF) / 16777216.0f; }
}  // namespace

void Editor::set_scatter(bool on) {
  if (on == scatter_) return;
  if (on) {
    if (paint_) set_paint(false);
    if (sculpt_) set_sculpt(false);
    if (strokeobj_) set_strokeobj(false);
    if (scatter_brush_.source.empty() || !scene_->find_by_name(scatter_brush_.source)) {
      refuse("Scatter: pick a Source first (Modeling Tools > Scatter)", "window Modeling Tools", "Open Scatter");
      return;
    }
  }
  scatter_ = on;
  scatter_stroking_ = false;
  ui_.redraw = true;
  Log::info("Scatter: %s", on ? "on - a dab places instances; the pen's eraser end (or scatter erase) removes them; Esc leaves" : "off");
}

GameObject *Editor::scatter_parent(bool make) {
  GameObject *g = scatter_parent_ ? scene_->find(scatter_parent_) : nullptr;
  if (g && (g->name.rfind("Scatter", 0) != 0 || g->mesh_filter())) g = nullptr, scatter_parent_ = 0;  // (an id reused by another object after an undo or a load: not ours)
  if (g) return g;
  if (!make) return nullptr;
  std::string name = "Scatter";
  for (int k = 1; scene_->find_by_name(name); k++) name = strprintf("Scatter.%03d", k);
  g = scene_->create(name);
  g->set_local_position(Vec3()), g->set_local_rotation(Quat()), g->set_local_scale(Vec3(1.0f));  // (at the origin: the instances' positions are world positions)
  scatter_parent_ = g->id;
  return g;
}

/* Whether the surface's mask layer covers the world point: the point's UV from the hit object's mesh (the triangle it
 * lies on, by a short ray along the normal), the Paint layer's coverage there. */
bool Editor::scatter_masked(GameObject &surface_obj, Vec3 world_point) {
  scatter_mask_why_.clear();
  if (scatter_brush_.mask_layer < 0) return false;
  std::string why;
  ProceduralTexturePtr pt = surface_of(&surface_obj, &why);
  if (!pt || scatter_brush_.mask_layer >= (int)pt->layers.size()) {
    scatter_mask_why_ = pt ? strprintf("no layer %d (%zu layers)", scatter_brush_.mask_layer, pt->layers.size()) : "no surface: " + why;
    return false;
  }
  TextureLayer *l = pt->layers[(size_t)scatter_brush_.mask_layer].get();
  if (std::string(l->type_name()) != "Paint") {
    scatter_mask_why_ = std::string("layer is ") + l->type_name();
    return false;
  }
  PaintLayer *pl = static_cast<PaintLayer *>(l);
  const PaintImages &im = paint_layer_images(*pl, std::max(pt->width, pt->height));
  if (im.empty()) {
    scatter_mask_why_ = "no images";
    return false;
  }
  const MeshFilter *mf = surface_obj.mesh_filter();
  if (!mf || !mf->mesh) return false;
  const Mesh &mesh = *mf->mesh;
  const RenderMesh &rm = mesh.render_mesh();
  if (rm.tri_corner.size() != rm.indices.size() || mesh.uvs.size() != mesh.corner_verts.size()) {
    scatter_mask_why_ = strprintf("corners %zu / indices %zu, uvs %zu / corner_verts %zu", rm.tri_corner.size(), rm.indices.size(), mesh.uvs.size(), mesh.corner_verts.size());
    return false;
  }
  const Mat4 inv = surface_obj.world_matrix().inverse();
  const Vec3 p = inv.point(world_point);
  /* The nearest triangle by the point's distance to its plane within its bounds: the point lies on the surface. */
  float best = 1e30f;
  Vec2 uv;
  for (size_t t = 0; t < rm.tri_count(); t++) {
    const uint32_t *ix = &rm.indices[t * 3], *cn = &rm.tri_corner[t * 3];
    const Vec3 a = rm.positions[ix[0]], b = rm.positions[ix[1]], c = rm.positions[ix[2]];
    /* The cheap reject first: the point outside the triangle's bounds (grown a little) cannot lie on it. */
    const float eps = 1e-3f;
    if (p.x < std::min({a.x, b.x, c.x}) - eps || p.x > std::max({a.x, b.x, c.x}) + eps || p.y < std::min({a.y, b.y, c.y}) - eps || p.y > std::max({a.y, b.y, c.y}) + eps ||
        p.z < std::min({a.z, b.z, c.z}) - eps || p.z > std::max({a.z, b.z, c.z}) + eps)
      continue;
    const Vec3 n = cross(b - a, c - a);
    const float nl = length(n);
    if (nl < 1e-12f) continue;
    const float d = std::fabs(dot(p - a, n) / nl);
    if (d > best) continue;
    /* Barycentrics by projection onto the plane. */
    const Vec3 v0 = b - a, v1 = c - a, v2 = p - a;
    const float d00 = dot(v0, v0), d01 = dot(v0, v1), d11 = dot(v1, v1), d20 = dot(v2, v0), d21 = dot(v2, v1);
    const float den = d00 * d11 - d01 * d01;
    if (std::fabs(den) < 1e-12f) continue;
    const float bv = (d11 * d20 - d01 * d21) / den, bw = (d00 * d21 - d01 * d20) / den, bu = 1.0f - bv - bw;
    if (bu < -0.02f || bv < -0.02f || bw < -0.02f) continue;
    best = d;
    uv = mesh.uvs[cn[0]] * bu + mesh.uvs[cn[1]] * bv + mesh.uvs[cn[2]] * bw;
  }
  if (best > 1e29f) {
    scatter_mask_why_ = "no triangle under the point";
    return false;
  }
  const int x = std::max(0, std::min(im.w - 1, (int)(uv.x * im.w))), y = std::max(0, std::min(im.h - 1, (int)((1.0f - uv.y) * im.h)));
  scatter_mask_why_ = strprintf("uv (%.3f, %.3f) texel (%d, %d) cover %d", uv.x, uv.y, x, y, (int)im.cover[(size_t)y * im.w + x]);
  return im.cover[(size_t)y * im.w + x] != 0;
}

/* A dab: `density x area` points in the disc round the hit, each dropped onto the surface by a ray along the normal,
 * kept when it lands on the same object and is not masked; `erase` removes the parent's children within the disc. */
bool Editor::scatter_dab(const Recti &, float x, float y, bool erase) {
  const Ray ray = scene_r3d_.screen_ray(x, y);
  SceneHit hit;
  const uint64_t parent_id = scatter_parent_;
  auto skip = [&](const GameObject &g) {
    if (g.get<Strokes>()) return true;
    for (const GameObject *p = &g; p; p = p->parent)
      if (p->id == parent_id && parent_id) return true;
    return false;
  };
  if (!scene_raycast(*scene_, ray.origin, ray.dir, 1e5f, hit, skip)) return false;
  const ScatterBrush &b = scatter_brush_;
  const float r = std::max(0.01f, b.radius);
  if (erase) {
    GameObject *parent = scatter_parent(false);
    if (!parent) return true;
    std::vector<GameObject *> gone;
    for (GameObject *c : parent->children)
      if (length(c->world_position() - hit.point) <= r) gone.push_back(c);
    if (gone.empty()) return true;
    for (GameObject *c : gone) scene_->destroy(c);
    mark_changed("Scatter Erase");
    return true;
  }
  GameObject *src = scene_->find_by_name(b.source);
  const MeshFilter *smf = src ? src->mesh_filter() : nullptr;
  if (!smf || !smf->mesh) {
    Log::warn("Scatter: no mesh object named '%s'", b.source.c_str());
    return false;
  }
  GameObject *surface_obj = const_cast<GameObject *>(hit.object);
  GameObject *parent = scatter_parent(true);
  const Vec3 n = normalize(hit.normal);
  Vec3 tangent = std::fabs(n.y) < 0.9f ? normalize(cross(Vec3(0.0f, 1.0f, 0.0f), n)) : Vec3(1.0f, 0.0f, 0.0f);
  const Vec3 bitangent = cross(n, tangent);
  const float area = 3.14159265f * r * r;
  const double asked = (double)std::max(0.0f, b.density) * area;
  if (asked > 5000.0) {
    Log::warn("Scatter: %.0f instances asked for one dab (the cap is 5,000: a smaller radius or density)", asked);
    return false;
  }
  const int want = std::max(1, (int)std::lround(asked));
  MaterialPtr tinted;  // (one tinted copy a dab, its own asset: relinking keeps it)
  if (b.tint)
    if (const MeshRenderer *smr = src->get<MeshRenderer>())
      if (!smr->materials.empty() && smr->materials[0]) {
        tinted = std::make_shared<Material>(*smr->materials[0]);
        tinted->asset_path.clear();
        tinted->name = smr->materials[0]->name + " Tinted";
        tinted->base_color = tinted->base_color * b.tint_color;
        tinted->touch();
      }
  /* The seed and the hit point make a dab repeatable: the same brush at the same spot places the same instances. */
  const uint32_t dab_seed = scatter_hash(b.seed * 7919u + (uint32_t)std::lround(hit.point.x * 97.0f) * 131u + (uint32_t)std::lround(hit.point.y * 97.0f) * 17u + (uint32_t)std::lround(hit.point.z * 97.0f));
  scatter_dabs_++;
  int placed = 0;
  for (int i = 0; i < want; i++) {
    /* A point in the disc by its area (sqrt of the radius' share), jittered on a grid of `want` cells: even without
     * rows. */
    const float a = i * 2.399963f + (scatter_rand(dab_seed, (uint32_t)i * 5u) - 0.5f) * 0.6f, rr = std::sqrt((i + 0.5f) / (float)want) * r;  // (the golden angle: even over the disc, jittered)
    const Vec3 q = hit.point + tangent * (std::cos(a) * rr) + bitangent * (std::sin(a) * rr);
    SceneHit drop;
    if (!scene_raycast(*scene_, q + n * (r * 2.0f), n * -1.0f, r * 4.0f, drop, skip) || drop.object != hit.object) continue;
    if (scatter_masked(*surface_obj, drop.point)) continue;
    GameObject *inst = scene_->create(b.source + " Instance", parent);
    (void)parent;
    inst->add<MeshFilter>()->mesh = smf->mesh;
    MeshRenderer *mr = inst->add<MeshRenderer>();
    if (const MeshRenderer *smr = src->get<MeshRenderer>()) {
      mr->materials = smr->materials;
      if (tinted && !mr->materials.empty()) mr->materials[0] = tinted;
    }
    const Vec3 up = b.align == 1 ? Vec3(0.0f, 1.0f, 0.0f) : normalize(drop.normal);
    const float yaw = (scatter_rand(dab_seed, (uint32_t)i * 5u + 1u) * 2.0f - 1.0f) * std::max(0.0f, b.rotate) * kDeg2Rad;
    const float sc = b.scale_min + (b.scale_max - b.scale_min) * scatter_rand(dab_seed, (uint32_t)i * 5u + 2u);
    Quat rot = rotation_between(Vec3(0.0f, 1.0f, 0.0f), up) * Quat::axis_angle(Vec3(0.0f, 1.0f, 0.0f), yaw);
    inst->set_local_position(drop.point);
    inst->set_local_rotation(rot);
    inst->set_local_scale(Vec3(std::max(0.001f, sc)));
    placed++;
  }
  if (placed) mark_changed("Scatter");
  return true;
}

/* The parent's instances joined into one mesh on a new object (for export, or a crowd): the instances are kept. */
int Editor::scatter_merge() {
  GameObject *parent = scatter_parent(false);
  if (!parent || parent->children.empty()) {
    Log::warn("scatter merge: nothing scattered yet");
    return 0;
  }
  auto merged = std::make_shared<Mesh>();
  std::vector<MaterialPtr> mats;
  int n = 0;
  for (GameObject *c : parent->children) {
    const MeshFilter *mf = c->mesh_filter();
    if (!mf || !mf->mesh) continue;
    if (const MeshRenderer *mr = c->get<MeshRenderer>())
      if (mats.empty()) mats = mr->materials;
    meshops::append_mesh(*merged, *mf->mesh, c->world_matrix());
    n++;
  }
  merged->sync_attributes();
  merged->touch();
  std::string name = parent->name + " Merged";
  for (int k = 1; scene_->find_by_name(name); k++) name = strprintf("%s Merged.%03d", parent->name.c_str(), k);
  GameObject *g = scene_->create(name);
  g->add<MeshFilter>()->mesh = merged;
  g->add<MeshRenderer>()->materials = mats;
  mark_changed("Scatter Merge");
  Log::info("Scatter: %d instance(s) merged into %s (%zu faces)", n, name.c_str(), merged->face_count());
  return n;
}

bool Editor::scatter_update(const Recti &view) {
  if (!scatter_) return false;
  auto &in = ui_.in;
  const float x = (float)(in.mx - view.x), y = (float)(in.my - view.y);
  if (!scatter_stroking_) {
    if (scene_hovered_ && in.key_pressed[platform::KEY_ESCAPE] && !ui_.wants_keyboard()) {
      set_scatter(false);
      in.key_pressed[platform::KEY_ESCAPE] = false;
      return true;
    }
    if (!(scene_hovered_ && in.pressed[0] && !ui_.any_active() && !qm_.open)) return false;
    if (!scatter_dab(view, x, y, in.eraser)) return false;
    ui_.consume_click();
    scatter_stroking_ = true;
    scatter_last_px_ = {x, y};
    return true;
  }
  if (in.down[0]) {
    const float step = std::max(8.0f, scatter_brush_.radius * 60.0f * ui_.scale);
    if (length(Vec2(x, y) - scatter_last_px_) >= step) scatter_dab(view, x, y, in.eraser), scatter_last_px_ = {x, y};
    return true;
  }
  scatter_stroking_ = false;
  return true;
}

void Editor::scatter_draw(const Recti &view) {
  if (!scatter_ || !scene_hovered_) return;
  auto &u = ui_;
  const Vec2 c((float)u.in.mx, (float)u.in.my);
  if (c.x < view.x || c.y < view.y || c.x > view.right() || c.y > view.bottom()) return;
  draw_brush_ring(c.x, c.y, std::max(4.0f, scatter_brush_.radius * 40.0f * u.scale), u.in.eraser || u.in.alt(), u.theme.accent);  // (task 0269)
}

/* scatter <object> | density n | radius r | scale a b | rotate deg | align normal|up | tint [r g b]|off | seed n | mask <layer>|off |
 * on|off | dab x y | erase x y | merge | count */
void Editor::scatter_command(const std::vector<std::string> &t) {
  auto arg = [&](size_t i, const std::string &d) { return t.size() > i ? to_lower(t[i]) : d; };
  auto num = [&](size_t i, float d) { return t.size() > i ? (float)std::atof(t[i].c_str()) : d; };
  ScatterBrush &b = scatter_brush_;
  const std::string v = arg(1, "count");
  if (v == "on" || v == "off") set_scatter(v == "on");
  else if (v == "density") b.density = std::max(0.0f, std::min(10000.0f, num(2, 4.0f)));
  else if (v == "radius") b.radius = std::max(0.01f, std::min(1000.0f, num(2, 1.0f)));
  else if (v == "scale") b.scale_min = std::max(0.001f, num(2, 0.8f)), b.scale_max = std::max(b.scale_min, num(3, 1.2f));
  else if (v == "rotate") b.rotate = std::max(0.0f, std::min(180.0f, num(2, 180.0f)));
  else if (v == "align") b.align = arg(2, "normal") == "up" ? 1 : 0;
  else if (v == "tint") {
    b.tint = arg(2, "off") != "off";
    if (b.tint && t.size() >= 5) b.tint_color = Vec3(num(2, 1.0f), num(3, 1.0f), num(4, 1.0f));
  }
  else if (v == "seed") b.seed = (uint32_t)std::max(0.0f, num(2, 1.0f));
  else if (v == "mask") b.mask_layer = arg(2, "off") == "off" ? -1 : (int)num(2, -1.0f);
  else if (v == "dab" || v == "erase") {
    if (!scatter_) set_scatter(true);
    if (!scatter_ || !scatter_dab(scene_rect_, num(2, 0.0f), num(3, 0.0f), v == "erase")) Log::warn("scatter %s: nothing under (%g, %g)", v.c_str(), num(2, 0.0f), num(3, 0.0f));
  }
  else if (v == "merge") scatter_merge();
  else if (v == "count") {
    GameObject *p = scatter_parent(false);
    Log::info("Scatter: %zu instance(s)%s", p ? p->children.size() : (size_t)0, b.source.empty() ? " (scatter <object> names the source)" : strprintf(" of %s", b.source.c_str()).c_str());
  }
  else {  // the source object (its name, spaces joined)
    std::string name;
    for (size_t k = 1; k < t.size(); k++) name += (k > 1 ? " " : "") + t[k];
    if (!scene_->find_by_name(name) || !scene_->find_by_name(name)->mesh_filter()) {
      Log::warn("scatter: no mesh object named '%s' (scatter <object> | density|radius|scale|rotate|align|tint|seed|mask | on|off | dab|erase x y | merge | count)", name.c_str());
      return;
    }
    b.source = name;
    Log::info("Scatter: %s will be scattered (scatter on, then dab)", name.c_str());
  }
}

}  // namespace bl
