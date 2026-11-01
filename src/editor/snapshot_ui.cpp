// SPDX-License-Identifier: GPL-2.0-or-later
// Blender and Unreal into Blendity (round 374, task 0351):
// - import <folder>.blendity-snapshot: a bridge's Send to Blendity (scene/snapshot.h) - its model through the import
//   every model takes (skins, shape keys, clips, materials into Assets), then its lights, cameras, engine classes and
//   custom properties from snapshot.json, as one Undo step.
// - import <file>.blend: the installed Blender, in the background, writes the snapshot (bridges/blender/
//   blend_to_snapshot.py) into Library/Blend; it is imported when Blender is done.
// - The inbox: Live Link's discovery file names <project>/Incoming; a bridge's snapshot written there is imported
//   within a second, then moved to Incoming/Done. The Live Link protocol stays one-way (ADR 0055).
#include "editor.h"

#include "../scene/godot_scene.h"
#include "../scene/material.h"
#include "../scene/snapshot.h"
#include "../core/core.h"

#include <filesystem>
#include <thread>

namespace bl {

namespace {

/* The object a snapshot names: by its path under the root (names joined by '/'), else the first of its name; the root
 * itself when the model's one object became it (its name before the snapshot's went on it). */
GameObject *snapshot_object(GameObject *root, const std::string &root_was, const SnapshotObject &s) {
  if (s.name == root_was || s.path == root_was) return root;
  GameObject *by_name = nullptr, *by_path = nullptr;
  std::function<void(GameObject *, const std::string &)> walk = [&](GameObject *g, const std::string &path) {
    for (GameObject *c : g->children) {
      const std::string p = path.empty() ? c->name : path + "/" + c->name;
      by_path = by_path ? by_path : p == s.path ? c : nullptr;
      by_name = by_name ? by_name : c->name == s.name ? c : nullptr;
      walk(c, p);
    }
  };
  walk(root, "");
  return by_path ? by_path : by_name;
}

/* A glTF light or camera looks down -Z; Blendity's along +Z: turned 180 degrees about Y, its children turned back so
 * they stay where they were (as Godot's import does). */
void turn_light_or_camera(GameObject *g) {
  g->set_local(godot_turn(g->local(), true, false));
  for (GameObject *c : g->children) c->set_local(godot_turn(c->local(), false, true));
}

/* One snapshot object's parts on its object; made under the root when the model doesn't hold it (an Unreal light).
 * Its transform (the snapshot's frame, from the pivot) places one the model doesn't hold, and a light or camera; a
 * mesh the model holds keeps the model's, its geometry being in that frame (review 374). */
void apply_snapshot_object(Scene &scene, GameObject *root, const std::string &root_was, const Mat4 &pivot, const Snapshot &snap, const SnapshotObject &s,
                           int &made) {
  GameObject *g = snapshot_object(root, root_was, s);
  if (!g && !s.has_transform) return;
  const bool found = g != nullptr, lit = s.has_light || s.has_camera;
  if (!g) g = scene.create(s.name, root), made++;
  if (s.has_transform && (!found || lit))
    g->set_world_matrix(pivot * Mat4::translate(s.transform.position) * Mat4::rotate(s.transform.rotation) * Mat4::scale(s.transform.scale));
  if (s.has_light) snapshot_light(s.light, g->light() ? *g->light() : *g->add<Light>());
  if (s.has_camera) snapshot_camera(s.camera, g->camera() ? *g->camera() : *g->add<Camera>());
  if (found && lit && !s.has_transform) turn_light_or_camera(g);  // (one placed by its transform already faces Blendity's way)
  if (s.engine_class.empty() && s.props.empty()) return;
  EngineComponent *e = g->add<EngineComponent>();
  e->engine = snapshot_engine(snap), e->type = s.engine_class.empty() ? std::string("Custom Properties") : s.engine_class, e->data = s.props;
}

/* The snapshot's parts on the imported model: the model turned into the snapshot's frame about the pivot, the root
 * named (a lone object named in the snapshot keeps its name), each object's parts. */
void apply_snapshot(Scene &scene, GameObject *root, const Snapshot &snap, int &made, int &applied) {
  const std::string root_was = root->name;
  const Mat4 pivot = Mat4::translate(root->world_position());
  root->set_world_matrix(pivot * Mat4::rotate(snap.model_rotation) * Mat4::translate(-root->world_position()) * root->world_matrix());
  const bool lone_named = std::any_of(snap.objects.begin(), snap.objects.end(), [&](const SnapshotObject &s) { return s.name == root_was || s.path == root_was; });
  if (!lone_named) root->name = snap.name;
  for (const SnapshotObject &s : snap.objects) {
    applied += s.has_light || s.has_camera || !s.engine_class.empty() || !s.props.empty() || s.has_transform;
    apply_snapshot_object(scene, root, root_was, pivot, snap, s, made);
  }
}

}  // namespace

std::string Editor::snapshot_inbox() const { return fs::join(project_root_, "Incoming"); }

/* import <folder>.blendity-snapshot (or its snapshot.json). */
bool Editor::import_snapshot(const std::string &path_in) {
  const std::string folder = fs::filename(path_in) == "snapshot.json" ? fs::parent(path_in) : path_in;
  std::string text, why;
  Snapshot snap;
  if (!fs::read_file(fs::join(folder, "snapshot.json"), text)) return refuse("Import snapshot: no snapshot.json in " + folder, "", ""), false;
  if (!read_snapshot(text, snap, why)) return refuse("Import snapshot: " + fs::filename(folder) + " - " + why, "", ""), false;
  const std::string model = fs::join(folder, snap.model);
  ScopedTimer t;
  ImportResult r;
  if (!import_model(model, r)) return refuse("Import snapshot: " + snap.model + " - " + r.error, "", ""), false;
  int made = 0, applied = 0;
  finish_import(model, true, r, t, ImportOutline::Keep, 0.0f, false, [&](GameObject *root) {
    if (root) apply_snapshot(*scene_, root, snap, made, applied);
  });
  Log::info("Import snapshot: %s from %s - %d object(s) given their lights, cameras, classes or properties (%d made from the snapshot alone)",
            snap.name.c_str(), snap.source.empty() ? "another program" : snap.source.c_str(), applied, made);
  return true;
}

/* ---------------------------------------------------------------------------------------- .blend through Blender */

/* The Blender run: blend_to_snapshot.py writes the snapshot; its exit code says whether it did. Blender's own settings
 * and add-ons (--factory-startup), and never the file's scripts or drivers (--disable-autoexec): a .blend from
 * anywhere is only read (review 374). */
void Editor::run_blend_job(BlendJob &job, const std::string &blender, const std::string &script) {
  fs::make_dirs(job.out);
  const std::vector<std::string> argv{blender, "-b", "--factory-startup", "--disable-autoexec", job.blend, "--python-exit-code", "1", "--python", script, "--", job.out};
  std::string log;
  job.ok = sys::run_capture(argv, log, 600) == 0 && fs::exists(fs::join(job.out, "snapshot.json"));
  const size_t err = log.rfind("Error");
  job.message = job.ok ? std::string() : err != std::string::npos ? log.substr(err, std::min<size_t>(300, log.size() - err)) : "Blender didn't write the snapshot";
  job.done = true;
}

/* import <file>.blend: Blender in the background (a toast meanwhile; at once headless). */
bool Editor::import_blend(const std::string &path) {
  if (blend_job_) return refuse("Import .blend: Blender is still reading " + fs::filename(blend_job_->blend), "", ""), false;
  if (!fs::exists(path)) return Log::warn("Import .blend: no file %s", path.c_str()), false;
  std::string version;
  const std::string blender = find_blender(version);
  if (blender.empty())
    return refuse("Import .blend: Blender isn't found - install it from blender.org/download, or name it: blender path <blender.exe>", "", ""), false;
  auto job = std::make_shared<BlendJob>();
  job->blend = fs::normalize(path);
  job->out = fs::join(fs::join(project_root_, "Library/Blend"), fs::stem(path) + ".blendity-snapshot");
  std::error_code ec;
  std::filesystem::remove_all(job->out, ec);  // (an earlier read's: never mixed in)
  const std::string script = fs::join(bridges_dir(), "blender/blend_to_snapshot.py");
  blend_job_ = job;
  if (headless_) {
    run_blend_job(*job, blender, script);
    blend_job_poll();
    return job->ok;
  }
  std::thread([job, blender, script] { run_blend_job(*job, blender, script); }).detach();
  toast(LogLevel::Info, "Import .blend: Blender " + version + " is reading " + fs::filename(path) + "...");
  return true;
}

void Editor::blend_job_poll() {
  if (!blend_job_ || !blend_job_->done || playing_) return;  // (held through Play: Stop would take the import away - review 374)
  const std::shared_ptr<BlendJob> job = std::move(blend_job_);
  if (!job->ok) return refuse("Import .blend: " + fs::filename(job->blend) + " - " + job->message, "", "");
  import_snapshot(job->out);
}

/* blender path [exe]: the Blender .blend files are read through (found by itself when not named). */
void Editor::blender_path_command(const std::string &exe) {
  if (!exe.empty() && to_lower(exe) != "clear" && !fs::exists(exe)) return refuse("blender path: no file " + exe + " (blender path clear finds Blender by itself)", "", "");
  if (!exe.empty()) {
    blender_path_ = to_lower(exe) == "clear" ? std::string() : exe;
    save_prefs();
  }
  std::string version;
  const std::string found = find_blender(version);
  Log::info("Blender: %s%s", found.empty() ? "not found (blender path <blender.exe>)" : found.c_str(), version.empty() ? "" : (" (" + version + ")").c_str());
}

/* ---------------------------------------------------------------------------------------- the inbox */

/* The bridges' snapshots in Incoming: each imported once (its snapshot.json there means whole), then moved to Done. */
void Editor::poll_snapshot_inbox(bool now) {
  if (playing_) return now ? refuse("Send to Blendity: stop Play first - an import in Play is gone at Stop", "play", "Stop") : void();
  if (!now && now_seconds() - inbox_checked_ < 1.0) return;
  inbox_checked_ = now_seconds();
  const std::string inbox = snapshot_inbox();
  for (const DirEntry &e : fs::list(inbox)) {
    const std::string folder = fs::join(inbox, e.name);
    if (!e.is_dir || fs::extension(e.name) != ".blendity-snapshot" || !fs::exists(fs::join(folder, "snapshot.json"))) continue;
    if (!inbox_tried_.insert(folder).second) continue;  // (one that couldn't be moved: not imported every second)
    const bool ok = import_snapshot(folder);
    const std::string done = fs::join(inbox, "Done");
    fs::make_dirs(done);
    std::string to = fs::join(done, e.name);
    for (int n = 2; fs::exists(to) && n < 1000; n++) to = fs::join(done, fs::stem(e.name) + strprintf(" %d", n) + ".blendity-snapshot");
    if (!fs::move(folder, to)) Log::warn("Send to Blendity: couldn't move %s to Done (left in Incoming, not imported again)", e.name.c_str());
    else inbox_tried_.erase(folder);
    if (ok) toast(LogLevel::Info, "Sent to Blendity: " + fs::stem(e.name) + " (Ctrl+Z takes it away)");
  }
}

}  // namespace bl
