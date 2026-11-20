## 2026-11-19 (round 609): Hosted Windows builds provision MSVC 14.51

**Asked:** Keep Windows CI and installer builds on MSVC 14.51 as the hosted runner image changes.

**Changed (task 0591, ADR 0156)**
- Select the newest usable x64 14.51 patch in the Visual Studio instance the build selects. Add the versioned
  component through Visual Studio Installer when the runner lacks it.
- Activate the exact selected patch in the build shell and verify the compiler, linker and toolset version before
  compilation. Log the runner image, revision, installer outcome and active tool paths.

**Checked:** Windows fixtures cover preinstalled and missing toolsets, repeat setup, installer failures and exact
activation for each build variant. See task 0591 for hosted verification.
