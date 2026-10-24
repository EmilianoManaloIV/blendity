// SPDX-License-Identifier: GPL-2.0-or-later
#include "optix_device.h"

#include "../core/core.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>

#if defined(BL_WITH_OPTIX)
/* As Cycles does (intern/cycles/device/optix/util.h): the CUDA driver API through cuew (loaded at run time from the
 * driver), then OptiX's headers without CUDA's own; optixInit loads the driver's nvoptix library. */
#  include <cuew.h>
#  define OPTIX_DONT_INCLUDE_CUDA
#  include <optix.h>
#  include <optix_stubs.h>
#  include <optix_function_table_definition.h>
#  include "optix_kernel_ptx.h"  // the committed PTX (src/render/optix, tools/optix_ptx_embed.bat)
#  include "optix_params.h"
/* The host headers must be the OptiX the PTX was built against (task 0169): another SDK's function table and structs
 * with this PTX would fail at run time, or worse. */
static_assert(OPTIX_VERSION == BL_OPTIX_PTX_OPTIX_VERSION, "OptiX's host headers aren't the committed PTX's version: use the pinned "
                                                          "headers (BLENDITY_FETCH_OPTIX=1, build.bat's OPTIX_DEV_TAG) or run tools/optix_ptx_embed.bat");
#endif

namespace bl::gpu {

namespace {
std::atomic<int> g_backend{0};
bool env_off() {
  const char *off = std::getenv("BLENDITY_NO_OPTIX");
  return off && std::string(off) == "1";
}
}  // namespace

void set_backend(int b) {
  b = std::max(0, std::min(2, b));
  const int was = g_backend.exchange(b);
  if ((was == 2) != (b == 2)) drop_ready_renderers();  // (the pool held renderers for the old choice; Auto and Vulkan are the same)
}
/* BLENDITY_GPU_BACKEND (auto | vulkan | optix) wins over the setting: the tests and the stress run pin GPU Compute so
 * their numbers don't change with the card's driver, and a test asks for OptiX by name. */
int backend() {
  const char *e = std::getenv("BLENDITY_GPU_BACKEND");
  if (e && *e) {
    const std::string s = e;
    return s == "optix" ? 2 : s == "vulkan" ? 1 : 0;
  }
  return g_backend.load();
}
/* Task 0170: OptiX only when asked for. Auto keeps GPU Compute: with the whole integrator in one kernel, every value
 * live across optixTrace is saved on OptiX's continuation stack, and it measured about three times slower than Vulkan's
 * inline ray queries on the same RT cores (ADR 0054's amendment); a wavefront integrator would change that. */
namespace {
std::mutex g_optix_m;
std::vector<int> g_failed;  // cards whose OptiX open failed
std::atomic<int> g_listed{0};  // 0 not yet, 1 listing, 2 listed
}  // namespace
void optix_failed(int index) {
  std::lock_guard<std::mutex> lock(g_optix_m);
  g_failed.push_back(index);
}
bool use_optix(int index) {
  if (backend() != 2 || !optix_compiled_in()) return false;
  if (index < 0 || index >= (int)devices().size() || !devices()[(size_t)index].hardware_rt) return false;  // (no RT: GPU Compute in software)
  {
    std::lock_guard<std::mutex> lock(g_optix_m);
    if (std::find(g_failed.begin(), g_failed.end(), index) != g_failed.end()) return false;
  }
  if (g_listed.load() != 2) {
    optix_list_async();
    return false;  // (not listed yet: GPU Compute meanwhile, the card reopened once the list is in)
  }
  return optix_device_of(index) >= 0;
}

#if !defined(BL_WITH_OPTIX)

bool optix_compiled_in() { return false; }
std::vector<std::string> optix_devices(std::string *error) {
  if (error) *error = "this build has no OptiX (OptiX's headers weren't there when it was built)";
  return {};
}
std::string optix_status() { return "OptiX: not in this build"; }
int optix_device_of(int) { return -1; }
void optix_list_async() {}
std::unique_ptr<Renderer> create_optix_renderer(int, std::string *error) {
  if (error) *error = "this build has no OptiX";
  return nullptr;
}

#else

namespace {

bool cuda_ready(std::string *error) {
  static const int ok = [] {
    if (cuewInit(CUEW_INIT_CUDA) != CUEW_SUCCESS) return 1;
    return cuInit(0) == CUDA_SUCCESS ? 0 : 2;
  }();
  if (ok == 0) return true;
  if (error) *error = ok == 1 ? "no NVIDIA driver (nvcuda) on this system" : "the CUDA driver didn't start";
  return false;
}
bool optix_ready(std::string *error) {
  static const OptixResult r = optixInit();
  if (r == OPTIX_SUCCESS) return true;
  if (error) *error = std::string("the driver has no OptiX (") + optixGetErrorName(r) + ")";
  return false;
}
/* The committed PTX's pieces as one text (MSVC limits a string literal; the header splits it). */
const std::string &ptx_text() {
  static const std::string s = [] {
    std::string t;
    t.reserve(BL_OPTIX_PTX_BYTES);
    for (const char *part : optix_kernel_ptx_parts) t += part;
    return t;
  }();
  return s;
}

struct Buf {
  CUdeviceptr ptr = 0;
  size_t size = 0;
};

class OptixRenderer final : public Renderer {
 public:
  ~OptixRenderer() override { destroy(); }

