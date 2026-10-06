# Research → Features

This folder connects your own research to the editor.

```
research/
├── papers/        ← drop papers here (or drag them onto the Blendity window)
├── FEATURES.md    ← tracker: paper → feature → status → source file
└── README.md      ← this file
src/research/      ← where paper-derived features are implemented (C++)
```

## Workflow

1. **Add a paper.** Drop it into `research/papers/`. The Research tab lists everything in that folder, and you can open each paper from there.
2. **Ask for a feature.** In Claude Code, for example: *"Read research/papers/X.pdf and implement section 4's method as a Blendity feature."*
3. **Implementation.** The technique is written in plain C++ (no new dependencies) in `src/research/`, then registered with `research::register_feature(...)`:

   ```cpp
   register_feature({"taubin_smoothing", "Taubin lambda|mu Smoothing",
                     "Taubin, G. (1995). A signal processing approach to fair surface design. SIGGRAPH '95.",
                     "Smooths without shrinking ...", "implemented", "src/research/research.cpp",
                     [](Mesh &m) { taubin_smooth(m); return true; }});
   ```

   A registered feature shows up automatically in:
   - the **Research** tab (citation, description, status, and an "Apply to selection" button),
   - **Mesh → Research Features**,
   - the Inspector's MeshFilter tools.
4. **Verify.** Unit tests go in `tests/test_main.cpp`, for example the check that Taubin keeps more volume than Laplacian smoothing. Timing goes in `stress/stress_main.cpp` if performance matters.
5. **Track.** Add a row to `FEATURES.md`.

Features that aren't mesh operators (a new component, renderer option or importer) follow the same idea: they get registered in the matching registry (`register_component` for components) and cited in `FEATURES.md`.
