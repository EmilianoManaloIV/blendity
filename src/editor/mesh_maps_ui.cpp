// SPDX-License-Identifier: GPL-2.0-or-later
// Mesh maps in the editor (task 0243, ADR 0079): the bake on a worker with the mesh copied bare, the result landing as
// PNGs under the project's Assets, the generated layout written onto the mesh, and a record on the Mesh Renderer when
// the mesh is still the one it was baked from.
#include "editor.h"
#include "../core/core.h"
#include "../core/jobs.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace bl {

namespace {

constexpr const char *kMapSuffixes[] = {"_normal.png", "_position.png", "_curvature.png", "_ao.png", "_thickness.png"};

std::string safe_file_name(std::string s) {
  for (char &c : s)
    if (!(std::isalnum((unsigned char)c) || c == '_' || c == '-' || c == ' ')) c = '_';
  while (!s.empty() && s.back() == ' ') s.pop_back();
  return s.empty() ? "Mesh" : s;
}

/* The mesh the bake reads and the record is keyed on: the evaluated mesh when the modifiers keep the geometry (a
 * deform follows the pose), else nullptr with the reason. */
const Mesh *bake_source(const GameObject *g, const MeshFilter *mf, std::string *why) {
  const Mesh *ev = g->evaluated_mesh();
  if (!ev || ev == mf->mesh.get()) return mf->mesh.get();
  if (ev->vert_count() == mf->mesh->vert_count() && ev->face_count() == mf->mesh->face_count() && ev->corner_verts.size() == mf->mesh->corner_verts.size()) return ev;
  *why = g->name + "'s modifiers add or remove geometry; apply them first (Mesh > Apply Modifiers)";
  return nullptr;
}

}  // namespace

/* The bake started on the active mesh object: one job at a time; Edit Mode and Weight Paint left first (the landing
 * writes the layout onto the mesh and is a step on the object). */
bool Editor::bake_mesh_maps_start(const MeshMapsSettings &s) {
  if (meshmaps_job_) {
    Log::warn("Mesh Maps: a bake is running (meshmaps cancel stops it)");
    return false;
  }
  if (playing_) {
    Log::warn("Mesh Maps: stop Play first");
    return false;
  }
  GameObject *g = active_object();
  MeshFilter *mf = g ? g->mesh_filter() : nullptr;
  if (!mf || !mf->mesh || mf->mesh->face_count() == 0) {
    Log::warn("Mesh Maps: select a mesh object with faces first");
    return false;
  }
  if (edit_mode_) exit_edit_mode();
  if (weight_paint_) set_weight_paint(false);
  std::string why;
  const Mesh *src = bake_source(g, mf, &why);
  if (!src) {
    Log::warn("Mesh Maps: %s", why.c_str());
    return false;
  }
  auto job = std::make_unique<MeshMapsJob>();
  job->obj = g->id;
  job->mesh_uid = mf->mesh->uid.v;
  job->version = mf->mesh->version, job->deform_version = mf->mesh->deform_version;
  job->input = mesh_copy_bare(*src);
  for (int32_t &grp : job->input->face_group)
    if (grp < 0) grp = 0;  // (hidden faces are part of the surface the maps describe: they cover texels and occlude)
  job->model = g->world_matrix();
  job->settings = s;
  /* New files every time, as Bake Textures names its images; the worker writes them (five PNGs at 2,048 are a
   * second's work: not the frame's). */
  std::string name = safe_file_name(g->name);
  for (int k = 2; fs::exists(resolve_asset_path("Assets/Textures/MeshMaps/" + name + "_ao.png")); k++) name = safe_file_name(g->name) + " " + std::to_string(k);
  job->rel = "Assets/Textures/MeshMaps/" + name;
  job->base = resolve_asset_path(job->rel);
  MeshMapsJob *raw = job.get();
  job->thread = std::thread([raw] {
    JobSystem::global().run_background([raw] {  // (the bake's parallel rows in an arena of their own, the main thread's loops untouched)
      raw->ok = bake_mesh_maps(*raw->input, raw->model, raw->settings, raw->result, &raw->why, &raw->cancel, &raw->progress);
      if (raw->ok && !raw->cancel.load(std::memory_order_relaxed)) raw->ok = write_mesh_maps(raw->result, raw->base, &raw->why);
    });
    raw->done.store(true, std::memory_order_release);
  });
  meshmaps_job_ = std::move(job);
  Log::info("Mesh Maps: baking %d x %d with %d samples on a worker (Esc in the Scene view or `meshmaps cancel` stops it)", s.size, s.size, s.samples);
  ui_.redraw = true;
  return true;
}

/* Each frame: a finished bake lands - the generated layout written onto the mesh, the record set - as one step when
 * the mesh is still the one it was baked from and no mode holds it; dropped, its files removed, with a log line
 * otherwise. */
