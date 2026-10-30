// SPDX-License-Identifier: GPL-2.0-or-later
#include "inbetween.h"
#include "../core/core.h"
#include "../core/json.h"
#include "../../extern/onnxruntime/onnxruntime_c_api.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace bl {
namespace ai {

namespace {
const float kPiA = 3.14159265f;
/* The canonical joints (tools/inbetween/cmu.py's JOINTS, in its order). */
enum Joint {
  Hips, LUpLeg, LLeg, LFoot, LToes, RUpLeg, RLeg, RFoot, RToes, Spine, Spine1, Chest, Neck, HeadJ,
  LShoulder, LArm, LForeArm, LHand, RShoulder, RArm, RForeArm, RHand
};
}  // namespace

std::string archive_name() {
#if defined(_WIN32)
  return std::string("blendity-ai-windows-x64-") + kTag + ".zip";
#elif defined(__APPLE__) && defined(__aarch64__)
  return std::string("blendity-ai-macos-arm64-") + kTag + ".tar.gz";
#elif defined(__linux__) && defined(__x86_64__)
  return std::string("blendity-ai-linux-x64-") + kTag + ".tar.gz";
#else
  return "";
#endif
}

std::string install_dir() {
  const char *env = std::getenv("BLENDITY_AI_DIR");
  if (env && *env) return env;
  return fs::join(fs::join(fs::join(fs::home_dir(), ".blendity"), "ai"), kTag);
}

bool parse_model_info(const std::string &text, ModelInfo &out) {
  JsonParser p{text.data(), text.data() + text.size()};
  const Json j = p.value();
  if (!p.ok) return false;
  ModelInfo m;
  for (size_t i = 0; i < j["offsets"].size(); i++) {
    const Json &o = j["offsets"][i];
    m.offsets.push_back(Vec3((float)o[0].number(), (float)o[1].number(), (float)o[2].number()));
  }
  for (size_t i = 0; i < j["parents"].size(); i++) m.parents.push_back(j["parents"][i].integer());
  for (size_t i = 0; i < j["styles"].size(); i++) m.styles.push_back(j["styles"][i].str);
  m.ctx = std::max(1, j["ctx"].integer(10));
  m.hidden = std::max(0, j["hidden"].integer(0));
  for (size_t i = 0; i < m.parents.size(); i++)
    if (m.parents[i] >= (int)i) return false;  // (a parent before each joint: forward kinematics in one pass)
  if (!m.valid()) return false;
  out = m;
  return true;
}

/* ================================================================ ONNX Runtime, loaded when used */

namespace {

#ifdef _WIN32
const char *const kLib = "onnxruntime.dll";
#elif defined(__APPLE__)
const char *const kLib = "libonnxruntime.dylib";
#else
const char *const kLib = "libonnxruntime.so";
#endif

void *open_library(const std::string &path) {
#ifdef _WIN32
  const int n = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
  std::wstring wide(n > 0 ? (size_t)n : 0, L'\0');
  if (n > 0) MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &wide[0], n);
  return (void *)LoadLibraryExW(wide.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);  // (its providers DLL beside it)
#else
  return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
}

void *symbol(void *lib, const char *name) {
#ifdef _WIN32
  return (void *)GetProcAddress((HMODULE)lib, name);
#else
  return dlsym(lib, name);
#endif
}

class OrtRuntime : public Runtime {
 public:
  ~OrtRuntime() override {
    if (!api_) return;
    if (session_) api_->ReleaseSession(session_);
    if (mem_) api_->ReleaseMemoryInfo(mem_);
    if (env_) api_->ReleaseEnv(env_);  // (the library stays loaded: unloading ONNX Runtime isn't safe while threads wind down)
  }

  bool load(const std::string &dir, std::string &error) {
    std::string text;
    if (!fs::read_file(fs::join(dir, "inbetween.json"), text) || !parse_model_info(text, info)) return error = "inbetween.json is missing or unreadable", false;
    void *lib = open_library(fs::join(dir, kLib));
    if (!lib) return error = std::string(kLib) + " didn't load", false;
    using GetBase = const OrtApiBase *(ORT_API_CALL *)();
    GetBase get = (GetBase)symbol(lib, "OrtGetApiBase");
    const OrtApiBase *base = get ? get() : nullptr;
    api_ = base ? base->GetApi(ORT_API_VERSION) : nullptr;
    if (!api_) return error = "ONNX Runtime's API " + std::to_string(ORT_API_VERSION) + " isn't there (an older library?)", false;
    OrtSessionOptions *so = nullptr;
    const int threads = std::max(1, std::min(4, (int)std::thread::hardware_concurrency() / 2));
    if (!ok(api_->CreateEnv(ORT_LOGGING_LEVEL_ERROR, "blendity", &env_), error) || !ok(api_->CreateSessionOptions(&so), error)) return false;
    ok(api_->SetIntraOpNumThreads(so, threads), error);
    const std::string model = fs::join(dir, "inbetween.onnx");
#ifdef _WIN32
    int n = MultiByteToWideChar(CP_UTF8, 0, model.c_str(), -1, nullptr, 0);
    std::wstring path(n > 0 ? (size_t)n : 0, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, model.c_str(), -1, &path[0], n);
    const bool made = ok(api_->CreateSession(env_, path.c_str(), so, &session_), error);
#else
    const bool made = ok(api_->CreateSession(env_, model.c_str(), so, &session_), error);
#endif
    api_->ReleaseSessionOptions(so);
    if (!made) return false;
    return ok(api_->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &mem_), error);
  }

