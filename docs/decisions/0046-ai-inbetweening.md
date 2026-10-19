# 0046: AI inbetweening: a small network trained on CMU, ONNX Runtime as an optional download

- **Status:** accepted (tasks 0128-0131; the user chose AI inbetweening as an optional install "similar to how MoonRay
  is used", trained on CMU motion capture and on their own clips, and approved the downloads on 2026-10-18)
- **Date:** 2026-10-18
- **Requirements:** R-13

## Context
Cascadeur's AI Inbetweening fills the motion between two key poses with a network trained on motion capture, a style
per span. Blendity's assistants must be switches, write ordinary keys and change nothing on their own (ADR 0045), and
no dependency may enter the base install (ADR 0001); MoonRay showed how an optional download works (ADR 0041).

## Decision
- **The model:**
  - **Approach:** a transformer of 4.9M parameters (6 layers, 256 wide, 8 heads) that corrects plain
    interpolation (Oreshkin et al. 2022's delta-interpolator, Qin et al. 2022's inputs).
  - **Input:** 10 context frames, the gap filled by interpolation, the target frame, which frames are known, how far
    each is from the target, and a style. The 8 styles are walk, run, jump, fall, crawl, combat, acrobatic and
    other, labelled from the database's own trial descriptions.
  - **Frame format:** the root's place and 22 joints' turns in 6D, at 30 frames a second, in a space where the last
    context frame faces +Z at the origin.
  - **Training:** on 2,503 CMU clips (10.5 hours) for 100,000 steps on the RTX 4070 SUPER (78 minutes), with every
    tenth subject held out.
  - **Tools:** `tools/inbetween/` (`cmu.py`, `model.py`, `train.py`, `export.py`, `styles_eval.py`, `package.py`) are
    build-time only. The data and the Python environment stay outside the repository.
- **The runtime is ONNX Runtime 1.31.0** (Microsoft's, MIT). It is loaded with LoadLibrary or dlopen through its C API
  (headers in `extern/onnxruntime/`) only when the feature is used.
- **The download (task 0129):**
  - one archive per platform, about 25 MB: the library, the model and its description, built by `package.py` and
    checked by `.sha256`;
  - installed in `~/.blendity/ai/ai-v1` after a test inference;
  - offered the first time the switch is turned on, never at startup;
  - removable;
  - its members checked before anything is unpacked, then unpacked into a staging folder and moved into place.
    Removing deletes only the install's own files.
- **The rig is mapped onto the model's skeleton** through `gait::analyse`'s bones:
  - each bone's turn from its rest, in the world;
  - mirrored on x (Blendity is left-handed, the data right-handed) and turned to face +Z;
  - each bone's rest direction aligned to the data's (the data's legs are splayed and its arms out), the ends taking
    their parent's alignment;
  - the root scaled by the legs.
  - A 60 fps clip is resampled to the model's 30 frames a second, and the answer interpolated back onto every frame.
- **Only the chosen span changes:**
  - the keys at its ends keep their values, and their outer slopes are frozen, so the curves outside keep their shape;
  - a turn keyed whole turns away is spread over the span rather than spun on its last frame;
  - the result is shown as a ghost and written by Apply as one undo step.
- **Fine-tuning on the animator's clips (task 0131) fits the network's last layer, not the network:**
  - ONNX Runtime's standard build has no on-device training (`GetTrainingApi` returns null in 1.31.0);
  - the network's last features, its second output, are fitted to what it missed on the character's clips, by ridge
    regression solved exactly in Blendity's own code;
  - the fit is saved beside the project, switchable off; the shipped model is never changed.

## Measured (the spike, task 0128)
- **On the held-out subjects:** the joints' mean error, network against interpolation:

  | Gap | Interpolation | Network |
  |---|---|---|
  | 15 frames | 2.7 cm | 2.0 cm |
  | 30 frames | 5.7 cm | 4.0 cm |
  | 60 frames | 10.2 cm | 7.2 cm |
  | 120 frames | 16.8 cm | 13.1 cm |

- **By style** (30 frames): walk 62% better, jump 53%, acrobatic 40%, combat 35%, fall 20%, other 18%. No run or
  crawl clip of the held-out subjects is long enough to measure.
- **Foot sliding:** below interpolation's up to 30 frames (0.33 against 0.41 cm a frame), above it at 60 (0.43 against
  0.38) and 120 (0.85 against 0.28).
- **Speed:** 60 frames in 3 ms through ONNX Runtime on this machine's CPU, 7.4 ms on one thread, against 100 ms
  allowed.
- **In Blendity:** a Walk Cycle clip's middle 29 frames taken out and made again come 9.3 cm on average from the walk
  (the feet, hands and head), the keys' own interpolation 14.5 cm.
- **GO.**

## Consequences
- Long gaps (two seconds and more) slide the feet more than interpolation. AutoPhysics and Contacts (tasks 0116-0119)
  are the tools to clean a long made span; the Learn lesson says to keep spans short.
- Publishing the archives to the libraries release is the user's call; until then `ai install <archive>` installs a
  local one.
- The fit can only re-weigh what the network already sees (a character's habit), not teach it a new kind of motion.
