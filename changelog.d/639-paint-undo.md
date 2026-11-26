## 2026-11-26 (round 639): Refresh Textures cards after paint Undo and Redo

**Asked:** Show the original texture after undoing the first paint stroke on a surface.

**Fixed (task 0621, ADR 0081)**
- Textures cards follow the restored paint pixels after Undo and Redo. Each stroke remains a tile-based history step.
- The Paint layer's stroke stamp now reaches the thumbnail hash and the saved texture. The baseline already restored the saved albedo after Undo, while the card stayed painted.
- A new stroke on a Paint layer saved at the maximum stamp wraps to zero. Undo and Redo restore the stamp for each saved pixel state.
- Reopening Textures after a hidden Undo and replacement stroke refreshes the painted card and selected preview even when the replacement reuses the undone stamp.
- Undo and Redo refresh a full preview that still contains live dab pixels from before the tile restore.

**Checked:** Windows and Linux each passed 150 focused checks. Real Paint, Undo and Redo events restored the expected layers, saved pixels, card and selected preview. Tests cover a hidden replacement stroke, immediate Undo, the maximum saved stamp, and an unchanged neighboring texture.

<!-- README -->
after: model, face-corner paint that survives mesh editing, and texture baking.
Textures cards follow paint Undo and Redo (round 639).

<!-- docs/USABILITY.md -->
after: | The stack could generate wear and dirt but nothing could be painted where the artist wanted it
| Undo Paint left the painted pixels visible on the Textures card. (round 639) | The card shows the restored texture after Undo, and the painted texture after Redo. |
