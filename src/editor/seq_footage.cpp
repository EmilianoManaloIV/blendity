// SPDX-License-Identifier: GPL-2.0-or-later
// After Effects and Premiere on the Sequencer (round 391, task 0368): footage (a video file as a card, decoded by
// render/video_decode), keyed opacity and transform on a card (Premiere's Effect Controls), Premiere's Add Edit (the
// blade) and Ripple Delete, and a paper card by its name.
// `seq video <file> <track> <start> [frames]`, `seq key <bar> opacity|x|y|scale|rotation <frame> <value>`,
// `seq unkey <bar> <channel> [frame]`, `seq blade <frame> [track]`, `seq ripple <bar>`.
#include "editor.h"

#include "../render/video_decode.h"
#include "../render/video_encode.h"
#include "../scene/sequence.h"
#include "../core/core.h"

#include <algorithm>
#include <cmath>

namespace bl {

/* A video card's picture at a sequence frame: its reader opened once, the frame the playhead stands on decoded. */
const Image *Editor::card_video_picture(const SeqInstance &i, float frame, float rate, uint64_t *stamp) {
  CardPicture &e = card_pictures_["5:" + i.media];
  if (!e.video && !e.video_failed) {
    std::string err;
    e.video = video::open_reader(resolve_asset_path(i.media), &err);
    e.video_failed = !e.video;
    if (!e.video) Log::warn("Sequencer: the footage '%s' can't be read (%s)", i.name.c_str(), err.c_str());
  }
  if (!e.video) return nullptr;
  const double seconds = (frame - i.start + i.trim_in) / std::max(1.0f, rate);
  if (std::fabs(seconds - e.video_at) > 1e-6 || !e.made) {
    const bool ok = video::read_frame_at(*e.video, seconds, e.img);
    e.video_at = seconds;
    e.made = ok ? ++card_stamp_ : e.made;
  }
  if (stamp) *stamp = e.made;
  return e.img.width ? &e.img : nullptr;
}

/* seq video <file> <track> <start> [frames]: footage on the timeline, its own length unless one is given. */
bool Editor::seq_add_video(const std::string &file, int track, float start, float frames) {
  SequencePtr s = seq_sequence();
  if (!s) return false;
  if (!video::available()) {  // (the build with Blender's libraries reads video: FFmpeg's)
    refuse("Sequencer: footage needs FFmpeg, which this build runs without - Blender's libraries bring it", "update libs", "Get Blender's Libraries...");
    return false;
  }
  std::string err;
  std::shared_ptr<video::Reader> r = video::open_reader(resolve_asset_path(file), &err);
  if (!r) return Log::warn("Sequencer: %s", err.c_str()), false;
  const float own = std::max(1.0f, std::round((float)video::reader_seconds(*r) * s->frame_rate));
  return seq_add_card((int)SeqKind::Video, file, track, start, std::isfinite(frames) && frames >= 1.0f ? frames : own);
}

/* One bar's length on the timeline (a clip's by its asset). */
float Editor::seq_bar_frames(const SeqInstance &i) { return i.duration(i.kind == (int)SeqKind::Clip ? clip_asset(i.clip).get() : nullptr); }

/* Premiere's Add Edit: every bar across `frame` (on one track, or all with track < 0) cut in two there. */
bool Editor::seq_blade(float frame, int track) {
  SequencePtr s = seq_sequence();
  if (!s || !std::isfinite(frame)) return false;
  frame = std::round(frame);
  std::vector<std::pair<int, std::string>> cut;
  for (size_t t = 0; t < s->tracks.size(); t++) {
    if (track >= 0 && (int)t != track) continue;
    for (const SeqInstance &i : s->tracks[t].items)
      if (frame > i.start && frame < i.start + seq_bar_frames(i)) cut.push_back({(int)t, i.name});
  }
  if (cut.empty()) return Log::warn("Sequencer: no bar across frame %g to cut", frame), false;
  std::vector<float> scale;
  for (const auto &[t, name] : cut) scale.push_back(s->find(name)->kind == (int)SeqKind::Clip ? s->find(name)->speed : 1.0f);
  return seq_edit("Add Edit", [&](Sequence &q) {
    for (size_t k = 0; k < cut.size(); k++) {
      int t = -1, at = -1;
      SeqInstance *i = q.find(cut[k].second, &t, &at);
      SeqInstance right;
      seq_split_bar(*i, frame, scale[k], q.unique_name(i->name), right);
      q.tracks[(size_t)t].items.push_back(std::move(right));
    }
  });
}

/* Premiere's Ripple Delete: the bar goes and the later bars on its track close the gap. */
bool Editor::seq_ripple(const std::string &name) {
  SequencePtr s = seq_sequence();
  int t = -1, at = -1;
  SeqInstance *i = s ? s->find(name, &t, &at) : nullptr;
  if (!i) return Log::warn("Sequencer: no bar named '%s'", name.c_str()), false;
  const float from = i->start, gap = seq_bar_frames(*i);
  const bool done = seq_edit("Ripple Delete", [&](Sequence &q) {
    std::vector<SeqInstance> &items = q.tracks[(size_t)t].items;
    items.erase(items.begin() + at);
    for (SeqInstance &x : items)
      if (x.start >= from) x.start = std::max(0.0f, x.start - gap);
  });
  if (done) seqr_.selected_name.clear();  // (review 391: kept when Play refused it)
  return done;
}

/* seq key <bar> <channel> <frame> <value>: a key on a card (frames from the bar's start); unkey: a channel's keys, or
 * the one at a frame. */
bool Editor::seq_key(const std::string &name, const std::string &channel, float frame, float value, bool remove) {
  SequencePtr s = seq_sequence();
  SeqInstance *i = s ? s->find(name) : nullptr;
  const int c = seq_channel_index(channel);
  if (!i) return Log::warn("Sequencer: no bar named '%s'", name.c_str()), false;
  if (i->kind == (int)SeqKind::Clip || i->kind == (int)SeqKind::Camera || i->kind == (int)SeqKind::Audio)
    return Log::warn("Sequencer: '%s' isn't a picture - keys go on images, papers and footage", name.c_str()), false;
  if (c < 0) return Log::warn("Sequencer: a key's channel is opacity, x, y, scale or rotation, not '%s'", channel.c_str()), false;
  if (!remove && (!std::isfinite(frame) || !std::isfinite(value))) return Log::warn("Sequencer: a key needs a frame and a value"), false;
  const float at = std::isfinite(frame) ? std::max(0.0f, std::round(frame)) : frame;  // (review 391: as it is kept, then compared)
  auto same = [&](const SeqKey &k) { return k.channel == c && (!std::isfinite(at) || k.frame == at); };
  if (remove && std::none_of(i->keys.begin(), i->keys.end(), same)) return Log::warn("Sequencer: '%s' has no %s key there", name.c_str(), seq_channel_name(c)), false;
  return seq_edit(remove ? "Remove Card Key" : "Key Card", [&](Sequence &q) {
    SeqInstance *x = q.find(name);
    x->keys.erase(std::remove_if(x->keys.begin(), x->keys.end(), same), x->keys.end());
    if (!remove) x->keys.push_back({c, at, seq_channel_clamp(c, value)});
    std::sort(x->keys.begin(), x->keys.end(), [](const SeqKey &a, const SeqKey &b) { return a.channel != b.channel ? a.channel < b.channel : a.frame < b.frame; });
  });
}

/* A paper card by its name: a word with no extension that is a paper in Assets/Papers. */
std::string Editor::seq_paper_by_name(const std::string &media) {
  if (!fs::extension(media).empty()) return media;
  const std::string path = paper_path(board_clean_name(media));
  return fs::exists(path) ? make_asset_relative(path) : media;
}

}  // namespace bl
