// SPDX-License-Identifier: GPL-2.0-or-later
// Sequences of animation clips, Premiere's way (task 0062, ADR 0030): a Sequence asset (Assets/Animations/<name>.seq)
// lays clips on tracks of a timeline, overlapping ones crossfade and higher tracks win; triggers jump to a named clip
// in Play with a blend. The Sequencer component plays one; the editor's Sequencer window edits it.
#pragma once

#include "animation.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace bl {

/* What a bar holds (round 364, task 0341): an animation clip, or - for an animatic or a cinematic - a picture held
 * full-frame (an image file, or a paper's drawing), a camera cut (the scene's camera switched to `media`) or a sound
 * (a WAV file, round 365, task 0342). */
enum class SeqKind { Clip = 0, Image = 1, Paper = 2, Camera = 3, Audio = 4, Video = 5 };

/* A key on a card (round 391, task 0368): Premiere's Effect Controls - opacity (0-1), x and y (fractions of the
 * frame's width and height from its middle), scale (1 fitted) and rotation (degrees, clockwise) - at a frame from the
 * bar's start; linear between keys, held before the first and after the last. */
enum SeqChannel { kSeqOpacity, kSeqX, kSeqY, kSeqScale, kSeqRotation, kSeqChannels };
struct SeqKey {
  int channel = 0;
  float frame = 0.0f, value = 0.0f;
};
/* A card's look at a frame: what its keys give (the defaults where a channel has none). */
struct SeqCardLook {
  float opacity = 1.0f, x = 0.0f, y = 0.0f, scale = 1.0f, rotation = 0.0f;
};
bool seq_is_video_file(const std::string &path);  // (.mp4, .mov, .mkv, .webm, .avi, .m4v)
int seq_channel_index(const std::string &name);  // "opacity", "x", "y", "scale", "rotation" (any case); -1 none
const char *seq_channel_name(int channel);
float seq_channel_clamp(int channel, float v);  // (opacity 0-1, scale 0.01-100, x and y -10-10, rotation -36000-36000)

/* One clip on the timeline: which clip, where it starts (sequence frames), the part of it used (clip frames; out < 0:
 * to the clip's end), its speed, and the name triggers jump to. A card (an Image, a Paper or a Camera bar) has no
 * clip: its `media` (a file, a camera's name) and its length in trim_in..trim_out. */
struct SeqInstance {
  int kind = 0;  // SeqKind
  std::string media;
  std::string clip, name;
  std::string target;  // the object it plays on, by name (task 0070); empty: the Sequencer's own object
  float start = 0.0f;
  float trim_in = 0.0f, trim_out = -1.0f;
  float speed = 1.0f;
  float volume = 1.0f;  // (a sound's: 0 silent, 1 as recorded, up to 4)
  std::vector<SeqKey> keys;  // (round 391: a picture card's, by channel then frame)
  float clip_out(const AnimationClip *c) const;  // the trimmed end in clip frames (the clip's end when not set)
  float duration(const AnimationClip *c) const;  // on the timeline, in sequence frames
  float clip_frame(float seq_frame) const;       // the clip frame shown at a sequence frame
};

struct SeqTrack {
  std::string name = "Track";
  std::vector<SeqInstance> items;  // by start
  void sort();
};

/* In Play, pressing `key` ("J", "1", "Space") jumps to instance `instance`, blending over `blend` seconds. */
struct SeqTrigger {
  std::string key, instance;
  float blend = 0.25f;
};

struct Sequence {
  std::string name = "Sequence", asset_path;
  float frame_rate = 60.0f;
  std::vector<SeqTrack> tracks;  // 0 the bottom; a higher track wins
  std::vector<SeqTrigger> triggers;
  uint64_t version = 1;
  void touch() { version++; }
  float end_frame() const;  // where the last instance ends
  /* The instance named `name` (first match), or nullptr. */
  SeqInstance *find(const std::string &name, int *track = nullptr, int *index = nullptr);
  const SeqInstance *find(const std::string &name) const;
  std::string unique_name(const std::string &base) const;  // "Walk", "Walk 2", ...
};
using SequencePtr = std::shared_ptr<Sequence>;

std::string save_sequence_text(const Sequence &s);
bool load_sequence_text(const std::string &text, Sequence &s);

/* The sequence assets, shared like the clips. */
SequencePtr sequence_asset(const std::string &path);
SequencePtr create_sequence_asset(const std::string &path, const std::string &name);
size_t save_dirty_sequence_assets();
size_t retarget_sequence_assets(const std::string &from, const std::string &to);  // a .seq (or folder) moved
size_t forget_sequence_assets(const std::string &path);
void clear_sequence_assets();
/* A .anim (or folder) moved: the instances in every loaded sequence follow it (and on disk, the ones not loaded). */
size_t retarget_sequence_clips(const std::string &from, const std::string &to);

