// SPDX-License-Identifier: GPL-2.0-or-later
// AI inbetweening (tasks 0128-0131, ADR 0046): the motion between two keys filled by a small transformer trained on
// the CMU motion capture database (tools/inbetween/), as Cascadeur's AI Inbetweening. An optional download, as MoonRay
// is: ONNX Runtime (MIT, Microsoft's) and the model, in ~/.blendity/ai/<tag>, loaded only when used - the base build
// has no dependency on it.
//
// The model works on one canonical humanoid (22 joints: the CMU skeleton as tools/inbetween/cmu.py reads it). A rig is
// mapped onto it through gait::analyse's bones: each bone's turn from its rest, mirrored from Blendity's left-handed
// space to the data's right-handed one, its size scaled by the legs. Whatever it makes is ordinary keys, and only
// inside the span the animator chose.
#pragma once
#include "../core/math.h"
#include "animation.h"
#include "gait.h"
#include "scene.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace bl {
namespace ai {

/* The download Blendity's archives hold: the model and ONNX Runtime 1.31.0 (tools/inbetween/package.py). */
constexpr const char *kTag = "ai-v1";
constexpr int kJoints = 22, kFeatures = 3 + kJoints * 6, kMaxGap = 120;

/* "blendity-ai-windows-x64-ai-v1.zip" (.tar.gz for Linux and macOS on Apple silicon), "" where there is none. */
std::string archive_name();
/* ~/.blendity/ai/<tag> (or BLENDITY_AI_DIR: the tests'). */
std::string install_dir();

/* What the model was trained with (its inbetween.json): the canonical skeleton and the styles. */
struct ModelInfo {
  std::vector<Vec3> offsets;   // each joint's rest offset from its parent (metres, the data's space)
  std::vector<int> parents;
  std::vector<std::string> styles;
  int ctx = 10;                // context frames before the span
  int hidden = 0;              // the network's last features per frame (its second output; 0: none, so no fine-tuning)
  bool valid() const { return (int)offsets.size() == kJoints && (int)parents.size() == kJoints && !styles.empty(); }
};
bool parse_model_info(const std::string &json, ModelInfo &out);

/* The network: frames (W x kFeatures: the context, the span filled by interpolation, the target), which are known
 * (W), a style; the frames with the span corrected, and (`hidden`) its last features (W x info.hidden). ONNX
 * Runtime's in a real install; a stand-in in the tests. */
class Runtime {
 public:
  virtual ~Runtime() = default;
  virtual bool run(const std::vector<float> &frames, const std::vector<float> &known, int style, int W, std::vector<float> &out,
                   std::string &error, std::vector<float> *hidden = nullptr) = 0;
  ModelInfo info;
};

/* ---- Fine-tuning on the animator's clips (task 0131) ----
 * ONNX Runtime's on-device training isn't in its standard build, so the network itself stays as shipped: a correction
 * of its answer from its last features, fitted to the animator's clips (ridge regression, solved exactly), saved with
 * the project and switched off at will. */
struct Head {
  int d = 0;                // features per frame
  std::vector<float> w;     // (d + 1) x kFeatures: the last row the bias
  int windows = 0;          // what it was fitted on
  float before = 0.0f, after = 0.0f;  // the mean error on those windows without and with it
  bool valid() const { return d > 0 && (int)w.size() == (d + 1) * kFeatures; }
};
std::string head_text(const Head &h);
bool head_from_text(const std::string &text, Head &out);
/* The network's answer corrected on the unknown frames: out += hidden . w. */
void apply_head(const Head &h, const std::vector<float> &hidden, const std::vector<float> &known, int W, std::vector<float> &out);
/* Fitting: each window's unknown frames (their features, and what the network missed by) added; then solved. */
struct Fit {
  int d = 0;
  std::vector<double> hth, hty, yty;  // (d+1)^2, (d+1) x kFeatures, kFeatures: running sums (no window is kept)
  int rows = 0, windows = 0;
  void add(const std::vector<float> &hidden, const std::vector<float> &known, int W, const std::vector<float> &out, const std::vector<float> &truth);
  bool solve(float lambda, Head &h) const;
  /* The RMS of what the network missed on the windows added, without a correction and with `h` (from the sums). */
  double rms_before() const;
  double rms_after(const Head &h) const;
};
/* ONNX Runtime loaded from `dir` (its library, inbetween.onnx and inbetween.json): null with `error`. */
std::unique_ptr<Runtime> load_runtime(const std::string &dir, std::string &error);

/* The install: there and loadable ("" = yes), else why not. */
std::string install_problem();
/* Unpacks a checked archive into install_dir() and runs a test inference (on the downloading thread). */
bool unpack(const std::string &archive, std::string &error);
/* A test inference on the install: 60 frames between two standing poses; its time in `ms`. */
bool probe(std::string &error, double *ms = nullptr);
bool remove(std::string &error);

/* ---- A span of the clip ---- */
struct Span {
  int from = 0, to = 0;  // the bounding keyframes: the frames strictly between are made
  int style = 0;         // into ModelInfo::styles
  uint32_t seed = 0;     // 0: as the keys say; another: a variation (the context frames taken a little faster or slower)
};
/* The request for a span, made from the clip on the editor's thread: the rig mapped, the frames in the model's space.
 * Runs on any thread. */
struct Request {
  Span span;
  int W = 0, gap = 0;     // the window, and the frames the model makes (at its 30 frames a second)
  int clip_gap = 0;       // the clip's frames strictly between the keys
  float step = 1.0f;      // clip frames for one of the model's (a 60 fps clip: 2)
  std::vector<float> frames, known;
  Mat4 to_world;      // the model's space (the last context frame at the origin facing +Z) back to the world
  float scale = 1.0f; // the rig's metres for one of the data's
  std::string problem;
};
Request make_request(Scene &scene, GameObject &anim_root, const gait::Rig &rig, const AnimationClip &clip, const Span &span, const ModelInfo &info);
/* A clip at the model's 30 frames a second, as its skeleton (frames x kFeatures, the data's world): on the editor's thread (it
 * poses the scene). Then any window of it, and what the clip really does in it (`truth`, W x kFeatures, the model's
 * space), on any thread: fine-tuning's examples. */
std::vector<float> clip_frames(Scene &scene, GameObject &anim_root, const gait::Rig &rig, const AnimationClip &clip, const ModelInfo &info, int &frames);
Request window_of(const std::vector<float> &rows, int frames, const Span &span, const ModelInfo &info, std::vector<float> *truth);
/* The span's poses keyed into `out` (a copy of `clip`): every frame strictly between the bounding keys on the rig's
 * bones (and IK controls, so IK holds the pose), nothing outside the span changed. `poses` (optional): each made
 * frame's bone world places, for the ghost. Frames written; 0 with `error`. */
int apply(Scene &scene, GameObject &anim_root, const gait::Rig &rig, const AnimationClip &clip, const Request &req, const std::vector<float> &result,
          const ModelInfo &info, AnimationClip &out, std::string &error, std::vector<std::vector<std::pair<Vec3, Vec3>>> *poses = nullptr);

/* The interpolation the network corrects (the root lerped, the turns slerped): also what the tests' stand-in returns. */
void interpolate_gap(std::vector<float> &frames, int ctx, int gap);

}  // namespace ai
}  // namespace bl
