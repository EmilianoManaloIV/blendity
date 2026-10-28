// SPDX-License-Identifier: GPL-2.0-or-later
// Rendering glue between the scene and the two render engines:
//   * Shaded viewport / Game view: deferred PBR rasterizer + sun shadow map
//     (Blender: EEVEE "Material Preview"/"Rendered"; Unity: Scene view "Shaded")
//   * Rendered viewport + final render: progressive path tracer (Blender: Cycles)
//   * World environment: gradient / Hosek-Wilkie sky / HDRI / colour
#include "editor.h"
#include "../render/dof.h"
#include "../render/display.h"

#include "../core/core.h"
#include "../core/jobs.h"
#include "../render/sky.h"

#include <algorithm>
#include <cmath>
#include <atomic>
#include <cstring>
#include <ctime>

namespace bl {

/* ===================================================================== */
/* Scene -> render data                                                   */
/* ===================================================================== */

/* An object's scale as one number (the mean of its axes), for a thickness given in metres. */
static float world_scale_of(const Mat4 &w) { return (length(w.dir(Vec3(1, 0, 0))) + length(w.dir(Vec3(0, 1, 0))) + length(w.dir(Vec3(0, 0, 1)))) / 3.0f; }

static bool needs_tangents(const std::vector<MaterialPtr> &mats) {
  for (auto &m : mats)
    if (m && (!m->normal_map.empty() || (m->shading_model == 1 && m->toon_hair_highlight))) return true;  // (the hair band runs across the strands: task 0148)
  return false;
}

std::vector<DrawItem> Editor::collect_items(bool game, bool want_tangents) {
  ProfileScope prof{prof_.ms_collect, {}};
  std::vector<DrawItem> items;
  /* (Of what is shown: the preview's own staleness while there is one.) */
  lm_stale_live_ = scene_->lighting.realtime_gi && !lighting_manual() &&
                   (!preview_data_.empty() ? preview_data_.scene_key != preview_scene_key_ : !lighting_data_.empty() && lighting_out_of_date());
  scene_->for_each([&](GameObject &g) {
    if (!g.active_in_hierarchy()) return;
    auto *mr = g.get<MeshRenderer>();
    auto *mf = g.get<MeshFilter>();
    if (!mr || !mr->enabled || !mf || !mf->mesh) return;
    if (game ? !mr->show_in_renders : mr->display_as != 0) return;  // wire / bounds: drawn as overlays
    bool editing = !game && edit_mode_ && g.id == edit_obj_;
    /* Edit Mode shows the modifiers set to Show in Edit Mode over the cage; renders use Show in Renders. */
    const Mesh *m = g.evaluated_mesh(editing ? 2 : game ? 1 : 0);
    if (editing && m != mf->mesh.get()) editing = false;  // a modifier result: shade it normally
    if (!m) return;
    DrawItem it = mesh_draw_item(g, *m, *mr, want_tangents, editing);
    if (editing && elem_ == EditElement::Face) it.face_highlight = &face_sel_;
    if (!editing && mr->contribute_gi && mr->receive_gi == 0 && (game || scene_lighting_)) attach_lightmap(g, *m, it);  // not under the headlight
    it.use_probes = !editing && !it.lightmap && (game || scene_lighting_);
    it.realtime_lm = !editing && mr->contribute_gi && mr->receive_gi == 0 && (game || scene_lighting_);
    items.push_back(it);
    if (editing) return;  // (no outline over the cage)
    /* The line's width by the object's distance from this view's eye (task 0149); a scaled shell keeps its own slot,
     * so a path tracer holding the plain one isn't left with a freed mesh. */
    const Component *oc = g.find_role(Component::RenderRole::Outline);
    if (!oc || !oc->enabled) return;
    const Outline &ol = *static_cast<const Outline *>(oc);
    const float scale = world_scale_of(it.model);
    const float dist = outline_view_.ortho ? 1.0f : length(g.world_bounds().center() - outline_view_.eye);
    const float tscale = outline_view_.on ? ol.outline_distance_scale(dist, outline_view_.px_per_m, scale, game ? 1 : 0) : 1.0f;
    DrawItem o;
    if (outline_draw_item(g, *m, it, (game ? 1 : 0) + (tscale != 1.0f ? 4 : 0), tscale, o)) items.push_back(o);
  });
  return items;
}

/* A mesh's draw item as every view builds it (the import preview too, task 0152). */
DrawItem Editor::mesh_draw_item(const GameObject &g, const Mesh &m, const MeshRenderer &mr, bool want_tangents, bool editing) {
  DrawItem it;
  it.mesh = want_tangents && needs_tangents(mr.materials) ? &m.render_mesh_tangents(editing) : &m.render_mesh(editing);
  it.model = g.world_matrix();
  const Material &mat0 = *mr.material(0);
  it.albedo = mat0.base_color;
  it.specular = mat0.specular * 0.5f;
  it.unlit = mat0.unlit || mat0.shading_model == 2;  // (task 0247)
  it.double_sided = editing;
  for (auto &mp : mr.materials)
    if (mp && mp->double_sided) it.double_sided = true;
  it.materials = &mr.materials;
  it.receive_shadows = mr.receive_shadows;
  it.id = (uint32_t)g.id;
  return it;
}

/* Toon outlines (ADR 0020): the inverted hull as its own item, back faces always culled. False without a hull. */
bool Editor::outline_draw_item(const GameObject &g, const Mesh &m, const DrawItem &it, int slot, float thickness_scale, DrawItem &o) {
  const Component *oc = g.find_role(Component::RenderRole::Outline);
  if (!oc || !oc->enabled) return false;
  const Outline &ol = *static_cast<const Outline *>(oc);
  const Mesh *shell = ol.shell(m, world_scale_of(it.model), slot, thickness_scale);
  if (!shell) return false;
  o = DrawItem();
  o.mesh = &shell->render_mesh();
  o.model = it.model;
  o.albedo = ol.materials()[0]->base_color;  // (NaN-free)
  o.unlit = true;
  o.materials = &ol.materials();
  o.receive_shadows = false;
  o.always_cull = true;
  o.id = it.id;
  return true;
}

/* A Light component as the renderers see it (all four types, temperature applied). */
RenderLight to_render_light(const GameObject &g, const Light &l) {
  RenderLight rl;
  rl.type = l.type == 1 ? RenderLight::Point : l.type == 2 ? RenderLight::Spot : l.type == 3 ? RenderLight::Area : RenderLight::Directional;
  const Quat q = g.world_rotation();
  rl.direction = normalize(q.rotate({0, 0, 1}));
  rl.right = normalize(q.rotate({1, 0, 0}));
  rl.position = g.world_position();
  rl.color = l.final_color();
  rl.intensity = l.intensity;
  rl.bake_mode = std::max(0, std::min(2, l.mode));
  rl.range = l.range;
  const float outer = std::max(1.0f, std::min(179.0f, l.spot_angle)) * 0.5f;
  const float inner = std::min(outer, std::max(0.0f, l.inner_spot_angle) * 0.5f);
  rl.cos_outer = std::cos(outer * kDeg2Rad);
  rl.cos_inner = std::cos(inner * kDeg2Rad);
  rl.disk = l.area_shape == 1;
  /* The object's scale stretches an area light, as in Blender. */
  const Mat4 &w = g.world_matrix();
  rl.width = l.area_width * length(w.dir({1, 0, 0}));
  rl.height = l.area_height * length(w.dir({0, 1, 0}));
  return rl;
}

LightingEnv Editor::make_lighting(Vec3 eye, bool use_scene_lights, bool rasterized) {
  LightingEnv env;
  const EnvironmentSettings &es = scene_->environment;
  env.sky = es.sky;
  env.equator = es.equator;
  env.ground = es.ground;
  env.camera_pos = eye;
  env.view_transform = view_transform_from_setting(scene_->render.view_transform);
  env.exposure = scene_->render.exposure;
  if (!use_scene_lights) {
    /* Blender "Studio" style headlight so unlit scenes stay readable. */
    RenderLight head;
    head.direction = normalize(cam_.forward() + cam_.up() * -0.3f + cam_.right() * 0.2f);
    head.intensity = 0.85f;
    env.lights.push_back(head);
    env.sky = env.equator = env.ground = Vec3(0.35f);
    return env;
  }
  scene_->for_each([&](GameObject &g) {
    if (!g.active_in_hierarchy()) return;
    auto *l = g.get<Light>();
    if (!l || !l->enabled) return;
    /* A Baked light exists only through the bake (task 0021, as Unity): never rasterized, so no direct
     * light, shadow map or live bounce from it. Before a bake, Preview Lighting shows it (Bakery's way). */
    if (rasterized && l->mode == 2) return;
    RenderLight rl = to_render_light(g, *l);
    /* What the lightmaps hold, not the light's current mode: a light baked as Baked and switched to
     * Realtime since must not shine twice, and one added since must not vanish. */
    const LightingData &ld = shown_lighting();
    auto baked = ld.light_modes.find(g.id);
    rl.bake_mode = baked != ld.light_modes.end() && scene_->lighting.baked_gi ? baked->second : 0;
    env.lights.push_back(rl);
  });
  update_environment();
  env.environment = &env_;
  return env;
}

void Editor::update_environment() {
  const EnvironmentSettings &es = scene_->environment;
  Vec3 sun{0.35f, 0.75f, -0.55f};
  scene_->for_each([&](GameObject &g) {
    if (auto *l = g.get<Light>())
      if (l->enabled && l->type == 0 && g.active_in_hierarchy()) sun = -normalize(g.world_rotation().rotate({0, 0, 1}));
  });
  /* Cache key: everything the baked environment depends on. */
  uint64_t key = kFnvBasis;
  auto mix = [&](const void *p, size_t n) { key = fnv1a(p, n, key); };
  mix(&es.mode, 4);
  mix(&es.turbidity, 4);
  mix(&es.ground_albedo, 4);
  mix(&es.rotation, 4);
  mix(es.hdri.path.data(), es.hdri.path.size());
  if (es.mode == 1) mix(&sun, sizeof(sun));
  env_.sky = es.sky;
  env_.equator = es.equator;
  env_.ground = es.ground;
  env_.color = es.color;
  env_.strength = es.strength;
  env_.rotation = es.rotation;
  env_.mode = (Environment::Mode)std::max(0, std::min(3, es.mode));
  /* The stylized sky and the fog (task 0248): cheap fields, copied every time (no key: they change nothing baked). */
  env_.sun_dir = length(sun) > 1e-6f ? normalize(sun) : Vec3(0.3f, 0.8f, 0.5f);
  env_.disc_cos = es.sun_disc > 0.0f ? std::cos(std::min(89.0f, es.sun_disc) * 0.5f * kDeg2Rad) : 2.0f;
  env_.disc_soft = es.sun_disc_softness, env_.disc_color = es.sun_disc_color;
  env_.band_width = std::max(0.0f, es.horizon_band), env_.band_color = es.horizon_band_color;
  env_.fog = es.fog, env_.fog_start = es.fog_start, env_.fog_end = es.fog_end, env_.fog_color = es.fog_color, env_.fog_far = es.fog_color_far;
  if (key == env_key_) return;
  env_key_ = key;
  env_.map = nullptr;
  if (es.mode == 1) {
    env_.map = generate_sky_texture(sun, es.turbidity, es.ground_albedo, 512);
    env_.rotation = 0.0f;  // the sky is baked in world space
  }
  else if (es.mode == 2 && !es.hdri.empty()) {
    std::string err;
    env_.map = texture_load(resolve_asset_path(es.hdri.path), false, &err);
    if (!env_.map) Log::warn("World HDRI: %s", err.c_str());
  }
  if ((es.mode == 1 || es.mode == 2) && !env_.map) env_.mode = Environment::Gradient;
  env_.compute_sh();
}

/* One object's part of the scene render hash (ADR 0009 / 0015). The parts that only change with a
 * transform, a material or the hierarchy (the world matrix, the flags, the material versions) are
 * memoised on the object, keyed by its transform version and the material pointers and versions;
 * everything that can change on its own (the evaluated-mesh keys with their modifiers, the light, the
 * probe volume) is hashed every time. `memo` false hashes everything from scratch (the tests compare). */
uint64_t Editor::object_render_hash(GameObject &g, bool memo) {
  uint64_t h = kFnvBasis;
  auto mix = [&](const void *p, size_t n) { h = mix_bytes(h, p, n); };  // 8 bytes a step: it runs for every view, every frame
  /* One pass over the components: a virtual call each instead of a dynamic_cast per type. */
  MeshRenderer *mr = nullptr;
  Light *l = nullptr;
  ProbeVolume *pv = nullptr;
  Component *outline = nullptr;
  for (auto &c : g.components) switch (c->render_role()) {
      case Component::RenderRole::Outline: if (!outline) outline = c.get(); break;
      case Component::RenderRole::MeshRenderer: if (!mr) mr = static_cast<MeshRenderer *>(c.get()); break;
      case Component::RenderRole::Light: if (!l) l = static_cast<Light *>(c.get()); break;
      case Component::RenderRole::ProbeVolume: if (!pv) pv = static_cast<ProbeVolume *>(c.get()); break;
      default: break;
    }
  /* The hierarchy: a selected object's children get an outline of their own. */
  const uint64_t parent = g.parent ? g.parent->id : 0;
  if (!mr && !l && !pv) {
    mix(&parent, 8);
    return h;
  }
  /* The flags the views draw by (Show in Renders, shadows, wireframe, Display As). */
  uint64_t flags = 0, materials_key = 0;
  if (mr) {
    flags = 1ull << 63 | (uint64_t)mr->enabled | (uint64_t)mr->show_wireframe << 1 | (uint64_t)mr->cast_shadows << 2 |
            (uint64_t)mr->receive_shadows << 3 | (uint64_t)mr->show_in_renders << 4 | (uint64_t)(uint32_t)mr->display_as << 8 |
            (uint64_t)mr->materials.size() << 16 | (uint64_t)mr->contribute_gi << 40 | (uint64_t)(uint32_t)mr->receive_gi << 41;
    if (mr->enabled) {
      materials_key = kFnvBasis;
      for (auto &mp : mr->materials) {  // a material's version is an input of every object using it
        materials_key = (materials_key ^ (uint64_t)(uintptr_t)mp.get()) * kFnvPrime;
        materials_key = (materials_key ^ (mp ? mp->version : 0)) * kFnvPrime;
        if (mp && mp->axis_mode != 0)  // an axis material's parts as resolved (what is rendered; ADR 0018, one level)
          for (const MaterialPtr &part : mp->textures().axis) {
            materials_key = (materials_key ^ (uint64_t)(uintptr_t)part.get()) * kFnvPrime;
            materials_key = (materials_key ^ (part ? part->version : 0)) * kFnvPrime;
          }
      }
    }
  }
  GameObject::RenderHashMemo &m = g.render_memo;
  const Mat4 &world = g.world_matrix();  // cleans the dirty flag: a later change bumps the version
  if (memo && m.valid && m.transform_version == g.transform_version && m.id == g.id && m.parent_id == parent && m.flags == flags &&
      m.materials_key == materials_key)
    h = m.hash;
  else {
    mix(&parent, 8);
    mix(&g.id, 8);
    mix(world.m, sizeof(float) * 16);
    if (mr) {
      const uint64_t f = flags & ~(1ull << 63);
      mix(&f, 8);
      mix(&materials_key, 8);  // the materials' pointers and versions, an axis material's parts included
    }
    m.transform_version = g.transform_version;
    m.id = g.id;
    m.parent_id = parent;
    m.flags = flags;
    m.materials_key = materials_key;
    m.hash = h;
    m.valid = true;
  }
  if (mr && mr->enabled) {
    /* What each kind of evaluated mesh is made from, without evaluating it (a modifier stack would
     * run every frame): renders (Show in Renders), the viewport, and Edit Mode's cage. Every time: a
     * modifier's fields change on their own. */
    uint64_t k[3];
    g.evaluated_keys(k);
    mix(k, sizeof(k));
    /* Settled tangents (ADR 0034): the same mesh drawn a little differently - the views draw again, nothing that
     * keys on the evaluated meshes (a modifier stack's cache) sees a change. */
    if (const MeshFilter *mf = g.mesh_filter(); mf && mf->mesh) mix(&mf->mesh->looks, 8);
  }
  if (outline) {  // a toon outline's thickness, colour and on / off (ADR 0020)
    const uint64_t oh = hash_component(*outline) ^ (uint64_t)outline->enabled << 63;
    mix(&oh, 8);
  }
  if (pv) {  // where the probes go (the volume box, remembered between frames, keys on this)
    const uint64_t ph = hash_component(*pv) ^ (uint64_t)pv->enabled << 63;
    mix(&ph, 8);
  }
  if (l && l->enabled) {
    /* Every field of the light (colour temperature, spot angles, area size, shadows...):
     * a change to any of them re-renders the Camera Preview and the Rendered view. */
    const uint64_t lh = hash_component(*l);
    mix(&lh, 8);
  }
  return h;
}

uint64_t Editor::scene_render_hash(bool memo) {
  ProfileScope prof{prof_.ms_hash, {}};
  prof_.scene_hashes++;
  uint64_t h = kFnvBasis;
  auto mix = [&](const void *p, size_t n) { h = mix_bytes(h, p, n); };
  scene_->for_each([&](GameObject &g) {
    if (!g.active_in_hierarchy()) return;
    const uint64_t oh = object_render_hash(g, memo);
    mix(&oh, 8);
  });
  const EnvironmentSettings &es = scene_->environment;
  mix(&es.mode, 4);
  mix(&es.strength, 4);
  mix(&es.rotation, 4);
  mix(&es.turbidity, 4);
  mix(&es.color, sizeof(Vec3));
  mix(&es.sky, sizeof(Vec3) * 3);
  mix(es.hdri.path.data(), es.hdri.path.size());
  /* (Not Edit Mode: only the Scene view draws the cage - collect_items and submit_scene edit only for it - and its own
   * key, scene_view_key, holds the edit state. Here it redrew the Game view and staled the lighting keys on every
   * entry: task 0185.) */
  mix(&scene_lighting_, 1);
  mix(&scene_->serial, 8);
  mix(&lighting_gen_, 8);  // a bake finished, was cleared or loaded
  const uint64_t tex_gen = material_textures_generation();  // a texture reloaded or a procedural texture edited
  mix(&tex_gen, 8);
  mix(&scene_->lighting.baked_gi, 1);
  mix(&probe_live_gen_, 8);  // live probes changed
  for (const VoxelGIState &st : vgi_) mix(&st.rtlm.generation, 8);  // realtime lightmaps published
  return h;
}

bool Editor::camera_for_render(Mat4 &view, Mat4 &proj, float aspect) {
  GameObject *owner = nullptr;
  Camera *cam = main_camera(*scene_, &owner);
  if (!cam) {
    view = cam_.view();
    proj = cam_.proj(aspect);
    return false;
  }
  Quat q = owner->world_rotation();
  Vec3 eye = owner->world_position();
  view = Mat4::look_at(eye, eye + q.rotate({0, 0, 1}), q.rotate({0, 1, 0}));
  proj = cam->projection(aspect);
  return true;
}

/* ===================================================================== */
/* Rasterized (EEVEE-like)                                                */
/* ===================================================================== */

/* A cached map for the views now: the Low tier's take the cheaper filter (renders keep 3 x 3), and so do the CPU
 * views while you interact (task 0104). */
const ShadowMap *Editor::shadow_for_view(ShadowMap &m, bool cpu_view) const {
  m.fast = (quality_tier_ == TierLow || (cpu_view && moving_cheap())) && !final_render_;
  return m.valid() ? &m : nullptr;
}

const ShadowMap *Editor::update_shadow_map(const std::vector<DrawItem> &items, const LightingEnv &env, bool frame_only) {
  int li = -1;
  for (size_t i = 0; i < env.lights.size(); i++)
    if (env.lights[i].type == RenderLight::Directional) { li = (int)i; break; }
  if (li < 0) return nullptr;
  std::vector<const DrawItem *> caster_ptrs;  // copied into `casters` only when the map is rendered
  std::vector<uint64_t> caster_keys, motion_keys;
  AABB bounds;
  auto mixer = [](uint64_t &key, const void *p, size_t n) { key = mix_bytes(key, p, n); };
  /* What the map depends on: each caster's triangles (the render mesh's serial: a new one whenever it is rebuilt),
   * placement and materials' surface type and Cast Shadows (see-through surfaces cast none; a colour doesn't matter),
   * the light's direction and the resolution. */
  uint64_t key = kFnvBasis;
  bool cacheable = true;
  for (const DrawItem &it : items) {
    bool casts = false;
    if (!it.materials || it.materials->empty()) casts = true;
    else
      for (auto &m : *it.materials)
        if (!m || m->cast_shadows) casts = true;
    if (!casts) continue;
    caster_ptrs.push_back(&it);
    bounds.add(it.mesh->bounds.transformed(it.model));
    if (it.mesh->serial == 0) cacheable = false;
    uint64_t ck = kFnvBasis ^ it.id;
    mixer(ck, it.model.m, sizeof(float) * 16);
    motion_keys.push_back(ck);  // what makes it "moving": its placement
    /* Its shape (task 0088): the Scene view's flat Edit Mode cache and the Game view's smooth one share it, so the two
     * views share one map instead of each rendering its own every frame of a drag. */
    const uint64_t shape = it.mesh->shape ? it.mesh->shape : it.mesh->serial;
    mixer(ck, &shape, 8);
    /* Only what the depth pass reads (its surface type, Cast Shadows), not the material's address: an undo restores
     * the scene's snapshot, whose materials are copies at other addresses, and the map would be drawn again (task 0185). */
    if (it.materials)
      for (const MaterialPtr &m : *it.materials) {
        const uint64_t mv[2] = {m ? (uint64_t)m->surface : 0, m ? (uint64_t)m->cast_shadows : 1};
        mixer(ck, mv, sizeof(mv));
      }
    caster_keys.push_back(ck);
    mixer(key, &ck, 8);
  }
  const int res = shadow_resolution_for_view(!frame_only);  // (the GPU's own map stays whole: it costs it little)
  if (!frame_only && res < shadow_resolution_for_view()) prof_.cheap_shadows++;
  const Vec3 dir = env.lights[li].direction;
  /* The frame, snapped (task 0050): its radius up to a step of a geometric series, its centre to a sixteenth of that,
   * so dragging one object about leaves the frame, and the casters that stayed still keep their map. */
  AABB frame;
  if (final_render_) frame = bounds;  // a render fits its casters exactly (as before the snapping)
  else if (bounds.valid()) {
    /* Steps of 10 % in the radius and 1/16 of it at the centre: shadow texels at most ~1.2 times larger than a tight fit. */
    const float r = std::max(0.5f, length(bounds.extent()));
    const float rs = std::pow(1.1f, std::ceil(std::log(r) / std::log(1.1f)));
    const float step = rs / 16.0f;
    const Vec3 c = bounds.center();
    const Vec3 cs(std::round(c.x / step) * step, std::round(c.y / step) * step, std::round(c.z / step) * step);
    const float rr = rs + length(cs - c);  // still holds every caster after the centre moved
    const Vec3 h(rr / std::sqrt(3.0f));
    frame.add(cs - h);
    frame.add(cs + h);
  }
  if (frame_only) {
    /* The GPU views render the map themselves from its frame (task 0096): nothing drawn here, nothing cached. */
    gpu_shadow_.depth.clear();
    gpu_shadow_.size = frame.valid() && !caster_ptrs.empty() ? res : 0;
    if (gpu_shadow_.size) gpu_shadow_.view_proj = shadow_view_proj(dir, frame);
    gpu_shadow_.bias = ShadowMap{}.bias;
    return shadow_for_view(gpu_shadow_);
  }
  mixer(key, &dir, sizeof(Vec3));
  mixer(key, &res, sizeof(int));
  mixer(key, &frame, sizeof(AABB));
  const uint64_t n = caster_ptrs.size();
  mixer(key, &n, 8);
  if (key == 0) key = 1;  // 0 marks an empty entry
  shadow_clock_++;
  if (cacheable)
    for (ShadowEntry &e : shadow_cache_)
      if (e.key == key) {
        e.used = shadow_clock_;
        return shadow_for_view(e.map, true);
      }
  /* Which casters are moving: changed within the last second. */
  const double now = now_seconds();
  std::vector<uint8_t> moving(caster_ptrs.size(), 0);
  size_t n_moving = 0;
  for (size_t i = 0; i < caster_ptrs.size(); i++) {
    CasterMotion &m = caster_motion_[caster_ptrs[i]->id];
    if (m.key != motion_keys[i]) {
      if (m.key != 0) m.changed = now;  // (a caster seen for the first time is still: nothing to compare)
      m.key = motion_keys[i];
    }
    moving[i] = now - m.changed < 1.0;
    n_moving += moving[i];
  }
  if (caster_motion_.size() > caster_ptrs.size() * 2 + 64) {  // forget objects that are gone
    std::unordered_set<uint64_t> live;
    for (const DrawItem *it : caster_ptrs) live.insert(it->id);
    for (auto it = caster_motion_.begin(); it != caster_motion_.end();) it = live.count(it->first) ? std::next(it) : caster_motion_.erase(it);
  }
  /* Render into the least recently used entry. */
  ShadowEntry &e = shadow_cache_[0].used <= shadow_cache_[1].used ? shadow_cache_[0] : shadow_cache_[1];
  ScopedTimer t;
  std::vector<DrawItem> casters;
  if (cacheable && n_moving > 0 && n_moving < caster_ptrs.size()) {
    /* The still casters' layer (drawn again only when they or the frame change), then the moving ones over it. */
    uint64_t skey = kFnvBasis;
    mixer(skey, &dir, sizeof(Vec3));
    mixer(skey, &res, sizeof(int));
    mixer(skey, &frame, sizeof(AABB));
    for (size_t i = 0; i < caster_ptrs.size(); i++)
      if (!moving[i]) mixer(skey, &caster_keys[i], 8);
    if (skey == 0) skey = 1;
    ShadowLayer *layer = shadow_static_[0].key == skey ? &shadow_static_[0] : shadow_static_[1].key == skey ? &shadow_static_[1] : nullptr;
    if (!layer) {
      layer = shadow_static_[0].used <= shadow_static_[1].used ? &shadow_static_[0] : &shadow_static_[1];
      for (size_t i = 0; i < caster_ptrs.size(); i++)
        if (!moving[i]) casters.push_back(*caster_ptrs[i]);
      render_shadow_map(layer->map, casters, dir, frame, res);
      layer->key = layer->map.valid() ? skey : 0;
      shadow_layer_renders_++;
      casters.clear();
    }
    layer->used = shadow_clock_;
    for (size_t i = 0; i < caster_ptrs.size(); i++)
      if (moving[i]) casters.push_back(*caster_ptrs[i]);
    render_shadow_map(e.map, casters, dir, frame, res, layer->key ? &layer->map : nullptr);
  }
  else {
    casters.reserve(caster_ptrs.size());
    for (const DrawItem *it : caster_ptrs) casters.push_back(*it);
    render_shadow_map(e.map, casters, dir, frame, res);
  }
  prof_.shadow_renders++;
  prof_.ms_shadow += t.ms();
  e.key = cacheable ? key : 0;
  e.used = shadow_clock_;
  return shadow_for_view(e.map, true);
}

void Editor::render_deferred(Renderer3D &r3d, RenderTarget &rt, const Mat4 &v, const Mat4 &p, Vec3 eye, bool game,
                             bool scene_lights, const Camera *cam, const FilterStack *filters) {
  LightingEnv env = make_lighting(eye, scene_lights);
  if (cam) env.exposure += cam->exposure_stops();  // ISO, shutter and f-stop
  outline_view_ = {true, eye, 0.5f * (float)rt.height * p.m[5], p.m[15] == 1.0f};  // (task 0149: the outlines' width by distance; m[5] is the projection's focal term, m[15] 1 for an orthographic one)
  std::vector<DrawItem> items = collect_items(game, true);
  outline_view_.on = false;
  /* Task 0096: a view the GPU will draw renders its own shadow map; the CPU only fits its frame. */
  const bool gpu_maybe = gpu_view_may_draw(rt, env, items, filters);
  const bool shadows = scene_lights && scene_->render.shadows;
  if (shadows) {
    if (const ShadowMap *sm = update_shadow_map(items, env, gpu_maybe)) {
      for (size_t i = 0; i < env.lights.size(); i++)
        if (env.lights[i].type == RenderLight::Directional) { env.shadow_light = (int)i; break; }
      env.shadow = sm;
    }
  }
  /* Realtime GI traces only what the bake doesn't hold (task 0017): with a current bake of every light
   * that bounces, the views cost what Realtime GI off costs. */
  const bool live_gi = scene_lights && live_gi_needed();
  if (live_gi) env.gi = update_voxel_gi(items, env, game);
  /* Manual (task 0020): nothing traced, but the last realtime lightmaps and live probes stay up. */
  const bool frozen = scene_lights && lighting_frozen();
  bool bounce_not_baked = env.gi && env.gi->bounce_not_baked;
  if (frozen && !env.gi) {
    /* As update_voxel_gi decides it: the first directional light's bounce isn't in the shown bake. */
    bounce_not_baked = true;
    for (const RenderLight &l : env.lights)
      if (l.type == RenderLight::Directional) {
        if (!shown_lighting().empty() && scene_->lighting.baked_gi) bounce_not_baked = l.bake_mode == 0;
        break;
      }
    env.rt_bounce_not_baked = bounce_not_baked;
    const float so = scene_->lighting.gi_specular_occlusion;
    env.rt_specular_occlusion = finite_bits(so) ? std::max(0.0f, std::min(1.0f, so)) : 1.0f;
  }
  if (env.gi || frozen) {
    /* Realtime lightmaps where a pass has been published: read, not traced, and not probe-lit (Receive GI =
     * Lightmaps, as Unity's realtime GI). */
    const RealtimeLightmaps &rl = vgi_[game ? 1 : 0].rtlm;
    if (!rl.shown.empty())
      for (DrawItem &it : items) {
        if (!it.realtime_lm || !it.mesh) continue;
        auto e = rl.shown.entries.find(it.id);
        if (e == rl.shown.entries.end() || e->second.page < 0 || (size_t)e->second.page >= rl.shown.bounce.size()) continue;
        if (e->second.hash != scene_ident_) continue;  // published for the scene before this one (undo keeps the id)
        if (e->second.tri_uv.size() != it.mesh->tri_count() * 3) continue;  // a different mesh since: traced until the next pass
        it.rt_bounce = &rl.shown.bounce[(size_t)e->second.page];
        it.rt_sky = &rl.shown.sky[(size_t)e->second.page];
        it.rt_uv = &e->second.tri_uv;
        it.use_probes = false;
      }
  }
  const LightingSettings &pls = scene_->lighting;
  const bool baked_probes = !probes_.empty() && probes_.scene_key != 0 && pls.baked_gi;
  const bool live_probes = !probes_.empty() && (live_gi || frozen) && probe_live_ready_;
  if (scene_lights && (baked_probes || live_probes)) {
    const LightingSettings &ls = pls;
    probe_frame_.data = &probes_;
    probe_frame_.normal_bias = finite_bits(ls.probe_normal_bias) ? std::max(0.0f, std::min(10.0f, ls.probe_normal_bias)) : 0.25f;
    probe_frame_.view_bias = finite_bits(ls.probe_view_bias) ? std::max(0.0f, std::min(10.0f, ls.probe_view_bias)) : 0.1f;
    probe_frame_.leak_reduction = ls.probe_leak_reduction;
    probe_frame_.quad_reuse = probe_quad_reuse_;
    /* The live copy where the bake doesn't cover it: no bake, a stale one, or a sun it doesn't hold. */
    probe_frame_.use_live = live_probes && (!baked_probes || lm_stale_live_ || bounce_not_baked);
    env.probes = &probe_frame_;
  }
  RasterOptions opt = raster_opt_;
  opt.shade = ShadeMode::Deferred;
  if (filters) filters->apply_raster(opt);
  r3d.begin(&rt, v, p, env, opt);  // (also for the GPU: the overlays drawn next go through it)
  if (gpu_maybe && gpu_view_draw(rt, v, p, eye, env, items, cam, scene_lights, filters, opt)) {
    size_t tris = 0;
    for (const DrawItem &it : items) tris += it.mesh ? it.mesh->tri_count() : 0;
    r3d.note_submitted((int)items.size(), tris);
    return;
  }
  if (gpu_maybe && env.shadow) {  // (the GPU couldn't after all: the CPU needs the map itself, not only its frame)
    env.shadow = update_shadow_map(items, env);
    r3d.begin(&rt, v, p, env, opt);
  }
  Mat4 inv = (p * v).inverse();
  if (render_clear_override_on_) {  // a transparent F12's black / white pass (task 0035)
    r3d.clear(render_clear_override_);
    if (!rt.hdr.empty()) std::fill(rt.hdr.begin(), rt.hdr.end(), Vec3(0.0f));  // no light there for bloom in either pass
  }
  else if (cam && cam->clear_flags == 1) r3d.clear(to_display_pixel(cam->background, ViewTransform::Standard, 0.0f));
  else if (!scene_lights)  // the editor's own sky; with the scene's lighting the world's gradient, sky or HDRI
    r3d.clear_sky(inv, {0.33f, 0.47f, 0.68f}, {0.70f, 0.74f, 0.79f}, {0.27f, 0.26f, 0.25f});
  else r3d.clear_environment(inv, env_, env.view_transform, env.exposure);
  for (const DrawItem &it : items) r3d.add(it);
  r3d.flush();
}

/* A picture drawn over black and over white -> straight colour and alpha, in place of the white one (task 0035):
 * alpha = 1 - (white - black), colour = black / alpha. Under 4 % alpha is clear: a pass that quantises colours
 * (15-bit, posterize) or adds grain leaves the background a few levels apart in the two, not a real veil. */
static void combine_black_white(const uint32_t *black, uint32_t *white_out, size_t n) {
  JobSystem::global().parallel_for((int64_t)n, 16384, [&](int64_t b, int64_t e) {
    for (int64_t i = b; i < e; i++) {
      const uint32_t bk = black[(size_t)i], wt = white_out[(size_t)i];
      const int rb = bk >> 16 & 255, gb = bk >> 8 & 255, bb = bk & 255, rw = wt >> 16 & 255, gw = wt >> 8 & 255, bw = wt & 255;
      const float a = clampf(1.0f - ((rw - rb) + (gw - gb) + (bw - bb)) / (3.0f * 255.0f), 0.0f, 1.0f);
      if (a < 0.04f) {
        white_out[(size_t)i] = 0;
        continue;
      }
      const int a8 = (int)std::lround(a * 255.0f);
      auto un = [&](int c) { return (uint32_t)std::min(255, (int)std::lround(c / a)); };
      white_out[(size_t)i] = (uint32_t)a8 << 24 | un(rb) << 16 | un(gb) << 8 | un(bb);
    }
  });
}

/* A transparent render's pixels over black (a JPEG has no alpha; task 0035). */
static std::vector<uint32_t> over_black(const std::vector<uint32_t> &px) {
  std::vector<uint32_t> out(px.size());
  for (size_t i = 0; i < px.size(); i++) {
    const uint32_t c = px[i], a = c >> 24;
    auto m = [&](uint32_t v) { return (v * a + 127) / 255; };
    out[i] = 0xFF000000u | m(c >> 16 & 255) << 16 | m(c >> 8 & 255) << 8 | m(c & 255);
  }
  return out;
}

/* A camera's filter stack, kept per camera object and rebuilt when its filter components change: building
 * one makes a std::function per pass, and every view asked for it on every render (task 0024). */
const FilterStack &Editor::camera_filters(const GameObject *owner) {
  static const FilterStack none;
  if (!owner) return none;
  uint64_t h = kFnvBasis;
  for (const auto &c : owner->components)
    if (c->render_role() == Component::RenderRole::CameraFilter) {
      const uint64_t fh = hash_component(*c) ^ (uint64_t)c->enabled;
      h = (h ^ fh) * kFnvPrime;
    }
  FilterCacheEntry &e = filter_cache_[owner->id];  // (trimmed between frames, never here: callers hold references)
  if (!e.valid || e.hash != h) {
    e.stack = FilterStack();
    for (const auto &c : owner->components)
      if (c->render_role() == Component::RenderRole::CameraFilter && c->enabled) static_cast<const CameraFilter *>(c.get())->contribute(e.stack);
    e.hash = h;
    e.valid = true;
    prof_.filter_stack_builds++;
  }
  return e.stack;
}

/* Nearest-neighbour copy of one plane (colour, depth or ids) into r of a w-wide destination. */
template<class T> static void upscale_plane(const std::vector<T> &src, int sw, int sh, std::vector<T> &dst, int dw, const Recti &r) {
  if (src.size() < (size_t)sw * sh || dst.empty() || r.w <= 0 || r.h <= 0) return;
  std::vector<int> col((size_t)r.w);  // (each view's planes every frame at a reduced Viewport Resolution: task 0050)
  for (int x = 0; x < r.w; x++) col[(size_t)x] = std::min(sw - 1, (int)((int64_t)x * sw / r.w));
  JobSystem::global().parallel_for(r.h, 32, [&](int64_t y0, int64_t y1) {
    for (int64_t y = y0; y < y1; y++) {
      const T *s = src.data() + (size_t)std::min<int64_t>(sh - 1, y * sh / r.h) * sw;
      T *d = dst.data() + (size_t)(r.y + y) * dw + r.x;
      for (int x = 0; x < r.w; x++) d[x] = s[col[(size_t)x]];
    }
  });
}

/* Viewport Resolution Scale (task 0050): on a 4-core machine the software rasterizer's cost is mostly per pixel, so the
 * views can draw their 3D at 50-100 % and scale it up. Depth and ids follow at full size (nearest), so picking, the
 * tools' visibility tests and the overlays (drawn after, at full size) work as before - unless `planes` is false: a
 * view nobody picks in (the Game view, the Camera Preview) keeps only the colour (task 0092). */
void Editor::render_scaled(Renderer3D &r3d, RenderTarget &rt, FilterTarget &vs, const Mat4 &v, const Mat4 &p,
                           const std::function<void(RenderTarget &)> &draw, const GameObject *filter_owner, bool fill, bool planes,
                           bool full_size) {
  const float s = full_size ? 1.0f : finite_bits(view_scale_) ? std::max(0.25f, std::min(1.0f, view_scale_)) : 1.0f;
  const int iw = std::max(1, (int)std::lround(rt.width * s)), ih = std::max(1, (int)std::lround(rt.height * s));
  bool own_size = false;  // a filter stack with its own internal resolution (a retro console's): its pixels stay crisp
  if (filter_owner) {
    const FilterStack &fs = camera_filters(filter_owner);
    if (!fs.empty()) {
      int fw, fh;
      Recti d;
      filter_layout(fs, fill ? (int)FilterStack::Fill : fs.fit, rt.width, rt.height, fw, fh, d);
      own_size = fw != rt.width || fh != rt.height;
    }
  }
  const bool was = planes_wanted_;
  planes_wanted_ = planes || planes_always_;
  if (own_size || s >= 0.999f || (iw >= rt.width && ih >= rt.height) || rt.width < 8 || rt.height < 8) {
    draw(rt);
    planes_wanted_ = was;
    return;
  }
  if (vs.img.width != iw || vs.img.height != ih) vs.img.resize(iw, ih);
  vs.rt.attach(vs.img, {0, 0, iw, ih});
  draw(vs.rt);
  planes_wanted_ = was;
  upscale_bilinear(vs.img.pixels.data(), iw, ih, iw, rt.color, rt.stride, {0, 0, rt.width, rt.height});
  /* Depth and ids at full size are for picking and the tools (the Scene view); a view nobody picks in (the Game view,
   * the Camera Preview) skips them (task 0092). */
  if (planes) {
    upscale_plane(vs.rt.depth, iw, ih, rt.depth, rt.width, {0, 0, rt.width, rt.height});
    upscale_plane(vs.rt.ids, iw, ih, rt.ids, rt.width, {0, 0, rt.width, rt.height});
  }
  r3d.rebind(&rt, v, p);
}

void Editor::set_view_scale(float s) {
  view_scale_ = finite_bits(s) ? std::max(0.5f, std::min(1.0f, s)) : 1.0f;
  ui_.redraw = true;
}

int Editor::shadow_resolution_for_view(bool cpu_view) const {
  const int res = std::max(256, scene_->render.shadow_resolution);
  const int capped = !final_render_ && tier_shadow_cap_ > 0 ? std::min(res, tier_shadow_cap_) : res;
  return cpu_view && moving_cheap() ? std::max(256, capped / 2) : capped;
}

/* The tiers (ADR 0026). Low is measured at 8 threads on the minimum machine's budget: the views at 67 %, a 1024 shadow
 * map, Realtime GI's rays and reflective shadow map halved, 60 fps. Custom keeps whatever was set by hand. */
void Editor::set_quality_tier(int tier) {
  quality_tier_ = std::max(0, std::min(3, tier));
  if (quality_tier_ == TierCustom) return;
  struct Tier { float scale; int shadow, rays, rsm, fps; };
  static const Tier kTiers[3] = {{0.67f, 1024, 4, 128, 60}, {0.85f, 2048, 8, 256, 120}, {1.0f, 0, 0, 0, 144}};
  const Tier &t = kTiers[quality_tier_];
  view_scale_ = t.scale;
  tier_shadow_cap_ = t.shadow;
  tier_gi_rays_cap_ = t.rays;
  tier_rsm_cap_ = t.rsm;
  max_fps_ = t.fps;
  ui_.redraw = true;
}

int Editor::recommended_tier() {
  if (const char *e = std::getenv("BLENDITY_MIN_SPEC"); e && *e && *e != '0') return TierLow;  // (try the minimum machine's start)
  const uint64_t ram = sys::physical_memory_bytes();
  const bool small = sys::hardware_threads() <= 8 || (ram > 0 && ram <= (uint64_t)9 << 30);  // "8 GB" machines report a little under
  return small ? TierLow : TierHigh;
}

void Editor::set_cpu_threads(int n) {
  cpu_threads_ = std::max(0, std::min(1024, n));
  JobSystem::global().set_thread_limit(cpu_threads_);
}

void Editor::render_camera(Renderer3D &r3d, RenderTarget &rt, FilterTarget &ft, const Mat4 &v, const Mat4 &p, Vec3 eye, Vec3 forward, const GameObject *owner,
                           const Camera *cam, float aspect, bool game, bool scene_lights, bool animate) {
  const FilterStack &fs = camera_filters(owner);
  /* The Scene view fills whatever the stack says: its overlays and picking use the full view's projection. */
  const int fit = cam ? fs.fit : (int)FilterStack::Fill;
  /* The camera whose exposure, clear colour and far clip the picture takes: the Scene view (cam == nullptr)
   * keeps the editor's projection but looks through the filters' camera (ADR 0016); no depth of field there
   * (the user is working in that view). */
  const Camera *look = cam ? cam : owner ? owner->camera() : nullptr;
  if (fs.empty()) {
    render_deferred(r3d, rt, v, p, eye, game, scene_lights, look);
    if (cam) camera_dof(rt, v, p, eye, forward, cam, aspect, cam->vertical_fov_deg(aspect));
    return;
  }
  int iw, ih;
  Recti dst;
  filter_layout(fs, fit, rt.width, rt.height, iw, ih, dst);
  const bool boxed = dst.x != 0 || dst.y != 0 || dst.w != rt.width || dst.h != rt.height;
  const bool scaled = iw != rt.width || ih != rt.height;
  /* Letterboxed: the picture has the console frame's shape, so the projection does too. */
  const float a2 = boxed ? dst.w / (float)std::max(1, dst.h) : aspect;
  const Mat4 pp = !boxed ? p : cam ? cam->projection(a2) : cam_.proj(a2);
  RenderTarget *t = &rt;
  if (scaled) {
    if (ft.img.width != iw || ft.img.height != ih) {
      ft.img.resize(iw, ih);
      prof_.filter_target_resizes++;
    }
    ft.rt.attach(ft.img, {0, 0, iw, ih});
    t = &ft.rt;
  }
  t->want_hdr = fs.needs_hdr;  // bloom reads linear light (only allocated when asked)
  render_deferred(r3d, *t, v, pp, eye, game, scene_lights, look, &fs);
  t->want_hdr = false;
  if (cam) camera_dof(*t, v, pp, eye, forward, cam, a2, cam->vertical_fov_deg(a2));
  FilterFrame frame;
  frame.inv_view_proj = (pp * v).inverse();
  frame.eye = eye;
  frame.forward = normalize(forward);
  frame.far_distance = look ? look->far_clip : 1000.0f;
  frame.pixel_scale = t->height / 1080.0f;
  frame.time = play_time_;
  frame.animate = animate;  // only the Game view while playing: F12, previews and piloting stay reproducible
  for (const FilterPass &pass : fs.passes) pass(*t, &frame);
  if (!scaled) return;
  /* Scale up into the view; its depth and ids follow, so picking and overlays still work. */
  if (boxed)
    for (int y = 0; y < rt.height; y++) std::fill(rt.color + (size_t)y * rt.stride, rt.color + (size_t)y * rt.stride + rt.width, 0xFF000000u);
  upscale_nearest(ft.img.pixels.data(), iw, ih, iw, rt.color, rt.stride, dst);
  if (!planes_wanted_) return r3d.rebind(&rt, v, p);  // (a view nobody picks in: task 0092)
  if (boxed) {
    std::fill(rt.depth.begin(), rt.depth.end(), 1.0f);
    std::fill(rt.ids.begin(), rt.ids.end(), 0u);
  }
  upscale_plane(ft.rt.depth, iw, ih, rt.depth, rt.width, dst);
  upscale_plane(ft.rt.ids, iw, ih, rt.ids, rt.width, dst);
  r3d.rebind(&rt, v, p);  // overlays (grid, gizmos) draw on the full-size view; the stats stay
}

/* ===================================================================== */
/* Path traced (Cycles-like)                                              */
/* ===================================================================== */

std::vector<int> Editor::enabled_gpus() const {
  std::vector<int> out;
  for (const gpu::DeviceInfo &d : gpu::devices())
    if (!render_devices_off_.count(d.name)) out.push_back(d.index);
  return out;
}

PTSettings Editor::make_pt_settings(const RenderSettings &rs) const {
  PTSettings s;
  /* GPU Compute: the ticked GPUs (and the CPU too when asked). With no GPU
   * available it quietly stays on the CPU. */
  if (rs.device >= 1 && gpu::available()) {
    s.gpus = enabled_gpus();
    s.use_cpu = rs.gpu_with_cpu || rs.device == 2 || s.gpus.empty();  // Auto: both, by speed (task 0051)
    s.gpu_hardware_rt = rs.hardware_rt;
    s.gpu_backend = rs.gpu_backend;
  }
  s.max_bounces = rs.max_bounces;
  s.clamp_indirect = rs.clamp_indirect;
  s.denoise = rs.denoise;
  s.use_oidn = rs.denoiser == 0;
  s.denoise_device = rs.denoise_device;
  s.use_embree = rs.use_embree;
  s.use_guiding = rs.path_guiding;
  s.view_transform = view_transform_from_setting(rs.view_transform);
  s.exposure = rs.exposure;
  return s;
}

static void build_pt(PathTracer &pt, Editor &, const std::vector<DrawItem> &items, const LightingEnv &env,
                     const Environment &world) {
  std::vector<PTObject> objs;
  for (const DrawItem &it : items) objs.push_back({it.mesh, it.model, it.materials, it.id});
  pt.build(objs, env.lights, world);
}

/* A camera filter's rasterizer stages for a path-traced picture (task 0032, ADR 0008 amended): the settings, and
 * vertex snapping done to copies of the meshes (each vertex's screen position rounded to the filter's grid at
 * its internal size, depth and w kept, as the rasterizer's to_screen; then back into the object's own space,
 * so Box / Generated mapping still read object coordinates). `keep` holds the copies while the tracer uses them. */
static void retro_for_trace(const FilterStack &fs, const Mat4 &v, const Mat4 &p, int w, int h, PTSettings &ps, std::vector<DrawItem> &items,
                            std::unordered_map<uint64_t, std::shared_ptr<RenderMesh>> &keep) {
  /* The copies are kept by what made them (mesh, model, camera, size, grid): a rebuild with nothing of that
   * changed reuses the same copy, so the tracer's BVH caches (keyed by mesh) still hit. */
  std::unordered_map<uint64_t, std::shared_ptr<RenderMesh>> old;
  old.swap(keep);
  ps.retro = PTSettings::Retro{};
  if (fs.empty() || w <= 0 || h <= 0) return;
  const Mat4 vp = p * v;
  ps.retro.affine_uv = fs.affine_uv;
  ps.retro.screen_door = fs.screen_door;
  ps.retro.tex = fs.tex;
  ps.retro.view_proj = vp;
  ps.retro.width = w;
  ps.retro.height = h;
  const float snap = finite_bits(fs.vertex_snap) && fs.vertex_snap > 0.0f ? fs.vertex_snap : 0.0f;
  if (snap <= 0.0f) return;
  const Mat4 ivp = vp.inverse();
  for (DrawItem &it : items) {
    if (!it.mesh) continue;
    const Mat4 mvp = vp * it.model, to_local = it.model.inverse();
    /* Wholly off one side of the view: no vertex lands on the image, nothing to snap (the original mesh). */
    if (it.mesh->bounds.valid()) {
      int out[6] = {0, 0, 0, 0, 0, 0};
      for (int k = 0; k < 8; k++) {
        const Vec3 cb{k & 1 ? it.mesh->bounds.max.x : it.mesh->bounds.min.x, k & 2 ? it.mesh->bounds.max.y : it.mesh->bounds.min.y,
                      k & 4 ? it.mesh->bounds.max.z : it.mesh->bounds.min.z};
        const Vec4 c = mvp * Vec4(cb, 1.0f);
        out[0] += c.x < -c.w, out[1] += c.x > c.w, out[2] += c.y < -c.w, out[3] += c.y > c.w, out[4] += c.w <= 0.0f, out[5] += c.z > c.w;
      }
      bool outside = false;
      for (int k = 0; k < 6; k++) outside = outside || out[k] == 8;
      if (outside) continue;
    }
    uint64_t key = kFnvBasis;
    auto mix = [&](const void *d, size_t n) { key = fnv1a(d, n, key); };
    const void *src = it.mesh;
    mix(&src, sizeof(src));
    mix(&it.mesh->serial, sizeof(it.mesh->serial));
    mix(it.model.m, sizeof(it.model.m));
    mix(vp.m, sizeof(vp.m));
    mix(&w, sizeof(w));
    mix(&h, sizeof(h));
    mix(&snap, sizeof(snap));
    if (auto f = old.find(key); f != old.end()) {
      it.mesh = f->second.get();
      keep[key] = f->second;
      continue;
    }
    auto m = std::make_shared<RenderMesh>(*it.mesh);
    m->serial = 0;
    m->shape = 0;  // (its positions are snapped: not the mesh's shape any more)
    for (Vec3 &pos : m->positions) {
      const Vec4 c = mvp * Vec4(pos, 1.0f);
      if (!(c.w > 1e-6f)) continue;  // behind the camera: as it is
      const float sx = std::floor(((c.x / c.w) * 0.5f + 0.5f) * w / snap + 0.5f) * snap, sy = std::floor((0.5f - (c.y / c.w) * 0.5f) * h / snap + 0.5f) * snap;
      const Vec4 q = ivp * Vec4((sx / w * 2.0f - 1.0f) * c.w, (1.0f - sy / h * 2.0f) * c.w, c.z, c.w);
      if (!(std::fabs(q.w) > 1e-12f)) continue;
      const Vec3 wp = q.xyz() / q.w;
      if (finite_bits(wp.x) && finite_bits(wp.y) && finite_bits(wp.z)) pos = to_local.point(wp);
    }
    it.mesh = m.get();
    keep[key] = std::move(m);
  }
}

/* A camera's Clear Flags: Solid Color for the path tracer (ADR 0016): the display colour taken as linear
 * light, so a Standard view transform shows it as set (Filmic tints it a little, as it tints everything). */
static void pt_solid_background(PTSettings &ps, const Camera *cam) {
  ps.solid_background = cam && cam->clear_flags == 1;
  if (ps.solid_background)  // divided by the exposure the resolve applies, so it shows as set, like the raster clear
    ps.background = Vec3(srgb_to_linear(cam->background.x), srgb_to_linear(cam->background.y), srgb_to_linear(cam->background.z)) /
                    std::exp2(std::max(-16.0f, std::min(16.0f, ps.exposure)));
}

FilterFrame Editor::trace_filter_frame(PathTracer &pt, RenderTarget &rt, const Mat4 &v, const Mat4 &p, Vec3 eye, Vec3 forward, const Camera *cam) {
  FilterFrame frame;
  const Mat4 vp = p * v;
  frame.inv_view_proj = vp.inverse();
  frame.eye = eye;
  frame.forward = normalize(forward);
  frame.far_distance = cam ? cam->far_clip : 1000.0f;
  frame.pixel_scale = rt.height / 1080.0f;
  frame.time = play_time_;
  frame.animate = false;
  std::vector<float> dist;
  std::vector<uint32_t> ids;
  pt.first_hit_planes(dist, ids);
  const int W = rt.width, H = rt.height;
  if ((int)dist.size() < W * H) return frame;
  rt.depth.assign((size_t)W * H, 1.0f);
  rt.ids = std::move(ids);
  JobSystem::global().parallel_for(H, 16, [&](int64_t y0, int64_t y1) {
    for (int64_t y = y0; y < y1; y++)
      for (int x = 0; x < W; x++) {
        const size_t i = (size_t)y * W + x;
        const float t = dist[i];
        if (!(t < 1e5f)) continue;  // the sky stays at the far plane
        /* The hit point along this pixel's camera ray, then its z / w through the same projection the
         * rasterizer would have used. */
        const float nx = (x + 0.5f) / W * 2.0f - 1.0f, ny = 1.0f - (y + 0.5f) / H * 2.0f;
        const Vec4 nq = frame.inv_view_proj * Vec4(nx, ny, 0.0f, 1.0f), fq = frame.inv_view_proj * Vec4(nx, ny, 1.0f, 1.0f);
        if (nq.w == 0.0f || fq.w == 0.0f) continue;
        const Vec3 pa = nq.xyz() / nq.w, dir = normalize(fq.xyz() / fq.w - pa);  // as the tracer's ray: from the near plane
        const Vec4 q = vp * Vec4(pa + dir * t, 1.0f);
        if (q.w != 0.0f) rt.depth[i] = std::max(0.0f, std::min(1.0f, q.z / q.w));
      }
  });
  return frame;
}

/* ===================================================================== */
/* Final render (F12)                                                     */
/* ===================================================================== */

/* The camera's thin lens for the path tracer (Cycles: camera_sample_aperture). */
static PTLens camera_lens(const Camera *cam, float aspect);

/* F12 in MoonRay (task 0114): the scene exported as the path tracer would be given it, run in the background. False
 * when MoonRay can't render here (not installed): the path tracer renders instead. */
bool Editor::start_moonray(int w, int h, const Mat4 &v, const Mat4 &p, const Camera *cam, Vec3 eye) {
  if (!mr_located_) {  // (looked for in the background - WSL takes a moment to start - and started then)
    moonray_locate();
    mr_waiting_render_ = true;
    rendering_ = true;
    render_status_ = "MoonRay: looking for it...";
    return true;
  }
  if (!mr_.ready()) {
    Log::warn("MoonRay %s: rendering with the path tracer instead (Edit > Preferences > Renderers)",
              mr_.problem.empty() ? "isn't installed" : ("can't run here: " + mr_.problem).c_str());
    return false;
  }
  if (cam && cam->orthographic) {
    Log::warn("MoonRay: an orthographic camera isn't sent yet: rendering with the path tracer instead");
    return false;
  }
  const RenderSettings &rs = scene_->render;
  render_filtered_ = false;  // (camera filters are Blendity's: noted, not drawn)
  if (!render_filters_.empty()) Log::info("MoonRay: the camera's filters aren't applied to its render");
  moonray::ExportScene sc;
  for (const DrawItem &it : collect_items(true, true)) sc.objects.push_back({it.mesh, it.model, it.materials});
  sc.lights = make_lighting(eye, true, false).lights;
  sc.world = &env_;
  sc.view = v;
  sc.proj = p;
  sc.lens = camera_lens(cam, w / (float)h);
  sc.width = w;
  sc.height = h;
  sc.samples = rs.samples;
  sc.max_bounces = rs.max_bounces;
  const PTSettings ps = make_pt_settings(rs);
  sc.point_radius = ps.point_radius;
  sc.sun_angle_deg = ps.sun_angle_deg;
  MoonRayFrame fr;
  fr.width = w;
  fr.height = h;
  fr.transparent = rs.transparent;
  fr.solid = !rs.transparent && cam && cam->clear_flags == 1;
  fr.view_transform = ps.view_transform;
  fr.exposure = ps.exposure + (cam ? cam->exposure_stops() : 0.0f);
  if (fr.solid) fr.background = Vec3(srgb_to_linear(cam->background.x), srgb_to_linear(cam->background.y), srgb_to_linear(cam->background.z)) /
                                std::exp2(std::max(-16.0f, std::min(16.0f, fr.exposure)));
  sc.hide_background = fr.transparent || fr.solid;
  return begin_moonray_render(sc, fr);
}

static PTLens camera_lens(const Camera *cam, float aspect) {
  PTLens l;
  if (!cam) return l;
  l.radius = cam->aperture_radius(aspect);
  l.focus_distance = cam->focus_distance;
  l.blades = cam->blades >= 3 ? cam->blades : 0;
  l.rotation = cam->blade_rotation * kDeg2Rad;
  return l;
}

void Editor::camera_dof(RenderTarget &rt, const Mat4 &view, const Mat4 &proj, Vec3 eye, Vec3 forward, const Camera *cam, float aspect, float vfov_deg) {
  if (!cam || !cam->dof || cam->orthographic) return;
  DofParams d;
  d.inv_view_proj = (proj * view).inverse();
  d.eye = eye;
  d.forward = normalize(forward);
  d.aperture_radius = cam->aperture_radius(aspect);
  d.focus_distance = std::max(0.01f, cam->focus_distance);
  d.tan_half_vfov = std::tan(std::max(0.1f, vfov_deg) * 0.5f * kDeg2Rad);
  d.far_distance = cam->far_clip;
  d.max_radius_px = std::max(4.0f, rt.height * 0.05f);
  d.samples = rt.width * rt.height > 600000 ? 48 : 64;
  apply_depth_of_field(rt, d);
}

void Editor::update_camera_focus() {
  scene_->for_each([&](GameObject &g) {
    Camera *c = g.get<Camera>();
    if (!c || !c->dof || !c->focus_track) return;
    const float d = dot(c->focus_point - g.world_position(), normalize(g.world_rotation().rotate({0, 0, 1})));
    if (d > 0.01f && std::fabs(d - c->focus_distance) > 1e-5f) c->focus_distance = d;
  });
}

void Editor::start_final_render(bool preview, bool open_window) {
  /* F12 and the Render window's preview draw on the CPU, the reference (the GPU is the interactive views'). */
  struct KeepCpu {
    bool &off;
    bool was;
    ~KeepCpu() { off = was; }
  } keep_cpu{gpu_view_off_, gpu_view_off_};
  gpu_view_off_ = true;
  mr_job_.reset();  // (a MoonRay render under way stops: this render replaces it)
  mr_waiting_render_ = false;
  const RenderSettings &rs = scene_->render;
  const int pct = preview ? std::max(1, rs.percent * rs.preview_percent / 100) : rs.percent;
  int w = std::max(16, rs.width * pct / 100), h = std::max(16, rs.height * pct / 100);
  render_preview_ = preview;
  live_preview_hash_ = live_preview_hash();
  live_preview_time_ = now_seconds();
  GameObject *owner = nullptr;
  Camera *cam = main_camera(*scene_, &owner);
  /* A camera with its own aspect ratio keeps the width and sets the height. */
  if (cam) h = std::max(16, (int)std::lround(w / cam->image_aspect(w / (float)h)));
  Mat4 v, p;
  camera_for_render(v, p, w / (float)h);
  Vec3 eye = owner ? owner->world_position() : cam_.position();
  render_img_.resize(w, h);
  render_linear_.clear();
  render_alpha_lin_.clear();
  render_transparent_ = rs.transparent;
  render_start_ = now_seconds();
  render_resolved_samples_ = -1;  // (task 0161: the next frame resolves whatever the tracer holds)
  render_has_result_ = true;
  if (open_window) dock_open(WindowKind::Render);
  render_filters_ = camera_filters(cam ? owner : nullptr);
  render_filtered_ = !render_filters_.empty();
  if (rs.engine == 2 && !preview && start_moonray(w, h, v, p, cam, eye)) return;  // (task 0114; else the path tracer)
  if (rs.engine == 0 && render_filtered_) {
    /* A filtered camera: its own resolution and passes; no supersampling (it would soften
     * the pixels the filter is there to make). */
    RenderTarget rt;
    rt.attach(render_img_, {0, 0, w, h});
    Renderer3D r3d;
    render_with_alpha([&] { render_camera(r3d, rt, render_ft_, v, p, eye, owner->world_rotation().rotate({0, 0, 1}), owner, cam, w / (float)h); });
    render_time_ = now_seconds() - render_start_;
    rendering_ = false;
    int iw, ih;
    Recti dst;
    filter_layout(render_filters_, w, h, iw, ih, dst);
    render_status_ = strprintf("%s %dx%d through camera filters (%dx%d), %.0f ms (%zu tris)", preview ? "Preview (rasterized)" : "Rasterized", w, h, iw, ih,
                               render_time_ * 1000.0, r3d.stats().tris_submitted);
    if (!preview) Log::info("Render finished: %s", render_status_.c_str());
    return;
  }
  if (rs.engine == 0) {
    /* Rasterized: supersample then box-filter down (SSAA). */
    int aa = preview ? 1 : std::max(1, std::min(4, rs.raster_aa));
    Image big;
    big.resize(w * aa, h * aa);
    RenderTarget rt;
    rt.attach(big, {0, 0, big.width, big.height});
    Renderer3D r3d;
    render_with_alpha([&] {
    render_deferred(r3d, rt, v, p, eye, true, true, cam);
    if (cam && owner) camera_dof(rt, v, p, eye, owner->world_rotation().rotate({0, 0, 1}), cam, w / (float)h, cam->vertical_fov_deg(w / (float)h));
    JobSystem::global().parallel_for(h, 8, [&](int64_t y0, int64_t y1) {
      for (int64_t y = y0; y < y1; y++)
        for (int x = 0; x < w; x++) {
          uint32_t r = 0, g = 0, b = 0;
          for (int sy = 0; sy < aa; sy++)
            for (int sx = 0; sx < aa; sx++) {
              uint32_t c = big.pixels[(size_t)(y * aa + sy) * big.width + x * aa + sx];
              r += (c >> 16) & 255;
              g += (c >> 8) & 255;
              b += c & 255;
            }
          int n = aa * aa;
          render_img_.pixels[(size_t)y * w + x] = 0xFF000000u | ((r / n) << 16) | ((g / n) << 8) | (b / n);
        }
    });
    });
    render_time_ = now_seconds() - render_start_;
    rendering_ = false;
    render_status_ = strprintf("%s %dx%d, %dx SSAA, %.0f ms (%zu tris)", preview ? "Preview (rasterized)" : "Rasterized", w, h, aa,
                               render_time_ * 1000.0, r3d.stats().tris_submitted);
    if (!preview) Log::info("Render finished: %s", render_status_.c_str());
    return;
  }
  LightingEnv env = make_lighting(eye, true, false);  // every light: the path tracer computes true GI
  std::vector<DrawItem> items = collect_items(true, true);
  PTSettings ps = make_pt_settings(rs);
  if (cam) ps.exposure += cam->exposure_stops();
  pt_solid_background(ps, cam);
  if (rs.transparent) {  // Film > Transparent: camera rays that miss add nothing (the world still lights the scene)
    ps.solid_background = true;
    ps.background = Vec3(0.0f);
    ps.transparent_film = true;
  }
  int tw = w, th = h;
  if (render_filtered_) {
    /* Traced at the filters' resolution (fewer samples to wait for), scaled up when shown. */
    filter_layout(render_filters_, w, h, tw, th, render_dst_);
    render_small_.resize(tw, th);
    if (render_dst_.w != w || render_dst_.h != h) p = cam->projection(render_dst_.w / (float)render_dst_.h);
    std::fill(render_img_.pixels.begin(), render_img_.pixels.end(), 0xFF000000u);
  }
  retro_for_trace(render_filters_, v, p, tw, th, ps, items, render_snapped_);  // (no-op without a filter)
  final_pt_.set_settings(ps);  // before build(): it picks the ray backend
  build_pt(final_pt_, *this, items, env, env_);
  render_view_ = v;
  render_proj_ = p;
  render_eye_ = eye;
  render_forward_ = owner ? owner->world_rotation().rotate({0, 0, 1}) : cam_.forward();
  render_far_ = cam ? cam->far_clip : 1000.0f;
  final_pt_.set_camera(v, p, tw, th, camera_lens(cam, tw / (float)th));
  rendering_ = true;
  render_status_ = strprintf("%s %dx%d: %s scene of %zu tris built in %.0f ms", preview ? "Preview" : "Path tracing", w, h, final_pt_.device_summary().c_str(),
                             final_pt_.stats().triangles,
                             final_pt_.stats().bvh_build_ms);
  if (!preview) Log::info("%s", render_status_.c_str());
}

/* Task 0159: the card's busy share over the whole F12 render ("" on the CPU or without timestamps). */
std::string Editor::render_gpu_busy_text() const {
  const PTStats &s = final_pt_.stats();
  if (s.gpu_busy_ms < 0.0 || render_time_ <= 0.0) return "";
  const double cards = (double)std::max<size_t>(1, s.gpu_samples.size());  // (the busy is every card's, summed: a share per card)
  return strprintf("; GPU busy %.0f %% of the render's wall time (%s)", 100.0 * s.gpu_busy_ms / (render_time_ * 1000.0 * cards), cards > 1 ? "a card's average" : "one card");
}

void Editor::step_final_render() {
  if (!rendering_) return;
  if (mr_job_ || mr_waiting_render_) return step_moonray_render();
  const RenderSettings &rs = scene_->render;
  int target = std::max(1, render_preview_ ? rs.preview_samples : rs.samples);
  if (final_pt_.adopt_ready_gpus()) {  // its GPU opened (task 0037): the samples restart there
    render_start_ = now_seconds();
    render_resolved_samples_ = -1;  // (task 0161: the next frame resolves whatever the tracer holds)
    render_status_ = strprintf("%s: %s", render_preview_ ? "Preview" : "Path tracing", final_pt_.device_summary().c_str());
    Log::info("%s", render_status_.c_str());
  }
  /* The CPU's slice, or with cards at work (task 0161, ADR 0052) a short wait: they render between frames. */
  int done = final_pt_.render(final_pt_.gpu_async() ? 4.0 : 40.0, target);
  render_time_ = now_seconds() - render_start_;
  bool finished = done >= target;
  /* Task 0161: the picture can only change when the tracer merged new samples (the cards' sums arrive every merge
   * interval); a frame that merged nothing keeps the resolved image and stays short. */
  if (finished || final_pt_.samples() != render_resolved_samples_) {
    resolve_final_render(finished);
    render_resolved_samples_ = final_pt_.samples();
  }
  /* Samples per second, and how they were shared when several devices render. */
  std::string split;
  if (!final_pt_.stats().gpu_samples.empty()) {
    split = strprintf("  (CPU %d", final_pt_.stats().cpu_samples);
    for (size_t g = 0; g < final_pt_.stats().gpu_samples.size(); g++) split += strprintf(", GPU%zu %d", g + 1, final_pt_.stats().gpu_samples[g]);
    split += ")";
  }
  render_status_ = strprintf("%s: sample %d / %d%s  |  %.1f s  |  %.1f samples/s  |  %s%s", render_preview_ ? "Preview" : "Path tracing", done,
                             target, split.c_str(), render_time_, done / std::max(1e-3, render_time_), final_pt_.device_summary().c_str(),
                             finished && rs.denoise ? strprintf("  |  denoised (%s)", final_pt_.denoise_backend()).c_str() : "");
  if (finished) {
    rendering_ = false;
    if (!render_preview_) Log::info("Render finished: %d samples in %.1f s (%s)%s", done, render_time_, final_pt_.device_summary().c_str(), render_gpu_busy_text().c_str());
  }
}

/* A path tracer's linear output (w x h x 3, before exposure) as a target's HDR plane, for bloom. */
void Editor::fill_hdr_from_linear(RenderTarget &rt, const std::vector<float> &lin, ViewTransform vt, float exposure) {
  const size_t n = (size_t)rt.width * rt.height;
  if (lin.size() < n * 3) return;
  rt.hdr.resize(n);
  for (size_t i = 0; i < n; i++) rt.hdr[i] = Vec3(lin[i * 3], lin[i * 3 + 1], lin[i * 3 + 2]);
  rt.hdr_view_transform = vt;
  rt.hdr_exposure = exposure;
}

void Editor::resolve_final_render(bool finished) {
  const bool denoise = scene_->render.denoise;
  if (!render_filtered_) {
    if (finished) {
      /* Denoise once and keep the linear result for HDR / EXR saving. */
      render_linear_ = final_pt_.linear_rgb(denoise);
      final_pt_.resolve_rgb(render_linear_, render_img_.pixels.data(), render_img_.width);
      if (render_transparent_) apply_trace_alpha(render_linear_, false);
    }
    else if (render_transparent_) apply_trace_alpha(final_pt_.linear_rgb(false), false);
    else final_pt_.resolve(render_img_.pixels.data(), render_img_.width, false);
    return;
  }
  /* Through camera filters: the small image, its passes (with the first hits' depth and ids, so outlines and
   * fog work), then scaled up. No float result: a retro image is an 8-bit one. */
  Image &s = render_small_;
  RenderTarget rt;
  rt.attach(s, {0, 0, s.width, s.height});
  if (finished) {
    const std::vector<float> lin = final_pt_.linear_rgb(denoise);
    final_pt_.resolve_rgb(lin, s.pixels.data(), s.width);
    if (render_filters_.needs_hdr) fill_hdr_from_linear(rt, lin, final_pt_.settings().view_transform, final_pt_.settings().exposure);
  }
  else {
    const std::vector<float> lin = final_pt_.linear_rgb(false);  // once: the resolve and bloom's plane both read it
    final_pt_.resolve_rgb(lin, s.pixels.data(), s.width);
    if (render_filters_.needs_hdr)  // bloom reads the linear light at every resolve, so the glow doesn't grow with the samples
      fill_hdr_from_linear(rt, lin, final_pt_.settings().view_transform, final_pt_.settings().exposure);
  }
  FilterFrame frame = trace_filter_frame(final_pt_, rt, render_view_, render_proj_, render_eye_, render_forward_, nullptr);
  frame.far_distance = render_far_;  // the camera's when the render started (it may have changed since)
  if (render_transparent_) {
    /* Transparent (task 0035): the picture (straight colour by the coverage) over black and over white, each through
     * the passes; alpha from the difference, so what a pass draws or moves (outlines, pixelation, distortion)
     * carries its alpha as in the rasterizer. */
    const std::vector<float> lin = final_pt_.linear_rgb(finished && denoise), cov = final_pt_.coverage();
    const size_t n = (size_t)s.width * s.height;
    if (cov.size() >= n && lin.size() >= n * 3) {
      std::vector<uint32_t> over_black(n), over_white(n);
      const ViewTransform vt = final_pt_.settings().view_transform;
      const float ex = final_pt_.settings().exposure;
      for (size_t i = 0; i < n; i++) {
        const float a = clampf(cov[i], 0.0f, 1.0f), d = std::max(a, 0.05f);
        const float st[3] = {lin[i * 3] / d, lin[i * 3 + 1] / d, lin[i * 3 + 2] / d};
        uint32_t px;
        display::encode_span(st, &px, 1, vt, ex);
        auto ch = [&](int sh, float bg) { return (uint32_t)std::lround(((px >> sh) & 255) * a + bg * (1.0f - a)); };
        over_black[i] = 0xFF000000u | ch(16, 0) << 16 | ch(8, 0) << 8 | ch(0, 0);
        over_white[i] = 0xFF000000u | ch(16, 255) << 16 | ch(8, 255) << 8 | ch(0, 255);
      }
      const std::vector<Vec3> hdr = rt.hdr;  // (bloom's light, the same for both: the background has none)
      s.pixels = over_black;
      for (const FilterPass &pass : render_filters_.passes) pass(rt, &frame);
      const std::vector<uint32_t> black = s.pixels;
      s.pixels = over_white;
      rt.hdr = hdr;
      for (const FilterPass &pass : render_filters_.passes) pass(rt, &frame);
      combine_black_white(black.data(), s.pixels.data(), n);
    }
    upscale_nearest(s.pixels.data(), s.width, s.height, s.width, render_img_.pixels.data(), render_img_.width, render_dst_);
    return;
  }
  for (const FilterPass &pass : render_filters_.passes) pass(rt, &frame);
  upscale_nearest(s.pixels.data(), s.width, s.height, s.width, render_img_.pixels.data(), render_img_.width, render_dst_);
}

/* A transparent rasterized render (Film > Transparent, task 0035): drawn over black, then over white; where the two
 * differ the background showed through. alpha = 1 - (white - black), colour = black / alpha (straight). */
void Editor::render_with_alpha(const std::function<void()> &once) {
  struct FinalRender {  // the quality tier's caps are for the editor's views, not for renders
    bool &flag;
    explicit FinalRender(bool &f) : flag(f) { flag = true; }
    ~FinalRender() { flag = false; }
  } final_render(final_render_);
  if (!render_transparent_) {
    once();
    return;
  }
  render_clear_override_on_ = true;
  render_clear_override_ = 0xFF000000u;
  once();
  const std::vector<uint32_t> black = render_img_.pixels;
  render_clear_override_ = 0xFFFFFFFFu;
  once();
  render_clear_override_on_ = false;
  combine_black_white(black.data(), render_img_.pixels.data(), render_img_.pixels.size());
}

/* A transparent path-traced render (task 0035): alpha from the share of camera samples that hit something; the
 * colour (premultiplied: misses add nothing) divided by it before the view transform. Filtered: the passes ran on the
 * premultiplied picture; its display colour is divided instead, and the alpha scaled up as the picture was. */
void Editor::apply_trace_alpha(const std::vector<float> &lin, bool filtered) {
  const std::vector<float> cov = final_pt_.coverage();
  const int w = render_img_.width, h = render_img_.height;
  if (!filtered) {
    if (cov.size() < (size_t)w * h || lin.size() < (size_t)w * h * 3) return;
    const ViewTransform vt = final_pt_.settings().view_transform;
    const float ex = final_pt_.settings().exposure;
    JobSystem::global().parallel_for(h, 16, [&](int64_t y0, int64_t y1) {
      for (int64_t y = y0; y < y1; y++)
        for (int x = 0; x < w; x++) {
          const size_t i = (size_t)y * w + x;
          const float a = cov[i];
          const int a8 = (int)std::lround(clampf(a, 0.0f, 1.0f) * 255.0f);
          if (a8 <= 0) {
            render_img_.pixels[i] = 0;
            continue;
          }
          const float d = std::max(a, 0.05f);  // (a silhouette's few samples: don't amplify the denoised colour's bleed)
          const float straight[3] = {lin[i * 3] / d, lin[i * 3 + 1] / d, lin[i * 3 + 2] / d};
          uint32_t px;
          display::encode_span(straight, &px, 1, vt, ex);
          render_img_.pixels[i] = (uint32_t)a8 << 24 | (px & 0xFFFFFF);
        }
    });
    if (render_linear_.size() == (size_t)w * h * 3) render_alpha_lin_ = cov;
    return;
  }
  (void)filtered;  // (a filtered render's alpha comes from its two pass runs, in resolve_final_render)
}

void Editor::save_render() {
  if (!render_has_result_ || render_img_.width == 0) {
    Log::warn("Nothing rendered yet - press Render Image (F12) first");
    return;
  }
  if (render_preview_) Log::warn("Saving a preview (%d%% resolution) - press Render for the full image", scene_->render.preview_percent);
  std::string dir = fs::join(project_root_, "Renders");
  fs::make_dirs(dir);
  std::time_t t = std::time(nullptr);
  char buf[64];
  std::strftime(buf, sizeof(buf), "render_%Y%m%d_%H%M%S", std::localtime(&t));
  int fmt = scene_->render.file_format;  // 0 PNG, 1 JPEG, 2 Radiance HDR, 3 OpenEXR
  if (fmt == 3 && !exr_available()) {
    Log::warn("This build has no OpenEXR (needs Blender's libraries); saving Radiance HDR instead");
    fmt = 2;
  }
  if (fmt >= 2 && render_linear_.empty()) {
    Log::warn("Float formats need the path-traced engine (linear light); saving PNG instead");
    fmt = 0;
  }
  static const char *exts[] = {".png", ".jpg", ".hdr", ".exr"};
  std::string path = fs::join(dir, std::string(buf) + exts[fmt]);
  /* Ask where (Blender: Image > Save As); the file type follows the name or the filter. */
  std::vector<platform::FileFilter> filters = {{"PNG", {".png"}}, {"JPEG", {".jpg", ".jpeg"}}, {"Radiance HDR", {".hdr"}}};
  if (exr_available()) filters.push_back({"OpenEXR", {".exr"}});
  int filter = std::min(fmt, (int)filters.size() - 1);
  std::string chosen;
  switch (pick_save_path("Save Render", path, filters, chosen, &filter)) {
    case PathPick::Cancelled: return;
    case PathPick::Unavailable: break;
    case PathPick::Chosen: {
      path = chosen;
      const std::string ext = fs::extension(path);
      int by_name = ext == ".png" ? 0 : (ext == ".jpg" || ext == ".jpeg") ? 1 : ext == ".hdr" ? 2 : ext == ".exr" && exr_available() ? 3 : -1;
      fmt = by_name >= 0 ? by_name : filter;
      if (fmt >= 2 && render_linear_.empty()) {
        Log::warn("Float formats need the path-traced engine (linear light); saving PNG instead");
        fmt = 0;
        path = fs::join(fs::parent(path), fs::stem(path) + ".png");
      }
      break;
    }
  }
  const int w = render_img_.width, h = render_img_.height;
  const std::vector<uint32_t> jpg = render_transparent_ && fmt == 1 ? over_black(render_img_.pixels) : std::vector<uint32_t>();
  const float *alpha = render_transparent_ && render_alpha_lin_.size() == (size_t)w * h ? render_alpha_lin_.data() : nullptr;
  if (render_transparent_ && (fmt == 1 || fmt == 2))
    Log::info("%s has no alpha: the transparent background is saved black (PNG or EXR keep it)", fmt == 1 ? "JPEG" : "Radiance HDR");
  bool ok = fmt == 0   ? write_png(path, render_img_.pixels.data(), w, h, w, render_transparent_)
            : fmt == 1 ? write_jpeg(path, jpg.empty() ? render_img_.pixels.data() : jpg.data(), w, h, w, scene_->render.jpeg_quality)
            : fmt == 2 ? write_hdr(path, render_linear_.data(), w, h)
                       : write_exr(path, render_linear_.data(), w, h, true, alpha);
  if (ok) Log::info("Saved render: %s", path.c_str());
  else Log::error("Could not write %s", path.c_str());
  project_listed_ = -100;
}

/* What a render through one camera depends on: geometry, lights, materials,
 * the camera and every render / world setting. */
uint64_t Editor::camera_render_hash(const GameObject *owner, Camera *cam) { return camera_render_hash(owner, cam, scene_render_hash()); }

/* The same, given the scene hash already computed for this frame (the Scene view computes it for its
 * own key and then once more for its filters' camera). */
uint64_t Editor::camera_render_hash(const GameObject *owner, Camera *cam, uint64_t scene_hash) {
  uint64_t h = scene_hash;
  if (owner && cam) {
    Mat4 w = owner->world_matrix();
    uint64_t c = hash_component(*cam);
    h ^= c + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
    for (const auto &comp : owner->components)  // its camera filters too
      if (comp->render_role() == Component::RenderRole::CameraFilter) {
        const uint64_t fh = hash_component(*comp) ^ (uint64_t)comp->enabled;
        h ^= fh + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
      }
    for (float f : w.m) {
      uint32_t b;
      std::memcpy(&b, &f, 4);
      h = (h ^ b) * kFnvPrime;
    }
  }
  h ^= hash_reflect([this](Reflector &r) { scene_->render.reflect_raster(r); }) * 31;  // the path tracer's settings don't redraw a view
  h ^= hash_reflect([this](Reflector &r) { scene_->environment.reflect(r); }) * 131;
  h ^= hash_reflect([this](Reflector &r) { scene_->lighting.reflect(r); }) * 1031;  // baked and realtime GI settings
  return h;
}

/* ------------------------------------------------------- voxel GI (task 0013) */

/* The voxel grid and the sun's reflective shadow map for these items, each rebuilt only when what it
 * is drawn from changed: an object's voxels when its mesh or place did (the paper's static / dynamic
 * split), the RSM when the casters, their materials or the sun did. */
const VoxelGIFrame *Editor::update_voxel_gi(const std::vector<DrawItem> &items, const LightingEnv &env, bool game) {
  const LightingSettings &ls = scene_->lighting;
  vgi_updates_++;
  vgi_last_ = game ? 1 : 0;
  VoxelGIState &vgi = vgi_[vgi_last_];
  auto mix = [](uint64_t &h, const void *p, size_t n) { h = mix_bytes(h, p, n); };
  /* Occluders: what casts a shadow (as the shadow map's casters), but not see-through surfaces. */
  std::vector<const DrawItem *> occ;
  AABB bounds;
  for (const DrawItem &it : items) {
    bool solid = !it.materials || it.materials->empty();
    if (it.materials)
      for (const MaterialPtr &m : *it.materials)
        if (!m || ((m->opaque() || m->surface == (int)MaterialSurface::Cutout) && m->cast_shadows)) solid = true;
    if (!solid || !it.mesh) continue;
    occ.push_back(&it);
    bounds.add(it.mesh->bounds.transformed(it.model));
  }
  const int n = ls.gi_resolution <= 0 ? 64 : ls.gi_resolution == 1 ? 128 : 256;
  VoxelGrid fit;
  voxel_grid_fit(fit, bounds, n, false);  // (its placement: the columns only if it's taken below)
  if (fit.n <= 0) return nullptr;
  /* Keep the grid while the scene still fits it at the same voxel size (dragging the outermost object
   * would otherwise move the grid every 8 voxels and re-voxelize everything). */
  const bool keep = vgi.grid.valid() && vgi.grid.n == fit.n && vgi.grid.voxel == fit.voxel && bounds.min.x >= vgi.grid.origin.x &&
                    bounds.min.y >= vgi.grid.origin.y && bounds.min.z >= vgi.grid.origin.z &&
                    bounds.max.x <= vgi.grid.origin.x + fit.n * fit.voxel && bounds.max.y <= vgi.grid.origin.y + VoxelGrid::kDepth * fit.voxel &&
                    bounds.max.z <= vgi.grid.origin.z + fit.n * fit.voxel;
  if (!keep && fit.key != vgi.grid.key) {  // a new grid: every object's voxels are re-made
    voxel_grid_allocate(fit);
    vgi.grid = std::move(fit);
    vgi.objects.clear();
    vgi.assembled = 0;
  }
  uint64_t assembled = vgi.grid.key;
  std::vector<const std::vector<VoxelSpan> *> spans;
  for (const DrawItem *it : occ) {
    uint64_t key = vgi.grid.key;
    mix(key, &it->mesh->serial, 8);
    mix(key, it->model.m, sizeof(it->model.m));
    VoxelObject &o = vgi.objects[it->id];
    if (o.key != key || it->mesh->serial == 0) {
      voxelize_mesh(vgi.grid, *it->mesh, it->model, o.spans);
      o.key = key;
      vgi.voxelized++;
    }
    mix(assembled, &key, 8);
    spans.push_back(&o.spans);
  }
  if (vgi.objects.size() > occ.size() * 2 + 16) {  // forget objects that are gone
    std::unordered_set<uint64_t> live;
    for (const DrawItem *it : occ) live.insert(it->id);
    for (auto it = vgi.objects.begin(); it != vgi.objects.end();)
      it = live.count(it->first) ? std::next(it) : vgi.objects.erase(it);
  }
  if (assembled != vgi.assembled) {
    voxel_grid_build(vgi.grid, spans);
    vgi.assembled = assembled;
    vgi.assemblies++;
  }
  /* The RSM: the first directional light, as the shadow map. */
  const RenderLight *sun = nullptr;
  for (const RenderLight &l : env.lights)
    if (l.type == RenderLight::Directional) {
      sun = &l;
      break;
    }
  const int rsm_res = std::max(32, std::min(tier_rsm_cap_ > 0 && !final_render_ ? tier_rsm_cap_ : 2048, ls.gi_rsm_resolution));
  if (ls.gi_bounce && sun) {
    uint64_t key = kFnvBasis;
    for (const DrawItem *it : occ) {
      mix(key, &it->mesh->serial, 8);
      mix(key, it->model.m, sizeof(it->model.m));
      if (it->materials)
        for (const MaterialPtr &m : *it->materials) {
          const uint64_t mv[2] = {(uint64_t)(uintptr_t)m.get(), m ? m->version : 0};
          mix(key, mv, sizeof(mv));
          if (m && m->axis_mode != 0)  // the bounce's colour comes from an axis material's parts too
            for (const MaterialPtr &part : m->textures().axis) {
              const uint64_t pv[2] = {(uint64_t)(uintptr_t)part.get(), part ? part->version : 0};
              mix(key, pv, sizeof(pv));
            }
        }
    }
    mix(key, &sun->direction, sizeof(Vec3));
    mix(key, &sun->color, sizeof(Vec3));
    mix(key, &sun->intensity, sizeof(float));
    mix(key, &rsm_res, sizeof(int));
    if (key != vgi.rsm_key) {
      std::vector<DrawItem> casters;
      for (const DrawItem *it : occ) casters.push_back(*it);
      AABB b;
      for (const DrawItem &c : casters) b.add(c.mesh->bounds.transformed(c.model));
      render_rsm(vgi.rsm, casters, *sun, b, rsm_res);
      vgi.rsm_key = key;
      vgi.rsm_renders++;
    }
  }
  else {
    vgi.rsm = Rsm{};
    vgi.rsm_key = 0;
  }
  GiParams p;
  p.rays = tier_gi_rays_cap_ > 0 && !final_render_ ? std::min(ls.gi_rays, tier_gi_rays_cap_) : ls.gi_rays;  // (the views only)
  p.radius = ls.gi_radius;
  p.intensity = ls.gi_intensity;
  p.sky_occlusion = ls.gi_sky_occlusion;
  p.bounce = ls.gi_bounce;
  p.downsample = ls.gi_downsample == 0 ? 2 : 4;
  p.specular_occlusion = ls.gi_specular_occlusion;
  vgi.frame.params = gi_params_sanitized(p);
  vgi.frame.grid = &vgi.grid;
  vgi.frame.rsm = vgi.rsm.valid() ? &vgi.rsm : nullptr;
  /* Lightmapped surfaces add the bounce of a sun the bake doesn't hold (Realtime, or added since). */
  vgi.frame.bounce_not_baked = true;
  if (sun && !shown_lighting().empty() && ls.baked_gi) vgi.frame.bounce_not_baked = sun->bake_mode == 0;
  /* Realtime lightmaps (task 0016): laid out again when a Contribute GI object's mesh or placement or the
   * resolution changes (unwrapped UVs are kept per object); update_realtime_lightmaps() fills them. */
  const float rres = finite_bits(ls.realtime_resolution) ? std::max(0.0f, std::min(100.0f, ls.realtime_resolution)) : 2.0f;
  if (rres > 0.0f) {
    uint64_t lkey = kFnvBasis;
    mix(lkey, &rres, sizeof(float));
    mix(lkey, &scene_ident_, 8);  // another scene's objects can share ids
    for (const DrawItem &it : items) {
      if (!it.realtime_lm || !it.mesh) continue;
      mix(lkey, &it.id, sizeof(it.id));
      mix(lkey, &it.mesh->serial, 8);
      mix(lkey, it.model.m, sizeof(it.model.m));
    }
    if (lkey != vgi.rtlm.layout_key) {
      std::vector<BakeObject> objs;
      for (const DrawItem &it : items) {
        if (!it.realtime_lm || !it.mesh) continue;
        GameObject *g = scene_->find(it.id);
        const MeshRenderer *mr = g ? g->get<MeshRenderer>() : nullptr;
        const Mesh *m = g ? g->evaluated_mesh(game ? 1 : 0) : nullptr;
        if (!mr || !m || (&m->render_mesh() != it.mesh && &m->render_mesh_tangents() != it.mesh)) continue;
        BakeObject o;
        o.id = it.id;
        o.mesh = std::shared_ptr<const Mesh>(std::shared_ptr<const Mesh>(), m);  // not owned: laid out now
        o.model = it.model;
        o.scale = mr->scale_in_lightmap;
        o.generate_uvs = mr->generate_lightmap_uvs;
        o.hash = scene_ident_;  // Entry::hash: which scene the chart belongs to
        objs.push_back(std::move(o));
      }
      realtime_lightmap_layout(vgi.rtlm, objs, rres, lkey);
    }
  }
  else if (vgi.rtlm.layout_key != 0) {
    const uint64_t gen = vgi.rtlm.generation;
    vgi.rtlm = RealtimeLightmaps{};
    vgi.rtlm.generation = gen + 1;
  }
  return &vgi.frame;
}

/* ------------------------------------------------------- view render cache */

static std::atomic<uint64_t> g_cache_mismatches{0};
uint64_t Editor::render_cache_mismatches_all() { return g_cache_mismatches.load(); }
void Editor::note_render_cache_mismatch() { g_cache_mismatches++; }

/* Everything a rasterized view draws from the scene (ADR 0009): scene_render_hash() (geometry,
 * transforms, the hierarchy, MeshRenderer flags, materials, lights, the world) and the render and
 * world settings. Computed for each view that asks (not once per frame: a panel drawn
 * between two views can change the scene). */
uint64_t Editor::frame_scene_hash(uint64_t *scene_hash) {
  uint64_t h = scene_render_hash();
  if (scene_hash) *scene_hash = h;
  auto mix = [&](uint64_t v) { h = hash_mix(h, v); };
  mix(hash_reflect([this](Reflector &r) { scene_->render.reflect_raster(r); }));  // (not the path tracer's settings)
  mix(hash_reflect([this](Reflector &r) { scene_->environment.reflect(r); }));
  mix(hash_reflect([this](Reflector &r) { scene_->lighting.reflect(r); }));  // baked and realtime GI settings
  return h;
}

/* What the Scene view's 3D layer depends on besides the scene: its camera and size, the shading and
 * overlay toggles, the selection (outlines, Edit Mode highlights) and the UI scale (line widths). */
uint64_t Editor::scene_view_key(const Recti &view) {
  uint64_t scene_hash = 0;
  uint64_t h = frame_scene_hash(&scene_hash);
  auto mix = [&](const void *p, size_t n) { h = mix_bytes(h, p, n); };
  const float aspect = view.w / (float)std::max(1, view.h);
  const Mat4 v = cam_.view(), p = cam_.proj(aspect);
  mix(&view, sizeof(view));
  mix(v.m, sizeof(v.m));
  mix(p.m, sizeof(p.m));
  const uint64_t flags = (uint64_t)shading_ | (uint64_t)scene_lighting_ << 8 | (uint64_t)scene_filters_ << 9 | (uint64_t)show_grid_ << 10 |
                         (uint64_t)edit_mode_ << 11 | (uint64_t)ngon_mode_ << 12 | (uint64_t)elem_ << 16 |
                         (uint64_t)scene_full_size() << 24;  // (the card's frames draw at full size: task 0100)
  mix(&flags, 8);
  mix(&ui_.scale, sizeof(float));
  mix(&cam_.distance, sizeof(float));  // the grid's level follows the orbit distance and pivot
  mix(&cam_.pivot, sizeof(Vec3));
  const uint64_t ro = raster_opt_key();
  mix(&ro, 8);
  mix(&active_, 8);
  mix(&edit_obj_, 8);
  const uint64_t wp[4] = {(uint64_t)weight_paint_, wp_obj_, wp_version_, (uint64_t)wp_group_};  // the painted weights (task 0046)
  mix(wp, sizeof(wp));
  mix(selection_.data(), selection_.size() * sizeof(uint64_t));
  mix(vert_sel_.data(), vert_sel_.size());
  mix(face_sel_.data(), face_sel_.size());
  uint64_t es = edge_sel_.size();  // order-free: the set's iteration order isn't stable
  for (uint64_t e : edge_sel_) es += (e * 0x9E3779B97F4A7C15ull) ^ (e >> 31);
  mix(&es, 8);
  if (scene_filters_) {  // the main camera's filters are on the view
    GameObject *owner = nullptr;
    if (Camera *mc = main_camera(*scene_, &owner)) {
      const uint64_t ch = camera_render_hash(owner, mc, scene_hash);
      mix(&ch, 8);
    }
  }
  return h ? h : 1;
}

/* Puts a cached picture back into the framebuffer (the frame cleared it), if it is the one for `key`. */
/* Task 0193: a cached picture the card holds can be shown only while it presents this window and the slot still has it;
 * shown, it goes in this frame's views. */
bool Editor::card_picture_kept(const ViewCache &c) const {
  return c.card_slot < 0 || (card_present_ && gpu_slot_gen_[c.card_slot] == c.card_gen);
}

bool Editor::view_cache_hit(ViewCache &c, uint64_t key, const Recti &r) {
  if (key == 0 || c.key != key || c.img.width != r.w || c.img.height != r.h || r.x < 0 || r.y < 0 || r.right() > fb_.width ||
      r.bottom() > fb_.height || !card_picture_kept(c))
    return false;
  if (c.card_slot >= 0 && !cache_verify_) card_views_.push_back({c.card_slot, r.x, r.y});
  JobSystem::global().parallel_for(r.h, 64, [&](int64_t y0, int64_t y1) {
    for (int64_t y = y0; y < y1; y++) std::memcpy(fb_.row((int)(r.y + y)) + r.x, c.img.row((int)y), sizeof(uint32_t) * r.w);
  });
  return true;
}

bool Editor::view_cache_show(ViewCache &c, const Recti &r) {
  if (c.key == 0 || c.img.width != r.w || c.img.height != r.h || r.x < 0 || r.y < 0 || r.right() > fb_.width || r.bottom() > fb_.height ||
      !card_picture_kept(c))
    return false;
  if (c.card_slot >= 0) card_views_.push_back({c.card_slot, r.x, r.y});
  JobSystem::global().parallel_for(r.h, 64, [&](int64_t y0, int64_t y1) {
    for (int64_t y = y0; y < y1; y++) std::memcpy(fb_.row((int)(r.y + y)) + r.x, c.img.row((int)y), sizeof(uint32_t) * r.w);
  });
  return true;
}

void Editor::view_cache_store(ViewCache &c, uint64_t key, const Recti &r) {
  c.key = 0;
  c.card_slot = -1;
  for (const gpu::Raster::PresentView &v : card_views_)  // (task 0193: its picture is the card's)
    if (v.x == r.x && v.y == r.y) c.card_slot = v.slot, c.card_gen = gpu_slot_gen_[v.slot];
  if (key == 0 || r.w <= 0 || r.h <= 0 || r.x < 0 || r.y < 0 || r.right() > fb_.width || r.bottom() > fb_.height) return;
  if (c.img.width != r.w || c.img.height != r.h) c.img.resize(r.w, r.h);
  JobSystem::global().parallel_for(r.h, 64, [&](int64_t y0, int64_t y1) {
    for (int64_t y = y0; y < y1; y++) std::memcpy(c.img.row((int)y), fb_.row((int)(r.y + y)) + r.x, sizeof(uint32_t) * r.w);
  });
  c.key = key;
}

/* Verify mode: the picture just rendered against the cached one it would have reused. */
bool Editor::view_cache_verify(const ViewCache &c, const Recti &r, const char *what) {
  if (c.img.width != r.w || c.img.height != r.h) return true;
  size_t diff = 0;
  for (int y = 0; y < r.h; y++)
    for (int x = 0; x < r.w; x++) diff += fb_.row(r.y + y)[r.x + x] != c.img.pixels[(size_t)y * c.img.width + x];
  if (diff) {
    prof_.cache_mismatches++;
    g_cache_mismatches++;
    Log::warn("Render cache: the %s would have shown a stale picture (%zu pixels differ)", what, diff);
  }
  return diff == 0;
}

/* The raster options (Profiler toggles: culling, threads, fast setup...), field by field. */
uint64_t Editor::raster_opt_key() const {
  const RasterOptions &o = raster_opt_;
  uint32_t vs;
  std::memcpy(&vs, &view_scale_, 4);  // Viewport Resolution Scale: a different picture in every view it applies to
  uint64_t k = (uint64_t)o.multithreaded | (uint64_t)o.backface_culling << 1 | (uint64_t)o.frustum_culling << 2 |
               (uint64_t)o.span_rows << 3 | (uint64_t)o.fast_setup << 4 | (uint64_t)o.affine_uv << 5 | (uint64_t)o.screen_door << 6 |
               (uint64_t)o.shade << 8 | (uint64_t)o.uniform_shading << 7 | (uint64_t)o.simd_shading << 11 | (uint64_t)(uint32_t)o.tile_size << 16;
  uint32_t snap;
  std::memcpy(&snap, &o.vertex_snap, 4);
  k ^= (uint64_t)snap << 32;
  k ^= (uint64_t)vs * 0xC2B2AE3D27D4EB4Full;
  /* The quality tier's caps (task 0050): the shadow map's size and Realtime GI's rays and reflective shadow map. */
  k ^= ((uint64_t)(uint32_t)tier_shadow_cap_ << 40 | (uint64_t)(uint32_t)tier_gi_rays_cap_ << 20 | (uint64_t)(uint32_t)tier_rsm_cap_) * 0x165667B19E3779F9ull;
  /* Task 0104: a picture drawn the cheaper way while moving is never reused once you stop (ADR 0039). */
  k ^= moving_cheap() ? 0xD6E8FEB86659FD93ull : 0;
  return k * 0x9E3779B97F4A7C15ull;
}

uint64_t Editor::live_preview_hash() {
  GameObject *owner = nullptr;
  Camera *cam = main_camera(*scene_, &owner);
  return camera_render_hash(owner, cam) ^ trace_settings_hash();
}

/* The path tracer's own settings (samples, bounces, denoiser, device...): a traced picture restarts when
 * they change, while the rasterized views' keys hash only the raster-relevant ones. */
uint64_t Editor::trace_settings_hash() { return hash_reflect([this](Reflector &r) { scene_->render.reflect(r); }) * 0x9E3779B97F4A7C15ull; }

/* Unity's Camera Preview: selecting a camera shows what it sees in a corner
 * of the Scene view. Shaded is the rasterizer (cheap); Rendered runs the
 * render engine at the inset's size, progressively, like a small render
 * preview (path traced with the camera's depth of field and exposure). */
void Editor::draw_camera_preview(const Recti &view) {
  /* Locked: the same camera stays up whatever is selected, in Edit Mode too,
   * so the shot can be watched while the mesh is changed. */
  GameObject *g = cam_preview_lock_ ? scene_->find(cam_preview_lock_) : active_object();
  if (cam_preview_lock_ && (!g || !g->get<Camera>())) {
    cam_preview_lock_ = 0;
    g = active_object();
  }
  Camera *cam = g ? g->get<Camera>() : nullptr;
  cam_preview_rect_ = Recti{};
  if (!cam || !cam->enabled || (edit_mode_ && !cam_preview_lock_) || (g && g->id == pilot_cam_)) {  // piloting: the view is the preview
    cam_preview_pt_hash_ = 0;
    cam_preview_key_ = 0;
    return;
  }
  auto &u = ui_;
  const RenderSettings &rs = scene_->render;
  const float aspect = cam->image_aspect(rs.width / (float)std::max(1, rs.height));
  int w = std::max(u.px(120), std::min(u.px(360), view.w / 4)), h = (int)(w / aspect);
  if (h > view.h / 3) {
    h = view.h / 3;
    w = (int)(h * aspect);
  }
  if (w < 32 || h < 24) return;
  Quat q = g->world_rotation();
  Vec3 eye = g->world_position();
  Mat4 v = Mat4::look_at(eye, eye + q.rotate({0, 0, 1}), q.rotate({0, 1, 0}));
  Mat4 p = cam->projection(aspect);
  const bool traced = cam_preview_rendered_;  // Rendered is always the path tracer, whatever the final engine
  std::string status;
  if (traced) {
    cam_preview_key_ = 0;  // the inset now holds the traced picture
    /* Rebuild when the scene, this camera or the inset size changes; then add
     * a few milliseconds of samples every frame until the preview count. */
    uint64_t hash = camera_render_hash(g, cam) ^ trace_settings_hash() ^ ((uint64_t)w << 40) ^ ((uint64_t)h << 20) ^ g->id;
    const FilterStack &filters = camera_filters(g);
    int tw = w, th = h;
    Recti tdst{0, 0, w, h};
    if (!filters.empty()) filter_layout(filters, w, h, tw, th, tdst);
    Image &traced_img = filters.empty() ? cam_preview_img_ : cam_preview_small_;
    if (hash != cam_preview_pt_hash_) {
      cam_preview_pt_hash_ = hash;
      LightingEnv env = make_lighting(eye, true, false);  // every light: the path tracer computes true GI
      std::vector<DrawItem> items = collect_items(true, true);
      PTSettings ps = make_pt_settings(rs);
      ps.exposure += cam->exposure_stops();
      pt_solid_background(ps, cam);
      const float ta = tdst.w / (float)std::max(1, tdst.h);
      const Mat4 tp = tdst.w == w && tdst.h == h ? p : cam->projection(ta);
      retro_for_trace(filters, v, tp, tw, th, ps, items, cam_preview_snapped_);  // a filter's rasterizer stages (task 0032)
      cam_preview_pt_.set_settings(ps);
      build_pt(cam_preview_pt_, *this, items, env, env_);
      cam_preview_pt_.set_camera(v, tp, tw, th, camera_lens(cam, ta));
      cam_preview_done_ = false;
    }
    else if (cam_preview_pt_.adopt_ready_gpus()) cam_preview_done_ = false;  // its GPU opened: samples restart there (task 0037)
    if (cam_preview_img_.width != w || cam_preview_img_.height != h) cam_preview_img_.resize(w, h);  // keeps a finished preview
    if (traced_img.width != tw || traced_img.height != th) traced_img.resize(tw, th);
    const int target = std::max(1, rs.preview_samples);
    if (!cam_preview_done_) {
      int done = cam_preview_pt_.render(cam_preview_pt_.gpu_async() ? 2.0 : 12.0, target);  // (task 0161: the cards work between frames)
      if (done >= target) {
        std::vector<float> lin = cam_preview_pt_.linear_rgb(rs.denoise);
        cam_preview_pt_.resolve_rgb(lin, traced_img.pixels.data(), tw);
        if (filters.needs_hdr) cam_preview_lin_ = std::move(lin);  // the bloom pass below reads it
        cam_preview_done_ = true;
      }
      else {
        if (filters.needs_hdr) {  // the linear light once: the resolve and bloom's plane (not twice a frame)
          cam_preview_lin_ = cam_preview_pt_.linear_rgb(false);
          cam_preview_pt_.resolve_rgb(cam_preview_lin_, traced_img.pixels.data(), tw);
        }
        else cam_preview_pt_.resolve(traced_img.pixels.data(), tw, false);
        u.redraw = true;
      }
      if (!filters.empty()) {
        /* The filters' passes, with the first hits' depth and ids (outlines, fog), scaled up into the inset. */
        RenderTarget trt;
        trt.attach(traced_img, {0, 0, tw, th});
        if (filters.needs_hdr)  // the linear light at every resolve (kept above): the bloom doesn't grow as samples accumulate
          fill_hdr_from_linear(trt, cam_preview_lin_, cam_preview_pt_.settings().view_transform, cam_preview_pt_.settings().exposure);
        const float ta = tdst.w / (float)std::max(1, tdst.h);
        const FilterFrame tframe = trace_filter_frame(cam_preview_pt_, trt, v, tdst.w == w && tdst.h == h ? p : cam->projection(ta), eye, q.rotate({0, 0, 1}), cam);
        for (const FilterPass &pass : filters.passes) pass(trt, &tframe);
        std::fill(cam_preview_img_.pixels.begin(), cam_preview_img_.pixels.end(), 0xFF000000u);
        upscale_nearest(traced_img.pixels.data(), tw, th, tw, cam_preview_img_.pixels.data(), w, tdst);
      }
    }
    status = strprintf("  %d / %d", std::min(cam_preview_pt_.samples(), target), target);
    if (cam_preview_pt_.waiting_for_gpu()) status += "  (CPU: GPU starting)";
  }
  else {
    cam_preview_pt_hash_ = 0;
    /* Re-rendered only when the scene, this camera or the inset's size changes. */
    ScopedTimer rt;
    const uint64_t key = (camera_render_hash(g, cam) ^ raster_opt_key() ^ ((uint64_t)w << 40) ^ ((uint64_t)h << 20) ^ g->id) | 1;
    const bool hit = key == cam_preview_key_ && cam_preview_img_.width == w && cam_preview_img_.height == h;
    const bool held = !hit && hold_other_views() && cam_preview_key_ != 0 && cam_preview_of_ == g->id && cam_preview_img_.width == w &&
                      cam_preview_img_.height == h;
    if (hit && !cache_verify_) prof_.view_cache_hits++;
    else if (held) prof_.views_held++;  // (task 0103: the drag's last picture until it ends; the key still differs then)
    else {
      std::vector<uint32_t> before;
      if (hit) before = cam_preview_img_.pixels;
      cam_preview_img_.resize(w, h);
      cam_preview_rt_.attach(cam_preview_img_, {0, 0, w, h});
      render_scaled(cam_preview_r3d_, cam_preview_rt_, preview_vs_, v, p, [&](RenderTarget &t) {
        render_camera(cam_preview_r3d_, t, preview_ft_, v, p, eye, q.rotate({0, 0, 1}), g, cam, aspect);
      }, g, false, false);  // (nothing picks in the Camera Preview: no full-size depth and ids)
      prof_.view_renders++;
      if (hit && before != cam_preview_img_.pixels) {
        prof_.cache_mismatches++;
        g_cache_mismatches++;
        Log::warn("Render cache: the Camera Preview would have shown a stale picture");
      }
      cam_preview_key_ = key;
      cam_preview_of_ = g->id;
    }
    prof_.ms_preview += rt.ms();
  }
  Recti box{view.right() - w - u.px(12), view.bottom() - h - u.px(12) - u.row_h(), w, h};
  cam_preview_rect_ = {box.x - u.px(4), box.y - u.row_h() - u.px(4), w + u.px(8), h + u.row_h() + u.px(8)};  // clicks here stay off the scene
  u.canvas.fill_rect({box.x - u.px(4), box.y - u.row_h() - u.px(4), w + u.px(8), h + u.row_h() + u.px(8)}, Color::hex(0x222222, 230));
  /* Header: the camera's name, then a Shaded / Rendered switch. */
  const char *mode = cam_preview_rendered_ ? "Rendered" : "Shaded";
  int bw = u.font.text_width("Rendered") + u.px(12), lw = u.font.text_width("Locked") + u.px(12);
  Recti mb{box.right() - bw, box.y - u.row_h() - u.px(2), bw, u.row_h()};
  Recti lb{mb.x - lw - u.px(4), mb.y, lw, mb.h};
  u.label({box.x, box.y - u.row_h(), w - bw - lw - u.px(8), u.row_h()}, g->name + status, u.theme.text);
  if (u.button(lb, cam_preview_lock_ ? "Locked" : "Lock", cam_preview_lock_ != 0)) cam_preview_lock_ = cam_preview_lock_ ? 0 : g->id;
  u.tooltip("Lock: keep this camera's preview up while you select other objects and edit meshes\n"
            "(Edit Mode too), so you can see the shot change as you model. Click again to unlock.");
  if (u.button(mb, mode, cam_preview_rendered_)) {
    cam_preview_rendered_ = !cam_preview_rendered_;
    cam_preview_pt_hash_ = 0;
  }
  u.tooltip("Click to switch. Shaded: the fast rasterized view.\nRendered: a small path-traced render preview through this camera\n"
            "(depth of field, exposure, Preview Samples; denoised when done).");
  Image *fb = u.canvas.target();
  Recti dst = box.intersect(view);
  for (int y = dst.y; y < dst.bottom(); y++)
    std::memcpy(fb->row(y) + dst.x, cam_preview_img_.row(y - box.y) + (dst.x - box.x), sizeof(uint32_t) * dst.w);
  {
    /* Composition guides (task 0033) over the camera's frame in the inset (a filter's letterbox inside it). */
    Recti frame = box;
    const FilterStack &fs = camera_filters(g);
    if (!fs.empty()) {
      int iw, ih;
      Recti d;
      filter_layout(fs, box.w, box.h, iw, ih, d);
      frame = {box.x + d.x, box.y + d.y, d.w, d.h};
    }
    u.canvas.push_clip(view);
    draw_camera_guides(*cam, frame);
    u.canvas.pop_clip();
  }
  u.canvas.rect_outline(box, Color::hex(0x101010));
}

/* ---------------------------------------------------------- Camera sequence */

std::vector<GameObject *> Editor::sequence_cameras() {
  std::vector<std::pair<GameObject *, int>> list;
  int order = 0;
  scene_->for_each_ordered([&](GameObject &g, int) {
    Camera *c = g.get<Camera>();
    if (c && c->enabled && c->in_sequence && g.active_in_hierarchy()) list.push_back({&g, order});
    order++;
  });
  std::stable_sort(list.begin(), list.end(), [](const std::pair<GameObject *, int> &a, const std::pair<GameObject *, int> &b) {
    return a.first->get<Camera>()->sequence_order < b.first->get<Camera>()->sequence_order;
  });
  std::vector<GameObject *> out;
  for (auto &p : list) out.push_back(p.first);
  return out;
}

/* The finished render in the scene's File Format, without asking (PNG when a float
 * format has no float pixels, e.g. from the rasterizer). */
bool Editor::write_render_file(const std::string &base, std::string *written) {
  const int w = render_img_.width, h = render_img_.height;
  if (!w || !h) return false;
  int fmt = scene_->render.file_format;
  if (fmt == 3 && !exr_available()) fmt = 2;
  if (fmt >= 2 && render_linear_.empty()) fmt = 0;
  static const char *exts[] = {".png", ".jpg", ".hdr", ".exr"};
  const std::string path = base + exts[fmt];
  const std::vector<uint32_t> jpg = render_transparent_ && fmt == 1 ? over_black(render_img_.pixels) : std::vector<uint32_t>();
  const float *alpha = render_transparent_ && render_alpha_lin_.size() == (size_t)w * h ? render_alpha_lin_.data() : nullptr;
  const bool ok = fmt == 0   ? write_png(path, render_img_.pixels.data(), w, h, w, render_transparent_)
                  : fmt == 1 ? write_jpeg(path, jpg.empty() ? render_img_.pixels.data() : jpg.data(), w, h, w, scene_->render.jpeg_quality)
                  : fmt == 2 ? write_hdr(path, render_linear_.data(), w, h)
                             : write_exr(path, render_linear_.data(), w, h, true, alpha);
  if (ok && written) *written = path;
  return ok;
}

bool Editor::start_render_sequence(const std::string &dir_in) {
  std::vector<GameObject *> cams = sequence_cameras();
  if (cams.empty()) {
    Log::warn("Render Camera Sequence: no camera has Render in Sequence on");
    return false;
  }
  stop_render_sequence();
  stop_render_animation();  // (one thing drives the F12 render at a time)
  seq_ = RenderSequence{};
  for (GameObject *g : cams) seq_.cams.push_back(g->id);
  if (dir_in.empty()) {
    std::time_t t = std::time(nullptr);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", std::localtime(&t));
    std::string scene = scene_->name.empty() ? std::string("Scene") : scene_->name;
    for (char &c : scene)
      if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') c = '_';
    seq_.dir = fs::join(fs::join(project_root_, "Renders"), scene + "_sequence_" + buf);
  }
  else seq_.dir = dir_in;
  fs::make_dirs(seq_.dir);
  seq_.active = true;
  dock_open(WindowKind::Render);
  Log::info("Render Camera Sequence: %zu camera(s) into %s", seq_.cams.size(), seq_.dir.c_str());
  step_render_sequence();
  return true;
}

void Editor::stop_render_sequence() {
  if (seq_.active) {
    if (rendering_) rendering_ = false;
    final_pt_.pause();  // (task 0161: the cards stop too)
    Log::info("Render Camera Sequence stopped after %zu of %zu", seq_.written.size(), seq_.cams.size());
  }
  seq_.active = false;
  seq_.started = false;
  render_camera_override_ = 0;
}

/* One camera at a time: start its render, and when it finishes (at once for the
 * rasterizer, after its samples for the path tracer) save it and go to the next. */
void Editor::step_render_sequence() {
  if (!seq_.active) return;
  for (int guard = 0; guard < 64 && seq_.active; guard++) {
    if (seq_.index >= seq_.cams.size()) {
      seq_.active = false;
      render_camera_override_ = 0;
      render_status_ = strprintf("Camera sequence: %zu image(s) in %s", seq_.written.size(), seq_.dir.c_str());
      Log::info("%s", render_status_.c_str());
      invalidate_project_listing();
      return;
    }
    GameObject *g = scene_->find(seq_.cams[seq_.index]);
    if (!g || !g->get<Camera>()) {  // deleted meanwhile: skip it
      seq_.index++;
      seq_.started = false;
      continue;
    }
    if (!seq_.started) {
      render_camera_override_ = g->id;
      start_final_render(false, false);
      seq_.started = true;
    }
    if (rendering_) {
      render_status_ = strprintf("Camera %zu / %zu: %s  |  %s", seq_.index + 1, seq_.cams.size(), g->name.c_str(), render_status_.c_str());
      return;  // the path tracer is still sampling: next frame
    }
    std::string name = g->name;
    for (char &c : name)
      if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') c = '_';
    std::string path;
    if (write_render_file(fs::join(seq_.dir, strprintf("%02zu_%s", seq_.index + 1, name.c_str())), &path)) {
      seq_.written.push_back(path);
      Log::info("Camera %zu / %zu: %s -> %s", seq_.index + 1, seq_.cams.size(), g->name.c_str(), fs::filename(path).c_str());
    }
    else Log::error("Render Camera Sequence: could not write the image for '%s'", g->name.c_str());
    seq_.index++;
    seq_.started = false;
  }
}

}  // namespace bl
