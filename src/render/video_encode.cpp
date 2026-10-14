// SPDX-License-Identifier: GPL-2.0-or-later
// Video files from rendered frames (task 0077): see video_encode.h. The encoding follows FFmpeg's own example
// (doc/examples/encode_video.c, mux.c) and Blender's movie writer (imbuf/movie/intern/movie_write.cc): open the MP4
// muxer and the H.264 encoder, convert each RGBA frame to BT.709 yuv420p with swscale, send it, write the packets that
// come back, then flush both.
#include "video_encode.h"

#include "../core/core.h"
#include "../image/image.h"

#ifdef BL_WITH_FFMPEG
extern "C" {
#  include <libavcodec/avcodec.h>
#  include <libavformat/avformat.h>
#  include <libavutil/imgutils.h>
#  include <libavutil/opt.h>
#  include <libavutil/version.h>
#  include <libswscale/swscale.h>
}
#endif

#include <algorithm>
#include <cmath>
#include <cstring>

namespace bl::video {

#ifndef BL_WITH_FFMPEG

bool available() { return false; }
std::string description() { return ""; }
void init() {}
bool encode_png_frames(const std::vector<std::string> &, float, const std::string &, std::string *error, const std::atomic<bool> *) {
  if (error) *error = "built without FFmpeg";
  return false;
}
int probe_frames(const std::string &, int *, int *) { return -1; }

#else

bool available() { return true; }

/* FFmpeg's log is process-wide: quieted once, from the main thread (x264's statistics aren't the console's business;
 * errors come back to the caller instead). */
void init() { av_log_set_level(AV_LOG_ERROR); }

/* The H.264 encoder Blender's FFmpeg has (libx264); FFmpeg's own MPEG-4 Part 2 when a build lacks it. */
static const AVCodec *video_codec() {
  if (const AVCodec *c = avcodec_find_encoder_by_name("libx264")) return c;
  if (const AVCodec *c = avcodec_find_encoder(AV_CODEC_ID_H264)) return c;
  return avcodec_find_encoder(AV_CODEC_ID_MPEG4);
}

std::string description() {
  const AVCodec *c = video_codec();
  return strprintf("FFmpeg %s (%s)", av_version_info(), c ? c->name : "no video encoder");
}

/* Everything one encode holds, freed in any order it stopped at. */
struct Encoder {
  AVFormatContext *fmt = nullptr;
  AVCodecContext *ctx = nullptr;
  AVStream *stream = nullptr;
  AVFrame *frame = nullptr;
  AVPacket *packet = nullptr;
  SwsContext *sws = nullptr;
  int w = 0, h = 0;  // the frames' own size (the encoded one is padded to even)
  ~Encoder() {
    sws_freeContext(sws);
    av_packet_free(&packet);
    av_frame_free(&frame);
    avcodec_free_context(&ctx);
    if (fmt && !(fmt->oformat->flags & AVFMT_NOFILE)) avio_closep(&fmt->pb);
    avformat_free_context(fmt);
  }
};

static bool fail(std::string *error, const std::string &why, int code = 0) {
  if (!error) return false;
  char buf[AV_ERROR_MAX_STRING_SIZE] = {};
  if (code) av_strerror(code, buf, sizeof(buf));
  *error = code ? why + ": " + buf : why;
  return false;
}

/* Packets the encoder has ready, into the file (`frame` null: the end, everything it still holds). */
static bool write_packets(Encoder &e, const AVFrame *frame, std::string *error) {
  int r = avcodec_send_frame(e.ctx, frame);
  if (r < 0) return fail(error, "sending a frame to the encoder", r);
  while (true) {
    r = avcodec_receive_packet(e.ctx, e.packet);
    if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) return true;
    if (r < 0) return fail(error, "encoding", r);
    av_packet_rescale_ts(e.packet, e.ctx->time_base, e.stream->time_base);
    e.packet->stream_index = e.stream->index;
    r = av_interleaved_write_frame(e.fmt, e.packet);  // (takes the packet's data)
    if (r < 0) return fail(error, "writing the MP4", r);
  }
}

