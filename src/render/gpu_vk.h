// SPDX-License-Identifier: GPL-2.0-or-later
/* Internal to src/render: the process-wide Vulkan instance gpu_device.cpp opens, for the other Vulkan users in this
 * folder (task 0095's rasteriser, gpu_raster.cpp). The rest of the program goes through gpu_device.h. */
#pragma once

#include <vector>

namespace bl::gpu::vk {

/* The subset name is in vulkan_beta.h in some SDKs. Enabling it requires only the string. */
inline constexpr char kPortabilitySubsetExtension[] = "VK_KHR_portability_subset";

inline std::vector<const char *> base_device_extensions(bool portability_subset, bool swapchain) {
  std::vector<const char *> extensions;
  if (swapchain) extensions.push_back("VK_KHR_swapchain");
  if (portability_subset) extensions.push_back(kPortabilitySubsetExtension);
  return extensions;
}

}  // namespace bl::gpu::vk

#if defined(BL_WITH_VULKAN)
#  ifndef VK_NO_PROTOTYPES
#    define VK_NO_PROTOTYPES
#  endif
#  include <vulkan/vulkan.h>
#  include <shaderc/shaderc.h>

#  include <string>

namespace bl::gpu::vk {

/* The loader's vkGetInstanceProcAddr and the instance (null when Vulkan isn't there). */
PFN_vkGetInstanceProcAddr get_instance_proc_addr();
VkInstance instance();
struct PhysicalDevice {
  VkPhysicalDevice handle = VK_NULL_HANDLE;
  bool portability_subset = false;
};
/* The physical device behind devices()[index] (empty for a bad index). */
PhysicalDevice physical(int index);
/* The instance was made with the window-surface extensions (VK_KHR_surface and the platform's). */
bool surfaces();
/* GLSL to SPIR-V for any stage, cached in memory and on disk as the compute kernels are. */
std::vector<uint32_t> compile(const std::string &glsl, shaderc_shader_kind kind, std::string *error);

}  // namespace bl::gpu::vk
#endif
