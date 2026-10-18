// SPDX-License-Identifier: GPL-2.0-or-later
// MoonRay as an external renderer: finding it, installing Blendity's archive, running it (tasks 0112, 0114, ADR 0041).
#include "moonray.h"
#include "../core/core.h"
#include <cctype>
#include <chrono>
#include <cstdlib>

namespace bl {
namespace moonray {

namespace {
#ifdef _WIN32
std::string wsl_exe() {
  const char *root = std::getenv("SystemRoot");
  return fs::join(fs::join(root ? root : "C:\\Windows", "System32"), "wsl.exe");  // (System32's, not one on a path)
}
/* wsl.exe [-d distro] -e args...: run in the distribution without a shell (arguments arrive as they are). */
std::vector<std::string> wsl_command(const Install &in, const std::vector<std::string> &args) {
  std::vector<std::string> cmd{wsl_exe()};
  if (!in.distro.empty()) cmd.insert(cmd.end(), {"-d", in.distro});
  cmd.push_back("-e");
  cmd.insert(cmd.end(), args.begin(), args.end());
  return cmd;
}
#endif

std::string trim(const std::string &s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace((unsigned char)s[a])) a++;
  while (b > a && std::isspace((unsigned char)s[b - 1])) b--;
  return s.substr(a, b - a);
}

/* A command run where MoonRay runs (WSL on Windows): its output, trimmed; "" when it failed. */
std::string ask(const Install &in, const std::vector<std::string> &args, int *code = nullptr) {
  std::string out;
#ifdef _WIN32
  const int c = sys::run_capture(wsl_command(in, args), out, 60);
#else
  (void)in;
  const int c = sys::run_capture(args, out, 60);
#endif
  if (code) *code = c;
  return c == 0 ? trim(out) : "";
}

/* Blendity's own install folder for kTag, as MoonRay's system sees it. */
std::string own_dir(const Install &in) {
  return in.home + "/.blendity/moonray/" + kTag;
}

/* Fills root / version from `dir` when it holds a usable MoonRay: Blendity's archive (bin/blendity-moonray) or the
 * user's own build (scripts/setup.sh). */
bool adopt(Install &in, const std::string &dir) {
  if (dir.empty()) return false;
  int code = 0;
  ask(in, {"test", "-x", dir + "/bin/blendity-moonray"}, &code);
  if (code == 0) {
    in.root = dir;
    in.own_build = false;
    in.version = ask(in, {"cat", dir + "/VERSION"});
    return true;
  }
  ask(in, {"test", "-f", dir + "/scripts/setup.sh"}, &code);
  if (code != 0) return false;
  in.root = dir;
  in.own_build = true;
  in.version = "own build";
  return true;
}
}  // namespace

std::string archive_name() {
#if defined(_WIN32) || (defined(__linux__) && (defined(__x86_64__) || defined(_M_X64)))
  return std::string("blendity-moonray-linux-x64-") + kTag + ".tar.gz";
#elif defined(__APPLE__) && defined(__aarch64__)
  return std::string("blendity-moonray-macos-arm64-") + kTag + ".tar.gz";
#else
  return "";
#endif
}

Install locate(const std::string &folder) {
  Install in;
#ifdef _WIN32
  in.kind = Install::Wsl;
  if (!fs::exists(wsl_exe())) {
    in.problem = "MoonRay runs in WSL on Windows, and WSL isn't installed (wsl --install, as an administrator)";
    return in;
  }
  std::string list;
  if (sys::run_capture({wsl_exe(), "-l", "-q"}, list, 30) != 0 || parse_wsl_list(list).empty()) {
    in.problem = "MoonRay runs in WSL on Windows, and WSL has no Linux distribution yet (wsl --install -d Ubuntu)";
    return in;
  }
  in.distro = parse_wsl_list(list)[0];  // (WSL lists its default first; installed into and run there, always the same one)
  const std::string arch = ask(in, {"uname", "-m"});
  if (arch != "x86_64") {
    in.problem = arch.empty() ? "WSL didn't answer (is the distribution set up? open it once from the Start menu)"
                              : "MoonRay's Linux build is for x86-64; WSL here runs " + arch;
    return in;
  }
  in.home = ask(in, {"sh", "-c", "echo $HOME"});
  if (in.home.empty()) {
    in.problem = "WSL didn't say where its home folder is";
    return in;
  }
#else
  in.kind = Install::Native;
  in.home = fs::home_dir();
  if (archive_name().empty()) in.problem = "there is no MoonRay build for this kind of computer (Linux x86-64 and Apple silicon Macs)";
#endif
  if (!folder.empty()) {
    adopt(in, folder);
    return in;
  }
  adopt(in, own_dir(in) + "/moonray");
  return in;
}

std::vector<std::string> command(const Install &in, const std::vector<std::string> &args) {
  if (!in.launcher.empty()) {
    std::vector<std::string> cmd = in.launcher;
    cmd.insert(cmd.end(), args.begin(), args.end());
    return cmd;
  }
  std::vector<std::string> cmd;
  if (in.own_build) {  // (the user's own MoonRay: its setup script sets the paths)
    cmd = {"sh", "-c", ". \"$0/scripts/setup.sh\" >/dev/null 2>&1; exec moonray \"$@\"", in.root};
  }
  else {
    cmd = {in.root + "/bin/blendity-moonray"};
  }
  cmd.insert(cmd.end(), args.begin(), args.end());
#ifdef _WIN32
  return wsl_command(in, cmd);
#else
  return cmd;
#endif
}

std::string path_for(const Install &in, const std::string &path) {
  return in.kind == Install::Wsl && in.launcher.empty() ? wsl_path(path) : path;
}

std::string wsl_path(const std::string &windows_path) {
  std::string p = windows_path;
  for (char &c : p)
    if (c == '\\') c = '/';
  if (p.size() >= 2 && std::isalpha((unsigned char)p[0]) && p[1] == ':')
    return "/mnt/" + std::string(1, (char)std::tolower((unsigned char)p[0])) + p.substr(2);
  return p;
}

std::vector<std::string> parse_wsl_list(const std::string &output) {
  /* UTF-16LE (wsl.exe's own output): every other byte, the names being ASCII. */
  size_t zeros = 0;
  for (size_t i = 1; i < output.size(); i += 2) zeros += output[i] == 0;
  std::string text;
  if (output.size() >= 2 && zeros * 2 >= output.size() / 2) {
    for (size_t i = 0; i + 1 < output.size(); i += 2)
      if (output[i + 1] == 0) text += output[i];
  }
  else {
    text = output;
  }
  if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) text.erase(0, 3);
  std::vector<std::string> names;
  size_t start = 0;
  while (start <= text.size()) {
    const size_t end = std::min(text.find('\n', start), text.size());
    std::string name = trim(text.substr(start, end - start));
    if (!name.empty() && (unsigned char)name[0] == 0xFF) name.clear();  // (a stray UTF-16 BOM byte)
    if (!name.empty()) names.push_back(name);
    start = end + 1;
  }
  return names;
}

