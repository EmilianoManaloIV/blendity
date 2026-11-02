// SPDX-License-Identifier: GPL-2.0-or-later
// Video files read a frame at a time (round 391, task 0368): see video_decode.h. The reading follows FFmpeg's own
// example (doc/examples/demux_decode.c) and Blender's movie reader (imbuf/movie/intern/movie_read.cc): open the
// container, decode its video stream's packets, convert each frame to BGRA (0xAARRGGBB in memory) with swscale; a seek
// goes to the keyframe before the time and decodes on from there.
#include "video_decode.h"

#ifdef BL_WITH_FFMPEG
extern "C" {
#  include <libavcodec/avcodec.h>
#  include <libavformat/avformat.h>
#  include <libswscale/swscale.h>
}
#endif

#include "../core/core.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace bl::video {

#ifndef BL_WITH_FFMPEG

struct Reader {};
std::shared_ptr<Reader> open_reader(const std::string &, std::string *error) {
  if (error) *error = "this build reads no video (it has no FFmpeg: Help > Get Blender's Libraries)";
  return nullptr;
}
double reader_seconds(const Reader &) { return 0.0; }
float reader_fps(const Reader &) { return 0.0f; }
int reader_width(const Reader &) { return 0; }
int reader_height(const Reader &) { return 0; }
bool read_frame_at(Reader &, double, Image &) { return false; }

#else

namespace {
constexpr double kSeekAhead = 2.0;      // (seconds: further on than this, a seek beats decoding every frame between)
constexpr int64_t kMaxPixels = 8192LL * 8192LL;  // (review 391: a frame larger than this is refused - each is copied twice)
constexpr double kSlowMs = 250.0;       // (a decode longer than this is said once: long-GOP footage scrubbed back)
}

/* Everything one open file holds, freed in any order it stopped at. */
struct Reader {
  AVFormatContext *fmt = nullptr;
  AVCodecContext *ctx = nullptr;
  AVFrame *frame = nullptr;
  AVPacket *packet = nullptr;
  SwsContext *sws = nullptr;
  int stream = -1;
  double time_base = 0.0, start = 0.0, seconds = 0.0;
  float fps = 30.0f;
  bool sent_end = false;  // (the decoder told the file has ended: it gives what it holds, then EOF)
  bool held = false;      // (`frame` holds a decoded picture not yet shown - the one after `shown`)
  double held_at = 0.0, shown = -1.0;
  bool said_slow = false;
  Image img;
  ~Reader() {
    sws_freeContext(sws);
    av_packet_free(&packet);
    av_frame_free(&frame);
    avcodec_free_context(&ctx);
    avformat_close_input(&fmt);
  }
};

namespace {

/* The next picture out of the decoder into `frame`; false at the end or on an error. */
bool decode_next(Reader &r) {
  for (;;) {
    const int got = avcodec_receive_frame(r.ctx, r.frame);
    if (got == 0) return true;
    if (got != AVERROR(EAGAIN) || r.sent_end) return false;
    if (av_read_frame(r.fmt, r.packet) < 0) {
      r.sent_end = true;
      avcodec_send_packet(r.ctx, nullptr);
      continue;
    }
    if (r.packet->stream_index == r.stream) avcodec_send_packet(r.ctx, r.packet);
    av_packet_unref(r.packet);
  }
}

double frame_seconds(const Reader &r) {
  const int64_t pts = r.frame->best_effort_timestamp;
  return pts == AV_NOPTS_VALUE ? r.shown + 1.0 / r.fps : pts * r.time_base - r.start;
}

/* The held picture into `img` (BGRA is 0xAARRGGBB as a little-endian word). */
bool show_held(Reader &r) {
  const int w = r.frame->width, h = r.frame->height;
  r.sws = sws_getCachedContext(r.sws, w, h, (AVPixelFormat)r.frame->format, w, h, AV_PIX_FMT_BGRA, SWS_BILINEAR, nullptr, nullptr, nullptr);
  if (!r.sws || w <= 0 || h <= 0 || (int64_t)w * h > kMaxPixels) return false;
  if (r.img.width != w || r.img.height != h) r.img.resize(w, h);
  uint8_t *dst[4] = {(uint8_t *)r.img.pixels.data(), nullptr, nullptr, nullptr};
  const int stride[4] = {w * 4, 0, 0, 0};
  sws_scale(r.sws, r.frame->data, r.frame->linesize, 0, h, dst, stride);
  r.shown = r.held_at, r.held = false;
  return true;
}

void seek(Reader &r, double seconds) {
  av_seek_frame(r.fmt, r.stream, (int64_t)((seconds + r.start) / r.time_base), AVSEEK_FLAG_BACKWARD);
  avcodec_flush_buffers(r.ctx);
  r.sent_end = false, r.held = false, r.shown = -1.0;
}

}  // namespace

