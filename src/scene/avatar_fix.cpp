// SPDX-License-Identifier: GPL-2.0-or-later
// Fix Model for VRChat avatars (round 357, task 0334): see avatar_fix.h.
#include "avatar_fix.h"

#include "animation.h"
#include "humanoid.h"
#include "import.h"
#include "material.h"
#include "mesh.h"
#include "scene.h"
#include "../core/core.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <unordered_map>

namespace bl {

namespace {

/* The bones under `g`, parents before children. */
void bones_below(GameObject &g, std::vector<GameObject *> &out) {
  for (GameObject *c : g.children) {
    if (c->get<Bone>()) out.push_back(c);
    bones_below(*c, out);
  }
}

std::vector<GameObject *> bones_of(GameObject &armature) {
  std::vector<GameObject *> out;
  bones_below(armature, out);
  return out;
}

void objects_below(GameObject &g, std::vector<GameObject *> &out) {
  for (GameObject *c : g.children) {
    out.push_back(c);
    objects_below(*c, out);
  }
}

Mesh *mutable_mesh(GameObject &g) {
  MeshFilter *mf = g.get<MeshFilter>();
  if (!mf || !mf->mesh) return nullptr;
  return mesh_make_mutable(mf->mesh).get();
}

/* `from`'s weights added into `to` (made when missing), `from` dropped. The sums aren't clamped: the skin normalises,
 * and a rig that isn't normalised keeps its ratios. */
void move_group(GameObject &g, const std::string &from, const std::string &to) {
  const MeshFilter *mf = g.get<MeshFilter>();
  if (from == to || !mf || !mf->mesh || mf->mesh->find_group(from) < 0 || !mf->mesh->groups_fit()) return;
  Mesh &m = *mutable_mesh(g);
  if (m.find_group(to) < 0) m.vertex_groups.push_back({to, std::vector<float>(m.vert_count(), 0.0f)});
  VertexGroup &dst = m.vertex_groups[(size_t)m.find_group(to)];
  const VertexGroup &src = m.vertex_groups[(size_t)m.find_group(from)];
  for (size_t i = 0; i < dst.weights.size() && i < src.weights.size(); i++) dst.weights[i] += src.weights[i];
  m.vertex_groups.erase(m.vertex_groups.begin() + m.find_group(from));
  m.touch();
}

float group_sum(const std::vector<GameObject *> &meshes, const std::string &name) {
  float sum = 0.0f;
  for (GameObject *g : meshes) {
    const MeshFilter *mf = g->get<MeshFilter>();
    const int i = mf && mf->mesh ? mf->mesh->find_group(name) : -1;
    if (i < 0) continue;
    for (float w : mf->mesh->vertex_groups[(size_t)i].weights) sum += w;
  }
  return sum;
}

std::string strip(const std::string &s) {
  const size_t a = s.find_first_not_of(' '), b = s.find_last_not_of(' ');
  return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

/* "Left Hand=Bone.003; Head=head" with each value `from` turned into `to`. */
std::string rename_in_handles(const std::string &handles, const std::string &from, const std::string &to) {
  std::string out;
  for (const std::string &entry : split_list(handles, ';')) {
    const size_t eq = entry.find('=');
    const bool hit = eq != std::string::npos && strip(entry.substr(eq + 1)) == from;
    const std::string e = hit ? strip(entry.substr(0, eq)) + "=" + to : entry;
    out += (out.empty() ? "" : "; ") + e;
  }
  return out;
}

bool is_twist(const GameObject &g) { return to_lower(g.name).find("twist") != std::string::npos; }
bool is_eye(const GameObject &g) { return to_lower(g.name).find("eye") != std::string::npos; }
/* A bone something else needs: a spring chain, a collider, anything beyond the Bone itself. */
bool carries_more(const GameObject &g) {
  for (const auto &c : g.components)
    if (!dynamic_cast<const Bone *>(c.get())) return true;
  return false;
}

/* Unity Humanoid's name for a part ("Left Upper Arm" -> "LeftUpperArm"). */
std::string unity_name(humanoid::Part p) {
  std::string s = humanoid::kPartNames[p];
  s.erase(std::remove(s.begin(), s.end(), ' '), s.end());
  return s;
}

bool is_unity_part_name(const std::string &name) {
  for (int p = 0; p < humanoid::kParts; p++)
    if (unity_name((humanoid::Part)p) == name) return true;
  return false;
}

bool ascii(const std::string &s) {
  for (unsigned char c : s)
    if (c >= 0x80) return false;
  return true;
}

/* The part each part hangs from in Unity's Humanoid (kParts: the armature itself). */
constexpr humanoid::Part kUp[humanoid::kParts] = {
    humanoid::kParts, humanoid::Hips, humanoid::Spine, humanoid::Chest, humanoid::Neck,
    humanoid::Chest, humanoid::LShoulder, humanoid::LUpperArm, humanoid::LLowerArm,
    humanoid::Chest, humanoid::RShoulder, humanoid::RUpperArm, humanoid::RLowerArm,
    humanoid::Hips, humanoid::LUpperLeg, humanoid::LLowerLeg, humanoid::LFoot,
    humanoid::Hips, humanoid::RUpperLeg, humanoid::RLowerLeg, humanoid::RFoot};

/* A clip played over the rig, and every object's path below its player as Fix Model found them: the tracks follow
 * their objects through the renames, the merges and the join (review 357: only bones' had been followed). */
struct ClipPaths {
  AnimationClipPtr clip;
  GameObject *player = nullptr;
  std::unordered_map<std::string, uint64_t> object_of;  // the old path -> the object's id
};

/* What the pass shares between its steps. */
struct Pass {
  Scene &scene;
  GameObject &armature;
  const FixModelOptions &o;
  FixModelReport &r;
  std::vector<ClipPaths> clips;
  std::unordered_map<uint64_t, uint64_t> joined_into;  // a joined mesh -> the Body that took it
};

std::vector<ClipPaths> note_clip_paths(GameObject &armature) {
  std::vector<ClipPaths> out;
  for (GameObject *p = &armature; p; p = p->parent) {
    const Animation *a = p->get<Animation>();
    AnimationClipPtr clip = a && !a->clip.empty() ? clip_asset(a->clip) : nullptr;
    if (!clip) continue;
    ClipPaths cp{clip, p, {}};
    std::vector<GameObject *> all;
    objects_below(*p, all);
    for (GameObject *g : all) cp.object_of[anim_path(*p, *g)] = g->id;
    out.push_back(std::move(cp));
  }
  return out;
}

/* A track's object as noted (nullptr: not one of them). */
uint64_t noted_object(const ClipPaths &cp, const AnimTrack &t) {
  const auto it = cp.object_of.find(t.path);
  return it == cp.object_of.end() ? 0 : it->second;
}

/* Any clip keys `g` (a bone a clip moves isn't merged or removed: its motion - MMD's Center carries a dance's root
 * motion - would have nowhere to go). */
bool animated(const Pass &p, const GameObject &g) {
  for (const ClipPaths &cp : p.clips)
    for (const AnimTrack &t : cp.clip->tracks)
      if (noted_object(cp, t) == g.id) return true;
  return false;
}

/* Every key of `g`'s Position, Rotation and Scale tracks with `c` put in front: `g` was moved under another parent,
 * `c` taking its old parent's space into the new one (review 357: a merged Center's child dropped by Center's height
 * when its keys played). The three axes of a channel are sampled together at each of their keys. */
void convert_keys(Pass &p, GameObject &g, const Transform &c, const Transform &old) {
  for (ClipPaths &cp : p.clips) {
    std::string path;  // (its path as noted: the tracks move onto the new paths at the end)
    for (const auto &[at, id] : cp.object_of)
      if (id == g.id) path = at;
    if (path.empty()) continue;
    for (const char *prop : {"Position", "Rotation", "Scale"}) {
      AnimTrack *axis[3] = {cp.clip->find(path, prop, 0), cp.clip->find(path, prop, 1), cp.clip->find(path, prop, 2)};
      if (!axis[0] && !axis[1] && !axis[2]) continue;
      std::set<float> frames;
      for (AnimTrack *t : axis)
        for (const AnimKey &k : t ? t->keys : std::vector<AnimKey>()) frames.insert(k.frame);
      const std::string P = prop;
      const Vec3 rest = P == "Position" ? old.position : P == "Rotation" ? old.euler_hint : old.scale;  // (an axis with no track: its old value)
      std::vector<std::pair<float, Vec3>> out;
      for (float f : frames) {
        Vec3 v = rest;
        for (int a = 0; a < 3; a++)
          if (axis[a]) (&v.x)[a] = axis[a]->sample(f);
        if (P == "Position") v = c.rotation.rotate(v * c.scale.x) + c.position;
        else if (P == "Rotation") v = (c.rotation * Quat::euler(v)).to_euler();
        else v = v * c.scale.x;
        out.push_back({f, v});
      }
      for (int a = 0; a < 3; a++) {
        AnimTrack &t = cp.clip->track(path, prop, a);
        for (const auto &[f, v] : out) t.set_key(f, (&v.x)[a]);
      }
      cp.clip->touch();
    }
  }
}

/* `g` moved under `parent` keeping its place, its rest the new local one, its keys converted. */
void reparent(Pass &p, GameObject &g, GameObject *parent) {
  const Transform old = g.local();
  const Mat4 before = g.local_matrix();
  p.scene.set_parent(&g, parent, -1, true);
  const Transform c = transform_from_matrix(g.local_matrix() * before.inverse());
  if (Bone *b = g.get<Bone>()) b->set_rest(g.local());
  convert_keys(p, g, c, old);
}

/* Moves `bone`'s weights to `heir`, its children to its parent (their rests and keys kept in the world), and deletes
 * it. */
void dissolve(Pass &p, GameObject &bone, GameObject &heir) {
  for (GameObject *m : avatar_meshes(p.scene, p.armature)) move_group(*m, bone.name, heir.name);
  p.scene.for_each([&](GameObject &g) {
    IKConstraint *ik = g.get<IKConstraint>();
    if (!ik || !p.scene.is_ancestor(&p.armature, &g)) return;
    if (ik->target == bone.name) ik->target = heir.name;
    if (ik->pole == bone.name) ik->pole = heir.name;
  });
  GameObject *parent = bone.parent;
  const std::vector<GameObject *> kids = bone.children;
  for (GameObject *c : kids) reparent(p, *c, parent);
  p.scene.destroy(&bone);
}

/* The rig at rest: rests are what the skins measure from, and a merge keeps the children's rests in the world. */
void to_rest(GameObject &armature) {
  for (GameObject *b : bones_of(armature)) {
    const Bone *bone = b->get<Bone>();
    Transform t;
    t.position = bone->rest_position, t.rotation = bone->rest_rotation, t.scale = bone->rest_scale, t.euler_hint = bone->rest_euler_hint;
    b->set_local(t);
  }
}

/* A bone of this rig named `name`, other than `but`. */
GameObject *rig_bone_named(GameObject &armature, const std::string &name, const GameObject *but) {
  for (GameObject *b : bones_of(armature))
    if (b != but && b->name == name) return b;
  return nullptr;
}

/* Step 1: Unity's names - the mapped parts by Unity's, the rest translated from MMD's Japanese and without Mixamo's
 * "mixamorig:". A bone in the way that is renamed too steps aside first (review 357: the order had decided); one that
 * keeps its name keeps it, and the part keeps its own. */
void rename_step(Pass &p) {
  const humanoid::Map map = humanoid::map(p.scene, p.armature);
  std::map<uint64_t, std::string> wanted;
  for (int i = 0; i < humanoid::kParts; i++)
    if (map.part[i]) wanted[map.part[i]->id] = unity_name((humanoid::Part)i);
  for (GameObject *b : bones_of(p.armature)) {
    if (wanted.count(b->id)) continue;
    std::string n = b->name;
    const size_t colon = n.find(':');
    if (colon != std::string::npos) n = n.substr(colon + 1);  // mixamorig:LeftHandIndex1, Armature:Hair_01
    if (!ascii(n)) n = mmd_bone_name(n);
    if (n != b->name && !n.empty()) wanted[b->id] = n;
  }
  for (const auto &[id, name] : wanted) {  // (out of the way: every bone being renamed to a name another holds)
    GameObject *b = p.scene.find(id);
    if (b && b->name != name) rename_bone(p.scene, p.armature, *b, b->name + ".fixmodel");
  }
  for (const auto &[id, name] : wanted) {
    GameObject *b = p.scene.find(id);
    if (!b) continue;
    const std::string was = b->name.size() > 9 && b->name.compare(b->name.size() - 9, 9, ".fixmodel") == 0 ? b->name.substr(0, b->name.size() - 9) : b->name;
    if (was == name) {
      rename_bone(p.scene, p.armature, *b, name);
      continue;
    }
    const bool taken = rig_bone_named(p.armature, name, b) != nullptr;
    rename_bone(p.scene, p.armature, *b, taken ? was : name);
    if (taken) continue;
    p.r.lines.push_back("  " + was + " -> " + name);
    p.r.renamed++;
  }
}

/* Step 2a: MMD's upper body beside its lower body (both under the waist): the spine moved under the hips, as Unity's
 * Humanoid needs every part below the one it hangs from (review 357). */
void spine_under_hips_step(Pass &p) {
  GameObject *hips = rig_bone_named(p.armature, "Hips", nullptr), *spine = rig_bone_named(p.armature, "Spine", nullptr);
  if (!hips || !spine || p.scene.is_ancestor(hips, spine) || p.scene.is_ancestor(spine, hips)) return;
  reparent(p, *spine, hips);
  p.r.lines.push_back("  Spine moved under Hips (the upper body had stood beside the lower)");
}

/* A mapped part at or below `g` other than through `path` (the arms on a chest: that bone is the torso, not a gap). */
bool holds_other_part(const Scene &scene, const GameObject &g, const GameObject *path, const humanoid::Map &map) {
  for (const GameObject *c : g.children) {
    if (c == path) continue;
    for (const GameObject *part : map.part)
      if (part && (part == c || scene.is_ancestor(c, part))) return true;
  }
  return false;
}

/* Step 2b: Hips > Spine > Chest > Neck > Head and the limbs with nothing between - a bone between two parts that holds
 * no other part (an MMD "upper body 3" under the arms is Unity's Upper Chest, and stays) dissolved into the one above;
 * a twist bone kept when asked, a bone a clip moves kept. */
void chain_step(Pass &p) {
  for (int i = 0; i < humanoid::kParts; i++) {
    const humanoid::Map map = humanoid::map(p.scene, p.armature);  // (again a part: the last one dissolved bones)
    GameObject *part = map.part[i];
    if (!part) continue;
    humanoid::Part q = kUp[i];
    while (q != humanoid::kParts && !map.part[q]) q = kUp[q];
    GameObject *above = q == humanoid::kParts ? &p.armature : map.part[q];
    std::vector<GameObject *> between;
    GameObject *below = part;
    GameObject *g = part->parent;
    for (; g && g != above && g != &p.armature; below = g, g = g->parent)
      if (!holds_other_part(p.scene, *g, below, map)) between.push_back(g);
    if (g != above) continue;
    for (GameObject *b : between) {
      if (!b->get<Bone>() || (p.o.keep_twist && is_twist(*b)) || carries_more(*b)) continue;
      if (animated(p, *b)) {
        p.r.lines.push_back("  " + b->name + " kept: a clip moves it");
        continue;
      }
      GameObject *heir = b->parent && b->parent->get<Bone>() ? b->parent : part;  // (above the hips: the hips take it)
      p.r.lines.push_back("  " + b->name + " merged into " + heir->name);
      dissolve(p, *b, *heir);
      p.r.merged++;
    }
  }
}

/* Step 3: IK and its targets and poles, which VRChat doesn't run. */
void constraints_step(Pass &p) {
  std::vector<std::pair<GameObject *, IKConstraint *>> iks;
  p.scene.for_each([&](GameObject &g) {
    if (IKConstraint *ik = g.get<IKConstraint>(); ik && p.scene.is_ancestor(&p.armature, &g)) iks.push_back({&g, ik});
  });
  for (auto &[g, ik] : iks) {
    g->remove_component(ik);
    p.r.constraints++;
  }
}

/* `b` is a mapped part or has one below it (the chest the arms hang from). */
bool holds_part(const Scene &scene, const GameObject &b, const humanoid::Map &map) {
  for (const GameObject *part : map.part)
    if (part && (part == &b || scene.is_ancestor(&b, part))) return true;
  return false;
}

/* `b` is an IK's target or pole (kept with the IK: review 357). */
bool ik_needs(Pass &p, const GameObject &b) {
  bool needed = false;
  p.scene.for_each([&](GameObject &g) {
    const IKConstraint *ik = g.get<IKConstraint>();
    needed = needed || (ik && p.scene.is_ancestor(&p.armature, &g) && (ik->target == b.name || ik->pole == b.name));
  });
  return needed;
}

/* Step 4: bones that move nothing - leaves first, so a chain of empty bones goes whole; the parts and the bones above
 * them, Unity's part names, eyes, twist bones (when kept), bones carrying more than a Bone, bones a clip moves and an
 * IK's targets stay. */
void zero_weight_step(Pass &p) {
  const humanoid::Map map = humanoid::map(p.scene, p.armature);
  std::vector<GameObject *> bones = bones_of(p.armature);
  std::reverse(bones.begin(), bones.end());
  const std::vector<GameObject *> meshes = avatar_meshes(p.scene, p.armature);
  for (GameObject *b : bones) {
    if (holds_part(p.scene, *b, map) || is_unity_part_name(b->name)) continue;  // (the skeleton Unity maps: a hand that moves nothing still is one)
    if (is_eye(*b) || (p.o.keep_twist && is_twist(*b)) || carries_more(*b) || animated(p, *b) || ik_needs(p, *b)) continue;
    if (group_sum(meshes, b->name) > 0.0f) continue;
    /* (Leaves first: what's still below it was kept - a weighted bone, an eye - and moves up to its parent.) */
    const bool parent_bone = b->parent && b->parent->get<Bone>();
    if (!b->children.empty() && !parent_bone) continue;
    p.r.lines.push_back("  " + b->name + " moved nothing: removed");
    if (parent_bone) dissolve(p, *b, *b->parent);
    else p.scene.destroy(b);
    p.r.removed++;
  }
}

/* Step 5: groups whose every weight is 0. */
void empty_groups_step(Pass &p) {
  for (GameObject *g : avatar_meshes(p.scene, p.armature)) {
    const MeshFilter *mf = g->get<MeshFilter>();
    const auto empty = [](const VertexGroup &vg) { return std::all_of(vg.weights.begin(), vg.weights.end(), [](float w) { return w <= 0.0f; }); };
    if (!mf || !mf->mesh || std::none_of(mf->mesh->vertex_groups.begin(), mf->mesh->vertex_groups.end(), empty)) continue;
    Mesh &m = *mutable_mesh(*g);
    const size_t before = m.vertex_groups.size();
    m.vertex_groups.erase(std::remove_if(m.vertex_groups.begin(), m.vertex_groups.end(), empty), m.vertex_groups.end());
    p.r.groups += (int)(before - m.vertex_groups.size());
    m.touch();
  }
}

/* A mesh Join can take whole: only its Skin modifying it, its groups and keys fitting (review 357: a Mirror's half or
 * a short group would have been lost silently). Why not, else "". */
std::string unjoinable(const GameObject &g) {
  for (const auto &c : g.components)
    if (c->is_modifier() && !dynamic_cast<const SkinModifier *>(c.get())) return std::string("its ") + c->type_name() + " (apply it first)";
  const Mesh &m = *g.get<MeshFilter>()->mesh;
  if (!m.groups_fit() || !m.keys_fit()) return "its vertex groups or shape keys don't fit the mesh";
  return "";
}

/* Step 6: the meshes into the biggest, named Body when that name is free; slots kept, the same material one slot; the
 * parts' key weights and their clips' tracks onto Body. A mesh Join can't take whole stays apart, said. */
void join_step(Pass &p) {
  std::vector<GameObject *> meshes;
  for (GameObject *g : avatar_meshes(p.scene, p.armature)) {
    const std::string why = unjoinable(*g);
    if (why.empty()) meshes.push_back(g);
    else p.r.lines.push_back("  " + g->name + " not joined: " + why);
  }
  if (meshes.size() < 2) return;
  GameObject *body = *std::max_element(meshes.begin(), meshes.end(), [](GameObject *a, GameObject *b) {
    return a->get<MeshFilter>()->mesh->vert_count() < b->get<MeshFilter>()->mesh->vert_count();
  });
  /* Out from under any mesh joined into it (review 357: destroying that parent had destroyed Body mid-join). */
  for (bool moved = true; moved;) {
    moved = false;
    for (GameObject *o : meshes)
      if (o != body && p.scene.is_ancestor(o, body)) p.scene.set_parent(body, o->parent, -1, true), moved = true;
  }
  MeshRenderer *br = body->get<MeshRenderer>();
  if (!br) br = body->add<MeshRenderer>();
  Mesh &dst = *mutable_mesh(*body);
  const Mat4 inv = body->world_matrix().inverse();
  for (GameObject *o : meshes) {
    if (o == body) continue;
    const Mesh &src = *o->get<MeshFilter>()->mesh;
    std::vector<int> remap;
    const MeshRenderer *sr = o->get<MeshRenderer>();
    for (size_t s = 0; sr && s < sr->materials.size(); s++) {
      const auto at = std::find(br->materials.begin(), br->materials.end(), sr->materials[s]);
      remap.push_back(at != br->materials.end() ? (int)(at - br->materials.begin()) : (int)br->materials.size());
      if (at == br->materials.end()) br->materials.push_back(sr->materials[s]);
    }
    for (const ShapeKey &k : src.shape_keys)
      if (sr && sr->shape_weight(k.name) != 0.0f) br->set_shape_weight(k.name, sr->shape_weight(k.name));
    meshops::append_mesh(dst, src, inv * o->world_matrix(), remap, true);
    const std::vector<GameObject *> kids = o->children;
    for (GameObject *c : kids) p.scene.set_parent(c, body, -1, true);
    p.r.lines.push_back("  " + o->name + " joined into " + body->name);
    p.joined_into[o->id] = body->id;
    p.scene.destroy(o);
    p.r.joined++;
  }
  dst.touch();
  if (!p.scene.find_by_name("Body")) body->name = "Body";
}

/* A material's values without its name: two slots with the same are one. */
std::string material_key(const MaterialPtr &m) {
  if (!m) return "";
  const std::string name = m->name;
  m->name.clear();
  std::string text = save_material_text(*m);
  m->name = name;
  return text;
}

/* Step 7: slots holding the same material (by its values) become one; the faces follow. */
void materials_step(Pass &p) {
  for (GameObject *g : avatar_meshes(p.scene, p.armature)) {
    MeshRenderer *mr = g->get<MeshRenderer>();
    if (!mr || mr->materials.size() < 2) continue;
    std::vector<int> to(mr->materials.size());
    std::vector<MaterialPtr> kept;
    std::vector<std::string> keys;
    for (size_t s = 0; s < mr->materials.size(); s++) {
      const std::string k = material_key(mr->materials[s]);
      const auto at = std::find(keys.begin(), keys.end(), k);
      to[s] = at != keys.end() ? (int)(at - keys.begin()) : (int)keys.size();
      if (at == keys.end()) keys.push_back(k), kept.push_back(mr->materials[s]);
    }
    if (kept.size() == mr->materials.size()) continue;
    Mesh &m = *mutable_mesh(*g);
    for (int &f : m.face_material) f = f >= 0 && f < (int)to.size() ? to[(size_t)f] : f;
    p.r.slots += (int)(mr->materials.size() - kept.size());
    mr->materials = std::move(kept);
    m.touch();
  }
}

/* Last: each noted track onto its object's path now - a joined mesh's (its key weights) onto Body's. Counts the paths
 * that changed. */
void rewrite_clip_paths(Pass &p) {
  for (ClipPaths &cp : p.clips) {
    for (AnimTrack &t : cp.clip->tracks) {
      uint64_t id = noted_object(cp, t);
      if (!id) continue;
      if (!p.scene.find(id) && p.joined_into.count(id)) id = p.joined_into[id];
      GameObject *g = p.scene.find(id);
      if (!g) continue;
      const std::string path = anim_path(*cp.player, *g);
      if (path == t.path) continue;
      t.path = path;
      p.r.tracks++;
    }
    cp.clip->touch();
  }
}

}  // namespace

std::vector<GameObject *> avatar_meshes(Scene &scene, const GameObject &armature) {
  std::vector<GameObject *> out;
  scene.for_each([&](GameObject &g) {
    const SkinModifier *s = g.get<SkinModifier>();
    const MeshFilter *mf = g.get<MeshFilter>();
    /* (By name, as the skin finds its rig - and that name this rig's: two rigs called Armature are two avatars.) */
    if (s && s->object == armature.name && mf && mf->mesh && scene.find_by_name(s->object) == &armature) out.push_back(&g);
  });
  return out;
}

void rename_bone(Scene &scene, GameObject &armature, GameObject &bone, const std::string &name) {
  const std::string old = bone.name;
  if (old == name) return;
  for (GameObject *m : avatar_meshes(scene, armature)) move_group(*m, old, name);
  scene.for_each([&](GameObject &g) {
    IKConstraint *ik = g.get<IKConstraint>();
    if (!ik || !scene.is_ancestor(&armature, &g)) return;
    if (ik->target == old) ik->target = name;
    if (ik->pole == old) ik->pole = name;
  });
  if (Armature *a = armature.get<Armature>()) a->puppet_bones = rename_in_handles(a->puppet_bones, old, name);
  bone.name = name;
}

bool merge_bone_into_parent(Scene &scene, GameObject &armature, GameObject &bone) {
  if (!bone.parent || !bone.parent->get<Bone>() || carries_more(bone)) return false;
  to_rest(armature);
  FixModelOptions o;
  FixModelReport r;
  Pass p{scene, armature, o, r, note_clip_paths(armature), {}};
  if (animated(p, bone)) return false;
  dissolve(p, bone, *bone.parent);
  rewrite_clip_paths(p);
  return true;
}

void put_at_rest(GameObject &armature) { to_rest(armature); }

bool clip_moves(GameObject &armature, const GameObject &g) {
  for (const ClipPaths &cp : note_clip_paths(armature))
    for (const AnimTrack &t : cp.clip->tracks)
      if (noted_object(cp, t) == g.id) return true;
  return false;
}

int remove_zero_weight_bones(Scene &scene, GameObject &armature, const FixModelOptions &o, FixModelReport &r) {
  to_rest(armature);
  Pass p{scene, armature, o, r, note_clip_paths(armature), {}};
  zero_weight_step(p);
  empty_groups_step(p);
  rewrite_clip_paths(p);
  return r.removed + r.groups;
}

int join_avatar_meshes(Scene &scene, GameObject &armature, FixModelReport &r) {
  FixModelOptions o;
  Pass p{scene, armature, o, r, note_clip_paths(armature), {}};
  join_step(p);
  rewrite_clip_paths(p);
  return r.joined;
}

FixModelReport fix_model(Scene &scene, GameObject &armature, const FixModelOptions &o) {
  FixModelReport r;
  to_rest(armature);
  Pass p{scene, armature, o, r, note_clip_paths(armature), {}};
  rename_step(p);
  spine_under_hips_step(p);
  chain_step(p);
  if (o.remove_constraints) constraints_step(p);
  if (o.remove_zero_weight) zero_weight_step(p);
  if (o.remove_zero_weight) empty_groups_step(p);
  if (o.join_meshes) join_step(p);
  if (o.combine_materials) materials_step(p);
  rewrite_clip_paths(p);
  r.lines.insert(r.lines.begin(), strprintf("Fix Model: %d renamed, %d merged, %d bones that moved nothing removed, %d constraints, %d empty groups, "
                                            "%d meshes joined, %d slots combined, %d clip tracks moved",
                                            r.renamed, r.merged, r.removed, r.constraints, r.groups, r.joined, r.slots, r.tracks));
  return r;
}

}  // namespace bl
