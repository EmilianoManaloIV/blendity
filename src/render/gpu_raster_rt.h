// SPDX-License-Identifier: GPL-2.0-or-later
/* Round 543 (task 0523, ADR 0151): the views' shadows traced on the card, as Blender 5.3's Workbench does on cards with ray
 * tracing. Internal to src/render: only gpu_raster.cpp uses it.
 *
 * The rasterizer opens its own VkDevice (gpu_raster.cpp), so it can't use the path tracer's acceleration structures: it
 * keeps its own here. One bottom-level structure per drawn part of a mesh that casts shadows (its vertex and index
 * buffers read in place: the vertex's position is the first 12 bytes of a RasterVertex), rebuilt when the mesh's
 * vertices change; one top-level structure of the casting instances, rebuilt only when the list or a matrix changes.
 * The builds go into the frame's own command buffer, before the lighting pass reads them.
 *
 * The shader side: `shadow_vis()` (kShadowVisGlsl) replaces the lighting passes' shadow-map lookups when the frame asks
 * for traced shadows; without them the same name looks the map up as before. */
#pragma once

#if defined(BL_WITH_VULKAN)
#  include "gpu_vk.h"

#  include <cstdint>
#  include <string>
#  include <unordered_map>
#  include <vector>

namespace bl::gpu::rt {

/* The device extensions and features ray queries need: chained by the rasterizer's device creation when the card has
 * them (DeviceInfo::hardware_rt). */
extern const char *const kDeviceExtensions[3];

/* GLSL: `shadow_vis(li, shadow_light, receive, P, geo, L)`, placed in the lighting functions right after shadow_lookup.
 * With BL_RT_SHADOWS (kRtExtension at the top of a shader: the extension and the define) it is the traced test and
 * declares the top level at set 0, binding 38 (claimed in Pass 27's board); without, the shadow map's lookup as the
 * lighting passes made it before. */
extern const char *const kRtExtension;
extern const char *const kShadowVisGlsl;
constexpr uint32_t kTlasBinding = 38;

/* A drawn part of a mesh that casts shadows, with its placement. */
struct Caster {
  VkBuffer vb = VK_NULL_HANDLE, ib = VK_NULL_HANDLE;
  uint32_t vertices = 0, vertex_stride = 0;
  uint32_t first_index = 0, index_count = 0;  // the part: a material's triangles (index_count 0: none - skipped)
  uint64_t mesh = 0;                          // the rasterizer's mesh slot
  uint64_t content = 0;                       // changes whenever the mesh's buffers or vertices change
  float model[16];                            // column-major, as RasterInstance::model
};

struct Counters {
  uint64_t blas_builds = 0, tlas_builds = 0, tlas_made = 0;
  double last_build_ms = 0;  // (the host's recording time of the last frame's builds)
};

class Shadows {
 public:
  ~Shadows() { destroy(); }
  /* Loads the device's ray-tracing functions; false (with why) when the device lacks them. */
  bool init(VkDevice dev, PFN_vkGetDeviceProcAddr gdpa, const VkPhysicalDeviceMemoryProperties &mem, VkDeviceSize scratch_align, std::string *err);
  void destroy();
  bool ready() const { return dev_ != VK_NULL_HANDLE; }
  /* Records the builds this frame needs into `cmd` (missing or stale bottom levels, the top level when the casters
   * changed) and a barrier so the fragment shaders read them. Call between frames' submissions only (the card done with
   * the last one: the scratch and instance buffers are reused). */
  bool record(VkCommandBuffer cmd, const std::vector<Caster> &casters, std::string *err);
  /* The top level (VK_NULL_HANDLE before the first record) and a serial that moves when its handle changes, so the
   * descriptor sets are written again. */
  VkAccelerationStructureKHR tlas() const { return tlas_.as; }
  uint64_t tlas_serial() const { return tlas_serial_; }
  /* A mesh slot freed or given new buffers: its bottom levels go (the card must be done with them). */
  void forget_mesh(uint64_t mesh);
  /* A recorded frame that was never submitted: its builds didn't happen, so every structure is built again next time. */
  void invalidate();
  const Counters &counters() const { return counters_; }

 private:
  struct Buffer {
    VkBuffer buf = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
    VkDeviceAddress addr = 0;
    void *mapped = nullptr;
  };
  struct Accel {
    VkAccelerationStructureKHR as = VK_NULL_HANDLE;
    Buffer store;
    VkDeviceAddress addr = 0;
    uint64_t content = 0;
  };
  struct Key {
    uint64_t mesh;
    uint32_t first, count;
    bool operator==(const Key &o) const { return mesh == o.mesh && first == o.first && count == o.count; }
  };
  struct KeyHash {
    size_t operator()(const Key &k) const { return (size_t)(k.mesh * 0x9E3779B97F4A7C15ull ^ ((uint64_t)k.first << 32) ^ k.count); }
  };
  bool make_buffer(Buffer &b, VkDeviceSize size, VkBufferUsageFlags usage, bool host);
  void free_buffer(Buffer &b);
  void free_accel(Accel &a);
  bool make_accel(Accel &a, VkAccelerationStructureTypeKHR type, VkDeviceSize size);
  VkDeviceAddress address(VkBuffer b) const;
  bool grow_scratch(VkDeviceSize need);
  VkDeviceSize tlas_scratch(size_t count);  // the top level's build scratch, at the size it may grow to
  bool build_blas(VkCommandBuffer cmd, const Caster &c, Accel &a, VkDeviceSize &scratch_at, std::string *err);
  bool build_tlas(VkCommandBuffer cmd, const std::vector<VkAccelerationStructureInstanceKHR> &inst, VkDeviceSize scratch_at, std::string *err);
  uint32_t memory_type(uint32_t bits, VkMemoryPropertyFlags props) const;

#  define BL_RT_FNS(X)                                                                                                     \
    X(vkCreateBuffer) X(vkDestroyBuffer) X(vkGetBufferMemoryRequirements) X(vkAllocateMemory) X(vkFreeMemory)               \
    X(vkBindBufferMemory) X(vkMapMemory) X(vkUnmapMemory) X(vkCmdPipelineBarrier) X(vkGetBufferDeviceAddressKHR)            \
    X(vkCreateAccelerationStructureKHR) X(vkDestroyAccelerationStructureKHR) X(vkGetAccelerationStructureBuildSizesKHR)     \
    X(vkCmdBuildAccelerationStructuresKHR) X(vkGetAccelerationStructureDeviceAddressKHR)
#  define BL_RT_DECLARE(name) PFN_##name name = nullptr;
  struct Fns {
    BL_RT_FNS(BL_RT_DECLARE)
  } f_;
#  undef BL_RT_DECLARE

  VkDevice dev_ = VK_NULL_HANDLE;
  VkPhysicalDeviceMemoryProperties mem_{};
  VkDeviceSize align_ = 256;  // the scratch's alignment: the card's minimum, at least 256
  std::unordered_map<Key, Accel, KeyHash> blas_;
  Accel tlas_;
  uint32_t tlas_capacity_ = 0;
  uint64_t tlas_serial_ = 0, tlas_key_ = ~0ull;
  Buffer scratch_, instances_;
  Counters counters_;
};

}  // namespace bl::gpu::rt
#endif