  bool init(int index, int cuda_device, std::string *err) {
    info_ = devices()[(size_t)index];
    info_.type = "OptiX";
    if (!cu(cuDeviceGet(&dev_, cuda_device), "cuDeviceGet", err)) return false;
    /* The card's primary context, shared by every renderer on it (the Camera Preview, F12, the pool's spare). */
    if (!cu(cuDevicePrimaryCtxRetain(&ctx_, dev_), "cuDevicePrimaryCtxRetain", err)) return false;
    cuCtxSetCurrent(ctx_);
    if (!cu(cuStreamCreate(&stream_, CU_STREAM_NON_BLOCKING), "cuStreamCreate", err)) return false;
    if (!cu(cuEventCreate(&ev_[0], 0), "cuEventCreate", err) || !cu(cuEventCreate(&ev_[1], 0), "cuEventCreate", err)) return false;
    OptixDeviceContextOptions o{};
    if (!ox(optixDeviceContextCreate(ctx_, &o, &octx_), "optixDeviceContextCreate", err)) return false;
    ScopedTimer t;
    const bool ok = make_pipeline(err);
    stats_.pipeline_ms = t.ms();
    return ok;
  }

  OpenStats open_stats() const override { return stats_; }

  /* The scene's buffers as the Vulkan device has them, then a triangle structure per mesh and the instances over
   * them (one per object with a mesh, its id the object's index: the kernel's `obj`). */
  bool upload(const Scene &s, std::string *err) override {
    cuCtxSetCurrent(ctx_);
    submits_++;
    if (!upload_buffers(s, err)) return false;
    return build_structures(s, err);
  }

  void set_params(const GParams &p) override {
    cuCtxSetCurrent(ctx_);
    params_ = p;
    const size_t pixels = (size_t)std::max(1, p.size[0]) * std::max(1, p.size[1]);
    std::string e;
    if (pixels != pixels_) {
      pixels_ = 0;  // (render() refuses until both buffers are there: a launch on a null buffer kills the context)
      if (!alloc(accum_, pixels * 16, &e) || !alloc(aux_, pixels * 48, &e)) {  // albedo, normal + depth, coverage (task 0035)
        Log::warn("%s", e.c_str());
        return;
      }
      pixels_ = pixels;
    }
    if (!params_buf_.ptr && !alloc(params_buf_, sizeof(GParams), &e)) {  // (once: cuMemFree synchronises the context)
      pixels_ = 0;
      return;
    }
    cuMemcpyHtoD(params_buf_.ptr, &p, sizeof(GParams));
    launch_.params = params_buf_.ptr, launch_.accum = accum_.ptr, launch_.aux = aux_.ptr;  // (the frame's buffers into the launch)
    clear();
  }

