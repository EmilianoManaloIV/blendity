# 0111: MoonRay built in CI and packaged for Blendity (spike: GO / NO-GO)

- **Status:** done
- **Requirements:** R-05, C-01
- **Decisions:** ADR 0041 (new)
- **Model:** main session (Opus)
- **PR:** (this task's)

## Goal
The user asked "Is it possible to integrate MoonRay from DreamWorks into this project?", then chose to "make this an
external renderer ... an optional install when using Blendity for the first time", built and hosted by us, run
through WSL2 on Windows. MoonRay ships as source only, so the first question is whether CI can build it and package
something a clean machine runs.

## Scope
- In: `.github/workflows/moonray.yml`: run by hand or by a push to `moonray-build/*`; Linux x86-64 in a
  `rockylinux:9` container, macOS on Apple silicon; MoonRay's dependency superbuild cached.
- In: `tools/moonray/`: `clone.sh` (the pinned tag, without the private `materialx_shaders` submodule),
  `linux_deps.sh`, `linux_build.sh`, `macos_build.sh`, `package_linux.sh`, `package_macos.sh`.
- In: the archive `blendity-moonray-<platform>-<tag>.tar.gz` with its `.sha256`, as a CI artifact. It contains the
  `moonray` program, every library it loads but glibc's / the system's, `rdl2dso/`, the launcher
  `bin/blendity-moonray`, `licenses/`, `test/rectangle.rdla` and `VERSION`.
- In: the archive checked on clean `ubuntu:22.04`, `ubuntu:24.04`, `debian:12` and `fedora:41` containers, and on
  `macos-15`.
- Out: publishing it (only when the user says so); a GPU (XPU) build.

## Acceptance criteria
1. The workflow builds within GitHub's 6-hour job limit.
2. The archive renders its test scene on every clean system above.
3. Its size is reasonable for a download (target under about 500 MB).

## As built
- **Linux: GO.**
  - CI builds MoonRay v2026.29.1 in Rocky Linux 9: dependencies cached, about an hour from scratch.
  - It packages a 296 MB archive with every library MoonRay loads but the system's, its scene objects, a launcher, a
    test scene and every licence.
  - Run 39114195746 renders the test scene on clean Ubuntu 22.04 and 24.04, Debian 12 and Fedora 41.
  - The archive, installed in WSL on Windows, renders Blendity's scenes as the path tracer does (task 0113, round 136).
- What it took:
  - MoonRay's private `materialx_shaders` submodule is skipped;
  - `--nocgroup` (libcgroup's RPM is gone);
  - Python 3.11 (Boost.Python fails on 3.13);
  - the dependencies cached the moment they are built;
  - the check containers need `seccomp=unconfined` (MoonRay calls `mbind`).
- **macOS: still building in CI.** CMake 4 needs `CMAKE_POLICY_VERSION_MINIMUM`, and autotools come from Homebrew.
  freetype2 was put on the include path for MoonRay's telemetry overlay. The dependency cache is now found by prefix
  after a script change.
- **Not yet published:** the archives stay CI artifacts until the user says to put them on a release.
