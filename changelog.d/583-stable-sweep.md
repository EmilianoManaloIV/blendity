## 2026-11-18 (round 583): Stable console examples in the operations sweep

**Asked:** Keep an unrelated unit test that uses a shorter command from changing the measured inventory (open work 9).

**Changed (task 0565, ADR 0105)**
- The generator now maintains an explicit invocation or skip reason for every registered verb. All 203 runnable commands and 32 skips match the 1.0.0 table. Adding or removing a verb reports its name until the inventory matches. A changed example shows the affected row in the check output.
- The local chain runs the generator's self-test, prints the check output and stops on failure.

**Checked:** The generator's three regression tests and read-only check pass on Windows and Linux. The generated include matches the 1.0.0 blob. A missing verb in a scratch registry makes the chain's sweep step name the verb and exit 1.

The editor group passed 3,872 checks on Windows with Blender's libraries and 3,846 each on dependency-free Windows and Linux. In an exclusive quick stress window, all 203 runnable commands matched the release on each of two scenes, with the same 32 skips. Of 704 operations, one took over 50 ms and none took over 100 ms.

<!-- tools/README.md -->
replace: one real invocation per console verb taken from the unit tests, the verbs not run headless listed with the reason
with: add an invocation to `EXAMPLES` or a headless skip reason to `SKIP` for each registered verb, then run the generator
