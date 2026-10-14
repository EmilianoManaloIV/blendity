# 0073: Themes - Dark, Light, Emiliano Light and Emiliano Dark

- **Status:** done
- **Requirements:** R-01
- **Decisions:** none
- **Model:** main session (Opus)
- **PR:** https://github.com/EmilianoManaloIV/blendity/pull/66

## Goal
The user (2026-10-14): an editor palette and theme option using the user's Figma site ("look at the variables tab in
figma").

## Scope
- In: `ui::Theme` gains the colours windows wrote by hand (list stripes, strips, row hover, key diamonds, a drawing's
  background); `ui::theme_named` makes Dark (Blendity's), Light, Emiliano Light and Emiliano Dark; Preferences > Theme
  and `theme <name>` switch it, and the prefs keep it.
- The Emiliano themes use the Figma file's Variables ("Branding Guide", Light and Dark modes): background F1EEE5 /
  292927, text 292927 / F1EEE5, accent D7882B and A65E1E, signal mint 45E8A2, magenta ED24C7, blue 4C76FF, red E6533C,
  amber FF9A00, yellow FFE44D, supporting stone D8D3C9, slate 6B6B63, silver 9A9992, charcoal 1A1A1A.
- Out: the Scene view's own drawing colours (grid, gizmos, selection outline), which stay readable over any scene.

## Acceptance criteria
1. `theme Emiliano Light` gives windows F1EEE5, text 292927, focus D7882B; Emiliano Dark the swapped pair and mint.
2. An unknown name is refused; the theme survives a restart (prefs).

## As built
- Mapping: background for windows and panels; stone and slate for borders and fields; accent for buttons and
  selection; accent dark pressed; focus amber in Light and mint in Dark; mint ok; amber warnings; red errors and X; blue Z; charcoal the frame.
- Tests: round 99.
