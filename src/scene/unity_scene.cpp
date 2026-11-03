// SPDX-License-Identifier: GPL-2.0-or-later
// Unity prefabs and scenes into Blendity's objects (round 370, task 0347): see unity_scene.h.
#include "unity_scene.h"
#include "unity_sdk_types.h"
#include "unity_material.h"

#include "spring_bones.h"
#include "../core/core.h"
#include "../image/image.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <set>

namespace bl {

const char *unity_builtin_mesh(int64_t file_id) {
  switch (file_id) {
    case 10202: return "Cube";
    case 10206: return "Cylinder";
    case 10207: return "Sphere";
    case 10208: return "Capsule";
    case 10209: return "Plane";
    case 10210: return "Quad";
    default: return nullptr;
  }
}

namespace {

using ObjectMap = std::map<int64_t, GameObject *>;

Vec3 vec3_of(const YamlNode &b, const char *key, Vec3 def) {
  const YamlNode *n = b.get(key);
  return n ? Vec3((float)n->num("x", def.x), (float)n->num("y", def.y), (float)n->num("z", def.z)) : def;
}

Quat unit(Quat q) {
  const float l = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
  return l > 1e-12f && std::isfinite(l) ? Quat(q.x / l, q.y / l, q.z / l, q.w / l) : Quat();
}

Quat quat_of(const YamlNode &b) {
  const YamlNode *n = b.get("m_LocalRotation");
  return n ? unit(Quat((float)n->num("x"), (float)n->num("y"), (float)n->num("z"), (float)n->num("w", 1.0))) : Quat();
}

GameObject *object_at(const ObjectMap &m, int64_t fid) {
  const auto it = m.find(fid);
  return it == m.end() ? nullptr : it->second;
}

bool is_transform(const UnityDoc &d) { return d.class_id == 4 || d.class_id == 224; }  // (Transform, RectTransform)

/* What the builder keeps while it works. */
struct Build {
  ObjectMap by_fid;
  std::vector<std::pair<GameObject *, int64_t>> fathers;  // (each object and its parent's transform fileID; 0: none)
  UnityTargets targets;                                   // (round 395: every document's object and class - what overrides name)
  std::map<int64_t, UnityTargets> instances;              // (a PrefabInstance's fileID: its objects by their source fileIDs)
  std::map<int64_t, GameObject *> instance_roots;         // (...and its root: what a stripped object of a model falls back to)
  std::map<GameObject *, std::vector<MaterialPtr>> model_materials;
  int skinned = 0;
};

void make_objects(const std::vector<UnityDoc> &docs, Scene &scene, UnityImport &ctx, Build &b) {
  for (const UnityDoc &d : docs) {
    if (d.class_id != 1 || d.stripped || b.by_fid.count(d.file_id)) continue;  // (a fileID twice: the first)
    if (++*ctx.total > ctx.max_objects) return ctx.notes.push_back("stopped at " + std::to_string(ctx.max_objects) + " objects");
    GameObject *g = scene.create(d.body.str("m_Name", "GameObject"), nullptr);
    g->active = d.body.num("m_IsActive", 1.0) != 0.0;
    if (!ctx.source.empty()) {  // (round 372: where it came from, for the way back)
      UnityOrigin *o = g->add<UnityOrigin>();
      o->file = ctx.source, o->game_object = std::to_string(d.file_id), o->nested = ctx.depth > 0;
    }
    b.by_fid[d.file_id] = g;
    b.targets[d.file_id] = {g, 1};
    ctx.objects++;
  }
}

void place_objects(const std::vector<UnityDoc> &docs, Build &b) {
  for (const UnityDoc &d : docs) {
    GameObject *g = is_transform(d) && !d.stripped ? object_at(b.by_fid, d.body.file_id("m_GameObject")) : nullptr;
    if (!g) continue;
    b.by_fid[d.file_id] = g;
    b.targets[d.file_id] = {g, d.class_id};
    Transform t;
    t.position = vec3_of(d.body, "m_LocalPosition", Vec3(0.0f));
    t.rotation = quat_of(d.body);
    t.scale = vec3_of(d.body, "m_LocalScale", Vec3(1.0f));
    t.euler_hint = t.rotation.to_euler();
    g->set_local(t);
    if (UnityOrigin *o = g->get<UnityOrigin>()) o->transform = std::to_string(d.file_id);
    b.fathers.push_back({g, d.body.file_id("m_Father")});
  }
}

bool is_renderer(int class_id) { return class_id == 23 || class_id == 137; }

/* A renderer's material slot (m_Materials.Array.data[n]) or slot count (.size): the .mat converted, or the slot
 * emptied (Unity's default material). False when the material isn't in the project: left to Unity. */
bool material_override(const YamlNode &e, GameObject *g, UnityImport &ctx) {
  MeshRenderer *r = g->get<MeshRenderer>();
  const std::string prop = e.str("propertyPath"), value = e.str("value");
  if (!r) return false;
  if (prop == "m_Materials.Array.size") {
    const long n = std::atol(value.c_str());
    if (n < 0 || n > 256) return false;
    return r->materials.resize((size_t)n), true;
  }
  const size_t open = prop.find('[');
  const long slot = open == std::string::npos ? -1 : std::atol(prop.c_str() + open + 1);
  if (prop.rfind("m_Materials.Array.data[", 0) != 0 || slot < 0 || slot > 255) return false;
  const std::string guid = e.guid("objectReference");
  MaterialPtr m = guid.empty() || !ctx.material ? nullptr : ctx.material(guid);
  if (!m && !guid.empty()) return false;
  if (r->materials.size() <= (size_t)slot) r->materials.resize((size_t)slot + 1);
  r->materials[(size_t)slot] = m;
  ctx.materials += m != nullptr;
  return true;
}

/* A renderer's switches: on or off, its shadows, its lightmap scale. */
bool renderer_override(const std::string &prop, double v, GameObject *g) {
  MeshRenderer *r = g->get<MeshRenderer>();
  if (!r) return false;
  if (prop == "m_Enabled") return r->enabled = v != 0.0, true;
  if (prop == "m_CastShadows") return r->cast_shadows = v != 0.0, true;  // (Off 0; On, Two Sided, Shadows Only cast)
  if (prop == "m_ReceiveShadows") return r->receive_shadows = v != 0.0, true;
  if (prop == "m_ScaleInLightmap") return std::isfinite(v) && v >= 0.0 && (r->scale_in_lightmap = (float)v, true);
  return false;
}

bool light_override(const std::string &prop, double v, GameObject *g) {
  Light *l = g->get<Light>();
  if (!l || !std::isfinite(v)) return false;
  if (prop == "m_Enabled") return l->enabled = v != 0.0, true;
  if (prop == "m_Color.r" || prop == "m_Color.g" || prop == "m_Color.b") return l->color[prop.back() == 'r' ? 0 : prop.back() == 'g' ? 1 : 2] = srgb_to_linear((float)std::max(0.0, v)), true;  // (round 401: gamma in the file)
  if (prop == "m_Shadows.m_Type") return l->shadows = v >= 0.0 && v <= 2.0 ? (int)v : 2, true;  // (round 401)
  if (prop == "m_Shadows.m_Strength") return l->shadow_strength = (float)std::max(0.0, std::min(1.0, v)), true;
  if (prop == "m_BounceIntensity") return l->bounce_intensity = (float)std::max(0.0, v), true;
  if (prop == "m_Color.a") return true;  // (a light's alpha does nothing in Unity either)
  if (prop == "m_Intensity") return l->intensity = (float)v, true;
  if (prop == "m_Range") return l->range = (float)v, true;
  if (prop == "m_SpotAngle") return l->spot_angle = (float)v, true;
  if (prop == "m_InnerSpotAngle") return l->inner_spot_angle = (float)v, true;
  if (prop == "m_Lightmapping") return l->mode = v == 2.0 ? 2 : v == 1.0 ? 1 : 0, true;
  return false;
}

/* Contribute GI (StaticEditorFlags' first bit) is a renderer's in Blendity. */
void set_static_flags(GameObject *g, double flags) {
  if (MeshRenderer *r = g->get<MeshRenderer>()) r->contribute_gi = ((long long)flags & 1) != 0;
}

/* A new name for an object; when it holds an armature, the skins of its placement that named it by the old one follow
 * (review 399: skins find their armature by name - a renamed placement had left its meshes unbound). */
void rename_object(GameObject *g, const std::string &name) {
  const std::string was = g->name;
  g->name = name;
  if (!g->get<Armature>() || was == name) return;
  GameObject *top = g;
  while (top->parent) top = top->parent;
  std::vector<GameObject *> under{top};
  while (!under.empty()) {
    GameObject *o = under.back();
    under.pop_back();
    if (SkinModifier *s = o->get<SkinModifier>(); s && s->object == was) s->object = name;
    for (GameObject *k : o->children) under.push_back(k);
  }
}

/* One override of a nested prefab or a model: its transform, name, active state, a renderer's materials and switches,
 * a light's; false for what Blendity doesn't hold. */
bool apply_override(const YamlNode &e, const UnityTargets &m, std::set<GameObject *> &turned, UnityImport &ctx) {
  const auto at = m.find(e.file_id("target"));
  const std::string prop = e.str("propertyPath"), value = e.str("value");
  if (at == m.end() || !at->second.object) return false;
  GameObject *g = at->second.object;
  const int cls = at->second.class_id;
  const double v = std::atof(value.c_str());
  if (prop.rfind("m_Materials.Array.", 0) == 0) return (cls == 0 || is_renderer(cls)) && material_override(e, g, ctx);
  if (is_renderer(cls)) return renderer_override(prop, v, g);
  if (cls == 108) return light_override(prop, v, g);
  if (cls != 0 && cls != 1 && cls != 4 && cls != 224) return false;  // (another component's field: Unity's)
  if (prop == "m_Name") return rename_object(g, value), true;
  if (prop == "m_IsActive") return g->active = v != 0.0, true;
  if (prop == "m_StaticEditorFlags") return set_static_flags(g, v), true;
  if (prop == "m_RootOrder" || prop.rfind("m_LocalEulerAnglesHint.", 0) == 0) return true;  // (Unity's bookkeeping: the rotation itself says it)
  const size_t dot = prop.rfind('.');
  if (dot == std::string::npos || dot + 2 != prop.size()) return false;
  const std::string field = prop.substr(0, dot);
  const char axis = prop.back();
  const float f = (float)v;
  Transform t = g->local();
  float *slot = nullptr;
  if (field == "m_LocalPosition" || field == "m_LocalScale") {
    Vec3 &vec = field == "m_LocalPosition" ? t.position : t.scale;
    slot = axis == 'x' ? &vec.x : axis == 'y' ? &vec.y : axis == 'z' ? &vec.z : nullptr;
  }
  if (field == "m_LocalRotation") slot = axis == 'x' ? &t.rotation.x : axis == 'y' ? &t.rotation.y : axis == 'z' ? &t.rotation.z : axis == 'w' ? &t.rotation.w : nullptr;
  if (!slot || !std::isfinite(f)) return false;
  *slot = f;
  if (field == "m_LocalRotation") turned.insert(g);
  g->set_local(t);
  return true;
}

void apply_overrides(const YamlNode &instance, const UnityTargets &m, UnityImport &ctx) {
  const YamlNode *mod = instance.get("m_Modification");
  const YamlNode *list = mod ? mod->get("m_Modifications") : nullptr;
  const YamlNode *removed = mod ? mod->get("m_RemovedComponents") : nullptr;
  /* Round 399: a removed renderer draws nothing here either (switched off: SideRail's bar counter collider mesh had
   * shown); any other removed component comes back, said - Unity removes it again. */
  for (const YamlNode &ref : removed ? removed->seq : std::vector<YamlNode>()) {
    const auto it = m.find(std::strtoll(ref.str("fileID").c_str(), nullptr, 10));
    MeshRenderer *r = it != m.end() && it->second.object && is_renderer(it->second.class_id) ? it->second.object->get<MeshRenderer>() : nullptr;
    if (r) it->second.object->remove_component(r);  // (review 399: gone, as in Unity - not drawn, not in the Budget)
    else ctx.notes.push_back("a nested prefab's removed components came back (Unity removes them again)");
  }
  std::set<GameObject *> turned;
  for (const YamlNode &e : list ? list->seq : std::vector<YamlNode>()) {
    const bool done = apply_override(e, m, turned, ctx);
    ctx.overrides_applied += done, ctx.overrides_kept += !done;
  }
  for (GameObject *g : turned) {  // (a rotation's parts set one by one: made whole once all are in)
    Transform t = g->local();
    t.rotation = unit(t.rotation);
    t.euler_hint = t.rotation.to_euler();
    g->set_local(t);
  }
}

/* Properties only a renderer has: an override of one on a model with a single renderer can only be that renderer's. */
bool renderer_only(const std::string &prop) {
  return prop.rfind("m_Materials.Array.", 0) == 0 || prop == "m_CastShadows" || prop == "m_ReceiveShadows" || prop == "m_ScaleInLightmap" ||
         prop == "m_ProbeAnchor" || prop == "m_LightProbeUsage" || prop == "m_ReflectionProbeUsage" || prop == "m_LightmapParameters";
}

/* What apply_override can hold (review 395: an override of anything else waits for nothing - Unity's numbers wouldn't
 * bring it in). */
bool holdable(const std::string &prop) {
  static const char *const kFields[] = {"m_Name", "m_IsActive", "m_StaticEditorFlags", "m_Enabled", "m_CastShadows", "m_ReceiveShadows", "m_ScaleInLightmap",
                                        "m_Intensity", "m_Range", "m_SpotAngle", "m_InnerSpotAngle", "m_Lightmapping"};
  for (const char *f : kFields)
    if (prop == f) return true;
  for (const char *p : {"m_Materials.Array.", "m_LocalPosition.", "m_LocalRotation.", "m_LocalScale.", "m_Color."})
    if (prop.rfind(p, 0) == 0) return true;
  return false;
}

/* The model's one renderer, or nullptr (none, or several). */
GameObject *lone_renderer(GameObject *root) {
  GameObject *found = nullptr;
  int n = 0;
  std::function<void(GameObject *)> walk = [&](GameObject *g) {
    if (g->get<MeshRenderer>()) found = g, n++;
    for (GameObject *c : g->children) walk(c);
  };
  walk(root);
  return n == 1 ? found : nullptr;
}

/* A model placed as a prefab: its nodes as objects under one root, its overrides applied to the parts whose fileIDs
 * are known (round 395; the rest wait for Unity's numbers, counted); stripped objects and added components fall to the
 * part they name, else the root. */
void instantiate_model(const UnityDoc &d, const std::string &guid, GameObject *root, UnityTargets &parts, UnityImport &ctx, Build &b) {
  ctx.prefabs++;
  const YamlNode *mod = d.body.get("m_Modification");
  b.fathers.push_back({root, mod ? mod->file_id("m_TransformParent") : 0});
  b.instance_roots[d.file_id] = root;
  const YamlNode *list = mod ? mod->get("m_Modifications") : nullptr;
  GameObject *lone = lone_renderer(root);
  for (const YamlNode &e : list ? list->seq : std::vector<YamlNode>()) {
    const int64_t fid = e.file_id("target");
    const bool own = e.guid("target") == guid;
    if (lone && own && !parts.count(fid) && renderer_only(e.str("propertyPath"))) parts[fid] = {lone, 23};
    if (parts.count(fid) || !own || !holdable(e.str("propertyPath"))) continue;
    if (ctx.models_numbered->count(guid)) {  // (round 399: Unity numbered it and has no such part - an override Unity keeps unused)
      ctx.model_overrides_stale++;
      continue;
    }
    ctx.model_overrides_waiting++;
    ctx.models_waiting.insert(guid);
  }
  apply_overrides(d.body, parts, ctx);
  b.instances[d.file_id] = parts;
}

/* A nested prefab: its own objects made (by its own fileIDs), its overrides applied, its roots placed under the
 * transform the instance names. */
void instantiate(const UnityDoc &d, Scene &scene, UnityImport &ctx, Build &b) {
  const std::string guid = d.body.guid("m_SourcePrefab");
  std::vector<UnityDoc> docs;
  if (ctx.depth >= 8) return ctx.notes.push_back("a prefab nested more than 8 deep was left out");
  if (ctx.building->count(guid)) return ctx.notes.push_back("a prefab that holds itself (GUID " + guid + ") was left out at its second appearance");
  if (*ctx.total > ctx.max_objects) return;
  const bool yaml = ctx.prefab && ctx.prefab(guid, docs);
  UnityTargets parts;
  GameObject *model = !yaml && ctx.model ? ctx.model(guid, scene, parts) : nullptr;
  if (model) return instantiate_model(d, guid, model, parts, ctx, b);
  if (docs.empty()) return ctx.notes.push_back("a nested prefab (GUID " + guid + ") isn't in the project, or isn't one Blendity reads");
  UnityImport inner;
  inner.mesh = ctx.mesh, inner.script_name = ctx.script_name, inner.prefab = ctx.prefab, inner.model = ctx.model, inner.depth = ctx.depth + 1;
  inner.material = ctx.material;  // (round 395: a nested prefab's .mat files had come in as its model's own)
  inner.models_numbered = ctx.models_numbered;
  inner.builtin_default = ctx.builtin_default;
  inner.physboned = ctx.physboned;
  inner.light_volume_textures = ctx.light_volume_textures;
  inner.building = ctx.building, inner.total = ctx.total, inner.max_objects = ctx.max_objects;
  inner.path_of = ctx.path_of, inner.source = ctx.path_of ? ctx.path_of(guid) : std::string();
  ctx.building->insert(guid);
  UnityTargets m;
  build_unity_objects(docs, scene, nullptr, inner, &m);
  ctx.building->erase(guid);
  ctx.dropped += inner.dropped;
  ctx.objects += inner.objects, ctx.meshes += inner.meshes, ctx.missing_meshes += inner.missing_meshes, ctx.lights += inner.lights;
  ctx.cameras += inner.cameras, ctx.colliders += inner.colliders, ctx.engine += inner.engine, ctx.prefabs += inner.prefabs + 1;
  ctx.overrides_kept += inner.overrides_kept, ctx.overrides_applied += inner.overrides_applied;
  ctx.materials += inner.materials, ctx.outlines += inner.outlines, ctx.springs += inner.springs, ctx.light_volumes += inner.light_volumes;
  ctx.model_overrides_waiting += inner.model_overrides_waiting, ctx.model_overrides_stale += inner.model_overrides_stale;
  ctx.models_waiting.insert(inner.models_waiting.begin(), inner.models_waiting.end());
  ctx.notes.insert(ctx.notes.end(), inner.notes.begin(), inner.notes.end());
  apply_overrides(d.body, m, ctx);
  const YamlNode *mod = d.body.get("m_Modification");
  const int64_t parent = mod ? mod->file_id("m_TransformParent") : 0;
  std::set<GameObject *> roots;
  for (const auto &[fid, t] : m)
    if (t.object && !t.object->parent) roots.insert(t.object);
  for (GameObject *g : roots) b.fathers.push_back({g, parent});
  b.instances[d.file_id] = std::move(m);
}

/* A stripped document stands for an object of a nested prefab: it maps to that object. */
void map_stripped(const std::vector<UnityDoc> &docs, Build &b) {
  for (const UnityDoc &d : docs) {
    if (!d.stripped) continue;
    const int64_t pi = d.body.file_id("m_PrefabInstance");
    const auto inst = b.instances.find(pi);
    const auto src = inst == b.instances.end() ? UnityTargets::const_iterator() : inst->second.find(d.body.file_id("m_CorrespondingSourceObject"));
    GameObject *g = inst != b.instances.end() && src != inst->second.end() ? src->second.object : nullptr;
    if (!g) g = object_at(b.instance_roots, pi);  // (a model's: its root takes what was added to it)
    if (!g) continue;
    b.by_fid[d.file_id] = g;
    b.targets[d.file_id] = {g, d.class_id};
  }
}

void parent_objects(Scene &scene, GameObject *root, Build &b) {
  for (const auto &[g, father] : b.fathers) {
    GameObject *p = father ? object_at(b.by_fid, father) : nullptr;
    if (p == g || (p && scene.is_ancestor(g, p))) p = nullptr;  // (a parent loop: under the root instead)
    scene.set_parent(g, p ? p : root, -1, false);
  }
}

/* Children in Unity's order (m_Children; review 370: they followed the file's order). */
void order_children(const std::vector<UnityDoc> &docs, Scene &scene, Build &b) {
  for (const UnityDoc &d : docs) {
    GameObject *p = is_transform(d) && !d.stripped ? object_at(b.by_fid, d.file_id) : nullptr;
    const YamlNode *kids = p ? d.body.get("m_Children") : nullptr;
    int at = 0;
    for (const YamlNode &k : kids ? kids->seq : std::vector<YamlNode>()) {
      GameObject *c = object_at(b.by_fid, std::strtoll(k.str("fileID", "0").c_str(), nullptr, 10));
      if (c && c->parent == p) scene.set_parent(c, p, at++, false);
    }
  }
}

/* --- components --------------------------------------------------------------------------------------------- */

void add_mesh(const UnityDoc &d, GameObject *g, UnityImport &ctx, Build &b) {
  const std::string guid = d.body.guid("m_Mesh");
  const int64_t fid = d.body.file_id("m_Mesh");
  if (!fid) return;  // (no mesh assigned in Unity either)
  std::vector<MaterialPtr> mats;
  MeshPtr mesh = ctx.mesh ? ctx.mesh(guid, fid, g->name, mats) : nullptr;
  if (!mesh) {
    ctx.missing_meshes++;
    return ctx.notes.push_back(g->name + "'s mesh (GUID " + guid + ", fileID " + std::to_string(fid) + ") isn't in the project");
  }
  g->add<MeshFilter>()->mesh = mesh;
  b.model_materials[g] = mats;
  ctx.meshes++;
}

/* The renderer's slots: each Unity material converted (round 371), else the model's own for that slot. */
std::vector<MaterialPtr> renderer_materials(const UnityDoc &d, GameObject *g, UnityImport &ctx, Build &b) {
  const auto own = b.model_materials.find(g);
  const std::vector<MaterialPtr> model = own == b.model_materials.end() ? std::vector<MaterialPtr>() : own->second;
  const YamlNode *list = d.body.get("m_Materials");
  std::vector<MaterialPtr> out;
  for (const YamlNode &ref : list ? list->seq : std::vector<YamlNode>()) {
    MaterialPtr m = ctx.material && !ref.str("guid").empty() ? ctx.material(ref.str("guid")) : nullptr;
    ctx.materials += m != nullptr;
    /* Round 399: Unity's built-in Default-Material (a white Standard) - a slot, as Unity's renderer has (SideRail's
     * mirror triggers had come in with none). */
    if (!m && ref.str("guid") == "0000000000000000f000000000000000" && ref.str("fileID") == "10303") {
      if (!*ctx.builtin_default) *ctx.builtin_default = make_material("Default-Material", Vec3(1.0f));
      m = *ctx.builtin_default;
    }
    out.push_back(m ? m : out.size() < model.size() ? model[out.size()] : nullptr);  // (a slot left empty stays empty: the default material - review 371)
  }
  const bool any = std::any_of(out.begin(), out.end(), [](const MaterialPtr &m) { return m != nullptr; });
  return any ? out : model;
}

/* A toon material's outline on the object that wears it (Blendity's outline is the object's). */
void add_outline(GameObject *g, const std::vector<MaterialPtr> &mats, UnityImport &ctx) {
  const MaterialPtr *line = nullptr;
  int outlined = 0;
  for (const MaterialPtr &m : mats) {
    line = !line && m && m->import_outline > 0.0f ? &m : line;
    outlined += m && m->import_outline > 0.0f;
  }
  if (!line || g->get<Outline>()) return;
  if (outlined < (int)mats.size())  // (Blendity's outline is the object's: one width for all its slots)
    ctx.notes.push_back(g->name + ": an outline from " + std::to_string(outlined) + " of its " + std::to_string(mats.size()) + " materials now outlines all of it");
  Outline *o = g->add<Outline>();
  o->thickness = (*line)->import_outline, o->color = (*line)->import_outline_color, o->world_space = true;
  ctx.outlines++;
}

void add_renderer(const UnityDoc &d, GameObject *g, UnityImport &ctx, Build &b) {
  if (!g->get<MeshFilter>()) return;
  MeshRenderer *r = g->get<MeshRenderer>() ? g->get<MeshRenderer>() : g->add<MeshRenderer>();
  r->enabled = d.body.num("m_Enabled", 1.0) != 0.0;
  r->cast_shadows = d.body.num("m_CastShadows", 1.0) != 0.0;  // (round 395)
  r->receive_shadows = d.body.num("m_ReceiveShadows", 1.0) != 0.0;
  r->scale_in_lightmap = (float)std::max(0.0, d.body.num("m_ScaleInLightmap", 1.0));
  if (d.class_id == 137 && d.body.get("m_Bones") && !d.body.get("m_Bones")->seq.empty()) b.skinned++;
  const std::vector<MaterialPtr> mats = renderer_materials(d, g, ctx, b);
  if (!mats.empty()) r->materials = mats;
  add_outline(g, mats, ctx);
  /* Round 402: a skinned renderer's blend shape weights (Unity's 0..100 by index; Blendity's 0..1 by name) - the avatar's
   * default expression. */
  const YamlNode *bw = d.class_id == 137 ? d.body.get("m_BlendShapeWeights") : nullptr;
  const MeshPtr mesh = g->get<MeshFilter>()->mesh;
  if (bw && bw->kind == YamlNode::Seq && mesh)
    for (size_t i = 0; i < bw->seq.size() && i < mesh->shape_keys.size(); i++) {
      const float w = (float)std::atof(bw->seq[i].value.c_str()) / 100.0f;
      if (!(std::fabs(w) > 1e-6f)) continue;
      const std::string &name = mesh->shape_keys[i].name;
      auto it = std::find_if(r->shape_weights.begin(), r->shape_weights.end(), [&](const auto &p) { return p.first == name; });
      if (it != r->shape_weights.end()) it->second = w;
      else r->shape_weights.push_back({name, w});
    }
}

/* Round 402 (ADR 0100): VRChat's avatar components as Blendity's, every field from the document; the references (transform
 * and renderer fileIDs) kept to be resolved once every object is made (resolve_avatar_refs), the assets by GUID to their
 * paths. Each stays an Engine Component too (add_engine: the curves and the fields not modelled). */
Quat yaml_quat(const YamlNode *q) { return q ? Quat((float)q->num("x"), (float)q->num("y"), (float)q->num("z"), (float)q->num("w", 1.0)) : Quat(); }
Vec3 yaml_vec3(const YamlNode *v, Vec3 def = Vec3(0.0f)) { return v ? Vec3((float)v->num("x", def.x), (float)v->num("y", def.y), (float)v->num("z", def.z)) : def; }
std::string asset_path_of(const UnityImport &ctx, const YamlNode &b, const char *key) {
  const std::string guid = b.guid(key);
  return guid.empty() || !ctx.path_of ? std::string() : ctx.path_of(guid);
}

void add_avatar_descriptor(const UnityDoc &d, GameObject *g, UnityImport &ctx) {
  if (g->get<AvatarDescriptorComponent>()) return;
  AvatarDescriptorComponent *a = g->add<AvatarDescriptorComponent>();
  const YamlNode &b = d.body;
  a->unity_id = std::to_string(d.file_id);
  a->enabled = b.num("m_Enabled", 1.0) != 0.0;
  a->view_position = yaml_vec3(b.get("ViewPosition"), Vec3(0.0f, 1.6f, 0.2f));  // (Unity's axes, as every transform here)
  a->lip_sync = std::max(0, std::min(4, (int)b.num("lipSync")));
  a->jaw_fid = b.file_id("lipSyncJawBone");
  a->jaw_closed = yaml_quat(b.get("lipSyncJawClosed")), a->jaw_open = yaml_quat(b.get("lipSyncJawOpen"));
  a->viseme_mesh_fid = b.file_id("VisemeSkinnedMesh");
  a->mouth_open_shape = b.str("MouthOpenBlendShapeName");
  a->jaw_flap_shape = b.str("lipSyncJawBlendShapeName");  // (as the SDK names it)
  if (const YamlNode *v = b.get("VisemeBlendShapes"))
    for (size_t k = 0; k < 15 && k < v->seq.size(); k++) a->visemes[k] = v->seq[k].value;
  a->eye_look = b.num("enableEyeLook") != 0.0;
  if (const YamlNode *e = b.get("customEyeLookSettings")) {
    if (const YamlNode *m = e->get("eyeMovement")) a->eye_confidence = (float)m->num("confidence", 0.5), a->eye_excitement = (float)m->num("excitement", 0.5);
    a->left_eye_fid = e->file_id("leftEye"), a->right_eye_fid = e->file_id("rightEye");
    static const char *looks[5] = {"eyesLookingStraight", "eyesLookingUp", "eyesLookingDown", "eyesLookingLeft", "eyesLookingRight"};
    for (int k = 0; k < 5; k++)
      if (const YamlNode *l = e->get(looks[k])) a->look_linked[k] = l->num("linked", 1.0) != 0.0, a->look_left[k] = yaml_quat(l->get("left")), a->look_right[k] = yaml_quat(l->get("right"));
    a->eyelid_type = std::max(0, std::min(2, (int)e->num("eyelidType")));
    static const char *lids[4] = {"upperLeftEyelid", "upperRightEyelid", "lowerLeftEyelid", "lowerRightEyelid"};
    for (int k = 0; k < 4; k++) a->eyelid_bone_fids[k] = e->file_id(lids[k]);
    a->eyelids_mesh_fid = e->file_id("eyelidsSkinnedMesh");
    const std::string hex = e->str("eyelidsBlendshapes");  // (an int array as Unity writes it: 8 hex digits each, little-endian)
    for (int k = 0; k < 3 && (size_t)(k + 1) * 8 <= hex.size(); k++) {
      uint32_t v = 0;
      for (int byte = 3; byte >= 0; byte--) v = v << 8 | (uint32_t)std::strtoul(hex.substr((size_t)k * 8 + (size_t)byte * 2, 2).c_str(), nullptr, 16);
      a->eyelid_shapes[k] = (int32_t)v;
    }
  }
  a->customize_layers = b.num("customizeAnimationLayers") != 0.0;
  auto layers = [&](const char *key, AvatarDescriptorComponent::Layer *out, size_t count) {
    const YamlNode *l = b.get(key);
    for (size_t k = 0; l && k < count && k < l->seq.size(); k++) {
      const YamlNode &e = l->seq[k];
      out[k].enabled = e.num("isEnabled") != 0.0, out[k].type = (int)e.num("type"), out[k].is_default = e.num("isDefault", 1.0) != 0.0;
      out[k].controller = asset_path_of(ctx, e, "animatorController"), out[k].mask = asset_path_of(ctx, e, "mask");
    }
  };
  layers("baseAnimationLayers", a->base_layers, 5);
  layers("specialAnimationLayers", a->special_layers, 3);
  a->custom_expressions = b.num("customExpressions") != 0.0;
  a->expressions_menu = asset_path_of(ctx, b, "expressionsMenu"), a->expression_parameters = asset_path_of(ctx, b, "expressionParameters");
  a->portrait_offset = yaml_vec3(b.get("portraitCameraPositionOffset")), a->portrait_rotation = yaml_quat(b.get("portraitCameraRotationOffset"));
  a->unity_version = b.str("unityVersion");
}

std::vector<int64_t> yaml_fids(const YamlNode *list) {
  std::vector<int64_t> out;
  for (const YamlNode &e : list ? list->seq : std::vector<YamlNode>())
    if (const YamlNode *f = e.get("fileID")) out.push_back((int64_t)std::strtoll(f->value.c_str(), nullptr, 10));
  return out;
}

void add_physbone_component(const UnityDoc &d, GameObject *g) {
  PhysBoneComponent *p = g->add<PhysBoneComponent>();
  const YamlNode &b = d.body;
  p->unity_id = std::to_string(d.file_id);
  p->enabled = b.num("m_Enabled", 1.0) != 0.0;
  p->version = (int)b.num("version", 1.0), p->integration_type = (int)b.num("integrationType");
  p->root_fid = b.file_id("rootTransform"), p->ignore_fids = yaml_fids(b.get("ignoreTransforms"));
  p->endpoint_position = yaml_vec3(b.get("endpointPosition"));
  p->multi_child_type = (int)b.num("multiChildType");
  p->pull = (float)b.num("pull", 0.2), p->spring = (float)b.num("spring", 0.2), p->stiffness = (float)b.num("stiffness", 0.2);
  p->gravity = (float)b.num("gravity"), p->gravity_falloff = (float)b.num("gravityFalloff"), p->immobile = (float)b.num("immobile"), p->immobile_type = (int)b.num("immobileType");
  p->allow_collision = (int)b.num("allowCollision", 1.0), p->radius = (float)b.num("radius");
  p->collider_fids = yaml_fids(b.get("colliders"));
  p->limit_type = (int)b.num("limitType"), p->max_angle_x = (float)b.num("maxAngleX", 45.0), p->max_angle_z = (float)b.num("maxAngleZ", 45.0);
  p->limit_rotation = yaml_vec3(b.get("limitRotation"));
  p->allow_grabbing = (int)b.num("allowGrabbing", 1.0), p->allow_posing = (int)b.num("allowPosing", 1.0);
  p->grab_movement = (float)b.num("grabMovement", 0.5), p->max_stretch = (float)b.num("maxStretch"), p->max_squish = (float)b.num("maxSquish"), p->stretch_motion = (float)b.num("stretchMotion");
  p->snap_to_hand = b.num("snapToHand") != 0.0, p->is_animated = b.num("isAnimated") != 0.0, p->reset_when_disabled = b.num("resetWhenDisabled") != 0.0;
  p->parameter = b.str("parameter"), p->show_gizmos = b.num("showGizmos", 1.0) != 0.0;
  p->bone_opacity = (float)b.num("boneOpacity", 0.5), p->limit_opacity = (float)b.num("limitOpacity", 0.5);
}

void add_physbone_collider(const UnityDoc &d, GameObject *g) {
  PhysBoneColliderComponent *c = g->add<PhysBoneColliderComponent>();
  const YamlNode &b = d.body;
  c->unity_id = std::to_string(d.file_id);
  c->enabled = b.num("m_Enabled", 1.0) != 0.0;
  c->root_fid = b.file_id("rootTransform");
  c->shape_type = (int)b.num("shapeType"), c->inside_bounds = b.num("insideBounds") != 0.0, c->bones_as_spheres = b.num("bonesAsSpheres") != 0.0;
  c->radius = (float)b.num("radius", 0.1), c->height = (float)b.num("height");
  c->position = yaml_vec3(b.get("position")), c->rotation = yaml_quat(b.get("rotation"));
}

void add_contact(const UnityDoc &d, GameObject *g, bool receiver) {
  ContactComponent *c = g->add<ContactComponent>();
  const YamlNode &b = d.body;
  c->unity_id = std::to_string(d.file_id);
  c->enabled = b.num("m_Enabled", 1.0) != 0.0;
  c->receiver = receiver;
  c->root_fid = b.file_id("rootTransform");
  c->shape_type = (int)b.num("shapeType"), c->radius = (float)b.num("radius", 0.1), c->height = (float)b.num("height");
  c->position = yaml_vec3(b.get("position")), c->rotation = yaml_quat(b.get("rotation"));
  std::string tags;
  for (const YamlNode &t : b.get("collisionTags") ? b.get("collisionTags")->seq : std::vector<YamlNode>()) tags += (tags.empty() ? "" : ";") + t.value;
  c->tags = tags;
  c->allow_self = b.num("allowSelf", 1.0) != 0.0, c->allow_others = b.num("allowOthers", 1.0) != 0.0, c->local_only = b.num("localOnly") != 0.0;
  c->receiver_type = (int)b.num("receiverType"), c->parameter = b.str("parameter"), c->value = (float)b.num("value", 1.0), c->min_velocity = (float)b.num("minVelocity");
}

void add_vrc_constraint(const UnityDoc &d, GameObject *g, const std::string &type) {
  VRCConstraintComponent *c = g->add<VRCConstraintComponent>();
  const YamlNode &b = d.body;
  c->unity_id = std::to_string(d.file_id);
  c->enabled = b.num("m_Enabled", 1.0) != 0.0;
  c->kind = type == "VRCPositionConstraint" ? 1 : type == "VRCRotationConstraint" ? 2 : type == "VRCScaleConstraint" ? 3 : type == "VRCLookAtConstraint" ? 4 : type == "VRCAimConstraint" ? 5 : 0;
  c->is_active = b.num("IsActive", 1.0) != 0.0, c->global_weight = (float)b.num("GlobalWeight", 1.0);
  c->target_fid = b.file_id("TargetTransform");
  c->solve_in_local_space = b.num("SolveInLocalSpace") != 0.0, c->freeze_to_world = b.num("FreezeToWorld") != 0.0;
  c->rebake_offsets_when_unfrozen = b.num("RebakeOffsetsWhenUnfrozen") != 0.0, c->locked = b.num("Locked") != 0.0;
  if (const YamlNode *s = b.get("Sources"))
    for (const auto &[key, src] : s->map) {  // (source0 .. source15; the ones with a transform count)
      if (key.rfind("source", 0) != 0 || src.file_id("SourceTransform") == 0) continue;
      VRCConstraintComponent::Source out;
      out.fid = src.file_id("SourceTransform"), out.weight = (float)src.num("Weight", 1.0);
      out.position_offset = yaml_vec3(src.get("ParentPositionOffset")), out.rotation_offset = yaml_vec3(src.get("ParentRotationOffset"));
      c->sources.push_back(out);
    }
  c->position_offset = yaml_vec3(b.get("PositionOffset")), c->position_at_rest = yaml_vec3(b.get("PositionAtRest"));
  c->rotation_offset = yaml_vec3(b.get("RotationOffset")), c->rotation_at_rest = yaml_vec3(b.get("RotationAtRest"));
  c->scale_offset = yaml_vec3(b.get("ScaleOffset"), Vec3(1.0f)), c->scale_at_rest = yaml_vec3(b.get("ScaleAtRest"), Vec3(1.0f));
  static const char *axes[6][3] = {{"AffectsPositionX", "AffectsPositionY", "AffectsPositionZ"}, {"AffectsPositionX", "AffectsPositionY", "AffectsPositionZ"},
                                   {"AffectsRotationX", "AffectsRotationY", "AffectsRotationZ"}, {"AffectsScaleX", "AffectsScaleY", "AffectsScaleZ"}, {"", "", ""}, {"", "", ""}};
  static const char *rot_axes[3] = {"AffectsRotationX", "AffectsRotationY", "AffectsRotationZ"};
  for (int k = 0; k < 3; k++) c->affects[k] = axes[c->kind][k][0] ? b.num(axes[c->kind][k], 1.0) != 0.0 : true;
  for (int k = 0; k < 3; k++) c->affects_rotation[k] = c->kind == 0 ? b.num(rot_axes[k], 1.0) != 0.0 : c->affects[k];  // (a Parent constraint's rotation part)
}

/* Round 403: the world's Scene Descriptor and Pipeline Manager, and Unity's LOD Group onto Blendity's. */
void add_scene_descriptor(const UnityDoc &d, GameObject *g) {
  if (g->get<SceneDescriptorComponent>()) return;
  SceneDescriptorComponent *s = g->add<SceneDescriptorComponent>();
  const YamlNode &b = d.body;
  s->unity_id = std::to_string(d.file_id);
  s->enabled = b.num("m_Enabled", 1.0) != 0.0;
  s->spawn_fids = yaml_fids(b.get("spawns"));
  auto whole = [&](const char *key, int lo, int hi) {
    const double v = b.num(key);
    return std::isfinite(v) ? (int)std::max((double)lo, std::min((double)hi, v)) : 0;  // (review 403: a NaN or a huge number never an INT_MIN)
  };
  s->spawn_order = whole("spawnOrder", 0, 3), s->spawn_orientation = whole("spawnOrientation", 0, 2);
  s->camera_fid = b.file_id("ReferenceCamera");
  s->respawn_height = std::isfinite(b.num("RespawnHeightY", -100.0)) ? (float)b.num("RespawnHeightY", -100.0) : -100.0f;
  s->respawn_behaviour = whole("ObjectBehaviourAtRespawnHeight", 0, 1);
  s->forbid_user_portals = b.num("ForbidUserPortals") != 0.0, s->auto_spatialize = b.num("autoSpatializeAudioSources") != 0.0;
  s->interact_thru_layers = whole("interactThruLayers", -2147483647, 2147483647), s->capacity = whole("capacity", 0, 100000);
  s->gravity = yaml_vec3(b.get("gravity"), Vec3(0.0f, -9.81f, 0.0f)), s->draw_distance = std::isfinite(b.num("DrawDistance")) ? (float)b.num("DrawDistance") : 0.0f;
  s->unity_version = b.str("unityVersion");
}

void add_pipeline_manager(const UnityDoc &d, GameObject *g) {
  if (g->get<PipelineManagerComponent>()) return;
  PipelineManagerComponent *p = g->add<PipelineManagerComponent>();
  p->unity_id = std::to_string(d.file_id);
  p->blueprint_id = d.body.str("blueprintId"), p->content_type = d.body.num("contentType") >= 0.5 ? 1 : 0;
}

void add_animator(const UnityDoc &d, GameObject *g, const UnityImport &ctx) {
  if (g->get<AnimatorComponent>()) return;
  AnimatorComponent *a = g->add<AnimatorComponent>();
  const YamlNode &b = d.body;
  a->unity_id = std::to_string(d.file_id);
  a->enabled = b.num("m_Enabled", 1.0) != 0.0;
  a->controller = asset_path_of(ctx, b, "m_Controller"), a->avatar = asset_path_of(ctx, b, "m_Avatar");
  a->apply_root_motion = b.num("m_ApplyRootMotion") != 0.0, a->update_mode = (int)b.num("m_UpdateMode"), a->culling_mode = (int)b.num("m_CullingMode");
}

/* The references of round 402's components, once every object and component of the file is made: transforms by
 * their fileIDs (the objects'), renderers by their documents' (the targets'), as names. */
void resolve_avatar_refs(const Build &b) {
  auto name_of = [&](int64_t fid) {
    const GameObject *g = object_at(b.by_fid, fid);
    if (!g) {
      const auto t = b.targets.find(fid);
      g = t != b.targets.end() ? t->second.object : nullptr;
    }
    return g ? g->name : std::string();
  };
  auto names_of = [&](const std::vector<int64_t> &fids) {
    std::string s;
    for (int64_t f : fids) {
      const std::string n = name_of(f);
      if (!n.empty()) s += (s.empty() ? "" : ";") + n;
    }
    return s;
  };
  for (const auto &[fid, g] : b.by_fid) {
    if (!g) continue;
    for (auto &c : g->components) {
      if (auto *a = dynamic_cast<AvatarDescriptorComponent *>(c.get())) {
        a->jaw_bone = name_of(a->jaw_fid), a->viseme_mesh = name_of(a->viseme_mesh_fid), a->eyelids_mesh = name_of(a->eyelids_mesh_fid);
        a->left_eye = name_of(a->left_eye_fid), a->right_eye = name_of(a->right_eye_fid);
        for (int k = 0; k < 4; k++) a->eyelid_bones[k] = name_of(a->eyelid_bone_fids[k]);
      }
      else if (auto *p = dynamic_cast<PhysBoneComponent *>(c.get())) p->root_transform = name_of(p->root_fid), p->ignore_transforms = names_of(p->ignore_fids), p->colliders = names_of(p->collider_fids);
      else if (auto *pc = dynamic_cast<PhysBoneColliderComponent *>(c.get())) pc->root_transform = name_of(pc->root_fid);
      else if (auto *ct = dynamic_cast<ContactComponent *>(c.get())) ct->root_transform = name_of(ct->root_fid);
      else if (auto *vc = dynamic_cast<VRCConstraintComponent *>(c.get())) {
        vc->target_transform = name_of(vc->target_fid);
        for (auto &s : vc->sources) s.transform = name_of(s.fid);
      }
      else if (auto *sd = dynamic_cast<SceneDescriptorComponent *>(c.get())) sd->spawns = names_of(sd->spawn_fids), sd->reference_camera = name_of(sd->camera_fid);  // (round 403)
    }
  }
}

void add_light(const UnityDoc &d, GameObject *g, const UnityImport &ctx) {
  static const int kType[] = {2, 0, 1, 3, 3};  // (Unity: Spot, Directional, Point, Rectangle, Disc)
  const int t = (int)d.body.num("m_Type", 2.0);
  Light *l = g->add<Light>();
  l->type = kType[t >= 0 && t <= 4 ? t : 2];
  l->area_shape = t == 4 ? 1 : 0;
  const YamlNode *c = d.body.get("m_Color");
  /* Unity writes a light's colour in gamma, whatever the project's colour space; Blendity's is linear (round 401: it had
   * been read as it was - a half-grey light a third too bright). */
  if (c) l->color = Vec3(srgb_to_linear((float)std::max(0.0, c->num("r", 1.0))), srgb_to_linear((float)std::max(0.0, c->num("g", 1.0))), srgb_to_linear((float)std::max(0.0, c->num("b", 1.0))));
  if (const YamlNode *sh = d.body.get("m_Shadows")) {  // (round 401)
    const int st = (int)sh->num("m_Type", 2.0);
    l->shadows = st >= 0 && st <= 2 ? st : 2;
    l->shadow_strength = (float)std::max(0.0, std::min(1.0, sh->num("m_Strength", 1.0)));
  }
  l->bounce_intensity = (float)std::max(0.0, d.body.num("m_BounceIntensity", 1.0));
  l->cookie_size = (float)std::max(0.01, d.body.num("m_CookieSize", 10.0));
  const std::string cookie = d.body.guid("m_Cookie");
  if (!cookie.empty() && d.body.file_id("m_Cookie") && ctx.path_of) l->cookie.path = ctx.path_of(cookie);
  if (const YamlNode *mask = d.body.get("m_CullingMask")) l->culling_mask = (int)(int64_t)mask->num("m_Bits", -1.0);
  l->intensity = (float)d.body.num("m_Intensity", 1.0);
  l->range = (float)d.body.num("m_Range", 10.0);
  l->spot_angle = (float)d.body.num("m_SpotAngle", 30.0);
  l->inner_spot_angle = (float)d.body.num("m_InnerSpotAngle", l->spot_angle * 0.727);  // (review 370: the rest of Unity's light)
  const YamlNode *area = d.body.get("m_AreaSize");
  if (area) l->area_width = (float)area->num("x", 1.0), l->area_height = (float)area->num("y", 1.0);
  l->use_temperature = d.body.num("m_UseColorTemperature", 0.0) != 0.0;
  l->temperature = (float)d.body.num("m_ColorTemperature", 6500.0);
  const int lm = (int)d.body.num("m_Lightmapping", 4.0);  // (Unity: 4 Realtime, 1 Mixed, 2 Baked)
  l->mode = lm == 2 ? 2 : lm == 1 ? 1 : 0;
  l->enabled = d.body.num("m_Enabled", 1.0) != 0.0;
}

void add_camera(const UnityDoc &d, GameObject *g) {
  Camera *c = g->add<Camera>();
  c->fov = (float)d.body.num("field of view", 60.0);
  c->near_clip = (float)d.body.num("near clip plane", 0.3);
  c->far_clip = (float)d.body.num("far clip plane", 1000.0);
  c->orthographic = d.body.num("orthographic", 0.0) != 0.0;
  c->ortho_size = (float)d.body.num("orthographic size", 5.0);
  c->enabled = d.body.num("m_Enabled", 1.0) != 0.0;
  /* Round 401: the rest of Unity's Camera - Clear Flags (1 Skybox, 2 Solid Color, 3 Depth only, 4 Nothing), Background
   * (gamma, as a material's colour), HDR, FOV Axis, and the physical camera (m_projectionMatrixMode 2): focal length,
   * sensor, lens shift, gate fit (0 None, 1 Vertical, 2 Horizontal, 3 Fill, 4 Overscan: Blendity's Auto / Horizontal /
   * Vertical fit). */
  const int clear = (int)d.body.num("m_ClearFlags", 1.0);
  c->clear_flags = clear == 2 ? 1 : 0;
  c->unity_clear_flags = clear;
  if (const YamlNode *bg = d.body.get("m_BackGroundColor"))
    c->background = Vec3(srgb_to_linear((float)std::max(0.0, bg->num("r"))), srgb_to_linear((float)std::max(0.0, bg->num("g"))), srgb_to_linear((float)std::max(0.0, bg->num("b")))),
    c->background_alpha = (float)bg->num("a", 0.0);
  c->hdr = d.body.num("m_HDR", 1.0) != 0.0;
  c->fov_axis = d.body.num("m_FOVAxisMode", 0.0) != 0.0 ? 1 : 0;
  if (d.body.num("m_projectionMatrixMode", 0.0) == 2.0) {  // (Unity: 0 Implicit, 1 Explicit, 2 Physical Properties Based)
    c->physical = true;
    c->focal_length = (float)std::max(1.0, d.body.num("m_FocalLength", 50.0));
    if (const YamlNode *s = d.body.get("m_SensorSize")) c->sensor_preset = 0, c->sensor_width = (float)s->num("x", 36.0), c->sensor_height = (float)s->num("y", 24.0);
    if (const YamlNode *s = d.body.get("m_LensShift")) c->shift_x = (float)s->num("x", 0.0), c->shift_y = (float)s->num("y", 0.0);
    const int gate = (int)d.body.num("m_GateFitMode", 2.0);
    c->sensor_fit = gate == 2 ? 1 : gate == 1 ? 2 : 0;
  }
}

/* Round 401: Unity's ReflectionProbe (class 215) as Blendity's. */
void add_reflection_probe(const UnityDoc &d, GameObject *g, const UnityImport &ctx) {
  ReflectionProbeComponent *p = g->add<ReflectionProbeComponent>();
  const int mode = (int)d.body.num("m_Mode", 0.0);
  p->mode = mode >= 0 && mode <= 2 ? mode : 0;
  if (const YamlNode *s = d.body.get("m_BoxSize")) p->box_size = Vec3((float)s->num("x", 10.0), (float)s->num("y", 10.0), (float)s->num("z", 10.0));
  if (const YamlNode *o = d.body.get("m_BoxOffset")) p->box_offset = Vec3((float)o->num("x", 0.0), (float)o->num("y", 0.0), (float)o->num("z", 0.0));
  p->box_projection = d.body.num("m_BoxProjection", 0.0) != 0.0;
  p->intensity = (float)std::max(0.0, d.body.num("m_IntensityMultiplier", 1.0));
  p->blend_distance = (float)std::max(0.0, d.body.num("m_BlendDistance", 1.0));
  p->importance = (int)d.body.num("m_Importance", 1.0);
  p->resolution = std::max(16, std::min(2048, (int)d.body.num("m_Resolution", 128.0)));
  p->hdr = d.body.num("m_HDR", 1.0) != 0.0;
  p->near_clip = (float)std::max(0.01, d.body.num("m_NearClip", 0.3));
  p->far_clip = (float)std::max(0.1, d.body.num("m_FarClip", 1000.0));
  if (const YamlNode *mask = d.body.get("m_CullingMask")) p->culling_mask = (int)(int64_t)mask->num("m_Bits", -1.0);
  p->enabled = d.body.num("m_Enabled", 1.0) != 0.0;
  const std::string custom = d.body.guid("m_CustomBakedTexture");
  if (!custom.empty() && d.body.file_id("m_CustomBakedTexture") && ctx.path_of) p->custom_cubemap.path = ctx.path_of(custom);
}

void add_engine(const UnityDoc &d, GameObject *g, UnityImport &ctx);

void add_collider(const UnityDoc &d, GameObject *g, UnityImport &ctx) {
  if (d.body.num("m_IsTrigger", 0.0) != 0.0) {  // (a trigger collides with nothing: kept as Unity's, not made solid - review 370)
    ctx.notes.push_back("a trigger collider is kept as Unity's (Blendity's colliders are solid)");
    return add_engine(d, g, ctx);
  }
  if (d.class_id == 136 && d.body.num("m_Direction", 1.0) != 1.0) ctx.notes.push_back("a capsule collider along X or Z stands along Y here");
  ColliderComponent *c = g->add<ColliderComponent>();
  c->enabled = d.body.num("m_Enabled", 1.0) != 0.0;
  c->fit = false;  // (Unity's own size, not refitted)
  c->center = vec3_of(d.body, "m_Center", Vec3(0.0f));
  c->shape = d.class_id == 65 ? 0 : d.class_id == 135 ? 1 : d.class_id == 136 ? 2 : d.body.num("m_Convex", 0.0) != 0.0 ? 3 : 4;
  c->size = vec3_of(d.body, "m_Size", Vec3(1.0f));
  c->radius = (float)d.body.num("m_Radius", 0.5);
  c->height = (float)d.body.num("m_Height", 2.0);
}

/* VRChat's components by their fields (their scripts sit in DLLs: the GUID names the DLL, not the class). */
std::string vrchat_type(const YamlNode &b) {
  if (b.get("VisemeBlendShapes") || b.get("ViewPosition")) return "VRCAvatarDescriptor";
  if (b.get("pull") && b.get("spring") && b.get("rootTransform")) return "VRCPhysBone";
  if (b.get("shapeType") && b.get("rootTransform") && (b.get("insideBounds") || b.get("bonesAsSpheres"))) return "VRCPhysBoneCollider";
  if (b.get("shapeType") && b.get("rootTransform") && b.get("collisionTags")) return b.get("allowSelf") ? "VRCContactReceiver" : "VRCContactSender";
  if (b.get("spawns") && b.get("RespawnHeightY")) return "VRCSceneDescriptor";
  if (b.get("Sources") && b.get("GlobalWeight")) {  // (round 402: VRC Constraints by their own fields; a Parent one carries both parts' Affects)
    if (b.get("AimAxis") || b.get("UpAxis")) return b.get("UseUpTransform") || b.get("RollOffset") ? "VRCLookAtConstraint" : "VRCAimConstraint";
    if (b.get("AffectsPositionX") && b.get("AffectsRotationX")) return "VRCParentConstraint";
    if (b.get("ScaleOffset") || b.get("ScaleAtRest") || b.get("AffectsScaleX")) return "VRCScaleConstraint";
    if (b.get("RotationOffset") || b.get("RotationAtRest") || b.get("AffectsRotationX")) return "VRCRotationConstraint";
    if (b.get("PositionOffset") || b.get("PositionAtRest") || b.get("AffectsPositionX")) return "VRCPositionConstraint";
    return "VRCParentConstraint";
  }
  if (b.get("serializedProgramAsset") || b.get("programSource")) return "UdonBehaviour";
  return "";
}

/* A VRChat PhysBone as Blendity's spring bone settings on its root (round 371): Pull (and Stiffness, in Advanced
 * integration) hold the chain, Spring is its bounce (less damping), Gravity (-1..1, VRChat's ratio to Pull) and Radius
 * as they are. The PhysBone stays too, for Unity. The settings move nothing until the skin's bones come in (review
 * 371: the import makes no Bone components yet). */
void add_physbone(const UnityDoc &d, GameObject *g, UnityImport &ctx, Build &b) {
  GameObject *root = object_at(b.by_fid, d.body.file_id("rootTransform"));
  GameObject *on = root ? root : g;
  SpringBone *s = on->get<SpringBone>();
  if (s && ctx.physboned->count(on)) return (void)s->physbones++, (void)ctx.springs++;  // (round 399: another PhysBone on the root - counted)
  if (!s) s = on->add<SpringBone>();  // (review 399: a spring the model brought is this PhysBone's, not one more)
  s->physbones = 1;
  ctx.physboned->insert(on);
  const bool advanced = d.body.num("integrationType", 0.0) > 0.5;
  s->stiffness = (float)std::clamp(advanced ? d.body.num("pull", 0.2) * 0.5 + d.body.num("stiffness", 0.2) * 0.5 : d.body.num("pull", 0.2), 0.0, 1.0);
  s->damping = (float)std::clamp(1.0 - d.body.num("spring", 0.2), 0.0, 1.0);
  s->gravity = (float)std::clamp(d.body.num("gravity", 0.0), -1.0, 1.0);
  s->radius = (float)std::max(0.0, d.body.num("radius", 0.02));
  ctx.springs++;
}

/* Round 397: VRC Light Volumes' components (red.sim.lightvolumes 3) by their scripts' GUIDs - a Light Volume as
 * Blendity's, its fields the package's; a Point Light Volume as its light's Light Volume option. Both stay Engine
 * Components too, written back as they were. */
constexpr const char *kLightVolumeGuid = "21bc8ec8ba5e5f3408667533e591f313", *kPointLightVolumeGuid = "064ede0cbb894e94abe683cb8db91f6f";

/* By the package's GUID only (review 397: a project's own script of the same name isn't the package's). */
bool is_script(const UnityDoc &d, const char *guid) { return d.class_id == 114 && d.body.guid("m_Script") == guid; }

/* A Point Light Volume beside a light: the light's option (the light comes with the object's Light component; one
 * without a light is the package's own light - kept as its Engine Component, which the Budget counts). A disabled one
 * turns nothing on. */
void mark_light_volume(const UnityDoc &d, GameObject &g) {
  Light *l = g.get<Light>();
  if (l && d.body.num("m_Enabled", 1.0) != 0.0) l->light_volume = true;
}

void add_light_volume(const UnityDoc &d, GameObject *g, UnityImport &ctx) {
  if (g->get<LightVolumeComponent>()) return;
  const std::string t0 = d.body.guid("Texture0"), t1 = d.body.guid("Texture1"), t2 = d.body.guid("Texture2");
  if (ctx.light_volume_textures && !t0.empty() && !t1.empty() && !t2.empty()) ctx.light_volume_textures(*g, t0, t1, t2);  // (round 398: Unity's bake)
  LightVolumeComponent *v = g->add<LightVolumeComponent>();
  const YamlNode &b = d.body;
  v->enabled = b.num("m_Enabled", 1.0) != 0.0;
  v->dynamic = b.num("IsDynamic") != 0.0, v->additive = b.num("IsAdditive") != 0.0;
  if (const YamlNode *c = b.get("Color"))
    v->color = Vec3(srgb_to_linear((float)c->num("r", 1.0)), srgb_to_linear((float)c->num("g", 1.0)), srgb_to_linear((float)c->num("b", 1.0)));
  v->intensity = (float)b.num("Intensity", 1.0), v->smooth_blending = (float)std::clamp(b.num("SmoothBlending", 0.25), 0.0, 1.0);
  v->exposure = (float)b.num("Exposure"), v->shadows = (float)b.num("Shadows"), v->highlights = (float)b.num("Highlights");
  v->bake = b.num("Bake", 1.0) != 0.0, v->adaptive_resolution = b.num("AdaptiveResolution", 1.0) != 0.0;
  v->voxels_per_unit = (float)b.num("VoxelsPerUnit", 3.0);
  if (const YamlNode *r = b.get("Resolution"))
    v->resolution[0] = std::max(1, (int)r->num("x", 16)), v->resolution[1] = std::max(1, (int)r->num("y", 16)), v->resolution[2] = std::max(1, (int)r->num("z", 16));
}

/* Anything Blendity doesn't hold: kept as it is. */
void add_engine(const UnityDoc &d, GameObject *g, UnityImport &ctx) {
  EngineComponent *e = g->add<EngineComponent>();
  e->engine = "Unity";
  e->script = d.class_id == 114 ? d.body.guid("m_Script") : std::string();
  const std::string name = !e->script.empty() && ctx.script_name ? ctx.script_name(e->script) : std::string();
  const std::string script_fid = std::to_string(d.body.file_id("m_Script"));  // (a DLL's scripts: one GUID, a fileID each)
  const std::string vrc = d.class_id == 114 ? vrchat_type(d.body) : std::string();
  const std::string sdk = d.class_id == 114 ? unity_sdk_class_name((int32_t)d.body.file_id("m_Script")) : std::string();  // (round 403: a DLL's class by its fileID)
  e->type = !name.empty() ? name : !vrc.empty() ? vrc : !sdk.empty() ? sdk : d.class_id == 114 ? "MonoBehaviour (" + e->script + ":" + script_fid + ")" : d.type;  // (its fields first: a test's fake id names nothing)
  if (e->script == kLightVolumeGuid || e->script == kPointLightVolumeGuid)  // (round 397: named without the package's scripts in the project)
    e->type = e->script == kLightVolumeGuid ? "LightVolumeInstance" : "PointLightVolumeInstance";
  e->data = d.header + d.text;  // (the document as written, its header line and line ends unchanged)
  ctx.engine++;
}

void add_component(const UnityDoc &d, GameObject *g, UnityImport &ctx, Build &b) {
  switch (d.class_id) {
    case 23: return add_renderer(d, g, ctx, b);
    case 108: return add_light(d, g, ctx), (void)ctx.lights++;
    case 20: return add_camera(d, g), (void)ctx.cameras++;
    case 64: case 65: case 135: case 136: return add_collider(d, g, ctx), (void)ctx.colliders++;
    case 215: return add_reflection_probe(d, g, ctx), (void)ctx.reflection_probes++;  // (round 401)
    case 33: return;  // (meshes: before)
    case 137: return add_renderer(d, g, ctx, b);
    case 95: {  // (round 402: shown and written back; the document kept too, read-only)
      add_animator(d, g, ctx);
      add_engine(d, g, ctx);
      static_cast<EngineComponent *>(g->components.back().get())->native = true;
      return;
    }
    default:
      if (d.class_id == 114 && vrchat_type(d.body) == "VRCPhysBone" && (!ctx.script_name || ctx.script_name(d.body.guid("m_Script")).empty()))
        add_physbone(d, g, ctx, b);  // (and kept below, for Unity; a project's own script with such fields isn't one)
      if (d.class_id == 114 && (!ctx.script_name || ctx.script_name(d.body.guid("m_Script")).empty())) {  // (round 402: the avatar's components as Blendity's)
        const std::string vt = vrchat_type(d.body);
        const size_t before = g->components.size();
        if (vt == "VRCAvatarDescriptor") add_avatar_descriptor(d, g, ctx);
        else if (vt == "VRCSceneDescriptor") add_scene_descriptor(d, g);
        else if (d.body.get("blueprintId") && d.body.get("contentType")) add_pipeline_manager(d, g);  // (round 403)
        else if (vt == "VRCPhysBone") add_physbone_component(d, g);
        else if (vt == "VRCPhysBoneCollider") add_physbone_collider(d, g);
        else if (vt == "VRCContactSender" || vt == "VRCContactReceiver") add_contact(d, g, vt == "VRCContactReceiver");
        else if (vt.size() > 13 && vt.compare(vt.size() - 10, 10, "Constraint") == 0 && vt.rfind("VRC", 0) == 0) add_vrc_constraint(d, g, vt);
        if (g->components.size() > before) {  // (review 403: the kept text of a document a Blendity component holds is read-only)
          add_engine(d, g, ctx);
          static_cast<EngineComponent *>(g->components.back().get())->native = true;
          return;
        }
      }
      if (is_script(d, kLightVolumeGuid)) add_light_volume(d, g, ctx), ctx.light_volumes++;  // (round 397: and kept below)
      return add_engine(d, g, ctx);
  }
}

bool structural(const UnityDoc &d) { return d.class_id == 1 || is_transform(d) || d.class_id == 1001 || d.stripped; }

}  // namespace

namespace {
/* The shortest text that reads back as the same float (Unity's own way: 0.1, not 0.100000001). */
std::string fmt(float v) {
  std::string s;
  for (int digits = 6; digits <= 9; digits++) {
    s = strprintf("%.*g", digits, (double)v);
    if ((float)std::strtod(s.c_str(), nullptr) == v) break;
  }
  return s == "-0" ? "0" : s;
}

bool near(double a, double b) { return std::fabs(a - b) <= 1e-6 * std::max(1.0, std::fabs(a)); }

/* The document's body (past its header and type lines) as YAML: the values an edit is compared with. */
YamlNode doc_body(const std::string &doc) {
  const YamlNode n = parse_yaml(doc.substr(std::min(doc.size(), doc.find('\n') + 1)));
  return n.kind == YamlNode::Map && !n.map.empty() ? n.map[0].second : YamlNode{};
}

bool same3(const YamlNode &b, const char *key, Vec3 v) {
  const YamlNode *n = b.get(key);
  return n && near(n->num("x"), v.x) && near(n->num("y"), v.y) && near(n->num("z"), v.z);
}

/* A document's line for `key` at the body's indent ("  key: ..."): the new value, the line's own end kept. */
/* The end of a value that Unity wrapped over several lines (round 403): the lines after `end` that are indented past
 * `indent` and are neither a key's line nor a list item belong to it (as the parser joins them). */
size_t wrapped_end(const std::string &doc, size_t end, size_t indent) {
  auto continuation = [&](const std::string &line) {  // (indented past the key, not a key's line nor a list item)
    const size_t first = line.find_first_not_of(" \r");
    if (first == std::string::npos || first <= indent || line[first] == '-') return false;
    const size_t colon = line.find(": ");
    const bool key_line = colon != std::string::npos && line.find_first_of(" '\"{[", first) >= colon;  // (a key, not a quoted text with a colon)
    const std::string trimmed = line.substr(0, line.find_last_not_of(" \r") + 1);
    return !key_line && !(trimmed.back() == ':' && trimmed.find_first_of(" '\"{[", first) == std::string::npos);
  };
  while (end < doc.size()) {
    const size_t next = doc.find('\n', end + 1);
    const std::string line = doc.substr(end + 1, (next == std::string::npos ? doc.size() : next) - end - 1);
    if (line.find_first_not_of(" \r") == std::string::npos) {  // (an empty line: a folded newline when a continuation follows it)
      if (next == std::string::npos) break;
      const size_t after = doc.find('\n', next + 1);
      const std::string peek = doc.substr(next + 1, (after == std::string::npos ? doc.size() : after) - next - 1);
      if (!continuation(peek)) break;
      end = next;
      continue;
    }
    if (!continuation(line)) break;
    end = next == std::string::npos ? doc.size() : next;
  }
  return end;
}

bool replace_line(std::string &doc, const std::string &key, const std::string &value) {
  const std::string head = "\n  " + key + ": ";
  size_t at = doc.find(head);
  if (at == std::string::npos) return false;
  at += 1;
  size_t end = doc.find('\n', at);
  if (end == std::string::npos) end = doc.size();
  end = wrapped_end(doc, end, 2);
  const bool cr = end > at && doc[end - 1] == '\r';
  doc.replace(at, end - at, "  " + key + ": " + value + (cr ? "\r" : ""));
  return true;
}

std::string vec3_text(Vec3 v) { return "{x: " + fmt(v.x) + ", y: " + fmt(v.y) + ", z: " + fmt(v.z) + "}"; }

/* A field's current text equals `value`: numbers within float noise, anything else as written. */
bool same_scalar(const YamlNode *n, const std::string &value) {
  if (!n) return false;
  char *ea = nullptr, *eb = nullptr;
  const double a = std::strtod(n->value.c_str(), &ea), b = std::strtod(value.c_str(), &eb);
  const bool numbers = !n->value.empty() && !value.empty() && *ea == '\0' && *eb == '\0';
  if (numbers) return std::fabs(a - b) <= 1e-5 * std::max(1.0, std::fabs(a));
  if (n->kind == YamlNode::Map && value.size() > 1 && value.front() == '{') {  // (an inline mapping: each of its numbers)
    const std::string inner = value.substr(1, value.size() - 2);
    for (size_t at = 0; at < inner.size();) {
      size_t comma = inner.find(',', at);
      if (comma == std::string::npos) comma = inner.size();
      const std::string part = inner.substr(at, comma - at);
      const size_t colon = part.find(':');
      if (colon == std::string::npos) return false;
      std::string key = part.substr(0, colon), v = part.substr(colon + 1);
      key.erase(0, key.find_first_not_of(' ')), v.erase(0, v.find_first_not_of(' '));
      if (!same_scalar(n->get(key), v)) return false;
      at = comma + 1;
    }
    return true;
  }
  return n->value == value;
}

/* The line "    child: ..." inside the mapping "  parent:" replaced; false when either isn't there. */
bool replace_nested_line(std::string &doc, const std::string &parent, const std::string &child, const std::string &value) {
  const size_t at = doc.find("\n  " + parent + ":");
  if (at == std::string::npos) return false;
  size_t line = doc.find('\n', at + 1);
  while (line != std::string::npos && line + 1 < doc.size() && doc.compare(line + 1, 4, "    ") == 0) {
    const size_t end = doc.find('\n', line + 1) == std::string::npos ? doc.size() : doc.find('\n', line + 1);
    const std::string head = "    " + child + ": ";
    if (doc.compare(line + 1, head.size(), head) == 0) {
      const size_t wend = wrapped_end(doc, end, 4);
      const bool cr = wend > line + 1 && doc[wend - 1] == '\r';
      doc.replace(line + 1, wend - line - 1 - (cr ? 1 : 0), head + value);
      return true;
    }
    line = end == doc.size() ? std::string::npos : end;
  }
  return false;
}

/* Round 401: a document's own fields written where they differ. */
void apply_field_edits(std::string &doc, const UnityEdit &e, int &changed) {
  const YamlNode b = doc_body(doc);
  for (const auto &[key, value] : e.fields) {
    const size_t slash = key.find('/');
    if (slash == std::string::npos) {
      if (!same_scalar(b.get(key), value)) changed += replace_line(doc, key, value);
      continue;
    }
    const YamlNode *parent = b.get(key.substr(0, slash));
    if (!same_scalar(parent ? parent->get(key.substr(slash + 1)) : nullptr, value)) changed += replace_nested_line(doc, key.substr(0, slash), key.substr(slash + 1), value);
  }
}

void apply_edit(std::string &doc, const UnityEdit &e, int &changed) {
  if (!e.document.empty()) {  // (round 403: a kept document edited here goes back whole - when it differs)
    std::string want = e.document;
    if (want.back() != '\n') want += '\n';
    if (want != doc) doc = want, changed++;
    return;
  }
  if (!e.fields.empty()) return apply_field_edits(doc, e, changed);
  const YamlNode b = doc_body(doc);
  if (!e.transform) {
    if (b.str("m_Name") != e.name) changed += replace_line(doc, "m_Name", unity_yaml_string(e.name));
    if ((b.num("m_IsActive", 1.0) != 0.0) != e.active) changed += replace_line(doc, "m_IsActive", e.active ? "1" : "0");
    return;
  }
  const Quat q = e.rotation;
  const YamlNode *r = b.get("m_LocalRotation");
  const bool turned = !r || !((near(r->num("x"), q.x) && near(r->num("y"), q.y) && near(r->num("z"), q.z) && near(r->num("w", 1.0), q.w)) ||
                              (near(r->num("x"), -q.x) && near(r->num("y"), -q.y) && near(r->num("z"), -q.z) && near(r->num("w", 1.0), -q.w)));
  if (turned) {
    changed += replace_line(doc, "m_LocalRotation", "{x: " + fmt(q.x) + ", y: " + fmt(q.y) + ", z: " + fmt(q.z) + ", w: " + fmt(q.w) + "}");
    changed += replace_line(doc, "m_LocalEulerAnglesHint", vec3_text(e.euler));
  }
  if (!same3(b, "m_LocalPosition", e.position)) changed += replace_line(doc, "m_LocalPosition", vec3_text(e.position));
  if (!same3(b, "m_LocalScale", e.scale)) changed += replace_line(doc, "m_LocalScale", vec3_text(e.scale));
}
}  // namespace

/* ----------------------------------------------------------------- post-processing (round 401) */

namespace {

/* A PPv2 parameter's value when the profile overrides it: {overrideState: 1, value: ...}. */
const YamlNode *overridden(const YamlNode &effect, const char *name) {
  const YamlNode *p = effect.get(name);
  return p && p->num("overrideState", 0.0) != 0.0 ? p->get("value") : nullptr;
}
float post_num(const YamlNode &effect, const char *name, float def) {
  const YamlNode *v = overridden(effect, name);
  return v ? (float)std::atof(v->value.c_str()) : def;
}
Vec3 post_color(const YamlNode &effect, const char *name, Vec3 def) {
  const YamlNode *v = overridden(effect, name);
  return v ? Vec3((float)v->num("r", def.x), (float)v->num("g", def.y), (float)v->num("b", def.z)) : def;
}
void post_vec4(const YamlNode &effect, const char *name, float out[4]) {
  const YamlNode *v = overridden(effect, name);
  if (!v) return;
  out[0] = (float)v->num("x", out[0]), out[1] = (float)v->num("y", out[1]), out[2] = (float)v->num("z", out[2]), out[3] = (float)v->num("w", out[3]);
}
bool post_on(const YamlNode &effect) {
  if (effect.num("active", 1.0) == 0.0) return false;
  const YamlNode *v = overridden(effect, "enabled");
  return v ? v->value != "0" : false;  // (an effect not overriding `enabled` is off, as PPv2 takes it)
}

}  // namespace

bool unity_post_profile(const std::string &yaml, UnityPostProfile &out) {
  std::vector<UnityDoc> docs;
  std::string why;
  if (!parse_unity_yaml(yaml, docs, why)) return false;
  out = UnityPostProfile{};
  bool any = false;
  for (const UnityDoc &d : docs) {
    if (d.class_id != 114) continue;
    const std::string name = d.body.str("m_Name");
    if (name == "PostProcessProfile" || d.body.get("settings")) { any = true; continue; }  // (the profile's own document lists its effects)
    any = true;
    if (name == "ColorGrading") {
      UnityPostProfile::Grading &g = out.grading;
      g.on = post_on(d.body);
      g.tonemapper = (int)post_num(d.body, "tonemapper", 0.0f);
      g.post_exposure = post_num(d.body, "postExposure", 0.0f);
      g.temperature = post_num(d.body, "temperature", 0.0f), g.tint = post_num(d.body, "tint", 0.0f), g.hue_shift = post_num(d.body, "hueShift", 0.0f);
      g.saturation = post_num(d.body, "saturation", 0.0f), g.contrast = post_num(d.body, "contrast", 0.0f);
      g.color_filter = post_color(d.body, "colorFilter", g.color_filter);
      post_vec4(d.body, "lift", g.lift), post_vec4(d.body, "gamma", g.gamma), post_vec4(d.body, "gain", g.gain);
      continue;
    }
    if (name == "Bloom") {
      UnityPostProfile::Bloom &b = out.bloom;
      b.on = post_on(d.body);
      b.intensity = post_num(d.body, "intensity", 0.0f), b.threshold = post_num(d.body, "threshold", 1.0f), b.soft_knee = post_num(d.body, "softKnee", 0.5f);
      b.diffusion = post_num(d.body, "diffusion", 7.0f), b.clamp = post_num(d.body, "clamp", 65472.0f);
      b.color = post_color(d.body, "color", b.color);
      continue;
    }
    if (name == "Vignette") {
      UnityPostProfile::Vignette &v = out.vignette;
      v.on = post_on(d.body);
      v.intensity = post_num(d.body, "intensity", 0.0f), v.smoothness = post_num(d.body, "smoothness", 0.2f), v.roundness = post_num(d.body, "roundness", 1.0f);
      v.color = post_color(d.body, "color", v.color);
      continue;
    }
    if (!name.empty() && post_on(d.body)) out.undrawn.push_back(name);
  }
  return any;
}

void unity_post_apply(const UnityPostProfile &p, Scene &scene, GameObject &camera, std::vector<std::string> &notes) {
  if (p.grading.on) {
    scene.render.view_transform = p.grading.tonemapper == 2 ? 2 : p.grading.tonemapper == 1 ? 1 : 0;  // (ACES, Neutral as Filmic, None as Standard)
    scene.render.exposure = p.grading.post_exposure;
  }
  CameraFilters *cf = camera.get<CameraFilters>();
  if (!cf && (p.grading.on || p.bloom.on || p.vignette.on)) cf = camera.add<CameraFilters>();
  auto replace = [&](const char *type) -> FilterEffect * {
    for (size_t i = 0; cf && i < cf->effects.size(); i++)
      if (cf->effects[i] && std::string(cf->effects[i]->type_name()) == type) cf->effects.erase(cf->effects.begin() + (std::ptrdiff_t)i), i--;
    return cf ? cf->add(type) : nullptr;
  };
  const UnityPostProfile::Grading &g = p.grading;
  const bool graded = g.on && (g.temperature != 0.0f || g.tint != 0.0f || g.saturation != 0.0f || g.contrast != 0.0f || g.lift[3] != 0.0f || g.gamma[3] != 0.0f || g.gain[3] != 0.0f ||
                               g.lift[0] != g.lift[1] || g.lift[1] != g.lift[2] || g.gamma[0] != g.gamma[1] || g.gamma[1] != g.gamma[2] || g.gain[0] != g.gain[1] || g.gain[1] != g.gain[2]);
  if (graded)
    if (auto *e = dynamic_cast<ColorGradingEffect *>(replace(ColorGradingEffect::kName))) {
      e->temperature = g.temperature / 100.0f, e->tint = g.tint / 100.0f, e->saturation = g.saturation / 100.0f, e->contrast = g.contrast / 100.0f;
      /* PPv2's wheels: xyz the colour (grey 1 neutral), w the offset -1..1; Blendity's lift adds (black neutral), gamma
       * and gain scale (1 neutral). */
      e->lift = Vec3(g.lift[0] - 1.0f + g.lift[3], g.lift[1] - 1.0f + g.lift[3], g.lift[2] - 1.0f + g.lift[3]);
      e->gamma = Vec3(g.gamma[0] + g.gamma[3], g.gamma[1] + g.gamma[3], g.gamma[2] + g.gamma[3]);
      e->gain = Vec3(g.gain[0] + g.gain[3], g.gain[1] + g.gain[3], g.gain[2] + g.gain[3]);
    }
  if (p.bloom.on)
    if (auto *b = dynamic_cast<BloomEffect *>(replace(BloomEffect::kName))) {
      b->intensity = std::exp2(p.bloom.intensity / 10.0f) - 1.0f;  // (PPv2: intensity = 2^(i / 10) - 1)
      b->threshold = srgb_to_linear(std::max(0.0f, std::min(1.0f, p.bloom.threshold))) + std::max(0.0f, p.bloom.threshold - 1.0f);  // (its threshold is gamma)
      b->soft_knee = p.bloom.soft_knee, b->scatter = std::max(0.0f, std::min(1.0f, p.bloom.diffusion / 10.0f)), b->clamp = p.bloom.clamp;
      b->tint = p.bloom.color;
    }
  if (p.vignette.on)
    if (auto *v = dynamic_cast<VignetteEffect *>(replace(VignetteEffect::kName))) {
      v->intensity = p.vignette.intensity, v->smoothness = p.vignette.smoothness, v->roundness = p.vignette.roundness, v->color = p.vignette.color;
    }
  for (const std::string &u : p.undrawn) notes.push_back("the post-processing profile's " + u + " is kept for Unity: Blendity doesn't draw it");
}

std::string unity_yaml_num(double v) { return fmt(v); }
std::string unity_yaml_color(Vec3 c, float alpha) {
  auto g = [](float v) { return v <= 1.0f ? linear_to_srgb(std::max(0.0f, v)) : std::pow(v, 1.0f / 2.2f); };  // (HDR past 1 by the same curve)
  return "{r: " + fmt(g(c.x)) + ", g: " + fmt(g(c.y)) + ", b: " + fmt(g(c.z)) + ", a: " + fmt(alpha) + "}";
}
std::string unity_yaml_vec3(Vec3 v) { return vec3_text(v); }

std::string unity_yaml_string(const std::string &s) {
  bool plain = !s.empty() && s.find(": ") == std::string::npos && s.find(":\t") == std::string::npos && s.find(" #") == std::string::npos && s.back() != ' ' &&
               s.front() != ' ' && s.back() != ':';  // (a trailing colon reads as a mapping - review 372)
  for (unsigned char c : s) plain = plain && c >= 0x20 && c < 0x7f;
  plain = plain && std::string("'\"{[&*!|>%@`-?,#~").find(s.front()) == std::string::npos;
  if (plain) return s;
  std::string out = "\"";
  for (size_t i = 0; i < s.size();) {  // (UTF-8 to \uXXXX, surrogate pairs past the BMP, as Unity writes)
    const unsigned char c = (unsigned char)s[i];
    const int n = c < 0x80 ? 0 : (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : 3;
    uint32_t cp = n == 0 ? c : n == 1 ? (c & 31u) : n == 2 ? (c & 15u) : (c & 7u);
    for (int k = 1; k <= n && i + k < s.size(); k++) cp = cp << 6 | ((unsigned char)s[i + k] & 63u);
    i += (size_t)n + 1;
    if (cp == '"' || cp == '\\') out += '\\', out += (char)cp;
    else if (cp >= 0x20 && cp < 0x7f) out += (char)cp;
    else if (cp >= 0x10000) out += strprintf("\\u%04X\\u%04X", 0xD800 + ((cp - 0x10000) >> 10), 0xDC00 + ((cp - 0x10000) & 0x3FF));
    else out += strprintf("\\u%04X", cp);
  }
  return out + "\"";
}

bool unity_set_field(std::string &doc, const std::string &key, const std::string &value) {
  const size_t slash = key.find('/');
  return slash == std::string::npos ? replace_line(doc, key, value) : replace_nested_line(doc, key.substr(0, slash), key.substr(slash + 1), value);
}

std::string patch_unity_objects(const std::string &text, const std::vector<UnityEdit> &edits, int *changed) {
  int n = 0;
  std::string out;
  for (size_t at = 0; at < text.size();) {  // (document by document: "--- !u!<class> &<fileID>" to the next)
    size_t next = text.find("\n--- ", at);
    next = next == std::string::npos ? text.size() : next + 1;
    std::string doc = text.substr(at, next - at);
    at = next;
    const size_t amp = doc.rfind("--- ", 0) == 0 ? doc.find('&') : std::string::npos;
    const int64_t fid = amp == std::string::npos || amp > doc.find('\n') ? 0 : std::strtoll(doc.c_str() + amp + 1, nullptr, 10);
    const size_t bang = doc.rfind("--- !u!", 0) == 0 ? 7 : std::string::npos;
    const int cls = bang == std::string::npos ? 0 : (int)std::strtol(doc.c_str() + bang, nullptr, 10);
    const bool live = doc.find("stripped") > doc.find('\n');
    for (const UnityEdit &e : edits) {
      if (!live || !fid) continue;
      if (e.class_id == 0 && e.file_id == fid) apply_edit(doc, e, n);
      else if (e.class_id != 0 && e.class_id == cls && (e.game_object == 0 || doc_body(doc).file_id("m_GameObject") == e.game_object)) apply_edit(doc, e, n);  // (round 401)
    }
    out += doc;
  }
  if (changed) *changed = n;
  return out;
}

GameObject *objects_from_model(const ImportResult &r, const std::string &name, Scene &scene, std::vector<GameObject *> *made_out) {
  GameObject *root = scene.create(name);
  std::vector<GameObject *> made;
  for (const ImportedNode &n : r.nodes) {
    GameObject *g = scene.create(n.name.empty() ? name : n.name);
    Transform t;
    t.position = n.position, t.rotation = n.rotation, t.scale = n.scale, t.euler_hint = n.rotation.to_euler();
    g->set_local(t);
    if (n.mesh) g->add<MeshFilter>()->mesh = n.mesh, g->add<MeshRenderer>()->materials = n.materials;
    if (n.mesh) g->get<MeshRenderer>()->shape_weights = n.shape_weights;  // (round 399: as the model import keeps them)
    made.push_back(g);
  }
  for (size_t i = 0; i < r.nodes.size(); i++) {
    const int p = r.nodes[i].parent;
    scene.set_parent(made[i], p >= 0 && p < (int)made.size() ? made[(size_t)p] : root, -1, false);
  }
  bind_import_skins(scene, r, made, root);  // (round 399: its bones and skins - Shinano's 275 bones had come in as plain objects, the Budget's 0)
  if (made_out) *made_out = made;
  return root;
}

GameObject *unity_collapse_model_root(Scene &scene, GameObject *root) {
  if (!root || root->children.size() != 1 || !root->components.empty()) return root;
  GameObject *top = root->children[0];
  scene.set_parent(top, root->parent, -1, true);  // (the root is where the model was placed: the node keeps its place)
  if (!top->get<Armature>()) top->name = root->name;  // (an armature keeps the unique name its skins find it by - review 399)
  scene.destroy(root);
  return top;
}

namespace {

/* A mesh's slots in the order its faces first use them - Unity's submesh order (a model's submesh 0 is the material
 * of its first polygon, not the file's first material: SideRail's tunnel floor had worn its trim sheet). */
std::vector<int> slots_by_first_use(const Mesh &m) {
  std::vector<int> order;
  for (size_t f = 0; f < m.face_count(); f++) {
    const int k = m.material_of(f);
    if (k >= 0 && std::find(order.begin(), order.end(), k) == order.end()) order.push_back(k);
  }
  return order;
}

bool in_order(const std::vector<int> &order, size_t slots) {
  if (order.size() != slots) return false;
  for (size_t i = 0; i < order.size(); i++)
    if (order[i] != (int)i) return false;
  return true;
}

}  // namespace

int unity_drop_unused_materials(ImportResult &r) {
  /* Each mesh once (review 399: glTF and Collada nodes share one - renumbering it for the first node had given the
   * second its slots by the new numbers): its slots in use in Unity's order, then every node of it takes those. */
  std::map<const Mesh *, std::vector<int>> order_of;
  for (const ImportedNode &n : r.nodes)
    if (n.mesh && !order_of.count(n.mesh.get())) order_of[n.mesh.get()] = slots_by_first_use(*n.mesh);
  int dropped = 0;
  for (ImportedNode &n : r.nodes) {
    const auto it = n.mesh ? order_of.find(n.mesh.get()) : order_of.end();
    if (it == order_of.end() || it->second.empty() || n.materials.size() < 2 || in_order(it->second, n.materials.size())) continue;
    std::vector<MaterialPtr> kept;
    for (int k : it->second) kept.push_back((size_t)k < n.materials.size() ? n.materials[(size_t)k] : n.materials.back());
    dropped += (int)(n.materials.size() - std::min(n.materials.size(), kept.size()));
    n.materials = std::move(kept);
  }
  for (const auto &[mesh, order] : order_of) {
    if (order.empty() || in_order(order, (size_t)*std::max_element(order.begin(), order.end()) + 1)) continue;
    std::vector<int> to;
    for (size_t i = 0; i < order.size(); i++) {
      if ((size_t)order[i] >= to.size()) to.resize((size_t)order[i] + 1, 0);
      to[(size_t)order[i]] = (int)i;
    }
    Mesh *m = const_cast<Mesh *>(mesh);
    for (int32_t &k : m->face_material) k = k >= 0 && k < (int)to.size() ? to[(size_t)k] : 0;
    m->touch();
  }
  return dropped;
}

int64_t unity_nested_file_id(int64_t inner, int64_t prefab_instance) {
  return (int64_t)(((uint64_t)inner ^ (uint64_t)prefab_instance) & 0x7FFFFFFFFFFFFFFFull);
}

int64_t unity_hashed_file_id(const std::string &type, const std::string &name, int occurrence) {
  const std::string s = "Type:" + type + "->" + name + std::to_string(occurrence);
  return (int64_t)xxh64(s.data(), s.size());
}

namespace {

/* Unity's numbers for a model's root, the same in every model: persistent (2019.3+) and legacy. */
const struct {
  int64_t file_id;
  int class_id;
} kModelRootIds[] = {{919132149155446097LL, 1}, {-8679921383154817045LL, 4}, {-7511558181221131132LL, 23}, {-5754084199372789682LL, 33},
                     {100000, 1}, {400000, 4}, {2300000, 23}, {3300000, 33}};

/* A legacy table's entry: its class from the number (Unity's class times 100000, plus twice the index). */
void add_legacy(int64_t fid, const std::string &name, UnityModelIds &out) {
  const int cls = (int)(fid / 100000);
  if (fid <= 0 || (cls != 1 && cls != 4 && cls != 23 && cls != 33 && cls != 137)) return;
  out[fid] = {cls, name == "//RootNode" ? std::string() : name};
}

}  // namespace

void unity_model_ids_from_meta(const YamlNode &meta, UnityModelIds &out) {
  for (const auto &r : kModelRootIds) out[r.file_id] = {r.class_id, std::string()};
  const YamlNode *importer = meta.get("ModelImporter");
  if (!importer) return;
  if (const YamlNode *recycle = importer->get("fileIDToRecycleName"))
    for (const auto &[k, name] : recycle->map) add_legacy(std::strtoll(k.c_str(), nullptr, 10), name.value, out);
  const YamlNode *table = importer->get("internalIDToNameTable");
  for (const YamlNode &e : table ? table->seq : std::vector<YamlNode>()) {
    const YamlNode *first = e.get("first");
    for (const auto &[cls, id] : first ? first->map : std::vector<std::pair<std::string, YamlNode>>()) add_legacy(std::strtoll(id.value.c_str(), nullptr, 10), e.str("second"), out);
  }
}

namespace {

/* A texture's .meta text: beside it (a Unity project's Assets copied in) or by its GUID in the package folder that lists
 * it (.unity-guids, the folder's .unity-meta). */
bool texture_meta(const std::string &asset_path, std::string &meta) {
  const std::string abs = resolve_asset_path(asset_path);
  if (fs::read_file(abs + ".meta", meta)) return true;
  std::string rel = make_asset_relative(abs);
  std::replace(rel.begin(), rel.end(), '\\', '/');
  for (std::string dir = fs::parent(abs); !dir.empty() && dir != fs::parent(dir); dir = fs::parent(dir)) {
    std::string text;
    if (!fs::read_file(fs::join(dir, ".unity-guids"), text)) continue;
    const size_t at = text.find("\t" + rel + "\n");
    if (at == std::string::npos) continue;  // (review 399: a nested package's list - the one above may hold it)
    const size_t start = text.rfind('\n', at) == std::string::npos ? 0 : text.rfind('\n', at) + 1;
    const std::string guid = text.substr(start, text.find('\t', start) - start);
    return fs::read_file(fs::join(fs::join(dir, ".unity-meta"), guid + ".meta"), meta);
  }
  return false;
}

/* TextureImporterFormat's bytes a texel (crunched as what it unpacks to); 0: automatic or one not listed. */
double format_bytes(int format) {
  switch (format) {
    case 10: case 28: case 26: case 34: case 41: case 45: case 46: case 64: return 0.5;  // DXT1 (+Crunched), BC4, ETC_RGB4 (+Crunched), EAC_R, ETC2_RGB4, ETC2 punch-through
    case 1: case 12: case 29: case 24: case 25: case 27: case 47: case 48: case 63: case 65: return 1.0;  // Alpha8, DXT5 (+Crunched), BC6H, BC7, BC5, ETC2_RGBA8 (+Crunched), ASTC 4x4, R8
    case 2: case 7: case 9: case 13: case 15: return 2.0;    // ARGB16, RGB16, R16, RGBA16, RHalf
    case 3: return 3.0;                                      // RGB24
    case 4: case 5: case 16: case 18: case 22: return 4.0;   // RGBA32, ARGB32, RGHalf, RFloat, RGB9E5
    case 17: case 19: return 8.0;                            // RGBAHalf, RGFloat
    case 20: return 16.0;                                    // RGBAFloat
    default: return 0.0;
  }
}

/* A side as Unity's Non-Power of 2 setting makes it (0 none, 1 to nearest, 2 to larger, 3 to smaller). */
double npot_side(double side, int mode) {
  if (mode == 0 || side < 1.0) return side;
  const double below = std::exp2(std::floor(std::log2(side))), above = below == side ? side : below * 2.0;
  if (mode == 2) return above;
  if (mode == 3) return below;
  return side - below <= above - side ? below : above;
}

}  // namespace

bool unity_texture_megabytes(const std::string &asset_path, int width, int height, bool has_alpha, double &mb) {
  std::string text;
  if (width <= 0 || height <= 0 || !texture_meta(asset_path, text)) return false;
  const YamlNode meta = parse_yaml(text);
  const YamlNode *imp = meta.get("TextureImporter");
  if (!imp) return false;
  const YamlNode *chosen = nullptr;
  const YamlNode *platforms = imp->get("platformSettings");
  static const std::vector<YamlNode> kNoPlatforms;
  const std::vector<YamlNode> &items = platforms ? platforms->seq : kNoPlatforms;  // (both lvalues: `chosen` points into the meta, not a copy)
  for (const YamlNode &s : items) {
    if (s.str("buildTarget") == "DefaultTexturePlatform" && !chosen) chosen = &s;
    if (s.str("buildTarget") == "Standalone" && s.num("overridden") != 0.0) chosen = &s;
  }
  const double cap = chosen ? chosen->num("maxTextureSize", imp->num("maxTextureSize", 2048)) : imp->num("maxTextureSize", 2048);
  const int npot = (int)imp->num("nPOTScale", 1.0);
  const double pw = npot_side(width, npot), ph = npot_side(height, npot);  // (review 399: rounded first, then capped)
  const double side = std::max(pw, ph), scale = side > cap && cap > 0 ? cap / side : 1.0;
  const double w = std::max(1.0, std::floor(pw * scale)), h = std::max(1.0, std::floor(ph * scale));
  const int alpha_source = (int)imp->num("alphaSource", 1.0);  // (0 none, 1 from the image, 2 from its grey)
  has_alpha = alpha_source == 2 || (alpha_source == 1 && has_alpha);
  const YamlNode *mips = imp->get("mipmaps");
  const bool mipped = (mips ? mips->num("enableMipMap", 1) : imp->num("enableMipMap", 1)) != 0.0;
  const bool normal = imp->num("textureType", 0) == 1.0;
  double bytes = format_bytes(chosen ? (int)chosen->num("textureFormat", -1) : -1);
  const int quality = chosen ? (int)chosen->num("textureCompression", 1) : 1;  // (0 none, 1 normal, 2 high: BC7, 3 low)
  if (bytes == 0.0) bytes = quality == 0 ? 4.0 : quality == 2 || normal || has_alpha ? 1.0 : 0.5;
  mb = w * h * bytes * (mipped ? 4.0 / 3.0 : 1.0) / (1024.0 * 1024.0);
  return true;
}

std::map<std::string, std::string> unity_material_remap(const YamlNode &meta) {
  std::map<std::string, std::string> out;
  const YamlNode *importer = meta.get("ModelImporter");
  const YamlNode *list = importer ? importer->get("externalObjects") : nullptr;
  for (const YamlNode &e : list ? list->seq : std::vector<YamlNode>()) {
    const YamlNode *first = e.get("first");
    const std::string guid = e.guid("second");
    if (first && first->str("type") == "UnityEngine:Material" && !first->str("name").empty() && !guid.empty()) out[first->str("name")] = guid;
  }
  return out;
}

void remap_unity_slots(std::vector<MaterialPtr> &slots, const std::map<std::string, std::string> &remap,
                       const std::function<MaterialPtr(const std::string &guid)> &material, std::map<std::string, MaterialPtr> &converted) {
  for (MaterialPtr &slot : slots) {
    const auto it = slot ? remap.find(slot->name) : remap.end();
    if (it == remap.end()) continue;
    auto c = converted.find(it->second);
    if (c == converted.end()) c = converted.emplace(it->second, material ? material(it->second) : nullptr).first;
    if (c->second) slot = c->second;
  }
}

int apply_unity_material_remap(const std::vector<GameObject *> &made, const std::map<std::string, std::string> &remap,
                               const std::function<MaterialPtr(const std::string &guid)> &material) {
  std::map<std::string, MaterialPtr> converted;  // (each .mat converted once for the model)
  for (GameObject *g : made) {
    MeshRenderer *mr = g ? g->get<MeshRenderer>() : nullptr;
    if (mr) remap_unity_slots(mr->materials, remap, material, converted);  // (the renderer's own slots: `mr ? mr->materials : {}` would be a copy)
  }
  return (int)std::count_if(converted.begin(), converted.end(), [](const auto &c) { return c.second != nullptr; });
}

void parse_unity_model_ids(const std::string &text, std::map<std::string, UnityModelIds> &out) {
  for (size_t at = 0; at < text.size();) {
    size_t end = text.find('\n', at);
    if (end == std::string::npos) end = text.size();
    std::string line = text.substr(at, end - at);
    at = end + 1;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const size_t t1 = line.find('\t'), t2 = t1 == std::string::npos ? t1 : line.find('\t', t1 + 1);
    const size_t t3 = t2 == std::string::npos ? t2 : line.find('\t', t2 + 1);
    if (t3 == std::string::npos || t1 != 32) continue;  // (a GUID is 32 hex digits)
    const int64_t fid = std::strtoll(line.c_str() + t1 + 1, nullptr, 10);
    const int cls = std::atoi(line.c_str() + t2 + 1);
    if (fid != 0 && cls > 0) out[line.substr(0, 32)][fid] = {cls, line.substr(t3 + 1)};
  }
}

void unity_model_targets(const ImportResult &r, GameObject *root, const std::vector<GameObject *> &made, const UnityModelIds &ids, UnityTargets &out) {
  const size_t n = std::min(made.size(), r.nodes.size());
  int tops = 0, top = -1;
  for (size_t i = 0; i < n; i++)
    if (r.nodes[i].parent < 0 || r.nodes[i].parent >= (int)n) tops++, top = (int)i;
  const int collapsed = tops == 1 ? top : -1;  // (one top node: Unity's root is that node)
  std::map<std::string, GameObject *> by_path;
  std::map<std::string, int> name_count;
  std::map<std::string, GameObject *> by_name;
  for (size_t i = 0; i < n; i++) {
    std::string path;
    int guard = 0;
    for (int k = (int)i; k >= 0 && k < (int)n && k != collapsed && guard++ < 256; k = r.nodes[(size_t)k].parent)
      path = path.empty() ? r.nodes[(size_t)k].name : r.nodes[(size_t)k].name + "/" + path;
    by_path.emplace(path, made[i]);
    name_count[r.nodes[i].name]++;
    by_name[r.nodes[i].name] = made[i];
  }
  for (const auto &[fid, part] : ids) {
    GameObject *g = nullptr;
    if (part.path.empty()) g = collapsed >= 0 ? made[(size_t)collapsed] : root;  // (review 395: its transform is the one the instance overrides)
    if (!g && by_path.count(part.path)) g = by_path[part.path];
    if (!g && name_count[part.path] == 1) g = by_name[part.path];  // (a legacy table names the node, not its path)
    if (g) out[fid] = {g, part.class_id};
  }
}

/* Unity's RenderSettings: AmbientMode 0 Skybox, 1 Trilight, 3 Flat, 4 Custom; FogMode 1 Linear, 2 Exponential,
 * 3 Exponential Squared. Colours are gamma, as a material's. */
/* Round 401: the skybox material as Blendity's world - Unity's procedural sky as the Sky, a panorama or cubemap as the
 * HDRI, a plain colour as the Color; the ambient's Intensity Multiplier as the strength (it scales Unity's ambient alone,
 * Blendity's background too - noted). */
void skybox_world(const UnitySkybox &sb, EnvironmentSettings &env, std::vector<std::string> &notes) {
  const std::string s = to_lower(sb.shader);
  if (s.find("procedural") != std::string::npos) {
    env.mode = 1;
    env.turbidity = std::max(1.0f, std::min(10.0f, 1.0f + 2.0f * sb.atmosphere_thickness));  // (1 -> 3, Unity's and Hosek's clear days)
    env.strength *= sb.exposure;
    return;
  }
  if (s.find("panoramic") != std::string::npos || s.find("cubemap") != std::string::npos) {
    if (sb.texture.empty()) return notes.push_back("the skybox's picture isn't in the project: Blendity's sky stays");
    env.mode = 2, env.hdri.path = sb.texture, env.rotation = sb.rotation;
    env.strength *= sb.exposure * std::max(0.0f, sb.tint.x + sb.tint.y + sb.tint.z) * (2.0f / 3.0f);  // (Unity's grey tint 0.5 is neutral)
    if (s.find("cubemap") != std::string::npos) notes.push_back("the skybox is a cubemap: shown as the HDRI only when its file is a panorama");
    return;
  }
  if (s.find("6 sided") != std::string::npos || s.find("6sided") != std::string::npos) {
    env.mode = 3, env.color = sb.tint * (2.0f * sb.exposure);
    return notes.push_back("a 6 Sided skybox isn't stitched yet: its tint as a plain colour");
  }
  if (s.find("unlit") != std::string::npos || s == "unlit/color") {
    env.mode = 3, env.color = sb.color;
    return;
  }
  notes.push_back("the skybox's shader (" + sb.shader + ") isn't one Blendity reads: Blendity's sky stays");
}

bool unity_render_settings(const std::vector<UnityDoc> &docs, EnvironmentSettings &env, std::vector<std::string> &notes,
                           const std::function<bool(const std::string &guid, UnitySkybox &out)> &skybox, UnitySceneLink *link) {
  const UnityDoc *rs = nullptr;
  for (const UnityDoc &d : docs) rs = !rs && d.class_id == 104 ? &d : rs;
  if (!rs) return false;
  if (link) link->ambient_mode = (int)rs->body.num("m_AmbientMode", 0.0), link->ambient_intensity = (float)std::max(0.0, rs->body.num("m_AmbientIntensity", 1.0));
  auto linear = [](double c) { return c <= 1.0 ? srgb_to_linear((float)std::max(0.0, c)) : (float)std::pow(c, 2.2); };  // (HDR past 1 by the same curve)
  auto color = [&](const char *key, Vec3 def) {
    const YamlNode *c = rs->body.get(key);
    return c ? Vec3(linear(c->num("r")), linear(c->num("g")), linear(c->num("b"))) : def;
  };
  const int ambient = (int)rs->body.num("m_AmbientMode", 0.0);
  if (ambient == 1) env.mode = 0, env.sky = color("m_AmbientSkyColor", env.sky), env.equator = color("m_AmbientEquatorColor", env.equator), env.ground = color("m_AmbientGroundColor", env.ground);
  if (ambient == 3 || ambient == 4) env.mode = 3, env.color = color("m_AmbientSkyColor", env.color);  // (round 401: Custom's probe isn't in the file - its sky colour)
  if (ambient == 1 || ambient == 3 || ambient == 4) env.strength = 1.0f;  // (review 395: Unity's Intensity Multiplier is a Skybox source's; a colour is as it is)
  if (ambient == 4) notes.push_back("the scene's Custom ambient probe isn't in the file: its sky colour as a flat ambient");
  env.reflection_intensity = (float)std::max(0.0, rs->body.num("m_ReflectionIntensity", 1.0));
  UnitySkybox sb;
  if (ambient == 0 && rs->body.file_id("m_SkyboxMaterial") && skybox && skybox(rs->body.guid("m_SkyboxMaterial"), sb)) {
    env.strength = (float)std::max(0.0, rs->body.num("m_AmbientIntensity", 1.0));
    skybox_world(sb, env, notes);
  }
  else if (ambient == 0 && rs->body.file_id("m_SkyboxMaterial")) notes.push_back("the scene's ambient is its skybox material's: Blendity's sky stays (set it in the Lighting window)");
  if (link) link->world_mode = env.mode, link->world_key = UnitySceneLink::ambient_key(env);  // (re-check 401: an untouched world writes no ambient back)
  env.fog = rs->body.num("m_Fog", 0.0) != 0.0;
  const int fog = (int)rs->body.num("m_FogMode", 3.0);
  env.fog_mode = fog == 1 ? 0 : fog == 2 ? 1 : 2;  // (round 401: Unity's three modes as they are, by view depth)
  env.fog_density = (float)std::max(0.0, rs->body.num("m_FogDensity", 0.01));
  env.fog_color = env.fog_color_far = color("m_FogColor", env.fog_color);
  env.fog_start = (float)std::max(0.0, rs->body.num("m_LinearFogStart", 0.0));
  env.fog_end = (float)std::max((double)env.fog_start + 0.1, rs->body.num("m_LinearFogEnd", 300.0));
  return true;
}

std::map<int64_t, GameObject *> build_unity_objects(const std::vector<UnityDoc> &docs, Scene &scene, GameObject *parent, UnityImport &ctx,
                                                    UnityTargets *targets) {
  if (!ctx.building) ctx.building = std::make_shared<std::set<std::string>>();
  if (!ctx.total) ctx.total = std::make_shared<int>(0);
  Build b;
  make_objects(docs, scene, ctx, b);
  place_objects(docs, b);
  for (const UnityDoc &d : docs)
    if (d.class_id == 1001 && !d.stripped) instantiate(d, scene, ctx, b);
  map_stripped(docs, b);
  parent_objects(scene, parent, b);
  order_children(docs, scene, b);
  for (const UnityDoc &d : docs) {  // (meshes first: renderers take their model's materials)
    GameObject *g = (d.class_id == 33 || d.class_id == 137) && !d.stripped ? object_at(b.by_fid, d.body.file_id("m_GameObject")) : nullptr;
    if (g) add_mesh(d, g, ctx, b);
  }
  for (const UnityDoc &d : docs) {
    if (structural(d)) continue;
    GameObject *g = object_at(b.by_fid, d.body.file_id("m_GameObject"));
    if (g) add_component(d, g, ctx, b), b.targets[d.file_id] = {g, d.class_id};
    else ctx.dropped++;  // (review 370: never silently - counted, said)
  }
  resolve_avatar_refs(b);  // (round 402: the descriptor's, PhysBones', contacts' and constraints' references, now that every object is made)
  for (const UnityDoc &d : docs) {  // (round 397: a Point Light Volume - after the components: its light may come later in the file)
    GameObject *g = is_script(d, kPointLightVolumeGuid) ? object_at(b.by_fid, d.body.file_id("m_GameObject")) : nullptr;
    if (g) mark_light_volume(d, *g), ctx.light_volumes++;
  }
  for (const UnityDoc &d : docs) {  // (round 395: Contribute GI - a static flag on the GameObject, a renderer's in Blendity)
    GameObject *g = d.class_id == 1 && !d.stripped ? object_at(b.by_fid, d.file_id) : nullptr;
    if (g && d.body.get("m_StaticEditorFlags")) set_static_flags(g, d.body.num("m_StaticEditorFlags"));
  }
  /* Round 399: an object inside a nested prefab as a file around this one names it - Unity's id for it there is its id
   * in the nested prefab XOR the PrefabInstance's, the top bit cleared - so a scene's override or parent on a part of a
   * variant reaches it (SideRail's LowTable, a variant of a model: its name, place and parent had been lost). */
  for (const auto &[pi, parts] : b.instances)
    for (const auto &[fid, t] : parts) b.targets.emplace(unity_nested_file_id(fid, pi), t);
  if (targets) *targets = b.targets;
  if (b.skinned) ctx.notes.push_back("skinned meshes come in unbound to their bones (the bind is a later round)");
  return b.by_fid;
}

}  // namespace bl
