// SPDX-License-Identifier: GPL-2.0-or-later
// The avatar module's Model Options and Pose and Shape tools (round 358, task 0335): see avatar_tools.h.
#include "avatar_tools.h"

#include "avatar_fix.h"
#include "humanoid.h"
#include "mesh.h"
#include "scene.h"
#include "visemes.h"
#include "../core/core.h"

#include <algorithm>
#include <cmath>

namespace bl {

namespace {

/* The base mesh without its keys, copied once (review 358: a copy a key had made Apply as Rest O(keys squared)). */
Mesh keyless(const Mesh &base) {
  Mesh m = base;
  m.shape_keys.clear();
  return m;
}

/* `plain` (a keyless copy of the base) with `offsets` added, through `skin` as the modifier stack runs it (its vertex
 * group limit honoured), in the mesh's own space. */
Mesh skinned(const SkinModifier &skin, const Mesh &plain, const std::vector<Vec3> *offsets) {
  Mesh m = plain;
  for (size_t i = 0; offsets && i < m.positions.size() && i < offsets->size(); i++) m.positions[i] += (*offsets)[i];
  run_modifier(skin, m);
  return m;
}

const SkinModifier *live_skin(const GameObject &g) {
  const SkinModifier *s = g.get<SkinModifier>();
  return s && s->enabled ? s : nullptr;
}

void bones_below(GameObject &g, std::vector<GameObject *> &out) {
  for (GameObject *c : g.children) {
    if (c->get<Bone>()) out.push_back(c);
    bones_below(*c, out);
  }
}

/* "vrc.v_aa" -> its place in VRChat's order; blinks and lower lids after; -1 the rest. */
int viseme_rank(const std::string &name) {
  const std::vector<std::string> &v = vrchat_viseme_names();
  const auto at = std::find(v.begin(), v.end(), name);
  if (at != v.end()) return (int)(at - v.begin());
  static const char *const kEyes[] = {"vrc.blink_left", "vrc.blink_right", "vrc.lowerlid_left", "vrc.lowerlid_right"};
  for (int i = 0; i < 4; i++)
    if (name == kEyes[i]) return 15 + i;
  return -1;
}

/* The bones' rests from where they stand now (the pose made the rest). */
void pose_to_rest(GameObject &armature) {
  std::vector<GameObject *> bones;
  bones_below(armature, bones);
  for (GameObject *b : bones) b->get<Bone>()->set_rest(b->local());
}

/* The rig a skinned mesh follows, for the Rest Position check. */
const GameObject *rig_of(const GameObject &g) {
  const SkinModifier *s = g.get<SkinModifier>();
  return s && g.scene ? skin_armature_of(g, s->object) : nullptr;
}

/* One bone pointed at its one child from the rest, its length the gap; the children kept in place, their rests
 * following. False when there was nothing to do. */
bool connect_one(GameObject &b, GameObject &child) {
  const Vec3 dir = child.world_position() - b.world_position();
  const float len = length(dir);
  if (len < 1e-5f) return false;
  Bone *bone = b.get<Bone>();
  const float want = len / bone_world_unit(b);
  const Vec3 y = normalize(b.world_matrix().dir(Vec3(0, 1, 0)));
  const bool turned = dot(y, dir / len) < 0.99999f;
  const bool longer = std::fabs(bone->length - want) > 1e-5f * std::max(1.0f, want);
  if (!turned && !longer) return false;
  bone->length = want;  // (review 358: an aligned bone's length is set too)
  if (!turned) return true;
  std::vector<std::pair<GameObject *, Mat4>> keep;
  for (GameObject *c : b.children) keep.push_back({c, c->world_matrix()});
  b.set_world_rotation(rotation_between(y, dir / len) * b.world_rotation());
  for (auto &[c, w] : keep) c->set_world_matrix(w);
  bone->set_rest(b.local());
  for (auto &[c, w] : keep)
    if (Bone *cb = c->get<Bone>()) cb->set_rest(c->local());
  bone->length = len / bone_world_unit(b);
  return true;
}

}  // namespace

std::string pose_tools_blocked(const GameObject &armature) {
  const Armature *a = armature.get<Armature>();
  if (a && a->rest_position) return "the Armature shows its Rest Position - untick it first (the skins pose nothing while it's on)";
  return "";
}

std::vector<float> skinned_positions_flat(const GameObject &g) {
  std::vector<float> out;
  const SkinModifier *skin = live_skin(g);
  const MeshFilter *mf = g.get<MeshFilter>();
  if (!skin || !mf || !mf->mesh) return out;
  for (const Vec3 &p : skinned(*skin, keyless(*mf->mesh), nullptr).positions) out.insert(out.end(), {p.x, p.y, p.z});
  return out;
}

bool pose_to_shape_key(GameObject &g, const std::string &name, std::string &why) {
  const SkinModifier *skin = live_skin(g);
  MeshFilter *mf = g.get<MeshFilter>();
  if (name.empty()) return why = "give the key a name", false;
  if (!skin || !mf || !mf->mesh) return why = "select a skinned mesh (an enabled Skin modifier on it)", false;
  const GameObject *rig = rig_of(g);
  if (rig && !(why = pose_tools_blocked(*rig)).empty()) return false;
  const Mesh posed = skinned(*skin, keyless(*mf->mesh), nullptr);
  Mesh &m = *mesh_make_mutable(mf->mesh);
  ShapeKey k{name, std::vector<Vec3>(m.vert_count(), Vec3(0.0f))};
  for (size_t i = 0; i < k.delta.size() && i < posed.positions.size(); i++) k.delta[i] = posed.positions[i] - m.positions[i];
  const int at = m.find_shape(name);
  if (at >= 0) m.shape_keys[(size_t)at] = std::move(k);
  else m.shape_keys.push_back(std::move(k));
  m.touch();
  return true;
}

int apply_pose_as_rest(Scene &scene, GameObject &armature) {
  int changed = 0;
  for (GameObject *g : avatar_meshes(scene, armature)) {
    const SkinModifier *skin = live_skin(*g);
    if (!skin) continue;
    MeshFilter *mf = g->get<MeshFilter>();
    const Mesh &base = *mf->mesh;
    const Mesh plain = keyless(base);
    const Mesh posed = skinned(*skin, plain, nullptr);
    std::vector<std::vector<Vec3>> keys;
    for (const ShapeKey &k : base.shape_keys) keys.push_back(k.delta.size() == base.vert_count() ? skinned(*skin, plain, &k.delta).positions : k.delta);
    Mesh &m = *mesh_make_mutable(mf->mesh);
    for (size_t k = 0; k < m.shape_keys.size(); k++) {
      if (m.shape_keys[k].delta.size() != m.vert_count()) continue;
      for (size_t i = 0; i < m.vert_count(); i++) m.shape_keys[k].delta[i] = keys[k][i] - posed.positions[i];
    }
    m.positions = posed.positions;
    if (m.custom_normals_fit() && posed.custom_normals_fit()) m.custom_normals = posed.custom_normals;  // (review 358: lit the old way otherwise)
    m.touch();
    changed++;
  }
  pose_to_rest(armature);
  return changed;
}

bool apply_shape_key_as_basis(Mesh &m, const std::string &name, std::string &why) {
  const int at = m.find_shape(name);
  if (at < 0) return why = "no shape key '" + name + "'", false;
  if (m.shape_keys[(size_t)at].delta.size() != m.vert_count()) return why = "'" + name + "' doesn't fit the mesh (its vertices changed since)", false;
  ShapeKey reverted{name + " - Reverted", m.shape_keys[(size_t)at].delta};
  for (size_t i = 0; i < m.vert_count(); i++) m.positions[i] += reverted.delta[i], reverted.delta[i] = -reverted.delta[i];
  m.shape_keys.erase(m.shape_keys.begin() + at);
  m.shape_keys.push_back(std::move(reverted));
  m.touch();
  return true;
}

void sort_shape_keys(Mesh &m) {
  std::stable_sort(m.shape_keys.begin(), m.shape_keys.end(), [](const ShapeKey &a, const ShapeKey &b) {
    const int ra = viseme_rank(a.name), rb = viseme_rank(b.name);
    if (ra < 0 || rb < 0) return ra >= 0 && rb < 0;  // (ranked first; the rest keep their order)
    return ra < rb;
  });
}

bool arms_to_pose(Scene &scene, GameObject &armature, float degrees, std::string &why) {
  if (!(why = pose_tools_blocked(armature)).empty()) return false;
  put_at_rest(armature);
  const humanoid::Map map = humanoid::map(scene, armature);
  const humanoid::Part uppers[2] = {humanoid::LUpperArm, humanoid::RUpperArm}, lowers[2] = {humanoid::LLowerArm, humanoid::RLowerArm};
  for (int s = 0; s < 2; s++)
    if (!map.part[uppers[s]] || !map.part[lowers[s]]) return why = "no upper and lower arms found on both sides", false;
  /* The rig's own frame: its sideways and up (review 358: a rig facing +X had its arms swung along the world's X). */
  const Quat q = armature.world_rotation();
  const Vec3 side = normalize(q.rotate(Vec3(1, 0, 0))), up = normalize(q.rotate(Vec3(0, 1, 0)));
  const float a = degrees * 3.14159265f / 180.0f;
  for (int s = 0; s < 2; s++) {
    const Vec3 d = normalize(map.part[lowers[s]]->world_position() - map.part[uppers[s]]->world_position());
    if (std::fabs(dot(d, side)) < 0.3f) return why = "an arm doesn't reach out to the rig's side (is the rig facing +Z?)", false;
  }
  for (int s = 0; s < 2; s++) {
    GameObject *upper = map.part[uppers[s]], *lower = map.part[lowers[s]];
    const Vec3 d = normalize(lower->world_position() - upper->world_position());
    const float sx = dot(d, side) < 0.0f ? -1.0f : 1.0f;
    const Vec3 t = normalize(side * (sx * std::cos(a)) - up * std::sin(a));
    upper->set_world_rotation(rotation_between(d, t) * upper->world_rotation());
  }
  apply_pose_as_rest(scene, armature);
  return true;
}

int connect_bones(Scene &scene, GameObject &armature, std::vector<std::string> &skipped) {
  put_at_rest(armature);  // (review 358: a posed rig's rests had become its pose, the skins snapping back)
  std::vector<GameObject *> bones;
  bones_below(armature, bones);
  int changed = 0;
  for (GameObject *b : bones) {
    std::vector<GameObject *> kids;
    for (GameObject *c : b->children)
      if (c->get<Bone>()) kids.push_back(c);
    if (kids.size() != 1) continue;
    /* (Review 358: a clip's keys on the bone or its child are absolute local values - turned, they'd twist the body.) */
    if (clip_moves(armature, *b) || clip_moves(armature, *kids[0])) {
      skipped.push_back(b->name);
      continue;
    }
    changed += connect_one(*b, *kids[0]) ? 1 : 0;
  }
  return changed;
}

size_t remove_doubles_keeping_keys(Mesh &m, float dist) {
  std::vector<uint8_t> free(m.vert_count(), 1);
  for (const ShapeKey &k : m.shape_keys)
    for (size_t i = 0; i < k.delta.size() && i < free.size(); i++)
      if (length(k.delta[i]) > 1e-7f) free[i] = 0;
  return meshops::merge_by_distance_selected(m, dist, free);
}

}  // namespace bl
