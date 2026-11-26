## 2026-11-26 (round 637): Keep CPU libraries available with a partial Vulkan bundle

**Asked:** Preserve optional CPU acceleration when the build bundle is missing Vulkan development files.

**Fixed (task 0619, ADR 0001)**

- The Windows batch, Unix shell and CMake build entry points enable Vulkan when the public Vulkan and shaderc headers and the platform's shaderc link library are present. A missing file leaves the CPU libraries available.

**Checked:** 28/28 Windows and Linux plans passed. The baseline failed all 12 partial-bundle cases and passed all 16 controls. GCC 15.2 reproduced the missing shaderc header failure in the original Linux build. The corrected Linux build compiled the device code in each configuration. Its partial-bundle probe reported no GPU backend, and the complete-bundle archive linked and returned its SPIR-V version.
