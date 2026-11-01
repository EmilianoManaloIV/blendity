// SPDX-License-Identifier: GPL-2.0-or-later
// Import Unity Package (round 369, task 0346): a .unitypackage unpacked into Assets/Imported/<package>/ with Unity's
// own folders, on a worker (a package can be hundreds of megabytes), shown in the job tray with Cancel; its GUIDs kept
// in .unity-guids beside the files for the way back. The files then are the project's: models, textures and the rest
// open as any dropped file does; scripts, prefabs and scenes stay as they are for the rounds after (tasks 0347-0349).
#include "editor.h"

#include "../scene/unity_package.h"
#include "../scene/unity_scene.h"
#include "../scene/material.h"
#include "../core/core.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <thread>

namespace bl {

namespace {

/* What kind of file it is, for the summary. */
int asset_kind(const std::string &path) {
  const std::string ext = fs::extension(path);
  if (model_extension_supported(ext)) return 0;
  if (image_extension_supported(ext) || ext == ".psd" || ext == ".tif" || ext == ".tiff") return 1;
  if (ext == ".cs" || ext == ".js" || ext == ".shader" || ext == ".cginc" || ext == ".hlsl") return 2;  // (kept as they are)
  if (ext == ".prefab" || ext == ".unity" || ext == ".mat" || ext == ".controller" || ext == ".anim") return 3;
  return 4;
}

/* A GUID as Unity writes it: 32 hex digits (review 369: a tab or a newline in one broke .unity-guids). */
bool unity_guid(const std::string &g) {
  return g.size() == 32 && std::all_of(g.begin(), g.end(), [](char c) { return std::isxdigit((unsigned char)c) != 0; });
}

}  // namespace

/* The worker's whole job: read, unpack, write - nothing of the editor touched. Nothing it meets ends the program
 * (review 369: a pathname that isn't UTF-8 threw on the worker, and Blendity closed). */
void Editor::PackageJob::run() {
  try {
    unpack();
  } catch (const std::exception &e) {
    finish(std::string("stopped: ") + e.what());
  } catch (...) {
    finish("stopped by an error");
  }
}

void Editor::PackageJob::unpack() {
  std::string bytes;
  std::vector<UnityAsset> assets;
  if (!fs::read_file(path, bytes)) return finish("can't be read");
  if (!read_unity_package(bytes, assets, why, &cancel)) return finish(why);
  std::string guids;
  std::set<std::string> written;  // (each target once, as Windows sees it: Case.txt and case.txt are one file)
  for (size_t i = 0; i < assets.size() && !cancel.load(); i++) {
    UnityAsset &a = assets[i];
    progress.store((float)(i + 1) / (float)assets.size());
    const std::string rel = unity_package_target(a.pathname, into_rel);
    const bool dup = !rel.empty() && !written.insert(unity_path_key(rel)).second;
    if (rel.empty() || !unity_guid(a.guid) || a.duplicate || dup) {
      skipped.push_back(a.pathname + (a.duplicate || dup ? " (twice in the package)" : ""));  // (never written)
      continue;
    }
    if (!a.has_data && !a.folder) continue;  // (a .meta without its file: nothing to make)
    if (!write_asset(a, rel)) {
      skipped.push_back(a.pathname);
      continue;
    }
    guids += a.guid + "\t" + a.pathname + "\t" + rel + "\n";  // (only what was made - review 369)
    if (!a.meta.empty()) {  // (round 370: the .meta kept - a model's names its submeshes' fileIDs; hidden, as Unity hides them)
      fs::make_dirs(fs::join(fs::join(project_root, into_rel), ".unity-meta"));
      fs::write_file(fs::join(fs::join(fs::join(project_root, into_rel), ".unity-meta"), a.guid + ".meta"), a.meta);
    }
    std::string().swap(a.data);
  }
  fs::make_dirs(fs::join(project_root, into_rel));
  fs::write_file(fs::join(fs::join(project_root, into_rel), ".unity-guids"), guids);
  finish(cancel.load() ? "cancelled - the files unpacked so far stay in " + into_rel : "");
}

/* One asset written (a folder made); counted. */
bool Editor::PackageJob::write_asset(const UnityAsset &a, const std::string &rel) {
  const std::string abs = fs::join(project_root, rel);
  if (a.folder) return fs::make_dirs(abs), folders++, true;
  fs::make_dirs(fs::parent(abs));
  if (!fs::write_file(abs, a.data)) return false;
  counts[asset_kind(abs)]++;
  return true;
}

void Editor::PackageJob::finish(const std::string &failure) {
  why = failure;
  done.store(true);
}

/* Import Unity Package: unpacked on a worker; false when it can't start (one already running, no file). */
bool Editor::import_unity_package(const std::string &path) {
  if (pkg_job_) return Log::warn("Import Unity Package: %s is still being unpacked", fs::filename(pkg_job_->path).c_str()), false;
  if (!fs::exists(path)) return Log::warn("Import Unity Package: no file %s", path.c_str()), false;
  auto job = std::make_shared<PackageJob>();
  job->path = path;
  job->project_root = project_root_;
  job->into_rel = "Assets/Imported/" + unity_package_folder(fs::stem(path));
  pkg_job_ = job;
  Log::info("Import Unity Package: unpacking %s into %s", fs::filename(path).c_str(), job->into_rel.c_str());
  if (headless_) return job->run(), poll_unity_package(), true;  // (headless runs and the tests: at once)
  std::thread([job] { job->run(); }).detach();
  return true;
}

/* Once a frame: a finished unpacking said and shown in the Project window. */
void Editor::poll_unity_package() {
  if (!pkg_job_ || !pkg_job_->done.load()) return;
  const std::shared_ptr<PackageJob> job = std::move(pkg_job_);
  pkg_last_ = job;
  project_listed_ = -100;
  unity_guid_index_cached(assets_dir_, project_root_, true);  // (round 371: the new GUIDs known before its materials load)
  image_assets_time_ = -100;
  for (const std::string &p : job->skipped) Log::warn("Import Unity Package: %s was left out (its path leaves the package's folder, or it couldn't be written)", p.c_str());
  if (!job->why.empty()) return Log::warn("Import Unity Package: %s - %s", fs::filename(job->path).c_str(), job->why.c_str());
  Log::info("Import Unity Package: %s into %s - %d model(s), %d texture(s), %d script/shader file(s) kept as they are, %d prefab/scene/material "
            "file(s), %d other, %d folder(s)", fs::filename(job->path).c_str(), job->into_rel.c_str(), job->counts[0], job->counts[1], job->counts[2],
            job->counts[3], job->counts[4], job->folders);
}

/* Quitting while a package unpacks: it stops at its next file and is waited for (review 369: the thread was cut at
 * exit, a file left half written and no .unity-guids). */
void Editor::stop_unity_package() {
  if (!pkg_job_) return;
  pkg_job_->cancel.store(true);
  const double t0 = now_seconds();
  while (!pkg_job_->done.load() && now_seconds() - t0 < 10.0) std::this_thread::sleep_for(std::chrono::milliseconds(20));
}

/* File > Import Unity Package...: the picker on .unitypackage files. */
void Editor::import_package_dialog() {
  defer([this] {
    std::string path;
    if (pick_open_path("Import Unity Package", import_dir_.empty() ? assets_dir_ : import_dir_, {{"Unity packages", {".unitypackage"}}}, path) != PathPick::Chosen) return;
    import_dir_ = fs::parent(path);
    import_unity_package(path);
  });
}

}  // namespace bl
