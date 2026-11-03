// SPDX-License-Identifier: GPL-2.0-or-later
// A model's part numbers from Unity (round 395, task 0372): a scene or prefab overrides a model's parts - a renderer's
// materials, its shadows, a node turned off - by the fileIDs Unity gave them when it imported the model, and Unity
// 2019.3+ doesn't write those into the model's .meta. The meshes' and materials' numbers are hashes of their names
// (unity_hashed_file_id); the nodes' and their components' aren't any Blendity could find. So Unity, when it's
// installed, is asked once: a scratch project in the cache with the models and their own .meta files, an editor
// script that lists every part's number, in batch mode on a worker. The table goes into the project's
// Assets/.unity-model-ids, and the next import of the scene uses it.
#include "editor.h"

#include "../scene/unity_scene.h"
#include "../core/core.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <thread>

namespace bl {

namespace {

constexpr int kIdsRunning = -1000000;

/* The editor script Unity runs: "<guid>\t<fileID>\t<class>\t<path below the model's root>" for every GameObject,
 * Transform, MeshRenderer, SkinnedMeshRenderer and MeshFilter of every model in the project. */
const char *const kDumpScript = R"cs(using System.IO;
using System.Text;
using UnityEditor;
using UnityEngine;

public static class BlendityModelIds {
    static string PathOf(Transform t) {
        string p = "";
        for (; t.parent != null; t = t.parent) p = p.Length == 0 ? t.name : t.name + "/" + p;
        return p;
    }

    static int ClassOf(Object o) {
        if (o is GameObject) return 1;
        if (o is SkinnedMeshRenderer) return 137;
        if (o is MeshRenderer) return 23;
        if (o is MeshFilter) return 33;
        if (o is Transform) return 4;
        return 0;
    }

    public static void Run() {
        var sb = new StringBuilder();
        foreach (string asset in AssetDatabase.FindAssets("t:Model", new[] { "Assets" })) {
            string path = AssetDatabase.GUIDToAssetPath(asset);
            foreach (Object o in AssetDatabase.LoadAllAssetsAtPath(path)) {
                int cls = o == null ? 0 : ClassOf(o);
                if (cls == 0 || !AssetDatabase.TryGetGUIDAndLocalFileIdentifier(o, out string guid, out long fid)) continue;
                Transform t = o is GameObject go ? go.transform : ((Component)o).transform;
                sb.Append(guid).Append('\t').Append(fid).Append('\t').Append(cls).Append('\t').Append(PathOf(t)).Append('\n');
            }
        }
        File.WriteAllText("blendity-model-ids.tsv", sb.ToString());
    }
}
)cs";

/* A GUID as Unity writes one: 32 hex digits (review 395: a .meta's own text builds a path from it). */
bool plain_guid(const std::string &g) {
  return g.size() == 32 && std::all_of(g.begin(), g.end(), [](char c) { return std::isxdigit((unsigned char)c) != 0; });
}

std::string ids_cache_dir() {
  const char *env = std::getenv("BLENDITY_CACHE_DIR");
  return fs::join(env && *env ? std::string(env) : fs::join(fs::join(fs::home_dir(), ".blendity"), "cache"), "unity-model-ids");
}

/* Unity's own folders under Unity Hub, newest version first. */
std::vector<std::string> hub_editors() {
#if defined(_WIN32)
  const std::vector<std::string> roots = {"C:/Program Files/Unity/Hub/Editor"};
  const char *exe = "Editor/Unity.exe";
#elif defined(__APPLE__)
  const std::vector<std::string> roots = {"/Applications/Unity/Hub/Editor"};
  const char *exe = "Unity.app/Contents/MacOS/Unity";
#else
  const std::vector<std::string> roots = {fs::join(fs::home_dir(), "Unity/Hub/Editor")};
  const char *exe = "Editor/Unity";
#endif
  std::vector<std::string> out;
  for (const std::string &root : roots)
    for (const DirEntry &de : fs::list(root))
      if (de.is_dir && fs::exists(fs::join(fs::join(root, de.name), exe))) out.push_back(fs::join(fs::join(root, de.name), exe));
  std::sort(out.rbegin(), out.rend());
  return out;
}

/* The table's lines for the GUIDs in `fresh` replaced by its lines; the others kept. */
std::string merge_ids(const std::string &old_text, const std::string &fresh) {
  std::set<std::string> guids;
  for (size_t at = 0; at + 32 <= fresh.size(); at = fresh.find('\n', at) == std::string::npos ? fresh.size() : fresh.find('\n', at) + 1)
    guids.insert(fresh.substr(at, 32));
  std::string out;
  for (size_t at = 0; at < old_text.size();) {
    size_t end = old_text.find('\n', at);
    if (end == std::string::npos) end = old_text.size();
    const std::string line = old_text.substr(at, end - at);
    at = end + 1;
    if (line.size() >= 32 && !guids.count(line.substr(0, 32))) out += line + "\n";
  }
  return out + fresh;
}

}  // namespace

