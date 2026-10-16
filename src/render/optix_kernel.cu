// SPDX-License-Identifier: GPL-2.0-or-later
/* Task 0097 (spike, ADR 0037): a small OptiX path tracer, compiled to PTX by NVRTC at build time (build.bat with
 * tools/ptx_compile.cpp, only with the CUDA Toolkit and the OptiX SDK) and timed against the Vulkan path tracer on the
 * same scene.
 *
 * Deliberately simpler than the Vulkan kernel: Lambert surfaces with flat normals, one sun with a shadow ray per
 * bounce (next-event estimation), the gradient sky, cosine-weighted bounces. That makes its speed an upper bound of
 * what OptiX would gain: if even this isn't clearly faster, a full OptiX port won't be. */
#include <optix.h>

#include "optix_params.h"

extern "C" {
__constant__ bl::gpu::OptixParams params;
}

namespace {

struct Rng {
  unsigned int s;
  __device__ float next() {
    s = s * 747796405u + 2891336453u;
    unsigned int w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u;
    return (float)((w >> 22u) ^ w) * (1.0f / 4294967296.0f);
  }
};

__device__ float3 operator+(float3 a, float3 b) { return make_float3(a.x + b.x, a.y + b.y, a.z + b.z); }
__device__ float3 operator-(float3 a, float3 b) { return make_float3(a.x - b.x, a.y - b.y, a.z - b.z); }
__device__ float3 operator*(float3 a, float3 b) { return make_float3(a.x * b.x, a.y * b.y, a.z * b.z); }
__device__ float3 operator*(float3 a, float s) { return make_float3(a.x * s, a.y * s, a.z * s); }
__device__ float dot(float3 a, float3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
__device__ float3 cross(float3 a, float3 b) { return make_float3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
__device__ float3 normalize(float3 a) { return a * rsqrtf(fmaxf(dot(a, a), 1e-20f)); }
__device__ float3 f3(const float *v) { return make_float3(v[0], v[1], v[2]); }

/* The sky's gradient (the editor's Gradient world), as the Vulkan kernel's background. */
__device__ float3 sky(float3 d) {
  const float3 s = f3(params.sky), e = f3(params.equator), g = f3(params.ground);
  if (d.y >= 0.0f) {
    const float t = sqrtf(fminf(d.y, 1.0f));
    return e + (s - e) * t;
  }
  const float t = sqrtf(fminf(-d.y * 4.0f, 1.0f));
  return e + (g - e) * t;
}

__device__ float3 cosine_dir(float3 n, Rng &rng) {
  const float u1 = rng.next(), u2 = rng.next();
  const float r = sqrtf(u1), phi = 6.2831853f * u2;
  const float3 t = normalize(fabsf(n.x) > 0.9f ? cross(n, make_float3(0, 1, 0)) : cross(n, make_float3(1, 0, 0)));
  const float3 b = cross(n, t);
  return normalize(t * (r * cosf(phi)) + b * (r * sinf(phi)) + n * sqrtf(fmaxf(0.0f, 1.0f - u1)));
}

/* A radiance ray: hit (1), its distance, its flat normal and the object's index. */
struct Hit {
  bool hit;
  float t;
  float3 n;
  unsigned int inst;
};

__device__ Hit trace(float3 o, float3 d) {
  unsigned int p0 = 0, p1 = 0, p2 = 0, p3 = 0, p4 = 0, p5 = 0;
  optixTrace(params.handle, o, d, 1e-3f, 1e30f, 0.0f, OptixVisibilityMask(255), OPTIX_RAY_FLAG_DISABLE_ANYHIT, 0, 1, 0, p0, p1, p2, p3, p4, p5);
  Hit h;
  h.hit = p0 != 0;
  h.t = __uint_as_float(p1);
  h.n = make_float3(__uint_as_float(p2), __uint_as_float(p3), __uint_as_float(p4));
  h.inst = p5;
  return h;
}

__device__ bool occluded(float3 o, float3 d) {
  unsigned int p0 = 1;  // (the shadow miss program clears it)
  optixTrace(params.handle, o, d, 1e-3f, 1e30f, 0.0f, OptixVisibilityMask(255),
             OPTIX_RAY_FLAG_DISABLE_ANYHIT | OPTIX_RAY_FLAG_TERMINATE_ON_FIRST_HIT | OPTIX_RAY_FLAG_DISABLE_CLOSESTHIT, 0, 1, 1, p0);
  return p0 != 0;
}

}  // namespace

extern "C" __global__ void __raygen__render() {
  const uint3 idx = optixGetLaunchIndex();
  const unsigned int x = idx.x, y = idx.y, W = params.width;
  const float3 sun_l = f3(params.sun_dir) * -1.0f, sun_c = f3(params.sun_color);
  float3 sum = make_float3(0, 0, 0);
  for (int s = 0; s < params.samples; s++) {
    Rng rng{(y * W + x) * 9781u + (unsigned int)(params.first_sample + s) * 6271u + 1u};
    rng.next();
    /* The camera ray through the pixel, jittered: the inverse view-projection's near and far points. */
    const float nx = ((x + rng.next()) / W) * 2.0f - 1.0f, ny = 1.0f - ((y + rng.next()) / params.height) * 2.0f;
    const float *m = params.inv_view_proj;
    auto unproject = [&](float z) {
      const float wx = m[0] * nx + m[4] * ny + m[8] * z + m[12], wy = m[1] * nx + m[5] * ny + m[9] * z + m[13];
      const float wz = m[2] * nx + m[6] * ny + m[10] * z + m[14], ww = m[3] * nx + m[7] * ny + m[11] * z + m[15];
      return make_float3(wx / ww, wy / ww, wz / ww);
    };
    const float3 a = unproject(0.0f), b = unproject(1.0f);
    float3 o = a, d = normalize(b - a), thr = make_float3(1, 1, 1), L = make_float3(0, 0, 0);
    for (int bounce = 0; bounce <= params.max_bounces; bounce++) {
      const Hit h = trace(o, d);
      if (!h.hit) {
        L = L + thr * sky(d);
        break;
      }
      const float3 p = o + d * h.t;
      const float3 n = dot(h.n, d) > 0.0f ? h.n * -1.0f : h.n;
      const float3 alb = f3(&params.albedo[h.inst * 4]);
      const float ndl = dot(n, sun_l);
      /* The sun (Unity's intensity convention: albedo x N.L at intensity 1), if nothing blocks it. */
      if (ndl > 0.0f && !occluded(p + n * 1e-3f, sun_l)) L = L + thr * alb * sun_c * ndl;
      thr = thr * alb;
      o = p + n * 1e-3f;
      d = cosine_dir(n, rng);
    }
    sum = sum + L;
  }
  float *acc = params.accum + (unsigned long long)(y * W + x) * 4;
  acc[0] += sum.x, acc[1] += sum.y, acc[2] += sum.z, acc[3] += (float)params.samples;
}

extern "C" __global__ void __miss__radiance() { optixSetPayload_0(0); }
extern "C" __global__ void __miss__shadow() { optixSetPayload_0(0); }

extern "C" __global__ void __closesthit__radiance() {
  float3 v[3];
  optixGetTriangleVertexData(v);
  /* The triangle's normal in the world: its object-space normal through the instance's transform. */
  float3 on = cross(v[1] - v[0], v[2] - v[0]);
  if (dot(on, on) < 1e-30f) on = make_float3(0, 1, 0);  // (a degenerate triangle: any normal, never a zero one)
  const float3 wn = normalize(optixTransformNormalFromObjectToWorldSpace(on));
  optixSetPayload_0(1);
  optixSetPayload_1(__float_as_uint(optixGetRayTmax()));
  optixSetPayload_2(__float_as_uint(wn.x));
  optixSetPayload_3(__float_as_uint(wn.y));
  optixSetPayload_4(__float_as_uint(wn.z));
  optixSetPayload_5(optixGetInstanceId());
}
