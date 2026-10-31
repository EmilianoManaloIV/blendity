# Task 0327: an HCI comparison with Blender, Unity, Godot and Unreal through three productions

- **Status:** done (round 350, 2026-10-31) - analysis only; the fixes wait for the user's choice
- **Asked:** "Can we do another HCI analysis of the whole program, all tabs, several use cases. Comparing it to ease of
  use with Blender, Unity3D, Godot, and Unreal?" - with a VRChat world, a VRChat avatar and a short film's pipeline as
  the use cases.

## Done
- `docs/hci/compare-2026-10.md`: the method, the productions and their measured outputs, each stage against the four
  programs (from their docs, cited), the five-program KLM table, the verdict per audience, 15 ranked findings.
- `tools/hci_klm.py --compare`: recounts the five-program table's cells and prints each program's total.
- Round 349 fixed the one bug the productions found in this round's own code.

## Next (for the user to choose)
The ranked list's top six: shape keys in glTF / FBX export; painted textures referenced by the .glb; a performance
budget panel with VRChat PC / Quest presets; a viseme preset; export waiting for (or warning about) a running remesh;
video without installing FFmpeg.
