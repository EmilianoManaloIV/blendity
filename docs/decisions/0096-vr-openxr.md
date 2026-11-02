# ADR 0096: VR through OpenXR, its loader loaded at run time, eyes shown through Direct3D 11

- **Status:** accepted (round 383, task 0360)
- **Context:** the user asked for VR (2026-10-31): to experience the project at scale first, then sketch and paint in
  it. Headsets on Windows are reached through OpenXR (SteamVR, the Oculus app with Quest Link, Windows Mixed Reality),
  which is what Blender uses (intern/ghost/intern/GHOST_Xr*.cc). Blendity draws its views on the CPU, or on the GPU
  through its own Vulkan device (gpu::Raster). Neither is a graphics API an OpenXR runtime can take frames from
  without tying the runtime to device creation. ADR 0001's rule is that dependencies are only libraries Blender itself
  bundles.

## Decision
- **The OpenXR SDK** is the dependency: Khronos' `OpenXR.Loader` package (1.1.65, Apache-2.0 OR MIT). Blender bundles
  the same SDK as `xr_openxr_sdk` (its versions.cmake pins 1.1.53), so this stays within ADR 0001's rule.
  - **Where it goes:** it is unpacked into Blender's lib folder as `openxr/include` (the headers) and
    `openxr/bin/openxr_loader.dll`.
  - **The build:** `build.bat` finds it like any other library (`BL_WITH_OPENXR`, headers only) and copies the DLL
    beside Blendity.
- **No link-time dependency.** `render/xr_openxr.cpp` loads `openxr_loader.dll` from Blendity's own folder or System32
  (never the current directory) and asks it for every function.
- **What happens when something is missing:** a missing loader, runtime or headset leaves VR off with the reason.
  Builds without the SDK, and other platforms, compile VR out, with a reason too.
- **Frames go through Direct3D 11** (XR_KHR_D3D11_enable): a D3D11 device on the adapter the runtime names, a
  swapchain per eye, and each eye's pixels uploaded with UpdateSubresource.
  - **Why:** it needs no interop with Blendity's own renderers. Each eye is drawn as the Game view draws
    (`render_deferred`: the GPU when the views use it, else the CPU) and copied in.
  - **Cost:** about 11 ms a frame for both eyes at half an Index's size, in the default scene.
- **What every build has (`render/xr.h`):** the stereo maths and the session's state machine, written against a small
  `Runtime` interface, so a stub runtime tests them with no headset.

## Consequences
- **Platforms:** Windows only for now. Linux would need a Vulkan or OpenGL binding.
- **Performance:** the eyes cost a read-back and an upload. Drawing them straight into the swapchain through Vulkan
  (XR_KHR_vulkan_enable2, on gpu::Raster's device) is the faster path for later.
- **Live checking:** the suite never starts a real runtime, since that would launch SteamVR. The live check is the
  user's, step by step in task 0360's spec.
