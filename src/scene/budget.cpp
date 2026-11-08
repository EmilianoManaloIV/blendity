// SPDX-License-Identifier: GPL-2.0-or-later
// Performance budgets (round 355, task 0332): see budget.h.
#include "budget.h"

#include "export.h"
#include "material.h"
#include "scene.h"
#include "spring_bones.h"
#include "unity_scene.h"
#include "../core/core.h"
#include "../image/image.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <future>
#include <map>
#include <mutex>
#include <thread>
#include <unordered_set>

namespace bl {

namespace {
constexpr double kNone = -1.0;
std::atomic<bool> g_budget_wait{false};  // (set_budget_wait_for_textures)
}  // namespace

void set_budget_wait_for_textures(bool wait) { g_budget_wait = wait; }
bool budget_wait_for_textures() { return g_budget_wait; }

const char *budget_row_name(BudgetRow r) {
  static const char *const kNames[] = {"Triangles", "Skinned Meshes", "Meshes", "Material Slots", "Bones", "Spring Chains (PhysBones)",
                                       "Spring Bones (PhysBone transforms)", "Texture Memory (MB)", "Lights"};
  return (int)r >= 0 && (int)r < (int)BudgetRow::Count ? kNames[(int)r] : "";
}

/* VRChat's Avatar Performance Ranking limits (creators.vrchat.com/avatars/avatar-performance-ranking-system), in the
 * rows' order: triangles, skinned meshes, basic meshes, material slots, bones, PhysBone components, PhysBone transforms,
 * texture memory (MB), lights. */
const std::vector<BudgetTarget> &budget_targets() {
  static const std::vector<BudgetTarget> kTargets = {
      {"VRChat PC",
       {{"Excellent", {32000, 1, 4, 4, 75, 4, 16, 40, 0}},
        {"Good", {70000, 2, 8, 8, 150, 8, 64, 75, 0}},
        {"Medium", {70000, 8, 16, 16, 256, 16, 128, 110, 0}},
        {"Poor", {70000, 16, 24, 32, 400, 32, 256, 150, 1}}}},
      {"VRChat Quest",
       {{"Excellent", {7500, 1, 1, 1, 75, 0, 0, 10, 0}},
        {"Good", {10000, 1, 1, 1, 90, 4, 16, 18, 0}},
        {"Medium", {15000, 2, 2, 2, 150, 6, 32, 25, 0}},
        {"Poor", {20000, 2, 2, 4, 150, 8, 64, 40, 0}}}},
  };
  return kTargets;
}

int budget_target_index(const std::string &name) {
  const std::vector<BudgetTarget> &t = budget_targets();
  for (size_t i = 0; i < t.size(); i++)
    if (to_lower(t[i].name) == to_lower(name)) return (int)i;
  return -1;
}

namespace {

/* A mesh's triangles: each face split into its size less two. */
double triangles_of(const Mesh &m) {
  double n = 0.0;
  for (size_t f = 0; f < m.face_count(); f++) n += std::max(0, (int)m.face_size(f) - 2);
  return n;
}


/* What the count needs of a texture: its size and whether any texel is less than opaque. */
struct TextureFacts {
  int w = 0, h = 0;
  bool alpha = true;
};

/* A texture only an engine reads (a Unity material's masks and second layers: Blendity never draws or loads them) is
 * read on a worker once per version of its file - review 399: the Budget recounts on the UI thread every half second,
 * and decoding Shinano's dozen 4096 masks there took seconds. Until it has been read it counts nothing; `wait` (a
 * headless count) waits for it. The Unity size of each is kept with it. */
struct EngineTextures {
  struct Entry {
    int64_t version = -1;
    TextureFacts facts;
    std::shared_future<TextureFacts> reading;
  };
  std::mutex mu;
  std::map<std::string, Entry> by_path;
  int reading = 0;  // (workers at it: at most kReaders - review 399: a world names hundreds)
};
const int kReaders = std::max(2, (int)std::thread::hardware_concurrency() / 2);  // (round 422: most reads are a header now: cheap to run wide)
thread_local size_t t_textures_unread = 0;  // (the count in progress's engine-only textures not read yet)

EngineTextures &engine_textures() {
  static EngineTextures e;
  return e;
}

int64_t file_version(const std::string &path) {
  std::error_code ec;
  const auto t = std::filesystem::last_write_time(std::filesystem::u8path(path), ec);
  return ec ? 0 : (int64_t)t.time_since_epoch().count();
}

/* Round 422: a texture's size from its file's header, and whether it can hold alpha at all - PNG's IHDR (its colour
 * type, a tRNS chunk before the data), JPEG's SOF (never alpha), TGA's header (its depth and alpha bits). `alpha_known`
 * false: a PNG or TGA that may be less than opaque somewhere (only its texels say). False: none of these, or cut short. */
bool texture_header(const std::string &abs, int &w, int &h, bool &alpha, bool &alpha_known) {
  std::ifstream in(std::filesystem::u8path(abs), std::ios::binary);
  if (!in) return false;
  std::string b(256 * 1024, '\0');
  in.read(&b[0], (std::streamsize)b.size());
  b.resize((size_t)in.gcount());
  auto u8 = [&](size_t i) { return (uint32_t)(uint8_t)b[i]; };
  auto be32 = [&](size_t i) { return u8(i) << 24 | u8(i + 1) << 16 | u8(i + 2) << 8 | u8(i + 3); };
  if (b.size() >= 33 && b.compare(0, 8, "\x89PNG\r\n\x1a\n") == 0 && b.compare(12, 4, "IHDR") == 0) {
    w = (int)be32(16), h = (int)be32(20);
    const uint32_t type = u8(25);
    bool trns = false;
    for (size_t at = 8; at + 8 <= b.size();) {  // (the chunks before the first IDAT: a tRNS there gives alpha)
      const uint32_t len = be32(at);
      const std::string kind = b.substr(at + 4, 4);
      if (kind == "IDAT" || kind == "IEND") break;
      trns = trns || kind == "tRNS";
      at += 12 + (size_t)len;
    }
    alpha = type == 4 || type == 6 || trns, alpha_known = !alpha;
    return w > 0 && h > 0;
  }
  if (b.size() >= 4 && u8(0) == 0xFF && u8(1) == 0xD8) {
    for (size_t at = 2; at + 9 < b.size();) {
      if (u8(at) != 0xFF) return false;
      const uint32_t m = u8(at + 1), len = u8(at + 2) << 8 | u8(at + 3);
      if (m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC) {
        h = (int)(u8(at + 5) << 8 | u8(at + 6)), w = (int)(u8(at + 7) << 8 | u8(at + 8));
        alpha = false, alpha_known = true;
        return w > 0 && h > 0;
      }
      at += 2 + (size_t)len;
    }
    return false;
  }
  const std::string ext = fs::extension(abs);  // (lowercase)
  if (ext == ".tga" && b.size() >= 18) {
    w = (int)(u8(12) | u8(13) << 8), h = (int)(u8(14) | u8(15) << 8);
    const uint32_t depth = u8(16), alpha_bits = u8(17) & 15;
    alpha = depth == 32 || alpha_bits > 0, alpha_known = !alpha;
    return w > 0 && h > 0;
  }
  return false;
}

/* The facts: from the header alone when it settles them - or when Unity's settings for the texture give the same size
 * with alpha and without (its .meta names the format) - else the image decoded and its texels looked at. Round 422:
 * `budget pc` on a world had decoded every engine texture in full for this (10 s on SideRail). */
TextureFacts read_texture_facts(const std::string &abs, const std::string &path) {
  TextureFacts f;
  bool alpha = true, known = false;
  if (texture_header(abs, f.w, f.h, alpha, known)) {
    if (known) return f.alpha = alpha, f;
    double with = 0.0, without = 0.0;
    if (unity_texture_megabytes(path, f.w, f.h, true, with) && unity_texture_megabytes(path, f.w, f.h, false, without) && with == without)
      return f.alpha = true, f;  // (the count doesn't depend on it)
  }
  f = TextureFacts{};
  Bitmap b;
  std::string why;
  if (!load_image(abs, b, why) || b.width <= 0 || b.height <= 0) return f;
  f.w = b.width, f.h = b.height, f.alpha = b.is_float;
  for (size_t i = 3; !f.alpha && i < b.rgba8.size(); i += 4) f.alpha = b.rgba8[i] != 255;
  return f;
}

/* An engine-only texture's facts once read (false: not yet - its reading started). */
bool engine_texture_facts(const std::string &path, bool wait, TextureFacts &out) {
  const std::string abs = resolve_asset_path(path);
  const int64_t version = file_version(abs);
  EngineTextures &et = engine_textures();
  std::shared_future<TextureFacts> reading;
  {
    std::lock_guard<std::mutex> lock(et.mu);
    EngineTextures::Entry &e = et.by_path[abs];
    if (e.version == version && !e.reading.valid()) return out = e.facts, true;
    if (e.version != version && e.reading.valid()) return false;  // (an older version still being read: next time)
    if (!e.reading.valid() && wait) {  // (a waiting count reads it here: no worker to wait for)
      e.version = version, e.facts = read_texture_facts(abs, path);
      return out = e.facts, true;
    }
    if (!e.reading.valid()) {
      if (et.reading >= kReaders) return false;
      et.reading++;
      e.version = version;
      e.reading = std::async(std::launch::async, read_texture_facts, abs, path).share();
    }
    reading = e.reading;
  }
  if (!wait && reading.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return false;
  const TextureFacts f = reading.get();
  std::lock_guard<std::mutex> lock(et.mu);
  EngineTextures::Entry &e = et.by_path[abs];
  if (e.reading.valid()) et.reading--, e.facts = f, e.reading = {};
  return out = f, true;
}

/* Unity's size for a texture, kept (review 399: its .meta and the package's .unity-guids had been read and parsed at
 * every recount); -1: Unity has no settings for it. */
double unity_mb_cached(const std::string &path, const TextureFacts &f) {
  static std::mutex mu;
  static std::map<std::string, double> known;
  const std::string key = path + "|" + std::to_string(f.w) + "x" + std::to_string(f.h) + (f.alpha ? "a" : "o");
  {
    std::lock_guard<std::mutex> lock(mu);
    const auto it = known.find(key);
    if (it != known.end()) return it->second;
  }
  double mb = 0.0;
  if (!unity_texture_megabytes(path, f.w, f.h, f.alpha, mb)) mb = -1.0;
  std::lock_guard<std::mutex> lock(mu);
  return known[key] = mb;
}

double texture_bytes_mb(const TextureFacts &f, bool normal_map) {
  const double bytes = normal_map || f.alpha ? 1.0 : 0.5;
  return (double)f.w * (double)f.h * bytes * (4.0 / 3.0) / (1024.0 * 1024.0);
}

/* A map read from its file's facts (its header, or decoded on a worker - engine_texture_facts). */
double file_texture_mb(const TextureRef &t, bool normal_map, bool wait) {
  TextureFacts f;
  if (!engine_texture_facts(t.path, wait, f)) return (void)t_textures_unread++, 0.0;
  if (f.w <= 0) return 0.0;
  const double unity = unity_mb_cached(t.path, f);
  return unity >= 0.0 ? unity : texture_bytes_mb(f, normal_map);
}

/* A texture's memory on the card: as Unity imports it when it has Unity's settings, else as its automatic compression
 * stores it, a third more for mips - a byte a texel with alpha or for a normal map (DXT5 / BC5), half a byte for an
 * opaque colour map (DXT1). Round 399: Shinano's opaque 4096s had counted twice their size, 133 MB to the SDK's 61.
 * A drawn map is the one Blendity loaded to draw; an engine-only one is read once on a worker. */
double texture_mb(const TextureRef &t, std::unordered_set<std::string> &seen, bool normal_map = false, bool engine_only = false, bool wait = false) {
  if (t.empty() || !seen.insert(t.path).second) return 0.0;
  if (engine_only) return file_texture_mb(t, normal_map, wait);
  /* A drawn map: the texture the views loaded when there is one; else its file's facts, as an engine-only map's (round
   * 422: a count before anything drew - headless, or with the views on the card - decoded every map in full for its
   * size: SideRail's first `budget pc` 7 s). Procedural and generated maps are made, not read. */
  const bool file = !texture_ref_is_procedural(t) && t.path.rfind("generated:", 0) != 0;
  if (file && !texture_cached(resolve_asset_path(t.path), !t.non_color)) return file_texture_mb(t, normal_map, wait);
  const TexturePtr tex = resolve_texture_ref(t);
  if (!tex || tex->width <= 0 || tex->height <= 0) return 0.0;
  const TextureFacts f{tex->width, tex->height, tex->has_alpha};
  const double unity = unity_mb_cached(t.path, f);  // (Unity's own settings for it)
  return unity >= 0.0 ? unity : texture_bytes_mb(f, normal_map);
}

/* The armature a mesh's skin follows (by name, as SkinModifier finds it), or nullptr. */
const GameObject *skin_armature(const GameObject &g) {
  const SkinModifier *s = g.get<SkinModifier>();
  if (!s || !s->enabled || s->object.empty() || !g.scene) return nullptr;
  const GameObject *t = skin_armature_of(g, s->object);
  return t && t != &g && t->get<Armature>() ? t : nullptr;
}

/* The transforms a skin names in an engine (skin_joints: its armature and everything under it, at most 4096), into
 * `joints` - a set, so a nested rig or two meshes on one rig count once. Round 396: VRChat's SDK counts the bones the
 * skinned meshes name (5 for a rig of 4 bones: the rig itself one of them); counting Bone components had said 4. */
void add_skeleton(const GameObject &arm, std::unordered_set<const GameObject *> &joints) {
  std::vector<const GameObject *> under{&arm};
  for (size_t n = 0; !under.empty() && n < 4096; n++) {
    const GameObject *b = under.back();
    under.pop_back();
    joints.insert(b);
    for (const GameObject *k : b->children) under.push_back(k);
  }
}

/* What one object adds: its mesh (and an Outline's shell, a mesh and a material slot more in an engine), its spring
 * chain and the bones under it, its light, its materials' textures (its skin's bones: budget_count, as a set). */
void count_object(GameObject &g, BudgetCounts &c, std::unordered_set<std::string> &textures, std::unordered_set<const GameObject *> &roots) {
  double *v = c.value;
  if (const Light *l = g.get<Light>(); l && l->enabled) v[(int)BudgetRow::Lights] += 1;
  const Light *lit = g.get<Light>();
  const bool own_light = lit && lit->enabled && lit->light_volume && lit->type != 0;
  if (own_light) c.light_volume_lights++;  // (round 397)
  for (const auto &comp : g.components) {  // (review 397: the package's own lights from Unity - a Point Light Volume with no Unity light)
    const auto *e = dynamic_cast<const EngineComponent *>(comp.get());
    if (!own_light && e && e->enabled && e->type == "PointLightVolumeInstance" && e->data.find("m_Enabled: 0") == std::string::npos) c.light_volume_lights++;
  }
  if (const LightVolumeComponent *lv = g.get<LightVolumeComponent>(); lv && lv->enabled) c.light_volumes++;
  if (const SpringBone *sb = g.get<SpringBone>(); sb && sb->enabled) {
    /* (Round 359: chains under one RootBone_ are one PhysBone component - Bone Root's point.) */
    const bool grouped = g.parent && spring::is_bone_root(*g.parent);
    v[(int)BudgetRow::SpringChains] += !grouped || roots.insert(g.parent).second ? std::max(1, sb->physbones) : 0;  // (round 399: Unity's several on one root)
    std::vector<GameObject *> under{&g};
    while (!under.empty()) {
      GameObject *b = under.back();
      under.pop_back();
      if (b->get<Bone>()) v[(int)BudgetRow::SpringBones] += 1;
      for (GameObject *k : b->children) under.push_back(k);
    }
  }
  const MeshRenderer *mr = g.get<MeshRenderer>();
  const Mesh *m = mr ? g.evaluated_mesh() : nullptr;  // (round 399: a renderer switched off counts - VRChat counts it, an avatar toggles it on)
  if (!m || !m->face_count()) return;
  const Component *ol = g.find_role(Component::RenderRole::Outline);
  const bool outline = ol && ol->enabled;
  const SkinModifier *skin = g.get<SkinModifier>();
  const MeshFilter *kf = g.get<MeshFilter>();
  /* A mesh with shape keys is skinned to Unity (a SkinnedMeshRenderer), so to VRChat's count too (review 355). */
  const bool skinned = (skin && skin->enabled) || (kf && kf->mesh && !kf->mesh->shape_keys.empty());
  v[(int)(skinned ? BudgetRow::SkinnedMeshes : BudgetRow::Meshes)] += outline ? 2 : 1;
  v[(int)BudgetRow::Triangles] += triangles_of(*m) * (outline ? 2.0 : 1.0);
  v[(int)BudgetRow::MaterialSlots] += (double)std::max<size_t>(1, mr->materials.size()) + (outline ? 1.0 : 0.0);
  const MeshFilter *mf = g.get<MeshFilter>();
  c.shape_keys += mf && mf->mesh ? mf->mesh->shape_keys.size() : 0;
  for (const MaterialPtr &mp : mr->materials) {
    if (!mp) continue;
    for (const TextureRef *t : {&mp->base_map, &mp->normal_map, &mp->metallic_map, &mp->roughness_map, &mp->emission_map})
      v[(int)BudgetRow::TextureMB] += texture_mb(*t, textures, t == &mp->normal_map, false, budget_wait_for_textures());
    for (const TextureRef &t : mp->engine_maps) v[(int)BudgetRow::TextureMB] += texture_mb(t, textures, false, true, budget_wait_for_textures());
  }
}

}  // namespace

BudgetCounts budget_count(const std::vector<GameObject *> &objects) {
  BudgetCounts c;
  t_textures_unread = 0;
  std::unordered_set<const GameObject *> once;
  std::unordered_set<std::string> textures;
  std::unordered_set<const GameObject *> roots, joints;
  for (GameObject *g : objects) {
    if (!g || !once.insert(g).second) continue;  // (round 399: inactive objects count too - VRChat's SDK counts a hidden outfit)
    count_object(*g, c, textures, roots);
    const MeshRenderer *mr = g->get<MeshRenderer>();
    const GameObject *arm = mr ? skin_armature(*g) : nullptr;
    if (arm) add_skeleton(*arm, joints);  // (review 396: what the skins name, not every armature present)
  }
  c.value[(int)BudgetRow::Bones] = (double)joints.size();
  c.textures_reading = t_textures_unread;
  return c;
}

BudgetCounts budget_count_items(const std::vector<ExportItem> &items) {
  BudgetCounts c;
  t_textures_unread = 0;
  double *v = c.value;
  std::unordered_set<uint64_t> joints;  // (review 396: the skins' joints as a set - a nested rig's are its outer rig's too)
  double unnamed = 0.0;                 // (a skin without its joints' objects: its joints counted once per rig)
  std::unordered_set<uint64_t> rigs;
  std::unordered_set<std::string> textures;
  for (const ExportItem &it : items) {
    if (!it.mesh || !it.mesh->face_count() || it.collider) continue;  // (an engine hides a collider: not drawn, not ranked)
    const bool skinned = it.skin || !it.mesh->shape_keys.empty();
    v[(int)(skinned ? BudgetRow::SkinnedMeshes : BudgetRow::Meshes)] += 1;
    v[(int)BudgetRow::Triangles] += triangles_of(*it.mesh);
    v[(int)BudgetRow::MaterialSlots] += (double)std::max<size_t>(1, it.materials.size());
    c.shape_keys += it.mesh->shape_keys.size();
    for (const MaterialPtr &mp : it.materials) {
      if (!mp) continue;
      for (const TextureRef *t : {&mp->base_map, &mp->normal_map, &mp->metallic_map, &mp->roughness_map, &mp->emission_map})
        v[(int)BudgetRow::TextureMB] += texture_mb(*t, textures, t == &mp->normal_map, false, budget_wait_for_textures());
      for (const TextureRef &t : mp->engine_maps) v[(int)BudgetRow::TextureMB] += texture_mb(t, textures, false, true, budget_wait_for_textures());
    }
    if (it.skin) joints.insert(it.skin->joint_ids.begin(), it.skin->joint_ids.end());
    if (it.skin && it.skin->joint_ids.empty() && rigs.insert(it.skin->rig).second) unnamed += (double)it.skin->joints.size();
  }
  v[(int)BudgetRow::Bones] = (double)joints.size() + unnamed;
  c.textures_reading = t_textures_unread;
  return c;
}

BudgetResult budget_rank(const BudgetTarget &t, const BudgetCounts &c) {
  BudgetResult r;
  const int ranks = (int)t.ranks.size();
  for (int row = 0; row < (int)BudgetRow::Count; row++) {
    int best = ranks;  // (the first rank the row fits - ranks are best first; past the last, the target's "over")
    for (int k = 0; k < ranks && best == ranks; k++)
      if (t.ranks[(size_t)k].limit[row] == kNone || c.value[row] <= t.ranks[(size_t)k].limit[row]) best = k;
    r.row_rank[row] = best;
    r.rank = std::max(r.rank, best);
  }
  return r;
}

std::string budget_rank_name(const BudgetTarget &t, int rank) { return rank >= 0 && rank < (int)t.ranks.size() ? t.ranks[(size_t)rank].name : t.over; }

bool budget_texture_header(const std::string &abs, int &w, int &h, bool &alpha, bool &alpha_known) {
  return texture_header(abs, w, h, alpha, alpha_known);
}

}  // namespace bl
