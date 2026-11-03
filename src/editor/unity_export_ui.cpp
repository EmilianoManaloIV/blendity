// SPDX-License-Identifier: GPL-2.0-or-later
// Export Unity Package (round 372, task 0349): an unpacked package's folder back to Unity as a .unitypackage - each
// file under its own GUID, Unity path and .meta (kept since the import), new files under a GUID their path always
// gives; the prefabs and scenes with what was changed here (names, active states, transforms of the objects imported
// from them) written into their own lines and every other byte as it was, scripts and VRChat's components included.
// On a worker, a file at a time (review 372: a package of hundreds of megabytes stalled the editor and was held three
// times over). The project's files are never written over: the package carries the edits.
#include "editor.h"

#include "../scene/material.h"
#include "../scene/unity_package.h"
#include "../scene/unity_scene.h"
#include "../core/core.h"

#include <set>
#include <thread>

namespace bl {

namespace {

struct Known {
  std::string guid, pathname;
};

/* The folder's .unity-guids: each project-relative path's GUID and Unity path. */
std::map<std::string, Known> known_guids(const std::string &abs_folder) {
  std::map<std::string, Known> out;
  std::string text;
  fs::read_file(fs::join(abs_folder, ".unity-guids"), text);
  for (size_t at = 0; at < text.size();) {
    size_t end = text.find('\n', at);
    if (end == std::string::npos) end = text.size();
    const std::string line = text.substr(at, end - at);
    at = end + 1;
    const size_t t1 = line.find('\t'), t2 = t1 == std::string::npos ? t1 : line.find('\t', t1 + 1);
    if (t2 != std::string::npos) out[line.substr(t2 + 1)] = {line.substr(0, t1), line.substr(t1 + 1, t2 - t1 - 1)};
  }
  return out;
}

/* A new file's .meta: its GUID alone (Unity fills in the importer's settings). */
std::string fresh_meta(const std::string &guid, bool folder) {
  return "fileFormatVersion: 2\nguid: " + guid + "\n" + (folder ? "folderAsset: yes\nDefaultImporter:\n  externalObjects: {}\n  userData: \n  assetBundleName: \n  assetBundleVariant: \n" : "");
}

}  // namespace

/* The worker: each file read, patched and compressed in turn; the file written whole at the end (none on Cancel). */
void Editor::ExportJob::run() {
  try {
    GzipWriter gz;
    for (size_t i = 0; i < items.size() && !cancel.load(); i++) {
      UnityExportItem &it = items[i];
      progress.store((float)(i + 1) / (float)items.size());
      UnityAsset a;
      a.guid = it.guid, a.pathname = it.pathname, a.meta = it.meta, a.folder = it.folder;
      a.has_data = !it.folder && fs::read_file(it.abs, a.data);
      if (!it.folder && !a.has_data) {  // (unreadable: left out and said, not an empty entry)
        skipped.push_back(it.pathname);
        continue;
      }
      int n = 0;
      if (!it.edits.empty()) a.data = patch_unity_objects(a.data, it.edits, &n), lines += n;
      gz.write(unity_asset_entries(a));
      written++;
    }
    if (cancel.load()) return finish("cancelled - nothing written");
    gz.write(std::string(1024, '\0'));
    gz.finish();
    fs::make_dirs(fs::parent(out));
    finish(fs::write_file(out, gz.output()) ? "" : "can't write " + out);
  } catch (const std::exception &e) {
    finish(std::string("stopped: ") + e.what());
  }
}

void Editor::ExportJob::finish(const std::string &failure) {
  why = failure;
  done.store(true);
}

/* The scene's edits to objects imported from files under `folder_rel`, by file: the first object of each fileID (a
 * duplicate copies its UnityOrigin - review 372: the copy's edits had overwritten the original's); nested prefabs'
 * objects left out. */
std::map<std::string, std::vector<UnityEdit>> Editor::unity_edits(const std::string &folder_rel, int &nested, int &copies) {
  std::map<std::string, std::vector<UnityEdit>> out;
  std::set<std::string> seen;
  scene_->for_each([&](GameObject &g) {
    const UnityOrigin *o = g.get<UnityOrigin>();
    if (!o || o->file.rfind(folder_rel + "/", 0) != 0) return;
    if (o->nested) return (void)nested++;
    if (!seen.insert(o->file + "#" + o->game_object).second) return (void)copies++;
    UnityEdit go, tr;
    go.file_id = std::strtoll(o->game_object.c_str(), nullptr, 10), go.name = g.name, go.active = g.active;
    tr.file_id = std::strtoll(o->transform.c_str(), nullptr, 10), tr.transform = true;
    tr.position = g.local().position, tr.rotation = g.local().rotation, tr.scale = g.local().scale, tr.euler = g.local().euler_hint;
    out[o->file].push_back(go);
    if (tr.file_id) out[o->file].push_back(tr);
    unity_component_edits(g, go.file_id, out[o->file]);  // (round 401: its camera's and light's fields)
  });
  /* Round 401: the scene's world settings into the .unity it came from. */
  if (!scene_->unity.file.empty() && scene_->unity.file.rfind(folder_rel + "/", 0) == 0) out[scene_->unity.file].push_back(unity_world_edit(scene_->environment, scene_->unity));
  return out;
}

/* Round 401: a Camera's and a Light's Unity fields as Blendity holds them now. */
void unity_component_edits(const GameObject &g, int64_t game_object, std::vector<UnityEdit> &out) {
  if (const Camera *c = g.get<Camera>()) {
    UnityEdit e;
    e.class_id = 20, e.game_object = game_object;
    /* Review 401: Unity's Depth-only and Nothing clear flags stay what they were while the camera still clears to the sky here. */
    const bool skybox_kept = c->clear_flags == 0 && (c->unity_clear_flags == 3 || c->unity_clear_flags == 4);
    e.fields = {{"m_ClearFlags", c->clear_flags == 1 ? "2" : skybox_kept ? std::to_string(c->unity_clear_flags) : "1"}, {"m_BackGroundColor", unity_yaml_color(c->background, c->background_alpha)}, {"m_HDR", c->hdr ? "1" : "0"},
                {"m_FOVAxisMode", c->fov_axis == 1 ? "1" : "0"}, {"field of view", unity_yaml_num(c->fov)}, {"near clip plane", unity_yaml_num(c->near_clip)},
                {"far clip plane", unity_yaml_num(c->far_clip)}, {"orthographic", c->orthographic ? "1" : "0"}, {"orthographic size", unity_yaml_num(c->ortho_size)},
                {"m_Enabled", c->enabled ? "1" : "0"}};
    if (c->physical) e.fields.push_back({"m_FocalLength", unity_yaml_num(c->focal_length)});
    out.push_back(e);
  }
  if (const ReflectionProbeComponent *p = g.get<ReflectionProbeComponent>()) {  // (round 401)
    UnityEdit e;
    e.class_id = 215, e.game_object = game_object;
    e.fields = {{"m_Mode", std::to_string(std::max(0, std::min(2, p->mode)))}, {"m_BoxSize", unity_yaml_vec3(p->box_size)}, {"m_BoxOffset", unity_yaml_vec3(p->box_offset)},
                {"m_BoxProjection", p->box_projection ? "1" : "0"}, {"m_IntensityMultiplier", unity_yaml_num(p->intensity)}, {"m_BlendDistance", unity_yaml_num(p->blend_distance)},
                {"m_Importance", std::to_string(p->importance)}, {"m_Resolution", std::to_string(p->resolution)}, {"m_HDR", p->hdr ? "1" : "0"},
                {"m_NearClip", unity_yaml_num(p->near_clip)}, {"m_FarClip", unity_yaml_num(p->far_clip)}, {"m_Enabled", p->enabled ? "1" : "0"}};
    out.push_back(e);
  }
  if (const Light *l = g.get<Light>()) {
    static const char *kUnityType[] = {"1", "2", "0", "3"};  // (Blendity: directional, point, spot, area -> Unity's 1, 2, 0, 3 / 4)
    UnityEdit e;
    e.class_id = 108, e.game_object = game_object;
    e.fields = {{"m_Type", l->type == 3 && l->area_shape == 1 ? "4" : kUnityType[std::max(0, std::min(3, l->type))]}, {"m_Color", unity_yaml_color(l->color)},
                {"m_Intensity", unity_yaml_num(l->intensity)}, {"m_Range", unity_yaml_num(l->range)}, {"m_SpotAngle", unity_yaml_num(l->spot_angle)},
                {"m_InnerSpotAngle", unity_yaml_num(l->inner_spot_angle)}, {"m_AreaSize", "{x: " + unity_yaml_num(l->area_width) + ", y: " + unity_yaml_num(l->area_height) + "}"},
                {"m_Lightmapping", l->mode == 2 ? "2" : l->mode == 1 ? "1" : "4"}, {"m_Shadows/m_Type", std::to_string(std::max(0, std::min(2, l->shadows)))},
                {"m_Shadows/m_Strength", unity_yaml_num(l->shadow_strength)}, {"m_BounceIntensity", unity_yaml_num(l->bounce_intensity)},
                {"m_UseColorTemperature", l->use_temperature ? "1" : "0"}, {"m_ColorTemperature", unity_yaml_num(l->temperature)}, {"m_Enabled", l->enabled ? "1" : "0"}};
    out.push_back(e);
  }
}

/* Round 401: the scene's RenderSettings (fog, ambient, reflections) as Blendity's world holds them. */
/* Review 401: the ambient is written only where Blendity's world still means what Unity's did - a Gradient that came from
 * Trilight, a Color from Flat or Custom (the mode kept), a Sky or HDRI from a Skybox (its intensity as read: the world's
 * strength folds the skybox's exposure in). A Gradient left from a skybox Blendity couldn't read writes nothing. */
UnityEdit unity_world_edit(const EnvironmentSettings &e, const UnitySceneLink &link) {
  UnityEdit w;
  w.class_id = 104;
  w.fields = {{"m_Fog", e.fog ? "1" : "0"}, {"m_FogMode", e.fog_mode == 1 ? "2" : e.fog_mode == 2 ? "3" : "1"}, {"m_FogDensity", unity_yaml_num(e.fog_density)},
              {"m_FogColor", unity_yaml_color(e.fog_color)}, {"m_LinearFogStart", unity_yaml_num(e.fog_start)}, {"m_LinearFogEnd", unity_yaml_num(e.fog_end)},
              {"m_ReflectionIntensity", unity_yaml_num(e.reflection_intensity)}};
  if (link.world_mode >= 0 && UnitySceneLink::ambient_key(e) == link.world_key) return w;  // (re-check 401: the ambient as imported - nothing to write)
  if (e.mode == 0 && link.ambient_mode == 1) w.fields.push_back({"m_AmbientMode", "1"}), w.fields.push_back({"m_AmbientSkyColor", unity_yaml_color(e.sky)}),
      w.fields.push_back({"m_AmbientEquatorColor", unity_yaml_color(e.equator)}), w.fields.push_back({"m_AmbientGroundColor", unity_yaml_color(e.ground)});
  if (e.mode == 3 && (link.ambient_mode == 3 || link.ambient_mode == 4)) w.fields.push_back({"m_AmbientMode", std::to_string(link.ambient_mode)}), w.fields.push_back({"m_AmbientSkyColor", unity_yaml_color(e.color)});
  if (e.mode == 3 && link.ambient_mode != 3 && link.ambient_mode != 4) w.fields.push_back({"m_AmbientMode", "3"}), w.fields.push_back({"m_AmbientSkyColor", unity_yaml_color(e.color)});
  if ((e.mode == 1 || e.mode == 2) && link.ambient_mode == 0) w.fields.push_back({"m_AmbientMode", "0"}), w.fields.push_back({"m_AmbientIntensity", unity_yaml_num(link.ambient_intensity)});
  return w;
}

/* export unitypackage <folder> [out]: the folder (an unpacked package under Assets/Imported) as a .unitypackage, on a
 * worker (at once headless). */
bool Editor::export_unity_package(const std::string &folder_in, const std::string &out_in) {
  if (export_job_) return Log::warn("Export Unity Package: %s is still being written", fs::filename(export_job_->out).c_str()), false;
  const std::string abs_folder = fs::normalize(resolve_asset_path(folder_in));
  const std::string folder_rel = forward_slashes(make_asset_relative(abs_folder));
  if (!fs::is_dir(abs_folder)) return Log::warn("Export Unity Package: no folder %s", folder_in.c_str()), false;
  const std::string out = fs::normalize(!out_in.empty() ? resolve_asset_path(out_in) : fs::join(fs::join(project_root_, "Exports"), fs::filename(abs_folder) + ".unitypackage"));
  if (forward_slashes(out).rfind(forward_slashes(abs_folder) + "/", 0) == 0) return Log::warn("Export Unity Package: not into the package's own folder (the next export would carry it)"), false;
  if (fs::exists(out) && fs::extension(out) != ".unitypackage") return Log::warn("Export Unity Package: %s is another kind of file - not written over", out.c_str()), false;
  auto job = std::make_shared<ExportJob>();
  job->out = out, job->folder = folder_rel;
  const std::map<std::string, Known> known = known_guids(abs_folder);
  const auto edits = unity_edits(folder_rel, job->nested, job->copies);
  std::function<void(const std::string &, const std::string &, int)> walk = [&](const std::string &dir, const std::string &unity_dir, int depth) {
    if (depth > 16) return (void)job->skipped.push_back(unity_dir + " (deeper than 16 folders)");
    for (const DirEntry &de : fs::list(dir)) {
      const std::string abs = fs::join(dir, de.name), key = forward_slashes(folder_rel + abs.substr(abs_folder.size()));
      const auto k = known.find(key);
      UnityExportItem it;
      it.abs = abs, it.folder = de.is_dir;
      it.pathname = k != known.end() ? k->second.pathname : unity_dir + "/" + de.name;
      it.guid = k != known.end() ? k->second.guid : stable_unity_guid(folder_rel, it.pathname);
      job->fresh += k == known.end();
      std::string meta;
      it.meta = fs::read_file(fs::join(fs::join(abs_folder, ".unity-meta"), it.guid + ".meta"), meta) ? meta : fresh_meta(it.guid, it.folder);
      const auto e = edits.find(key);
      if (e != edits.end()) it.edits = e->second;
      job->items.push_back(std::move(it));
      if (de.is_dir) walk(abs, job->items.back().pathname, depth + 1);
    }
  };
  walk(abs_folder, "Assets", 0);
  if (job->items.empty()) return Log::warn("Export Unity Package: %s is empty", folder_rel.c_str()), false;
  export_job_ = job;
  Log::info("Export Unity Package: writing %s", out.c_str());
  if (headless_) return job->run(), poll_unity_export(), true;
  std::thread([job] { job->run(); }).detach();
  return true;
}

/* Once a frame: a finished export said. */
void Editor::poll_unity_export() {
  if (!export_job_ || !export_job_->done.load()) return;
  const std::shared_ptr<ExportJob> job = std::move(export_job_);
  export_last_ = job;
  for (const std::string &p : job->skipped) Log::warn("Export Unity Package: %s left out (couldn't be read)", p.c_str());
  if (!job->why.empty()) return Log::warn("Export Unity Package: %s", job->why.c_str());
  Log::info("Export Unity Package: %s - %d file(s) and folder(s) under their GUIDs (%d new), %d line(s) of changes in the prefabs and scenes%s%s",
            job->out.c_str(), job->written, job->fresh, job->lines, job->nested ? strprintf("; %d object(s) of nested prefabs left as they were", job->nested).c_str() : "",
            job->copies ? strprintf("; %d copy(s) of imported objects left out (their originals' edits go)", job->copies).c_str() : "");
}

/* Quitting while an export writes: it stops at its next file (nothing written) and is waited for. */
void Editor::stop_unity_export() {
  if (!export_job_) return;
  export_job_->cancel.store(true);
  const double t0 = now_seconds();
  while (!export_job_->done.load() && now_seconds() - t0 < 10.0) std::this_thread::sleep_for(std::chrono::milliseconds(20));
}

/* File > Export Unity Package...: the package folder of the selected object - found up its parents, or below it (an
 * import's root, selected after the import - review 372). */
void Editor::export_package_of_selection() {
  GameObject *g = active_object();
  const UnityOrigin *o = nullptr;
  for (GameObject *p = g; p && !o; p = p->parent) o = p->get<UnityOrigin>();
  std::function<void(GameObject *)> below = [&](GameObject *x) {
    for (GameObject *c : x->children) {
      o = o ? o : c->get<UnityOrigin>();
      if (!o) below(c);
    }
  };
  if (!o && g) below(g);
  const std::string folder = o ? o->file : std::string();
  const size_t at = folder.rfind("Assets/Imported/", 0) == 0 ? folder.find('/', 16) : std::string::npos;
  if (at == std::string::npos)
    return refuse("Export Unity Package: select an object imported from a Unity package (its prefab or scene), or use export unitypackage <folder>", "window Project", "Open Project");
  export_unity_package(folder.substr(0, at));
}

}  // namespace bl