std::shared_ptr<Reader> open_reader(const std::string &path, std::string *error) {
  auto r = std::make_shared<Reader>();
  auto fail = [&](const std::string &why) {
    if (error) *error = why;
    return std::shared_ptr<Reader>();
  };
  av_log_set_level(AV_LOG_ERROR);  // (review 391: FFmpeg's chatter out of the log)
  AVDictionary *opts = nullptr;
  av_dict_set(&opts, "protocol_whitelist", "file", 0);  // (review 391: a file on disk only - never a URL a scene names)
  const int opened = avformat_open_input(&r->fmt, path.c_str(), nullptr, &opts);
  av_dict_free(&opts);
  if (opened < 0) return fail("can't open " + path);
  if (avformat_find_stream_info(r->fmt, nullptr) < 0) return fail("can't read " + path + "'s streams");
  const AVCodec *codec = nullptr;
  r->stream = av_find_best_stream(r->fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
  if (r->stream < 0 || !codec) return fail(path + " has no video this FFmpeg can decode");
  AVStream *st = r->fmt->streams[r->stream];
  r->ctx = avcodec_alloc_context3(codec);
  r->frame = av_frame_alloc();
  r->packet = av_packet_alloc();
  if (!r->ctx || !r->frame || !r->packet) return fail("out of memory");
  if (avcodec_parameters_to_context(r->ctx, st->codecpar) < 0) return fail("the video stream's parameters");
  r->ctx->thread_count = 0;  // (as many as FFmpeg likes)
  if (avcodec_open2(r->ctx, codec, nullptr) < 0) return fail(std::string("can't open the ") + codec->name + " decoder");
  r->time_base = av_q2d(st->time_base);
  r->start = st->start_time != AV_NOPTS_VALUE ? st->start_time * r->time_base : 0.0;
  /* Blender's guess (review 391: a phone's variable rate has a base rate far above its real one). */
  const AVRational rate = av_guess_frame_rate(r->fmt, st, nullptr);
  r->fps = rate.num && rate.den ? (float)av_q2d(rate) : 30.0f;
  r->fps = std::isfinite(r->fps) && r->fps > 0.0f ? std::min(r->fps, 1000.0f) : 30.0f;
  /* Its length: the stream's duration (a well-made file's counts its last frame's length); the frames it holds at that
   * rate when there is none. */
  const double by_pts = st->duration != AV_NOPTS_VALUE ? st->duration * r->time_base : r->fmt->duration > 0 ? r->fmt->duration / (double)AV_TIME_BASE : 0.0;
  r->seconds = by_pts > 0.0 ? by_pts : st->nb_frames / (double)r->fps;
  if ((int64_t)r->ctx->width * r->ctx->height > kMaxPixels) return fail(path + " is larger than 8192 x 8192 a frame");
  return r;
}

double reader_seconds(const Reader &r) { return r.seconds; }
float reader_fps(const Reader &r) { return r.fps; }
int reader_width(const Reader &r) { return r.ctx ? r.ctx->width : 0; }
int reader_height(const Reader &r) { return r.ctx ? r.ctx->height : 0; }

bool read_frame_at(Reader &r, double seconds, Image &out) {
  const double half = 0.5 / r.fps;
  seconds = std::max(0.0, std::isfinite(seconds) ? seconds : 0.0);
  const bool back = r.shown >= 0.0 && seconds + half < r.shown;
  const bool far = r.shown >= 0.0 && seconds > r.shown + kSeekAhead;
  if (back || far) seek(r, seconds);
  const auto t0 = std::chrono::steady_clock::now();
  for (;;) {
    if (!r.held && !decode_next(r)) break;
    if (!r.held) r.held = true, r.held_at = frame_seconds(r);
    if (r.shown >= 0.0 && r.held_at > seconds + half) break;  // (the next picture is later: the shown one stays)
    if (!show_held(r)) return false;
  }
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  if (ms > kSlowMs && !r.said_slow) r.said_slow = true, Log::warn("Video: %.0f ms to reach a frame - footage with keyframes far apart scrubs slowly backwards (re-encode it with more)", ms);
  if (r.shown < 0.0) return false;
  out = r.img;
  return true;
}

#endif

}  // namespace bl::video
