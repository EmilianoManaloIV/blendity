# 0114: MoonRay in F12, camera sequences and animation renders

- **Status:** done
- **Requirements:** R-05
- **Decisions:** ADR 0041
- **Model:** main session (Opus)
- **PR:** (with tasks 0111-0113)

## Goal
Render Engine > MoonRay renders F12 images in MoonRay, with progress and Stop, and shows the image through Blendity's
own colour management.

## Scope
- In: `RenderSettings::engine` 2, "MoonRay (external)", with Samples and Max Bounces; the console's `engine moonray`.
- In: F12 (`start_moonray`, `begin_moonray_render`, `step_moonray_render`):
  - the scene exported to a scratch folder and MoonRay run on its own thread (through WSL on Windows);
  - its progress parsed into the Render window's status;
  - Stop kills it;
  - the EXR read back (`decode_exr_plain`: uncompressed scanline EXR without Blender's libraries), the background a
    camera's solid colour or transparent, then the view transform and exposure as for the path tracer.
- In: camera sequences and animation renders frame by frame (they wait for F12 as for the path tracer).
- In: without MoonRay installed, or where it can't run, F12 path traces and says why; previews always path trace.
- In: a Learn section ("MoonRay, an optional third engine").
- Out: camera filters on a MoonRay image; live preview in MoonRay.

## Acceptance criteria
1. With a stand-in MoonRay, F12 shows its progress and its image (through the view transform), transparent films keep
   its alpha, a failure shows MoonRay's own message, Stop ends it within a moment.
2. Without MoonRay, F12 path traces.
3. An uncompressed EXR reads without the libraries (float and half); a compressed one is refused by name.

## As built
- Tests: round 135.
