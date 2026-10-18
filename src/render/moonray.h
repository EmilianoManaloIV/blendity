// SPDX-License-Identifier: GPL-2.0-or-later
// MoonRay as an external renderer (tasks 0111-0114, ADR 0041): DreamWorks' production path tracer (Apache-2.0), a
// separate program Blendity downloads on request and runs - never linked. On Windows it runs inside WSL2 (MoonRay
// builds on Linux and macOS only).
//
//   Blender                         here
//   ----------------------------    ------------------------------------------------
//   Render engine add-ons (Hydra)   RenderSettings::engine 2: the scene exported to MoonRay's RDLA,
//                                   `moonray -in scene.rdla -out frame.exr` run, the EXR read back
#pragma once
#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace bl {
namespace moonray {

/* The MoonRay release Blendity's archives are built from (.github/workflows/moonray.yml). */
constexpr const char *kTag = "v2026.29.1";

/* Where MoonRay can run on this machine, and what is installed there. */
struct Install {
  enum Kind { None = 0, Native = 1, Wsl = 2 } kind = None;
  std::string distro;    // WSL: the distribution (empty: WSL's default)
  std::string home;      // the user's home folder as MoonRay's system sees it
  std::string root;      // the install's folder, as MoonRay's system sees it ("" = not installed)
  std::string version;   // its VERSION file
  bool own_build = false;  // a folder the user set (their own MoonRay: scripts/setup.sh), not Blendity's archive
  std::vector<std::string> launcher;  // tests: a program standing in for MoonRay (run as it is, never through WSL)
  std::string problem;   // why MoonRay can't run here, in words ("" = it can, once installed)
  bool installable() const { return kind != None && problem.empty(); }
  bool ready() const { return installable() && !root.empty(); }
};

/* The archive for this machine ("blendity-moonray-linux-x64-<tag>.tar.gz"; Windows takes Linux's, in WSL), or "" when
 * there is none (an Intel Mac, Linux on ARM). */
std::string archive_name();
/* Finds where MoonRay runs (WSL on Windows) and an install: `folder` (the user's own MoonRay, a Preferences setting)
 * when set, else Blendity's under ~/.blendity/moonray/<tag>. Runs wsl.exe on Windows (a second or two): call it off
 * the main thread. */
Install locate(const std::string &folder = "");
/* The command that runs MoonRay with `args` (through wsl.exe on Windows). */
std::vector<std::string> command(const Install &in, const std::vector<std::string> &args);
/* A file of ours as MoonRay's system sees it: on Windows its WSL path (C:\a\b -> /mnt/c/a/b), elsewhere itself. */
std::string path_for(const Install &in, const std::string &path);
/* C:\Users\x\a b.rdla -> /mnt/c/Users/x/a b.rdla (a UNC or relative path is returned with its slashes turned). */
std::string wsl_path(const std::string &windows_path);
/* The distributions `wsl.exe -l -q` lists (it writes UTF-16; UTF-8 is read too), in its order. */
std::vector<std::string> parse_wsl_list(const std::string &output);
/* The last percentage in MoonRay's output ("[+] Rendering [=====    ] 45.2%"), 0..1, or -1 when there is none. */
float parse_progress(const std::string &text);
/* Unpacks a checked archive into ~/.blendity/moonray/<tag> (in WSL on Windows) and checks it renders its test scene;
 * `in` then names the install. On the downloading thread. */
bool unpack(Install &in, const std::string &archive, std::string &error);
/* Renders the archive's test scene (a few seconds): true when MoonRay ran and wrote the image. */
bool probe(const Install &in, std::string &error);
/* This Blendity's own scratch folder for MoonRay's scenes and images (another Blendity's are left alone). */
std::string scratch_dir();
/* Removes Blendity's install (never a folder the user set). */
bool remove(Install &in, std::string &error);

/* Runs MoonRay: `args` after the program; each piece of its output to `out` as it comes; stopped when `cancel` turns
 * true. Its exit code (sys::run_streaming's). */
int run(const Install &in, const std::vector<std::string> &args, const std::function<void(const std::string &)> &out,
        const std::atomic<bool> *cancel);

}  // namespace moonray
}  // namespace bl
