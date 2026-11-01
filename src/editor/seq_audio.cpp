// SPDX-License-Identifier: GPL-2.0-or-later
// The Sequencer's sound (round 365, task 0342): Audio bars played through the default device in step with the
// playhead (the window's Preview playing, or the Sequencer component in Play), and mixed into Render Animation's
// frames as sound.wav beside them and an AAC stream in the MP4. The mixing is scene/audio's.
#include "editor.h"

#include "../core/audio_out.h"
#include "../scene/audio.h"
#include "../scene/sequence.h"
#include "../core/core.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mutex>

namespace bl {

namespace {
constexpr int kSoundRate = 48000;
}  // namespace

/* What the stream's thread mixes: a copy of the sequence and its sounds, taken on the main thread whenever the
 * playhead jumps, the sequence changes or the sound drifts from the picture. */
struct Editor::SeqAudio {
  audio_out::Stream stream;
  std::mutex m;
  Sequence seq;
  SoundSet sounds;
  std::string path;
  uint64_t version = 0;
  double pos = 0.0;   // the timeline frame the next sample plays at
  double step = 1.0;  // timeline frames a frame of real time (Play's Speed)
  bool active = false;
  std::vector<float> buf;
  /* The main thread's own: */
  float last_frame = -1e9f;  // the frame at the last look (whether the picture moves forward)
  double idle_since = 0.0;   // the last time a sound was wanted
  double retry_at = 0.0;     // when to try the device again after it failed or went away
  bool said = false;         // (why there's no sound, said once)
  ~SeqAudio() { stream.close(); }  // (the thread stopped before the mutex and the copies it reads go)

  /* Whether the picture moved forward since the last look. Paused, held, Speed 0, a backward Speed and Ping Pong's way
   * back are silent (review 365: the sound ran forward over them and was put back every fifth of a second); a jump
   * back of more than half a second is a loop's wrap or a scrub, forward again from there. */
  bool forward(float frame, double fps) {
    const double d = (double)frame - last_frame;
    last_frame = frame;
    return d > 1e-4 || d < -0.5 * fps;
  }

  /* The mixer's copy brought up to date: the sequence and its sounds copied outside the lock when they changed (an
   * edit keeps the sound where it is - review 365's stutter while dragging Volume); the position put back only on a
   * start, another sequence, or a drift past a fifth of a second. `seq` null: silent. */
  void update(const Sequence *seq, float frame, double step, double fps) {
    if (!seq) {
      std::lock_guard<std::mutex> lock(m);
      active = false;
      return;
    }
    const bool changed = path != seq->asset_path || version != seq->version;
    Sequence copy;
    SoundSet loaded;
    if (changed) copy = *seq, loaded = sequence_sounds(*seq);
    std::lock_guard<std::mutex> lock(m);
    if (changed) seq_swap(copy, loaded);
    const bool jump = !active || path != seq->asset_path || std::fabs(pos - frame) > 0.2 * fps;
    path = seq->asset_path, version = seq->version;
    if (jump) pos = frame;
    this->step = step;
    active = true;
  }
  void seq_swap(Sequence &copy, SoundSet &loaded) {
    std::swap(this->seq, copy);
    std::swap(sounds, loaded);
  }

  /* The device: opened when a sound is wanted (again a few seconds after a failure or a lost device), closed after
   * three seconds without one (review 365: a running stream kept Windows from sleeping). */
  void device(bool want, bool allowed, double now) {
    if (want) idle_since = now;
    if (stream.is_open() && now - idle_since > 3.0) stream.close();
    if (!want || !allowed || stream.is_open() || now < retry_at) return;
    std::string why;
    if (stream.open(kSoundRate, [this](float *out, size_t n) { fill(out, n); }, &why)) return;
    retry_at = now + 5.0;
    if (!said) Log::warn("Sequencer: no sound out (%s); the sound is still in Render Animation's video", why.c_str());
    said = true;
  }

