// SPDX-License-Identifier: GPL-2.0-or-later
// Unity prefabs and scenes into Blendity's objects (round 370, task 0347): see unity_scene.h.
#include "unity_scene.h"

#include "../core/core.h"

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
  std::map<int64_t, ObjectMap> instances;                 // (a PrefabInstance's fileID: its objects by their source fileIDs)
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
    b.by_fid[d.file_id] = g;
    ctx.objects++;
  }
}

void place_objects(const std::vector<UnityDoc> &docs, Build &b) {
  for (const UnityDoc &d : docs) {
    GameObject *g = is_transform(d) && !d.stripped ? object_at(b.by_fid, d.body.file_id("m_GameObject")) : nullptr;
    if (!g) continue;
    b.by_fid[d.file_id] = g;
    Transform t;
    t.position = vec3_of(d.body, "m_LocalPosition", Vec3(0.0f));
    t.rotation = quat_of(d.body);
    t.scale = vec3_of(d.body, "m_LocalScale", Vec3(1.0f));
    t.euler_hint = t.rotation.to_euler();
    g->set_local(t);
    b.fathers.push_back({g, d.body.file_id("m_Father")});
  }
}

/* One override of a nested prefab: its transform, name or active state; false for what Blendity doesn't hold. */
bool apply_override(const YamlNode &e, ObjectMap &m, std::set<GameObject *> &turned) {
  GameObject *g = object_at(m, e.file_id("target"));
  const std::string prop = e.str("propertyPath"), value = e.str("value");
  if (!g) return false;
  if (prop == "m_Name") return g->name = value, true;
  if (prop == "m_IsActive") return g->active = std::atof(value.c_str()) != 0.0, true;
  if (prop == "m_RootOrder" || prop.rfind("m_LocalEulerAnglesHint.", 0) == 0) return true;  // (Unity's bookkeeping: the rotation itself says it)
  const size_t dot = prop.rfind('.');
  if (dot == std::string::npos || dot + 2 != prop.size()) return false;
  const std::string field = prop.substr(0, dot);
  const char axis = prop.back();
  const float v = (float)std::atof(value.c_str());
  Transform t = g->local();
  float *slot = nullptr;
  if (field == "m_LocalPosition" || field == "m_LocalScale") {
    Vec3 &vec = field == "m_LocalPosition" ? t.position : t.scale;
    slot = axis == 'x' ? &vec.x : axis == 'y' ? &vec.y : axis == 'z' ? &vec.z : nullptr;
  }
  if (field == "m_LocalRotation") slot = axis == 'x' ? &t.rotation.x : axis == 'y' ? &t.rotation.y : axis == 'z' ? &t.rotation.z : axis == 'w' ? &t.rotation.w : nullptr;
  if (!slot || !std::isfinite(v)) return false;
  *slot = v;
  if (field == "m_LocalRotation") turned.insert(g);
  g->set_local(t);
  return true;
}

void apply_overrides(const YamlNode &instance, ObjectMap &m, UnityImport &ctx) {
  const YamlNode *mod = instance.get("m_Modification");
  const YamlNode *list = mod ? mod->get("m_Modifications") : nullptr;
  const YamlNode *removed = mod ? mod->get("m_RemovedComponents") : nullptr;
  if (removed && !removed->seq.empty()) ctx.notes.push_back("a nested prefab's removed components came back (Unity removes them again)");
  std::set<GameObject *> turned;
  for (const YamlNode &e : list ? list->seq : std::vector<YamlNode>())
    if (!apply_override(e, m, turned)) ctx.overrides_kept++;
  for (GameObject *g : turned) {  // (a rotation's parts set one by one: made whole once all are in)
    Transform t = g->local();
    t.rotation = unit(t.rotation);
    t.euler_hint = t.rotation.to_euler();
    g->set_local(t);
  }
}

/* A nested prefab: its own objects made (by its own fileIDs), its overrides applied, its roots placed under the
 * transform the instance names. */
/* A model placed as a prefab: its nodes as objects under one root; stripped objects and added components fall to it. */
void instantiate_model(const UnityDoc &d, GameObject *root, UnityImport &ctx, Build &b) {
  ctx.prefabs++;
  const YamlNode *mod = d.body.get("m_Modification");
  b.fathers.push_back({root, mod ? mod->file_id("m_TransformParent") : 0});
  b.instance_roots[d.file_id] = root;
  b.instances[d.file_id];
  const YamlNode *list = mod ? mod->get("m_Modifications") : nullptr;
  ctx.overrides_kept += list ? (int)list->seq.size() : 0;  // (its nodes' fileIDs are Unity's own hashes: the overrides stay Unity's)
}

