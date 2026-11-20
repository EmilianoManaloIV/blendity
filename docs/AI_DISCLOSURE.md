# AI disclosure

Blendity is written with an AI. This page says how, so that anyone reading, using or building on the project knows
what the machine did and what a person did.

## How Blendity is built
- **The direction is human.** Emiliano Manalo decides what Blendity is for, which features it gets and in what order,
  reviews the work on GitHub and merges every pull request. Nothing lands without that review.
- **AI assists with development.** Anthropic's Claude through Claude Code helped build Blendity through 1.0.0.
  New work continues
  with OpenAI Codex under the engineering standards in [AGENTS.md](../AGENTS.md). The owner approves task specs
  and architectural decisions before implementation. Separate workers test from the spec and review the change
  before publication. The round's record reports the findings and their resolution.
- **Attribution follows the work.** Preserve historical `Co-Authored-By: Claude ...` trailers and
  "Generated with Claude Code" pull-request disclosures. New AI-assisted
  commits carry a `Co-Authored-By` trailer for the tool that helped write the change, for example
  `Co-Authored-By: OpenAI Codex <noreply@openai.com>` for Codex. Pull requests identify the tool and its role.
  The CHANGELOG records what was asked and what changed in each round.
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

## Authorship and copyright
Blendity is copyright Emiliano Manalo and contributors. The project licenses its work GPL-2.0-or-later
(binaries GPL-3.0-or-later) to the extent copyright subsists in it, including the human selection, direction,
review and editing. A `Co-Authored-By` trailer records attribution. The repository's licence files describe
the grant. No third-party
code is claimed as the project's own: what is copied is listed with its licence in `licenses/README.md`, what is
ported keeps its origin's notice in its header, and what is followed is credited in `CREDITS.md` and in the two books'
References chapter (ADR 0104). The AI is a tool at build time, not in the product: the GPL program depends on no
service.