/* The encoder's settings: x264's quality where it is x264 (CRF 18, visually lossless, Blender's "High quality"), a
 * high bit rate for a fallback encoder (whose 200 kb/s default would look poor); BT.709 limited range, tagged, as
 * Blender writes HD video, so players don't guess. */
static void configure(AVCodecContext *ctx, const AVCodec *codec, int w, int h, float fps) {
  const AVRational rate = av_d2q(fps, 100000);
  ctx->width = w + (w & 1);  // (yuv420p halves the colour planes: even sizes)
  ctx->height = h + (h & 1);
  ctx->time_base = av_inv_q(rate);
  ctx->framerate = rate;
  ctx->pix_fmt = AV_PIX_FMT_YUV420P;
  ctx->gop_size = 12;
  ctx->colorspace = AVCOL_SPC_BT709;
  ctx->color_primaries = AVCOL_PRI_BT709;
  ctx->color_trc = AVCOL_TRC_IEC61966_2_1;  // (sRGB: the frames are display-referred PNGs)
  ctx->color_range = AVCOL_RANGE_MPEG;
  if (std::strcmp(codec->name, "libx264") != 0) {
    ctx->bit_rate = (int64_t)ctx->width * ctx->height * 6;  // (about 12 Mb/s at 1080p)
    return;
  }
  av_opt_set(ctx->priv_data, "crf", "18", 0);
  av_opt_set(ctx->priv_data, "preset", "medium", 0);
}

/* The muxer, the encoder and the frame for `w` x `h` source frames at `fps`. */
static bool open_encoder(Encoder &e, int w, int h, float fps, const std::string &out, std::string *error) {
  int r = avformat_alloc_output_context2(&e.fmt, nullptr, "mp4", out.c_str());
  if (r < 0 || !e.fmt) return fail(error, "no MP4 muxer", r);
  const AVCodec *codec = video_codec();
  if (!codec) return fail(error, "no H.264 or MPEG-4 encoder in this FFmpeg");
  e.w = w;
  e.h = h;
  e.stream = avformat_new_stream(e.fmt, nullptr);
  e.ctx = avcodec_alloc_context3(codec);
  e.packet = av_packet_alloc();
  e.frame = av_frame_alloc();
  if (!e.stream || !e.ctx || !e.packet || !e.frame) return fail(error, "out of memory");
  configure(e.ctx, codec, w, h, fps);
  if (e.fmt->oformat->flags & AVFMT_GLOBALHEADER) e.ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  r = avcodec_open2(e.ctx, codec, nullptr);
  if (r < 0) return fail(error, std::string("opening the ") + codec->name + " encoder", r);
  r = avcodec_parameters_from_context(e.stream->codecpar, e.ctx);
  if (r < 0) return fail(error, "the stream's parameters", r);
  e.stream->time_base = e.ctx->time_base;
  e.frame->format = e.ctx->pix_fmt;
  e.frame->width = e.ctx->width;
  e.frame->height = e.ctx->height;
  r = av_frame_get_buffer(e.frame, 0);
  if (r < 0) return fail(error, "a frame buffer", r);
  /* The frame's own size into the top-left of the (even) picture; the odd edge is copied in after (pad_edges). */
  e.sws = sws_getContext(w, h, AV_PIX_FMT_RGBA, w, h, AV_PIX_FMT_YUV420P, SWS_BICUBIC, nullptr, nullptr, nullptr);
  if (!e.sws) return fail(error, "no RGBA to yuv420p conversion");
  const int *bt709 = sws_getCoefficients(SWS_CS_ITU709);
  sws_setColorspaceDetails(e.sws, bt709, 1, bt709, 0, 0, 1 << 16, 1 << 16);  // (full-range RGB to limited-range video)
  r = avio_open(&e.fmt->pb, out.c_str(), AVIO_FLAG_WRITE);  // (FFmpeg reads the path as UTF-8 on Windows too)
  if (r < 0) return fail(error, "creating " + out, r);
  r = avformat_write_header(e.fmt, nullptr);
  return r < 0 ? fail(error, "the MP4's header", r) : true;
}

