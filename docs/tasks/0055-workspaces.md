# 0055: Layouts for each stage of 3D work

- **Status:** done (as built below)
- **Requirements:** R-01
- **Decisions:** ADR 0027
- **Model:** main session (Opus)
- **PR:** (link when opened)

## Goal
The user asked (2026-10-12) for layouts that focus on the stages of 3D work, "Reference/Planning, Blocking Out,
Modeling and Topology, UV Unwrapping, Texturing and materials, and then lighting and Rendering; of course the generic
editor too ... separate layouts for rigging, skinning and weight painting, animation", to "avoid that feeling of so
many tools".

## Scope
- In:
  - **The Layout dropdown** (top right, as in Unity) and Window > Layouts list the layouts in this order:
    1. Default (generic).
    2. Reference & Planning.
    3. Blocking Out.
    4. Modeling & Topology.
    5. UV Unwrapping.
    6. Texturing & Materials.
    7. Lighting & Rendering.
    8. Rigging.
    9. Skinning & Weight Painting.
    10. Animation.
    11. The arrangements: 2 by 3, Tall, Wide, Learning.
    12. The user's own layouts.
    13. Save Layout... and Delete Layout....
  - **Each layout** arranges the windows for its stage and shows that stage's tool groups (ADR 0027). The rest stay
    reachable by key and console. Show All Tools turns the hiding off.
  - **Reference Image** object (GameObject > Reference Image..., `refimage <path> [front|side|top]`).
  - Console: `layout <name>`, `layout save <name>`, `layout delete <name>`, `alltools on|off`.
  - A Learn lesson: "Layouts for each stage".
- Out: the Animation window itself (task 0059 adds it to the Animation layout).

## Acceptance criteria
1. Each stage layout opens its windows. Examples: UV Unwrapping has the UV Editor beside the Scene view; Texturing
   has the Materials and Textures windows; Rigging has the Rigging window.
2. Modeling & Topology shows the Tools window's Edit Mode section. Texturing & Materials hides it.
3. Show All Tools shows it again.
4. Hidden menu items are still reachable by console.
5. Save Layout stores the arrangement and focus. It survives a restart (preferences) and is listed; Delete Layout
   removes it.
6. A Reference Image is unlit, sized to its image's aspect, faces the view asked for, casts no shadow and isn't in
   renders.

## As built
- **One table** (workspaces.cpp) holds every layout's windows and focus. `dock_reset` reads it, so a lesson's button,
  `--layout` and the dropdown give the same windows.
- **An arrangement of your own** (dragged tabs, a dock string) shows as "Custom" in the dropdown, so any layout can be
  picked from there.
- **The Modeling Tools window's first row** says how many sections other stages hide and turns Show All Tools on. Once
  it's on, the row turns it off again.
- **A Reference Image from outside the project** is copied into Assets/Textures/References, so the scene keeps it on
  another machine.
- **Names:** a user layout's name loses `;` and `=` (the preferences line's separators), and a built-in layout's name is
  refused.
- **The Animation layout** has the Scene, Game and Rigging windows; task 0059 adds the Animation window to it.

## Tests and checks to run
- Unit tests (round 77); full suite, verify mode, Linux, sanitizer.
