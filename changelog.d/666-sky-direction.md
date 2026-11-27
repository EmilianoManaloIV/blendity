## 2026-11-27 (round 666): Adjust the physical Sky world's sun direction

**Asked:** Place the sky's sun without adding a Directional Light.

**Added:** World settings for a Sky expose Sun Elevation and Sun Rotation in degrees. The current scene stores the angles. The sky and its direct light share the direction. An enabled Directional Light still controls both. Learn includes a low-sun example.

**Checks:** Lighting, editor and Learn checks cover light precedence, changing angles with the direct sun switched off, console edits and scene persistence.

<!-- README -->
after: Find and apply Low, Medium or High Viewport Quality from the command palette.
The physical Sky world's Sun Elevation and Sun Rotation position its sun when there is no enabled Directional Light.
