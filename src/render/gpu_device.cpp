// SPDX-License-Identifier: GPL-2.0-or-later
#include "gpu_device.h"

#include "../core/core.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <mutex>
#include <thread>

#if defined(BL_WITH_VULKAN)
#  define VK_NO_PROTOTYPES
#  include <vulkan/vulkan.h>
#  include <shaderc/shaderc.h>
#  include "gpu_kernel.h"
#  ifdef _WIN32
#    ifndef WIN32_LEAN_AND_MEAN
#      define WIN32_LEAN_AND_MEAN
#    endif
#    include <windows.h>
#  else
#    include <dlfcn.h>
#  endif
#endif

namespace bl::gpu {

#if !defined(BL_WITH_VULKAN)

bool compiled_in() { return false; }
bool available() { return false; }
const std::vector<DeviceInfo> &devices() {
  static const std::vector<DeviceInfo> none;
  return none;
}
std::string status() { return "GPU rendering: not built (needs Blender's vulkan + shaderc libraries)"; }
void prewarm() {}
std::unique_ptr<Renderer> Renderer::create(int, bool, std::string *error) {
  if (error) *error = "this build has no GPU rendering";
  return nullptr;
}

#else

/* ===================================================================== */
/* Loader and function table                                              */
/* ===================================================================== */

namespace {

#define BL_VK_GLOBAL(X) X(vkCreateInstance) X(vkEnumerateInstanceExtensionProperties) X(vkEnumerateInstanceVersion)
#define BL_VK_INSTANCE(X)                                                                                                     \
  X(vkDestroyInstance) X(vkEnumeratePhysicalDevices) X(vkGetPhysicalDeviceProperties) X(vkGetPhysicalDeviceProperties2)       \
  X(vkGetPhysicalDeviceFeatures2) X(vkGetPhysicalDeviceMemoryProperties) X(vkGetPhysicalDeviceQueueFamilyProperties)          \
  X(vkEnumerateDeviceExtensionProperties) X(vkCreateDevice) X(vkGetDeviceProcAddr)
#define BL_VK_DEVICE(X)                                                                                                       \
  X(vkDestroyDevice) X(vkGetDeviceQueue) X(vkCreateBuffer) X(vkDestroyBuffer) X(vkGetBufferMemoryRequirements)                \
  X(vkAllocateMemory) X(vkFreeMemory) X(vkBindBufferMemory) X(vkMapMemory) X(vkUnmapMemory) X(vkCreateCommandPool)           \
  X(vkDestroyCommandPool) X(vkAllocateCommandBuffers) X(vkBeginCommandBuffer) X(vkEndCommandBuffer) X(vkResetCommandBuffer)   \
  X(vkCmdCopyBuffer) X(vkCmdFillBuffer) X(vkCmdBindPipeline) X(vkCmdBindDescriptorSets) X(vkCmdPushConstants)                 \
  X(vkCmdDispatch) X(vkCmdPipelineBarrier) X(vkQueueSubmit) X(vkQueueWaitIdle) X(vkCreateFence) X(vkDestroyFence)             \
  X(vkWaitForFences) X(vkResetFences) X(vkCreateShaderModule) X(vkDestroyShaderModule) X(vkCreateDescriptorSetLayout)         \
  X(vkDestroyDescriptorSetLayout) X(vkCreatePipelineLayout) X(vkDestroyPipelineLayout) X(vkCreateComputePipelines)            \
  X(vkDestroyPipeline) X(vkCreateDescriptorPool) X(vkDestroyDescriptorPool) X(vkAllocateDescriptorSets)                      \
  X(vkUpdateDescriptorSets) X(vkDeviceWaitIdle) X(vkInvalidateMappedMemoryRanges)
#define BL_VK_DEVICE_RT(X)                                                                                                    \
  X(vkGetBufferDeviceAddressKHR) X(vkCreateAccelerationStructureKHR) X(vkDestroyAccelerationStructureKHR)                     \
  X(vkGetAccelerationStructureBuildSizesKHR) X(vkCmdBuildAccelerationStructuresKHR)                                           \
  X(vkGetAccelerationStructureDeviceAddressKHR)

#define BL_VK_DECLARE(name) PFN_##name name = nullptr;
struct InstanceFns {
  PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;
  BL_VK_GLOBAL(BL_VK_DECLARE)
  BL_VK_INSTANCE(BL_VK_DECLARE)
};
struct DeviceFns {
  BL_VK_DEVICE(BL_VK_DECLARE)
  BL_VK_DEVICE_RT(BL_VK_DECLARE)
};

/* The process-wide Vulkan instance, created on first use. */
struct VkGlobal {
  void *lib = nullptr;
  InstanceFns f;
  VkInstance instance = VK_NULL_HANDLE;
  uint32_t api = VK_API_VERSION_1_0;
  std::vector<VkPhysicalDevice> phys;  // parallel to devices()
  std::vector<DeviceInfo> infos;
  std::string error;
  bool tried = false;
};

VkGlobal &G() {
  static VkGlobal g;
  return g;
}

void *open_loader() {
#ifdef _WIN32
  return (void *)LoadLibraryA("vulkan-1.dll");
#elif defined(__APPLE__)
  void *h = dlopen("libvulkan.1.dylib", RTLD_NOW | RTLD_LOCAL);
  if (!h) h = dlopen("libMoltenVK.dylib", RTLD_NOW | RTLD_LOCAL);
  return h;
#else
  void *h = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
  if (!h) h = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
  return h;
#endif
}
void *loader_symbol(void *lib, const char *name) {
#ifdef _WIN32
  return (void *)GetProcAddress((HMODULE)lib, name);
#else
  return dlsym(lib, name);
#endif
}

bool has_ext(const std::vector<VkExtensionProperties> &exts, const char *name) {
  for (auto &e : exts)
    if (std::strcmp(e.extensionName, name) == 0) return true;
  return false;
}

const char *vendor_name(uint32_t id) {
  switch (id) {
    case 0x10DE: return "NVIDIA";
    case 0x1002: return "AMD";
    case 0x8086: return "Intel";
    case 0x106B: return "Apple";
    case 0x13B5: return "ARM";
    case 0x5143: return "Qualcomm";
    default: return "Unknown";
  }
}

bool init_global() {
  VkGlobal &g = G();
  if (g.tried) return g.instance != VK_NULL_HANDLE;
  g.tried = true;
  g.lib = open_loader();
  if (!g.lib) {
    g.error = "no Vulkan loader on this system (install or update the GPU driver)";
    return false;
  }
  g.f.vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)loader_symbol(g.lib, "vkGetInstanceProcAddr");
  if (!g.f.vkGetInstanceProcAddr) {
    g.error = "the Vulkan loader has no vkGetInstanceProcAddr";
    return false;
  }
#define BL_VK_LOAD_GLOBAL(name) g.f.name = (PFN_##name)g.f.vkGetInstanceProcAddr(nullptr, #name);
  BL_VK_GLOBAL(BL_VK_LOAD_GLOBAL)
  if (!g.f.vkCreateInstance) {
    g.error = "the Vulkan loader is incomplete";
    return false;
  }
  if (g.f.vkEnumerateInstanceVersion) g.f.vkEnumerateInstanceVersion(&g.api);
  uint32_t n = 0;
  g.f.vkEnumerateInstanceExtensionProperties(nullptr, &n, nullptr);
  std::vector<VkExtensionProperties> exts(n);
  g.f.vkEnumerateInstanceExtensionProperties(nullptr, &n, exts.data());
  std::vector<const char *> enable;
  VkInstanceCreateFlags flags = 0;
  /* MoltenVK (macOS) is a "portability" implementation and is only listed when asked for. */
  if (has_ext(exts, "VK_KHR_portability_enumeration")) {
    enable.push_back("VK_KHR_portability_enumeration");
    flags |= 0x00000001;  // VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR
  }
  if (has_ext(exts, "VK_KHR_get_physical_device_properties2")) enable.push_back("VK_KHR_get_physical_device_properties2");
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app.pApplicationName = "Blendity";
  app.pEngineName = "Blendity";
  app.apiVersion = g.api >= VK_API_VERSION_1_2 ? VK_API_VERSION_1_2 : VK_API_VERSION_1_1;
  VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  ci.flags = flags;
  ci.pApplicationInfo = &app;
  ci.enabledExtensionCount = (uint32_t)enable.size();
  ci.ppEnabledExtensionNames = enable.data();
  if (g.f.vkCreateInstance(&ci, nullptr, &g.instance) != VK_SUCCESS) {
    g.error = "could not create a Vulkan instance";
    g.instance = VK_NULL_HANDLE;
    return false;
  }
#define BL_VK_LOAD_INSTANCE(name) g.f.name = (PFN_##name)g.f.vkGetInstanceProcAddr(g.instance, #name);
  BL_VK_INSTANCE(BL_VK_LOAD_INSTANCE)
  uint32_t pc = 0;
  g.f.vkEnumeratePhysicalDevices(g.instance, &pc, nullptr);
  std::vector<VkPhysicalDevice> pds(pc);
  g.f.vkEnumeratePhysicalDevices(g.instance, &pc, pds.data());
  for (VkPhysicalDevice pd : pds) {
    VkPhysicalDeviceProperties props;
    g.f.vkGetPhysicalDeviceProperties(pd, &props);
    /* Software rasterizers (llvmpipe, SwiftShader) are slower than Blendity's own CPU path. */
    if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU) continue;
    if (props.apiVersion < VK_API_VERSION_1_1) continue;
    uint32_t qn = 0;
    g.f.vkGetPhysicalDeviceQueueFamilyProperties(pd, &qn, nullptr);
    std::vector<VkQueueFamilyProperties> qs(qn);
    g.f.vkGetPhysicalDeviceQueueFamilyProperties(pd, &qn, qs.data());
    bool compute = false;
    for (auto &q : qs) compute = compute || (q.queueFlags & VK_QUEUE_COMPUTE_BIT);
    if (!compute) continue;
    DeviceInfo info;
    info.index = (int)g.infos.size();
    info.name = props.deviceName;
    info.vendor = vendor_name(props.vendorID);
    info.type = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU     ? "Discrete GPU"
                : props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? "Integrated GPU"
                : props.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU    ? "Virtual GPU"
                                                                             : "GPU";
    if (props.vendorID == 0x10DE)
      info.driver = strprintf("%u.%u", (props.driverVersion >> 22) & 0x3ff, (props.driverVersion >> 14) & 0xff);
    else
      info.driver = strprintf("%u.%u.%u", VK_VERSION_MAJOR(props.driverVersion), VK_VERSION_MINOR(props.driverVersion), VK_VERSION_PATCH(props.driverVersion));
    VkPhysicalDeviceMemoryProperties mp;
    g.f.vkGetPhysicalDeviceMemoryProperties(pd, &mp);
    for (uint32_t h = 0; h < mp.memoryHeapCount; h++)
      if (mp.memoryHeaps[h].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) info.memory_mb = std::max<uint64_t>(info.memory_mb, mp.memoryHeaps[h].size >> 20);
    uint32_t en = 0;
    g.f.vkEnumerateDeviceExtensionProperties(pd, nullptr, &en, nullptr);
    std::vector<VkExtensionProperties> dexts(en);
    g.f.vkEnumerateDeviceExtensionProperties(pd, nullptr, &en, dexts.data());
    if (props.apiVersion >= VK_API_VERSION_1_2 && g.api >= VK_API_VERSION_1_2 && has_ext(dexts, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) &&
        has_ext(dexts, VK_KHR_RAY_QUERY_EXTENSION_NAME) && has_ext(dexts, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME)) {
      VkPhysicalDeviceRayQueryFeaturesKHR rq{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
      VkPhysicalDeviceAccelerationStructureFeaturesKHR as{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
      VkPhysicalDeviceVulkan12Features v12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
      rq.pNext = &as;
      as.pNext = &v12;
      VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
      f2.pNext = &rq;
      g.f.vkGetPhysicalDeviceFeatures2(pd, &f2);
      info.hardware_rt = rq.rayQuery && as.accelerationStructure && v12.bufferDeviceAddress;
    }
    g.infos.push_back(info);
    g.phys.push_back(pd);
  }
  if (g.infos.empty()) g.error = "Vulkan found no GPU";
  return true;
}

/* ===================================================================== */
/* Shaders                                                                */
/* ===================================================================== */

/* Optimised SPIR-V is cached in ~/.blendity/cache: shaderc's optimiser takes
 * about 6 s on the megakernel. Unoptimised SPIR-V compiles in 70 ms but costs
 * far more later. NVIDIA's driver then spent ~105 s per GPU building the
 * pipeline the first time, and the software-BVH kernel ran 1.7x slower. */
static std::string kernel_cache_path(uint64_t key) {
  std::string dir = fs::join(fs::join(fs::home_dir(), ".blendity"), "cache");
  fs::make_dirs(dir);
  return fs::join(dir, strprintf("gpu_kernel_%016llx.spv", (unsigned long long)key));
}

std::vector<uint32_t> compile_kernel(bool ray_query, std::string *error) {
  static std::mutex &m = *new std::mutex;  // leaked: prewarm() may still be compiling at exit
  static std::vector<uint32_t> *cache = new std::vector<uint32_t>[2];
  std::lock_guard<std::mutex> lock(m);
  std::vector<uint32_t> &out = cache[ray_query ? 1 : 0];
  if (!out.empty()) return out;
  std::string src;
  for (const char *part : kernel_source_parts) src += part;
  /* The key covers the source, the variant and the compiler options. */
  uint64_t key = 1469598103934665603ull;
  auto mix = [&](const char *p, size_t n) {
    for (size_t i = 0; i < n; i++) key = (key ^ (uint8_t)p[i]) * 1099511628211ull;
  };
  const char *opts = ray_query ? "v1 vk1.2 spv1.4 size ray_query" : "v1 vk1.2 spv1.4 size";
  mix(src.data(), src.size());
  mix(opts, std::strlen(opts));
  const std::string path = kernel_cache_path(key);
  std::string bytes;
  if (fs::read_file(path, bytes) && bytes.size() >= 8 && bytes.size() % 4 == 0) {
    /* Words: SPIR-V..., then the key's low 32 bits as a trailer (catches a
     * truncated write). */
    uint32_t magic, trailer;
    std::memcpy(&magic, bytes.data(), 4);
    std::memcpy(&trailer, bytes.data() + bytes.size() - 4, 4);
    if (magic == 0x07230203u && trailer == (uint32_t)key) {
      out.resize(bytes.size() / 4 - 1);
      std::memcpy(out.data(), bytes.data(), out.size() * 4);
      return out;
    }
  }
  shaderc_compiler_t c = shaderc_compiler_initialize();
  shaderc_compile_options_t o = shaderc_compile_options_initialize();
  shaderc_compile_options_set_target_env(o, shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);
  shaderc_compile_options_set_target_spirv(o, shaderc_spirv_version_1_4);
  /* The size level gives the same SPIR-V as performance here, 30% sooner. */
  shaderc_compile_options_set_optimization_level(o, shaderc_optimization_level_size);
  if (ray_query) shaderc_compile_options_add_macro_definition(o, "USE_RAY_QUERY", 13, "1", 1);
  ScopedTimer t;
  shaderc_compilation_result_t r = shaderc_compile_into_spv(c, src.data(), src.size(), shaderc_compute_shader, "pathtracer.comp", "main", o);
  if (shaderc_result_get_compilation_status(r) != shaderc_compilation_status_success) {
    if (error) *error = std::string("GPU kernel failed to compile: ") + shaderc_result_get_error_message(r);
  }
  else {
    size_t n = shaderc_result_get_length(r) / 4;
    out.resize(n);
    std::memcpy(out.data(), shaderc_result_get_bytes(r), n * 4);
    std::string file(reinterpret_cast<const char *>(out.data()), n * 4);
    const uint32_t trailer = (uint32_t)key;
    file.append(reinterpret_cast<const char *>(&trailer), 4);
    fs::write_file(path, file);
    Log::info("GPU kernel (%s) compiled to SPIR-V in %.0f ms", ray_query ? "hardware ray tracing" : "software BVH", t.ms());
  }
  shaderc_result_release(r);
  shaderc_compile_options_release(o);
  shaderc_compiler_release(c);
  return out;
}

/* ===================================================================== */
/* One device                                                             */
/* ===================================================================== */

struct Buffer {
  VkBuffer buf = VK_NULL_HANDLE;
  VkDeviceMemory mem = VK_NULL_HANDLE;
  VkDeviceSize size = 0;
  VkDeviceAddress address = 0;
  void *mapped = nullptr;
};

class VulkanRenderer final : public Renderer {
 public:
  ~VulkanRenderer() override { destroy(); }

