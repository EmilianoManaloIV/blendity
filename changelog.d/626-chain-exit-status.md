## 2026-11-20 (round 626): Propagate local verification failures

**Fixed (task 0608, ADR 0134)**

- Local verification returns failure when a build, test group, full suite, WSL invocation, audit or selected optional stage fails. The chain names the failed stage and keeps its logs.
- Windows batch invocation handles checkout paths with spaces. The usage example supplies all three scratch checkout paths and the Windows library bundle.
- Each Windows build verifies its requested library mode. The WSL build runs on the ten cores specified in the machine handoff.

**Checked:** Synthetic Git Bash verification with fake Windows build, test and WSL commands.