  void clear() override {
    cuCtxSetCurrent(ctx_);
    if (accum_.ptr) cuMemsetD32(accum_.ptr, 0, accum_.size / 4);
    if (aux_.ptr) cuMemsetD32(aux_.ptr, 0, aux_.size / 4);
    cuStreamSynchronize(stream_);
  }

  /* Up to eight samples a launch, as the Vulkan device dispatches them (each pixel sums them in registers); the card's
   * own time from two events round the launches. */
  bool render(int first, int count, std::string *err) override {
    cuCtxSetCurrent(ctx_);
    if (!pipeline_ || pixels_ == 0) return fail(err, "OptiX not ready");
    const int w = std::max(1, params_.size[0]), h = std::max(1, params_.size[1]);
    cuEventRecord(ev_[0], stream_);
    constexpr int kSamplesPerLaunch = 8;
    for (int i = 0; i < count; i += kSamplesPerLaunch) {
      launch_.sample_index = first + i;
      launch_.sample_count = std::min(kSamplesPerLaunch, count - i);
      if (!cu(cuMemcpyHtoDAsync(launch_buf_.ptr, &launch_, sizeof(launch_), stream_), "the launch's parameters", err)) return false;
      if (!ox(optixLaunch(pipeline_, stream_, launch_buf_.ptr, sizeof(launch_), &sbt_, (unsigned)w, (unsigned)h, 1), "optixLaunch", err)) return false;
      submits_++;
    }
    cuEventRecord(ev_[1], stream_);
    if (!cu(cuStreamSynchronize(stream_), info_.name + ": the samples", err)) return false;
    float ms = -1.0f;
    last_gpu_ms_ = cuEventElapsedTime(&ms, ev_[0], ev_[1]) == CUDA_SUCCESS ? ms : -1.0;
    busy_ += std::max(0.0, last_gpu_ms_);
    return true;
  }
  double take_busy_ms() override {
    const double b = busy_;
    busy_ = 0.0;
    return b;
  }
  double last_gpu_ms() const override { return last_gpu_ms_; }
  uint64_t submits() const override { return submits_; }

  void download_accum(std::vector<float> &accum) override {
    cuCtxSetCurrent(ctx_);
    accum.resize(pixels_ * 4);
    cuMemcpyDtoH(accum.data(), accum_.ptr, pixels_ * 16);
  }
  void download_aux(std::vector<float> &albedo, std::vector<float> &normal_depth, std::vector<float> *coverage) override {
    cuCtxSetCurrent(ctx_);
    albedo.resize(pixels_ * 4);
    normal_depth.resize(pixels_ * 4);
    cuMemcpyDtoH(albedo.data(), aux_.ptr, pixels_ * 16);
    cuMemcpyDtoH(normal_depth.data(), aux_.ptr + pixels_ * 16, pixels_ * 16);
    if (coverage) {
      coverage->resize(pixels_ * 4);
      cuMemcpyDtoH(coverage->data(), aux_.ptr + pixels_ * 32, pixels_ * 16);
    }
  }
  /* Baking: the points where the kernel reads them (the aux buffer's start, after set_params). */
  bool set_points(const float *data, size_t floats, std::string *err) override {
    cuCtxSetCurrent(ctx_);
    if (!aux_.ptr || floats * 4 > aux_.size) return fail(err, "bake points don't fit");
    return cu(cuMemcpyHtoD(aux_.ptr, data, floats * 4), "the bake points", err);
  }

  const DeviceInfo &info() const override { return info_; }
  bool using_hardware_rt() const override { return true; }

