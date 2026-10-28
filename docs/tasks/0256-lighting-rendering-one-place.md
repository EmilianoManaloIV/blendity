# 0256: lighting and rendering - one Environment, one Device section, the rendering components always at hand

- **Status:** in progress (round 276)
- **Requirements:** R-14
- **Decisions:** ADR 0085 (the area rounds)
- **Model:** main session
- **PR:** #222

## Asked
The HCI pass (ADR 0085), the lighting and rendering round: the walkthrough found the Environment edited in two windows
(Lighting and Render) with two headers for one thing, four device pickers in three places with "Auto" meaning the same
thing in each but defined nowhere, the Rendering components (Light, Camera, Camera Filters, Probe Volume, Outline)
missing from Add Component unless the layout's stage was lighting or materials, and bakes and renders with no time to
finish (round 273's tray gave them that). Nielsen's H4 (consistency), H6 (recognition over recall), H10 (help).

## What changed
- **One Environment**: the Render window's "World / Environment Lighting" section is a row - "Environment: edited in the
  Lighting window" with an **Open Lighting** button - instead of a second copy of the fields; the Lighting window's
  Environment section is the one place.
- **One Device section**: the Render window's Render Devices section opens with "Auto: the GPU when there is one, else
  the CPU" said once, then the Viewport Device (Preferences' field, the same setting) and the GI Device (the Lighting
  settings' field) beside the render device and the per-card switches; the render Device and the Denoise Device stay in
  the Render Settings fields above (named there), the mirrored fields keep their own homes (Preferences, the Lighting
  window). The GI Device drives Realtime GI, realtime lightmaps and live probes; Generate Lighting uses the render Device
  - the tooltip says so.
- **The Rendering components always at hand**: Add Component (the Inspector's and the Component menu's) lists Light,
  Camera, Camera Filters, Probe Volume and Outline in every layout, not only when the stage is lighting or materials
  (the Rigging components keep their stage rule: the rigging round decides).

## Tests (`tests_round276`)
- In the Default layout (no Lighting window) the Render window has the Open Lighting button and no Environment field
  (no "Sky Color"); a real click on the button shows the Lighting window.
- The Render Devices section has the Viewport Device and GI Device rows (the Viewport one is the same member Preferences
  edits; driving the combo by a click is not in the test).
- Add Component in the Default layout (a modeling stage) lists Camera Filters.

## Not done
- Open Lighting docks the window in the largest leaf; in a layout where that leaf holds the Render window itself the
  click hides the window it came from (a dock rule, not this round's).
- Clear Flags gains no "Depth Only" (three renderers would have to agree on it: a round of its own).
- The help texts (Preview Lighting described once, the Light component's help, the Toon help's placement, the one-option
  Lighting Mode) are left for the compositing and materials round, which rewrites that Inspector section.
- "Bake" still names four things (Generate Lighting, Bake Textures, Bake Mesh Maps, the baked-light layer): each label
  says what it bakes, the verb stays.