  bool run(const std::vector<float> &frames, const std::vector<float> &known, int style, int W, std::vector<float> &out,
           std::string &error, std::vector<float> *hidden) override {
    if ((int)frames.size() != W * kFeatures || (int)known.size() != W) return error = "a window of the wrong size", false;
    std::vector<float> f = frames, k = known;
    int64_t s = style;
    const int64_t fshape[3] = {1, W, kFeatures}, kshape[2] = {1, W}, sshape[1] = {1};
    OrtValue *in[3] = {nullptr, nullptr, nullptr};
    bool good = ok(api_->CreateTensorWithDataAsOrtValue(mem_, f.data(), f.size() * sizeof(float), fshape, 3, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &in[0]), error) &&
                ok(api_->CreateTensorWithDataAsOrtValue(mem_, k.data(), k.size() * sizeof(float), kshape, 2, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &in[1]), error) &&
                ok(api_->CreateTensorWithDataAsOrtValue(mem_, &s, sizeof(s), sshape, 1, ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64, &in[2]), error);
    const bool two = hidden && info.hidden > 0;  // (the last features too: fine-tuning's)
    const char *names[3] = {"frames", "known", "style"}, *outs[2] = {"out", "hidden"};
    OrtValue *got[2] = {nullptr, nullptr};
    if (good) good = ok(api_->Run(session_, nullptr, names, in, 3, outs, two ? 2 : 1, got), error);
    float *p = nullptr, *h = nullptr;
    if (good) good = sized(got[0], (size_t)W * kFeatures, error) && ok(api_->GetTensorMutableData(got[0], (void **)&p), error);
    if (good) out.assign(p, p + (size_t)W * kFeatures);
    if (good && two) good = sized(got[1], (size_t)W * info.hidden, error) && ok(api_->GetTensorMutableData(got[1], (void **)&h), error);
    if (good && two) hidden->assign(h, h + (size_t)W * info.hidden);
    for (OrtValue *v : in)
      if (v) api_->ReleaseValue(v);
    for (OrtValue *v : got)
      if (v) api_->ReleaseValue(v);
    return good;
  }

 private:
  /* The output holds `n` numbers (a model and an inbetween.json that disagree are refused, not read past). */
  bool sized(OrtValue *v, size_t n, std::string &error) {
    OrtTensorTypeAndShapeInfo *ts = nullptr;
    size_t count = 0;
    if (!ok(api_->GetTensorTypeAndShape(v, &ts), error)) return false;
    const bool got = ok(api_->GetTensorShapeElementCount(ts, &count), error);
    api_->ReleaseTensorTypeAndShapeInfo(ts);
    if (got && count != n) error = strprintf("the model answered %zu numbers, not %zu (its inbetween.json doesn't match it)", count, n);
    return got && count == n;
  }
  bool ok(OrtStatus *st, std::string &error) {
    if (!st) return true;
    error = api_->GetErrorMessage(st);
    api_->ReleaseStatus(st);
    return false;
  }
  const OrtApi *api_ = nullptr;
  OrtEnv *env_ = nullptr;
  OrtSession *session_ = nullptr;
  OrtMemoryInfo *mem_ = nullptr;
};

}  // namespace

std::unique_ptr<Runtime> load_runtime(const std::string &dir, std::string &error) {
  auto r = std::make_unique<OrtRuntime>();
  if (!r->load(dir, error)) return nullptr;
  return r;
}

namespace {

/* The pose prior's session: one input, one output, a pose at a time (task 0280). */
class OrtPrior : public PriorModel {
 public:
  ~OrtPrior() override {
    if (!api_) return;
    if (session_) api_->ReleaseSession(session_);
    if (mem_) api_->ReleaseMemoryInfo(mem_);
    if (env_) api_->ReleaseEnv(env_);
  }

  bool load(const std::string &dir, std::string &error) {
    std::string text;
    if (!fs::read_file(fs::join(dir, "posemodel.json"), text) || !parse_model_info(text, info)) return error = "posemodel.json is missing or unreadable", false;
    void *lib = open_library(fs::join(dir, kLib));
    if (!lib) return error = std::string(kLib) + " didn't load", false;
    using GetBase = const OrtApiBase *(ORT_API_CALL *)();
    GetBase get = (GetBase)symbol(lib, "OrtGetApiBase");
    const OrtApiBase *base = get ? get() : nullptr;
    api_ = base ? base->GetApi(ORT_API_VERSION) : nullptr;
    if (!api_) return error = "ONNX Runtime's API " + std::to_string(ORT_API_VERSION) + " isn't there (an older library?)", false;
    OrtSessionOptions *so = nullptr;
    if (!ok(api_->CreateEnv(ORT_LOGGING_LEVEL_ERROR, "blendity-prior", &env_), error) || !ok(api_->CreateSessionOptions(&so), error)) return false;
    ok(api_->SetIntraOpNumThreads(so, 1), error);  // (one small pose: threads cost more than they give)
    const std::string model = fs::join(dir, "posemodel.onnx");
#ifdef _WIN32
    int n = MultiByteToWideChar(CP_UTF8, 0, model.c_str(), -1, nullptr, 0);
    std::wstring path(n > 0 ? (size_t)n : 0, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, model.c_str(), -1, &path[0], n);
    const bool made = ok(api_->CreateSession(env_, path.c_str(), so, &session_), error);
#else
    const bool made = ok(api_->CreateSession(env_, model.c_str(), so, &session_), error);
#endif
    api_->ReleaseSessionOptions(so);
    if (!made) return false;
    return ok(api_->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &mem_), error);
  }

  bool run(const float *goals, float *pose, std::string &error) override {
    float in_data[kPriorInputs];
    std::copy(goals, goals + kPriorInputs, in_data);
    const int64_t shape[2] = {1, kPriorInputs};
    OrtValue *in = nullptr, *got = nullptr;
    bool good = ok(api_->CreateTensorWithDataAsOrtValue(mem_, in_data, sizeof(in_data), shape, 2, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &in), error);
    const char *names[1] = {"goals"}, *outs[1] = {"pose"};
    if (good) good = ok(api_->Run(session_, nullptr, names, &in, 1, outs, 1, &got), error);
    float *p = nullptr;
    size_t count = 0;
    OrtTensorTypeAndShapeInfo *ts = nullptr;
    if (good) good = ok(api_->GetTensorTypeAndShape(got, &ts), error) && ok(api_->GetTensorShapeElementCount(ts, &count), error);
    if (ts) api_->ReleaseTensorTypeAndShapeInfo(ts);
    if (good && count != (size_t)kPriorOutputs) error = strprintf("the prior answered %zu numbers, not %d", count, kPriorOutputs), good = false;
    if (good) good = ok(api_->GetTensorMutableData(got, (void **)&p), error);
    if (good) std::copy(p, p + kPriorOutputs, pose);
    if (in) api_->ReleaseValue(in);
    if (got) api_->ReleaseValue(got);
    return good;
  }

 private:
  bool ok(OrtStatus *st, std::string &error) {
    if (!st) return true;
    error = api_->GetErrorMessage(st);
    api_->ReleaseStatus(st);
    return false;
  }
  const OrtApi *api_ = nullptr;
  OrtEnv *env_ = nullptr;
  OrtSession *session_ = nullptr;
  OrtMemoryInfo *mem_ = nullptr;
};

}  // namespace

