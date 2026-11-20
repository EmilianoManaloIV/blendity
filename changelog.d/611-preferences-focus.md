## 2026-11-19 (round 611): Keep the active Preferences field visible

When Preferences status rows appear or disappear, the scroll area brings the field being edited back into view. Text and number fields retain their current input, caret and selection. A wheel scroll still lets the user look elsewhere while editing.

Three regression cases passed 22 assertions on Windows with and without Blender's libraries and on dependency-free Linux. The HCI and editor groups passed on each build.

<!-- README -->
after: **Ctrl+P** plays, and **Ctrl+K** finds any command by name.
4. In Preferences, press **Tab** or **Shift+Tab** to move between controls. The field you are editing stays in view when settings change the layout.

<!-- docs/USABILITY.md -->
after: ## Interface conventions

Preferences keeps the field being edited visible through layout changes. Tab and Shift+Tab continue moving between controls. You can use the wheel to inspect another part of the panel without losing the edit.
