# 0125: Walk Cycle, the procedural gait

- **Status:** done
- **Requirements:** R-13
- **Decisions:** ADR 0045
- **Model:** main session (Opus)
- **PR:** (with task 0124)

## Goal
Walking and running cycles made from the rig, as 3ds Max's CATMotion makes them: a gait from per-part numbers, feet
planted without sliding, presets blended by weight, at any size of character.

## Scope
- In: `gait::Style`:
  - **The numbers:** speed, stride, feet-down share, foot lift, heel-toe roll, bounce, sway, hip twist and drop, the
    spine's counter-turn, arm swing, elbows, lean, crouch, and each leg's phase.
  - **Presets:** walk, jog, run, sneak, limp; walk, trot, gallop for four legs.
  - **`blend`, and `.gait` text files.**
- In: `gait::cycle_of`:
  - stride from the Froude number (a cycle is 2.3 L Fr^0.3 long, Alexander 1984), with speeds given for a 0.9 m leg;
  - `pose_at(rig, style, t, travel)`: the hips, the planted or swinging ankles, the feet's heel-toe turn, the spine's
    and head's counter-turn, the arm swing; the pelvis kept within every foot's reach.
- In: `gait::generate`:
  - the cycle keyed on the rig's own controls (hips, foot IK targets and poles, feet, spine, head, arms; or the legs'
    bones by a two-bone solve without IK), every frame;
  - the planted frames as contacts;
  - a treadmill loop (a whole number of frames a cycle, the stride adjusted) or travelling forward.
- In:
  - the Rigging window's **Walk Cycle** section: a preset, its numbers, Cycles, In Place, Generate;
  - the Animation window's **Walk Cycle** button;
  - `walk cycle [preset] [speed] [cycles] [inplace|travel] | set | save | load | presets`.
  - All only with the Walk Cycle assistant on.
- Out: turning while walking (task 0126's paths), starts and stops (task 0126), uneven ground (tasks 0126, 0127).

## Acceptance criteria
1. A planted foot moves under 1 mm; a treadmill cycle loops; the keys are on the IK targets.
2. Stride and cadence scale with the leg (Froude); a run has both feet up, a walk never; a trot moves diagonal pairs
   together.
3. Presets, blending and .gait files work; the quadruped walks; AutoPhysics changes a walk by a few centimetres at
   most.
4. In the editor: refused while off; on, keyed as ordinary keys that edit by hand and undo in one step.

## As built
- Tests: round 145.
  - A planted ankle moves 0.001 mm; the treadmill's seam is 0.0002 mm.
  - A 1.76 m leg's cycle is exactly twice a 0.88 m leg's, and sqrt(2) as long in time.
  - A run is airborne 30% of the time.
- Found on the way:
  - **The loop seam:** rounding the period to whole frames by changing the speed also changed the stride (the Froude
    number), so the period never landed on a frame. The stride is adjusted instead.
  - **Float time at a cycle's boundary** left a foot "mid-swing" for a frame. The pelvis then wasn't lowered for it, and
    IK fell 5 cm short. The phase is snapped at whole cycles, and every foot, planted or swinging, is kept within reach.
- AutoPhysics moves a generated walk up to 4.1 cm, spread over it: the pelvis keeps one forward speed where a real walk
  slows over each planted foot.
- After review:
  - the arms keyed through their IK controls;
  - the heel-toe roll pivots on the heel, then the toes; the toes no longer go 18 mm through the floor at push-off;
  - `walk set` keeps the other numbers;
  - forward read from the feet (a rig facing -Z);
  - legs without IK bend their knees the rest pose's way;
  - Generate works on a copy, keeps the clip's length and the hands' contacts;
  - the preset list shows "custom";
  - the body strip and Body Mass hidden with the physics tools off.
- "No sliding" is measured where the foot touches the ground: as the foot rolls, the ankle rises on purpose while the
  heel or the toes stay put (0.000 mm).