void Editor::meshmaps_poll() {
  if (!meshmaps_job_ || !meshmaps_job_->done.load(std::memory_order_acquire)) return;
  std::unique_ptr<MeshMapsJob> job = std::move(meshmaps_job_);
  if (job->thread.joinable()) job->thread.join();
  std::string drop;
  if (job->cancel.load(std::memory_order_relaxed)) drop = "cancelled";
  else if (!job->ok) drop = job->why;
  GameObject *g = drop.empty() ? scene_->find(job->obj) : nullptr;
  MeshFilter *mf = g ? g->mesh_filter() : nullptr;
  MeshRenderer *mr = g ? g->get<MeshRenderer>() : nullptr;
  if (drop.empty()) {
    const bool same = mf && mf->mesh && mr && mf->mesh->uid.v == job->mesh_uid && mf->mesh->version == job->version && mf->mesh->deform_version == job->deform_version;
    if (!same) drop = "the mesh changed while they baked (a stroke, an undo, another scene)";
    else if (edit_mode_ || weight_paint_ || playing_) drop = "the object is in Edit Mode, Weight Paint or Play";
    else if (!job->result.own_uvs && job->result.uvs.size() != mf->mesh->corner_verts.size()) drop = "the layout no longer fits the mesh";
  }
  if (!drop.empty()) {  // (the job's destructor removes the files of a bake that did not land)
    if (drop == "cancelled") Log::info("Mesh Maps: cancelled - nothing kept");  // (the user's own doing: not a warning)
    else Log::warn("Mesh Maps: %s - nothing kept", drop.c_str());
    ui_.redraw = true;
    return;
  }
  commit_undo();
  if (!job->result.own_uvs) {  // (the generated layout becomes the mesh's UVs, as Bake Textures does: the maps fit what the object has)
    MeshPtr &m = mesh_make_mutable(mf->mesh);
    m->uvs = job->result.uvs;
    m->touch();
  }
  std::string why;
  const Mesh *src = bake_source(g, mf, &why);
  mr->mesh_maps = job->rel;
  mr->mesh_maps_key = mesh_maps_key_text(src ? *src : *mf->mesh, g->world_matrix());
  job->landed = true;
  mark_changed("Bake Mesh Maps");
  project_listed_ = -100;  // (the Project window lists the new files)
  Log::info("Mesh Maps: %s_{normal,position,curvature,ao,thickness}.png - %d x %d, %zu texels, %s UVs, %llu rays, %.0f ms", job->rel.c_str(), job->result.size, job->result.size,
            job->result.texels, job->result.own_uvs ? "the mesh's own" : (job->settings.keep_uvs ? "generated (the mesh's own overlap or leave 0..1: the layout is written onto the mesh)" : "generated (written onto the mesh)"),
            (unsigned long long)job->result.rays, job->result.ms_total);
  ui_.redraw = true;
}

void Editor::meshmaps_cancel() {
  if (!meshmaps_job_) return;
  meshmaps_job_->cancel.store(true, std::memory_order_relaxed);
  Log::info("Mesh Maps: cancelling (it stops at the next row)");
}

/* Whether the object's record is for a mesh other than the one it has now (its key: the evaluated positions,
 * topology and UVs hashed - not a session counter, so it survives a save and a reopen). */
bool Editor::mesh_maps_stale(const GameObject *g) const {
  const MeshFilter *mf = g ? g->mesh_filter() : nullptr;
  const MeshRenderer *mr = g ? g->get<MeshRenderer>() : nullptr;
  if (!mf || !mf->mesh || !mr || mr->mesh_maps.empty()) return false;
  std::string why;
  const Mesh *src = bake_source(g, mf, &why);
  return mesh_maps_key_text(src ? *src : *mf->mesh, g->world_matrix()) != mr->mesh_maps_key;
}

/* meshmaps [size] [samples] [keepuvs|layout] | cancel | status */
void Editor::meshmaps_command(const std::vector<std::string> &t) {
  auto arg = [&](size_t i, const std::string &d) { return t.size() > i ? to_lower(t[i]) : d; };
  if (arg(1, "") == "cancel") {
    meshmaps_cancel();
    return;
  }
  if (arg(1, "") == "status") {
    const GameObject *g = active_object();
    const MeshRenderer *mr = g ? g->get<MeshRenderer>() : nullptr;
    if (!mr || mr->mesh_maps.empty()) Log::info("Mesh Maps: none baked for %s", g ? g->name.c_str() : "no object");
    else Log::info("Mesh Maps: %s (%s)", mr->mesh_maps.c_str(), mesh_maps_stale(g) ? "stale: the mesh changed since" : "current");
    return;
  }
  MeshMapsSettings s = meshmaps_settings_;
  for (size_t i = 1; i < t.size(); i++) {
    const std::string a = to_lower(t[i]);
    const bool number = !a.empty() && a.size() <= 9 && std::all_of(a.begin(), a.end(), [](char ch) { return ch >= '0' && ch <= '9'; });
    if (number && i == 1) s.size = std::max(256, std::min(4096, (int)std::strtol(a.c_str(), nullptr, 10)));
    else if (number) s.samples = std::max(4, std::min(64, (int)std::strtol(a.c_str(), nullptr, 10)));
    else if (a == "keepuvs") s.keep_uvs = true;
    else if (a == "layout") s.keep_uvs = false;
    else {
      Log::info("usage: meshmaps [size 256-4096] [samples 4-64] [keepuvs|layout] | cancel | status");
      return;
    }
  }
  meshmaps_settings_ = s;
  bake_mesh_maps_start(s);
}

}  // namespace bl
