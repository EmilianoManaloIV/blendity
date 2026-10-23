# ADR 0060: Interaction principles - Unity's structure, Blender 2.8's reach, scaffolding for newcomers

- **Status:** accepted (2026-10-22, task 0189, round 205)
- **Deciders:** the user (asked for an HCI-grounded pass: keep the Unity feel, cut the friction), the main session
  (proposed)

## Context
Blendity's structure is Unity's (Hierarchy, Inspector, Scene and Game views, components, the Add Component search,
Unity's keys by default) and its depth is Blender's (Edit Mode, modifiers, the path tracer, rigging). Depth costs reach:
a command lives in a menu, a window's button or the console, and layouts hide another stage's tools (task 0055). The
research the user pointed to: a 2025 study of scaffolded interfaces on Blender (arXiv 2505.12101 - task-relevant tools,
progressive disclosure, tools by domain concept: lower task load for beginners, gains for experts too); Blender 2.80's
UI work (workspaces as task layouts, Quick Favorites, one tool system, a status bar with the current tool's mouse and
key hints); classic results - Fitts's law (big, near, edge targets), Hick's law (fewer choices per level; search over
browse), Nielsen's heuristics (visibility of system status, recognition over recall, user control and undo,
consistency), Shneiderman's direct manipulation; the command palette pattern (VS Code, Unity's search).

## Decision
- **Unity stays the frame:** the four-panel layout, component editing, the gizmo conventions and the keymap presets
  don't change. Additions reach commands; they don't move them.
- **Reach by name:** a command palette (Ctrl+K, F3) finds every keymap action, window, layout, creatable object and a
  catalogue of console-backed commands by typed words, shows each one's shortcut (recognition over recall, and the
  shortcut learned on the way), lists recent ones first, and reaches tools a layout hides. Its backend is the console:
  nothing new to wire for a command, and nothing the palette does that the console can't.
- **Reach by habit:** Quick Favorites (Shift+Q; right-click a palette row or a menu row to add) per layout.
- **Status visible:** the status bar says what the mouse and keys do where the mouse is (the Scene view's tool and
  mode, the feel's navigation) and offers Undo for the last action by name.
- **Targets that are easy to hit:** splitters' grab zones 8 pixels at any scale; the Add Component search lists what
  was added last above the categories.
- **Measure first:** each round's changes are chosen from an audit of common tasks' paths (clicks, key chords, typed
  words, dead ends), recorded in its task doc with the before / after, honest where a target isn't met.

## Consequences
- The palette's catalogue grows with the console: a new console command worth finding gets one line.
- The keymap gains two actions (edit.palette, edit.favorites) in every preset; Shift+Q because Q is a tool key in them.
- Task 0190 takes the next changes by the same measure (progressive disclosure in the Inspector, scaffolded first
  runs, keyboard-only Hierarchy / Inspector, undo labels...).

## Alternatives considered
- A searchable menu bar only (macOS Help search): rejected - the console's commands and windows' buttons aren't menus.
- Moving Blendity toward Blender's layout: rejected by the user's framing (keep the Unity feel).
