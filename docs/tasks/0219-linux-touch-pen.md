# 0219: Linux - fingers and the pen through XInput 2

- **Status:** done (round 239)
- **Requirements:** R-14, C-01, C-02
- **Decisions:** ADR 0062 (task 0213)
- **Model:** main session
- **PR:** #185

## Asked
The plan for a 2-in-1: the same fingers and pen on Linux.

## What changed
- **XInput 2, without a new dependency:** libXi is opened when the program starts, as Vulkan is. Its header is used
  where the system has it (`libxi-dev`). A build without the header, or a system without libXi, keeps the mouse's
  events as before.
- **Fingers:** touch events from the master devices go through task 0213's recognizer: one finger is the left button
  tagged Touch, two pinch, pan and twist. X then makes no pointer events of them for the window.
- **Pens:**
  - the tablets are the slave pointers with a pressure axis (not touch screens), found by their axes' labels;
  - their own events give pressure, tilt (degrees, as Xorg's drivers give them) and the eraser (a device of its own,
    found by its name);
  - the core events that follow, where the pen is, become the pen's; its side buttons (2 and 3) become the right
    button with the barrel held, as on Windows;
  - a touch while the pen moves is a palm;
  - the device list is read again when X says it changed.
- **CI** installs `libxi-dev`, so the XInput 2 code is compiled there. No machine here has the header: this round's own
  builds compiled the code without it.

## Checked
- tests/test_input.cpp `tests_round239`: the axes by label (and a touch screen's "MT" axes not), pressure as 0..1,
  tilt in degrees for Xorg's ranges and stretched for others, the eraser by name, and a valuator read from an event's
  mask.
- **Not tried:** a Linux touch screen or tablet, or a build with the XInput 2 header. CI compiles it for the first time;
  task 0222's check list is Windows'.
- An Opus review found these, all fixed:
  - a pen whose device appears after the start (libinput adds one at a pen's first use, and a tablet can be plugged in
    later) was never seen; the devices are looked at again whenever X says they changed;
  - tilt was stretched: Xorg's drivers give degrees already, now kept as they are;
  - a touch screen reporting pressure could be taken for a pen and block every finger; touch screens and "MT" axes
    are left out;
  - while a pen hovered, the mouse's events were taken for the pen's; now only events where the pen is are;
  - `-ldl` for older glibc, and an X error while selecting a device that just went is ignored rather than ending the
    program.
