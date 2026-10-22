# extern

Code copied unmodified from Blender's own tree (its licences are in `../licenses/README.md`):

| Folder | From Blender | What for |
|---|---|---|
| `ufbx/` | `extern/ufbx` | FBX import |
| `fast_float/` | `extern/fast_float` | float parsing in the scene loader |
| `mikktspace/` | `intern/mikktspace` (and `mikktspace_shim.hh`) | tangents for normal maps |
| `sky/` | `intern/sky` | the Hosek-Wilkie sky |
| `cuew/` | `extern/cuew` | the CUDA driver API loaded at run time, for OptiX (task 0097; built whenever OptiX's headers are there - the installers since task 0169, ADR 0054) |
| `libc_compat/` | `intern/libc_compat` | Linux builds with Blender's libraries: the `-ffast-math` math symbols newer glibc dropped, which the static codecs in Blender's FFmpeg still call (task 0077) |

Not from Blender's tree:

| Folder | From | What for |
|---|---|---|
| `onnxruntime/` | Microsoft's ONNX Runtime 1.31.0 release, `include/` (MIT, `../licenses/onnxruntime-MIT.txt`) | the C API's headers only: AI inbetweening loads ONNX Runtime at run time from its optional download (task 0129, ADR 0046); nothing links against it |
