# 0007: Camera filters are components with a raster stage and an image stage

- **Status:** accepted (2026-10-09, approved with the plan for tasks 0004 and 0005)
- **Requirements:** R-10, C-02

## Context
The user wants per-camera "filters", the first family recreating 3D graphics on the PlayStation 1,
N64, Sega Saturn and DOS PCs. Blendity has no GPU rasterizer: the Game view, Camera Preview and
rasterized renders come from the CPU rasterizer (`Renderer3D`), and Vulkan is used only for path-tracing
compute. These looks come partly from how the hardware drew triangles and partly from its output:
- How it drew:
  - vertices snapped to whole pixels (the PS1 "wobble");
  - textures interpolated without perspective correction (affine warp);
  - nearest-neighbour or 3-point texture filtering, small textures, no mipmaps.
- What it output:
  - a low resolution;
  - 15-bit colour with an ordered dither;
  - a 256-colour palette.

## Decision
- **Filters are components** on the camera's GameObject (base `CameraFilter`, several allowed, applied in
  component order), like Unity's post-processing effects. The first is `Retro Console Filter`, with a
  Console preset (PS1, N64, Saturn, DOS) that fills in editable fields.
- **Each filter contributes to a `FilterStack`** (`src/render/camera_filter.h`):
  1. **Raster stage:** settings the rasterizer applies while drawing (`RasterOptions`):
     - vertex snapping;
     - affine UVs;
     - a texture override (filter, size cap, mipmaps).
  2. **Internal resolution:** the camera renders at that size and is scaled up nearest-neighbour, filling
     the view (square pixels) or letterboxed to the console's own frame.
  3. **Image passes:** functions over the finished 8-bit target and its depth (fog, colour depth,
     dither), run in order, like depth of field.
- **One entry point, `Editor::render_camera`**, renders a camera through its stack, for the Game view,
  the Camera Preview, F12 renders and camera sequences. The Scene view uses the main camera's stack
  when its "Camera Filters" toggle is on.
- **The path tracer** takes the internal resolution and the image passes that don't need depth. It
  has no raster stage, so there is no wobble or affine warp in path-traced renders.
- **CPU only**, with no new dependency (ADR 0001): identical on every platform and in the
  dependency-free build.

## Consequences
- A filtered camera renders fewer pixels, so it is usually faster than an unfiltered one.
- The rasterizer gains three small branches: the snap in screen positioning, the affine barycentrics
  in shading, and the texture override. With no filter, images are bit-identical to before (tested).
- New filters are new `CameraFilter` components; they need no changes to the editor's render paths.
- Image passes see display-referred 8-bit colour, not HDR. Filters that need HDR (bloom) would need a
  float target later.
