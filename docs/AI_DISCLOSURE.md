# AI disclosure

Blendity is written with an AI. This page says how, so that anyone reading, using or building on the project knows
what the machine did and what a person did.

## How Blendity is built
- **The direction is human.** Emiliano Manalo decides what Blendity is for, which features it gets and in what order,
  reviews the work on GitHub and merges every pull request. Nothing lands without that review.
- **The code is written with Claude.** The C++ source, the tests, the stress sections and the documentation are
  written by Anthropic's Claude through Claude Code, working from task specifications (`docs/tasks/`) and
  architecture decisions (`docs/decisions/`) that the human approves before code is written, under the engineering
  standards in `CLAUDE.md`. A second model pass reviews each change against its spec before it is opened as a pull
  request, and its findings are fixed or answered in the task's record.
- **Every commit says so.** Commits carry a `Co-Authored-By: Claude ...` trailer naming the model, and pull requests
  end with "Generated with Claude Code". The CHANGELOG records, round by round, what was asked and what changed.
- **Numbers are measured, not guessed.** Check counts, timings and the stress suite's figures in the CHANGELOG and
  README come from runs on the machines named there. Where something was not tried (a pen on hardware, a
  particular GPU), the task's record says so.

## What is and isn't in the program
- **No telemetry.** Blendity sends nothing about you or your work anywhere. The one network use the editor makes on
  its own is an optional, once-a-day check for a newer release on GitHub (Preferences > Updates, off by one switch).
- **No AI at run time, except where it says so.** The editor's tools - modeling, sketching, rendering, rigging,
  physics - are ordinary code. The one learned model is **AI Inbetweening** (task 0130): an optional download
  (ONNX Runtime and a small network Blendity trained on CMU motion-capture data), offered only when you turn the
  switch on, running locally on your machine, showing its result as a ghost you apply or discard. It never changes a
  scene on its own. No other feature calls a model, local or remote.
- **No cloud.** Scenes, assets, renders and preferences stay on your disk.

## Using what Blendity makes
Models, textures, renders and animations you make in Blendity are yours. The program's source is GPL-2.0-or-later
and its binaries GPL-3.0-or-later ([licenses/README.md](../licenses/README.md)); that covers the program, not your
work.

## Questions
Open an issue on GitHub. If something in this page is out of date, that is a bug: say so.
