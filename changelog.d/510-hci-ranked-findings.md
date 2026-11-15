## 2026-11-14 (round 510): the survey's ranked findings that are fixes (task 0494)

**Asked** (the hub, Pass 25's HCI stream): what round 505's survey ranked after the sweep's own faults and is a fix, not a
feature - no text sends the artist to the Console where a control does the job, no shortcut is written into a string,
the smaller things the pictures showed are right, and what round 509 left.

**Fixed**
- **Texts that send the artist to the Console (finding 7), 13 to 5.** Fix Model and Create Visemes name no command (the
  buttons are the text); Decimate to fit's refusal says "Profiler window > Budget"; the UI file's tooltip says the
  Console window lists what a loaded file brings; the polygon sides tooltip says 5, 6 or 8; the ID mask warning says
  "Bake All Maps (Textures window, on the surface)"; VR paint says "Paint in the Modeling Tools window's Paint
  section"; the CPU-view tooltip says the Profiler window lists what the card can't draw. The baseline is 5: Blockout's
  three, Unbake's help, and the Lights Gamma warning (no control for Lights Gamma exists; the command is its only way).
- **Shortcut labels written into strings (finding 8), 16 to 0.** The Board's Paste text and tooltip, the Sequencer's Add
  Edit / Ripple Delete rows, the Draw window's Undo and Redo tooltips and More menu rows, the Profiler's "F12 stays
  exact", the Send to Blendity toast take the keymap's text, and read as a sentence with no key. The Sketch Pad's
  selection keys (Alt+Backspace, Ctrl+D, Ctrl+T) and the Draw tool's Tab (Project: View / Closest) are keys those tools
  read themselves - no keymap action - and join the label audit's fixed gestures.
- **Finding 13.** Preferences' Animation Assistants has its five switches at one x, the heading the first one's label; "not installed" no
  longer repeats the Install... button beside it; the Animation window's Play button says Play (Pause while it runs) and
  the arrows stay the key steps, each with its own name and a tooltip.
- The Sequencer's toolbar wraps item by item, so every control is inside its pane at 1024x640 x1.5 and the body starts
  under the last row; the Game view's camera dropdown stays in a narrow header, down to a control's height (its name
  then in the tooltip; under that it is left out and the Hierarchy picks the camera). The Profiler lists "Scene view on
  the CPU" with the card's reason also when no view is on the card (the row the CPU badge's tooltip sends you to).

**Added**
- The toolkit records its hand-drawn widgets (`mark_widget`, `text_area`, `drag_label`, `curve_field`, `color_ramp`) as
  controls of kinds mark, textarea, draglabel, curve and ramp, with no Tab stop; the sweep counts them and judges them
  for `covered` and size, and prints "hand-drawn, no keyboard stop" per configuration. The X, Y, Z letters of a vector
  field (10 px wide scrub handles, the number field beside each being the full target) are reported as `small-handle`.

**Checked:** the whole unit suite on Windows with Blender's libraries in a fresh folder (36,857 checks: the three card-picture
tests of the `gpu` group that fail on main too, and two load-timed tests that passed when run again on a quiet
machine); the `hci` and `editor` groups dependency-free (1,083 and 2,328, the same load-timed bake test passing on a
rerun) and on Linux (1,082 and 2,333), with animation, board, assets and render 0 failed there; the five static
audits at their baselines; one code review (changes needed, all made) and the test-engineer's six tests. The numbers
and what is left, each with its owner, are in the write-up of `docs/tasks/0494-hci-ranked-findings.md`.

<!-- docs/USABILITY.md -->
after: | Set Origin could only use fixed points
| Eight places told the artist to type a command in the Console (13 to 5 by the audit); the Board's, Sequencer's, Draw window's and a few other shortcut labels were written into the text and right in one keymap only. (round 510) | They name the button or menu; those labels follow the keymap. (The Sketch Pad's four keys and the Draw tool's Tab were right in every keymap: fixed gestures.) |
| The Sequencer's last toolbar buttons could not be reached in a narrow pane; the Game view lost its camera dropdown; two buttons were both ">". (round 510) | The toolbar wraps; the dropdown shrinks; Play says Play. |
