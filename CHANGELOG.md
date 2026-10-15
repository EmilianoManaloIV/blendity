# Changelog

One entry per round of requests, newest first: what was asked, what changed, and how it was checked. Earlier rounds are summarised from their commits.

## 2026-10-15 (round 113): the Edit Mode drag loop stops redoing whole-mesh work (task 0087)

**Asked:** the performance pass (plan item 0087).

**Changed**
- **Dragging vertices of a big mesh:** the check for flat rings of faces that a drag folds (they are zipped again)
  built a map of every edge of the mesh on every frame; it now looks edges up in per-vertex face lists kept while the
  mesh's faces don't change. The drag's own work on a 50k-face mesh went from 25 ms a frame to 1.6.
- **G** reuses its copy of the mesh from frame to frame instead of copying the whole mesh each time.
- **During any drag** (a mouse button held, G / R / S, Push/Pull, a modal adjustment) the overlapping-vertices check
  and the "Lighting out of date" status wait for it to end; the Modeling Tools panel shows what it found before.
- With task 0086: a Move-gizmo drag on 50k faces went from 98 ms a frame to 43 on the laptop setting, G from 100 to 42.

**Checked**
- New tests (round 113): a 10-frame gizmo drag on a big mesh copies it at most once, builds no render mesh, checks no
  overlaps and hashes no lighting key, and releasing checks once; G reuses its copy and Esc restores the mesh exactly;
  the per-vertex face lists give exactly what the old edge map gave, odd faces included, and are kept while only
  positions move.
- Unit checks: Windows 10,088, 0 failed (698 tests); the whole suite in render-cache verify mode: 10,089,
  0 failed, 0 stale pictures. Linux with libraries: 9,996, 0 failed. Sanitizer build: 9,902, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). Fixed:
- Holding the lighting answer during a drag froze Live GI and the status bar until release: during a drag a change is
  taken as out of date without the exact (costly) check, which runs when the drag ends.
- The overlapping-vertices overlay vanished whenever a mouse button was held (orbiting, a click): it keeps showing a
  result that is still current, and hides only while a drag changes the mesh.
- G's copy is remembered as a weak reference rather than an address; edge lookups scan the shorter vertex list.

## 2026-10-15 (round 112): Edit Mode - the render mesh follows a drag in place (task 0086, ADR 0034)

**Asked:** the performance pass (plan item 0086).

**Changed**
- **Dragging vertices in Edit Mode no longer rebuilds the mesh's render data every frame.** When only positions
  moved, the shaded mesh follows them in place - the same triangles and the same picture as a rebuild (quads and
  n-gons are split again from the new positions) - and the edge table and smoothing adjacency are kept. A Move-gizmo
  drag on a 50k-face mesh went from 98 to 69 ms a frame on the laptop setting, G from 100 to 67 ms.
- Normal-mapped meshes' tangents follow a drag the quick way and are made with MikkTSpace again when the drag ends.
- Deforming modifiers move their render mesh with the exact triangulation now (they kept the build's before).

**Checked**
- New tests (round 112): 48 random drag steps on six kinds of mesh draw bit for bit what a rebuild draws; a quad's
  diagonal flipping during a drag; a new face builds again while moved vertices keep the edge table; quick tangents
  within 10 degrees of MikkTSpace's, exact after the drag ends; a fold that changes where smoothing splits builds
  again; NaN positions draw as a rebuild's; F9 still adjusts a positions-only operator on a normal-mapped mesh.
- Unit checks: Windows 10,078, 0 failed (695 tests); the whole suite in render-cache verify mode: 10,079,
  0 failed, 0 stale pictures. Linux with libraries: 9,986, 0 failed. Sanitizer build: 9,892, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). Fixed:
- Settling the tangents after a drag bumped the mesh's version, which ended an F9 adjustment and re-ran modifier
  stacks: it only drops the caches now, and a separate counter the views key on makes them redraw.
- Meshes changed outside Edit Mode (an origin moved) kept their quick tangents until their topology changed, so a
  bake could depend on how a mesh got its shape: every mesh with quick tangents is settled when an interaction ends.
- Deforms worked out the triangle slots every frame: once per topology now. `memory_bytes()` (and so the undo budget)
  counts the kept topology; the test's drags are the same on every compiler.

## 2026-10-15 (round 111): per-view waste - the voxel GI grid's columns, the shadow map's ids plane (task 0085)

**Asked:** the performance pass (plan item 0085).

**Changed**
- **Realtime GI:** a view checking whether its voxel grid still fits the scene no longer allocates a fresh column
  pyramid (about 87,000 columns at 256) every frame to find out; only a grid it actually takes gets its columns.
- **Shadow maps** keep no picking-ids plane: nothing is cleared or written for it. The sun's map on a moved-object
  frame went from 0.68 to 0.48 ms on the laptop setting.
- **The GI blur** no longer copies its input plane before writing every texel anyway.
- **Measured and left alone:** gathering the views' draw items (0.07 ms a moved frame) and the scene hashes (0.03 ms)
  are too small to be worth changing in these scenes; shading is 5.1 of the Scene view's 7.2 ms (tasks 0088-0089).
  The Realtime GI drag in Automatic mode (71 ms) is the automatic lighting update, not the grid.

**Checked**
- New tests (round 111): small moves inside the GI grid make no new pyramid, leaving it makes one; a grid fitted
  without columns has the same placement and key; a shadow map has no ids plane and the same depth.
- Unit checks: Windows 10,058, 0 failed (689 tests); the whole suite in render-cache verify mode: 10,059,
  0 failed, 0 stale pictures. Linux with libraries: 9,966, 0 failed. Sanitizer build: 9,872, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). No bugs; hardened: the Gouraud raster path and the selection outline also skip a
target without ids (a shadow map), so a future pass drawing into one can't index an empty plane; a shadow target
gives its ids memory back.

## 2026-10-15 (round 110): the frame-rate cap holds, and only what changed is presented (task 0084)

**Asked:** the performance pass (task 0083 measured; this is its first fix).

**Fixed**
- **A 60 fps cap ran at about 33 fps on Windows**, whatever the frame cost: every wait was rounded up to Windows'
  15.6 ms timer tick, so a frame that left less than a tick to wait lost a whole one more. The editor now waits on a
  high-resolution timer, and a frame pacer blocks only for as long as the waits have proved on time and polls for
  the rest: a real window's 13 ms frames under a 60 fps cap went from 30.6 ms apart to 16.6 ms (95 % within 16.7).
- **Presenting** sends only the bands of rows that changed: 0.68 ms instead of 2.42 for a Scene-view-sized change
  (95 % within 0.70 ms instead of 9.38), and 0.07 ms when nothing changed.
- **A window repaint drew the same frame twice** (the paint, then a queued event); now once.

**Checked**
- New tests (round 110): the pacer under a pretend 15.6 ms timer (the old loop 31.2 ms apart, the pacer 16.6); with
  a precise timer it blocks for nearly the whole wait; input waking it early doesn't start the frame early; only
  changed bands are presented, everything on a new size or when asked; one 30 ms-late wake-up doesn't leave the
  pacer polling for good; a change only in pixels' top bits is still seen.
- Unit checks: Windows 10,037, 0 failed (686 tests); the whole suite in render-cache verify mode: 10,038,
  0 failed, 0 stale pictures. Linux with libraries: 9,945, 0 failed. Sanitizer build: 9,851, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). Fixed:
- On Linux (and Windows' fallback) a window uncovered or first shown asked for a repaint as an event, which presented
  nothing when the frame hadn't changed: a repaint, resize or DPI change now presents all of it.
- One very late wake-up could leave the pacer polling for good (it only learnt from waits it blocked in): an unused
  margin now shrinks each frame.
- The band hash could miss a change confined to pixels' top bits: each chain rotates and is finalised.
- macOS presented the whole picture once per changed run: once per frame now. `winmm` is linked for CMake builds too.
- Waits were truncated to whole milliseconds: `poll_events_for` takes seconds; Windows' waits wake for input already
  queued and can't hang on a timer that never fires.

## 2026-10-15 (round 109): performance pass 10 begins - measuring Edit Mode drags, the mesh caches and frame pacing (task 0083)

**Asked:** "Let's increase overall editor performance in general." Chosen: the viewport's frame rate and big meshes
in Edit Mode, keeping the CPU renderer; plan of tasks 0083-0091. This first task only measures.

**Added**
- **The Profiler** shows the time between frames (what the screen sees, the cap's waiting included) and what the
  mesh caches did in the last frame: copies for an edit, render meshes built or moved in place, tangents, edge
  tables; plus an Edit Mode drag's writes, the overlap check, the lighting key, snapping, the views' draw items and
  the scene hashes. The Learn tab's performance lesson explains them ("Reading a frame").
- **blendity_stress:** `editor_render` drags the 50k-face mesh for real (the Move gizmo, and G) and notes each part;
  `--only present` opens a window to time presenting frames and the cap's pacing.

**Found** (the laptop setting, `--laptop --quality low`; docs/PERFORMANCE.md, Pass 10)
- A 60 fps cap with 13 ms frames gave **30.6 ms between frames** (33 fps): Windows rounds the loop's waits to its
  15.6 ms timer. Presenting a frame costs 2.4 ms on average (9.3 ms at the 95th percentile).
- A gizmo drag on 50k faces costs 98 ms a frame: two render meshes built from scratch (38.6 ms, the Scene view's flat
  one and the Game view's smooth one), the drag's writes 24.5 ms (ring repair scans every face), the shadow map
  7.5 ms, the Edit Mode overlay 8.7 ms. G is the same plus a 2.7 ms mesh copy a frame.

**Checked**
- New tests (round 109): a gizmo drag moves the counters; the Profiler draws its new rows during a drag.
- Unit checks: Windows 10,022, 0 failed (680 tests); the whole suite in render-cache verify mode: 10,021,
  0 failed, 0 stale pictures. Linux with libraries: 9,928, 0 failed. Sanitizer build: 9,834, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). No behaviour changed; fixed:
- The pacing figure was measured with present on top of 13 ms of work: present now counts inside the 13 ms (the
  result held: 30.6 ms between frames).
- "Moved in place" counted refused moves too; the first frame after start showed everything counted before it.
- The 95th percentile was rounded down (the Profiler and stress); the Profiler shows it from two frames on.
- The stress run's undo after the gizmo drag could undo an earlier step if the drag had missed.
- The tests now look for the Profiler's rows by name, and no longer pin today's rebuilds; ARCHITECTURE has a row.

## 2026-10-14 (round 108): installing without knowing GitHub - one-click downloads, releases of installers only (task 0082)

**Asked:** "Remove build-pr71" (done: the release and its tag). "Note the installer in the releases and update the
readMe in how to install. A non-technical person should be able to install it." "Have the release not contain the
source files if the user doesn't want them. Only have an executable or similar on whatever platform it's downloaded
to." Chosen: one-click links (v0.x releases are GitHub's Latest, still called pre-releases), extras moved aside.
Then: "Make them artifacts" - a merged PR's build is no longer published as a release.

**Changed**
- **README, Download:** "Download for Windows / Mac / Linux" links that fetch the newest installer directly, then
  step-by-step installs (Windows' "Windows protected your PC" > Run anyway; the Mac's Open Anyway; Linux's Allow
  executing), uninstalling and first steps.
- **Each release's notes** say which file to download and how to install it. An update inside Blendity still shows
  only what changed.
- **A release lists only the installers** (Blendity-Setup-Windows.exe, Blendity-macos.dmg, Blendity-linux-x86_64.AppImage)
  and SHA256SUMS.txt. The libraries archives and the Linux .tar.gz moved to one "libraries" release that Blendity's
  updater reads by itself. GitHub's own "Source code" files can't be removed from a release (GitHub adds them to
  every one); the notes say they're for developers.
- **0.10.0 and later 0.x versions** are GitHub's Latest release (so the links work), titled "(pre-release)"; Version
  History calls them pre-releases. PR builds stay GitHub pre-releases.
- **Merges into main make no release any more**, only build artifacts: a release comes from a `v*` tag, made when
  the user asks for one ("Make them artifacts. I will let you know explicitly when I want a release.").
- **Mac:** the app is signed ad hoc (when the signature verifies), so macOS offers Open Anyway instead of calling the
  download damaged.

**Review** (Opus code-reviewer). Fixed:
- Builds installed before this change look for "Blendity-Setup-...": the Windows installer is
  Blendity-Setup-Windows.exe, so they are still offered updates.
- A 0.x beta would have become the Latest release (and the README's download): betas are checked first now, and
  GitHub picks the Latest.
- Tagged releases had lost their "what changed" notes: GitHub's summary of the merged PRs comes before the install
  guide again.
- The "libraries" release would have grown without end: it keeps tagged releases' archives and the newest 20 PR
  builds'. The publishing script stops if an installer is missing, copes with no archives, and is split into steps.
- Ad-hoc signing could fail on the bundle's data files: it is kept only when it verifies.

**Checked**
- New tests (round 108): an update's notes leave out the install guide; a release of version-free installers finds
  its installer, takes its libraries from the "libraries" release by exact tag (build-pr7's never build-pr72's) and
  the "libraries" release is never offered.
- The publishing script against a stand-in `gh`: installers to the release, the rest to "libraries", a deleted
  build's archive removed.
- Unit checks: Windows 10,011, 0 failed (678 tests). Linux and the sanitizer build: on CI for this PR.

## 2026-10-14 (round 107): redo of material assets, tests that never touch the project, one version number (task 0081)

**Asked:** merge the stack (done: #61-#71 into main); "push V1.0.0 (or whatever nomenclature you use for the first
release before hotfixes)"; "test material shouldn't be even in the project at all when it comes to release"; "fix the
redo material system". Then: "let's leave it at 0.10.0 because I don't want an official 1.0.0 release just yet (needs
more refining)", and "still publish that version so people know it's a pre-release of the program" - so the version
stays 0.10.0, published as the pre-release `v0.10.0`.

**Changed**
- **Blendity 0.10.0 published as a pre-release:** a `v*` tag before 1.0 (or with a suffix, v1.0.0-beta.1) makes a
  GitHub Release marked "pre-release", titled "Blendity 0.10.0 (pre-release)". The updater still offers such a tagged
  version with previews off (previews are the per-PR builds); Version History labels it "pre-release".
- **One version number:** the VERSION file (still 0.10.0). CMake's project version and the macOS bundle read it (the
  bundle said 0.9.0). A future stable release is a VERSION change plus a `v*` tag.
- **Fixed: redo of a material linked to a .mat** brought back the undone values (any field) and the asset kept them:
  an undo or redo state set aside keeps copies of its own linked materials now.
- **Fixed: tests could write into the project they ran from.** A test making a clip or material before any editor
  existed resolved its path against the working folder; the test and stress programs point the asset root at their
  scratch projects first. The leaked R86Hold/R86Spin/R86Up.anim and the empty Materials/R51 were deleted from the
  project. (Installers never carried Assets.)

**Checked**
- New tests (round 107): a .mat-linked material's edit undoes and redoes twice and the asset follows (it fails without
  the fix: green 0.10 after the redo); objects sharing a .mat still share one copy in an undo state; the tests'
  asset root is the scratch project before the first test, and a relative asset path lands there; a tagged version
  GitHub marks a pre-release is offered with previews off, PR builds only with them on.
- Unit checks: Windows 10,004, 0 failed (676 tests); the whole suite in render-cache verify mode: 9,999,
  0 failed, 0 stale pictures. Linux with libraries (before the review fixes; CI runs them after): 9,906, 0 failed. Sanitizer build: 9,812, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). Fixed:
- An undo state gave every slot its own copy of a shared .mat material, so objects that shared one material stopped
  sharing it after an undo: slots sharing one material share one copy now (as duplicating a scene does).
- The asset-root test passed whatever the tests did first: it checks the root main() set before any test now.
- CMake's project version and the macOS bundle's version read the VERSION file instead of a number of their own.

## 2026-10-14 (rounds 105-106): updates, older builds, and Blender's libraries optional at run time (tasks 0079-0080, ADR 0033)

**Asked:** "an option for the software while installed on a computer to check for updates and then ask the user if
they want to install the update. The install note should note the changes that the update will bring. Furthermore,
the user can choose to go to an older update (Before A Certain Pull Request)"; "if the DLL or external libraries isn't
found, result back to the fallback with the software that doesn't rely on dependencies (such as situations with no
internet, etc.). Then if there is internet, ask the user if they want to install the new libraries"; "The user should
always have the option to use dependencies or not."

**Changed**
- **Builds for every merged PR:** a merge into `main` publishes a pre-release `build-pr<N>` (notes: the PR's
  description); v* tags publish stable releases. Each carries the installers, the libraries archive and
  SHA256SUMS.txt. Builds know their tag and version (shown in About and the status bar).
- **Updates:** once a day, when online, Blendity asks GitHub's releases list (WinHTTP on Windows, `curl` elsewhere: no
  new library) and offers a newer build with its notes - Install / Not now / Skip this one. Help > Check for Updates
  and **Help > Version History** (every build for this system, older ones too: the build before a certain PR).
  Downloads are checked against the release's SHA-256 list; Windows installs silently and restarts Blendity, an
  AppImage replaces itself, macOS opens the disk image. Preferences > Updates: check at start, include PR builds.
- **Libraries optional at run time:** an install holds Blendity (dependency-free, always starts) and Blendity-full.
  Blendity hands over when the full build's libraries load (`--probe`) and **Preferences > Libraries > Use Blender's
  libraries** is on; otherwise - a library missing or damaged, offline, turned off, `--no-libs` - it runs on its own
  code, and online it offers this build's libraries (Download / Not now / Never ask), unpacked into
  `~/.blendity/libs/<build>`. The Windows installer's "Blendity only" type leaves them out.
- The JSON reader moved from the glTF importer to `core/json.h`; SHA-256, downloads, releases and the launcher are new
  in `core/`.
- #70's installer jobs needed: Windows Embree's SYCL runtime (`dpcpp`), Linux GCC 14 (Blender's libraries call its C++
  runtime, shipped in `lib/`), macOS's own zlib.

**Checked**
- New tests (round 105): SHA-256 against FIPS 180-4's vectors; SHA256SUMS lines; a releases list gives the newer builds
  (stable only or with PR builds), none for the newest or a local build, all in Version History; the Update and
  History dialogs at 1280 x 720, Skip remembered; (opt-in, `BLENDITY_NET_TESTS=1`) GitHub's real list fetched, a 404
  reported.
- New test (round 106): this build knows whether it has the libraries; `--probe` answers; the Libraries dialog at
  1280 x 720; Never ask and Use Blender's libraries kept with the preferences.
- By hand on Windows: a scratch install with both programs - with the libraries it handed over, with `--no-libs` and
  with `embree4.dll` removed it ran itself.
- Unit checks: Windows 9,992, 0 failed (672 tests); the whole suite in render-cache verify mode: 9,993,
  0 failed, 0 stale pictures. Linux with libraries: 9,900, 0 failed. Sanitizer build: 9,806, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). Fixed:
- On Linux and macOS, opening a folder or starting a program ignored SIGCHLD for the whole process, after which every
  download and the libraries probe failed: children nobody waits for are reaped by a thread of their own instead.
- The Windows installer never started Blendity again after an update: it does now ([Run] after an update started
  from Blendity), installing where the old copy is (for this user, or for everyone).
- A build older than the newest 100 releases never saw an update or its libraries: its own release is fetched too.
- Installing "Blendity only" over an install with the libraries left the old full build to be handed over to: it is
  removed.
- On Windows the dependency-free program returned at once while the full one worked: it waits and passes on the exit
  code.
- Downloads went to guessable names in the shared /tmp (another user could plant a link): a private
  ~/.blendity/downloads, and only plain file names from a release.
- Closing Version History by clicking outside stopped all later offers for the session.
- Replacing an AppImage could lose it on a full disk, and restarting after a libraries download ran from the old
  AppImage's vanished mount: the new one is copied beside the old first, and Blendity restarts as the AppImage.
- Unpacking the libraries and copying an AppImage ran on the editor's thread (seconds): on the downloading thread now.
- Releases are published one at a time, in merge order; the libraries are asked about at most once a day and never
  turn into an update offer when update checks are off.
- The workflow's token is read-only except in the release job (a pull request's build scripts run with it).
- curl keeps to https (redirects too) with no fixed time limit on large downloads; WinHTTP treats an unfollowed
  redirect as an error; SHA-256 streams the file; Windows' own tar.exe from System32; the probe gives up after 30 s;
  a download stops when Blendity closes; Version History scrolls through every build; the title no longer reads
  "Blendity Blendity".
- Left: a full build that crashes after a good probe doesn't fall back by itself (Preferences > Libraries or
  --no-libs does).

## 2026-10-14 (rounds 103-104): FFmpeg linked like Blender's other libraries, and the libraries in the installers (tasks 0077-0078, ADR 0032)

**Asked:** "Can we install ffmpeg and treat it like a blender dependency?" - link it, and "make sure other dependencies
should be included when installing the project. The user shouldn't really have to worry on downloading blender on
their own."

**Changed**
- **FFmpeg 8.1** (Blender's build: x264, x265, libvpx, libaom, Opus, Vorbis, Theora, LAME) is linked behind
  `BL_WITH_FFMPEG` by build.bat, build.sh and CMake. Render Animation encodes its MP4 itself (H.264, CRF 18) on a
  background thread; a build without it falls back to an `ffmpeg` program or PNG frames, as before. `libs` lists it.
- Fetched into this machine's Blender library checkouts: `ffmpeg` (Windows 46 MB, Linux 67 MB) and Windows' `aom`
  (FFmpeg's AV1 DLL, 9.5 MB); Linux also links OpenJPEG and Blender's `libc_compat` (copied to `extern/`).
- **`packaging/fetch_blender_libs.sh <platform>`** fetches only the library folders Blendity uses, at commits pinned in
  `packaging/blender-libs.txt` (about 600 MB a platform), plus Blender's colour config when missing.
- **The installers carry Blender's libraries:** the release workflow fetches them (cached), builds all three with
  them and checks each has Embree, OpenImageDenoise, OpenColorIO, Jolt and FFmpeg; the macOS app bundle holds `lib/`
  and `datafiles/`. Nobody installing Blendity needs Blender or its libraries.
- Licences: FFmpeg and its codecs listed; LGPL-2.1 and GPL-2.0 texts added.

**Checked**
- New test (round 103): six 101 x 57 PNG frames become an MP4 FFmpeg reads back as 6 frames of 102 x 58 with libx264;
  a missing frame is refused with a reason. Render Animation's test made its MP4 in-process. Both on Windows and Linux.
- Unit checks: Windows 9,965, 0 failed (667 tests); the whole suite in render-cache verify mode: 9,966,
  0 failed, 0 stale pictures. Linux with libraries: 9,873, 0 failed. Sanitizer build: 9,779, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). Fixed:
- CMake's Linux build wouldn't have linked: `libc_compat` sat in the static library, where nothing pulled it in before
  the codecs asked; it is now on every executable's link line.
- An odd frame size was stretched by a pixel (a soft, shifted picture): it is padded by repeating the edge now.
- The video wasn't tagged and used BT.601 colours: it is BT.709, limited range, tagged, as Blender writes it.
- Closing the editor while an MP4 was being encoded left the encoder running into the program's exit: it stops
  between frames and the editor waits for it.
- A failed or stopped encode left an unplayable half-MP4: it is deleted.
- x264's options went to whatever encoder was found: a fallback encoder gets a high bit rate instead.
- FFmpeg's log level was set from the worker thread: once, from the main thread.
- Only the five FFmpeg DLLs Blendity uses are shipped (not avdevice / avfilter); CMake no longer links an FFmpeg
  folder without its libraries; `extern/README.md` says where each copied file comes from.
- Left: the FFmpeg DLLs load with the program (as the other libraries' do), so a missing one stops it starting.

## 2026-10-14 (round 102): installers for Windows, Linux and macOS (task 0076)

**Asked:** installers for Windows, Linux and macOS; the choice was an Inno Setup .exe, an AppImage and a .tar.gz, and
an unsigned .dmg, attached to a GitHub Release on v* tags.

**Changed**
- **Windows:** `Blendity-Setup-<version>.exe` (Inno Setup, `packaging/windows/blendity.iss`): installs for the user
  alone (no administrator), Start menu and optional desktop shortcuts, `.scene` files opening in Blendity, an
  uninstaller in Settings > Apps. `build.bat installer` makes it locally when Inno Setup 6 is installed.
- **Linux:** `Blendity-<version>-x86_64.AppImage` and `blendity-<version>-linux-x86_64.tar.gz`
  (`packaging/linux/make_packages.sh`, built on Ubuntu 22.04 for older systems).
- **macOS:** `Blendity-<version>-macos.dmg` (`packaging/macos/make_dmg.sh`): the app with its icon and version and an
  Applications link; unsigned (right-click > Open the first time).
- `.github/workflows/release.yml` builds all three on pull requests that touch packaging, installs or mounts each and
  starts it headless, and on a v* tag makes the GitHub Release with them. `VERSION` is 0.10.0. No tag was pushed.
- An icon (a cube in the Emiliano palette) drawn by `packaging/make_icon.py` without any library.
- A `.scene` path given alone - a double-clicked file - opens, with the project folder above it.
- The debug OpenJPH DLL (`openjph.0.25d.dll`) is no longer copied into `dist\windows`.
- README: a Download section.
- Fixed: the test "feel: Unreal's End drops the selection..." failed twice on CI's sanitizer build (#61, #67): it gave
  Alt+J's camera turn a fixed 0.5 s, and a sanitizer frame can take most of that; it now waits for the turn to end.
  The auto-rig's view test (#68) and two others that waited the same way do too.

**Checked**
- New test (round 102): a lone `.scene` argument is the scene and its project is found above it; an option's value
  isn't.
- The Linux .tar.gz from the WSL build, unpacked elsewhere, starts headless and makes `~/BlendityProject`.
- Unit checks: Windows 9,953, 0 failed (666 tests); the whole suite in render-cache verify mode: 9,954,
  0 failed, 0 stale pictures. Linux with libraries: 9,861, 0 failed. Sanitizer build: 9,778, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.
- The release workflow's own run on this PR (Inno Setup isn't installed on this machine).
- A headless run writing to a path with an accented letter (UTF-8 arguments on Windows).

**Review** (Opus code-reviewer). Fixed:
- `build.bat release all` went on into the installer step (CI's Windows check would have failed without Inno Setup).
- The release job looked for the installers where the artifacts don't put them: no release would have been made.
- Installing "for all users" put the program in one user's folder: `{autopf}` follows the choice.
- A double-clicked scene with an accented letter in its path didn't open (Windows' ANSI argv): the arguments come from
  the wide command line as UTF-8. `--scene`'s value now finds its project too, and `.SCENE` in any case counts.
- The macOS note: on macOS 15 an unsigned app is allowed in System Settings > Privacy & Security > Open Anyway.
- The workflow's checks start each installed copy away from the repository (a fresh machine's project), wait for the
  Windows uninstaller and check that it removes the `.scene` association; rerunning the dmg script no longer nests
  the licences.

## 2026-10-14 (round 101): the laptop minimum - an i7, 8 GB, the HP Envy 13.3" 2-in-1 (task 0075, ADR 0026 amended)

**Asked:** a new minimum machine: an i7 with 8 GB of RAM, the HP Envy 2-in-1 13.3".

**Changed**
- The minimum is now that laptop (i7-1165G7, 4 cores / 8 threads, 8 GB shared with Iris Xe, 1080p at 150 %): README,
  MANUAL chapter 11, ADR 0026 and PERFORMANCE.md pass 9.
- **First run** on such a machine: Low, and a welcome that says so, with **Use High** one click away
  (`BLENDITY_MIN_SPEC=1` shows it on any machine).
- **Touch (Windows):** two fingers over the Scene view pinch to zoom, drag to pan, twist to turn round the pivot, in
  every editor feel; one finger is the mouse. **Pen:** its pressure scales the weight-paint brush.
- The Low tier's views read shadows with a 2 x 2 bilinear filter (4 taps instead of 9).
- An integrated GPU's path-tracing budget is a quarter of RAM (Iris Xe reports half as its own).
- `blendity_stress --laptop`: 5 threads (4 cores' 8 threads), the times to be multiplied by 1.4 for the laptop's clock.

**Checked**
- `editor_render` at Low with `--laptop` x 1.4: orbiting 13.0 ms (60 fps), moving an object 26.0, a material edit 24.9,
  Play 22.8 (30-40 fps; 60 while editing is **not met** on the laptop, and Low keeps 67 % rather than looking soft).
  On 8 full cores (pass 8's setup) the frames pass 8 left over 16.7 ms are now 12.9 and 11.4.
- New tests (round 101): a pinch of 2 halves the distance, a two-finger drag pans with the fingers, a twist turns the
  view by its angle, over the Inspector nothing, NaN ignored; a stroke at pressure 0.5 adds half; the first run on the
  minimum machine shows the welcome, which fits 1280 x 720, and Use High switches and is saved.
- The Preferences dialog checked at 1280 x 720 (a headless screenshot).
- Unit checks: Windows 9,948, 0 failed (665 tests); the whole suite in render-cache verify mode: 9,949,
  0 failed, 0 stale pictures. Linux with libraries: 9,856, 0 failed. Sanitizer build: 9,773, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). Fixed:
- `--laptop`'s message had a line break inside its string: the stress tool didn't compile.
- A pen hovering after lift-off left its pressure 0 for every later mouse stroke: only mouse messages Windows made from
  a pen carry it now, and lifting or leaving resets it.
- The first finger's press, when Windows makes one, moved a held drag to the gesture's middle: gestures leave the
  mouse position alone while a button is down.
- Windows' own gestures (begin, end, taps) went unanswered; the pointer calls are looked up so the program starts
  without them.
- The 2 x 2 filter's edge is narrower than 3 x 3's, not as soft: the comments and lesson say so.
- Left: the Low filter is a flag on the cached shadow map, safe while every render runs on the main thread (a note for
  any render moved off it).

## 2026-10-14 (round 100): render animations and sequences to video (task 0074)

**Asked:** rendering sequences to video; the choice was PNG frames plus an MP4 through FFmpeg.

**Changed**
- **Render Animation** (File menu, the Render window, Ctrl+F12, `renderanim [first last] [fps] | stop`): every frame
  of the scene's sequence - or, without one, the selected object's clip - through the F12 render, into
  `Renders/<scene>_animation_<time>/0001.png...`; the pose goes back afterwards.
- Then, when FFmpeg is installed (on the PATH, or `renderanim ffmpeg <path>`, kept with the preferences), an H.264 MP4
  is made beside the frames in the background. Blendity runs FFmpeg rather than linking it: no new dependency.
  Without it the frames are the result, and the log says how to make a video.
- A render stops cleanly when something ends what it poses through: Play, a save, an undo, another scene, another
  object selected for a clip. Camera Sequence and Render Animation don't run at once.
- Fixed: the IK test "in Play a Rotator swinging a foot target" failed once on a fast CI runner (Play runs on the
  clock); it now steps until the target has moved.

**Checked**
- New tests (round 100): 21 frames of a sequence written as 0001-0021.png at the render's size, the first and last
  different, the pose put back; FFmpeg's arguments and the Windows command-line quoting; the MP4 made where FFmpeg is on
  the PATH (not on this machine); a clip's render stops when another object is selected or its preview ends; a lone
  number is refused.
- Unit checks: Windows 9,935, 0 failed (662 tests); the whole suite in render-cache verify mode: 9,936,
  0 failed, 0 stale pictures. Linux with libraries: 9,843, 0 failed. Sanitizer build: 9,760, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). Fixed:
- The rasterizer's frames waited for mouse or key input in the real app (the editor slept between them).
- A save, an undo, Play or another scene mid-render left the rest of the frames unposed; another object selected for
  a clip was posed and left so. The render now stops with a message.
- Camera Sequence could start over a running Render Animation.
- FFmpeg read the terminal on Linux (`-nostdin`); its exit code was lost once SIGCHLD is ignored, and a failed encode
  with a file left behind counted as made.
- An empty scene sequence hid the selected clip; a frame rate of 0 or NaN from a file reached FFmpeg; a fractional end
  rendered one frame too many.
- The Render window's tooltip searched the PATH every frame; it is looked up when a render starts.
- Two renders in one second shared a folder; a failed PNG went on with a gap; a lone number or unknown word in
  `renderanim` started a render.
- Left: a PATH entry with non-ASCII characters on Windows isn't searched (set the path with `renderanim ffmpeg`).

## 2026-10-14 (round 99): themes - Dark, Light, Emiliano Light and Emiliano Dark (task 0073)

**Asked:** an editor palette / theme option using the user's Figma site ("look at the variables tab in figma").

**Changed**
- **Preferences > Theme** (console: `theme <name>`): Dark (Blendity's), Light, **Emiliano Light** and **Emiliano Dark**.
  The two Emiliano themes take the Figma file's Variables (Branding Guide, Light and Dark modes): paper F1EEE5 and
  charcoal 292927 swapped between the modes, amber D7882B / A65E1E for buttons and selection, mint 45E8A2, red E6533C,
  blue 4C76FF and amber FF9A00 as signal colours (focus, X / Y / Z, errors, warnings), stone, slate and silver for
  borders and dim text. The choice is kept with the preferences.
- Colours the windows wrote by hand (list stripes, ruler strips, row hover, keyframe diamonds, the auto-rig's figure)
  are now theme colours.

**Checked**
- New test (round 99): Emiliano Light and Dark give the site's colours, case aside; an unknown name is refused; the
  theme survives saving and loading the preferences.
- Headless screenshots of both Emiliano themes (Animation layout).
- Unit checks: Windows 9,919, 0 failed (660 tests); the whole suite in render-cache verify mode: 9,920,
  0 failed, 0 stale pictures. Linux with libraries: 9,827, 0 failed. Sanitizer build: 9,744, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). Fixed:
- In the light themes, tooltips, the F9 panel, the Hierarchy's scene row, every other Console line, the Learn sidebar,
  notes and questions, the Lighting and Render settings and the Project folder tree kept dark fills under dark text:
  they take theme colours now (tooltip, bar, pane, stripe, well, note, question, link), as do the scrollbars, menu
  separators, popup outlines, the windows' toolbars, the timeline playhead and the Edit Mode button's label.
- Emiliano Dark's borders and field outlines were the toolbar's colour; they show now. Its selected keys are amber.
- Emiliano Light's auto-rig figure and graphs sit on stone, not charcoal.
- `set_theme` and the Preferences combo share one name lookup (`ui::theme_index`).
- The Dark theme's Learn table rows, Sequencer details and curve keys moved by a shade or two (now theme colours).
- Left as they were: the Scene and Game views' and the UV editor's own drawing (they read over any picture).

## 2026-10-14 (round 98): a texture's Physical Size drives its materials' tiling (task 0072)

**Asked:** a texture's Physical Size should really adjust its tiling.

**Changed**
- Changing a procedural texture's **Physical Size** re-tiles every material of the scene using it (Tiling = 1 / size,
  in any of its maps), in the same undo step. A material tiled by hand keeps its Tiling.

**Checked**
- New test (round 98): Bricks from 2 m to 4 m takes a material from Tiling 0.5 to 0.25, and one using it only as a normal
  map; hand-tiled ones keep 3 and 1/3 (passed on the way); an unused `.mat` is left; undo and redo.