  bool init(int index, bool want_rt, std::string *err) {
    VkGlobal &g = G();
    info_ = g.infos[(size_t)index];
    pd_ = g.phys[(size_t)index];
    rt_ = want_rt && info_.hardware_rt;
    g.f.vkGetPhysicalDeviceMemoryProperties(pd_, &memprops_);
    uint32_t qn = 0;
    g.f.vkGetPhysicalDeviceQueueFamilyProperties(pd_, &qn, nullptr);
    std::vector<VkQueueFamilyProperties> qs(qn);
    g.f.vkGetPhysicalDeviceQueueFamilyProperties(pd_, &qn, qs.data());
    /* Prefer a compute-only queue family (async compute) when there is one. */
    qfam_ = UINT32_MAX;
    for (uint32_t i = 0; i < qn; i++)
      if ((qs[i].queueFlags & VK_QUEUE_COMPUTE_BIT) && !(qs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) { qfam_ = i; break; }
    if (qfam_ == UINT32_MAX)
      for (uint32_t i = 0; i < qn; i++)
        if (qs[i].queueFlags & VK_QUEUE_COMPUTE_BIT) { qfam_ = i; break; }
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = qfam_;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;
    std::vector<const char *> exts;
    VkPhysicalDeviceVulkan12Features v12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceAccelerationStructureFeaturesKHR asf{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
    VkPhysicalDeviceRayQueryFeaturesKHR rqf{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
    VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    if (rt_) {
      exts = {VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, VK_KHR_RAY_QUERY_EXTENSION_NAME, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME};
      v12.bufferDeviceAddress = VK_TRUE;
      asf.accelerationStructure = VK_TRUE;
      rqf.rayQuery = VK_TRUE;
      f2.pNext = &v12;
      v12.pNext = &asf;
      asf.pNext = &rqf;
    }
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.pNext = rt_ ? &f2 : nullptr;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = (uint32_t)exts.size();
    dci.ppEnabledExtensionNames = exts.data();
    if (g.f.vkCreateDevice(pd_, &dci, nullptr, &dev_) != VK_SUCCESS) {
      if (err) *err = "could not open " + info_.name;
      dev_ = VK_NULL_HANDLE;
      return false;
    }
#define BL_VK_LOAD_DEVICE(name) f_.name = (PFN_##name)g.f.vkGetDeviceProcAddr(dev_, #name);
    BL_VK_DEVICE(BL_VK_LOAD_DEVICE)
    if (rt_) {
      BL_VK_DEVICE_RT(BL_VK_LOAD_DEVICE)
      if (!f_.vkGetBufferDeviceAddressKHR) f_.vkGetBufferDeviceAddressKHR = (PFN_vkGetBufferDeviceAddressKHR)g.f.vkGetDeviceProcAddr(dev_, "vkGetBufferDeviceAddress");
      VkPhysicalDeviceAccelerationStructurePropertiesKHR asp{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
      VkPhysicalDeviceProperties2 p2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
      p2.pNext = &asp;
      g.f.vkGetPhysicalDeviceProperties2(pd_, &p2);
      scratch_align_ = std::max<uint32_t>(1, asp.minAccelerationStructureScratchOffsetAlignment);
    }
    f_.vkGetDeviceQueue(dev_, qfam_, 0, &queue_);
    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = qfam_;
    f_.vkCreateCommandPool(dev_, &pci, nullptr, &pool_);
    VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool = pool_;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    f_.vkAllocateCommandBuffers(dev_, &cai, &cmd_);
    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    f_.vkCreateFence(dev_, &fci, nullptr, &fence_);
    return create_pipeline(err);
  }

  bool upload(const Scene &s, std::string *err) override {
    f_.vkDeviceWaitIdle(dev_);
    for (int i = 3; i < 16; i++) free_buffer(bufs_[i]);  // 0-2: params and pixel buffers
    free_accel();
    scene_params_ = s.params;
    auto up = [&](int binding, const void *data, size_t bytes, VkBufferUsageFlags extra = 0) {
      return make_device_buffer(bufs_[binding], data, bytes, extra, err);
    };
    const VkBufferUsageFlags as_input = rt_ ? (VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) : 0;
    bool ok = up(3, s.nodes.data(), s.nodes.size() * sizeof(GNode)) && up(4, s.tris.data(), s.tris.size() * sizeof(GTri)) &&
              up(5, s.instances.data(), s.instances.size() * sizeof(GInstance)) && up(6, s.verts.data(), s.verts.size() * sizeof(GVertex), as_input) &&
              up(7, s.idx.data(), s.idx.size() * 4) && up(8, s.materials.data(), s.materials.size() * sizeof(GMaterial)) &&
              up(9, s.lights.data(), s.lights.size() * sizeof(GLight)) && up(10, s.mesh_lights.data(), s.mesh_lights.size() * sizeof(GMeshLight)) &&
              up(11, s.mesh_cdf.data(), s.mesh_cdf.size() * 4) && up(12, s.tex8.data(), s.tex8.size() * 4) &&
              up(13, s.texf.data(), s.texf.size() * 4) && up(14, s.textures.data(), s.textures.size() * sizeof(GTexInfo)) &&
              up(15, s.tlas_order.data(), s.tlas_order.size() * 4);
    if (!ok) return false;
    if (rt_ && !build_accel(s, err)) return false;
    set_params(s.params);
    return true;
  }

  void set_params(const GParams &p) override {
    scene_params_ = p;  // render() sizes its dispatch from these
    f_.vkDeviceWaitIdle(dev_);
    const size_t pixels = (size_t)std::max(1, p.size[0]) * std::max(1, p.size[1]);
    if (pixels != pixels_) {
      free_buffer(bufs_[1]);
      free_buffer(bufs_[2]);
      free_buffer(readback_);
      std::string e;
      make_device_buffer(bufs_[1], nullptr, pixels * 16, 0, &e);
      make_device_buffer(bufs_[2], nullptr, pixels * 32, 0, &e);
      /* Read back through CPU-cached memory: plain host-visible memory is
       * uncached on most GPUs, and reading 15 MB from it took ~50 ms. */
      readback_coherent_ = make_buffer(readback_, pixels * 48, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
      if (!readback_coherent_ && !make_buffer(readback_, pixels * 48, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT)) {
        make_host_buffer(readback_, pixels * 48, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        readback_coherent_ = true;
      }
      pixels_ = pixels;
    }
    free_buffer(bufs_[0]);
    std::string e;
    make_device_buffer(bufs_[0], &p, sizeof(GParams), 0, &e);
    update_descriptors();
    clear();
  }

  void clear() override {
    begin();
    f_.vkCmdFillBuffer(cmd_, bufs_[1].buf, 0, VK_WHOLE_SIZE, 0);
    f_.vkCmdFillBuffer(cmd_, bufs_[2].buf, 0, VK_WHOLE_SIZE, 0);
    submit_wait();
  }

  bool render(int first, int count, std::string *err) override {
    if (!pipeline_ || pixels_ == 0) {
      if (err) *err = "GPU not ready";
      return false;
    }
    begin();
    f_.vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
    f_.vkCmdBindDescriptorSets(cmd_, VK_PIPELINE_BIND_POINT_COMPUTE, layout_, 0, 1, &set_, 0, nullptr);
    const uint32_t gx = (uint32_t)(scene_params_.size[0] + 7) / 8, gy = (uint32_t)(scene_params_.size[1] + 7) / 8;
    for (int i = 0; i < count; i++) {
      int s = first + i;
      f_.vkCmdPushConstants(cmd_, layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, 4, &s);
      f_.vkCmdDispatch(cmd_, gx, gy, 1);
      VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
      mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
      mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
      f_.vkCmdPipelineBarrier(cmd_, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
    }
    if (!submit_wait()) {
      if (err) *err = info_.name + ": the GPU stopped responding (device lost)";
      return false;
    }
    return true;
  }

  void download_accum(std::vector<float> &accum) override {
    begin();
    VkBufferCopy c{0, 0, pixels_ * 16};
    f_.vkCmdCopyBuffer(cmd_, bufs_[1].buf, readback_.buf, 1, &c);
    submit_wait();
    invalidate_readback();
    const float *p = (const float *)readback_.mapped;
    accum.assign(p, p + pixels_ * 4);
  }
  void download_aux(std::vector<float> &albedo, std::vector<float> &normal_depth) override {
    begin();
    VkBufferCopy c{0, pixels_ * 16, pixels_ * 32};
    f_.vkCmdCopyBuffer(cmd_, bufs_[2].buf, readback_.buf, 1, &c);
    submit_wait();
    invalidate_readback();
    const float *p = (const float *)readback_.mapped;
    albedo.assign(p + pixels_ * 4, p + pixels_ * 8);
    normal_depth.assign(p + pixels_ * 8, p + pixels_ * 12);
  }

  const DeviceInfo &info() const override { return info_; }
  bool using_hardware_rt() const override { return rt_; }

 private:
  DeviceInfo info_;
  VkPhysicalDevice pd_ = VK_NULL_HANDLE;
  VkPhysicalDeviceMemoryProperties memprops_{};
  VkDevice dev_ = VK_NULL_HANDLE;
  DeviceFns f_;
  uint32_t qfam_ = 0;
  VkQueue queue_ = VK_NULL_HANDLE;
  VkCommandPool pool_ = VK_NULL_HANDLE;
  VkCommandBuffer cmd_ = VK_NULL_HANDLE;
  VkFence fence_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
  VkPipelineLayout layout_ = VK_NULL_HANDLE;
  VkPipeline pipeline_ = VK_NULL_HANDLE;
  VkDescriptorPool dpool_ = VK_NULL_HANDLE;
  VkDescriptorSet set_ = VK_NULL_HANDLE;
  Buffer bufs_[16];
  Buffer readback_;
  bool readback_coherent_ = true;
  void invalidate_readback() {
    if (readback_coherent_) return;
    VkMappedMemoryRange r{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
    r.memory = readback_.mem;
    r.size = VK_WHOLE_SIZE;
    f_.vkInvalidateMappedMemoryRanges(dev_, 1, &r);
  }
  size_t pixels_ = 0;
  GParams scene_params_{};
  bool rt_ = false;
  uint32_t scratch_align_ = 256;
  std::vector<Buffer> as_buffers_;
  std::vector<VkAccelerationStructureKHR> blas_;
  VkAccelerationStructureKHR tlas_ = VK_NULL_HANDLE;

  uint32_t memory_type(uint32_t bits, VkMemoryPropertyFlags want) {
    for (uint32_t i = 0; i < memprops_.memoryTypeCount; i++)
      if ((bits & (1u << i)) && (memprops_.memoryTypes[i].propertyFlags & want) == want) return i;
    return UINT32_MAX;
  }

  bool make_buffer(Buffer &b, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props) {
    size = std::max<VkDeviceSize>(size, 16);
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = size;
    bi.usage = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (f_.vkCreateBuffer(dev_, &bi, nullptr, &b.buf) != VK_SUCCESS) return false;
    VkMemoryRequirements req;
    f_.vkGetBufferMemoryRequirements(dev_, b.buf, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = memory_type(req.memoryTypeBits, props);
    VkMemoryAllocateFlagsInfo fl{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
    if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
      fl.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
      ai.pNext = &fl;
    }
    if (ai.memoryTypeIndex == UINT32_MAX || f_.vkAllocateMemory(dev_, &ai, nullptr, &b.mem) != VK_SUCCESS) {
      f_.vkDestroyBuffer(dev_, b.buf, nullptr);
      b.buf = VK_NULL_HANDLE;
      return false;
    }
    f_.vkBindBufferMemory(dev_, b.buf, b.mem, 0);
    b.size = size;
    if (props & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) f_.vkMapMemory(dev_, b.mem, 0, VK_WHOLE_SIZE, 0, &b.mapped);
    if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
      VkBufferDeviceAddressInfo dai{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
      dai.buffer = b.buf;
      b.address = f_.vkGetBufferDeviceAddressKHR(dev_, &dai);
    }
    return true;
  }
  bool make_host_buffer(Buffer &b, VkDeviceSize size, VkBufferUsageFlags usage) {
    return make_buffer(b, size, usage, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  }
  /* Device-local storage buffer, filled through a staging copy (or zeroed). */
  bool make_device_buffer(Buffer &b, const void *data, size_t bytes, VkBufferUsageFlags extra, std::string *err) {
    VkBufferUsageFlags usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | extra;
    if (!make_buffer(b, bytes, usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
      if (err) *err = strprintf("%s: out of GPU memory (%.1f MB buffer)", info_.name.c_str(), bytes / 1048576.0);
      return false;
    }
    begin();
    if (data && bytes) {
      Buffer staging;
      make_host_buffer(staging, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
      std::memcpy(staging.mapped, data, bytes);
      VkBufferCopy c{0, 0, bytes};
      f_.vkCmdCopyBuffer(cmd_, staging.buf, b.buf, 1, &c);
      submit_wait();
      free_buffer(staging);
    }
    else {
      f_.vkCmdFillBuffer(cmd_, b.buf, 0, VK_WHOLE_SIZE, 0);
      submit_wait();
    }
    return true;
  }
  void free_buffer(Buffer &b) {
    if (b.mapped) f_.vkUnmapMemory(dev_, b.mem);
    if (b.buf) f_.vkDestroyBuffer(dev_, b.buf, nullptr);
    if (b.mem) f_.vkFreeMemory(dev_, b.mem, nullptr);
    b = Buffer{};
  }

  void begin() {
    f_.vkResetCommandBuffer(cmd_, 0);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    f_.vkBeginCommandBuffer(cmd_, &bi);
  }
  bool submit_wait() {
    f_.vkEndCommandBuffer(cmd_);
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd_;
    f_.vkResetFences(dev_, 1, &fence_);
    if (f_.vkQueueSubmit(queue_, 1, &si, fence_) != VK_SUCCESS) return false;
    return f_.vkWaitForFences(dev_, 1, &fence_, VK_TRUE, UINT64_MAX) == VK_SUCCESS;
  }

  bool create_pipeline(std::string *err) {
    std::vector<uint32_t> spv = compile_kernel(rt_, err);
    if (spv.empty()) return false;
    VkShaderModuleCreateInfo smi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    smi.codeSize = spv.size() * 4;
    smi.pCode = spv.data();
    VkShaderModule mod;
    if (f_.vkCreateShaderModule(dev_, &smi, nullptr, &mod) != VK_SUCCESS) {
      if (err) *err = "could not load the GPU kernel";
      return false;
    }
    std::vector<VkDescriptorSetLayoutBinding> b;
    for (uint32_t i = 0; i < 16; i++) b.push_back({i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr});
    if (rt_) b.push_back({16, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr});
    VkDescriptorSetLayoutCreateInfo dli{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dli.bindingCount = (uint32_t)b.size();
    dli.pBindings = b.data();
    f_.vkCreateDescriptorSetLayout(dev_, &dli, nullptr, &set_layout_);
    VkPushConstantRange pcr{VK_SHADER_STAGE_COMPUTE_BIT, 0, 4};
    VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &set_layout_;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &pcr;
    f_.vkCreatePipelineLayout(dev_, &pli, nullptr, &layout_);
    VkComputePipelineCreateInfo cpi{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    cpi.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    cpi.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    cpi.stage.module = mod;
    cpi.stage.pName = "main";
    cpi.layout = layout_;
    VkResult r = f_.vkCreateComputePipelines(dev_, VK_NULL_HANDLE, 1, &cpi, nullptr, &pipeline_);
    f_.vkDestroyShaderModule(dev_, mod, nullptr);
    if (r != VK_SUCCESS) {
      if (err) *err = "could not create the GPU pipeline";
      pipeline_ = VK_NULL_HANDLE;
      return false;
    }
    std::vector<VkDescriptorPoolSize> ps = {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 16}};
    if (rt_) ps.push_back({VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1});
    VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpi.maxSets = 1;
    dpi.poolSizeCount = (uint32_t)ps.size();
    dpi.pPoolSizes = ps.data();
    f_.vkCreateDescriptorPool(dev_, &dpi, nullptr, &dpool_);
    VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dai.descriptorPool = dpool_;
    dai.descriptorSetCount = 1;
    dai.pSetLayouts = &set_layout_;
    f_.vkAllocateDescriptorSets(dev_, &dai, &set_);
    return true;
  }

  void update_descriptors() {
    VkDescriptorBufferInfo infos[16];
    std::vector<VkWriteDescriptorSet> w;
    for (uint32_t i = 0; i < 16; i++) {
      if (!bufs_[i].buf) continue;
      infos[i] = {bufs_[i].buf, 0, VK_WHOLE_SIZE};
      VkWriteDescriptorSet ws{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
      ws.dstSet = set_;
      ws.dstBinding = i;
      ws.descriptorCount = 1;
      ws.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      ws.pBufferInfo = &infos[i];
      w.push_back(ws);
    }
    VkWriteDescriptorSetAccelerationStructureKHR asw{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
    if (rt_ && tlas_) {
      asw.accelerationStructureCount = 1;
      asw.pAccelerationStructures = &tlas_;
      VkWriteDescriptorSet ws{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
      ws.pNext = &asw;
      ws.dstSet = set_;
      ws.dstBinding = 16;
      ws.descriptorCount = 1;
      ws.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
      w.push_back(ws);
    }
    f_.vkUpdateDescriptorSets(dev_, (uint32_t)w.size(), w.data(), 0, nullptr);
  }

  /* Hardware ray tracing: one bottom-level structure per unique mesh (in its
   * own space) and a top-level one over the objects, as Cycles builds them
   * for OptiX - the driver picks the layout its RT units traverse fastest. */
  bool build_accel(const Scene &s, std::string *err) {
    const VkBufferUsageFlags as_usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    const VkBufferUsageFlags scratch_usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    /* Tight 32-bit index triplets for the builder. */
    std::vector<uint32_t> tri_idx(s.idx.size() / 4 * 3);
    for (size_t t = 0; t < s.idx.size() / 4; t++)
      for (int k = 0; k < 3; k++) tri_idx[t * 3 + k] = s.idx[t * 4 + k];
    Buffer ib;
    if (!make_device_buffer(ib, tri_idx.data(), tri_idx.size() * 4,
                            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, err))
      return false;
    as_buffers_.push_back(ib);
    std::vector<VkDeviceAddress> blas_addr;
    for (const GBlas &gb : s.blas) {
      VkAccelerationStructureGeometryKHR geo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
      geo.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
      geo.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
      auto &tr = geo.geometry.triangles;
      tr.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
      tr.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
      tr.vertexData.deviceAddress = bufs_[6].address + (VkDeviceAddress)gb.vtx_off * sizeof(GVertex);
      tr.vertexStride = sizeof(GVertex);
      tr.maxVertex = std::max(1u, gb.vtx_count) - 1;
      tr.indexType = VK_INDEX_TYPE_UINT32;
      tr.indexData.deviceAddress = ib.address + (VkDeviceAddress)gb.idx_off * 12;
      VkAccelerationStructureBuildGeometryInfoKHR bi{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
      bi.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
      bi.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
      bi.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
      bi.geometryCount = 1;
      bi.pGeometries = &geo;
      VkAccelerationStructureKHR as;
      if (!build_one(bi, gb.tri_count, as_usage, scratch_usage, as, err)) return false;
      blas_.push_back(as);
      VkAccelerationStructureDeviceAddressInfoKHR adi{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
      adi.accelerationStructure = as;
      blas_addr.push_back(f_.vkGetAccelerationStructureDeviceAddressKHR(dev_, &adi));
    }
    /* Instances: transform, custom index = object, culling off (double-sided). */
    std::vector<VkAccelerationStructureInstanceKHR> inst(s.instances.size());
    for (size_t o = 0; o < s.instances.size(); o++) {
      const float *m = s.instances[o].to_world;  // column-major
      for (int r = 0; r < 3; r++)
        for (int c = 0; c < 4; c++) inst[o].transform.matrix[r][c] = m[c * 4 + r];
      inst[o].instanceCustomIndex = (uint32_t)o;
      inst[o].mask = 0xFF;
      inst[o].instanceShaderBindingTableRecordOffset = 0;
      inst[o].flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR | VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR;
      inst[o].accelerationStructureReference = blas_addr[s.object_blas[o]];
    }
    Buffer instb;
    if (!make_device_buffer(instb, inst.data(), inst.size() * sizeof(VkAccelerationStructureInstanceKHR),
                            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, err))
      return false;
    as_buffers_.push_back(instb);
    VkAccelerationStructureGeometryKHR geo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    geo.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geo.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    geo.geometry.instances.data.deviceAddress = instb.address;
    VkAccelerationStructureBuildGeometryInfoKHR bi{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    bi.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    bi.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    bi.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    bi.geometryCount = 1;
    bi.pGeometries = &geo;
    return build_one(bi, (uint32_t)inst.size(), as_usage, scratch_usage, tlas_, err);
  }
  bool build_one(VkAccelerationStructureBuildGeometryInfoKHR &bi, uint32_t prims, VkBufferUsageFlags as_usage, VkBufferUsageFlags scratch_usage,
                 VkAccelerationStructureKHR &out, std::string *err) {
    VkAccelerationStructureBuildSizesInfoKHR sz{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    f_.vkGetAccelerationStructureBuildSizesKHR(dev_, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &bi, &prims, &sz);
    Buffer store, scratch;
    if (!make_buffer(store, sz.accelerationStructureSize, as_usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) ||
        !make_buffer(scratch, sz.buildScratchSize + scratch_align_, scratch_usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
      if (err) *err = info_.name + ": out of GPU memory for acceleration structures";
      return false;
    }
    VkAccelerationStructureCreateInfoKHR ci{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
    ci.buffer = store.buf;
    ci.size = sz.accelerationStructureSize;
    ci.type = bi.type;
    if (f_.vkCreateAccelerationStructureKHR(dev_, &ci, nullptr, &out) != VK_SUCCESS) {
      if (err) *err = info_.name + ": could not create an acceleration structure";
      return false;
    }
    bi.dstAccelerationStructure = out;
    bi.scratchData.deviceAddress = (scratch.address + scratch_align_ - 1) / scratch_align_ * scratch_align_;
    VkAccelerationStructureBuildRangeInfoKHR range{prims, 0, 0, 0};
    const VkAccelerationStructureBuildRangeInfoKHR *pr = &range;
    begin();
    f_.vkCmdBuildAccelerationStructuresKHR(cmd_, 1, &bi, &pr);
    bool ok = submit_wait();
    free_buffer(scratch);
    as_buffers_.push_back(store);
    if (!ok && err) *err = info_.name + ": building an acceleration structure failed";
    return ok;
  }
  void free_accel() {
    for (auto as : blas_) f_.vkDestroyAccelerationStructureKHR(dev_, as, nullptr);
    blas_.clear();
    if (tlas_) f_.vkDestroyAccelerationStructureKHR(dev_, tlas_, nullptr);
    tlas_ = VK_NULL_HANDLE;
    for (auto &b : as_buffers_) free_buffer(b);
    as_buffers_.clear();
  }

  void destroy() {
    if (!dev_) return;
    f_.vkDeviceWaitIdle(dev_);
    free_accel();
    for (auto &b : bufs_) free_buffer(b);
    free_buffer(readback_);
    if (pipeline_) f_.vkDestroyPipeline(dev_, pipeline_, nullptr);
    if (layout_) f_.vkDestroyPipelineLayout(dev_, layout_, nullptr);
    if (set_layout_) f_.vkDestroyDescriptorSetLayout(dev_, set_layout_, nullptr);
    if (dpool_) f_.vkDestroyDescriptorPool(dev_, dpool_, nullptr);
    if (fence_) f_.vkDestroyFence(dev_, fence_, nullptr);
    if (pool_) f_.vkDestroyCommandPool(dev_, pool_, nullptr);
    f_.vkDestroyDevice(dev_, nullptr);
    dev_ = VK_NULL_HANDLE;
  }
};

}  // namespace

bool compiled_in() { return true; }

bool available() { return init_global() && !G().infos.empty(); }

const std::vector<DeviceInfo> &devices() {
  init_global();
  return G().infos;
}

std::string status() {
  if (!init_global() || G().infos.empty()) return "GPU rendering (Vulkan): " + G().error;
  std::string s = "GPU rendering (Vulkan):";
  for (const DeviceInfo &d : G().infos)
    s += strprintf(" [%d] %s (%s, %llu MB, %s)", d.index, d.name.c_str(), d.type.c_str(), (unsigned long long)d.memory_mb,
                   d.hardware_rt ? "hardware ray tracing" : "compute");
  return s;
}

void prewarm() {
  static std::atomic<bool> started{false};
  if (started.exchange(true) || !available()) return;
  bool rt = false, sw = false;
  for (const DeviceInfo &d : G().infos) (d.hardware_rt ? rt : sw) = true;
  /* Detached: compile_kernel keeps its results in leaked statics, so quitting
   * mid-compile is safe. */
  std::thread([rt, sw] {
    if (rt) compile_kernel(true, nullptr);
    if (sw) compile_kernel(false, nullptr);
  }).detach();
}

std::unique_ptr<Renderer> Renderer::create(int index, bool hardware_rt, std::string *error) {
  if (!available() || index < 0 || index >= (int)G().infos.size()) {
    if (error) *error = "no such GPU";
    return nullptr;
  }
  auto r = std::make_unique<VulkanRenderer>();
  if (!r->init(index, hardware_rt, error)) return nullptr;
  return r;
}

#endif

}  // namespace bl::gpu