std::unique_ptr<PriorModel> load_prior(const std::string &dir, std::string &error) {
  auto r = std::make_unique<OrtPrior>();
  if (!r->load(dir, error)) return nullptr;
  return r;
}

std::string prior_problem() {
  const std::string dir = install_dir();
  if (!fs::exists(fs::join(dir, "posemodel.onnx")) || !fs::exists(fs::join(dir, kLib))) return "the pose prior isn't installed (the AI download)";
  return "";
}

std::string install_problem() {
  const std::string dir = install_dir();
  if (!fs::exists(fs::join(dir, "inbetween.onnx")) || !fs::exists(fs::join(dir, kLib))) return "not installed (Install...)";
  return "";
}

namespace {
std::string tar_program() {
#ifdef _WIN32
  const char *root = std::getenv("SystemRoot");
  return fs::join(fs::join(root ? root : "C:\\Windows", "System32"), "tar.exe");  // (System32's, not one on a path)
#else
  return "tar";
#endif
}

/* The files an install holds: nothing else is unpacked, and nothing else is ever deleted. */
bool own_file(const std::string &name) {
  static const char *const files[] = {"inbetween.onnx", "inbetween.json", "posemodel.onnx", "posemodel.json", "README.txt", "LICENSE-onnxruntime.txt", "ThirdPartyNotices-onnxruntime.txt",
                                      "onnxruntime.dll", "onnxruntime_providers_shared.dll", "libonnxruntime.so", "libonnxruntime.dylib"};
  for (const char *f : files)
    if (name == f) return true;
  return false;
}

/* Deletes the install's own files in `dir`, then the folder when that leaves it empty. */
bool remove_own(const std::string &dir, std::string &error) {
  if (!fs::exists(dir)) return true;
  bool all = true;
  std::error_code ec;
  for (const auto &e : std::filesystem::directory_iterator(std::filesystem::u8path(dir), ec)) {
    const std::string name = e.path().filename().u8string();
    if (own_file(name) && !fs::remove_file(fs::join(dir, name))) all = false;
  }
  std::filesystem::remove(std::filesystem::u8path(dir), ec);  // (only when empty: a folder holding anything else stays)
  if (!all) error = "can't remove " + dir + " (ONNX Runtime is in use: restart Blendity, then remove it)";
  return all;
}
}  // namespace

bool unpack(const std::string &archive, std::string &error) {
  const std::string dir = install_dir(), staging = dir + ".new";
  /* The archive's members first: plain names of the install's own files only (no folders, links or ../). */
  std::string listing;
  const int listed = sys::run_capture({tar_program(), "-tf", archive}, listing, 60);
  for (const std::string &name : split_ws(listing))
    if (!own_file(name)) return fs::remove_file(archive), error = "the archive holds '" + name + "', which isn't part of the AI download", false;
  if (listed != 0 || listing.find("inbetween.onnx") == std::string::npos) return fs::remove_file(archive), error = "the archive can't be read", false;
  std::string ignored;
  remove_own(staging, ignored);
  fs::make_dirs(staging);
  const int code = sys::run_program({tar_program(), "-xf", archive, "-C", staging});
  fs::remove_file(archive);
  if (code != 0) return remove_own(staging, ignored), error = "the archive couldn't be unpacked (tar " + std::to_string(code) + ")", false;
  /* Into place: the old install out of the way first (a loaded ONNX Runtime can't be replaced: restart first). */
  if (!remove_own(dir, error)) return remove_own(staging, ignored), false;
  if (!fs::move(staging, dir)) return remove_own(staging, ignored), error = "can't move the install into " + dir, false;
  if (probe(error)) return true;
  remove_own(dir, ignored);  // (an install that can't run isn't left looking installed)
  error = "the installed model didn't run: " + error;
  return false;
}

bool remove(std::string &error) { return remove_own(install_dir(), error); }

/* ================================================================ the canonical skeleton's frames */

