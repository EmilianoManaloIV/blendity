// SPDX-License-Identifier: GPL-2.0-or-later
// The Sequencer's cards and camera cuts (round 364, task 0341): an animatic's or a cinematic's pictures held
// full-frame over the Game view and Render Animation's frames, and the cuts that switch the camera - the bars
// themselves are scene/sequence's (SeqKind).
#include "editor.h"

#include "../image/image.h"
#include "../scene/audio.h"
#include "../scene/board.h"
#include "../scene/material.h"
#include "../scene/sequence.h"
#include "../core/core.h"

#include <algorithm>
#include <cmath>

namespace bl {

namespace {

/* A loaded image as canvas pixels (0xAARRGGBB). Empty when it can't be read. */
Image image_from_file(const std::string &path) {
  Image out;
  Bitmap b;
  std::string err;
  if (!load_image(resolve_asset_path(path), b, err) || b.width <= 0 || b.height <= 0 || b.rgba8.empty()) return out;
  out.resize(b.width, b.height);
  for (size_t i = 0; i < out.pixels.size(); i++) {
    const uint8_t *p = &b.rgba8[i * 4];
    out.pixels[i] = (uint32_t)p[3] << 24 | (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2];
  }
  return out;
}

/* A paper's picture (the Board's) as an image. */
Image image_from_paper(const std::shared_ptr<BoardPicture> &pic) {
  Image out;
  if (!pic || pic->w <= 0 || pic->levels.empty()) return out;
  out.resize(pic->levels[0].w, pic->levels[0].h);
  out.pixels = pic->levels[0].argb;
  return out;
}

/* A file's write time (seconds), -1 when it isn't there. */
int64_t file_time(const std::string &path) {
  const std::string name = to_lower(fs::filename(path));
  for (const DirEntry &e : fs::list(fs::parent(path)))
    if (to_lower(e.name) == name) return e.mtime;
  return -1;
}

uint32_t over(uint32_t src, uint32_t dst) {
  const uint32_t a = src >> 24;
  if (a == 255) return src;
  if (a == 0) return dst;
  auto mix = [&](int shift) { return (((src >> shift) & 255) * a + ((dst >> shift) & 255) * (255 - a)) / 255; };
  return 0xFF000000u | mix(16) << 16 | mix(8) << 8 | mix(0);
}

}  // namespace

/* A card's picture: an image file (read again when the file changes) or a paper's drawing (the Board's picture of it,
 * redrawn as the paper is drawn on); `stamp` changes whenever the picture does. Review 364: nothing kept stale, and a
 * file that couldn't be read is tried again once it changes. */
const Image *Editor::card_picture(const SeqInstance &i, uint64_t *stamp) {
  CardPicture &e = card_pictures_[std::to_string(i.kind) + ":" + i.media];
  bool redone = false;
  if (i.kind == (int)SeqKind::Paper) {
    const std::shared_ptr<BoardPicture> pic = board_paper_picture(resolve_asset_path(i.media));
    redone = pic != e.paper || !e.made;
    if (redone) e.paper = pic, e.img = image_from_paper(pic);
  }
  if (i.kind == (int)SeqKind::Image && clock_now() - e.checked >= 1.0) {
    e.checked = clock_now();
    const int64_t t = file_time(resolve_asset_path(i.media));
    redone = t != e.mtime || !e.made;
    if (redone) e.mtime = t, e.img = image_from_file(i.media);
  }
  if (redone) e.made = ++card_stamp_;
  if (redone && !e.img.width) Log::warn("Sequencer: the card '%s' can't be read (%s)", i.name.c_str(), i.media.c_str());
  if (stamp) *stamp = e.made;
  return e.img.width ? &e.img : nullptr;
}

/* Pictures no card of the sequence shows go (review 364: the cache only grew). */
void Editor::card_trim_pictures(const Sequence &s) {
  if (card_pictures_.size() <= 8) return;
  std::set<std::string> used;
  for (const SeqTrack &t : s.tracks)
    for (const SeqInstance &i : t.items) used.insert(std::to_string(i.kind) + ":" + i.media);
  for (auto it = card_pictures_.begin(); it != card_pictures_.end();)
    it = used.count(it->first) ? std::next(it) : card_pictures_.erase(it);
}

/* Whether a card's file, or a cut's camera, is still there (the bar says "(missing)" when not). */
bool Editor::seq_card_there(const SeqInstance &i) {
  if (i.kind != (int)SeqKind::Camera) return fs::exists(resolve_asset_path(i.media));
  GameObject *g = scene_->find_by_name(i.media);
  return g && g->get<Camera>();
}

/* `card` over `dst`, fitted inside it (letterboxed on black, as a cut to a held drawing is). */
void composite_card(Image &dst, const Image &card) {
  if (!dst.width || !dst.height || !card.width || !card.height) return;
  const float s = std::min((float)dst.width / card.width, (float)dst.height / card.height);
  const int w = std::max(1, (int)(card.width * s)), h = std::max(1, (int)(card.height * s));
  const int x0 = (dst.width - w) / 2, y0 = (dst.height - h) / 2;
  std::fill(dst.pixels.begin(), dst.pixels.end(), 0xFF000000u);
  for (int y = 0; y < h; y++) {
    const uint32_t *src = card.pixels.data() + (size_t)std::min(card.height - 1, (int)(y / s)) * card.width;
    uint32_t *row = dst.row(y0 + y) + x0;
    for (int x = 0; x < w; x++) row[x] = over(src[std::min(card.width - 1, (int)(x / s))], 0xFF000000u);
  }
}

/* The frame the scene's sequence stands at now: Play's Sequencer, else the Sequencer window's Preview; -1 neither. */
float Editor::sequence_frame_now(SequencePtr &seq) {
  seq = nullptr;
  if (playing_) {  // (the first Sequencer playing - review 364)
    Sequencer *c = playing_sequencer();
    seq = c ? sequence_asset(c->sequence) : nullptr;
    return seq && c->playing ? hold_frame(c->frame(), c->hold) : -1.0f;
  }
  if (!seqr_.preview) return -1.0f;
  seq = seq_sequence();
  return seq ? seqr_.frame : -1.0f;
}

/* The Game view's card: drawn over its image, fitted (kept scaled while the card and the view stay the same). */
void Editor::draw_sequence_card(const Recti &img) {
  SequencePtr seq;
  const float frame = sequence_frame_now(seq);
  const SeqInstance *card = seq && frame >= 0.0f ? sequence_card_at(*seq, frame) : nullptr;
  uint64_t stamp = 0;
  const Image *pic = card ? card_picture(*card, &stamp) : nullptr;
  if (seq) card_trim_pictures(*seq);
  if (!pic || img.w <= 0 || img.h <= 0) return;
  const std::string key = card->media + "#" + std::to_string(stamp);  // (a redrawn paper or a saved image: fitted again)
  if (card_view_.width != img.w || card_view_.height != img.h || card_view_key_ != key) {
    card_view_.resize(img.w, img.h);
    composite_card(card_view_, *pic);
    card_view_key_ = key;
  }
  ui_.canvas.blit(card_view_, img.x, img.y);
}

/* Render Animation's frame: the card at that frame, if any, over the render. */
void Editor::composite_render_card(Image &frame_img, float frame) {
  SequencePtr seq = seq_sequence();
  const SeqInstance *card = seq ? sequence_card_at(*seq, frame) : nullptr;
  const Image *pic = card ? card_picture(*card) : nullptr;
  if (seq) card_trim_pictures(*seq);
  if (pic) composite_card(frame_img, *pic);
}

/* The Preview's camera cut: the scene's camera the cut at the playhead names (the one before Preview restored after). */
void Editor::seq_camera_cut() {
  SequencePtr seq = seq_sequence();
  const std::string cut = seq ? sequence_camera_at(*seq, seqr_.frame) : std::string();
  GameObject *cam = cut.empty() ? nullptr : scene_->find_by_name(cut);
  if (cam && cam->get<Camera>()) {
    scene_->active_camera = cam->id;
    seqr_.cut_on = true;
    return;
  }
  if (seqr_.cut_on) scene_->active_camera = seqr_.saved_camera;  // (scrubbed out of the cut: the camera before it - review 364)
  seqr_.cut_on = false;
}

/* seq image|paper|cut <file or camera> <track> <start> [frames]: a card or a camera cut on the timeline. */
bool Editor::seq_add_card(int kind, const std::string &media_in, int track, float start, float frames) {
  std::string media = media_in;
  std::replace(media.begin(), media.end(), '\\', '/');
  if (kind != (int)SeqKind::Camera) media = make_asset_relative(media);  // (in the project: kept relative, as a drop is - review 364)
  SequencePtr s = seq_sequence();
  if (!s || media.empty() || !std::isfinite(start)) return false;
  const std::string ext = fs::extension(media);
  if (ext == ".hdr" || ext == ".exr")
    return Log::warn("Sequencer: %s is a high-range image; save it as a PNG or JPEG to hold it as a card", media.c_str()), false;
  if (kind == (int)SeqKind::Camera && (!scene_->find_by_name(media) || !scene_->find_by_name(media)->get<Camera>()))
    return Log::warn("Sequencer: no camera named '%s'", media.c_str()), false;
  if (kind != (int)SeqKind::Camera && !fs::exists(resolve_asset_path(media))) return Log::warn("Sequencer: no file %s", media.c_str()), false;
  if (kind == (int)SeqKind::Audio && ext != ".wav")
    return Log::warn("Sequencer: %s isn't a WAV file; sounds go on the timeline as WAV (save MP3 and others as WAV)", media.c_str()), false;
  const float sound = kind == (int)SeqKind::Audio ? audio_frames(media, s->frame_rate) : 0.0f;  // (round 365: the sound's own length)
  if (kind == (int)SeqKind::Audio && sound < 1.0f) return Log::warn("Sequencer: the sound %s can't be read", media.c_str()), false;
  if (track < 0 || track > (int)s->tracks.size()) return Log::warn("Sequencer: track %d isn't there (0 to %zu)", track, s->tracks.size()), false;
  const float len = std::isfinite(frames) && frames >= 1.0f ? std::min(1e6f, std::round(frames)) : sound >= 1.0f ? sound : 60.0f;
  static const char *const kWhat[] = {"Add Clip to Sequence", "Add Image to Sequence", "Add Paper to Sequence", "Add Camera Cut to Sequence",
                                      "Add Sound to Sequence"};
  return seq_edit(kWhat[kind], [&](Sequence &q) {
    if (track == (int)q.tracks.size()) {
      q.tracks.emplace_back();
      q.tracks.back().name = kind == (int)SeqKind::Camera ? "Cameras" : kind == (int)SeqKind::Audio ? "Sound" : "Cards";
    }
    SeqInstance i;
    i.kind = kind, i.media = media, i.start = std::max(0.0f, std::round(start)), i.trim_in = 0.0f, i.trim_out = len;
    i.name = q.unique_name(kind == (int)SeqKind::Camera ? "Cut " + media : fs::stem(media));
    q.tracks[(size_t)track].items.push_back(i);
    q.tracks[(size_t)track].sort();
    seqr_.selected_name = i.name;
  });
}

}  // namespace bl