- Unit checks: Windows 9,913, 0 failed (659 tests); the whole suite in render-cache verify mode: 9,914,
  0 failed, 0 stale pictures. Linux with libraries: 9,821, 0 failed. Sanitizer build: 9,738, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). Fixed:
- A `.mat` asset no object used was re-tiled but undo couldn't put it back: only the scene's materials follow now.
- Only the colour map counted as using the texture: its normal, roughness, metallic and emission maps count too.
- A dragged Physical Size passing a hand-tiled material's size picked it up: which materials follow is decided at
  the first change of the undo step.
- Box mapping's z tiling follows too when it matched.
- The re-tile no longer scans every `.mat` file on each frame of a drag, nor logs each frame.
- New checks: a normal-map-only material, a hand-tiled one passed on the way, the unused `.mat`, redo.
- Left for its own task: redo of an edit to a material linked to a `.mat` (any field, not only tiling) keeps the
  undone values (older than this task).

## 2026-10-14 (round 97): drag stacks into order (task 0071)

**Asked:** drag-and-drop reordering of the camera filter stack, the modifier stack and the other stacks.

**Changed**
- Drag a **modifier's**, a **camera filter's** or a **texture layer's** name in its header to another place in its stack:
  a line shows where it lands and the item is outlined; letting go moves it, one undo step. A click without moving still
  folds the header; the menus' Move Up / Down stay.
- The **Sequencer's tracks** move the same way, by their names (clips go with them). Console: `seq trackmove <from> <to>`.

**Checked**
- New tests (round 97): Bevel dragged below Smooth in a Bevel / Array / Smooth stack ends last, undo puts it back in one
  step, a click still folds; a camera's first filter dragged below the second ends last; a Sequencer track dragged
  below another takes its clips, and `seq trackmove` moves it back.
- Unit checks: Windows 9,902, 0 failed (658 tests); the whole suite in render-cache verify mode: 9,903,
  0 failed, 0 stale pictures. Linux with libraries: 9,810, 0 failed. Sanitizer build: 9,727, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). Fixed:
- A drag whose list stopped showing (another object selected, the Sequencer emptied by an undo) stayed alive and moved
  an item the next time the list showed; it is now dropped.
- Undo, redo and Esc during a drag now drop it (the item under its old index could have been another one).
- The insert line could be drawn over the window above a scrolled Inspector; it is drawn inside the panel now.
- Drags that couldn't land (a track in Play, an unsaved texture's layer) no longer start.
- New checks: a filter dragged up, a drag whose list went away, Esc.
- Left as is: the line below the last item sits under its header, above its open settings.

## 2026-10-14 (round 95): a global sequencer - each clip names its object (task 0070, ADR 0030 amended)

**Asked:** "The sequencer should work globally and have two things to consider when adding a clip. Which animation clip
it is using and what object is it attributed to."

**Changed**
- The Sequencer window edits **the scene's sequence** (saved with the scene), with or without a selection.
- **Each bar is a clip and an object:** a dropped clip plays on the selected character; the Object field below the
  tracks takes an object dragged from the Hierarchy; bars read "Walk -> Robot". Console: `seq add <clip> <track>
  <start> [object]`, `seq object <clip> <object>`.
- Preview poses every bar's object and puts them all back. In Play the scene's sequence plays (triggers and `seq jump`
  too); an object's Animation component steps aside while the sequence has a bar on it.
- Overlapping bars crossfade only when they play on the same object.
- `.seq` files gain a 4th column (the object); older files load unchanged. The Sequencer component still plays a
  sequence on its own object.

**Checked**
- New test (round 95): one scene sequence moves two lids from two tracks in Preview (and puts them back), overlapping
  bars on different objects don't blend, the `.seq` and the scene file keep the objects and the sequence, and Play
  moves them.
- New test (round 96): three objects' bars overlapping on one track all play; an object given a bar during Preview is
  put back; renaming the `.seq` keeps the scene on it.
- Unit checks: Windows 9,885, 0 failed (656 tests); the whole suite in render-cache verify mode: 9,886,
  0 failed, 0 stale pictures. Linux with libraries: 9,793, 0 failed. Sanitizer build: 9,710, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.
- Stress `animation`: 201 tracks 0.0735 ms a frame (target 0.2 ms); a sequence of 4 tracks crossfading 201-track
  clips 0.3143 ms a frame (target 0.5 ms).

**Review** (Opus code-reviewer). Fixed:
- Two rigs could share a lookup key and pose each other's bones.
- Renaming the scene's `.seq` lost it.
- An object given a bar during Preview kept its previewed pose.
- A track with three overlapping objects dropped one.
- A jump before Play's first frame didn't find the bars' objects.
- `seq jump` couldn't reach a component's sequence.
- Sequences made before this task are editable again.

## 2026-10-14 (rounds 89-94): rigging - character roots, bone handles and extrude, rig controls, auto-rig colours and views, grouped keyframes (tasks 0065-0069, ADR 0031)

**Asked** (2026-10-14):
- "When adding an armature to a mesh/object nest it in the hierarchy itself; similar to how unity handle's humanoid
  models being imported."
- "Add the ability to move the base and tip of a bone ... add bones using extrude like vertices."
- "Give the inverse kinematics a special gizmo ... (also add the option to remove FK/IK systems) ... like blender and
  maya."
- "For auto-rig ... make them different colors to indicate different parts of the body ... transfer to different
  orthographic views."
- "Select multiple keyframes in the animation block (drag select) ... x,y,z in the same frame and then expand as
  needed."

**Added**
- **Character roots** (0065): GameObject > Armature on a mesh, Add Armature (Skin) and the auto-rig put the mesh and its
  Armature inside "<mesh> Rig" in the mesh's old place; the Animation window and the Sequencer animate that root.
- **Bone handles and Extrude** (0066): a selected bone's tail handle (and Shift+drag on its head) aims and stretches it,
  children staying put, the result its rest pose; Extrude (Ctrl+E / E) grows a bone from the tip that follows the mouse
  until a click (Esc cancels). Console `bone head|tail|extrude`.
- **Rig controls** (0067, ADR 0031): the Rig Control component (Circle, Square, Box, Diamond, Sphere, Arrows, Cross,
  Root) drawn as wire over the scene, coloured by side (left blue, right red, middle yellow). IK targets are boxes,
  poles diamonds with a dashed line to their joint; IK's bones are tinted. FK controls (circles that turn their bones).
  Remove IK, Add / Remove FK Controls; console `ik remove`, `fk add|remove`.
- **Auto-rig** (0068): markers coloured by body part; Front / Left / Right / Back / Top views (`autorig view`), a
  dragged marker keeping its depth away from the front view.
- **Keyframes** (0069): the dopesheet groups a vector's axes in one row that unfolds; a group diamond selects and moves
  all its axes' keys; box select takes every diamond it touches, lights them while dragging, Shift adds, Ctrl removes;
  dragged keys show where they will land.
- Learn: "A character's root", "Bone heads, tails and Extrude", "Rig controls: IK and FK", "Auto-rig colours and
  views".

**Checked**
- New tests (rounds 89-94): the rig root's shape and place (in a group and at the scene's top level), undo; the skin
  following the root; Add Armature (Skin) nesting and leaving a shared skeleton; tail and head drags (children kept, rest
  pose, one step, a half-scale armature); Extrude kept (one step) and cancelled (no step, no bone); IK controls' shapes
  and sides (including ".L" names), Remove IK (one step, a user's object kept); FK turning its bone, a click on its wire,
  Remove FK, FK refused on IK bones, Reset Pose with FK, an idle solve; marker colours; view switching; a side-view drag
  keeping depth; grouped rows, a group diamond's move, a thin box, Ctrl box.
- Unit checks: Windows 9,873, 0 failed (654 tests); the whole suite in render-cache verify mode: 9,874,
  0 failed, 0 stale pictures. Linux with libraries: 9,781, 0 failed. Sanitizer build: 9,698, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). Fixed:
- The bone handles were drawn before the picture and painted over.
- An extrude made an undo step per frame (and Esc left steps).
- Remove IK could delete a user's object used as a target.
- FK on an IK bone redrew the editor forever.
- Reset Pose was undone by FK controls next frame.
- The side and top views moved dragged markers to the wrong depth.
- A scaled armature's tail landed away from the mouse.
- Also: the head handle took the gizmo's centre (now Shift), roll drift and the grab offset, a stale bone drag across
  scenes, shortcuts and right-click fly during an extrude, the rig root's place at the scene's top level, wrapping an
  import's mesh again, any single-child parent taken as a root, Create Child > Armature's meaning, ".L" control
  names, a misspelt `fk remove` name, disabled controls drawn and picked, FK lookups by name every frame.

## 2026-10-14 (round 88): typed object and asset fields, Unity's "None (Armature)" (task 0064)

**Asked:** "Add the ability to drop objects from hierarchy into parameter fields in the inspector and not just rely on
the name of an object (and indicate type just like unity3d), like armature parameter into skin under Rigging … such as
armature, animation, sequencer etc."

**Added**
- **Object fields know their type** (`Reflector::object(name, value, type)`):
  - Shown as Unity's object field: the type's icon and "Armature (Armature)", or "None (Armature)" when empty; yellow
    when the object is missing or of the wrong kind.
  - Dragging an object of another kind over a field turns it red, and the drop is refused with a Console line.
  - The ⊙ picker at the field's right lists None and the scene's objects of that kind.
  - Clicking the field still types a name.
  - Typed fields:
    - Skin Armature (Armature)
    - Lattice Object (Lattice)
    - Curve and Array Curve (Curve)
    - Boolean and Mesh Deform Object (a mesh)
    - Transfer Weights From (a mesh)
    - IK Target and Pole take any object.
- **Asset fields** (`Reflector::asset(name, path, ext, kind)`):
  - The Animation's Clip (.anim) and the Sequencer's Sequence (.seq) take a file dragged from the Project window, and
    refuse another kind.
  - Their picker lists the project's files of that kind.
- Learn: "Typed fields and asset fields" in the Inspector lesson.

**Changed**
- A press on a file in the Project window that turns into a drag no longer selects the file, so the Inspector stays on
  the object whose field you're filling. A click selects it when the button comes up.

**Checked**
- New tests (round 88):
  - A Skin's Armature field refuses a Sphere and takes the Armature (one undo step), and shows "None (Armature)" then
    "Armature (Armature)".
  - The picker lists None and the armature but not the Sphere; picking None is an undo step of its own.
  - A .anim dragged from the Project window fills an Animation's Clip, a .seq is refused, the object stays selected,
    undo empties it, and a click on a file still selects it.
- Unit checks: Windows 9,795, 0 failed (643 tests); the whole suite in render-cache verify mode: 9,796,
  0 failed, 0 stale pictures. Linux with libraries: 9,703, 0 failed. Sanitizer build: 9,620, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). Fixed:
- A field's help tooltip (and the picker's) never showed: the picker had become the last widget.
- The asset picker walked the project's folders every frame while open; it now reads them once, as it opens.
- Each asset field checked the disk every frame; now once a second.
- A pick whose field wasn't drawn the next frame could apply much later.
- A fast press-move-release on a file selected it before seeing the drag.
- The missing type icon.

## 2026-10-13 (round 87): switch the editor's feel: Unity, Unreal, Blender or Maya (task 0063)

**Asked:** "Switch the editor's feel between Unity / Unreal / Blender / Maya". The user chose keys and mouse navigation
only.

**Added**
- **Editor Feel**: Edit > Editor Feel, Preferences > Keymap, or `feel unity|unreal|blender|maya`. It sets the keymap
  preset and the Scene view's mouse navigation together, and is kept in the prefs. Your own key changes stay on top.
- **Navigation by feel:**
  - Unity: as before.
  - Unreal: RMB look + WASD; Alt+LMB orbit; MMB, Alt+MMB or LMB+RMB (in either order) pan; Alt+RMB dolly.
    Letting go of LMB while RMB is still held looks round again.
  - Blender: MMB orbit, Shift+MMB pan, Ctrl+MMB zoom. Alt+click selects an edge loop and Ctrl+Alt+click a ring,
    without moving the view; RMB doesn't fly.
  - Maya: Alt+LMB tumble, Alt+MMB track, Alt+RMB dolly. Plain MMB and RMB don't move the view.
- **An Unreal keymap preset**: Unity's keys plus Alt+P Play, Ctrl+W duplicate, End drop to floor (the whole object
  with its children, onto y = 0), and Alt+G / H / J / K for the perspective, front, top and side views.
- New actions for any keymap: Perspective / Front / Top / Side View, Drop to Floor.
- Learn: "Work like Unreal, Blender or Maya" in the Edit Mode lesson. MANUAL chapter 12 has a table of the feels.

**Changed**
- Navigation drags end when the button that started them is let go (needed by MMB orbit and LMB+RMB pan).
- The test runner flushes after each test, so a crash shows which test was running.
- The keymap presets list Unreal second, so the Preferences dropdown test (round 79) now picks Unreal from the second
  row.

**Checked**
- New tests (round 87):
  - Each feel's mouse buttons with real drags: Blender MMB, Shift+MMB, Ctrl+MMB; Maya Alt+LMB, Alt+MMB, Alt+RMB.
  - Unreal RMB, and LMB then RMB.
  - Unity's whole set unchanged.
  - Each feel's keys; an unknown feel refused.
  - Blender Alt+click loop and Ctrl+Alt+click ring with the view still.
  - Unreal End on an object and on an empty with a child; Alt+J top view.
  - The feel kept in the prefs (an unknown one falls back to Unity); your own key change kept across a feel switch.
- Unit checks: Windows 9,774, 0 failed (641 tests); the whole suite in render-cache verify mode: 9,775,
  0 failed, 0 stale pictures. Linux with libraries: 9,682, 0 failed. Sanitizer build: 9,599, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). Fixed:
- Blender Alt+click loop select did nothing.
- Unreal LMB-then-RMB box-selected instead of panning.
- A feel switch wiped your own key changes.
- `feel` with no argument switched to Unity.
- Drop to Floor ignored children.
- Maya's plain MMB panned.
- The Learn answer about RMB, and the prefs test's path.

## 2026-10-13 (round 86): a Premiere-style clip sequencer instead of a state machine (task 0062, ADR 0030)

**Asked:** "A clip editor like Premiere Pro to go between animation clips instead of using a state machine"; for Play,
the user chose "a timeline plus triggers".