  void fill(float *out, size_t frames) {
    std::lock_guard<std::mutex> lock(m);
    if (!active) return (void)std::memset(out, 0, frames * 2 * sizeof(float));
    mix_sequence_audio(seq, sounds, pos, frames, kSoundRate, buf, step);
    std::memcpy(out, buf.data(), frames * 2 * sizeof(float));
    pos += (double)frames * (seq.frame_rate > 0.0f ? seq.frame_rate : 60.0) / kSoundRate * step;
  }
};

/* Play's Sequencer that is playing (the director first), or nullptr. */
Sequencer *Editor::playing_sequencer() {
  if (!playing_) return nullptr;
  Sequencer *c = scene_->director ? static_cast<Sequencer *>(scene_->director.get()) : nullptr;
  if (c && !c->playing) c = nullptr;
  scene_->for_each([&](GameObject &g) {
    Sequencer *q = g.get<Sequencer>();
    c = c || !q || !q->playing ? c : q;
  });
  return c;
}

/* The frame the sound should be at now, and the sequence: Play's playing Sequencer, else the window's Preview while it
 * plays; -1 when nothing plays (or Play's sequence has run to its end and holds there). */
float Editor::seq_audio_frame(SequencePtr &seq, double &step) {
  step = 1.0;
  seq = nullptr;
  Sequencer *c = paused_ ? nullptr : playing_sequencer();  // (review 365: paused Play is silent)
  if (playing_ && !c) return -1.0f;
  if (c) {
    seq = sequence_asset(c->sequence);
    step = std::isfinite(c->speed) ? std::fabs(c->speed) : 1.0;
    const float f = seq ? c->frame() : -1.0f;
    return seq && f < seq->end_frame() - 1.0f ? f : -1.0f;
  }
  if (playing_ || !seqr_.playing || !seqr_.preview) return -1.0f;
  seq = seq_sequence();
  return seq ? seqr_.frame : -1.0f;
}

/* Once a frame: the sound follows the picture - started, stopped, and put back at the playhead when it jumps or
 * drifts more than a fifth of a second (a loop's wrap, a scrub, a trigger's jump). */
void Editor::seq_audio_tick() {
  SequencePtr seq;
  double step = 1.0;
  const float frame = seq_audio_frame(seq, step);
  const bool sounding = frame >= 0.0f && seq && sequence_has_audio(*seq);
  if (!sounding && !seq_audio_) return;
  if (!seq_audio_) seq_audio_ = std::make_shared<SeqAudio>();
  SeqAudio &a = *seq_audio_;
  const double fps = seq && seq->frame_rate > 0.0f ? seq->frame_rate : 60.0;
  const bool want = sounding && a.forward(frame, fps);
  if (!sounding) a.last_frame = -1e9f;
  a.update(want ? seq.get() : nullptr, frame, step, fps);
  a.device(want, !headless_ && !std::getenv("BLENDITY_NO_AUDIO"), now_seconds());
}

bool Editor::seq_audio_for_test(double &pos, bool &active) {
  if (!seq_audio_) return false;
  std::lock_guard<std::mutex> lock(seq_audio_->m);
  pos = seq_audio_->pos, active = seq_audio_->active;
  return true;
}

void Editor::seq_audio_pull_for_test(float *out, size_t frames) {
  if (seq_audio_) seq_audio_->fill(out, frames);
}

/* Render Animation's sound: the sequence's Audio bars over the frames written, at the video's rate (a sequence at
 * 60 fps rendered at 30 plays its timeline at half speed, and so does its sound), as sound.wav beside the frames.
 * `out` empty when the frames aren't the Sequencer's or it has no sound; true when sound.wav was written. */
bool Editor::render_animation_sound(std::vector<float> &out) {
  out.clear();
  SequencePtr seq = ranim_.source == 1 ? seq_sequence() : nullptr;
  if (!seq || !sequence_has_audio(*seq) || ranim_.written.empty()) return false;
  const double fps = std::isfinite(ranim_.fps) && ranim_.fps > 0.0f ? ranim_.fps : 30.0;
  const double seq_fps = seq->frame_rate > 0.0f ? seq->frame_rate : 60.0;
  const size_t count = (size_t)std::llround((double)ranim_.written.size() / fps * kSoundRate);
  mix_sequence_audio(*seq, sequence_sounds(*seq), ranim_.first, count, kSoundRate, out, fps / seq_fps);
  const std::string wav = fs::join(ranim_.dir, "sound.wav");
  if (!fs::write_file(wav, encode_wav16(out, kSoundRate))) return Log::warn("Render Animation: could not write %s", wav.c_str()), false;
  Log::info("Render Animation: the sound is in %s", wav.c_str());
  return true;
}

}  // namespace bl
