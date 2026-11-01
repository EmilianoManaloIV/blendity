# Task 0342: the Sequencer's sound

- **Status:** done (round 365, 2026-10-31)
- **Asked:** "fix all fixes as stated in comparing with the other established programs" - finding 11 of
  docs/hci/compare-2026-10.md: the short film couldn't be cut with its sound in Blendity. Round 364 (task 0341) added
  cards and camera cuts; this round adds the sound.

## What
- **`scene/audio`:**
  - `decode_wav`: RIFF/WAVE in-house - PCM 8/16/24/32-bit and IEEE float 32-bit, and WAVE_FORMAT_EXTENSIBLE of either;
    mono or stereo (more channels: the first two). Compressed WAVs and other files are refused with the reason.
  - `encode_wav16`: a 16-bit stereo WAV.
  - `audio_asset`: a sound kept by path, read again when the file's time or size changes.
  - `sequence_sounds` loads a sequence's sounds on the main thread.
  - `mix_sequence_audio` mixes the Audio bars over any span: each from its trim_in (timeline frames into the sound),
    at its volume, resampled linearly, at a step (Play's Speed, or a render's rate against the sequence's), clipped.
- **`SeqKind::Audio`** with `SeqInstance::volume` (0 to 4), saved on its `card` line (a round-364 reader skips kind 4).
- **`core/audio_out`:** a stereo float stream to the default device, fed on its own thread.
  - Windows: WASAPI shared mode, event-driven, about 40 ms queued.
  - Linux: PulseAudio's simple API loaded at run time (no link dependency).
  - macOS: none yet; it says why.
- **`editor/seq_audio.cpp`:**
  - Once a frame, the sound follows the picture: the Preview playing, or Play's playing Sequencer (not past its end).
  - The stream's thread mixes a copy of the sequence and its sounds. The copy is retaken when the playhead jumps or
    drifts more than a fifth of a second, the sequence changes or the speed changes.
  - The device opens on the first sound played (never headless, or with `BLENDITY_NO_AUDIO`), and says once if it can't.
- **Render Animation:**
  - The Sequencer's sound over the frames written goes to sound.wav beside them.
  - With Blender's FFmpeg, the MP4 gets an AAC stream (192 kb/s, 48 kHz), written beside each picture so the two
    interleave.
  - With an ffmpeg program, `-i sound.wav -c:a aac -shortest`.
- **Making sound bars:**
  - drop a .wav onto a lane, or `seq sound <file.wav> <track> <start> [frames]` (the recording's own length by default);
  - `seq volume <bar> <v>` and the details' Volume field;
  - the bars are coloured and labelled [sound].

## Checked
- **`tests_round365`:**
  - WAVs of each format decoded to their values, a compressed one and a non-WAV refused, a written WAV read back;
  - a 0.5 hum at half volume mixed only within its bar;
  - a ramp trimmed half a second in, at half the output's rate, at the right values, and twice as fast at Speed 2;
  - the volume saved and read back;
  - `seq sound` at the file's length (60 frames for 1 s at 60 fps), a PNG refused;
  - Preview's Play: the stream at the playhead with the hum at 0.25, a jump to frame 200 moving it there (silent), Stop
    silencing it;
  - Render Animation of 12 frames: sound.wav of 9600 samples at the hum's level; in the libraries' build the MP4 has
    one 48 kHz audio stream and 12 frames;
  - the default device opened and fed a third of a second (opt-in `BLENDITY_TEST_AUDIO_OUT=1`): passes on Windows (WASAPI); under WSL the PulseAudio server is there but libpulse-simple.so.0 isn't installed, so the stream refused with that reason - the Linux output is not yet tried live;
  - the Try-it.

## The Opus review's fixes
- **Paused Play repeated a fifth of a second of sound forever.** It is now silent while paused.
- **A backward Speed or Ping Pong's way back made the sound chop.** The sound plays only while the picture moves
  forward: held, paused, Speed 0 and backward are silent. A jump back of more than half a second (a loop's wrap, a
  scrub) plays forward from there.
- **Editing during playback stuttered** (dragging Volume re-mixed the queued sound every frame). An edit now copies
  the sequence and its sounds but keeps the sound's position; only a start, another sequence or a drift moves it.
- **Sounds were loaded on the main thread under the mixer's lock.** They are now loaded outside it and swapped in, and
  only when the sequence changed.
- **The stream ran forever once opened** (Windows wouldn't sleep). It now closes after three seconds without sound.
- **A lost device (unplugged, another default) was silent for good.** The stream notices and is opened again; a
  failed open is retried every five seconds.
- **Decoded sounds were kept forever.** Unused ones go once more than eight are kept, and they are kept by the file
  itself, not the project-relative path.
- **Render Animation's mix walked every sample for every bar.** It now walks only each bar's samples.
- **Huge WAVs could exhaust memory.** Sounds over an hour are refused, and an allocation failure is caught.
- **No AAC encoder failed the whole MP4.** The video is now made without sound (sound.wav still has it).
- **Smaller fixes:**
  - float WAVs other than 32-bit are refused;
  - the ffmpeg program is given sound.wav only when it was written;
  - lanes light up while dragging a sound or a card;
  - a card's path keeps its width in the details;
  - NOMINMAX is guarded;
  - `<cstdint>` is included;
  - a failed PulseAudio write marks the stream lost.
- **Tests:**
  - held, a step back and a step on;
  - malformed WAVs: a short data chunk, 0 channels, data before fmt, 16-bit float, over an hour;
  - the ffmpeg arguments with sound.wav;
  - the Try-it refusing an existing .mp3;
  - the scratch project cleaned after the render test.

## Not done
- **Below 20 fps in Play** (its step is capped at 0.05 s), the picture falls behind real time, and the sound is put
  back each fifth of a second.
- **Other formats** (MP3, OGG, FLAC) through FFmpeg: WAV only for now.
- **macOS output** (Core Audio).
- **A waveform drawn on the bar**, fades and crossfades between sound bars.
