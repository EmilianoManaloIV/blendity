// SPDX-License-Identifier: GPL-2.0-or-later
// Video files read a frame at a time (round 391, task 0368): footage on the Sequencer, decoded in-process with
// FFmpeg's libraries as Blender's movie reader does (blender/source/blender/imbuf/movie/intern/movie_read.cc). Built
// with BL_WITH_FFMPEG; without it, open_reader says so and returns nothing.
#pragma once

#include "canvas.h"

#include <memory>
#include <string>

namespace bl::video {

struct Reader;

/* The file opened for reading, its first video stream's decoder ready; null (with `error`) when it can't be. */
std::shared_ptr<Reader> open_reader(const std::string &path, std::string *error);
/* Its length in seconds, its frame rate and its picture size. */
double reader_seconds(const Reader &r);
float reader_fps(const Reader &r);
int reader_width(const Reader &r);
int reader_height(const Reader &r);
/* The picture shown `seconds` into the file (0xAARRGGBB): decoded on from the last one asked for, or after a seek
 * when it is earlier or more than two seconds on. False when there is none (the file broken, the time past its end
 * with nothing decoded). */
bool read_frame_at(Reader &r, double seconds, Image &out);

}  // namespace bl::video
