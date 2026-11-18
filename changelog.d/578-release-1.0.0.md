## 2026-11-18 (round 578): Blendity 1.0.0

**Asked:** an official 1.0.0 release, today, with everything on the open stack in it (the owner, 2026-11-18).

**What Blendity is at 1.0.0.** A 3D editor in C++17 that works like Unity and is built on Blender's ideas: a
Hierarchy, an Inspector, Scene and Game views and Play mode over Blender's mesh layout, Edit Mode operators, UV
unwrapping, Principled materials, a rasterizer and a path tracer on the CPU and on a Vulkan graphics card, baked
lighting, rigging, skinning and animation; with Blockouts, drawing and Push/Pull for buildings, sculpting and
painting, Gaussian splats, imports of the common formats (OBJ, FBX, glTF, USD) and of Unity's packages and scenes
(a .blend comes in only through an installed Blender), a Learn tab that teaches each part, and
installers for Windows, macOS and Linux. It runs with Blender's prebuilt libraries or, dependency-free, on its own
code. The 0.x versions were pre-releases; the entries below are the whole history.

**Changed (task 0560)**
- The version is 1.0.0: `VERSION`, the version a build without it reports, the installer script's example, the
  manual, the release notes' link to the install guide, and the two books' title pages, colophon and install
  chapter, which no longer call the program a pre-release. A `v1.0.0` tag publishes "Blendity 1.0.0" as a release, not a pre-release, with the installers under the
  names the README links to (`release.yml` and `packaging/publish_release.sh` already did this for a 1.x tag: checked,
  not changed).
- The README's rewrite for a first-time visitor and its new pictures are round 577, in the same release; the Codex
  handoff (`AGENTS.md`, `docs/handoff/`) is round 575.

**Known issues at 1.0.0**
- With an avatar posed inside a world on the graphics card, frames take 17 to 25 ms on the machine it was measured on
  (an RTX 4070 SUPER), over the 16.7 ms of 60 fps: the card idles in its lowest power state while the editor gives it
  little to do (round 565). Alone, posing meets 60 fps there.
  The driver's own setting for the program (NVIDIA: "Power management mode: Prefer maximum performance" for
  Blendity.exe) may help; it has not been tested.
- A text or number field that Tab is editing can still be pushed out of view by rows that grow above it; other
  focused controls are brought back (round 573).
- The installers are not signed with a paid certificate: Windows and macOS ask once before opening them (`docs/INSTALL.md`
  says what to click). Macs with an Intel processor are not supported.
- The PDFs in `docs/` still carry 0.13.0 on their title pages; the release's own PDFs are built from the 1.0.0
  sources.
- The other open follow-ups are listed in round 574's entry, below.

**Checked:** CHECKS_578