 private:
  struct alignas(OPTIX_SBT_RECORD_ALIGNMENT) Record {
    char header[OPTIX_SBT_RECORD_HEADER_SIZE];
  };
  DeviceInfo info_;
  OpenStats stats_;
  CUdevice dev_ = 0;
  CUcontext ctx_ = nullptr;
  CUstream stream_ = nullptr;
  CUevent ev_[2] = {};
  OptixDeviceContext octx_ = nullptr;
  OptixModule module_ = nullptr;
  OptixProgramGroup groups_[3] = {};
  OptixPipeline pipeline_ = nullptr;
  OptixShaderBindingTable sbt_{};
  OptixLaunchParams launch_{};
  GParams params_{};
  size_t pixels_ = 0;
  double last_gpu_ms_ = -1.0, busy_ = 0.0;
  uint64_t submits_ = 0;
  Buf sbt_buf_, launch_buf_, params_buf_, accum_, aux_, ias_, index3_;
  Buf scene_[11];  // insts, verts, idx, mats, lights, mlights, mcdf, tex8, texf, texinfo, instances
  std::vector<Buf> gas_;

  static bool fail(std::string *err, const std::string &what) {
    if (err) *err = what;
    return false;
  }
  static bool cu(CUresult r, const std::string &what, std::string *err) {
    return r == CUDA_SUCCESS ? true : fail(err, strprintf("%s failed (CUDA error %d)", what.c_str(), (int)r));
  }
  static bool ox(OptixResult r, const char *what, std::string *err) {
    return r == OPTIX_SUCCESS ? true : fail(err, strprintf("%s failed (%s)", what, optixGetErrorName(r)));
  }
  void release(Buf &b) {
    if (b.ptr) cuMemFree(b.ptr);
    b = Buf{};
  }
  bool alloc(Buf &b, size_t bytes, std::string *err) {
    release(b);
    if (!cu(cuMemAlloc(&b.ptr, std::max<size_t>(bytes, 16)), info_.name + ": memory", err)) return false;
    b.size = bytes;
    return true;
  }
  bool upload_bytes(Buf &b, const void *data, size_t bytes, std::string *err) {
    if (!alloc(b, bytes, err)) return false;
    return !bytes || cu(cuMemcpyHtoD(b.ptr, data, bytes), info_.name + ": an upload", err);
  }
  template<class T> bool upload_vec(Buf &b, const std::vector<T> &v, std::string *err) { return upload_bytes(b, v.data(), v.size() * sizeof(T), err); }

  bool upload_buffers(const Scene &s, std::string *err) {
    const bool ok = upload_vec(scene_[0], s.instances, err) && upload_vec(scene_[1], s.verts, err) && upload_vec(scene_[2], s.idx, err) &&
                    upload_vec(scene_[3], s.materials, err) && upload_vec(scene_[4], s.lights, err) && upload_vec(scene_[5], s.mesh_lights, err) &&
                    upload_vec(scene_[6], s.mesh_cdf, err) && upload_vec(scene_[7], s.tex8, err) && upload_vec(scene_[8], s.texf, err) &&
                    upload_vec(scene_[9], s.textures, err);
    if (!ok) return false;
    /* The triangles' indices without the material slot, three a triangle (as the Vulkan device builds its BLASes). */
    std::vector<uint32_t> i3(s.idx.size() / 4 * 3);
    for (size_t t = 0; t < s.idx.size() / 4; t++)
      for (int k = 0; k < 3; k++) i3[t * 3 + k] = s.idx[t * 4 + k];
    if (!upload_vec(index3_, i3, err)) return false;
    launch_.insts = scene_[0].ptr, launch_.verts = scene_[1].ptr, launch_.idx = scene_[2].ptr, launch_.mats = scene_[3].ptr;
    launch_.lights = scene_[4].ptr, launch_.mlights = scene_[5].ptr, launch_.mcdf = scene_[6].ptr, launch_.tex8 = scene_[7].ptr;
    launch_.texf = scene_[8].ptr, launch_.texinfo = scene_[9].ptr;
    return true;
  }

