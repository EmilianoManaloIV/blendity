// SPDX-License-Identifier: GPL-2.0-or-later
#include "optix_spike.h"

#include "../core/core.h"

#include <algorithm>
#include <cstring>

#if defined(BL_WITH_OPTIX)
/* As Cycles does (intern/cycles/device/optix/util.h): the CUDA driver API through cuew (loaded at run time from the
 * driver), then OptiX's headers without CUDA's own; optixInit loads the driver's nvoptix library. */
#  include <cuew.h>
#  define OPTIX_DONT_INCLUDE_CUDA
#  include <optix.h>
#  include <optix_stubs.h>
#  include <optix_function_table_definition.h>
#  include "optix_kernel_ptx.h"  // the PTX build.bat made with NVRTC (tools/ptx_compile.cpp) and bin2c
#  include "optix_params.h"
#endif

namespace bl::gpu {

#if !defined(BL_WITH_OPTIX)

bool OptixTracer::compiled_in() { return false; }
std::vector<std::string> OptixTracer::devices(std::string *error) {
  if (error) *error = "this build has no OptiX (the CUDA Toolkit and the OptiX SDK weren't there when it was built)";
  return {};
}
std::unique_ptr<OptixTracer> OptixTracer::create(int, std::string *error) {
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

struct Buf {
  CUdeviceptr ptr = 0;
  size_t size = 0;
};

class OptixTracerImpl final : public OptixTracer {
 public:
  ~OptixTracerImpl() override { destroy(); }

  bool init(int device, std::string *err) {
    CUdevice dev;
    char name[256] = {};
    if (!cu(cuDeviceGet(&dev, device), "cuDeviceGet", err)) return false;
    cuDeviceGetName(name, sizeof(name), dev);
    name_ = name;
    if (!cu(cuCtxCreate(&ctx_, 0, dev), "cuCtxCreate", err) || !cu(cuStreamCreate(&stream_, 0), "cuStreamCreate", err)) return false;
    if (!ox(optixInit(), "optixInit (the driver's OptiX)", err)) return false;
    OptixDeviceContextOptions o{};
    if (!ox(optixDeviceContextCreate(ctx_, &o, &octx_), "optixDeviceContextCreate", err)) return false;
    return make_pipeline(err);
  }

  bool build(const std::vector<OptixMeshIn> &meshes, const std::vector<OptixObjectIn> &objects, double &ms, std::string *err) override {
    cuCtxSetCurrent(ctx_);
    ScopedTimer t;
    free_scene();
    std::vector<OptixTraversableHandle> gas(meshes.size(), 0);
    for (size_t i = 0; i < meshes.size(); i++)
      if (!build_gas(meshes[i], gas[i], err)) return false;
    std::vector<OptixInstance> inst(objects.size());
    std::vector<float> albedo(objects.size() * 4, 1.0f);
    for (size_t i = 0; i < objects.size(); i++) {
      const OptixObjectIn &o = objects[i];
      if (o.mesh < 0 || (size_t)o.mesh >= gas.size()) return fail(err, "an object of a mesh that isn't there");
      OptixInstance &in = inst[i];
      std::memset(&in, 0, sizeof(in));
      for (int r = 0; r < 3; r++)
        for (int c = 0; c < 4; c++) in.transform[r * 4 + c] = o.model[c * 4 + r];  // (row-major 3 x 4)
      in.instanceId = (unsigned)i;
      in.visibilityMask = 255;
      in.flags = OPTIX_INSTANCE_FLAG_NONE;
      in.traversableHandle = gas[(size_t)o.mesh];
      for (int k = 0; k < 3; k++) albedo[i * 4 + k] = o.albedo[k];
    }
    Buf ib;
    if (!upload(ib, inst.data(), inst.size() * sizeof(OptixInstance), err)) return false;
    if (!upload(albedo_, albedo.data(), albedo.size() * 4, err)) {
      release(ib);
      return false;
    }
    OptixBuildInput bi{};
    bi.type = OPTIX_BUILD_INPUT_TYPE_INSTANCES;
    bi.instanceArray.instances = ib.ptr;
    bi.instanceArray.numInstances = (unsigned)inst.size();
    const bool ok = accel(bi, OPTIX_BUILD_FLAG_PREFER_FAST_TRACE, ias_, handle_, err);
    release(ib);
    if (!ok || !cu(cuStreamSynchronize(stream_), "build", err)) return false;
    ms = t.ms();
    return true;
  }

  bool clear(int w, int h, std::string *err) override {
    cuCtxSetCurrent(ctx_);
    const size_t bytes = (size_t)w * h * 16;
    if (accum_.size != bytes) {
      release(accum_);
      if (!cu(cuMemAlloc(&accum_.ptr, bytes), "cuMemAlloc", err)) return false;
      accum_.size = bytes;
    }
    w_ = w, h_ = h;
    return cu(cuMemsetD32(accum_.ptr, 0, bytes / 4), "cuMemsetD32", err);
  }

  bool render(const OptixFrameIn &f, int first_sample, std::string *err) override {
    cuCtxSetCurrent(ctx_);
    if (f.width != w_ || f.height != h_) return fail(err, "clear() the frame's size first");
    OptixParams p{};
    p.accum = (float *)(uintptr_t)accum_.ptr;
    p.handle = handle_;
    p.albedo = (const float *)(uintptr_t)albedo_.ptr;
    std::memcpy(p.inv_view_proj, f.inv_view_proj, 64);
    for (int k = 0; k < 3; k++) {
      p.sun_dir[k] = f.sun_dir[k], p.sun_color[k] = f.sun_color[k];
      p.sky[k] = f.sky[k], p.equator[k] = f.equator[k], p.ground[k] = f.ground[k];
    }
    p.width = f.width, p.height = f.height, p.first_sample = first_sample, p.samples = f.samples, p.max_bounces = f.max_bounces;
    if (!params_.ptr && !cu(cuMemAlloc(&params_.ptr, sizeof(p)), "cuMemAlloc", err)) return false;
    if (!cu(cuMemcpyHtoD(params_.ptr, &p, sizeof(p)), "params", err)) return false;
    if (!ox(optixLaunch(pipeline_, stream_, params_.ptr, sizeof(p), &sbt_, (unsigned)f.width, (unsigned)f.height, 1), "optixLaunch", err)) return false;
    return cu(cuStreamSynchronize(stream_), "the launch", err);
  }

  bool read(std::vector<float> &rgb, std::string *err) override {
    cuCtxSetCurrent(ctx_);
    std::vector<float> acc((size_t)w_ * h_ * 4);
    if (!cu(cuMemcpyDtoH(acc.data(), accum_.ptr, acc.size() * 4), "read", err)) return false;
    rgb.resize((size_t)w_ * h_ * 3);
    for (size_t i = 0; i < (size_t)w_ * h_; i++) {
      const float n = acc[i * 4 + 3] > 0 ? 1.0f / acc[i * 4 + 3] : 0.0f;
      for (int k = 0; k < 3; k++) rgb[i * 3 + k] = acc[i * 4 + k] * n;
    }
    return true;
  }

  const std::string &name() const override { return name_; }

 private:
  struct alignas(OPTIX_SBT_RECORD_ALIGNMENT) Record {
    char header[OPTIX_SBT_RECORD_HEADER_SIZE];
  };
  std::string name_;
  CUcontext ctx_ = nullptr;
  CUstream stream_ = nullptr;
  OptixDeviceContext octx_ = nullptr;
  OptixModule module_ = nullptr;
  OptixProgramGroup groups_[5] = {};
  OptixPipeline pipeline_ = nullptr;
  OptixShaderBindingTable sbt_{};
  Buf sbt_buf_, params_, accum_, albedo_, ias_;
  std::vector<Buf> gas_, mesh_bufs_;
  OptixTraversableHandle handle_ = 0;
  int w_ = 0, h_ = 0;

  static bool fail(std::string *err, const std::string &what) {
    if (err) *err = what;
    return false;
  }
  static bool cu(CUresult r, const char *what, std::string *err) {
    return r == CUDA_SUCCESS ? true : fail(err, strprintf("%s failed (CUDA error %d)", what, (int)r));
  }
  static bool ox(OptixResult r, const char *what, std::string *err) {
    return r == OPTIX_SUCCESS ? true : fail(err, strprintf("%s failed (%s)", what, optixGetErrorName(r)));
  }
  bool upload(Buf &b, const void *data, size_t bytes, std::string *err) {
    release(b);
    if (!cu(cuMemAlloc(&b.ptr, std::max<size_t>(bytes, 16)), "cuMemAlloc", err)) return false;
    b.size = bytes;
    return !bytes || cu(cuMemcpyHtoD(b.ptr, data, bytes), "cuMemcpyHtoD", err);
  }
  void release(Buf &b) {
    if (b.ptr) cuMemFree(b.ptr);
    b = Buf{};
  }

  bool make_pipeline(std::string *err) {
    OptixModuleCompileOptions mco{};
    mco.optLevel = OPTIX_COMPILE_OPTIMIZATION_DEFAULT;
    mco.debugLevel = OPTIX_COMPILE_DEBUG_LEVEL_NONE;
    OptixPipelineCompileOptions pco{};
    pco.traversableGraphFlags = OPTIX_TRAVERSABLE_GRAPH_FLAG_ALLOW_SINGLE_LEVEL_INSTANCING;
    pco.numPayloadValues = 6;
    pco.numAttributeValues = 2;
    pco.exceptionFlags = OPTIX_EXCEPTION_FLAG_NONE;
    pco.pipelineLaunchParamsVariableName = "params";
    pco.usesPrimitiveTypeFlags = OPTIX_PRIMITIVE_TYPE_FLAGS_TRIANGLE;
    char log[4096];
    size_t ls = sizeof(log);
    if (optixModuleCreate(octx_, &mco, &pco, (const char *)optix_kernel_ptx, sizeof(optix_kernel_ptx), log, &ls, &module_) != OPTIX_SUCCESS)
      return fail(err, std::string("the OptiX module didn't load: ") + log);
    OptixProgramGroupDesc d[5] = {};
    d[0].kind = OPTIX_PROGRAM_GROUP_KIND_RAYGEN;
    d[0].raygen.module = module_;
    d[0].raygen.entryFunctionName = "__raygen__render";
    d[1].kind = OPTIX_PROGRAM_GROUP_KIND_MISS;
    d[1].miss.module = module_;
    d[1].miss.entryFunctionName = "__miss__radiance";
    d[2].kind = OPTIX_PROGRAM_GROUP_KIND_MISS;
    d[2].miss.module = module_;
    d[2].miss.entryFunctionName = "__miss__shadow";
    d[3].kind = OPTIX_PROGRAM_GROUP_KIND_HITGROUP;
    d[3].hitgroup.moduleCH = module_;
    d[3].hitgroup.entryFunctionNameCH = "__closesthit__radiance";
    d[4].kind = OPTIX_PROGRAM_GROUP_KIND_HITGROUP;  // (shadow rays: no programs)
    OptixProgramGroupOptions pgo{};
    ls = sizeof(log);
    if (optixProgramGroupCreate(octx_, d, 5, &pgo, log, &ls, groups_) != OPTIX_SUCCESS) return fail(err, std::string("the program groups: ") + log);
    OptixPipelineLinkOptions plo{};
    plo.maxTraceDepth = 1;
    ls = sizeof(log);
    if (optixPipelineCreate(octx_, &pco, &plo, groups_, 5, log, &ls, &pipeline_) != OPTIX_SUCCESS) return fail(err, std::string("the pipeline: ") + log);
    Record rec[5];
    for (int i = 0; i < 5; i++) optixSbtRecordPackHeader(groups_[i], &rec[i]);
    if (!upload(sbt_buf_, rec, sizeof(rec), err)) return false;
    sbt_.raygenRecord = sbt_buf_.ptr;
    sbt_.missRecordBase = sbt_buf_.ptr + sizeof(Record);
    sbt_.missRecordStrideInBytes = sizeof(Record);
    sbt_.missRecordCount = 2;
    sbt_.hitgroupRecordBase = sbt_buf_.ptr + 3 * sizeof(Record);
    sbt_.hitgroupRecordStrideInBytes = sizeof(Record);
    sbt_.hitgroupRecordCount = 2;
    return true;
  }

  /* An acceleration structure from one build input (its memory sized by OptiX), into `out`. */
  bool accel(const OptixBuildInput &bi, unsigned flags, Buf &out, OptixTraversableHandle &h, std::string *err) {
    OptixAccelBuildOptions ao{};
    ao.buildFlags = flags;
    ao.operation = OPTIX_BUILD_OPERATION_BUILD;
    OptixAccelBufferSizes sz{};
    if (!ox(optixAccelComputeMemoryUsage(octx_, &ao, &bi, 1, &sz), "optixAccelComputeMemoryUsage", err)) return false;
    Buf temp;
    release(out);
    if (!cu(cuMemAlloc(&temp.ptr, std::max<size_t>(sz.tempSizeInBytes, 16)), "cuMemAlloc", err)) return false;
    if (!cu(cuMemAlloc(&out.ptr, std::max<size_t>(sz.outputSizeInBytes, 16)), "cuMemAlloc", err)) {
      release(temp);
      return false;
    }
    out.size = sz.outputSizeInBytes;
    const bool ok = ox(optixAccelBuild(octx_, stream_, &ao, &bi, 1, temp.ptr, sz.tempSizeInBytes, out.ptr, sz.outputSizeInBytes, &h, nullptr, 0),
                       "optixAccelBuild", err) &&
                    cu(cuStreamSynchronize(stream_), "the build", err);
    release(temp);
    return ok;
  }

  bool build_gas(const OptixMeshIn &m, OptixTraversableHandle &h, std::string *err) {
    Buf vb, ib;
    const bool up = upload(vb, m.positions, m.vertices * 12, err) && upload(ib, m.indices, m.index_count * 4, err);
    mesh_bufs_.push_back(vb);  // (kept even on a failure: free_scene releases them)
    mesh_bufs_.push_back(ib);
    if (!up) return false;
    const unsigned geo_flags[1] = {OPTIX_GEOMETRY_FLAG_DISABLE_ANYHIT};
    OptixBuildInput bi{};
    bi.type = OPTIX_BUILD_INPUT_TYPE_TRIANGLES;
    bi.triangleArray.vertexBuffers = &vb.ptr;
    bi.triangleArray.numVertices = (unsigned)m.vertices;
    bi.triangleArray.vertexFormat = OPTIX_VERTEX_FORMAT_FLOAT3;
    bi.triangleArray.vertexStrideInBytes = 12;
    bi.triangleArray.indexBuffer = ib.ptr;
    bi.triangleArray.numIndexTriplets = (unsigned)(m.index_count / 3);
    bi.triangleArray.indexFormat = OPTIX_INDICES_FORMAT_UNSIGNED_INT3;
    bi.triangleArray.indexStrideInBytes = 12;
    bi.triangleArray.flags = geo_flags;
    bi.triangleArray.numSbtRecords = 1;
    gas_.emplace_back();
    /* Random vertex access: the closest-hit program reads the triangle for its flat normal. */
    return accel(bi, OPTIX_BUILD_FLAG_ALLOW_RANDOM_VERTEX_ACCESS | OPTIX_BUILD_FLAG_PREFER_FAST_TRACE, gas_.back(), h, err);
  }

  void free_scene() {
    for (Buf &b : gas_) release(b);
    for (Buf &b : mesh_bufs_) release(b);
    gas_.clear();
    mesh_bufs_.clear();
    release(ias_);
    release(albedo_);
    handle_ = 0;
  }

  void destroy() {
    if (!ctx_) return;
    cuCtxSetCurrent(ctx_);
    free_scene();
    for (Buf *b : {&sbt_buf_, &params_, &accum_}) release(*b);
    if (pipeline_) optixPipelineDestroy(pipeline_);
    for (OptixProgramGroup g : groups_)
      if (g) optixProgramGroupDestroy(g);
    if (module_) optixModuleDestroy(module_);
    if (octx_) optixDeviceContextDestroy(octx_);
    if (stream_) cuStreamDestroy(stream_);
    cuCtxDestroy(ctx_);
    ctx_ = nullptr;
  }
};

}  // namespace

bool OptixTracer::compiled_in() { return true; }

std::vector<std::string> OptixTracer::devices(std::string *error) {
  std::vector<std::string> out;
  int n = 0;
  if (!cuda_ready(error) || cuDeviceGetCount(&n) != CUDA_SUCCESS) return out;
  for (int i = 0; i < n; i++) {
    CUdevice d;
    char name[256] = {};
    if (cuDeviceGet(&d, i) == CUDA_SUCCESS) cuDeviceGetName(name, sizeof(name), d);
    out.push_back(name);
  }
  return out;
}

std::unique_ptr<OptixTracer> OptixTracer::create(int device, std::string *error) {
  if (!cuda_ready(error)) return nullptr;
  auto t = std::make_unique<OptixTracerImpl>();
  if (!t->init(device, error)) return nullptr;
  return t;
}

#endif

}  // namespace bl::gpu
