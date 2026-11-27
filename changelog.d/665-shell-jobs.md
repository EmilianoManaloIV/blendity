## 2026-11-27 (round 665): Choose the shell build's parallel job count

**Asked:** Honor a caller's job limit on Linux and macOS.

**Changed:** Set `JOBS=2 ./build.sh release tests`, for example, to give the compiler dispatcher two jobs. An unset or empty count uses the detected processor count. Zero and malformed values stop the build before compilation.

**Checked:** The build entry point passed one and two to the dispatcher and used the detected seven for both default cases in an isolated shell fixture. It rejected zero, all-zero digits, a negative count, text and a value containing whitespace.