namespace {

Vec3 mirror(Vec3 v) { return Vec3(-v.x, v.y, v.z); }  // (Blendity is left-handed, the data right-handed: x flips)
Quat mirror(Quat q) { return Quat(q.x, -q.y, -q.z, q.w); }

void put6(float *f, Quat q) {
  const Vec3 a = q.rotate(Vec3(1, 0, 0)), b = q.rotate(Vec3(0, 1, 0));
  f[0] = a.x, f[1] = a.y, f[2] = a.z, f[3] = b.x, f[4] = b.y, f[5] = b.z;
}
Quat get6(const float *f) {
  Vec3 a(f[0], f[1], f[2]), b(f[3], f[4], f[5]);
  a = normalize(a);
  b = normalize(b - a * dot(a, b));
  if (!(length(a) > 0.5f) || !(length(b) > 0.5f)) return Quat();
  return quat_from_axes(a, b, cross(a, b));
}
Quat slerp_q(Quat a, Quat b, float t) {
  float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
  if (d < 0.0f) b = Quat(-b.x, -b.y, -b.z, -b.w), d = -d;
  if (d > 0.9995f) return normalize(Quat(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t));
  const float th = std::acos(std::min(1.0f, d)), s = std::sin(th), wa = std::sin((1 - t) * th) / s, wb = std::sin(t * th) / s;
  return normalize(Quat(a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb));
}

/* The rig's bone for each canonical joint (null where the rig has none: its turn is its parent's). */
std::vector<GameObject *> bones_of(const gait::Rig &rig) {
  std::vector<GameObject *> b(kJoints, nullptr);
  b[Hips] = rig.hips, b[HeadJ] = rig.head;
  for (const gait::Leg &l : rig.legs) {
    const int o = l.side ? RUpLeg : LUpLeg;
    b[o] = l.upper, b[o + 1] = l.lower, b[o + 2] = l.foot, b[o + 3] = l.toe;
  }
  for (const gait::Arm &a : rig.arms) {
    const int o = a.side ? RShoulder : LShoulder;
    b[o] = a.shoulder, b[o + 1] = a.upper, b[o + 2] = a.lower, b[o + 3] = a.hand;
  }
  std::vector<GameObject *> s = rig.spine;
  GameObject *neck = nullptr;
  if (!s.empty() && to_lower(s.back()->name).find("neck") != std::string::npos) neck = s.back(), s.pop_back();
  b[Neck] = neck;
  if (s.size() == 1) b[Spine] = s[0];
  if (s.size() == 2) b[Spine] = s[0], b[Chest] = s[1];
  if (s.size() >= 3) b[Spine] = s[0], b[Spine1] = s[s.size() / 2], b[Chest] = s.back();
  return b;
}

/* A bone's world turn at rest: its rest turns up the chain of bones, then the armature's turn now. */
Quat rest_world(const GameObject &g) {
  Quat r;
  const GameObject *a = &g;
  for (; a && a->get<Bone>(); a = a->parent) r = normalize(a->get<Bone>()->rest_rotation * r);
  return a ? normalize(a->world_rotation() * r) : r;
}

/* How the rig is mapped: its bones, their rest turns, the turn taking its facing to +Z, its size against the data's,
 * and the ground under it (the data's rest has the legs splayed and the arms out: `align` turns the rig's onto it). */
using Mapping = PoseMapping;

/* The joint each joint's bone points at (-1: none, or several - the hips, the ends). */
int child_of(int j) {
  static const int c[kJoints] = {-1, LLeg, LFoot, LToes, -1, RLeg, RFoot, RToes, -1, Spine1, Chest, Neck, HeadJ, -1,
                                 LArm, LForeArm, LHand, -1, RArm, RForeArm, RHand, -1};
  return c[j];
}

Mapping mapping_of(const gait::Rig &rig, const ModelInfo &info) {
  Mapping m;
  m.bones = bones_of(rig);
  for (GameObject *g : m.bones) m.rest.push_back(g ? rest_world(*g) : Quat());
  const Vec3 f = normalize(Vec3(rig.forward.x, 0.0f, rig.forward.z));
  m.face = Quat::axis_angle(Vec3(0, 1, 0), -std::atan2(f.x, f.z));
  m.align.assign(kJoints, Quat());
  for (int j = 0; j < kJoints; j++) {
    const int c = child_of(j);
    if (c < 0 || !m.bones[(size_t)j] || !(length(info.offsets[(size_t)c]) > 1e-4f)) continue;
    const Vec3 rig_dir = normalize(mirror(m.face.rotate(m.rest[(size_t)j].rotate(Vec3(0, 1, 0)))));  // (a bone points along its +Y)
    m.align[(size_t)j] = rotation_between(rig_dir, normalize(info.offsets[(size_t)c]));
  }
  for (int j = 1; j < kJoints; j++)  // (an end - a hand, the head, the toes - points nowhere: its parent's, so at rest it is)
    if (child_of(j) < 0 && m.bones[(size_t)j]) m.align[(size_t)j] = m.align[(size_t)info.parents[(size_t)j]];
  const float data_leg = length(info.offsets[LLeg]) + length(info.offsets[LFoot]);
  m.scale = data_leg > 1e-4f && rig.leg_length > 1e-4f ? rig.leg_length / data_leg : 1.0f;
  m.ground = rig.rest_hips.y - rig.hip_height - rig.sole;
  return m;
}

/* The rig as it is posed now, as one frame of the data (the root's place, each joint's turn from its parent). */
void frame_of(const Mapping &m, const ModelInfo &info, float *f) {
  Quat world[kJoints];
  for (int j = 0; j < kJoints; j++) {
    const GameObject *g = m.bones[(size_t)j];
    if (!g) {
      world[j] = info.parents[(size_t)j] >= 0 ? world[info.parents[(size_t)j]] : Quat();
      continue;
    }
    const Quat from_rest = normalize(g->world_rotation() * m.rest[(size_t)j].conjugate());  // (the turn from rest, in the world)
    world[j] = normalize(mirror(normalize(m.face * from_rest * m.face.conjugate())) * m.align[(size_t)j].conjugate());  // (G = D A^-1)
  }
  const Vec3 hips = m.bones[Hips]->world_position();
  const Vec3 p = mirror(m.face.rotate(Vec3(hips.x, hips.y - m.ground, hips.z))) * (1.0f / m.scale);
  f[0] = p.x, f[1] = p.y, f[2] = p.z;
  for (int j = 0; j < kJoints; j++) {
    const int pa = info.parents[(size_t)j];
    put6(f + 3 + j * 6, pa >= 0 ? normalize(world[pa].conjugate() * world[j]) : world[j]);
  }
}

/* One frame of the data put on the rig: the hips placed, each mapped bone's world turn (the root down). */
void pose_rig(const Mapping &m, const ModelInfo &info, const float *f) {
  Quat world[kJoints];
  for (int j = 0; j < kJoints; j++) {
    const int pa = info.parents[(size_t)j];
    const Quat local = get6(f + 3 + j * 6);
    world[j] = pa >= 0 ? normalize(world[pa] * local) : local;
  }
  const Vec3 p = m.face.conjugate().rotate(mirror(Vec3(f[0], f[1], f[2]) * m.scale));
  m.bones[Hips]->set_world_position(Vec3(p.x, p.y + m.ground, p.z));
  for (int j = 0; j < kJoints; j++) {
    GameObject *g = m.bones[(size_t)j];
    if (!g) continue;
    const Quat from_rest = normalize(m.face.conjugate() * mirror(normalize(world[j] * m.align[(size_t)j])) * m.face);  // (D = G A)
    g->set_world_rotation(normalize(from_rest * m.rest[(size_t)j]));
  }
}

/* The scene posed at a (fractional) frame of the clip, IK solved; `fn` reads it. Put back as it was after. */
void at_frames(Scene &scene, GameObject &root, const AnimationClip &clip, const std::vector<float> &frames, const std::function<void(size_t)> &fn) {
  std::vector<std::pair<GameObject *, Transform>> saved;
  scene.for_each([&](GameObject &g) { saved.push_back({&g, g.local()}); });
  for (size_t i = 0; i < frames.size(); i++) {
    apply_clip(clip, root, frames[i]);
    solve_constraints(scene);
    fn(i);
  }
  for (const auto &s : saved) s.first->set_local(s.second);
  solve_constraints(scene);
}

/* The model's space: the last context frame's hips at x = z = 0, facing +Z (as tools/inbetween/train.py's normalise). */
void to_model_space(std::vector<float> &frames, int W, int last, Mat4 &to_world, std::vector<float> *extra = nullptr) {
  const Quat hips = get6(&frames[(size_t)last * kFeatures + 3]);
  const Vec3 f = hips.rotate(Vec3(0, 0, 1));
  const float yaw = std::atan2(f.x, f.z);
  const Quat turn = Quat::axis_angle(Vec3(0, 1, 0), -yaw);
  const Vec3 origin(frames[(size_t)last * kFeatures], 0.0f, frames[(size_t)last * kFeatures + 2]);
  for (int t = 0; t < W; t++) {
    float *fr = &frames[(size_t)t * kFeatures];
    const Vec3 p = turn.rotate(Vec3(fr[0], fr[1], fr[2]) - origin);
    fr[0] = p.x, fr[1] = p.y, fr[2] = p.z;
    put6(fr + 3, normalize(turn * get6(fr + 3)));
  }
  for (size_t t = 0; extra && t * kFeatures < extra->size(); t++) {
    float *fr = &(*extra)[t * kFeatures];
    const Vec3 p = turn.rotate(Vec3(fr[0], fr[1], fr[2]) - origin);
    fr[0] = p.x, fr[1] = p.y, fr[2] = p.z;
    put6(fr + 3, normalize(turn * get6(fr + 3)));
  }
  to_world = Mat4::translate(origin) * Mat4::rotate(turn.conjugate());
}

void from_model_space(float *fr, const Mat4 &to_world, Quat back) {
  const Vec3 p = to_world.point(Vec3(fr[0], fr[1], fr[2]));
  fr[0] = p.x, fr[1] = p.y, fr[2] = p.z;
  put6(fr + 3, normalize(back * get6(fr + 3)));
}

uint32_t hash(uint32_t x) {
  x ^= x >> 16, x *= 0x7feb352d, x ^= x >> 15, x *= 0x846ca68b, x ^= x >> 16;
  return x;
}

}  // namespace

