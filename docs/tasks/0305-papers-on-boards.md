# Task 0305 - Papers on the reference board (round 325) - done

The user asked for this on 2026-10-29: "...incorporate the two so I can put all my references in one place along with my
paper sketches." Their choices: a paper is a board item (its drawing shown, double-click to draw on it), a linked file
any board can show.

## What changes
- `BoardItem::Paper`: a paper's file on the board, 1000 units high at its page's aspect. Its picture is the paper's
  sketch drawn off screen through the sketch's own layer drawing - the open paper from memory (at most four times a
  second), another from its file (again after each save).
- In: Boards > New Paper Here, Boards > Add Paper, a `.paper` dragged from the Project window, More > Paper > Pin to the
  Board. Double-click a paper item: the Draw tab draws on it (source Paper), its history kept.
- Renaming a paper relinks every board (files, and this board's undo steps). A missing paper shows "Missing paper:".
- Boards: Duplicate Board, Delete Board (to the trash). `board rename|copy|trash|addpaper|newpaper|openpaper`.

## Checks (tests_round325)
- New Paper Here by clicks adds a paper item at the page's aspect; double-click opens it on the Draw tab; a stroke there
  darkens the board's picture of it; opening it again and New Paper Here keep its undo steps.
- The same paper on two boards; a rename keeps both linked; Duplicate Board and Delete Board.
