// SPDX-License-Identifier: GPL-2.0-or-later
// The job tray (task 0253, ADR 0085 F2): every long job - the remesh, mesh-maps, baked-light, surface and skin workers,
// the lightmap and probe bake, the final render, MoonRay, the animation's frames and its video, a main-thread import -
// listed each frame from the state that already exists, drawn in the status bar's right slot with its progress, the
// time it has run and an estimate left, and one Cancel that reaches the job's own flag (Nielsen H1: visibility of
// system status; H3: user control). A job's end raises an Info toast with its time, so it is seen from anywhere.
#include "editor.h"

#include "../core/core.h"

#include <algorithm>
#include <cmath>

namespace bl {

static float clamp01(float v) { return std::max(0.0f, std::min(1.0f, v)); }

static std::string mmss(double seconds) {
  const int t = (int)std::max(0.0, seconds);
  return strprintf("%d:%02d", t / 60, t % 60);
}

/* The running jobs, newest state each frame; `started` from the tray's own record (jobs_poll). */
std::vector<Editor::JobView> Editor::jobs_running() {
  std::vector<JobView> jobs;
  auto add = [&](const std::string &label, float progress, std::function<void()> cancel, bool cancelled = false) {
    JobView j;
    j.label = label, j.progress = progress, j.cancellable = cancel != nullptr, j.cancel = std::move(cancel), j.cancelled = cancelled;
    jobs.push_back(std::move(j));
  };
  if (!busy_label_.empty()) add(busy_label_, -1.0f, nullptr);
  if (sculpt_remesh_)
    add("Quad Remesh", -1.0f, [this] { sculpt_remesh_->cancel.store(true, std::memory_order_relaxed); }, sculpt_remesh_->cancel.load(std::memory_order_relaxed));
  if (meshmaps_job_)
    add("Mesh Maps", meshmaps_job_->progress.load(std::memory_order_relaxed), [this] { meshmaps_cancel(); }, meshmaps_job_->cancel.load(std::memory_order_relaxed));
  if (bakelight_)
    add(bakelight_->gradient ? "Baked Gradient" : "Baked Light", bakelight_->progress.load(std::memory_order_relaxed),
        [this] { bakelight_->cancel.store(true, std::memory_order_relaxed); }, bakelight_->cancel.load(std::memory_order_relaxed));
  if (surface_job_) add("Surface", -1.0f, nullptr);  // (no Cancel: the stack would be evaluated again at once)
  if (skin_job_) add(skin_job_->label, skin_job_->progress.load(), [this] { skin_job_->cancel = true; }, skin_job_->cancel.load());
  if (baking_) add("Baking lighting", probe_phase_ ? -1.0f : baker_.progress(), [this] { bake_cancel(); });  // (one job through both phases)
  if (rendering_ && mr_job_) {
    const float p = mr_job_->progress.load();
    add("MoonRay", p > 0.0f ? p : -1.0f, [this] { mr_job_->cancel.store(true); }, mr_job_->cancel.load());
  }
  else if (rendering_ && !ranim_.active && !seq_.active) {  // (an animation or a sequence is one job below, not a Render a frame)
    const RenderSettings &rs = scene_->render;
    const int target = std::max(1, render_preview_ ? rs.preview_samples : rs.samples);
    add(render_preview_ ? "Preview render" : "Render", std::min(1.0f, (float)final_pt_.samples() / (float)target), [this] { render_stop(); });
  }
  if (ranim_.active) {
    const int n = std::max(1, ranim_.last - ranim_.first + 1);
    add("Rendering animation", (float)(ranim_.frame - ranim_.first) / (float)n, [this] { stop_render_animation(); });
  }
  if (seq_.active) add("Rendering camera sequence", (float)seq_.index / (float)std::max<size_t>(1, seq_.cams.size()), [this] { stop_render_sequence(); });
  if (ranim_.encode) add("Encoding video", -1.0f, nullptr);  // (no Cancel: an external FFmpeg cannot be told)
  for (JobView &j : jobs) {
    const auto seen = std::find_if(jobs_seen_.begin(), jobs_seen_.end(), [&](const JobSeen &s) { return s.label == j.label; });
    if (seen != jobs_seen_.end()) j.started = seen->started;
  }
  return jobs;
}

/* Each frame: a job seen for the first time is recorded (its start, the HCI log); one gone since the last frame ends
 * with an Info toast saying how long it took, or that it was cancelled. */
void Editor::jobs_poll() {
  const double now = clock_now();
  const std::vector<JobView> jobs = jobs_running();
  for (JobSeen &s : jobs_seen_) s.alive = false;
  for (const JobView &j : jobs) {
    const auto seen = std::find_if(jobs_seen_.begin(), jobs_seen_.end(), [&](const JobSeen &s) { return s.label == j.label; });
    if (seen != jobs_seen_.end()) {
      seen->alive = true;
      seen->cancelled = seen->cancelled || j.cancelled;  // (a cancel made another way - Esc, a console verb - counts)
      continue;
    }
    jobs_seen_.push_back({j.label, now, false, true});
    hci_log("job\tstart " + j.label);
  }
  for (size_t i = 0; i < jobs_seen_.size();) {
    if (jobs_seen_[i].alive) {
      i++;
      continue;
    }
    const JobSeen s = jobs_seen_[i];
    jobs_seen_.erase(jobs_seen_.begin() + (std::ptrdiff_t)i);
    const double secs = now - s.started;
    const char *end = s.cancelled ? "cancelled" : "done";
    /* A toast for a cancel, or for a job that ran a second or more; never for the Live Preview's renders. */
    const bool worth_a_toast = s.cancelled || (secs >= 1.0 && s.label != "Preview render");
    if (worth_a_toast) toast(LogLevel::Info, strprintf("%s %s in %.1f s", s.label.c_str(), end, secs));
    hci_log(strprintf("job\t%s %s %.3f", end, s.label.c_str(), secs));
  }
}

/* The tray in the status bar: the newest job's label, percentage, time run and estimate left, a bar behind the text
 * (a moving stripe for a job without a progress), "+N" for the others (all of them in the tooltip), and Cancel. Returns
 * the width taken, from `right_x` leftward. */
int Editor::draw_job_tray(int right_x, int y, int h, const std::vector<JobView> &jobs) {
  if (jobs.empty()) return 0;
  auto &u = ui_;
  const JobView &j = jobs.front();
  const double now = clock_now(), elapsed = j.started > 0.0 ? now - j.started : 0.0;
  std::string text = j.label;
  if (j.progress >= 0.0f) text += strprintf("  %d%%", (int)(clamp01(j.progress) * 100.0f));
  text += "  " + mmss(elapsed);
  if (j.progress >= 0.02f && j.progress < 1.0f) text += "  ~" + mmss(elapsed * (1.0 - j.progress) / j.progress) + " left";
  if (jobs.size() > 1) text += strprintf("  +%zu", jobs.size() - 1);
  const int bw = j.cancellable ? u.font.text_width("Cancel") + u.px(16) : 0;
  const int tw = u.font.text_width(text) + u.px(16);
  const int w = tw + (bw ? bw + u.px(6) : 0) + u.px(12);
  const Recti bar{right_x - w + u.px(6), y + u.px(3), tw, h - u.px(6)};
  u.frame(bar, u.theme.well, u.theme.border, u.px(3));
  const Recti inner{bar.x + 1, bar.y + 1, bar.w - 2, bar.h - 2};
  if (j.progress >= 0.0f) u.canvas.fill_rect({inner.x, inner.y, (int)((float)inner.w * clamp01(j.progress)), inner.h}, u.theme.progress);  // (task 0261: the theme's, under text_bright)
  else {
    const int sw = std::max(1, inner.w / 4), sx = (int)(std::fmod(now * 0.5, 1.0) * (double)(inner.w - sw));
    u.canvas.fill_rect({inner.x + sx, inner.y, sw, inner.h}, u.theme.progress);
  }
  u.label(bar, text, u.theme.text_bright, ui::Align::Center);
  u.mark_widget(bar);
  std::string all;
  for (const JobView &k : jobs) {
    if (!all.empty()) all += "\n";
    all += k.label + (k.progress >= 0.0f ? strprintf("  %d%%", (int)(clamp01(k.progress) * 100.0f)) : std::string("  (no progress to show)")) + "  " +
           mmss(k.started > 0.0 ? now - k.started : 0.0);
  }
  u.tooltip(all);
  if (u.record_buttons) u.button_rects["job:tray"] = bar;
  if (bw) {
    const Recti cb{bar.right() + u.px(6), y + u.px(3), bw, h - u.px(6)};
    if (u.button(cb, "Cancel")) jobs_cancel(j.label);
    u.tooltip("Stop this job; what it made so far is dropped.");
    if (u.record_buttons) u.button_rects["job:Cancel"] = cb;
  }
  u.redraw = true;  // (the times and the stripe move)
  return w;
}

/* Cancel by label, from the tray or the console: the job's own flag, and the end toast says "cancelled". */
bool Editor::jobs_cancel(const std::string &label) {
  const std::vector<JobView> jobs = jobs_running();
  const auto it = std::find_if(jobs.begin(), jobs.end(), [&](const JobView &j) { return j.label == label; });
  if (it == jobs.end()) return false;
  if (!it->cancellable) {
    Log::warn("%s cannot be cancelled", label.c_str());
    return true;
  }
  for (JobSeen &s : jobs_seen_)
    if (s.label == label) s.cancelled = true;
  hci_log("job\tcancel " + label);
  it->cancel();
  return true;
}

/* jobs | jobs cancel [label] */
void Editor::jobs_command(const std::vector<std::string> &t) {
  const std::vector<JobView> jobs = jobs_running();
  const bool cancel = t.size() > 1 && to_lower(t[1]) == "cancel";
  if (cancel && jobs.empty()) return Log::warn("jobs cancel: nothing is running");
  if (cancel) {
    std::string name;
    for (size_t i = 2; i < t.size(); i++) name += (name.empty() ? "" : " ") + t[i];
    if (name.empty()) name = jobs.front().label;
    if (!jobs_cancel(name)) Log::warn("jobs cancel: no job named '%s'", name.c_str());
    return;
  }
  if (jobs.empty()) return Log::info("jobs: nothing is running");
  const double now = clock_now();
  for (const JobView &j : jobs)
    Log::info("job: %s%s  %s%s", j.label.c_str(), j.progress >= 0.0f ? strprintf("  %d%%", (int)(clamp01(j.progress) * 100.0f)).c_str() : "",
              mmss(j.started > 0.0 ? now - j.started : 0.0).c_str(), j.cancellable ? "" : "  (cannot be cancelled)");
}

/* A main-thread job that blocks: its label in the tray for a frame first, then the work, two frames on. */
void Editor::busy(const std::string &label, std::function<void()> fn) {
  busy_label_ = label;
  ui_.redraw = true;
  defer([this, fn] {
    fn();
    busy_label_.clear();
  }, 2);
}

/* The final render stopped: what the Render window's Stop did, shared with the tray. */
void Editor::render_stop() {
  if (!rendering_) return;
  rendering_ = false;
  final_pt_.pause();  // (task 0161: the cards stop too)
  resolve_final_render(true);
  render_status_ += "  (stopped)";
}

}  // namespace bl
