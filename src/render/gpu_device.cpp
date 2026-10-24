// SPDX-License-Identifier: GPL-2.0-or-later
#include "gpu_device.h"
#include "optix_device.h"

#include "../core/core.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <cstdio>
#include <map>
#include <mutex>
#include <set>
#include <thread>

#if defined(BL_WITH_VULKAN)
#  include "gpu_vk.h"
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

/* ~/.blendity/cache, or BLENDITY_CACHE_DIR (tests keep their files out of the user's). */
[[maybe_unused]] static std::string cache_dir() {
  const char *env = std::getenv("BLENDITY_CACHE_DIR");
  std::string dir = env && *env ? std::string(env) : fs::join(fs::join(fs::home_dir(), ".blendity"), "cache");
  fs::make_dirs(dir);
  return dir;
}

#if !defined(BL_WITH_VULKAN)

bool compiled_in() { return false; }
bool available() { return false; }
const std::vector<DeviceInfo> &devices() {
  static const std::vector<DeviceInfo> none;
  return none;
}
std::string status() { return "GPU rendering: not built (needs Blender's vulkan + shaderc libraries)"; }
void prewarm(bool, bool) {}
bool compile_kernels(bool, std::string *) { return false; }
std::string pipeline_cache_file(int, bool) { return ""; }
std::unique_ptr<Renderer> Renderer::create(int, bool, std::string *error) {
  if (error) *error = "this build has no GPU rendering";
  return nullptr;
}
std::unique_ptr<Renderer> create_renderer(int, bool, bool, std::string *error) {  // (task 0170's pool calls this form)
  if (error) *error = "this build has no GPU rendering";
  return nullptr;
}
std::unique_ptr<Compute> Compute::create(int, const std::string &, std::string *error) {
  if (error) *error = "this build has no GPU compute";
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
  X(vkUpdateDescriptorSets) X(vkDeviceWaitIdle) X(vkInvalidateMappedMemoryRanges) X(vkCreatePipelineCache)                \
  X(vkDestroyPipelineCache) X(vkGetPipelineCacheData) X(vkCreateQueryPool) X(vkDestroyQueryPool) X(vkCmdResetQueryPool)   \
  X(vkCmdWriteTimestamp) X(vkGetQueryPoolResults)
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
  bool surfaces = false;  // VK_KHR_surface and the platform's surface extension are on (task 0095)
};

VkGlobal &G() {
  static VkGlobal &g = *new VkGlobal;  // leaked: background opens (task 0037) may outlive main
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
  /* Window surfaces (task 0095: a GPU view shown through a swapchain). Asked for only where both are there. */
#ifdef _WIN32
  const char *platform_surface = "VK_KHR_win32_surface";
#else
  const char *platform_surface = "VK_KHR_xlib_surface";
#endif
  if (has_ext(exts, "VK_KHR_surface") && has_ext(exts, platform_surface)) {
    enable.push_back("VK_KHR_surface");
    enable.push_back(platform_surface);
    g.surfaces = true;
  }
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
    /* An integrated GPU (Iris Xe) shares the system's memory and reports about half of it as its own: a scene sized to
     * that would leave an 8 GB laptop's editor short. It gets a quarter of RAM (task 0075). */
    const uint64_t ram_mb = sys::physical_memory_bytes() >> 20;
    if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU && ram_mb) info.memory_mb = std::min<uint64_t>(info.memory_mb, ram_mb / 4);
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
  const std::string dir = cache_dir();
  return fs::join(dir, strprintf("gpu_kernel_%016llx.spv", (unsigned long long)key));
}

/* A cached SPIR-V file: its words, then the key's low 32 bits as a trailer (catches a truncated write). Task 0211: the
 * user's cache first, then the shaders the build compiled beside the executable (`shaders/`, written by
 * `--warm-shaders`): a new build's first open on the card no longer waits for shaderc. */
