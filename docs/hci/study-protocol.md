# The think-aloud study with SUS (protocol; first run in round 282)

## Who
Six artists, recruited during round 275: at least one each who works daily in Unity, Unreal and the Adobe suite
(Photoshop or Substance), one animator, two pen-primary (a drawing tablet or a 2-in-1), one keyboard-only. None has
used Blendity. Paid for their time; told it is the editor being tested, not them.

## Set-up
A fresh profile on the reference machine (a 13-inch 2-in-1, 1.5 UI scale, Tablet Mode on for pen users), the sample
scene, `Blendity --hci-log <file>` so every toast, hint shown, refusal, undo label and layout change is written with
a timestamp (no screen or audio capture; the facilitator takes notes). Two facilitators: one talks, one writes.

## Script (75 minutes)
1. (5 min) The origin picker: "I come from ...". Note the choice and the first thing they look for.
2. (20 min) **Task A - a prop:** model a crate from a cube (bevel the edges), unwrap it, give it a material, light
   it, render a still. Think aloud. The facilitator does not help unless the participant is stuck for two minutes
   (noted as an assist).
3. (20 min) **Task B - a character:** auto-rig the sample character, pose it, record a three-key clip, play it.
4. (15 min) **Task C - a stylized scene:** paint a bush of cards on the tree, scatter rocks, turn fog on.
5. (10 min) SUS (ten items, 1-5), then three transfer questions: "Which key did you reach for first for brush size?",
   "Where did you expect the sky settings?", "What did you expect Ctrl+N to do?"
6. (5 min) Debrief: the one thing they would change.

## What is recorded
Per task: time to complete, completed / abandoned, assists, errors (a refusal shown, an undo of a wrong action), hints
read aloud, the `--hci-log` file. Per participant: `study-<date>/p<n>.md` with the notes and the log's counts.
`summary.md`: SUS per participant, mean and range; median time per task; the top five observations, each mapped to a
task number or marked new.

## Targets
Formative, not statistical: SUS >= 70 (mean) with the range reported; median Task A time down 30 % against the
baseline run (the facilitators perform the tasks on the baseline build before round 272 lands and record their own
times as the expert floor, not as the baseline - the baseline is the first participant run). Every observation that
names a step maps back to the walkthroughs' rows.