/* A card's look `frame` frames into its bar (round 391). */
SeqCardLook seq_card_look(const SeqInstance &i, float frame);
/* A bar cut in two at sequence frame `frame` (Premiere's Add Edit): `i` becomes the left part, `right` the rest named
 * `right_name` - its trim moved on by the cut times `scale` (a clip's speed; 1 for a card) and the keys after the cut
 * moved to it, each keyed channel holding its value at the cut on both sides (round 391). */
void seq_split_bar(SeqInstance &i, float frame, float scale, const std::string &right_name, SeqInstance &right);
/* The card shown at `frame`: the topmost track's Image, Paper or Video bar there, else nullptr (round 364, 391). */
const SeqInstance *sequence_card_at(const Sequence &s, float frame);
/* The camera the cut at `frame` names (the topmost Camera bar there), else "". */
std::string sequence_camera_at(const Sequence &s, float frame);

/* Animated numbers by (path, property, axis): what a clip or a sequence gives at a frame, before it's applied. A value
 * is known by a 64-bit key of the three and points at a track that names them (read it only while the clips are as
 * they were: the same frame). The table is reused frame to frame, so sampling allocates nothing once warm. */
struct AnimValue {
  uint64_t key = 0;
  const AnimTrack *track = nullptr;
  float value = 0.0f;
  GameObject *target = nullptr;  // the object the track's path starts from (none: the root apply is given)
};
struct AnimValues {
  std::vector<AnimValue> values;  // in the order first set
  void clear();
  void set(const AnimTrack &t, float value);
  void set_key(uint64_t key, const AnimTrack *t, float value, GameObject *target = nullptr);
  const float *get(uint64_t key) const;
  const float *get(const std::string &path, const std::string &property, int axis) const;

 private:
  std::vector<uint32_t> slots_;  // open addressing: index + 1 into values (0: empty)
  int find(uint64_t key) const;  // its slot (holding it, or the empty one where it goes)
};
uint64_t anim_value_key(const std::string &path, const std::string &property, int axis);
/* Rotation (Euler degrees) blends the shorter way round each axis; anything else linearly. */
float blend_anim_value(const std::string &property, float a, float b, float t);
/* The sequence at `frame`: per track bottom-up, the instances there (two overlapping: crossfaded), the higher tracks'
 * numbers replacing the lower ones'. */
void sample_sequence(const Sequence &s, float frame, AnimValues &out, const Scene *scene = nullptr);  // (scene: bars' targets found in it)
/* The values onto the hierarchy under `root` (each object's transform set once). */
void apply_anim_values(const AnimValues &v, GameObject *root);  // (a value with a target starts from it)
/* The current numbers of everything `v` names (what a jump blends from). */
void read_anim_values(const AnimValues &names, GameObject *root, AnimValues &out);
/* Whether the scene's sequence has a bar on `g` (its Animation component then steps aside in Play). */
bool scene_sequence_drives(const Scene &scene, const GameObject &g);
/* "J" / "1" / "Space" / "F5" -> the key's code (0: not a key). */
int trigger_key_code(const std::string &name);

/* Unity's PlayableDirector, roughly: plays a sequence in Play; its triggers' keys jump. */
struct Sequencer : ComponentBase<Sequencer> {
  static constexpr const char *kName = "Sequencer";
  std::string sequence;  // the .seq asset, project-relative
  bool play_automatically = true;
  float speed = 1.0f;
  int wrap = 0;  // Loop, Once
  int hold = 1;  // on 1s, 2s or 3s (task 0149): the timeline's frame held to multiples of this
  float time = 0.0f;  // seconds into the timeline (Play only)
  bool playing = false;
  /* A jump's blend: from the values of the moment to the sequence's own over blend_total seconds (by key only: the
   * tracks it points at may change before the blend ends). */
  AnimValues blend_from, scratch;
  float blend_left = 0.0f, blend_total = 0.0f;
  std::vector<uint8_t> was_down;  // each trigger's key last frame (a jump on the press, not while held)
  uint64_t base_camera = 0;       // (round 364: the scene's camera at start, given back when a cut ends)
  bool cut_on = false;
  void reflect(Reflector &r) override;
  void start(PlayContext &ctx) override;
  void update(PlayContext &ctx) override;
  bool jump(const std::string &instance, float blend);  // false: no such instance
  Scene *scene_ = nullptr;  // the scene it last played in (bars' targets are found there)
  float frame() const;  // the timeline frame now (wrapped)
};

}  // namespace bl
