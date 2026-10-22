// SPDX-License-Identifier: GPL-2.0-or-later
// Render > Render Animation (task 0074): every frame of the scene's sequence (or the selected object's clip) through
// the F12 render, as numbered PNGs in Renders/<scene>_<time>/, then an MP4 (H.264) made by FFmpeg when there is one:
// the program set in Preferences or found on the PATH. Blender renders an animation the same way (Ctrl+F12) and
// writes video through FFmpeg too; Blendity doesn't link FFmpeg, it runs it, so without it the frames are the result.
#include "editor.h"
#include "../render/canvas.h"
#include "../render/video_encode.h"

#include <cmath>
#include <ctime>
#include <chrono>
#include <thread>

namespace bl {

/* FFmpeg's arguments for numbered PNGs (0001.png...) in `dir` to an H.264 MP4 any player opens (yuv420p needs an even
 * size: an odd one is padded by a pixel). */
std::vector<std::string> ffmpeg_png_to_mp4_args(const std::string &ffmpeg, const std::string &dir, float fps, const std::string &out) {
  return {ffmpeg, "-y", "-nostdin", "-loglevel", "error", "-framerate", strprintf("%g", fps), "-start_number", "1", "-i", fs::join(dir, "%04d.png"),
          "-c:v", "libx264", "-pix_fmt", "yuv420p", "-vf", "pad=ceil(iw/2)*2:ceil(ih/2)*2", out};
}

/* The FFmpeg to run: Preferences' path when it's there, else one on the PATH; "" for none. Looked for again when a
 * render starts or the path is set (a PATH search stats every folder in it: not every frame). */
std::string Editor::ffmpeg_program() {
  if (ffmpeg_looked_) return ffmpeg_found_;
  ffmpeg_looked_ = true;
  ffmpeg_found_ = !ffmpeg_path_.empty() && fs::exists(ffmpeg_path_) ? ffmpeg_path_ : sys::find_program("ffmpeg");
  return ffmpeg_found_;
}

/* A frame rate from a file: 30 when it isn't a usable one. */
static float usable_fps(float fps) { return std::isfinite(fps) && fps >= 1.0f && fps <= 1000.0f ? fps : 30.0f; }

/* What the animation's frames come from: the scene's sequence, else the selected object's clip. Its last frame and
 * frame rate; false when there is neither. */
bool Editor::render_animation_source(int &source, int &last, float &fps) {
  SequencePtr s = seq_sequence();
  if (s && s->end_frame() >= 1.0f) {  // (an empty sequence gives way to the selected clip)
    source = 1, last = (int)std::floor(s->end_frame()), fps = usable_fps(s->frame_rate);
    return true;
  }
  AnimationClipPtr clip = anim_clip();
  if (!clip || !anim_root()) return false;
  source = 2, last = (int)std::floor(clip->end_frame()), fps = usable_fps(clip->frame_rate);
  return true;
}

static std::string render_folder_name(const std::string &scene_name) {
  std::time_t t = std::time(nullptr);
  char buf[64];
  std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", std::localtime(&t));
  std::string scene = scene_name.empty() ? std::string("Scene") : scene_name;
  for (char &c : scene)
    if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') c = '_';
  return scene + "_animation_" + buf;
}

/* Starts rendering frames `first` to `last` (-1: the source's whole length) at `fps` (0: the source's). */
bool Editor::start_render_animation(int first, int last, float fps) {
  if (playing_) {
    Log::warn("Render Animation: Stop Play first (it poses the scene frame by frame)");
    return false;
  }
  int source = 0, src_last = 0;
  float src_fps = 30.0f;
  if (!render_animation_source(source, src_last, src_fps)) {
    Log::warn("Render Animation: the scene has no sequence and the selection no clip (Window > Sequencer or Animation)");
    return false;
  }
  stop_render_animation();
  stop_render_sequence();
  RenderAnimation was = std::move(ranim_);
  ranim_ = RenderAnimation{};
  ranim_.encode = was.encode, ranim_.encode_video = was.encode_video;  // (a video still being made keeps reporting)
  ranim_.source = source;
  ranim_.first = std::max(0, first);
  ranim_.last = last < 0 ? src_last : std::max(ranim_.first, last);
  ranim_.fps = fps > 0.0f && std::isfinite(fps) ? fps : src_fps;
  ranim_.frame = ranim_.first;
  const std::string renders = fs::join(project_root_, "Renders");
  std::string name = render_folder_name(scene_->name);
  for (int k = 2; fs::exists(fs::join(renders, name)) && k < 100; k++) name = render_folder_name(scene_->name) + strprintf("_%d", k);
  ranim_.dir = fs::join(renders, name);
  ranim_.video = fs::join(renders, name + ".mp4");
  fs::make_dirs(ranim_.dir);
  if (source == 1) seq_set_preview(true);
  else anim_set_preview(true);
  GameObject *root = anim_root();
  ranim_.root = source == 2 && root ? root->id : 0;
  ffmpeg_looked_ = false;
  ranim_.active = true;
  dock_open(WindowKind::Render);
  Log::info("Render Animation: frames %d to %d at %g fps into %s", ranim_.first, ranim_.last, ranim_.fps, ranim_.dir.c_str());
  step_render_animation();
  return true;
}

/* Stopped (Stop, a new one, something that ended the preview it poses through) or finished: the pose shown goes back;
 * the frames written stay. */
void Editor::stop_render_animation() {
  if (!ranim_.active) return;
  ranim_.active = false;
  if (rendering_) rendering_ = false;
  final_pt_.pause();  // (task 0161: the cards stop too)
  if (ranim_.source == 1) seq_set_preview(false);
  else anim_set_preview(false);
  invalidate_project_listing();
}

/* The frame `ranim_.frame` posed and rendered: its file name in the folder. */
static std::string frame_file(const std::string &dir, int index) { return fs::join(dir, strprintf("%04d.png", index)); }

/* One frame a tick: pose it, render it (at once for the rasterizer, over a few ticks for the path tracer), save it. */
void Editor::step_render_animation() {
  poll_render_video();
  if (!ranim_.active) return;
  if (const char *why = render_animation_broken()) {
    Log::warn("Render Animation stopped after %zu frame(s): %s", ranim_.written.size(), why);
    return stop_render_animation();
  }
  if (ranim_.frame > ranim_.last) return finish_render_animation();
  if (!ranim_.started) {
    /* Held frames (task 0149): the sequence's or the clip's root's Hold, as Play shows them. */
    if (ranim_.source == 1) {
      int hold = 1;
      scene_->for_each([&](GameObject &g) {
        if (const Sequencer *sq = g.get<Sequencer>(); sq && sq->enabled && hold == 1) hold = sq->hold;
      });
      seq_set_frame(hold_frame_from((float)ranim_.first, (float)ranim_.frame, hold));
    }
    else {
      GameObject *root = anim_root();
      const Animation *an = root ? root->get<Animation>() : nullptr;
      anim_set_frame(hold_frame_from((float)ranim_.first, (float)ranim_.frame, an ? an->hold : 1));
    }
    start_final_render(false, false);
    ranim_.started = true;
  }
  const int n = ranim_.last - ranim_.first + 1, k = ranim_.frame - ranim_.first + 1;
  if (rendering_) {
    render_status_ = strprintf("Animation frame %d / %d  |  %s", k, n, render_status_.c_str());
    return;
  }
  const std::string path = frame_file(ranim_.dir, k);
  if (!write_png(path, render_img_.pixels.data(), render_img_.width, render_img_.height, render_img_.width, render_transparent_)) {
    Log::error("Render Animation: could not write %s; stopped (a gap would end the video there)", path.c_str());
    return stop_render_animation();
  }
  ranim_.written.push_back(path);
  render_status_ = strprintf("Animation frame %d / %d", k, n);
  ranim_.frame++;
  ranim_.started = false;
  ui_.redraw = true;
}

/* Why the frames can't go on as they started: Play, the preview turned off (a save, an undo, another scene, another
 * clip), another object selected for a clip. nullptr while all is well. */
const char *Editor::render_animation_broken() {
  if (playing_) return "Play started";
  if (ranim_.source == 1 && !seqr_.preview) return "the Sequencer's Preview ended (a save, an undo, another scene)";
  if (ranim_.source == 2 && !anim_.preview) return "the Animation window's Preview ended (a save, an undo, another scene)";
  GameObject *root = ranim_.source == 2 ? anim_root() : nullptr;
  if (ranim_.source == 2 && (!root || root->id != ranim_.root)) return "another object was selected";
  return nullptr;
}

/* All frames written: the pose goes back, and FFmpeg (if there is one) makes the video in the background. */
void Editor::finish_render_animation() {
  const size_t frames = ranim_.written.size();
  stop_render_animation();
  render_status_ = strprintf("Animation: %zu frame(s) in %s", frames, ranim_.dir.c_str());
  Log::info("%s", render_status_.c_str());
  if (!frames) return;
  if (video::available()) return encode_render_video();
  const std::string ffmpeg = ffmpeg_program();
  if (ffmpeg.empty()) {
    Log::info("Render Animation: no FFmpeg, so the frames are the result. For an MP4, install FFmpeg (ffmpeg.org) on the "
              "PATH or set its path in Preferences, or open the frames in Blender's Video Sequencer.");
    return;
  }
  auto job = std::make_shared<VideoJob>();
  ranim_.encode = job;
  ranim_.encode_video = ranim_.video;
  const std::vector<std::string> args = ffmpeg_png_to_mp4_args(ffmpeg, ranim_.dir, ranim_.fps, ranim_.video);
  std::thread([args, job] { job->code.store(sys::run_program(args)); }).detach();
  render_status_ += "  |  making the MP4...";
  Log::info("Render Animation: FFmpeg is making %s", ranim_.video.c_str());
}

/* Built with FFmpeg's libraries (task 0077): the MP4 is encoded here, on a background thread, as Blender does. */
void Editor::encode_render_video() {
  auto job = std::make_shared<VideoJob>();
  ranim_.encode = job;
  ranim_.encode_video = ranim_.video;
  const std::vector<std::string> frames = ranim_.written;
  const std::string out = ranim_.video;
  const float fps = ranim_.fps;
  video::init();  // (FFmpeg's log, quieted from this thread)
  std::thread([frames, out, fps, job] {
    std::string error;
    const bool ok = video::encode_png_frames(frames, fps, out, &error, &job->cancel);
    job->error = error;  // (before the code: the main thread reads it once the code is set)
    job->code.store(ok ? 0 : 1);
  }).detach();
  render_status_ += "  |  making the MP4...";
  Log::info("Render Animation: %s is making %s", video::description().c_str(), out.c_str());
}

/* Closing with an MP4 still being encoded in here: it stops at the next frame (and deletes its unfinished file)
 * before the program's statics go. An ffmpeg program finishes on its own. */
void Editor::cancel_render_video() {
  if (!ranim_.encode || !video::available()) return;
  ranim_.encode->cancel.store(true);
  const double t0 = now_seconds();
  while (ranim_.encode->code.load() == kVideoRunning && now_seconds() - t0 < 10.0) std::this_thread::sleep_for(std::chrono::milliseconds(20));
  Log::warn("Render Animation: closed while making %s; it was stopped", ranim_.encode_video.c_str());
}

/* The background FFmpeg done: say so (the file is checked, not only the exit code). */
void Editor::poll_render_video() {
  if (!ranim_.encode) return;
  const int code = ranim_.encode->code.load();
  if (code == kVideoRunning) return;
  const std::string why = ranim_.encode->error;
  ranim_.encode.reset();
  const std::string &video = ranim_.encode_video;
  const bool ran = code == 0 || code == sys::kExitUnknown;  // (unknown: on Linux once open_external reaps children itself)
  bool written = false;
  for (const DirEntry &de : fs::list(fs::parent(video))) written = written || (de.name == fs::filename(video) && de.size > 0);
  if (ran && written) {
    render_status_ = "Animation: " + video;
    Log::info("Render Animation: %s", video.c_str());
  }
  else if (!why.empty()) Log::error("Render Animation: couldn't make %s (%s); the frames stay", video.c_str(), why.c_str());
  else Log::error("Render Animation: FFmpeg couldn't make %s (exit code %d); the frames stay", video.c_str(), code);
  invalidate_project_listing();
  ui_.redraw = true;
}

/* renderanim [first last] [fps] | stop | ffmpeg [path]: the console's Render Animation. */
void Editor::render_animation_command(const std::vector<std::string> &t) {
  const std::string verb = t.size() > 1 ? to_lower(t[1]) : "";
  if (verb == "stop") return stop_render_animation();
  if (verb == "ffmpeg") return set_ffmpeg_path(t.size() > 2 ? t[2] : "");
  const bool numbers = t.size() == 1 || (t.size() >= 3 && std::isdigit((unsigned char)t[1][0]) && std::isdigit((unsigned char)t[2][0]));
  if (!numbers) return Log::warn("renderanim [first last] [fps] | stop | ffmpeg [path]");
  const int first = t.size() > 2 ? std::atoi(t[1].c_str()) : -1, last = t.size() > 2 ? std::atoi(t[2].c_str()) : -1;
  const float fps = t.size() > 3 ? (float)std::atof(t[3].c_str()) : 0.0f;
  start_render_animation(first, last, fps);
}

/* Preferences' FFmpeg (empty: look on the PATH). */
void Editor::set_ffmpeg_path(const std::string &path) {
  ffmpeg_path_ = path;
  ffmpeg_looked_ = false;
  save_prefs();
  const std::string found = ffmpeg_program();
  if (found.empty()) Log::warn("FFmpeg: none found%s; Render Animation writes PNG frames only", path.empty() ? " on the PATH" : (" at " + path).c_str());
  else Log::info("FFmpeg: %s", found.c_str());
}

/* The Render window's Render Animation button (Stop while it runs); its tooltip says what makes the video. */
void Editor::draw_render_animation_button(ui::Layout &lay) {
  auto &u = ui_;
  Recti r = lay.row(u.row_h() + u.px(6));
  if (ranim_.active) {
    const int n = ranim_.last - ranim_.first + 1, k = std::min(n, ranim_.frame - ranim_.first + 1);
    if (u.button(r, strprintf("Stop Render Animation (%d / %d)", k, n), true, ui::Icon::Pause)) stop_render_animation();
    return;
  }
  if (u.button(r, "Render Animation (Ctrl+F12)", false, ui::Icon::Camera)) start_render_animation();
  if (!u.hovered(r)) return;  // (the PATH is searched only for the tooltip)
  const std::string ffmpeg = ffmpeg_program();
  u.tooltip("Every frame of the scene's sequence (or the selected object's clip) through this render, as\n"
            "Renders/<scene>_animation_<time>/0001.png..., then an MP4 next to them " +
            (ffmpeg.empty() ? std::string("once FFmpeg is installed (none found: frames only).") : "made by " + ffmpeg + "."));
}

}  // namespace bl
