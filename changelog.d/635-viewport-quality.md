## 2026-11-26 (round 635): Find viewport quality in the command palette

**Asked** — Assess common editor workflows and make it easier to lower viewport quality while editing.

**Changed (task 0617)** — Search the command palette for Low, Medium or High Viewport Quality to apply the existing editor presets. Existing user shortcuts and Quick Favorites for raw quality commands still use their saved names. The quality tiers lesson shows the search route.

**Checked** — At 1024 by 640, Unity and Blender keyboard searches applied all three tiers and preserved render settings and undo history. The saved-shortcut and Quick Favorite fixture failed six checks on the earlier implementation and passed all 16 after correction. Windows with libraries, Windows dependency-free and Linux dependency-free each passed all 95 focused checks. Their HCI/editor groups passed 2,208/3,900, 2,205/3,889 and 2,203/3,865 checks respectively. The five HCI audits passed. Geometry stress was outside this UI change.

<!-- README -->
after: The Lighting window shows the active update mode first
  Find and apply Low, Medium or High Viewport Quality from the command palette.

<!-- docs/USABILITY.md -->
after: | A command lived in a menu, a window's button or the console,
| Searching for a viewport quality tier did not offer a preset (round 635). | Search for Low, Medium or High Viewport Quality to apply that tier directly. |