void interpolate_gap(std::vector<float> &frames, int ctx, int gap) {
  const float *a = &frames[(size_t)(ctx - 1) * kFeatures], *b = &frames[(size_t)(ctx + gap) * kFeatures];
  for (int i = 1; i <= gap; i++) {
    const float t = (float)i / (float)(gap + 1);
    float *f = &frames[(size_t)(ctx - 1 + i) * kFeatures];
    for (int c = 0; c < 3; c++) f[c] = a[c] + (b[c] - a[c]) * t;
    for (int j = 0; j < kJoints; j++) put6(f + 3 + j * 6, slerp_q(get6(a + 3 + j * 6), get6(b + 3 + j * 6), t));
  }
}

Request make_request(Scene &scene, GameObject &anim_root, const gait::Rig &rig, const AnimationClip &clip, const Span &span, const ModelInfo &info) {
  Request r;
  r.span = span;
  r.clip_gap = span.to - span.from - 1;
  r.step = std::max(0.25f, (clip.frame_rate > 0.0f ? clip.frame_rate : 30.0f) / 30.0f);  // (the model learned 30 frames a second)
  r.gap = std::max(1, (int)std::lround((float)(span.to - span.from) / r.step) - 1);
  if (!info.valid()) return r.problem = "the model's description is missing", r;
  if (!rig.valid()) return r.problem = rig.problem, r;
  if (rig.quadruped || rig.legs.size() != 2) return r.problem = "AI inbetweening is for two-legged characters", r;
  if (r.clip_gap < 1 || r.gap > kMaxGap)
    return r.problem = strprintf("a span of 1 frame to %.1f s between two keys (this one: %d frames)", (kMaxGap + 1) / 30.0f, std::max(0, r.clip_gap)), r;
  if (span.style < 0 || span.style >= (int)info.styles.size()) return r.problem = "no such style", r;
  if (span.from < 0 || (float)span.to > clip.end_frame() + 0.5f) return r.problem = "a span inside the clip (its keys are past its end)", r;
  const Mapping m = mapping_of(rig, info);
  r.scale = m.scale;
  r.W = info.ctx + r.gap + 1;
  r.frames.assign((size_t)r.W * kFeatures, 0.0f);
  r.known.assign((size_t)r.W, 0.0f);
  /* The context: the frames up to the first key (a variation takes them a little faster or slower), then the target. */
  const float pace = span.seed ? 0.85f + 0.3f * (float)(hash(span.seed) % 1000u) / 999.0f : 1.0f;
  std::vector<float> at;
  for (int i = 0; i < info.ctx; i++) at.push_back(std::max(0.0f, (float)span.from - (float)(info.ctx - 1 - i) * pace * r.step));
  at.push_back((float)span.to);
  at_frames(scene, anim_root, clip, at, [&](size_t i) {
    const size_t row = i < (size_t)info.ctx ? i : (size_t)(r.W - 1);
    frame_of(m, info, &r.frames[row * kFeatures]);
    r.known[row] = 1.0f;
  });
  to_model_space(r.frames, r.W, info.ctx - 1, r.to_world);
  interpolate_gap(r.frames, info.ctx, r.gap);
  return r;
}

