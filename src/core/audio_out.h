// SPDX-License-Identifier: GPL-2.0-or-later
// Sound out to the default device (round 365, task 0342): a stereo float stream fed by a callback on the stream's
// own thread - WASAPI in shared mode on Windows (as Blender's audaspace does through its WASAPI device), PulseAudio's
// simple API loaded at run time on Linux (no link-time dependency: a machine without it just has no sound), none yet
// on macOS. The Sequencer's sound plays through it.
#pragma once

#include <functional>
#include <memory>
#include <string>

namespace bl::audio_out {

/* Fills `frames` stereo frames (2 floats each, -1..1) at the stream's rate. Called on the stream's thread. */
using Fill = std::function<void(float *out, size_t frames)>;

class Stream {
 public:
  Stream();
  ~Stream();
  Stream(const Stream &) = delete;
  Stream &operator=(const Stream &) = delete;
  /* The default device opened at `rate` (stereo float) with `fill` feeding it; false with why (no device, no library,
   * macOS). About 40 ms of sound is queued ahead. */
  bool open(int rate, Fill fill, std::string *why);
  void close();  // (stops the thread; safe when not open)
  bool is_open() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace bl::audio_out
