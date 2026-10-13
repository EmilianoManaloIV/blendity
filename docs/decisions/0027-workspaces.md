# 0027: Layouts for each stage of the work, each with its own set of tools

- **Status:** accepted (task 0055; the roadmap of 2026-10-12, approved by the user)
- **Date:** 2026-10-13
- **Requirements:** R-01, Q-04
- **Relates to:** the docking layouts (`dock_reset`), the Tools window, the keymap presets (task 0013)

## Context
The user wants layouts for the stages of a 3D job: Reference / Planning, Blocking Out, Modeling and Topology, UV
Unwrapping, Texturing and Materials, Lighting and Rendering, Rigging, Skinning and Weight Painting, Animation, and a
generic one. Their words: "I want to avoid that feeling of so many tools when you open up Blender for the first time
so you can focus on certain parts of the process." Today Blendity has five window arrangements (Default, 2 by 3,
Tall, Wide, Learning). Their names are listed in three places, and every tool shows all the time.

Blender's answer is Workspaces: a tab per stage, each a screen layout plus a mode. Unity's is a Layout dropdown at
the top right, holding the window arrangements and Save / Delete Layout.

## Decision
- **One table of layouts** (`kWorkspaces`): a name, a dock string and a **focus**. The focus is a set of tool groups:
  - Create;
  - Draw;
  - Object;
  - Edit Mode;
  - Checks;
  - UV;
  - Snapping;
  - Lighting and cameras;
  - Materials and textures;
  - Deformers (curves, lattices);
  - Rigging;
  - Reference.

  The top-right Layout dropdown, Window > Layouts and the `layout` command all read it. Unity's place and names are
  kept, so it is a dropdown, not Blender's tabs.
- **The focus hides, never removes.** It filters the Modeling Tools window's sections, the GameObject menu's groups
  and the Add Component categories. **Show All Tools** (Window > Layouts, the Tools window's header, `alltools
  on|off`) turns it off. The keyboard and the console always reach everything, so a hidden tool is still one key or
  word away.
- **The arrangements stay** (2 by 3, Tall, Wide, Learning), with every tool showing. The Default layout is the
  generic one.
- **Your own layouts.** Save Layout... stores the current arrangement and focus under a name, in preferences (per
  user, as Unity does). Delete Layout... removes one.
- **A Reference Image object** for Reference & Planning (Blender's Add > Image > Reference):
  - an unlit textured quad sized to the image's aspect, facing the chosen view;
  - it casts no shadows and isn't in renders;
  - GameObject > Reference Image..., or `refimage <path> [front|side|top]`.

## Consequences
- A layout's focus lives in preferences, so a later "editor feel" switch (Unity / Unreal / Blender / Maya) can be a
  bundle of keymap, layouts and focus.
- Tests and scripts that click tools by section first pick a layout that shows them, or Show All Tools. The Default
  layout shows everything, so existing behaviour is unchanged.
