## 2026-11-19 (round 598): Make the release manuals easier to read and find

- Asked: correct the five findings from the 1.0.0 documentation audit and document how assigned work reaches final owner review.
- Changed: the Architecture module map wraps the Blender shader path and the User Guide names the double-chevron overflow button. The README points to the released manuals and the architecture guide distinguishes the PR citation check from release regeneration. The standing workflow records the owner assignment before implementation and routes owner-reserved decisions through the hub. Explicit project-wide workflow changes carry forward to future sessions within the scope set by the owner. The hub updates affected briefs and tooling, and each role cites its evidence.
- Checked: the Windows build and release workflows both request MSVC 14.51. Both current-book URLs resolved to their 1.0.0 attachments. The committed citation check passed with 182 references. Regeneration checked 184 references and both books compiled with LuaLaTeX and Biber. On physical page 14 of Architecture the complete path fits its column. On physical page 11 of the User Guide the button name renders and extracts as text.

<!-- README -->
replace: [User Guide](docs/Blendity-User-Guide.pdf)
with: [User Guide](https://github.com/EmilianoManaloIV/blendity/releases/latest/download/Blendity-User-Guide.pdf)
replace: [Architecture and Engineering](docs/Blendity-Architecture.pdf)
with: [Architecture and Engineering](https://github.com/EmilianoManaloIV/blendity/releases/latest/download/Blendity-Architecture.pdf)

<!-- docs/ARCHITECTURE.md -->
replace: `--check` in CI keeps them from drifting; `release.yml` builds and attaches the PDFs
with: PR builds run `--check-cited` to validate the committed citation keys. Source changes can leave the committed indices stale. The release books job runs `--bib --check-cited` to regenerate the bibliography and indices and validate their keys before compiling both PDFs for the release