static bool read_cached_spv(uint64_t key, std::vector<uint32_t> &out) {
  const std::string name = strprintf("gpu_kernel_%016llx.spv", (unsigned long long)key);
  for (const std::string &dir : {cache_dir(), fs::join(fs::executable_dir(), "shaders")}) {
    std::string bytes;
    if (!fs::read_file(fs::join(dir, name), bytes) || bytes.size() < 8 || bytes.size() % 4 != 0) continue;
    uint32_t magic, trailer;
    std::memcpy(&magic, bytes.data(), 4);
    std::memcpy(&trailer, bytes.data() + bytes.size() - 4, 4);
    if (magic != 0x07230203u || trailer != (uint32_t)key) continue;
    out.resize(bytes.size() / 4 - 1);
    std::memcpy(out.data(), bytes.data(), out.size() * 4);
    return true;
  }
  return false;
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
  uint64_t key = kFnvBasis;
  auto mix = [&](const void *p, size_t n) { key = fnv1a(p, n, key); };
  const char *opts = ray_query ? "v1 vk1.2 spv1.4 size ray_query" : "v1 vk1.2 spv1.4 size";
  mix(src.data(), src.size());
  mix(opts, std::strlen(opts));
  const std::string path = kernel_cache_path(key);
  if (read_cached_spv(key, out)) return out;
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

  OpenStats open_stats() const override { return stats_; }

  bool init(int index, bool want_rt, std::string *err) {
    ScopedTimer open_timer;
    index_ = index;
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
    for (Flight &fl : flights_) {  // (task 0161: the two render submissions that may be in flight)
      f_.vkAllocateCommandBuffers(dev_, &cai, &fl.cmd);
      f_.vkCreateFence(dev_, &fci, nullptr, &fl.fence);
    }
    if (const char *e = std::getenv("BLENDITY_GPU_FAKE_SLOW")) fake_slow_ms_ = std::max(0.0, std::atof(e));
    create_timestamps(g);
    stats_.device_ms = open_timer.ms();
    return create_pipeline(err);
  }

  /* Timestamp queries round a render's dispatches (task 0159): the card's own time, which the Profiler shows
   * against the wall time; a queue without timestamps leaves last_gpu_ms at -1. */
  void create_timestamps(VkGlobal &g) {
    VkPhysicalDeviceProperties props;
    g.f.vkGetPhysicalDeviceProperties(pd_, &props);
    ts_period_ns_ = props.limits.timestampPeriod;
    uint32_t qn = 0;
    g.f.vkGetPhysicalDeviceQueueFamilyProperties(pd_, &qn, nullptr);
    std::vector<VkQueueFamilyProperties> qs(qn);
    g.f.vkGetPhysicalDeviceQueueFamilyProperties(pd_, &qn, qs.data());
    timestamps_ = qfam_ < qn && qs[qfam_].timestampValidBits > 0 && ts_period_ns_ > 0.0f;
    if (!timestamps_) return;
    ts_mask_ = qs[qfam_].timestampValidBits >= 64 ? ~0ull : (1ull << qs[qfam_].timestampValidBits) - 1;  // (a shorter counter wraps: the difference is taken in its width)
    VkQueryPoolCreateInfo qpi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    qpi.queryType = VK_QUERY_TYPE_TIMESTAMP;
    qpi.queryCount = 2 * kFlights;  // (two a submission in flight, task 0161)
    if (f_.vkCreateQueryPool(dev_, &qpi, nullptr, &queries_) != VK_SUCCESS) timestamps_ = false;
  }
  void stamp(VkCommandBuffer cmd, uint32_t i, VkPipelineStageFlagBits stage) {
    if (timestamps_) f_.vkCmdWriteTimestamp(cmd, stage, queries_, i);
  }
  /* A finished submission's time from its two queries (-1 without timestamps). Two flights overlap on the card -
   * the second's first stamp is written while the first still runs - so the time is taken from where the last one
   * ended: the union, never counted twice. */
  double gpu_time_of(uint32_t first_query) {
    uint64_t t[2] = {0, 0};
    if (!timestamps_ || f_.vkGetQueryPoolResults(dev_, queries_, first_query, 2, sizeof(t), t, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT) != VK_SUCCESS) return -1.0;
    const uint64_t start = ((last_end_ticks_ - t[0]) & ts_mask_) < (ts_mask_ >> 1) ? last_end_ticks_ : t[0];  // (the later of the two on a wrapping clock)
    const uint64_t busy = ((t[1] - start) & ts_mask_) < (ts_mask_ >> 1) ? (t[1] - start) & ts_mask_ : 0;
    last_end_ticks_ = t[1];
    return (double)busy * ts_period_ns_ * 1e-6;
  }

  /* The whole scene: every buffer's copy recorded from one staging arena and submitted once (task 0162; it was a
   * submission a buffer), then the acceleration structures (the BLASes in one submission, the TLAS in one). */
  bool upload(const Scene &s, std::string *err) override {
    wait_all(nullptr);
    f_.vkDeviceWaitIdle(dev_);
    for (int i = 3; i < 16; i++) free_buffer(bufs_[i]);  // 0-2: params and pixel buffers
    free_accel();
    scene_params_ = s.params;
    const VkBufferUsageFlags as_input = rt_ ? (VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) : 0;
    Batch b;
    const bool ok = batch_put(b, 3, s.nodes.data(), s.nodes.size() * sizeof(GNode), 0, err) && batch_put(b, 4, s.tris.data(), s.tris.size() * sizeof(GTri), 0, err) &&
                    batch_put(b, 5, s.instances.data(), s.instances.size() * sizeof(GInstance), 0, err) &&
                    batch_put(b, 6, s.verts.data(), s.verts.size() * sizeof(GVertex), as_input, err) && batch_put(b, 7, s.idx.data(), s.idx.size() * 4, 0, err) &&
                    batch_put(b, 8, s.materials.data(), s.materials.size() * sizeof(GMaterial), 0, err) &&
                    batch_put(b, 9, s.lights.data(), s.lights.size() * sizeof(GLight), 0, err) &&
                    batch_put(b, 10, s.mesh_lights.data(), s.mesh_lights.size() * sizeof(GMeshLight), 0, err) &&
                    batch_put(b, 11, s.mesh_cdf.data(), s.mesh_cdf.size() * 4, 0, err) && batch_put(b, 12, s.tex8.data(), s.tex8.size() * 4, 0, err) &&
                    batch_put(b, 13, s.texf.data(), s.texf.size() * 4, 0, err) && batch_put(b, 14, s.textures.data(), s.textures.size() * sizeof(GTexInfo), 0, err) &&
                    batch_put(b, 15, s.tlas_order.data(), s.tlas_order.size() * 4, 0, err);
    if (!ok || !batch_send(b, err)) return false;
    if (rt_ && !build_accel(s, err)) return false;
    if (pixels_ == 0) set_params(s.params);  // (the first scene: the pixel buffers; later reset() sends the parameters at the frame's size)
    else update_descriptors();
    return true;
  }

  /* Task 0162: the instances alone (a moved object): their buffer, the TLAS's nodes (the software BVH's prefix of
   * the nodes buffer) and order; on ray tracing hardware the TLAS is rebuilt over the kept BLASes. */
  bool update_instances(const Scene &s, std::string *err) override {
    if (!bufs_[5].buf) return upload(s, err);
    wait_all(nullptr);
    f_.vkDeviceWaitIdle(dev_);
    scene_params_ = s.params;
    Batch b;
    const size_t tlas_nodes = (size_t)std::max(0, s.params.counts[3]) * sizeof(GNode);
    const bool ok = batch_put(b, 5, s.instances.data(), s.instances.size() * sizeof(GInstance), 0, err) &&
                    batch_put(b, 15, s.tlas_order.data(), s.tlas_order.size() * 4, 0, err) &&
                    (tlas_nodes == 0 || batch_patch(b, 3, s.nodes.data(), tlas_nodes, err));
    if (!ok || !batch_send(b, err)) return false;
    if (rt_ && !build_tlas(s, err)) return false;
    update_descriptors();  // (the new buffers; reset() sends the parameters and clears)
    return true;
  }
  bool update_materials(const Scene &s, std::string *err) override {
    if (!bufs_[8].buf) return upload(s, err);
    wait_all(nullptr);
    f_.vkDeviceWaitIdle(dev_);
    scene_params_ = s.params;
    Batch b;
    if (!batch_put(b, 8, s.materials.data(), s.materials.size() * sizeof(GMaterial), 0, err) || !batch_send(b, err)) return false;
    update_descriptors();
    return true;
  }
  bool update_lights(const Scene &s, std::string *err) override {
    if (!bufs_[9].buf) return upload(s, err);
    wait_all(nullptr);
    f_.vkDeviceWaitIdle(dev_);
    scene_params_ = s.params;
    Batch b;
    const bool ok = batch_put(b, 9, s.lights.data(), s.lights.size() * sizeof(GLight), 0, err) &&
                    batch_put(b, 10, s.mesh_lights.data(), s.mesh_lights.size() * sizeof(GMeshLight), 0, err) &&
                    batch_put(b, 11, s.mesh_cdf.data(), s.mesh_cdf.size() * 4, 0, err);
    if (!ok || !batch_send(b, err)) return false;
    update_descriptors();
    return true;
  }
  uint64_t submits() const override { return submits_; }

  /* A batch of uploads: one staging arena, every copy recorded, one submission. */
  struct Batch {
    std::vector<uint8_t> staging;  // the sections one after another
    struct Copy { int binding; size_t src, bytes; };
    std::vector<Copy> copies;
  };
  /* A new device buffer for the binding, its data queued in the batch (an empty section gets a 16-byte buffer, zeroed). */
  bool batch_put(Batch &b, int binding, const void *data, size_t bytes, VkBufferUsageFlags extra, std::string *err) {
    free_buffer(bufs_[binding]);
    const VkBufferUsageFlags usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | extra;
    if (!make_buffer(bufs_[binding], bytes, usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
      if (err) *err = strprintf("%s: out of GPU memory (%.1f MB buffer)", info_.name.c_str(), bytes / 1048576.0);
      return false;
    }
    if (!data || !bytes) {
      b.copies.push_back({binding, SIZE_MAX, 0});  // (zeroed when sent)
      return true;
    }
    return batch_patch(b, binding, data, bytes, err);
  }
  /* The first `bytes` of an existing buffer replaced from the batch. */
  bool batch_patch(Batch &b, int binding, const void *data, size_t bytes, std::string *err) {
    if (bytes > bufs_[binding].size) {
      if (err) *err = info_.name + ": a section grew past its buffer";
      return false;
    }
    const size_t at = b.staging.size();
    b.staging.insert(b.staging.end(), (const uint8_t *)data, (const uint8_t *)data + bytes);
    b.copies.push_back({binding, at, bytes});
    return true;
  }
  bool batch_send(Batch &b, std::string *err) {
    Buffer staging;
    if (!b.staging.empty()) {
      if (!make_host_buffer(staging, b.staging.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT)) {
        if (err) *err = strprintf("%s: out of memory for a %.1f MB upload", info_.name.c_str(), b.staging.size() / 1048576.0);
        return false;
      }
      std::memcpy(staging.mapped, b.staging.data(), b.staging.size());
    }
    begin();
    for (const Batch::Copy &c : b.copies) {
      if (c.src == SIZE_MAX) {
        f_.vkCmdFillBuffer(cmd_, bufs_[c.binding].buf, 0, VK_WHOLE_SIZE, 0);
        continue;
      }
      VkBufferCopy copy{c.src, 0, c.bytes};
      f_.vkCmdCopyBuffer(cmd_, staging.buf, bufs_[c.binding].buf, 1, &copy);
    }
    VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};  // (the copies before the shaders and the acceleration-structure builds that read them)
    mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | (rt_ ? VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR : 0);
    f_.vkCmdPipelineBarrier(cmd_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | (rt_ ? VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR : 0), 0, 1, &mb, 0, nullptr, 0, nullptr);
    const bool ok = submit_wait();
    free_buffer(staging);
    if (!ok && err) *err = info_.name + ": the GPU stopped responding";
    return ok;
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
      make_device_buffer(bufs_[2], nullptr, pixels * 48, 0, &e);  // albedo, normal + depth, coverage (task 0035)
      /* Read back through CPU-cached memory: plain host-visible memory is
       * uncached on most GPUs, and reading 15 MB from it took ~50 ms. */
      readback_coherent_ = make_buffer(readback_, pixels * 64, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
      if (!readback_coherent_ && !make_buffer(readback_, pixels * 64, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT)) {
        make_host_buffer(readback_, pixels * 64, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
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
    wait_all(nullptr);
    begin();
    f_.vkCmdFillBuffer(cmd_, bufs_[1].buf, 0, VK_WHOLE_SIZE, 0);
    f_.vkCmdFillBuffer(cmd_, bufs_[2].buf, 0, VK_WHOLE_SIZE, 0);
    submit_wait();
  }

  /* render() is submit() then wait_all(): one submission, waited for, its card time in last_gpu_ms(). */
  bool render(int first, int count, std::string *err) override {
    if (!submit(first, count, err) || !wait_all(err)) return false;
    last_gpu_ms_ = take_busy_ms();
    return true;
  }

  /* Task 0161: the samples' dispatches recorded into the next free flight and submitted without waiting; the flight
   * used two submissions ago is waited for first when both are busy. */
  bool submit(int first, int count, std::string *err) override {
    if (!pipeline_ || pixels_ == 0) {
      if (err) *err = "GPU not ready";
      return false;
    }
    if (in_flight_ >= kFlights && !wait_oldest(err)) return false;
    Flight &fl = flights_[next_flight_];
    const uint32_t q = 2 * (uint32_t)next_flight_;
    f_.vkResetCommandBuffer(fl.cmd, 0);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    f_.vkBeginCommandBuffer(fl.cmd, &bi);
    if (timestamps_) f_.vkCmdResetQueryPool(fl.cmd, queries_, q, 2);
    stamp(fl.cmd, q, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
    record_samples(fl.cmd, first, count);
    stamp(fl.cmd, q + 1, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    f_.vkEndCommandBuffer(fl.cmd);
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &fl.cmd;
    f_.vkResetFences(dev_, 1, &fl.fence);
    submits_++;
    if (f_.vkQueueSubmit(queue_, 1, &si, fl.fence) != VK_SUCCESS) {
      if (err) *err = info_.name + ": the GPU refused the samples";
      return false;
    }
    fl.query = q;
    fl.pending = true;
    next_flight_ = (next_flight_ + 1) % kFlights;
    in_flight_++;
    return true;
  }

  /* The oldest submission in flight: waited for, its time taken into the busy sum. */
  bool wait_oldest(std::string *err) override {
    if (in_flight_ == 0) return false;
    const int oldest = (next_flight_ + kFlights - in_flight_) % kFlights;
    Flight &fl = flights_[oldest];
    if (fake_slow_ms_ > 0.0) std::this_thread::sleep_for(std::chrono::microseconds((long long)(fake_slow_ms_ * 1000.0)));  // (the test hook: a slow card)
    const bool ok = f_.vkWaitForFences(dev_, 1, &fl.fence, VK_TRUE, UINT64_MAX) == VK_SUCCESS;
    fl.pending = false;
    in_flight_--;
    if (!ok) {
      if (err) *err = info_.name + ": the GPU stopped responding (device lost)";
      return false;
    }
    const double ms = gpu_time_of(fl.query);
    if (ms >= 0.0) busy_acc_ = std::max(0.0, busy_acc_) + ms;
    return true;
  }
  bool wait_all(std::string *err) override {
    while (in_flight_ > 0)
      if (!wait_oldest(err)) return false;
    return true;
  }
  int in_flight() const override { return in_flight_; }
  double take_busy_ms() override {
    const double b = busy_acc_;
    busy_acc_ = -1.0;
    return b;
  }

  /* The samples' dispatches (task 0105: up to kSamplesPerDispatch samples a dispatch, summed in registers: the
   * accumulation and the denoiser's planes are read and written once for them, not once a sample). */
  void record_samples(VkCommandBuffer cmd, int first, int count) {
    f_.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
    f_.vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout_, 0, 1, &set_, 0, nullptr);
    const uint32_t gx = (uint32_t)(scene_params_.size[0] + 7) / 8, gy = (uint32_t)(scene_params_.size[1] + 7) / 8;
    constexpr int kSamplesPerDispatch = 8;
    for (int i = 0; i < count; i += kSamplesPerDispatch) {
      const int pc[2] = {first + i, std::min(kSamplesPerDispatch, count - i)};
      f_.vkCmdPushConstants(cmd, layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, 8, pc);
      f_.vkCmdDispatch(cmd, gx, gy, 1);
      VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
      mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
      mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
      f_.vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
    }
  }

  double last_gpu_ms() const override { return last_gpu_ms_; }

  void download_accum(std::vector<float> &accum) override {
    wait_all(nullptr);  // (nothing may be writing the sums)
    begin();
    VkBufferCopy c{0, 0, pixels_ * 16};
    f_.vkCmdCopyBuffer(cmd_, bufs_[1].buf, readback_.buf, 1, &c);
    submit_wait();
    invalidate_readback();
    const float *p = (const float *)readback_.mapped;
    accum.assign(p, p + pixels_ * 4);
  }
  void download_aux(std::vector<float> &albedo, std::vector<float> &normal_depth, std::vector<float> *coverage) override {
    wait_all(nullptr);
    begin();
    VkBufferCopy c{0, pixels_ * 16, pixels_ * 48};
    f_.vkCmdCopyBuffer(cmd_, bufs_[2].buf, readback_.buf, 1, &c);
    submit_wait();
    invalidate_readback();
    const float *p = (const float *)readback_.mapped;
    albedo.assign(p + pixels_ * 4, p + pixels_ * 8);
    normal_depth.assign(p + pixels_ * 8, p + pixels_ * 12);
    if (coverage) coverage->assign(p + pixels_ * 12, p + pixels_ * 16);
  }

  bool set_points(const float *data, size_t floats, std::string *err) override {
    const size_t bytes = floats * 4;
    if (!bufs_[2].buf || bytes > bufs_[2].size) {
      if (err) *err = "bake points don't fit";
      return false;
    }
    Buffer staging;
    if (!make_host_buffer(staging, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT)) {
      if (err) *err = info_.name + ": out of memory for the bake points";
      return false;
    }
    std::memcpy(staging.mapped, data, bytes);
    begin();
    VkBufferCopy c{0, 0, bytes};
    f_.vkCmdCopyBuffer(cmd_, staging.buf, bufs_[2].buf, 1, &c);
    const bool ok = submit_wait();
    free_buffer(staging);
    if (!ok && err) *err = info_.name + ": the GPU stopped responding";
    return ok;
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
  /* Task 0161: the render submissions in flight, each its own command buffer, fence and pair of timestamps. */
  static constexpr int kFlights = 2;
  struct Flight {
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    uint32_t query = 0;
    bool pending = false;
  };
  Flight flights_[kFlights];
  int next_flight_ = 0, in_flight_ = 0;
  double busy_acc_ = -1.0;
  double fake_slow_ms_ = 0.0;
  uint64_t last_end_ticks_ = 0;  // (the card's clock where the last finished submission ended)
  VkQueryPool queries_ = VK_NULL_HANDLE;  // task 0159: two timestamps round a render's dispatches (two pairs since task 0161)
  bool timestamps_ = false;
  float ts_period_ns_ = 0.0f;
  double last_gpu_ms_ = -1.0;
  uint64_t ts_mask_ = ~0ull;
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
  int index_ = 0;
  OpenStats stats_;
  uint32_t scratch_align_ = 256;
  std::vector<Buffer> as_buffers_;
  std::vector<Buffer> tlas_buffers_;            // (task 0162: the TLAS's store and instance buffer, replaced with it)
  std::vector<VkDeviceAddress> blas_addr_;      // (the kept BLASes' addresses, for a TLAS rebuilt alone)
  std::vector<VkAccelerationStructureKHR> blas_;
  VkAccelerationStructureKHR tlas_ = VK_NULL_HANDLE;
  std::atomic<uint64_t> submits_{0};

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
    submits_++;
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
    VkPushConstantRange pcr{VK_SHADER_STAGE_COMPUTE_BIT, 0, 8};  // the first sample and how many (task 0105)
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
    /* Built through a pipeline cache kept on disk (task 0037): a later run takes the compiled pipeline from it
     * even when the driver's own cache is cold. A file from another GPU or driver is ignored (its header). */
    const std::string cache_path = pipeline_cache_file(index_, rt_);
    std::string blob;
    if (!fs::read_file(cache_path, blob) || !strip_pipeline_trailer(blob) || !pipeline_cache_matches(blob)) blob.clear();
    VkPipelineCache pcache = VK_NULL_HANDLE;
    VkPipelineCacheCreateInfo pcci{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    pcci.initialDataSize = blob.size();
    pcci.pInitialData = blob.empty() ? nullptr : blob.data();
    if (f_.vkCreatePipelineCache(dev_, &pcci, nullptr, &pcache) != VK_SUCCESS) pcache = VK_NULL_HANDLE;
    ScopedTimer pipeline_timer;
    VkResult r = f_.vkCreateComputePipelines(dev_, pcache, 1, &cpi, nullptr, &pipeline_);
    stats_.pipeline_ms = pipeline_timer.ms();
    stats_.from_pipeline_cache = !blob.empty();
    if (pcache) {
      if (r == VK_SUCCESS) save_pipeline_cache(pcache, cache_path, blob.size());
      f_.vkDestroyPipelineCache(dev_, pcache, nullptr);
    }
    if (stats_.pipeline_ms > 250.0)
      Log::info("GPU %s: kernel pipeline built in %.0f ms%s", info_.name.c_str(), stats_.pipeline_ms, blob.empty() ? "" : " (with the pipeline cache)");
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
    if (!build_blases(s, ib, blas_addr, err)) return false;
    blas_addr_ = blas_addr;
    return build_tlas(s, err);
  }
  /* Every BLAS built in one submission (task 0162: it was one each), their addresses out. */
  bool build_blases(const Scene &s, const Buffer &ib, std::vector<VkDeviceAddress> &blas_addr, std::string *err) {
    const VkBufferUsageFlags as_usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    const VkBufferUsageFlags scratch_usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    std::vector<VkAccelerationStructureGeometryKHR> geos(s.blas.size());
    std::vector<VkAccelerationStructureBuildGeometryInfoKHR> infos(s.blas.size());
    std::vector<VkAccelerationStructureBuildRangeInfoKHR> ranges(s.blas.size());
    std::vector<const VkAccelerationStructureBuildRangeInfoKHR *> range_ptrs(s.blas.size());
    std::vector<Buffer> scratches;
    for (size_t k = 0; k < s.blas.size(); k++) {
      const GBlas &gb = s.blas[k];
      VkAccelerationStructureGeometryKHR &geo = geos[k];
      geo = VkAccelerationStructureGeometryKHR{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
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
      VkAccelerationStructureBuildGeometryInfoKHR &bi = infos[k];
      bi = VkAccelerationStructureBuildGeometryInfoKHR{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
      bi.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
      bi.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
      bi.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
      bi.geometryCount = 1;
      bi.pGeometries = &geo;
      VkAccelerationStructureKHR as;
      Buffer scratch;
      if (!prepare_one(bi, gb.tri_count, as_usage, scratch_usage, as, scratch, err)) {
        for (Buffer &sc : scratches) free_buffer(sc);
        return false;
      }
      scratches.push_back(scratch);
      blas_.push_back(as);
      ranges[k] = {gb.tri_count, 0, 0, 0};
      range_ptrs[k] = &ranges[k];
      VkAccelerationStructureDeviceAddressInfoKHR adi{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
      adi.accelerationStructure = as;
      blas_addr.push_back(f_.vkGetAccelerationStructureDeviceAddressKHR(dev_, &adi));
    }
    bool ok = true;
    if (!infos.empty()) {
      begin();
      f_.vkCmdBuildAccelerationStructuresKHR(cmd_, (uint32_t)infos.size(), infos.data(), range_ptrs.data());
      ok = submit_wait();
    }
    for (Buffer &sc : scratches) free_buffer(sc);
    if (!ok && err) *err = info_.name + ": building the acceleration structures failed";
    return ok;
  }
  /* The TLAS over the instances and the kept BLASes (its old one and instance buffer freed); one submission. */
  bool build_tlas(const Scene &s, std::string *err) {
    const VkBufferUsageFlags as_usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    const VkBufferUsageFlags scratch_usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    if (tlas_) {
      f_.vkDestroyAccelerationStructureKHR(dev_, tlas_, nullptr);
      tlas_ = VK_NULL_HANDLE;
    }
    for (Buffer &b : tlas_buffers_) free_buffer(b);
    tlas_buffers_.clear();
    const std::vector<VkDeviceAddress> &blas_addr = blas_addr_;
    if (s.object_blas.size() != s.instances.size()) {
      if (err) *err = info_.name + ": the instances and their meshes disagree";
      return false;
    }
    for (uint32_t ob : s.object_blas)
      if (ob != UINT32_MAX && ob >= blas_addr.size()) {
        if (err) *err = info_.name + ": an instance of a mesh the card doesn't hold";
        return false;
      }
    /* Instances: transform, custom index = object, culling off (double-sided); an object without a mesh stays out. */
    std::vector<VkAccelerationStructureInstanceKHR> inst;
    inst.reserve(s.instances.size());
    for (size_t o = 0; o < s.instances.size(); o++) {
      if (s.object_blas[o] == UINT32_MAX) continue;
      VkAccelerationStructureInstanceKHR in{};
      const float *m = s.instances[o].to_world;  // column-major
      for (int r = 0; r < 3; r++)
        for (int c = 0; c < 4; c++) in.transform.matrix[r][c] = m[c * 4 + r];
      in.instanceCustomIndex = (uint32_t)o;
      in.mask = 0xFF;
      in.instanceShaderBindingTableRecordOffset = 0;
      in.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR | VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR;
      in.accelerationStructureReference = blas_addr[s.object_blas[o]];
      inst.push_back(in);
    }
    Buffer instb;
    if (!make_device_buffer(instb, inst.data(), inst.size() * sizeof(VkAccelerationStructureInstanceKHR),
                            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, err))
      return false;
    tlas_buffers_.push_back(instb);
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
    Buffer scratch;
    if (!prepare_one(bi, (uint32_t)inst.size(), as_usage, scratch_usage, tlas_, scratch, err)) return false;
    tlas_buffers_.push_back(as_buffers_.back());  // (the TLAS's store, freed with the next TLAS)
    as_buffers_.pop_back();
    VkAccelerationStructureBuildRangeInfoKHR range{(uint32_t)inst.size(), 0, 0, 0};
    const VkAccelerationStructureBuildRangeInfoKHR *pr = &range;
    begin();
    VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};  // (the BLASes built before: read by this build)
    mb.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    mb.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    f_.vkCmdPipelineBarrier(cmd_, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0, 1, &mb, 0, nullptr, 0, nullptr);
    f_.vkCmdBuildAccelerationStructuresKHR(cmd_, 1, &bi, &pr);
    const bool ok = submit_wait();
    free_buffer(scratch);
    if (!ok && err) *err = info_.name + ": building the top-level acceleration structure failed";
    if (ok) update_descriptors();  // (the descriptor set names the TLAS)
    return ok;
  }
  /* The structure's store and scratch made and the build info pointed at them; the build is recorded by the caller
   * (task 0162: several in one submission). The scratch is the caller's to free after the build. */
  bool prepare_one(VkAccelerationStructureBuildGeometryInfoKHR &bi, uint32_t prims, VkBufferUsageFlags as_usage, VkBufferUsageFlags scratch_usage,
                   VkAccelerationStructureKHR &out, Buffer &scratch, std::string *err) {
    VkAccelerationStructureBuildSizesInfoKHR sz{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    f_.vkGetAccelerationStructureBuildSizesKHR(dev_, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &bi, &prims, &sz);
    Buffer store;
    if (!make_buffer(store, sz.accelerationStructureSize, as_usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) ||
        !make_buffer(scratch, sz.buildScratchSize + scratch_align_, scratch_usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
      free_buffer(store);
      free_buffer(scratch);
      if (err) *err = info_.name + ": out of GPU memory for acceleration structures";
      return false;
    }
    VkAccelerationStructureCreateInfoKHR ci{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
    ci.buffer = store.buf;
    ci.size = sz.accelerationStructureSize;
    ci.type = bi.type;
    if (f_.vkCreateAccelerationStructureKHR(dev_, &ci, nullptr, &out) != VK_SUCCESS) {
      free_buffer(store);
      free_buffer(scratch);
      if (err) *err = info_.name + ": could not create an acceleration structure";
      return false;
    }
    bi.dstAccelerationStructure = out;
    bi.scratchData.deviceAddress = (scratch.address + scratch_align_ - 1) / scratch_align_ * scratch_align_;
    as_buffers_.push_back(store);
    return true;
  }
  void free_accel() {
    for (Buffer &b : tlas_buffers_) free_buffer(b);
    tlas_buffers_.clear();
    blas_addr_.clear();
    for (auto as : blas_) f_.vkDestroyAccelerationStructureKHR(dev_, as, nullptr);
    blas_.clear();
    if (tlas_) f_.vkDestroyAccelerationStructureKHR(dev_, tlas_, nullptr);
    tlas_ = VK_NULL_HANDLE;
    for (auto &b : as_buffers_) free_buffer(b);
    as_buffers_.clear();
  }

  /* A VkPipelineCache's header (Vulkan spec, VkPipelineCacheHeaderVersionOne): it must name this GPU. */
  bool pipeline_cache_matches(const std::string &blob) const {
    if (blob.size() < 32) return false;
    uint32_t hsize, version, vendor, device;
    std::memcpy(&hsize, blob.data(), 4);
    std::memcpy(&version, blob.data() + 4, 4);
    std::memcpy(&vendor, blob.data() + 8, 4);
    std::memcpy(&device, blob.data() + 12, 4);
    VkPhysicalDeviceProperties props;
    G().f.vkGetPhysicalDeviceProperties(pd_, &props);
    return hsize >= 32 && hsize <= blob.size() && version == 1 && vendor == props.vendorID && device == props.deviceID &&
           std::memcmp(blob.data() + 16, props.pipelineCacheUUID, 16) == 0;
  }

  /* The file is the driver's data, then its length, an FNV-1a hash and a tag: a torn or truncated file (the
   * driver must get exactly what it gave) is ignored. True with the trailer removed. */
  static uint64_t fnv64(const char *p, size_t n) { return fnv1a(p, n); }
  static bool strip_pipeline_trailer(std::string &blob) {
    if (blob.size() < 16) return false;
    const size_t body = blob.size() - 16;
    uint64_t len, hash;
    std::memcpy(&len, blob.data() + body, 4 + 4);  // length (u32) + tag (u32)
    std::memcpy(&hash, blob.data() + body + 8, 8);
    const uint32_t n = (uint32_t)(len & 0xFFFFFFFFu), tag = (uint32_t)(len >> 32);
    if (tag != 0x43504C42u || n != body || hash != fnv64(blob.data(), body)) return false;  // "BLPC"
    blob.resize(body);
    return true;
  }

  /* Written when it grew, to a temporary file of this process and thread, then renamed over the old one in one
   * step (another Blendity may be reading it). */
  void save_pipeline_cache(VkPipelineCache pc, const std::string &path, size_t had) const {
    size_t n = 0;
    if (f_.vkGetPipelineCacheData(dev_, pc, &n, nullptr) != VK_SUCCESS || n == 0 || n == had || n > ((size_t)256 << 20)) return;
    std::string data(n, '\0');
    if (f_.vkGetPipelineCacheData(dev_, pc, &n, &data[0]) != VK_SUCCESS) return;
    data.resize(n);
    const uint32_t len = (uint32_t)n, tag = 0x43504C42u;
    const uint64_t hash = fnv64(data.data(), n);
    data.append(reinterpret_cast<const char *>(&len), 4);
    data.append(reinterpret_cast<const char *>(&tag), 4);
    data.append(reinterpret_cast<const char *>(&hash), 8);
    const uint64_t unique = (uint64_t)std::hash<std::thread::id>{}(std::this_thread::get_id()) ^
                            (uint64_t)std::chrono::steady_clock::now().time_since_epoch().count() ^ (uint64_t)(uintptr_t)this;
    const std::string tmp = path + strprintf(".%016llx.tmp", (unsigned long long)unique);
    if (!fs::write_file(tmp, data)) return;
    std::error_code ec;
    std::filesystem::rename(std::filesystem::u8path(tmp), std::filesystem::u8path(path), ec);  // replaces the old file
    if (ec) std::remove(tmp.c_str());
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
    for (Flight &fl : flights_)
      if (fl.fence) f_.vkDestroyFence(dev_, fl.fence, nullptr);
    if (queries_) f_.vkDestroyQueryPool(dev_, queries_, nullptr);
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

bool compile_kernels(bool ray_query, std::string *error) {
  if (!available()) return false;
  bool ok = !compile_kernel(false, error).empty();
  if (ray_query) ok = !compile_kernel(true, error).empty() && ok;
  return ok;
}

void prewarm(bool open_devices, bool hardware_rt) {
  static std::atomic<bool> started{false};
  if (started.exchange(true) || !available()) return;
  if (backend() == 2) optix_list_async();  // (task 0170: CUDA's devices listed off the UI thread)
  bool rt = false, sw = false;
  for (const DeviceInfo &d : G().infos) (d.hardware_rt ? rt : sw) = true;
  /* Detached: compile_kernel keeps its results in leaked statics, so quitting
   * mid-compile is safe. Then (the interactive editor) a renderer per GPU opens in the background, so turning a
   * view to the GPU later finds one ready (task 0037). */
  std::thread([rt, sw, open_devices, hardware_rt] {
    if (rt) compile_kernel(true, nullptr);
    if (sw) compile_kernel(false, nullptr);
    if (open_devices)
      for (const DeviceInfo &d : devices()) {
        bool pending = false;
        if (std::unique_ptr<Renderer> r = take_renderer(d.index, hardware_rt, &pending)) return_renderer(std::move(r), d.index, hardware_rt);
      }
  }).detach();
}

std::string pipeline_cache_file(int index, bool hardware_rt) {
  if (!available() || index < 0 || index >= (int)G().infos.size()) return "";
  VkPhysicalDeviceProperties props;
  G().f.vkGetPhysicalDeviceProperties(G().phys[(size_t)index], &props);
  const std::string dir = cache_dir();
  return fs::join(dir, strprintf("pipeline_%04x_%04x_%s.bin", props.vendorID, props.deviceID, hardware_rt && G().infos[(size_t)index].hardware_rt ? "rt" : "bvh"));
}

/* Task 0170: the OptiX device when `optix` (decided once by the caller: the backend, the card), GPU Compute otherwise or
 * when OptiX can't open - the failure remembered, so the card isn't offered OptiX again this run. */
std::unique_ptr<Renderer> create_renderer(int index, bool hardware_rt, bool optix, std::string *error) {
  if (!available() || index < 0 || index >= (int)G().infos.size()) {
    if (error) *error = "no such GPU";
    return nullptr;
  }
  if (optix) {
    std::string why;
    if (std::unique_ptr<Renderer> o = create_optix_renderer(index, &why)) return o;
    optix_failed(index);
    Log::warn("OptiX on %s unavailable (%s): GPU Compute instead", G().infos[(size_t)index].name.c_str(), why.c_str());
  }
  auto r = std::make_unique<VulkanRenderer>();
  if (!r->init(index, hardware_rt, error)) return nullptr;
  return r;
}

std::unique_ptr<Renderer> Renderer::create(int index, bool hardware_rt, std::string *error) {
  return create_renderer(index, hardware_rt, hardware_rt && use_optix(index), error);
}

/* ===================================================================== */
/* General compute (task 0018)                                            */
/* ===================================================================== */

namespace {

/* GLSL to SPIR-V, cached in memory per source and on disk like the path tracer's kernel. */
std::vector<uint32_t> compile_glsl(const std::string &src, shaderc_shader_kind kind, std::string *error) {
  static std::mutex &m = *new std::mutex;
  static std::map<uint64_t, std::vector<uint32_t>> &mem = *new std::map<uint64_t, std::vector<uint32_t>>;
  uint64_t key = fnv1a(src.data(), src.size());
  const char *opts = "compute v1 vk1.2 spv1.4 performance";
  key = fnv1a_str(opts, key);
  if (kind != shaderc_compute_shader) key = (key ^ (uint64_t)(kind + 1)) * kFnvPrime;  // (the compute key as before)
  {  // (task 0198: the lock for the memory cache only - shaders compile in parallel)
    std::lock_guard<std::mutex> lock(m);
    auto hit = mem.find(key);
    if (hit != mem.end() && !hit->second.empty()) return hit->second;
  }
  std::vector<uint32_t> out;
  auto keep = [&] {
    std::lock_guard<std::mutex> lock(m);
    mem[key] = out;
  };
  const std::string path = kernel_cache_path(key);
  if (read_cached_spv(key, out)) {
    keep();
    return out;
  }
  shaderc_compiler_t c = shaderc_compiler_initialize();
  shaderc_compile_options_t o = shaderc_compile_options_initialize();
  shaderc_compile_options_set_target_env(o, shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);
  shaderc_compile_options_set_target_spirv(o, shaderc_spirv_version_1_4);
  shaderc_compile_options_set_optimization_level(o, shaderc_optimization_level_performance);
  shaderc_compilation_result_t r = shaderc_compile_into_spv(c, src.data(), src.size(), kind, "shader.glsl", "main", o);
  if (shaderc_result_get_compilation_status(r) != shaderc_compilation_status_success) {
    if (error) *error = std::string("A GPU shader failed to compile: ") + shaderc_result_get_error_message(r);
  }
  else {
    const size_t n = shaderc_result_get_length(r) / 4;
    out.resize(n);
    std::memcpy(out.data(), shaderc_result_get_bytes(r), n * 4);
    std::string file(reinterpret_cast<const char *>(out.data()), n * 4);
    const uint32_t trailer = (uint32_t)key;
    file.append(reinterpret_cast<const char *>(&trailer), 4);
    /* (Task 0198: written beside it and renamed into place - two threads may compile one source at once, and a reader
     * never sees half a file.) */
    const std::string tmp = path + strprintf(".%zx.tmp", std::hash<std::thread::id>{}(std::this_thread::get_id()));
    std::error_code ec;
    if (fs::write_file(tmp, file)) std::filesystem::rename(std::filesystem::u8path(tmp), std::filesystem::u8path(path), ec);
    if (ec) std::filesystem::remove(std::filesystem::u8path(tmp), ec);
  }
  shaderc_result_release(r);
  shaderc_compile_options_release(o);
  shaderc_compiler_release(c);
  if (!out.empty()) keep();
  return out;
}

std::vector<uint32_t> compile_compute(const std::string &src, std::string *error) { return compile_glsl(src, shaderc_compute_shader, error); }

class VulkanCompute final : public Compute {
 public:
  ~VulkanCompute() override {
    if (!dev_) return;
    f_.vkDeviceWaitIdle(dev_);
    for (Buffer &b : bufs_) free_buffer(b);
    free_buffer(staging_);
    free_buffer(readback_);
    if (pipeline_) f_.vkDestroyPipeline(dev_, pipeline_, nullptr);
    if (layout_) f_.vkDestroyPipelineLayout(dev_, layout_, nullptr);
    if (dpool_) f_.vkDestroyDescriptorPool(dev_, dpool_, nullptr);
    if (set_layout_) f_.vkDestroyDescriptorSetLayout(dev_, set_layout_, nullptr);
    if (fence_) f_.vkDestroyFence(dev_, fence_, nullptr);
    if (pool_) f_.vkDestroyCommandPool(dev_, pool_, nullptr);
    f_.vkDestroyDevice(dev_, nullptr);
  }

  bool init(int index, const std::string &glsl, std::string *err) {
    VkGlobal &g = G();
    info_ = g.infos[(size_t)index];
    pd_ = g.phys[(size_t)index];
    g.f.vkGetPhysicalDeviceMemoryProperties(pd_, &memprops_);
    uint32_t qn = 0;
    g.f.vkGetPhysicalDeviceQueueFamilyProperties(pd_, &qn, nullptr);
    std::vector<VkQueueFamilyProperties> qs(qn);
    g.f.vkGetPhysicalDeviceQueueFamilyProperties(pd_, &qn, qs.data());
    qfam_ = UINT32_MAX;
    for (uint32_t i = 0; i < qn && qfam_ == UINT32_MAX; i++)
      if ((qs[i].queueFlags & VK_QUEUE_COMPUTE_BIT) && !(qs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) qfam_ = i;
    for (uint32_t i = 0; i < qn && qfam_ == UINT32_MAX; i++)
      if (qs[i].queueFlags & VK_QUEUE_COMPUTE_BIT) qfam_ = i;
    if (qfam_ == UINT32_MAX) {
      if (err) *err = info_.name + " has no compute queue";
      return false;
    }
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = qfam_;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    if (g.f.vkCreateDevice(pd_, &dci, nullptr, &dev_) != VK_SUCCESS) {
      if (err) *err = "could not open " + info_.name;
      dev_ = VK_NULL_HANDLE;
      return false;
    }
#define BL_VK_LOAD_COMPUTE(name) f_.name = (PFN_##name)g.f.vkGetDeviceProcAddr(dev_, #name);
    BL_VK_DEVICE(BL_VK_LOAD_COMPUTE)
#undef BL_VK_LOAD_COMPUTE
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
    /* The pipeline: 16 storage buffers and 128 bytes of push constants. */
    std::vector<uint32_t> spv = compile_compute(glsl, err);
    if (spv.empty()) return false;
    VkShaderModuleCreateInfo smi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    smi.codeSize = spv.size() * 4;
    smi.pCode = spv.data();
    VkShaderModule mod;
    if (f_.vkCreateShaderModule(dev_, &smi, nullptr, &mod) != VK_SUCCESS) {
      if (err) *err = "could not load the GPU compute kernel";
      return false;
    }
    std::vector<VkDescriptorSetLayoutBinding> b;
    for (uint32_t i = 0; i < 16; i++) b.push_back({i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr});
    VkDescriptorSetLayoutCreateInfo dli{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dli.bindingCount = (uint32_t)b.size();
    dli.pBindings = b.data();
    f_.vkCreateDescriptorSetLayout(dev_, &dli, nullptr, &set_layout_);
    VkPushConstantRange pcr{VK_SHADER_STAGE_COMPUTE_BIT, 0, 128};
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
    const VkResult r = f_.vkCreateComputePipelines(dev_, VK_NULL_HANDLE, 1, &cpi, nullptr, &pipeline_);
    f_.vkDestroyShaderModule(dev_, mod, nullptr);
    if (r != VK_SUCCESS) {
      if (err) *err = "could not create the GPU compute pipeline";
      pipeline_ = VK_NULL_HANDLE;
      return false;
    }
    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 16};
    VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpi.maxSets = 1;
    dpi.poolSizeCount = 1;
    dpi.pPoolSizes = &ps;
    f_.vkCreateDescriptorPool(dev_, &dpi, nullptr, &dpool_);
    VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dai.descriptorPool = dpool_;
    dai.descriptorSetCount = 1;
    dai.pSetLayouts = &set_layout_;
    f_.vkAllocateDescriptorSets(dev_, &dai, &set_);
    /* Every binding gets a small buffer, so the set is always complete. */
    for (int i = 0; i < 16; i++)
      if (!grow(i, 16, err)) return false;
    return true;
  }

  bool set(int binding, const void *data, size_t bytes, std::string *err) override {
    if (binding < 0 || binding >= 16) return false;
    if (!grow(binding, bytes, err)) return false;
    if (!bytes) return true;
    if (!staging_.buf || staging_.size < bytes) {
      free_buffer(staging_);
      if (!make_buffer(staging_, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
        if (err) *err = info_.name + ": out of memory for an upload";
        return false;
      }
    }
    std::memcpy(staging_.mapped, data, bytes);
    begin();
    VkBufferCopy c{0, 0, bytes};
    f_.vkCmdCopyBuffer(cmd_, staging_.buf, bufs_[binding].buf, 1, &c);
    return submit_wait(err);
  }
  bool output(int binding, size_t bytes, std::string *err) override {
    if (binding < 0 || binding >= 16 || !grow(binding, bytes, err)) return false;
    begin();
    f_.vkCmdFillBuffer(cmd_, bufs_[binding].buf, 0, VK_WHOLE_SIZE, 0);
    return submit_wait(err);
  }
  bool run(uint32_t groups, const void *push, uint32_t push_bytes, std::string *err) override {
    if (!pipeline_ || push_bytes > 128) return false;
    begin();
    f_.vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
    f_.vkCmdBindDescriptorSets(cmd_, VK_PIPELINE_BIND_POINT_COMPUTE, layout_, 0, 1, &set_, 0, nullptr);
    if (push_bytes) f_.vkCmdPushConstants(cmd_, layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, push_bytes, push);
    f_.vkCmdDispatch(cmd_, std::max(1u, groups), 1, 1);
    return submit_wait(err);
  }
  bool read(int binding, void *out, size_t bytes, std::string *err) override {
    if (binding < 0 || binding >= 16 || bytes > bufs_[binding].size) {
      if (err) *err = "read past the end of a GPU buffer";
      return false;
    }
    if (!readback_.buf || readback_.size < bytes) {
      free_buffer(readback_);
      if (!make_buffer(readback_, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) &&
          !make_buffer(readback_, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
        if (err) *err = info_.name + ": out of memory for a read-back";
        return false;
      }
    }
    begin();
    VkBufferCopy c{0, 0, bytes};
    f_.vkCmdCopyBuffer(cmd_, bufs_[binding].buf, readback_.buf, 1, &c);
    if (!submit_wait(err)) return false;
    std::memcpy(out, readback_.mapped, bytes);
    return true;
  }
  const DeviceInfo &info() const override { return info_; }

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
  Buffer bufs_[16], staging_, readback_;

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
    if (ai.memoryTypeIndex == UINT32_MAX || f_.vkAllocateMemory(dev_, &ai, nullptr, &b.mem) != VK_SUCCESS) {
      f_.vkDestroyBuffer(dev_, b.buf, nullptr);
      b.buf = VK_NULL_HANDLE;
      return false;
    }
    f_.vkBindBufferMemory(dev_, b.buf, b.mem, 0);
    b.size = size;
    if (props & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) f_.vkMapMemory(dev_, b.mem, 0, VK_WHOLE_SIZE, 0, &b.mapped);
    return true;
  }
  void free_buffer(Buffer &b) {
    if (b.mapped) f_.vkUnmapMemory(dev_, b.mem);
    if (b.buf) f_.vkDestroyBuffer(dev_, b.buf, nullptr);
    if (b.mem) f_.vkFreeMemory(dev_, b.mem, nullptr);
    b = Buffer{};
  }
  /* The binding's buffer holds at least `bytes` (a new one is bound to the set). */
  bool grow(int binding, size_t bytes, std::string *err) {
    Buffer &b = bufs_[binding];
    if (b.buf && b.size >= bytes) return true;
    f_.vkDeviceWaitIdle(dev_);
    free_buffer(b);
    const VkDeviceSize want = std::max<VkDeviceSize>(16, (VkDeviceSize)bytes + bytes / 4);  // room to grow
    if (!make_buffer(b, want, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
      if (err) *err = strprintf("%s: out of GPU memory (%.1f MB buffer)", info_.name.c_str(), bytes / 1048576.0);
      return false;
    }
    VkDescriptorBufferInfo bi{b.buf, 0, VK_WHOLE_SIZE};
    VkWriteDescriptorSet ws{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    ws.dstSet = set_;
    ws.dstBinding = (uint32_t)binding;
    ws.descriptorCount = 1;
    ws.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    ws.pBufferInfo = &bi;
    f_.vkUpdateDescriptorSets(dev_, 1, &ws, 0, nullptr);
    return true;
  }
  void begin() {
    f_.vkResetCommandBuffer(cmd_, 0);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    f_.vkBeginCommandBuffer(cmd_, &bi);
  }
  bool submit_wait(std::string *err) {
    f_.vkEndCommandBuffer(cmd_);
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd_;
    f_.vkResetFences(dev_, 1, &fence_);
    if (f_.vkQueueSubmit(queue_, 1, &si, fence_) != VK_SUCCESS ||
        f_.vkWaitForFences(dev_, 1, &fence_, VK_TRUE, 10ull * 1000 * 1000 * 1000) != VK_SUCCESS) {
      if (err) *err = info_.name + ": the GPU stopped responding";
      return false;
    }
    return true;
  }
};

}  // namespace

namespace vk {
PFN_vkGetInstanceProcAddr get_instance_proc_addr() { return available() ? G().f.vkGetInstanceProcAddr : nullptr; }
VkInstance instance() { return available() ? G().instance : VK_NULL_HANDLE; }
VkPhysicalDevice physical(int index) {
  if (!available() || index < 0 || index >= (int)G().phys.size()) return VK_NULL_HANDLE;
  return G().phys[(size_t)index];
}
bool surfaces() { return available() && G().surfaces; }
std::vector<uint32_t> compile(const std::string &glsl, shaderc_shader_kind kind, std::string *error) { return compile_glsl(glsl, kind, error); }
}  // namespace vk

std::unique_ptr<Compute> Compute::create(int index, const std::string &glsl, std::string *error) {
  if (!available() || index < 0 || index >= (int)G().infos.size()) {
    if (error) *error = "no such GPU";
    return nullptr;
  }
  auto c = std::make_unique<VulkanCompute>();
  if (!c->init(index, glsl, error)) return nullptr;
  return c;
}

#endif

std::unique_ptr<Renderer> create_renderer(int index, bool hardware_rt, bool optix, std::string *error);

/* ===================================================================== */
/* Renderers opened in the background (task 0037, ADR 0021)              */
/* ===================================================================== */

namespace {

struct RendererPool {
  std::mutex m;
  std::map<int, std::unique_ptr<Renderer>> ready;  // key: device index * 4 + hardware ray tracing + OptiX (pool_key)
  std::map<int, std::pair<std::string, double>> failed;  // an open that failed (its error and when): retried after 30 s
  std::set<int> opening;
  std::atomic<uint64_t> generation{1};
  std::atomic<int> delay_ms{0};
};
/* Leaked: a background open may still run when the program quits. */
RendererPool &pool() {
  static RendererPool *p = new RendererPool;
  return *p;
}
/* Task 0170: the backend is in the key too - a renderer opened for GPU Compute isn't handed out when OptiX is asked
 * for (BLENDITY_GPU_BACKEND or Render Settings changed). A ready renderer is filed by what it is. */
int pool_key_of(int index, bool rt, bool optix) { return index * 4 + (rt ? 1 : 0) + (optix ? 2 : 0); }
int pool_key(int index, bool rt) { return pool_key_of(index, rt, rt && use_optix(index)); }
bool device_rt(int index) { return index >= 0 && index < (int)devices().size() && devices()[(size_t)index].hardware_rt; }

/* Starts opening one on a detached thread, unless one is ready or opening. The pool is locked. */
void start_open(RendererPool &p, int index, bool rt) {
  const bool optix = rt && use_optix(index);  // (decided once: the open makes what its key says)
  const int key = pool_key_of(index, rt, optix);
  if (p.ready.count(key) || p.opening.count(key)) return;
  auto f = p.failed.find(key);
  if (f != p.failed.end()) {
    if (now_seconds() - f->second.second < 30.0) return;
    p.failed.erase(f);
  }
  p.opening.insert(key);
  const std::string name = devices()[(size_t)index].name;  // copied: nothing global is read after the open
  std::thread([index, rt, optix, key, name] {
    RendererPool &p = pool();
    if (const int d = p.delay_ms.load()) std::this_thread::sleep_for(std::chrono::milliseconds(d));
    std::string err;
    ScopedTimer t;
    std::unique_ptr<Renderer> r = create_renderer(index, rt, optix, &err);
    const double ms = t.ms();
    const bool ok = r != nullptr;
    {
      std::lock_guard<std::mutex> lock(p.m);
      p.opening.erase(key);
      if (ok) p.ready[pool_key_of(index, rt, r->info().type == "OptiX")] = std::move(r);  // (an OptiX open that fell back is filed as GPU Compute)
      else p.failed[key] = {err.empty() ? std::string("could not open the GPU") : err, now_seconds()};
    }
    p.generation++;
    if (ok && ms > 250.0) Log::info("GPU %s ready (opened in the background in %.0f ms)", name.c_str(), ms);
    if (!ok) Log::warn("GPU %s unavailable: %s", name.c_str(), err.c_str());
  }).detach();
}

}  // namespace

/* Back into the pool (prewarm opened it for whichever view comes first). */
void return_renderer(std::unique_ptr<Renderer> r, int index, bool hardware_rt) {
  if (!r) return;
  RendererPool &p = pool();
  std::lock_guard<std::mutex> lock(p.m);
  std::unique_ptr<Renderer> &slot = p.ready[pool_key(index, hardware_rt && device_rt(index))];
  if (!slot) slot = std::move(r);
}

std::unique_ptr<Renderer> take_renderer(int index, bool hardware_rt, bool *pending, std::string *error) {
  if (pending) *pending = false;
  if (!available() || index < 0 || index >= (int)devices().size()) {
    if (error) *error = "no such GPU";
    return nullptr;
  }
  hardware_rt = hardware_rt && device_rt(index);  // the same renderer either way on a GPU without RT hardware
  RendererPool &p = pool();
  std::lock_guard<std::mutex> lock(p.m);
  auto it = p.ready.find(pool_key(index, hardware_rt));
  if (it != p.ready.end() && it->second) {
    std::unique_ptr<Renderer> r = std::move(it->second);
    p.ready.erase(it);
    start_open(p, index, hardware_rt);  // the spare for the next view
    return r;
  }
  start_open(p, index, hardware_rt);
  auto f = p.failed.find(pool_key(index, hardware_rt));
  if (f != p.failed.end()) {  // failed less than 30 s ago
    if (error) *error = f->second.first;
    return nullptr;
  }
  if (pending) *pending = true;
  return nullptr;
}

bool renderer_ready(int index, bool hardware_rt) {
  if (!available() || index < 0 || index >= (int)devices().size()) return false;
  RendererPool &p = pool();
  std::lock_guard<std::mutex> lock(p.m);
  auto it = p.ready.find(pool_key(index, hardware_rt && device_rt(index)));
  return it != p.ready.end() && it->second != nullptr;
}

static std::atomic<bool> g_background_default{false};
void set_background_open_default(bool on) { g_background_default = on; }
bool background_open_default() { return g_background_default.load(); }

std::unique_ptr<Renderer> open_renderer(int index, bool hardware_rt, std::string *error) {
  if (available() && index >= 0 && index < (int)devices().size()) {
    RendererPool &p = pool();
    std::lock_guard<std::mutex> lock(p.m);
    auto it = p.ready.find(pool_key(index, hardware_rt && device_rt(index)));
    if (it != p.ready.end() && it->second) {
      std::unique_ptr<Renderer> r = std::move(it->second);
      p.ready.erase(it);
      return r;
    }
  }
  return Renderer::create(index, hardware_rt, error);
}

uint64_t renderers_generation() { return pool().generation.load(); }

bool renderers_opening() {
  RendererPool &p = pool();
  std::lock_guard<std::mutex> lock(p.m);
  return !p.opening.empty();
}

void set_open_delay_for_test(int ms) { pool().delay_ms = std::max(0, ms); }

void drop_ready_renderers() { drop_ready_renderers_for_test(); }

void drop_ready_renderers_for_test() {
  RendererPool &p = pool();
  std::map<int, std::unique_ptr<Renderer>> drop;
  {
    std::lock_guard<std::mutex> lock(p.m);
    drop.swap(p.ready);
    p.failed.clear();
  }
}

}  // namespace bl::gpu