  bool build_structures(const Scene &s, std::string *err) {
    launch_.handle = 0;  // (the old structures go first: no launch may see their handle)
    release(ias_);
    for (Buf &b : gas_) release(b);
    gas_.assign(s.blas.size(), Buf{});
    std::vector<OptixTraversableHandle> gas(s.blas.size(), 0);
    for (size_t k = 0; k < s.blas.size(); k++)
      if (!build_gas(s.blas[k], gas_[k], gas[k], err)) return false;
    if (s.object_blas.size() != s.instances.size()) return fail(err, info_.name + ": the instances and their meshes disagree");
    std::vector<OptixInstance> inst;
    for (size_t o = 0; o < s.instances.size(); o++) {
      const uint32_t b = s.object_blas[o];
      if (b == UINT32_MAX || b >= gas.size()) continue;  // (an object without a mesh stays out)
      OptixInstance in{};
      const float *m = s.instances[o].to_world;  // column-major
      for (int r = 0; r < 3; r++)
        for (int c = 0; c < 4; c++) in.transform[r * 4 + c] = m[c * 4 + r];
      in.instanceId = (unsigned)o;
      in.visibilityMask = 255;
      in.flags = OPTIX_INSTANCE_FLAG_DISABLE_TRIANGLE_FACE_CULLING | OPTIX_INSTANCE_FLAG_DISABLE_ANYHIT;
      in.traversableHandle = gas[b];
      inst.push_back(in);
    }
    launch_.handle = 0;
    if (inst.empty()) return true;  // (nothing to hit: the kernel's rays all miss)
    if (!upload_vec(scene_[10], inst, err)) return false;
    OptixBuildInput bi{};
    bi.type = OPTIX_BUILD_INPUT_TYPE_INSTANCES;
    bi.instanceArray.instances = scene_[10].ptr;
    bi.instanceArray.numInstances = (unsigned)inst.size();
    OptixTraversableHandle h = 0;
    if (!accel(bi, OPTIX_BUILD_FLAG_PREFER_FAST_TRACE, ias_, h, err)) return false;
    launch_.handle = h;
    return true;
  }

  bool build_gas(const GBlas &gb, Buf &out, OptixTraversableHandle &h, std::string *err) {
    CUdeviceptr vb = scene_[1].ptr + (CUdeviceptr)gb.vtx_off * sizeof(GVertex);
    const unsigned geo_flags[1] = {OPTIX_GEOMETRY_FLAG_DISABLE_ANYHIT};
    OptixBuildInput bi{};
    bi.type = OPTIX_BUILD_INPUT_TYPE_TRIANGLES;
    bi.triangleArray.vertexBuffers = &vb;
    bi.triangleArray.numVertices = std::max(1u, gb.vtx_count);
    bi.triangleArray.vertexFormat = OPTIX_VERTEX_FORMAT_FLOAT3;
    bi.triangleArray.vertexStrideInBytes = sizeof(GVertex);
    bi.triangleArray.indexBuffer = index3_.ptr + (CUdeviceptr)gb.idx_off * 12;
    bi.triangleArray.numIndexTriplets = gb.tri_count;
    bi.triangleArray.indexFormat = OPTIX_INDICES_FORMAT_UNSIGNED_INT3;
    bi.triangleArray.indexStrideInBytes = 12;
    bi.triangleArray.flags = geo_flags;
    bi.triangleArray.numSbtRecords = 1;
    return accel(bi, OPTIX_BUILD_FLAG_PREFER_FAST_TRACE, out, h, err);
  }

  /* An acceleration structure from one build input (its memory sized by OptiX), into `out`. */
  bool accel(const OptixBuildInput &bi, unsigned flags, Buf &out, OptixTraversableHandle &h, std::string *err) {
    OptixAccelBuildOptions ao{};
    ao.buildFlags = flags;
    ao.operation = OPTIX_BUILD_OPERATION_BUILD;
    OptixAccelBufferSizes sz{};
    if (!ox(optixAccelComputeMemoryUsage(octx_, &ao, &bi, 1, &sz), "optixAccelComputeMemoryUsage", err)) return false;
    Buf temp;
    if (!alloc(temp, sz.tempSizeInBytes, err) || !alloc(out, sz.outputSizeInBytes, err)) {
      release(temp);
      return false;
    }
    const bool ok = ox(optixAccelBuild(octx_, stream_, &ao, &bi, 1, temp.ptr, sz.tempSizeInBytes, out.ptr, sz.outputSizeInBytes, &h, nullptr, 0),
                       "optixAccelBuild", err) &&
                    cu(cuStreamSynchronize(stream_), info_.name + ": the build", err);
    release(temp);
    return ok;
  }

