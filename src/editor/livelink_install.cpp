// SPDX-License-Identifier: GPL-2.0-or-later
// Install a Bridge (task 0278, ADR 0086): the Live Link window puts a bridge where its program finds it - Blender's
// extensions folder (enabled by Blender itself, in the background), a Unity project's Packages (an embedded package),
// a Godot project's addons (enabled in project.godot), an Unreal project's Plugins (enabled in the .uproject). The
// bridges come from the bridges folder beside Blendity (the installers carry it) or the repository's.
#include "editor.h"

#include "../core/core.h"

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <thread>

namespace bl {

namespace {

bool copy_tree(const std::string &from, const std::string &to, int &files) {
  if (!fs::make_dirs(to)) return false;
  for (const DirEntry &e : fs::list(from)) {
    if (e.name == "__pycache__" || e.name.empty() || e.name[0] == '.') continue;
    const std::string src = fs::join(from, e.name), dst = fs::join(to, e.name);
    if (e.is_dir && !copy_tree(src, dst, files)) return false;
    if (e.is_dir) continue;
    if (!fs::copy_file(src, dst)) return false;
    files++;
  }
  return true;
}

/* "Blender 5.2" -> 5.2 as a number to compare; 0 when the name isn't one. */
double blender_folder_version(const std::string &name) {
  const std::string prefix = "Blender ";
  if (name.rfind(prefix, 0) != 0) return 0.0;
  return std::atof(name.c_str() + prefix.size());
}

}  // namespace

/* The bridges: BLENDITY_BRIDGES_DIR, beside the program (an installed Blendity), or the repository's. */
std::string Editor::bridges_dir() const {
  if (const char *env = std::getenv("BLENDITY_BRIDGES_DIR"); env && *env) return env;
  const std::string exe = fs::executable_dir();
  for (const std::string &d : {fs::join(exe, "bridges"), fs::join(exe, "../Resources/bridges"), fs::join(exe, "../../bridges"), fs::join(fs::current_dir(), "bridges")})
    if (fs::is_dir(fs::join(d, "unity"))) return d;
  return "";
}

/* Blender: BLENDITY_BLENDER, on the PATH, or the newest under Program Files (Windows) / Applications (macOS). */
std::string Editor::find_blender(std::string &version) const {
  version.clear();
  if (const char *env = std::getenv("BLENDITY_BLENDER"); env && *env) return env;
#ifdef _WIN32
  const char *pf = std::getenv("ProgramFiles");
  const std::string base = fs::join(pf ? pf : "C:/Program Files", "Blender Foundation");
  double best = 0.0;
  std::string found;
  for (const DirEntry &e : fs::list(base)) {
    const double v = blender_folder_version(e.name);
    const std::string exe = fs::join(fs::join(base, e.name), "blender.exe");
    if (!e.is_dir || v <= best || !fs::exists(exe)) continue;
    best = v, found = exe, version = e.name.substr(8);
  }
  if (!found.empty()) return found;
#elif defined(__APPLE__)
  if (fs::exists("/Applications/Blender.app/Contents/MacOS/Blender")) return "/Applications/Blender.app/Contents/MacOS/Blender";
#endif
  return sys::find_program("blender");
}

/* Where Blender 4.2+ looks for the user's extensions (BLENDER_USER_RESOURCES moves it, as Blender does). */
static std::string blender_extensions_dir(const std::string &version) {
  if (const char *env = std::getenv("BLENDER_USER_RESOURCES"); env && *env) return fs::join(env, "extensions/user_default");
#ifdef _WIN32
  const char *appdata = std::getenv("APPDATA");
  return fs::join(appdata ? appdata : "", "Blender Foundation/Blender/" + version + "/extensions/user_default");
#elif defined(__APPLE__)
  return fs::join(fs::home_dir(), "Library/Application Support/Blender/" + version + "/extensions/user_default");
#else
  return fs::join(fs::home_dir(), ".config/blender/" + version + "/extensions/user_default");
#endif
}

/* The version Blender says ("Blender 5.2.2 LTS" -> "5.2"), "" when it says none. Runs Blender: off the main thread. */
static std::string blender_version(const std::string &blender) {
  std::string out;
  sys::run_capture({blender, "--version"}, out, 60);
  const size_t at = out.find("Blender ");
  if (at == std::string::npos) return "";
  std::string v = out.substr(at + 8, out.find_first_not_of("0123456789.", at + 8) - at - 8);
  if (std::count(v.begin(), v.end(), '.') > 1) v = v.substr(0, v.rfind('.'));
  return v;
}

/* The whole install on a thread of its own: Blender's version when the folder didn't say it, the add-on copied into its
 * extensions, then enabled by Blender itself (its preferences saved), a failed enable an exit code. */
void Editor::run_blender_install(BridgeJob &job, const std::string &blender, std::string version, const std::string &from) {
  if (version.empty()) version = blender_version(blender);
  if (version.empty()) {
    job.message = "Install the Blender bridge: couldn't tell " + blender + "'s version";
    return void(job.done = true);
  }
  const std::string to = fs::join(blender_extensions_dir(version), "blendity_livelink");
  int files = 0;
  if (!copy_tree(from, to, files) || files == 0) {
    job.message = "Install the Blender bridge: couldn't copy it into " + to;
    return void(job.done = true);
  }
  const std::vector<std::string> argv{blender, "-b", "--python-exit-code", "1", "--python-expr",
                                      "import bpy\n"
                                      "r = bpy.ops.preferences.addon_enable(module='bl_ext.user_default.blendity_livelink')\n"
                                      "assert 'FINISHED' in r, r\n"
                                      "bpy.ops.wm.save_userpref()\n"};
  std::string out;
  job.ok = sys::run_capture(argv, out, 120) == 0;
  job.message = job.ok ? "The Blender bridge is on in Blender " + version + ": it connects by itself (View3D > Sidebar > Blendity)"
                       : "The Blender bridge is copied into Blender " + version + " but it couldn't enable it: Edit > Preferences > Add-ons > Blendity Live Link";
  job.done = true;
}

bool Editor::install_bridge_blender() {
  if (bridge_job_) return refuse("Install the Blender bridge: Blender is still enabling it - a moment", "", ""), false;
  std::string version;
  const std::string blender = find_blender(version);  // (no process: folders and the PATH)
  if (blender.empty()) return refuse("Install the Blender bridge: Blender isn't found - install Blender 4.2 or newer (or set BLENDITY_BLENDER)", "", ""), false;
  /* Its state shared with the thread: an editor closed meanwhile isn't written to. */
  auto job = std::make_shared<BridgeJob>();
  bridge_job_ = job;
  const std::string from = fs::join(bridges_dir(), "blender/blendity_livelink");
  std::thread([job, blender, version, from] { run_blender_install(*job, blender, version, from); }).detach();
  toast(LogLevel::Info, "Installing the Blender bridge: Blender is enabling it...");
  return true;
}

/* The Blender install's result, once a frame while it runs. */
void Editor::bridge_job_poll() {
  if (!bridge_job_ || !bridge_job_->done) return;
  const std::shared_ptr<BridgeJob> job = std::move(bridge_job_);
  if (job->ok) toast(LogLevel::Info, job->message);
  else refuse(job->message, "", "");
}

bool Editor::install_bridge_unity(const std::string &project) {
  if (!fs::is_dir(fs::join(project, "Assets")) || !fs::is_dir(fs::join(project, "ProjectSettings")))
    return refuse("Install the Unity bridge: " + project + " isn't a Unity project (no Assets and ProjectSettings folders)", "", ""), false;
  const std::string to = fs::join(project, "Packages/com.blendity.livelink");
  int files = 0;
  if (!copy_tree(fs::join(bridges_dir(), "unity/com.blendity.livelink"), to, files) || files == 0)
    return refuse("Install the Unity bridge: couldn't copy it into " + to, "", ""), false;
  toast(LogLevel::Info, "The Unity bridge is in " + fs::filename(project) + "'s Packages: Unity loads it when the project opens (Window > Blendity Live Link)");
  return true;
}

/* project.godot's [editor_plugins] enabled list, with the plugin added when it isn't there. */
static std::string godot_enable(std::string text) {
  const std::string plugin = "\"res://addons/blendity_livelink/plugin.cfg\"";
  if (text.find(plugin) != std::string::npos) return text;
  const size_t sec = text.find("[editor_plugins]");
  if (sec == std::string::npos) return text + (text.empty() || text.back() == '\n' ? "" : "\n") + "\n[editor_plugins]\n\nenabled=PackedStringArray(" + plugin + ")\n";
  const size_t key = text.find("enabled=PackedStringArray(", sec);
  const size_t next = text.find("\n[", sec + 1);
  if (key == std::string::npos || (next != std::string::npos && key > next)) {
    if (text.find('\n', sec) == std::string::npos) text += "\n";  // (the section's line ends the file)
    return text.insert(text.find('\n', sec) + 1, "\nenabled=PackedStringArray(" + plugin + ")\n");
  }
  const size_t open = key + std::string("enabled=PackedStringArray(").size();
  const bool empty = text.compare(open, 1, ")") == 0;
  return text.insert(open, plugin + (empty ? "" : ", "));
}

bool Editor::install_bridge_godot(const std::string &project) {
  const std::string cfg = fs::join(project, "project.godot");
  std::string text;
  if (!fs::read_file(cfg, text)) return refuse("Install the Godot bridge: " + project + " isn't a Godot project (no project.godot)", "", ""), false;
  const std::string to = fs::join(project, "addons/blendity_livelink");
  int files = 0;
  if (!copy_tree(fs::join(bridges_dir(), "godot/addons/blendity_livelink"), to, files) || files == 0)
    return refuse("Install the Godot bridge: couldn't copy it into " + to, "", ""), false;
  if (!fs::write_file(cfg, godot_enable(text))) return refuse("Install the Godot bridge: copied, but project.godot couldn't be written - enable it in Project Settings > Plugins", "", ""), false;
  toast(LogLevel::Info, "The Godot bridge is in " + fs::filename(project) + "'s addons and enabled: the Blendity dock connects by itself");
  return true;
}

/* The .uproject's Plugins list with the plugin on (a text edit: the file keeps its own layout). */
static std::string unreal_enable(std::string text) {
  const size_t named = text.find("\"BlendityLiveLink\"");
  if (named != std::string::npos) {  // (listed: its "Enabled" in the same entry turned on)
    const size_t end = text.find('}', named), en = text.find("\"Enabled\"", named);
    const size_t off = en == std::string::npos || en > end ? std::string::npos : text.find("false", en);
    if (off != std::string::npos && off < end) text.replace(off, 5, "true");
    return text;
  }
  const std::string entry = "{\n\t\t\t\"Name\": \"BlendityLiveLink\",\n\t\t\t\"Enabled\": true\n\t\t}";
  const size_t key = text.find("\"Plugins\"");
  if (key != std::string::npos) {
    const size_t open = text.find('[', key);
    const size_t after = text.find_first_not_of(" \t\r\n", open + 1);
    const bool empty = after != std::string::npos && text[after] == ']';
    return text.insert(open + 1, "\n\t\t" + entry + (empty ? "\n\t" : ","));
  }
  const size_t close = text.rfind('}');
  if (close == std::string::npos) return text;
  size_t last = text.find_last_not_of(" \t\r\n", close - 1);
  const bool comma = last != std::string::npos && text[last] != '{';
  return text.insert(last + 1, std::string(comma ? "," : "") + "\n\t\"Plugins\": [\n\t\t" + entry + "\n\t]");
}

bool Editor::install_bridge_unreal(const std::string &project) {
  std::string uproject;
  for (const DirEntry &e : fs::list(project))
    if (!e.is_dir && fs::extension(e.name) == ".uproject") uproject = fs::join(project, e.name);
  std::string text;
  if (uproject.empty() || !fs::read_file(uproject, text)) return refuse("Install the Unreal bridge: " + project + " isn't an Unreal project (no .uproject)", "", ""), false;
  const std::string to = fs::join(project, "Plugins/BlendityLiveLink");
  int files = 0;
  if (!copy_tree(fs::join(bridges_dir(), "unreal/BlendityLiveLink"), to, files) || files == 0)
    return refuse("Install the Unreal bridge: couldn't copy it into " + to, "", ""), false;
  const std::string fixture = fs::join(bridges_dir(), "../tests/fixtures/livelink_stream.bin");
  if (fs::exists(fixture)) fs::make_dirs(fs::join(to, "Resources")), fs::copy_file(fixture, fs::join(to, "Resources/livelink_stream.bin"));
  if (!fs::write_file(uproject, unreal_enable(text))) return refuse("Install the Unreal bridge: copied, but the .uproject couldn't be written - enable it in Edit > Plugins", "", ""), false;
  toast(LogLevel::Info, "The Unreal bridge is in " + fs::filename(project) + "'s Plugins and enabled: Unreal builds it when the project opens (a C++ project; Window > Blendity Live Link)");
  return true;
}

/* livelink install blender | unity|godot|unreal <project folder> */
void Editor::livelink_install_command(const std::vector<std::string> &t) {
  const std::string what = t.size() > 2 ? to_lower(t[2]) : "";
  std::string folder;
  for (size_t k = 3; k < t.size(); k++) folder += (k > 3 ? " " : "") + t[k];
  if (bridges_dir().empty()) return refuse("Install a Bridge: the bridges folder isn't beside Blendity (reinstall Blendity, or set BLENDITY_BRIDGES_DIR)", "", "");
  if (what == "blender") return (void)install_bridge_blender();
  if (folder.empty()) return Log::warn("livelink install blender | unity|godot|unreal <project folder>");
  if (what == "unity") return (void)install_bridge_unity(folder);
  if (what == "godot") return (void)install_bridge_godot(folder);
  if (what == "unreal") return (void)install_bridge_unreal(folder);
  Log::warn("livelink install blender | unity|godot|unreal <project folder>");
}

/* The Live Link window's Install a Bridge row: Blender at once, the engines through a folder picker. */
void Editor::draw_bridge_install(ui::Layout &lay) {
  auto &u = ui_;
  u.label(lay.row(), "Install a Bridge", u.theme.text_bright);
  const Recti r = lay.row(u.row_h() + u.px(4));
  static const char *const kNames[] = {"Blender", "Unity Project...", "Godot Project...", "Unreal Project..."};
  static const char *const kIds[] = {"blender", "unity", "godot", "unreal"};
  const int w = (r.w - u.px(18)) / 4;
  for (int i = 0; i < 4; i++) {
    const Recti b{r.x + i * (w + u.px(6)), r.y, w, r.h};
    if (u.record_buttons) u.button_rects[std::string("livelink:install ") + kIds[i]] = b;
    if (!u.button(b, kNames[i])) continue;
    if (i == 0) {
      livelink_install_command({"livelink", "install", "blender"});
      continue;
    }
    const std::string id = kIds[i], title = std::string("Choose the ") + (kNames[i] + std::string()).substr(0, std::string(kNames[i]).find("...")) + " Folder";
    defer([this, id, title] {
      std::string folder;
      const PathPick res = pick_folder(title, project_root_, folder);
      if (res == PathPick::Chosen) livelink_install_command({"livelink", "install", id, folder});
      if (res == PathPick::Unavailable) picker_unavailable(title);
    });
  }
  u.tooltip("Blender: copied into its extensions and enabled. Unity, Godot, Unreal: choose the project's folder - the bridge goes into\n"
            "its Packages / addons / Plugins and is enabled. Each connects by itself while Live Link runs.");
}

}  // namespace bl
