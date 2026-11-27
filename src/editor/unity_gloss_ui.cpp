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
#include <iterator>
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

/* Undo/Redo, Stop and the current scene can each own a different copy of the same asset. */
void Editor::visit_material_states(const std::function<void(Material &)> &visit) {
  auto scene_materials = [&](Scene *scene) {
    if (!scene) return;
    scene->for_each([&](GameObject &g) {
      if (auto *mr = g.get<MeshRenderer>())
        for (const MaterialPtr &m : mr->materials)
          if (m && !m->asset_path.empty()) visit(*m);
    });
  };
  scene_materials(scene_.get());
  scene_materials(stable_.get());
  scene_materials(play_backup_.get());
  for (UndoState &state : undo_) scene_materials(state.scene.get());
  for (UndoState &state : redo_) scene_materials(state.scene.get());
}

size_t Editor::reread_unity_materials(const std::string &under) {
  // The full read registers its own requests. Older requests for these paths
  // must not apply to materials that an explicit import has since replaced.
  auto &wanted = unity_gloss_wanted();
  auto pending = std::move(wanted);
  wanted.clear();
  std::vector<MaterialPtr> reread;
  const size_t changed = reload_foreign_materials(under, nullptr, nullptr, &reread);
  std::unordered_map<std::string, const Material *> fresh;
  for (const MaterialPtr &m : reread) fresh[m->asset_path] = m.get();
  for (const auto &[guid, materials] : pending)
    for (const auto &[path, provisional] : materials)
      if (!fresh.count(path)) wanted[guid].emplace(path, provisional);
  if (fresh.empty()) return 0;
  if (gloss_job_)
    for (auto &item : gloss_job_->items)
      for (auto it = item.materials.begin(); it != item.materials.end();)
        it = fresh.count(it->first) ? item.materials.erase(it) : std::next(it);
  visit_material_states([&](Material &m) {
    const auto it = fresh.find(m.asset_path);
    if (it != fresh.end() && &m != it->second) m = *it->second;
  });
  ui_.redraw = true;
  return changed;
}

size_t Editor::apply_measured_unity_gloss(const std::map<std::string, std::map<std::string, float>> &requests) {
  struct Update { float provisional, measured; };
  std::unordered_map<std::string, Update> updates;
  std::set<Material *> visited;
  auto apply = [&](Material &m) {
    const auto it = updates.find(m.asset_path);
    if (it == updates.end() || !m.foreign || !visited.insert(&m).second) return;
    if (m.roughness == it->second.provisional) m.roughness = it->second.measured;
    // Move every version once, including a copy with a hand-edited roughness.
    // Undo compares versions before restoring the copy's other material fields.
    m.touch();
  };
  for (const auto &[path, material] : loaded_material_assets()) {
    const auto it = requests.find(path);
    Update update;
    if (it == requests.end() || !material->foreign ||
        !measured_unity_roughness(path, it->second, update.provisional, update.measured)) continue;
    updates.emplace(path, update);
    apply(*material);
  }
  if (!updates.empty()) {
    visit_material_states(apply);
    ui_.redraw = true;
  }
  return updates.size();
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
  std::map<std::string, std::map<std::string, float>> asked;  // .mat path to GUID and original roughness
  size_t measured = 0, left = 0;
  for (const GlossJob::Item &it : job->items) {
    if (it.materials.empty()) continue;  // a full reimport superseded all requests for this measurement
    if (!it.ok) {
      ++left;
      gloss_failed_.insert(it.guid);  // (unreadable, or not reached before Cancel: not sent again on its own)
      continue;
    }
    measured++;
    /* Kept for the session whatever becomes of the table (a folder that can't be written): a texture measured once is
     * not sent to the worker again while its file stays the one that was measured. */
    unity_gloss_session_means()[it.guid] = UnityGlossMean{it.path, it.bytes, {it.mean[0], it.mean[1], it.mean[2], it.mean[3]}};
    for (const auto &[path, provisional] : it.materials) asked[path][it.guid] = provisional;
    if (!it.table.empty()) lines[it.table] += unity_stats_line(it.guid, it.bytes, it.width, it.height, it.mean);
  }
  for (const auto &[dir, added] : lines) {
    std::string text;
    fs::read_file(fs::join(dir, ".unity-stats"), text);
    if (!text.empty() && text.back() != '\n') text += '\n';  // (a table cut short, or edited by hand: the new lines start on their own)
    if (!fs::write_file(fs::join(dir, ".unity-stats"), text + added)) Log::warn("Unity materials: %s could not be written - their gloss is measured again next time", fs::join(dir, ".unity-stats").c_str());
  }
  const size_t changed = asked.empty() ? 0 : apply_measured_unity_gloss(asked);
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
  auto &wanted = unity_gloss_wanted();
  if (gloss_job_ || wanted.empty()) return;
  const std::shared_ptr<const UnityGuidIndex> index = unity_guid_index_cached(assets_dir_, project_root_);
  auto job = std::make_shared<GlossJob>();
  std::map<std::string, std::map<std::string, float>> known;
  std::set<std::string> known_guids;  // (materials whose map has a mean by now - measured since they asked: read again at once)
  for (const auto &[guid, materials] : wanted) {
    const auto it = index->find(guid);
    if (it == index->end() || gloss_failed_.count(guid)) continue;
    if (it->second.has_mean || unity_gloss_session_mean(guid, it->second.path)) {
      for (const auto &[path, provisional] : materials) known[path][guid] = provisional;
      known_guids.insert(guid);
      continue;
    }
    GlossJob::Item item;
    item.guid = guid, item.path = it->second.path, item.table = it->second.table, item.materials = materials;
    job->items.push_back(item);
  }
  wanted.clear();
  if (!known.empty()) apply_measured_unity_gloss(known);
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
