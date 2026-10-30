// SPDX-License-Identifier: GPL-2.0-or-later
// MoonRay in the editor (tasks 0112, 0114, ADR 0041): finding it, the optional install (offered on the first run as
// Blender's libraries are, task 0080), Preferences > Renderers, the console, and F12 renders run in it.
#include "editor.h"
#include "../core/net.h"
#include "../render/display.h"
#include <chrono>
#include <ctime>
#include <filesystem>
#include <mutex>

namespace bl {

/* Looks for MoonRay in the background (on Windows this starts WSL: a second or more). */
void Editor::moonray_locate() {
  if (mr_locate_ && mr_locate_->folder == mr_folder_) return;
  auto job = std::make_shared<MoonRayLocate>();
  mr_locate_ = job;  // (a search for another folder still running is dropped when it ends)
  job->folder = mr_folder_;
  std::thread([job] {
    job->found = moonray::locate(job->folder);
    job->done.store(true);
  }).detach();
}

/* A slow chore on its own thread (one at a time); its result is logged and taken by poll_moonray. */
void Editor::moonray_task(const std::string &what, const std::function<bool(moonray::Install &, std::string &)> &chore) {
  if (mr_task_) return Log::warn("MoonRay: still busy with the last request");
  auto task = std::make_shared<MoonRayTask>();
  mr_task_ = task;
  task->install = mr_;
  Log::info("MoonRay: %s...", what.c_str());
  std::thread([task, chore] {
    std::string error;
    task->ok = chore(task->install, error);
    task->message = error;
    task->done.store(true);
  }).detach();
}

bool Editor::moonray_pref(const std::string &key, const std::string &value) {
  if (key == "moonray_never_ask") mr_never_ask_ = value == "1";
  else if (key == "moonray_last") mr_last_ask_ = std::atof(value.c_str());
  else if (key == "moonray_folder") mr_folder_ = value;
  else if (key == "moonray_installed") mr_installed_ = value == "1";
  else return false;
  return true;
}

/* Each frame: a finished search, the install question when it is due, F12 waiting for the search. */
void Editor::poll_moonray() {
  if (mr_waiting_render_ && !rendering_) mr_waiting_render_ = false;  // (Stopped while MoonRay was being looked for)
  if (mr_locate_ && mr_locate_->done.load()) {
    const std::shared_ptr<MoonRayLocate> job = std::move(mr_locate_);
    if (job->folder != mr_folder_) return moonray_locate();  // (the folder changed meanwhile: look again)
    mr_ = job->found;
    mr_located_ = true;
    if (mr_.ready() != mr_installed_) {
      mr_installed_ = mr_.ready();
      if (!headless_) save_prefs();
    }
    if (mr_.ready()) Log::info("MoonRay %s found%s", mr_.version.c_str(), mr_.kind == moonray::Install::Wsl ? " in WSL" : "");
    if (mr_waiting_render_) {
      mr_waiting_render_ = false;
      rendering_ = false;
      start_final_render(false, false);  // (now MoonRay's, or the path tracer's when it can't run)
    }
    if (!mr_queued_.empty()) {
      const std::vector<std::string> t = std::move(mr_queued_);
      mr_queued_.clear();
      moonray_command(t);
    }
  }
  if (mr_task_ && mr_task_->done.load()) {
    const std::shared_ptr<MoonRayTask> task = std::move(mr_task_);
    mr_ = task->install;
    mr_installed_ = mr_.ready();
    if (!headless_) save_prefs();
    if (!task->ok) Log::warn("MoonRay: %s", task->message.c_str());
    else Log::info("MoonRay: done%s", mr_.ready() ? (" (" + mr_.version + " installed)").c_str() : " (not installed)");
  }
  if (mr_job_ && !rendering_) mr_job_.reset();  // (Stop: MoonRay is killed and waited for)
  if (!mr_offer_pending_ || dialog_ != Dialog::None) return;
  if (!mr_located_) return moonray_locate();
  mr_offer_pending_ = false;
  if (mr_never_ask_ || mr_.ready()) return;
  const bool first = mr_last_ask_ == 0.0;
  mr_last_ask_ = (double)std::time(nullptr);
  save_prefs();
  if (!mr_.installable()) {  // (no WSL on Windows, a Mac without Apple silicon: said once, on the first run)
    if (first) dialog_ = Dialog::MoonRay;
    return;
  }
  upd_.want_moonray = true;
  start_update_check(false, false);
}

/* The archive for this machine is published: the MoonRay dialog. */
bool Editor::offer_moonray() {
  const releases::Release *ex = extras_release();
  const std::string name = moonray::archive_name();
  if (!ex || name.empty()) return false;
  for (const releases::Asset &a : ex->assets)
    if (a.name == name) {
      dialog_ = Dialog::MoonRay;
      return true;
    }
  Log::info("MoonRay: %s isn't published yet", name.c_str());
  return false;
}

namespace {
/* The archive and its checksum file in the libraries release, or nulls. */
std::pair<const releases::Asset *, const releases::Asset *> moonray_assets(const releases::Release *ex) {
  const std::string name = moonray::archive_name();
  const releases::Asset *a = nullptr, *sum = nullptr;
  if (!ex || name.empty()) return {a, sum};
  for (const releases::Asset &x : ex->assets) {
    if (x.name == name) a = &x;
    if (x.name == name + ".sha256") sum = &x;
  }
  return {a, sum};
}
}  // namespace

/* "MoonRay, an optional render engine": what it is, Download / Not now / Never ask (or why it can't run here). */
void Editor::draw_moonray_dialog(const std::function<void()> &close) {
  auto &u = ui_;
  auto say = [&](const std::string &l) {
    Recti row = u.popup_row();
    u.label({row.x + u.px(12), row.y, row.w - u.px(20), row.h}, l);
  };
  say(("MoonRay is DreamWorks' production renderer (open source, Apache-2.0). Blendity can render " + key("render.image")).c_str());
  say("images and animations in it, as a third render engine beside the rasterizer and the path tracer.");
  if (!mr_.installable()) {
    say("It can't run on this computer yet: " + mr_.problem + ".");
    say("Edit > Preferences > Renderers installs it later.");
    Recti b = u.popup_row(u.row_h() + u.px(10));
    if (u.button({b.right() - u.px(212), b.y + u.px(4), u.px(90), b.h - u.px(8)}, "OK")) return close();
    if (!u.button({b.right() - u.px(114), b.y + u.px(4), u.px(102), b.h - u.px(8)}, "Never ask")) return;
    mr_never_ask_ = true;
    save_prefs();
    return close();
  }
  const auto [a, sum] = moonray_assets(extras_release());
  if (!a || !sum) return close();
  if (mr_.kind == moonray::Install::Wsl) say("On Windows it runs in WSL (" + mr_.distro + "), where it is installed.");
  say(strprintf("It is optional: Blendity works the same without it. Download it (%.0f MB)?", a->size / 1048576.0));
  if (draw_update_progress()) return;
  Recti b = u.popup_row(u.row_h() + u.px(10));
  if (u.button({b.right() - u.px(330), b.y + u.px(4), u.px(110), b.h - u.px(8)}, "Download")) download_verified(*a, *sum, false, true);
  if (u.button({b.right() - u.px(212), b.y + u.px(4), u.px(90), b.h - u.px(8)}, "Not now")) return close();
  if (!u.button({b.right() - u.px(114), b.y + u.px(4), u.px(102), b.h - u.px(8)}, "Never ask")) return;
  mr_never_ask_ = true;
  save_prefs();
  close();
}

/* Preferences > Renderers: MoonRay's state, Install / Remove. */
void Editor::draw_moonray_settings() {
  auto &u = ui_;
  if (!mr_located_) moonray_locate();
  Recti r = u.popup_row(u.row_h() + u.px(4));
  u.label({r.x + u.px(12), r.y, u.px(140), r.h}, "Renderers");
  std::string state = "MoonRay: looking for it...";
  if (mr_located_) {
    if (mr_.ready()) state = "MoonRay " + mr_.version + (mr_.kind == moonray::Install::Wsl ? " (in WSL)" : "") + ": Render Engine > MoonRay";
    else if (!mr_.installable()) state = "MoonRay can't run here: " + mr_.problem;
    else state = "MoonRay isn't installed (optional: DreamWorks' production renderer)";
  }
  u.label({r.x + u.px(160), r.y, r.w - u.px(300), r.h}, state, mr_.ready() ? u.theme.text : u.theme.text_dim);
  u.tooltip(std::string("MoonRay is a separate program Blendity downloads on request and runs for ") + key("render.image") + " renders (on Windows inside WSL).");
  if (!mr_located_) return;
  const Recti btn{r.right() - u.px(130), r.y + u.px(2), u.px(118), r.h - u.px(4)};
  if (mr_task_) return;  // (busy: its result is logged)
  draw_moonray_own_row();
  if (mr_.ready() && !mr_.own_build && u.button(btn, "Remove")) return moonray_command({"moonray", "remove"});
  if (!mr_.ready() && mr_.installable() && u.button(btn, "Install...")) moonray_command({"moonray", "install"});
}

/* Task 0288: your own MoonRay (a folder you built) and a test render - what `moonray folder` and `moonray check` did. */
void Editor::draw_moonray_own_row() {
  auto &u = ui_;
  Recti r = u.popup_row(u.row_h() + u.px(4));
  std::string &text = picker_text_["pref:moonray"];
  if (!u.editing(u.id("pref_mr_folder"))) text = mr_folder_;
  u.label({r.x + u.px(160), r.y, u.px(120), r.h}, "Your own MoonRay", u.theme.text_dim);
  if (path_picker_field({r.x + u.px(284), r.y + u.px(2), r.w - u.px(430), r.h - u.px(4)}, u.id("pref_mr_folder"), "pref:moonray", text, PickKind::Folder, {},
                        [this](const std::string &p) { moonray_command({"moonray", "folder", p}); }) &&
      text != mr_folder_)
    moonray_command({"moonray", "folder", text});
  u.tooltip("A MoonRay you built yourself (its folder, with bin/moonray); empty: the one Blendity installs.");
  const Recti check{r.right() - u.px(130), r.y + u.px(2), u.px(118), r.h - u.px(4)};
  if (u.record_buttons) u.button_rects["pref:MoonRay Check"] = check;
  if (u.button_enabled(check, "Check", mr_.ready(), "MoonRay isn't installed: Install... first.")) moonray_command({"moonray", "check"});
  u.tooltip("Render MoonRay's test scene now: says whether it works here, and how long it took.");
}

/* moonray status | install [archive] | remove | folder [path] | check | locate: the console's MoonRay. */
void Editor::moonray_command(const std::vector<std::string> &t) {
  const std::string sub = t.size() > 1 ? to_lower(t[1]) : "status";
  if (sub == "locate" || (!mr_located_ && sub != "folder")) {
    moonray_locate();
    if (sub != "locate") mr_queued_ = t;  // (answered once it is found: WSL may take a few seconds to start)
    return Log::info("MoonRay: looking for it...");
  }
  if (sub == "status") {
    if (mr_.ready()) return Log::info("MoonRay %s at %s%s", mr_.version.c_str(), mr_.root.c_str(), mr_.kind == moonray::Install::Wsl ? " (WSL)" : "");
    return Log::info("MoonRay: %s", mr_.installable() ? "not installed (moonray install)" : mr_.problem.c_str());
  }
  if (sub == "folder") {
    mr_folder_ = t.size() > 2 ? t[2] : "";
    save_prefs();
    mr_located_ = false;
    moonray_locate();
    return Log::info("MoonRay: %s", mr_folder_.empty() ? "Blendity's own download" : ("your MoonRay at " + mr_folder_).c_str());
  }
  if (sub == "remove") return moonray_task("removing it", [](moonray::Install &in, std::string &e) { return moonray::remove(in, e); });
  if (sub == "check") return moonray_task("rendering its test scene", [](moonray::Install &in, std::string &e) { return moonray::probe(in, e); });
  if (sub != "install") return Log::warn("moonray status | install [archive] | remove | folder [path] | check");
  if (!mr_.installable()) return Log::warn("MoonRay can't run here: %s", mr_.problem.c_str());
  if (t.size() > 2) {  // (an archive already here, checked by its .sha256 beside it when there is one)
    const std::string archive = t[2];
    return moonray_task("installing " + fs::filename(archive), [archive](moonray::Install &in, std::string &e) {
      std::string sums;
      if (fs::read_file(archive + ".sha256", sums) && releases::sum_for(sums, fs::filename(archive)) != net::sha256_file(archive))
        return e = fs::filename(archive) + " doesn't match its checksum", false;
      const std::string copy = fs::join(net::downloads_dir(), fs::filename(archive));  // (unpack removes the file it was given)
      if (!fs::copy_file(archive, copy)) return e = "can't copy " + archive, false;
      return moonray::unpack(in, copy, e);
    });
  }
  upd_.want_moonray = true;
  start_update_check(false, false);
}

/* F12 in MoonRay: the scene written for it, then MoonRay run on a thread of its own; step_moonray_render takes the
 * image. False if the scene couldn't be written (the path tracer renders instead). */
bool Editor::begin_moonray_render(const moonray::ExportScene &sc, const MoonRayFrame &frame) {
  const std::string dir = moonray::scratch_dir();  // (this Blendity's own: another one's render isn't touched)
  const moonray::Install in = mr_;
  moonray::ExportReport rep;
  std::string error;
  const double t0 = now_seconds();
  if (!moonray::export_scene(sc, dir, [&](const std::string &p) { return moonray::path_for(in, p); }, rep, error)) {
    Log::error("MoonRay: the scene couldn't be written (%s): rendering with the path tracer", error.c_str());
    return false;
  }
  for (const std::string &n : rep.notes) {
    if (std::find(mr_notes_shown_.begin(), mr_notes_shown_.end(), n) != mr_notes_shown_.end()) continue;
    mr_notes_shown_.push_back(n);
    Log::info("MoonRay: %s", n.c_str());
  }
  mr_frame_ = frame;
  auto job = std::make_shared<MoonRayJob>();
  job->image = rep.image_path;
  job->progress_path = rep.progress_path;
  const std::string scene = moonray::path_for(in, rep.scene_path);
  MoonRayJob *j = job.get();  // (the job outlives its thread: its destructor joins it)
  job->thread = std::thread([j, in, scene, textures = std::move(rep.texture_jobs)] {
    for (const moonray::TextureJob &tj : textures) {  // (here, not on the editor's thread: a big image takes a while)
      if (j->cancel.load()) return j->state.store(MoonRayJob::Cancelled);
      std::string e;
      if (!moonray::convert_texture(tj, e) && j->complaints.size() < 20)
        j->complaints.push_back("the texture " + tj.source + " couldn't be read (" + e + "): its material's color is used");
    }
    std::string tail, line;
    /* MoonRay's checkpoints, watched twice a second while it renders (it prints nothing meanwhile): each new one read
     * here, off the editor's thread, for the Render panel. MoonRay writes them whole (a .part file renamed). */
    std::atomic<bool> ran{false};
    std::thread watch([j, &ran] {
      std::filesystem::file_time_type seen{};
      while (!ran.load() && !j->cancel.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        std::error_code ec;
        const auto t = std::filesystem::last_write_time(std::filesystem::u8path(j->progress_path), ec);
        if (ec || t == seen) continue;
        Bitmap bm;
        std::string e;
        if (!load_image(j->progress_path, bm, e) || !bm.is_float) continue;
        seen = t;
        std::lock_guard<std::mutex> lock(j->preview_mu);
        j->preview = std::move(bm);
        j->preview_version.fetch_add(1);
      }
    });
    auto complain = [&] {  // (its "Error:" lines - a texture it couldn't read - kept to be logged, even when it renders)
      const size_t at = line.find("Error:");
      if (at != std::string::npos && j->complaints.size() < 20) {
        std::string msg = line.substr(at);
        msg.erase(std::remove(msg.begin(), msg.end(), '\x1b'), msg.end());
        j->complaints.push_back(msg);
      }
      line.clear();
    };
    const int code = moonray::run(in, {"-in", scene}, [&](const std::string &s) {
      tail += s;
      for (char c : s) {
        if (c == '\n' || c == '\r') complain();
        else line += c;
      }
      if (tail.size() > 4096) tail.erase(0, tail.size() - 2048);
      const float p = moonray::parse_progress(tail.size() > 200 ? tail.substr(tail.size() - 200) : tail);
      if (p >= 0.0f) j->progress.store(p);
    }, &j->cancel);
    ran.store(true);
    watch.join();
    complain();  // (a last line without its newline)
    if (code == sys::kExitCancelled) return j->state.store(MoonRayJob::Cancelled);
    if (code == 0 && fs::exists(j->image)) return j->state.store(MoonRayJob::Done);
    j->error = strprintf("exit %d: %s", code, tail.size() > 600 ? tail.substr(tail.size() - 600).c_str() : tail.c_str());
    j->state.store(MoonRayJob::Failed);
  });
  mr_job_ = job;
  rendering_ = true;
  render_status_ = strprintf("MoonRay %dx%d: %zu meshes (%zu triangles), %zu lights written in %.0f ms; rendering...", frame.width, frame.height,
                             rep.meshes, rep.triangles, rep.lights, (now_seconds() - t0) * 1000.0);
  Log::info("%s", render_status_.c_str());
  return true;
}

/* A frame MoonRay didn't render: an animation or a camera sequence stops (rather than save an empty image). */
void Editor::moonray_frame_failed() {
  if (ranim_.active) stop_render_animation();
  if (seq_.active) stop_render_sequence();
}

/* Each frame of an F12 render in MoonRay: its progress; then its image, through Blendity's view transform and exposure
 * (as the path tracer's), the background a camera's solid colour or transparent. */
void Editor::step_moonray_render() {
  if (mr_waiting_render_ || !mr_job_) return;  // (still looking for MoonRay)
  const int st = mr_job_->state.load();
  render_time_ = now_seconds() - render_start_;
  if (st == MoonRayJob::Running) {
    const float p = mr_job_->progress.load();
    render_status_ = p > 0.0f ? strprintf("MoonRay: %.0f%%  |  %.1f s", p * 100.0f, render_time_)
                              : strprintf("MoonRay: filling in (%d update%s)  |  %.1f s", mr_job_->preview_version.load(),
                                          mr_job_->preview_version.load() == 1 ? "" : "s", render_time_);  // (checkpoints: no percentage)
    const int v = mr_job_->preview_version.load();
    if (v == mr_job_->preview_shown) return;
    std::lock_guard<std::mutex> lock(mr_job_->preview_mu);
    if (show_moonray_image(mr_job_->preview)) mr_job_->preview_shown = v, ui_.redraw = true;  // (the image filling in)
    return;
  }
  const std::shared_ptr<MoonRayJob> job = std::move(mr_job_);
  rendering_ = false;
  for (const std::string &c : job->complaints) {  // (once each: an animation's frames all say the same)
    if (std::find(mr_notes_shown_.begin(), mr_notes_shown_.end(), c) != mr_notes_shown_.end()) continue;
    mr_notes_shown_.push_back(c);
    Log::warn("MoonRay: %s", c.c_str());
  }
  if (st != MoonRayJob::Done) {
    render_status_ = st == MoonRayJob::Cancelled ? "MoonRay: stopped" : "MoonRay failed: " + job->error;
    (st == MoonRayJob::Cancelled ? Log::info : Log::error)("%s", render_status_.c_str());
    return moonray_frame_failed();
  }
  Bitmap bm;
  std::string error;
  if (!load_image(job->image, bm, error) || !show_moonray_image(bm)) {
    render_status_ = "MoonRay's image couldn't be read: " + (error.empty() ? std::string("the wrong size") : error);
    Log::error("%s", render_status_.c_str());
    return moonray_frame_failed();
  }
  render_status_ = strprintf("MoonRay %dx%d, %.1f s", mr_frame_.width, mr_frame_.height, render_time_);
  Log::info("Render finished: %s", render_status_.c_str());
}

/* MoonRay's image (a checkpoint or the final one) in the Render panel: through Blendity's view transform and exposure,
 * as the path tracer's, the background a camera's solid colour or transparent. False when it isn't this frame's. */
bool Editor::show_moonray_image(const Bitmap &bm) {
  if (!bm.is_float || bm.width != mr_frame_.width || bm.height != mr_frame_.height) return false;
  const MoonRayFrame &f = mr_frame_;
  const size_t n = (size_t)f.width * f.height;
  render_linear_.assign(n * 3, 0.0f);
  std::vector<float> alpha(n);
  for (size_t i = 0; i < n; i++) {
    const float *px = &bm.rgbaf[i * 4];
    const float a = std::max(0.0f, std::min(1.0f, px[3]));
    alpha[i] = a;
    for (int c = 0; c < 3; c++) {
      const float bg = f.solid ? (&f.background.x)[c] : 0.0f;
      render_linear_[i * 3 + c] = std::isfinite(px[c]) ? px[c] + bg * (1.0f - a) : 0.0f;  // (MoonRay's colour is premultiplied)
    }
  }
  render_img_.resize(f.width, f.height);
  display::encode_span(render_linear_.data(), render_img_.pixels.data(), n, f.view_transform, f.exposure);
  if (f.transparent) {
    for (size_t i = 0; i < n; i++) {
      const float a = alpha[i], d = std::max(a, 0.05f);
      const float straight[3] = {render_linear_[i * 3] / d, render_linear_[i * 3 + 1] / d, render_linear_[i * 3 + 2] / d};
      uint32_t px;
      display::encode_span(straight, &px, 1, f.view_transform, f.exposure);
      const int a8 = (int)std::lround(a * 255.0f);
      render_img_.pixels[i] = a8 <= 0 ? 0u : ((uint32_t)a8 << 24 | (px & 0xFFFFFF));
    }
    render_alpha_lin_ = alpha;
  }
  return true;
}

}  // namespace bl
