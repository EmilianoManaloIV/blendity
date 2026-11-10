// SPDX-License-Identifier: GPL-2.0-or-later
// Round 484 (task 0460): two measuring verbs for tools/unity_materials.py, which sets what Blendity made of a Unity
// material beside what Unity's own shader reads from the same .mat.
//   unity materials [all] [to "<file>"]       every material the scene's renderers wear (all: every .mat under Assets
//                                             too) - its values and, for each map slot, the path and whether a file is
//                                             behind it
//   unity mapstats [decoders] "<image>"... [to "<file>"]
//                                             an image as Blendity decodes it as data: size, each channel's mean, least
//                                             and greatest value, and what loading it took; with `decoders`, also its
//                                             means through Blendity's own decoder (where the image library differs)
// Neither changes the scene, saves anything or takes an undo step. The text is line by line `key value...`, strings in
// quotes, so a script reads it without knowing Blendity's scene format.
#include "editor.h"

#include "../core/core.h"
#include "../image/image.h"
#include "../scene/material.h"

#include <algorithm>
#include <cfloat>
#include <unordered_map>

namespace bl {

namespace {

/* A string as the listing writes it: in quotes, a quote or a backslash inside it escaped. */
std::string quoted(const std::string &s) {
  std::string out = "\"";
  for (char c : s) {
    if (c == '"' || c == '\\') out += '\\';
    out += c;
  }
  return out + "\"";
}

/* A map slot: `-` when empty; else its path, whether an image is behind it (`ok`: the file is there, or the slot names
 * a generated or procedural texture; `missing`: no such file) and how it is read (`data` or `colour`). */
std::string slot_text(const TextureRef &t) {
  if (t.empty()) return "-";
  const bool file = !starts_with(t.path, "generated:") && !texture_ref_is_procedural(t);
  const bool there = !file || fs::exists(resolve_asset_path(t.path));
  return quoted(t.path) + (there ? " ok" : " missing") + (t.non_color ? " data" : " colour");
}

struct Wear {
  size_t users = 0;
  std::string first;  // the first renderer that wears it, by its path in the Hierarchy
};

std::string hierarchy_path(const GameObject &g) {
  std::string path = g.name;
  for (const GameObject *p = g.parent; p; p = p->parent) path = p->name + "/" + path;
  return path;
}

std::string material_block(const Material &m, const Wear &w) {
  std::string s = "material " + quoted(m.name) + "\n";
  s += m.asset_path.empty() ? std::string("  asset - scene\n") : "  asset " + quoted(m.asset_path) + (m.foreign ? " unity\n" : " blendity\n");
  s += strprintf("  users %zu first %s\n", w.users, w.users ? quoted(w.first).c_str() : "-");
  s += strprintf("  surface %d shading %d unlit %d double_sided %d\n", m.surface, m.shading_model, m.unlit ? 1 : 0, m.double_sided ? 1 : 0);
  s += strprintf("  base_color %.6g %.6g %.6g alpha %.6g\n", m.base_color.x, m.base_color.y, m.base_color.z, m.alpha);
  s += strprintf("  metallic %.6g roughness %.6g specular %.6g\n", m.metallic, m.roughness, m.specular);
  s += strprintf("  exposure_occlusion %.6g emission_strength %.6g normal_strength %.6g\n", m.exposure_occlusion, m.emission_strength, m.normal_strength);
  s += "  base_map " + slot_text(m.base_map) + "\n";
  s += "  metallic_map " + slot_text(m.metallic_map) + "\n";
  s += "  roughness_map " + slot_text(m.roughness_map) + "\n";
  s += "  normal_map " + slot_text(m.normal_map) + "\n";
  s += "  emission_map " + slot_text(m.emission_map) + "\n";
  return s + "end\n";
}

/* An image's channels over its full-size level: the mean, the least and the greatest value of each, in 0..1 for an
 * 8-bit image (as stored: no colour curve - a data map's numbers). */
struct ChannelStats {
  double mean[4] = {0, 0, 0, 0};
  float lo[4] = {FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX}, hi[4] = {-FLT_MAX, -FLT_MAX, -FLT_MAX, -FLT_MAX};
};

ChannelStats channel_stats(const Texture::Level &l) {
  ChannelStats st;
  const size_t n = l.px8.empty() ? l.pxf.size() : l.px8.size();
  uint64_t sum8[4] = {0, 0, 0, 0};
  uint32_t lo8[4] = {255, 255, 255, 255}, hi8[4] = {0, 0, 0, 0};
  for (const uint32_t px : l.px8)
    for (int k = 0; k < 4; k++) {
      const uint32_t v = (px >> (8 * k)) & 0xFFu;
      sum8[k] += v, lo8[k] = std::min(lo8[k], v), hi8[k] = std::max(hi8[k], v);
    }
  for (int k = 0; k < 4 && !l.px8.empty(); k++)
    st.mean[k] = (double)sum8[k] / (255.0 * (double)n), st.lo[k] = (float)lo8[k] / 255.0f, st.hi[k] = (float)hi8[k] / 255.0f;
  if (!l.px8.empty()) return st;
  for (const Vec4 &px : l.pxf)
    for (int k = 0; k < 4; k++) st.mean[k] += px[k], st.lo[k] = std::min(st.lo[k], px[k]), st.hi[k] = std::max(st.hi[k], px[k]);
  for (int k = 0; k < 4 && n; k++) st.mean[k] /= (double)n;
  return st;
}

/* The image through Blendity's own decoders alone (no image library, no cache, no alpha rule), as ` own_mean r g b a` -
 * or ` own_mean -` when they can't read it. A build's image library and the built-in decoder should give a file the same
 * values; `decoders` prints both so a tool can tell where they don't (round 484: a 16-bit PNG, which libpng's simple
 * interface takes as linear light). decode_image_own leaves the process's codec switch alone: the texture streamer
 * and F12's prefetch decode on other threads, and what they read stays in the cache. */
std::string own_decoder_text(const std::string &path) {
  Bitmap bmp;
  std::string bytes, err;
  const bool ok = fs::read_file(path, bytes) && decode_image_own(bytes, bmp, err);
  const size_t n = (size_t)std::max(0, bmp.width) * (size_t)std::max(0, bmp.height);
  if (!ok || n == 0 || (bmp.is_float ? bmp.rgbaf.size() : bmp.rgba8.size()) < n * 4) return " own_mean -";
  double sum[4] = {0, 0, 0, 0};
  for (size_t i = 0; i < n * 4; i++) sum[i & 3] += bmp.is_float ? (double)bmp.rgbaf[i] : (double)bmp.rgba8[i] / 255.0;
  return strprintf(" own_mean %.6f %.6f %.6f %.6f", sum[0] / (double)n, sum[1] / (double)n, sum[2] / (double)n, sum[3] / (double)n);
}

/* One image's line: `mapstats "<as given>" size W H float 0|1 alpha 0|1 mean r g b a min r g b a max r g b a load_ms t`
 * (`cached` after it when the image was in the texture cache already, so the time is not a load's), or `missing` /
 * `unreadable` in place of the numbers; with `decoders`, own_decoder_text at its end. */
std::string mapstats_line(const std::string &given, bool decoders) {
  const std::string path = resolve_asset_path(given);
  if (!fs::exists(path)) return "mapstats " + quoted(given) + " missing";
  const bool cached = texture_cached(path, false) != nullptr;
  ScopedTimer t;
  const TexturePtr tex = texture_load(path, false);
  const double ms = t.ms();
  if (!tex || tex->levels.empty() || (tex->levels[0].px8.empty() && tex->levels[0].pxf.empty())) return "mapstats " + quoted(given) + " unreadable";
  const ChannelStats st = channel_stats(tex->levels[0]);
  return strprintf("mapstats %s size %d %d float %d alpha %d mean %.6f %.6f %.6f %.6f min %.6f %.6f %.6f %.6f max %.6f %.6f %.6f %.6f load_ms %.1f%s", quoted(given).c_str(),
                   tex->levels[0].w, tex->levels[0].h, tex->is_float ? 1 : 0, tex->has_alpha ? 1 : 0, st.mean[0], st.mean[1], st.mean[2], st.mean[3], st.lo[0], st.lo[1],
                   st.lo[2], st.lo[3], st.hi[0], st.hi[1], st.hi[2], st.hi[3], ms, cached ? " cached" : "") +
         (decoders ? own_decoder_text(path) : std::string());
}

/* The text into `file` (a relative one is the project's, its folder made), or line by line into the log. */
bool write_or_log(const std::string &text, const std::string &file, const std::string &project_root, const char *verb) {
  if (file.empty()) {
    for (size_t at = 0; at < text.size();) {
      const size_t end = std::min(text.find('\n', at), text.size());
      Log::info("%s", text.substr(at, end - at).c_str());
      at = end + 1;
    }
    return true;
  }
  const bool absolute = file.size() > 1 && (file[0] == '/' || file[0] == '\\' || file[1] == ':');
  const std::string path = absolute ? file : fs::join(project_root, file);
  fs::make_dirs(fs::parent(path));
  if (fs::write_file(path, text)) return true;
  Log::error("%s: could not write %s", verb, path.c_str());
  return false;
}

/* The words after the verb: a closing `to <file>` taken out into `file`; false (and said) when `to` closes the line
 * with no file after it. Only the last two words count, so an image that happens to be named `to` is still an image. */
bool take_file_word(std::vector<std::string> &words, std::string &file, const char *verb) {
  const size_t n = words.size();
  if (n >= 1 && to_lower(words[n - 1]) == "to") return Log::warn("%s: `to` needs a file after it", verb), false;
  if (n < 2 || to_lower(words[n - 2]) != "to") return true;
  file = words[n - 1];
  words.resize(n - 2);
  return true;
}

}  // namespace

/* Every material the scene's renderers wear, in the Hierarchy's order of first use; with `all`, then the project's
 * .mat files no renderer wears (loaded as the Materials window loads them). */
std::string Editor::unity_materials_text(bool all, size_t *count, size_t *from_unity) {
  std::unordered_map<const Material *, Wear> wear;
  scene_->for_each_ordered([&](GameObject &g, int) {
    const MeshRenderer *mr = g.get<MeshRenderer>();
    for (const MaterialPtr &m : mr ? mr->materials : std::vector<MaterialPtr>()) {
      if (!m) continue;
      Wear &w = wear[m.get()];
      if (w.users++ == 0) w.first = hierarchy_path(g);
    }
  });
  std::vector<MaterialPtr> list = scene_materials();
  const size_t worn = list.size();
  for (const std::string &path : all ? material_asset_paths() : std::vector<std::string>()) {
    const MaterialPtr m = material_asset(path);
    if (m && !wear.count(m.get())) list.push_back(m);
  }
  size_t unity = 0;
  std::string text = strprintf("unity_materials 1 scene %s materials %zu worn %zu\n", quoted(scene_->name).c_str(), list.size(), worn);
  for (const MaterialPtr &m : list) {
    unity += m->foreign ? 1 : 0;
    const auto w = wear.find(m.get());
    text += material_block(*m, w == wear.end() ? Wear{} : w->second);
  }
  if (count) *count = list.size();
  if (from_unity) *from_unity = unity;
  return text;
}

void Editor::unity_materials_command(const std::vector<std::string> &words_in) {
  std::vector<std::string> words = words_in;
  std::string file;
  if (!take_file_word(words, file, "unity materials")) return;
  bool all = false;
  for (const std::string &w : words) {
    if (to_lower(w) != "all") return Log::warn("unity materials [all] [to \"<file>\"]: '%s' isn't one of its words", w.c_str());
    all = true;
  }
  ScopedTimer t;
  size_t count = 0, unity = 0;
  const std::string text = unity_materials_text(all, &count, &unity);
  if (!write_or_log(text, file, project_root_, "unity materials")) return;
  Log::info("unity materials: %zu material(s), %zu of them read from Unity files%s%s (%.1f ms)", count, unity, file.empty() ? "" : ", written to ", file.c_str(), t.ms());
}

void Editor::unity_mapstats_command(const std::vector<std::string> &words_in) {
  if (!headless_) return Log::warn("unity mapstats: a script's (it decodes every image it is given, on the main thread) - run it in a headless Blendity");
  std::vector<std::string> images = words_in;
  std::string file;
  if (!take_file_word(images, file, "unity mapstats")) return;
  const bool decoders = !images.empty() && to_lower(images[0]) == "decoders";  // (the first word: both decoders' values)
  if (decoders) images.erase(images.begin());
  if (images.empty()) return Log::warn("unity mapstats [decoders] \"<image>\"... [to \"<file>\"]: give at least one image (a relative path is the project's)");
  ScopedTimer t;
  std::string text;
  size_t read = 0;
  for (const std::string &image : images) {
    const std::string line = mapstats_line(image, decoders);
    read += line.find(" size ") != std::string::npos ? 1 : 0;
    text += line + "\n";
  }
  if (!write_or_log(text, file, project_root_, "unity mapstats")) return;
  Log::info("unity mapstats: %zu of %zu image(s) read%s%s (%.0f ms)", read, images.size(), file.empty() ? "" : ", written to ", file.c_str(), t.ms());
}

}  // namespace bl
