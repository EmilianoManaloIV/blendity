# Task 0306 - Export a board as a picture or a PDF (round 326) - done

The user asked for this on 2026-10-29: "Make it so I can export the board as pdf or photo format."

## What changes
- Boards > Export...: a panel of lists - What (whole board, selection, view), Format (PNG, JPEG, PDF), Size (1024-8192
  px on the long side), Background (the board's, white, transparent for PNG) - then Export... and where to save (a
  typed extension decides the format).
- Drawn through the window's own item drawing; images larger in the picture than the board's 2048 px copies read again
  at full size, papers drawn at their size in the picture; pictures loaded and the file encoded and written on threads
  (the board says "exporting...").
- The PDF: one page, A4 on its long side, the picture as a JPEG (`encode_pdf_image`, `encode_jpeg`). No new dependency.
- `board export <path> [png|jpg|pdf] [size <px>] [selection|view] [bg board|white|none]`.

## Checks (tests_round326)
- Two images as a PNG of the asked size with each colour in its place; the selection alone on transparency; a JPEG
  that decodes; a PDF whose xref points at its five objects and whose picture decodes; the panel stays open while its
  lists are used.
- 2 px stripes on a 3000 px image exported at 3240 px stay red and blue (full size, not the copy's blur), the editor
  stepping meanwhile.
