# 0565: Keep stress sweep commands stable when tests change

- **Status:** implementation complete, independently reviewed and verified locally
- **Pass / round:** 29 / 583
- **Requirements:** Q-03 (repeatable checks), Q-06 (responsiveness inventory)
- **Decisions:** ADR 0105, ADR 0123, ADR 0134
- **Model:** Codex
- **PR:** pending

## Goal

Adding a unit test with a shorter command invocation should leave the operations sweep unchanged. A developer adding or removing a console verb should see its name in a failing local check before publishing the change. OPEN-WORK item 9 records three Linux CI failures from the current test-dependent generator.

## Scope and ownership

- Keep the existing console registry in `src/editor/editor_commands.cpp` authoritative for verb membership. Keep the existing Python generator's write and read-only `--check` entry points and the C++ table consumed by the operations stress section.
- Own `tools/sweep_commands.py`, its Python self-test, `stress/sweep_commands.inc`, the sweep check in `tools/dev/chain.sh`, this spec and the round fragment. The registry and stress consumer are read-only references. Other local audits and the Windows workflow belong outside this round.
- Replace the test scan with explicit runnable invocations and skip reasons in the generator, seeded from all 235 release rows, including bare-verb invocations. Keep the release row order, command arguments, skip reasons and output format. A new verb requires an explicit runnable invocation or a reason to skip it.

## Acceptance criteria

1. Regeneration on v1.0.0 produces the same table bytes. Adding a test with a shorter invocation does not change any bytes. Repeated generation is deterministic.
2. A registered verb without an inventory entry and an inventory entry for a removed verb each fail validation in write and check modes. Diagnostics name each offending verb. A failed write leaves the existing table intact.
3. Changing a maintained example requires regeneration. Before regeneration, `--check` names the affected verb and returns nonzero without writing. After regeneration, the check passes.
4. The local chain runs the self-test and the check, exposes their diagnostics and returns a failing status on error. The current `py ... --check | tail -1` loses the generator's exit code and can hide named failures. Keep failure handling local to the sweep step.
5. The scratch-project operations stress run completes and reports the release inventory's same runnable and skipped verbs.

## Tests and checks to run

- Use Python standard-library tests with temporary inputs and outputs. Pin release-table equivalence during migration. Exercise shorter test examples, missing and removed registry verbs, a changed maintained example, failed-write preservation, and successful regeneration followed by a read-only check. Verify a failing check through the actual shell invocation used in the chain.
- Run the generator self-test and `--check` with Windows `py` and WSL `python3`. Run the local chain for the editor group on Windows and Linux. CI runs the full unit suite.
- Run `blendity_stress --quick --only operations --out <scratch-report-dir>` with a scratch project and trash. Reserve a coordinated timing window for the measured run. Record verb and skip counts in the fragment.

## Risks and documentation

Preserve each skip reason, including for commands that write files, launch external programs or depend on hardware. Keep relative paths in runnable examples directed to the scratch project during validation. The plan changes no command semantics or timing thresholds.

Write `changelog.d/583-stable-sweep.md` with validation results and a `<!-- tools/README.md -->` section describing how to maintain an example or skip reason when changing the registry.

## Verification

- All three Python regression tests passed with Windows Python and WSL Python. The release include retained Git blob `3e1269d1f5030f922329e6ac5ea8f12ae751e2a5`. Running the chain's sweep commands against a scratch registry with an extra verb printed its name and exited 1.
- The editor group passed 3,872 checks on Windows with Blender's libraries and 3,846 each on dependency-free Windows and Linux. The dependency-free binaries came from the green v1.0.0 CI build (run 44422753853). The Windows libraries build compiled locally with MSVC 14.51. The generated C++ table and its consumers match the release in all three cases.
- The quick operations sweep finished in 28.9 seconds in an exclusive window with no competing builds or tests. Its 704 operations included exactly the release's 203 command invocations on each of two scenes and 32 named skips. One row exceeded 50 ms and none exceeded 100 ms.
- All 398 command, action, window and reach entries have coverage. The separate widget-label audit still reports 173 of 210 widgets without label-driven coverage on unchanged release sources. The HCI audits, Unity metadata check, book citations and performance-tool self-tests passed.