namespace {
/* The model's window at a fraction `u` of the way from the first key to the second: between its two nearest frames
 * (the root lerped, the turns slerped). */
std::vector<float> model_frame_at(const std::vector<float> &w, int ctx, int gap, float u) {
  const float k = std::max(0.0f, std::min((float)(gap + 1), u * (float)(gap + 1)));
  const int a = std::min(gap, (int)std::floor(k));
  const float t = k - (float)a;
  const float *fa = &w[(size_t)(ctx - 1 + a) * kFeatures], *fb = &w[(size_t)(ctx + a) * kFeatures];
  std::vector<float> f((size_t)kFeatures);
  for (int c = 0; c < 3; c++) f[(size_t)c] = fa[c] + (fb[c] - fa[c]) * t;
  for (int j = 0; j < kJoints; j++) put6(&f[(size_t)(3 + j * 6)], slerp_q(get6(fa + 3 + j * 6), get6(fb + 3 + j * 6), t));
  return f;
}

Vec3 euler_near(Vec3 e, Vec3 close) {
  auto wrap = [&](Vec3 v) {
    for (int k = 0; k < 3; k++) {
      while (v[k] - close[k] > 180.0f) v[k] -= 360.0f;
      while (v[k] - close[k] < -180.0f) v[k] += 360.0f;
    }
    return v;
  };
  const Vec3 a = wrap(e), b = wrap(Vec3(180.0f - e.x, e.y + 180.0f, e.z + 180.0f));
  auto off_by = [&](Vec3 v) { return std::fabs(v.x - close.x) + std::fabs(v.y - close.y) + std::fabs(v.z - close.z); };
  return off_by(b) < off_by(a) ? b : a;
}

/* A track's keys at the span's ends: kept, with their slopes outside the span frozen (Broken, as they were - an Auto
 * slope would otherwise follow the new keys inside and bend the curve outside); added where the track had none. The
 * keys inside removed. */
void write_bounds(AnimTrack &t, float from, float to, float v0, float v1) {
  for (float at : {from, to}) {
    const int i = t.key_at(at);
    if (i < 0) continue;
    const float in = t.in_tangent((size_t)i), out = t.out_tangent((size_t)i);
    AnimKey &k = t.keys[(size_t)i];
    k.tangent = TanBroken, k.in_slope = in, k.out_slope = out;
  }
  t.keys.erase(std::remove_if(t.keys.begin(), t.keys.end(), [&](const AnimKey &k) { return k.frame > from + 0.01f && k.frame < to - 0.01f; }), t.keys.end());
  if (t.key_at(from) < 0) t.set_key(from, v0);
  if (t.key_at(to) < 0) t.set_key(to, v1);
}

/* The span's turns as Euler angles, continuous from the key at `from`. When the key at `to` names the same turn whole
 * turns away (a 200 degree yaw keyed, the network's answer the short way), that difference is spread over the span:
 * the curve meets the key instead of spinning round on the last frame. */
std::vector<Vec3> span_eulers(const std::vector<Transform> &tr, int n, Vec3 at_from, Vec3 at_to) {
  std::vector<Vec3> e;
  Vec3 prev = at_from;
  for (int i = 0; i < n; i++) e.push_back(prev = euler_near(tr[(size_t)i].rotation.to_euler(), prev));
  const Vec3 meets = euler_near(tr[(size_t)n + 1].rotation.to_euler(), n ? e.back() : at_from);
  for (int a = 0; a < 3; a++) {
    const float off = at_to[a] - meets[a], whole = std::round(off / 360.0f) * 360.0f;
    if (whole == 0.0f || std::fabs(off - whole) > 1.0f) continue;  // (the same turn, or a different Euler form: left)
    for (int i = 0; i < n; i++) e[(size_t)i][a] += whole * (float)(i + 1) / (float)(n + 1);
  }
  return e;
}

/* One object's span written: its ends (each channel), then every frame inside. `tr`: its local transform on each
 * inside frame, then at `from` and at `to`. */
void write_span(AnimationClip &out, const std::string &path, bool pos, const std::vector<Transform> &tr, int n, float from, float to) {
  const Transform &e0 = tr[(size_t)n], &e1 = tr[(size_t)n + 1];
  for (int a = 0; a < 3; a++) {
    write_bounds(out.track(path, "Rotation", a), from, to, e0.rotation.to_euler()[a], e1.rotation.to_euler()[a]);
    if (pos) write_bounds(out.track(path, "Position", a), from, to, e0.position[a], e1.position[a]);
  }
  Vec3 at_from, at_to;
  for (int a = 0; a < 3; a++) at_from[a] = out.track(path, "Rotation", a).sample(from), at_to[a] = out.track(path, "Rotation", a).sample(to);
  const std::vector<Vec3> e = span_eulers(tr, n, at_from, at_to);
  for (int i = 0; i < n; i++)
    for (int a = 0; a < 3; a++) {
      out.track(path, "Rotation", a).set_key(from + 1.0f + (float)i, e[(size_t)i][a], AnimLinear);
      if (pos) out.track(path, "Position", a).set_key(from + 1.0f + (float)i, tr[(size_t)i].position[a], AnimLinear);
    }
}

/* Every object the span keys: the mapped bones, and the limbs' IK controls under the animated object (IK then holds
 * the pose); whether its place is keyed too. */
struct Keyed {
  GameObject *g;
  bool pos;
};
std::vector<Keyed> keyed_of(Scene &scene, GameObject &root, const gait::Rig &rig, const Mapping &m) {
  std::vector<Keyed> k;
  auto under = [&](const GameObject *g) { return g && (g == &root || scene.is_ancestor(&root, g)); };
  auto add = [&](GameObject *g, bool pos) {
    if (!under(g) || std::find_if(k.begin(), k.end(), [&](const Keyed &x) { return x.g == g; }) != k.end()) return;
    k.push_back({g, pos});
  };
  for (size_t j = 0; j < m.bones.size(); j++) add(m.bones[j], j == Hips);
  for (const gait::Leg &l : rig.legs) add(l.ik_target, true), add(l.ik_pole, true);
  for (const gait::Arm &a : rig.arms) add(a.ik_target, true), add(a.ik_pole, true);
  return k;
}

/* The limbs' IK controls where the posed limbs end (the target on the foot or hand, the pole out along the bend). */
void place_controls(const gait::Rig &rig) {
  auto place = [](GameObject *target, GameObject *pole, GameObject *upper, GameObject *lower, GameObject *end) {
    if (!target || !upper || !lower || !end) return;
    target->set_world_position(end->world_position());
    if (!pole) return;
    const Vec3 a = upper->world_position(), b = lower->world_position(), line = end->world_position() - a;
    const Vec3 bend = (b - a) - line * (dot(b - a, line) / std::max(1e-9f, dot(line, line)));
    if (length(bend) > 1e-4f) pole->set_world_position(b + normalize(bend) * std::max(0.3f, length(line)));
  };
  for (const gait::Leg &l : rig.legs) place(l.ik_target, l.ik_pole, l.upper, l.lower, l.foot);
  for (const gait::Arm &a : rig.arms) place(a.ik_target, a.ik_pole, a.upper, a.lower, a.hand);
}
}  // namespace

