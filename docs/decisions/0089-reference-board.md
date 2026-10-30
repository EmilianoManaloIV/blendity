# ADR 0089: The reference board - a project asset of its own, not scene data

- **Status:** accepted (2026-10-29, task 0299, round 322)
- **Deciders:** the user and the main session. The user asked for a mood/reference board tab and chose PureRef's canvas;
  the main session proposed the format.

## Context
A reference board collects pictures for a drawing or a model: often many boards per project, reused across scenes, and
edited while the scene is not.

## Decision
- **A board is an asset:** `Assets/Boards/<name>.board`, JSON written with the editor's own `JsonWriter` and read with
  `JsonParser`. It is not part of the `.scene`, so boards don't bloat scenes or ride their undo.
- **Images are kept with the project:** an image from outside Assets is copied into `Assets/Textures/References` (as a
  Reference Image is). The board names asset-relative paths.
- **Its own undo:** snapshots of the board's items, at most 100, with Ctrl+Z routed to it while the pointer is over the
  board (the Editing window's pattern, task 0181).
- **It saves itself** a second after the last change, so nothing is lost if Blendity closes. There is no Save button to
  forget.
- **A clipboard image API in the platform layer** (`get_clipboard_image`) returns a file's bytes, so the image codecs
  decode it.
- **A window's own keys come first while the pointer is over it.** The Draw tab's pad and the board each have a keymap
  context that is looked at before the others. The clash check treats such a context as clashing only with itself, so
  binding Ctrl+D to the board doesn't clash with the global Duplicate. Over the board, though, Ctrl+D is the board's.
  While a stroke or a drag is under way, its keys are held and do nothing, so they never reach the scene behind.
- **Pictures load on a thread of their own** (`std::async`) into a board cache. Each is at most 2048 px on its long
  side, with mips, and HDR and EXR are tone-mapped. The renderer's texture cache isn't used: a board of hundreds of
  photos would thrash it.

## Consequences
- A board opened on another machine finds its images, since they live in the project.
- X11's INCR transfers (very large images) aren't read. Pasting a file path, or dropping the file, still works.
