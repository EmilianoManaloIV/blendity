// SPDX-License-Identifier: GPL-2.0-or-later
// The avatar module's Eye Tracking (round 360, task 0337): see avatar_eyes.h.
#include "avatar_eyes.h"

#include "avatar_fix.h"
#include "gait.h"
#include "humanoid.h"
#include "mesh.h"
#include "scene.h"
#include "../core/core.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace bl {

namespace {

/* A name for an eye, not its brow, lashes or lids. */
bool eye_name(const std::string &name) {
  const std::string n = to_lower(name);
  if (n.find("eye") == std::string::npos) return false;
  for (const char *not_eye : {"brow", "lash", "eyelid", "line", "shadow", "blink"})
    if (n.find(not_eye) != std::string::npos) return false;
  return true;
}

void bones_below(GameObject &g, std::vector<GameObject *> &out) {
  for (GameObject *c : g.children) {
    if (c->get<Bone>()) out.push_back(c);
    bones_below(*c, out);
  }
}

/* The rig's bone named `name` (review 360: a second avatar's LeftEye had been found first). */
GameObject *rig_bone(GameObject &armature, const std::string &name) {
  std::vector<GameObject *> bones;
  bones_below(armature, bones);
  for (GameObject *b : bones)
    if (b->name == name) return b;
  return nullptr;
}

/* One eye vertex: its mesh object, its index and where it is in the world. */
struct EyeVert {
  GameObject *mesh;
  size_t index;
  Vec3 world;
};

void eye_group_vertices(GameObject &g, std::vector<EyeVert> &out) {
  const Mesh &m = *g.get<MeshFilter>()->mesh;
  const Mat4 &w = g.world_matrix();
  for (const VertexGroup &vg : m.vertex_groups) {
    if (!eye_name(vg.name)) continue;
    for (size_t i = 0; i < vg.weights.size() && i < m.vert_count(); i++)
      if (vg.weights[i] > 0.5f) out.push_back({&g, i, w.point(m.positions[i])});
  }
}

void whole_mesh_vertices(GameObject &g, std::vector<EyeVert> &out) {
  const Mesh &m = *g.get<MeshFilter>()->mesh;
  for (size_t i = 0; i < m.vert_count(); i++) out.push_back({&g, i, g.world_matrix().point(m.positions[i])});
}

/* The eye vertices on the avatar: in eye vertex groups (weight over a half) of its skinned meshes, and every vertex of
 * meshes named for an eye that are skinned to the rig or under it. */
std::vector<EyeVert> eye_vertices(Scene &scene, GameObject &armature) {
  std::vector<EyeVert> out;
  for (GameObject *g : avatar_meshes(scene, armature)) {
    if (eye_name(g->name)) whole_mesh_vertices(*g, out);
    else eye_group_vertices(*g, out);
  }
  scene.for_each([&](GameObject &g) {
    const MeshFilter *mf = g.get<MeshFilter>();
    if (mf && mf->mesh && !g.get<SkinModifier>() && eye_name(g.name) && scene.is_ancestor(&armature, &g)) whole_mesh_vertices(g, out);
  });
  return out;
}

/* Anything to set up (checked before the rig is put at rest - review 360: a failed setup had lost the pose). */
bool has_work(Scene &scene, GameObject &armature, const EyeOptions &o) {
  bool any = false;
  if (o.movement) {
    for (GameObject *g : avatar_meshes(scene, armature)) {
      const Mesh &m = *g->get<MeshFilter>()->mesh;
      any = any || eye_name(g->name) || std::any_of(m.vertex_groups.begin(), m.vertex_groups.end(), [](const VertexGroup &vg) { return eye_name(vg.name); });
    }
    scene.for_each([&](GameObject &g) { any = any || (g.get<MeshFilter>() && eye_name(g.name) && scene.is_ancestor(&armature, &g)); });
  }
  for (GameObject *g : o.blinking ? avatar_meshes(scene, armature) : std::vector<GameObject *>()) {
    const Mesh &m = *g->get<MeshFilter>()->mesh;
    any = any || m.find_shape("Blink") >= 0 || m.find_shape("LowerLid") >= 0;
  }
  return any;
}

/* The rig's forward in the world: its gait's, else its +Z. */
Vec3 rig_forward(Scene &scene, GameObject &armature) {
  const gait::Rig rig = gait::analyse(scene, armature);
  return normalize(rig.valid() ? rig.forward : armature.world_rotation().rotate(Vec3(0, 0, 1)));
}

/* An eye bone named `name` under the head at `at`, pointing `fwd`; made, or moved there with its children kept where
 * they are (review 360: a re-run had dragged the eye along). */
GameObject *eye_bone(Scene &scene, GameObject &armature, GameObject &head, const std::string &name, Vec3 at, Vec3 fwd) {
  GameObject *b = rig_bone(armature, name);
  if (!b) {
    b = scene.create(name, &head);
    b->add<Bone>()->length = std::max(0.005f, length(head.world_position() - armature.world_position()) * 0.012f);  // (about 2 cm on a person)
  }
  std::vector<std::pair<GameObject *, Mat4>> kids;
  for (GameObject *c : b->children) kids.push_back({c, c->world_matrix()});
  b->set_world_position(at);
  b->set_world_rotation(rotation_between(Vec3(0, 1, 0), fwd));  // (its +Y the rig's forward - review 360: not times the rig's turn)
  for (auto &[c, w] : kids) c->set_world_matrix(w);
  b->get<Bone>()->set_rest(b->local());
  return b;
}

/* The names of the rig's bones: the groups an eye vertex's weight is taken from (review 360: every group had been
 * zeroed - a Skin's own limit group too, and the eye stopped following). */
std::set<std::string> bone_names(GameObject &armature) {
  std::vector<GameObject *> bones;
  bones_below(armature, bones);
  std::set<std::string> out;
  for (GameObject *b : bones) out.insert(b->name);
  return out;
}

/* A skinned mesh's eye vertices given wholly to their side's bone. */
void weight_skinned(GameObject &g, const std::vector<std::pair<size_t, const GameObject *>> &verts, const std::set<std::string> &bones) {
  Mesh &m = *mesh_make_mutable(g.get<MeshFilter>()->mesh);
  for (const auto &[i, bone] : verts) {
    if (m.find_group(bone->name) < 0) m.vertex_groups.push_back({bone->name, std::vector<float>(m.vert_count(), 0.0f)});
    for (VertexGroup &vg : m.vertex_groups) {
      if (i >= vg.weights.size()) continue;
      if (vg.name == bone->name) vg.weights[i] = 1.0f;
      else if (bones.count(vg.name)) vg.weights[i] = 0.0f;
    }
  }
  m.touch();
}

/* A separate eye mesh: of one eye, parented to its bone; of both (an "Eyes" mesh), skinned to the two (review 360: it
 * had been parented to the right eye, the left orbiting it). */
void follow_unskinned(Scene &scene, GameObject &armature, GameObject &g, const std::vector<std::pair<size_t, const GameObject *>> &verts) {
  std::set<const GameObject *> sides;
  for (const auto &v : verts) sides.insert(v.second);
  if (sides.size() == 1) {
    GameObject *bone = const_cast<GameObject *>(*sides.begin());
    if (g.parent != bone) scene.set_parent(&g, bone, -1, true);
    return;
  }
  scene.set_parent(&g, &armature, -1, true);  // (skinned: it follows the bones, not a parent)
  Mesh &m = *mesh_make_mutable(g.get<MeshFilter>()->mesh);
  m.vertex_groups.clear();
  for (const GameObject *s : sides) m.vertex_groups.push_back({s->name, std::vector<float>(m.vert_count(), 0.0f)});
  for (const auto &[i, bone] : verts) m.vertex_groups[(size_t)m.find_group(bone->name)].weights[i] = 1.0f;
  m.touch();
  SkinModifier *s = g.get<SkinModifier>();
  if (!s) s = g.add<SkinModifier>();
  s->object = armature.name;
}

/* The left in the rig's frame: the side its left arm is on (review 360: an unconverted rig's left is +X), else -X. */
float left_sign(Scene &scene, GameObject &armature, const Mat4 &rig_inv, float mid) {
  const humanoid::Map map = humanoid::map(scene, armature);
  const GameObject *arm = map.part[humanoid::LUpperArm] ? map.part[humanoid::LUpperArm] : map.part[humanoid::LShoulder];
  if (!arm) return -1.0f;
  return rig_inv.point(arm->world_position()).x < mid ? -1.0f : 1.0f;
}

}  // namespace

