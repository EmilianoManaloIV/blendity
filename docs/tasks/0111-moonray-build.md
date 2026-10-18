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
- (Filled in from the CI runs.)
