# 0656: Consolidate the Pass 38 input evidence

- **Status:** in review
- **Authorization:** Pass 38 Hub assignment, round 674 initially reserved on `b11687aa`. The final register uses `0cf2c48f`, after the Hub integrated 0652 and 0654.
- **Requirements:** Q-03, Q-04 and Q-06.
- **Decisions:** ADR 0056, 0123 and 0134.
- **Implementation:** The inventory owner maintains the generator and joins the two producer inputs. The existing independent tester and reviewer verify the staged result.
- **PR:** Local handoff. The Hub owns integration and publication.

## Goal

Let the owner distinguish newly verified controls from controls that gained a second input route, and reproduce the resulting counts from signed evidence.

## Scope

- In scope are the 57 signed texture and paint routes from 0651, the 87 lighting routes from 0652, an exact-key join and the source-location refresh for the publication base.
- Runtime changes, fresh product tests, benchmarking and publication stay with their owning lanes.

## Likely files

- `tools/dev/tool_inventory_0630.py` reads the two signed inputs and regenerates `docs/perf/0630-inventory.tsv`.
- `docs/perf/0651-texture-paint-controls.tsv` supplies the existing texture evidence. `docs/perf/0656-lighting-evidence.tsv` supplies the lighting evidence. `.gitattributes` preserves the signed lighting file's LF bytes and intentional final empty cells.
- This spec and `changelog.d/674-inventory-consolidation.md` record the reconciliation.

## Acceptance criteria

1. Match each producer's input digest and account for every exact key, including any shared keys and previously checked routes.
2. Preserve prior console observations, measured and dispatch evidence, route-specific qualifications and all evidence outside the two cohorts.
3. Independently reconcile before and after totals, verify the refreshed source metadata and reproduce the committed TSV from the parent register. Applying the join again must yield identical bytes.

## Tests and checks to run

Run the generator with the parent evidence on Windows and under WSL. Compare both results with the stored inventory, then run the idempotence check. Verify that passing a partial overlay or a full source-only export in place of the prior evidence fails before writing the output. Audit the changed keys against the independent producer receipts and verify every referenced artifact. Check the exact spec headings and metadata against the template, apply the changelog fragment check and run the staged whitespace check. The change only affects offline inventory generation and documentation.

## Risks and edge cases

- A pre-existing console result and a new pointer result can describe the same field. Preserve both observations and count the key once.
- A signed file can acquire CRLF on checkout or have an intentionally empty final gap column. Check the signed LF representation and retain every column.
- A failed timing expectation for a generated HDRI must stay outside the functional evidence join. Each observation carries its own limits.

## Documentation to update

Refresh the canonical TSV and the round outcome below. The round 674 fragment contributes the performance documentation entry when the Hub folds the pass.

## Outcome

The two signed inputs have 144 distinct keys and zero overlap. All 57 texture and paint keys and 66 lighting keys moved from unexercised to end-state. The other 21 lighting keys gained direct Inspector evidence alongside their earlier console observations. They count once each. The 144 keys cover 98 fields, 44 panel controls and two menu entries. The 0651 evidence paths in the canonical register resolve under `D:/Blendity-agent-scratch/pass38/0651/`. The 0652 input already uses absolute paths. For previously checked fields, the text before the 0652 Inspector label records the earlier observation, including the gap as understood at that time.

| Status | Before | After | Net change |
|---|---:|---:|---:|
| End-state | 1,515 | 1,638 | +123 |
| Measured | 92 | 92 | 0 |
| Dispatch | 756 | 756 | 0 |
| Unexercised | 3,544 | 3,421 | −123 |
| Failed | 0 | 0 | 0 |
| **Distinct keys** | **5,907** | **5,907** | **0** |

Checked coverage (measured or end-state) rose from 1,607 to 1,730. The source scan also refreshed 741 source-location rows on the final Pass 38 base, comprising 218 commands, 341 subcommands, 179 fields and three panel controls. No alias received transferred evidence.

The 0651 functional observations apply to the documented Windows Editing, Paint and Textures fixtures. The 0652 overlay covers visible control actions, with additional persisted state, Undo and render checks only where the corresponding row records them. The 600-part latency job selected a disk HDRI in all three intended generated-HDRI trials and exited 1. No timing or generated-HDRI measurement was joined. [Texture input](../perf/0651-texture-paint-controls.tsv) · [Lighting input](../perf/0656-lighting-evidence.tsv) · [Canonical register](../perf/0630-inventory.tsv)

### Reproduction

Run `python tools/dev/tool_inventory_0630.py --pass38 --check` in the checkout. To reproduce from the prior register in a Bash shell for the checkout, export the exact parent input and compare the generated output:

```bash
work=$(mktemp -d)
git show 0cf2c48fcff629fa7b14f65610184762f1b5cc37:docs/perf/0630-inventory.tsv > "$work/prior.tsv"
python3 tools/dev/tool_inventory_0630.py --pass38 --evidence "$work/prior.tsv" --output "$work/joined.tsv"
cmp "$work/joined.tsv" docs/perf/0630-inventory.tsv
```

The producer-signed LF input SHA-256 values are `32997ccc8961a84bfa14b0322783089ba8d0340df8300e293c5af8fd7c9de50c` (0651) and `7b7a39a917e22084730320b425c3a748818283051a742bf611b4442789813d24` (0652). The 0651 file can have CRLF in a Windows checkout. Its Git blob carries the signed LF bytes. [0651 producer verification](C:/blab/pass38-hub/0651-test.md) · [0652 producer verification](C:/blab/pass38-hub/0652-test.md) · [Final 0652 readback](C:/blab/pass38-hub/0652-receipt.md)
