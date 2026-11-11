// SPDX-License-Identifier: GPL-2.0-or-later
// Round 485 (task 0461, ADR 0131): a Unity material whose smoothness is kept in a texture's alpha comes in with that
// alpha's mean, which `.unity-stats` holds beside a package folder's `.unity-guids`. An unpack writes the table itself
// (unity_package_ui.cpp). For a package unpacked before, the textures a material asked a mean of are measured here -
// decoded on a worker, never on the frame - their lines added to their package's table, and the Unity materials read
// again. `unity rematerials` (Assets > Reimport Unity Materials) asks for all of it at once.
#include "editor.h"

#include "../scene/unity_material.h"
#include "../scene/unity_scene.h"
#include "../core/core.h"

#include <chrono>
#include <thread>
#include <unordered_map>

namespace bl {

/* The worker's whole job: each texture decoded and averaged. Nothing of the editor is touched, and nothing it meets
 * ends the program (an image too large to hold is left unmeasured). */
void Editor::GlossJob::run() {
  ScopedTimer t;
  for (size_t i = 0; i < items.size() && !cancel.load(); i++) {
    Item &it = items[i];
    it.bytes = unity_file_bytes(it.path);
    try {
      it.ok = unity_texture_mean(it.path, it.width, it.height, it.mean);
    } catch (...) {
      it.ok = false;
    }
    progress.store((float)(i + 1) / (float)items.size());
  }
  ms = t.ms();
  done.store(true);
}

namespace {

/* Every slot of `scene` that wears a copy of one of `fresh` (by its .mat's path) takes its values and its version. */
void take_reread_materials(Scene *scene, const std::unordered_map<std::string, const Material *> &fresh) {
  if (!scene) return;
  scene->for_each([&](GameObject &g) {
    MeshRenderer *mr = g.get<MeshRenderer>();
    if (!mr) return;
    for (MaterialPtr &m : mr->materials) {
      const auto it = m && !m->asset_path.empty() ? fresh.find(m->asset_path) : fresh.end();
      if (it != fresh.end() && m.get() != it->second) *m = *it->second;
    }
  });
}

}  // namespace

size_t Editor::reread_unity_materials(const std::string &under, const std::set<std::string> *only) {
  std::vector<MaterialPtr> changed;
  reload_foreign_materials(under, &changed, only);
  if (changed.empty()) return 0;
  /* Every copy of a scene the editor keeps has its own copies of a .mat's values. The states an Undo or Redo would
   * bring back: a restored state's values win over the asset's when their versions differ (relink_material_assets), so
   * left alone an Undo back past this moment would put the old reading on the asset again for the rest of the session.
   * The scene Stop brings back, which is not linked again (review 485: a job landing in Play left it the old reading).
   * And the scene itself, whose slots are such copies after a Stop. */
  std::unordered_map<std::string, const Material *> fresh;
  for (const MaterialPtr &m : changed) fresh[m->asset_path] = m.get();
  take_reread_materials(scene_.get(), fresh);
  take_reread_materials(stable_.get(), fresh);
  take_reread_materials(play_backup_.get(), fresh);
  for (UndoState &s : undo_) take_reread_materials(s.scene.get(), fresh);
  for (UndoState &s : redo_) take_reread_materials(s.scene.get(), fresh);
  ui_.redraw = true;
  return changed.size();
}

/* A finished job on the main thread: each package's new lines added to its table (a texture of no package folder is
 * kept for the session alone), and the materials that asked for those maps read again - they and no others (review
 * 485: reading every Unity material again here took a value typed on any of them, in any package, whenever some
 * package's maps happened to be measured). They find the means in the session's own table, so this frame forces no
 * fresh walk of the project; the index's next walk - when it is next found stale, or at the next start - finds the
 * lines. What the frame costs is said in the log. */
void Editor::land_unity_gloss() {
  const std::shared_ptr<GlossJob> job = std::move(gloss_job_);
  ScopedTimer landing;
  std::map<std::string, std::string> lines;  // (a package folder: its new lines)
  std::set<std::string> asked;               // (the .mat files whose maps were measured)
  size_t measured = 0;
  for (const GlossJob::Item &it : job->items) {
    if (!it.ok) {
      gloss_failed_.insert(it.guid);  // (unreadable, or not reached before Cancel: not sent again on its own)
      continue;
    }
    measured++;
    /* Kept for the session whatever becomes of the table (a folder that can't be written): a texture measured once is
     * not sent to the worker again while its file stays the one that was measured. */
    unity_gloss_session_means()[it.guid] = UnityGlossMean{it.path, it.bytes, {it.mean[0], it.mean[1], it.mean[2], it.mean[3]}};
    asked.insert(it.materials.begin(), it.materials.end());
    if (!it.table.empty()) lines[it.table] += unity_stats_line(it.guid, it.bytes, it.width, it.height, it.mean);
  }
  for (const auto &[dir, added] : lines) {
    std::string text;
    fs::read_file(fs::join(dir, ".unity-stats"), text);
    if (!text.empty() && text.back() != '\n') text += '\n';  // (a table cut short, or edited by hand: the new lines start on their own)
    if (!fs::write_file(fs::join(dir, ".unity-stats"), text + added)) Log::warn("Unity materials: %s could not be written - their gloss is measured again next time", fs::join(dir, ".unity-stats").c_str());
  }
  const size_t changed = asked.empty() ? 0 : reread_unity_materials(std::string(), &asked);
  const size_t left = job->items.size() - measured;
  const std::string rest = !left ? std::string()
                           : job->cancel.load() ? strprintf(" (cancelled with %zu left: Assets > Reimport Unity Materials measures them)", left)
                                                : strprintf(" (%zu could not be read)", left);
  Log::info("Unity materials: %zu texture(s) measured for their gloss in %.0f ms%s%s, %zu material(s) read again (%.1f ms of this frame)", measured, job->ms,
            headless_ ? "" : " on a worker", rest.c_str(), changed, landing.ms());
  ui_.redraw = true;
}

/* Quitting while the job measures: it stops at its next texture and is waited for, as an unpack is - the worker must
 * not be decoding while the program's statics go. What it measured is dropped (the next session measures again). */
void Editor::stop_unity_gloss() {
  if (!gloss_job_) return;
  gloss_job_->cancel.store(true);
  const double t0 = now_seconds();
  while (!gloss_job_->done.load() && now_seconds() - t0 < 10.0) std::this_thread::sleep_for(std::chrono::milliseconds(20));
}

/* Once a frame, and where a Unity scene has just come in: a finished job lands; the textures materials asked a mean of
 * go to a worker, one job at a time. Nothing lands or starts while a package unpacks: it writes its own table (a
 * landing would write the same file from this thread - review 485) and reads its materials again where it ends.
 * Headless, the job runs and lands at once, so a script's next line reads the measured gloss. */
void Editor::poll_unity_gloss() {
  if (pkg_job_) return;
  if (gloss_job_ && gloss_job_->done.load()) land_unity_gloss();
  std::map<std::string, std::set<std::string>> &wanted = unity_gloss_wanted();
  if (gloss_job_ || wanted.empty()) return;
  const std::shared_ptr<const UnityGuidIndex> index = unity_guid_index_cached(assets_dir_, project_root_);
  auto job = std::make_shared<GlossJob>();
  std::set<std::string> known, known_guids;  // (materials whose map has a mean by now - measured since they asked: read again at once)
  for (const auto &[guid, materials] : wanted) {
    const auto it = index->find(guid);
    if (it == index->end() || gloss_failed_.count(guid)) continue;
    if (it->second.has_mean || unity_gloss_session_mean(guid, it->second.path)) {
      known.insert(materials.begin(), materials.end());
      known_guids.insert(guid);
      continue;
    }
    GlossJob::Item item;
    item.guid = guid, item.path = it->second.path, item.table = it->second.table, item.materials = materials;
    job->items.push_back(item);
  }
  wanted.clear();
  if (!known.empty()) reread_unity_materials(std::string(), &known);
  /* (A material that asks again for a map whose mean is there - one no alpha comes out of - is not read a second time:
   * once a frame for ever otherwise.) */
  for (const std::string &guid : known_guids) wanted.erase(guid);
  if (job->items.empty()) return;
  gloss_job_ = job;
  Log::info("Unity materials: measuring %zu texture(s) their gloss is read from (once; kept beside the package)", job->items.size());
  if (headless_) return job->run(), land_unity_gloss();
  std::thread([job] { job->run(); }).detach();
}

/* unity rematerials: every .mat under Assets loaded (as the Materials window loads them), the Unity ones read again
 * from their files now - whatever was read too early, or by an older rule, is put right at once - and the job started
 * for the textures whose mean is still missing. Returns how many materials changed at once. */
size_t Editor::unity_rematerials() {
  if (pkg_job_) return Log::warn("unity rematerials: not while %s unpacks - its materials are read again when it ends", fs::filename(pkg_job_->path).c_str()), 0;
  gloss_failed_.clear();  // (asked for by hand: a texture that could not be read, or was left at a Cancel, is tried again)
  unity_guid_index_cached(assets_dir_, project_root_, true);
  material_paths_listed_ = -100;
  size_t unity = 0;
  for (const std::string &path : material_asset_paths()) {
    const MaterialPtr m = material_asset(path);
    unity += m && m->foreign ? 1 : 0;
  }
  const size_t changed = reread_unity_materials();
  const size_t waiting = unity_gloss_wanted().size();
  poll_unity_gloss();
  Log::info("unity rematerials: %zu Unity material(s) read again, %zu changed%s", unity, changed,
            waiting && gloss_job_ ? strprintf("; %zu texture(s) are being measured for their gloss - the materials follow when it ends", waiting).c_str() : "");
  ui_.redraw = true;
  return changed;
}

}  // namespace bl
