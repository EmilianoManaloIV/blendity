// SPDX-License-Identifier: GPL-2.0-or-later
// VRChat's visemes and an avatar's checklist (round 356, task 0333): see visemes.h.
#include "visemes.h"

#include "budget.h"
#include "gait.h"
#include "humanoid.h"
#include "mesh.h"
#include "scene.h"
#include "../core/core.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace bl {

const std::vector<std::string> &vrchat_viseme_names() {
  static const std::vector<std::string> kNames = {"vrc.v_sil", "vrc.v_pp", "vrc.v_ff", "vrc.v_th", "vrc.v_dd", "vrc.v_kk", "vrc.v_ch", "vrc.v_ss",
                                                  "vrc.v_nn",  "vrc.v_rr", "vrc.v_aa", "vrc.v_e",  "vrc.v_ih", "vrc.v_oh", "vrc.v_ou"};
  return kNames;
}

namespace {

/* Each viseme as a mix of the three sculpted mouths (AA, OH, CH), in vrchat_viseme_names()' order - CATS' Create Visemes'
 * table (github.com/absolute-quantum/cats-blender-plugin, tools/viseme.py, MIT). */
struct Mix {
  float aa, oh, ch;
};
const Mix kMixes[15] = {
    {0.0002f, 0.0f, 0.0002f},  // sil (all but still: a key that moves nothing is dropped by Unity's import)
    {0.0004f, 0.0004f, 0.0f},  // pp
    {0.2f, 0.0f, 0.4f},        // ff
    {0.4f, 0.4f, 0.0f},        // th
    {0.3f, 0.0f, 0.7f},        // dd
    {0.7f, 0.0f, 0.4f},        // kk
    {0.0f, 0.0f, 0.9996f},     // ch
    {0.0f, 0.0f, 0.8f},        // ss
    {0.2f, 0.0f, 0.7f},        // nn
    {0.0f, 0.3f, 0.5f},        // rr
    {0.9998f, 0.0f, 0.0f},     // aa
    {0.5f, 0.0f, 0.2f},        // e
    {0.0f, 0.3f, 0.7f},        // ih
    {0.2f, 0.8f, 0.0f},        // oh
    {0.0f, 0.9994f, 0.0f},     // ou
};

const ShapeKey *key_named(const Mesh &m, const std::string &name) {
  for (const ShapeKey &k : m.shape_keys)
    if (k.name == name) return &k;
  return nullptr;
}

bool moves(const ShapeKey &k) {
  for (const Vec3 &d : k.delta)
    if (std::fabs(d.x) + std::fabs(d.y) + std::fabs(d.z) > 1e-7f) return true;
  return false;
}

}  // namespace

std::string viseme_bases_problem(const Mesh &m, const std::string &aa, const std::string &oh, const std::string &ch) {
  const char *const kWhich[3] = {"an open mouth (AA)", "a round mouth (OH)", "a wide mouth (CH)"};
  const std::string names[3] = {aa, oh, ch};
  for (int b = 0; b < 3; b++) {
    const ShapeKey *k = key_named(m, names[b]);
    if (!k) return "no shape key '" + names[b] + "' - sculpt " + kWhich[b] + " first (Add Shape Key)";
    if (k->delta.size() != m.vert_count()) return "'" + names[b] + "' doesn't fit the mesh (its vertices changed since)";
  }
  return "";
}

bool has_visemes(const Mesh &m) {
  for (const std::string &v : vrchat_viseme_names())
    if (key_named(m, v)) return true;
  return false;
}

