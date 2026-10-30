# Task 0299 - The reference board: the window, its file, getting images in (round 322) - done

The user asked for this on 2026-10-29, during round 319: "Have an additional tab for making mood/reference boards.
Utilize good HCI and other art applications for reference and usability."

**The user's choices:**
- a PureRef-style canvas;
- an eyedropper to the pen;
- side by side with Draw, and a reference floating over the pad;
- sending an image to the scene.

The eyedropper, floating and sending are task 0301. Tasks 0299-0302 land as one PR, stacked on #234.

## What changes
- **A window kind, Board** (`WindowKind::Board`, its dock id 24).
  - The Drawing layout has it as a tab beside the Draw tab. A new layout, **Drawing with References**, shows the two
    side by side. Splitting the Drawing layout's pad made its toolbar fold away.
  - Reference & Planning has it as a tab.
  - `window Board` opens it.
- **The board** (`scene/board.h`, ADR 0089) is a list of items.
  - Each item has its place, scale, turn, flips, crop, gray, opacity and lock, in board units (an image's own pixels at
    100%).
  - It is saved as JSON in `Assets/Boards/<name>.board`, by itself a second after the last change. The toolbar shows the
    name and whether it is saved.
  - The Boards menu makes a new board or opens another.
- **Images in:**
  - **Dropped from the system** onto the board: they land in a row at the drop point, one undo step.
  - **Dragged from the Project window:** the board is outlined while one is over it.
  - **Add Image...:** a file dialog.
  - **Paste** (Ctrl+V over the board) reads the clipboard's image through a new `platform::get_clipboard_image`:
    - Windows: its PNG format, else the DIB made a BMP;
    - X11: image/png;
    - macOS: PNG, else TIFF made a PNG.

    The image is saved in `Assets/Textures/References/Pasted NNN.png`. A file path copied as text is added as that
    file.
  - **An image from outside the project** is copied into `Assets/Textures/References`, as a Reference Image is.
- **Drawing:** each image is sampled from its mip level for the size it is shown at, bilinear, through its turn, flips
  and crop, with gray and opacity. A missing file keeps its place, marked "Missing or unreadable".
- **The view:**
  - the wheel zooms about the pointer;
  - a middle-drag pans;
  - two fingers pinch and pan;
  - Home (or the zoom button) fits everything;
  - F frames the selection.
- **The selection:**
  - **Picking:** a click picks the image under the pointer, and Shift adds or takes one away. A drag on nothing draws a
    marquee that picks what it meets.
  - **The handles:** dragging an image moves the selection. A corner handle scales it about the opposite corner, and the
    handle above the top turns it (Ctrl: 15 degree steps).
  - **The arrows** nudge it (Shift: 10 px).
- **Keys over the board** (a Board keymap context, first while the pointer is over it):
  - Delete or Backspace;
  - Ctrl+D duplicate;
  - Ctrl+A select all;
  - Ctrl+V paste;
  - Home fit, F frame;
  - Ctrl+] / Ctrl+[ bring to front / send to back.

  Ctrl+Z and Ctrl+Y over the board undo and redo the board's own steps (each drag is one), as the Editing window's do.
- **Console:** `board new [name]|open <name>|save|list|add <path> [x y]|paste|select <i>|all|none|move <dx> <dy>|scale <f>|turn <deg>|delete|duplicate|front|back|fit|frame|zoom <f>|undo|redo`.

## The review (Opus code-reviewer: changes needed) and its fixes
- **Saving on close:** it saves every frame a second after a change (shown or not) and when Blendity closes.
- **The board used last** opens again.
- **Same-name files:** another file of the same name gets "name 2". The Reference Image shares this copy.
- **Pictures load on a thread** into the board's own capped cache, so there are no stalls and no texture-cache thrash.
  HDR and EXR are tone-mapped. An unreadable file is taken off the board, with its copy.
- **Mid-drag:** the board's keys, undo and console are held. A press that doesn't move changes nothing.
- **Picking:** an item of no size catches no click, and the marquee tests turned outlines.
- **Platforms:**
  - macOS's paste has an autorelease pool;
  - X11 ignores a late image answer;
  - Windows falls back to the DIB, then to a file copied in Explorer.
- **The docs and a Learn question** were written.
- **Tests:** they clean up References first, plus tests for each fix.

## The second review (changes needed) and its fixes
- **Sizes:** filled from the cache every frame and after undo, redo, a drag and opening, for pictures that landed
  while a press or an undo step held the items.
- **A file copied in Explorer** is added by its path (only an image). The TGA decoder is bounded, and no longer reads a
  palette past its data.
- **The cache:** trimmed to what the board, its undo and its drag name, on a new or opened board.
- **Loading:** three loads at a time, with a loader's failures caught.
- **Pasting:** pasted images are kept as they came, with no decode on the main thread.
- **Reused names:** a reused name is read afresh.
- **Copying:** same-named files are compared by size first.
- **Rows:** they keep a moved image.
- **Unreadable files** are taken out of the undo steps too.
- **The puck** holds the brush keys.

## Moved
- **Shift for free-aspect scaling:** the handles are on the selection's bounds, so a free scale of turned images has no
  clear meaning. Scaling keeps the aspect.
- **Space+drag panning:** Space is the Wheel's key over the board, so the middle button pans.
- **Sharing the pad's 2D view maths (View2D):** not done. The board has no turn and the pad's view stays as it was
  tested.

## Checks (tests/test_board.cpp, `--group board`)
- **The window:** it is in the Drawing layout to the right of the Draw tab. An item's frame maps both ways, and a board
  file reads back.
- **Dropping:**
  - two images dropped land in a row, and one from outside the project is copied into References;
  - the pixel under the first is its color;
  - the file is written within 1.4 s and opens again as it was.
- **Pasting:** pasted PNG bytes are kept in References and placed on the board.
- **The handles, by drags:**
  - a move;
  - a corner scale to 2.0 that keeps the top left within 2 px;
  - a turn with Ctrl to 45 degrees;
  - Ctrl+Z three times back to the start, then Ctrl+Y;
  - a marquee picking one, and Shift+click adding the other.
- **Keys:** Ctrl+D, Delete, Ctrl+A, Ctrl+] / Ctrl+[, Home, F and the eight keymap actions, plus a Project window drag.
- **Console:** every `board` verb.
