// SPDX-License-Identifier: GPL-2.0-or-later
// Downloads and SHA-256 (tasks 0079-0080): see net.h.
#include "net.h"

#include "core.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <winhttp.h>
#else
#  include <cerrno>
#  include <csignal>
#  include <spawn.h>
#  include <sys/stat.h>
#  include <sys/wait.h>
#  include <unistd.h>
extern char **environ;
#endif

namespace bl::net {

std::string safe_file_name(const std::string &name) {
  const bool bad = name.empty() || name == "." || name == ".." || name.find_first_of(std::string("/\\:\0", 4)) != std::string::npos;
  return bad || name.find("..") != std::string::npos ? std::string() : name;
}

std::string downloads_dir() {
  const std::string d = fs::join(fs::join(fs::home_dir(), ".blendity"), "downloads");
  fs::make_dirs(d);
#ifndef _WIN32
  chmod(d.c_str(), 0700);  // (the user's own: nobody else plants files in it)
#endif
  return d;
}

/* ---------------------------------------------------------------- SHA-256 (FIPS 180-4) */

namespace {

struct Sha256 {
  uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  uint8_t block[64] = {};
  size_t used = 0;
  uint64_t bits = 0;

  static uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

  void compress() {
    static const uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be,
        0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa,
        0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85,
        0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
        0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
        0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    uint32_t w[64];
    for (int i = 0; i < 16; i++) w[i] = (uint32_t)block[i * 4] << 24 | (uint32_t)block[i * 4 + 1] << 16 | (uint32_t)block[i * 4 + 2] << 8 | block[i * 4 + 3];
    for (int i = 16; i < 64; i++) {
      const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
      const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; i++) {
      const uint32_t t1 = hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
      const uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
      hh = g, g = f, f = e, e = d + t1, d = c, c = b, b = a, a = t1 + t2;
    }
    h[0] += a, h[1] += b, h[2] += c, h[3] += d, h[4] += e, h[5] += f, h[6] += g, h[7] += hh;
  }

  void add(const uint8_t *p, size_t n) {
    bits += (uint64_t)n * 8;
    for (size_t i = 0; i < n; i++) {
      block[used++] = p[i];
      if (used == 64) compress(), used = 0;
    }
  }

