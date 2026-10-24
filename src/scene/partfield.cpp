// SPDX-License-Identifier: GPL-2.0-or-later
// PartField's features through ONNX Runtime (task 0180): see partfield.h. The library is the AI install's (ADR 0046),
// loaded only when the switch is on and the model is there.
#include "partfield.h"

#include "../core/core.h"
#include "../../extern/onnxruntime/onnxruntime_c_api.h"
#include "inbetween.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <thread>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

namespace bl::partfield {

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
  return (void *)LoadLibraryExW(wide.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
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

class OrtModel : public Model {
 public:
  ~OrtModel() override {
    if (!api_) return;
    if (session_) api_->ReleaseSession(session_);
    if (mem_) api_->ReleaseMemoryInfo(mem_);
    if (env_) api_->ReleaseEnv(env_);  // (the library stays loaded: unloading ONNX Runtime isn't safe while threads wind down)
  }

  bool load(std::string &error) {
    void *lib = open_library(fs::join(ai::install_dir(), kLib));
    if (!lib) return error = std::string(kLib) + " didn't load (the AI install: ai install)", false;
    using GetBase = const OrtApiBase *(ORT_API_CALL *)();
    GetBase get = (GetBase)symbol(lib, "OrtGetApiBase");
    const OrtApiBase *base = get ? get() : nullptr;
    api_ = base ? base->GetApi(ORT_API_VERSION) : nullptr;
    if (!api_) return error = "ONNX Runtime's API " + std::to_string(ORT_API_VERSION) + " isn't there", false;
    OrtSessionOptions *so = nullptr;
    if (!ok(api_->CreateEnv(ORT_LOGGING_LEVEL_ERROR, "blendity-partfield", &env_), error) || !ok(api_->CreateSessionOptions(&so), error)) return false;
    ok(api_->SetIntraOpNumThreads(so, std::max(1, (int)std::thread::hardware_concurrency() / 2)), error);
    const std::string model = fs::join(install_dir(), kModelFile);
#ifdef _WIN32
    const int n = MultiByteToWideChar(CP_UTF8, 0, model.c_str(), -1, nullptr, 0);
    std::wstring path(n > 0 ? (size_t)n : 0, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, model.c_str(), -1, &path[0], n);
    const bool made = ok(api_->CreateSession(env_, path.c_str(), so, &session_), error);
#else
    const bool made = ok(api_->CreateSession(env_, model.c_str(), so, &session_), error);
#endif
    api_->ReleaseSessionOptions(so);
    return made && ok(api_->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &mem_), error);
  }

  bool run(const std::vector<float> &points, size_t n, int &dims, std::vector<float> &out, std::string &error) override {
    if (points.size() != n * 6) return error = "the points aren't n x 6", false;
    std::vector<float> p = points;
    const int64_t shape[3] = {1, (int64_t)n, 6};
    OrtValue *in = nullptr, *got = nullptr;
    const char *in_name = "points", *out_name = "features";
    bool good = ok(api_->CreateTensorWithDataAsOrtValue(mem_, p.data(), p.size() * sizeof(float), shape, 3, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &in), error);
    if (good) good = ok(api_->Run(session_, nullptr, &in_name, &in, 1, &out_name, 1, &got), error);
    if (good) good = read_output(got, n, dims, out, error);
    if (in) api_->ReleaseValue(in);
    if (got) api_->ReleaseValue(got);
    return good;
  }

 private:
  /* The output [1, n, D]: D read from its shape (a model of another shape is refused, not read past). */
  bool read_output(OrtValue *v, size_t n, int &dims, std::vector<float> &out, std::string &error) {
    OrtTensorTypeAndShapeInfo *ts = nullptr;
    if (!ok(api_->GetTensorTypeAndShape(v, &ts), error)) return false;
    size_t rank = 0;
    int64_t shape[3] = {0, 0, 0};
    bool good = ok(api_->GetDimensionsCount(ts, &rank), error) && rank == 3 && ok(api_->GetDimensions(ts, shape, 3), error);
    api_->ReleaseTensorTypeAndShapeInfo(ts);
    if (!good || shape[0] != 1 || shape[1] != (int64_t)n || shape[2] <= 0 || shape[2] > 4096)
      return error = "the model's output isn't [1, faces, features]", false;
    float *f = nullptr;
    if (!ok(api_->GetTensorMutableData(v, (void **)&f), error)) return false;
    dims = (int)shape[2];
    out.assign(f, f + n * (size_t)dims);
    return true;
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

std::string install_dir() {
  const char *env = std::getenv("BLENDITY_PARTFIELD_DIR");
  if (env && *env) return env;
  return fs::join(fs::join(fs::join(fs::home_dir(), ".blendity"), "ai"), "partfield");
}

std::string install_problem() {
  if (!fs::exists(fs::join(install_dir(), kModelFile)))
    return "PartField isn't installed: convert the model you obtained from NVIDIA with tools/partfield/export_onnx.py into " +
           fs::join(install_dir(), kModelFile) + " (its weights are non-commercial; Blendity doesn't download them)";
  if (!fs::exists(fs::join(ai::install_dir(), kLib))) return "ONNX Runtime isn't installed (ai install brings it)";
  return "";
}

std::unique_ptr<Model> load(std::string &error) {
  error = install_problem();
  if (!error.empty()) return nullptr;
  auto m = std::make_unique<OrtModel>();
  if (!m->load(error)) return nullptr;
  return m;
}

std::vector<float> face_points(const Mesh &m) {
  AABB box;
  for (const Vec3 &p : m.positions) box.add(p);
  const Vec3 centre = (box.min + box.max) * 0.5f, ext = box.max - box.min;
  const float scale = 1.0f / std::max(1e-12f, std::max(ext.x, std::max(ext.y, ext.z)));
  std::vector<float> pts;
  pts.reserve(m.face_count() * 6);
  for (size_t f = 0; f < m.face_count(); f++) {
    Vec3 c(0.0f);
    const uint32_t *v = m.face_verts(f);
    for (uint32_t i = 0; i < m.face_size(f); i++) c += m.positions[v[i]];
    c = (c / (float)std::max(1u, m.face_size(f)) - centre) * scale;
    const Vec3 n = m.face_normal(f);
    for (float x : {c.x, c.y, c.z, n.x, n.y, n.z}) pts.push_back(x);
  }
  return pts;
}

bool face_features(Model &model, const Mesh &m, int &dims, std::vector<float> &out, std::string &error) {
  const size_t n = m.face_count();
  if (n == 0 || n > kMaxPoints) return error = strprintf("PartField is asked about at most %zu faces (this mesh has %zu)", kMaxPoints, n), false;
  if (!model.run(face_points(m), n, dims, out, error)) return false;
  for (size_t f = 0; f < n; f++) {  // (unit rows: the part tree's Ward distances then compare directions, as PartUV's clustering does)
    float *row = out.data() + f * (size_t)dims;
    double len = 0;
    for (int k = 0; k < dims; k++) len += (double)row[k] * row[k];
    const float s = len > 1e-24 ? (float)(1.0 / std::sqrt(len)) : 0.0f;
    for (int k = 0; k < dims; k++) row[k] *= s;
  }
  return true;
}

}  // namespace bl::partfield
