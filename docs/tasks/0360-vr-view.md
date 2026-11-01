# Task 0360: VR View (round 383)

## Asked
Phase C's first round (plan cozy-fluttering-corbato; the user, 2026-10-31: "VR support so I can paint and sketch in VR. Or
at least experience the project through VR to get a sense of scale"; viewing first):
- the scene in a headset at 1:1, head-tracked;
- standing where the Scene view looks;
- OpenXR, so SteamVR, Quest Link and Windows Mixed Reality all work;
- VR off, with the reason, when there's no runtime or headset.

## Built
- **`render/xr.h` / `xr.cpp`, in every build:**
  - **The stereo maths:**
    - OpenXR's right-handed, -Z-forward poses mirrored into Blendity's left-handed, +Z-forward axes;
    - an eye's view matrix;
    - an off-centre projection from the eye's four field angles, in Blendity's convention (depth 0..1; a symmetric one
      is `Mat4::perspective`);
    - the stage (the headset's floor) placed at y = 0 below the Scene view's pivot, facing its yaw, at a scale, raised
      1.6 m for a runtime with only a local (seated) space.
  - **`Session`, the session's state machine against a `Runtime`:**
    - Off, Starting, then Running once the runtime says Ready;
    - each frame: wait, begin, locate both eyes, draw and submit each, end with the projection layer, or end empty
      when the runtime doesn't want it drawn;
    - Stopping ends the session; Ready again (the headset woke) begins it again; Exiting lets it go;
    - the user's stop lets a running session go at once, without xrEndSession, which is only valid once the runtime has
      said Stopping;
    - a loss pending, or a frame not waited for, is Lost with the reason.
- **`render/xr_openxr.cpp`** (the build with Blender's libraries, Windows):
  - `openxr_loader.dll` is loaded at run time, from Blendity's folder or System32 only, so the program needs nothing
    at link time and runs anywhere (ADR 0096);
  - XR_KHR_D3D11_enable, with a Direct3D 11 device on the adapter the runtime names;
  - the stage space if there is one, else the local space;
  - a swapchain per eye at `resolution` of the recommended size, in an 8-bit BGRA or RGBA format;
  - each eye's pixels uploaded into its image, with red and blue swapped for RGBA and the values made linear for a
    plain (non-sRGB) format;
  - an image whose wait failed is released, and the frame ends without the layer;
  - position tracking lost for a moment keeps the last position instead of blanking the headset.

  The reason given when VR can't start:

  | Situation | Reason |
  |---|---|
  | No loader | "no openxr_loader.dll beside Blendity" |
  | No runtime | "no OpenXR runtime answered" |
  | No headset | "no headset: SteamVR sees none connected" |
  | No Direct3D 11 | "...has no Direct3D 11 support" |
- **The editor:**
  - **Commands:** `vr start [resolution]`, `vr stop`, `vr status`, `vr here` (the stage moved to where the Scene view
    looks now), `vr scale <s>` (the world at 1:s).
  - **Menu:** Edit > VR View / Stop VR View, its tooltip giving the last reason.
  - **Each frame**, after Live Link: the session's tick. Each eye is drawn as the Game view draws (`render_deferred`:
    the GPU when the views draw there, else the CPU) at half the headset's size each way by default.
  - **While VR runs** the editor draws every frame, and `xrWaitFrame` paces it instead of the editor's frame cap.
    Idle, the editor would otherwise draw one frame a second and freeze the headset.
- **The SDK:**
  - The OpenXR SDK's `OpenXR.Loader` 1.1.65 package (Khronos' release, Apache-2.0 OR MIT) unpacked into Blender's lib
    folder: `lib/windows_x64/openxr/include` (headers) and `openxr/bin/openxr_loader.dll`.
  - `build.bat`'s `:lib openxr BL_WITH_OPENXR` finds it, and the DLL is copied beside Blendity.
  - The dependency-free build and Linux have VR off and say why.

## Checked
- **The maths:**
  - an off-centre projection's edges land on the screen's;
  - a symmetric one equals `Mat4::perspective`;
  - a turn mirrors as the space does;
  - a point 5 m ahead is 5 m deep at the centre;
  - at 1:2 the eyes are twice as high.
- **The session, on a stub runtime:**
  - the calls in order for a drawn frame;
  - an empty frame;
  - Stopping and Exiting;
  - Lost;
  - no headset at the start.
- **A stereo frame of a known scene:** a red cube 3 m ahead, two 512 x 512 eyes 64 mm apart at 90 degrees.
  - Its centre is 5.51 px apart between the eyes (5.46 expected), at the same height, and right of centre in the left
    eye.
- **The verbs,** and that this build's runtime is the loader (not started in the suite: it would start SteamVR).
- **Up is up:** the cube 30 cm above the eyes appears 25.6 rows above the middle (measured 26.6).
- **Cost:** a frame with both eyes at 1008 x 1120 (half an Index's size) takes 10.9 ms on average, 12.4 ms at most, in
  the default scene. A heavier scene costs what its Game view costs, twice.
- **Starting:** `vr start` creates the instance and finds the headset on the main thread. With SteamVR not running,
  that launches it, and the editor waits seconds for it.

## Your own check with the headset
1. Connect the headset and start SteamVR (or the Oculus app with Quest Link, or Windows Mixed Reality) so it shows as
   ready.
2. In Blendity, look at the scene in the Scene view where you want to stand, then choose Edit > VR View (or type
   `vr start`). `vr status` should say "running on SteamVR" with frames counting up.
3. Put the headset on.
   - **What to see:** the scene at life size, the floor on the floor; a 1 m cube should look a metre across.
   - **Scale:** `vr scale 2` makes the world look half size.
   - **Moving the stage:** `vr here` moves the stage to wherever the Scene view looks now.
4. `vr stop` (or Edit > Stop VR View) ends it.

**Comfort:** if the picture is slow, `vr start 0.35` draws smaller eyes; SteamVR's reprojection fills in.

## The review's fixes (Opus, round 383)
- **An idle editor no longer drops the headset to one frame a second:** it draws every frame while VR runs.
- **The session resumes** after Stopping, then Ready.
- **The ADR:** ADR 0096 records the dependency and the D3D11 choice. The loader has a row in licenses/README.md.
- **The user's stop** no longer calls xrEndSession outside Stopping.
- **A failed eye** ends the frame without the layer, and its image is released.
- **Brief tracking loss** keeps the last position instead of blanking.
- **Plain UNORM swapchains** get linear values.
- **A local space** is raised to a standing height.
- **The loader** is searched for only in Blendity's folder and System32.
- **Odd numbers** are refused.
- **New tests:** resuming, stopping, up in the picture, frames while running, the cost.

## Limits
- **Drawing:** the eyes go through a read-back and an upload. Drawing them straight into the swapchain (Vulkan, on
  gpu::Raster's device) is a later step.
- **LODs:** the eyes draw every LOD Group at its level 0. Only the Game view picks levels.
- **Platforms:** Windows only for now: the swapchains are Direct3D 11's.
- **No controllers yet.** Teleport, grab and scale come next (round 384).
