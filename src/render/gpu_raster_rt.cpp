// SPDX-License-Identifier: GPL-2.0-or-later
/* Round 543 (task 0523, ADR 0151): the rasterizer's acceleration structures and the traced shadow test. See
 * gpu_raster_rt.h. */
#include "gpu_raster_rt.h"

#if defined(BL_WITH_VULKAN)
#  include "../core/core.h"

#  include <algorithm>
#  include <cstring>
#  include <unordered_set>

namespace bl::gpu::rt {

const char *const kDeviceExtensions[3] = {VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, VK_KHR_RAY_QUERY_EXTENSION_NAME,
                                          VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME};

const char *const kRtExtension = "#extension GL_EXT_ray_query : require\n#define BL_RT_SHADOWS 1\n";

/* The traced test. The frame's block: rt_shadow.z the origin's offset per unit of distance from the eye, rt_bits.x the
 * lights that cast shadows (bit i: light i). The ray leaves the surface along its own (triangle) normal turned
 * toward the light - so a smooth normal lit past the triangle's edge doesn't start inside it - and stops short of a
 * point or spot light. An area light casts no realtime shadow, as with the map. Without traced shadows: the one shadow
 * light's map, exactly as the lighting passes looked it up before. */
const char *const kShadowVisGlsl = R"(
#ifdef BL_RT_SHADOWS
layout(set = 0, binding = 38) uniform accelerationStructureEXT rt_tlas;
float shadow_vis(int li, int shadow_light, bool receive, vec3 P, vec3 geo, vec3 L) {
  if (!receive || li < 0 || li >= 32 || (rt_bits.x & (1u << uint(li))) == 0u) return 1.0;
  Light l = lights[li];
  int type = int(l.pos_type.w);
  if (type == 3) return 1.0;
  float side = dot(geo, L) >= 0.0 ? 1.0 : -1.0;
  vec3 o = P + geo * side * (0.002 + rt_shadow.z * length(P - eye.xyz));
  float tmax = 1e7;
  if (type != 0) tmax = length(l.pos_type.xyz - o) * 0.999 - 1e-3;
  if (!(tmax > 0.0)) return 1.0;
  rayQueryEXT q;
  rayQueryInitializeEXT(q, rt_tlas, gl_RayFlagsOpaqueEXT | gl_RayFlagsTerminateOnFirstHitEXT, 0xFFu, o, 0.0, L, tmax);
  while (rayQueryProceedEXT(q)) {}
  return rayQueryGetIntersectionTypeEXT(q, true) == gl_RayQueryCommittedIntersectionNoneEXT ? 1.0 : 0.0;
}
#else
float shadow_vis(int li, int shadow_light, bool receive, vec3 P, vec3 geo, vec3 L) {
  return li == shadow_light && receive ? shadow_lookup(P + geo * 0.01, dot(geo, L)) : 1.0;
}
#endif
)";

namespace {
VkDeviceSize align_up(VkDeviceSize v, VkDeviceSize a) { return (v + a - 1) / a * a; }
}  // namespace

bool Shadows::init(VkDevice dev, PFN_vkGetDeviceProcAddr gdpa, const VkPhysicalDeviceMemoryProperties &mem, VkDeviceSize scratch_align,
                   std::string *err) {
#  define BL_RT_LOAD(name) f_.name = (PFN_##name)gdpa(dev, #name);
  BL_RT_FNS(BL_RT_LOAD)
#  undef BL_RT_LOAD
  if (!f_.vkGetBufferDeviceAddressKHR) f_.vkGetBufferDeviceAddressKHR = (PFN_vkGetBufferDeviceAddressKHR)gdpa(dev, "vkGetBufferDeviceAddress");
  if (!f_.vkCreateAccelerationStructureKHR || !f_.vkCmdBuildAccelerationStructuresKHR || !f_.vkGetBufferDeviceAddressKHR ||
      !f_.vkGetAccelerationStructureBuildSizesKHR || !f_.vkGetAccelerationStructureDeviceAddressKHR) {
    if (err) *err = "the device has no ray-tracing functions";
    return false;
  }
  dev_ = dev;
  mem_ = mem;
  align_ = std::max<VkDeviceSize>(256, scratch_align);
  return true;
}

void Shadows::invalidate() {
  for (auto &kv : blas_) kv.second.content = 0;  // (no mesh has content 0: built again before the top level reads it)
  tlas_key_ = ~0ull;
}

void Shadows::destroy() {
  if (!dev_) return;
  for (auto &kv : blas_) free_accel(kv.second);
  blas_.clear();
  free_accel(tlas_);
  free_buffer(scratch_);
  free_buffer(instances_);
  tlas_capacity_ = 0;
  tlas_key_ = ~0ull;
  dev_ = VK_NULL_HANDLE;
}

