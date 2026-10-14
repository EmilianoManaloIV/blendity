// SPDX-License-Identifier: GPL-2.0-or-later
// Video files from rendered frames (task 0077): an H.264 MP4 encoded in-process with FFmpeg's libraries, as Blender's
// FFmpeg output does (blender/source/blender/imbuf/movie/intern/movie_write.cc). Built with BL_WITH_FFMPEG (Blender's
// prebuilt FFmpeg, lib/<platform>/ffmpeg); without it, available() is false and Render Animation runs an ffmpeg
// program instead, or writes PNG frames only.
#pragma once

#include <atomic>
#include <string>
#include <vector>

namespace bl::video {

/* Was Blendity built with FFmpeg's libraries? */
bool available();
/* "FFmpeg 8.1 (libx264)": the library and the H.264 encoder it would use; "" without FFmpeg. */
std::string description();
/* Once, on the main thread, before an encode: FFmpeg's process-wide log down to errors. */
void init();
/* `frames` (8-bit PNGs, one size) as an MP4 at `fps`: H.264 in BT.709 yuv420p, an odd size padded by repeating the
 * edge pixel. False with a reason in `error` when it can't, and then no file is left. `cancel`, when set, stops it
 * between frames. Slow (seconds a minute of video): call it off the main thread. */
bool encode_png_frames(const std::vector<std::string> &frames, float fps, const std::string &out, std::string *error,
                       const std::atomic<bool> *cancel = nullptr);
/* A video file's frame count and picture size, read back by FFmpeg (tests, the log); -1 when it can't. */
int probe_frames(const std::string &path, int *width = nullptr, int *height = nullptr);

}  // namespace bl::video
