# 0591. Reproducible hosted Windows builds with MSVC 14.51 (round 609)

- **Status** In review.
- **Authorization** The owner approved CI-only 14.51 provisioning through the hub on 2026-11-19.
- **Decision** [ADR 0156](../decisions/0156-hosted-msvc-provisioning.md).
- **Baseline** Stack head `1e7bdb289fc065d752c8fd8480dc33dabf6bde49`. [Task 0561](0561-windows-toolset-pin.md) owns the 14.51 pin.

## Goal and scope

Build and validate the existing Windows targets on a standard GitHub-hosted runner whether or not
the image includes MSVC 14.51. Limit setup to the Windows compilation jobs in the build and release
workflows. Preserve local developer builds, job names, release gates and validation of both installers.
The implementation owner handles the two workflows, a shared hosted-only setup helper and its tests,
the runbook in `docs/handoff/OPEN-WORK.md` item 19 and the round 609 fragment. Coordinate any necessary
edit to `build.bat` with round 608 before changing the shared file.

## Installer and build procedure

1. Resolve the VS instance used by `build.bat` through `vswhere -latest -products * -requires
   Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`. Log runner image,
   VS instance, toolset inventory and candidate revision. Only the hosted Windows runner's build jobs
   may invoke setup. GitHub's Windows VMs run as administrators. [Hosted runner reference](https://docs.github.com/en/actions/reference/runners/github-hosted-runners)
2. Look for a usable x64 toolset under the selected instance's `VC/Tools/MSVC/14.51.*`. The original
   image can carry 14.51 as its default. Check actual tools even if the separately versioned installer
   component is absent from the installed-component inventory. Choose the newest usable servicing
   patch numerically and record its full version. Otherwise add
   `Microsoft.VisualStudio.Component.VC.14.51.x86.x64`. Microsoft's 18.12.1 catalog lists this exact
   component, version `18.12.12405.243`. [Microsoft catalog](https://download.visualstudio.microsoft.com/download/pr/646c5a89-4d3a-4f43-985e-9c4c6771f304/cc6841a1e5de4eadb9d1dec5c4d75dfe081c2e6b39981c220b6e69c50b557f39/VisualStudio.vsman) · [Versioned toolset support](https://learn.microsoft.com/en-us/visualstudio/releases/2026/release-notes-v18.11)
3. From the workspace or runner temporary directory call the installed VS Installer's `setup.exe
   modify --installPath "<instance>" --add Microsoft.VisualStudio.Component.VC.14.51.x86.x64
   --quiet --norestart`. Wait for the process and its exit code using PowerShell. The installer's
   `--wait` switch is supported by the bootstrapper only. Capture setup diagnostics and elapsed time.
   A restart-required exit must be reported explicitly and stop this job. Re-enumerate the tools after
   installer success. [Microsoft installer parameters and exit codes](https://learn.microsoft.com/en-us/visualstudio/install/use-command-line-parameters-to-install-visual-studio?view=visualstudio)
4. Activate the exact discovered patch with that instance's `vcvars64.bat -vcvars_ver=<patch>` in
   the same command process that calls the build. Check activation status, `VCToolsVersion`, and the
   first resolved x64 `cl.exe` and `link.exe` against the selected directory before compiling. Log the
   tool paths, compiler banner and full version. Keep `BLENDITY_VCVARS_STRICT=1`. Separate Actions
   steps start separate shells. At the baseline, `build.bat` skips activation when any compiler is
   already on PATH and warns on a version mismatch. [Windows build entry point](../../build.bat)
5. Invoke the helper in `windows-build` before the native build and in the release workflow's
   `windows` job before its expensive library fetch. Ensure helper-only changes also trigger release
   validation through the PR path filter. Each compilation job provisions its own VM. Retain read-only
   build permissions and the existing tag-only release gate. [Build workflow](../../.github/workflows/build.yml) · [Release workflow](../../.github/workflows/release.yml)

## Acceptance and evidence

1. With 14.51 already usable, a test records zero installer calls and selects the exact x64 compiler
   and linker. Cover 14.52 alongside it, another VS instance, multiple patches and an inherited
   compiler PATH. Fail before compiling on incorrect activation.
2. Starting without 14.51, one simulated installer call selects the expected component and instance.
   Cover missing setup, installer failure, reboot required and a success exit with the toolset still
   absent. Each failure returns nonzero before any compilation or artifact upload. Use scratch fixtures
   and a fake installer for local testing.
3. On an actual hosted runner initially missing 14.51, record before/after inventory, installer
   exit, elapsed time and selected tool paths. Invoke setup again on that VM to verify the real
   already-installed path. A run on an old pre-equipped image alone leaves real installation unverified.
4. At the reviewed revision record the workflow run URL, attempt and compiler patch. The build workflow
   passes Windows unit and quick stress. On a PR or branch dispatch the release workflow builds both
   Windows variants, probes libraries, packages both installation modes, exercises full and lite
   headless start/uninstall, and uploads the installer and screenshots. Its release-publish job skips.
   Keep the required aggregate checks and fragment check green on the combined candidate.
5. Update item 19 with observed hosted evidence and retain its separate 14.52 compatibility criteria.
   Mock results and the public component catalog establish intended behavior. A successful hosted
   installation and resulting builds establish actual runner support.

## Verification

Run `pwsh -NoProfile -File tools/dev/test_hosted_msvc.ps1` on Windows. The fixture uses a scratch Visual Studio
layout, stand-ins for `vswhere`, the installer and the compiler, and a stub build. PowerShell 7 and Windows
PowerShell 5.1 passed eight setup and eleven build scenarios, including negative exit codes from activation
and verification.

Diagnostic revision `13137680`, attempt 1, exercised both hosted image configurations on 2026-11-19.

- The regular build on image `20261103.298.1` already had toolset `14.51.36231`. Both setup
  invocations skipped the installer. The build verified x64 compiler `19.51.36260.0` and linker
  `14.51.36260.0`, then compiled the editor, tests and stress executable. [Build log](https://github.com/EmilianoManaloIV/blendity/actions/runs/44615390495/job/135883877438)
- The release-validation VM on image `20261116.315.1` initially had only 14.29, 14.44 and 14.52.
  The installer added `14.51.36231` in 651 seconds with exit 0. A second setup on that VM skipped
  the installer. Both builds verified x64 compiler `19.51.36263.0` and linker `14.51.36263.0`.
  The library probe, full and lite installation, headless screenshots, uninstall and artifact upload
  passed. The tag-only publication job skipped. [Release validation log](https://github.com/EmilianoManaloIV/blendity/actions/runs/44615391184/job/135887129038) · [Workflow gates](https://github.com/EmilianoManaloIV/blendity/actions/runs/44615391184)

The reviewed wrapper includes the signed-exit correction and hides the installer window. The
combined PR runs the normal unit, quick stress and installer checks on its own revision. [PR 497 checks](https://github.com/EmilianoManaloIV/blendity/pull/497/checks)
