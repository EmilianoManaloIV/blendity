# 0623: Keep Linux CI package setup independent of third-party repositories

- **Status:** in review
- **Authorization:** owner requested repair of PR #504's failing CI, assigned by Pass 35 Hub, round 641.
- **Requirements:** Q-03
- **Decisions:** none needed
- **Implementation:** assigned CI repair worker
- **PR:** [#504](https://github.com/EmilianoManaloIV/blendity/pull/504).

## Goal
Run the Linux checks when their dependencies are present or available from the Ubuntu archive, including when an unrelated repository rejects access.

## Scope
- Change the five package setup steps in the main build workflow and their shared helper.
- Preserve package lists, builds, test coverage, other workflows and system configuration.

## Likely files
- `.github/workflows/build.yml` and a Bash helper under `tools/ci/`.

## Acceptance criteria
1. All five callers pass the same package names they passed before.
2. Fully installed packages require no network or privileged command.
3. Missing packages trigger a signed Ubuntu-only update and install. An unavailable Ubuntu archive, failed install or incomplete installation fails the job.
4. A third-party repository in either supported runner source layout cannot make a required build or test fail.

## Tests and checks to run
- Exercise the helper with simulated package state, distro and third-party sources, and update/install failures. Compare callers against the base workflow. Check Bash syntax, workflow structure and changelog formatting.
- The hosted run of PR #504 exercises real runner images after hub publication.

## Risks and edge cases
A removed or partially installed package can still have a dpkg database record. Verification must inspect installed state. Each command that resolves packages must use the same archive restriction.

## Documentation to update
- `changelog.d/641-linux-ci-packages.md`.

## Outcome
The [failed sanitizer shard](https://github.com/EmilianoManaloIV/blendity/actions/runs/45900917772/job/140198103342) exited 100 during package setup after the Microsoft Ubuntu 26.04 feed returned HTTP 403. In the same run, [Linux unit](https://github.com/EmilianoManaloIV/blendity/actions/runs/45900917772/job/140190266718), [Linux stress](https://github.com/EmilianoManaloIV/blendity/actions/runs/45900917772/job/140190266685) and [sanitizer shard 5](https://github.com/EmilianoManaloIV/blendity/actions/runs/45900917772/job/140198103634) passed. The five jobs now check installed state before any privileged command and install missing packages using the Ubuntu archive keyring. A local Ubuntu 26.04 APT print-URI run generated 45 index targets exclusively on archive.ubuntu.com and security.ubuntu.com. Independent verification exercised 14 offline scenarios, covering present and held packages, missing and incomplete installations, modern and legacy source layouts, and update, install and final verification failures. Reversing the five command substitutions reproduced the base workflow exactly. Bash syntax, workflow parsing and changelog checks passed. Hosted verification follows publication.
