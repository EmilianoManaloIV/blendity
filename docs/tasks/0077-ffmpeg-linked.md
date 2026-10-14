# 0077: FFmpeg linked as a Blender dependency

- **Status:** done
- **Requirements:** R-05
- **Decisions:** ADR 0032
- **Model:** main session (Opus)
- **PR:** https://github.com/EmilianoManaloIV/blendity/pull/70

## Goal
The user (2026-10-14): "Can we install ffmpeg and treat it like a blender dependency?" - and link it, with every
dependency included, so nobody has to fetch Blender's libraries themselves (the second half is task 0078).

## Scope
- In: Blender's FFmpeg 8.1 (and Windows' aom.dll) fetched into the sparse lib checkouts; BL_WITH_FFMPEG in build.bat,
  build.sh and CMake; the DLLs deployed; `video::encode_png_frames` / `probe_frames`; Render Animation encoding
  in-process when it is built in; `libs` lists FFmpeg; licences; Learn, manual and README text.
- Out: audio, other containers and codecs, encoding while rendering (frames are encoded after).

## Acceptance criteria
1. Six PNG frames of 101 x 57 become an MP4 that FFmpeg reads back as 6 frames of 102 x 58, H.264 (libx264).
2. Render Animation makes its MP4 with no ffmpeg program anywhere.
3. Builds without Blender's libraries still build, and fall back as before.

## As built
- Windows: avcodec needs `aom.dll` (Blender's `aom` folder), added to the deployed DLLs.
- Linux: Blender's `libc_compat` for the static codecs; OpenJPEG linked after FFmpeg.
- Fetching ffmpeg into the Linux checkout re-applied its sparse pattern and removed dpcpp's and openjph's files (they
  were outside it); they are back, and the pattern now lists them.
- Odd sizes are padded (the edge repeated), BT.709 tagged; a failed or cancelled encode leaves no file; the editor
  stops an encode when it closes.
- Tests: round 103.
