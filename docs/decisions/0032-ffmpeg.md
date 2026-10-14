# 0032: FFmpeg linked for video, as Blender does

- **Status:** accepted (task 0077; the user's request of 2026-10-14)
- **Date:** 2026-10-14
- **Requirements:** R-05
- **Relates to:** ADR 0001 (dependencies only from Blender's own bundle, behind a switch), task 0074 (Render Animation)

## Context
Render Animation (task 0074) wrote PNG frames and made an MP4 only by running an `ffmpeg` program the user had to
install. The user asked to treat FFmpeg like Blender's other dependencies. Blender bundles FFmpeg 8.1 (with x264, x265,
libvpx, libaom, Opus, Vorbis, Theora, LAME) in its prebuilt libraries and encodes its movies with it in-process.

## Decision
- **BL_WITH_FFMPEG** links Blender's FFmpeg from `lib/<platform>/ffmpeg` (Windows: the DLLs and import libraries,
  plus `aom.dll` from `lib/windows_x64/aom`; Linux and macOS: the static libraries with their codecs, in Blender's
  link order, plus OpenJPEG, zlib and macOS's frameworks). build.bat, build.sh and CMake all do it.
- **`src/render/video_encode.cpp`** encodes PNG frames to an H.264 (libx264, CRF 18) MP4 in yuv420p, on a background
  thread; `probe_frames` reads a file back. Render Animation uses it when it is built in.
- **Fallback** (the dependency-free build CI tests): run an `ffmpeg` program from the PATH or the preferences, else PNG
  frames only - task 0074's path, unchanged.
- Linux needs Blender's `intern/libc_compat` (copied to `extern/libc_compat`): Blender's static codecs were built with
  -ffast-math against an older glibc and call `__pow_finite` and friends, which newer glibc dropped.

## Consequences
- The editor makes videos without the user installing anything. The DLLs add about 45 MB to the Windows build.
- x264 and x265 are GPL-2.0-or-later, so FFmpeg as built is GPL; Blendity's binaries were GPL-3.0-or-later already
  (licenses/README.md lists them).
- The installers (task 0078) carry these libraries; a source build gets them with `packaging/fetch_blender_libs.sh`.
