// SPDX-License-Identifier: GPL-2.0-or-later
// Blendity's builds as GitHub publishes them (task 0079): every merged PR's pre-release (`build-pr<N>`) and the v*
// stable releases, each with installers for the three systems, the libraries for the fallback (task 0080) and a
// SHA256SUMS.txt. Pure functions over the releases list, so the updater's choices are testable without a network.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace bl::releases {

/* This build: its tag ("build-pr72", "v0.10.0", or "dev" for a local build), version and a label for people. */
const char *build_tag();
const char *version();
std::string build_label();  // "0.10.0 (PR #72)", "0.10.0", "0.10.0 (local build)"
/* "windows", "linux" or "macos": the installers this build can install. */
const char *platform();

struct Asset {
  std::string name, url;
  uint64_t size = 0;
};

struct Release {
  std::string tag, name, notes, published;  // published: ISO 8601, sorts as text
  bool prerelease = false;
  int pr = 0;  // the PR a build-pr<N> release was made from
  std::vector<Asset> assets;
};

/* GitHub's /releases reply, newest first; drafts left out. Empty when it isn't one. */
std::vector<Release> parse(const std::string &json);
/* The installer this system runs ("Blendity-Setup-*.exe", "*.AppImage", "*.dmg"), or null. */
const Asset *installer(const Release &r, const std::string &platform);
/* The libraries archive for the fallback (blendity-libs-<platform>-*), or null. */
const Asset *libraries(const Release &r, const std::string &platform);
/* SHA256SUMS.txt's asset, or null. */
const Asset *checksums(const Release &r);
/* The lower-case SHA-256 a SHA256SUMS.txt (sha256sum's format) gives for `file`, or "". */
std::string sum_for(const std::string &sums, const std::string &file);
/* The releases newer than the one tagged `own` that this system can install, newest first (only stable ones unless
 * `previews`). A build not in the list (a local one) has none newer: it offers nothing by itself. */
std::vector<const Release *> newer(const std::vector<Release> &all, const std::string &own, const std::string &platform, bool previews);
/* Every release this system can install, newest first (Version History). */
std::vector<const Release *> installable(const std::vector<Release> &all, const std::string &platform, bool previews);

}  // namespace bl::releases
