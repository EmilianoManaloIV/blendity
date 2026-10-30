// SPDX-License-Identifier: GPL-2.0-or-later
// Materials made on import (task 0134): textures found where a model's links don't point (beside it, in its .fbm
// folder, a textures folder), embedded images extracted, packed maps split into the channels Blendity reads, texture
// sets matched by name to materials the file left bare, and a material for every mesh that has none - as Unity's model
// importer makes them. The texture finder (task 0143) looks through the folders around the model and pairs each bare
// material with the set its name is likeliest to own.
#include "import.h"

#include "../core/core.h"
#include "../image/image.h"
#include "../render/canvas.h"

#include <algorithm>
#include <cctype>
#include <cmath>
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

/* ---- The texture finder (task 0143): where a model's textures are, and whose they are ---- */

/* Folders the search never climbs into or enters: the user's home folder and the system's folders by place
 * (Users / Program Files / Windows at a drive root, the temp folder), Downloads / Desktop / Documents / AppData by
 * name, a drive root. A model dropped loose in one has only what lies right beside it read. */
bool crawl_stops(const std::string &folder) {
  static const char *const by_name[] = {"downloads", "desktop", "documents", "appdata", "temp", "tmp"};
  static const char *const at_root[] = {"users", "home", "program files", "program files (x86)", "windows", "programdata"};
  const std::string name = lower(fs::filename(folder)), parent = fs::parent(folder);
  if (parent == folder || name.empty()) return true;
  const std::string norm = fs::normalize(folder);
  if (norm == fs::normalize(fs::home_dir()) || norm == fs::normalize(fs::temp_dir())) return true;
  if (std::find(std::begin(by_name), std::end(by_name), name) != std::end(by_name)) return true;
  return fs::parent(parent) == parent && std::find(std::begin(at_root), std::end(at_root), name) != std::end(at_root);
}

/* A file that lives beside textures without being one: Unity's .meta, Godot's .import, a readme, a material. */
bool sidecar(const std::string &ext) {
  static const char *const exts[] = {".meta", ".import", ".txt", ".json", ".mat", ".sbsar", ".md", ".xml", ".ini", ".db"};
  return std::find(std::begin(exts), std::end(exts), ext) != std::end(exts);
}

/* The crawl's budget: at most 2,000 images, 400 folder listings, and about 150 ms (a library on a slow disk or a
 * share must not stall the drop); a folder of over 200 subfolders is a library root, not a texture folder. */
struct Crawl {
  ImportResult &r;
  ScopedTimer timer;
  int listings = 0;
  bool cut = false;
  bool allowed() {
    if (r.listing.size() >= 2000 || listings >= 400 || timer.ms() > 150.0) cut = true;
    return !cut;
  }
};

/* The images in `dir` and (to `depth` levels) its subfolders, each with how far from the model it was: 0 the model's
 * own folder, then one more a level; `beside`: in the folders beside the model's, not its own tree. A folder full of
 * other files (over 50 that are neither images nor their sidecars, and twice the images) is left alone unless it is
 * the model's own: a texture folder holds textures. */
void crawl(const std::string &dir, int depth, int distance, bool own, bool beside, Crawl &c) {
  if (!c.allowed()) return;
  c.listings++;
  const std::vector<DirEntry> entries = fs::list(dir);
  size_t others = 0, images = 0, folders = 0;
  for (const DirEntry &e : entries) {
    const std::string ext = lower(fs::extension(e.name));
    folders += e.is_dir;
    if (!e.is_dir) (image_extension_supported(ext) ? images : sidecar(ext) ? images : others)++;  // (sidecars count with their images)
  }
  if (!own && others > 50 && others > 2 * images) return;
  for (const DirEntry &e : entries)
    if (!e.is_dir && image_extension_supported(lower(fs::extension(e.name))) && c.r.listing.size() < 2000)
      c.r.listing.push_back(fs::join(dir, e.name)), c.r.listing_distance.push_back(distance), c.r.listing_beside.push_back(beside);
  if (depth <= 0 || folders > 200) return;
  for (const DirEntry &e : entries)
    if (e.is_dir && !crawl_stops(fs::join(dir, e.name))) crawl(fs::join(dir, e.name), depth - 1, distance + 1, false, beside, c);
}

/* The images around the model, listed once per import: its folder and three levels below, then the folders beside
 * it (Sketchfab's `source/` and `textures/`, a Unity package's `Materials/` and `Textures/`) two levels down. */
