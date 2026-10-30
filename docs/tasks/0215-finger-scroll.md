# 0215: panels follow the finger

- **Status:** done (round 235)
- **Requirements:** R-14, R-01
- **Decisions:** ADR 0062 (task 0213)
- **Model:** main session
- **PR:** #181

## Asked
The plan for a 2-in-1: in Tablet Mode a finger scrolls the Hierarchy, Inspector, Project and the rest, and targets are
sized for a finger.

## What changed
- **One place for every list:** `ui::Context::begin_scroll`, which every scrolled area uses (Hierarchy, Inspector,
  Project, Console, Learn, the Material, Texture and tool windows, Preferences).
  - In Tablet Mode, a finger dragging up or down by 10 px - more up or down than across - scrolls the area under it.
  - The offset follows the finger from where it pressed. The widget it pressed loses the press: nothing clicks.
  - A flick glides on and slows to a stop; the next touch or wheel stops it.
- **Sideways is still editing.** A finger moves a slider, or drags a number, once it has gone 6 px more across than up
  or down; from then on the scroll can't take that press. A tap still sets a slider where it lands.
- **Rows and files:**
  - in Tablet Mode a finger taps rows and files to select them;
  - dragging them - reparenting in the Hierarchy, an asset onto an object, a material from its window - is the pen's
    and the mouse's, as the plan has the pen manipulate;
  - a scroll selects nothing, and keeps a multi-selection.
- **The scrollbar** under a finger is dragged as the mouse drags it.
- **Targets:** the transform gizmo's handles, the Scene Gizmo and Edit Mode's pick radius are all sized through the UI
  scale, which Tablet Mode already makes 1.5 times as big (task 0188). No change was needed; the Tablet Mode test pins
  the scale.

## Checked
- tests/test_input.cpp `tests_round235`:
  - a finger dragging the Hierarchy 200 px scrolls it 200 px and selects nothing; sideways it doesn't scroll; outside
    Tablet Mode it doesn't either;
  - a tap still selects a row; a flick glides and the next touch stops it;
  - a scroll from a selected row keeps all 86 selected; the scrollbar drags the right way;
  - Tablet Mode turned off mid-scroll leaves nothing stuck.
- Not tried on a touch screen; task 0222's check list covers it.
- An Opus review found five things, all fixed:
  - a scroll that began on a selected Hierarchy row collapsed a multi-selection;
  - a scroll that began on a slider or a number first changed its value; a finger now moves them only once it has gone
    sideways, and once it has, the scroll can't take the press;
  - a finger on the scrollbar dragged it backwards; it drags the thumb as the mouse does;
  - Tablet Mode turned off mid-scroll could leave the scroll stuck, so mouse clicks on rows stopped selecting;
  - a glide whose list went undrawn jumped on when it came back; it ends instead.

  Each has a test.
