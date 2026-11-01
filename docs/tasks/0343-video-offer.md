# Task 0343: no MP4 says how to get one, and round 350's report corrected

- **Status:** done (round 366, 2026-10-31)
- **Asked:** "fix all fixes as stated in comparing with the other established programs" - finding 6 of
  docs/hci/compare-2026-10.md, "no video without installing FFmpeg".
- **The finding, corrected:**
  - The build with Blender's libraries has made MP4s itself since task 0077 (`render/video_encode.cpp`, FFmpeg
    in-process, as Blender does).
  - Round 350 ran `renderanim ffmpeg`, which looks for an external program, and read "none found" as "no video".
  - Only the dependency-free build has no video maker.

## What
- **`Editor::offer_video_libraries`:** Render Animation's finish, with no video maker, is a refusal toast. It names the
  frames' folder and the reason, and its Fix is either:
  - **Get Blender's Libraries...** (`update libs`): asks GitHub for this build's libraries and opens the Libraries
    dialog (task 0080);
  - **Use Blender's Libraries** (`update libson`): when they were turned off in Preferences, turned on from the next
    start.
  An ffmpeg program on the PATH or set in Preferences still makes the MP4 instead.
- **docs/hci/compare-2026-10.md:** a correction note at the top; the C production row, C6, KLM row 7 and its note, the
  Blender comparison and finding 6 corrected. The published report (the Artifact) is updated to match.

## The Opus review's fixes
- **Two messages still said "frames only" in the libraries' build:**
  - the Render Animation button's tooltip;
  - Preferences' FFmpeg path, `set_ffmpeg_path`, whose warning was the very line round 350 read as "no video".
  Both now say the MP4 is made here (naming FFmpeg's version), and only the dependency-free build says "frames only".
  The file's header comment is corrected too.
- **The message by state:**
  - a full build made without FFmpeg points to an ffmpeg program (Open Preferences);
  - a start with `--no-libs`, or libraries turned on since, says to start again;
  - libraries turned off with the full build there offers `update libson`;
  - otherwise `update libs`.
- **"Get Blender's Libraries..." with nothing to download** (a local build, a release without them) said so only in
  the log. It now shows a toast with Open Preferences.
- **Tests:** check the folder in the message and the Fix each state expects, and the Try-it reads the `libs` row.
- **The lesson's paths and answers** are corrected.
- **The report** says "that run" and notes finding 6's first scoring.

## Checked
- **`tests_round366`:**
  - the message names the libraries and the ffmpeg program, with an `update libs` / `update libson` Fix;
  - with the libraries turned off, `update libson` and "turned off";
  - a 3-frame render's finish shows the offer exactly when neither FFmpeg in-process nor an ffmpeg program is there;
  - the lesson's Try-it (`libs` lists FFmpeg).

## Not done
- **Bundling FFmpeg into the dependency-free build:** that build's point is to have no dependencies.