bool split_key_by_mask(Mesh &m, const std::string &key, const std::vector<uint8_t> &left, const std::string &left_name, const std::string &right_name) {
  const int at = m.find_shape(key);
  if (at < 0 || m.shape_keys[(size_t)at].delta.size() != m.vert_count() || left.size() != m.vert_count()) return false;
  const std::vector<Vec3> d = m.shape_keys[(size_t)at].delta;
  for (const std::string &name : {left_name, right_name}) {
    const bool want = name == left_name;
    ShapeKey k{name, std::vector<Vec3>(m.vert_count(), Vec3(0.0f))};
    for (size_t i = 0; i < m.vert_count(); i++)
      if ((left[i] != 0) == want) k.delta[i] = d[i];
    const int was = m.find_shape(name);
    if (was >= 0) m.shape_keys[(size_t)was] = std::move(k);
    else m.shape_keys.push_back(std::move(k));
  }
  m.touch();
  return true;
}

bool split_key_by_side(Mesh &m, const std::string &key, float side_x, const std::string &left_name, const std::string &right_name) {
  std::vector<uint8_t> left(m.vert_count());
  for (size_t i = 0; i < m.vert_count(); i++) left[i] = m.positions[i].x < side_x ? 1 : 0;
  return split_key_by_mask(m, key, left, left_name, right_name);
}

