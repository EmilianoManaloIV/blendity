# Task 0288 - Render, MoonRay and AI verbs without the console (round 308)

Part of the console-only-to-UI plan (tasks 0271-0273, 0286-0290). Every control calls the code the console calls.

## What
| Verb | Now reached by |
| --- | --- |
| `renderanim first last fps` | the Render window: First Frame, Last Frame, Frame Rate under Render Animation (0: the sequence's or the clip's own) |
| `rendersequence <folder>` | the Render window: Sequence Folder (typed, or ... for the folder dialog; empty: Renders/<scene>_sequence_<time>) |
| `renderanim ffmpeg <path>` | Preferences: FFmpeg (typed, Enter, or ... for the file dialog; empty: the PATH's) |
| `moonray folder <path>` | Preferences > Renderers: Your own MoonRay (a folder) |
| `moonray check` | Preferences > Renderers: Check (enabled once MoonRay is installed) |
| `ai install <archive>` | Preferences > Animation Assistants, AI Inbetweening: From a File... |
| `ai check` | the same row: Check (when installed) |
| `ai span from to` | the Animation window's AI "..." options: From / To and Make This Span |
| `ai finetune on / off` | the same popup: Use the Fit to My Clips |

The "..." options sit right after AI Inbetween, so they show whether the download is installed or not (Make This Span
then says it needs the download, as AI Inbetween does).

## Acceptance
- `tests_round308` (tests/test_editor.cpp): the Render window's range fields and Sequence Folder drawn; FFmpeg's path
  typed into its field and applied by Enter; From a File... without a file dialog says how else; the AI options
  turn the fit off and list Make This Span - by real clicks and keys.
- The Render Animation range test (tests/test_animation.cpp): with First 30, Last 40 and 12 fps in the fields, a click
  on Render Animation renders frames 30 to 40 at 12 fps.
- MoonRay's row and both Check buttons need an install; they are not driven by the tests (the MoonRay and AI tests
  that run the real programs keep covering `moonray check` and `ai check`).
- The whole suite in the dependency-free non-LTCG build; the chain on main.
