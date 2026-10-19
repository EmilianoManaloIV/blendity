// SPDX-License-Identifier: GPL-2.0-or-later
// Materials made on import (task 0134): textures found where a model's links don't point (beside it, in its .fbm
// folder, a textures folder), embedded images extracted, packed maps split into the channels Blendity reads, texture
// sets matched by name to materials the file left bare, and a material for every mesh that has none - as Unity's model
// importer makes them.
#include "import.h"

#include "../core/core.h"
#include "../image/image.h"
#include "../render/canvas.h"

#include <algorithm>
#include <cctype>
#include <map>

namespace bl {

namespace {

std::string lower(std::string s) {
  for (char &c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}

/* Only letters and digits, lower case: "T_Body-01" and "body01" compare equal after a leading "t_" is dropped. */
std::string key_of(const std::string &name) {
  std::string k;
  for (char c : lower(name))
    if (std::isalnum((unsigned char)c)) k += c;
  if (k.size() > 1 && lower(name).rfind("t_", 0) == 0) k = k.substr(1);  // (Unreal's texture prefix)
  return k;
}

/* The folders a model's textures are usually in. */
std::vector<std::string> texture_dirs(const ImportResult &r) {
  const std::string dir = fs::parent(r.source), stem = fs::stem(r.source);
  std::vector<std::string> d{dir, fs::join(dir, stem + ".fbm"), fs::join(dir, "textures"), fs::join(dir, "Textures"), fs::join(dir, "tex"),
                             fs::join(dir, "images"), fs::join(dir, "texture")};
  d.erase(std::remove_if(d.begin(), d.end(), [](const std::string &p) { return !fs::is_dir(p); }), d.end());
  return d;
}

/* What a texture's file name says it is, from the word after its last separator ("Body_BaseColor", "T_Body_N").
 * `letter`: the word was a single letter (_d, _n, _r, _m, _e), which ordinary names end in too ("Hand_R"). */
enum Kind { None, Base, Normal, Rough, Gloss, Metal, MetalSmooth, Orm, Emit };
Kind kind_of(const std::string &stem, std::string &prefix, bool &letter) {
  static const std::map<std::string, Kind> words = {
      {"basecolor", Base}, {"basecolour", Base}, {"albedo", Base}, {"diffuse", Base}, {"diff", Base}, {"color", Base}, {"colour", Base},
      {"col", Base}, {"base", Base}, {"d", Base}, {"normal", Normal}, {"normalgl", Normal}, {"normalogl", Normal}, {"nrm", Normal},
      {"norm", Normal}, {"n", Normal}, {"roughness", Rough}, {"rough", Rough}, {"r", Rough}, {"glossiness", Gloss}, {"gloss", Gloss},
      {"smoothness", Gloss}, {"smooth", Gloss}, {"metallic", Metal}, {"metalness", Metal}, {"metal", Metal}, {"m", Metal},
      {"metallicsmoothness", MetalSmooth}, {"metallicgloss", MetalSmooth}, {"metallicglossmap", MetalSmooth}, {"orm", Orm}, {"arm", Orm},
      {"occlusionroughnessmetallic", Orm}, {"emissive", Emit}, {"emission", Emit}, {"emit", Emit}, {"e", Emit}};
  const size_t cut = stem.find_last_of("_- .");
  if (cut == std::string::npos) return None;
  const std::string word = key_of(stem.substr(cut + 1));
  const auto it = words.find(word);
  if (it == words.end()) return None;
  prefix = stem.substr(0, cut);
  letter = word.size() == 1;
  return it->second;
}

struct Set {
  std::string files[9];  // by Kind
  int maps = 0, words = 0;  // how many maps; how many named by a whole word
};

/* The files in the model's texture folders, listed once per import. */
const std::vector<std::string> &texture_files(ImportResult &r) {
  if (r.listed) return r.listing;
  r.listed = true;
  for (const std::string &d : texture_dirs(r))
    for (const auto &e : fs::list(d))
      if (!e.is_dir) r.listing.push_back(fs::join(d, e.name));
  return r.listing;
}

/* Every texture set in the model's folders, by its name's key. A set named only by single letters is a set when it
 * has two maps or more (Rock_d with Rock_n), not one image that happens to end in one (Hand_R, Logo_e). */
std::map<std::string, Set> texture_sets(ImportResult &r) {
  std::map<std::string, Set> sets;
  for (const std::string &p : texture_files(r)) {
    if (!image_extension_supported(lower(fs::extension(p)))) continue;
    std::string prefix;
    bool letter = false;
    const Kind k = kind_of(fs::stem(p), prefix, letter);
    if (k == None) continue;
    Set &s = sets[key_of(prefix)];
    if (!s.files[k].empty()) continue;
    s.files[k] = p;
    s.maps++;
    s.words += !letter;
  }
  for (auto it = sets.begin(); it != sets.end();) it = it->second.words == 0 && it->second.maps < 2 ? sets.erase(it) : std::next(it);
  return sets;
}

/* The set for a material: by its name, then a mesh's using it, then the folder's only set - that last only for a
 * material the import made (the file's own colours, "Red" and "Blue", are not a rock texture's). */
const Set *set_for(const std::map<std::string, Set> &sets, const Material &m, const std::vector<std::string> &meshes, bool made) {
  const auto own = sets.find(key_of(m.name));
  if (own != sets.end()) return &own->second;
  for (const std::string &n : meshes) {
    const auto it = sets.find(key_of(n));
    if (it != sets.end()) return &it->second;
  }
  return made && sets.size() == 1 ? &sets.begin()->second : nullptr;
}

/* A set's maps on the material's empty slots (the file's own links are never replaced). */
void apply_set(const Set &s, Material &m, ImportResult &r, std::vector<std::string> &used) {
  auto put = [&](TextureRef &ref, const std::string &file, bool non_color) {
    if (!ref.empty() || file.empty()) return;
    ref = TextureRef{file, non_color};
    r.textures.push_back(file);
    used.push_back(fs::filename(file));
  };
  /* A map split out of a packed one: only for a slot still empty (decoding a 4K image to throw away is slow). */
  auto split = [&](TextureRef &ref, const std::string &src, int channel, bool invert, const char *tag) {
    if (ref.empty() && !src.empty()) put(ref, derive_channel(src, channel, invert, tag, r), true);
  };
  if (m.base_map.empty() && !s.files[Base].empty()) m.base_color = Vec3(1.0f);  // (the texture is the colour, not tinted grey)
  put(m.base_map, s.files[Base], false);
  put(m.normal_map, s.files[Normal], true);
  put(m.roughness_map, s.files[Rough], true);
  put(m.metallic_map, s.files[Metal], true);
  put(m.emission_map, s.files[Emit], false);
  if (!m.emission_map.empty() && m.emission_strength <= 0.0f) m.emission = Vec3(1.0f), m.emission_strength = 1.0f;
  split(m.roughness_map, s.files[Gloss], 0, true, "rough");
  split(m.metallic_map, s.files[MetalSmooth], 0, false, "metal");  // (Unity's: metallic in red, smoothness in alpha)
  split(m.roughness_map, s.files[MetalSmooth], 3, true, "rough");
  split(m.roughness_map, s.files[Orm], 1, false, "rough");  // (Unreal's: occlusion red, roughness green, metallic blue)
  split(m.metallic_map, s.files[Orm], 2, false, "metal");
  if (!m.metallic_map.empty()) m.metallic = 1.0f;   // (the map is the value: factor 1)
  if (!m.roughness_map.empty()) m.roughness = 1.0f;
}

}  // namespace

/* ~/.blendity/cache/import/<model>-<hash of its full path>: two models called scene.gltf never share one. */
std::string import_cache_dir(const ImportResult &r) {
  uint64_t h = 1469598103934665603ull;
  for (char c : lower(fs::normalize(r.source))) h = (h ^ (uint8_t)c) * 1099511628211ull;
  return fs::join(fs::join(fs::join(fs::home_dir(), ".blendity"), fs::join("cache", "import")), strprintf("%s-%08x", fs::stem(r.source).c_str(), (unsigned)(h ^ h >> 32)));
}

std::string find_texture(ImportResult &r, const std::string &file) {
  if (file.empty()) return "";
  std::string f = file;
  for (char &c : f)
    if (c == '\\') c = '/';
  if (fs::exists(f)) return f;
  const std::string dir = fs::parent(r.source);
  if (fs::exists(fs::join(dir, f))) return fs::join(dir, f);
  /* By name, in the usual folders (an absolute path from another machine, or a folder moved), any letter case. */
  const std::string name = lower(fs::filename(f));
  for (const std::string &p : texture_files(r))
    if (lower(fs::filename(p)) == name) return p;
  return fs::join(dir, fs::filename(f));  // (missing: named where it should be, so the import says what is missing)
}

std::string save_embedded(const std::string &name, const void *data, size_t size, ImportResult &r) {
  if (!data || !size) return "";
  const std::string dir = import_cache_dir(r);
  fs::make_dirs(dir);
  std::string file = fs::filename(name);
  for (char &c : file)
    if (c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|' || c == '/' || c == '\\') c = '_';
  if (file.empty() || file == "." || file == "..") file = "embedded.png";
  /* Two images of one name in one file (body/diffuse.png, head/diffuse.png): the second gets "diffuse 2.png". */
  const std::string bytes((const char *)data, size);
  std::string p = fs::join(dir, file);
  for (int k = 2; std::find(r.written.begin(), r.written.end(), p) != r.written.end(); k++) {
    std::string old;
    if (fs::read_file(p, old) && old == bytes) return p;  // (the same image again)
    p = fs::join(dir, fs::stem(file) + " " + std::to_string(k) + fs::extension(file));
  }
  if (!fs::write_file(p, bytes)) return "";
  r.written.push_back(p);
  return p;
}

std::string derive_channel(const std::string &src, int channel, bool invert, const std::string &tag, ImportResult &r) {
  const std::string memo = src + '\n' + std::to_string(channel) + (invert ? "i" : "") + tag;
  if (const auto it = r.derived.find(memo); it != r.derived.end()) return it->second;  // (20 materials, one ORM map)
  std::string &done = r.derived[memo];
  Bitmap bm;
  std::string e;
  if (!load_image(src, bm, e) || bm.width <= 0 || bm.height <= 0) return "";
  const std::string dir = import_cache_dir(r);
  fs::make_dirs(dir);
  const std::string out = fs::join(dir, fs::stem(src) + "_" + tag + ".png");
  std::vector<uint32_t> px((size_t)bm.width * bm.height);
  for (size_t i = 0; i < px.size(); i++) {
    float v = bm.is_float ? std::max(0.0f, std::min(1.0f, bm.rgbaf[i * 4 + (size_t)channel])) : bm.rgba8[i * 4 + (size_t)channel] / 255.0f;
    if (invert) v = 1.0f - v;
    const uint32_t g = (uint32_t)std::lround(v * 255.0f);
    px[i] = 0xFF000000u | g << 16 | g << 8 | g;
  }
  if (!write_png(out, px.data(), bm.width, bm.height, bm.width)) return "";
  r.notes.push_back(fs::filename(src) + (invert ? " inverted" : "") + " into " + fs::filename(out));
  return done = out;
}

void complete_materials(ImportResult &r) {
  /* A material for every mesh the file left without one (named after it). */
  std::vector<const Material *> made;
  for (ImportedNode &n : r.nodes) {
    if (!n.mesh || !n.materials.empty()) continue;
    n.materials.push_back(make_material(n.name, Vec3(0.8f)));
    r.materials.push_back(n.materials.back());
    made.push_back(n.materials.back().get());
  }
  /* Texture sets by name, for the materials the file gave no textures (or only some). */
  const std::map<std::string, Set> sets = texture_sets(r);
  if (sets.empty()) return;
  std::vector<std::string> used;
  for (const MaterialPtr &m : r.materials) {
    if (!m || !m->base_map.empty()) continue;
    std::vector<std::string> meshes;
    for (const ImportedNode &n : r.nodes)
      if (std::find(n.materials.begin(), n.materials.end(), m) != n.materials.end()) meshes.push_back(n.name);
    const bool ours = std::find(made.begin(), made.end(), m.get()) != made.end() || r.materials.size() == 1;
    if (const Set *s = set_for(sets, *m, meshes, ours)) apply_set(*s, *m, r, used);
  }
  if (!used.empty()) {
    std::sort(used.begin(), used.end());
    used.erase(std::unique(used.begin(), used.end()), used.end());
    std::string list;
    for (const std::string &u : used) list += (list.empty() ? "" : ", ") + u;
    r.notes.push_back("textures found by name: " + list);
  }
}

}  // namespace bl
