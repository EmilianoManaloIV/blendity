# 0285: Puppet Posing, written up and measured

- **Status:** done (round 305)
- **Requirements:** R-08
- **Decisions:** ADR 0087
- **Model:** main session
- **PR:** #230

## Asked
The user (2026-10-29, /plan): "How to implement Animatica Marionette". Rounds 299-304 built it as Puppet Posing; this
round documents and measures it.

## What Marionette offers, and where it is in Blendity

| Marionette (as described by Digital Production, 2026-10-21, and its launch page) | Blendity |
|---|---|
| Drag a hand, a foot, the hips or the head; a learned model poses the rest | Puppet Posing's handles (0281) on the pose prior (0280) under exact IK |
| Feet hold the floor; ~15 ms a pose on the CPU, offline | AutoPose's pins; 0.3 ms a pose, offline (the optional AI download) |
| Mixamo, UE5, Character Creator, Daz, VRoid, Biped, Rigify; T or A pose | One humanoid map (0279): all seven schemes by name, T or A measured |
| Plain FK keys on your own rig, nothing added | Keys on the bones, or on the rig's FK controls when it has them (0280) |
| Motion Trail: drag the path, Alt-drag to retime | Motion Trails (0282): drag a key dot to pose, Alt-drag to move the key |
| Zoetrope: neighbouring frames side by side, each poseable | The Zoetrope strip (0283) |
| Smooth Brush, Lock in Place, Motion Check | All three (0284), the check's findings each with a Fix |
| Reach: an edit spreading into nearby frames | Reach (0281) |
| Onion skinning | Onion Skin (0282) |

## Measured
- The prior on held-out CMU subjects: 2.0 cm mean joint error with every goal, 2.9 cm with the hands and feet, 10.3 cm
  with one hand (0280's table); 0.13 ms a pose in ONNX Runtime, 0.28 ms through Blendity.
- The keystroke-level model (`docs/hci/klm.md`, `tools/hci_klm.py`): posing a reach 20.2 s bone by bone, 3.75 s by a
  handle; finding and fixing a foot slide 30.4 s by hand, 6.3 s with Motion Check. Model estimates for an expert, not
  measurements of people; the table's 21 rows now 131.5 s before, 64.7 s after.

## Not done
- No study with animators (as for the HCI pass: the protocol is written, it hasn't been run).
- The prior ships once the maintainer publishes the AI archives with it (`package.py --prior`).