int apply(Scene &scene, GameObject &anim_root, const gait::Rig &rig, const AnimationClip &clip, const Request &req, const std::vector<float> &result,
          const ModelInfo &info, AnimationClip &out, std::string &error, std::vector<std::vector<std::pair<Vec3, Vec3>>> *poses) {
  out = clip;
  if (!req.problem.empty()) return error = req.problem, 0;
  if ((int)result.size() != req.W * kFeatures) return error = "the model's answer is the wrong size", 0;
  for (float v : result)
    if (!std::isfinite(v)) return error = "the model's answer isn't a number", 0;
  const Mapping m = mapping_of(rig, info);
  const std::vector<Keyed> keyed = keyed_of(scene, anim_root, rig, m);
  /* Each made frame: the clip's pose there, the mapped bones turned to the model's, the IK controls following. */
  std::vector<float> at;
  for (int i = 1; i <= req.clip_gap; i++) at.push_back((float)(req.span.from + i));
  at.push_back((float)req.span.from), at.push_back((float)req.span.to);
  std::vector<std::vector<Transform>> locals(at.size());
  /* The model's space back to the data's world: the turn about +Y in to_world. */
  const Vec3 zx = req.to_world.point(Vec3(0, 0, 1)) - req.to_world.point(Vec3(0, 0, 0));
  const Quat turn = Quat::axis_angle(Vec3(0, 1, 0), std::atan2(zx.x, zx.z));
  if (poses) poses->assign((size_t)req.clip_gap, {});
  at_frames(scene, anim_root, clip, at, [&](size_t i) {
    if ((int)i < req.clip_gap) {
      std::vector<float> fr = model_frame_at(result, info.ctx, req.gap, (float)(i + 1) / (float)(req.clip_gap + 1));
      from_model_space(fr.data(), req.to_world, turn);
      pose_rig(m, info, fr.data());
      place_controls(rig);
      solve_constraints(scene);
      if (poses)
        for (GameObject *g : m.bones)
          if (g && g->get<Bone>()) (*poses)[i].push_back({g->world_position(), g->world_matrix().point(Vec3(0, g->get<Bone>()->length, 0))});
    }
    for (const Keyed &k : keyed) locals[i].push_back(k.g->local());
  });
  /* The keys: inside the span every frame (what was there replaced); the keys at its ends kept, their slopes outside
   * frozen, and added where a track had none there - so the clip outside the span is as it was. */
  const float from = (float)req.span.from, to = (float)req.span.to;
  for (size_t ki = 0; ki < keyed.size(); ki++) {
    std::vector<Transform> track_of;
    for (const std::vector<Transform> &l : locals) track_of.push_back(l[ki]);
    write_span(out, anim_path(anim_root, *keyed[ki].g), keyed[ki].pos, track_of, req.clip_gap, from, to);
  }
  return req.clip_gap;
}

std::vector<float> clip_frames(Scene &scene, GameObject &anim_root, const gait::Rig &rig, const AnimationClip &clip, const ModelInfo &info, int &frames) {
  std::vector<float> rows;
  frames = 0;
  if (!info.valid() || !rig.valid() || rig.quadruped || rig.legs.size() != 2) return rows;
  const Mapping m = mapping_of(rig, info);
  const float step = std::max(0.25f, (clip.frame_rate > 0.0f ? clip.frame_rate : 30.0f) / 30.0f);  // (at the model's 30 a second)
  frames = std::max(0, std::min(20000, (int)std::floor(clip.end_frame() / step) + 1));
  rows.assign((size_t)frames * kFeatures, 0.0f);
  std::vector<float> at;
  for (int f = 0; f < frames; f++) at.push_back((float)f * step);
  at_frames(scene, anim_root, clip, at, [&](size_t i) { frame_of(m, info, &rows[i * kFeatures]); });
  return rows;
}

Request window_of(const std::vector<float> &rows, int frames, const Span &span, const ModelInfo &info, std::vector<float> *truth) {
  Request r;
  r.span = span;
  r.gap = r.clip_gap = span.to - span.from - 1;  // (the rows are at the model's rate already)
  if (r.gap < 1 || r.gap > kMaxGap || span.from < 0 || span.to >= frames) return r.problem = "a span outside the clip", r;
  r.W = info.ctx + r.gap + 1;
  r.frames.assign((size_t)r.W * kFeatures, 0.0f);
  r.known.assign((size_t)r.W, 0.0f);
  auto row = [&](int f) { return &rows[(size_t)std::max(0, std::min(frames - 1, f)) * kFeatures]; };
  for (int t = 0; t < r.W; t++) {
    const bool ctx = t < info.ctx, target = t == r.W - 1;
    const int f = span.from - (info.ctx - 1) + t;
    if (!ctx && !target) continue;
    std::copy(row(f), row(f) + kFeatures, &r.frames[(size_t)t * kFeatures]);
    r.known[(size_t)t] = 1.0f;
  }
  if (truth) {
    truth->assign((size_t)r.W * kFeatures, 0.0f);
    for (int t = 0; t < r.W; t++) std::copy(row(span.from - (info.ctx - 1) + t), row(span.from - (info.ctx - 1) + t) + kFeatures, &(*truth)[(size_t)t * kFeatures]);
  }
  to_model_space(r.frames, r.W, info.ctx - 1, r.to_world, truth);
  interpolate_gap(r.frames, info.ctx, r.gap);
  return r;
}

std::string head_text(const Head &h) {
  std::string s = strprintf("blendity-inbetween-head 1\nd %d\nwindows %d\nbefore %.6g\nafter %.6g\n", h.d, h.windows, h.before, h.after);
  for (size_t i = 0; i < h.w.size(); i++) s += strprintf(i % 8 == 7 ? "%.7g\n" : "%.7g ", h.w[i]);
  return s + "\n";
}

bool head_from_text(const std::string &text, Head &out) {
  Head h;
  const std::vector<std::string> t = split_ws(text);
  if (t.size() < 10 || t[0] != "blendity-inbetween-head" || t[2] != "d" || t[4] != "windows" || t[6] != "before" || t[8] != "after") return false;
  h.d = std::atoi(t[3].c_str()), h.windows = std::atoi(t[5].c_str());
  h.before = (float)std::atof(t[7].c_str()), h.after = (float)std::atof(t[9].c_str());
  if (h.d <= 0 || h.d > 4096 || t.size() != 10 + (size_t)(h.d + 1) * kFeatures) return false;
  for (size_t i = 10; i < t.size(); i++) {
    const float v = (float)std::atof(t[i].c_str());
    if (!std::isfinite(v)) return false;
    h.w.push_back(v);
  }
  out = h;
  return true;
}

void apply_head(const Head &h, const std::vector<float> &hidden, const std::vector<float> &known, int W, std::vector<float> &out) {
  if (!h.valid() || (int)hidden.size() != W * h.d || (int)out.size() != W * kFeatures) return;
  for (int t = 0; t < W; t++) {
    if (known[(size_t)t] != 0.0f) continue;
    const float *x = &hidden[(size_t)t * h.d];
    float *o = &out[(size_t)t * kFeatures];
    for (int c = 0; c < kFeatures; c++) {
      double v = h.w[(size_t)h.d * kFeatures + c];  // (the bias)
      for (int k = 0; k < h.d; k++) v += (double)x[k] * h.w[(size_t)k * kFeatures + c];
      o[c] += (float)v;
    }
  }
}

