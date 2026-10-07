// SPDX-License-Identifier: GPL-2.0-or-later
// Image codecs from Blender's libraries (blender/source/blender/imbuf/intern/
// format_*.cc read them through OpenImageIO; Blendity calls them directly):
//   OpenEXR        .exr read/write (half or float, any compression)
//   libjpeg-turbo  SIMD JPEG decoding
//   libpng         PNG decoding
// Each returns false when unavailable or on failure, and image.cpp falls back
// to Blendity's own decoder.
#include "image.h"

#include "../core/core.h"

#include <algorithm>
#include <csetjmp>
#include <cstdio>
#include <cstring>

#ifdef BL_WITH_OPENEXR
#  include <OpenEXR/ImfArray.h>
#  include <OpenEXR/ImfChannelList.h>
#  include <OpenEXR/ImfFrameBuffer.h>
#  include <OpenEXR/ImfHeader.h>
#  include <OpenEXR/ImfIO.h>
#  include <OpenEXR/ImfInputFile.h>
#  include <OpenEXR/ImfOutputFile.h>
#  include <OpenEXR/ImfRgbaFile.h>
#endif
#ifdef BL_WITH_LIBJPEG
#  include <jpeglib.h>
#endif
#ifdef BL_WITH_LIBPNG
#  include <png.h>
#endif

namespace bl {

static bool g_use_lib_codecs = true;
void set_image_library_codecs(bool on) { g_use_lib_codecs = on; }
bool image_library_codecs() { return g_use_lib_codecs; }

bool exr_available() {
#ifdef BL_WITH_OPENEXR
  return true;
#else
  return false;
#endif
}

/* ------------------------------------------------------------- OpenEXR */

#ifdef BL_WITH_OPENEXR
namespace {
/* Reads an EXR from memory (decode_image works on bytes). */
class MemIStream : public Imf::IStream {
 public:
  MemIStream(const char *data, size_t size) : Imf::IStream("memory"), data_(data), size_(size) {}
  bool isMemoryMapped() const override { return true; }
  char *readMemoryMapped(int n) override {
    if (pos_ + (size_t)n > size_) throw std::runtime_error("EXR: read past end");
    char *p = const_cast<char *>(data_ + pos_);
    pos_ += n;
    return p;
  }
  bool read(char c[], int n) override {
    if (pos_ + (size_t)n > size_) throw std::runtime_error("EXR: read past end");
    std::memcpy(c, data_ + pos_, n);
    pos_ += n;
    return pos_ < size_;
  }
  uint64_t tellg() override { return pos_; }
  void seekg(uint64_t pos) override { pos_ = (size_t)pos; }
  void clear() override {}

 private:
  const char *data_;
  size_t size_;
  size_t pos_ = 0;
};
}  // namespace
#endif

bool decode_exr_lib(const std::string &bytes, Bitmap &out, std::string &error) {
#ifdef BL_WITH_OPENEXR
  try {
    MemIStream stream(bytes.data(), bytes.size());
    /* RgbaInputFile handles RGB(A), luminance and YC images alike. */
    Imf::RgbaInputFile file(stream);
    Imath::Box2i dw = file.dataWindow();
    const int w = dw.max.x - dw.min.x + 1, h = dw.max.y - dw.min.y + 1;
    if (w <= 0 || h <= 0) {
      error = "EXR: empty data window";
      return false;
    }
    Imf::Array2D<Imf::Rgba> px(h, w);
    file.setFrameBuffer(&px[0][0] - dw.min.x - (size_t)dw.min.y * w, 1, (size_t)w);
    file.readPixels(dw.min.y, dw.max.y);
    out = Bitmap();
    out.width = w;
    out.height = h;
    out.is_float = true;
    out.rgbaf.resize((size_t)w * h * 4);
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++) {
        const Imf::Rgba &p = px[y][x];
        float *o = &out.rgbaf[((size_t)y * w + x) * 4];
        o[0] = p.r;
        o[1] = p.g;
        o[2] = p.b;
        o[3] = (file.channels() & Imf::WRITE_A) ? (float)p.a : 1.0f;
      }
    return true;
  }
  catch (const std::exception &e) {
    error = std::string("EXR: ") + e.what();
    return false;
  }
#else
  (void)bytes;
  (void)out;
  error = "built without OpenEXR";
  return false;
#endif
}

