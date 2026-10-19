# 0131: Fine-tuning on your own clips

- **Status:** done
- **Requirements:** R-13
- **Decisions:** ADR 0046
- **Model:** main session (Opus)
- **PR:** (with tasks 0128-0130, 0132)

## Goal
The network taught how this animator's character moves, from their own clips, without shipping a training framework.

## Scope
- In: the model's second output, its last features per frame.
- In: `ai::Fit`, `ai::Head`:
  - every window of 10 to 45 frames of the character's clips is run through the network;
  - a correction of its last layer is fitted from those features to what it missed: ridge regression, solved exactly
    by a Cholesky factorisation;
  - `ai::apply_head` adds it to the network's answer.
- In: the editor:
  - Fit to Clips (`ai finetune`): every clip of the project that animates the character, sampled here at the model's
    rate, then fitted on a worker with progress and Stop;
  - saved as `Assets/AI/<character>.aihead`, used for that character's spans;
  - `ai finetune off | on`: the shipped model alone, or with the fit.
- Out: training the whole network on the user's machine.

## Acceptance criteria
1. A fit lowers the error on the clips it was fitted to.
2. The shipped model is untouched (off: its own answers again).
3. Stop leaves the previous model in use.

## As built
- The plan's training API isn't there: ONNX Runtime's standard build has no on-device training (`GetTrainingApi`
  returns null in 1.31.0), and its training builds aren't published for all three platforms. Fitting the last layer
  exactly in Blendity's own code needs nothing more, and takes seconds. It can only re-weigh what the network already
  sees (a habit of the character's: a lower crouch, a wider stance), not learn a new kind of motion.
- Tests: round 151.