bool make_visemes(Mesh &m, const std::string &aa, const std::string &oh, const std::string &ch, std::string &why) {
  why = viseme_bases_problem(m, aa, oh, ch);
  if (!why.empty()) return false;
  const ShapeKey *base[3] = {key_named(m, aa), key_named(m, oh), key_named(m, ch)};
  std::vector<ShapeKey> made;
  for (size_t v = 0; v < 15; v++) {
    ShapeKey k;
    k.name = vrchat_viseme_names()[v];
    k.delta.assign(m.vert_count(), Vec3(0.0f));
    for (size_t i = 0; i < k.delta.size(); i++) k.delta[i] = base[0]->delta[i] * kMixes[v].aa + base[1]->delta[i] * kMixes[v].oh + base[2]->delta[i] * kMixes[v].ch;
    if (!moves(k) && !k.delta.empty()) k.delta[0] = Vec3(0.0f, 1e-5f, 0.0f);  // (the mixes all still: one vertex a hair, so it isn't dropped)
    made.push_back(std::move(k));
  }
  /* Every key of a viseme's name replaced (review 356: a second copy stayed stale - the one Unity may pick). */
  const auto old = [&](const ShapeKey &e) { return e.name.rfind("vrc.v_", 0) == 0 && std::find(vrchat_viseme_names().begin(), vrchat_viseme_names().end(), e.name) != vrchat_viseme_names().end(); };
  m.shape_keys.erase(std::remove_if(m.shape_keys.begin(), m.shape_keys.end(), old), m.shape_keys.end());
  for (ShapeKey &k : made) m.shape_keys.push_back(std::move(k));
  return true;
}

std::vector<std::string> shape_key_problems(const Mesh &m) {
  std::vector<std::string> out;
  std::unordered_map<std::string, int> seen;
  for (const ShapeKey &k : m.shape_keys) {
    if (++seen[k.name] == 2) out.push_back("two shape keys named '" + k.name + "' (Unity keeps one; VRChat's lip sync can pick the wrong one)");
    if (k.delta.size() != m.vert_count()) out.push_back("'" + k.name + "' doesn't fit the mesh (its vertices changed since): it does nothing");
    else if (!moves(k)) out.push_back("'" + k.name + "' moves nothing (Unity drops it on import)");
  }
  if (!has_visemes(m)) return out;
  std::string missing;
  for (const std::string &v : vrchat_viseme_names())
    if (!key_named(m, v)) missing += (missing.empty() ? "" : ", ") + v.substr(6);
  if (!missing.empty()) out.push_back("visemes missing: " + missing + " (VRChat's lip sync needs all 15; Rigging > Create Visemes)");
  return out;
}

namespace {

/* The meshes an armature deforms (their Skin names it). */
std::vector<GameObject *> skinned_by(Scene &scene, const GameObject &armature) {
  std::vector<GameObject *> out;
  scene.for_each([&](GameObject &g) {
    const SkinModifier *s = g.get<SkinModifier>();
    if (s && s->object == armature.name && g.get<MeshFilter>()) out.push_back(&g);
  });
  return out;
}

/* Unity Humanoid's bone name for a part: the part's name without its spaces ("LeftUpperArm"). */
std::string unity_name(humanoid::Part p) {
  std::string s = humanoid::kPartNames[p];
  s.erase(std::remove(s.begin(), s.end(), ' '), s.end());
  return s;
}

/* The first skinned mesh with a viseme (the face), else the first skinned mesh. */
GameObject *face_of(const std::vector<GameObject *> &meshes) {
  for (GameObject *g : meshes) {
    const MeshFilter *mf = g->get<MeshFilter>();
    if (mf && mf->mesh && has_visemes(*mf->mesh)) return g;
  }
  return meshes.empty() ? nullptr : meshes.front();
}

AvatarCheck item(const char *name, bool ok, const std::string &detail, const std::string &fix = "", uint64_t on = 0) {
  AvatarCheck c;
  c.item = name, c.ok = ok, c.detail = detail, c.fix = fix, c.on = on;
  return c;
}

}  // namespace

AABB rest_bounds(const GameObject &g) {
  AABB out;
  const MeshFilter *mf = g.get<MeshFilter>();
  if (!mf || !mf->mesh) return out;
  const AABB b = mf->mesh->bounds();
  if (!b.valid()) return out;
  const Mat4 &w = g.world_matrix();
  for (int c = 0; c < 8; c++) out.add(w.point(Vec3(c & 1 ? b.max.x : b.min.x, c & 2 ? b.max.y : b.min.y, c & 4 ? b.max.z : b.min.z)));
  return out;
}

std::vector<GameObject *> avatar_objects(Scene &scene, GameObject &armature) {
  std::vector<GameObject *> all{&armature};
  for (GameObject *g : skinned_by(scene, armature)) all.push_back(g);
  scene.for_each([&](GameObject &g) {
    if (&g != &armature && scene.is_ancestor(&armature, &g)) all.push_back(&g);
  });
  return all;
}