const std::vector<std::string> &texture_files(ImportResult &r) {
  if (r.listed) return r.listing;
  r.listed = true;
  Crawl c{r, {}, 0, false};
  const std::string dir = fs::parent(r.source);
  if (crawl_stops(dir)) {  // (loose in Downloads: only what lies right beside it)
    crawl(dir, 0, 0, true, false, c);
    return r.listing;
  }
  crawl(dir, 3, 0, true, false, c);
  const std::string parent = fs::parent(dir);
  if (parent != dir && !crawl_stops(parent)) {
    const std::string own = fs::normalize(dir);
    for (const DirEntry &e : fs::list(parent)) {
      const std::string sub = fs::join(parent, e.name);
      if (e.is_dir && fs::normalize(sub) != own && lower(e.name) != lower(fs::filename(dir)) && !crawl_stops(sub)) crawl(sub, 2, 2, false, true, c);
    }
  }
  if (c.cut) r.notes.push_back("the search for textures stopped early (a large folder tree around the model): a missing texture may lie farther away");
  return r.listing;
}

/* What a texture's file name says it is, from its last one or two words ("Body_BaseColor", "Carmen_Base_color",
 * "Hair_Normal_OpenGL", "T_Body_N"), a trailing "map" / "tex" or a size ("_2k", "_4096", ".1001") dropped first.
 * `letter`: the word was a single letter (_d, _n, _r, _m, _e), which ordinary names end in too ("Hand_R"). */
enum Kind { None, Base, Normal, Rough, Gloss, Metal, MetalSmooth, Orm, MetalRough, Emit, Ao, Opacity, Height, KindCount };
const char *const kKindNames[KindCount] = {"", "base color", "normal", "roughness", "gloss", "metallic", "metallic-smoothness", "ORM", "metallic-roughness", "emission", "occlusion", "opacity", "height"};
constexpr Kind kLastSlotKind = Emit;  // (the kinds Blendity has a slot for; AO, opacity and height are only reported)

std::vector<std::string> name_words(const std::string &stem) {
  std::vector<std::string> w;
  std::string cur;
  for (char c : stem) {
    if (c == '_' || c == '-' || c == ' ' || c == '.') {
      if (!cur.empty()) w.push_back(cur), cur.clear();
    }
    else cur += c;
  }
  if (!cur.empty()) w.push_back(cur);
  return w;
}

bool size_word(const std::string &w) {  // "2k", "4096", "1001" (a UDIM tile), "map", "tex"
  const std::string k = lower(w);
  if (k.size() >= 2 && k.back() == 'k' && std::all_of(k.begin(), k.end() - 1, [](char c) { return std::isdigit((unsigned char)c); })) return true;
  if (k.size() >= 3 && k.size() <= 4 && std::all_of(k.begin(), k.end(), [](char c) { return std::isdigit((unsigned char)c); })) return true;
  return k == "map" || k == "tex" || k == "texture" || k == "img" || k == "image";
}

Kind kind_of(const std::string &stem, std::string &prefix, bool &letter) {
  static const std::map<std::string, Kind> words = {
      {"basecolor", Base}, {"basecolour", Base}, {"albedo", Base}, {"diffuse", Base}, {"diff", Base}, {"color", Base}, {"colour", Base},
      {"col", Base}, {"base", Base}, {"d", Base}, {"c", Base}, {"alb", Base}, {"normal", Normal}, {"normalgl", Normal}, {"normalogl", Normal}, {"normalopengl", Normal},
      {"normaldx", Normal}, {"normaldirectx", Normal}, {"nrm", Normal}, {"norm", Normal}, {"nor", Normal}, {"n", Normal}, {"roughness", Rough}, {"rough", Rough},
      {"rgh", Rough}, {"r", Rough}, {"glossiness", Gloss}, {"gloss", Gloss}, {"smoothness", Gloss}, {"smooth", Gloss}, {"metallic", Metal},
      {"metalness", Metal}, {"metal", Metal}, {"met", Metal}, {"m", Metal}, {"metallicsmoothness", MetalSmooth}, {"metallicgloss", MetalSmooth},
      {"metallicglossmap", MetalSmooth}, {"metalsmoothness", MetalSmooth}, {"orm", Orm}, {"arm", Orm}, {"occlusionroughnessmetallic", Orm},
      {"metallicroughness", MetalRough}, {"metalrough", MetalRough}, {"metalroughness", MetalRough},
      {"emissive", Emit}, {"emission", Emit}, {"emit", Emit}, {"e", Emit}, {"glow", Emit}, {"ao", Ao}, {"occlusion", Ao}, {"ambientocclusion", Ao},
      {"ambient", Ao}, {"opacity", Opacity}, {"alpha", Opacity}, {"transparency", Opacity}, {"mask", Opacity}, {"height", Height},
      {"displacement", Height}, {"disp", Height}, {"bump", Height}};
  std::vector<std::string> w = name_words(stem);
  while (w.size() > 1 && size_word(w.back())) w.pop_back();
  if (w.size() < 2) return None;
  auto join = [&](size_t from) {
    std::string p;
    for (size_t i = 0; i < from; i++) p += (i ? "_" : "") + w[i];
    return p;
  };
  /* Two words first ("Base_color", "Normal_OpenGL"), then one. */
  if (w.size() >= 3) {
    const std::string two = key_of(w[w.size() - 2] + w[w.size() - 1]);
    if (const auto it = words.find(two); it != words.end()) {
      prefix = join(w.size() - 2);
      letter = false;
      return it->second;
    }
  }
  const std::string one = key_of(w.back());
  const auto it = words.find(one);
  if (it == words.end()) return None;
  prefix = join(w.size() - 1);
  letter = one.size() == 1;
  return it->second;
}