/* An odd width or height: the extra luma column / row repeats its neighbour (the chroma planes, half size, are
 * already whole). Padding, not stretching: the picture keeps its pixels. */
static void pad_edges(Encoder &e) {
  AVFrame *f = e.frame;
  uint8_t *y = f->data[0];
  const int ls = f->linesize[0];
  if (e.w & 1)
    for (int r = 0; r < e.h; r++) y[(size_t)r * ls + e.w] = y[(size_t)r * ls + e.w - 1];
  if (e.h & 1) std::memcpy(y + (size_t)e.h * ls, y + (size_t)(e.h - 1) * ls, (size_t)f->width);
}

/* One frame's pixels converted into the picture at `index` and encoded. */
static bool encode_bitmap(Encoder &e, const Bitmap &b, int index, std::string *error) {
  const int r = av_frame_make_writable(e.frame);
  if (r < 0) return fail(error, "a frame buffer", r);
  const uint8_t *src[1] = {b.rgba8.data()};
  const int stride[1] = {e.w * 4};
  sws_scale(e.sws, src, stride, 0, e.h, e.frame->data, e.frame->linesize);
  pad_edges(e);
  e.frame->pts = index;
  return write_packets(e, e.frame, error);
}

/* A PNG of the first frame's size, as 8-bit RGBA. */
static bool read_frame(const std::string &path, int w, int h, Bitmap &b, std::string *error) {
  std::string why;
  if (!load_image(path, b, why)) return fail(error, "reading " + path + ": " + why);
  if (b.is_float || b.width != w || b.height != h) return fail(error, path + " isn't an 8-bit image of the first frame's size");
  return true;
}

static bool encode_all(Encoder &e, const std::vector<std::string> &frames, const Bitmap &first, std::string *error,
                       const std::atomic<bool> *cancel) {
  if (!encode_bitmap(e, first, 0, error)) return false;
  for (size_t i = 1; i < frames.size(); i++) {
    if (cancel && cancel->load()) return fail(error, "cancelled");
    Bitmap b;
    if (!read_frame(frames[i], e.w, e.h, b, error) || !encode_bitmap(e, b, (int)i, error)) return false;
  }
  if (!write_packets(e, nullptr, error)) return false;
  const int r = av_write_trailer(e.fmt);
  return r < 0 ? fail(error, "the MP4's index", r) : true;
}

bool encode_png_frames(const std::vector<std::string> &frames, float fps, const std::string &out, std::string *error,
                       const std::atomic<bool> *cancel) {
  if (frames.empty()) return fail(error, "no frames");
  if (!std::isfinite(fps) || fps <= 0.0f) fps = 30.0f;
  fps = std::min(fps, 1000.0f);
  Bitmap first;
  std::string why;
  if (!load_image(frames[0], first, why)) return fail(error, "reading " + frames[0] + ": " + why);
  if (first.is_float || first.width < 2 || first.height < 2) return fail(error, frames[0] + " isn't an 8-bit image of 2 x 2 or more");
  bool ok = false;
  {
    Encoder e;
    ok = open_encoder(e, first.width, first.height, fps, out, error) && encode_all(e, frames, first, error, cancel);
  }  // (the file closes here)
  if (!ok) fs::remove_file(out);  // (a half-written MP4 has no index: it plays nowhere)
  return ok;
}

/* Counted by reading every packet of the video stream (an MP4's own count can be absent). */
int probe_frames(const std::string &path, int *width, int *height) {
  AVFormatContext *fmt = nullptr;
  if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) < 0) return -1;
  const int si = avformat_find_stream_info(fmt, nullptr) >= 0 ? av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0) : -1;
  int n = si >= 0 ? 0 : -1;
  if (si >= 0 && width) *width = fmt->streams[si]->codecpar->width;
  if (si >= 0 && height) *height = fmt->streams[si]->codecpar->height;
  AVPacket *pk = si >= 0 ? av_packet_alloc() : nullptr;
  while (pk && av_read_frame(fmt, pk) >= 0) {
    n += pk->stream_index == si;
    av_packet_unref(pk);
  }
  av_packet_free(&pk);
  avformat_close_input(&fmt);
  return n;
}

#endif

}  // namespace bl::video
