# Task 0341: the Sequencer for animatics and cinematics - cards and camera cuts

- **Status:** done (round 364, 2026-10-31)
- **Asked:** "fix all fixes as stated in comparing with the other established programs" - finding 11 of
  docs/hci/compare-2026-10.md: the short film's animatic couldn't be cut in Blendity, because the Sequencer held only
  animation clips.
- **Split:** audio (WAV decoding, playback in sync, muxed into Render Animation's video) is round 365, and the later
  rounds move one along.

## What
- **`scene/sequence`:**
  - `SeqKind` (Clip, Image, Paper, Camera), and `SeqInstance::kind` and `media` (a file, or a camera's name);
  - a card has no clip: its length is trim_in..trim_out, not a clip's end;
  - cards are saved as `card <kind> <start> <in> <out>\t<name>\t<media>` lines, which older readers skip;
  - animation sampling skips cards (`covering`);
  - `sequence_card_at` gives the topmost Image or Paper bar covering a frame;
  - `sequence_camera_at` gives the topmost Camera bar's camera;
  - in Play, the Sequencer component sets the scene's active camera to the cut's.
- **`editor/seq_cards.cpp`:**
  - **Pictures:** an image file (through the image loader) or a paper's drawing (the Board's `board_paper_picture`),
    kept by path.
  - **`composite_card`:** fits the card into a frame, letterboxed on black.
  - **The Game view** draws the card at the sequence's frame now (Play's Sequencer, else the window's Preview) over its
    image, kept fitted while the card and the view stay the same.
  - **Render Animation** composites the card into each frame before it's written.
  - **The window's Preview** applies the cut at the playhead and gives the old camera back when it ends.
- **Making cards:** drop an image or a .paper onto a lane (two seconds long), or `seq image|paper <file> <track>
  <start> [frames]` and `seq cut <camera> <track> <start> [frames]`. A missing file or camera is refused.
- **The bars:** cards and cuts are coloured by kind and labelled [image], [paper] or [camera cut].

## Checked
- `tests_round364`:
  - an Image, a Paper and a Camera bar saved and read back;
  - the topmost card at frames 50, 110 and 200, and the cut at 40 and 90;
  - a card's 120 frames (not a clip's 60);
  - sampling at a card's frame giving no values;
  - a 4 x 2 red PNG fitted over an 8 x 8 frame (black bars top and bottom, red in the middle), and a frame past the card
    left as rendered;
  - a cut to CamB from 10 to 40: CamB at frame 20 in Preview, CamA again after Preview;
  - the Sequencer component in Play switching to the cut's camera;
  - the lesson's Try-it (a cut to a camera that isn't there refused).

## The Opus review's fixes
- **A card's trim** was clamped to a clip's 60-frame end (in seq_trim, the bar's drag and the details' In/Out). Cards
  and cuts now trim to any length, and speed doesn't apply to them: the details show what the card shows instead of
  Object and Speed, and the right-click menu has no speeds for them.
- **A camera cut never gave the camera back** when the playhead left it. In Preview, the camera from before Preview
  comes back once the playhead is out of every cut. In Play, the Sequencer gives back the camera it started with.
- **Card pictures were cached forever.** A paper card follows the Board's picture of the paper as it's drawn on. An
  image card reads its file again when the file's time changes (looked at once a second). A file that couldn't be read
  is tried again once it changes. Pictures no card of the sequence shows are let go.
- **A card's file moved, or a cut's camera renamed:** the bar says "(missing)".
- **.hdr and .exr cards** are refused, with the reason (save them as PNG or JPEG).
- **`seq image|paper` paths** inside the project are kept relative, as a drop's are.
- **The Game view** takes the first Sequencer that is playing.
- **Trims of cards** are undone as "Trim Card".
- **Checked:** a card trimmed to 200 frames, then at speed 2, stays 200 frames; scrubbing to frame 60, past a cut,
  in Preview gives CamA back; in Play, frame 180, past the cut, gives C1 back.

## Not done
- **Audio:** round 365.
- **Crossfades between cards**, and pictures in renders other than Render Animation's (F12 of one frame).
- **A card-only animatic still renders the 3D scene** under each card, and Render Animation still wants a camera.
  Skipping the render under a full card is left for later.
