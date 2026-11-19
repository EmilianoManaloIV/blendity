# 0156. Provision MSVC 14.51 on hosted Windows build runners

**Status** Accepted by the owner on 2026-11-19 with [task 0591](../tasks/0591-hosted-msvc.md).

## Context

Task 0561 pins Windows CI and installer builds to MSVC 14.51. Its temporary recovery depends on a
retry receiving an older image. Two jobs on different revisions stopped before compilation when the
new image lacked that toolset. [Build failure](https://github.com/EmilianoManaloIV/blendity/actions/runs/44594445925/job/135871685492) · [Release validation failure](https://github.com/EmilianoManaloIV/blendity/actions/runs/44608999876/job/135860320654)

## Decision

The two GitHub-hosted Windows compilation jobs share a setup step. Reuse an installed x64 14.51
toolset when available. Otherwise ask the official Visual Studio Installer to add the versioned
x64/x86 component to the same instance the build selects. Before compiling, validate the active
compiler, linker and toolset in the build process. Keep the existing 14.51 servicing line as the pin
and record the resolved patch in each build. The setup happens only on disposable hosted runners.

## Consequences

A runner missing 14.51 needs Microsoft catalog and package access, disk space and time to install.
An unavailable component, failed install or failed activation stops that job with a diagnostic.
Local builds keep their existing developer-prompt behavior. Compatibility work for MSVC 14.52 follows
the existing unit and stress criteria. [Open work, item 19](../handoff/OPEN-WORK.md)
