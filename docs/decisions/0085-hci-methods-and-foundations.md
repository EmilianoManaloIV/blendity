# ADR 0085: the HCI pass over the whole editor - the methods, the foundations, the measuring sticks

- **Status:** accepted (task 0251, round 271; the pass runs through task 0262)
- **Relates to:** ADR 0073 (the HCI and modularity pass over windows, tabs and menus), ADR 0059 (per-window view
  state), ADR 0066 (the pen and touch input), the Learn tab (task 0039), the command palette (task 0055)

## Context
Task 0235 audited the editor's chrome against Nielsen's heuristics and Fitts's law and fixed what it found, but left
the Scene view's tool modes and the windows' workflows out. Three read-only audits in October 2026 - modeling, placing
and UV; lighting, rendering, compositing and materials; animation, rigging, Play, files and the shell - found the
same seven problems repeating in every area: refusals and progress that live only in the console; shortcut labels
hard-coded and wrong under any keymap but Unity's; silent data loss on New / Open / Exit; pen and tablet gaps
(wheel-only parameters, hover-dependent modals, middle-drag panning, right-click-only actions, 5 px targets);
one thing done in several places or under several names; invisible modes (Record, Play, the GPU fallback); and
features hidden from the paths artists look in. The user asked for additions chosen by HCI methods, with artists from
Unity, Unreal and the Adobe suite on keyboard, pen and tablet as the lens, and for the whole program to be covered.

## Decision
1. **Methods, named per change.** Every addition in this pass states the heuristic or model that motivates it:
   Nielsen's ten heuristics (H1-H10), Fitts's law for target size and distance, Hick's law for menu and palette
   length, the keystroke-level model (Card, Moran and Newell) for frequent actions, the cognitive walkthrough's four
   questions (Polson et al.) for each pipeline step, transfer of learning for users arriving from another tool, and
   WCAG 1.4.1 / 1.4.11 for colour and contrast.
2. **Foundations before areas.** The repeated problems get one mechanism each, used by every area afterwards:
   toasts with a fix and buttons that say why they are disabled (F1); one job tray with progress, time left and
   cancel (F2); the status bar's hints from a table keyed by mode and state (F3); every shortcut label from the keymap
   through `shortcut_text` (F4); unsaved-changes prompts (F5); a History panel over `UndoState::label` (F6); a
   colour-vision-safe palette and a High Contrast theme with state cues never by colour alone (F7).
3. **Measuring sticks in the test suite** (F8, this task): a Fitts audit that dumps every recorded widget rectangle per
   window and tool mode at two UI scales and reports the targets a pen cannot hit; the coverage map extended to
   buttons, fields and sections; a static audit of hard-coded shortcut labels; a status-hint completeness test over
   every mode and state. Each prints its number; the thresholds become assertions as the rounds fix what they find.
4. **Evaluation artefacts live in `docs/hci/`:** the cognitive walkthroughs (one per pipeline), the KLM table, the
   Fitts reports per round, the study protocol and its runs. The baseline is taken before any fix lands, so each
   later round's effect is a diff in git.
5. **Transfer is a first-class target.** The keymap grows an Adobe / Substance preset beside Unity, Unreal, Blender,
   Maya, 3ds Max and SketchUp; onboarding asks where the user comes from and shows each guide's "in Unity this was..."
   line; the manual's shortcut table is generated from the presets so it cannot drift.

## Consequences
- Refusals keep their Console line; the toast is an addition, so scripts and tests reading the log see no change.
- The job tray generalises the Skin job's progress pattern; workers that cannot stop mid-kernel discard their result
  on completion when cancelled, which is honest and cheap; true interruption comes where a kernel allows it.
- Labels from the keymap mean a menu item may show no key in a preset that lacks one, which is the truth.
- The widget coverage rows will be numerous and mostly empty at first; the coverage check counts them only once the
  area rounds have driven them, so the chain does not go red on the baseline.
- A pass of twelve rounds: the foundations (272-274) touch many files and are reviewed as cross-cutting changes; each
  area round (275-281) is reviewed against the walkthrough that motivated it; the study (282) measures the whole.
