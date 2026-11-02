// SPDX-License-Identifier: GPL-2.0-or-later
// Sound for the Sequencer (round 365, task 0342): WAV files decoded in-house (PCM 8 / 16 / 24 / 32-bit and 32-bit float,
// mono or stereo), the timeline's Audio bars mixed into stereo floats at any span of frames - what plays in sync with
// the playhead and what Render Animation muxes into its video.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace bl {

struct Sequence;

/* A decoded sound: interleaved stereo floats (-1..1) at `rate` samples a second. */
struct AudioClip {
  int rate = 48000;
  std::vector<float> stereo;  // L R L R ...
  size_t frames() const { return stereo.size() / 2; }
  double seconds() const { return rate > 0 ? (double)frames() / rate : 0.0; }
};
using AudioClipPtr = std::shared_ptr<const AudioClip>;

/* A WAV's bytes decoded (RIFF/WAVE, "fmt " PCM 1 or IEEE float 3 or WAVE_FORMAT_EXTENSIBLE of either; 1 or 2 channels;
 * more channels: the first two). False with why. */
bool decode_wav(const std::string &bytes, AudioClip &out, std::string &why);
/* A stereo float buffer as a 16-bit PCM WAV's bytes (Render Animation's sound beside PNG frames). */
std::string encode_wav16(const std::vector<float> &stereo, int rate);

/* A sound file, decoded and kept by its path - read again when the file's time or size changes (missing or
 * unreadable: nullptr, said once a change). Main thread only. */
AudioClipPtr audio_asset(const std::string &path);
void clear_audio_assets();

/* The sounds a sequence's Audio bars play, by path: loaded on the main thread, then mixed on any (the mixer reads
 * nothing else that's shared). */
using SoundSet = std::map<std::string, AudioClipPtr>;
SoundSet sequence_sounds(const Sequence &s);
void trim_audio_assets(const SoundSet &in_use);

/* The sequence's Audio bars mixed into `out` (interleaved stereo, `count` sample frames at `rate`) starting at timeline
 * frame `from` (the sequence's frame rate), `step` timeline frames a second of output (1: real time): each bar's sound
 * from its trim_in (in timeline frames into the sound), at its volume, resampled linearly to `rate` and summed, clipped
 * to -1..1. */
void mix_sequence_audio(const Sequence &s, const SoundSet &sounds, double from, size_t count, int rate, std::vector<float> &out,
                        double step = 1.0);
/* The sound's length in the sequence's frames (0 when it can't be read). */
float audio_frames(const std::string &path, float frame_rate);
/* Whether the sequence has a sound to play. */
bool sequence_has_audio(const Sequence &s);

}  // namespace bl
