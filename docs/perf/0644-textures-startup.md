# First opening Textures in a mixed project

Existing measurements cover a texture-only library with up to 2,000 assets, including first open, scroll, steady interaction and reopen. Round 662 addresses a different project shape. A project can have many meshes, images and metadata files for each procedural texture that its Textures window can display. [Texture-only library measurements](../tasks/0612-texture-preview.md)

At base d7503d4e the Windows directory enumerator provides a wide filename and file attributes. For each ordinary entry the browser constructed a filesystem path. For each ordinary file it also converted the extension to UTF-8 and lowercased it before deciding whether the file could appear in the window. The candidate checks ordinary filenames for an ASCII case-insensitive `.ptex` suffix first. It still descends into ordinary directories and resolves reparse points using the original rules. [Windows texture enumeration](../../src/editor/texture_window.cpp)

## Verification and measurement

Two stock Windows release editors used the same product objects except for the Textures window object. The 1600 × 900 CPU viewport ran in a fresh process for every sample. The texture-only fixture included 144 valid textures and 24 malformed ones in 24 nested directories, with 24 backup filenames. The mixed fixture added 6,912 unrelated files. File-system caches were uncontrolled. The driver timed the first workspace frame and warm reopen separately, and checked visible panel pixels and discovered paths. [Fixture and preregistered thresholds](C:/blab/pass37-hub/0644-timing-plan.md)

Before the large comparison, the empty, texture-only and mixed fixtures produced matching reference panels in both editors, including the preview after pointer selection in each populated fixture. A single mixed first frame at two workers took 60.7 ms in the original and 60.6 ms in the candidate. [Original and candidate smoke samples](D:/Blendity-agent-scratch/pass37/0635/0644/baseline1/report.json)

The independent edit test compared visible names and decoded panel pixels for mixed-case extensions, Unicode filenames, nested directories and excluded suffixes. After the first frame, an edit addressed by its mixed-case texture path changed the width from 512 to 128 and wrote only that file. The saved bytes matched and the edit persisted in a fresh process. A separate pointer click after reopening selected a matching preview. [Independent edit and reopen comparison](D:/Blendity-agent-scratch/pass37/0635/tests0644/windows/parity/report.json)

## Results

Each row below uses five alternating original/candidate pairs and three original/original pairs. The timing target was the mixed project's first Textures frame. For each worker setting, the median paired saving had to exceed 1 ms, 5% of the baseline median and the largest absolute original/original difference, with at least four faster pairs. [Measurement plan](C:/blab/pass37-hub/0644-timing-plan.md)

| First frame | Worker limit | Original median (ms) | Candidate median (ms) | Paired saving (ms) | Faster pairs | Required saving (ms) |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| Mixed project | Default | 24.2 | 23.9 | 0.3 | 3/5 | 1.21 |
| Mixed project | 2 | 61.3 | 59.6 | 1.7 | 5/5 | 3.065 |
| Texture-only control | Default | 15.9 | 16.2 | −0.3 | 1/5 | 1.30 |
| Texture-only control | 2 | 55.3 | 55.0 | 0.6 | 4/5 | 2.765 |

The maximum original/original difference on the mixed first frame was 0.7 ms at default concurrency and 1.8 ms at two workers. Mixed-project warm reopen had a median paired change of 0.2 ms slower at default concurrency and 0.0 ms at two workers. All 64 fresh processes produced the expected discovered paths and panel captures for their fixtures. [Original/original samples](D:/Blendity-agent-scratch/pass37/0635/0644/aa1/report.json) · [Original/candidate samples](D:/Blendity-agent-scratch/pass37/0635/0644/paired1/report.json)

The mixed first-frame saving fell below the predefined threshold at both worker settings. Round 662 retains the findings and leaves the shipping enumeration path as it was. The deferred editor and assets suites applied only to a retained implementation. [Independent verification](C:/blab/pass37-hub/0644-test.md) · [Timing window](C:/blab/pass37-hub/timing-queue/0644-serial-timing1.result.json)