std::vector<AvatarCheck> avatar_check(Scene &scene, GameObject &armature) {
  std::vector<AvatarCheck> out;
  const humanoid::Map map = humanoid::map(scene, armature);
  static const humanoid::Part kNeeded[] = {humanoid::Hips,      humanoid::Spine,     humanoid::Head,      humanoid::LUpperArm, humanoid::LLowerArm,
                                           humanoid::LHand,     humanoid::RUpperArm, humanoid::RLowerArm, humanoid::RHand,     humanoid::LUpperLeg,
                                           humanoid::LLowerLeg, humanoid::LFoot,     humanoid::RUpperLeg, humanoid::RLowerLeg, humanoid::RFoot};
  std::string missing, renamed;
  for (humanoid::Part p : kNeeded) {
    if (!map.part[p]) missing += (missing.empty() ? "" : ", ") + std::string(humanoid::kPartNames[p]);
    else if (map.part[p]->name != unity_name(p)) renamed += (renamed.empty() ? "" : ", ") + map.part[p]->name;
  }
  out.push_back(item("Humanoid bones", missing.empty(), missing.empty() ? "the 15 Unity's Humanoid needs, found" : "not found: " + missing));
  out.push_back(item("Unity's bone names", renamed.empty(), renamed.empty() ? "as Unity names them: Rig > Humanoid maps them at once"
                                                                          : "named otherwise: " + renamed + " (Unity's Configure... maps them by hand)"));
  out.push_back(item("T or A pose", map.valid(), map.valid() ? (map.a_pose() ? "an A pose" : "a T pose") : map.problem));
  const std::vector<GameObject *> meshes = skinned_by(scene, armature);
  AABB b;
  for (GameObject *g : meshes) b.add(rest_bounds(*g));
  const float h = meshes.empty() ? 0.0f : b.max.y - b.min.y;
  out.push_back(item("Height 0.5 to 5 m", h >= 0.5f && h <= 5.0f, meshes.empty() ? "no mesh skinned to it" : strprintf("%.2f m", h)));
  const Vec3 at = armature.world_position();
  out.push_back(item("At the origin", length(at) < 0.01f, strprintf("its root at (%.2f, %.2f, %.2f)", at.x, at.y, at.z),
                     "avatar origin", armature.id));  // (its world place: a parent's offset too - review 356)
  const gait::Rig rig = gait::analyse(scene, armature);
  const bool upright = map.part[humanoid::Head] && map.part[humanoid::Hips] && map.part[humanoid::Head]->world_position().y > map.part[humanoid::Hips]->world_position().y;
  const bool facing = rig.valid() && dot(normalize(rig.forward), Vec3(0, 0, 1)) > 0.7f;
  const bool parts = map.part[humanoid::Head] && map.part[humanoid::Hips];
  const char *upright_detail = !parts ? "no head and hips found (see Humanoid bones)" : !upright ? "the head isn't above the hips" : facing ? "facing +Z (Unity's forward)" : "not facing +Z: turn the root";
  out.push_back(item("Upright, facing +Z", upright && facing, upright_detail));
  GameObject *face = face_of(meshes);
  const MeshFilter *fmf = face ? face->get<MeshFilter>() : nullptr;
  size_t have = 0;
  for (const std::string &v : vrchat_viseme_names()) have += fmf && fmf->mesh && key_named(*fmf->mesh, v) ? 1 : 0;
  out.push_back(item("Visemes", have == 15, strprintf("%zu of 15 vrc.v_* keys", have), face ? "visemes" : "", face ? face->id : 0));
  bool eyes = false;
  scene.for_each([&](GameObject &g) { eyes = eyes || (g.get<Bone>() && scene.is_ancestor(&armature, &g) && to_lower(g.name).find("eye") != std::string::npos); });
  out.push_back(item("Eye bones (Eye Look)", eyes, eyes ? "found" : "none (optional: the Avatar Descriptor's Eye Look needs LeftEye / RightEye)", eyes ? "" : "avatar eyes",
                     armature.id));  // (round 360: Eye Tracking sets them up)
  const BudgetTarget &pc = budget_targets()[0];
  const BudgetResult r = budget_rank(pc, budget_count(avatar_objects(scene, armature)));
  out.push_back(item("VRChat PC rank", r.rank < (int)pc.ranks.size(), budget_rank_name(pc, r.rank), "avatar decimate", armature.id));
  return out;
}

}  // namespace bl