  std::string hex() {
    const uint64_t total = bits;
    const uint8_t one = 0x80, zero = 0;
    add(&one, 1);
    while (used != 56) add(&zero, 1);
    for (int i = 7; i >= 0; i--) {
      const uint8_t byte = (uint8_t)(total >> (i * 8));
      add(&byte, 1);
    }
    std::string out;
    for (uint32_t v : h) out += strprintf("%08x", v);
    return out;
  }
};

}  // namespace

std::string sha256(const std::string &data) {
  Sha256 s;
  s.add((const uint8_t *)data.data(), data.size());
  return s.hex();
}

static FILE *open_file(const std::string &path, const char *mode);

/* Streamed in 1 MB pieces: a libraries archive of hundreds of MB never sits in memory whole. */
std::string sha256_file(const std::string &path) {
  FILE *f = open_file(path, "rb");
  if (!f) return "";
  Sha256 s;
  std::vector<uint8_t> buf(1 << 20);
  size_t n;
  while ((n = std::fread(buf.data(), 1, buf.size(), f)) > 0) s.add(buf.data(), n);
  std::fclose(f);
  return s.hex();
}

static bool fail(std::string *error, const std::string &why) {
  if (error) *error = why;
  return false;
}

/* Only https: a release list or file from anywhere else is refused before anything is asked. */
static bool https_only(const std::string &url, std::string *error) {
  return url.rfind("https://", 0) == 0 ? true : fail(error, "not an https address: " + url);
}

/* ---------------------------------------------------------------- Windows: WinHTTP */

#ifdef _WIN32

static std::wstring wide(const std::string &s) {
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
  std::wstring w(n > 0 ? n - 1 : 0, L'\0');
  if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
  return w;
}

static FILE *open_file(const std::string &path, const char *mode) { return _wfopen(wide(path).c_str(), wide(mode).c_str()); }

/* One request's handles, closed in any order it stopped at. */
struct Request {
  HINTERNET session = nullptr, connect = nullptr, request = nullptr;
  ~Request() {
    if (request) WinHttpCloseHandle(request);
    if (connect) WinHttpCloseHandle(connect);
    if (session) WinHttpCloseHandle(session);
  }
};

/* A GET sent and answered (redirects followed, as GitHub's release assets need): the HTTP status, or 0 with `error`. */
static int open_request(Request &r, const std::string &url, int timeout_s, std::string *error) {
  std::wstring w = wide(url);
  URL_COMPONENTS uc{};
  uc.dwStructSize = sizeof(uc);
  wchar_t host[256] = {}, path[4096] = {};
  uc.lpszHostName = host, uc.dwHostNameLength = 256;
  uc.lpszUrlPath = path, uc.dwUrlPathLength = 4096;
  if (!WinHttpCrackUrl(w.c_str(), 0, 0, &uc)) return fail(error, "not a web address: " + url), 0;
  r.session = WinHttpOpen(L"Blendity", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!r.session) return fail(error, "no internet access (WinHTTP)"), 0;
  const int ms = timeout_s * 1000;
  WinHttpSetTimeouts(r.session, ms, ms, ms, ms);
  r.connect = WinHttpConnect(r.session, host, uc.nPort, 0);
  const DWORD flags = uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
  r.request = r.connect ? WinHttpOpenRequest(r.connect, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags) : nullptr;
  if (!r.request) return fail(error, "couldn't reach " + url), 0;
  const wchar_t *headers = L"Accept: application/vnd.github+json, application/octet-stream\r\n";
  if (!WinHttpSendRequest(r.request, headers, (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(r.request, nullptr))
    return fail(error, strprintf("offline, or no answer from %ls (error %lu)", host, GetLastError())), 0;
  DWORD status = 0, size = sizeof(status);
  WinHttpQueryHeaders(r.request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
  if (status >= 300) return fail(error, strprintf("HTTP %lu from %s", status, url.c_str())), 0;  // (a redirect not followed: no file)
  return (int)status;
}

static uint64_t content_length(Request &r) {
  wchar_t buf[32] = {};
  DWORD size = sizeof(buf);
  if (!WinHttpQueryHeaders(r.request, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, buf, &size, WINHTTP_NO_HEADER_INDEX)) return 0;
  return (uint64_t)_wtoi64(buf);
}

/* The body, chunk by chunk, to `sink` (false from it stops). */
static bool read_body(Request &r, const std::function<bool(const char *, size_t)> &sink, std::string *error) {
  std::vector<char> buf(1 << 16);
  while (true) {
    DWORD got = 0;
    if (!WinHttpReadData(r.request, buf.data(), (DWORD)buf.size(), &got)) return fail(error, "the connection dropped");
    if (got == 0) return true;
    if (!sink(buf.data(), got)) return false;
  }
}

bool get(const std::string &url, std::string &body, std::string *error, int timeout_s) {
  Request r;
  if (!https_only(url, error) || !open_request(r, url, timeout_s, error)) return false;
  body.clear();
  return read_body(r, [&](const char *p, size_t n) { return body.append(p, n), true; }, error);
}

bool download(const std::string &url, const std::string &path, std::string *error, const std::function<void(uint64_t, uint64_t)> &progress,
              const std::atomic<bool> *cancel) {
  Request r;
  if (!https_only(url, error) || !open_request(r, url, 60, error)) return false;
  const uint64_t total = content_length(r);
  const std::string part = path + ".part";
  FILE *f = open_file(part, "wb");
  if (!f) return fail(error, "can't write " + part);
  uint64_t done = 0;
  const bool ok = read_body(r, [&](const char *p, size_t n) {
    if (cancel && cancel->load()) return fail(error, "cancelled");
    if (std::fwrite(p, 1, n, f) != n) return fail(error, "the disk is full, or " + part + " can't be written");
    done += n;
    if (progress) progress(done, total);
    return true;
  }, error);
  std::fclose(f);
  if (!ok || (total && done != total)) {
    fs::remove_file(part);
    return ok ? fail(error, "the download stopped short") : false;
  }
  fs::remove_file(path);
  return fs::move(part, path) ? true : fail(error, "can't rename " + part);
}

#else

/* ---------------------------------------------------------------- Linux, macOS: the curl program */

static FILE *open_file(const std::string &path, const char *mode) { return std::fopen(path.c_str(), mode); }

static uint64_t file_size(const std::string &path) {
  struct stat st {};
  return stat(path.c_str(), &st) == 0 ? (uint64_t)st.st_size : 0;
}

/* curl into `out`, polled for progress and cancel; true when it exited 0. https only, redirects too; `timeout_s` 0:
 * no limit on the whole (a large download on a slow line), only on a stall (under 1 KB/s for a minute). */
static bool run_curl(const std::string &url, const std::string &out, int timeout_s, std::string *error,
                     const std::function<void(uint64_t, uint64_t)> &progress, const std::atomic<bool> *cancel) {
  if (!https_only(url, error)) return false;
  std::vector<std::string> a = {"curl", "-fsSL", "--proto", "=https", "--proto-redir", "=https", "--speed-limit", "1024", "--speed-time", "60",
                                "-A", "Blendity", "-H", "Accept: application/vnd.github+json, application/octet-stream", "-o", out};
  if (timeout_s > 0) a.insert(a.end(), {"--max-time", std::to_string(timeout_s)});
  a.push_back(url);  // (https://..., checked above: never read as an option)
  std::vector<char *> argv;
  for (std::string &s : a) argv.push_back(&s[0]);
  argv.push_back(nullptr);
  pid_t pid;
  if (posix_spawnp(&pid, "curl", nullptr, nullptr, argv.data(), environ) != 0) return fail(error, "no curl program to download with");
  int status = 0;
  while (true) {
    const pid_t r = waitpid(pid, &status, WNOHANG);
    if (r == pid) break;
    if (r < 0 && errno != EINTR) return fail(error, "lost the curl program");  // (SIGCHLD ignored: it reaped itself)
    if (cancel && cancel->load()) {
      kill(pid, SIGTERM);
      waitpid(pid, &status, 0);
      return fail(error, "cancelled");
    }
    if (progress) progress(file_size(out), 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  if (WIFEXITED(status) && WEXITSTATUS(status) == 0) return true;
  const int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  return fail(error, code == 6 || code == 7 || code == 28 ? "offline, or no answer from " + url : strprintf("curl failed (exit %d) for %s", code, url.c_str()));
}

/* A file of its own in the user's private downloads folder (never a guessable name in the shared /tmp). */
bool get(const std::string &url, std::string &body, std::string *error, int timeout_s) {
  std::string tmpl = fs::join(downloads_dir(), "get-XXXXXX");
  const int fd = mkstemp(&tmpl[0]);
  if (fd < 0) return fail(error, "can't make a file in " + downloads_dir());
  close(fd);
  const bool ok = run_curl(url, tmpl, timeout_s, error, {}, nullptr) && fs::read_file(tmpl, body);
  fs::remove_file(tmpl);
  return ok;
}

bool download(const std::string &url, const std::string &path, std::string *error, const std::function<void(uint64_t, uint64_t)> &progress,
              const std::atomic<bool> *cancel) {
  const std::string part = path + ".part";
  if (!run_curl(url, part, 0, error, progress, cancel)) {
    fs::remove_file(part);
    return false;
  }
  fs::remove_file(path);
  return fs::move(part, path) ? true : fail(error, "can't rename " + part);
}

#endif

}  // namespace bl::net
