// SPDX-License-Identifier: GPL-2.0-or-later
// The avatar module's last part (round 361, task 0338): see avatar_merge.h.
#include "avatar_merge.h"

#include "animation.h"
#include "avatar_fix.h"
#include "material.h"
#include "mesh.h"
#include "scene.h"
#include "../core/core.h"

#include <algorithm>
#include <map>
#include <set>

namespace bl {

namespace {

void bones_below(GameObject &g, std::vector<GameObject *> &out) {
  for (GameObject *c : g.children) {
    if (c->get<Bone>()) out.push_back(c);
    bones_below(*c, out);
  }
}

GameObject *bone_named(GameObject &armature, const std::string &name) {
  std::vector<GameObject *> bones;
  bones_below(armature, bones);
  for (GameObject *b : bones)
    if (b->name == name) return b;
  return nullptr;
}

/* MMD's common morphs and materials (the names PMX models ship with), whole names first, then parts. */
const std::map<std::string, std::string> &dictionary() {
  static const std::map<std::string, std::string> kWords = {
      {"\xE3\x81\xBE\xE3\x81\xB0\xE3\x81\x9F\xE3\x81\x8D", "Blink"},                  // まばたき
      {"\xE7\xAC\x91\xE3\x81\x84", "Smile"},                                          // 笑い
      {"\xE3\x82\xA6\xE3\x82\xA3\xE3\x83\xB3\xE3\x82\xAF", "Wink"},                  // ウィンク
      {"\xE3\x82\xA6\xE3\x82\xA3\xE3\x83\xB3\xE3\x82\xAF\xE5\x8F\xB3", "Wink Right"},  // ウィンク右
      {"\xE6\x80\x92\xE3\x82\x8A", "Angry"},                                          // 怒り
      {"\xE5\x9B\xB0\xE3\x82\x8B", "Troubled"},                                       // 困る
      {"\xE3\x81\xAB\xE3\x81\x93\xE3\x82\x8A", "Cheerful"},                           // にこり
      {"\xE7\x9C\x9F\xE9\x9D\xA2\xE7\x9B\xAE", "Serious"},                            // 真面目
      {"\xE3\x81\x82", "A"},                                                           // あ
      {"\xE3\x81\x84", "I"},                                                           // い
      {"\xE3\x81\x86", "U"},                                                           // う
      {"\xE3\x81\x88", "E"},                                                           // え
      {"\xE3\x81\x8A", "O"},                                                           // お
      {"\xE8\x82\x8C", "Skin"},                                                        // 肌
      {"\xE9\xAB\xAA", "Hair"},                                                        // 髪
      {"\xE9\xA1\x94", "Face"},                                                        // 顔
      {"\xE7\x9B\xAE", "Eye"},                                                         // 目
      {"\xE7\x99\xBD\xE7\x9B\xAE", "Eye White"},                                       // 白目
      {"\xE6\x9C\x8D", "Clothes"},                                                     // 服
      {"\xE9\x9D\xB4", "Shoes"},                                                       // 靴
      {"\xE7\x9C\x89", "Eyebrow"},                                                     // 眉
      {"\xE5\x8F\xA3", "Mouth"},                                                       // 口
      {"\xE6\xAD\xAF", "Teeth"},                                                       // 歯
      {"\xE8\x88\x8C", "Tongue"},                                                      // 舌
      {"\xE5\x8F\xB3", "Right"},                                                       // 右
      {"\xE5\xB7\xA6", "Left"},                                                        // 左
      {"\xE4\xB8\x8A", "Up"},                                                          // 上
      {"\xE4\xB8\x8B", "Down"},                                                        // 下
  };
  return kWords;
}

bool ascii(const std::string &s) {
  for (unsigned char c : s)
    if (c >= 0x80) return false;
  return true;
}

}  // namespace

std::string translate_name(const std::string &japanese) {
  if (ascii(japanese)) return "";
  const auto &d = dictionary();
  if (const auto it = d.find(japanese); it != d.end()) return it->second;
  /* By parts: the longest known word at each place, the rest kept (an unknown character keeps the whole name). */
  std::string out;
  for (size_t at = 0; at < japanese.size();) {
    size_t best = 0;
    std::string word;
    for (const auto &[k, v] : d)
      if (k.size() > best && japanese.compare(at, k.size(), k) == 0) best = k.size(), word = v;
    if (!best) return "";
    out += (out.empty() ? "" : " ") + word;
    at += best;
  }
  return out;
}

/* The base bone a bone of theirs hangs from: the base itself for a bone on their root, the base's bone of its parent's
 * name for a bone on a shared one; nullptr when its parent is theirs alone (it goes along with that one). */
GameObject *base_parent_of(GameObject &base, GameObject &other, const GameObject &b, const std::set<std::string> &base_names) {
  if (b.parent == &other) return &base;
  if (!b.parent || !base_names.count(b.parent->name)) return nullptr;
  return bone_named(base, b.parent->name);
}

bool merge_armatures(Scene &scene, GameObject &base, GameObject &other, std::vector<std::string> &lines, std::string &why) {
  if (&base == &other || scene.is_ancestor(&base, &other) || scene.is_ancestor(&other, &base)) return why = "choose two separate armatures", false;
  std::vector<GameObject *> theirs, ours;
  bones_below(other, theirs);
  bones_below(base, ours);
  std::set<std::string> base_names;  // (the base's names before the merge: a moved bone's children follow it, not again)
  for (GameObject *b : ours) base_names.insert(b->name);
  int shared = 0;
  for (GameObject *b : theirs) shared += base_names.count(b->name) ? 1 : 0;
  if (!shared) return why = "no bone of the same name in both (merge needs one to hang the other's bones on - run Fix Model on both first)", false;
  put_at_rest(base);  // (only once the merge will happen - review 361: a refused one had lost the pose)
  put_at_rest(other);
  /* Their meshes skinned to the base (a bone's group finds the base's bone of its name). */
  const std::vector<GameObject *> meshes = avatar_meshes(scene, other);
  for (GameObject *g : meshes) g->get<SkinModifier>()->object = base.name;
  /* Each of their bones the base hasn't, under the base's bone named as its parent (parents first). */
  int moved = 0;
  for (GameObject *b : theirs) {
    if (base_names.count(b->name)) continue;  // (a shared bone: dropped with their root)
    GameObject *parent = base_parent_of(base, other, *b, base_names);
    if (!parent) continue;
    reparent_keeping(scene, base, *b, parent);
    lines.push_back("  " + b->name + " under " + parent->name);
    moved++;
  }
  /* What hangs from a shared bone and isn't a bone (a hat on their Head) to the base's bone of that name, and what's
   * left on their root to the base - all kept where it is (review 361: the hat had gone with their root). */
  for (GameObject *b : theirs) {
    if (!base_names.count(b->name)) continue;
    GameObject *to = bone_named(base, b->name);
    const std::vector<GameObject *> kids = b->children;
    for (GameObject *c : kids)
      if (!c->get<Bone>()) scene.set_parent(c, to, -1, true);
  }
  const std::vector<GameObject *> kids = other.children;
  for (GameObject *c : kids)
    if (!c->get<Bone>()) scene.set_parent(c, &base, -1, true);
  lines.insert(lines.begin(), strprintf("Merge Armatures: %s into %s - %d shared bone(s), %d moved, %zu mesh(es) on %s now", other.name.c_str(), base.name.c_str(), shared,
                                        moved, meshes.size(), base.name.c_str()));
  scene.destroy(&other);
  return true;
}

bool attach_mesh_to_bone(Scene &scene, GameObject &armature, GameObject &mesh, const std::string &bone, std::string &why) {
  MeshFilter *mf = mesh.get<MeshFilter>();
  if (!mf || !mf->mesh) return why = "select the mesh to attach", false;
  if (mesh.get<Bone>() || mesh.get<Armature>()) return why = "that is a bone, not a mesh", false;
  if (!bone_named(armature, bone)) return why = "no bone '" + bone + "' on " + armature.name, false;
  /* (Review 361: the avatar's own body, skinned to many bones, isn't a prop - all its weight on one bone makes it rigid.) */
  const std::vector<GameObject *> skinned = avatar_meshes(scene, armature);
  if (std::find(skinned.begin(), skinned.end(), &mesh) != skinned.end() && mf->mesh->vertex_groups.size() > 1)
    return why = mesh.name + " is skinned to the avatar's bones already - Attach Mesh is for a rigid prop", false;
  /* Under the armature, not a bone: the skin moves it, so a parent bone would turn it twice (review 361). */
  if (mesh.parent != &armature) scene.set_parent(&mesh, &armature, -1, true);
  Mesh &m = *mesh_make_mutable(mf->mesh);
  m.vertex_groups = {{bone, std::vector<float>(m.vert_count(), 1.0f)}};
  m.touch();
  SkinModifier *s = mesh.get<SkinModifier>();
  if (!s) s = mesh.add<SkinModifier>();
  s->object = armature.name;
  return true;
}

/* A clip's BlendShape tracks on `from` onto `to` (the avatar's clips: review 361 - a VMD's morph tracks had been left on
 * the Japanese names). */
void rename_shape_tracks(GameObject &armature, const std::string &from, const std::string &to) {
  const std::string old_prop = "MeshRenderer.BlendShape/" + from, new_prop = "MeshRenderer.BlendShape/" + to;
  for (GameObject *p = &armature; p; p = p->parent) {
    const Animation *a = p->get<Animation>();
    AnimationClipPtr clip = a && !a->clip.empty() ? clip_asset(a->clip) : nullptr;
    if (!clip) continue;
    for (AnimTrack &t : clip->tracks)
      if (t.property == old_prop) t.property = new_prop;
    clip->touch();
  }
}

/* A shape key's new name: its weight and its clips' tracks with it, the old weight dropped. */
void rename_key(GameObject &armature, MeshRenderer *mr, ShapeKey &k, const std::string &to) {
  if (mr) {
    mr->set_shape_weight(to, mr->shape_weight(k.name));
    auto &w = mr->shape_weights;
    w.erase(std::remove_if(w.begin(), w.end(), [&](const std::pair<std::string, float> &e) { return e.first == k.name; }), w.end());
  }
  rename_shape_tracks(armature, k.name, to);
  k.name = to;
}

/* A material's new name: an asset is copied under it (the file's name is the asset's name - review 361: a rename in
 * place came back on reload and renamed it for every user); an inline material is renamed. */
MaterialPtr renamed_material(const MaterialPtr &mat, const std::string &to) {
  if (mat->asset_path.empty()) {
    mat->name = to;
    return mat;
  }
  Material copy = *mat;
  copy.name = to;
  std::string path = fs::parent(mat->asset_path) + "/" + to + ".mat";
  for (int k = 2; fs::exists(resolve_asset_path(path)); k++) path = fs::parent(mat->asset_path) + "/" + to + " " + std::to_string(k) + ".mat";
  MaterialPtr made = create_material_asset(copy, path);
  return made ? made : mat;
}

/* The materials' names on the avatar's meshes now (a name taken isn't given twice - review 361). */
std::set<std::string> material_names(Scene &scene, GameObject &armature) {
  std::set<std::string> out;
  for (GameObject *g : avatar_meshes(scene, armature))
    if (const MeshRenderer *mr = g->get<MeshRenderer>())
      for (const MaterialPtr &m : mr->materials)
        if (m) out.insert(m->name);
  return out;
}

int translate_avatar_names(Scene &scene, GameObject &armature, std::vector<std::string> &lines) {
  int n = 0;
  std::set<std::string> taken = material_names(scene, armature);
  for (GameObject *g : avatar_meshes(scene, armature)) {
    MeshFilter *mf = g->get<MeshFilter>();
    MeshRenderer *mr = g->get<MeshRenderer>();
    const bool any_key = std::any_of(mf->mesh->shape_keys.begin(), mf->mesh->shape_keys.end(), [](const ShapeKey &k) { return !translate_name(k.name).empty(); });
    if (any_key) {  // (copied and touched only when a key is renamed)
      Mesh &m = *mesh_make_mutable(mf->mesh);
      for (ShapeKey &k : m.shape_keys) {
        const std::string to = translate_name(k.name);
        if (to.empty() || m.find_shape(to) >= 0) continue;
        lines.push_back("  " + k.name + " -> " + to);
        rename_key(armature, mr, k, to);
        n++;
      }
      m.touch();
    }
    if (!mr) continue;
    for (MaterialPtr &mat : mr->materials) {  // (the slots themselves: a renamed copy goes into them)
      const std::string to = mat ? translate_name(mat->name) : "";
      if (to.empty() || taken.count(to)) continue;
      lines.push_back("  " + mat->name + " -> " + to);
      mat = renamed_material(mat, to);
      taken.insert(to);
      n++;
    }
  }
  return n;
}

}  // namespace bl
