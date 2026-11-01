// SPDX-License-Identifier: GPL-2.0-or-later
// Sound for the Sequencer (round 365, task 0342): see audio.h.
#include "audio.h"

#include "sequence.h"
#include "material.h"
#include "../core/core.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <new>
#include <set>

namespace bl {

namespace {

uint32_t le32(const std::string &b, size_t at) { return (uint32_t)(uint8_t)b[at] | (uint32_t)(uint8_t)b[at + 1] << 8 | (uint32_t)(uint8_t)b[at + 2] << 16 | (uint32_t)(uint8_t)b[at + 3] << 24; }
uint16_t le16(const std::string &b, size_t at) { return (uint16_t)((uint8_t)b[at] | (uint8_t)b[at + 1] << 8); }

/* One sample at `p` of `bits` (and `is_float`) as -1..1. */
float sample_at(const uint8_t *p, int bits, bool is_float) {
  if (is_float && bits == 32) {
    float f;
    std::memcpy(&f, p, 4);
    return std::isfinite(f) ? std::clamp(f, -1.0f, 1.0f) : 0.0f;
  }
  if (bits == 8) return ((int)p[0] - 128) / 128.0f;
  if (bits == 16) return (int16_t)(p[0] | p[1] << 8) / 32768.0f;
  if (bits == 24) return (int32_t)((uint32_t)p[0] << 8 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 24) / 2147483648.0f;
  if (bits == 32) return (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24) / 2147483648.0f;
  return 0.0f;
}

struct WavFormat {
  int channels = 0, rate = 0, bits = 0;
  bool is_float = false;
};

/* The "fmt " chunk's format; false when it isn't one this reads. */
bool read_format(const std::string &b, size_t at, uint32_t size, WavFormat &f, std::string &why) {
  if (size < 16) return why = "a short fmt chunk", false;
  uint16_t tag = le16(b, at);
  f.channels = le16(b, at + 2), f.rate = (int)le32(b, at + 4), f.bits = le16(b, at + 14);
  if (tag == 0xFFFE && size >= 26) tag = le16(b, at + 24);  // WAVE_FORMAT_EXTENSIBLE: the sub-format's first two bytes
  f.is_float = tag == 3;
  if (tag != 1 && tag != 3) return why = "not PCM or float (compressed WAV)", false;
  if (f.is_float && f.bits != 32) return why = "float samples other than 32-bit", false;  // (review 365: read as PCM they were noise)
  if (f.channels < 1 || f.rate < 1000 || f.rate > 384000) return why = "an unusable channel count or rate", false;
  if (f.bits != 8 && f.bits != 16 && f.bits != 24 && f.bits != 32) return why = "an unusable sample size", false;
  return true;
}

}  // namespace

bool decode_wav(const std::string &b, AudioClip &out, std::string &why) {
  if (b.size() < 12 || b.compare(0, 4, "RIFF") != 0 || b.compare(8, 4, "WAVE") != 0) return why = "not a WAV file", false;
  WavFormat f;
  bool have_format = false;
  for (size_t at = 12; at + 8 <= b.size();) {
    const std::string id = b.substr(at, 4);
    const uint32_t size = le32(b, at + 4);
    const size_t body = at + 8;
    if (body + size > b.size() && id != "data") return why = "a chunk past the file's end", false;
    if (id == "fmt ") {
      if (!read_format(b, body, size, f, why)) return false;
      have_format = true;
    }
    if (id == "data") {
      if (!have_format) return why = "data before its format", false;
      const size_t bytes = std::min<size_t>(size, b.size() - body), step = (size_t)f.channels * (f.bits / 8);
      const size_t frames = step ? bytes / step : 0;
      if (frames > (size_t)f.rate * 3600) return why = "longer than an hour (cut it into parts)", false;  // (review 365: the floats would fill memory)
      out.rate = f.rate;
      try {
        out.stereo.assign(frames * 2, 0.0f);
      } catch (const std::bad_alloc &) {
        return why = "too large for the memory free", false;
      }
      const uint8_t *p = (const uint8_t *)b.data() + body;
      for (size_t i = 0; i < frames; i++, p += step) {
        const float l = sample_at(p, f.bits, f.is_float);
        out.stereo[i * 2] = l;
        out.stereo[i * 2 + 1] = f.channels > 1 ? sample_at(p + f.bits / 8, f.bits, f.is_float) : l;
      }
      return true;
    }
    at = body + size + (size & 1);  // (chunks pad to an even size)
  }
  return why = have_format ? "no data chunk" : "no fmt chunk", false;
}

std::string encode_wav16(const std::vector<float> &stereo, int rate) {
  const uint32_t data = (uint32_t)(stereo.size() * 2);
  std::string out;
  out.reserve(44 + data);
  auto u32 = [&](uint32_t v) { for (int k = 0; k < 4; k++) out += (char)(v >> (8 * k) & 255); };
  auto u16 = [&](uint16_t v) { out += (char)(v & 255), out += (char)(v >> 8); };
  out += "RIFF", u32(36 + data), out += "WAVEfmt ", u32(16), u16(1), u16(2), u32((uint32_t)rate), u32((uint32_t)rate * 4), u16(4), u16(16), out += "data", u32(data);
  for (float s : stereo) u16((uint16_t)(int16_t)std::lround(std::clamp(s, -1.0f, 1.0f) * 32767.0f));
  return out;
}

namespace {
struct Kept {
  AudioClipPtr clip;
  int64_t mtime = -2;
  uint64_t size = 0;
};
std::map<std::string, Kept> &library() {
  static std::map<std::string, Kept> lib;
  return lib;
}

/* The file's time and size (-1 when it isn't there). */
void file_stamp(const std::string &abs, int64_t &mtime, uint64_t &size) {
  mtime = -1, size = 0;
  const std::string name = to_lower(fs::filename(abs));
  for (const DirEntry &e : fs::list(fs::parent(abs)))
    if (to_lower(e.name) == name) mtime = e.mtime, size = e.size;
}
}  // namespace

AudioClipPtr audio_asset(const std::string &path) {
  if (path.empty()) return nullptr;
  const std::string abs = resolve_asset_path(path);
  Kept &k = library()[abs];  // (by the file itself: another project's file of the same name is another sound - review 365)
  int64_t mtime = 0;
  uint64_t size = 0;
  file_stamp(abs, mtime, size);
  if (mtime == k.mtime && size == k.size) return k.clip;
  k.mtime = mtime, k.size = size;
  std::string bytes, why;
  auto clip = std::make_shared<AudioClip>();
  const bool ok = fs::read_file(abs, bytes) && decode_wav(bytes, *clip, why);
  if (!ok) Log::warn("Sound %s: %s", path.c_str(), why.empty() ? "can't be read" : why.c_str());
  k.clip = ok ? clip : nullptr;
  return k.clip;
}

SoundSet sequence_sounds(const Sequence &s) {
  SoundSet out;
  for (const SeqTrack &t : s.tracks)
    for (const SeqInstance &i : t.items)
      if (i.kind == (int)SeqKind::Audio && !out.count(i.media)) out[i.media] = audio_asset(i.media);
  trim_audio_assets(out);
  return out;
}

/* Sounds no longer in use go once more than a few are kept (review 365: every sound played stayed decoded). */
void trim_audio_assets(const SoundSet &in_use) {
  auto &lib = library();
  if (lib.size() <= 8) return;
  std::set<std::string> keep;
  for (const auto &[path, clip] : in_use) keep.insert(resolve_asset_path(path));
  for (auto it = lib.begin(); it != lib.end();) it = keep.count(it->first) ? std::next(it) : lib.erase(it);
}

void clear_audio_assets() { library().clear(); }

bool sequence_has_audio(const Sequence &s) {
  for (const SeqTrack &t : s.tracks)
    for (const SeqInstance &i : t.items)
      if (i.kind == (int)SeqKind::Audio) return true;
  return false;
}

namespace {
/* One bar's sound added into `out` (see mix_sequence_audio). */
void mix_bar(const SeqInstance &i, const AudioClip &a, double from, double per_sample, double fps, std::vector<float> &out) {
  const double bar_start = i.start, bar_end = i.start + i.duration(nullptr), gain = std::clamp((double)i.volume, 0.0, 4.0);
  const size_t n = a.frames(), count = out.size() / 2;
  /* Only the samples within the bar (review 365: every sample was walked for every bar). */
  const double k0 = std::ceil((bar_start - from) / per_sample), k1 = std::ceil((bar_end - from) / per_sample);
  const size_t first = k0 <= 0.0 ? 0 : (size_t)std::min<double>(k0, (double)count), last = k1 <= 0.0 ? 0 : (size_t)std::min<double>(k1, (double)count);
  for (size_t k = first; k < last; k++) {
    const double frame = from + (double)k * per_sample;  // (the timeline frame this sample plays at)
    if (frame < bar_start || frame >= bar_end) continue;
    const double pos = (frame - bar_start + i.trim_in) / fps * a.rate;
    const size_t s0 = (size_t)pos;
    if (pos < 0.0 || s0 >= n) continue;
    const size_t s1 = std::min(n - 1, s0 + 1);
    const float t = (float)(pos - (double)s0);  // (linear between the sound's samples: its rate needn't be the output's)
    out[k * 2] += (float)((a.stereo[s0 * 2] * (1.0f - t) + a.stereo[s1 * 2] * t) * gain);
    out[k * 2 + 1] += (float)((a.stereo[s0 * 2 + 1] * (1.0f - t) + a.stereo[s1 * 2 + 1] * t) * gain);
  }
}
}  // namespace

void mix_sequence_audio(const Sequence &s, const SoundSet &sounds, double from, size_t count, int rate, std::vector<float> &out, double step) {
  out.assign(count * 2, 0.0f);
  const double fps = s.frame_rate > 0.0f ? s.frame_rate : 60.0;
  const double per_sample = fps / std::max(1, rate) * (std::isfinite(step) ? step : 1.0);
  for (const SeqTrack &t : s.tracks)
    for (const SeqInstance &i : t.items) {
      const auto a = i.kind == (int)SeqKind::Audio ? sounds.find(i.media) : sounds.end();
      if (a != sounds.end() && a->second && a->second->rate > 0) mix_bar(i, *a->second, from, per_sample, fps, out);
    }
  for (float &v : out) v = std::clamp(v, -1.0f, 1.0f);
}

float audio_frames(const std::string &path, float frame_rate) {
  const AudioClipPtr a = audio_asset(path);
  return a ? (float)std::round(a->seconds() * (frame_rate > 0.0f ? frame_rate : 60.0f)) : 0.0f;
}

}  // namespace bl
