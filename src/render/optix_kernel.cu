// SPDX-License-Identifier: GPL-2.0-or-later
/* Task 0170 (ADR 0054): the OptiX device's kernel. The integrator is the GPU path tracer's own GLSL (gpu_kernel.h),
 * translated by tools/glsl_to_cuda.py into build/optix/glsl_kernel.cuh at embed time, so OptiX and GPU Compute run one
 * integrator: the same random numbers, the same samples, the same picture. What is OptiX's here: the rays go through
 * optixTrace and the scene's instance / triangle structures built by the driver (the RT cores), where GPU Compute
 * walks its BVH or uses Vulkan's ray queries. One raygen program runs the kernel's main() per pixel. */
#include <optix.h>

#include "glsl.cuh"
#include "optix_params.h"

extern "C" {
__constant__ OptixLaunchParams LP;
}

/* The closest triangle along the ray within (0, t): its distance, barycentrics, triangle (the mesh's own index) and
 * object (the instance's id), as gpu_kernel.h's ray-query intersect() returns them. */
static __forceinline__ __device__ bool optix_intersect(vec3 o, vec3 d, float &t, float &u, float &v, uint &prim, uint &obj) {
  if (LP.handle == 0) return false;  // (a scene with nothing to hit)
  unsigned int p0 = __float_as_uint(t), p1 = 0, p2 = 0, p3 = 0, p4 = 0xFFFFFFFFu;
  optixTrace((OptixTraversableHandle)LP.handle, make_float3(o.x, o.y, o.z), make_float3(d.x, d.y, d.z), 0.0f, t, 0.0f, 255,
             OPTIX_RAY_FLAG_DISABLE_ANYHIT, 0, 1, 0, p0, p1, p2, p3, p4);
  if (p4 == 0xFFFFFFFFu) return false;
  t = __uint_as_float(p0), u = __uint_as_float(p1), v = __uint_as_float(p2), prim = p3, obj = p4;
  return true;
}

/* The kernel's buffers and built-ins, named as GLSL names them (the translation keeps the names). */
#define B_P (*(const ParamsBlock *)LP.params)
#define B_accum ((vec4 *)LP.accum)
#define B_aux ((vec4 *)LP.aux)
#define B_insts ((const Instance *)LP.insts)
#define B_verts ((const Vertex *)LP.verts)
#define B_idx ((const uvec4 *)LP.idx)
#define B_mats ((const Material *)LP.mats)
#define B_lights ((const Light *)LP.lights)
#define B_mlights ((const MeshLight *)LP.mlights)
#define B_mcdf ((const float *)LP.mcdf)
#define B_tex8 ((const uint *)LP.tex8)
#define B_texf ((const vec4 *)LP.texf)
#define B_texinfo ((const TexInfo *)LP.texinfo)
#define B_PC LP
#define gl_GlobalInvocationID (uvec3{optixGetLaunchIndex().x, optixGetLaunchIndex().y, 0u})

#include "glsl_kernel.cuh"

extern "C" __global__ void __raygen__render() {
  GlslKernel k;
  k.run();
}

extern "C" __global__ void __closesthit__hit() {
  const float2 bc = optixGetTriangleBarycentrics();
  optixSetPayload_0(__float_as_uint(optixGetRayTmax()));
  optixSetPayload_1(__float_as_uint(bc.x));
  optixSetPayload_2(__float_as_uint(bc.y));
  optixSetPayload_3(optixGetPrimitiveIndex());
  optixSetPayload_4(optixGetInstanceId());
}

extern "C" __global__ void __miss__miss() { optixSetPayload_4(0xFFFFFFFFu); }
