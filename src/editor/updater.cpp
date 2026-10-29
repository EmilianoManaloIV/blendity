// SPDX-License-Identifier: GPL-2.0-or-later
// Updates (task 0079, ADR 0033): Blendity asks GitHub's releases list which builds exist (the v* releases, and any
// PR build published as a preview), offers a newer one with its notes, and installs it - or any older one from Help >
// Version History - after checking its SHA-256. The network work runs on background threads; the dialogs only read
// what they left.
#include "editor.h"

#include "../core/launch.h"
#include "../core/net.h"
#include "../core/releases.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <thread>

namespace bl {

static const char *kReleases = "https://api.github.com/repos/EmilianoManaloIV/blendity/releases";

/* Asks for the releases list in the background - the newest 100, and this build's own release when it is older than
 * those (so a build far behind still sees what came after it). `manual`: Help > Check for Updates (says "up to date"
 * too); `updates` false: only this build's libraries are wanted (task 0080). */
void Editor::start_update_check(bool manual, bool updates) {
  upd_.want_updates = upd_.want_updates || updates;
  upd_.manual = upd_.manual || manual;
  if (upd_.check && upd_.check->state.load() == UpdateCheck::Running) return;
  auto job = std::make_shared<UpdateCheck>();
  upd_.check = job;
  if (updates) update_last_check_ = (double)std::time(nullptr);
  if (!headless_) save_prefs();
  const std::string own = releases::build_tag();
  std::thread([job, own] {
    std::string body, error;
    const bool ok = net::get(std::string(kReleases) + "?per_page=100", body, &error);
    const bool listed = body.find("\"tag_name\":\"" + own + "\"") != std::string::npos;
    if (ok && own != "dev" && !listed) net::get(std::string(kReleases) + "/tags/" + own, job->own, nullptr);
    if (ok) net::get(std::string(kReleases) + "/tags/" + releases::kExtrasTag, job->extras, nullptr);  // (libraries)
    job->body = std::move(body);
    job->error = std::move(error);
    job->state.store(ok ? UpdateCheck::Done : UpdateCheck::Failed);
  }).detach();
}

/* Each frame: a finished check becomes the list, and a newer build the Update dialog (unless skipped or quiet). */
void Editor::poll_updates() {
  poll_update_download();
  if (upd_.offer_waiting && dialog_ == Dialog::None) {  // (task 0261: the welcome has closed)
    upd_.offer_waiting = false;
    offer_after_check(upd_.waiting_manual, upd_.waiting_updates);
  }
  if (!upd_.check) return;
  const int st = upd_.check->state.load();
  if (st == UpdateCheck::Running) return;
  const std::shared_ptr<UpdateCheck> job = std::move(upd_.check);
  const bool manual = upd_.manual, updates = upd_.want_updates;
  upd_.manual = upd_.want_updates = false;
  if (st == UpdateCheck::Failed) {
    (manual ? Log::warn : Log::info)("Updates: couldn't check (%s)", job->error.c_str());
    return;
  }
  upd_.list = releases::parse(job->body);
  const std::vector<releases::Release> own = releases::parse("[" + job->own + "]");
  if (!own.empty() && !update_release(own[0].tag)) upd_.list.push_back(own[0]);  // (older than the 100 listed: last)
  upd_.extras = job->extras.empty() ? std::vector<releases::Release>() : releases::parse("[" + job->extras + "]");
  const auto newer = releases::newer(upd_.list, releases::build_tag(), releases::platform(), update_previews_);
  upd_.offer = newer.empty() ? "" : newer.front()->tag;
  if (dialog_ == Dialog::History) return;  // (Version History was waiting for the list: it shows it now)
  if (dialog_ != Dialog::None) {  // (task 0261: the welcome, or any open question, first; the offer waits for it to close)
    upd_.offer_waiting = true, upd_.waiting_manual = manual, upd_.waiting_updates = updates;
    return;
  }
  offer_after_check(manual, updates);
}

/* The questions a finished check may ask, one at a time: MoonRay, AI Inbetweening, the libraries, then a newer build. */
void Editor::offer_after_check(bool manual, bool updates) {
  if (upd_.want_moonray) {  // (task 0112: the MoonRay question, asked through the same check)
    upd_.want_moonray = false;
    if (offer_moonray()) return;  // (an update is offered by the next check: one question at a time)
  }
  if (upd_.want_ai) {  // (task 0129: the AI download, asked through the same check)
    upd_.want_ai = false;
    if (offer_ai()) return;
  }
  const bool libs_shown = upd_.want_libs && offer_libraries();
  if (libs_shown || !updates) return;  // (a libraries-only check never offers an update the user turned off)
  if (upd_.offer.empty()) {
    if (manual) Log::info("Updates: Blendity %s is the newest build for this system", releases::build_label().c_str());
    return;
  }
  if (!manual && upd_.offer == update_skip_) return;
  upd_.selected = upd_.offer;
  dialog_ = Dialog::Update;
}

/* The release holding the libraries archives, or null (not fetched, or none on GitHub). */
const releases::Release *Editor::extras_release() const { return upd_.extras.empty() ? nullptr : &upd_.extras[0]; }

const releases::Release *Editor::update_release(const std::string &tag) const {
  for (const releases::Release &r : upd_.list)
    if (r.tag == tag) return &r;
  return nullptr;
}

/* The release's notes, wrapped to `width`, paragraph by paragraph (Markdown kept as text). */
static std::vector<std::string> wrap_notes(Font &f, const std::string &notes, int width) {
  std::vector<std::string> out;
  size_t at = 0;
  while (at <= notes.size()) {
    size_t nl = notes.find('\n', at);
    if (nl == std::string::npos) nl = notes.size();
    std::string para = notes.substr(at, nl - at);
    if (!para.empty() && para.back() == '\r') para.pop_back();
    at = nl + 1;
    if (para.empty()) {
      out.push_back("");
      continue;
    }
    std::string cur;
    for (const std::string &w : split_ws(para)) {
      const std::string t = cur.empty() ? w : cur + " " + w;
      if (f.text_width(t) > width && !cur.empty()) out.push_back(cur), cur = w;
      else cur = t;
    }
    out.push_back(cur);
  }
  return out;
}

/* The notes in a scrolling box `h` high. */
void Editor::draw_release_notes(const releases::Release &r, int h) {
  auto &u = ui_;
  Recti box = u.popup_row(h);
  box = {box.x + u.px(10), box.y, box.w - u.px(20), box.h};
  u.frame(box, u.theme.field, u.theme.field_border, u.px(3));
  const std::vector<std::string> lines = wrap_notes(u.font, r.notes.empty() ? "(no notes)" : r.notes, box.w - u.px(24));
  const int lh = u.font.line_height();
  static int content = 0;
  const int off = u.begin_scroll(u.id("update_notes"), box, content);
  int y = box.y + u.px(6) - off;
  for (const std::string &l : lines) {
    if (y + lh > box.y && y < box.bottom()) u.canvas.text(u.font, box.x + u.px(8), y, l, u.theme.text);
    y += lh;
  }
  content = (int)lines.size() * lh + u.px(12);
  u.end_scroll();
}

/* Download progress and Cancel, while an install is under way; true then (the dialog shows only this). */
bool Editor::draw_update_progress() {
  auto &u = ui_;
  if (!upd_.download) return false;
  const UpdateDownload &d = *upd_.download;
  const uint64_t done = d.done.load(), total = d.total.load();
  Recti r = u.popup_row(u.row_h() + u.px(8));
  const std::string what = d.state.load() == UpdateDownload::Verifying ? "Checking the download..." :
                           total ? strprintf("Downloading %s: %.1f of %.1f MB", d.file.c_str(), done / 1048576.0, total / 1048576.0) :
                                   strprintf("Downloading %s: %.1f MB", d.file.c_str(), done / 1048576.0);
  u.label({r.x + u.px(12), r.y, r.w - u.px(120), r.h}, what);
  if (u.button({r.right() - u.px(100), r.y + u.px(4), u.px(88), r.h - u.px(8)}, "Cancel")) upd_.download->cancel.store(true);
  Recti bar = u.popup_row(u.px(8));
  bar = {bar.x + u.px(12), bar.y, bar.w - u.px(24), bar.h};
  u.canvas.fill_round_rect(bar, u.px(3), u.theme.well);
  if (total) u.canvas.fill_round_rect({bar.x, bar.y, (int)(bar.w * std::min(1.0, done / (double)total)), bar.h}, u.px(3), u.theme.accent);
  u.redraw = true;
  return true;
}

/* "Blendity <x> is available": its notes, Install / Not now / Skip this version. */
void Editor::draw_update_dialog(const std::function<void()> &close) {
  auto &u = ui_;
  const releases::Release *r = update_release(upd_.selected);
  if (!r) return close();
  Recti head = u.popup_row();
  u.label({head.x + u.px(12), head.y, head.w - u.px(20), head.h}, "This build: " + releases::build_label() + ".  What the new one brings:", u.theme.text_dim);
  draw_release_notes(*r, u.px(300));
  if (draw_update_progress()) return;
  Recti b = u.popup_row(u.row_h() + u.px(10));
  if (u.button({b.right() - u.px(330), b.y + u.px(4), u.px(110), b.h - u.px(8)}, "Install")) install_release(r->tag);
  u.tooltip("Downloads it, checks it, and installs it; Blendity saves the scene and restarts.");
  if (u.button({b.right() - u.px(212), b.y + u.px(4), u.px(90), b.h - u.px(8)}, "Not now")) return close();
  if (!u.button({b.right() - u.px(114), b.y + u.px(4), u.px(102), b.h - u.px(8)}, "Skip this one")) return;
  update_skip_ = r->tag;
  save_prefs();
  close();
}

/* Help > Version History: every build for this system, newest first, with its notes; any can be installed. */
void Editor::draw_history_dialog(const std::function<void()> &close) {
  auto &u = ui_;
  if (upd_.list.empty()) {
    Recti r = u.popup_row();
    u.label({r.x + u.px(12), r.y, r.w, r.h}, upd_.check ? "Asking GitHub for the builds..." : "No builds found (offline?). Help > Check for Updates tries again.", u.theme.text_dim);
    u.redraw = upd_.check != nullptr;
    Recti b = u.popup_row(u.row_h() + u.px(10));
    if (u.button({b.right() - u.px(92), b.y + u.px(4), u.px(80), b.h - u.px(8)}, "Close")) close();
    return;
  }
  const auto builds = releases::installable(upd_.list, releases::platform(), true);
  if (upd_.selected.empty() && !builds.empty()) upd_.selected = builds.front()->tag;
  /* Every build, newest first, in a scrolling list (Version History: "before PR #N" is the one just below it). */
  Recti list = u.popup_row(u.px(170));
  list = {list.x + u.px(10), list.y, list.w - u.px(20), list.h};
  u.frame(list, u.theme.field, u.theme.field_border, u.px(3));
  static int content = 0;
  const int off = u.begin_scroll(u.id("update_builds"), list, content);
  const int rh = u.row_h();
  int y = list.y + u.px(2) - off;
  for (const releases::Release *r : builds) {
    const Recti row{list.x + u.px(2), y, list.w - u.px(16), rh};
    y += rh;
    if (row.bottom() < list.y || row.y > list.bottom()) continue;
    const bool mine = r->tag == releases::build_tag(), sel = r->tag == upd_.selected;
    if (sel) u.canvas.fill_rect(row, u.theme.selection);
    if (u.hovered(row) && u.in.pressed[0]) upd_.selected = r->tag;
    const std::string label = r->name + "   " + r->published.substr(0, 10) + (r->pr ? "   preview" : r->prerelease || r->tag.rfind("v0.", 0) == 0 ? "   pre-release" : "   stable") + (mine ? "   (this build)" : "");
    u.label({row.x + u.px(6), row.y, row.w - u.px(8), row.h}, label, mine ? u.theme.text_bright : u.theme.text);
  }
  content = (int)builds.size() * rh + u.px(4);
  u.end_scroll();
  const releases::Release *sel = update_release(upd_.selected);
  if (sel) draw_release_notes(*sel, u.px(200));
  if (draw_update_progress()) return;
  Recti b = u.popup_row(u.row_h() + u.px(10));
  const bool mine = sel && sel->tag == releases::build_tag();
  if (sel && !mine && u.button({b.right() - u.px(260), b.y + u.px(4), u.px(160), b.h - u.px(8)}, "Install this version")) install_release(sel->tag);
  u.tooltip("Older builds install the same way (before a PR: pick the build just below it). Your scenes stay as they are.");
  if (u.button({b.right() - u.px(92), b.y + u.px(4), u.px(80), b.h - u.px(8)}, "Close")) close();
}

/* Downloads the release's installer and the checksums in the background, then installs (poll_update_download). */
void Editor::install_release(const std::string &tag) {
  const releases::Release *r = update_release(tag);
  const releases::Asset *a = r ? releases::installer(*r, releases::platform()) : nullptr;
  const releases::Asset *sums = r ? releases::checksums(*r) : nullptr;
  if (!a || !sums) return Log::warn("Updates: %s has no installer for this system, or no checksums: not installed", tag.c_str());
  download_verified(*a, *sums, false);
}

/* `a` downloaded and checked against the release's SHA256SUMS.txt in the background; then installed, or unpacked as
 * the libraries (`libraries`), by poll_update_download. */
void Editor::download_verified(const releases::Asset &a, const releases::Asset &sums, bool libraries, bool moonray, bool ai) {
  auto job = std::make_shared<UpdateDownload>();
  job->libraries = libraries;
  job->moonray = moonray;
  job->ai = ai;
  job->mr = mr_;
  job->file = net::safe_file_name(a.name);
  if (job->file.empty()) return Log::warn("Updates: '%s' isn't a plain file name: not downloaded", a.name.c_str());
  job->path = fs::join(net::downloads_dir(), job->file);
  job->total.store(a.size);
  upd_.download = job;
  const std::string url = a.url, sums_url = sums.url;
  std::thread([job, url, sums_url] {
    std::string text, error;
    bool ok = net::get(sums_url, text, &error) &&
              net::download(url, job->path, &error, [job](uint64_t d, uint64_t t) { job->done.store(d), job->total.store(t ? t : job->total.load()); }, &job->cancel);
    job->state.store(UpdateDownload::Verifying);
    const std::string want = ok ? releases::sum_for(text, job->file) : "";
    if (ok && (want.empty() || net::sha256_file(job->path) != want)) {
      ok = false;
      error = want.empty() ? "the checksums don't list " + job->file : job->file + " doesn't match its checksum";
      fs::remove_file(job->path);
    }
    if (ok) ok = prepare_install(*job, error);  // (unpacking, copying: here, not on the editor's thread)
    job->error = error;
    job->state.store(ok ? UpdateDownload::Done : UpdateDownload::Failed);
  }).detach();
}

/* A finished download: run the installer and close (Windows), replace the AppImage and restart (Linux), open the disk
 * image (macOS); the release page where Blendity can't install itself (a .tar.gz unpacked by hand). */
void Editor::poll_update_download() {
  if (!upd_.download) return;
  const int st = upd_.download->state.load();
  if (st == UpdateDownload::Running || st == UpdateDownload::Verifying) return;
  const std::shared_ptr<UpdateDownload> job = std::move(upd_.download);
  if (st == UpdateDownload::Failed) return Log::error("Updates: not installed - %s", job->error.c_str());
  Log::info("Updates: %s downloaded and checked", job->file.c_str());
  if (job->libraries) {
    Log::info("Libraries: installed in %s; starting Blendity again with them", launch::user_libs_dir().c_str());
    return restart_program();
  }
  if (job->moonray) {  // (task 0112: unpacked and its test scene rendered on the downloading thread)
    mr_ = job->mr;
    mr_located_ = true;
    mr_installed_ = true;
    save_prefs();
    if (dialog_ == Dialog::MoonRay) dialog_ = Dialog::None;
    return Log::info("MoonRay %s is installed: Render Settings > Render Engine > MoonRay", mr_.version.c_str());
  }
  if (job->ai) {  // (task 0129: unpacked and a test inference run on the downloading thread)
    ai_.rt.reset();
    if (dialog_ == Dialog::Ai) dialog_ = Dialog::None;
    return Log::info("AI Inbetweening is installed: the Animation window's AI Inbetween");
  }
  run_installer(job->path);
}

void Editor::run_installer(const std::string &path) {
#if defined(_WIN32)
  /* Inno Setup installs where this copy is (for this user, or for everyone when it is in Program Files), closes
   * Blendity if it is still open, and starts it again ([Run], after a silent install). */
  const char *local = std::getenv("LOCALAPPDATA");
  const bool per_user = local && to_lower(fs::executable_dir()).rfind(to_lower(fs::normalize(local)), 0) == 0;
  if (!sys::start_program({path, "/SILENT", "/SUPPRESSMSGBOXES", per_user ? "/CURRENTUSER" : "/ALLUSERS", "/CLOSEAPPLICATIONS", "/RESTARTBLENDITY"}))
    return Log::error("Updates: couldn't start %s", path.c_str());
  request_close();
#elif defined(__APPLE__)
  fs::open_external(path);  // (the disk image opens: drag Blendity onto Applications)
  Log::info("Updates: drag Blendity from the opened disk image onto Applications, then start it again");
#else
  const char *appimage = std::getenv("APPIMAGE");
  if (!appimage || !*appimage) {
    fs::open_external("https://github.com/EmilianoManaloIV/blendity/releases");
    return Log::info("Updates: this copy wasn't installed from the AppImage; the downloaded one is %s", path.c_str());
  }
  /* prepare_install put the new one beside the old as <AppImage>.new: two renames in one folder, nothing lost midway. */
  const std::string target = appimage;
  fs::remove_file(target + ".old");
  if (!fs::move(target, target + ".old")) return Log::error("Updates: couldn't replace %s", target.c_str());
  if (!fs::move(target + ".new", target)) {
    fs::move(target + ".old", target);
    return Log::error("Updates: couldn't replace %s (it is as it was)", target.c_str());
  }
  sys::start_program({target});
  request_close();
#endif
}

/* Closing while a download runs: it stops (and leaves no .part) before the program's statics go. */
void Editor::cancel_update_download() {
  if (!upd_.download) return;
  upd_.download->cancel.store(true);
  const double t0 = now_seconds();
  while (upd_.download->state.load() <= UpdateDownload::Verifying && now_seconds() - t0 < 5.0) std::this_thread::sleep_for(std::chrono::milliseconds(20));
}

/* ---------------------------------------------------------------- Blender's libraries (task 0080) */

/* At start, in the dependency-free build: the libraries missing or broken (not turned off), the user not having said
 * "Never ask", a window to ask in - so ask GitHub whether this build's libraries can be downloaded. */
void Editor::check_libraries_at_start() {
  const launch::Status &st = launch::status();
  if (st.full) return;
  Log::info("Blendity runs without Blender's libraries: %s", st.reason.c_str());
  if (headless_ || st.why == launch::Status::Chosen || libs_never_ask_ || !use_libs_) return;
  if (std::time(nullptr) - libs_last_check_ < 86400.0) return;  // (asked at most once a day)
  libs_last_check_ = (double)std::time(nullptr);
  upd_.want_libs = true;
  start_update_check(false, false);
}

/* This build's release has its libraries: the Libraries dialog. False when there's nothing to offer (a local build,
 * an older release without them). */
bool Editor::offer_libraries() {
  upd_.want_libs = false;
  const releases::Release *mine = update_release(releases::build_tag());
  const releases::Asset *a = mine ? releases::libraries(*mine, extras_release(), releases::platform()) : nullptr;
  if (!a || !releases::checksums(*mine)) {
    Log::info("Libraries: none to download for this build (%s)", releases::build_label().c_str());
    return false;
  }
  upd_.selected = mine->tag;
  dialog_ = Dialog::Libraries;
  return true;
}

/* "Blender's libraries are missing": what they bring, Download / Not now / Never ask. */
void Editor::draw_libraries_dialog(const std::function<void()> &close) {
  auto &u = ui_;
  const releases::Release *r = update_release(upd_.selected);
  const releases::Asset *a = r ? releases::libraries(*r, extras_release(), releases::platform()) : nullptr;
  if (!a) return close();
  const std::string size = strprintf("%.0f MB", a->size / 1048576.0);
  for (const std::string &l : {launch::status().reason + ", so Blendity runs on its own code.",
                               std::string("With them: Embree's fast ray tracing, OpenImageDenoise, OpenColorIO's views, Jolt physics,"),
                               std::string("FFmpeg's MP4s and the rest of Blender's libraries. Download them (") + size + ")?",
                               std::string("Either way works; Preferences > Libraries changes it later.")}) {
    Recti row = u.popup_row();
    u.label({row.x + u.px(12), row.y, row.w - u.px(20), row.h}, l);
  }
  if (draw_update_progress()) return;
  Recti b = u.popup_row(u.row_h() + u.px(10));
  if (u.button({b.right() - u.px(330), b.y + u.px(4), u.px(110), b.h - u.px(8)}, "Download")) download_verified(*a, *releases::checksums(*r), true);
  if (u.button({b.right() - u.px(212), b.y + u.px(4), u.px(90), b.h - u.px(8)}, "Not now")) return close();
  if (!u.button({b.right() - u.px(114), b.y + u.px(4), u.px(102), b.h - u.px(8)}, "Never ask")) return;
  libs_never_ask_ = true;
  save_prefs();
  close();
}

/* What a checked download needs before it can be used, on the downloading thread (seconds of work): the libraries
 * unpacked into ~/.blendity/libs/<build> and checked to load; an AppImage copied beside the running one. */
bool Editor::prepare_install(UpdateDownload &job, std::string &error) {
  if (job.libraries) return unpack_libraries(job.path, error);
  if (job.moonray) return moonray::unpack(job.mr, job.path, error);
  if (job.ai) return ai::unpack(job.path, error);
#if !defined(_WIN32) && !defined(__APPLE__)
  const char *appimage = std::getenv("APPIMAGE");
  if (!appimage || !*appimage) return true;
  const std::string next = std::string(appimage) + ".new";
  fs::remove_file(next);
  if (!fs::copy_file(job.path, next)) return error = "can't write " + next, false;
  fs::remove_file(job.path);
  sys::run_program({"chmod", "+x", next});
#endif
  return true;
}

/* The libraries archive unpacked (Windows' own tar reads .zip; tar refuses absolute and ../ paths) and probed. */
bool Editor::unpack_libraries(const std::string &archive, std::string &error) {
  const std::string dir = launch::user_libs_dir();
  fs::make_dirs(dir);
#ifdef _WIN32
  const char *root = std::getenv("SystemRoot");
  const std::string tar = fs::join(fs::join(root ? root : "C:\\Windows", "System32"), "tar.exe");  // (System32's, not one on a path)
  const int code = sys::run_program({tar, "-xf", archive, "-C", dir});
#else
  const int code = sys::run_program({"tar", "-xzf", archive, "-C", dir});
#endif
  fs::remove_file(archive);
  if (code != 0) return error = strprintf("couldn't unpack them into %s (tar: %d)", dir.c_str(), code), false;
  if (!launch::full_build_ready(dir)) return error = "unpacked into " + dir + ", but they don't load on this system", false;
  return true;
}

/* This program again, with the arguments it started with (it hands over to the full build if it can). An AppImage
 * starts as itself (this folder is its mount, gone when this process ends). */
void Editor::restart_program() {
  std::vector<std::string> args = launch::status().args;
  if (args.empty()) return;
  std::string self = fs::join(fs::executable_dir(), fs::filename(args[0]));
#ifdef _WIN32
  if (to_lower(fs::extension(self)) != ".exe") self += ".exe";
#else
  if (const char *appimage = std::getenv("APPIMAGE"); appimage && *appimage) self = appimage;
#endif
  args[0] = self;
  if (!sys::start_program(args)) return Log::error("couldn't start %s again", self.c_str());
  request_close();
}

/* Preferences > Libraries: the user's choice, which build is running and why, and Download when they're missing. */
void Editor::draw_libraries_settings() {
  auto &u = ui_;
  const launch::Status &st = launch::status();
  Recti r = u.popup_row(u.row_h() + u.px(4));
  u.label({r.x + u.px(12), r.y, u.px(140), r.h}, "Libraries");
  if (u.checkbox({r.x + u.px(160), r.y + u.px(2), r.h - u.px(4), r.h - u.px(4)}, use_libs_)) {
    libs_never_ask_ = libs_never_ask_ && !use_libs_;  // (turned on again: asking is fine again)
    save_prefs();
  }
  u.label({r.x + u.px(160) + r.h, r.y, u.px(220), r.h}, "Use Blender's libraries");
  u.tooltip("On: Blendity starts with Blender's libraries (Embree, OpenImageDenoise, OpenColorIO, Jolt, FFmpeg...) when\n"
            "they're installed. Off: always the dependency-free Blendity. Takes effect when Blendity starts.");
  if (!st.full && use_libs_ && st.why != launch::Status::Chosen && u.button({r.right() - u.px(130), r.y + u.px(2), u.px(118), r.h - u.px(4)}, "Download..."))
    update_command({"update", "libs"});
  Recti s = u.popup_row();
  const std::string now = st.full ? "Running with Blender's libraries." : "Running without them: " + st.reason + ".";
  u.label({s.x + u.px(160), s.y, s.w - u.px(170), s.h}, now, u.theme.text_dim);
}

/* update check | history | install <tag> | auto on|off | previews on|off: the console's updater. */
void Editor::update_command(const std::vector<std::string> &t) {
  const std::string verb = t.size() > 1 ? to_lower(t[1]) : "check";
  const bool on = t.size() > 2 && (to_lower(t[2]) == "on" || t[2] == "1");
  if (verb == "check") return start_update_check(true);
  if (verb == "history") {
    upd_.history = true;
    upd_.selected.clear();
    if (upd_.list.empty()) start_update_check(true);
    dialog_ = Dialog::History;
    return;
  }
  if (verb == "install" && t.size() > 2) return install_release(t[2]);
  if (verb == "libs") {  // update libs: ask for this build's libraries now (Preferences' Download)
    upd_.want_libs = true;
    return start_update_check(true, false);
  }
  if (verb == "libson" || verb == "libsoff") {  // Preferences > Libraries: Use Blender's libraries (task 0080)
    use_libs_ = verb == "libson";
    libs_never_ask_ = libs_never_ask_ && !use_libs_;
    Log::info("Blender's libraries: %s from the next start", use_libs_ ? "used" : "not used");
  }
  else if (verb == "auto") update_check_ = on;
  else if (verb == "previews") update_previews_ = on;
  else return Log::warn("update check | history | install <tag> | libs | libson | libsoff | auto on|off | previews on|off");
  save_prefs();
}

}  // namespace bl