**Added**
- **Sequences**, `Assets/Animations/<name>.seq`:
  - Tracks of clip instances. Each instance has its clip, start, In and Out (trim), speed and name.
  - Two instances overlapping on a track crossfade over the overlap (Premiere's cross dissolve). Rotations blend the
    short way round.
  - A higher track's clip wins for the numbers it animates.
- **The Sequencer component** plays a sequence in Play (Loop or Once, Speed).
  - Its **triggers** jump to a named clip when a key is pressed (a letter, a digit, Space, F1-F12).
  - A jump blends from the pose of that moment over the trigger's seconds, and the timeline plays on from there.
  - An Animation component on the same object steps aside while a sequence plays.
- **Window > Sequencer** (beside the Animation window in the Animation layout):
  - Drag `.anim` clips from the Project window onto a track.
  - Drag a bar to move it (onto another track too) and its ends to trim it; Esc cancels.
  - Overlaps show the crossfade as a cross.
  - Below the tracks: the selected bar's Name, Speed, In and Out, and the trigger list.
  - Right-click a bar for Go To, Speed and Delete. Add Track.
  - Preview and Play, as in the Animation window. Every edit is one undo step.
- **Console:** `seq new | add | move | trim | speed | rename | remove | track | trigger | jump | frame | preview | play |
  stop`.
- **Renames follow:** renaming or moving a clip, a sequence or their folder keeps sequences, Sequencer components and
  undo steps pointing at them. That includes sequences not loaded.
- Learn: "Sequencing clips" in the Animation lesson. MANUAL chapter 9 has a walk/run example.

**Changed**
- Game keys (`PlayContext::key_down`) are not seen while a text field has the keyboard.
- The two Previews take turns: turning on one turns off the other.

**Checked**
- New tests (round 86):
  - Sampling: one clip trimmed and at speed; a crossfade; a higher track winning; a bar inside another; rotations the
    short way.
  - `.seq` round-trip, refusing bad lines, trigger key names.
  - A clip rename and a folder rename (with a sequence not loaded) carry the sequences.
  - In Play: a trigger's key jumps and blends from the pose of the moment. Once holds the last frame. An empty
    Sequencer leaves the Animation playing.
  - In the window: a drop places a clip, a drag moves a bar, its end trims it, each one undo step.
  - Preview restores the pose with no undo step. The two Previews take turns.
  - Huge or NaN track numbers are refused. Undo and redo of an edit.
- Stress `animation`: a sequence of 4 tracks, each two crossfading 201-track clips, costs 0.31 ms a frame (target 0.5).
  The first version took 1.30 ms (string keys and allocations per value). Values are now 64-bit keys in a reused
  table.
- Unit checks: Windows 9,741, 0 failed (638 tests); the whole suite in render-cache verify mode: 9,742,
  0 failed, 0 stale pictures. Linux with libraries: 9,649, 0 failed. Sanitizer build: 9,566, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review** (Opus code-reviewer). Fixed:
- The Animation window's Preview didn't put the Sequencer's away.
- A folder rename missed sequences not yet loaded (the listing was walked before the move).
- `seq add` with a huge track number made that many tracks.
- A bar inside another snapped back at its end.
- Once stopped a frame short.
- A click in an overlap took the bar underneath; short bars couldn't be trimmed.
- Editing In pinned Out.
- A drag outlived its window.
- Triggers fired while typing in the Console.
- An empty Sequencer silenced the Animation component.
- Dropping a clip elsewhere logged a warning.
- Preview could pose another object after a selection change.
- A bad `.seq` was read again every frame.
- The stress test's clips stayed loaded after their files went.

## 2026-10-13 (round 85): the animation curve editor, Unity's Curves view (task 0061, ADR 0029 amended)

**Asked:** "Start on the later list": first the curve editor for the keyframe animation of task 0059.

**Added**
- **Tangents on keys** (Unity's modes):
  - Clamped Auto (the old Auto), Free Smooth (one slope both sides), Flat (level) and Broken (each side its own).
  - Linear and Constant spans stay.
  - `.anim` files keep a key's mode and slopes; files from task 0059 load with Clamped Auto keys.
- **The Curves view**: the Animation window's Dopesheet | Curves buttons.
  - Each property is a line of its value over time (x red, y green, z blue) on a value axis with grid lines and
    numbers.
  - Click track names to choose the curves (Ctrl+click for several); F frames them; the wheel zooms the values.
- **Editing curves:**
  - Drag keys in time and value (Shift: one direction only); drag a box to select.
  - A selected key's handles set its slope: Free Smooth moves both, Broken each alone.
  - Right-click a key for the tangent and span modes, or Delete.
  - Double-click a curve to add a key that keeps its shape.
  - In Preview the scene follows the drag.
  - Each edit is one undo step.
- Console: `anim tangent <object|.> <property> <axis> <frame> auto|free|flat|broken [in] [out]`.
- Learn: "Curves and tangents" in the Animation lesson.

**Changed**
- The Animation window's code is split into the ruler, the rows and the new `curve_editor.cpp`.
- Moving and pasting keys keep their tangents.

**Checked**
- New tests (round 85):
  - Free Smooth, Broken and Flat slopes on both sides of a key, and the file round-trip (old files too).
  - In the Curves view, dragging a key raises it and dragging its handle sets a Free Smooth slope, each one undo step.
  - Two keys of one track moved onto each other's frames both survive.
  - 300 wheel steps in on a value of 100 stop at a hair and don't hang.
  - A clip switched mid-drag gets no edit.
  - `anim tangent` with slopes is one undo step; an unknown mode changes nothing; slopes are kept to +-1e6.
- Unit checks: Windows 9,694, 0 failed (632 tests); the whole suite in render-cache verify mode: 9,695,
  0 failed, 0 stale pictures. Linux with libraries: 9,602, 0 failed. Sanitizer build: 9,519, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures, stress `animation`: 201 tracks in 0.075 ms a frame (target 0.2).

**Review** (Opus code-reviewer). Fixed:
- Moving several keys of one track could lose keys (each was moved onto the next before it moved).
- The value grid could loop for ever when zoomed far in on a large value: whole steps, at most 200 lines, and the zoom
  is clamped.
- A drag could commit into another clip after a clip change, an undo or the window losing its object.
- The commit now uses the last preview's amounts; handles and picks are limited to the shown curves; the in-handle
  shows only where the span before is a curve.
- `anim tangent` with slopes was two undo steps.
- A double-clicked key now keeps the curve's shape.
- Slopes are clamped on load; a refused edit no longer shifts the selection.
- Ctrl+wheel time zoom, in the spec, was not built: moved to the spec's "As built".

**Found on Linux**: loading a key with tangents wrote through a wrong index under GCC (the sanitizer build stopped on it).
`set_key` returned `keys.insert(...) - keys.begin()`, and C++ leaves the order of the two sides open: GCC took `begin()`
before the insert moved the keys. Now in two statements.

## 2026-10-13 (round 84): a README for GitHub and one user manual (task 0060)

**Asked:** "Update the GitHub README and compress the tutorial docs into one document with examples and use cases",
building on the Learn tab.

**Added**
- **docs/MANUAL.md**: one manual by stage of the work, the layouts' order:
  - Getting around, reference and planning, blocking out, modeling, UVs, materials and textures, lighting and
    rendering, rigging and skinning, animation, files, performance, shortcuts and the console.
  - Each chapter has short examples to follow, its use cases, and the Learn lessons that go deeper.
  - It takes in the old README's feature list and the lighting guide.
- **Opening it:** Help > Open Manual and the `manual` console command open it (the project's copy, else GitHub's).
  The Learn tab's Welcome lesson has a "The manual" section.

**Changed**
- **README.md** is a short front page:
  - What Blendity is, the screenshots and a quick start.
  - What it does by stage, the minimum and recommended machines, building, and links to the manual and the developer
    documents.
  - The stress highlights live in PERFORMANCE.md.
- `docs/guides/lighting.md` points into the manual.
- On Windows, opening a web address keeps its slashes.
- `help` lists the rigging, auto-rig, animation and manual commands.
- CMake's unit-test run starts in the source folder, as the fixtures need.

**Checked**
- New test (round 84): the manual has its 13 chapters, the README and the lighting guide link it, and every lesson
  and lesson section it names is in the Learn tab under that lesson (with CRLF checkouts too).
- Unit checks: Windows 9,658, 0 failed (628 tests); the whole suite in render-cache verify mode: 9,659,
  0 failed, 0 stale pictures. Linux with libraries: 9,566, 0 failed. Sanitizer build: 9,483, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review**: the reviewer checked every menu path, field and number against the code. Fixed:
- Shapes are under GameObject > Shapes (Parametric).
- The lighting window is Window > Lighting.
- Reference Image faces the front (`refimage ... side` for the others).
- Rigging and Skinning are two layouts.
- Preferences are Edit > Preferences... (Quality, Keymap).
- The Bricks preset already has Edge Wear.
- Axis Top / Side / Bottom and High Start.
- Console: PlayStation (PS1).
- Retiming drags the keys, not the ruler.
- The GPU also bakes and runs realtime GI.
- Two lessons were cited under the wrong lesson.
- Old README sections were restored in the manual (origins, number expressions, Join / Follow / Shell..., Active
  Camera, camera sequences, Transparent, the lighting settings).
- The test's CRLF and working-folder handling.

## 2026-10-13 (round 83): keyframe animation with a dopesheet, Unity's way (task 0059, ADR 0029)

**Asked:** "A keyframe/animation system like Unity", "Dopesheet for now" (a Premiere-style clip sequencer later
instead of a state machine).

**Added**
- **Animation clips**, `Assets/Animations/<name>.anim`:
  - Tracks of numbers by path below the animated root: Position, Rotation (Euler degrees) and Scale, or any
    component's number (`Light.Intensity`).
  - Keys with Auto (Unity's Clamped Auto: smooth, no overshoot between two keys), Linear or Constant.
- **The Animation window** (Window > Animation; the Animation layout along the bottom):
  - Preview, Record, first / previous key / play / next key / last, the frame, the clip menu with Create New Clip,
    and Add Property.
  - The tracks, with a ruler, the playhead and key diamonds.
  - Click, Shift+click or box-select keys; drag them in time; Delete; Ctrl+C / Ctrl+V at the playhead; double-click
    to add; right-click for the interpolation.
- **Record**: gizmo, Inspector or console changes to the animated objects become keys at the playhead.
- **Preview**: scrubbing poses the scene without an undo step, and turning it off puts it back.
- **The Animation component** (Unity's legacy one) plays its clip in Play (Loop, Once, Ping Pong, Clamp Forever,
  Speed), before IK solves.
- Key edits are undo steps; renaming a clip keeps the components pointing at it.
- Console: `anim new|key|interp|frame|preview|record|play|stop`.
- Learn: "Keyframes and the dopesheet" in the Bones and skins lesson, with questions.

**Checked**
- New tests (round 83):
  - Sampling: Constant, Linear, and Auto through a peak without overshoot.
  - The file, with names holding spaces and a bad line skipped.
  - Wrapping, including a negative Speed.
  - Keys on a lid: scrubbing makes no undo step, and Preview off puts the lid back.
  - Record keys a move but not a rename or a new child, and one undo takes the keys back.
  - Preview leaves unrelated objects alone, and an undo afterwards doesn't bring the pose back.
  - In Play, a bone turns and the Blender tube's skin follows.
  - Dopesheet clicks: the ruler, dragging a key, Delete.
  - Renaming a clip, and undo after it.
  - Preview refused in Play; New Scene clears the window.
- Stress `animation`: 201 tracks in 0.074 ms a frame (target 0.2 ms).
- Unit checks: Windows 9,653, 0 failed (627 tests); the whole suite in render-cache verify mode: 9,654,
  0 failed, 0 stale pictures. Linux with libraries: 9,561, 0 failed. Sanitizer build: 9,478, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review (code reviewer)**: seven majors and several minor findings, fixed:
- Property names with spaces broke the file.
- Preview put back every object in the scene.
- The previewed pose leaked into undo.
- Preview survived Play and scene switches.
- Record keyed renames, new children and IK-driven bones.
- Clips weren't forgotten on delete or cleared on another project.
- A rename didn't reach undo states.
- Also fixed:
  - Bad numbers in a file.
  - A negative Speed.
  - Missing clips read every frame.
  - The clip list walked every 2 s.
  - Pasting into another clip.
  - Moved keys' interpolation.
  - Fractional record frames.
  - Key edits in Play.
  - Console objects outside the root.
  - Unsafe clip file names.
  - Euler flips on record.
  - No dopesheet scroll.
  - Nesting.

## 2026-10-13 (round 82): the marker auto-rig, humanoid and quadruped (task 0058, ADR 0028 part 2)

**Asked:** "A Mixamo-styled automatic weight paint ... I want the markers and inpainted weights, but I want support for
quadruped auto rigging as well."

**Added**
- **Rigging > Auto-Rig**: Humanoid or Quadruped, with the character's mesh selected.
  - The Scene view turns to an orthographic front view (an animal: its left side).
  - A diagram shows the markers, the next one in orange.
- **Placing**: each click puts the next marker inside the body, halfway through the first part under the mouse.
  - Symmetry mirrors left and right.
  - Drag a marker to move it; Remove Last takes one back.
- **Markers**:
  - A person (Mixamo's): chin, wrists, elbows, knees, groin.
  - An animal: nose, base of the skull, withers, hips, base and tip of the tail, paws, front knees, hind hocks.
- **Build Rig**:
  - The skeleton with the presets' bone names (an animal also gets a three-bone tail).
  - IK on every limb, the poles following each limb's own bend.
  - The mesh bound with inpainted weights.
  - Its markers stay for another try.
- Console: `autorig humanoid|quadruped`, `autorig mark <marker> <x> <y> <z>`, `autorig symmetry on|off`,
  `autorig build`, `autorig cancel`.
- Learn: "Auto-rig with markers" in the Bones and skins lesson, with questions.

**Checked**
- New tests (round 82):
  - A 1.7 m mannequin's and a dog's markers give their bones within 2 % of the height.
  - Every vertex weighted.
  - Bending the forearm moves the hand (0.31) not the hips; lifting a front paw moves it (0.30) not the head.
  - Clicks in the front view land mid-depth (z = 0.000) and mirror.
  - Bent limbs stay as placed (bones at rest within 6e-8).
  - Markers on the wrong side, and a dog facing -Z, keep their sides.
  - Upside-down markers and unknown names are refused.
  - New Scene ends a session.
  - The background bind, its undo, and markers kept for a rerun.
- Unit checks: Windows 9,613, 0 failed (619 tests); the whole suite in render-cache verify mode: 9,614,
  0 failed, 0 stale pictures. Linux with libraries: 9,521, 0 failed. Sanitizer build: 9,438, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review (code reviewer)**: three majors and several minor findings, fixed:
- Build Rig turned limbs that weren't bent the poles' fixed way.
- The mesh's move gizmo took marker clicks.
- A session outlived New Scene, Open Scene and Play (a new scene's object ids could point it at another object).
- Also fixed:
  - Duplicate crossings at shared edges.
  - Weight Paint and Edit Mode during a session.
  - Double-clicks framing the view.
  - Esc taken from G / R / S.
  - Build while a solve ran.
  - An animal facing -Z.
  - Upside-down markers.
  - A wide stance.
  - Unknown marker names.

## 2026-10-13 (round 81): automatic weights and weight transfer by inpainting (task 0057, ADR 0028)

**Asked:** "A Mixamo-styled automatic weight paint ... based on Epic's Robust Skin Weights Transfer via Weight
Inpainting", with transfer from a body onto clothing.

**Added**
- **Bind: Automatic Weights** now inpaints:
  - A vertex clearly nearest one bone (under 0.8 of the next one's distance) that the bone can see (nothing of the
    mesh in between) takes that bone whole.
  - The rest get the smoothest weights that agree with those: one sparse solve per bone of `Q = -L + L M^-1 L` (the
    cotangent Laplacian and Voronoi areas), by Eigen's LDLT or conjugate gradients.
  - A hand hanging by a thigh, or below its own arm bone, takes no leg weight.
  - A bone no vertex is nearest (a tip bone past the end of a column) takes the vertices nearest it, so posing it still
    moves that end (round 69's Armature test).
  - **Bind: Envelope** keeps the old distance weights.
- **Transfer Weights** (Rigging window, `skin transfer <body> [mesh]`): a rigged body's weights onto clothing.
  - Matches within 5 % of the clothing's size with normals within 30 degrees (or opposite) copy; the rest are
    inpainted.
  - An **InpaintMask** vertex group forces places to be inpainted.
  - The clothing gets the body's Armature (Skin) modifier.
  - The body is an object field: drag it from the Hierarchy.
- **Large meshes (5,000+ vertices) solve on a worker thread**, in a job arena of their own, with a progress bar and
  Cancel. They land as one undo step; undo, Play or another scene cancels them.
- Console: `skin bind [object] [inpaint|envelope]`, `skin transfer <source> [target]`, `skin cancel`.
- Learn: "Automatic weights by inpainting" and "Weights onto clothing" in the Bones and skins lesson, with questions.

**Checked**
- New tests (round 81):
  - The Laplacian is symmetric, its rows sum to 0, and the masses add up to the area.
  - Inpainting between known ends is a monotone partition of unity.
  - A transfer onto a copy (renumbered and 2 mm out) gives back the weights within 1e-4.
  - The paper's loose sleeve takes no torso weight (plain closest point gives it 1.0).
  - InpaintMask vertices are inpainted, and a source's mask isn't carried.
  - A hand by the thigh takes no leg weight, whether by the ratio test or by visibility.
  - Short spine bones in a wide torso each get a band and blend (largest step between rings 0.14).
  - A NaN vertex, a wire edge and a zero-area face still give a partition, by the direct solver.
  - `skin bind` at once and on a worker thread, each one undo step; cancel, undo during a solve and closing the
    editor mid-solve.
  - Transfer by console and by dragging the body onto the Rigging window's field; the sleeve then bends with the arm.
- Stress `skin_weights` (8 threads, Eigen): 50k vertices in 108 ms (automatic) and 31 ms (transfer), against the
  2 s target. The editor's slowest frame while it solves 50k in the background: 1.4 ms.
- Unit checks: Windows 9,576, 0 failed (613 tests); the whole suite in render-cache verify mode: 9,577,
  0 failed, 0 stale pictures. Linux with libraries: 9,484, 0 failed. Sanitizer build: 9,401, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures, stress skin_weights: 50k vertices in 108 ms on 8 threads.

**Review (code reviewer)**: five majors and several minor findings, fixed:
- Short bones got hard bands.
- A wire edge or NaN vertex made the solve singular and dropped it to conjugate gradients.
- A transfer normalised over InpaintMask and non-bone groups.
- A large mesh's Add Armature (Skin) had no undo step.
- The background solve shared the job pool with the editor's frames.
- Also fixed:
  - Undo or Play during a solve.
  - Vertices moved during a solve.
  - No frames while a solve ran with the window closed.
  - The Inspector's bind button.
  - Unconverged solves went unreported.
  - The solve depended on the mesh's scale.
  - Exceptions in the worker.
  - Copying render caches.
  - Nesting.
  - Tests that missed the closest-point path and the visibility rule.

## 2026-10-13 (round 80): flatter code after the review on PR #50

**Asked:** "Can you read feedback in my GitHub and then make appropriate changes?" The one review (PR #50, task 0053):
"Be careful of nested if statements. Use methods as noted by @CodeAesthetic 'Why You Shouldn't Nest Your Code'."

**Changed** (extraction and guard clauses):
- **`MultiSelect`**: `click` is three lines over `add`, `toggle` and `range_to`. New `sync` and `select_all` replace
  blocks each of the three windows repeated.
- **One delete question** (`ask_delete` with a `DeleteQuestion`) replaces six copies: one and several, in the
  Materials, Textures and Project windows. One context-menu item (`delete_menu_item`) and two key checks
  (`select_all_key`, `delete_key`) replace the nested `if`s around them.
- **Helpers out of nested loops:** `slots_using`, `select_objects_using`, `materials_using_texture`,
  `edit_material_asset`, `project_keys`, `project_row_pressed`, `request_*_delete`.
- **`matdelete` / `texdelete`** moved out of the console's command chain into `delete_assets_command`, with
  `material_by_name`, `texture_by_name` and a shared `split_list` replacing nested lookup loops.
- Small differences:
  - Clicking away from the Materials window's "Delete the material ...?" now cancels it, as the other windows'
    questions did.
  - Ctrl+A and Delete over the Materials and Textures windows wait while a text field is being edited.
  - `matdelete` with two assets of one name deletes the first found (it took the last).
- The rule now applies to new code too (task 0057's solver is written as small functions).

**Checked**
- New tests (round 80):
  - `matdelete` with names holding spaces and an unknown one; `split_list` trims and drops empty parts.
  - The multi-selection: Ctrl+Shift with the last click gone, `sync` with hidden and edited items, Ctrl+A.
- Reviewed by the code reviewer twice (no behaviour bugs; its remaining nesting spots were flattened too).
- Unit checks: Windows 9,523, 0 failed (599 tests); the whole suite in render-cache verify mode: 9,524,
  0 failed, 0 stale pictures. Linux with libraries: 9,431, 0 failed. Sanitizer build: 9,349, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

## 2026-10-13 (round 79): drag objects onto fields; the Layout dropdown crash; a flaky CI check

**Asked:** "Some checks fail, check those out." "Clicking the drop down for the layouts crashes the program." "Also
add the ability to drag and drop objects from the hierarchy into the parameter field rather than just typing the name
(applies to all tools)."

**Fixed**
- **Clicking the Layout dropdown crashed the editor.** A dropdown's list draws at the end of the frame, after the
  Layout dropdown's per-frame list of names was gone. Every dropdown now keeps its own copy of its names while open.
- **CI (Windows) failed on PRs #49 and #50** at "min spec: a thread ceiling holds ...": on a 4-core runner every
  thread had to join one short loop. The check now takes the best of a few tries; the other Windows run of each PR
  passed, and the failed jobs were re-run.

**Added**
- **Object fields** (Unity's `ObjectField`): every field that names another object (Boolean, Lattice, Curve and Mesh
  Deform's Object, Array's Fit Curve, IK Target and Pole, a skin's Armature) takes an object dragged from the
  Hierarchy. The field lights up under it, the drop is one undo step, and the Console warns when two objects share
  the name.
- **Pressing a Hierarchy row selects it when the button comes up**, so dragging a row onto a field keeps the
  Inspector on the object you're editing (Unity selects on press, so its users lock the Inspector first). A click,
  a wobbly click, a reparent or a drop in the Scene view still selects it.
- Learn: "Object fields: drag, don't type" in the GameObjects lesson.

**Checked**
- New tests (round 79):
  - Opening the Layout dropdown and picking UV Unwrapping by mouse, from a named layout and from a custom one.
  - Dragging the Cylinder's row onto a Boolean's Object field fills it, keeps the Cube in the Inspector, and undoes
    in one step; a click still selects.
  - A click that slides 8 px still selects; a row dropped in the Scene view ends up selected; a press right after a
    drop finds no drag still live.
- Unit checks: Windows 9,511, 0 failed (597 tests); the whole suite in render-cache verify mode: 9,512,
  0 failed, 0 stale pictures. Linux with libraries: 9,419, 0 failed. Sanitizer build: 9,337, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review (code reviewer)**: two majors and several minor findings, fixed:
- A field scrolled out of view or under a popup took the drop.
- The drag state could outlive the button and drop again on the next click.
- Also fixed:
  - A wobbly click no longer selected, and a row dropped in the Scene view stayed unselected.
  - The lesson said Unity selects on release (it selects on press).
  - Two objects with one name went unreported.
  - A field being typed in wrote its old text back over a drop.
  - Nested reflectors didn't pass object fields on.

## 2026-10-13 (round 78): bind poses on rigged imports (task 0056, ADR 0025 amended)

**Asked:** "Yes, please follow Blender, Unity, and Maya methodology" (about rigged files saved in a pose).

**Fixed**
- **A rigged glTF or FBX saved mid-pose** imported with that pose as its rest, so the skin was wrong and Reset Pose
  did nothing. Each bone's rest is now its **bind pose**, as Blender, Unity and Maya read it: glTF's inverse bind
  matrices, FBX's cluster bind matrix. The pose in the file stays on the bones, and Reset Pose gives the shape the
  mesh was bound in.
- Files saved at rest import exactly as before. A bind that can't be used (missing, zero, non-finite, under a parent
  scaled to 0) falls back to the saved transform.
- The Console's "saved in a pose other than its bind pose" warning is gone. It now warns when two meshes bound a bone
  in different poses, or an FBX mesh moved after binding.
- Learn: the Bones and skins lesson says how posed files import.
- Tests: round 75's materials test left its MA..MD materials in the scratch project, so they piled up run after run
  until MC scrolled out of the window and the test failed (and crashed when run alone). It now clears them first.

**Checked**
- New tests (round 78), with fixtures exported from Blender 5.2 with the forearm bent 60 degrees:
  - The posed glTF and FBX import bent 60 degrees and match Blender's posed mesh (FBX within 9e-7).
  - `armature reset` gives the file's mesh and puts the forearm back above the upper arm.
  - Rigs saved at rest keep each bone's saved transform exactly.
  - Unusable binds leave the bone at its saved transform.
- Unit checks: Windows 9,493, 0 failed (594 tests); the whole suite in render-cache verify mode: 9,494,
  0 failed, 0 stale pictures. Linux with libraries: 9,401, 0 failed. Sanitizer build: 9,319, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review (code reviewer)**: one major and three minor findings, fixed:
- An FBX cluster without a bind matrix (ufbx leaves zeros) was taken as a bind, which froze its vertices.
- A parent scaled to 0 made a bone's rest its absolute bind.
- Two meshes bound in different poses, and a mesh moved after binding, went unreported.
- A rig saved at rest got float-rounded rests instead of the saved ones.

## 2026-10-13 (round 77): layouts for each stage of the work (task 0055, ADR 0027)

**Asked:** "Add different layouts that focus on different sections of the 3D modeling process such as:
Reference/Planning, Blocking Out, Modeling and Topology, UV Unwrapping, Texturing and materials, and then lighting and
Rendering; of course the generic editor too ... I want to avoid that feeling of so many tools ... separate layouts for
rigging, skinning and weight painting, animation".

**Added**
- **Stage layouts** in the Layout dropdown (top right, Unity's place) and Window > Layouts: Reference & Planning,
  Blocking Out, Modeling & Topology, UV Unwrapping, Texturing & Materials, Lighting & Rendering, Rigging, Skinning &
  Weight Painting, Animation.
- **Each layout arranges the windows** for its stage, for example the UV Editor beside the Scene view for UVs, or the
  Materials and Textures windows for texturing.
- **Each layout shows only its stage's tools** in the Modeling Tools window, the GameObject menu and Add Component.
  The keys and console reach the rest; Show All Tools shows them.
- **Save Layout... / Delete Layout...** for your own layouts, kept in preferences.
- **GameObject > Reference Image...**: an unlit plane with the image, facing front, side or top. It casts no shadow and
  stays out of renders; an image from outside the project is copied in.
- Console: `layout <name>|save <name>|delete <name>`, `alltools on|off`, `refimage <path> [front|side|top]`.
- Learn: "Layouts for each stage" in The Editor Layout lesson.

**Checked**
- New tests (round 77):
  - The stage layouts open their windows and show their groups.
  - Blocking Out's Tools window offers Show All Tools, and a click brings the Edit Mode section back.
  - The console reaches a hidden Light.
  - Save Layout keeps the windows and focus; built-in names are refused; Delete removes the layout.
  - A lesson's layout button arranges the windows.
  - Preferences bring back the layout and your own after a restart.
  - A Reference Image is unlit, 2:1 for a 200 x 100 picture, faces up for "top" and +X for "side", out of renders.
- Unit checks: Windows 9,470, 0 failed (591 tests); the whole suite in render-cache verify mode: 9,471,
  0 failed, 0 stale pictures. Linux with libraries: 9,378, 0 failed. Sanitizer build: 9,296, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review (code reviewer)**: two majors and several minor findings, fixed:
- A lesson's layout button (and `--layout`) changed the tools but not the windows; one table now serves both.
- Tooltips of hidden GameObject menu items landed on others.
- Also fixed:
  - The dropdown couldn't get back to a layout from a custom arrangement.
  - An unknown `layout` name went by silently.
  - `refimage` split paths at spaces.
  - Images outside the project were named by absolute path.
  - Duplicate saved layouts weren't merged.
  - The Tools window couldn't turn Show All Tools off.

## 2026-10-13 (round 76): curve and lattice points with the vertex and edge keys (task 0054)

**Asked:** "Add the ability to add more points and loop cuts for lattice and curves using similar key binds for
vertices and edges."

**Added**
- **Curves**, with a curve's points selected and outside Edit Mode:
  - **Extrude** (Ctrl+E; Blender's E) grows the curve from a selected end.
  - **Loop Cut** (Ctrl+R) puts a point on the segment under the mouse, where the mouse is.
  - **Merge at Center** (Alt+M; Blender's M) joins the selected points.
  - **Delete** removes the selected points, keeping two.
  - **Alt+C** opens or closes the curve.
- **Lattices**: Loop Cut over an edge adds a row across it, keeping the shape. Delete with a whole row selected
  removes one.
- Each is one undo step.
- Console: `points extrude|cut|merge|close|lattice ...`.
- Learn: "More points, by the vertex and edge keys" in the Modifiers lesson.

**Checked**
- New tests (round 76):
  - By keys, Extrude puts the new point beyond the last in the curve's direction and selects it; undo is one step.
  - In Blender's keymap, E grows the first point backwards.
  - Ctrl+R puts the point on the old path (0.004 off).
  - On a Poly curve, Ctrl+R puts it where the mouse was, away from the corners.
  - Merge leaves one point at the middle.
  - Delete keeps two points; Alt+C toggles Closed.
  - A lattice's U row added by Ctrl+R leaves the deformation unchanged (0 off), and Delete of a full row takes it
    away again; undo brings it back.
  - A merge of bad indices from the console is refused.
- Unit checks: Windows 9,444, 0 failed (587 tests); the whole suite in render-cache verify mode: 9,445,
  0 failed, 0 stale pictures. Linux with libraries: 9,352, 0 failed. Sanitizer build: 9,270, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review (code reviewer)**: two majors and several minor findings, fixed:
- Ctrl+R put the new point on top of the segment's first point (always on a Poly curve).
- A console merge with an index out of range could crash.
- Also fixed:
  - The closing span of a closed curve couldn't be cut by the mouse.
  - Finding the points' samples could take billions of steps on the biggest curves.
  - Delete in the Hierarchy stopped deleting a lattice with points selected.
  - The two-point rule now holds on every Delete path.
  - Extrude at the point limit said nothing.

## 2026-10-13 (round 75): many assets at once (task 0053)

**Asked:** "I want to be able to multiselect textures and materials in their tabs so I can en masse delete assets."

**Added**
- **Multi-select in the Materials, Textures and Project windows.** Ctrl+click toggles, Shift+click selects a range
  (Ctrl+Shift adds it), Ctrl+A selects everything shown (a search narrows it).
- **One question for many.** Delete, the toolbar or the context menu asks once ("Delete 3 materials?") and sends them
  all to the Recycle Bin / Trash. The Materials window empties their slots in one undo step.
- Console: `matdelete A, B` and `texdelete A, B`, with names separated by commas.
- Learn: "Many at once" in the Assets lesson.

**Checked**
- New tests (round 75):
  - By clicks: Ctrl, Shift and Ctrl+Shift select exactly the named materials.
  - Delete asks once and deletes three of four. The slot that used one shows the default, and one undo brings it
    back.
  - Ctrl+A in the Textures window takes every texture shown, or only the search's.
  - The console deletes two textures by name.
  - The Project window's Ctrl+click selects two files and Delete removes both after one question.
- The user accepted task 0050's two frames over 16.7 ms (material edit 19.8, Play 17.6).
- Unit checks: Windows 9,422, 0 failed (583 tests); the whole suite in render-cache verify mode: 9,423,
  0 failed, 0 stale pictures. Linux with libraries: 9,330, 0 failed. Sanitizer build: 9,248, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review (code reviewer)**: two majors and several minor findings, fixed:
- The Project window's context-menu Delete ignored the multi-selection.
- The console couldn't name assets with spaces.
- Also fixed:
  - Items hidden by a search were still deleted.
  - Two delete questions could fight.
  - Ctrl+A could fall through to the scene's Select All.
  - Shift with the anchor gone selected one item.
  - The Learn text said Project-window deletes empty slots (they keep the material).

## 2026-10-13 (round 74): the Rigging window (task 0052)

**Asked:** "rigging and weight painting tools should be in their own tab. The mesh filter area is getting a little
crowded from the amount of modeling operations we have."

**Added**
- **Window > Rigging**, opened beside the Inspector, with these sections:
  - **Skeleton**: Add Armature, Add Bone, Humanoid / Quadruped Rig, Reset Pose, Apply as Rest, and the armature's
    settings.
  - **Skin**: Add Armature (Skin) to the selected mesh for the selected skeleton, Bind: Automatic Weights, the
    skin's settings.
  - **Vertex Groups.**
  - **Weight Paint**: the toggle and the brush.
  - **Inverse Kinematics**: Add IK to Bone (makes its target and pole), Auto IK, the IK's settings.
- **Selecting a bone while painting** (in the Hierarchy) paints the group named after it, as Blender does.
- Console: `ik add [bone]` makes the target and pole.
- **Learn** (the user: "I really like the Learn feature ... feel free to use this system further"):
  - "The Rigging window" in the Modifiers lesson.
  - "Quality tiers: a small machine" in Performance and Stress Testing.
  - "A small graphics card" in Rendering on the GPU.
  - Each has Try it buttons and questions.

**Changed**
- The Inspector's MeshFilter section keeps modeling; one row ("Vertex Groups, Weight Paint: Rigging...") opens the
  window.

**Checked**
- New tests (round 74), by clicks:
  - Add Bone puts a child at the tip.
  - Reset Pose and Apply as Rest work.
  - Add IK to Bone makes a target and a pole, and moving the target poses the bones.
  - The MeshFilter row opens the window with the groups.
  - With a mesh and a bone selected, Add Armature (Skin) binds the mesh.
  - The window's Weight Paint paints a stroke.
  - Selecting bones while painting switches groups.
  - A layout with the Rigging tab loads back.
  - The new lessons' Try it commands make a humanoid and open the window, switch the tier and threads, and set the device to Auto.
- Unit checks: Windows 9,384, 0 failed (580 tests); the whole suite in render-cache verify mode: 9,377,
  0 failed, 0 stale pictures. Linux with libraries: 9,284, 0 failed. Sanitizer build: 9,202, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review (code reviewer)**: three majors and several minor findings, fixed:
- Add Armature (Skin) could almost never be clicked; it now looks through the whole selection.
- Fields shared UI ids: the IK's Weight and the brush's Weight. The window, each component and the brush have their
  own ids now.
- The buttons went by name, so two characters' identical bones or a name with a space went wrong. They act on the
  objects now.
- Also fixed:
  - A failed bind left a Skin with no undo step.
  - A bone selected mid-stroke was lost.
  - Add IK on an arm put the pole in front.
  - The test hook kept old button rectangles.

## 2026-10-13 (round 73): the minimum machine's GPU (task 0051)

**Asked:** the same request: "at minimum run on a GTX 1050ti".

**Added**
- **Render > Device: GPU + CPU (Auto).** Both render, each taking samples as fast as it finishes them.

**Changed**
- **GPU submissions are at most about 100 ms** (were up to 64 samples at once), so a slow card can't pass Windows'
  2 s driver timeout.
- **A scene too big for a card's memory** goes with smaller textures (a mip level at a time, never under 256), or
  to the CPU when even that won't fit, instead of failing to allocate. GPU bakes do the same.
  `BLENDITY_GPU_VRAM_MB` emulates a smaller card.

**Checked**
- New tests (round 73):
  - A 2048 texture on a 12 MB card arrives at 1024.
  - A frame whose buffers alone pass the card's memory goes to the CPU.
  - Device 2 is saved and renders on the CPU and the GPUs.
- Stress `gpu`: the longest submission is 98 ms (3060 Ti, compute kernel) and 106 ms (4070 SUPER). At 8 threads,
  the GPU-viewport spike is "go" only for ray-tracing cards; the minimum machine keeps the CPU viewport.
- Unit checks: Windows 9,349, 0 failed (574 tests); the whole suite in render-cache verify mode: 9,350,
  0 failed, 0 stale pictures. Linux with libraries: 9,257, 0 failed. Sanitizer build: 9,175, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review (code reviewer)**: two majors and several minor findings, fixed:
- The memory check used the previous frame size, because the scene is sent before the camera sets the size. It is
  checked again when the frame size changes.
- A scene that didn't fit threw its GPUs away and was built up to seven times on every pass. The GPUs are now set
  aside, open, for the next scene, and the texture sizes per level are worked out instead of built.
- Also fixed:
  - Bakes counted the render's frame in their budget.
  - The longest-submission figure never reset.
  - A card reporting no memory restarted the minimum.
  - The CPU row in Auto could be unticked to no effect.
  - `device auto` was missing from the console.
- Tests added: the fallback path through `build` then `set_camera` with a 16 MB card, and the GPUs coming back for a
  scene that fits.

## 2026-10-13 (round 72): the minimum machine, CPU and memory (task 0050)

**Asked:** "Look for more optimizations project wide to at minimum run on a GTX 1050ti and at least a quad-core
processor with eight threads running at a modest 3 Ghz and eight gigabytes of ram" (performance first).

**Added**
- **Quality tiers** (Preferences > Performance > Quality tier; console `quality low|medium|high|custom`): Low draws
  the Scene and Game views at 67 % and scales them up, with a 1024 shadow map, lighter Realtime GI and 60 fps;
  Medium is 85 % with a 2048 shadow map; High has no caps. They hold in the editor's views only, never in F12
  renders. The first run picks Low on a machine with 8 threads or fewer, or about 8 GB or less.
- **Viewport Resolution** (`viewscale 50..100`) and **CPU Threads** (`threads N`) in Preferences > Performance;
  `BLENDITY_THREADS` and `blendity_stress --threads N | --min-spec | --quality` emulate a smaller machine.
- **Cached shadows**: objects that stay still keep a shadow map of their own while another one is dragged.

**Changed**
- **The sky is drawn after the surfaces**, only where none landed, and only every 4th pixel where it is smooth. Its
  lookups use polynomial atan2 / asin. Pictures differ by at most one display step.
- **The views' shadow frame is snapped** (steps of 10 %), so texels can be up to about 1.2 times larger. F12 still
  fits exactly.
- **A material colour edit no longer redraws the shadow map.**
- **Jolt uses half of Blendity's threads.**
- **Memory budgets**: the texture cache drops its least recently used unheld textures past a quarter of RAM, and
  undo drops its oldest steps past an eighth (each at most 2 GB).

**Checked**
- New tests (round 72):
  - A ceiling of 3 holds a loop to 3 threads with TBB and with the built-in pool after its workers started.
  - At 50 % the Scene view draws 438 x 205 of 876 x 409, a click still selects the cube, and the scale is in the
    picture keys.
  - The tiers set scale, shadow map and fps; F12 keeps the scene's 2048.
  - Two tiers at the same scale but different caps are different pictures.
  - A moving caster drawn over the still casters' map equals the full map bit for bit. Six moved frames draw the
    still layer once.
  - The deferred sky matches a per-pixel evaluation within 1 display step.
  - The polynomial atan2 / asin are within 4e-7 of the library's.
  - The texture cache lets go of unheld textures past its budget and keeps a held one.
  - Undo keeps its newest steps within its budget, and they undo.
- Stress at 8 threads, Low, fastest of four runs:

  | Frame | Before (ms) | After (ms) |
  |---|---|---|
  | Orbit | 17.9 | 7.1 |
  | Moving an object | 47.2 | 15.5 |
  | Material colour edit | 48.1 | 19.8 |
  | Play | 39.3 | 17.6 |

  The material edit and Play are still over 16.7 ms; PERFORMANCE.md pass 8 says what is next. Peak working set with
  10,200 objects: 448 MB.
- Unit checks: Windows 9,336, 0 failed (570 tests); the whole suite in render-cache verify mode: 9,337,
  0 failed, 0 stale pictures. Linux with libraries: 9,246, 0 failed. Sanitizer build: 9,164, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review (code reviewer)**: five majors and several minor findings, fixed:
- The thread ceiling didn't hold in the dependency-free pool once its workers had started.
- The tiers' shadow and GI caps weren't in the views' picture keys, so a tier change could show a stale picture.
- F12 took the tiers' GI caps.
- Preferences from before the tiers lost the user's frame cap.
- Retro filter stacks were scaled twice and came out soft.
- Also fixed:
  - Edit Mode and modifier levels kept an object "moving" (motion is by placement now, with a still layer per view
    slot).
  - Snapping was coarse (now steps of 10 %, and none for F12).
  - A pending sky could outlive its frame.
  - A NaN depth could stall the sky fill.
  - The TBB arena could be freed while in use.

## 2026-10-12 (round 71): the macOS check, and merging the stack (task 0049)

**Asked:** "check for any errors on the GitHub side ... and merge them".

**Fixed**
- macOS CI failed on #45 and #46: a test compared the skinned mesh with the base mesh bit for bit after Apply as
  Rest; Apple clang's fused multiply-adds left one vertex one ulp off. The test compares within 1e-5 now (the
  editor was right).

**Changed**
- CLAUDE.md: agents merge only when the user asks, after CI passes, with a merge commit.

**Checked**
- The test on Windows; CI's four jobs on #46 before merging.

## 2026-10-12 (round 70): inverse kinematics and rig presets (task 0048)

**Asked:** the roadmap (2026-10-11): "IK/FK for humanoid and quadruped rigs", with name-based humanoid auto-map and
the gizmo refused on an IK-driven bone.

**Added**
- **IK constraints** (ADR 0025): an IKConstraint on a chain's end bone (a shin, a forearm) turns the chain so its
  tip reaches the Target object; two bones exactly (law of cosines) bending towards an optional Pole, longer chains
  by cyclic coordinate descent (Iterations), Weight from the rest pose to the solution. Solved every editor frame
  and in Play after the scripts, only when something it depends on moved; the solved pose makes no undo step. The
  gizmo and G / R / S refuse a bone IK turns ("move its target instead"). Console `ik add <bone> <target> [pole]
  [chain]`.
- **GameObject > Rig**: Humanoid (Hips, Spine, Chest, Neck, Head, arms, legs) and Quadruped (spine, neck, head,
  tail, four legs) with IK targets on hands and feet and poles at knees and elbows; **Auto IK on Selected
  Skeleton** finds the limbs of an imported Mixamo (`mixamorig:LeftUpLeg`), Unity (`LeftUpperLeg`) or Rigify
  (`thigh.L`) skeleton by name. Console `rig humanoid|quadruped|autoik [root]`.
- Learn > Modifiers: "Inverse kinematics".

**Fixed**
- The GameObject menu's Lattice and Armature items had each other's tooltips.

**Checked**
- New tests (round 70): two bones reach a target within 1.3e-7, point straight at one out of reach, and with a pole
  the knee lies in the plane of hip, target and pole; the skin follows; nothing moved, nothing solved; five bones
  by CCD within 7.7e-4; Reset Pose solves again; Weight 0 keeps the rest; a target the chain carries is refused;
  the presets make the named bones and 4 IK chains each, lifting a foot target bends the knee forward with the
  foot on it; auto IK finds 4 limbs in Mixamo, Unity and Rigify skeletons and adds nothing the second time; in the
  editor, dragging a foot target with the gizmo poses the leg, the gizmo leaves a driven shin alone, undo puts the
  target and the leg back, `ik add` and a scene file keep the constraint; in Play a Rotator swinging a target
  animates the leg and Stop restores it; a second rig has its own targets; a target that circles back leaves no
  twist; Weight 0.5 doesn't add up over many moves; a pole the chain carries is refused; a lost target doesn't hold
  the bone; G on a driven shin starts nothing while G, R on the head turns it.
- Stress `vgroups`: a humanoid's four chains cost 0.006 ms a frame when every target moves, 0.001 ms
  when nothing does; among 20,000 other objects 0.046 ms (the per-frame pass); no IK in a scene: no walk.
- Unit checks: Windows 9,303, 0 failed (562 tests); the whole suite in render-cache verify mode: 9,304,
  0 failed, 0 stale pictures. Linux with libraries: 9,213, 0 failed. Sanitizer build: 9,131, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review (code reviewer)**: three majors and several minor findings, fixed: a second preset rig's limbs reached for
the first rig's targets (names now unique); each solve started from the last one, so a circling target wound up
twist and a Weight under 1 added up frame by frame (now from the rest pose); a pole parented under the chain
re-solved every frame; the per-frame pass walked every object of every scene and looked targets up by name (now
nothing with no IK anywhere, ids between frames); G / R / S weren't refused on a driven bone; a constraint with a
lost target still blocked the gizmo; a straight chain without a pole could bend sideways (now the rest bend, then
the bone's own Z); auto IK's poles were world +Z / -Z (now where each limb bends). Left as it is: the undo snapshot
is taken after the frame's solve, harmless now that solving doesn't depend on the previous pose.

## 2026-10-12 (round 69): skeletons and skinning (task 0047)

**Asked:** the roadmap (2026-10-11): "rigging ... v1: import rigs from FBX/glTF and pose them".

**Added**
- **Bones and armatures** (ADR 0025): a bone is a GameObject with a Bone component (Length, Deform, its rest
  transform) under a root with an Armature (Deform: Linear or Dual Quaternion, Show Bones, Rest Position). Bones
  are drawn as Blender's octahedra and picked by clicking them; pose them with the gizmo. GameObject > Armature
  (a selected mesh is bound to it), `bone add <parent> [length]`, `armature reset|apply` (Reset Pose, Apply as
  Rest).
- **The Armature (Skin) modifier**: moves the mesh with the bones its vertex groups are named after, 4 influences,
  Linear or Dual Quaternion (twisted joints keep their girth), deform-only (task 0041). **Bind: Automatic Weights**
  (`skin bind`): groups from each vertex's distance to the bones.
- **Rigged FBX and glTF import**: bones, hierarchy, weights (normalised), an Armature and a Skin modifier.
- Learn > Modifiers: "Bones and skins".

**Checked**
- New tests (round 69): a two-bone tube is unchanged at rest and turning the upper bone 90 degrees turns its
  vertices about the joint exactly (2.7e-7) while the lower ones stay; a 150 degree twist keeps the joint's radius
  at 0.200 with Dual Quaternion where Linear shrinks it to 0.052, and both agree at rest and for single-bone
  vertices; Blender's glTF and FBX two-bone tubes import with Lower and Upper bones, an armature, weights summing to
  1, unchanged at rest, and posing Upper moves its vertices and not Lower's; GameObject > Armature binds a column,
  `bone add` places a child at its parent's tip, `skin bind` gives weights summing to 1, a pose is one undo step and
  is kept by a scene file, Reset Pose and Apply as Rest; Reset Pose on a root that is a bone; a rest near 90 degrees
  stays exact through a file; an unevenly scaled mesh keeps Dual Quaternion's girth; a mesh under the armature
  follows it, and the Skin's own vertex group limits it; automatic weights give every vertex away from the joint to
  its nearest bone.
- Stress `vgroups`: a pose change on 50,176 vertices with 3 bones, 5.5 ms Linear, 5.6 ms Dual Quaternion (target 8).
- Unit checks: Windows 9,232, 0 failed (555 tests); the whole suite in render-cache verify mode: 9,233,
  0 failed, 0 stale pictures. Linux with libraries: 9,142, 0 failed. Sanitizer build: 9,060, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review (code reviewer)**: five majors and several minor findings, fixed: Reset Pose and Apply as Rest skipped a
root that is a bone; rest rotations kept as Euler angles lost up to 0.8 degrees near 90 (now a quaternion); the ADR
said bind poses were read (it now says they aren't, and an FBX saved in another pose is reported); in an FBX with
two characters every mesh took every skin's bones (now its own); making the armature's name unique could rename a
bone (the armature is never a bone now). Also: Dual Quaternion fell back to Linear on an unevenly scaled mesh;
a zero rest scale made vertices jump; hidden bones were still picked and In Front did nothing; GameObject >
Armature stacked a second Skin; glTF welded touching vertices with different weights. Left as they are (in the
spec's As built or for later): renaming a bone doesn't rename its vertex group; duplicate joint names.

## 2026-10-12 (round 68): Weight Paint (task 0046)

**Asked:** the roadmap (2026-10-11): "weight painting", the second half: the brush.

**Added**
- **Weight Paint mode** (MeshFilter > Vertex Groups > Weight Paint, or `weightpaint on`): the chosen group of the
  active mesh shows as Blender's weight colours (blue 0, green 0.5, red 1) and dragging in the Scene view paints it.
  Brush: Add, Subtract, Draw (towards Weight), Blur (towards the neighbours); Radius (screen pixels), Strength,
  Weight, Falloff (Smooth, Sphere, Root, Sharp, Linear, Constant), Mirror X, Front Faces Only. One undo step per
  stroke. Normalize All and Clean. Console: `wpbrush`, `wpfalloff`, `wpmirror`, `wpfront`, `wpstroke x y ...`,
  `wpnormalize`, `wpclean`.
- Learn > Modifiers: "Weight painting".

**Checked**
- New tests (round 68): Add raises the weights under the brush only, Subtract lowers them, Draw reaches Weight; the
  Smooth falloff paints the centre more than the rim; Blur evens a hard edge; Front Faces Only leaves a sphere's
  back alone (and off, reaches it); Mirror X gives the twin the same weight; a real mouse drag there and back is one
  stroke, one undo step, and stays within one dab's strength (also along the mirror line); leaving the mode keeps
  the weights; the view shows red at weight 1 and blue at 0 (pixels); the painted group stays the painted object's
  when another object is selected; a scene file keeps the weights; a new scene leaves the mode; Normalize All,
  Clean.
- Stress `vgroups`: a dab on a 50,176-vertex mesh 0.08 ms (target 8); a stroke's press and release 3.8 ms.
- Unit checks: Windows 9,194, 0 failed (549 tests); the whole suite in render-cache verify mode: 9,195,
  0 failed, 0 stale pictures. Linux with libraries: 9,104, 0 failed. Sanitizer build: 9,022, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review (code reviewer)**: four majors and several minor findings, all fixed: an undo, a new scene or Play in the
middle of a drag let the stroke write into meshes the undo history shared (a stroke now belongs to its scene and
mesh and stops otherwise); a new scene kept painting whatever object had the same id; choosing another group showed
a stale picture; selecting another object could switch the painted group; every click made an undo step (only a
stroke that changes weights does now); Mirror X applied twice near the centre line; strokes accumulated past their
strength (Blender's no-accumulate rule now); Normalize All moved the active group (Lock Active now); the X-mirror
map could stall on a stray far vertex (an edge-length tolerance, and cached); Esc leaves the mode.

## 2026-10-12 (round 67): vertex groups (task 0045)

**Asked:** the roadmap (2026-10-11): "weight painting", the first half: the groups it paints.

**Added**
- **Vertex groups** (ADR 0024): MeshFilter > Vertex Groups in the Inspector (Add, Remove, rename; in Edit Mode a
  Weight with Assign, Remove from, Select, Deselect) and the console `vgroup add|remove|rename|assign|unassign|
  select|deselect|active`. Saved in scene files (`vg` lines), kept by undo.
- **Every deforming modifier** (Displace, Simple Deform, Smooth, Cast, Curve, Lattice, Mesh Deform) has **Vertex
  Group** and **Invert Group**: only that group's vertices move, each by its weight.
- The groups follow the vertices through every edit: welds and merges, Mirror, Array and Join carry them exactly;
  any other change of the vertex count mends them by position (a vertex that stayed keeps its weights, a new one
  takes the surface's at its place), at each undo step and after each generative modifier of a stack.
- Learn > Modifiers: "Vertex groups".

**Checked**
- New tests (round 67): 17 Edit Mode operators (merge, delete, dissolves, subdivide, loop cut, bevel, extrudes,
  inset, triangulate, poke, split, duplicate, quads, collapse, connect, edge split) keep one weight per vertex,
  vertices that stay keep both groups' weights, and new vertices on the surface carry a linear ramp exactly; adding
  a second group and removing one survive the undo commit; Separate keeps each part's weights and Join through the
  editor brings the other object's group by name; Apply Modifiers bakes a group-limited Displace as shown; Mirror
  copies give their sources' weights, Array and simple Subdivision carry them, Join merges groups by name; a Displace limited to a group moves each vertex by its weight, Invert the rest, an
  unknown name the whole mesh; groups round-trip through a scene file, old files load without them, the console
  adds, assigns (0.75), renames and undo takes each back.
- New stress section `vgroups`: 37 Edit Mode operators (random vertex or face selections) and 10 generative
  modifiers (each followed by a group-limited Displace) on the 26 stress meshes with groups: 1,222 operations, 0
  groups of the wrong size, 0 weights not finite or out of 0..1, 0 weights moved off a vertex that stayed. Carrying
  two groups through Subdivision Surface on 25k vertices: 1.8 ms a re-run (48 ms once, building the map).
- Push/Pull: 26,620 operations, 0 problems, 394 results with faces newly on top of each other (unchanged);
  `modifiers_ngon` 11,514 operations, 0 problems.
- Unit checks: Windows 9,151, 0 failed (543 tests); the whole suite in render-cache verify mode: 9,152,
  0 failed, 0 stale pictures. Linux with libraries: 9,061, 0 failed. Sanitizer build: 8,979, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review (code reviewer)**: a blocker, five majors and several minor findings, all fixed: the undo commit's mending
undid Add and Remove group and lost a joined object's groups (it now only mends groups an operator left the wrong
size); mending by position overwrote Mirror's and Array's exact copies; Separate dropped the groups; Apply of one
modifier ignored its vertex group; carrying groups through a generative modifier cost 89 ms every frame of a drag
(now a kept map: 1.8 ms); the tests couldn't tell carried weights from re-sampled ones and the stress selected no
faces; the search could stop early for points outside the old mesh, a far point overflowed an int, a deformer's
NaN reached weight-0 vertices, saving before the commit dropped the groups (save commits first).

## 2026-10-12 (round 66): the Mesh Deform modifier, a cage of any shape (task 0044)

**Asked:** the roadmap (2026-10-11): "a Cage (lattice / mesh-deform) tool".

**Added**
- **Mesh Deform** (Add Modifier > Deform): set Object to a closed mesh round this one and press **Bind** (or
  `meshdeform bind [object]`); then moving the cage's vertices shapes the mesh smoothly. Binding uses mean value
  coordinates (Ju, Schaefer and Warren 2005; ADR 0023), closed form, so it takes a moment instead of Blender's
  voxel solve. An open, flat or missing cage is refused with the reason; a cage whose vertex count changes stops
  the deform and says "Bind again". Deform-only, so dragging cage vertices updates in place.
- Learn > Modifiers: "A cage round a mesh".

**Checked**
- New tests (round 66): right after Bind a sphere in a cube cage and in an icosphere cage is unchanged; the weights
  sum to 1 (off by 1e-7); moving and turning the whole cage moves the mesh the same way (off by 7e-8); pulling one
  cage corner moves the mesh near it 18 times more than the far side; an open cage and a flat one are refused,
  Unbind leaves the mesh, an extra cage vertex stops the deform with "Bind again"; a cage wound the other way binds
  and reproduces a stretch, one with a face turned round or doubled is refused, a stray far vertex doesn't spoil the
  volume test; weights at a cage vertex, on a face and outside the cage reproduce the point; two meshes caging each
  other end without a crash; editing the bound mesh gives what a fresh Bind would; the binding round-trips through
  a scene file; in the editor a refused Bind says why, Bind is one undo step, and undoing an Unbind binds again.
- Stress `modifiers_ngon`: Mesh Deform on all 26 meshes with cages moved, NaN, huge and flattened after Bind:
  11,514 operations, 0 problems. Binding 20,002 vertices to a 1,280-triangle cage: 97 ms (target 200); each deform
  0.8 ms (target 5).
- Unit checks: Windows 9,126, 0 failed (537 tests); the whole suite in render-cache verify mode: 9,127,
  0 failed, 0 stale pictures. Linux with libraries: 9,036, 0 failed. Sanitizer build: 8,954, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review (code reviewer)**: a blocker, a major and several minor findings, all fixed: two meshes caging each other
recursed until the stack overflowed (the cage is now hashed as the Boolean's cutter is, without evaluating its
keys); editing the bound mesh rebuilt every weight on every drag frame (rows are now redone only for vertices that
moved); a stray cage vertex could fail the volume test, which also lost precision far from the origin; the binding
text was copied and hashed byte by byte in every evaluation (the change hash now sees a key); a binding read from a
file wasn't checked; a point on a cage face missed the on-face case by rounding (its tolerance is now 1e-6).

## 2026-10-12 (round 65): lattices (task 0043)

**Asked:** the roadmap (2026-10-11): "a Cage (lattice / mesh-deform) tool".

**Added**
- **Lattice objects** (GameObject > Lattice, also in the Hierarchy's + menu): a box of points fitted round the
  selected mesh, which gets a **Lattice modifier** naming it. Points U / V / W (2 to 32, changing them keeps the
  shape), Linear or B-Spline; the modifier has a Strength. Click a point to select it and move, turn or scale it
  with the gizmo (Shift adds, Esc lets go). The modifier is deform-only, so dragging a point updates the mesh in
  place (task 0041).
- Curve and lattice points are now one kind of thing to the editor (`Component` control points); the console has
  `points select|move <object> ...` for both (`curve` still works).
- Learn > Modifiers: "Squashing with a lattice".

**Checked**
- New tests (round 65): a lattice at rest leaves a mesh as it is (both interpolations, three resolutions); Linear:
  a corner moved by d moves the mesh's corner by d, the opposite corner not at all, the centre by d / 8, Strength
  0.5 half as far; B-Spline: pulling an inner point never moves neighbouring samples by more than the pull times
  their spacing over the cell size; changing the resolution keeps an affine pull (within 1e-3); NaN, flattened,
  missing and self-referencing lattices leave a valid mesh; a turned, stretched lattice on a turned, scaled object
  bends the mesh as the same lattice would in world space (off by 1e-7), and moving it re-bends; a B-Spline
  lattice keeps its points through resolution changes 2 to 10 and back, and outside its box blends on without a
  crease; GameObject > Lattice fits the box and adds the modifier, a click selects a point, a gizmo drag moves
  only it and reshapes the mesh, Delete then removes the lattice, undo restores each step, `set` reaches the
  resolution and any point, a second lattice is Lattice.001 and its modifier names it, and a scene file keeps
  everything.
- Stress `modifiers_ngon`: lattice cases on all 26 meshes (both interpolations, three resolutions, ordinary, NaN
  and huge offsets, a flattened box): 11,384 operations, 0 problems. A B-Spline 4 x 4 x 4 lattice deforms a 50k-
  vertex mesh in 0.5 ms (target 3 ms).
- Unit checks: Windows 9,083, 0 failed (531 tests); the whole suite in render-cache verify mode: 9,084,
  0 failed, 0 stale pictures. Linux with libraries: 8,993, 0 failed. Sanitizer build: 8,911, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review (code reviewer)**: two majors and several minor findings, all fixed: changing a B-Spline lattice's
resolution sampled it as a B-spline and shrank the shape a little every time (now sampled linearly, as Blender
does); a second lattice had the same name, so its modifier found the first (now Lattice.001); outside the box
B-Spline held the face value (a crease) where Blender blends on; a click on a mesh inside its lattice could select
the lattice (only the box's outer edges pick it now); the flattened-box test was absolute (now relative to the
box's size); Delete with a lattice point selected did nothing; the timing row subtracted two noisy timings.

## 2026-10-12 (round 64): curve objects, the Curve modifier and Array along a curve (task 0042)

**Asked:** the roadmap (2026-10-11): "Curves: objects follow a path (Blender's Curve modifier and Array-along-curve)".

**Added**
- **Curve objects** (GameObject > Curve > Bezier, Circle, Path, also in the Hierarchy's + menu): Bezier (Blender's
  Auto handles), Catmull-Rom or Poly; Closed; Resolution; per point a Tilt and a Radius. Drawn in the Scene view
  (orange when selected); click the line to select the curve, click a point to select it (Shift adds, Esc lets go),
  and the Move / Rotate / Scale gizmo moves the selected points, one undo step per drag. Console:
  `curve add|move|delete|select <object> ...`, and `set Curve.P2Position x y z`.
- **Curve modifier** (Deform): bends a mesh along a named Curve object on any of six axes; the mesh's origin sits
  at the curve's start, past an open curve's end the mesh goes on straight, a closed one wraps round. It is
  deform-only, so dragging its curve updates the mesh in place (task 0041).
- **Array: Fit Type** Fixed Count, Fit Length and Fit Curve (as many copies as fit the curve's length).
- Learn > Modifiers: "Bending along a curve".

**Checked**
- New tests (round 64): a straight curve of each type leaves a beam unchanged; a quarter circle bends a beam onto
  the arc (radius off by 2.5e-6, length along it kept); a closed curve wraps a beam round and an open one goes on
  straight past its end; Fit Curve gives 5 then 7 then 14 copies as the curve grows and is scaled, Fit Length 3,
  NaN or a missing curve one; moving a point or the curve object re-bends, a curve naming its own object or
  nothing leaves the mesh; NaN, huge and coincident points on every type, closed or not, and every axis give a
  finite mesh; a scene file keeps everything; in the editor a click selects the curve, then a point, the Move
  gizmo moves only that point and undo puts it back; moving points and the curve leaves no stale picture; a
  straight curve along each of the six deform axes changes nothing, Tilt 90 turns the cross-section a quarter turn
  and Radius 2 doubles it; the Circle preset is round within 0.3 %; a closed trefoil knot's frame meets itself;
  Delete with points selected removes the points, and selecting another object lets go of them.
- Stress `modifiers_ngon`: the Curve modifier on all 26 meshes along five ordinary and hostile curves, every type,
  closed or open, every axis: 10,760 operations, 0 problems.
- Unit checks: Windows 9,034, 0 failed (523 tests); the whole suite in render-cache verify mode: 9,035,
  0 failed, 0 stale pictures. Linux with libraries: 8,944, 0 failed. Sanitizer build: 8,862, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

**Review (code reviewer)**: approved, with minor findings fixed: Array's Fit Curve no longer re-evaluates when the
curve is only moved or turned (just its scale counts), so dragging a curve under "Array then Curve" stays on the
in-place path; a curve's path is sampled once per change (the Scene view used to sample every curve every frame)
and hashed straight from its fields; a point selection is dropped when another object is selected, and Delete
removes the selected points; the Circle preset has 8 points (4 bulged in by 4 %); `curve` is in the console's help.
Left as they are: point clicks win over a mesh in front within 9 pixels; V and Ctrl+Shift snapping don't apply to
curve points yet.

## 2026-10-12 (round 63): deforming modifiers update in place (task 0041)

**Asked:** the roadmap (2026-10-11): curves, lattices, cages and skinning all deform meshes, and dragging their
fields must stay interactive ("make sure the editor is still decently responsive").

**Changed**
- Smooth, Displace, Simple Deform and Cast only move vertices. At the end of a stack they now re-run on a kept copy
  of the mesh and the render mesh moves its positions, normals and tangents in place, instead of a copy, a full
  rebuild and new tangents on every change. A field drag on a 50k-face mesh went from 56 ms to 6.6 ms per change.
  Modifiers before them (Subdivision, Mirror, ...) are evaluated once and kept while only the deformers change.

**Checked**
- New tests (round 63): on a cube, a UV sphere, a 50k-face grid, a mesh with sharp edges and one with UV seams,
  each of the four deformers, three changes each: the in-place result equals the full evaluation (positions and
  normals bit for bit; tangents unit length and at right angles); only the tail re-runs and the render mesh keeps
  its build, a generative modifier or Edit Mode's cage rebuild in full; a flat grid made bumpy rebuilds then
  follows in place; a scene snapshot keeps its shape when the original changes again; a Boolean re-cuts when its
  cutter is deformed; tangents are the same whatever drag led to a shape (and within 18 degrees of MikkTSpace on a
  twisted cylinder); a saved and reloaded scene evaluates the same; in the editor a drag stays in place and
  re-renders the view, the shadows and voxel GI with no stale picture, and undo brings the old shape back exactly.
- Stress `modifiers_ngon`: Simple Deform angle on a 50,176-face mesh 6.6 ms per change, 8.3 ms with sharp edges
  (in full 56 ms); 6,080 operations on odd meshes, 0 problems.

**Review (code reviewer)**: two majors and several minor findings, all fixed: a Boolean whose cutter was deformed
kept its old cut (the cutter's mesh is now the same object, so its dependency mixes `deform_version`); tangents
were projected from the previous frame's and drifted with the drag (now from UV gradients); `deform_version` moved
only after the deformers ran; tests for voxel GI, save / load, the Boolean and tangents; a sharp-edges stress row.
- Unit checks: Windows 8,975, 0 failed (511 tests); the whole suite in render-cache verify mode: 8,976,
  0 failed, 0 stale pictures. Linux with libraries: 8,885, 0 failed. Sanitizer build: 8,803, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

## 2026-10-12 (round 62): axis materials by height with a noisy border; deleting textures (task 0040)

**Asked:** "procedural ... work based on global position. Like the top of a cliff where it's grassy but over the edge
it turns into dirt and rock" ("if it's quick, then do it"); "provide the option to delete textures in the texture
tab"; "Making a new material in the material tab doesn't actually show the new material."

**Added**
- **Axis High** on axis materials: a material above **High Start** (world height in World mode, the object's own in
  Local), faded over **High Blend**; and **Axis Noise** / **Noise Scale**, which make the grass-to-dirt and the
  high band's borders wander. The same in the views, CPU and GPU path tracing and bakes. Learn > Materials: the
  terrain section.
- **Deleting textures** in the Textures window: a Delete button, the thumbnail's right-click menu, or Del; it asks
  first, says how many materials use it (they then show it missing), and moves the file to the Recycle Bin.

**Fixed**
- New Material Asset (and New in the Textures window) with text left in the search box made something the grid
  filtered out, so it looked as if nothing happened: New now clears the search, and a search says how many items
  it hides ("Showing 1 of 15").

**Checked**
- New tests (round 62): above the band snow, below it the slope's parts, a blend between, nothing without it;
  Local mode by the object's height; noise 0 gives the slope rule exactly, noise makes the border wander (107 grass,
  92 dirt at the border angle) and is the same at the same point; NaN fields and positions stay finite; a tilted
  plane is 29.3 % snow rasterized, 30.0 % on the CPU, 30.1 % on both GPUs (checked to be the GPUs); the fields
  round-trip, an old .mat loads with the defaults, the pickers set every part; Delete asks first then trashes the
  texture and a material using it shows it missing; New clears a leftover search.
- Stress `shading`: with height and noise, every point near a border, 2.2x a plain material (the spec's 2x is
  2.5x for this worst case; see its As built); task 0029's axis row 1.5-1.6x. Both are now the fastest of five
  interleaved rounds against a plain run: single timings swung 1.5-2.4x for the unchanged 0029 row.

**Review (code reviewer)**: a blocker, a suspected major and several minor findings, all fixed: Axis High couldn't
be picked in the UI (the setter knew three parts); the GPU test could pass on a CPU fallback (it now checks the
device); a NaN height on the GPU; noise evaluated at NaN; weak tests; the delete popup's count (now unique
materials, assets included); undo could write a deleted texture's text into a new one of its name; unsaved texture
edits not saved before the trash.
- Unit checks: Windows 8,880, 0 failed (503 tests); the whole suite in render-cache verify mode: 8,881,
  0 failed, 0 stale pictures. Linux with libraries: 8,790, 0 failed. Sanitizer build: 8,712, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

## 2026-10-11 (round 61): anime and manga camera filters (task 0039)

**Asked:** the rest of "anime / 2D looks like Goo Engine (toon shading, outlines, halftone, ...)" (the roadmap's
item 6, second half).

**Added**
- Camera filters: **Halftone** (a dot screen, Mono ink on paper or Colour, the same dots at any resolution),
  **Hatching** (one to three line directions switched on by darkness), **Line Art** (silhouettes, object edges,
  creases by angle, Taper), **Paper** (blotches, fibres and grain) and **Color Ramp** (brightness through 2 to 4
  colours, Constant or Linear).
- **Looks**: Add Filter > Looks > Anime (Line Art, saturation, a soft Bloom) and Manga (Grayscale, Halftone, Line
  Art, Paper); `filter look Anime|Manga`. One undo step.

**Asked during the round:** "in the material tab and inspector it says the texture is either non color or missing."
And: "When I have a procedural map (bricks) and I have a color ramp that is based on height. It only works when height
map is enabled even though I just want the color ... Maybe add a toggle with the texture window to only enable or
disable certain maps."

- **Normal Map, Roughness Map and Height Map switches** on a procedural texture (the Textures window, below the layer stack): off, that
  output is neutral (a flat normal, roughness 1 so the material's Roughness rules, mid height) and Assign to
  Selection leaves it out, while the layers still compute the height inside, so a Color Ramp from the height
  colours the bricks without them also bumping the surface. Live shading follows the switches too.

- **Bake Textures in the Hierarchy's right-click menu** too (as GameObject > Bake Textures), as the user asked.

**Fixed**
- **A material's texture field said "(missing)" for a procedural texture** (Bricks.ptex#albedo is no file): it
  now shows the texture's image and its name ("Bricks (albedo)"), and its picker lists every procedural texture's
  outputs beside the images. Their Non-Color tag and colour-space switch are gone (an output's colour space is its
  own). The renderers always used them; only the field was wrong.
- Path-traced pictures gave the filters (Edge Outline, Line Art) depth from a random sample per pixel; now one ray
  through each pixel's centre, so their lines match the rasterizer's.
- The Add Filter menu showed two Color headings (Color Ramp was listed after the Stylize effects).

**Checked**
- New tests (round 61): halving Halftone's cell gives 3.9 times the dots and 540p and 1080p show the same 14,528;
  white stays white and black solid; Hatching's ink falls with brightness; Color Ramp Constant gives exactly 3
  colours; Paper at 0 changes nothing and is the same twice; Line Art draws a cube's edges rasterized and traced
  (the traced lines all within a pixel of the rasterized ones), a 30-degree fold at 20 and not at 40, nothing on
  a smooth sphere; the looks add their effects in order, undo, round-trip, and an unknown look adds nothing.
- Stress `filters`: each pass at 1080p on 8 threads against Edge Outline (about 10 ms): Halftone 0.6-0.9x,
  Hatching 0.8x, Line Art 1.8-2.5x, Paper 0.1x, Color Ramp 0.05x (the spec's flat 4 ms is below what Posterize
  costs; see the spec's As built); the fuzz of every effect with NaN / huge fields, 0 problems.

**Review (code reviewer)**: three major and several minor findings, all fixed but one noted: grey specks in pure
white and black halftone; noisy traced creases from jittered depth; Paper's cache holding one size (now four);
an unknown look adding an empty stack; scratch memory that never shrank; Taper on the sky side; dots sized from
one pixel. Noted: these colour passes veil a transparent film's background, as Invert and Posterize do.
- Unit checks: Windows 8,850, 0 failed (497 tests); the whole suite in render-cache verify mode: 8,851,
  0 failed, 0 stale pictures. Linux with libraries: 8,762, 0 failed. Sanitizer build: 8,684, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures; `filters` and `textures` stress 0 problems.

## 2026-10-11 (round 60): toon shading and outlines (task 0038)

**Asked:** "Anime / 2D looks like Goo Engine (toon shading, outlines, halftone, ...)" (the roadmap's item 6, first half).

**Added**
- **Toon shading model** (a material's Shading Model, or the Toon preset): light in Toon Bands flat tones with a
  hard (or Softness) edge at the Threshold, a Shadow Color for the dark side, a hard highlight spot, a rim light
  and a flat ambient. The same tones in the views, F12 on the CPU and the GPU, and Live Preview; bounces off toon
  surfaces stay ordinary (Lambert), so lighting bakes are unchanged.
- **Outline** component: the black line round an object (the inverted hull), in the views, path-traced renders
  (camera rays only) and exports; Thickness in metres or object units, a colour.
- Learn > Materials: *The anime look*, with a Try-it.

**Fixed**
- Objects with a negative scale (mirrored) were drawn inside out by the rasterizer.

**Checked**
- New tests (round 60): tones step through exactly Toon Bands levels; a rasterized toon sphere takes 2 tones with
  1 band and 4 with 3 (PBR: the 4 commonest colours cover 28 %); the CPU trace's tone shares are within 0.7 % of
  the rasterizer's, 3.1 % under a large area light; the GPU matches the CPU; the rim lights only the silhouette;
  an Outline adds a ring of its colour round the sphere and changes none of its pixels, rasterized and traced,
  mirrored too; thickness 0 draws nothing; the shell is pushed out with flipped faces, cached, world-space
  thickness; an outlined object with a modifier traced in the Camera Preview while the Scene view redraws; undo
  of the Outline and of a toon field; the fields round-trip through a .mat and the Outline through a scene.
- Stress `shading`: a toon material shades in 8.4 ms at 1080p against PBR's 10.4 ms (spec: at most +15 %).
- The GPU kernel came within reach of the SPIR-V optimiser's id limit (a second inlined shadow-ray call
  overflowed it): the toon light branch shares the PBR one's shadow ray.

**Review (code reviewer)**: a blocker, three major and several minor findings, all fixed but one noted: the
outline shell had one cache slot for several views (a path tracer could keep a freed one); mirrored objects drew
the hull's near side; the toon response was applied at bounces and in bakes; the rasterizer's toon ignored an area
light's facing; falloff scaled tones instead of being banded; flat ambient ignored occlusion; tests for
mirroring, modifiers, undo and area lights; the ADR now records the component and the camera-only hull. Noted:
an outline makes shadow rays use the closest-hit loop.
- Unit checks: Windows 8,756, 0 failed (490 tests); the whole suite in render-cache verify mode: 8,757,
  0 failed, 0 stale pictures. Linux with libraries: 8,668, 0 failed. Sanitizer build: 8,590, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures; `shading` and `gpu` stress 0 problems.

## 2026-10-11 (round 59): GPU devices open in the background (task 0037)

**Asked:** "Switching from shaded to rendered and path trace preview crashes/halts the program" ... "make sure the
editor is still decently responsive" (the retro stages stay in the path tracers).

**Fixed**
- **Turning the Camera Preview (or the Render window, Live Preview, a bake) to the GPU froze the editor** the first
  time in a session: the driver built the kernel's pipeline on the editor's thread, about 80 s with a cold cache,
  and Windows closed Blendity as not responding. Now the GPU opens in the background: the view traces on the CPU
  ("GPU starting") and moves to the GPU when it is ready; bakes do those slices on the CPU; the editor wakes four
  times a second meanwhile. A spare stays open so the next view gets a GPU at once.

**Added**
- A pipeline cache per GPU in `~/.blendity/cache` (checked against the GPU and a hash): later starts build the
  pipeline from it even when the driver's cache is cold (9-13 ms instead of 84-141 ms here with a warm one).

**Checked**
- New tests (round 59): a GPU that doesn't exist is refused at once; with the open slowed to 3 s the slowest
  editor frame is 17 ms, the preview traces on the CPU and is on the GPU after 3.3 s, and the spare is handed over
  at once; a tracer forgets a pending GPU when the device turns back to the CPU; a bake's gather returns at once
  while the GPU opens and uses it later; the pipeline cache is written and used, and corrupt, torn and foreign
  files are ignored. The tests keep their caches in a scratch folder.

**Review (code reviewer)**: four major and several minor findings, all fixed: the wake-up was cleared later in the
same frame; quitting while a device opened could crash in static destructors (now leaked statics and `_Exit`);
the GPU bakes still opened a device on the editor's thread; "GPU starting" stuck after switching to the CPU;
the cache file had no body check and a shared temporary name; a failed open was never retried; any other view's
open rebuilt a waiting tracer's scene; prewarm ignored the RT setting; the tests wrote the user's cache folder.
- Unit checks: Windows 8,714, 0 failed (480 tests); the whole suite in render-cache verify mode: 8,715,
  0 failed, 0 stale pictures. Linux with libraries: 8,627, 0 failed. Sanitizer build: 8,549, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures; `gpu` stress 0 problems.

## 2026-10-11 (round 58): bake textures onto a mesh for export (task 0036)

**Asked:** "Bake textures onto a mesh" for export (the roadmap's item 5), one atlas per object.

**Added**
- **Object > Bake Textures** (512 to 4096; console `baketex [size] [keepuvs]`): the active object's look (image
  maps, procedural and `.ptex` textures, axis materials, Box and Generated mapping, every material slot) baked into
  albedo, roughness, metallic and normal PNGs in `Assets/Textures/Baked`, on new UVs (Smart UV Project and Pack
  Islands); the mesh takes those UVs and a new `<Object> Baked` material on every slot. One undo step. Objects
  whose modifiers add geometry are refused with a message (apply them, or export with Bake Textures).
- **Export > Bake Textures** (OBJ, FBX, glb / glTF, with a size): each exported object baked on a copy, the
  images written beside the file and referenced; the scene is not changed.
- **Exports carry more maps** without a bake: glTF normal textures and roughness / metallic packed into one PNG in
  the buffer; FBX Texture and Video objects for base colour, normal, roughness, metallic and emission maps.

**Fixed**
- OBJ exports wrote a procedural texture's output (`X.ptex#albedo`) as a file path when it hadn't been converted.
- `CMakeLists.txt` didn't compile `src/texture` (the build scripts and CI did).

**Checked**
- New tests (round 58): six materials on a cube's faces bake their colours at the face centres on one atlas; a
  checker on a plane matches the renderer at all 64 cell centres; an axis material bakes grass on top and dirt on
  the sides, rotated too; a flat surface's normal map is within 0.3 degrees of +Z and a Bricks texture bakes its
  mortar; Object > Bake Textures keeps the rasterized look (0.02 % mean colour difference) and undo restores the
  UVs and material; OBJ, glb and FBX exports with Bake Textures write and reference the images (ufbx reads the
  FBX textures); ordinary materials' maps reach glTF and FBX; Keep UVs; NaN, empty and collapsed meshes bake
  nothing; two objects with one name get their own files; a Mirror modifier is refused; Opaque alpha bakes solid
  and a Transparent slot gives the baked material its surface.
- Opened in Blender 5.2: the OBJ, glb and FBX import with Base Color, Roughness, Metallic and Normal linked to
  the baked 256 x 256 images.
- Stress `texbake` (new): 50k faces at 2048 in 506 ms on 8 threads (spec: under 2 s), 441 ms on 32, 4096 in
  968 ms; 60 fuzzed bakes (NaN, huge, collapsed meshes, odd sizes) 0 problems. A 4096 bake from the menu holds
  the editor for about a second.

**Review (code reviewer)**: four major and several minor findings, all fixed: an Opaque material's alpha made the
baked object a Cutout; a bake overwrote another same-named object's images (now new files each time); the in-scene
bake ignored modifiers that add geometry (now refused); tracked stress reports deleted by a cleanup (restored);
mirrored objects' normal maps in exports; the Bake option shown with Materials off; Unlit and emission dropped;
packed roughness read as linear from sRGB maps; collapsed triangles baked.
- Unit checks: Windows 8,693, 0 failed (476 tests); the whole suite in render-cache verify mode: 8,694,
  0 failed, 0 stale pictures. Linux with libraries: 8,625, 0 failed. Sanitizer build: 8,547, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures; `texbake` and `textures` stress 0 problems.

## 2026-10-11 (round 57): transparent backgrounds in renders (task 0035)

**Asked:** "Add the ability to make backgrounds transparent when they are rendered (png transparency)."

**Added**
- **Transparent** (Render window; Blender's Film > Transparent): F12 renders, rasterized or path traced on the CPU
  or the GPU, leave the world out of the background while it still lights the scene. A saved PNG has alpha 0
  there, partial alpha at anti-aliased edges and behind see-through surfaces, 1 on solid objects; an EXR keeps the
  alpha too; a JPEG is composited over black. The Render window shows a transparent render over a checkerboard.
  Camera sequences save the same way.

**Checked**
- New tests (round 57): rasterized and path-traced (CPU and GPU) renders of a cube with no floor have alpha 0 at
  the corners, 255 in the middle and partial values at the edges; a 50 % see-through quad gives alpha 127 (raster)
  and 128 (traced); the PNG reads back with the alpha; off, every pixel is opaque. Opened in Blender 5.2: RGBA,
  straight alpha, 0 at the corner, 1 on the subject. A glass sphere is covered (alpha 255) and shows the world
  through it on the CPU and the GPU; a Bloom filter keeps a rasterized cube opaque; an Edge Outline keeps its
  alpha in rasterized and filtered path-traced renders.

**Review (code reviewer)**: three major and several minor findings, all fixed: glass vanished in transparent traces,
Bloom made a see-through rim in rasterized ones, a filtered trace's alpha ignored its passes, a faint veil from
colour-quantising passes, coverage accumulated on every GPU render, low coverage amplifying denoised colour.
- Unit checks: Windows 8,584, 0 failed (466 tests); the whole suite in render-cache verify mode: 8,585,
  0 failed, 0 stale pictures. Linux with libraries: 8,516, 0 failed. Sanitizer build: 8,438, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures; `filters` and `gpu` stress 0 problems.

## 2026-10-11 (round 56): the Research window retired; papers in Learn > References (task 0034)

**Asked:** "you can remove the research tab, doesn't really serve a purpose. Any features that use papers should be
simply referenced and included in the learn tab."

**Changed**
- **The Research window is gone** (Window menu, File and Help menus, the Project window's Research Papers folder,
  dropping a PDF copying it into the project). A layout saved with it loads without it.
- **Learn > References** lists the papers behind Blendity's features, each with the feature it became and where
  it is (Taubin smoothing, Catmull-Clark, LSCM unwrapping, MikkTSpace, voxel GI, GGX, the area-light work of
  Karis and Heitz et al., the BVH, path guiding, the physical sky, noise, colour temperature...), with a Try-it
  button for Taubin smoothing. `window Research`, `research`, `references` and Ctrl+8 open it. Mesh features from
  papers are now under Mesh > From Papers. Nothing in the `research/` folder was touched.

**Fixed**
- The Textures window's preview could keep an old picture after an undo (an image freed and a new one allocated
  at the same address looked unchanged); it, and the Materials window's previews, now hold their images.

**Checked**
- New tests (round 56): an old layout with Research (beside the Console, and alone in an area) loads without it,
  every preset too; `window Research` and `research` open References; every paper feature is cited there and the
  Taubin Try-it smooths a noisy sphere; dropping a PDF copies nothing; Ctrl+8 opens References from any lesson; a
  layout with Research in its root area, or only Research, loads without it.

**Review (code reviewer)**: a major and several minor findings, all fixed: Add Tab still offered Research, Ctrl+8
opened the last lesson, Research in a layout's root area stayed, the path-guiding citation (now Ruppert, Herholz
& Lensch 2020, the method Open PGL is set to), the research folder still deletable from Blendity, the Materials
previews holding textures (now a content key and weak references), dead state.
- Unit checks: Windows 8,552, 0 failed (461 tests); the whole suite in render-cache verify mode: 8,553,
  0 failed, 0 stale pictures. Linux with libraries: 8,488, 0 failed. Sanitizer build: 8,410, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures; `editor_ui` 0 problems.

## 2026-10-11 (round 55): camera composition guides (task 0033)

**Asked:** "adding 'rule of thirds' guides to the camera and can be togglable. Any additional camera guides or
helpers are also welcomed."

**Added**
- **Composition guides** on every camera (Blender's Composition Guides and Safe Areas): **Thirds**, **Center**,
  **Center Diagonal**, **Golden Ratio**, **Golden Triangle A / B**, **Harmonious Triangle A / B**, **Title Safe**
  and **Action Safe** (margins adjustable), in a colour and opacity of your choice, all behind one **Show
  Guides** switch. They draw over the camera's frame in the Game view, the Camera Preview and while piloting the
  camera (inside a filter's letterbox when there is one), never into renders. Toggle them in the Camera
  inspector, the Game view's new **Guides** menu or with `guides thirds on` / `guides off`. A Learn lesson
  explains them.

**Checked**
- New tests (round 55): the thirds lines at 1/3 and 2/3 of the Game view's camera frame (every pixel of the
  line), also in a 2.39:1 letterbox; each of the ten guides draws on its own; Show Guides off draws nothing; an
  F12 render with guides on is identical to one without; the camera's render hash ignores them; Title Safe at
  Blender's margins; the fields save, undo and redo; the Guides menu clicked with the mouse; the triangles'
  endpoints are Blender's.

**Review (code reviewer)**: a blocker and several minor findings, all fixed: the Guides menu read a dead pointer,
the triangles weren't Blender's construction, the piloted frame ignored a filter's letterbox, a hashing hazard in
`Camera::reflect`, loose console parsing, HiDPI lines, zero safe margins.
- Unit checks: Windows 8,522, 0 failed (458 tests); the whole suite in render-cache verify mode: 8,523,
  0 failed, 0 stale pictures. Linux with libraries: 8,458, 0 failed. Sanitizer build: 8,380, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures; `editor_render` idle 1.33 ms.

## 2026-10-11 (round 54): every camera filter in the path tracers (task 0032, ADR 0008 amended)

**Asked:** "I want all the camera filter modifier stack to show up in the render view ... I want the filters to
show up in the path traced system as well".

**Added**
- The Retro Console's four rasterizer stages now show in path-traced pictures (F12, the Render window's
  Preview, the Rendered Camera Preview), on the CPU and the GPU: **vertex snapping** (wobbly PS1 edges),
  **affine texture warping**, **the texture rules** (Nearest or 3-point filtering, the size cap) and the
  Saturn's **screen-door transparency**. The other filters (outlines, bloom, grading, fog...) already did.

**Checked**
- New tests (round 54): a traced quad at a grazing angle takes the rasterizer's affine UVs (mean difference
  0.19, against 0.94 for perspective), the GPU equal to the CPU; a 1-texel cap makes a textured quad one
  colour on the CPU and the GPU; the screen door is an exact checkerboard (solid and see-through) on both;
  a path-traced F12 render through a snapping Retro filter has the rasterized render's silhouette (0 pixels
  differ, 448 without the snap); the GPU takes the same capped mip level, Nearest and 3-point filters as the
  CPU; a nearly clear surface (alpha 0.05) vanishes through the door as in the rasterizer.
- Stress `filters`: the odd-settings check had been reporting 135 false problems since task 0026 (it still
  required the internal image to be no larger than the view); corrected: 300 runs, 0 problems.

**Review (code reviewer)**: three major and several minor findings, all fixed: the door kept nearly clear
surfaces, the GPU capped the sky map too, every preview rebuild copied every mesh (now cached and reused),
hits behind glass counted as camera hits, shadow rays skipped the texture rules, non-finite screen positions.
- Unit checks: Windows 8,494, 0 failed (452 tests); the whole suite in render-cache verify mode: 8,495,
  0 failed, 0 stale pictures. Linux with libraries: 8,430, 0 failed. Sanitizer build: 8,352, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures; `gpu` stress clean.

## 2026-10-11 (round 53): live procedural textures (task 0031, ADR 0017 amended)

**Asked:** "Not only do I want the procedural textures to be evaluated at a set resolution. I want to view them
change on the material itself. So one part is images at a set resolution, but still having the convenience of
live shaders."

**Added**
- **Live Shading** (a texture's field, on by default): up close, where a texel of the image would cover more
  than a pixel, the Scene and Game views, the Camera Preview, F12 raster and the Materials window's previews
  evaluate the texture's layers at the pixel, like a shader, so bricks, planks and tiles stay sharp at any
  distance (normals included). Further away they read the mipmapped image, which doesn't shimmer. The images
  at the set resolution are still what exports, bakes and the path tracers use.
- **Objects follow a texture edit while you drag:** dragging a field in the Textures window updates every
  material using the texture each frame (its images at 256 during the drag, the set size on release); before,
  they changed only on release.

**Changed**
- The Tile layer reads the texel its tiled point lands in (it read one half a cell off), so its images can
  differ slightly from before.

**Checked**
- New tests (round 53): a point evaluation equals the planes at every texel centre for every layer type (Warp
  and Tile within a mean 0.01); the live normal within 10 degrees of the image's normal; up close a mortar edge
  is sharp with Live Shading (0 in-between pixels across a row) and blurred without (13), and far away both
  give the same pixels; dragging a field gives the material a new 256 image every frame and the 1024 one on
  release; the preview's Edge Wear as wide as at full size; 12 stacked Warp / Tile layers in linear time; an
  axis material's scale and Mirror wrapping keep the image; the preview is per texture; Live Shading saves;
  NaN coordinates stay finite.

**Review (code reviewer)**: four major and several minor findings, all fixed but the frame-time target (2.4x for
a whole close-up frame, documented): extra planes kept for every texture, texel-sized filters 8x wide in the
drag preview, axis materials going live while minified, stacked Warp / Tile doubling per layer, a global
preview flag, every Materials preview redrawn on any texture edit, Live Shading toggles re-evaluating images,
Mirror / Clamp / Closest, Generated mapping, a hard switch line (now a crossfade).
- Stress `procedural_textures`: a point of the Bricks preset about 290 ns, 485 ns with its normal (one
  thread); a 1080p frame made entirely of magnified texels 2.4x the image-only one (the spec's target was
  under 2x: not met, see the spec's As built); the odd-settings fuzz now also evaluates every layer type at
  points: 147 runs, 0 problems. 1024 x 1024, 8 layers: 108 ms (unchanged).
- Unit checks: Windows 8,471, 0 failed (447 tests); the whole suite in render-cache verify mode: 8,472,
  0 failed, 0 stale pictures. Linux with libraries: 8,419, 0 failed. Sanitizer build: 8,341, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures; `editor_render` idle
  1.33 ms.

## 2026-10-11 (round 52): large area lights, the gizmo after Set Origin, Select Coplanar (task 0030)

**Asked:** "the area light when width and height are large (lets say 10 x 10) still creates weird rendered
artifacts (black splotches on the object, even a default cube)", "when you use the lighting generate lighting
feature it causes the error"; "when I retarget the origin of any object, the transform tool and gizmo doesn't
automatically apply to the new origin"; "the ability to select coplanar faces"; "certain camera filter
properties like edge outline don't show up in the render view ... in the path traced system as well".

**Fixed**
- **Generate Lighting under a large area light no longer bakes splotches.** The bake took the light's range
  falloff from each random point on the light (on a 10 m light it varied tenfold across it), so 32 samples
  per texel left heavy noise that the denoiser turned into blotches. Now the unshadowed light is computed
  (no noise) and the shadow rays only decide how much of it gets through, the falloff at the centre as in the
  path tracer. A floor under a 10 m light: texel-to-texel noise from about 15 % to 0.1 %.
- **Large area lights light the sides of things in the Scene and Game views and F12 raster.** The
  rasterizer treated an area light as a point at its centre, so a cube's sides under a 10 m light were black;
  it now takes the light's whole surface (the path tracer's answer within 1 %), with the specular from the
  light's nearest point. Light probes take area lights the same way.
- **The gizmo moves to the new origin at once** after the Inspector's Set Origin > Apply: the change asked for
  no new frame, so the app waited for the next input with the gizmo on the old origin.

**Added**
- **Select Coplanar** (Edit Mode, Mesh menu; `editop select_coplanar`): adds every face lying in the plane of a
  selected face, joined to it or not (Blender's Select Similar > Coplanar). In vertex and edge mode it starts
  from the faces whose corners are selected and adds to the selection.

**Checked, no change needed**
- Camera filters in path-traced renders: an Edge Outline shows in F12 path-traced renders at 720p with the
  denoiser, on the CPU and both GPUs and in the Render window's Preview (now a regression test). The filters
  that don't reach the path tracer are the Retro Console's rasterizer stages (vertex snapping, affine
  textures, the texture override, screen-door transparency); task 0032 brings them to it.

**Checked**
- New tests (round 52): the area light's integral against a million-point reference in 13 cases (within 1 %);
  a cube's side under a 10 m light lit in the rasterizer as in the path tracer (0.098 vs 0.098); a glossy
  floor's highlight within a factor 0.5 to 1.6 of the path tracer's; a baked floor under a 10 m light smooth
  (0.1 % texel-to-texel) and as bright as the path tracer (0.412 vs 0.413); a change asks for a frame and
  the gizmo is on the new origin in it; Select Coplanar (joined, apart, raised, flipped, tilted, degenerate,
  the vertex selection kept); the Edge Outline in path-traced renders.
- Stress `shading`: an area light per pixel, one thread: 175 ns (rectangle), 210 ns (a wall cutting it),
  280 ns (0.3 m below), 360 ns (disk); a point light is about 10 ns.
- Unit checks: Windows 8,423, 0 failed (438 tests); the whole suite in render-cache verify mode: 8,424,
  0 failed, 0 stale pictures. Linux with libraries: 8,371, 0 failed. Sanitizer build: 8,293, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures; `editor_render` idle
  1.41 ms; `bake` and `probes` odd-settings runs: 0 problems.

## 2026-10-11 (round 51): axis materials (task 0029, ADR 0018)

**Asked:** "a material that will always have grass on top followed by dirt, regardless of rotation".

**Added**
- **Axis materials:** a material's **Axis Mode** (World or Local) and three parts, **Top**, **Side** and
  **Bottom** (material assets picked in the Inspector; empty uses the material's own maps). Surfaces facing
  up take Top, steep ones Side, downward ones Bottom, by the normal in the world (World: grass stays on top
  however the object turns) or the object's own space (Local: moss that turns with the stone); **Axis
  Angle** sets where Top turns into Side, **Axis Blend** how soft the edge is, **Axis Scale** the
  projection's tile size. The parts are projected triplanar, so no UVs are needed.
- The same in the Scene and Game views, F12 raster, the CPU path tracer and the GPU path tracer; the
  Materials window's previews show axis materials and procedurals (they showed neither).

**Checked**
- New tests (round 51): World mode puts Top up, Side on steep faces and Bottom underneath whatever the
  object's rotation; Local mode follows the object; Blend 0 is a hard edge and 0.3 mixes; a part's texture
  varies across a surface without UVs; the fields round-trip through a `.mat`, an edit undoes, and a part's
  edit changes the views' hash; an F12 raster render from above shows the Top, still after a quarter turn
  in World mode and the Side in Local mode; the CPU and both GPUs agree on an axis-material cube (mean
  difference 0.0006 of 0.187).
- Stress `shading`: an axis material with two parts 1.51× a plain one per point (target under 1.6×).
- Unit checks: Windows 8,371, 0 failed (431 tests); the whole suite in render-cache verify mode: 8,372,
  0 failed, 0 stale pictures. Linux with libraries: 8,321, 0 failed. Sanitizer build: 8,243, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures; `gpu` stress clean.

**Review (code reviewer)** found and these were fixed, each with a test: two axis materials naming each
other recursed without end (now cut while resolving, a part naming the parent itself is ignored); the GPU
material table's iterator could be invalidated while appending parts; the GPU projected procedural parts at
a different point than the CPU; the shadow-map (RSM) cache key missed the parts; a NaN normal shaded black
(now the Side part); the views' hash mixes the parts as resolved. Also: a compiler warning in the normal
output's Sobel (64-bit indices).

## 2026-10-11 (round 50): the Textures window (task 0028)

**Asked:** a tab dedicated to making procedural textures with a Blender-style modifier stack.

**Added**
- **Window > Textures** (`window Textures`): the project's texture assets as thumbnails (click to edit,
  drag onto an object in the Scene view or a Hierarchy row to use it); **New** from a preset (Bricks, Wood
  Floor, Tatami, Tiles, Noise, Checker, or empty); the size; **Assign to Selection**; a search.
- **The layer stack**, drawn like a camera's filter stack: each layer a foldout with its checkbox and a menu
  (Move Up, Move Down, Reset, Remove Layer), **Add Layer** by category (Generators, Filters), every field
  editable; each edit one undo step.
- **A preview** of an output (Albedo, Normal, Roughness, Height), 1× or tiled 2 × 2 to check the seams; it
  evaluates at 256 while a field is being dragged and at the texture's size on release.
- Double-clicking a `.ptex` in the Project window opens it in the Textures window. The window opens beside
  the Materials window, so the Scene view stays visible to drag onto.
- From the code review (Opus): the window first opened over the Scene view, and a drop then landed on an
  object in the hidden view's last picture (drops onto the Scene view now need this frame's picture);
  dragging a field re-evaluated the whole texture every frame for every material using it (they now
  re-resolve once, on release, while the preview follows at 256); the preview repacked the full image on
  every frame, idle ones included (it is cached and drawn from the smallest fitting mip); a deleted texture
  stayed selected; the name is the file's, not a field; the drag highlight now matches where a texture
  asset can land; the tests clean up the scratch project's textures.

**Checked**
- New tests (round 50), by real clicks: `window Textures` opens it and the layout round-trips with it;
  New > Bricks creates and selects the asset; Add Layer > Blur appends a Blur, its menu's Move Up moves
  it, its checkbox disables it, and undo takes each back; a value typed into a layer field changes the
  texture and the preview, and undo / redo restore both; a thumbnail dragged onto the Cube gives it the
  texture's maps; the preview is 256 while a field is dragged and 1024 after.
- Unit checks: Windows 8,343, 0 failed (423 tests); the whole suite in render-cache verify mode: 8,344,
  0 failed, 0 stale pictures. Linux with libraries: 8,295, 0 failed. Sanitizer build: 8,217, 0 failed, no
  reports. `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures; `editor_render` idle
  1.39 ms (unchanged).

## 2026-10-11 (round 49): procedural texture assets (task 0027, ADR 0017)

**Asked:** a tab for making procedural textures (bricks, wood floors, tatami mats...) with a Blender-style
modifier stack. This round is the texture assets themselves; the Textures window is the next (0028).

**Added**
- **Texture assets, `Assets/Textures/<name>.ptex`:** a size (64–4096), a physical size (metres per tile),
  a normal strength, a seed, and **a stack of layers** evaluated in order to four images: **albedo**,
  **normal** (from the height), **roughness** and **height**.
- **21 layers.** Generators, all tiling seamlessly: Brick, Wood Floor, Tatami, Tiles (square, hexagon,
  herringbone), Voronoi, Noise, Wave, Checker, Stripes, Gradient, Image. Filters: Blur, Levels,
  Hue / Saturation, Invert, Warp, Tile, Edge Wear, Height AO, Color Ramp, Mask by Height. Each layer has
  a blend mode (Normal, Multiply, Add, Screen, Overlay, Height Max / Min), an opacity, the planes it
  writes, and can fade by the mask a Mask by Height layer below sets.
- **Materials use them by output:** `Assets/Textures/Bricks.ptex#albedo` (`#normal`, `#roughness`,
  `#height`); Assign to Selection (Project window, or `newtex` then the console) sets all three maps and
  the tiling from the physical size. Editing a texture redraws every view that shows it.
- **Exports carry them:** the outputs a material uses are written as PNGs beside an exported OBJ or glTF
  and referenced (by name in the MTL, so the folder can be moved). USD and FBX don't reference textures
  yet, so no images are written for them.
- **Assets behave like material assets:** saved when an edit settles and with the scene, renamed and moved
  with their references (materials and scene files), forgotten when deleted.
- Console: `newtex <name> [preset]` (Bricks, Wood Floor, Tatami, Tiles, Noise, Checker), `texlist`,
  `texlayer <texture> add|remove|move|reset|list`, `settex <texture> <Field> <value>` (a layer's field
  as `L0Rows`). **Texture edits undo and redo** (the asset's text travels with the undo step).
- From the code review (Opus): Hexagon and Herringbone tiles are real tilings now (the first draft's
  hexagons didn't repeat vertically and its herringbone was a square grid), Tatami lays true 1:2 mats
  (Basket, Running Bond, Pinwheel), and every count that decides a pattern's parity is rounded so the
  pattern repeats exactly; the seam test now checks sample(u, v) = sample(u + 1, v) = sample(u, v + 1)
  on 20 configurations instead of a step comparison that could not fail. Blur, Height AO and Edge Wear
  are separable (O(r) or O(1) per texel instead of O(r²): radius 64 at 4096² went from minutes to
  seconds) and copy only the planes they touch; evaluation runs outside the cache's lock; a texture's
  first load no longer bumps the texture generation in the middle of a render's resolve; NaN guards use
  `finite_bits` (a `v == v` test can be folded away under /fp:fast); integer fields are clamped where
  used; an unknown layer type (a newer file) is kept so the layers after it keep their fields; exported
  images get unique names per asset and each asset is evaluated once; assigning a texture to an object
  using a `.mat` asset gives that object its own copy instead of editing the shared asset.

**Checked**
- New tests (round 49): every generator tiles seamlessly and is deterministic; a Brick's mortar fraction
  follows its parameters, Tatami has its border, a Wood Floor its gaps; the stack order matters; a
  `.ptex` round-trips; a material with `Bricks.ptex#albedo` renders coloured, and a layer field and the
  normal strength redraw the view; OBJ and glTF exports write and reference the PNGs (by name in the MTL;
  the glTF read back by the importer) while the scene's material keeps the asset, and an STL export writes
  none; renaming the `.ptex` retargets the material; `texlayer` / `settex` edit the stack, the file is
  written when the edit settles, and undo / redo restore a field edit and an added layer.
- Unit checks: Windows 8,308, 0 failed (417 tests); the whole suite in render-cache verify mode: 8,309,
  0 failed, 0 stale pictures. Linux with libraries: 8,260, 0 failed. Sanitizer build: 8,182, 0 failed, no reports.
- Stress `procedural_textures` (new): 1024 x 1024 with 8 layers 108 ms on 32 threads (target under 150 ms);
  147 fuzzed runs (NaN / infinite / huge / negative fields on every layer type), 0 problems.
  `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

## 2026-10-11 (round 48): filters look the same at every resolution and during traces (task 0026, ADR 0016 part 2)

**Asked:** the camera-filter round, second part: a camera's filters should look the same in the Camera
Preview, the Game view and a render, and a filtered progressive trace shouldn't change its look as
samples accumulate.

**Changed**
- **Pixel-sized filter parameters scale with the picture's height** (1080p = 1x): Pixelate's cell, Edge
  Outline's thickness, Film Grain's size, CRT's scanline and phosphor pitch, Sharpen's radius. A 540p
  Preview and a 1080p render of the same camera now match (an outline covers 0.57 % of both).
- **A small view renders at its stack's resolution** (a 240-row retro stack in a 120-row inset) and is
  box-averaged into the view, instead of rendering at the inset's own rows.
- **Bloom on a progressive trace** reads the linear light at every resolve, so the glow doesn't grow
  with the samples; Color Grading's exposure applies in linear light when bloom's HDR plane is there.
- **The views' cache keys hash only the raster-relevant render settings:** changing the path tracer's
  Samples no longer redraws the Game view (changing Exposure still does).
- **The Game view's background is the world's gradient** (or sky / HDRI) when the scene's lighting is on;
  in Gradient mode it used the editor's fixed sky colours.
- A paused Game view keeps the grain's and the flicker's last pattern.

**Checked**
- New tests (round 48): an Edge Outline's covered fraction is equal at 540p and 1080p (0.0057 both); a
  Pixelate row has the same cell count at both sizes (54 / 55); Samples doesn't redraw the Game view,
  Exposure does (0 / 2 view renders); a Rendered Preview with bloom keeps its mean brightness from the
  first frames to the finished trace (152.6 / 152.6); a red world gradient reaches the Game view's sky;
  a paused Game view with Film Grain holds still. The `filter_layout` test now expects the stack's
  resolution for a small view.
- From the code review (Opus), three majors fixed: the Live Preview and the Rendered Camera Preview
  keyed on the raster-only settings too, so changing Samples, Max Bounces or the denoiser no longer
  restarted them (a trace-settings hash is mixed into their keys; a test changes Max Bounces); Color
  Grading's new HDR path re-encoded every pixel from the HDR plane, wiping a glow, an outline, depth of
  field or a Solid Color clear drawn after it (it now re-encodes only pixels whose colour still is the
  plane's encoding; a test checks a glow and a solid clear survive); the Fill layout's width was
  unbounded for a squeezed view (both sides are capped, and a letterboxed stack scales by one factor so
  its pixels stay square). Also: the linear buffer is taken once per progressive resolve, a stepped
  paused Game view keys on its time, and the preview's header is kept out of the bloom measurement.
  Note: a view smaller than its stack's resolution now renders and traces up to 1080p rows for it (a
  360-row Rendered Preview of a 1080-row retro stack traces 1920 x 1080).
- Unit checks: Windows 8,205, 0 failed (408 tests); the whole suite in render-cache verify mode: 8,206,
  0 failed, 0 stale pictures. Linux with libraries: 8,156, 0 failed. Sanitizer build: the
  round's tests 21 checks, 0 failed (the full run before the review fixes failed only in the two tests
  those fixes changed; CI's job covers the final code). Stress `filters`:
  300 fuzzed stacks, 0 problems; `editor_fuzz` in verify mode: 6,000 frames, no crash, 0 stale pictures.

## 2026-10-11 (round 47): the Active Camera, and filters wherever a camera's picture shows (task 0025, ADR 0016)

**Asked:** camera filters were missing from the Game view, the Camera Preview and the Render window.

**Found:** which camera "the" camera is was decided by name ("Main Camera", else the first), while the
Camera Preview showed the selected one: a filter on the selected camera showed in the Preview and not
in the Game view. The Scene view's filters ran with no camera (no exposure, clear colour or fog). Path-
traced pictures carried no depth or ids, so Edge Outline and the Retro fog were skipped in the Rendered
Preview and a path-traced F12, and Solid Color was ignored there. F12 raster without filters skipped
depth of field.

**Added / fixed**
- **An explicit Active Camera** per scene (Blender's scene camera, Unity's MainCamera tag): "Set as
  Active Camera" in the Camera inspector and the Hierarchy menu, a camera dropdown in the Game view's
  bar (replacing "Display 1"), `activecamera <name>|none` in the console, Ctrl+0 (Ctrl+Numpad0) in the
  Blender keymap; saved with the scene, one undo step, cleared when the camera is deleted. Without one
  the old name rule applies, so old scenes behave as before.
- **The path tracers (CPU and GPU) record the first hit's distance and object id,** and a traced picture
  gets a real `FilterFrame` with depth (as the rasterizer's z / w) and ids: Edge Outline and the Retro
  fog now work in a path-traced F12 and the Rendered Camera Preview. A camera's Solid Color fills the
  traced background (camera rays only; the lighting is unchanged).
- **The Scene view's filters look through the active camera** for exposure, Solid Color clear and far
  clip (the editor's projection stays; no depth of field while working). **F12 raster without filters**
  applies depth of field before the supersampling downsample.

**Checked**
- New tests (round 47): a filter on a second camera shows in the Game view only once it is the Active
  Camera, and undo restores the name rule; the Camera Preview and the Game view both follow the active
  camera's filters; the Scene view with filters takes the camera's exposure (+18 % mean brightness at
  +4 EV under Filmic) and Solid Color (a red sky pixel); a path-traced F12 with Edge Outline has 591
  near-black pixels against 1 without, and Solid Color fills its background; F12 raster depth of field
  lowers the neighbour difference 0.47 → 0.36; the active camera round-trips through a scene file and is
  cleared by deleting the camera; the console command reports, clears and refuses a non-camera.
- From the code review (Opus): the Game view's camera dropdown kept its labels in locals that died
  before the popup read them (a use after free the first time it was opened); they live in the editor
  now, the list shows only cameras the view could show, and a test opens it with mouse clicks and picks
  a camera. On a GPU the traced depth plane was a mean divided by a sample count that lags the sums
  (inflated fog between merges) and a mean that counted sky samples at 1e6 (an unfogged ring around
  distant objects): the plane now holds the last hitting sample's distance, consistent with the id. The
  hit point is rebuilt from the near plane as the tracer traces it (orthographic cameras too); the
  traced Solid Color is divided by the camera's exposure so it matches the raster clear; the far clip
  is kept with the render; `Scene::clear` resets the active camera and a dangling id in a file is
  dropped at load. A test checks outlines in the Rendered Camera Preview too. Note: F12 raster depth of
  field runs on the supersampled image (33 million pixels at 4x 1080p), a second or two.
- Unit checks: Windows 8,179, 0 failed (399 tests); the whole suite in render-cache verify mode: 8,180,
  0 failed, 0 stale pictures. Linux with libraries: 8,131, 0 failed. Sanitizer build (before the review
  fixes): 8,045, 0 failed, no reports. Stress `filters`: 300 fuzzed stacks, 0 problems; `editor_fuzz` in
  verify mode: 6,000 frames, no crash, 0 stale pictures; `editor_render` unchanged within noise.

## 2026-10-11 (round 46): render-frame waste (task 0024)

**Asked:** the editor-performance round, second part: the frames that really render (orbiting, dragging,
editing a material, playing) did work that isn't rendering.

**Changed**
- **One filter stack per camera,** rebuilt only when one of its filters changes (a stack is a
  `std::function` per pass, and every view asked for a new one on every render). The Rendered Camera
  Preview and F12 use it too.
- **One filter target per view** (Scene, Game, Camera Preview, F12): two filtered views of different
  sizes used to reallocate a shared one every frame. The Profiler counts resizes and stack builds.
- **The rasterizer resolves each material's textures once per flush** and shades through them,
  instead of a version check and an atomic read per pixel; its per-item scratch vectors are kept
  between flushes and the per-chunk tile table is thread-local.
- **The sun's shadow casters are copied only when the map is rendered** (pointers until the cache
  check). The RSM already did.
- Not built: early-outs for the Scene view's icons and z-fight outlines on a cache hit (they draw
  after the cached picture, so they must draw every frame); recorded in the spec.

**Checked**
- New tests (round 46): the resolved-texture shading equals the per-pixel path on 64 points of a
  UV-grid material; three filtered views keep their own targets (2 resizes to warm up, 0 afterwards);
  a camera's stack is built once and once more after a filter field changes; the shadow map still
  renders once per change.
- Unit checks: Windows 8,148, 0 failed (390 tests); the whole suite in render-cache verify mode: 8,149,
  0 failed, 0 stale pictures. Linux with libraries: 8,100, 0 failed. Sanitizer build: 8,021, 0 failed,
  no reports.
- Stress `editor_render` on the final code, quiet machine: idle 1.31 ms, orbit 7.38, moving an object
  18.13, editing a material 18.13, playing 11.88 (task 0023's 1.30 / 7.32 / 18.44 / 18.33 / 12.13: the
  same within noise, the waste being allocation and copies that were small next to the rendering);
  shadow renders per row unchanged (1 for a move and a material edit, 0 otherwise). `shading`, `raster`
  and `filters` within their usual numbers (deferred 1080p 10.5 ms, 5.2M triangles at 60 FPS, the PS1
  look 2.5 ms). `editor_fuzz` in verify mode: no crash, 0 stale pictures.
- From the code review (Opus): the filter-target and stack-build tests measured idle frames, which are
  cache hits that render nothing; they move an object every frame now and check that the views really
  rendered. The Scene view no longer copies the cached stack to change its fit (`filter_layout` takes
  the fit apart from the stack); the cache is trimmed between frames, never during a lookup.

## 2026-10-11 (round 45): idle editor frames (task 0023, ADR 0015)

**Asked:** improve the editor's performance, the Scene view first. This round is the idle frame
(nothing changed, the mouse over a panel); the next (task 0024) is the frames that really render.

**Found:** pass 6 had called the 2.1 ms idle frame "almost all hash". A per-phase profile said the
scene hash was about 0.3 ms of it; the rest was painting the UI: every icon rasterized from its
primitives on every draw (the tab bars alone 0.38 ms), 2.5 million pixels of fills the views painted
over anyway, the Project window listing folders from disk every frame, the material library copied and
sorted twice a frame.

**Changed**
- **The scene hash's per-object memo (ADR 0015):** a component says what the views read of it
  (`render_role()`: one virtual call instead of a `dynamic_cast` per type, which was most of the cost),
  each object remembers the hash of its world matrix, flags and materials keyed by a transform version
  (bumped wherever the world matrix goes stale, reparenting included) and the material versions, the
  three evaluated-mesh keys come from one pass, and the Scene view's filter camera takes the frame's
  scene hash. Lights, probe volumes and modifiers are hashed every time, so the memo can't go stale
  on them. The hash at 10,000 meshes: 0.46 ms (the idle frame there 4.8 → 2.7 ms).
- **Icons** are rasterized once per size into a coverage mask and blended through it.
- **Fills:** the frame fills only the border strips around the dock area (the dock fills its
  splitters), and no panel is painted behind the Scene and Game views, which paint every pixel.
- **The Project window** keeps its folder listings with the file list (refreshed every 2 s or on a
  change); `loaded_material_assets()` is rebuilt only when the library changes; the Materials window
  counts users in one pass; `Canvas::circle` walks the ring, not its bounding box; the Statistics panel
  shows the rasterizer's binning time (`ms_bin`, the binning part of the setup tasks over every chunk).
- **Not changed, on purpose:** the hasher's byte-at-a-time mixing (its values are in every saved
  bake's key); no per-frame memo for `main_camera()` or "Lighting out of date" (the serial they would
  key on doesn't change on edits). The spec's 1.0 ms idle target is not met: 1.3 ms, the rest being
  legitimate drawing (`docs/PERFORMANCE.md`, pass 7, lists it); recorded in the spec for sign-off.

**Checked**
- New tests (round 45): `evaluated_keys` equals the three `evaluated_key` calls on 300 modifier
  combinations; the transform version bumps on a move, a parent's move and reparenting; the memoised
  hash equals a from-scratch hash and changes after 23 kinds of change (a gizmo drag, the position
  field, undo, redo, parenting, a parent's move, unparenting, material, light, modifier added / changed
  / disabled / Show in Renders, renderer disabled / enabled / flag, a material slot, an object
  deactivated / reactivated / deleted / created, a probe volume added / changed), in verify mode with 0
  stale pictures; a camera filter field re-renders the Game view; the main camera follows a rename, a
  disabled Camera and a deactivated object; Statistics shows the binning time.
- From the code review (Opus): a small ring in `Canvas::circle` blended a few pixels twice (one run
  per row when the hole is narrow); an empty root dock leaf went unpainted without the frame's clear
  (filled now); an icon's anti-aliased edge past its rect was lost by the mask (the mask is padded by
  3 px); pixel-equality tests for the icon masks (within 2 of a direct draw) and `Canvas::circle`
  (identical to the bounding-box walk).
- Unit checks: Windows 8,139, 0 failed (386 tests); the whole suite in render-cache verify mode:
  8,140, 0 failed, 0 stale pictures. Linux with libraries: 8,091, 0 failed. Sanitizer build: 8,009,
  0 failed, no reports.
- Stress `editor_render`: idle 1.98 → 1.27 ms, mouse over the Hierarchy 2.01 → 1.25, orbit 8.49 →
  7.40, moving an object 20.99 → 18.42, editing a material 19.22 → 18.19. `editor_ui`: idle at 1,000
  meshes 1.48 → 1.11 ms, at 10,000 4.82 → 2.74 ms, scene hash at 10,000 meshes 0.46 ms (0.56 from
  scratch). `editor_fuzz` in verify mode: 6,000 frames, 10,621 events, no crash, 0 stale pictures.

## 2026-10-11 (round 44): area lights bake smoothly; Preview Lighting isn't blocky on small objects (task 0022)

**Asked:** adjusting an area light's Width and Height made objects look pixel-like.

**Found:** every change restarts Preview Lighting at its roughest pass: a quarter of the resolution (a
small sphere got a few texels across), 16 samples, and the area light sampled with at most 8 random
points. It looked blotchy for about a second before refining. The real bake sampled area lights
randomly too.

**Fixed**
- **Area lights are sampled on a jittered grid** over their surface in the baker (stratified, with a
  per-texel offset; a concentric mapping for disks). At 16 samples a floor under a 2 x 1 m light is
  within 3.7% of a converged bake on average, against 5.8% with random points.
- **Preview Lighting** gives every object a chart at least 32 texels across, and samples area lights
  32 to 64 times instead of at most 8. That is most of the fix: a small sphere had a few texels across
  and 8 noisy samples.
- **From the code review:** the preview's 32-texel minimum chart was bigger than a 16-texel page (Max
  Lightmap Size allows 16) and wrote past it. A layout now cuts its minimum chart down to the page
  size, for every caller, and a test lays out and previews with 16-texel pages. The baker also takes
  exactly the Direct Samples the user set (not rounded up to a grid).

**Checked**
- Unit checks: Windows 8,108, 0 failed (378 tests), twice. Linux with libraries: 8,060, 0 failed. Sanitizer build: 7,978, 0 failed, no reports (run before the review fix; the final code's run is CI's). Windows dependency-free: 7,980, 0 failed.

## 2026-10-11 (round 43): Baked lights exist only through the bake (task 0021)

**Asked:** don't rasterize lights whose Mode is Baked; the bake represents them. Mixed and Realtime
stay live. Before a bake, Preview Lighting is what shows them, as Bakery's Realtime Preview does.

**Changed**
- **A Baked light is left out of the rasterized views:** no direct light or highlight, no shadow map,
  no reflective shadow map (so no live bounce from it). Until now it was skipped only on lightmapped
  or baked-probe surfaces, and still lit everything else.
- **Before a bake it lights nothing;** turn on Preview Lighting or press Generate Lighting.
- **Unchanged:** its icon and gizmo, the bake itself, and the path tracer (F12 path traced, the Rendered
  Camera Preview), which computes true GI.
- **Solid shading** leaves Baked lights out too, and live GI picks its sun the way the shadow map does
  (the first directional light that isn't Baked).

**Fixed (found by the code review)**
- **The first version also dropped Baked lights from the path tracer,** which builds its lights the
  same way. A path-traced F12 render of a Baked-light scene came out sky-lit. The rasterizer and the
  path tracer now take different light lists, and a test renders F12 through the editor to check.
- **The light's Mode help text** said Baked lights still drew on unmapped objects.

**Checked**
- Unit checks: Windows 8,092, 0 failed (375 tests), twice. Render cache verify mode: 0 stale pictures.
- Linux with libraries: 8,044, 0 failed. Sanitizer build: 7,966, 0 failed, no reports. Windows dependency-free: 7,964, 0 failed.

## 2026-10-11 (round 42): the sanitizer CI job's flaky pilot test

**Asked:** the Linux sanitizer job was failing on the PR that brings tasks 0011-0020 to main.

**Fixed**
- **"Stopping the pilot brings back the editor view" failed only on the sanitizer runner:** 7,045
  pixels differed, all in one 294 x 24 box at the top of the Scene view. The test's click helper left
  the mouse resting on the Statistics toolbar button, and on a slow run the button's tooltip delay
  elapsed, so the tooltip drew over the view. The helper now parks the mouse outside the window after a
  click. Reproduced and fixed under the sanitizer build on 3 threads (CI's count).

**Checked**
- The pilot tests pass under the sanitizer build on 3 threads (twice), and on Windows.

## 2026-10-10 (round 41): manual lighting updates and a rough preview, like Bakery's (task 0020)

**Asked:** stop recomputing lighting on every change while moving things around, update it only when
asked, and add an optional very rough preview like Bakery's Realtime Preview.

**Added**
- **Lighting Updates** (Lighting window): Manual, the default, or Automatic (the behaviour until now).
  - **In Manual,** Realtime GI, realtime lightmaps, live probes and Auto Generate don't react to changes.
    Direct light and shadows stay live.
  - **The last lighting stays up:** a baked object that moved keeps its lightmap, and the last realtime
    lightmaps and live probes stay shown. "Lighting out of date" says when it's stale.
- **Update Lighting** (button, Ctrl+Shift+L, `lighting update`) runs Realtime GI once (the grid, the
  RSM, the realtime lightmaps, the live probes) and freezes again. Generate Lighting still bakes.
- **Preview Lighting** (a toggle, `lighting preview on|off`):
  - **What:** a rough progressive bake at a quarter of the Lightmap Resolution, 16, then 64, then 256
    samples, denoised, on the GPU when Render > Device is GPU Compute.
  - **When:** it starts half a second after changes stop and runs about 8 ms a frame.
  - **Shown, not saved:** it appears in place of the bake and is never saved.
- **`lighting mode manual|automatic`**, and the Learn lesson and guide explain both modes.
- **In Manual, one automatic update** when Realtime GI is turned on or a scene opens, so it never starts
  empty.
- **"Lighting out of date (Update Lighting)"** in the status bar when frozen Realtime GI is stale (with
  or without a bake).

**Fixed (found by the code review)**
- **A fresh Generate Lighting was covered by the preview again.** The preview now stands down while
  the real bake is current.
- **Frozen frames dropped a Realtime sun's bounce on lightmaps** (and the frozen live probes, and
  specular occlusion). A frozen frame is now the last updated picture, bit for bit.
- **Undo and redo dropped the frozen realtime lightmaps** (a clone gets a new scene serial). They're now
  keyed by a scene id that undo keeps.
- **In Automatic with a stale bake, the preview was never attached.** Staleness is now judged on what's
  shown.
- **Smaller fixes:**
  - A mesh edit in Manual drops the object's old map (only a move keeps it).
  - Opening a scene clears the last one's preview.
  - The preview's sample count resets on a change.
  - The Lighting window's out-of-date text fits Manual.
  - `lighting mode auto` works.
  - The stress section puts its scene back after the drag rows.
  - Two tests that couldn't fail now can: Auto Generate gets its real second, and "never saved" checks
    a real folder.
- **Not fixed:** each preview stage lays out and builds its scene again in one frame. That's a hitch
  on big scenes while orbiting during a preview.

**Checked**
- Unit checks: Windows 8,066, 0 failed (369 tests), with Automatic and with Manual as the default
  (`BLENDITY_LIGHTING_UPDATES`). Render cache verify mode: 0 stale pictures.
- Linux with libraries: 8,018, 0 failed. Sanitizer build: 7,940, 0 failed, no reports.
- Stress `editor_render`, 1600 × 1000, Realtime GI on, dragging a static object:

  | Mode | ms a frame |
  |---|---|
  | Automatic | 84–108 |
  | Manual | 22–27 (the same as with no GI) |
  | Manual + Preview Lighting | 22–23 (it waits for the drag to stop) |

  After the drag, the GPU preview shows its first pass in 1.4 s and reaches 256 samples in 3.4 s. The
  first run after a kernel change compiles the pipeline once (11 s).

## 2026-10-10 (round 40): lighting without lag: baked GI that costs nothing live, realtime lightmaps, GI and baking on the GPU, no path-traced Scene view (tasks 0016-0019)

**Asked:**
- remove the Scene view's path-traced Rendered mode (too slow);
- the approved tasks 0016–0018: realtime lightmaps first with a fallback, baked lighting with no lag,
  and GI, probes and baking on the GPU where it's faster.

**Added**
- **Realtime lightmaps (0016):** Contribute GI objects get a low-resolution map (Realtime Resolution, 2
  texels per metre by default).
  - Voxel GI lights it a slice at a time when the lighting changes, and it's read like a baked map.
  - **Orbit cost:** +1–3 ms at 1080p over no GI, against +12–14 ms traced per pixel.
  - **Look:** the same at any zoom (the Scene view's blocks are gone on mapped objects).
  - **Fallback:** objects without a map keep the per-pixel pass.
- **No live work once baked (0017):** Realtime GI traces only what a bake doesn't hold.
  - **Cases:** nothing baked, the scene changed since the bake, or a Realtime Directional Light.
  - **Otherwise idle:** it traces nothing, and the views equal Realtime GI off.
  - **Status:** the Lighting window's "Live GI:" line says which case applies, and the per-pixel cost.
- **Cheaper probe sampling (0017):** +4 ms at 1080p with every object probe-lit, from +15 ms (a brick
  hint, the cube's axes once per sample, one sample per 2 × 2 pixel quad).
- **GI on the GPU (0018, ADR 0014):** a Vulkan compute kernel for voxel GI's gather.
  - **What uses it:** realtime lightmaps and live probes. **GI Device** (Auto / CPU / GPU) chooses.
  - **Speed:** 4–8.6× faster on the RTX 4070 SUPER, 3.2–6.2× on the 3060 Ti. A whole pass fits in a
    frame.
- **Baking on the GPU (0018):** with Render > Device = GPU Compute, Generate Lighting traces lightmap
  texels and probe rays on the path tracer's kernel.
  - **Speed:** 8.6× (4070 SUPER) and 6× (3060 Ti) on the tracing.
  - **Agreement:** totals match the CPU's within 0.01%.
- **A lighting guide** (`docs/guides/lighting.md`) and a Learn lesson, "Baked and Realtime Lighting".

**Removed**
- **The Scene view's Rendered (path-traced) mode (0019).** `shading rendered` warns and shows Shaded.
  Path tracing stays in F12, the Render window and the Camera Preview's Rendered toggle.

**Fixed (found while building it)**
- **A small object's realtime lightmap had a black patch:** big texels gathering from inside the floor
  saw nothing. They now take their chart neighbours' light, as invalid probes do. Realtime charts are
  also at least 24 texels across.
- **Shared probe samples depended on thread scheduling** (row pairs split between threads). Rows now go
  out in pairs.
- **Changing GI Device didn't gather again;** it's in the live keys now.
- **From the code review:**
  - **A Sky world drawn from the sun was missing from the GI keys,** so turning the sun didn't re-gather
    with Bounce off, and the GPU kept the old sky. The keys now include the sky's own key.
  - **Probe bakes on the GPU could allocate gigabytes** at high sample counts. Slices are now sized by
    rays (about 8M).
  - **Test limits had been tuned to one build** (the probe quad's worst difference, the small ball's
    sky share). They're now principled bounds, with a comment.
  - **Smaller fixes:**
    - The GPU kernel's read-back buffer is freed.
    - Choosing a GI Device again retries the GPU after an error.
    - Unwrapped realtime charts keep their size when an object moves.
    - Another scene's published maps are never shown.
    - The GPU bake's random sequences follow each texel, not its place in a batch.
    - Realtime lightmaps are capped at about 2M texels.
    - Stale "Rendered" tips are gone.

**Not as the approved specs said (recorded in each spec's "As built"; all signed off by the user on 2026-10-10):**
- **0016:** no voxel ray jitter or occlusion fade; the maps' blur and bilinear filter smooth mapped
  surfaces, and the fallback keeps its look.
- **0017:** shared probe samples differ from per-pixel ones by up to 8 levels on 0.03% of pixels.
- **0018:**
  - GPU and CPU gathers can differ by one or two grazing rays per point (means within 0.002).
  - The per-pixel pass stays on the CPU.

**Checked**
- Unit checks:
  - Windows 7,994, 0 failed (358 tests), and the same with GI Device Auto on the GPU
    (`BLENDITY_GI_DEVICE=auto`);
  - Windows dependency-free (CI's build): 7,866, 0 failed.
  - Render cache verify mode: 0 stale pictures.
- The code review's open minors, not done:
  - a GPU failure mid-pass makes the CPU finish that pass in one frame (a one-off hitch);
  - criterion 5 of task 0016 (fallback pictures bit-identical to the old pass) is checked by traced
    receiver counts, not pixels.
- Linux with libraries: 7,946, 0 failed. Sanitizer build: 7,868, 0 failed, no reports.
- Stress:
  - `vgi`: a 1080p orbit reading realtime lightmaps costs +0.9–3.1 ms over no GI, against +12–14 ms
    traced. A pass of 9,377 texels takes 4.8 ms on the CPU.
  - `probes`: deferred shading at 1080p is 13.7 ms without probes and 17.7 ms with every object
    probe-lit (29.1 ms before).
  - `gpu_gi`: gathers and bakes as above (ADR 0014's table).

## 2026-10-10 (round 39): realtime lightmaps, baked lighting without lag, GI on the GPU (specs only)

**Asked:**
- realtime lightmaps, the faster option first with a fallback;
- GI and probes on the GPU if faster;
- baked lighting that doesn't lag the Scene view;
- why realtime GI looks pixel-like in the Scene view but not in the Game view.

**Found:**
- **The Scene view's blocks** come from the per-pixel GI pass tracing at ¼ of the screen resolution.
  Objects are small on screen there, so each 4 × 4-pixel GI cell shows.
  - GI Resolution = Half makes them smaller; a 256 voxel grid doesn't.
  - A dark band where objects meet the floor comes from the voxel steps.
- **Per-frame costs at 1080p:**
  - a baked lightmap: about 1 ms;
  - live voxel GI: about 12 ms, even where a bake covers the scene;
  - probe sampling: up to 15 ms.

**Drafted (for approval, no code yet):**
- **Task 0016:** realtime lightmaps per texel, with the per-pixel pass as the fallback.
- **Task 0017:** no live GI work once the bake covers the scene, cheaper probe sampling, and a lighting
  guide.
- **Task 0018:** GI, probes and baking on Vulkan compute, each behind a measured spike.

## 2026-10-10 (round 38): GPU viewport, a measured go / no-go spike (task 0015)

**Asked:** run the editor's views on the GPU, after the CPU wins, if it pays off.

**Added**
- **Stress section `gpu_viewport`:** a 1080p view on the CPU (deferred and the cached shadow map)
  against the same picture's work on each GPU. The GPU side is the Vulkan path tracer at 1 sample with
  no bounces, split into clears, render plus read-back, and resolve.
- **ADR 0013** (proposed) and **task 0015's spec** (no viewport code).

**Result: no-go.**
- **The bar:** two views at ≤ 0.50× the CPU's time.
- **The CPU frame:** 19.6 ms.
- **The GPU's render plus read-back alone:**
  - RTX 4070 SUPER: 10.6–11.8 ms (0.54–0.60×);
  - RTX 3060 Ti: 15.4–17.5 ms (0.79–0.89×).
- **End to end through the path tracer:** 31–39 ms.
- **Recommendation:** keep the views on the CPU and look next at voxel GI and probe sampling (about
  12–15 ms each at 1080p).

## 2026-10-10 (round 37): probe volumes, like Unity 6's Adaptive Probe Volumes (task 0014)

**Asked:** add probe volumes, kept live by the voxel GI.

**Added**
- **Probe Volume component:** Global (around everything that contributes GI) or a box that follows the
  object's rotation and scale.
- **Placement:** bricks of 4 × 4 × 4 probes, fine near geometry and coarse in open space. Probes inside
  geometry move out (virtual offset) or are marked invalid and filled from their neighbours.
- **Each probe is an ambient cube:**
  - the light from geometry per axis;
  - the share of the sky each axis sees, so a new sky colour relights them with no re-bake.
- **Baked** by Generate Lighting after the lightmaps, in slices so the editor stays responsive.
- **Live:** Realtime GI keeps the probes current, 4,096 probes a frame, with no bake needed.
- **Shading:** per pixel, with normal bias, view bias and leak reduction.
  - **Receive GI** (MeshRenderer) picks Lightmaps or Light Probes.
  - Objects that don't contribute GI always use the probes.
- **Settings:** min and max probe spacing, the biases, leak reduction.
- **Debug display:** `probes show` / `probes hide`, or Show Probes in the Lighting window.

**Fixed (found by the tests and the review)**
- **Enormous volumes and NaN settings overflowed float-to-int casts** (the sanitizer caught them): in
  placement, in the live update's voxel lookup and in the volume box's key. The probe cap could also be
  broken. Counts are now in double, cells are clamped before the cast, the key hashes the setting's
  bits, and the cap is strict.
- **Live probes never showed with Realtime GI on**, yet they re-rendered the views. Probe-lit objects
  now keep the probes (as in APV), and the live probes are what follow lighting changes.
- **Light counted twice with Baked GI off:** baked probes are now used only with Baked GI on. Probes
  from another bake are refused on open.
- **A re-bake without a volume** left the old `ProbeVolume.bin` to come back on open; it is removed.
  Probes are saved even when there are no lightmaps.
- **The probe bake ran on the UI thread in one go:** it now runs in slices within the bake's frame
  budget, and the bake's mesh copies are freed after it.
- **Global volumes put probes on the floor plane:** the pad is now 1.5 spacings.
- **Thin walls leaked light:** APV's normal-based leak reduction was added (on by default).
- **Live probes near a floor saw through it:** they gather from the nearest free voxel.
- **Receive GI didn't redraw the views.**
- **Smaller fixes:**
  - Indirect Intensity applies to the probes.
  - A cell with every valid probe behind the surface falls back to their plain average.
  - The virtual offset is limited to ¾ of a spacing.
  - Receive GI = Light Probes objects get no lightmap chart.
  - The live key covers both views' voxel GI and only the settings that change the gathers.
  - The volume box is remembered while nothing changes.
- **Tests:**
  - **A NaN volume was a valid point box on GCC:** `AABB::add` drops NaN there, so the placement test
    sets the box directly.
  - **"Stopping the pilot brings back the editor view" failed under the sanitizer:** it compared the
    Statistics overlay, whose frame time differs on a slow run. Statistics is now off for that test,
    and the view comes back bit for bit.

**Not as the approved spec said (awaits sign-off; recorded in the spec and ADR 0012):**
- Each probe is an ambient cube, not SH L1 / L2.
- Probes win over per-pixel voxel GI for the objects they light.
- Live probes restart as a whole, not only near a change.
- The debug display draws dots.

**Checked**
- Unit checks: Windows 7,880, 0 failed (344 tests; round 37 has 29). Render cache verify mode: 0 stale
  pictures.
- Linux with libraries: 7,856, 0 failed. Sanitizer build (ASan + UBSan + float-cast-overflow): 7,778, 0 failed, no reports.
- Stress `probes` (32 threads):
  - **Scene:** 38,144 probes in 596 bricks, 2,704 inside geometry;
  - **Time:** place 9.2 ms, validate 11.1 ms, bake 24.2 ms (64 rays);
  - **Memory:** 206 bytes per probe;
  - **Live:** 1.48 ms per 4,096-probe slice;
  - **Shading at 1080p:** deferred shading 13.6 ms without probes and 29.1 ms with every object
    probe-lit (a follow-up);
  - **Fuzz:** 100 odd volumes, 0 problems.

## 2026-10-10 (round 36): realtime GI from the research paper: voxel-based global illumination (task 0013)

**Asked:** implement the first paper in `research/papers/`, Thiedemann et al., *Voxel-based Global
Illumination* (I3D 2011), working with the baking so that changing the lighting never needs a manual
re-bake.

**Added**
- **Realtime GI** (Lighting window): one bounce of the sun's light and the sky's occlusion, live in
  every rasterized view.
- **The paper's pieces, on the CPU:**
  - **Voxel grid (section 3):** the scene voxelized into columns of 128 bits, with an exact conservative
    span voxelizer.
  - **Moving objects:** each object's voxels are kept, so a moved object is voxelized alone.
  - **Ray test (section 4):** a mip-map hierarchy and its ray walk.
  - **Bounce (section 5.1):** gathered through the sun's reflective shadow map.
  - **Sky:** the share of the sky that gets through gives directional occlusion; open ground keeps
    exactly today's light.
  - **Per pixel:** traced at ½ or ¼ resolution with 16 interleaved ray sets, a geometry-aware blur and
    an upsample.
- **With the baking:**
  - Lightmapped surfaces add the live bounce of lights the bake doesn't hold.
  - Objects changed since a bake, or a whole bake made stale by a light or world change, switch to live
    GI instead of flat ambient.
  - **Auto Generate** re-bakes a second after changes stop.
- **Settings:** voxel resolution, radius, rays, intensity, sky occlusion, bounce, GI resolution, RSM
  resolution, specular occlusion.

**Fixed (found by the tests and the review)**
- **Bounce depended on where the scene sat:** the sun's reflective shadow map flipped normals toward a
  camera at the world origin. It now faces the sun.
- **GI settings didn't redraw the views,** and they made the bake look out of date (which would have
  started Auto Generate). Only the settings a bake depends on count now.
- **Lightmapped surfaces:** they keep their baked specular. Receivers that take everything from a
  lightmap trace nothing.
- **Zero-area triangles** add no voxels.
- **Auto Generate's wake-up** was cleared in the same frame, so an idle editor never re-baked.
- **The Scene view and Game view shared one voxel / RSM cache** and rebuilt each other's every frame.
  Each kind of view now has its own.
- **The ray walks stall or skip far from the grid's corner:** they re-derived cells from rounded
  positions. They now step cell indices and pick children by crossing time. A test checks 20,000 rays
  over 256 voxels long on a 256 grid.
- **A pre-existing rasterizer bug:** a vertex far beyond float range cast infinity to int in tile
  binning, which the sanitizer caught.
- **Smaller fixes:**
  - The RSM holds the sun's light only.
  - The self-occlusion offset clears a voxel's diagonal.
  - Ray sets differ in elevation too.
  - The grid stays put while the scene fits it.
  - Objects that are gone are forgotten.
  - Non-casting objects don't occlude.
  - The lightmaps' out-of-date key covers bake settings only (a 0012 bake would show as out of date
    once).

**Not as the approved spec said (awaits sign-off; recorded in the spec and ADR 0011):**
- Realtime lightmaps are per pixel, not per texel: the per-pixel pass covers the same cases.
- The settings live in the Lighting window (`LightingSettings`).

**Checked**
- **Unit checks:** Windows 4,077, Linux 4,053, sanitizer build 3,975; 0 failed. 315 tests. The
  test-engineer wrote 17 tests and the review added 2:
  - **Voxelizer:** matches a double-precision triangle–box test, exactly.
  - **Hierarchy and ray walks:**
    - mips are the OR of the level below;
    - the hierarchical walk equals the column walk exactly on 100k rays, and on 20k rays longer than
      256 voxels;
    - it agrees with an independent 3D DDA on all of them;
    - the first hit is right going up and down.
  - **Gather:** RSM back-projection gathers lit points and not shadowed or back-facing ones.
  - **Pictures:**
    - a red wall bleeds onto the floor;
    - a box darkens the floor beside it, and far floor is bit-identical;
    - GI off is bit-identical;
    - deterministic, and 1 thread equals all threads.
  - **Caching:** orbiting re-voxelizes nothing; moving one object re-voxelizes only it.
  - **With the baking:**
    - a Realtime sun's bounce follows it on a lightmapped floor without a bake;
    - Auto Generate waits, re-bakes and sets its wake-up time.
  - **Settings:** save, load, undo; odd values are safe.
- **Verify mode:** 0 stale pictures.
- **Stress `vgi` (Windows):**
  - **Voxelizing:** 2.2 ms for 257 objects at 128, and 5 ms at 256.
  - **Ray walks, short rays:** the hierarchical walk equals the column walk (6.8 vs 7.4 Mrays/s on one
    thread).
  - **Ray walks, 40 m rays:** the hierarchical walk is 5x faster (5.4 vs 1.1 Mrays/s).
  - **RSM 256²:** 3 ms, only when the casters or the sun change.
  - **The GI pass adds:**
    - 720p, 8 rays, Quarter: 5.8 ms;
    - 1080p, 8 rays, Quarter: 11.8 ms (over the spec's ~10.5 ms budget);
    - 1080p, 4 rays, Quarter: 8.5 ms.
  - **Odd settings:** 200 runs, 0 problems.

## 2026-10-10 (round 35): baked lighting like Unity's (task 0012)

**Asked:** "a similar light baking system like Unity3D": lightmaps and light modes, the approved task 0012.
Voxel GI (0013) and probe volumes (0014) work with the same lightmaps, so changing the lighting won't
need a manual re-bake.

**Added**
- **Per object and per light** (all saved, undoable and settable with `set`):
  - **Contribute GI** on MeshRenderer, with **Scale In Lightmap** and **Generate Lightmap UVs**;
  - **Mode** on each Light: Realtime, Mixed or Baked.
- **A Lighting window** (Window menu, or `window Lighting`) holds:
  - **Generate Lighting** and Cancel, with a progress bar;
  - **Clear Baked Data**;
  - the Lightmapping settings: resolution in texels per unit, max size, padding, direct and indirect
    samples, bounces, denoise, indirect intensity, Auto Generate;
  - the Environment.

  Console: `bake start|cancel|clear`.
- **The baker (`Lightmapper`) runs on the path tracer:**
  - **Lightmap UVs:** generated per object (Smart UV Project + Pack Islands), sized so each object
    gets the asked density.
  - **Indirect light per texel:** cosine-weighted rays through the static objects, the Baked and Mixed
    lights and the sky, with every bounce. Realtime lights are left out, as in Unity.
  - **Direct light:** Baked lights only, with shadow rays.
  - **Finish:** a filter within each object's charts, then dilation into the padding.
  - **Runs** in slices per frame, cancellable, on its own copies of the meshes.
- **Shading:**
  - Lightmapped surfaces take their ambient light from the map.
  - Baked lights are skipped there in realtime.
  - Mixed lights keep their realtime light and shadows (Baked Indirect).
- **Files:** the lightmaps are saved next to the scene, as `<scene>/Lightmap-N.hdr` and
  `LightingData.bin`, and load with it.
- **Stale objects:** an object changed since the bake falls back to realtime ambient, and the status
  bar says **"Lighting out of date"** (ADR 0010).

**Fixed (found by the tests)**
- **Lightmaps had 21–64% fewer texels than asked.** The packed charts fill only part of their square.
  Charts are now sized from the area their UVs actually cover. A test checks density within 20% of
  Lightmap Resolution × Scale In Lightmap across shapes and sizes.
- **A NaN vertex gave NaN lightmap UVs.** Fast-math compiles `std::isfinite` away, so the check is now
  by bits.

**Fixed (from the review)**
- **The Window menu read past its shortcut list** once the Lighting window was added: undefined
  behaviour every time the menu opened. A compile-time check now ties the list to the windows.
- **A bake still running when another scene was opened or created** would have written its lightmaps
  into that scene's folder. Switching scenes now cancels it, and a new scene starts with no baked data.
- **The lighting folder came from the name stored in the scene**, not the file name. A renamed scene
  could share or delete another scene's lightmaps, and a name of "." pointed at the Scenes folder
  itself. It is now the file's name, and anything that isn't a plain name gives no folder.
- **A bake made before the scene had a file, or before Save As, was lost on save.** Saving now writes
  it next to the file, and so does the auto-save on exit.
- **Dragging a lightmapped object re-hashed its whole mesh every frame**, several times. The mesh part
  of the hash is now remembered by render-mesh serial (only the matrix is rehashed), and "Lighting out
  of date" is recomputed only when the scene or the lighting settings change. This also fixes a hash
  that could go stale after opening another scene.
- **Light skipping:** lightmapped surfaces skip the lights the maps were baked with, as they were
  baked. A light baked as Baked and switched to Realtime no longer shines twice, and one added since
  is no longer dropped.
- **A light on an object with its own non-contributing mesh was missing from the out-of-date check.**
- **A damaged `LightingData.bin` could ask for gigabytes.** Counts are now checked against the file's
  size.
- **Bounces were one too many:** the gather's own hit is the first bounce. With 0 bounces you get only
  the sky and direct light.
- **Smaller fixes:**
  - The content hash now includes per-face materials, sharp edges and smoothing.
  - Batches are sized by sample count, so a frame's budget holds at high sample counts.
  - A cancelled bake frees its memory.
  - Lightmaps are not used under the Scene view's headlight.
  - The stress section reports the filter + dilation time.
- **Five tests from the review:**
  - baking before saving;
  - a new scene during a bake;
  - a damaged file;
  - a lamp mesh with a Baked light;
  - a mode switched after the bake.

**Not done, from the review:**
- The texel rasterizer still lives inside the baker; task 0013 will expose it for realtime lightmaps.
- Texels are taken at texel centres, not conservatively, so slivers thinner than a texel get none;
  dilation covers them.
- The Scene view uses a lightmap only when its viewport mesh matches what was baked. With modifiers
  that differ between the viewport and renders it shows realtime light while the status says up to
  date.
- Laying out a very large bake (the UVs, the atlas) still runs on the main thread before the first
  slice.

**Not as the spec said:** the lightmaps are Radiance `.hdr` files, not `.exr`. Blendity can write EXR
only with Blender's libraries and has no EXR reader; `.hdr` is written and read without them.

**Checked**
- **Unit checks:** Windows 3,540, Linux 3,516, sanitizer build 3,438; 0 failed. 293 tests. The
  test-engineer wrote 19 tests, and the review added 5:
  - **UVs:** inside 0..1, no overlaps, padding, density.
  - **Colour bleeding:** a red wall tints the floor with a Baked or a Mixed sun, and a white wall
    doesn't.
  - **Accuracy:**
    - open sky exact;
    - under a covering slab, dark;
    - at a slab's edge within 0.013 of an 8,192-sample path-traced reference;
    - a Baked sun's direct light exact (colour × intensity × N·L), 0 in shadow.
  - **Light modes:**
    - a Baked sun alone lights the floor;
    - a Mixed sun keeps the realtime shadow (90% of the lit region in common with an unbaked scene);
    - a Realtime sun leaves the maps bit-identical.
  - **Lifecycle:**
    - save and reopen;
    - a moved box falls back, then comes back bit-identical;
    - `bake clear` restores the picture bit-identically;
    - dynamic objects untouched;
    - path-traced F12 unchanged.
  - **Robustness and undo:** cancel, undo, NaN and odd settings.
- **Verify mode:** the suite has 0 stale pictures (the view caches re-render when a bake finishes,
  clears or loads).
- **Stress `bake` (Windows):**

| Static objects | Texels | Bake time |
|---|---|---|
| 10 | 12k | 0.03 s |
| 100 | 122k | 0.25 s |
| 1,000 | 1.24M | 2.2 s (3 lightmaps) |

  - That is about 500k texels/s and 42–56 Mrays/s, of which filtering and dilation take 3–140 ms.
  - Shading a lightmap costs 1–1.4 ms more at 1080p (10.1 → 11.0–11.5 ms).
  - Odd settings: 30 runs, 0 problems.

## 2026-10-10 (round 34): a faster editor: views and shadows reused until something changes (task 0011)

**Asked:** improve performance for the Scene view and the editor (task 0011 of the approved plan).

**Found**
- **The sun's shadow map was rendered on every shaded render**, up to three times a frame, at about
  8 ms each. `docs/PERFORMANCE.md` and a stress note both said it was cached.
- **Any input re-rendered every visible 3D view from scratch**: a mouse move over a panel, a caret blink,
  a tooltip timer. In a working scene (1600×1000, Scene and Game views, a Camera Preview), an idle frame
  cost about 46 ms.

**Changed**
- **Views reuse their picture while nothing they show has changed (ADR 0009).**
  - **What is cached:** the Scene view, the Game view and the Shaded Camera Preview.
  - **The key:** a content hash of the scene (geometry, transforms, the hierarchy, MeshRenderer flags,
    materials, lights, world, render settings), plus each view's camera, size, toggles and selection.
  - **Never cached:** Rendered shading, piloting, Play while running.
  - **Verify mode** (`BLENDITY_RENDER_CACHE_VERIFY=1`) renders anyway and compares. The unit suite and
    the random-input fuzz both run clean in it.
- **The shadow map is kept until its casters, their materials or the light change.** There are two
  entries, so the Scene and Game views don't evict each other. Each render mesh gets a `serial` the
  caches key on.
- **Clears run in one parallel pass**, and the visibility plane exists only for deferred shading.
- **Overlay lines and points are drawn in parallel bands of rows**, bit-identical to drawing them one by
  one. The F12 supersampling downsample is parallel too.
- **The scene hash no longer evaluates modifier stacks:** `GameObject::evaluated_key` gives the key
  without the mesh.
- **The Profiler window shows where the frame went:**
  - time in the Scene view and its overlays, the Game view, the preview, the shadow map, the UI and
    present;
  - how many views were rendered or reused.

**Fixed (from the review)**
- **The Game view and Camera Preview missed MeshRenderer flags.** Unticking Show in Renders or Receive
  Shadows left them stale. The flags are now in the shared scene hash, which also fixes the
  path-traced preview's key.
- **A modifier's "Show in Edit Mode" didn't redraw the Edit Mode cage.** The cage's evaluated mesh is
  now in the key.
- **Re-parenting a selected object's child** (for example by undo) left its child outline. The
  hierarchy is now in the key.
- **Smaller gaps:**
  - Reopening a scene file now starts fresh caches (each Scene has a `serial`).
  - The raster options are in the Game view and Camera Preview keys.
  - The grid's orbit distance and pivot are in the Scene view key.
  - A batch can't stay open across frames.
- **One test** checks the first three cases in verify mode. Without the fix it finds 3 stale pictures.

**Checked**
- **Linux, dependency-free build, `editor_render`, before → after:**

| Frame | Before (ms) | After (ms) |
|---|---|---|
| Idle | 47.3 | 2.1 |
| Mouse over the Hierarchy | 44.2 | 2.1 |
| Orbiting the Scene view | 43.6 | 9.9 (no shadow pass) |
| Moving an object | 45.1 | 24.7 |
| Editing a material colour | 45.4 | 27.0 |
| Playing | 32.5 | 14.8 |

- **Idle frames with many objects:** 1.9 ms at 1,000 meshes and 5.1 ms at 10,000 (the hash). An idle
  frame with 1,000 empties went from 10.6 to 1.5 ms.
- **Overlays:** 1.15 → 0.77 ms in a normal frame, and 17.5 → 12.4 ms with a 50k-face Edit Mode cage.
- **Edit Mode on a 50k-face mesh is still about 280 ms a change.**
  - The render mesh costs 13–19 ms and the tangents 28–37 ms.
  - Most of the rest is the overlapping-vertex check. It runs on changes made outside a drag; a real
    gizmo drag skips it.
  - Speeding this up is the follow-up (a positions-only mesh update; it needs an ADR).
- **Unit checks:** Windows 3,203, Linux 3,179, sanitizer build 3,101; 0 failed. 270 tests. The
  test-engineer wrote 26 tests and the review added 1:
  - idle and panel-only frames render nothing;
  - fourteen kinds of change each redraw, including a real gizmo drag, an Edit Mode vertex drag, undo
    (bit-identical to before), Play, shading, the World and Exposure;
  - orbiting renders the shadow map at most once;
  - about 55 mixed actions in verify mode leave no stale picture;
  - the overlay batch is bit-identical on 450 random lines and points;
  - the parallel clears.
- **Stress:**
  - `editor_fuzz` (6,000 frames of random input) in verify mode: no crash, 0 stale pictures.
  - `filters`: 0 problems.
- **The whole unit suite** runs in 18 s on Windows (21.7 s before this task); the tests reuse views too.

**Not done from the spec**
- **`is_selected` as a set** and **one render snapshot per frame:** the scene walks and the linear
  selection search don't show in the measurements at 1,000–10,000 objects.
- **`outline_ids` in one pass:** it reuses its mask instead; still two passes when a selection has
  children.
- **A test for the caret blink:** it is the same frame as the idle case (no events, a redraw).

## 2026-10-09 (round 33): leaner, faster unit tests (task 0010)

**Asked:** consolidate the tests, removing redundant or unnecessary ones. The same request also asked for
editor performance, Unity-style baked lighting, probe volumes, and the voxel-based global illumination paper
in `research/papers/`. Those are specced as tasks 0011–0014, approved, and come next. A GPU viewport
follows as task 0015, specced after a measured spike.

**Changed**
- **Per-element checks are counted.** A loop that ran `CHECK` on every pixel or table entry now counts the
  failures and checks once (`count_bad`, which prints the first bad element). Coverage is the same. The
  bloom odd-values test alone had run 53,800 checks.
- **Overlapping tests are merged**:

| Old test | Now covered by |
|---|---|
| filter stack: an empty stack, or one with every effect off, gives the same Game view as no component | The stack part moved into "with no filter the rasterizer's image is unchanged". The Game view part (empty stack, two disabled effects) moved into "PS1 Game view is 15-bit ... with no filter, an empty stack, disabled or neutral effects it is the old picture". |
| camera filters: the component saves, loads (its own values beat the preset), round-trips twice, and clones | "filter stack: a stack saves, loads (every field, its own values beating the preset) and round-trips; clones and Duplicate deep-copy it", which gained a third effect with every field moved off the preset. |
| bloom: a target that is a window into a larger image (row stride) only changes inside the window | "odd sizes: every pass works ... (row stride, Bloom too)" runs Bloom on the same window, with a white patch to spread. |
| Bloom's save / load / set / undo in "bloom: Add Filter > Bloom on the Main Camera ..." | Threshold is Bloom's row in the effect table "every filter effect (Retro Console, Color, Lens, Stylize, Bloom) can be added from the console, set, saved, loaded and undone". The table also gained a Retro Console row, and checks that every registered effect has a row. The bloom test keeps the Game view glow, Intensity and Scatter's save / load / undo (restored after the review), and undoing the add. |
| The PS1 preset fields in "presets: PS1, N64, Saturn and DOS ..." | "Retro Console Filter contributes the PS1 look" (now also checks that there is no screen-door). |
| The two-filter ordering in "Retro Console Filter contributes the PS1 look" | "effects combine in list order". The later snap grid (in both orders) and the "cap of 0 doesn't lift a cap" checks stay. |
| The Camera Preview rebuild in "the component's hash changes with any effect field ..." | "the Camera Preview (Rendered) refreshes when a filter field changes", which now sets the size through the stack's field names, for the first effect and for a second one (E1). The hash test no longer opens an editor. |

- **Checks that tested nothing:**
  - `CHECK(true)` after drawing a window became a check that the window is open.
  - An empty bloom or retro target is checked as untouched.
  - Dead variables are removed, including an unchecked `plain_changes_within_key`, an unused render, and
    an unused `esc` event.
- **Shared helpers:**
  - `kNaN` / `kInf` sit at the top of the file.
  - `cf_draw` is shared by the PS1 helpers and the bloom scene.
  - Each test prints its time.
- **Faster tests.** Sample counts were lowered only with a tolerance worked out from the noise, written in
  the test.
  - **Path tracer:** the MIS reference uses 1024 spp instead of 8192. The ratio of the two means has σ ≈ 0.5%,
    so the 1% check is about 2σ of the estimator. The tracer is seeded per pixel and sample, so it gives
    the same answer on every run; the measured ratio is 0.9965. The path-guiding reference uses 1024 spp
    instead of 4096 (measured agreement 0.4%, bound 4%).
  - **Bloom path-traced F12:** waits for the render to report done, instead of a 6,000-frame sleep loop.
  - **Pilot click:** clicks only where there is geometry. The pilot-frame test reuses the window size it
    found.
  - **Film grain:** 45 ms waits instead of 60 ms. The seed changes every 1/24 s.
  - **Filter-menu tests:** use a wireframe Scene view (only the Inspector is under test), and the
    checkbox sweep steps 4 px.
  - **Bloom odd-values test:** tries every odd value at two sizes, and the other sizes with NaN and
    infinity.
  - **The rotate-shape sweep:** uses a smaller window.

**Checked**
- **Checks:** Windows 3,021 (was 60,624), Linux 2,997 (was 60,600), sanitizer build 2,919. 0 failed.
  243 tests (3 merged away).
- **Time:**
  - Linux wall time (Release, median of 3): **37.5 s → 22.0 s**.
  - Windows, sum of test times: **32.0 s → 21.7 s**.
- **Slowest ten, before → after (Windows, ms):**

| Test | Before | After |
|---|---|---|
| pilot click | 3974 | 194 |
| path guiding | 2264 | 865 |
| rotate a shape on a rectangle | 2249 | 2024 |
| bloom path-traced F12 | 1798 | 223 |
| effect menu clicks | 1283 | 681 |
| pilot frame = Game view | 1218 | 867 |
| film grain in the editor | 1068 | 703 |
| MIS | 1031 | 136 |
| bloom odd values | 1017 | 418 |
| enabled checkbox | 740 | 289 |

- **The spec's 3× target was met by 5 of the 10.** The rest are bound by something the test is about:
  - **rotate-shape:** 20 editors at about 100 ms each to set up;
  - **film grain:** real time has to pass for the grain to move;
  - **pilot frame:** two 1600×1000 editors;
  - **effect menu:** about 25 clicked frames;
  - **path guiding:** the guided render itself.
- **Review fixes:**
  - Bloom's Intensity and Scatter lost their save / load / undo coverage in the merge; it is back in the
    bloom test.
  - The "later snap grid wins" check is back in both orders.
  - The Camera Preview is also checked through a second effect's fields.
  - The statistics comments now give the real margins.
  - Two more per-texel loops are counted.
  - Local NaN constants use `kNaN` / `kInf`.
- **Not done from the spec** (left as they were):
  - **The neutral/identity tests stay separate:** the rasterizer with no filter, the Color / Lens /
    Stylize passes at neutral settings, and Bloom at intensity 0. They check different layers, so one
    table would hide which one broke.
  - **The shared event helpers were not built** (`ev`, `click`, `cmd`, `grab`, `settle`). The copies differ
    in small ways (frames per click, the rows grabbed), and unifying them is a refactor of about 40 tests
    with no change in coverage.
  - **The F12 sequence test (8592), the denoiser test (2088) and the 9833 editor count are not sped up.**
    Each is under 300 ms now, and none is in the slowest ten.
  - **The check count is 3,021, not about 2,500.** The rest are single checks.
- **The slimmer tests still catch bugs.** Three bugs were planted by hand and not committed; each was
  caught:
  - bloom dropping alpha (caught by the counted check);
  - Bloom's Threshold not saving (the new table row);
  - mesh-light NEE biased by +5% (the 1024-spp MIS check: ratio 1.044).

## 2026-10-09 (round 32): Bloom (task 0007)

**Asked:** start task 0007: the Bloom & glow category of the Camera Filters stack.

**Added**
- **Bloom** in Add Filter (Bloom & glow): light brighter than a threshold spills into the pixels around it.
  Settings: intensity, threshold, soft knee, scatter (the glow's spread), tint, and a clamp against
  fireflies. It works from the rasterizer's linear light and from the path tracer's (F12 renders and the
  finished Rendered Camera Preview).
- **An HDR plane in the rasterizer:** linear light before exposure and tone mapping, kept only when a
  camera's stack has a Bloom. The sky and HDRI write their radiance into it too. Without a Bloom nothing is
  allocated and pictures are bit-identical.

**Fixed (from the review)**
- **Bloom undid the effects before it.** Placed after Posterize (or a retro dither, or depth of field), it
  re-encoded the shader's original light and lost their result. It now re-encodes only pixels still as
  shaded, and adds the glow on top of anything an earlier pass changed. A test checks that Posterize's
  levels survive.
- **The glow's size depended on the resolution:** a small Camera Preview glowed far more widely than a 4K
  render. The pyramid now starts from a fixed 540-row base, so the glow covers the same part of the frame
  at any size.
- **A seam at silhouettes against an HDRI or physical sky:** the sky was decoded from 8 bits while the
  object beside it had full linear light. The sky now writes its radiance into the HDR plane.
- **Memory:** the reused buffers are given back when much bigger than needed (after a 4K render).

**Checked**
- 10 tests (9 by the test-engineer, 1 from the review): an emissive quad above the threshold glows, falling
  off with distance, and nothing changes below it; more intensity adds more light and more scatter
  spreads it; no Bloom means no HDR plane and identical pictures; the stack order holds; Posterize's levels
  survive a later Bloom; odd values and 1x1 / 2x2 frames are safe; save, load, undo and Add Filter; a
  path-traced F12 glows. Several check every pixel, which is why the count jumps.
- Unit checks: Windows 60624, Linux 60600, sanitizer build 60522; 0 failed.
- Stress `filters` (1080p, Windows / Linux): Bloom 14.3 / 15.9 ms on decoded colour, 14.0 / 12.9 ms on the
  HDR plane; the HDR plane itself costs 2.5 / 1.6 ms a frame when asked for. Random stacks of every effect type: 300 runs, 0 problems.

## 2026-10-09 (round 31): Color, Lens and Stylize camera filters (task 0006)

**Asked:** start task 0006: the Color, Lens and Stylize categories of the Camera Filters stack. Animated
effects move in Play mode only.

**Added:** twelve effects in Add Filter, each with its own settings, stackable in any order.

| Category | Effects |
|---|---|
| Color | **Color Grading** (in linear light: exposure, contrast through middle grey, saturation, temperature and tint, lift / gamma / gain), **Posterize**, **Grayscale / Sepia**, **Invert** |
| Lens | **Vignette** (intensity, smoothness, roundness, colour), **Chromatic Aberration**, **Film Grain** (strongest in the midtones), **Lens Distortion** (below 0 barrel, above 0 pincushion, with zoom) |
| Stylize | **Pixelate**, **Edge Outline** (lines where objects meet and where the depth creases or steps), **CRT** (scanlines, a curved tube, phosphor stripes, flicker), **Sharpen** |

Film grain and CRT flicker move only in the Game view while playing. F12 renders, camera sequences,
previews and piloting stay reproducible.

**Fixed (from the review)**
- **F12 during Play animated the grain.** Only the Game view animates now; there is a test.
- **CRT flicker vanished at 60 fps.** A 30 Hz sine sampled every 1/60 s sits on its zero crossings. It is a
  random brightness every 1/30 s now; a test checks 60 fps frames.
- **Edge Outline drew a band on floors seen at a grazing angle.** It tests flatness now: the second
  difference of distance and of 1/distance (linear across the screen on a plane, orthographic or
  perspective) instead of a jump in distance. A test checks a grazing floor with an 8 px, very sensitive
  outline.
- **Lift / gamma / gain clamped above 1**, so exposure plus a lower gain flattened the highlights.
- **The grain seed's time** is wrapped to a day, so a huge time can't overflow the cast.
- **Lens Distortion's help** had barrel and pincushion swapped (the test-engineer noticed).

**Checked**
- 19 tests by the test-engineer, plus 2 from the review. For each effect: neutral settings are
  bit-identical, and its behaviour matches hand-computed values (posterize level counts, the sepia matrix,
  exact outline columns, CRT rows and mask, sharpen at a step, +1 stop = x2 linear light). Grain and flicker
  animate only when asked. All 12 effects save, load and undo. NaN, infinite and huge values are safe at
  1x1 and 64x48 and with a row stride.
- Unit checks: Windows 5710, Linux 5686, sanitizer build 5608; 0 failed.
- Stress `filters` (Windows, 1080p, defaults): every effect 0.4-10 ms (Lens Distortion and CRT the slowest).
  300 random stacks of every effect type with every field fuzzed: 0 problems (crash, size or copy-hash).

## 2026-10-09 (round 30): piloting a camera shows its game view (task 0009)

**Asked:** whenever a camera is piloted, its camera filters should show: treat the pilot camera as the game
camera (chosen: the pure game look).

**Changed**
- **Piloting** (Pilot Camera, or `pilot`) shows the camera's Game view picture inside its frame: its filters,
  lens, exposure and depth of field, and the scene's lights, whatever the Shading mode. Around it is a plain
  dark passepartout.
  - The grid, gizmos, icons, overlays and statistics are hidden while piloting.
  - The banner stays, and a thin outline sits just outside the frame.
  - Clicking an object in the frame still selects it.
  - Entering Edit Mode ends piloting, because its cage and selection need the editor's overlays.

**Fixed:** the round-29 Saturn screen-door test failed on macOS (arm64). It checked that every pixel of a
normally blended quad differs from both the veil and the background, and a few didn't. The screen-door checks
themselves are exact and passed; the contrast check now asks for 90% and prints the count.

**Checked**
- Seven new tests (five by the test-engineer, two from the review):
  - The frame equals a second editor's Game view of the same camera at the same size: 0 of 518,870 pixels
    differ, plain and with the PS1 filter.
  - PS1 shows in every Shading mode.
  - With the grid, gizmos and statistics on or off, no pixel changes.
  - The passepartout is uniform.
  - A click selects.
  - Stopping restores the grid.
- Unit checks: Windows 5062, Linux 5038, sanitizer build 4960; 0 failed.
- Review (code-reviewer agent, Opus): changes needed. Fixed:
  - **Depth copied for the editor's tools** used the camera's near / far, so the tools' visibility tests
    disagreed. It is re-projected into the editor's projection now. A test compares it with the editor's own
    render: worst difference 0.0008.
  - **Edit Mode ends piloting.**
  - **No busy redraw** in Rendered mode while piloting.
  - **Documented limit:** with an orthographic camera, lens shift or a letterboxing filter, the editor's
    projection doesn't line up with the picture. Clicks still select what they hit, and the gizmos are hidden.

## 2026-10-09 (round 29): the Camera Filters stack, and the N64, Saturn and DOS looks (task 0005)

**Asked:** make camera filters a general system like Unity's post-processing, where Retro Console is one
category among others (Color, Lens, Stylize, Bloom & glow come in tasks 0006 and 0007), then start task 0005.

**Changed**
- **Camera Filters** replaces the separate Retro Console Filter component.
  - It is one component per camera holding a list of effects (ADR 0008).
  - **Add Filter** lists the effects by category.
  - Each effect has its own toggle, foldout and menu (Move Up, Move Down, Reset, Remove Filter).
  - Order matters: later resolution and texture settings win, the smaller texture cap wins, and colour
    passes run top to bottom.
- **Old scenes and habits still work.**
  - A scene saved with the Retro Console Filter component loads into a stack.
  - `component Retro Console Filter` adds one to the stack.
  - `set RetroConsole.Width 160` reaches the first such effect, and `set CameraFilters.E1Width 160` the
    second.
- **`redo` with no arguments** now redoes. `redo <param> <value>` still adjusts the last operation. The
  plain form didn't work in round 28: an older command of the same name caught it first.

**Added**
- **Retro Console presets:**

  | Console | Resolution | Look |
  |---|---|---|
  | Nintendo 64 | 320x240 | the RDP's 3-point texture filter, 64 px textures with mipmaps, 15-bit colour with a Bayer dither, fog |
  | Sega Saturn | 320x224 | whole-pixel vertices, affine textures, 15-bit colour, see-through surfaces as a checkerboard "mesh" |
  | DOS PC | 320x200 | 256 colours (a 6x6x6 cube plus 40 greys) with an ordered dither, affine textures |

- **The `filter` console command:** `add <name>` (any case), `remove <i>`, `move <i> <j>`, `reset <i>`
  and `list` (which lists the available effects for an empty stack).

**Fixed (found by this round's tests and review)**
- **The DOS palette's nearest-colour table turned 14 palette colours into neighbours**, black among them,
  so a DOS frame could never be pure black. The nearest colour is now computed exactly from the palette's
  structure, and it is faster too.
- **Editing several selected cameras skipped their effects.** Effect edits now carry the same names the
  files use ("E0 Width"), so they reach each selected camera.
- **`set CameraFilters.Effects ...` could replace the whole stack.** The type list can only be changed with
  `filter`.
- **Screen-door pixels** skip shading for the holes and write depth for the drawn pixels (fog and later
  surfaces see them).

**Checked**
- Unit checks: the test-engineer wrote 12 tests (round 29) from the spec, plus a review test that types into
  the Inspector with two cameras selected. Windows 5029, Linux 5005 and the sanitizer build 4927; 0 failed.
- Stress `filters` (Windows, 1080p): every preset is 6.5-8.2x faster than unfiltered; 200 random stacks with
  odd values, 0 problems; every 256-colour frame uses only palette colours.
- Review (code-reviewer agent, Opus): changes needed. All are fixed above: multi-camera editing, `set` on the
  type list, screen-door shading and depth, the stress comment, docs and the console nits.
- Headless screenshots of each preset on the textured scene.

## 2026-10-09 (round 28): component menus work again, steady macOS CI (task 0008)

**Asked:** components couldn't be removed from an object; the macOS build kept failing in CI.

**Fixed**
- **Remove Component, Move Up and the modifier menu did nothing.** A component's or modifier's menu runs
  at the end of the frame, after the Inspector has drawn. Its items wrote to the Inspector's own
  variables, which by then no longer existed, so the click was lost (and wrote to freed stack memory).
  The choice is now kept by the editor and applied on the next frame. This covers Remove Component, Move
  Up and Reset, and the modifier menu's Apply, Duplicate, Copy to Selected, Move and Reset.
- **macOS CI failed now and then.** The Scene view filter test compared frames pixel by pixel, and the
  overlay text changes more pixels on slower machines. It now checks whether the view is filtered (its
  share of 15-bit pixels).

**Added:** console commands `undo` and `redo`.

**Checked**
- Two new real-event tests: they click a component's menu (Move Up, Reset, Remove Component, then undo)
  and a modifier's menu (Duplicate, Apply).
- Unit checks: Windows 4298, Linux 4274, sanitizer build 4196; 0 failed.
- Review (Opus): approved. Its suggestions are in:
  - the Keymap window's Category dropdown read freed strings while open (same kind of bug);
  - a menu choice not applied by the next frame is dropped;
  - Transform > Reset finds the object by id;
  - the test now really picks Remove and then selects another object, and the Cube keeps its
    component.

## 2026-10-09 (round 27): camera filters, with the PS1 look (task 0004)

**Asked:** camera "filters" built from shaders, starting with one that recreates 3D graphics on the
PlayStation 1 and older consoles. Decisions taken with the user:
- filters are components on the camera;
- they show in the Game view and Play mode, the Camera Preview, F12 and sequences, and the Scene view
  through a toggle;
- presets for PS1, N64, Saturn and DOS (N64, Saturn and DOS are task 0005).

**Added**
- **Retro Console Filter** (Add Component > Rendering), with a Console preset (PS1). Its fields:
  - Width / Height and Fit (Fill View with square pixels, or Letterbox to the console's frame);
  - Vertex Snap with a grid;
  - Affine Textures;
  - Texture Filter, Max Texture Size and Mipmaps;
  - Colour Depth (24 / 15-bit) and Dither (PS1 4x4 / Bayer);
  - Fog with start, end and colour.
- **The PS1 preset:** 320x240, vertices on whole pixels, affine textures, nearest 256 px textures with no
  mipmaps, 15-bit colour through the PS1's dither matrix.
- **Where filters render:** everywhere the camera's image shows, through one `Editor::render_camera`.
  - A path-traced render through a filtered camera traces at the low resolution and gets the colour pass.
  - The Scene view's new camera button (or `filters on|off`) previews the main camera's filters there.
- **Speed:** a filtered camera renders fewer pixels: the PS1 look at 1080p took 2.4 ms against 18.3 ms
  unfiltered.
- ADR 0007 (camera filters), requirement R-10.

**Fixed**
- **Components with spaces in their names** couldn't be added from the console (`component Retro Console
  Filter`), and were dropped when a scene was loaded. Both take the whole name now.
- **A texture could be freed while it was being drawn.**
  - When several shading threads met a material whose textures weren't loaded yet, each loaded them and
    replaced the others' (AddressSanitizer: heap-use-after-free).
  - Both renderers now load materials' textures before shading in parallel.
- **NaN checks under `/fp:fast`.** The Windows build is compiled with `/fp:fast`, which lets the compiler
  assume no NaNs. `display::linear_to_srgb8(NaN)` returned 255 or 0 depending on how the binary was inlined
  (an existing test failed once new code changed the inlining). NaN tests now read the bits
  (`nan_bits` / `finite_bits`).

**Checked**
- Unit checks: Windows with libraries 4280, Linux 4256, sanitizer build 4178; 0 failed.
  - The test-engineer agent wrote 14 tests from the spec. One of them found the save/load bug.
  - Two Linux failures were in the tests, not the code, and the tests were fixed:
    - a straight-edged rectangle can't show sub-pixel creep, so the creep check uses a slanted quad;
    - the Scene view's log line needs a 1% tolerance.
- Stress `filters`:
  - the PS1 look at 1080p: 2.4-2.9 ms against 18.3-24.7 ms unfiltered;
  - no 15-bit pixel off the 5-bit grid;
  - 300 runs of odd settings (NaN / infinite snap and fog, 1x1 to 100000 px), 0 problems.
- Same-session A/B of `shading` against `main` (Linux): equal within noise, so the filter code costs
  nothing when unused.
- Headless screenshots of a textured scene: no filter, PS1 Fill, PS1 Letterbox, the Scene view toggle.
- Review (code-reviewer agent, Opus): changes needed. Fixed:
  - the texture warm-up now runs in every shade mode (the see-through pass shades materials in Solid
    view too), and its lock is re-entrant, so a texture load's own parallel_for can't deadlock;
  - the path tracer resolves textures at the start of every render call, not only at build;
  - stats survive a filtered render (overlays rebind the renderer without resetting them);
  - the Scene view always uses Fill, so grid, gizmos and vertex picking match the picture.

## 2026-10-09 (round 26): sanitizer findings fixed, branch protection on (task 0002)

**Asked:** after merging tasks 0001 and 0003, deal with the current issues before the camera
filters: sync, branch protection, and the sanitizer findings.

**Changed**
- `main` is protected:
  - pull requests only;
  - the `windows`, `linux` and `macos` checks must pass, on a branch up to date with `main`;
  - no force pushes or deletions;
  - 0 approvals, since GitHub doesn't count approving your own PR.
- `linux-sanitizers` is blocking now and joins the required checks once this merges. It also checks
  float-to-int casts that overflow (`float-cast-overflow`, which GCC leaves out of `undefined`).
- `CLAUDE.md` says how to call `gh` from the Bash tool and how to read CI.

**Fixed** (both undefined behaviour; neither showed as a wrong result yet, but a compiler is free to
miscompile them):
- **Merge by Distance and proportional editing far from the origin.** Their spatial hash multiplied
  grid cells in `int`. A point at x = -2 with a 0.1 mm distance already overflowed, and far points
  over tiny distances overflowed the cell index itself. One shared helper now uses 64-bit cells and
  unsigned hashing.
- **JPEG decoding** shifted a negative number (`-1 << s`) for every negative coefficient.

**Checked**
- Sanitizer build (ASan + UBSan + float-cast-overflow, dependency-free): 1832 checks, 0 failed, no reports.
- Windows with libraries: 1934 checks, 0 failed. Linux with libraries: 1910, 0 failed.
- The merge stress section: hash and O(n²) reference agree, speed unchanged.

## 2026-10-09 (round 25): CI passes again (task 0003)

**Asked:** continue with the next task, making CI pass on all three platforms so branch protection can
be switched on.

**Fixed**
- **`build.sh` stopped before compiling without Blender's libraries.** The object folder's name was
  built with `[ ... ] && echo _libs`. That command fails when the libraries are absent, and `set -e` ended the
  script without a message. This broke every Linux and macOS CI run.
- **`build.sh` was not executable in git** (mode 644, committed from Windows), so CI's
  `./build.sh` failed with "permission denied" (exit 126) before even reaching that line. It is
  now 755.
- **macOS saved numbers with nine digits.** Apple's C++ library doesn't announce floating-point
  `to_chars`, so scene and material files used `%.9g` there: 0.7 was written as `0.699999988`, and a
  material test looking for "0.7" failed. The fallback now writes the fewest digits that read back as the
  same number, as `to_chars` does, so files are identical on all three platforms.

**Changed:** CI's unit-test steps turn each failing check (and each sanitizer report) into a GitHub
annotation, so failures can be read on the run page without signing in to view the logs.
The quick stress step has hung on CI until GitHub's 6-hour limit on every Windows run so far (and now
on Linux), though it takes under 90 s locally even on one core. It now stops after 15 minutes, and
`blendity_stress` reports each section's start and end on stderr, so the run page shows where it was.

**Fixed: the stress test never ended on 3 or 5 threads.** The job-system section steps through thread
counts and, to end on the machine's count, stepped back to half of it. With 3 or 5 threads (GitHub's
macOS and Linux runners) that step-back repeated forever. Now the counts are listed first (1, 2, 3, 4, 8,
16... then the machine's own). This was the hang that kept every Windows CI run going for 6 hours.
- **Push Through out through several faces in one plane needed Manifold.** Pushing a rectangle
  through a wall whose back already had a circle, or an archway where the back had a rectangle across
  it, was refused in the dependency-free build. Now the outline is cut across the back faces and
  the front outline gets a corner opposite each new back corner, then a tube joins the two. Both builds
  take this path, so they agree. Manifold is used only when the exit faces aren't coplanar.

**Checked**
- Unit checks:
  - Windows, dependency-free: 1822, 0 failed.
  - Linux, dependency-free: 1822, 0 failed.
  - Windows with libraries: 1924, 0 failed.
  - Linux with libraries: 1900, 0 failed.
- Linux, dependency-free: the quick stress pass and the headless screenshot run as CI runs them.
- Push/Pull stress: 0 problems; 394 results with faces newly on top of each other (unchanged).
- Review (code-reviewer agent, Opus): approved. It suggested the cut be checked to form the opening's outline edge for edge before the tube is built, so a face missed inside it can't leave edges with three faces; that check is in.

## 2026-10-09 (round 24): engineering setup for agentic development (task 0001)

**Asked:** set the repository up for research-informed agentic development:
- standards, three agents (implementer, test engineer, code reviewer), requirements, decision records and task specs;
- CI for the real stack;
- a plan / implement / test / review / human-merge workflow.

**Added**
- `CLAUDE.md`: the project map, build and test commands, testing rules, code standards, documentation duties, approval gates and the publishing procedure.
- `.claude/agents/`:
  - `implementer` (Sonnet): one approved task; no architecture, dependencies, commits or pushes.
  - `test-engineer` (Sonnet): tests from the acceptance criteria; edits only `tests/` and `stress/`; reports bugs as failing tests.
  - `code-reviewer` (Opus): read-only; ranked findings and a verdict.
- `.claude/settings.json`: denies force pushes, pushes to `main` / `blendity`, merging PRs and hard resets; asks before commits, pushes and `gh pr create` / `gh api`.
- `docs/requirements.md`: product, quality and constraint requirements with IDs.
- `docs/decisions/`: the index, a template and six ADRs recording decisions already in force:
  1. optional Blender libraries;
  2. licensing;
  3. copy-on-write meshes;
  4. SketchUp Push/Pull semantics;
  5. tool settings in the F9 and drag helpers;
  6. subtree publishing with human-merged PRs.
- `docs/tasks/`: the lifecycle, a template, task 0001 (this) and draft task 0002 (sanitizer findings).
- `docs/ARCHITECTURE.md`: a system overview (layers, one frame, one edit), key invariants and module ownership at the top. The phase tables are kept below as the history.
- CI: a `linux-sanitizers` job (unit tests under ASan + UBSan, non-blocking until task 0002), and cancelling superseded runs.
- `build.sh` honours `EXTRA_FLAGS`.
- `.github/pull_request_template.md`.

**Changed:** work reaches `main` only through a pull request that CI must pass and the human merges. There are no more direct pushes (ADR 0006).

**Found while checking:**
- **CI has failed on every recent push to `main`.**
  - `build.sh` exits silently before compiling when Blender's libraries are absent (always the case in CI).
  - Two Push Through tests rely on the optional Manifold library.
  - Recorded as task 0003, to finish before branch protection is switched on.
- **The sanitizer job's first finding:** a signed overflow in `merge_by_distance`'s spatial hash (task 0002).
- **Local checks:** Windows 1924 and Linux 1900 unit checks pass with Blender's libraries. With `build.sh` corrected, the sanitizer build compiles and runs.

## 2026-10-09 (round 23): arched shapes in one piece, array copies past a surface's edge

**Asked:** arraying a vertical rectangle with an arched top cut some copies improperly.

**Fixed**
- **The arch itself was two faces.** An arc drawn on a rectangle's top crosses an edge of the faces around the rectangle. The pieces inside the arc were merged, except where the crossed edge ran to the arc's own end (the rectangle's corner), so the arch stayed split. The array then copied both pieces. Edges from a crossing to the arc's ends are now dissolved too, so the arch is one face, and so is each copy's.
- **Copies past the surface's edge.** A copy hanging past the edge of the wall used to fall back to a loose duplicate lying over the wall (overlapping faces, an open mesh). Now:
  - The part of it on the surface is cut in: clipped to the surface's outline when that outline is convex, as a wall's is.
  - A copy with nothing under it at all is still a separate copy.
  - A cut that can't be made leaves that copy out rather than half of it.

**Checked:** 1924 unit checks on Windows and 1900 on Linux, 0 failed. New test: an arched doorway on a wall arrayed 2 and 3 times, with 8 and 16 arc segments. The doorway area must be exact, or exact plus the clipped part for the copy past the wall's end. No overlaps, still closed, no bad triangulations, no wire edges.

Stress:
- Push/Pull: 0 problems.
- The modeling section: 0 problems.
- Curved surfaces: 8 problems.
- The fuzzer: no crash.

## 2026-10-09 (round 22): Merge Coplanar on a selection, Array copies cut into the surface

**Asked:** Merge Coplanar on a selected group of faces; Array copies of a face should cut into the mesh like the original instead of lying on top of it.

**Added**
- **Merge Coplanar on the selection.**
  - With faces selected (in any mode, a face counts when all its corners are selected), only the edges between two selected coplanar faces dissolve, and only corners that no unselected face uses go. The rest of the mesh keeps its edges, and the merged faces stay selected.
  - A 3 x 3 block of the Plane becomes one 12-cornered face (it keeps the corners its neighbours share, so there are no T-junctions); two separate blocks become two faces.
  - With nothing selected it works on the whole mesh, as before.

**Changed**
- **Array copies are drawn onto the surface.**
  - Each copy of a shape that sits on a face is cut into the faces it lands on, as drawing the shape there would, instead of a duplicate lying on top (z-fighting). A copy crossing the faces round an earlier drawing becomes one face.
  - Only a copy that lies wholly on the surface is cut in; one hanging off it, or a face with nothing flat under its copy (a box's whole top), is still a separate copy.
  - The originals and all copies end up selected, ready for Push/Pull or Recess together.

**Checked:** 1916 unit checks on Windows and 1892 on Linux, 0 failed. New tests:
- Merge Coplanar on a 3 x 3 block, two blocks, and nothing selected.
- A drawn rectangle arrayed three times across a cube's top: cut in, no overlaps, still closed, no bad triangulations. The three pushed in together stay closed; a copy with nothing under it stays a separate copy.

Stress:
- Push/Pull: 0 problems.
- The modeling section: 0 problems.
- Curved surfaces: 8 problems.
- The fuzzer: no crash.

## 2026-10-09 (round 21): tool helpers, settings out of the Inspector, Push/Pull on non-manifold meshes, overlap marking

**Asked:**
- Drop "Fluent" from Hard Surface, and add a helper for the tools such as Array.
- Make Push/Pull work on non-manifold meshes.
- Edges were marked as overlapping when they weren't: only the smaller edge should be marked.
- Thickness, draft angle, extrude distance and the like belong in each tool's helper, not in the general area under Draw.

**Changed**
- **Hard Surface** (no "Fluent" in the interface). Grid, Pipe, Array, Taper Extrude and Recess / Plate:
  - Clicked in the menu or the Inspector, each opens a **drag helper** like Ctrl+B's. The mouse sets the spacing, radius, distance or depth, or you type it. The wheel steps the count, columns (Shift: rows), sides, end scale or border. X / Y / Z set Array's axis. Click or Enter confirms, Esc cancels.
  - Afterwards each one is in **Adjust Last Operation (F9)**.
- **Tool settings live with the tools.** The Inspector's Edit Mode area keeps only the switches that change how tools behave: Push/Pull Each Face, Inset Individual, Auto Fuse, Auto Smooth and Sharp Angle.
  - Each tool's numbers (thickness, angle, distance, segments, profile, count...) are in its F9 panel or drag helper.
  - Shell, Thicken and Draft are now adjustable (F9) operations too.
  - Whatever you set there is kept for that tool's next use.
- **Overlapping edges:** of two edges along each other, only the shorter one is marked and selected. That's the piece lying on a longer edge, or one of two doubled edges; the long edge it lies on is left alone.

**Fixed**
- **Push/Pull on non-manifold meshes.** Pushing a face in next to a fin, or next to a second solid sharing the edge, was refused as "the faces around the selection lean over it". The fin or the other solid counted as a wall of this one. Only the faces that carry the surface on across the edge count now. Tested on:
  - a fin (pushing and pulling the top, the front and the fin itself);
  - two cubes sharing an edge, and two sharing a corner;
  - an open box;
  - a cube with a stray wire edge and a loose point;
  - two faces at once.

**Checked:** 1906 unit checks on Windows and 1882 on Linux, 0 failed. New tests:
- The Array helper from the menu: wheel, Z, a typed spacing, Enter. Grid, Taper and Recess open their helpers and Esc leaves the mesh as it was.
- The non-manifold Push/Pull cases.
- No overlap alarms on the primitives, the teapot, a bevelled, a subdivided and a drawn-on cube; only the shorter edge selected.
- Shell, Thicken and Draft as F9 operations.

Stress:
- Push/Pull: 0 problems, and 394 results with faces newly on top of each other. That's 4 more than before, from pushes beside fins that used to be refused.
- The modeling section: 0 problems.
- Curved surfaces: 8 problems, the same as last round.
- The fuzzer: no crash.

## 2026-10-09 (round 20): bevel profile, arcs on drawn shapes, smooth region inset

**Asked:** a profile for the bevel, to make it concave or convex; an arc drawn on a rectangle drawn on a surface didn't make a face inside the arc; insetting several faces "stepped" instead of changing smoothly.

**Added**
- **Bevel Profile** (Blender's superellipse), 0 to 1:
  - 0.5 is a round quarter circle, 0.25 a flat chamfer.
  - Toward 1 it bulges out to a square corner (convex); toward 0 it scoops in (concave).
  - In the Inspector (Bevel Profile), the F9 panel, the Ctrl+B helper (Alt+wheel, shown in its readout), the Bevel modifier (saved with the scene), and the console (`bevelprofile 0.8`).
  - Profile 0.5 is now an exact quarter circle; before, it was a quadratic curve slightly flatter than a circle.

**Fixed**
- **Arcs (and other open lines) across the faces round a drawn shape.**
  - A shape drawn on a face sits in a ring of several faces. A line crossing their edges only cut a face that held all of it, so elsewhere it fell back to loose wire edges.
  - The line is now split where it crosses edges, and each face it runs through is cut. The pieces inside an arc (between it and the chord joining its ends) become one face again.
- **Dissolve Edges** could drop a corner that lay on a straight line in the merged face while a neighbouring face still used it, opening the mesh. Such corners now stay.
- **Inset Region no longer steps.** When an inset would fold over (wider than the region), it used to halve the thickness, so dragging jumped between values. It now finds the furthest thickness that doesn't fold (the middle face keeps at least 2% of its area, and no ring quad crosses into a bow-tie), so dragging slides smoothly to that limit and holds there. A region that folds at any thickness is left as it was.
- **Scripted drawing points** on an edge shared by two faces belong to the face that faces the camera, as a click would.

**Checked:** 1838 unit checks on Windows and 1814 on Linux, 0 failed. New tests:
- Profiles 0 to 1 give a rising volume; flat equals a one-segment chamfer; round equals a quarter circle; the editor setting reaches Ctrl+B and the modifier saves its own.
- Arcs on a drawn rectangle: side to side out and in, corner to corner, across a corner, out to the cube's edge, and on the cube's own edge. No wire edges, still closed, and one face inside the arc where it encloses one.
- Region inset swept from 0 to 0.8 in 0.01 steps: no jumps, never growing back, holding at the limit.

Stress:
- Push/Pull: 0 problems.
- The modeling section: 0 problems.
- Curved surfaces: 8 problems. 4 are new and come from Recess on regions that touch themselves at a sphere's pole, which can leave two corners in one place.
- The fuzzer: no crash.

## 2026-10-09 (round 19): Follow along the edges themselves, no Open Space Plane, Push/Pull on several faces

**Asked:** Follow with picked edges didn't follow the edges (especially several of them); remove the Open Space Plane, since guides do that work; Push/Pull on several faces at once, each along its own normal.

**Fixed**
- **Follow along picked edges** sweeps the face along the edges where they are, from the path's end nearest the face (SketchUp's Follow Me). Before, it carried a copy of the path's shape over to start at the face's centre, so the result ran beside the edges instead of along them. The face's own sides, still selected after switching from face to edge mode, no longer join the path and break it.

**Changed**
- **No Open Space Plane.** The plane chooser (front, side, view, last face), its offset and Draw in Open Space Only are gone. A click off the mesh lands on the ground, and guides (X / Y / Z, Ctrl+click an edge) take a shape anywhere else.
- **A new drawing's turn** follows the shape's first side, so a rectangle drawn at an angle gives its object that angle.

**Added**
- **Push/Pull on several faces.** Each connected group of selected faces moves along its own normal; separate groups move together (top and bottom of a box both out, say).
  - **Push/Pull Each Face** (Inspector, F9's Each Face, `pushpull individual on`) moves every selected face along its own normal.

**Checked:** 1716 unit checks on Windows and 1692 on Linux, 0 failed. New tests:
- Follow along a bent rail of quads, directly and through the editor with a stray side of the face selected; the end cap must sit at the path's end.
- Clicks off the mesh land on the ground.
- A 3-point rectangle at 30 degrees: the object's origin and X.
- Push/Pull on top and bottom, on every face of a cube, and with P on two sides in the editor.

Stress:
- Curved surfaces now include Push/Pull Each Face on curved regions: 5,036 operations, 4 problems. Pushing neighbouring faces of a convex surface in, each along its own normal, can make their walls meet (8 results), as Blender's Extrude Individual does.
- Push/Pull: 0 problems and 390 overlaps.
- The modeling section: 0 problems.
- The fuzzer: no crash.

## 2026-10-08 (round 18): Plasticity guides and perpendicular snaps, one Make Face, overlapping elements, Fluent-style tools, bevel and rotation fixes

**Asked:** commit and push round 17; rotating a face drawn on a rectangular plane still left artifacts; a 90-degree edge snap for polylines like Plasticity's; Ctrl+B sometimes bevelled at once without the helper, and the helper wouldn't go past two segments; guides like Plasticity's (from an axis, local or global, and from edges with Ctrl) instead of axis planes; the Make Face shortcut didn't always work; shapes drawn at an angle kept a global transform (pivot / centre, local / global); a way to see overlapping vertices and edges; Make Face and Smart Fill as one operation; keep Follow like UModeler's; tools from Fluent 4 that fit the current ones.

**Done first:** round 17 committed and pushed.

**Fixed**
- **Bevel's helper (Ctrl+B).** Auto Smooth shaded the bevel after it was recorded, and that change made the helper think the bevel had been undone, so it quit and left the bevel as it was. That happened as soon as there were 2+ segments: a second Ctrl+B bevelled at once, and the wheel stopped at two. Shading is now part of the operation. The helper stays, and the wheel goes up to 64 segments.
- **Turning a shape drawn across the Plane's grid.** When the turned shape reached past the cells it was cut into, the ring around it couldn't be rebuilt. The flat faces around it now join the ring as needed.
- **Concave quads were drawn wrongly.** A dart-shaped quad was split along the diagonal that runs outside it, so one triangle faced backwards. The other diagonal is used now. This also removed the overlaps from drawing circles across curved faces (35 to 0 in the curved stress).
- **Make Face** works in vertex, edge and face mode, and its key works anywhere in Edit Mode, not only with the mouse over the Scene view.
- **A new drawing takes its shape's place.** The first shape drawn in a new Drawing object moves the object's origin to the shape's centre and turns the object with the shape's plane, so Pivot, Local and Global behave.
- **Local in Edit Mode** follows the selection: Y along the selected faces' normal, X along their longest edge. The gizmo and G/R/S now move, turn and scale a shape on a slope, or turned on its face, along its own sides.

**Changed**
- **Guides instead of axis planes** (Plasticity's construction lines):
  - X, Y or Z while drawing lays a guide along that axis, Local or Global as Snap Axes says, through the last point (or the point under the mouse before the first click).
  - Ctrl+click on an edge lays a guide along it.
  - A polyline can follow a guide off its plane, which makes a 3D path for Follow.
- **Make Face and Smart Fill are one operation**, Make Face (F in the Blender keymap, Alt+F in Unity's):
  - Holes and wire loops in the selection close (every hole when nothing is selected): flat ones with a face, bent ones with a fan, cracks welded shut.
  - Selected vertices that form no loop get one face through them.

**Added**
- **Perpendicular Snap** (Plasticity): a point on an edge where the line from the last point meets it at 90 degrees. On by default; switch it in the Inspector or Modeling Tools, or with `drawmode perp on|off`.
- **Overlapping vertices and edges:**
  - Vertices lying on top of each other get yellow rings, and edges running along each other get magenta lines, live in Edit Mode.
  - Modeling Tools > Check: Overlapping Vertices & Edges counts them and has Show, Select and Merge.
  - Also as Edit Mode tools (Select Overlapping, Merge Overlapping) and the console: `overlaps [select|merge|show on|off]`.
- **Follow, UModeler's way.** Select the face and press Follow; with no wire path drawn from it, the face is remembered. Then select the path's edges in edge mode and press Follow again. The path starts at the face, from whichever end leaves it along its normal, and the picked edges stay.
- **Hard-surface tools after Fluent 4** (a new Hard Surface group in Edit Mode; settings in the Inspector; console `hs ...`):
  - **Grid:** cuts quads into columns x rows, splitting neighbours to match.
  - **Pipe:** a capped tube along selected edges or a drawn polyline.
  - **Array:** repeats the selected faces along the selection's own X, Y or Z.
  - **Taper Extrude:** an extrusion with its end scaled.
  - **Recess / Plate:** an inset panel pushed in, or pulled out when the depth is negative.

**Checked:** 1718 unit checks on Windows and 1694 on Linux, 0 failed. New tests:
- Ctrl+B three ways (mouse over the Scene view, elsewhere, and a second bevel starting at 7 segments) with six wheel steps.
- Shapes turned 15 to 90 degrees on a quad, on the 10 x 10 Plane across and inside cells, and on long faces (where a shape turned past its face's edge has no surface left around it, only validity is required).
- The perpendicular snap, on and off.
- Ctrl+click and X guides on a turned cube, Local and Global.
- A 3D polyline along guides, then Follow.
- Make Face with both keymaps, in each mode, with the mouse away from the Scene view, plus loose vertices.
- Overlap finding, selecting and merging.
- Follow along picked edges.
- A rectangle drawn on the view plane (the new object's origin and up) and the Edit Mode frame.
- Each Fluent tool, closed where it should be.

Stress:
- Curved surfaces: 4,876 operations including the new tools, 4 problems, and 69 results with faces on top of each other (91 before).
- Push/Pull: 0 problems, and 390 results with faces newly on top of each other.
- The modeling section: 6,080 operations with 0 problems.
- The editor fuzzer: 6,000 frames with no crash.

## 2026-10-08 (round 17): drawing on any axis plane, turning drawn shapes cleanly, the keymap dropdown, the Utah teapot, deleting materials, curved surfaces

**Asked:** commit and push round 16; draw polylines on the YX and YZ planes as well as XZ, with an axis-lock workflow like Extrude's, usable with Follow; rotating a face just drawn onto a face left artifacts; the keymap preset dropdown in Preferences disappeared when clicked; the Utah teapot with a camera and a point light as the scene you start with; deleting a material from the Materials window; more edge-case testing on curved surfaces for Push/Pull and the other tools.

**Done first:** round 16 committed and pushed.

**Fixed**
- **Dropdowns inside dialogs.** A dropdown opened inside Preferences (the keymap preset, the frame-rate cap) closed every popup, the dialog included, so the list vanished. It now opens on top of the dialog. Picking an item, or clicking the field again, closes only the list.
- **Turning a shape drawn on a face.** Its ring of faces twisted over itself because the inner corners turned while the outer ones stayed. The ring is now rebuilt between the two outlines whenever a move folds it, live during gizmo drags and Blender's G / R / S. The selection follows.
- **Follow on a face of a solid** no longer leaves the old face inside as a start cap. That cap put three faces on each of its edges. The solid now grows the way Extrude grows it.
- **Regions that touch themselves at a vertex** (a cone's apex between two arms of a selection, a pinch on a torus) can now be extruded, inset and refilled without an edge ending up in four faces. Each such vertex gets one copy per side.
- **Smart Fill** fills holes whose outline touches itself one lobe at a time. Before, it made one face that ran through the shared vertex twice.
- **Inset Region** asked to go further than the region is wide now backs off until no face turns over, instead of folding the ring onto the inner faces.
- **Bevel** on curved surfaces no longer leaves zero-area patches where two bevels meet. The twin arcs are welded instead.
- **Push/Pull through a curved region** no longer leaves faces lying on the opening. When the cut can't open the far side cleanly, it stops at the far side, as it already does for other unclean holes.

**Added**
- **Axis planes while drawing.** X, Y and Z put the shape on the YZ, XZ or XY plane, like an axis lock on Extrude:
  - Before the first click, the plane goes through the first point. Pressing the same key again goes back to the surface's own plane.
  - Mid-polyline, the plane turns through the last point, so one line can climb a wall and then run across a floor. Enter keeps it as wire edges: a 3D path for Follow.
  - A closed loop that turned planes stays a loop of wire edges, since it isn't flat.
  - Also in the Inspector and Modeling Tools (Axis Plane), and the console: `drawmode axis x|y|z|none`.
- **The Utah teapot.** GameObject > 3D Object > Teapot is Martin Newell's 32 Bezier patches, tessellated and welded, with UVs. A new project, and File > New Scene, now start with it on a ground plane, along with a Main Camera and a Point Light (Blender's start-up cube, camera and light). Your existing SampleScene is unchanged.
- **Delete Material** in the Materials window, from the right-click menu or the Delete key over the window:
  - It asks first and shows how many slots use the material.
  - Those slots are emptied, so their faces get the default material.
  - A material asset's .mat file goes to the Recycle Bin.
  - Undo puts the slots back.
  - Console: `deletemat <name | Assets/...mat>`.
- **A curved-surfaces stress section** (`blendity_stress --only curved`). It runs 4,396 operations on 10 curved meshes: UV and ico spheres, a torus, a cylinder, a cone, Catmull-Clark and bevelled boxes, a bumpy blob, the teapot and an open hemisphere. The operations:
  - Push/Pull on single faces and on curved regions, in, out, through and onto.
  - Inset (individual and region), Extrude, Bevel, Poke, Triangulate / Tris to Quads.
  - Shapes drawn on and across curved faces, then turned.
  - Follow, Smart Fill, Bridge, Push Through, Delete Loose, Merge by Distance and Auto Smooth.
- Console: `newscene`.

**Checked:** 1643 unit checks on Windows and 1619 on Linux, 0 failed. New tests:
- The preset dropdown in Preferences opened, closed and picked with real clicks while the dialog stays open.
- The teapot's welding, facing, UVs and size.
- The starter scene, including save/load and New Scene.
- Drawing up and over a cube with Z then X, then Follow along that path into a closed solid.
- A bent closed loop staying wire edges.
- A drawn rectangle turned 30, 45, 90 and 170 degrees with R (no bad triangulations, no overlaps, still closed, still selected), and the ring repair on its own.
- Delete Material with its confirmation, a scene material, an asset going to the (scratch) trash, and undo.
- A cone region pinched at its apex extruded, inset, and refilled with Smart Fill.

Stress:
- Curved surfaces went from 59 problems to 4 (zero-area faces in rare corners) and from 283 results with faces on top of each other to 91.
- Push/Pull: 0 problems, and the results with faces newly on top of each other fell from 676 to 390.
- The modeling section: 6,080 operations with 0 problems.
- The editor fuzzer: 6,000 frames with no crash.

## 2026-10-08 (round 16): drawing in open space, a UV gizmo, Merge by Distance in Edit Mode, Z-fighting highlights, Delete in the Hierarchy

**Asked:** commit and push round 15; draw outside of a face (UModeler) or in open space (Plasticity); the Scene view's gizmo in the UV editor for moving, scaling and rotating; Blender's Merge by Distance; Z-fighting faces stayed highlighted in Object Mode and after the object was deleted; the Delete key for objects in the Hierarchy.

**Done first:** round 15 committed and pushed.

**Fixed**
- **Delete in the Hierarchy.** In Edit Mode, Delete removed the selected faces of the mesh being edited even with the Hierarchy focused. It now removes the selected objects there (leaving Edit Mode first), as in Unity.
- **The Project window's Delete key** now also needs the mouse over the Project window, so a Delete meant for the Hierarchy or the Scene view can never reach a file selected there earlier. (Several files went to the Recycle Bin this afternoon while the editor was open, which is what this guards against.)
- **Z-fighting highlights** are dropped once their object is deleted or its mesh changes. They only show while you edit one of the objects involved, or while the Z-Fighting panel is open after checking from it, so nothing looks selected in Object Mode.
- **Drawing rectangles:** the opposite corner no longer snaps onto the first corner's axes, which could flatten the rectangle into a line when seen at a shallow angle.

**Added**
- **Drawing in open space** (Inspector and Modeling Tools, while drawing):
  - Clicks off the mesh go onto an **Open Space Plane**: the ground, a **front** (XY) or **side** (YZ) plane, a plane **facing you** through the point you orbit, or the **last face's plane**, to keep drawing past a face.
  - A **Plane Offset** moves the plane along its normal.
  - **Draw in Open Space Only** ignores surfaces like Plasticity's construction plane. Corners and edges of the mesh still guide the point, which stays on the plane.
  - A faint grid shows the plane under the cursor. A closed shape there becomes a new face; an open one becomes wire edges.
- **The UV editor's gizmo**, like the Scene view's, at the centre of the UV selection:
  - **Move** has U (red) and V (green) arrows and a centre square for free movement. **Rotate** has a ring. **Scale** has U and V handles and a centre box for uniform scaling. **Transform** (Y) shows all three.
  - W, E, R and Y pick them in the UV editor too, and Ctrl snaps.
- **Merge by Distance in Edit Mode** (Merge & Clean Up; Blender preset: Alt+M):
  - Selected vertices closer than the distance weld (all of them when nothing is selected), and it reports how many were removed.
  - F9 adjusts the **Merge Distance** and **Unselected**, which lets selected vertices weld onto unselected ones, as in Blender.
- Console: `drawmode plane ground|front|side|view|face [offset]`, `drawmode space on|off`, `zfight clear`, `delete`, `editop merge_distance`.

**Checked:** 1573 unit checks on Windows and 1549 on Linux, 0 failed. New tests:
- Clicking a Hierarchy row and pressing Delete or Backspace, also from Edit Mode, with a Project file selected and focused earlier that must survive.
- Drawing with real clicks on the front plane at a depth, on the view plane, and "open space only" over a cube.
- Merge by Distance on an unwelded cube (24 to 8 vertices, closed), its F9 distance, selected only, and Unselected.
- Dragging the UV gizmo's U arrow, V scale handle and rotate ring with real mouse events.
- Z-fighting outlines gone in Object Mode, and pruned when the objects are deleted.

Stress: Push/Pull 0 problems; the modeling section 6,080 operations with 0 problems; the editor fuzzer 6,000 frames with no crash.

## 2026-10-08 (round 15): rebindable keymaps with presets, draw axes, a Z-fighting check, UV editing tools, grouped Edit Mode tools

**Asked:** commit and push round 14; local or global snap axes for drawing; put the selection tools in one place and combine other tools where they can be; a Z-fighting check that says which faces are a problem and can be removed; more UV tools (selecting faces, rotating, scaling, selecting and moving whole islands); re-render the Camera Preview whenever a light's colour temperature changes; rebindable shortcuts in Preferences, with keymaps for other programs (Blender, Maya and more).

**Done first:** round 14 committed and pushed.

**Fixed**
- **Colour temperature didn't refresh the Camera Preview.** The scene's render fingerprint, which decides when the preview, the Rendered view and the live preview re-render, only included a light's type, colour, intensity and range. It now includes every light setting: temperature, spot angles, area size, shadows.

**Added**
- **Keymap** (Preferences > Keymap):
  - Every shortcut is a named action (about 75) with a context: anywhere, in Edit Mode, over the Scene view, or the Scene view in Edit Mode. So one key can mean different things in different places, as before.
  - **Presets:** **Unity** (Blendity's own), **Blender** (G/R/S, E, X, M, F, Ctrl+R...), **Maya** (Q/W/E/R, F8-F11 for component modes, B for soft select...), **3ds Max** (W/E/R, Z to frame, 1/2/4 sub-objects, Shift+E...) and **SketchUp** (P, R, C, A, L, M, Q, Space...).
  - Click a binding and press the new key (Esc cancels, Backspace clears). Actions are found by name or by key, and clashes within overlapping contexts are shown.
  - Changes are saved with the preferences and can be reset. The old "Blender Transform Keys" option still works on top of Unity.
- **Snap Axes: Local or Global** for drawing (Inspector and Modeling Tools). Local follows the face's edges or the object's rotation; Global uses the world's axes. Parallel and perpendicular edge snaps and guides work either way.
- **Z-fighting check** (Modeling Tools > Check: Z-Fighting; "Select Z-Fighting" in Edit Mode):
  - Finds faces lying on top of each other, within objects and between objects (in world space).
  - Each pair says what it is and what can go: a duplicate (remove one), both sides of an inner wall (remove both), a face hidden under another (remove it), or a partial overlap (move one).
  - Pairs have Select and Remove buttons, there's **Remove Suggested** for all of them, and the faces are outlined in the Scene view (red: can go).
  - Objects merely resting on each other aren't flagged.
- **UV editor:**
  - **Vertex / Face / Island** select modes (1/2/3). Click inside a face, box-select by face centre, or click or drag a whole island.
  - **Sync** with Edit Mode's face selection.
  - **All / None / Invert / Grow to Islands.**
  - **Rotate ±90** or by an angle, **Scale** by a factor, **Move** by an offset, **Flip U / V**, **Fit** to 0-1, **Center**, and **Align** (left, right, top, bottom, straight U / V).
  - Arrow keys nudge the selection (Shift: further).
- **Edit Mode tools in groups:**
  - **Select** first: every way of selecting in one place, with All and None. Then Create & Extrude, Cut & Divide, Merge & Clean Up, Deform, and Shading & UV.
  - Each group folds away; the Mesh menu has the same groups.
  - Mark / Clear Seam, Mark / Clear Sharp, Shade Smooth / Flat, To Tris / Quads and Grow More / Less each share one button.
  - Extrude Individual is now a switch on Extrude, like Inset's.
  - Select Overlapping became the Z-fighting check.
  - The doubled Knife and Merge Coplanar buttons are gone.
- Console: `keymap <preset>`, `keymap bind <action> <keys>`, `keymap clear <action>`, `zfight [scene] / zfight fix`, `uvsel ...`, `uvxf ...`, `drawmode axes local|global`, `duplicate`.

**Checked:** 1527 unit checks on Windows and 1503 on Linux, 0 failed. New tests:
- Light settings change the render fingerprint.
- Every preset's key layout, with no clashes in any preset, and chords that print and parse back.
- Real key presses under Unity and Maya, a rebind, clash detection, and overrides saved and loaded.
- Local and global draw axes on a rotated cube.
- Z-fighting: a duplicated cube fixed with one copy's faces removed, the inner wall of two joined boxes, and a hidden face versus a partial overlap.
- UV face and island selection and every transform, synced with Edit Mode.
- Every operator in a group, all selection tools in Select, and the Extrude Individual switch.

Stress: Push/Pull 0 problems; the modeling section 6,080 operations with 0 problems; the editor fuzzer 6,000 frames with no crash, with keys going through the new keymap.

## 2026-10-08 (round 14): archways pushed through cleanly, drawing over earlier edges, Smart Fill, Auto Smooth, camera sequences

**Asked:** commit and push round 13; fix the face artifacts in the half circle of an archway pushed through; a Smart Fill that patches open edges into a surface, tested where the edges converge to no area; drawing on a face and pushing or pulling through should work smoothly with edges already on either face (rectangles, circles, arcs); an option to shade bevels and other round surfaces smooth automatically; render the scene from a sequence of cameras.

**Done first:** round 13 committed and pushed.

**Fixed**
- **Archway artifacts.** Where a hole comes out of a face, the face around it was built by zipping its outline to the hole's outline by angle. Seen from the wall's far corner, an arch's curve runs "backwards", so some faces came out flipped, on top of their neighbours. That's the flicker in the half circle. Now every zipped result is checked: all faces must face the right way and add up to exactly the ring's area. When they don't, the ring is made of two faces joined by bridges, which works for any hole shape. Archways with 4-, 8- and 16-segment arcs, pushed through with Push/Pull or Push Through, now give a closed wall with the exact opening, no overlapping faces and every face triangulated exactly.
- **A selection reaching the wall's edge** (the doorway, the arch and the wall above it) pushed through left a folded face. Push Through now welds its result like Push/Pull does.
- **Scripted drawing on non-convex faces.** `drawpoint` found the face under a point with a test that only worked for convex faces, so a point on the ring around an earlier circle counted as "on the ground". Mouse drawing was not affected.

**Added**
- **Smart Fill** (Edit Mode tools, Mesh menu, Modeling Tools for whole objects). Open edges, whether sides of faces with nothing beyond or wire edges, are chained into loops, inside the selection or across the whole mesh. Each loop is closed the way that suits it:
  - A flat loop becomes one face, wound like the faces around it, so a box with a missing side is a closed solid again.
  - A bent loop becomes a fan from its centre.
  - A loop with no area is welded shut, so no zero-area face is ever made: a crack between unwelded copies of an edge, a slit, or a rim converging on a point (welded into one vertex).
  - A loop lying on a line is left alone and reported.
  - A face's own outline is never filled a second time.
- **Auto Smooth:**
  - Operators that make curved surfaces (a bevel with 2+ segments, a pulled circle) turn on smooth shading by angle (30 degrees by default) on flat-shaded meshes. Round parts look round and corners stay crisp. Meshes you shaded per face are left alone.
  - Toggle and angle are in Edit Mode's bevel settings and the Mesh menu, and saved with the preferences.
  - **Shade Auto Smooth** applies it to whole objects (Mesh menu, Modeling Tools).
- **Render Camera Sequence** (Render window):
  - Every camera with **Render in Sequence** on (new in the Camera Inspector) is rendered in **Sequence Order**, using the current settings and engine.
  - Each image is saved to `Renders/<scene>_sequence_<time>/NN_<camera>.<format>`.
  - It shows progress and can be stopped. Renders go back to the Main Camera afterwards.
- Console: `autosmooth`, `rendersequence [folder|stop]`, `editop smart_fill`, `meshop smart_fill`, `meshop shade_auto_smooth`.

**Checked:** 1447 unit checks on Windows and 1423 on Linux, 0 failed. New tests:
- Archways (three arc sizes) pushed through two ways, plus the notch case.
- Five "prior edges" cases, each pushed through or pulled out, each a closed solid with the exact volume, no overlaps and clean triangulation:
  - A rectangle drawn over an earlier circle's edge.
  - A circle drawn across an earlier rectangle.
  - A rectangle pushed through where the back has a circle.
  - An archway pushed through where the back has a rectangle across it.
  - A rectangle over a circle, pulled out.
- Smart Fill: one and two holes, a selection, a bent hole, a crack, a pinched rim, a line, a wire triangle, a slit, and the editor operator.
- Auto Smooth on 1- and 4-segment bevels, per-face shading, off, and by hand.
- A three-camera sequence in order with one excluded, three different images, and a stopped path-traced sequence.

Stress results:
- The modeling section, with Smart Fill on holes, cracks and wire loops in all 26 awkward meshes: 6,080 operations, 0 problems.
- Push/Pull: 0 problems, overlapping results still 676.
- The editor fuzzer: 6,000 frames, no crash.

## 2026-10-08 (round 13): Push/Pull without overlapping faces, Delete Loose, region inset, depth of field everywhere

**Asked:** commit and push round 12; check Push/Pull and similar tools for the overlapping-face artifacts still showing up; Blender's Delete Loose; inset several faces individually or as one; clearing a number field and pressing Enter should give 0, as in Unity; a New Material made in the Inspector should appear in the Materials folder; draw polygons from a face's exact centre, as in Plasticity; let depth of field blur the foreground while the background stays sharp.

**Done first:** round 12 committed and pushed.

**Fixed: Push/Pull's overlapping faces.** A new check finds faces lying on top of each other, or a hair apart (they z-fight). On the stress suite's 26,620 Push/Pull operations the old code produced 1,502 such results, plus 2 broken (open) meshes. Now there are 676 and none broken: 0 problems. What's left is mostly holes through curved surfaces and deliberately broken inputs (unwelded faces, inside-out normals). The causes, each fixed:
- **Stopping 2 mm short.** Push/Pull stopped just short of any surface in its way. A block pushed back down hovered a hair above the surface, with paper-thin walls, and two halves pulled to the same height left a sliver between them. Now, where the walls beside the selection end (flush), it goes exactly there: the walls vanish and the faces merge back, as in SketchUp. Asked for more, it carries on from there as a second step, so pushing a pulled block past flush makes a pocket, and pulling one half past the other works in one go.
- **Sliding past a neighbour counted as hitting it.** Edges lying on the selection's outline, such as a neighbouring block's top edge, stopped the move. Only geometry inside the swept area does now.
- **New walls in a neighbour's plane.** With Merge Coplanar off, pushing along a coplanar side built a wall inside that side. And when a neighbour stretched along one edge but got a wall along another, the wall covered the strip it grew into. Now a neighbour that stretches takes every side lying in its plane.
- **Folds at corners.** Faces touching the outline only at a corner kept the old corner, making a fold outside the solid. When every face around the outline contains the move's direction, the corners now simply slide (Blender's move along the normal).
- **Faces flattened to nothing** are dissolved into their neighbour, the way Blender's Dissolve Degenerate does it, instead of being left to flicker or turn inside out. Back-and-forth spikes left by welding are removed too.

**Added**
- **Delete Loose** (Blender: Mesh > Clean Up > Delete Loose): removes vertices and wire edges that no face uses, within the selection, or everywhere when nothing is selected. It's in the Edit Mode tools, the Mesh menu and the Modeling Tools window, and also works on whole objects.
- **Select Overlapping:** selects faces lying on top of each other, so leftovers (or imported ones) are easy to find and delete.
- **Inset Individual** switch (Blender's Inset > Individual), also in the F9 panel:
  - On: each face is inset by its own amount.
  - Off: the whole selection is inset as one face. Only its outline moves in, by a thickness, and the edges between the selected faces stay.
- **Empty number fields give 0**, as in Unity, within the field's limits (a field with a minimum of 1 gives 1).
- **New Material in an Inspector slot** (and New Material of Type) now saves a `.mat` in `Assets/Materials`, so it shows up in the Project and Materials windows like any other.
- **Face centre (Plasticity):**
  - When drawing, a face's exact centre (its area centre, not the average of its corners) snaps, shown in orange.
  - **Start at Face Center** makes circles, polygons and centre rectangles begin there wherever you click on the face.
- **Depth of field everywhere:**
  - The Game view, the Shaded Camera Preview and a piloted Scene view now show the camera's depth of field, not only path-traced renders.
  - Nearer things blur as well as farther ones. A blurred foreground spills softly over a sharp background, and a blurred background never spills over a sharp foreground (a "scatter as gather" pass, as in EEVEE).
- **Keep Focus on Point:** Pick Focus Point now keeps that point in focus while the camera moves (Blender's Focus Object). Piloting no longer moves the focus to the orbit pivot.
- Console: `insetmode`, `newslotmat`, `focus`, `position`, `drawmode facecenter`, `editop delete_loose`, `editop select_overlapping`.

**Checked:** 1309 unit checks on Windows and 1285 on Linux, 0 failed. New tests:
- Push/Pull sequences:
  - A square pulled up then pushed back flush, then pushed past it.
  - A pocket pulled back flush.
  - Two halves pulled to the same height, to different heights and past each other.
  - One half pulled and the other pushed.
  - Inset, push in and pull back.

  Each must stay closed, keep the exact volume and have no overlapping faces.
- Delete Loose, with and without a selection.
- Region and individual inset: face counts, a closed result, the inner area.
- Clearing float and int fields (with a minimum).
- New Material assets from the Inspector.
- Face-centre drawing with real clicks, and the area centre of an L-shaped face.
- Rasterized depth of field: the in-focus background untouched, the foreground's blur spilling over it, and the reverse.
- Focus tracking while moving and piloting.

Stress: the modeling section ran 5,716 operations with 0 problems, and the editor fuzzer ran 6,000 frames with no crash.

**Note:** your two material files in `Assets/Materials` (`New Material.mat`, `Solid Material.mat`) were moved to the Recycle Bin at 1:34 PM, while your own Blendity session was running. They can be restored from there.

## 2026-10-08 (round 12): drawing across faces, camera piloting fixes, asset folders, face material picker, drawing modes, guide lines, frame rate

**Asked:** commit and push round 11; make the camera actually adjust while piloting; fix drawing a circle onto a surface whose faces overlap inside the circle; renaming a material should rename its file; organise assets into folders like Unity; pick the material itself when assigning faces; draw shapes uniformly (from the centre), not only corner to corner, as in Plasticity; set the editor's frame rate and explain the performance cost; delete things in the Project window; construction lines to line drawings up with, as in Plasticity.

**Done first:** round 11 committed and pushed.

**Fixed**
- **A circle (or any closed shape) drawn over several faces** used to be added as a separate face lying over them. It is now cut into every face it covers, with a vertex wherever it crosses an edge, and all the pieces inside come out selected for Push/Pull. Parts hanging off the surface become wire edges.
- **Piloting a camera:**
  - When the camera's frame was narrower than the view, the frame didn't match what the camera saw, and starting to pilot changed the camera's field of view. The frame is now exactly the camera's view, and starting changes nothing.
  - A physical camera's lens was worked out from the wrong side of the sensor; it now follows the camera's Sensor Fit.
  - Moving the camera any other way while piloting (gizmo, Inspector, undo) now moves the view with it, instead of being overwritten.
- **Copied materials kept the wrong name.** A `.mat` copied outside Blendity kept the old name inside it. The file name now wins, as in Unity, so nothing gets renamed by surprise.

**Added**
- **Project window like Unity's:**
  - A nested folder tree, and a **+** menu or right-click for **Create > Folder / Material** in the current folder.
  - Drag files or folders onto a folder, a breadcrumb or `..` to move them. Dragging a scene material from the Materials window onto a folder saves it there as an asset.
  - **F2 / Rename** renames in place.
  - **Delete / Del** asks first, then sends the item to the **Recycle Bin** (Trash on Linux and macOS), where it can be restored. Only things inside Assets, research papers or screenshots can be deleted.
  - Moving or renaming keeps everything working: the shared material, texture paths, `.scene` files that mention it, and undo. A deleted material stays on its objects as a scene material.
- **Material name = file name**, both ways: renaming a material renames its `.mat`, and renaming the file renames the material.
- **Face Material** field in Edit Mode (Inspector and Modeling Tools): a grid of preview spheres with every material asset (from any folder), the object's slots and the scene's materials. Pick one and the selected faces use it; its slot is found or added for you.
- **Drawing modes from Plasticity:**
  - Rectangles from a **Corner**, the **Center**, or **3 Points** (one side at any angle, then the width), with **Square** to keep the sides equal.
  - Circles and polygons from the **Center**, **2 Points** (across) or **3 Points** (on the circle).
- **Guide lines**, Plasticity's lines and SketchUp's guides:
  - The new **Guide** draw shape, or **Guides from Edges**. They show as dashed lines and are saved with the scene, and adding them can be undone.
  - Drawing snaps to where two guides cross (or where one pierces the drawing plane), along a guide, and parallel to it.
- **Editor frame rate** (Preferences > Performance, and the Profiler):
  - A cap from 30 to 240 fps or Unlimited, now applied to mouse movement too (before, every mouse move drew a frame).
  - Redraw **only when something changes** (the default) or **always**.
  - It shows the measured frame cost and what that reaches on this machine, warns when the cap is over budget, and explains what each setting costs (CPU, battery, input lag, path-tracing samples). The stats overlay shows the live rate.
- Console: `drawmode`, `guide`, `clearguides`, `guidesfromedges`, `fps`, `redraw`, `preferences`, `facemat`, `mkfolder`, `moveasset`, `renameasset`, `deleteasset`.

**Checked:**
- 1209 unit checks on Windows and 1185 on Linux, 0 failed. New tests cover:
  - Circles across a 4 x 4 grid: the pieces' areas add up, and pulling them makes a solid.
  - The circle through grid corners, hanging off the edge, and along existing edges.
  - A circle overlapping a drawn rectangle on a cube, then Push/Pull, which stays closed.
  - Piloting in a tall view, moving the camera from the Inspector, and a physical camera's lens.
  - Folder move and rename, scene-file rewrites, undo after a move, a copied `.mat`, delete to a scratch trash, and refusing to delete outside Assets.
  - Face material picking.
  - Every drawing mode.
  - Guide snapping with real mouse clicks, save and load, and undo.
  - The frame-cap timing.
- Stress: the modeling section now also draws random circles and squares across faces on all 26 awkward meshes, plus degenerate loops: 5,716 operations, 0 problems. Push/Pull still has only its 2 long-standing problems. The editor fuzzer ran 6,000 frames with no crash.
- **Safety:** the tests and the fuzzer now work in a scratch project and a scratch trash, because the Project window can move and delete files. Your `Assets` folder was checked to be unchanged after full runs.

## 2026-10-08 (round 11): snapping, a Materials window, Modeling Tools, camera piloting, Plasticity tools

**Asked:** commit and push round 10 and remove old Blendity builds; drawing that snaps to edges parallel to a rotated object, not only world axes; test edge cases (a rectangle, an arc on its side, then Push/Pull); materials as objects you can see and adjust outside any object (Unity); drag a mesh onto another and snap it to the surface, and Unity's vertex snapping; move and frame a camera through the Scene view, including its FOV; useful tools from Plasticity; a separate tools window like UModeler's; sanity and edge-case testing.

**Done first:** round 10 committed and pushed; `Blendity.old.exe` and the stale `build\dev.exe` deleted, leaving only `dist\windows\Blendity.exe`.

**Fixed (found by the new edge-case tests)**
- An arc drawn from corner to corner of a rectangle, bulging outside it, used to "split" the rectangle with points outside it (a broken face). It now becomes a second face sharing that side, as in SketchUp.
- Push/Pull on a free-standing face (nothing attached along its outline, like a shape drawn on the ground) left the bottom open. It now makes a closed solid, keeping the original face as the floor (or lid when pushed).
- Shapes drawn on a rotated object's face came out lined up with the world. The drawing plane now takes its axes from the face's edges (or the object's own rotation).
- Imprinting a shape that lines up exactly with the face's outline could make extra triangles in the ring (a floating-point tie); the ring is now clean quads.

**Added**
- **Drawing inference** (SketchUp-style), from the last point: the plane's own red and green axes, any mesh edge's direction (parallel) and its perpendicular, and square to the previous segment, each shown with a dotted guide; Shift locks to the nearest. **Corner Radius** rounds rectangles, polygons and closed polylines (Plasticity's curve fillet).
- **Materials window:** every material in the scene and every material asset as a preview sphere. Click one to edit it there, drag it onto objects, faces, Hierarchy rows or slots, or right-click to assign, save as an asset, duplicate, or select the objects using it.
- **Modeling Tools window** (UModeler's tool panel): Create (primitives and parametric shapes), Draw, Object tools (Join, Boolean, Reset XForm, Mirror, origin, camera), Edit Mode's tools, UV and snapping, always available. It shares the Edit Mode tool code with the Inspector.
- **Surface snapping:** Ctrl+Shift while moving drops objects onto the surface under the mouse, resting on their lowest point, and optionally aligned to its normal. Dragging objects from the Hierarchy into the Scene view does the same.
- **Vertex snapping** (Unity's V): hold V and drag; the selection's nearest vertex lands on the vertex under the mouse (objects, and selected vertices in Edit Mode), with Unity's square marker.
- **Pilot Camera:** look through a camera and frame the shot by navigating. The camera follows the view, the wheel dollies, Ctrl+wheel changes its FOV (or focal length on a physical camera), and a frame with thirds guides shows the shot. **Align to View** is there too. Both are in the Camera Inspector and the Modeling Tools window.
- **From Plasticity:** **Shell** (hollow a solid, opening the selected faces), **Thicken**, **Draft** (taper side faces by an angle) and a **Radial** mode on the Array modifier.

**Checked:** 1049 unit checks on Windows and 1025 on Linux, 0 failed. New tests cover: drawing on a rotated object and pushing the result through; the rectangle + arc + Push/Pull case; degenerate shapes and lines off the mesh; fillets and drawing into a drawn face; Shell, Draft, Thicken and Radial Array; dropping onto a surface; V vertex snapping with real mouse events; piloting with the wheel; both new windows. Stress results:
- Push/Pull ran 26,620 operations and still has only its 2 long-standing problems. A first run found 36 new zero-area faces from the new floor; fixed.
- The modeling section ran 5,066 operations with 0 problems.
- The editor fuzzer ran 6,000 frames with no crash.

## 2026-10-08 (round 10): materials as assets, drawing tools, UModeler tools, origin fix

**Asked:** push the previous round; keep a changelog; Shift + scale on a face makes a new face, as in Blender; automatic seams from sharp edges; a light "focus" like the camera's; UModeler features Blendity lacks; Unity-style material assets you drag onto objects and slots; removing material slots; a polyline tool for circles, arcs and so on, drawn onto a mesh with snapping; fix the origin not being used as the transform point.

**Fixed**
- **Origin as the transform point.** The toolbar's handle position defaulted to *Center*: the gizmo sat at the bounds centre and rotations turned about it, while scaling still used the origin, so a moved origin seemed to do nothing. It now defaults to *Pivot* (the origin, as Blender transforms one object), the choice is saved, and Center scales about the same point it rotates about; G / R / S follow the same setting.
- The "Blender Transform Keys" preference was never saved (it was missing from the preferences file).

**Added**
- **Shift + drag** a gizmo handle in Edit Mode extrudes first: faces get walls and a new cap, so Shift + scale makes an inset face and Shift + move an extrusion (Blender's E then S, ProBuilder / UModeler's Shift-drag). In vertex / edge mode the edges grow faces.
- **Seams from Sharp Edges** (UV menu, UV Editor, edge-mode tools): a seam on every edge whose faces meet at more than the Sharp Angle (30 degrees, Blender's default) and on edges marked sharp; *+ Unwrap* does both. **Select Sharp Edges** too.
- **Light aiming**: Light > *Aim at Point* is an eyedropper like the camera's focus picker. Click a surface and spot, area and directional lights turn to shine at it (X kept level, Unity's LookAt); the range grows to reach it. `pickfocus` and `aimlight x y z` in the console.
- **Material assets** (Unity): Assets > Create Material, or a slot's *Save as Material Asset*, writes `Assets/Materials/<name>.mat`. Drag one from the Project window onto an object in the Scene view (the face under the mouse picks the slot), onto a face in Edit Mode (all selected faces if it is one of them), onto a Hierarchy row or onto an Inspector slot. Images drag onto a material's texture fields. Selecting a `.mat` shows its full material editor; every renderer using it shares one instance, edits are written back to the file, and scenes refer to the file (the values are kept as a fallback).
- **Material slots**: an x on each slot removes it (its faces move to the slot before, as in Blender), plus **Remove Unused Slots**.
- **Drawing tools** (UModeler / SketchUp): Polyline, Rectangle, Circle, Arc and Polygon, drawn onto the edited mesh or the ground (Inspector > Draw, Mesh > Draw, `draw <shape>`). Snapping to corners (green), midpoints (cyan), edges (red), faces (blue) and the ground, with Ctrl for the grid and Shift for the plane's axes, plus a live length. A closed shape inside a face is cut into it, ready for Push/Pull; a line between points on a face's edges splits it through every bend; anything else becomes a new face or wire edges. With nothing selected it starts a new mesh.
- **From UModeler**: **Follow** (sweep a face along a drawn path, SketchUp's Follow Me, with mitred corners), **Spin / Lathe** (edges or vertices turned round an axis), **Slice** (cut by an X / Y / Z or view plane through the selection, optionally removing a side; Blender's Bisect), **Reset XForm** (bake rotation and scale into the mesh), **Mirror** (applied, welded), **Select Similar** (normal, area) and **Select by Material**.

**Checked:** 1005 unit checks on Windows and 981 on Linux, 0 failed (new tests: Shift-drag extrude, seams, light aiming, material assets with save / load and slot removal, drawing on a face and on the ground, Follow / Spin / Slice, Reset XForm and the pivot). The stress suite ran 4,728 operations of the new and existing tools on 26 awkward meshes, plus 390 random shapes, with 0 problems; the editor fuzzer ran 1,500 frames with no crash.

## 2026-10-08 (round 9): every Blender export format, file dialogs, Boolean, modifier stack, shapes, n-gons

**Asked:** Blender's export options and formats (tested against an installed Blender); Save As dialogs for objects and screenshots; origin handles; Boolean operations; combining meshes; parametric objects; SketchUp-style n-gon editing; extruding vertices and edges; a Blender-style modifier stack.

**Added:** export to OBJ, binary / ASCII FBX, glTF (.glb / .gltf), STL, PLY and USD (.usda) with Blender's options, and import of the same; every format checked both ways in Blender 5.2 (this found that the earlier ASCII FBX did not open in Blender). Native Save / Open dialogs. Join, Separate, Boolean (applied or a live modifier with a wire cutter). Wire edges and vertex / edge extrusion. Edit Origin with handles and click snapping. A modifier stack with Edit Mode / Viewport / Render toggles and Bevel, Weld, Triangulate, Wireframe, Displace, Simple Deform, Cast and Screw. Thirteen parametric shapes. N-gon Mode, Merge Coplanar and the Knife (K).

**Checked:** 963 checks on Windows, 939 on Linux; a new stress section with 0 problems.

## 2026-10-08 (round 8): lights, export, selection fixes

Spot and Area lights with colour temperature (rasterizer and CPU / GPU path tracing); OBJ and FBX export; editing another mesh by clicking it in Edit Mode; the Hierarchy's create menu; Alt+click loops and Ctrl+Alt+click rings; the scene gizmo's views; origin dots; a lockable Camera Preview; a camera focus eyedropper; old app builds removed.

## 2026-10-08 (round 7): Blender's modal editing, edge selection, per-face shading

G / R / S modal transforms with axis locks and typed values; edge mode keeps the picked edges; per-face Shade Smooth / Flat and Mark Sharp; Bevel Clamp Overlap and a dozen more Blender operators; Unity-style colour fields; render resolution presets; docs/USABILITY.md.

## 2026-10-07 (rounds 4-6): GPU rendering, Push/Pull, physical cameras

Vulkan GPU path tracing with ray tracing hardware and combined CPU + GPU rendering; SketchUp-style Push/Pull as a confirmed operator, hardened by a stress test on awkward meshes; tools split by selection mode; Set Origin; physical cameras with depth of field and exposure; edge tools (Bevel, Bridge, Push Through); Adjust Last Operation (F9); Inspector expressions and multi-object editing; surface types (opaque, cutout, transparent, glass).

## 2026-10-06 / 07 (rounds 1-3): Blendity

The editor itself: Blender's mesh model and Edit Mode behind a Unity-style editor; UV unwrapping, materials and textures; a deferred rasterizer and a path tracer; Blender's bundled libraries (Embree, OpenImageDenoise, OpenSubdiv, OpenColorIO, Manifold, Jolt and others), each optional; SIMD kernels; the Learn tab; stress tests.
