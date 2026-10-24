// SPDX-License-Identifier: GPL-2.0-or-later
// Blendity's builds as GitHub publishes them (task 0079): see releases.h.
#include "releases.h"

#include "core.h"
#include "json.h"

#include <algorithm>
#include <cctype>
#include <sstream>

/* The build scripts pass VERSION's text and BLENDITY_BUILD (CI's release tag) as bare tokens, made strings here (no
 * quotes to survive three shells). */
#define BL_STR2(x) #x
#define BL_STR(x) BL_STR2(x)
#ifdef BL_BUILD
#  define BL_BUILD_TAG BL_STR(BL_BUILD)
#else
#  define BL_BUILD_TAG "dev"
#endif
#ifdef BL_VERSION_NUMBER
#  define BL_VERSION BL_STR(BL_VERSION_NUMBER)
#else
#  define BL_VERSION "0.11.0"
#endif

namespace bl::releases {

const char *build_tag() { return BL_BUILD_TAG; }
const char *version() { return BL_VERSION; }

std::string build_label() {
  const std::string tag = build_tag();
  if (tag.rfind("build-pr", 0) == 0) return strprintf("%s (PR #%s)", version(), tag.c_str() + 8);
  if (tag == "dev") return strprintf("%s (local build)", version());
  return version();
}

const char *platform() {
#if defined(_WIN32)
  return "windows";
#elif defined(__APPLE__)
  return "macos";
#else
  return "linux";
#endif
}

static bool ends_with(const std::string &s, const char *tail) {
  const size_t n = std::strlen(tail);
  return s.size() >= n && s.compare(s.size() - n, n, tail) == 0;
}

static Release release_from(const Json &j) {
  Release r;
  r.tag = j["tag_name"].str;
  r.name = j["name"].str.empty() ? r.tag : j["name"].str;
  r.notes = j["body"].str;
  /* The release page's install guide (packaging/install-notes.md) is for people downloading by hand, not the
   * "what's new" an update shows. */
  const size_t guide = r.notes.find("<!-- install -->");
  if (guide != std::string::npos) {
    r.notes.erase(guide);
    while (!r.notes.empty() && std::isspace((unsigned char)r.notes.back())) r.notes.pop_back();
  }
  r.published = j["published_at"].str;
  r.prerelease = j["prerelease"].type == Json::Bool && j["prerelease"].b;
  if (r.tag.rfind("build-pr", 0) == 0) r.pr = std::atoi(r.tag.c_str() + 8);
  for (const Json &a : j["assets"].arr)
    r.assets.push_back({a["name"].str, a["browser_download_url"].str, (uint64_t)std::max(0.0, a["size"].number())});
  return r;
}

std::vector<Release> parse(const std::string &json) {
  JsonParser p{json.data(), json.data() + json.size()};
  const Json doc = p.value();
  std::vector<Release> out;
  if (!p.ok || doc.type != Json::Arr) return out;
  for (const Json &j : doc.arr) {
    const bool draft = j["draft"].type == Json::Bool && j["draft"].b;
    if (draft || j["tag_name"].str.empty()) continue;
    out.push_back(release_from(j));
  }
  std::stable_sort(out.begin(), out.end(), [](const Release &a, const Release &b) { return a.published > b.published; });
  return out;
}

const Asset *installer(const Release &r, const std::string &platform) {
  for (const Asset &a : r.assets) {
    const bool win = platform == "windows" && a.name.rfind("Blendity-Setup-", 0) == 0 && ends_with(a.name, ".exe");
    const bool lin = platform == "linux" && ends_with(a.name, ".AppImage");
    const bool mac = platform == "macos" && ends_with(a.name, ".dmg");
    if (win || lin || mac) return &a;
  }
  return nullptr;
}

const Asset *libraries(const Release &r, const Release *extras, const std::string &platform) {
  const std::string prefix = "blendity-libs-" + platform + "-";
  for (const Asset &a : r.assets)
    if (a.name.rfind(prefix, 0) == 0) return &a;
  if (!extras) return nullptr;
  const std::string mine = prefix + r.tag + ".";  // (".zip" / ".tar.gz": "build-pr7" never matches build-pr72's)
  for (const Asset &a : extras->assets)
    if (a.name.rfind(mine, 0) == 0) return &a;
  return nullptr;
}

const Asset *checksums(const Release &r) {
  for (const Asset &a : r.assets)
    if (a.name == "SHA256SUMS.txt") return &a;
  return nullptr;
}

std::string sum_for(const std::string &sums, const std::string &file) {
  std::istringstream in(sums);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.size() < 66 || line[64] != ' ') continue;
    const size_t name_at = line[65] == '*' || line[65] == ' ' ? 66 : 65;  // ("hash  name" or "hash *name")
    if (line.substr(name_at) != file) continue;
    std::string h = line.substr(0, 64);
    for (char &c : h) c = (char)std::tolower((unsigned char)c);
    return h;
  }
  return "";
}

/* A PR's build. A tagged version counts as released even while GitHub marks it a pre-release (0.x, task 0081). */
static bool offered(const Release &r, bool previews) { return previews || r.pr == 0; }

std::vector<const Release *> installable(const std::vector<Release> &all, const std::string &platform, bool previews) {
  std::vector<const Release *> out;
  for (const Release &r : all)
    if (offered(r, previews) && installer(r, platform)) out.push_back(&r);
  return out;
}

std::vector<const Release *> newer(const std::vector<Release> &all, const std::string &own, const std::string &platform, bool previews) {
  std::vector<const Release *> out;
  const auto mine = std::find_if(all.begin(), all.end(), [&](const Release &r) { return r.tag == own; });
  if (mine == all.end()) return out;
  for (auto it = all.begin(); it != mine; ++it)  // (newest first: everything before this build's own)
    if (offered(*it, previews) && installer(*it, platform)) out.push_back(&*it);
  return out;
}

}  // namespace bl::releases
