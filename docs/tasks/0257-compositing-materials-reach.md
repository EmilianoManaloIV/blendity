# 0257: compositing and materials - a pen's reach, a visible Assign, an Export Textures dialog

- **Status:** in progress (round 277)
- **Requirements:** R-14
- **Decisions:** ADR 0085 (the area rounds)
- **Model:** main session
- **PR:** #222

## Asked
The HCI pass (ADR 0085), the compositing and materials round: the Render window's image panned with the middle button
only (a pen has none); the Tone Curve's points went only by right-click; "Assign to Selection" lived in a right-click
menu with no button; the texture set's export was a console command with no window to find it in. Nielsen's H6
(recognition over recall), H7 (flexibility), the pen.

## What changed
- **The Render window's image pans with a left drag** that begins on the image (a press from the settings column crossing
  in pans nothing) as well as the middle button; the wheel still zooms and a double-click fits.
- **Tone Curve points go by Alt+click** as well as right-click (a pen's barrel button is often the right button; Alt is
  free in the Editing window); an Alt+click that removes nothing adds nothing; the tooltip says both.
- **Assign to Selection is a button** in the Materials window's toolbar, beside Edit UVs, disabled with its reason when
  no material or no object is selected; the right-click item stays.
- **Export Textures...** is a button in the Textures window's toolbar opening a dialog - the packing (glTF, Unity, HDRP,
  Unreal, separate maps - the maps named under the choice), the folder under the project, "Assign the exported images to the
  material" - that runs the console's
  `export textures` with those arguments; the console form stays for scripts.

## Tests (`tests_round277`)
- A left drag over the Render window's image moves its pan; a press from the settings column crossing the image does not.
- The Materials window's toolbar has the Assign to Selection button, disabled with its reason when nothing is selected.
- The Textures window's Export Textures... button opens the dialog with its Export and Cancel buttons recorded.

## Not done
- The Export dialog's Export button and the Assign button's click are not driven by a test (the console form and the
  context menu's call they share are).
- One exposure model: the Camera Filters' exposure and the Editing window's view exposure both apply `exp2(stops)`
  already; the labels do not yet say "stops" in both places, and the Render window's exposure field is a third - a
  naming pass with the help texts (round 0260 or a round of its own).
- The Editing window's canvas keeps its middle-button pan (a left drag paints a mask there).
- A tap-and-hold menu for the Tone Curve's points; the Alt+click path is not driven by a test (an image must be open).
- Rendering components' help texts (Light, Toon's placement, the one-option Lighting Mode) as in round 276's note.