struct Set {
  std::string files[KindCount];  // by Kind
  int map_distance[KindCount] = {};
  int maps = 0, words = 0;       // how many maps; how many named by a whole word
  int distance = 99;             // the nearest of its files
  bool beside = false;           // in a folder beside the model's, not its own tree
  std::string name;              // the files' shared prefix, as written
  bool usable() const {          // (a map Blendity has a slot for: a set of only AO / height / opacity isn't a candidate)
    for (int k = Base; k <= kLastSlotKind; k++)
      if (!files[k].empty()) return true;
    return false;
  }
  std::string first_file() const {
    for (int k = Base; k < KindCount; k++)
      if (!files[k].empty()) return files[k];
    return {};
  }
};

/* Every texture set around the model, one per folder and name (two projects' "Body" sets stay apart). A set named
 * only by single letters is a set when it has two maps or more (Rock_d with Rock_n), not one image that happens to
 * end in one (Hand_R, Logo_e). Of two copies of a map the nearer wins. */
std::map<std::string, Set> texture_sets(ImportResult &r) {
  std::map<std::string, Set> sets;
  const std::vector<std::string> &files = texture_files(r);
  for (size_t i = 0; i < files.size(); i++) {
    const std::string &p = files[i];
    std::string prefix;
    bool letter = false;
    const Kind k = kind_of(fs::stem(p), prefix, letter);
    if (k == None) continue;
    Set &s = sets[lower(fs::normalize(fs::parent(p))) + "|" + key_of(prefix)];
    const int distance = i < r.listing_distance.size() ? r.listing_distance[i] : 0;
    const bool beside = i < r.listing_beside.size() && r.listing_beside[i];
    if (!s.files[k].empty() && distance >= s.map_distance[k]) continue;
    if (s.files[k].empty()) s.maps++, s.words += !letter;
    s.files[k] = p, s.map_distance[k] = distance;
    s.distance = std::min(s.distance, distance);
    s.beside = s.beside || beside;
    if (s.name.empty()) s.name = prefix;
  }
  for (auto it = sets.begin(); it != sets.end();) it = it->second.words == 0 && it->second.maps < 2 ? sets.erase(it) : std::next(it);
  return sets;
}

/* The words of a name, lower case: at separators, at a small-to-capital change and at letter-digit changes
 * ("CarmenB" -> carmen, b; "T_character_body_2k" -> t, character, body, 2k). */
std::vector<std::string> tokens_of(const std::string &name) {
  std::vector<std::string> t;
  std::string cur;
  for (size_t i = 0; i < name.size(); i++) {
    const unsigned char c = (unsigned char)name[i], prev = i ? (unsigned char)name[i - 1] : 0;
    const bool sep = !std::isalnum(c);
    const bool camel = i && std::isupper(c) && std::islower(prev);
    const bool digit = i && (std::isdigit(c) != std::isdigit(prev));
    if ((sep || camel || digit) && !cur.empty()) t.push_back(cur), cur.clear();
    if (!sep) cur += (char)std::tolower(c);
  }
  if (!cur.empty()) t.push_back(cur);
  return t;
}

/* How much two names say the same thing, 0..1: the same key 1; one's words a run inside the other's (three letters
 * or more: "body" in "t character body 2 k", not "arm" in "armor") 0.9; else the share of words they have in common,
 * numbers left out ("Wood_01" and "Rock_01" share nothing). "CarmenB" against "CarmenB": 1; "Body" against
 * "T_character_body_2k": 0.9; "CarmenB" against "Carmen_Body": a half. */