void Shadows::forget_mesh(uint64_t mesh) {
  for (auto it = blas_.begin(); it != blas_.end();) {
    if (it->first.mesh != mesh) {
      ++it;
      continue;
    }
    free_accel(it->second);
    it = blas_.erase(it);
  }
  tlas_key_ = ~0ull;  // (the top level may name it: built again next frame)
}

uint32_t Shadows::memory_type(uint32_t bits, VkMemoryPropertyFlags props) const {
  for (uint32_t i = 0; i < mem_.memoryTypeCount; i++)
    if ((bits & (1u << i)) && (mem_.memoryTypes[i].propertyFlags & props) == props) return i;
  return UINT32_MAX;
}

VkDeviceAddress Shadows::address(VkBuffer b) const {
  VkBufferDeviceAddressInfo ai{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
  ai.buffer = b;
  return f_.vkGetBufferDeviceAddressKHR(dev_, &ai);
}

bool Shadows::make_buffer(Buffer &b, VkDeviceSize size, VkBufferUsageFlags usage, bool host) {
  VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bi.size = std::max<VkDeviceSize>(size, 256);
  bi.usage = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
  if (f_.vkCreateBuffer(dev_, &bi, nullptr, &b.buf) != VK_SUCCESS) return false;
  VkMemoryRequirements req;
  f_.vkGetBufferMemoryRequirements(dev_, b.buf, &req);
  const VkMemoryPropertyFlags props = host ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
  VkMemoryAllocateFlagsInfo fi{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
  fi.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
  VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  ai.pNext = &fi;
  ai.allocationSize = req.size;
  ai.memoryTypeIndex = memory_type(req.memoryTypeBits, props);
  if (ai.memoryTypeIndex == UINT32_MAX || f_.vkAllocateMemory(dev_, &ai, nullptr, &b.mem) != VK_SUCCESS) {
    f_.vkDestroyBuffer(dev_, b.buf, nullptr);
    b = Buffer{};
    return false;
  }
  b.size = bi.size;
  const bool bound = f_.vkBindBufferMemory(dev_, b.buf, b.mem, 0) == VK_SUCCESS;
  if (!bound || (host && f_.vkMapMemory(dev_, b.mem, 0, VK_WHOLE_SIZE, 0, &b.mapped) != VK_SUCCESS)) {
    free_buffer(b);
    return false;
  }
  b.addr = address(b.buf);
  return true;
}

void Shadows::free_buffer(Buffer &b) {
  if (b.mapped) f_.vkUnmapMemory(dev_, b.mem);
  if (b.buf) f_.vkDestroyBuffer(dev_, b.buf, nullptr);
  if (b.mem) f_.vkFreeMemory(dev_, b.mem, nullptr);
  b = Buffer{};
}

void Shadows::free_accel(Accel &a) {
  if (a.as) f_.vkDestroyAccelerationStructureKHR(dev_, a.as, nullptr);
  free_buffer(a.store);
  a = Accel{};
}

bool Shadows::make_accel(Accel &a, VkAccelerationStructureTypeKHR type, VkDeviceSize size) {
  if (!make_buffer(a.store, size, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, false)) return false;
  VkAccelerationStructureCreateInfoKHR ci{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
  ci.buffer = a.store.buf;
  ci.size = size;
  ci.type = type;
  if (f_.vkCreateAccelerationStructureKHR(dev_, &ci, nullptr, &a.as) != VK_SUCCESS) {
    free_buffer(a.store);
    return false;
  }
  VkAccelerationStructureDeviceAddressInfoKHR di{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
  di.accelerationStructure = a.as;
  a.addr = f_.vkGetAccelerationStructureDeviceAddressKHR(dev_, &di);
  return true;
}

/* Grow-only: the frame's builds share it, each at its own aligned offset (and nothing reads it after the frame). */
bool Shadows::grow_scratch(VkDeviceSize need) {
  if (scratch_.buf && scratch_.size >= need + align_) return true;
  free_buffer(scratch_);
  return make_buffer(scratch_, std::max<VkDeviceSize>(need + align_, 1u << 20), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false);
}

namespace {
VkAccelerationStructureGeometryKHR triangles_of(const Caster &c, VkDeviceAddress vb, VkDeviceAddress ib) {
  VkAccelerationStructureGeometryKHR g{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
  g.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
  g.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
  auto &t = g.geometry.triangles;
  t.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
  t.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;  // (a RasterVertex's position: its first 12 bytes)
  t.vertexData.deviceAddress = vb;
  t.vertexStride = c.vertex_stride;
  t.maxVertex = c.vertices ? c.vertices - 1 : 0;
  t.indexType = VK_INDEX_TYPE_UINT32;
  t.indexData.deviceAddress = ib;
  return g;
}
}  // namespace

bool Shadows::build_blas(VkCommandBuffer cmd, const Caster &c, Accel &a, VkDeviceSize &scratch_at, std::string *err) {
  const VkAccelerationStructureGeometryKHR geom = triangles_of(c, address(c.vb), address(c.ib));
  VkAccelerationStructureBuildGeometryInfoKHR bi{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
  bi.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
  bi.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
  bi.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
  bi.geometryCount = 1;
  bi.pGeometries = &geom;
  const uint32_t prims = c.index_count / 3;
  VkAccelerationStructureBuildSizesInfoKHR sz{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
  f_.vkGetAccelerationStructureBuildSizesKHR(dev_, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &bi, &prims, &sz);
  if (!a.as || a.store.size < sz.accelerationStructureSize) {
    free_accel(a);
    if (!make_accel(a, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, sz.accelerationStructureSize)) {
      if (err) *err = "out of card memory for a shadow structure";
      return false;
    }
  }
  bi.dstAccelerationStructure = a.as;
  bi.scratchData.deviceAddress = align_up(scratch_.addr + scratch_at, align_);
  scratch_at = align_up(scratch_.addr + scratch_at, align_) - scratch_.addr + sz.buildScratchSize;
  VkAccelerationStructureBuildRangeInfoKHR range{prims, c.first_index * (uint32_t)sizeof(uint32_t), 0, 0};
  const VkAccelerationStructureBuildRangeInfoKHR *ranges = &range;
  f_.vkCmdBuildAccelerationStructuresKHR(cmd, 1, &bi, &ranges);
  a.content = c.content;
  counters_.blas_builds++;
  return true;
}

VkDeviceSize Shadows::tlas_scratch(size_t count) {
  VkAccelerationStructureGeometryKHR geom{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
  geom.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
  geom.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
  VkAccelerationStructureBuildGeometryInfoKHR bi{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
  bi.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
  bi.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
  bi.geometryCount = 1;
  bi.pGeometries = &geom;
  const uint32_t n = (uint32_t)std::max<size_t>({count * 2, (size_t)tlas_capacity_, 64});  // (as build_tlas may grow to)
  VkAccelerationStructureBuildSizesInfoKHR sz{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
  f_.vkGetAccelerationStructureBuildSizesKHR(dev_, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &bi, &n, &sz);
  return sz.buildScratchSize;
}

bool Shadows::build_tlas(VkCommandBuffer cmd, const std::vector<VkAccelerationStructureInstanceKHR> &inst, VkDeviceSize scratch_at,
                         std::string *err) {
  const uint32_t n = (uint32_t)inst.size();
  if (!instances_.buf || instances_.size < std::max<size_t>(n, 1) * sizeof(VkAccelerationStructureInstanceKHR)) {
    free_buffer(instances_);
    const VkDeviceSize want = std::max<VkDeviceSize>(64, (VkDeviceSize)n * 2) * sizeof(VkAccelerationStructureInstanceKHR);
    if (!make_buffer(instances_, want, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, true)) {
      if (err) *err = "out of memory for the shadow instances";
      return false;
    }
  }
  if (n) std::memcpy(instances_.mapped, inst.data(), n * sizeof(VkAccelerationStructureInstanceKHR));
  VkAccelerationStructureGeometryKHR geom{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
  geom.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
  geom.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
  geom.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
  geom.geometry.instances.data.deviceAddress = instances_.addr;
  VkAccelerationStructureBuildGeometryInfoKHR bi{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
  bi.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
  bi.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;  // (rebuilt whenever something moves)
  bi.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
  bi.geometryCount = 1;
  bi.pGeometries = &geom;
  /* Sized for a capacity, so the handle (in every view's descriptor set) stays while the scene grows a little. */
  const uint32_t capacity = std::max<uint32_t>(n, std::max<uint32_t>(tlas_capacity_, 64));
  VkAccelerationStructureBuildSizesInfoKHR sz{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
  f_.vkGetAccelerationStructureBuildSizesKHR(dev_, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &bi, &capacity, &sz);
  if (!tlas_.as || n > tlas_capacity_) {
    free_accel(tlas_);
    const uint32_t grown = std::max<uint32_t>(capacity, n * 2);
    f_.vkGetAccelerationStructureBuildSizesKHR(dev_, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &bi, &grown, &sz);
    if (!make_accel(tlas_, VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, sz.accelerationStructureSize)) {
      if (err) *err = "out of card memory for the shadow structure";
      return false;
    }
    tlas_capacity_ = grown;
    tlas_serial_++;
    counters_.tlas_made++;
  }
  if (scratch_.size < align_up(scratch_at, align_) + sz.buildScratchSize + align_) {  // (record() sized it; never grown here)
    if (err) *err = "the shadow structure's scratch is too small";
    return false;
  }
  bi.dstAccelerationStructure = tlas_.as;
  bi.scratchData.deviceAddress = align_up(scratch_.addr + scratch_at, align_);
  VkAccelerationStructureBuildRangeInfoKHR range{n, 0, 0, 0};
  const VkAccelerationStructureBuildRangeInfoKHR *ranges = &range;
  f_.vkCmdBuildAccelerationStructuresKHR(cmd, 1, &bi, &ranges);
  counters_.tlas_builds++;
  return true;
}

bool Shadows::record(VkCommandBuffer cmd, const std::vector<Caster> &casters, std::string *err) {
  if (!dev_) {
    if (err) *err = "no ray tracing on this device";
    return false;
  }
  const ScopedTimer timer;
  /* The bottom levels that are missing or stale, and the scratch they need together (one buffer, each its own part). */
  std::vector<const Caster *> stale;
  std::unordered_set<Key, KeyHash> queued;
  VkDeviceSize scratch_need = 0;
  for (const Caster &c : casters) {
    if (c.index_count < 3) continue;
    const Key k{c.mesh, c.first_index, c.index_count};
    auto it = blas_.find(k);
    if (it != blas_.end() && it->second.as && it->second.content == c.content) continue;
    if (queued.insert(k).second) stale.push_back(&c);
  }
  for (const Caster *c : stale) {
    const VkAccelerationStructureGeometryKHR geom = triangles_of(*c, 0, 0);
    VkAccelerationStructureBuildGeometryInfoKHR bi{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    bi.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    bi.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    bi.geometryCount = 1;
    bi.pGeometries = &geom;
    const uint32_t prims = c->index_count / 3;
    VkAccelerationStructureBuildSizesInfoKHR sz{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    f_.vkGetAccelerationStructureBuildSizesKHR(dev_, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &bi, &prims, &sz);
    scratch_need += align_up(sz.buildScratchSize, align_) + align_;
  }
  scratch_need += tlas_scratch(casters.size()) + align_;  // (the top level's part, after the bottom levels')
  if (!grow_scratch(scratch_need)) {
    if (err) *err = "out of card memory for the shadow structures' scratch";
    return false;
  }
  VkDeviceSize scratch_at = 0;
  for (const Caster *c : stale)
    if (!build_blas(cmd, *c, blas_[Key{c->mesh, c->first_index, c->index_count}], scratch_at, err)) return false;
  /* The top level: one instance a caster, its bottom level and matrix (row-major 3 x 4). Built again only when that
   * list changed (a key over the addresses and matrices) or a bottom level was. */
  std::vector<VkAccelerationStructureInstanceKHR> inst;
  inst.reserve(casters.size());
  uint64_t key = 1469598103934665603ull;
  auto mix = [&key](const void *p, size_t n) {
    const unsigned char *b = (const unsigned char *)p;
    for (size_t i = 0; i < n; i++) key = (key ^ b[i]) * 1099511628211ull;
  };
  for (const Caster &c : casters) {
    if (c.index_count < 3) continue;
    const Accel &a = blas_[Key{c.mesh, c.first_index, c.index_count}];
    VkAccelerationStructureInstanceKHR x{};
    for (int r = 0; r < 3; r++)
      for (int k = 0; k < 4; k++) x.transform.matrix[r][k] = c.model[k * 4 + r];
    x.mask = 0xFF;
    x.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;  // (both sides cast, as the map's pass draws them)
    x.accelerationStructureReference = a.addr;
    mix(&x, sizeof(x));
    inst.push_back(x);
  }
  if (stale.empty() && key == tlas_key_ && tlas_.as) {
    counters_.last_build_ms = timer.ms();
    return true;
  }
  if (!stale.empty()) {  // (the bottom levels written before the top level reads them)
    VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    mb.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    mb.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    f_.vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0, 1,
                            &mb, 0, nullptr, 0, nullptr);
  }
  if (!build_tlas(cmd, inst, align_up(scratch_at, align_), err)) return false;
  tlas_key_ = key;
  VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  mb.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
  mb.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
  f_.vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &mb, 0, nullptr, 0,
                          nullptr);
  counters_.last_build_ms = timer.ms();
  return true;
}

}  // namespace bl::gpu::rt
#endif
