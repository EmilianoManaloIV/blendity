# ADR 0070: a window kind once per window, with a view state of its own

- **Status:** accepted (task 0232, round 252); amends ADR 0059
- **Relates to:** ADR 0059 (secondary windows), ADR 0027 (workspaces), ADR 0068 (the Wheel)

## Context
ADR 0059 gave Blendity secondary windows and ruled "each window type at most once overall", and left "a drag from one
window into another window's dock area is not a dock move" as a consequence. The user works on a drawing tablet beside
the main screen and wants the Scene view and the Inspector there while the main window keeps its own, and tabs that
land where they are dropped. The Scene view's state - camera, place, navigation, shading, overlays, caches, the
targets it draws into - was one set of `Editor` members, used in some six hundred places.

## Decision
- **A kind at most once per window.** A window's dock holds a kind once; another window may hold it too. (Window,
  kind) names an instance; `dock_find`, `dock_move`, `dock_remove` inside one window stay as they were. The kinds that
  duplicate are those with state of their own to carry: the Scene view, the Game view and the Inspector. The rest
  (Hierarchy, Project, Console...) show one thing, and still live in one window at a time.
- **The view state travels with the window**, the way ADR 0059's UI context, framebuffer and dock do: a
  `EditorViewState` per secondary window (the Scene view's camera, rect, hover, navigation drag, shading and overlay
  flags, pilot camera, GPU slot; the Game view's rect; the Inspector's pin; the render targets, renderers and filter
  targets the views draw into) is swapped into the editor's members with the window's UI in `swap_window_state`, and
  the private caches (the view caches, the overlay record) with it. No use site changes: every tool, shortcut and the
  Wheel act on the instance under the pointer because that window's state is in place while it draws. A window's
  state is made from the main window's when the window first draws (a duplicate copies the window it came from).
- **The card's slots are keyed by the view and its window.** The GPU view slots were keyed by a render target's
  address; the targets are now swapped by value, so the key gains the window the frame draws.
- **A drop lands where it is dropped.** A tab released over another editor window joins that window's dock at the
  zone under the pointer (`WindowMove::ToWindow`, applied between frames with that window's dock in place), and the
  main window is a target like any other. Overlapping OS windows aren't asked their order: a point inside the main
  framebuffer is the main window's.
- **Saved with the window.** Each `W(...)` in the `windows=` preference carries `;V(...)` with the Scene view's
  camera and shading and the Inspector's pin, read back when the window is restored; a line without it is an older
  build's and gets the main window's state.

## Consequences
- The main window's view members are what they were; between frames the main window's state is in place, so a
  console command's Scene view is the main window's.
- `scene_moving()` reads every window's state, so a camera gliding in a secondary window keeps frames coming.
- A layout reset in the main window no longer removes a Scene view, Game view or Inspector another window holds.
- Several instances of one kind inside one window would need instance ids in the dock strings and the widget ids;
  task 0235's module registry is the place to decide that.