float similarity(const std::string &a, const std::string &b) {
  const std::string ka = key_of(a), kb = key_of(b);
  if (ka.empty() || kb.empty()) return 0.0f;
  if (ka == kb) return 1.0f;
  std::vector<std::string> ta = tokens_of(a), tb = tokens_of(b);
  auto numeric = [](const std::string &t) { return std::all_of(t.begin(), t.end(), [](char c) { return std::isdigit((unsigned char)c); }); };
  ta.erase(std::remove_if(ta.begin(), ta.end(), numeric), ta.end());
  tb.erase(std::remove_if(tb.begin(), tb.end(), numeric), tb.end());
  if (ta.empty() || tb.empty()) return 0.0f;
  const std::vector<std::string> &shorter = ta.size() < tb.size() ? ta : tb, &longer = ta.size() < tb.size() ? tb : ta;
  size_t letters = 0;
  for (const std::string &t : shorter) letters += t.size();
  if (letters >= 3 && shorter.size() <= longer.size() && std::search(longer.begin(), longer.end(), shorter.begin(), shorter.end()) != longer.end()) return 0.9f;
  size_t common = 0;
  for (const std::string &x : ta) common += std::find(tb.begin(), tb.end(), x) != tb.end();
  return (float)common / (float)std::max(ta.size(), tb.size());
}

struct Match {
  const Set *set = nullptr;
  float score = 0.0f;
};

/* The set each material is likeliest to own, by its name and the names of the meshes it alone covers (a mesh with
 * several materials says nothing about which is which). A set farther from the model counts a little less (0.05 a
 * level): a `Body` set in a sibling project's folder loses to the `character_body` set beside the model. */
Match best_match(const std::map<std::string, Set> &sets, const Material &m, const std::vector<std::string> &own_meshes) {
  Match best;
  for (const auto &[key, set] : sets) {
    if (!set.usable()) continue;
    float score = similarity(m.name, set.name);
    for (const std::string &n : own_meshes) score = std::max(score, similarity(n, set.name) * 0.95f);  // (its own name first)
    score -= 0.05f * (float)std::min(set.distance, 6);
    const bool better = score > best.score + 1e-4f || (std::fabs(score - best.score) <= 1e-4f && best.set && set.distance < best.set->distance);
    if (better) best = {&set, score};
  }
  return best;
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
  split(m.roughness_map, s.files[MetalRough], 1, false, "rough");  // (glTF's: roughness green, metallic blue)
  split(m.metallic_map, s.files[MetalRough], 2, false, "metal");
  if (!m.metallic_map.empty()) m.metallic = 1.0f;   // (the map is the value: factor 1)
  if (!m.roughness_map.empty()) m.roughness = 1.0f;
}

}  // namespace

