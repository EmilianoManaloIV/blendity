// SPDX-License-Identifier: GPL-2.0-or-later
// The console: `set` by field name, the command handlers of several rounds and run_console_command, which every
// tool is reachable from (task 0177: moved out of editor.cpp as they were).
#include "../scene/curve.h"
#include "editor.h"
#include "learn.h"
#include "../texture/procedural_texture.h"
#include "../render/texture_bake.h"
#include "../core/core.h"
#include "../core/jobs.h"
#include "../core/launch.h"
#include "../core/releases.h"
#include "../deps/deps.h"
#include "../research/research.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>
#include <cctype>
#include <cstdio>
#include <functional>
#include <sstream>
#include <thread>

namespace bl {

using namespace platform;
using ui::Icon;

/* `set Camera.FocalLength 85`: sets a reflected field by name (letters and
 * digits only, case-insensitive; a unique prefix will do). Enumerations take
 * an index or the option's text. Blender's Python: bpy.data...prop = value. */
namespace {
std::string field_key(const std::string &s) {
  std::string k;
  for (char c : s)
    if (std::isalnum((unsigned char)c)) k += (char)std::tolower((unsigned char)c);
  return k;
}

struct SetFieldReflector : Reflector {
  std::string key, value;
  std::vector<std::string> names;  // pass 1: every field
  std::string target;              // pass 2: the field to set
  bool done = false;
  bool all = true;  // every field, or only the ones shown (matset: a toon field reaches toon materials only)
  bool all_fields() const override { return all; }
  bool hit(const char *n) {
    if (target.empty()) {
      names.push_back(n);
      return false;
    }
    if (done || target != n) return false;
    done = true;
    return true;
  }
  /* A number, or an expression from the field's own value (+=0.1, *=2, as the Inspector takes); one that isn't a
   * number sets nothing and says so. */
  bool number(float current, double &out) {
    if (ui::eval_number(value, out, current)) return true;
    bad = true;
    return false;
  }
  bool bad = false;
  void field(const char *n, float &v, float, float mn, float mx) override {
    double d;
    if (hit(n) && number(v, d)) v = std::max(mn, std::min(mx, (float)d));
  }
  void field(const char *n, int &v, int mn, int mx) override {
    double d;
    if (hit(n) && number((float)v, d)) v = std::max(mn, std::min(mx, (int)std::lround(d)));
  }
  void field(const char *n, bool &v) override {
    if (hit(n)) v = value == "1" || field_key(value) == "true" || field_key(value) == "on";
  }
  void field(const char *n, Vec3 &v) override {
    if (!hit(n)) return;
    float x = v.x, y = v.y, z = v.z;
    if (std::sscanf(value.c_str(), "%f %f %f", &x, &y, &z) == 3) v = {x, y, z};
  }
  void color(const char *n, Vec3 &v) override { field(n, v); }
  void enumeration(const char *n, int &v, const char *const *opts, int count) override {
    if (!hit(n)) return;
    for (int k = 0; k < count; k++)
      if (field_key(opts[k]).rfind(field_key(value), 0) == 0 && !field_key(value).empty()) {
        v = k;
        return;
      }
    if (!value.empty() && std::isdigit((unsigned char)value[0])) v = std::max(0, std::min(count - 1, std::atoi(value.c_str())));
  }
  void text(const char *n, std::string &v) override {
    if (hit(n)) v = value;
  }
  void mesh(const char *, MeshPtr &) override {}
};
}  // namespace

/* views follow on|off (task 0103): the Game view and Camera Preview follow drags as they happen, or catch up on release.
 * views simpler on|off (task 0104): the CPU views' shadows cheaper while you move, exact when you stop. No value: the
 * other way round; anything but "on" is off. */
void Editor::views_command(const std::string &what, const std::string &value) {
  if (what == "simpler") {  // views simpler on|off (task 0104): the CPU views' shadows cheaper while you interact, exact when you stop
    simpler_while_moving_ = value.empty() ? !simpler_while_moving_ : value == "on";
    save_prefs();
    Log::info("Simpler while moving: %s", simpler_while_moving_ ? "on" : "off");
    return;
  }
  if (what != "follow") {
    Log::warn("views follow|simpler on|off");
    return;
  }
  other_views_follow_ = value.empty() ? !other_views_follow_ : value == "on";
  save_prefs();
  Log::info("Other views follow drags: %s", other_views_follow_ ? "on" : "off (they catch up on release)");
}

/* Finds the field (its key exactly, else the first whose key starts with it) and sets it. */
static bool set_field_on(SetFieldReflector &sr, const std::function<void(Reflector &)> &reflect) {
  sr.target.clear();
  sr.names.clear();
  sr.done = false;
  reflect(sr);
  for (int pass = 0; pass < 2 && sr.target.empty(); pass++)
    for (const std::string &n : sr.names)
      if (pass == 0 ? field_key(n) == sr.key : field_key(n).rfind(sr.key, 0) == 0) {
        sr.target = n;
        break;
      }
  if (sr.target.empty() || field_key(sr.target) == "effects") return false;  // the stack's types: use the filter command
  reflect(sr);
  if (sr.bad) Log::warn("set %s: '%s' isn't a number (a number, or +=1, *=2 from its own value)", sr.target.c_str(), sr.value.c_str());
  return sr.done && !sr.bad;
}

/* animemat <preset> (task 0150): the anime preset's texture asset (a new .ptex each time, as newtex makes one; undo
 * takes the material back and leaves the file, as newtex's stays) and a flat-toned Toon material reading it in the
 * selected objects' first slot - a double-sided Cutout for a card. Nothing selected: nothing made. One undo step. */
MaterialPtr Editor::animemat_command(const std::string &preset_in) {
  std::string preset = to_lower(preset_in);
  if (preset.rfind("anime ", 0) != 0) preset = "anime " + preset;  // ("leaves" is "Anime Leaves")
  std::string name;
  for (const std::string &p : texture_preset_names())
    if (to_lower(p) == preset) {
      name = p;
      break;
    }
  if (name.empty()) {
    Log::warn("animemat: Anime Leaves, Grass, Wall, Windows, Roof, Sky, Clouds, Rock or Water");
    return nullptr;
  }
  std::vector<MeshRenderer *> targets;
  for (GameObject *g : selected_objects(false))
    if (MeshRenderer *mr = g->get<MeshRenderer>()) targets.push_back(mr);
  if (targets.empty()) {
    Log::warn("animemat: select objects with a MeshRenderer first");
    return nullptr;
  }
  ProceduralTexturePtr tex = new_texture_asset(name);
  if (!tex) return nullptr;
  /* A toon material in flat tones reading the texture's albedo; a card (leaves, grass, clouds) cuts out by its alpha. */
  MaterialPtr m = make_material_preset("Toon");
  m->name = name;
  m->base_map = TextureRef{tex->asset_path + "#albedo", false};
  m->toon_bands = 1;
  m->toon_softness = 0.0f;
  m->toon_specular_size = 0.0f;
  m->toon_rim_width = 0.0f;
  if (tex->alpha_from_cover) {
    m->surface = (int)MaterialSurface::Cutout;
    m->alpha_clip = 0.5f;
    m->double_sided = true;
  }
  m->touch();
  for (MeshRenderer *mr : targets) {
    if (mr->materials.empty()) mr->materials.resize(1);
    mr->materials[0] = m;
  }
  mark_changed("Anime Material " + name);
  Log::info("animemat: %s (%s) on %zu object(s)", name.c_str(), tex->asset_path.c_str(), targets.size());
  return m;
}

/* customnormals clear|show (task 0149): the selected meshes' own shading normals, cleared (the smooth angle and the
 * sharp edges shade them again) or counted. */
void Editor::customnormals_command(const std::string &verb) {
  size_t meshes = 0, with = 0;
  for (GameObject *g : selected_objects(false)) {
    MeshFilter *mf = g->mesh_filter();
    if (!mf || !mf->mesh) continue;
    meshes++;
    if (!mf->mesh->custom_normals_fit()) continue;
    with++;
    if (verb != "clear") continue;
    Mesh &m = *mesh_make_mutable(mf->mesh);
    m.custom_normals.clear();
    m.touch();
  }
  if (!meshes) {
    Log::warn("customnormals: select a mesh");
    return;
  }
  if (verb == "clear") {
    if (with) mark_changed("Clear Custom Normals");
    Log::info("customnormals: cleared on %zu of %zu mesh(es) (the smooth angle and sharp edges shade them again)", with, meshes);
    return;
  }
  Log::info("customnormals: %zu of %zu selected mesh(es) carry their own shading normals (customnormals clear removes them)", with, meshes);
}

/* Anime Face Normals (task 0149): a sphere proxy fitted to the head and a Normal Transfer reading it, placed before a
 * Skin modifier (the transfer runs once in the rest pose and the skin turns the normals with the bones). */
GameObject *Editor::add_face_normals(GameObject *mesh_obj) {
  MeshFilter *mf = mesh_obj ? mesh_obj->mesh_filter() : nullptr;
  if (!mf || !mf->mesh || mf->mesh->vert_count() == 0) {
    Log::warn("Anime Face Normals: the object has no mesh");
    return nullptr;
  }
  const Mesh &m = *mf->mesh;
  /* The head: the vertices a Head group weights (over a half), else the top sixth of the mesh. */
  int head = -1;
  for (size_t g = 0; g < m.vertex_groups.size() && head < 0; g++) {
    std::string n = m.vertex_groups[g].name;
    for (char &ch : n) ch = (char)std::tolower((unsigned char)ch);
    if (n.find("head") != std::string::npos && n.find("end") == std::string::npos && n.find("top") == std::string::npos && m.vertex_groups[g].weights.size() == m.vert_count())
      head = (int)g;
  }
  AABB all;
  for (const Vec3 &p : m.positions) all.add(p);
  AABB hb;
  for (size_t i = 0; i < m.positions.size(); i++) {
    const bool in = head >= 0 ? m.vertex_groups[(size_t)head].weights[i] > 0.5f : m.positions[i].y >= all.max.y - (all.max.y - all.min.y) / 6.0f;
    if (in) hb.add(m.positions[i]);
  }
  if (!hb.valid()) hb = all;
  const Vec3 ext = hb.extent();  // (half the head's size each way)
  const float radius = std::max(1e-3f, std::max(ext.x, std::max(ext.y, ext.z)));
  std::string group = head >= 0 ? m.vertex_groups[(size_t)head].name : std::string();
  /* The proxy: a sphere of that size at the head's centre, in the mesh's own space (its child), with no renderer. */
  std::string name = mesh_obj->name + " Face Normals";
  for (int k = 1; scene_->find_by_name(name); k++) name = strprintf("%s Face Normals.%03d", mesh_obj->name.c_str(), k);
  GameObject *proxy = scene_->create(name, mesh_obj);
  proxy->add<MeshFilter>()->mesh = primitives::uv_sphere(1.0f, 16, 12);  // (a few hundred triangles: the transfer's search is brute force)
  proxy->set_local_position(hb.center());
  proxy->set_local_scale(Vec3(radius));
  NormalTransferModifier *nt = mesh_obj->add<NormalTransferModifier>();
  nt->object = name;
  nt->vertex_group = group;
  /* Before the skin: the transfer runs once in the rest pose, the skin turns the normals with the head bone. */
  auto &comps = mesh_obj->components;
  for (size_t i = 0; i + 1 < comps.size(); i++)
    if (dynamic_cast<SkinModifier *>(comps[i].get())) {
      std::unique_ptr<Component> moved = std::move(comps.back());
      comps.pop_back();
      comps.insert(comps.begin() + (std::ptrdiff_t)i, std::move(moved));
      break;
    }
  mark_changed("Anime Face Normals");
  Log::info("Anime Face Normals: %s (radius %.3f%s) and a Normal Transfer on %s", name.c_str(), radius, group.empty() ? "" : (", group " + group).c_str(),
            mesh_obj->name.c_str());
  return proxy;
}

namespace {
/* A head bone by its name: the whole word "head" (not Forehead or Headphones) or MMD's 頭. */
bool head_named(const std::string &name) {
  if (name.find("\xe9\xa0\xad") != std::string::npos) return true;  // 頭
  for (const std::string &w : spring::name_words(name))
    if (w == "head") return true;
  return false;
}
/* The head bone of the armature a skin is bound to (the first found), or null. */
GameObject *head_bone_of(Scene &scene, const SkinModifier *sk) {
  GameObject *root = sk ? scene.find_by_name(sk->object) : nullptr;
  if (!root) return nullptr;
  std::vector<GameObject *> stack{root};
  while (!stack.empty()) {
    GameObject *g = stack.back();
    stack.pop_back();
    for (GameObject *c : g->children) stack.push_back(c);
    if (g->get<Bone>() && head_named(g->name)) return g;
  }
  return nullptr;
}
/* True while a bone or a bone above it is off its rest pose (a rig placed against it would float once it returns). */
bool posed(const GameObject *g) {
  for (; g && g->get<Bone>(); g = g->parent) {
    const Bone &b = *g->get<Bone>();
    const Quat q = g->local().rotation, r = b.rest_rotation;
    if (length(g->local().position - b.rest_position) > 1e-4f || std::fabs(q.x * r.x + q.y * r.y + q.z * r.z + q.w * r.w) < 0.99999f) return true;
  }
  return false;
}
/* Which lock each vertex is in (-1 none) and how many locks: the loose parts (hair cards) when there are no more of
 * them than `cards_cap`, else each card in the sector round the scalp its highest vertex falls in (`cards_cap` of
 * them); one shell cut into `sectors`. */
struct HairLocks {
  std::vector<int> vert_lock;
  size_t count = 0;
};
HairLocks assign_locks(const Mesh &m, Vec3 centre, int cards_cap, int sectors) {
  HairLocks out;
  out.vert_lock.assign(m.vert_count(), -1);
  auto sector_of = [&](Vec3 p, int n) {
    const float a = std::atan2(p.z - centre.z, p.x - centre.x);
    return std::min(n - 1, (int)std::floor((a + kPi) / (2.0f * kPi) * (float)n));
  };
  std::vector<int> face_part;
  const size_t parts = meshops::loose_parts(m, face_part);
  if (parts < 2) {
    for (size_t v = 0; v < m.vert_count(); v++) out.vert_lock[v] = sector_of(m.positions[v], sectors);
    out.count = (size_t)sectors;
    return out;
  }
  std::vector<int> part_of(m.vert_count(), -1);
  for (size_t f = 0; f < m.face_count(); f++)
    for (uint32_t k = m.face_offsets[f]; k < m.face_offsets[f + 1]; k++) part_of[m.corner_verts[k]] = face_part[f];
  if (parts <= (size_t)cards_cap) {
    out.vert_lock = part_of, out.count = parts;
    return out;
  }
  std::vector<Vec3> top(parts, Vec3(0.0f, -1e30f, 0.0f));
  for (size_t v = 0; v < m.vert_count(); v++)
    if (part_of[v] >= 0 && m.positions[v].y > top[(size_t)part_of[v]].y) top[(size_t)part_of[v]] = m.positions[v];
  for (size_t v = 0; v < m.vert_count(); v++)
    if (part_of[v] >= 0) out.vert_lock[v] = sector_of(top[(size_t)part_of[v]], cards_cap);
  out.count = (size_t)cards_cap;
  return out;
}
/* A lock's joints: its vertex nearest the scalp point first, then the centroids of `bones - 1` slices along the line to
 * its farthest vertex, then that vertex. */
std::vector<Vec3> lock_joints(const std::vector<Vec3> &pts, Vec3 scalp, int bones) {
  size_t root = 0;
  for (size_t i = 1; i < pts.size(); i++)
    if (length(pts[i] - scalp) < length(pts[root] - scalp)) root = i;
  size_t tip = root;
  for (size_t i = 0; i < pts.size(); i++)
    if (length(pts[i] - pts[root]) > length(pts[tip] - pts[root])) tip = i;
  const Vec3 axis = pts[tip] - pts[root];
  const float len2 = std::max(1e-12f, dot(axis, axis));
  std::vector<Vec3> joints{pts[root]};
  for (int k = 1; k < bones; k++) {
    const float at = (float)k / (float)bones;
    Vec3 sum(0.0f);
    int count = 0;
    for (const Vec3 &p : pts) {
      const float t = dot(p - pts[root], axis) / len2;
      if (std::fabs(t - at) < 0.5f / (float)bones) sum = sum + p, count++;
    }
    joints.push_back(count > 0 ? sum * (1.0f / (float)count) : pts[root] + axis * at);
  }
  joints.push_back(pts[tip]);
  return joints;
}
/* One lock's chain of bones under the rig, joint to joint (a joint pair too close for a bone is skipped), the length
 * in the bone's own units; the first bone, or null when none was made. */
struct HairLock {
  std::vector<Vec3> joints;  // world
  std::vector<GameObject *> bones;
};
GameObject *build_lock_chain(Scene &scene, GameObject *rig, const std::string &prefix, size_t l, HairLock &lock) {
  GameObject *parent = rig, *first = nullptr;
  for (size_t k = 0; k + 1 < lock.joints.size(); k++) {
    const Vec3 d = lock.joints[k + 1] - lock.joints[k];
    if (length(d) <= 1e-5f) continue;
    GameObject *g = scene.create(strprintf("%s Hair%zu_%zu", prefix.c_str(), l + 1, k + 1), parent);
    g->set_local_position(parent->world_matrix().inverse().point(lock.joints[k]));
    g->set_world_rotation(rotation_between(Vec3(0.0f, 1.0f, 0.0f), normalize(d)));
    Bone *b = g->add<Bone>();
    b->length = length(g->world_matrix().inverse().dir(d));  // (a scaled armature above: the length in the bone's units)
    b->set_rest(g->local());
    lock.bones.push_back(g);
    if (!first) first = g;
    parent = g;
  }
  return first;
}
/* The mesh skinned to the hair rig: each vertex to its own lock's bones by where it lies along the lock (between the
 * two nearest joints), the sums 1; the groups of the armature the mesh was skinned to before go (the rig hangs from
 * its head now). */
void bind_hair_skin(Scene &scene, GameObject &mesh_obj, Mesh &m, const HairLocks &hl, const std::vector<HairLock> &made, const std::string &rig_name) {
  SkinModifier *sk = mesh_obj.get<SkinModifier>();
  if (sk) {
    if (GameObject *old = scene.find_by_name(sk->object)) {
      std::vector<ArmatureBone> bs;
      armature_bones(*old, bs);
      for (const ArmatureBone &ab : bs) {
        const int gi = m.find_group(ab.object->name);
        if (gi >= 0) m.vertex_groups.erase(m.vertex_groups.begin() + gi);
      }
    }
  }
  else sk = mesh_obj.add<SkinModifier>();
  sk->object = rig_name;
  std::vector<std::vector<int>> group_of(made.size());  // each lock's bones' groups
  for (size_t l = 0; l < made.size(); l++)
    for (GameObject *b : made[l].bones) {
      group_of[l].push_back((int)m.vertex_groups.size());
      m.vertex_groups.push_back({b->name, std::vector<float>(m.vert_count(), 0.0f)});
    }
  const Mat4 world = mesh_obj.world_matrix();
  for (size_t v = 0; v < m.vert_count(); v++) {
    const int l = hl.vert_lock[v];
    if (l < 0 || (size_t)l >= made.size() || made[(size_t)l].bones.empty()) continue;
    const HairLock &lk = made[(size_t)l];
    const size_t nb = lk.bones.size();
    const Vec3 axis = lk.joints.back() - lk.joints[0];
    const float t = dot(world.point(m.positions[v]) - lk.joints[0], axis) / std::max(1e-12f, dot(axis, axis));
    const float tb = std::max(0.0f, std::min((float)nb - 1e-4f, t * (float)nb));
    const size_t k = (size_t)tb;
    const float f = tb - (float)k;
    m.vertex_groups[(size_t)group_of[(size_t)l][k]].weights[v] += k + 1 < nb ? 1.0f - f : 1.0f;
    if (k + 1 < nb) m.vertex_groups[(size_t)group_of[(size_t)l][k + 1]].weights[v] += f;
  }
  m.touch();
}
}  // namespace

/* Rig Hair from Mesh (task 0153): see editor.h. */
int Editor::rig_hair_from_mesh(GameObject *mesh_obj, int locks, int bones) {
  MeshFilter *mf = mesh_obj ? mesh_obj->mesh_filter() : nullptr;
  if (!mf || !mf->mesh || mf->mesh->face_count() == 0) {
    Log::warn("Rig Hair from Mesh: select a mesh with faces");
    return 0;
  }
  /* No lock count: every card its own lock (up to 64), a shell cut into eight. */
  const int cards_cap = locks > 0 ? std::min(64, locks) : 64, sectors = locks > 0 ? std::min(64, locks) : 8;
  bones = std::max(1, std::min(16, bones));
  const Mesh &m = *mf->mesh;
  AABB box;
  for (const Vec3 &p : m.positions) box.add(p);
  const Vec3 centre = box.center(), scalp(centre.x, box.max.y, centre.z);
  const HairLocks hl = assign_locks(m, centre, cards_cap, sectors);
  /* The hair armature hangs under the character's head bone when the mesh is skinned to one, so it follows the head. */
  const SkinModifier *sk = mesh_obj->get<SkinModifier>();
  GameObject *under = head_bone_of(*scene_, sk);
  if (sk && !under) {
    Log::warn("Rig Hair from Mesh: %s is skinned to %s, which has no bone named like a head (head, Head, 頭): name one, or rig the hair before skinning it", mesh_obj->name.c_str(),
              sk->object.c_str());
    return 0;
  }
  if (under && posed(under)) {
    Log::warn("Rig Hair from Mesh: put the character in its rest pose first (Rigging > Reset Pose), so the rig sits on the skull when the head returns to it");
    return 0;
  }
  const Mat4 world = mesh_obj->world_matrix();
  std::vector<std::vector<Vec3>> lock_pts(hl.count);
  for (size_t v = 0; v < m.vert_count(); v++)
    if (hl.vert_lock[v] >= 0) lock_pts[(size_t)hl.vert_lock[v]].push_back(world.point(m.positions[v]));
  std::string name = mesh_obj->name + " Hair Rig";
  for (int k = 1; scene_->find_by_name(name); k++) name = strprintf("%s Hair Rig.%03d", mesh_obj->name.c_str(), k);
  GameObject *rig = scene_->create(name, under ? under : mesh_obj->parent);
  rig->add<Armature>();
  const Vec3 scalp_w = world.point(scalp);
  rig->set_local_position(under ? under->world_matrix().inverse().point(scalp_w) : scalp_w);
  std::vector<HairLock> made(hl.count);
  int chains = 0;
  for (size_t l = 0; l < hl.count; l++) {
    if (lock_pts[l].size() < 2) continue;
    made[l].joints = lock_joints(lock_pts[l], scalp_w, bones);
    GameObject *first = build_lock_chain(*scene_, rig, mesh_obj->name, l, made[l]);
    if (!first) continue;
    spring::add(*first, 0);
    chains++;
  }
  if (!chains) {
    scene_->destroy(rig);
    Log::warn("Rig Hair from Mesh: no lock long enough for a bone");
    return 0;
  }
  bind_hair_skin(*scene_, *mesh_obj, *mesh_make_mutable(mf->mesh), hl, made, name);
  mark_changed("Rig Hair from Mesh");
  Log::info("Rig Hair from Mesh: %d lock(s) of up to %d bone(s) under %s%s, the mesh skinned to them, Spring Bones on each", chains, bones, name.c_str(),
            under ? (" (under " + under->name + ")").c_str() : "");
  return chains;
}


/* toonstyle <style> (task 0148): a toon style's preset on every selected material (the Materials window's selection
 * while it is open, else the selected objects' materials), making each a Toon material. One undo step. */
void Editor::toonstyle_command(const std::string &name) {
  const int style = toon_style_index(name);
  if (style < 0) {
    std::string names;
    for (const std::string &s : toon_style_names()) names += (names.empty() ? "" : ", ") + s;
    Log::warn("toonstyle: %s", names.c_str());
    return;
  }
  const bool from_window = dock_find(WindowKind::Materials) != nullptr && !material_multi_.items.empty();
  std::vector<MaterialPtr> targets = from_window ? material_multi_.items : std::vector<MaterialPtr>();
  if (!from_window)
    for (GameObject *g : selected_objects(false))
      if (auto *mr = g->get<MeshRenderer>())
        for (const MaterialPtr &m : mr->materials)
          if (m && std::find(targets.begin(), targets.end(), m) == targets.end()) targets.push_back(m);
  if (targets.empty()) {
    Log::warn("toonstyle: no material selected (select some in the Materials window, or objects that use them)");
    return;
  }
  for (const MaterialPtr &m : targets) apply_toon_style(*m, style);
  mark_changed("Toon Style " + toon_style_names()[(size_t)style]);
  Log::info("Toon style %s on %zu material(s)", toon_style_names()[(size_t)style].c_str(), targets.size());
}

/* matset <Field> <value...> (task 0147): the field on every selected material - the Materials window's selection,
 * else the selected objects' materials - where it applies (a toon field reaches the toon ones). One undo step. */
void Editor::matset_command(const std::vector<std::string> &t, const std::string &line) {
  if (t.size() < 3) {
    Log::warn("matset <Field> <value> (e.g. matset ToonThreshold 0.4): the selected materials");
    return;
  }
  /* The Materials window's selection while the window is open (it keeps its selection when closed), else the
   * selected objects' materials. */
  const bool from_window = dock_find(WindowKind::Materials) != nullptr && !material_multi_.items.empty();
  std::vector<MaterialPtr> targets = from_window ? material_multi_.items : std::vector<MaterialPtr>();
  if (!from_window)
    for (GameObject *g : selected_objects(false))
      if (auto *mr = g->get<MeshRenderer>())
        for (const MaterialPtr &m : mr->materials)
          if (m && std::find(targets.begin(), targets.end(), m) == targets.end()) targets.push_back(m);
  if (targets.empty()) {
    Log::warn("matset: no material selected (select some in the Materials window, or objects that use them)");
    return;
  }
  const std::string key = field_key(t[1]);
  if (key == "name") {
    Log::warn("matset: a name is each material's own (its file's); rename them one at a time");
    return;
  }
  /* One field for all of them: the exact name shown on any of them, else the first whose key starts with it - never a
   * different field per material. */
  SetFieldReflector sr;
  sr.all = false;
  sr.key = key;
  std::vector<std::string> shown;
  for (const MaterialPtr &m : targets) {
    sr.target.clear(), sr.names.clear();
    m->reflect(sr);
    for (const std::string &nm : sr.names)
      if (std::find(shown.begin(), shown.end(), nm) == shown.end()) shown.push_back(nm);
  }
  std::string field;
  for (int pass = 0; pass < 2 && field.empty(); pass++)
    for (const std::string &nm : shown)
      if (pass == 0 ? field_key(nm) == key : field_key(nm).rfind(key, 0) == 0) {
        field = nm;
        break;
      }
  if (field.empty()) {
    sr.all = true;  // is it a field at all, hidden on all of them (a toon field on PBR materials)?
    sr.target.clear(), sr.names.clear();
    targets[0]->reflect(sr);
    bool exists = false;
    for (const std::string &nm : sr.names) exists = exists || field_key(nm) == key || field_key(nm).rfind(key, 0) == 0;
    if (exists) Log::warn("matset: '%s' isn't shown on any selected material (a toon field needs Shading Model Toon, an axis field an Axis Mode)", t[1].c_str());
    else Log::warn("matset: no material field '%s'", t[1].c_str());
    return;
  }
  const size_t at = line.find(t[2], line.find(t[1], t[0].size()) + t[1].size());
  sr.all = false;
  sr.value = at == std::string::npos ? "" : line.substr(at);
  size_t n = 0;
  for (const MaterialPtr &m : targets) {
    sr.target = field;
    sr.done = false;
    sr.bad = false;
    m->reflect(sr);
    if (sr.bad) {
      Log::warn("matset %s: '%s' isn't a number (a number, or +=1, *=2 from each one's own value)", field.c_str(), sr.value.c_str());
      return;
    }
    if (!sr.done) continue;
    m->touch();
    n++;
  }
  if (!n) {
    Log::warn("matset: '%s' isn't shown on any selected material", field.c_str());
    return;
  }
  mark_changed(strprintf("Set %s on %zu Materials", field.c_str(), n));
  Log::info("matset %s: %zu of the %zu %s", field.c_str(), n, targets.size(), from_window ? "selected in the Materials window" : "selected objects' materials");
}

bool Editor::set_field_command(const std::string &path, const std::string &value) {
  size_t dot = path.find('.');
  if (dot == std::string::npos) {
    Log::warn("set: use set <Component>.<Field> <value>, e.g. set Camera.FStop 1.4 (or Render.Samples, World.Strength)");
    return false;
  }
  const std::string owner = field_key(path.substr(0, dot));
  SetFieldReflector sr;
  sr.key = field_key(path.substr(dot + 1));
  sr.value = value;
  auto apply = [&](const std::function<void(Reflector &)> &reflect) { return set_field_on(sr, reflect); };
  int n = 0;
  if (owner == "render") n += apply([&](Reflector &r) { scene_->render.reflect(r); });
  else if (owner == "world") n += apply([&](Reflector &r) { scene_->environment.reflect(r); });
  else if (owner == "lighting") n += apply([&](Reflector &r) { scene_->lighting.reflect(r); });
  else
    for (GameObject *g : selected_objects(false))
      for (auto &c : g->components)
        if (field_key(c->type_name()) == owner) n += apply([&](Reflector &r) { c->reflect(r); });
  if (!n)  // a filter effect by its type ("set RetroConsole.Width 160"; task 0004's "RetroConsoleFilter" too)
    for (GameObject *g : selected_objects(false))
      if (auto *cf = g->get<CameraFilters>())
        for (auto &e : cf->effects)
          if (field_key(e->type_name()) == owner || (owner == "retroconsolefilter" && dynamic_cast<RetroConsoleFilter *>(e.get()))) {
            n += apply([&](Reflector &r) { e->reflect(r); });
            break;
          }
  if (!n) {
    Log::warn("set: no field '%s' on %s (selected objects)", path.substr(dot + 1).c_str(), path.substr(0, dot).c_str());
    return false;
  }
  mark_changed("Set " + path);
  return true;
}

/* matdelete <name>[, <name>...] / texdelete <name>[, <name>...]: delete material or texture assets by name, at once
 * (task 0053; the windows ask first). Names are comma-separated, so they may hold spaces ("Wood Floor"). The scene side
 * is one undo step. */
void Editor::delete_assets_command(const std::string &cmd, const std::string &line) {
  const size_t at = line.find(cmd);
  const std::vector<std::string> names = split_list(at == std::string::npos ? std::string() : line.substr(at + cmd.size()), ',');
  /* Each kind deletes one by name, or says there is none. */
  auto delete_material_named = [this](const std::string &name) {
    const MaterialPtr m = material_by_name(name);
    if (m) delete_material(m);
    return m != nullptr;
  };
  auto delete_texture_named = [this](const std::string &name) {
    const ProceduralTexturePtr t = texture_by_name(name);
    if (t) delete_texture_asset(t);
    return t != nullptr;
  };
  const bool materials = cmd == "matdelete";
  int n = 0;
  for (const std::string &name : names) {
    if (materials ? delete_material_named(name) : delete_texture_named(name)) n++;
    else Log::warn("%s: no %s named '%s'", cmd.c_str(), materials ? "material" : "texture", name.c_str());
  }
  Log::info("%s: %d deleted", cmd.c_str(), n);
}

/* The user manual (task 0060): the project's copy when it has one (a source checkout), else GitHub's. */
void Editor::open_manual() {
  const std::string local = fs::join(project_root_, "docs/MANUAL.md");
  const bool here = fs::exists(local);
  fs::open_external(here ? local : std::string("https://github.com/EmilianoManaloIV/blendity/blob/main/docs/MANUAL.md"));
  Log::info("Manual: %s (open it with any Markdown viewer or a text editor)", here ? local.c_str() : "on GitHub");
}

void Editor::run_console_command(const std::string &line) {
  auto t = split_ws(line);
  if (t.empty()) return;
  Log::info("> %s", line.c_str());
  const std::string &c = t[0];
  auto arg = [&](size_t i, const std::string &def) { return t.size() > i ? t[i] : def; };
  if (c == "layout") {
    /* layout <name> | layout save <name> | layout delete <name> (task 0055); a raw dock string still works */
    const std::string rest = line.substr(line.find(' ') + 1);
    if (to_lower(arg(1, "")) == "save" && t.size() > 2) save_user_layout(rest.substr(rest.find(' ') + 1));
    else if (to_lower(arg(1, "")) == "delete" && t.size() > 2) delete_user_layout(rest.substr(rest.find(' ') + 1));
    else if (!apply_workspace(rest)) {
      if (rest.size() > 1 && rest[1] == '(') {  // a raw dock string: every tool shows
        dock_reset(rest);
        focus_ = FocusAll;
        workspace_ = "Custom";
      }
      else Log::warn("layout: no layout named '%s' (Window > Layouts lists them)", rest.c_str());
    }
    return;
  }
  if (c == "alltools") {  // alltools on|off (task 0055)
    show_all_tools_ = to_lower(arg(1, "on")) != "off";
    return;
  }
  if (c == "refimage") {  // refimage <path> [front|side|top] (task 0055); the path may hold spaces
    std::string rest = line.size() > c.size() ? line.substr(line.find(c) + c.size() + 1) : std::string(), view = "front";
    for (const char *v : {" front", " side", " top"}) {
      const size_t l = std::strlen(v);
      if (rest.size() > l && to_lower(rest.substr(rest.size() - l)) == v) {
        view = v + 1;
        rest.resize(rest.size() - l);
        break;
      }
    }
    create_reference_image(rest, view);
    return;
  }
  if (c == "matdelete" || c == "texdelete") {
    delete_assets_command(c, line);
    return;
  }
  if (c == "theme") {  // theme <Dark|Light|Emiliano Light|Emiliano Dark> (task 0073)
    std::string n;
    for (size_t i = 1; i < t.size(); i++) n += (i > 1 ? " " : "") + t[i];
    if (n.empty()) Log::info("Theme: %s (Dark, Light, Emiliano Light, Emiliano Dark)", theme_name_.c_str());
    else set_theme(n);
    return;
  }
  if (c == "update") return update_command(t);  // update check | history | install <tag> | auto on|off | previews on|off (task 0079)
  if (c == "moonray") return moonray_command(t);  // moonray status | install [archive] | remove | folder [path] | check (task 0112)
  if (c == "body") return body_command(t);
  if (c == "spring") return spring_command(t);
  if (c == "autopose") return autopose_command(t);  // autopose on|off | drag | lock | unlock (task 0122)
  if (c == "ai") return ai_command(t);              // ai status | install | remove | check | span | style | variation | apply (tasks 0129-0130)
  if (c == "import" && t.size() > 1) {  // import <model file> [asis|pbr|classic|guiltygear|soft|painterly|manga]: as dropped (copied into Assets); a shading word imports at once with it (task 0152)
    int shading = t.size() > 3 ? import_shading_index(t[t.size() - 2] + " " + t.back()) : -1;  // ("guilty gear", "soft anime")
    size_t words = shading >= 0 ? 2 : 0;
    if (shading < 0 && t.size() > 2) shading = import_shading_index(t.back()), words = shading >= 0 ? 1 : 0;
    std::string p;
    for (size_t i = 1; i + words < t.size(); i++) p += (i > 1 ? " " : "") + t[i];
    if (shading >= 0) return import_with_shading(p, true, shading);
    return import_model_file(p, true);
  }
  if (c == "assist") return assist_command(t);      // assist list | on <name> | off <name> (task 0124)
  if (c == "shape") return shape_command(t);        // shape list | add <name> [amount] | set <name> <weight> | remove <name> (task 0135)
  if (c == "walk") return walk_command(t);          // walk cycle | set | save | load | presets (task 0125)  // spring find | apply | add | remove | label | ignore | bake (task 0120)  // body show on|off | mass [kg] | com [frame] (task 0115)
  if (c == "renderanim") return render_animation_command(t);  // renderanim [first last] [fps] | stop | ffmpeg [path] (task 0074)
  if (c == "feel") {  // feel <Unity|Unreal|Blender|Maya>: keys and Scene view navigation together (task 0063)
    if (t.size() < 2) return Log::info("Editor feel: %s (feel unity|unreal|blender|maya)", kEditorFeels[(int)nav_style_]);
    set_editor_feel(t[1]);
    return;
  }
  if (c == "seq") {
    seq_command(t);
    return;
  }
  if (c == "anim") {
    anim_command(t);
    return;
  }
  if (c == "manual") {
    open_manual();
    return;
  }
  if (c == "autorig") {
    autorig_command(t);
    return;
  }
  if ((c == "ik" || c == "fk") && rig_control_command(t)) return;  // ik remove, fk add|remove (task 0067)
  if (c == "bone" || c == "armature" || c == "skin" || c == "ik" || c == "rig") {
    rig_command(t);
    return;
  }
  if (c == "overlay") {
    overlay_command(t);
    return;
  }
  if (c == "matset") {
    matset_command(t, line);
    return;
  }
  if (c == "animemat") {  // animemat <Anime preset>: the texture and a toon material on the selection (task 0150)
    std::string preset;
    for (size_t i = 1; i < t.size(); i++) preset += (i > 1 ? " " : "") + t[i];
    animemat_command(preset);
    return;
  }
  if (c == "customnormals") {  // customnormals clear|show: the selected meshes' own shading normals (task 0149)
    customnormals_command(t.size() > 1 ? t[1] : "show");
    return;
  }
  if (c == "hair") {  // hair rig [locks] [bones]: chains of spring bones from the selected hair mesh (task 0153)
    if (t.size() < 2 || to_lower(t[1]) != "rig") Log::warn("hair rig [locks] [bones]");
    else if (GameObject *mesh_obj = rig_mesh()) rig_hair_from_mesh(mesh_obj, t.size() > 2 ? std::atoi(t[2].c_str()) : 0, t.size() > 3 ? std::atoi(t[3].c_str()) : 3);
    else Log::warn("hair rig: select a mesh");
    return;
  }
  if (c == "facenormals") {  // facenormals: a head proxy and a Normal Transfer on the selected mesh (task 0149)
    GameObject *mesh_obj = rig_mesh();
    if (!mesh_obj) Log::warn("facenormals: select a mesh");
    else add_face_normals(mesh_obj);
    return;
  }
  if (c == "toonstyle") {  // toonstyle <Classic|Guilty Gear|Soft Anime|Painterly|Manga>: the selected materials (task 0148)
    std::string name;
    for (size_t i = 1; i < t.size(); i++) name += (i > 1 ? " " : "") + t[i];
    toonstyle_command(name);
    return;
  }
  if (c == "weightpaint" || c == "wpbrush" || c == "wpfalloff" || c == "wpmirror" || c == "wpfront" || c == "wpstroke" || c == "wpnormalize" ||
      c == "wpclean") {
    weight_paint_command(t);
    return;
  }
  if (c == "vgroup") {
    /* vgroup add|remove|rename|assign|unassign|select|deselect|active <name> [weight | new name] (task 0045). */
    vgroup_op(to_lower(arg(1, "")), arg(2, ""), arg(3, ""), (float)std::atof(arg(3, "1").c_str()));
    return;
  }
  if (c == "meshdeform") {
    /* meshdeform bind|unbind [object]: the first Mesh Deform modifier on the object (the active one by default). Here,
     * before the long chain below: it is at the compiler's nesting limit. */
    std::string name;
    for (size_t i = 2; i < t.size(); i++) name += (i > 2 ? " " : "") + t[i];
    GameObject *g = name.empty() ? active_object() : scene_->find_by_name(name);
    MeshDeformModifier *md = g ? g->get<MeshDeformModifier>() : nullptr;
    const std::string verb = to_lower(arg(1, "bind"));
    if (verb != "bind" && verb != "unbind") Log::warn("meshdeform: bind | unbind [object]");
    else if (!md) Log::warn("meshdeform: %s has no Mesh Deform modifier", g ? g->name.c_str() : "no object");
    else mesh_deform_bind(g, md, verb == "bind");
    return;
  }
  if (c == "help") {
    Log::info("Commands: help, clear, create <Cube|Sphere|...> [count], stress <count> [Cube|Sphere|Icosphere|Torus], select <name>,");
    Log::info("  delete, subdivide [levels], smooth, stats, play, stop, layout <Default|2 by 3|Tall|Wide|Learning>, scale <1.0>,");
    Log::info("  screenshot [file.png], bench [frames], lesson <n>, references, window <name>, tool <move|rotate|...>,");
    Log::info("  edit [vertex|edge|face|off] [all], camera <yaw> <pitch> <dist> [px py pz], shading <wire|solid|shaded|rendered|both>,");
    Log::info("  fsel <faces...> | fsel facing <x y z>, redo <param> <value> (adjust last operation),");
    Log::info("  material <Solid|Transparent|Cutout|Glass|Frosted Glass|Metal|Emissive|Unlit>, preview (quick render),");
    Log::info("  vsel <verts...>, editop <extrude|inset|bevel|bridge|push_through|fuse|subdivide_edges|connect|dissolve|collapse|fill|merge_center|recalc_normals|loopcut|select_loop|delete>,");
    Log::info("  loopcuts <n> [slide], proportional <on|off> [radius], component <Name>, applymods, uv <op>, seam <verts...>, libs");
    Log::info("  set <Component>.<Field> <value> (e.g. set Camera.FStop 1.4, set Render.Samples 64), origin <bounds|median|surface|volume|bottom|selection|pivot|geometry|point x y z>");
    Log::info("  curve|points add|move|delete|select <object> [index] [x y z] (a Curve's or Lattice's points), create Curve: Bezier|Circle|Path, create Lattice");
    Log::info("  meshdeform bind|unbind [object] (a Mesh Deform modifier's cage)");
    Log::info("  overlay list | all|bones|controls|icons|points|origins|footprints|outline|grid|gizmos|stats [on|off] (the Scene view's layers)");
    Log::info("  matset <Field> <value> (the selected materials, where the field applies: matset ToonThreshold 0.4)");
    Log::info("  toonstyle <Classic|Guilty Gear|Soft Anime|Painterly|Manga> (the selected materials' toon preset)");
    Log::info("  bone add <parent> [length], armature reset|apply [root], skin bind [object] [inpaint|envelope], skin transfer <body> [mesh] (rigging)");
    Log::info("  autorig humanoid|quadruped, autorig mark <marker> <x> <y> <z>, autorig build|cancel (the marker auto-rig)");
    Log::info("  anim new|key|interp|frame|preview|record|play|stop (the Animation window); manual (the user manual)");
    Log::info("  seq new|add|move|trim|speed|rename|remove|track|trigger|jump|frame|preview|play|stop (the Sequencer window)");
    Log::info("  vgroup add|remove|rename|assign|unassign|select|deselect|active <name> [weight | new name] (vertex groups)");
    Log::info("  weightpaint on|off, wpbrush add|subtract|draw|blur [radius] [strength] [weight], wpfalloff <curve>, wpmirror on|off,");
    Log::info("  wpfront on|off, wpstroke x y x y ..., wpnormalize, wpclean [threshold] (Weight Paint)");
  }
  else if (c == "clear") Log::clear();
  else if (c == "create") {
    /* create <kind words...> [count]: "create Spot Light", "create Cube 3" */
    std::string kind;
    int n = 1;
    for (size_t i = 1; i < t.size(); i++) {
      if (i + 1 == t.size() && !t[i].empty() && std::isdigit((unsigned char)t[i][0])) n = std::max(1, std::atoi(t[i].c_str()));
      else kind += (kind.empty() ? "" : " ") + t[i];
    }
    for (int i = 0; i < n; i++) create_object(kind.empty() ? "Cube" : kind);
  }
  else if (c == "stress") spawn_stress_grid(std::max(1, std::atoi(arg(1, "1000").c_str())), arg(2, "Sphere"));
  else if (c == "select") {
    if (GameObject *g = scene_->find_by_name(line.substr(line.find(' ') + 1))) select(g->id);
    else Log::warn("No object named that");
  }
  else if (c == "delete") delete_selected();
  else if (c == "subdivide") {
    int n = std::max(1, std::atoi(arg(1, "1").c_str()));
    for (int i = 0; i < n; i++) mesh_op("subdivide");
  }
  else if (c == "smooth") mesh_op("smooth");
  else if (c == "stats") {
    size_t verts = 0, faces = 0;
    scene_->for_each([&](GameObject &g) {
      if (const Mesh *m = g.evaluated_mesh()) { verts += m->vert_count(); faces += m->face_count(); }
    });
    Log::info("%zu objects, %zu verts, %zu faces, scene memory ~%s, undo steps %zu", scene_->object_count(), verts, faces,
              format_bytes(scene_->memory_bytes()).c_str(), undo_.size());
  }
  else if (c == "play") enter_play();
  else if (c == "stop") exit_play();
  else if (c == "scale") { ui_scale_pref_ = (float)std::atof(arg(1, "0").c_str()); apply_ui_scale(); }
  else if (c == "screenshot") screenshot(t.size() > 1 ? line.substr(line.find(' ') + 1) : "");
  else if (c == "lesson") { lesson_ = std::atoi(arg(1, "0").c_str()); dock_open(WindowKind::Learn); }
  else if (c == "research" || c == "references") open_references();  // (the Research window is retired: task 0034)
  else if (c == "window") {
    std::string w = line.substr(line.find(' ') + 1);
    for (int k = 0; k < (int)WindowKind::Count; k++)
      if (to_lower(w) == to_lower(window_title((WindowKind)k))) dock_open((WindowKind)k);
  }
  else if (c == "tool") {
    std::string w = to_lower(arg(1, "move"));
    tool_ = w == "view" ? Tool::View : w == "rotate" ? Tool::Rotate : w == "scale" ? Tool::Scale : w == "transform" ? Tool::Transform
            : Tool::Move;
  }
  else if (c == "edit" && arg(1, "") == "off") {
    if (edit_mode_) exit_edit_mode();
  }
  else if (c == "edit") {
    if (!edit_mode_) enter_edit_mode();
    if (arg(1, "") == "face") elem_ = EditElement::Face;
    if (arg(1, "") == "edge") elem_ = EditElement::Edge;
    if (arg(1, "") == "vertex") elem_ = EditElement::Vertex;
    if (arg(1, "") == "all" || arg(2, "") == "all") edit_select_all(true);
  }
  else if (c == "pickfocus") {
    /* pickfocus: the selected Camera's focus eyedropper (then click in the Scene view) */
    GameObject *a = active_object();
    if (a && (a->get<Camera>() || a->get<Light>())) focus_pick_cam_ = a->id;  // a Light: aim it at the clicked point
    else Log::warn("pickfocus: select a Camera or a Light first");
  }
  else if (c == "export") {
    /* export <obj|fbx|glb|gltf|stl|ply|usda> [all] [path]: the selection (or the whole scene) */
    std::string fmt = to_lower(arg(1, "obj"));
    if (fmt.empty() || fmt[0] != '.') fmt = "." + fmt;
    const int fi = export_format_from_extension(fmt);
    if (fi < 0) Log::warn("export: formats are obj, fbx, glb, gltf, stl, ply, usda");
    else {
      ExportOptions o = export_defaults(fi);
      size_t at = 2;
      o.selection_only = !(t.size() > at && to_lower(t[at]) == "all");
      if (!o.selection_only) at++;
      if (t.size() > at && to_lower(t[at]) == "ascii") o.ascii = true, at++;
      std::string path;
      for (size_t k = at; k < t.size(); k++) path += (path.empty() ? "" : " ") + t[k];
      export_model(o, path);
    }
  }
  else if (c == "join") join_selected();
  else if (c == "baketex") bake_object_textures(std::atoi(arg(1, "1024").c_str()), to_lower(arg(2, "")) == "keepuvs");  // baketex [size] [keepuvs]
  else if (c == "meshop") mesh_op(arg(1, ""));
  else if (c == "drawfillet") draw_fillet_ = std::max(0.0f, (float)std::atof(arg(1, "0").c_str()));  // corner radius of drawn shapes
  else if (c == "pilot") toggle_pilot_camera();  // fly the selected camera with the Scene view
  else if (c == "alignview") align_camera_to_view();
  else if (c == "droponsurface") {
    /* droponsurface <x> <y>: drop the selection onto the surface under that Scene view pixel */
    drop_objects_on_surface(selected_objects(true), std::atoi(arg(1, "0").c_str()), std::atoi(arg(2, "0").c_str()));
  }
  else if (c == "surfacealign") surface_align_ = to_lower(arg(1, surface_align_ ? "off" : "on")) == "on";  // meshop <apply_transform|mirror_x|...>: an object-level mesh operator
  else if (c == "aimlight") {
    /* aimlight <x> <y> <z>: turn the selected light to shine at that point */
    GameObject *a = active_object();
    if (a && a->get<Light>())
      aim_light(*a, Vec3((float)std::atof(arg(1, "0").c_str()), (float)std::atof(arg(2, "0").c_str()), (float)std::atof(arg(3, "0").c_str())));
    else Log::warn("aimlight: select a Light first");
  }
  else if (c == "newmat") {
    /* newmat [name]: a Material asset in Assets/Materials (a copy of the selection's first material) */
    GameObject *a = active_object();
    MaterialPtr src = a && a->get<MeshRenderer>() && !a->get<MeshRenderer>()->materials.empty() ? a->get<MeshRenderer>()->materials[0] : nullptr;
    MaterialPtr base = src ? std::make_shared<Material>(*src) : make_material("New Material", Vec3(0.8f));
    if (t.size() > 1) base->name = line.substr(line.find(' ') + 1);
    base->asset_path.clear();
    new_material_asset(base, false);
  }
  else if (c == "bevelprofile") bevel_profile_ = clampf((float)std::atof(arg(1, "0.5").c_str()), 0.0f, 1.0f);  // bevelprofile <0..1>
  else if (c == "pushpull") {
    /* pushpull individual on|off: Push/Pull Each Face */
    if (to_lower(arg(1, "")) == "individual") pp_individual_ = to_lower(arg(2, "on")) != "off";
  }
  else if (c == "hs") {
    /* hs grid <cols> <rows> | pipe <radius> <sides> | array <count> <spacing> <axis 0-2> | taper <distance> <scale> |
     * recess <border> <depth>: the hard-surface tools' settings */
    const std::string s = to_lower(arg(1, ""));
    auto f = [&](int i, float d) { return t.size() > (size_t)i ? (float)std::atof(t[(size_t)i].c_str()) : d; };
    if (s == "grid") grid_cols_ = (int)f(2, 4), grid_rows_ = (int)f(3, 4);
    else if (s == "pipe") pipe_radius_ = f(2, 0.05f), pipe_sides_ = (int)f(3, 12);
    else if (s == "array") array_count_ = (int)f(2, 3), array_spacing_ = f(3, 0.5f), array_axis_ = (int)f(4, 0);
    else if (s == "taper") taper_distance_ = f(2, 0.3f), taper_scale_ = f(3, 0.6f);
    else if (s == "recess") recess_border_ = f(2, 0.05f), recess_depth_ = f(3, 0.05f);
    else Log::warn("hs grid|pipe|array|taper|recess <values>");
  }
  else if (c == "overlaps") {
    /* overlaps [select|merge|show on|off]: overlapping vertices and edges of the mesh being edited */
    const std::string s = to_lower(arg(1, ""));
    if (s == "select") overlap_select();
    else if (s == "merge") overlap_merge();
    else if (s == "show") overlap_show_ = to_lower(arg(2, "on")) != "off";
    else {
      overlap_refresh(true);
      Log::info("%zu overlapping vertex pair(s), %zu overlapping edge pair(s)", overlap_.verts.size(), overlap_.edges.size());
    }
  }
  else if (c == "deletemat") {
    /* deletemat <name | Assets/Materials/x.mat>: delete a material without asking (slots emptied, the file trashed) */
    const std::string what = t.size() > 1 ? line.substr(line.find(' ') + 1) : std::string();
    MaterialPtr m = what.find(".mat") != std::string::npos ? material_asset(what) : nullptr;
    if (!m)
      for (const MaterialPtr &c2 : pickable_materials(nullptr))
        if (c2->name == what) m = c2;
    if (m) delete_material(m);
    else Log::warn("deletemat: no material %s", what.c_str());
  }
  else if (c == "assignmat") {
    /* assignmat <Assets/Materials/x.mat> [slot]: put the asset in a slot of every selected object */
    MaterialPtr m = material_asset(arg(1, ""));
    if (!m) Log::warn("assignmat: no material asset %s", arg(1, "").c_str());
    else
      for (GameObject *g : selected_objects(false)) assign_material(g, std::atoi(arg(2, "0").c_str()), m);
  }
  else if (c == "removeslot") {
    if (GameObject *a = active_object()) remove_material_slot(a, std::atoi(arg(1, "0").c_str()));
  }
  else if (c == "removeunusedslots") {
    if (GameObject *a = active_object()) remove_unused_material_slots(a);
  }
  else if (c == "draw") {
    /* draw <polyline|rectangle|circle|arc|polygon|guide> [segments / sides] */
    const std::string s = to_lower(arg(1, "polyline"));
    int k = 0;
    for (int i = 0; i < kDrawShapeCount; i++)
      if (s == to_lower(kDrawShapes[i])) k = i;
    if (t.size() > 2) (k == 4 ? draw_sides_ : draw_segments_) = std::max(3, std::atoi(t[2].c_str()));
    draw_begin(k);
  }
  else if (c == "drawmode") {
    /* drawmode rect <corner|center|3point>, drawmode circle <center|2point|3point>, drawmode square <on|off> */
    const std::string s = to_lower(arg(1, "")), v = to_lower(arg(2, ""));
    if (s == "square") draw_uniform_ = v != "off";
    else if (s == "axes") draw_global_axes_ = v == "global" || v == "world";
    else if (s == "perp") draw_perp_snap_ = v != "off";  // drawmode perp on|off
    else if (s == "facecenter") draw_face_center_ = v != "off";
    else if (s == "rect" || s == "rectangle") draw_rect_mode_ = v == "center" ? 1 : v == "3point" ? 2 : 0;
    else if (s == "circle" || s == "polygon") draw_circle_mode_ = v == "2point" ? 1 : v == "3point" ? 2 : 0;
    else Log::warn("drawmode rect <corner|center|3point> | circle <center|2point|3point> | square <on|off>");
  }
  else if (c == "raster" && t.size() >= 3) {
    /* raster simd|uniform on|off: the shader's 8-wide path (task 0091) and plain materials once per triangle (0088) */
    const bool on = to_lower(t[2]) != "off";
    const std::string what = to_lower(t[1]);
    if (what == "simd") raster_opt_.simd_shading = on;
    else if (what == "uniform") raster_opt_.uniform_shading = on;
    else Log::warn("raster simd|uniform on|off");
    ui_.redraw = true;
  }
  else if (c == "views" || (c == "viewport" && to_lower(arg(1, "")) == "device")) {
    /* views follow|simpler on|off (tasks 0103, 0104): in this branch, not one of its own (MSVC's nesting limit) */
    if (c == "views") {
      views_command(to_lower(arg(1, "")), to_lower(arg(2, "")));
      return;
    }
    /* viewport device auto|cpu|gpu (task 0096): where the views' 3D is drawn */
    const std::string d = to_lower(arg(2, "auto"));
    if (d != "auto" && d != "cpu" && d != "gpu") Log::warn("viewport device auto|cpu|gpu (not '%s')", d.c_str());
    else {
      set_viewport_device(d == "cpu" ? 1 : d == "gpu" ? 2 : 0);
      save_prefs();
      Log::info("Viewport Device: %s", d == "cpu" ? "CPU" : d == "gpu" ? "GPU" : "Auto");
    }
  }
  else if (c == "fps") {
    /* fps <n | 0 / unlimited>: the editor's frame rate cap */
    const std::string v = to_lower(arg(1, "120"));
    max_fps_ = v == "unlimited" || v == "off" ? 0 : std::max(0, std::min(1000, std::atoi(v.c_str())));
    Log::info("Editor frame rate: %s", max_fps_ ? strprintf("up to %d fps", max_fps_).c_str() : "unlimited");
  }
  else if (c == "redraw") always_redraw_ = to_lower(arg(1, "changes")) == "always";  // redraw always | changes
  else if (c == "quality") {
    /* quality low|medium|high|custom: the editor's quality tier (task 0050) */
    const std::string v = to_lower(arg(1, ""));
    const int t = v == "low" ? TierLow : v == "medium" ? TierMedium : v == "high" ? TierHigh : v == "custom" ? TierCustom : -1;
    if (t < 0) Log::warn("quality low|medium|high|custom");
    else {
      set_quality_tier(t);
      Log::info("Quality: %s (views at %.0f%%)", v.c_str(), view_scale_ * 100.0f);
    }
  }
  else if (c == "viewscale") {  // viewscale <50..100>: Viewport Resolution Scale in percent (the tier becomes Custom)
    set_view_scale((float)std::atof(arg(1, "100").c_str()) / 100.0f);
    quality_tier_ = TierCustom;
  }
  else if (c == "threads") set_cpu_threads(std::atoi(arg(1, "0").c_str()));  // threads <n | 0 = all>
  else if (c == "preferences") dialog_ = Dialog::Preferences;
  else if (c == "newscene") new_scene();  // File > New Scene
  else if (c == "guide") {
    /* guide <px> <py> <pz> <dx> <dy> <dz>: a construction line through p along d */
    auto f = [&](int i) { return (float)std::atof(arg(i, "0").c_str()); };
    const Vec3 d(f(4), f(5), f(6));
    if (length(d) > 1e-9f) {
      scene_->guides.push_back({Vec3(f(1), f(2), f(3)), normalize(d)});
      show_guides_ = true;
      mark_changed("Add Guide Line");
    }
  }
  else if (c == "clearguides") {
    scene_->guides.clear();
    mark_changed("Clear Guides");
  }
  else if (c == "guidesfromedges") guides_from_selected_edges();
  else if (c == "mkfolder") {  // mkfolder [Assets/dir]: that folder, or "New Folder" in Assets
    if (arg(1, "").empty()) create_project_folder(assets_dir_);
    else if (fs::make_dirs(fs::join(project_root_, arg(1, "")))) invalidate_project_listing();
  }
  else if (c == "moveasset") move_project_entry(fs::join(project_root_, arg(1, "")), fs::join(project_root_, arg(2, "Assets")));  // moveasset <Assets/x> <Assets/dir>
  else if (c == "renameasset") rename_project_entry(fs::join(project_root_, arg(1, "")), arg(2, ""));  // renameasset <Assets/x.mat> <new name>
  else if (c == "deleteasset") delete_project_entry(fs::join(project_root_, arg(1, "")));  // deleteasset <Assets/x>: to the Recycle Bin / Trash
  else if (c == "newslotmat") {
    /* newslotmat <slot> [preset]: the Inspector's New Material (of Type) on a slot of the selection */
    if (GameObject *a = active_object()) {
      const int slot = std::max(0, std::atoi(arg(1, "0").c_str()));
      auto *mr = a->get<MeshRenderer>();
      MaterialPtr cur = mr && slot < (int)mr->materials.size() ? mr->materials[(size_t)slot] : nullptr;
      std::string preset = t.size() > 2 ? line.substr(line.find(t[2])) : "";
      assign_material(a, slot, new_slot_material(preset.empty() ? cur : nullptr, preset));
    }
  }
  else if (c == "facemat") {
    /* facemat <Assets/x.mat | scene material name>: the selected faces use that material */
    MaterialPtr m = material_asset(arg(1, ""));
    if (!m)
      for (const MaterialPtr &sm : scene_materials())
        if (sm->name == arg(1, "")) m = sm;
    if (m) assign_material_to_selected_faces(m);
    else Log::warn("facemat: no material %s", arg(1, "").c_str());
  }
  else if (c == "drawpoint") {
    /* drawpoint <x> <y> <z> [close|finish]: a click of the drawing tool at a world point (scripts, tests) */
    if (draw_.active) {
      const Vec3 p((float)std::atof(arg(1, "0").c_str()), (float)std::atof(arg(2, "0").c_str()), (float)std::atof(arg(3, "0").c_str()));
      draw_point(p, to_lower(arg(4, "")));
    }
  }
  else if (c == "exportoptions") {
    /* exportoptions <obj|fbx|glb|gltf|stl|ply|usda>: File > Export > that format (the options, then Save As) */
    std::string fmt = to_lower(arg(1, "obj"));
    if (fmt[0] != '.') fmt = "." + fmt;
    const int fi = export_format_from_extension(fmt);
    if (fi >= 0) open_export_dialog(fi);
  }
  else if (c == "selectadd") {
    /* selectadd <name>: Ctrl+click it (it becomes the active object) */
    if (GameObject *g = scene_->find_by_name(line.substr(line.find(' ') + 1))) select(g->id, SEL_ADD);
    else Log::warn("No object named that");
  }
  else if (c == "boolean") {
    /* boolean <difference|union|intersect> [modifier]: the active object is cut by the other selected ones */
    const std::string o = to_lower(arg(1, "difference"));
    boolean_selected(o == "union" ? 1 : o == "intersect" ? 2 : 0, to_lower(arg(2, "")) != "modifier");
  }
  else if (c == "separate") separate(to_lower(arg(1, "loose")) == "selection" ? "selection" : "loose");
  else if (c == "esel") {
    /* esel <a> <b> [<c> <d> ...]: select exactly these edges (vertex pairs), edge mode */
    if (!edit_mode_) enter_edit_mode();
    if (edit_object()) {
      if (elem_ != EditElement::Edge) set_edit_element(EditElement::Edge);
      edge_sel_.clear();
      for (size_t i = 1; i + 1 < t.size(); i += 2)
        edge_sel_.insert(Mesh::edge_key((uint32_t)std::strtoul(t[i].c_str(), nullptr, 10), (uint32_t)std::strtoul(t[i + 1].c_str(), nullptr, 10)));
      verts_from_edges();
    }
  }
  else if (c == "vsel") {
    /* vsel <v0> <v1> ... : select exactly these vertices (edit mode) */
    if (!edit_mode_) enter_edit_mode();
    if (edit_object()) {
      vert_sel_.assign((*edit_mesh_ptr())->vert_count(), 0);
      for (size_t i = 1; i < t.size(); i++) {
        size_t v = std::strtoull(t[i].c_str(), nullptr, 10);
        if (v < vert_sel_.size()) vert_sel_[v] = 1;
      }
      sync_vert_face_selection(false);
    }
  }
  else if (c == "position" && t.size() >= 4) {
    /* position <x> <y> <z>: the selected objects' world position */
    const Vec3 p((float)std::atof(t[1].c_str()), (float)std::atof(t[2].c_str()), (float)std::atof(t[3].c_str()));
    for (GameObject *g : selected_objects(false)) g->set_world_position(p);
    mark_changed("Position");
  }
  else if (c == "focus") {
    /* focus <x> <y> <z> [fstop]: the selected camera keeps that point in focus (depth of field on) */
    GameObject *a = active_object();
    Camera *cam = a ? a->get<Camera>() : nullptr;
    if (!cam) Log::warn("focus: select a camera");
    else {
      cam->dof = true;
      cam->focus_track = true;
      cam->focus_point = Vec3((float)std::atof(arg(1, "0").c_str()), (float)std::atof(arg(2, "0").c_str()), (float)std::atof(arg(3, "0").c_str()));
      if (t.size() > 4) cam->f_stop = std::max(0.1f, (float)std::atof(t[4].c_str()));
      update_camera_focus();
      mark_changed("Camera Focus");
    }
  }
  else if (c == "autosmooth") {
    /* autosmooth on|off [angle]: Auto Smooth for new curved surfaces */
    auto_smooth_ = to_lower(arg(1, "on")) != "off";
    if (t.size() > 2) auto_smooth_angle_ = std::max(1.0f, std::min(180.0f, (float)std::atof(t[2].c_str())));
  }
  else if (c == "set_extrude_individual") extrude_individual_ = arg(1, "1") == "1";
  else if (c == "duplicate") duplicate_selected();
  else if (c == "uvsel") {
    /* uvsel all|none|invert|islands | uvsel face <f> | uvsel island <f> | uvsel mode vertex|face|island */
    const std::string w = to_lower(arg(1, "all"));
    if (w == "mode") {
      const std::string md = to_lower(arg(2, "vertex"));
      uv_select_mode_ = md == "face" ? 1 : md == "island" ? 2 : 0;
    }
    else uv_select(w, std::atoi(arg(2, "-1").c_str()));
  }
  else if (c == "uvxf") {
    /* uvxf rotate <deg> | scale <k> [ky] | move <du> <dv> | flip_u | flip_v | fit | center | align_left ... */
    uv_transform(to_lower(arg(1, "")), (float)std::atof(arg(2, "0").c_str()), (float)std::atof(arg(3, "0").c_str()));
  }
  else if (c == "zfight") {
    /* zfight [scene] | zfight fix [index] */
    if (to_lower(arg(1, "")) == "fix") zfight_fix(t.size() > 2 ? std::atoi(t[2].c_str()) : -1);
    else if (to_lower(arg(1, "")) == "clear") zfight_.clear();
    else {
      zfight_scan(to_lower(arg(1, "")) == "scene");
      zfight_from_panel_ = true;
    }
  }
  else if (c == "keymap") {
    /* keymap <Unity|Blender|Maya|3ds Max|SketchUp> | keymap bind <action> <chord>[|<chord>] | keymap clear <action> */
    const std::string sub = to_lower(arg(1, ""));
    if (sub == "bind" && t.size() >= 4) {
      std::vector<KeyChord> chords;
      std::string spec = t[3];
      size_t p = 0;
      while (p <= spec.size()) {
        size_t q = spec.find('|', p);
        if (q == std::string::npos) q = spec.size();
        const KeyChord ch = parse_chord(spec.substr(p, q - p));
        if (ch.key) chords.push_back(ch);
        p = q + 1;
      }
      set_binding(t[2], chords);
    }
    else if (sub == "clear" && t.size() >= 3) set_binding(t[2], {});
    else if (!sub.empty()) {
      const std::string name = line.substr(line.find(' ') + 1);
      for (const char *pn : kKeymapPresets)
        if (to_lower(pn) == to_lower(name)) set_keymap_preset(pn);
    }
  }
  else if (c == "rendersequence") {
    /* rendersequence [folder] | rendersequence stop: every In Sequence camera, each saved */
    if (to_lower(arg(1, "")) == "stop") stop_render_sequence();
    else start_render_sequence(arg(1, "").empty() ? "" : fs::join(project_root_, arg(1, "")));
  }
  else if (c == "insetmode") {
    /* insetmode individual|region [amount]: Inset's Individual switch and its amount (a fraction) or thickness */
    inset_individual_ = to_lower(arg(1, "individual")) != "region";
    if (t.size() > 2) (inset_individual_ ? inset_amount_ : inset_thickness_) = std::max(0.0f, (float)std::atof(t[2].c_str()));
  }
  else if (c == "edittool") edit_tool(arg(1, "fill"));  // edittool <op>: as the menus and buttons start it (helpers included)
  else if (c == "editop") {
    const std::string op = arg(1, "fill");
    if (op == "knife" || op == "origin_to_selection") edit_tool(op);  /* tools, not mesh operators */
    else edit_op(op);
  }
  else if (c == "ngon") ngon_mode_ = to_lower(arg(1, ngon_mode_ ? "off" : "on")) == "on";  // ngon [on|off]: SketchUp-style faces
  else if (c == "material") {
    /* material <Solid|Transparent|Cutout|Glass|Frosted Glass|Metal|Emissive|Unlit>: new material on the selection. */
    std::string preset = line.size() > 9 ? line.substr(9) : "Solid";
    int n = 0;
    for (GameObject *g : selected_objects(false))
      if (auto *mr = g->get<MeshRenderer>()) {
        if (mr->materials.empty()) mr->materials.resize(1);
        mr->materials[0] = make_material_preset(preset);
        n++;
      }
    if (n) mark_changed("Material " + preset);
    else Log::warn("Select objects with a MeshRenderer first");
  }
  else if (c == "preview") start_final_render(true);
  else if (c == "device") {
    /* device cpu | device gpu [+cpu] [nort] | device auto: Render Properties > Device (path tracing). */
    RenderSettings &rs = scene_->render;
    rs.device = arg(1, "cpu") == "gpu" ? 1 : arg(1, "cpu") == "auto" ? 2 : 0;
    rs.gpu_with_cpu = line.find("+cpu") != std::string::npos;
    rs.hardware_rt = line.find("nort") == std::string::npos;
    mark_changed("Render Device");
    Log::info("Device: %s%s", rs.device == 2 ? "GPU + CPU (Auto)" : rs.device ? "GPU Compute" : "CPU",
              rs.device == 1 ? (rs.gpu_with_cpu ? " + CPU" : "") : "");
    Log::info("%s", gpu::status().c_str());
  }
  else if (c == "pushpull") {
    /* pushpull <distance>: Push/Pull the selected faces (edit mode), like the P tool. */
    pp_last_distance_ = (float)std::atof(arg(1, "0.5").c_str());
    edit_op("push_pull");
  }
  else if (c == "camerapreview") {
    /* camerapreview rendered|shaded|lock|unlock (the Scene view's Camera Preview inset) */
    const std::string w = to_lower(arg(1, "rendered"));
    if (w == "lock") {
      GameObject *a = active_object();
      if (a && a->get<Camera>()) cam_preview_lock_ = a->id;
    }
    else if (w == "unlock") cam_preview_lock_ = 0;
    else {
      cam_preview_rendered_ = w == "rendered";
      cam_preview_pt_hash_ = 0;
    }
  }
  else if (c == "set") {
    /* set <Component>.<Field> <value...> (a Vec3 takes three numbers) */
    size_t at = t.size() > 2 ? line.find(t[2], line.find(t[1]) + t[1].size()) : std::string::npos;
    set_field_command(arg(1, ""), at == std::string::npos ? "" : line.substr(at));
  }
  else if (c == "origin") {
    /* origin bounds|median|surface|volume|bottom|selection|pivot|geometry, or origin point <x> <y> <z> */
    static const char *names[kOriginModeCount] = {"bounds", "median", "surface", "volume", "bottom", "point", "selection", "pivot", "geometry"};
    /* origin mode <name>: choose the Inspector's mode without applying it (the Scene view previews it). */
    const bool choose = to_lower(arg(1, "")) == "mode";
    if (to_lower(arg(1, "")) == "edit") {  // origin edit [on|off]: move the origin with the gizmo / click to snap
      const std::string v = to_lower(arg(2, ""));
      origin_edit_ = v.empty() ? !origin_edit_ : v == "on";
      return;
    }
    std::string w = to_lower(arg(choose ? 2 : 1, "bounds"));
    int mode = -1;
    for (int k = 0; k < kOriginModeCount; k++)
      if (w == names[k]) mode = k;
    if (mode < 0) Log::warn("origin: use bounds, median, surface, volume, bottom, selection, pivot, geometry or point x y z");
    else if (choose) origin_mode_ = mode;
    else
      set_origin(mode, Vec3((float)std::atof(arg(2, "0").c_str()), (float)std::atof(arg(3, "0").c_str()), (float)std::atof(arg(4, "0").c_str())));
  }
  else if (c == "fsel") {
    /* fsel <f0> <f1> ... | fsel facing <x> <y> <z> [more directions...]: select faces (edit mode). */
    if (!edit_mode_) enter_edit_mode();
    if (edit_object()) {
      const Mesh &m = **edit_mesh_ptr();
      elem_ = EditElement::Face;
      face_sel_.assign(m.face_count(), 0);
      if (arg(1, "") == "facing") {
        for (size_t i = 2; i + 2 < t.size(); i += 3) {
          Vec3 d = normalize(Vec3((float)std::atof(t[i].c_str()), (float)std::atof(t[i + 1].c_str()), (float)std::atof(t[i + 2].c_str())));
          for (size_t f = 0; f < m.face_count(); f++)
            if (dot(m.face_normal(f), d) > 0.99f) face_sel_[f] = 1;
        }
      }
      else
        for (size_t i = 1; i < t.size(); i++) {
          size_t f = std::strtoull(t[i].c_str(), nullptr, 10);
          if (f < face_sel_.size()) face_sel_[f] = 1;
        }
      sync_vert_face_selection(true);
    }
  }
  else if (c == "redo" && t.size() == 1) redo();  // no arguments: redo the last undo (Ctrl+Y)
  else if (c == "redo") {
    /* redo <amount|segments|twist|smooth|path|normal|orientation|fuse> <value> | redo move <x> <y> <z>:
     * change the last edit operator's parameters (the Adjust Last Operation panel). */
    if (!last_op_valid()) { Log::warn("Nothing to adjust: run an edit operator first"); return; }
    std::string k = arg(1, "");
    float v = (float)std::atof(arg(2, "0").c_str());
    if (k == "amount") last_op_.amount = v;
    else if (k == "segments") last_op_.segments = (int)v;
    else if (k == "twist") last_op_.twist = (int)v;
    else if (k == "smooth") last_op_.smooth = v;
    else if (k == "path") last_op_.path = (int)v;
    else if (k == "normal") last_op_.along_normal = v;
    else if (k == "orientation") last_op_.orientation = (int)v;
    else if (k == "fuse") last_op_.fuse = v != 0;
    else if (k == "clamp") last_op_.clamp = v != 0;
    else if (k == "move") last_op_.move = Vec3(v, (float)std::atof(arg(3, "0").c_str()), (float)std::atof(arg(4, "0").c_str()));
    else { Log::warn("redo: unknown parameter '%s'", k.c_str()); return; }
    run_last_op(false);
  }
  else if (c == "loopcuts") {
    loop_cuts_ = std::max(1, std::atoi(arg(1, "1").c_str()));
    loop_slide_ = (float)std::atof(arg(2, "0.5").c_str());
  }
  else if (c == "proportional") {
    proportional_ = arg(1, "on") != "off";
    prop_radius_ = (float)std::atof(arg(2, "1").c_str());
  }
  else if (c == "component") {
    /* Names may have spaces ("Retro Console Filter"): the rest of the line. */
    std::string name;
    for (size_t i = 1; i < t.size(); i++) name += (i > 1 ? " " : "") + t[i];
    add_component_to_selection(name.empty() ? "MirrorModifier" : name);
  }
  else if (c == "applymods") mesh_op("apply_modifiers");
  else if (c == "libs") {
    /* Which of Blender's libraries this build uses (deps/deps.h). */
    for (const deps::Library &l : deps::libraries())
      Log::info("%-18s %-10s %s%s", l.name, l.enabled ? l.version.c_str() : "-", l.enabled ? l.used_for : "not built in; using ",
                l.enabled ? "" : l.fallback);
    Log::info("%s", gpu::status().c_str());
  }
  else if (c == "camera" && t.size() >= 4) {
    cam_.yaw = (float)std::atof(t[1].c_str());
    cam_.pitch = (float)std::atof(t[2].c_str());
    cam_.distance = (float)std::atof(t[3].c_str());
    if (t.size() >= 7) cam_.pivot = {(float)std::atof(t[4].c_str()), (float)std::atof(t[5].c_str()), (float)std::atof(t[6].c_str())};
  }
  else if (c == "world") {
    std::string w = to_lower(arg(1, "gradient"));
    scene_->environment.mode = w == "sky" ? 1 : w == "hdri" ? 2 : w == "color" ? 3 : 0;
    if (w == "hdri" && t.size() > 2) scene_->environment.hdri.path = line.substr(line.find(t[2]));
    mark_changed("World");
  }
  else if (c == "engine") {
    const std::string e = to_lower(arg(1, "raster"));  // raster | path | moonray (task 0114)
    scene_->render.engine = e.rfind("path", 0) == 0 ? 1 : e.rfind("moon", 0) == 0 ? 2 : 0;
    mark_changed("Render Engine");
  }
  else if (c == "samples") {
    scene_->render.samples = std::max(1, std::atoi(arg(1, "64").c_str()));
  }
  else if (c == "texture") {
    std::string p = t.size() > 1 ? line.substr(line.find(t[1])) : "generated:UV Grid";
    if (p == "uvgrid") p = "generated:UV Grid";
    if (p == "colorgrid") p = "generated:Color Grid";
    assign_texture_to_selection(p);
  }
  else if (c == "selectall") {
    selection_.clear();
    scene_->for_each([&](GameObject &g) { if (g.get<MeshRenderer>()) selection_.push_back(g.id); });
  }
  else if (c == "uv") uv_op(arg(1, "unwrap"));
  else if (c == "seam") {
    /* seam <v0> <v1> ... : select those vertices (edit mode) and mark seams between them */
    if (!edit_mode_) enter_edit_mode();
    if (GameObject *g = edit_object()) {
      const Mesh &m = **edit_mesh_ptr();
      vert_sel_.assign(m.vert_count(), 0);
      for (size_t i = 1; i < t.size(); i++) {
        size_t v = std::strtoull(t[i].c_str(), nullptr, 10);
        if (v < vert_sel_.size()) vert_sel_[v] = 1;
      }
      (void)g;
      uv_op("mark_seam");
    }
  }
  else if (c == "render") start_final_render();
  else if (c == "probes") {
    /* probes show|hide: the probe volume's probes in the Scene view. */
    scene_->lighting.show_probes = to_lower(arg(1, "show")) != "hide";
    mark_changed("Show Probes");
  }
  else if (c == "bake") {
    /* bake [start|cancel|clear]: Generate Lighting, as the Lighting window's buttons. */
    const std::string w = to_lower(arg(1, "start"));
    if (w == "cancel") bake_cancel();
    else if (w == "clear") bake_clear();
    else bake_start();
  }
  else if (c == "saverender") save_render();
  else if (c == "lighting") {
    /* lighting update | lighting preview on|off | lighting mode manual|automatic (task 0020) */
    const std::string sub = to_lower(arg(1, "update"));
    if (sub == "update") lighting_update();
    else if (sub == "preview") {
      scene_->lighting.preview = to_lower(arg(2, scene_->lighting.preview ? "off" : "on")) == "on";
      mark_changed("Preview Lighting");
    }
    else if (sub == "mode") {
      const std::string m = to_lower(arg(2, "manual"));
      scene_->lighting.update_mode = m == "automatic" || m == "auto" ? 1 : 0;
      mark_changed("Lighting Updates");
    }
    else Log::warn("lighting: update | preview on|off | mode manual|automatic");
  }
  else if (c == "shading") {
    std::string w = to_lower(arg(1, "shaded"));
    if (w == "rendered")
      Log::warn("shading: the Scene view has no Rendered mode (too slow to edit in); showing Shaded. Path trace with F12, the "
                "Render window or `camerapreview rendered`.");
    shading_ = w == "wire" || w == "wireframe" ? Shading::Wireframe
               : w == "both"                      ? Shading::ShadedWireframe
               : w == "solid"                     ? Shading::Solid
                                                   : Shading::Shaded;
  }
  else if (c == "filter") {
    /* filter add <name> | look <Anime|Manga> | remove <i> | move <i> <j> | reset <i> | list: the selection's Camera Filters stacks. */
    const std::string sub = to_lower(arg(1, "list"));
    std::string name;
    for (size_t k = 2; k < t.size(); k++) name += (k > 2 ? " " : "") + t[k];
    const int i = std::atoi(arg(2, "0").c_str()), j = std::atoi(arg(3, "0").c_str());
    if ((sub == "remove" || sub == "reset" || sub == "move") && t.size() < (sub == "move" ? 4u : 3u)) {
      Log::warn("filter %s: give the filter's number%s (filter list shows them)", sub.c_str(), sub == "move" ? " and where to" : "");
      return;
    }
    for (GameObject *g : selected_objects(false)) {
      if (sub == "list") {
        const auto *cf = g->get<CameraFilters>();
        Log::info("%s: %zu filter(s)", g->name.c_str(), cf ? cf->effects.size() : (size_t)0);
        if (cf)
          for (size_t k = 0; k < cf->effects.size(); k++)
            Log::info("  %zu %s%s", k, cf->effects[k]->type_name(), cf->effects[k]->enabled ? "" : " (off)");
        if (!cf || cf->effects.empty()) {
          std::string all;
          for (const FilterEffectInfo &fi : filter_effect_infos()) all += (all.empty() ? "" : ", ") + fi.category + " > " + fi.name;
          Log::info("  available: %s", all.c_str());
        }
      }
      else filter_stack_op(g->id, sub, i, j, name);
    }
  }
  else if (c == "undo") undo();  // Ctrl+Z
  else if (c == "newtex") {
    /* newtex <name> [preset]: a new Assets/Textures/<name>.ptex (presets: Bricks, Wood Floor, Tatami, Tiles, Noise, Checker). */
    const std::string name = arg(1, "");
    if (name.empty()) {
      Log::warn("newtex <name> [preset]");
      return;
    }
    std::string preset;
    for (size_t i = 2; i < t.size(); i++) preset += (i > 2 ? " " : "") + t[i];
    ProceduralTexture src;
    src.name = name;
    if (!preset.empty() && !apply_texture_preset(src, preset)) Log::warn("newtex: no preset '%s' (Bricks, Wood Floor, Tatami, Tiles, Noise, Checker)", preset.c_str());
    const std::string rel = "Assets/Textures/" + name + ".ptex";
    if (ProceduralTexturePtr pt = create_texture_asset(src, rel)) {
      Log::info("Created %s (%zu layer%s)", rel.c_str(), pt->layers.size(), pt->layers.size() == 1 ? "" : "s");
      project_listed_ = -100;
    }
    else Log::error("Could not write %s", rel.c_str());
  }
  else if (c == "texlist") {
    for (const auto &kv : loaded_texture_assets())
      Log::info("%s: %dx%d, %zu layer(s)%s", kv.first.c_str(), kv.second->width, kv.second->height, kv.second->layers.size(),
                kv.second->layers.empty() ? "" : (": " + [&] {
                  std::string s;
                  for (size_t i = 0; i < kv.second->layers.size(); i++) s += (i ? ", " : "") + std::string(kv.second->layers[i]->type_name());
                  return s;
                }()).c_str());
    const std::string dir = fs::join(assets_dir_, "Textures");
    for (const DirEntry &de : fs::list(dir))
      if (!de.is_dir && fs::extension(de.name) == ".ptex") texture_asset(make_asset_relative(fs::join(dir, de.name)));
  }
  else if (c == "texlayer") {
    /* texlayer <asset> add <type> | remove <i> | move <i> <j> | reset <i> | list */
    ProceduralTexturePtr pt = texture_asset(arg(1, "").find('/') == std::string::npos ? "Assets/Textures/" + arg(1, "") + ".ptex" : arg(1, ""));
    if (!pt) {
      Log::warn("texlayer: no texture asset '%s' (a name in Assets/Textures, or a project path)", arg(1, "").c_str());
      return;
    }
    const std::string sub = to_lower(arg(2, "list"));
    std::string name;
    for (size_t k = 3; k < t.size(); k++) name += (k > 3 ? " " : "") + t[k];
    const int i = std::atoi(arg(3, "0").c_str()), j = std::atoi(arg(4, "0").c_str());
    const int n = (int)pt->layers.size();
    if (sub == "list") {
      for (int k = 0; k < n; k++) Log::info("  %d: %s%s", k, pt->layers[(size_t)k]->type_name(), pt->layers[(size_t)k]->enabled ? "" : " (off)");
      if (!n) Log::info("%s: no layers", pt->name.c_str());
    }
    else if (sub == "add" || sub == "remove" || sub == "move" || sub == "reset") {
      if (texture_stack_op(pt->asset_path, sub, i, j, name) && sub == "add") Log::info("%s: added %s (%zu layers)", pt->name.c_str(), name.c_str(), pt->layers.size());
    }
    else Log::warn("texlayer: add <type> | remove <i> | move <i> <j> | reset <i> | list");
  }
  else if (c == "settex") {
    /* settex <asset> <Field> <value...>: a texture asset's field, or a layer's as "L0 Rows" / L0Rows. */
    ProceduralTexturePtr pt = texture_asset(arg(1, "").find('/') == std::string::npos ? "Assets/Textures/" + arg(1, "") + ".ptex" : arg(1, ""));
    if (!pt || t.size() < 4) {
      Log::warn("settex <asset> <Field> <value...> (e.g. settex Bricks L0Rows 12)");
      return;
    }
    size_t at = line.find(t[3], line.find(t[2], line.find(t[1]) + t[1].size()) + t[2].size());
    SetFieldReflector sr;
    sr.key = field_key(t[2]);
    sr.value = at == std::string::npos ? "" : line.substr(at);
    sr.target.clear();
    sr.names.clear();
    sr.done = false;
    pt->reflect(sr);
    for (int pass = 0; pass < 2 && sr.target.empty(); pass++)
      for (const std::string &nm : sr.names)
        if (pass == 0 ? field_key(nm) == sr.key : field_key(nm).rfind(sr.key, 0) == 0) {
          sr.target = nm;
          break;
        }
    if (sr.target.empty()) {
      Log::warn("settex: no field '%s' on %s", t[2].c_str(), pt->name.c_str());
      return;
    }
    note_texture_edit(*pt);
    const float size_before = pt->physical_size;
    pt->reflect(sr);
    if (sr.done) {
      if (pt->physical_size != size_before) retile_texture_users(pt->asset_path, size_before, pt->physical_size);  // (task 0072)
      pt->touch();
      mark_changed("Set " + pt->name + " " + sr.target);
    }
  }
  else if (c == "guides") {
    /* guides list | guides off | guides <thirds|center|diagonal|golden|goldena|goldenb|harmonya|harmonyb|title|action|show> [on|off]:
     * the composition guides of the selected cameras, else the camera the Game view shows (task 0033). */
    std::vector<Camera *> cams;
    for (GameObject *g : selected_objects(false))
      if (Camera *cm = g->get<Camera>()) cams.push_back(cm);
    if (cams.empty())
      if (Camera *mc = main_camera(*scene_)) cams.push_back(mc);
    if (cams.empty()) {
      Log::warn("guides: no camera");
      return;
    }
    const std::string what = to_lower(arg(1, "list")), v = to_lower(arg(2, "on"));
    const bool on = v == "on" || v == "1" || v == "true", off_ = v == "off" || v == "0" || v == "false";
    if (what != "list" && what != "off" && !on && !off_) {
      Log::warn("guides %s: on or off", what.c_str());
      return;
    }
    bool changed = false;
    for (Camera *cm : cams) {
      struct G {
        const char *name;
        bool *flag;
      } all[] = {{"thirds", &cm->guide_thirds},        {"center", &cm->guide_center},           {"diagonal", &cm->guide_center_diagonal},
                 {"golden", &cm->guide_golden},        {"goldena", &cm->guide_golden_tri_a},    {"goldenb", &cm->guide_golden_tri_b},
                 {"harmonya", &cm->guide_harmony_a},   {"harmonyb", &cm->guide_harmony_b},      {"title", &cm->guide_title_safe},
                 {"action", &cm->guide_action_safe},   {"show", &cm->show_guides}};
      if (what == "list") {
        std::string on;
        for (const G &gg : all)
          if (*gg.flag) on += std::string(on.empty() ? "" : ", ") + gg.name;
        Log::info("guides of %s: %s (shown: %s)", cm->owner ? cm->owner->name.c_str() : "?", on.empty() ? "none" : on.c_str(), cm->show_guides ? "yes" : "no");
        continue;
      }
      if (what == "off") {
        changed = changed || cm->show_guides;
        cm->show_guides = false;
        continue;
      }
      bool found = false;
      for (const G &gg : all)
        if (what == gg.name) {
          changed = changed || *gg.flag != on;
          *gg.flag = on;
          if (gg.flag != &cm->show_guides && on) changed = changed || !cm->show_guides, cm->show_guides = true;
          found = true;
        }
      if (!found) {
        Log::warn("guides: thirds, center, diagonal, golden, goldena, goldenb, harmonya, harmonyb, title, action, show, off or list");
        return;
      }
    }
    if (changed) mark_changed("Guides");
  }
  else if (c == "curve" || c == "points") points_command(t);  // its own function: this chain is near the compiler's nesting limit
  else if (c == "activecamera") {
    /* activecamera <object name> | none: the scene's Active Camera; no argument prints it. */
    std::string name;
    for (size_t i = 1; i < t.size(); i++) name += (i > 1 ? " " : "") + t[i];
    if (name.empty()) {
      GameObject *shown = nullptr;
      main_camera(*scene_, &shown);
      Log::info("Active Camera: %s%s", shown ? shown->name.c_str() : "none",
                shown && shown->id != scene_->active_camera ? " (by name; none chosen)" : "");
    }
    else if (to_lower(name) == "none") set_active_camera(nullptr);
    else if (GameObject *g = scene_->find_by_name(name)) {
      if (g->camera()) set_active_camera(g);
      else Log::warn("activecamera: '%s' has no Camera component", name.c_str());
    }
    else Log::warn("activecamera: no object named '%s'", name.c_str());
  }
  else if (c == "filters") {
    /* The main camera's filters on the Scene view (Shaded): on / off / toggle. */
    const std::string w = to_lower(arg(1, "toggle"));
    scene_filters_ = w == "on" || w == "1" ? true : w == "off" || w == "0" ? false : !scene_filters_;
    Log::info("Camera filters in the Scene view: %s", scene_filters_ ? "on" : "off");
  }
  else if (c == "bench") {
    int frames = std::max(1, std::atoi(arg(1, "30").c_str()));
    if (scene_rt_.width <= 0) { Log::warn("Open the Scene view first"); return; }
    RasterOptions saved = raster_opt_;
    struct Cfg { const char *name; bool mt, cull, frus; };
    const FrameProfile before = prof_;  // (task 0159: the card's time over the bench's frames alone)
    for (Cfg cfg : {Cfg{"all optimizations", true, true, true}, Cfg{"single thread", false, true, true},
                    Cfg{"no backface culling", true, false, true}, Cfg{"no frustum culling", true, true, false}}) {
      raster_opt_.multithreaded = cfg.mt;
      raster_opt_.backface_culling = cfg.cull;
      raster_opt_.frustum_culling = cfg.frus;
      ScopedTimer bt;
      for (int i = 0; i < frames; i++) render_scene_view(scene_rect_);
      Log::info("bench %-22s %7.2f ms/frame  (%zu tris)", cfg.name, bt.ms() / frames, scene_stats_.tris_submitted);
    }
    raster_opt_ = saved;
    /* Task 0159: what the card did over the bench's own frames (the Scene view on the GPU), per frame. */
    const FrameProfile fp = prof_ - before;
    if (fp.ms_gpu > 0.0) Log::info("bench GPU busy %.2f ms of %.2f ms a frame in the GPU path (copies out and fence %.2f ms, the card busy %.0f %% of the path)", fp.gpu_busy_ms() / frames, fp.ms_gpu / frames, fp.ms_gpu_readback / frames, 100.0 * fp.gpu_busy_ms() / fp.ms_gpu);
    else Log::info("bench GPU busy: the views are on the CPU (Preferences > Viewport Device: GPU to measure the card)");
  }
  else Log::warn("Unknown command '%s' (type help)", c.c_str());
}

}  // namespace bl
