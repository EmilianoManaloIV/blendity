// SPDX-License-Identifier: GPL-2.0-or-later
// AI inbetweening in the editor (tasks 0129-0130, ADR 0046): the optional download (offered the first time the switch
// is turned on, Preferences > Animation Assistants, `ai install`), and the Animation window's AI Inbetween: the span
// between the keys around the playhead made by the network on a worker, shown as a ghost, written by Apply as
// ordinary keys (one undo step). Every other span keeps its own keys; nothing changes on its own.
#include "editor.h"
#include "../core/net.h"
#include "../scene/body_physics.h"

namespace bl {

namespace {
/* A file name from a character's name: letters, digits, spaces, - and _ (anything else _). */
std::string safe_name(std::string n) {
  for (char &ch : n)
    if (!std::isalnum((unsigned char)ch) && ch != ' ' && ch != '-' && ch != '_') ch = '_';
  return n.empty() ? std::string("character") : n;
}

std::string list_of(const std::vector<std::string> &v) {
  std::string s;
  for (const std::string &x : v) s += (s.empty() ? "" : ", ") + x;
  return s;
}

/* The keyframes of a clip: every track's key frames, once each, in order. */
std::vector<float> key_frames(const AnimationClip &c) {
  std::vector<float> f;
  for (const AnimTrack &t : c.tracks)
    for (const AnimKey &k : t.keys) f.push_back(k.frame);
  std::sort(f.begin(), f.end());
  f.erase(std::unique(f.begin(), f.end(), [](float a, float b) { return std::fabs(a - b) < 0.01f; }), f.end());
  return f;
}

/* The span around `frame`: the last keyframe at or before it and the next after that (false when there is none). */
bool span_around(const AnimationClip &c, float frame, int &from, int &to) {
  const std::vector<float> f = key_frames(c);
  for (size_t i = 0; i + 1 < f.size(); i++)
    if (f[i] <= frame + 0.01f && (frame < f[i + 1] - 0.01f || i + 2 == f.size())) {
      from = (int)std::lround(f[i]), to = (int)std::lround(f[i + 1]);
      return true;
    }
  return false;
}
}  // namespace

/* The model's description: the stand-in's (tests), else the install's inbetween.json. */
bool Editor::ai_info(ai::ModelInfo &info) {
  if (ai_.rt) return info = ai_.rt->info, true;
  if (!ai_.info_read) {  // (once: the toolbar asks every frame)
    std::string text;
    ai_.info_read = true;
    if (!fs::read_file(fs::join(ai::install_dir(), "inbetween.json"), text) || !ai::parse_model_info(text, ai_.info)) ai_.info = ai::ModelInfo();
  }
  return ai_.info.valid() ? (info = ai_.info, true) : false;
}

/* The ghost or result belongs to this character's clip (not another's that happens to share its version). */
bool Editor::ai_current(GameObject *root, const AnimationClipPtr &clip) const {
  return root && clip && root->id == ai_.root && clip == ai_.clip.lock();
}

/* A slow chore on a worker, one at a time; poll_ai logs its result. */
void Editor::ai_chore(const std::string &what, const std::function<bool(std::string &)> &fn) {
  if (ai_.chore) return Log::warn("AI Inbetweening: still busy (%s)", ai_.chore->what.c_str());
  auto c = std::make_shared<AiChore>();
  c->what = what;
  ai_.chore = c;
  Log::info("AI Inbetweening: %s...", what.c_str());
  std::thread([c, fn] {
    c->ok = fn(c->message);
    c->done.store(true);
  }).detach();
}

/* Closing: a fit stopped, and every worker waited for (a few seconds at most), before the editor goes. */
void Editor::ai_shutdown() {
  if (ai_.tune) ai_.tune->cancel.store(true);
  for (int i = 0; i < 1000; i++) {
    const bool busy = (ai_.job && !ai_.job->done.load()) || (ai_.tune && !ai_.tune->done.load()) || (ai_.chore && !ai_.chore->done.load());
    if (!busy) return;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}

/* Generate: the span's request made here (the rig mapped, the frames in the model's space), the network run on a
 * worker (ONNX Runtime loaded there on first use); poll_ai shows the result as a ghost. */
void Editor::ai_generate(int from, int to) {
  if (!assist_require(Assist::Ai)) return;
  if (playing_) return refuse("AI Inbetween: stop Play first", "stop", "Stop Play");
  if (ai_.job) return Log::warn("AI Inbetween: still working on the last span");
  ai::ModelInfo info;
  if (!ai_.rt && !ai::install_problem().empty()) return refuse("AI Inbetween needs its download (Preferences > Animation Assistants > Install)", "ai install", "Install");
  if (!ai_info(info)) return refuse("AI Inbetween: the install's inbetween.json can't be read - Remove and Install it again in Preferences > Animation Assistants", "preferences", "Open Preferences");
  GameObject *root = anim_root();
  AnimationClipPtr clip = anim_clip();
  const gait::Rig rig = walk_rig();
  if (!root || !clip) return Log::warn("AI Inbetween: select a character with a clip");
  if (from < 0 && !span_around(*clip, anim_.frame, from, to)) return Log::warn("AI Inbetween: no two keys around the playhead");
  ai::Span span;
  span.from = from, span.to = to, span.seed = ai_.seed;
  span.style = std::max(0, std::min((int)info.styles.size() - 1, ai_.style));
  auto job = std::make_shared<AiJob>();
  job->req = ai::make_request(*scene_, *root, rig, *clip, span, info);
  if (!job->req.problem.empty()) return Log::warn("AI Inbetween: %s", job->req.problem.c_str());
  job->clip_version = clip->version, job->clip = clip, job->root = root->id;
  job->rt = ai_.rt;
  if (ai_head_ready(*root)) job->head = ai_.head;
  ai_.job = job;
  std::thread([job] {
    const auto t0 = std::chrono::steady_clock::now();
    if (!job->rt) {
      std::unique_ptr<ai::Runtime> r = ai::load_runtime(ai::install_dir(), job->error);
      job->rt = std::shared_ptr<ai::Runtime>(std::move(r));
    }
    std::vector<float> hidden;
    const bool tuned = job->head.valid();
    if (job->rt && !job->rt->run(job->req.frames, job->req.known, job->req.span.style, job->req.W, job->result, job->error, tuned ? &hidden : nullptr))
      job->result.clear();
    if (tuned && !job->result.empty()) ai::apply_head(job->head, hidden, job->req.known, job->req.W, job->result);
    job->ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    job->done.store(true);
  }).detach();
  Log::info("AI Inbetween: frames %d to %d (%d made), %s...", from, to, to - from - 1, info.styles[(size_t)span.style].c_str());
}

/* Each frame: a finished span becomes the ghost (the clip with it, kept for Apply). */
void Editor::poll_ai() {
  poll_ai_tune();
  if (ai_.chore && ai_.chore->done.load()) {
    const std::shared_ptr<AiChore> c = std::move(ai_.chore);
    ai_.info_read = false;  // (an install or a check may have changed it)
    c->ok ? Log::info("AI Inbetweening: %s", c->message.c_str()) : Log::warn("AI Inbetweening: %s", c->message.c_str());
  }
  if (!ai_.job || !ai_.job->done.load()) return;
  const std::shared_ptr<AiJob> job = std::move(ai_.job);
  if (job->rt && !ai_.rt) ai_.rt = job->rt;  // (loaded once, kept)
  if (!assist_on(Assist::Ai)) return;  // (switched off meanwhile: nothing shown)
  if (job->result.empty()) return Log::warn("AI Inbetween: %s", job->error.c_str());
  GameObject *root = anim_root();
  AnimationClipPtr clip = anim_clip();
  if (!root || !clip || clip != job->clip.lock() || root->id != job->root || clip->version != job->clip_version)
    return Log::warn("AI Inbetween: the clip or the character changed while it worked - Generate again");
  std::string error;
  ai::ModelInfo info;
  ai_info(info);
  const gait::Rig rig = walk_rig();
  if (!ai::apply(*scene_, *root, rig, *clip, job->req, job->result, info, ai_.result, error, &ai_.poses))
    return Log::warn("AI Inbetween: %s", error.c_str());
  ai_.span = job->req.span, ai_.clip_version = clip->version, ai_.ghost = true, ai_.clip = clip, ai_.root = root->id;
  ui_.redraw = true;
  Log::info("AI Inbetween: %d frames made in %.0f ms - the ghost shows them; Apply writes them (Variation tries again differently)", job->req.clip_gap,
            job->ms);
}

/* The ghost at the playhead, inside the span. Stale (the keys changed since) it is drawn grey. */
void Editor::draw_ai_overlay() {
  if (!ai_.ghost || !assist_on(Assist::Ai)) return;
  const int i = (int)std::lround(anim_.frame) - ai_.span.from - 1;
  if (i < 0 || i >= (int)ai_.poses.size()) return;
  AnimationClipPtr clip = anim_clip();
  if (!ai_current(anim_root(), clip)) return;  // (another character selected: its ghost isn't this one's)
  const bool stale = clip->version != ai_.clip_version;
  for (const auto &b : ai_.poses[(size_t)i]) scene_r3d_.line(b.first, b.second, stale ? Color::hex(0x909090, 200) : Color::hex(0xB060E0, 235), false);
}

/* The Animation window's AI buttons (with the switch on): AI Inbetween, the style, then Variation and Apply. */
void Editor::draw_ai_toolbar(int &x, int y, int bh) {
  auto &u = ui_;
  auto button = [&](int w, const char *label, bool on, const char *tip) {
    const Recti b{x, y, w, bh};
    x += w + u.px(3);
    const bool hit = u.button(b, label, on);
    u.tooltip(tip);
    return hit;
  };
  const bool busy = (bool)ai_.job;
  if (button(u.px(92), busy ? "Working..." : "AI Inbetween", ai_.ghost || busy,
             "The motion between the keys around the playhead made by a network trained on motion capture (Cascadeur's AI\n"
             "Inbetweening), in the style picked beside it. Shown as a ghost; Apply writes it as keys - only that span changes.\n"
             "Needs its download (Preferences > Animation Assistants). The ... beside the style sets a span by frame numbers."))
    ai_command({"ai", ai_.ghost ? "off" : "span"});
  draw_ai_span_options(x, y, bh);  // (task 0288)
  ai::ModelInfo info;
  if (!ai_info(info)) return;
  std::vector<const char *> names;
  for (const std::string &s : info.styles) names.push_back(s.c_str());
  ai_.style = std::max(0, std::min((int)names.size() - 1, ai_.style));
  if (u.combo(u.id("ai_style"), {x, y, u.px(84), bh}, ai_.style, names.data(), (int)names.size()) && ai_.ghost) ai_command({"ai", "span", std::to_string(ai_.span.from), std::to_string(ai_.span.to)});
  x += u.px(88);
  if (ai_.tune) {
    const int total = std::max(1, ai_.tune->total.load());
    const std::string label = strprintf("Fitting %d%%", 100 * ai_.tune->progress.load() / total);
    if (button(u.px(92), label.c_str(), true, "Fitting the network to this character's clips: click to stop (the previous model stays).")) ai_finetune("stop");
  } else if (button(u.px(92), "Fit to Clips", false,
                    "Fine-tune on your own clips: every clip of this project that animates this character teaches the network's last layer\n"
                    "how this character moves (a few seconds to a minute, in the background). Saved as Assets/AI/<character>.aihead;\n"
                    "the ... options turn it off for the shipped model.")) {
    ai_finetune("");
  }
  if (!ai_.ghost) return;
  if (button(u.px(70), "Variation", false, "The same span made again a little differently (the frames before it taken faster or slower)."))
    ai_command({"ai", "variation"});
  if (button(u.px(54), "Apply", false, "Write the made span into the clip (one undo step). Every other key stays as it is.")) ai_apply();
}

/* Task 0288: AI Inbetween's span by frame numbers and the fitted model on or off - what `ai span from to` and
 * `ai finetune on|off` did - in a small popup beside the style. */
void Editor::draw_ai_span_options(int &x, int y, int bh) {
  auto &u = ui_;
  const Recti b{x, y, u.px(28), bh};
  x += b.w + u.px(3);
  const ui::Id id = u.id("ai_opts");
  if (u.record_buttons) u.button_rects["ai:options"] = b;
  if (u.button(b, "...")) u.open_popup(id, b);
  u.tooltip("AI Inbetween's options: a span by frame numbers, and whether the fit to your clips is used.");
  u.popup(id, u.px(260), [this] {
    auto &u = ui_;
    Recti r = u.popup_row(u.row_h() + u.px(4));
    const int half = (r.w - u.px(24)) / 2;
    u.label({r.x + u.px(8), r.y, u.px(40), r.h}, "From");
    u.int_field(u.id("ai_from"), {r.x + u.px(48), r.y + u.px(2), half - u.px(44), r.h - u.px(4)}, ai_from_, 0, 1000000);
    u.label({r.x + u.px(12) + half, r.y, u.px(24), r.h}, "To");
    u.int_field(u.id("ai_to"), {r.x + u.px(40) + half, r.y + u.px(2), half - u.px(36), r.h - u.px(4)}, ai_to_, 0, 1000000);
    u.tooltip("The keyed frames the made motion runs between (it changes only the frames inside); To at 0: the keys around the playhead (From unused).");
    if (ui_.menu_item("Make This Span", nullptr, false, ai_to_ > ai_from_ || ai_to_ == 0)) {
      ai_.seed = 0;
      ai_generate(ai_to_ > 0 ? ai_from_ : -1, ai_to_ > 0 ? ai_to_ : -1);
    }
    ui_.menu_separator();
    if (ui_.menu_item("Use the Fit to My Clips", nullptr, ai_.head_on)) ai_finetune(ai_.head_on ? "off" : "on");
    ui_.tooltip("Off: the shipped model alone, as downloaded; on: with this character's fit (Fit to Clips), where there is one.");
  });
}

/* Apply: the made span into the clip, one undo step (refused when the keys changed since: it would undo that edit). */
void Editor::ai_apply() {
  AnimationClipPtr clip = anim_clip();
  if (!ai_.ghost) return Log::warn("AI Inbetween: nothing to apply (Generate first)");
  if (!ai_current(anim_root(), clip)) return Log::warn("AI Inbetween: it was made for another character's clip - select it, or Generate again");
  if (clip->version != ai_.clip_version) return Log::warn("AI Inbetween: the keys changed since it was made - Generate again");
  anim_edit_keys("AI Inbetween", [&](AnimationClip &c) { c.tracks = ai_.result.tracks; });
  ai_.ghost = false, ai_.poses.clear();
  Log::info("AI Inbetween: frames %d to %d written as keys", ai_.span.from + 1, ai_.span.to - 1);
}

/* ai status | install [archive] | remove | check | span [from to] | style <name> | variation | apply | off | styles */
void Editor::ai_command(const std::vector<std::string> &t) {
  const std::string sub = t.size() > 1 ? to_lower(t[1]) : "status";
  if (sub == "status") {
    const std::string p = ai_.rt ? "" : ai::install_problem();
    return Log::info("AI Inbetweening: %s; the switch %s", p.empty() ? "installed" : p.c_str(), assist_on(Assist::Ai) ? "on" : "off");
  }
  if (sub == "install") return ai_install(t.size() > 2 ? t[2] : "");
  if (sub == "remove") {
    ai_.rt.reset(), ai_.info_read = false;
    std::string e;
    return ai::remove(e) ? Log::info("AI Inbetweening: removed") : Log::warn("AI Inbetweening: %s", e.c_str());
  }
  if (sub == "check")
    return ai_chore("checking it", [](std::string &m) {
      double ms = 0;
      if (!ai::probe(m, &ms)) return false;
      m = strprintf("a test of 60 frames in %.0f ms", ms);
      return true;
    });
  if (!assist_require(Assist::Ai)) return;
  ai::ModelInfo info;
  const bool have = ai_info(info);
  if (sub == "styles") return Log::info("AI Inbetween styles: %s", have ? list_of(info.styles).c_str() : "(install it first)");
  if (sub == "style" && t.size() > 2) {
    for (size_t i = 0; have && i < info.styles.size(); i++)
      if (info.styles[i] == to_lower(t[2])) return void(ai_.style = (int)i);
    return Log::warn("ai style <name>: %s", have ? list_of(info.styles).c_str() : "install it first");
  }
  if (sub == "span" && t.size() <= 3) ai_.seed = 0;  // (a new span: as the keys say; Variation varies it)
  if (sub == "span") return ai_generate(t.size() > 3 ? std::atoi(t[2].c_str()) : -1, t.size() > 3 ? std::atoi(t[3].c_str()) : -1);
  if (sub == "variation") {
    ai_.seed = ai_.seed * 1664525u + 1013904223u + 1u;
    return ai_generate(ai_.ghost ? ai_.span.from : -1, ai_.ghost ? ai_.span.to : -1);
  }
  if (sub == "apply") return ai_apply();
  if (sub == "finetune") return ai_finetune(t.size() > 2 ? to_lower(t[2]) : "");
  if (sub == "off") return void((ai_.ghost = false, ai_.poses.clear(), ui_.redraw = true));
  Log::warn("ai status | install [archive] | remove | check | span [from to] | style <name> | styles | variation | apply | off | finetune [stop|on|off]");
}

/* The character's fine-tuned correction: Assets/AI/<character>.aihead. */
std::string Editor::ai_head_path(const GameObject &root) const { return fs::join(fs::join(assets_dir_, "AI"), safe_name(root.name) + ".aihead"); }

/* The correction for `root` loaded (from its file, the first time) and switched on. */
bool Editor::ai_head_ready(const GameObject &root) {
  if (!ai_.head_on) return false;
  if (ai_.head_for != root.name) {
    std::string text;
    ai_.head = ai::Head();
    if (fs::read_file(ai_head_path(root), text)) ai::head_from_text(text, ai_.head);
    ai_.head_for = root.name;
  }
  return ai_.head.valid();
}

/* The fit, on its worker: ONNX Runtime loaded here when it isn't yet, every window of 10 to 45 frames (about 4,000 at
 * most, spread over the clips) through the network once, added to the sums, the correction solved, its error
 * measured from the sums. Nothing of a window is kept. */
void Editor::ai_fit(AiTune &t) {
  if (!t.rt) t.rt = std::shared_ptr<ai::Runtime>(ai::load_runtime(ai::install_dir(), t.error));
  if (!t.rt) return t.done.store(true);
  const ai::ModelInfo &info = t.rt->info;
  int frames = 0;
  for (const auto &c : t.clips) frames += c.second;
  const int stride = std::max(7, frames * 4 / 4000);
  std::vector<std::pair<size_t, ai::Span>> windows;
  for (size_t c = 0; c < t.clips.size(); c++)
    for (int gap : {10, 20, 30, 45})
      for (int from = info.ctx; from + gap + 1 < t.clips[c].second; from += stride) windows.push_back({c, ai::Span{from, from + gap + 1, t.style, 0}});
  t.total.store((int)windows.size());
  ai::Fit fit;
  fit.d = info.hidden;
  std::vector<float> out, hidden, truth;
  for (const auto &w : windows) {
    if (t.cancel.load()) break;
    const ai::Request req = ai::window_of(t.clips[w.first].first, t.clips[w.first].second, w.second, info, &truth);
    if (req.problem.empty() && t.rt->run(req.frames, req.known, w.second.style, req.W, out, t.error, &hidden)) fit.add(hidden, req.known, req.W, out, truth);
    t.progress.fetch_add(1);
  }
  if (!t.cancel.load() && fit.solve(0.05f, t.head)) {
    t.head.before = (float)fit.rms_before(), t.head.after = (float)fit.rms_after(t.head);  // (RMS: what the fit makes smallest)
  } else if (!t.cancel.load() && t.error.empty()) {
    t.error = "nothing to fit";
  }
  t.done.store(true);
}

/* ai finetune: the network fitted to this character's clips - every clip of the project that animates it, its frames
 * sampled here, then on a worker every window of 10 to 45 frames run through the network and the correction of its
 * last layer solved (task 0131). stop: the last correction stays; off / on: the shipped model alone, or with it. */
void Editor::ai_finetune(const std::string &sub) {
  GameObject *root = anim_root();
  if (sub == "stop") {
    if (ai_.tune) ai_.tune->cancel.store(true);
    return;
  }
  if (sub == "off" || sub == "on") {
    ai_.head_on = sub == "on";
    return Log::info("AI Inbetween: %s", ai_.head_on ? "fitted to your clips (where there is a fit)" : "the shipped model alone");
  }
  if (ai_.tune) return refuse("AI Inbetween: still fitting - the Fitting button stops it", "ai finetune stop", "Stop fitting");
  ai::ModelInfo info;
  if (!root || !ai_info(info)) return Log::warn("AI Inbetween: select a character (and install AI Inbetweening)");
  if (info.hidden <= 0) return Log::warn("AI Inbetween: this model can't be fitted (no last features: an older download)");
  const gait::Rig rig = walk_rig();
  auto tune = std::make_shared<AiTune>();
  int sampled = 0;
  for (const std::string &path : anim_clip_paths()) {
    if (tune->clips.size() >= 24 || sampled >= 6000) break;  // (enough to learn a habit: about three minutes; sampling is on this thread)
    AnimationClipPtr c = clip_asset(path);
    if (!c) continue;
    int hits = 0;
    for (const AnimTrack &t : c->tracks) hits += anim_target(*root, t.path) != nullptr;
    if (hits * 2 < (int)c->tracks.size() || c->tracks.empty()) continue;  // (another character's clip)
    int frames = 0;
    AnimationClip part = *c;  // (at most two minutes of it: sampling poses the scene here)
    part.length = std::min(part.end_frame(), 120.0f * std::max(1.0f, part.frame_rate));
    for (AnimTrack &tr : part.tracks)
      tr.keys.erase(std::remove_if(tr.keys.begin(), tr.keys.end(), [&](const AnimKey &k) { return k.frame > part.length; }), tr.keys.end());
    std::vector<float> rows = ai::clip_frames(*scene_, *root, rig, part, info, frames);
    if (frames > info.ctx + 12) sampled += frames, tune->clips.push_back({std::move(rows), frames});
  }
  if (tune->clips.empty()) return Log::warn("AI Inbetween: no clips of %s long enough to learn from", root->name.c_str());
  tune->rt = ai_.rt, tune->style = ai_.style, tune->path = ai_head_path(*root), tune->character = root->name;
  ai_.tune = tune;
  std::thread([tune] { ai_fit(*tune); }).detach();  // (the tune shared: it outlives the thread)
  Log::info("AI Inbetween: fitting to %zu clip%s of %s... (the Fitting button stops it)", tune->clips.size(), tune->clips.size() == 1 ? "" : "s", root->name.c_str());
}

/* A finished fit: kept and saved (a stopped or failed one changes nothing: the previous correction stays). */
void Editor::poll_ai_tune() {
  if (!ai_.tune || !ai_.tune->done.load()) return;
  const std::shared_ptr<AiTune> t = std::move(ai_.tune);
  if (t->rt && !ai_.rt) ai_.rt = t->rt;  // (loaded on its worker: kept)
  if (t->cancel.load()) return Log::info("AI Inbetween: fitting stopped - the previous model stays in use");
  if (!t->head.valid()) return Log::warn("AI Inbetween: the fit failed (%s)", t->error.c_str());
  fs::make_dirs(fs::parent(t->path));
  if (!fs::write_file(t->path, ai::head_text(t->head))) return Log::warn("AI Inbetween: can't write %s", t->path.c_str());
  ai_.head = t->head, ai_.head_for = t->character, ai_.head_on = true;
  Log::info("AI Inbetween: fitted to %s's clips on %d windows - their error %.4f before, %.4f now (saved as %s; ai finetune off: the shipped model)",
            t->character.c_str(), t->head.windows, t->head.before, t->head.after, fs::filename(t->path).c_str());
}

/* Installing: an archive given (checked by its .sha256 beside it when there is one), else the libraries release's. */
void Editor::ai_install(const std::string &archive) {
  if (ai::archive_name().empty()) return Log::warn("AI Inbetweening isn't built for this system");
  if (!archive.empty()) {
    ai_.rt.reset(), ai_.info_read = false;
    return ai_chore("installing " + fs::filename(archive), [archive](std::string &m) {
      std::string sums;
      if (fs::read_file(archive + ".sha256", sums) && releases::sum_for(sums, fs::filename(archive)) != net::sha256_file(archive))
        return m = fs::filename(archive) + " doesn't match its checksum", false;
      const std::string copy = fs::join(net::downloads_dir(), fs::filename(archive));  // (unpack removes the file it was given)
      if (!fs::copy_file(archive, copy)) return m = "can't copy " + archive, false;
      if (!ai::unpack(copy, m)) return false;
      m = "installed in " + ai::install_dir();
      return true;
    });
  }
  upd_.want_ai = true;
  start_update_check(false, false);
}

/* The archive is published: the AI dialog (else said once in the log). */
bool Editor::offer_ai() {
  const releases::Release *ex = extras_release();
  const std::string name = ai::archive_name();
  if (ex && !name.empty())
    for (const releases::Asset &a : ex->assets)
      if (a.name == name) {
        dialog_ = Dialog::Ai;
        return true;
      }
  Log::info("AI Inbetweening: %s isn't published yet (an archive built with tools/inbetween/package.py installs with `ai install <file>`)",
            name.c_str());
  return false;
}

/* "AI Inbetweening, an optional download": what it is, Download / Not now / Never ask. */
void Editor::draw_ai_dialog(const std::function<void()> &close) {
  auto &u = ui_;
  auto say = [&](const std::string &l) {
    Recti row = u.popup_row();
    u.label({row.x + u.px(12), row.y, row.w - u.px(20), row.h}, l);
  };
  const releases::Release *ex = extras_release();
  const releases::Asset *a = nullptr, *sum = nullptr;
  for (const releases::Asset &x : ex ? ex->assets : std::vector<releases::Asset>()) {
    if (x.name == ai::archive_name()) a = &x;
    if (x.name == ai::archive_name() + ".sha256") sum = &x;
  }
  if (!a || !sum) return close();
  say("AI Inbetweening fills the motion between two keys with a small network trained on motion capture (CMU's),");
  say("as Cascadeur's does. It runs on ONNX Runtime (Microsoft's, MIT), downloaded with the model.");
  say(strprintf("It is optional: animating works the same without it. Download it (%.0f MB)?", a->size / 1048576.0));
  if (draw_update_progress()) return;
  Recti b = u.popup_row(u.row_h() + u.px(10));
  if (u.button({b.right() - u.px(330), b.y + u.px(4), u.px(110), b.h - u.px(8)}, "Download")) download_verified(*a, *sum, false, false, true);
  if (u.button({b.right() - u.px(212), b.y + u.px(4), u.px(90), b.h - u.px(8)}, "Not now")) return close();
  if (!u.button({b.right() - u.px(114), b.y + u.px(4), u.px(102), b.h - u.px(8)}, "Never ask")) return;
  ai_.never_ask = true;
  save_prefs();
  close();
}

/* Preferences > Animation Assistants: the AI download's state, Install / Remove. */
void Editor::draw_ai_settings(const Recti &r) {
  auto &u = ui_;
  const std::string p = ai::install_problem();
  u.label({r.x + u.px(372), r.y, std::max(0, r.w - u.px(510)), r.h}, p.empty() ? "installed" : p,
          p.empty() ? u.theme.text : u.theme.text_dim);
  u.tooltip("The network and ONNX Runtime, in " + ai::install_dir() + ".");
  const Recti btn{r.right() - u.px(130), r.y + u.px(2), u.px(118), r.h - u.px(4)};
  if (p.empty() && u.button(btn, "Remove")) ai_command({"ai", "remove"});
  else if (!p.empty() && u.button(btn, "Install...")) ai_install("");
  /* Task 0288: an archive already downloaded (another PC, a slow line), and a check of the installed one - on a row of
   * their own, under the state they would cover. */
  const Recti r2 = u.popup_row(u.row_h() + u.px(4));
  const int right = p.empty() ? r2.right() - u.px(88) : r2.right() - u.px(12);
  u.label({r2.x + u.px(160), r2.y, u.px(120), r2.h}, "From an archive", u.theme.text_dim);
  std::string &text = picker_text_["pref:aiarchive"];
  const std::vector<platform::FileFilter> filters = {{"AI archives", {".zip", ".tar.gz", ".tgz"}}};
  if (path_picker_field({r2.x + u.px(284), r2.y + u.px(2), right - r2.x - u.px(284), r2.h - u.px(4)}, u.id("pref_aiarchive"), "pref:aiarchive", text, PickKind::Open, filters,
                        [this](const std::string &f) { ai_install(f); }) &&
      !text.empty())
    ai_install(text);
  u.tooltip("Install the AI download from an archive you have (its .sha256 beside it is checked when there is one): its path, or ... to choose it.");
  if (!p.empty()) return;
  const Recti check{r2.right() - u.px(82), r2.y + u.px(2), u.px(70), r2.h - u.px(4)};
  if (u.record_buttons) u.button_rects["pref:AI Check"] = check;
  if (u.button(check, "Check")) ai_command({"ai", "check"});
  u.tooltip("Run the network once on a test motion: says whether it works here, and how long it took.");
}

/* Turned on for the first time and not installed: the download offered, once (never at startup). */
void Editor::ai_switched_on() {
  if (ai_.never_ask || ai_.asked || headless_ || ai::install_problem().empty()) return;
  ai_.asked = true;
  save_prefs();
  upd_.want_ai = true;
  start_update_check(false, false);
}

bool Editor::ai_pref(const std::string &key, const std::string &value) {
  if (key == "ai_never_ask") ai_.never_ask = value == "1";
  else if (key == "ai_asked") ai_.asked = value == "1";
  else return false;
  return true;
}

std::string Editor::ai_prefs_text() const { return strprintf("ai_never_ask=%d\nai_asked=%d\n", ai_.never_ask ? 1 : 0, ai_.asked ? 1 : 0); }

}  // namespace bl