/* ~/.blendity/cache/import/<model>-<hash of its full path>: two models called scene.gltf never share one. */
std::string import_cache_dir(const ImportResult &r) {
  const std::string path = lower(fs::normalize(r.source));
  const uint64_t h = fnv1a(path.data(), path.size());
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
  const std::vector<std::string> &files = texture_files(r);
  std::string nearest;
  int best = 1 << 20;
  for (size_t i = 0; i < files.size(); i++) {  // (the nearest copy: a backup's deeper down loses to textures/)
    if (lower(fs::filename(files[i])) != name) continue;
    const int d = (i < r.listing_distance.size() ? r.listing_distance[i] : 0) + (i < r.listing_beside.size() && r.listing_beside[i] ? 10 : 0);
    if (d < best) best = d, nearest = files[i];
  }
  if (!nearest.empty()) return nearest;
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

/* Materials the file left bare get the texture sets found around the model (task 0143): each by the likeliest name,
 * else - when as many materials as sets are left and each has its own best - by that; a lone material with a lone set
 * always. What was matched, what stayed bare and what was seen but not used is said in the notes. */
void complete_materials(ImportResult &r) {
  /* A material for every mesh the file left without one (named after it). */
  std::vector<const Material *> made;
  for (ImportedNode &n : r.nodes) {
    if (!n.mesh || !n.materials.empty()) continue;
    n.materials.push_back(make_material(n.name, Vec3(0.8f)));
    r.materials.push_back(n.materials.back());
    made.push_back(n.materials.back().get());
  }
  const std::map<std::string, Set> sets = texture_sets(r);
  if (sets.empty()) return;
  struct Bare {
    MaterialPtr m;
    std::vector<std::string> meshes;
    Match match;
  };
  std::vector<Bare> bare;
  for (const MaterialPtr &m : r.materials) {
    if (!m || !m->base_map.empty()) continue;  // (a material the file textured is the file's: its missing normal isn't guessed)
    Bare b{m, {}, {}};
    for (const ImportedNode &n : r.nodes)
      if (n.materials.size() == 1 && n.materials[0] == m) b.meshes.push_back(n.name);
    b.match = best_match(sets, *m, b.meshes);
    bare.push_back(std::move(b));
  }
  constexpr float kFound = 0.6f;
  std::vector<const Set *> taken;
  for (Bare &b : bare)
    if (b.match.set && b.match.score >= kFound) taken.push_back(b.match.set);
  /* The ones still bare: as many as the sets nobody took, each with a best of its own, pair by those; a lone material
   * (one the import made, or the model's only one) with a lone set pairs whatever the names. */
  std::vector<Bare *> left;
  std::vector<const Set *> free_sets;
  for (Bare &b : bare)
    if (!(b.match.set && b.match.score >= kFound)) left.push_back(&b);
  for (const auto &[key, set] : sets)
    if (std::find(taken.begin(), taken.end(), &set) == taken.end()) free_sets.push_back(&set);
  constexpr float kFloor = 0.3f;  // (a 1:1 leftover still needs some likeness: "Wood_01" and "Rock_01" are not a pair)
  bool pair_by_best = !left.empty() && left.size() == free_sets.size();
  for (size_t a = 0; pair_by_best && a < left.size(); a++) {
    pair_by_best = left[a]->match.set && left[a]->match.score >= kFloor && std::find(free_sets.begin(), free_sets.end(), left[a]->match.set) != free_sets.end();
    for (size_t c = 0; pair_by_best && c < a; c++) pair_by_best = left[c]->match.set != left[a]->match.set;
  }
  /* The lone rule counts the model's own folder tree only (round 154's "the folder's only set"). */
  std::vector<const Set *> own_sets;
  for (const auto &[key, set] : sets)
    if (!set.beside && set.usable()) own_sets.push_back(&set);
  for (Bare *b : left) {
    if (pair_by_best) continue;  // (kept as its best)
    const bool ours = std::find(made.begin(), made.end(), b->m.get()) != made.end() || r.materials.size() == 1;
    if (left.size() == 1 && own_sets.size() == 1 && ours) b->match = {own_sets[0], 0.5f};
    else b->match = {};
  }
  std::vector<std::string> used, found, still_bare;
  for (Bare &b : bare) {
    if (!b.match.set) {
      still_bare.push_back(b.m->name);
      continue;
    }
    apply_set(*b.match.set, *b.m, r, used);
    int applied = 0;
    for (int k = Base; k <= kLastSlotKind; k++) applied += !b.match.set->files[k].empty();
    const std::string base = b.match.set->first_file();
    found.push_back(b.m->name + ": " + fs::filename(base) + " (" + fs::filename(fs::parent(base)) + "/" + (applied > 1 ? strprintf(", %d maps", applied) : "") + ")");
  }
  auto say = [&](const char *what, std::vector<std::string> &list, const char *sep, const char *tail) {
    if (list.empty()) return;
    std::string text;
    for (size_t i = 0; i < list.size() && i < 8; i++) text += (i ? sep : "") + list[i];
    r.notes.push_back(strprintf("%s: %s%s%s", what, text.c_str(), list.size() > 8 ? strprintf(" and %zu more", list.size() - 8).c_str() : "", tail));
  };
  say("textures found by name", found, "; ", " (wrong? change the material's map in the Inspector)");
  say(found.empty() ? "no texture found by name for" : "no texture found for", still_bare, ", ", "");
  /* Images seen but not used: other sets, and the kinds Blendity has no slot for. */
  std::vector<std::string> unused, no_slot;
  for (const auto &[key, set] : sets) {
    const bool was_used = std::any_of(bare.begin(), bare.end(), [&](const Bare &b) { return b.match.set == &set; });
    for (int k = Ao; k < KindCount; k++)
      if (!set.files[k].empty()) no_slot.push_back(fs::filename(set.files[k]) + " (" + kKindNames[k] + ")");
    if (!was_used && !set.files[Base].empty()) unused.push_back(fs::filename(set.files[Base]));
  }
  say("texture sets seen but matching no material", unused, ", ", "");
  say("maps Blendity has no slot for", no_slot, ", ", "");
}

}  // namespace bl
