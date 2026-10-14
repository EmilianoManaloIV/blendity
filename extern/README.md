# extern

Code copied unmodified from Blender's own tree (its licences are in `../licenses/README.md`):

| Folder | From Blender | What for |
|---|---|---|
| `ufbx/` | `extern/ufbx` | FBX import |
| `fast_float/` | `extern/fast_float` | float parsing in the scene loader |
| `mikktspace/` | `intern/mikktspace` (and `mikktspace_shim.hh`) | tangents for normal maps |
| `sky/` | `intern/sky` | the Hosek-Wilkie sky |
| `libc_compat/` | `intern/libc_compat` | Linux builds with Blender's libraries: the `-ffast-math` math symbols newer glibc dropped, which the static codecs in Blender's FFmpeg still call (task 0077) |
