// SPDX-License-Identifier: GPL-2.0-or-later
// Downloads for the updater and the library fallback (tasks 0079-0080), with no new library: Windows' own WinHTTP,
// and the `curl` program every Linux distribution and macOS ship elsewhere. And SHA-256, to check what came.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

namespace bl::net {

/* An HTTPS GET of a small reply (a releases list): true with the body; false with a reason (offline, refused, an HTTP
 * error). `timeout_s` caps the whole request. Blocking: call it off the main thread. */
bool get(const std::string &url, std::string &body, std::string *error = nullptr, int timeout_s = 15);

/* A download into `path` (written as `path`.part, renamed when complete). `progress(done, total)` is called from the
 * downloading thread (total 0 when unknown); `cancel` stops it. Blocking: call it off the main thread. */
bool download(const std::string &url, const std::string &path, std::string *error = nullptr,
              const std::function<void(uint64_t, uint64_t)> &progress = {}, const std::atomic<bool> *cancel = nullptr);

/* ~/.blendity/downloads: where updates and libraries download to (made, and private to the user, on first use). */
std::string downloads_dir();
/* An asset's name as a file name in that folder, or "" when it isn't a plain file name (a path, "..", NUL). */
std::string safe_file_name(const std::string &name);

/* The file's SHA-256 as 64 lower-case hex digits ("" if unreadable), and a string's. */
std::string sha256_file(const std::string &path);
std::string sha256(const std::string &data);

}  // namespace bl::net