  bool make_pipeline(std::string *err) {
    OptixModuleCompileOptions mco{};
    mco.optLevel = OPTIX_COMPILE_OPTIMIZATION_DEFAULT;
    mco.debugLevel = OPTIX_COMPILE_DEBUG_LEVEL_NONE;
    mco.maxRegisterCount = 255;  // (fewer spills round optixTrace: 1.5x faster than the default limit on the 4070 SUPER)
    OptixPipelineCompileOptions pco{};
    pco.traversableGraphFlags = OPTIX_TRAVERSABLE_GRAPH_FLAG_ALLOW_SINGLE_LEVEL_INSTANCING;
    pco.numPayloadValues = 5;
    pco.numAttributeValues = 2;
    pco.exceptionFlags = OPTIX_EXCEPTION_FLAG_NONE;
    pco.pipelineLaunchParamsVariableName = "LP";
    pco.usesPrimitiveTypeFlags = OPTIX_PRIMITIVE_TYPE_FLAGS_TRIANGLE;
    char log[4096];
    size_t ls = sizeof(log);
    const std::string &ptx = ptx_text();
    if (optixModuleCreate(octx_, &mco, &pco, ptx.data(), ptx.size(), log, &ls, &module_) != OPTIX_SUCCESS)
      return fail(err, std::string("the OptiX module didn't load: ") + log);
    OptixProgramGroupDesc d[3] = {};
    d[0].kind = OPTIX_PROGRAM_GROUP_KIND_RAYGEN;
    d[0].raygen.module = module_;
    d[0].raygen.entryFunctionName = "__raygen__render";
    d[1].kind = OPTIX_PROGRAM_GROUP_KIND_MISS;
    d[1].miss.module = module_;
    d[1].miss.entryFunctionName = "__miss__miss";
    d[2].kind = OPTIX_PROGRAM_GROUP_KIND_HITGROUP;
    d[2].hitgroup.moduleCH = module_;
    d[2].hitgroup.entryFunctionNameCH = "__closesthit__hit";
    OptixProgramGroupOptions pgo{};
    ls = sizeof(log);
    if (optixProgramGroupCreate(octx_, d, 3, &pgo, log, &ls, groups_) != OPTIX_SUCCESS) return fail(err, std::string("the program groups: ") + log);
    OptixPipelineLinkOptions plo{};
    plo.maxTraceDepth = 1;
    ls = sizeof(log);
    if (optixPipelineCreate(octx_, &pco, &plo, groups_, 3, log, &ls, &pipeline_) != OPTIX_SUCCESS) return fail(err, std::string("the pipeline: ") + log);
    Record rec[3];
    for (int i = 0; i < 3; i++) optixSbtRecordPackHeader(groups_[i], &rec[i]);
    if (!upload_bytes(sbt_buf_, rec, sizeof(rec), err) || !alloc(launch_buf_, sizeof(OptixLaunchParams), err)) return false;
    sbt_.raygenRecord = sbt_buf_.ptr;
    sbt_.missRecordBase = sbt_buf_.ptr + sizeof(Record);
    sbt_.missRecordStrideInBytes = sizeof(Record);
    sbt_.missRecordCount = 1;
    sbt_.hitgroupRecordBase = sbt_buf_.ptr + 2 * sizeof(Record);
    sbt_.hitgroupRecordStrideInBytes = sizeof(Record);
    sbt_.hitgroupRecordCount = 1;
    return true;
  }

