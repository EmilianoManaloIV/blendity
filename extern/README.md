# extern

Code copied unmodified from Blender's own tree (its licences are in `../licenses/README.md`):

| Folder | From Blender | What for |
|---|---|---|
| `ufbx/` | `extern/ufbx` | FBX import |
| `fast_float/` | `extern/fast_float` | float parsing in the scene loader |
| `mikktspace/` | `intern/mikktspace` (and `mikktspace_shim.hh`) | tangents for normal maps |
| `sky/` | `intern/sky` | the Hosek-Wilkie sky |
| `cuew/` | `extern/cuew` | the CUDA driver API loaded at run time, for OptiX (task 0097; built only with the CUDA Toolkit and the OptiX SDK, ADR 0037) |
| `libc_compat/` | `intern/libc_compat` | Linux builds with Blender's libraries: the `-ffast-math` math symbols newer glibc dropped, which the static codecs in Blender's FFmpeg still call (task 0077) |