float parse_progress(const std::string &text) {
  for (size_t at = text.rfind('%'); at != std::string::npos && at > 0; at = at == 0 ? std::string::npos : text.rfind('%', at - 1)) {
    size_t b = at;
    while (b > 0 && (std::isdigit((unsigned char)text[b - 1]) || text[b - 1] == '.')) b--;
    if (b == at) continue;
    const float v = (float)std::atof(text.substr(b, at - b).c_str());
    if (v >= 0.0f && v <= 100.0f) return v / 100.0f;
  }
  return -1.0f;
}

bool unpack(Install &in, const std::string &archive, std::string &error) {
  if (!in.installable()) return error = in.problem.empty() ? "MoonRay can't run on this system" : in.problem, false;
  const std::string dir = own_dir(in);
  int code = 0;
#ifdef _WIN32
  ask(in, {"sh", "-c", "rm -rf \"$1\" && mkdir -p \"$1\" && tar -xzf \"$2\" -C \"$1\"", "sh", dir, wsl_path(archive)}, &code);
#else
  fs::make_dirs(dir);
  code = sys::run_program({"tar", "-xzf", archive, "-C", dir});
#endif
  fs::remove_file(archive);
  if (code != 0) return error = strprintf("couldn't unpack MoonRay into %s (%d)", dir.c_str(), code), false;
  if (!adopt(in, dir + "/moonray")) return error = "unpacked into " + dir + ", but MoonRay isn't there", false;
  return probe(in, error);
}

