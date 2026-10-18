# 0041: MoonRay is an external renderer: a separate program, downloaded on request, run in WSL on Windows

- **Status:** accepted (tasks 0111-0114; the user chose "an external renderer ... an optional install when using
  Blendity for the first time", "We build and host it" and "Run it through WSL2")
- **Date:** 2026-10-18
- **Requirements:** R-05, C-01

## Context
The user asked whether DreamWorks' MoonRay could be integrated. MoonRay is Apache-2.0, ships as source only, builds
on Linux (Rocky Linux 9) and macOS, and needs about a dozen libraries Blender doesn't bundle: ISPC, OpenImageIO,
OpenVDB, USD, Boost, Lua, JsonCpp, Random123 and others. ADR 0001 lets Blendity link only libraries Blender ships, each
optional.

## Decision
- **A separate program, never linked.** Blendity writes the scene in MoonRay's RDLA, runs `moonray -in scene.rdla`
  and reads the image it writes. ADR 0001 is unchanged: nothing of MoonRay's is linked, and the dependency-free build
  works the same. The GPL binaries run it as they run `curl` or `ffmpeg`.
- **Built and hosted by us.** A manual workflow (`.github/workflows/moonray.yml`) builds MoonRay at a pinned release
  (`moonray::kTag`, now v2026.29.1) with its own dependency build: Linux x86-64 in Rocky Linux 9, macOS on Apple
  silicon. It packages the `moonray` program, every shared library it loads but glibc's / the system's, its scene
  objects (rdl2dso), a launcher setting their paths (`bin/blendity-moonray`), a test scene and every licence, as
  `blendity-moonray-<platform>-<tag>.tar.gz` with a `.sha256`. They are published to the `libraries` release only when
  the user says so.
- **Offered, never forced.** The first run asks "Download it?" (Download / Not now / Never ask), as task 0080 asks
  about Blender's libraries: online only, never in headless runs, again at most once a day after Not now.
  Preferences > Renderers and the console's `moonray` install or remove it, or point at the user's own MoonRay build.
  The download is checked against its `.sha256`, unpacked into `~/.blendity/moonray/<tag>`, and must render its test
  scene before it counts as installed.
- **Windows through WSL2.** Blendity runs `wsl.exe -d <distro> -e .../blendity-moonray`, with its paths translated
  (`C:\...` -> `/mnt/c/...`), and installs into the distribution's home. Without WSL it says how to get it
  (`wsl --install` needs an administrator, so Blendity never runs it).
- **F12 only.** Render Engine > MoonRay renders F12 images, camera sequences and animations. Previews and the views
  keep their own renderers. Without MoonRay installed, F12 path traces and says why.
- **What doesn't carry over is said, not dropped silently:** toon shading, camera filters, procedural and generated
  textures, Box / Generated mapping, alpha clipping, Unity's light Range curve (MoonRay's lights fall off physically;
  brightness is matched at 1 m), toon outlines. Each note is logged once a session.
- **MoonRay is looked for only when needed** (the first-run or daily question, Preferences, the console, an F12 with
  MoonRay selected): on Windows that starts WSL, which costs memory, so not at every start.

## Consequences
- MoonRay renders Blendity's scenes with no change to Blendity's build, its dependencies or its licences.
- Building MoonRay takes hours of CI (its dependencies cached); a new MoonRay release is a new `kTag` and a new run.
- The archive is large; the question says its size before anything is downloaded.
- A mesh is written per object (no instancing yet), so very large scenes write large files.
- Tests use a stand-in for MoonRay (`blendity_tests --fake-moonray`), so CI needs neither MoonRay nor WSL; the real
  archive is checked by the workflow on clean Ubuntu 22.04 / 24.04, Debian 12, Fedora 41 and macOS 15.