void Fit::add(const std::vector<float> &hidden, const std::vector<float> &known, int W, const std::vector<float> &out, const std::vector<float> &truth) {
  if (d <= 0 || (int)hidden.size() != W * d || (int)out.size() != W * kFeatures || truth.size() != out.size()) return;
  const int n = d + 1;
  if (hth.empty()) hth.assign((size_t)n * n, 0.0), hty.assign((size_t)n * kFeatures, 0.0), yty.assign((size_t)kFeatures, 0.0);
  std::vector<double> x((size_t)n);
  for (int t = 0; t < W; t++) {
    if (known[(size_t)t] != 0.0f) continue;
    for (int k = 0; k < d; k++) x[(size_t)k] = hidden[(size_t)t * d + k];
    x[(size_t)d] = 1.0;
    for (int a = 0; a < n; a++)
      for (int b = 0; b <= a; b++) hth[(size_t)a * n + b] += x[(size_t)a] * x[(size_t)b];
    for (int c = 0; c < kFeatures; c++) {
      const double y = (double)truth[(size_t)t * kFeatures + c] - out[(size_t)t * kFeatures + c];
      yty[(size_t)c] += y * y;
      for (int a = 0; a < n; a++) hty[(size_t)a * kFeatures + c] += x[(size_t)a] * y;
    }
    rows++;
  }
  windows++;
}

double Fit::rms_before() const {
  double s = 0.0;
  for (double v : yty) s += v;
  return rows ? std::sqrt(s / ((double)rows * kFeatures)) : 0.0;
}

double Fit::rms_after(const Head &h) const {
  /* Each column's |y - Xw|^2 = y'y - 2 w'X'y + w'X'Xw (X'X kept as its lower triangle). */
  const int n = d + 1;
  if (!rows || !h.valid() || h.d != d) return rms_before();
  double s = 0.0;
  for (int c = 0; c < kFeatures; c++) {
    double xy = 0.0, xx = 0.0;
    for (int a = 0; a < n; a++) {
      const double wa = h.w[(size_t)a * kFeatures + c];
      xy += wa * hty[(size_t)a * kFeatures + c];
      for (int b = 0; b < n; b++) xx += wa * h.w[(size_t)b * kFeatures + c] * hth[a >= b ? (size_t)a * n + b : (size_t)b * n + a];
    }
    s += std::max(0.0, yty[(size_t)c] - 2.0 * xy + xx);
  }
  return std::sqrt(s / ((double)rows * kFeatures));
}

bool Fit::solve(float lambda, Head &h) const {
  const int n = d + 1;
  if (rows < 1 || hth.empty()) return false;
  /* (H'H + lambda I) W = H'Y by Cholesky (the bias not held back). */
  std::vector<double> L((size_t)n * n, 0.0);
  for (int a = 0; a < n; a++)
    for (int b = 0; b <= a; b++) {
      double s = hth[(size_t)a * n + b] + (a == b && a < d ? (double)lambda * rows : 0.0) + (a == b ? 1e-9 : 0.0);
      for (int k = 0; k < b; k++) s -= L[(size_t)a * n + k] * L[(size_t)b * n + k];
      if (a == b) {
        if (!(s > 0.0)) return false;
        L[(size_t)a * n + a] = std::sqrt(s);
      } else {
        L[(size_t)a * n + b] = s / L[(size_t)b * n + b];
      }
    }
  h.d = d, h.windows = windows;
  h.w.assign((size_t)n * kFeatures, 0.0f);
  std::vector<double> z((size_t)n);
  for (int c = 0; c < kFeatures; c++) {
    for (int a = 0; a < n; a++) {
      double s = hty[(size_t)a * kFeatures + c];
      for (int k = 0; k < a; k++) s -= L[(size_t)a * n + k] * z[(size_t)k];
      z[(size_t)a] = s / L[(size_t)a * n + a];
    }
    for (int a = n - 1; a >= 0; a--) {
      double s = z[(size_t)a];
      for (int k = a + 1; k < n; k++) s -= L[(size_t)k * n + a] * h.w[(size_t)k * kFeatures + c];
      h.w[(size_t)a * kFeatures + c] = (float)(s / L[(size_t)a * n + a]);
    }
  }
  return true;
}

bool probe(std::string &error, double *ms) {
  const std::unique_ptr<Runtime> rt = load_runtime(install_dir(), error);
  if (!rt) return false;
  /* Two standing poses 60 frames apart (every joint at rest), as the data's skeleton: the answer must be finite. */
  const int W = rt->info.ctx + 60 + 1;
  std::vector<float> frames((size_t)W * kFeatures, 0.0f), known((size_t)W, 0.0f), out;
  for (int t = 0; t < W; t++) {
    frames[(size_t)t * kFeatures + 1] = 0.95f;
    for (int j = 0; j < kJoints; j++) put6(&frames[(size_t)t * kFeatures + 3 + j * 6], Quat());
    known[(size_t)t] = t < rt->info.ctx || t == W - 1 ? 1.0f : 0.0f;
  }
  const auto t0 = std::chrono::steady_clock::now();
  if (!rt->run(frames, known, 0, W, out, error)) return false;
  if (ms) *ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  for (float v : out)
    if (!std::isfinite(v)) return error = "the test inference gave numbers that aren't", false;
  return true;
}

PoseMapping pose_mapping(const gait::Rig &rig, const ModelInfo &info) { return mapping_of(rig, info); }
void rig_to_frame(const PoseMapping &m, const ModelInfo &info, float *f) { frame_of(m, info, f); }
void frame_to_rig(const PoseMapping &m, const ModelInfo &info, const float *f) { pose_rig(m, info, f); }
Vec3 world_to_data(const PoseMapping &m, Vec3 w) { return mirror(m.face.rotate(Vec3(w.x, w.y - m.ground, w.z))) * (1.0f / m.scale); }
Vec3 data_to_world(const PoseMapping &m, Vec3 d) {
  const Vec3 p = m.face.conjugate().rotate(mirror(d * m.scale));
  return Vec3(p.x, p.y + m.ground, p.z);
}

}  // namespace ai
}  // namespace bl
