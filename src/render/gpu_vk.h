// SPDX-License-Identifier: GPL-2.0-or-later
/* Internal to src/render: the process-wide Vulkan instance gpu_device.cpp opens, for the other Vulkan users in this
 * folder (task 0095's rasteriser, gpu_raster.cpp). The rest of the program goes through gpu_device.h. */
#pragma once

#if defined(BL_WITH_VULKAN)
#  ifndef VK_NO_PROTOTYPES
#    define VK_NO_PROTOTYPES
#  endif
#  include <vulkan/vulkan.h>
#  include <shaderc/shaderc.h>

#  include <string>
#  include <vector>

namespace bl::gpu::vk {

/* The loader's vkGetInstanceProcAddr and the instance (null when Vulkan isn't there). */
PFN_vkGetInstanceProcAddr get_instance_proc_addr();
VkInstance instance();
/* The physical device behind devices()[index] (VK_NULL_HANDLE for a bad index). */
VkPhysicalDevice physical(int index);
/* The instance was made with the window-surface extensions (VK_KHR_surface and the platform's). */
bool surfaces();
/* GLSL to SPIR-V for any stage, cached in memory and on disk as the compute kernels are. */
std::vector<uint32_t> compile(const std::string &glsl, shaderc_shader_kind kind, std::string *error);

}  // namespace bl::gpu::vk
#endif