bool setup_eyes(Scene &scene, GameObject &armature, const EyeOptions &o, std::vector<std::string> &lines, std::string &why) {
  const humanoid::Map map = humanoid::map(scene, armature);
  GameObject *head = map.part[humanoid::Head];
  if (!head) return why = "no head found on the rig", false;
  if (!has_work(scene, armature, o)) return why = "no eyes (a vertex group or mesh named for one) and no Blink key to split", false;
  put_at_rest(armature);
  const Mat4 rig_inv = armature.world_matrix().inverse();  // (sides in the rig's frame)
  const float mid = rig_inv.point(head->world_position()).x, ls = left_sign(scene, armature, rig_inv, mid);
  const auto is_left = [&](Vec3 world) { return (rig_inv.point(world).x - mid) * ls > 0.0f; };
  bool any = false;
  if (o.movement) {
    const std::vector<EyeVert> verts = eye_vertices(scene, armature);
    std::vector<const EyeVert *> side[2];
    Vec3 sum[2] = {Vec3(0.0f), Vec3(0.0f)};
    for (const EyeVert &v : verts) {
      const int s = is_left(v.world) ? 0 : 1;
      side[s].push_back(&v);
      sum[s] += v.world;
    }
    const bool both = !side[0].empty() && !side[1].empty();
    if (!both) lines.push_back("  no eyes found on both sides (a vertex group or mesh named for an eye: Eye_L, LeftEye, Eyes)");
    const Vec3 fwd = rig_forward(scene, armature);
    GameObject *eye_bones[2] = {};
    for (int s = 0; s < 2 && both; s++) {
      const Vec3 at = sum[s] / (float)side[s].size();
      eye_bones[s] = eye_bone(scene, armature, *head, s == 0 ? "LeftEye" : "RightEye", at, fwd);
      lines.push_back(strprintf("  %s at (%.3f, %.3f, %.3f), %zu vertices", eye_bones[s]->name.c_str(), at.x, at.y, at.z, side[s].size()));
    }
    /* Each mesh's eye vertices with their side's bone, a mesh at a time. */
    std::map<GameObject *, std::vector<std::pair<size_t, const GameObject *>>> by_mesh;
    for (int s = 0; s < 2 && both; s++)
      for (const EyeVert *v : side[s]) by_mesh[v->mesh].push_back({v->index, eye_bones[s]});
    const std::set<std::string> with_eyes = bone_names(armature);
    for (auto &[g, vs] : by_mesh) {
      if (g->get<SkinModifier>()) weight_skinned(*g, vs, with_eyes);
      else follow_unskinned(scene, armature, *g, vs);
    }
    any = both;
  }
  for (GameObject *g : o.blinking ? avatar_meshes(scene, armature) : std::vector<GameObject *>()) {
    const Mesh &cur = *g->get<MeshFilter>()->mesh;
    const bool blink = cur.find_shape("Blink") >= 0, lid = cur.find_shape("LowerLid") >= 0;
    if (!blink && !lid) continue;
    /* Each vertex's side in the rig's frame, as the eyes' (review 360: a turned body had blinked the other eye). */
    std::vector<uint8_t> left(cur.vert_count());
    for (size_t i = 0; i < cur.vert_count(); i++) left[i] = is_left(g->world_matrix().point(cur.positions[i])) ? 1 : 0;
    Mesh &m = *mesh_make_mutable(g->get<MeshFilter>()->mesh);
    if (blink) split_key_by_mask(m, "Blink", left, "vrc.blink_left", "vrc.blink_right");
    if (lid) split_key_by_mask(m, "LowerLid", left, "vrc.lowerlid_left", "vrc.lowerlid_right");
    lines.push_back("  " + g->name + ": the blinks split left and right");
    any = true;
  }
  if (!any) return why = "no eyes found on both sides and no Blink key to split", false;
  return true;
}

void eye_test(Scene &scene, GameObject &armature, float yaw, float pitch, float blink) {
  for (const char *name : {"LeftEye", "RightEye"}) {
    GameObject *b = rig_bone(armature, name);
    if (!b) continue;
    b->set_local_rotation(b->get<Bone>()->rest_rotation * Quat::euler(Vec3(-pitch, 0.0f, yaw)));
  }
  for (GameObject *g : avatar_meshes(scene, armature)) {
    MeshRenderer *mr = g->get<MeshRenderer>();
    if (!mr) continue;
    for (const char *k : {"vrc.blink_left", "vrc.blink_right"})
      if (g->get<MeshFilter>()->mesh->find_shape(k) >= 0) mr->set_shape_weight(k, blink);
  }
}

}  // namespace bl
