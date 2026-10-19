# 0129: The AI download, an optional install as MoonRay's

- **Status:** done
- **Requirements:** R-13
- **Decisions:** ADR 0046
- **Model:** main session (Opus)
- **PR:** (with tasks 0128, 0130-0132)

## Goal
AI inbetweening's runtime and model downloaded on request, never part of the base install: Blendity loads ONNX
Runtime only when the feature is used.

## Scope
- In: `tools/inbetween/package.py`:
  - builds `blendity-ai-<platform>-ai-v1` per platform (Windows .zip, Linux and macOS on Apple silicon .tar.gz) with
    a `.sha256` each;
  - each holds Microsoft's ONNX Runtime 1.31.0 library (MIT, its licence and notices), the trained model
    (`inbetween.onnx`), its description (`inbetween.json`) and a README crediting the CMU database.
- In: `ai::unpack` into `~/.blendity/ai/ai-v1` (`BLENDITY_AI_DIR` in the tests), then `ai::probe` (a test inference
  of 60 frames). An install that can't run is removed again, with why.
- In: `ai::load_runtime`: ONNX Runtime loaded with LoadLibrary or dlopen, through its C API (`extern/onnxruntime/`,
  its MIT headers). Nothing links against it.
- In: the editor:
  - the AI dialog, offered once, the first time the switch is turned on (never at startup), from the libraries
    release through the updater's checked download;
  - Preferences > Animation Assistants: the install's state, Install / Remove;
  - `ai status | install [archive] | remove | check`.
- Out: publishing the archives (the user's call); GPU execution providers (the CPU is fast enough: see 0128).

## Acceptance criteria
1. An archive that doesn't match its checksum is refused; an install that can't run is removed again.
2. Without the download, the feature says how to get it.
3. With a real install, ONNX Runtime loads and makes 60 frames in under 100 ms on the CPU.

## As built
- Tests: round 149 (the real install's test runs where `BLENDITY_AI_TEST_INSTALL` names one: skipped on CI).
- The plan's CI workflow wasn't needed: unlike MoonRay nothing is compiled. The archives are Microsoft's prebuilt
  library and the model, put together by `package.py`.