std::string scratch_dir() {
  static const std::string dir = fs::join(fs::temp_dir(), strprintf("blendity-moonray-%llu",
                                          (unsigned long long)std::chrono::steady_clock::now().time_since_epoch().count() % 1000000007ull));
  return dir;
}

bool probe(const Install &in, std::string &error) {
  if (!in.ready()) return error = "MoonRay isn't installed", false;
  /* A scene of our own (a lit rectangle, MoonRay's test scene's), so the user's own build is checked the same way. */
  const std::string dir = scratch_dir();
  fs::make_dirs(dir);
  const std::string scene = fs::join(dir, "check.rdla"), out = fs::join(dir, "check.exr");
  fs::write_file(scene,
                 "SceneVariables {\n    [\"camera\"] = PerspectiveCamera(\"camera\"),\n    [\"image_width\"] = 64,\n    [\"image_height\"] = 64,\n}\n"
                 "RdlMeshGeometry(\"rectangle\") {\n    [\"vertices_by_index\"] = {0, 1, 2, 0, 2, 3},\n"
                 "    [\"vertex_list_0\"] = {Vec3(-1, 0, 0), Vec3(1, 0, 0), Vec3(1, 1, 0), Vec3(-1, 1, 0)},\n"
                 "    [\"face_vertex_count\"] = {3, 3},\n    [\"is_subd\"] = false,\n}\n"
                 "GeometrySet(\"geometrySet\") {\n    RdlMeshGeometry(\"rectangle\"),\n}\n"
                 "Layer(\"defaultLayer\") {\n    {RdlMeshGeometry(\"rectangle\"), \"\", DwaBaseMaterial(\"material\"), LightSet(\"lightSet\"), undef(), "
                 "undef(), undef(), undef()},\n}\n"
                 "DwaBaseMaterial(\"material\") {\n}\nEnvLight(\"light\") {\n}\nLightSet(\"lightSet\") {\n    EnvLight(\"light\"),\n}\n"
                 "PerspectiveCamera(\"camera\") {\n    [\"node_xform\"] = Mat4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0.5, 2.28, 1),\n"
                 "    [\"focal\"] = 15.15,\n}\n");
  fs::remove_file(out);
  std::string log;
  const int code = sys::run_streaming(command(in, {"-in", path_for(in, scene), "-out", path_for(in, out)}),
                                      [&](const std::string &s) { log += s; }, nullptr, 300);
  const bool ok = code == 0 && fs::exists(out);
  fs::remove_file(out);
  if (ok) return true;
  const std::string tail = log.size() > 400 ? log.substr(log.size() - 400) : log;
  error = strprintf("MoonRay didn't render its test scene (exit %d): %s", code, trim(tail).c_str());
  return false;
}

bool remove(Install &in, std::string &error) {
  if (in.own_build) return error = "that's your own MoonRay: Blendity only forgets it (clear the folder in Preferences)", false;
  if (in.root.empty()) return true;
  int code = 0;
  ask(in, {"rm", "-rf", own_dir(in)}, &code);
  if (code != 0) return error = strprintf("couldn't remove %s (%d)", own_dir(in).c_str(), code), false;
  in.root.clear();
  in.version.clear();
  return true;
}

int run(const Install &in, const std::vector<std::string> &args, const std::function<void(const std::string &)> &out,
        const std::atomic<bool> *cancel) {
  return sys::run_streaming(command(in, args), out, cancel, 0);
}

}  // namespace moonray
}  // namespace bl