void instantiate(const UnityDoc &d, Scene &scene, UnityImport &ctx, Build &b) {
  const std::string guid = d.body.guid("m_SourcePrefab");
  std::vector<UnityDoc> docs;
  if (ctx.depth >= 8) return ctx.notes.push_back("a prefab nested more than 8 deep was left out");
  if (ctx.building->count(guid)) return ctx.notes.push_back("a prefab that holds itself (GUID " + guid + ") was left out at its second appearance");
  if (*ctx.total > ctx.max_objects) return;
  const bool yaml = ctx.prefab && ctx.prefab(guid, docs);
  GameObject *model = !yaml && ctx.model ? ctx.model(guid, scene) : nullptr;
  if (model) return instantiate_model(d, model, ctx, b);
  if (docs.empty()) return ctx.notes.push_back("a nested prefab (GUID " + guid + ") isn't in the project, or isn't one Blendity reads");
  UnityImport inner;
  inner.mesh = ctx.mesh, inner.script_name = ctx.script_name, inner.prefab = ctx.prefab, inner.model = ctx.model, inner.depth = ctx.depth + 1;
  inner.building = ctx.building, inner.total = ctx.total, inner.max_objects = ctx.max_objects;
  ctx.building->insert(guid);
  ObjectMap m = build_unity_objects(docs, scene, nullptr, inner);
  ctx.building->erase(guid);
  ctx.dropped += inner.dropped;
  ctx.objects += inner.objects, ctx.meshes += inner.meshes, ctx.missing_meshes += inner.missing_meshes, ctx.lights += inner.lights;
  ctx.cameras += inner.cameras, ctx.colliders += inner.colliders, ctx.engine += inner.engine, ctx.prefabs += inner.prefabs + 1;
  ctx.overrides_kept += inner.overrides_kept;
  ctx.notes.insert(ctx.notes.end(), inner.notes.begin(), inner.notes.end());
  apply_overrides(d.body, m, ctx);
  const YamlNode *mod = d.body.get("m_Modification");
  const int64_t parent = mod ? mod->file_id("m_TransformParent") : 0;
  std::set<GameObject *> roots;
  for (const auto &[fid, g] : m)
    if (!g->parent) roots.insert(g);
  for (GameObject *g : roots) b.fathers.push_back({g, parent});
  b.instances[d.file_id] = std::move(m);
}

/* A stripped document stands for an object of a nested prefab: it maps to that object. */
void map_stripped(const std::vector<UnityDoc> &docs, Build &b) {
  for (const UnityDoc &d : docs) {
    if (!d.stripped) continue;
    const int64_t pi = d.body.file_id("m_PrefabInstance");
    const auto inst = b.instances.find(pi);
    GameObject *g = inst == b.instances.end() ? nullptr : object_at(inst->second, d.body.file_id("m_CorrespondingSourceObject"));
    if (!g) g = object_at(b.instance_roots, pi);  // (a model's: its root takes what was added to it)
    if (g) b.by_fid[d.file_id] = g;
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

void add_renderer(const UnityDoc &d, GameObject *g, Build &b) {
  if (!g->get<MeshFilter>()) return;
  MeshRenderer *r = g->get<MeshRenderer>() ? g->get<MeshRenderer>() : g->add<MeshRenderer>();
  r->enabled = d.body.num("m_Enabled", 1.0) != 0.0;
  if (d.class_id == 137 && d.body.get("m_Bones") && !d.body.get("m_Bones")->seq.empty()) b.skinned++;
  const auto mats = b.model_materials.find(g);
  if (mats != b.model_materials.end() && !mats->second.empty()) r->materials = mats->second;  // (the .mat files: task 0348)
}

void add_light(const UnityDoc &d, GameObject *g) {
  static const int kType[] = {2, 0, 1, 3, 3};  // (Unity: Spot, Directional, Point, Rectangle, Disc)
  const int t = (int)d.body.num("m_Type", 2.0);
  Light *l = g->add<Light>();
  l->type = kType[t >= 0 && t <= 4 ? t : 2];
  l->area_shape = t == 4 ? 1 : 0;
  const YamlNode *c = d.body.get("m_Color");
  if (c) l->color = Vec3((float)c->num("r", 1.0), (float)c->num("g", 1.0), (float)c->num("b", 1.0));
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

/* Anything Blendity doesn't hold: kept as it is. */
void add_engine(const UnityDoc &d, GameObject *g, UnityImport &ctx) {
  EngineComponent *e = g->add<EngineComponent>();
  e->engine = "Unity";
  e->script = d.class_id == 114 ? d.body.guid("m_Script") : std::string();
  const std::string name = !e->script.empty() && ctx.script_name ? ctx.script_name(e->script) : std::string();
  const std::string script_fid = std::to_string(d.body.file_id("m_Script"));  // (a DLL's scripts: one GUID, a fileID each)
  e->type = !name.empty() ? name : d.class_id == 114 ? "MonoBehaviour (" + e->script + ":" + script_fid + ")" : d.type;
  e->data = d.header + d.text;  // (the document as written, its header line and line ends unchanged)
  ctx.engine++;
}

void add_component(const UnityDoc &d, GameObject *g, UnityImport &ctx, Build &b) {
  switch (d.class_id) {
    case 23: return add_renderer(d, g, b);
    case 108: return add_light(d, g), (void)ctx.lights++;
    case 20: return add_camera(d, g), (void)ctx.cameras++;
    case 64: case 65: case 135: case 136: return add_collider(d, g, ctx), (void)ctx.colliders++;
    case 33: return;  // (meshes: before)
    case 137: return add_renderer(d, g, b);
    default: return add_engine(d, g, ctx);
  }
}

bool structural(const UnityDoc &d) { return d.class_id == 1 || is_transform(d) || d.class_id == 1001 || d.stripped; }

}  // namespace

std::map<int64_t, GameObject *> build_unity_objects(const std::vector<UnityDoc> &docs, Scene &scene, GameObject *parent, UnityImport &ctx) {
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
    if (g) add_component(d, g, ctx, b);
    else ctx.dropped++;  // (review 370: never silently - counted, said)
  }
  if (b.skinned) ctx.notes.push_back("skinned meshes come in unbound to their bones (the bind is a later round)");
  return b.by_fid;
}

}  // namespace bl