std::string Editor::unity_editor_program() const {
  const char *env = std::getenv("BLENDITY_UNITY");
  if (env && *env) return fs::exists(env) ? std::string(env) : std::string();
  const std::vector<std::string> hub = hub_editors();
  return hub.empty() ? std::string() : hub.front();
}

std::string Editor::unity_model_ids_path() const { return fs::join(assets_dir_, ".unity-model-ids"); }

/* unity modelids [all] [wait]: the models the last import waited on (or every model in the project), numbered by Unity;
 * `wait` blocks till it's done (a script's: the editor's own runs it in the background). */
void Editor::read_unity_model_ids(bool all, bool wait) {
  if (unity_ids_job_) return Log::warn("Unity model IDs: Unity is already numbering %d model(s)", unity_ids_job_->models);
  if (wait && !headless_) return Log::warn("unity modelids wait: a script's (it holds the editor till Unity is done) - here it runs in the background without it");
  const std::string unity = unity_editor_program();
  if (unity.empty())
    return refuse("Unity model IDs: no Unity found (Unity Hub's, or BLENDITY_UNITY): a model's parts are numbered by the Unity that "
                  "imports it - install Unity through Unity Hub and run this again", "", "");
  const std::shared_ptr<const UnityGuidIndex> index = unity_guid_index_cached(assets_dir_, project_root_, true);
  auto job = std::make_shared<UnityIdsJob>();
  job->project = fs::join(ids_cache_dir(), strprintf("%016llx", (unsigned long long)fnv1a_str(assets_dir_.c_str())));
  for (const auto &[guid, e] : *index) {
    if (!all && !unity_ids_waiting_.count(guid)) continue;
    if (!plain_guid(guid) || !model_extension_supported(fs::extension(e.path)) || e.meta.empty()) continue;
    job->files.push_back({e.path, e.meta, "Assets/Models/" + guid + "/" + fs::filename(e.path)});
  }
  job->models = (int)job->files.size();
  if (!job->models) return Log::warn("Unity model IDs: no model to number%s", all ? "" : " (import a Unity scene first, or: unity modelids all)");
  unity_ids_job_ = job;
  if (wait) {
    job->run(unity);
    return poll_unity_model_ids();
  }
  std::thread([job, unity] { job->run(unity); }).detach();
  const std::string what = strprintf("Unity is numbering %d model(s)' parts in the background (a minute or more)", job->models);
  Log::info("%s", what.c_str());
  toast(LogLevel::Info, what);
}

/* On the worker: the scratch project (made once, kept in the cache), the models and their .meta files, the script; then
 * Unity in batch mode. */