bool write_exr(const std::string &path, const float *rgb, int w, int h, bool half) {
#ifdef BL_WITH_OPENEXR
  try {
    /* Blender's default for renders: ZIP compression; half floats unless
     * "Float (Full)" is chosen. */
    Imf::Header header(w, h);
    header.compression() = Imf::ZIP_COMPRESSION;
    const Imf::PixelType type = half ? Imf::HALF : Imf::FLOAT;
    for (const char *c : {"R", "G", "B"}) header.channels().insert(c, Imf::Channel(type));
    Imf::OutputFile file(path.c_str(), header);
    Imf::FrameBuffer fb;
    std::vector<Imf::Rgba> hbuf;
    if (half) {
      hbuf.resize((size_t)w * h);
      for (size_t i = 0; i < hbuf.size(); i++) hbuf[i] = Imf::Rgba(rgb[i * 3], rgb[i * 3 + 1], rgb[i * 3 + 2], 1.0f);
      char *base = (char *)hbuf.data();
      fb.insert("R", Imf::Slice(Imf::HALF, base + offsetof(Imf::Rgba, r), sizeof(Imf::Rgba), sizeof(Imf::Rgba) * w));
      fb.insert("G", Imf::Slice(Imf::HALF, base + offsetof(Imf::Rgba, g), sizeof(Imf::Rgba), sizeof(Imf::Rgba) * w));
      fb.insert("B", Imf::Slice(Imf::HALF, base + offsetof(Imf::Rgba, b), sizeof(Imf::Rgba), sizeof(Imf::Rgba) * w));
    }
    else {
      char *base = (char *)rgb;
      for (int k = 0; k < 3; k++)
        fb.insert(k == 0 ? "R" : k == 1 ? "G" : "B", Imf::Slice(Imf::FLOAT, base + k * sizeof(float), 3 * sizeof(float), 3 * sizeof(float) * w));
    }
    file.setFrameBuffer(fb);
    file.writePixels(h);
    return true;
  }
  catch (const std::exception &e) {
    Log::warn("EXR write failed: %s", e.what());
    return false;
  }
#else
  (void)path;
  (void)rgb;
  (void)w;
  (void)h;
  (void)half;
  return false;
#endif
}

/* ------------------------------------------------------- libjpeg-turbo */

#ifdef BL_WITH_LIBJPEG
namespace {
/* libjpeg's default error handler calls exit(): jump back instead. */
struct JpegError {
  jpeg_error_mgr mgr;
  std::jmp_buf jump;
  char message[JMSG_LENGTH_MAX];
};
void jpeg_error_exit(j_common_ptr cinfo) {
  auto *err = reinterpret_cast<JpegError *>(cinfo->err);
  cinfo->err->format_message(cinfo, err->message);
  std::longjmp(err->jump, 1);
}
}  // namespace
#endif

bool decode_jpeg_lib(const uint8_t *data, size_t size, Bitmap &out, std::string &error) {
#ifdef BL_WITH_LIBJPEG
  if (!g_use_lib_codecs) return false;
  jpeg_decompress_struct cinfo;
  JpegError jerr;
  cinfo.err = jpeg_std_error(&jerr.mgr);
  jerr.mgr.error_exit = jpeg_error_exit;
  jerr.message[0] = 0;
  /* Everything that can longjmp lives below this point; no C++ objects with
   * destructors are created between setjmp and the last libjpeg call. */
  std::vector<uint8_t> *pixels = &out.rgba8;
  if (setjmp(jerr.jump)) {
    jpeg_destroy_decompress(&cinfo);
    error = std::string("libjpeg: ") + jerr.message;
    return false;
  }
  jpeg_create_decompress(&cinfo);
  jpeg_mem_src(&cinfo, data, (unsigned long)size);
  jpeg_read_header(&cinfo, TRUE);
  if (cinfo.jpeg_color_space == JCS_CMYK || cinfo.jpeg_color_space == JCS_YCCK) {
    jpeg_destroy_decompress(&cinfo);
    error = "libjpeg: CMYK";
    return false;  // Blendity's decoder handles these
  }
  cinfo.out_color_space = JCS_EXT_RGBA;  // libjpeg-turbo extension: RGBA output
  jpeg_start_decompress(&cinfo);
  out.width = (int)cinfo.output_width;
  out.height = (int)cinfo.output_height;
  out.is_float = false;
  pixels->resize((size_t)out.width * out.height * 4);
  while (cinfo.output_scanline < cinfo.output_height) {
    JSAMPROW row = pixels->data() + (size_t)cinfo.output_scanline * out.width * 4;
    jpeg_read_scanlines(&cinfo, &row, 1);
  }
  jpeg_finish_decompress(&cinfo);
  jpeg_destroy_decompress(&cinfo);
  return true;
#else
  (void)data;
  (void)size;
  (void)out;
  (void)error;
  return false;
#endif
}

/* -------------------------------------------------------------- libpng */

bool decode_png_lib(const uint8_t *data, size_t size, Bitmap &out, std::string &error) {
#ifdef BL_WITH_LIBPNG
  if (!g_use_lib_codecs) return false;
  png_image img;
  std::memset(&img, 0, sizeof(img));
  img.version = PNG_IMAGE_VERSION;
  if (!png_image_begin_read_from_memory(&img, data, size)) {
    error = std::string("libpng: ") + img.message;
    return false;
  }
  img.format = PNG_FORMAT_RGBA;  // 16-bit, palette and grey inputs are converted
  out.width = (int)img.width;
  out.height = (int)img.height;
  out.is_float = false;
  out.rgba8.resize(PNG_IMAGE_SIZE(img));
  if (!png_image_finish_read(&img, nullptr, out.rgba8.data(), 0, nullptr)) {
    error = std::string("libpng: ") + img.message;
    png_image_free(&img);
    return false;
  }
  return true;
#else
  (void)data;
  (void)size;
  (void)out;
  (void)error;
  return false;
#endif
}

}  // namespace bl