  void destroy() {
    if (!ctx_) return;
    cuCtxSetCurrent(ctx_);
    for (Buf &b : gas_) release(b);
    for (Buf &b : scene_) release(b);
    for (Buf *b : {&sbt_buf_, &launch_buf_, &params_buf_, &accum_, &aux_, &ias_, &index3_}) release(*b);
    if (pipeline_) optixPipelineDestroy(pipeline_);
    for (OptixProgramGroup g : groups_)
      if (g) optixProgramGroupDestroy(g);
    if (module_) optixModuleDestroy(module_);
    if (octx_) optixDeviceContextDestroy(octx_);
    for (CUevent e : ev_)
      if (e) cuEventDestroy(e);
    if (stream_) cuStreamDestroy(stream_);
    cuDevicePrimaryCtxRelease(dev_);
    ctx_ = nullptr;
  }
};

}  // namespace

bool optix_compiled_in() { return !env_off(); }

std::vector<std::string> optix_devices(std::string *error) {
  std::vector<std::string> out;
  if (!optix_compiled_in()) {
    if (error) *error = "OptiX turned off (BLENDITY_NO_OPTIX=1)";
    return out;
  }
  int n = 0;
  if (!cuda_ready(error) || !optix_ready(error) || cuDeviceGetCount(&n) != CUDA_SUCCESS) return out;
  for (int i = 0; i < n; i++) {
    CUdevice d;
    char name[256] = {};
    if (cuDeviceGet(&d, i) == CUDA_SUCCESS && cuDeviceGetName(name, sizeof(name), d) == CUDA_SUCCESS) out.push_back(name);
  }
  return out;
}

std::string optix_status() {
  if (!optix_compiled_in()) return "OptiX: turned off (BLENDITY_NO_OPTIX=1)";
  std::string err;
  const std::vector<std::string> devs = optix_devices(&err);
  if (devs.empty()) return "OptiX: no driver (" + (err.empty() ? std::string("no NVIDIA device") : err) + ")";
  std::string line = "OptiX: " + std::to_string(devs.size()) + " device(s):";
  for (const std::string &d : devs) line += " " + d + ";";
  return line;
}

/* The CUDA devices that can run the kernel (compute 7.5+), by name; listed once. */
const std::vector<std::string> &optix_cards() {
  static const std::vector<std::string> cards = [] {
    std::vector<std::string> out;
    const std::vector<std::string> names = optix_devices(nullptr);
    for (size_t i = 0; i < names.size(); i++) {
      CUdevice d;
      int major = 0, minor = 0;
      if (cuDeviceGet(&d, (int)i) == CUDA_SUCCESS) {
        cuDeviceGetAttribute(&major, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR, d);
        cuDeviceGetAttribute(&minor, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR, d);
      }
      out.push_back(major * 10 + minor >= 75 ? names[i] : std::string());  // (an older card: listed empty, never matched)
    }
    return out;
  }();
  return cards;
}

void optix_list_async() {
  int expected = 0;
  if (!g_listed.compare_exchange_strong(expected, 1)) return;
  std::thread([] {
    (void)optix_cards();
    g_listed = 2;
  }).detach();
}

int optix_device_of(int index) {
  const std::vector<std::string> &cuda = optix_cards();
  g_listed = 2;
  const std::vector<DeviceInfo> &vk = devices();
  if (index < 0 || index >= (int)vk.size()) return -1;
  /* The k-th card of this name among Vulkan's is the k-th of this name among CUDA's. */
  int nth = 0;
  for (int i = 0; i < index; i++) nth += vk[(size_t)i].name == vk[(size_t)index].name;
  for (size_t c = 0; c < cuda.size(); c++)
    if (cuda[c] == vk[(size_t)index].name && nth-- == 0) return (int)c;
  return -1;
}

std::unique_ptr<Renderer> create_optix_renderer(int index, std::string *error) {
  if (!optix_compiled_in()) {
    if (error) *error = "OptiX turned off (BLENDITY_NO_OPTIX=1)";
    return nullptr;
  }
  if (!cuda_ready(error) || !optix_ready(error)) return nullptr;
  const int c = optix_device_of(index);
  if (c < 0) {
    if (error) *error = "this card has no OptiX device";
    return nullptr;
  }
  auto r = std::make_unique<OptixRenderer>();
  if (!r->init(index, c, error)) return nullptr;
  return r;
}

#endif

}  // namespace bl::gpu