void Editor::UnityIdsJob::run(const std::string &unity) {
  const std::string log = fs::join(project, "blendity-model-ids.log");
  if (!fs::exists(fs::join(project, "ProjectSettings"))) {
    fs::make_dirs(fs::parent(project));
    sys::run_program({unity, "-batchmode", "-nographics", "-createProject", project, "-quit", "-logFile", log}, 1800);
  }
  fs::make_dirs(fs::join(project, "Assets/Editor"));
  fs::write_file(fs::join(project, "Assets/Editor/BlendityModelIds.cs"), kDumpScript);
  std::error_code ec;  // (review 395: an earlier run's copies would number stale models over fresh lines)
  std::filesystem::remove_all(std::filesystem::u8path(fs::join(project, "Assets/Models")), ec);
  std::filesystem::remove(std::filesystem::u8path(fs::join(project, "Assets/Models.meta")), ec);
  for (const IdsFile &f : files) {
    const std::string to = fs::join(project, f.to);
    fs::make_dirs(fs::parent(to));
    if (!fs::copy_file(f.from, to) || !fs::write_file(to + ".meta", f.meta)) return why = "couldn't copy " + fs::filename(f.from) + " into " + project, code.store(-1);
  }
  fs::remove_file(fs::join(project, "blendity-model-ids.tsv"));
  code.store(sys::run_program({unity, "-batchmode", "-nographics", "-projectPath", project, "-executeMethod", "BlendityModelIds.Run", "-quit", "-logFile", log}, 3600));
}

void Editor::poll_unity_model_ids() {
  if (!unity_ids_job_ || unity_ids_job_->code.load() == kIdsRunning) return;
  const std::shared_ptr<UnityIdsJob> job = std::move(unity_ids_job_);
  std::string fresh, old_text;
  fs::read_file(fs::join(job->project, "blendity-model-ids.tsv"), fresh);
  if (job->code.load() != 0 || fresh.empty())
    return refuse("Unity model IDs: Unity didn't number the models (" + (job->why.empty() ? strprintf("exit %d", job->code.load()) : job->why) +
                  "): its log is " + fs::join(job->project, "blendity-model-ids.log"), "", "");
  fs::read_file(unity_model_ids_path(), old_text);
  if (!fs::write_file(unity_model_ids_path(), merge_ids(old_text, fresh))) return refuse("Unity model IDs: couldn't write " + unity_model_ids_path(), "", "");
  std::map<std::string, UnityModelIds> parsed;
  parse_unity_model_ids(fresh, parsed);
  for (const auto &[guid, ids] : parsed) unity_ids_waiting_.erase(guid);
  const std::string verb = unity_ids_last_opened_ ? "open " : "import ";  // (review 395: an opened scene is opened again, not imported on top of itself)
  const std::string again = unity_ids_last_import_.empty() ? std::string() : verb + unity_ids_last_import_;
  const std::string what = strprintf("Unity numbered %zu model(s)' parts: %s the scene again and their overrides apply", parsed.size(),
                                     unity_ids_last_opened_ ? "open" : "import");
  Log::info("%s", what.c_str());
  toast(LogLevel::Info, what, again, again.empty() ? "" : unity_ids_last_opened_ ? "Open Again" : "Import Again");
}

/* A .unity opened as a scene (round 395): a new one, its objects at the top, its lighting; asked first when this scene
 * has unsaved changes. */
void Editor::open_unity_scene_asked(const std::string &path) {
  ask_unsaved("Opening " + fs::filename(path), [this, path] { open_unity_scene(path); });
}

bool Editor::open_unity_scene(const std::string &typed) {
  const std::string path = unity_scene_path(typed);
  std::string text, why;
  std::vector<UnityDoc> docs;
  if (!fs::read_file(path, text) || !parse_unity_yaml(text, docs, why) || docs.empty())  // (review 395: read first - a typo had emptied the scene)
    return Log::warn("Open Unity scene: can't read %s%s", typed.c_str(), why.empty() ? "" : (" - " + why).c_str()), false;
  new_scene();
  std::vector<GameObject *> starter;
  scene_->for_each([&](GameObject &g) {
    if (!g.parent) starter.push_back(&g);
  });
  for (GameObject *g : starter) scene_->destroy(g);
  scene_->name = fs::stem(path);
  const bool ok = import_unity_scene(path, true, &docs);
  clear_selection();
  frame_selected();  // (nothing selected: the whole scene in view)
  cam_.update(clock_now() + 10.0);
  stable_ = scene_->clone();
  undo_.clear(), redo_.clear();
  scene_dirty_ = ok;  // (not saved as a Blendity scene yet)
  return ok;
}

}  // namespace bl
