# 0617: Find viewport quality while editing

- **Status:** in review
- **Authorization:** Owner assigned pass 35 round 635 to assess editor workflows and retain a consequential UI fix.
- **Requirements:** R-05, U-04, U-07
- **Decisions:** ADR 0026, 0036, 0060, 0085 and 0145
- **Implementation:** project implementer
- **PR:** [#504](https://github.com/EmilianoManaloIV/blendity/pull/504) against main. First published head 2caef8084bd9e48998bef2525796d79fafcf4068.

## Goal
An artist who wants to make the Scene view cheaper while editing can find and apply Low viewport quality by name, then return to Medium or High by the same route. The base palette includes Preferences but has no entries for these presets. Its search matches entry names and categories.

## Scope
- Extend the existing command palette with the three applied viewport presets. Use the existing quality commands. Make the route discoverable in the existing quality lesson.
- Assess viewport interaction, Low settings discovery, paint and undo, keyboard use at small window sizes and import feedback against actual interaction checks and earlier audits.
- Rendering algorithms, new shortcuts and changes to other workflows are outside this implementation.

## Likely files
- src/editor/command_palette.cpp (palette_entries and kViewportQuality), src/editor/lessons.cpp (quality tiers)
- tests/test_hci.cpp, changelog.d/635-viewport-quality.md

## Acceptance criteria
1. From a 1024 by 640 editor, open the palette by keyboard, type a query containing Low and viewport or quality, and activate the matched preset. The Low tier takes effect (67% viewport resolution).
2. Medium and High can be found and applied the same way. Canceling a pending selection leaves the active tier in place. Exercise both Unity and Blender keyboard presets.
3. Scene render settings and undo history survive the interaction. The lesson identifies the searchable route. Record before/after interaction evidence, workflow coverage and remaining limits.
4. A preexisting shortcut and favorite for a raw quality command still resolve and apply the same tier after the curated names are introduced. Keep names, shortcuts and palette search usable for existing layouts.

## Tests and checks to run
- Focused event reproduction, then hci and editor groups on Windows with and without libraries and on Linux. Check related baseline interaction tests for paint/undo, viewport navigation and import feedback.
- HCI label, consistency, console, KLM and checklist audits, command coverage and changelog validation. No geometry or timed change.

## Risks and edge cases
Search uses the name and category, so explanatory text alone cannot make presets discoverable. Use the existing preset application path and leave Custom, which preserves manually chosen settings, in Preferences.

## Documentation to update
Extend the existing lesson and write the round 635 fragment with README and USABILITY sections (ADR 0123).

## Outcome
Implemented against `68dafe59`. `palette_entries` in `src/editor/command_palette.cpp` appends the three named presets from `kViewportQuality` after user actions, so saved command labels such as `quality low` still resolve. Each preset delegates to the existing `quality` command in `src/editor/editor_commands.cpp`. The quality tiers lesson in `src/editor/lessons.cpp` gives the searchable names and the confirmation key. The entries let artists find the setting by name (ADR 0060 and ADR 0085).

The independent review identified a saved-favorite regression in the first implementation. A Linux replay of that version with the final regression fixture failed 6 of 16 checks. The favorite row and its chord label disappeared, clicking the missing row could not apply Low, and the raw command lost its original search rank. Reordering catalogue construction preserves the old entry. The corrected build passes all 16 checks, including the original shortcut and a newly assigned shortcut for Medium. See 635-runs/final/red-upgrade.log and linux-focused.log. Review also corrected the preset tooltips to describe the resolution setting. The GPU Scene view can draw at full resolution (ADR 0036, note on task 0100).

### Walkthrough and selection

The round used the four questions from [ADR 0085](../decisions/0085-hci-methods-and-foundations.md) and the keyboard reach requirement U-04. The [round 590 sweep](../hci/sweep-590.md) already measured control-level focus and visibility at 1024 by 640. It could pass even when the command a person would search for was absent. We therefore started with the editing task and exercised the command catalogue.

| Editing task | Decision and evidence |
| --- | --- |
| Lower the cost of drawing the Scene and Game views during an edit | Retained. In a scratch 1024 by 640 editor, the same palette matcher that found Preferences reported no results for “low viewport”, “medium quality” or “high quality”. The prior package SHA-256 was B7891386A22B1CF5FCB0AFB63B9E850C864D627C1598B93B621B5CA3D76730A3. See the before screenshot and console output in C:/blab/pass35-hub/635-runs/pre/. The base kCurated catalogue and match_score confirm why. A user who knew the Preferences path could already choose a tier. |
| Recognize the recommendation on a small machine | The first-run fixture in tests/test_editor.cpp, “laptop minimum: the first run”, emulates the recommendation and clicks Use High. It covers onboarding, while the new search route serves a user returning to an existing scene. |
| Orbit using the Scene Gizmo | The pointer fixture “dragging the Scene Gizmo orbits” in tests/test_input.cpp checks a drag to orbit and a click to switch projection. This is a functional navigation check, with no frame-time claim. |
| Paint, then undo a stroke | The weight paint fixture “a mouse drag in the Scene view is one stroke” in tests/test_modeling.cpp drags there and back and checks that Undo restores the original weights. The separate first-stroke Paint feedback finding belongs to pass 35 task 0621. The [sculpt and paint walkthrough](../hci/walkthrough-sculpt-paint.md) distinguishes finding a brush from recovering after an edit. |
| Check an import before accepting its effects | The scratch GLB fixture “import preview: the dialog shows the model” in tests/test_assets.cpp clicks shading choices and the outline checkbox, cancels, then reopens and imports. This samples the preview and confirmation feedback. Large-file progress and native file pickers are outside this check. |

The baseline observation used the existing packaged editor's palette query command. The independent acceptance fixture sends shortcut, text, Enter, Escape and Undo events under Unity and Blender. It checks all reflected scene render properties as well as the undo stack. These are CPU headless interaction replays. They do not measure the experience of a study participant, physical pen, laptop GPU, or low-memory machine.

### Independent verification

A separate implementer added three curated entries and the lesson text. A separate test engineer specified the event sequence before reading the diff. On the first Windows run the original scene already contained a sphere called Sphere, making a name-only Undo assertion ambiguous. The final fixture records the new object's ID, verifies cancellation keeps it and checks that Ctrl+Z removes that exact object while restoring the initial object count. All six applied presets closed the palette with the expected scales on Windows and Linux. The tests exercise Low at 67%, Medium at 85%, High at 100% and Escape before activation.

The targeted comparison fixtures passed on all three builds: gizmo orbit 2 checks (60 px drag changed yaw from −35° to −17°), weight-paint drag/Undo 7 (maximum weight 0.400, back to zero after Undo), import preview 26 (the preview rendered, Cancel discarded the import, then one outlined mesh was imported), emulated minimum first run 5, existing quality tiers 9 and lesson actions 8. Test outputs and build logs are under C:/blab/pass35-hub/635-runs/. These comparisons supported retaining the palette fix. They sample selected successful routes and cannot rule out other failures in those workflows.

Static audits: 141 checklist items across 18 areas, 0 missing control names, 0 shortcut-label and consistency findings, 6 console referrals at baseline, 28 KLM rows consistent with the existing model. The coverage map found 398 entries with 0 empty, and the command sweep was current with 235 verbs. The 28 historical KLM rows do not measure the new palette route.

The independent pass 35 baseline survey also replayed keyboard scaling in Preferences (5 checks), forward and backward Tab wrapping (8) and scrolling to the final F9 row in a short Scene view (9). Its full sweep covered 2,168 screens over 12 size/scale/theme configurations and matched the stored control-finding baseline. These results support retaining the existing Preferences and short-panel navigation routes. Scratch evidence: C:/blab/pass35-hub/sweep638/{keyboard,focus,adjust,hci}/output.log and hci.json. See task 0620 for the pass-wide survey.

### Final local checks

| Release build | Round 635 assertions | HCI group | Editor group |
| --- | ---: | ---: | ---: |
| Windows with Blender libraries, MSVC 14.51 | 95 | 2,208 | 3,900 |
| Linux dependency-free, GCC 15.2 | 95 | 2,203 | 3,865 |
| Windows dependency-free, MSVC 14.51 | 95 | 2,205 | 3,889 |

Every row passed with zero failures. Windows used the project build slot and the local chain's no-LTCG release setting. Linux used the WSL lock and ten-core build affinity. All editor runs used scratch projects, trash, caches and preferences. The Windows libraries editor and HCI groups ran with GPU enumeration disabled for this CPU interaction round. Windows libraries results are in 635-runs/full-{build,summary,focused,hci,editor}.log. Final dependency-free results use 635-runs/final/{lite,linux}-*.log. The independent reviewer approved the correction against product blobs 0eab03ed and 9a6c7f44 and test blob 6adf50af. The same three blobs are present in the Hub's joined snapshot 6c9dcd1d. The Hub owns combined verification and publication.
