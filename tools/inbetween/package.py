# SPDX-License-Identifier: GPL-2.0-or-later
"""Builds the AI download (task 0129): per platform, ONNX Runtime's library from Microsoft's release (MIT) and the
trained model, as the archive Blendity's `ai install` and the AI dialog unpack into ~/.blendity/ai/<tag>, with its
.sha256. Publishing them to the libraries release is the maintainer's call.

    python -I package.py <run folder (inbetween.onnx, inbetween.onnx.offsets.json)> <folder of ONNX Runtime releases> <out folder>

The ONNX Runtime folder holds the release archives, unpacked: onnxruntime-win-x64-1.31.0/, onnxruntime-linux-x64-1.31.0/,
onnxruntime-osx-arm64-1.31.0/ (each platform present is packaged).
"""
import hashlib
import io
import os
import shutil
import sys
import tarfile
import zipfile

TAG = "ai-v1"
ORT = "1.31.0"
PLATFORMS = [
    # (archive, release folder, {file in the release's lib/: name in the archive})
    ("blendity-ai-windows-x64-%s.zip" % TAG, "onnxruntime-win-x64-" + ORT, {"onnxruntime.dll": "onnxruntime.dll", "onnxruntime_providers_shared.dll": "onnxruntime_providers_shared.dll"}),
    ("blendity-ai-linux-x64-%s.tar.gz" % TAG, "onnxruntime-linux-x64-" + ORT, {"libonnxruntime.so." + ORT: "libonnxruntime.so"}),
    ("blendity-ai-macos-arm64-%s.tar.gz" % TAG, "onnxruntime-osx-arm64-" + ORT, {"libonnxruntime.%s.dylib" % ORT: "libonnxruntime.dylib"}),
]
README = """Blendity's AI inbetweening (%s)

inbetween.onnx   the inbetweening network (Blendity's, GPL-2.0-or-later like Blendity), trained on the CMU Graphics Lab
                 Motion Capture Database (mocap.cs.cmu.edu: "The data used in this project was obtained from
                 mocap.cs.cmu.edu. The database was created with funding from NSF EIA-0196217.")
inbetween.json   its skeleton and styles
ONNX Runtime %s  Microsoft's (MIT): LICENSE-onnxruntime.txt, ThirdPartyNotices-onnxruntime.txt
""" % (TAG, ORT)


def main(run, ort_dir, out):
    os.makedirs(out, exist_ok=True)
    model = os.path.join(run, "inbetween.onnx")
    info = model + ".offsets.json"
    for archive, folder, libs in PLATFORMS:
        rel = os.path.join(ort_dir, folder)
        if not os.path.isdir(rel):
            print("skipped", archive, "(no", folder, ")")
            continue
        files = {"inbetween.onnx": model, "inbetween.json": info,
                 "LICENSE-onnxruntime.txt": os.path.join(rel, "LICENSE"),
                 "ThirdPartyNotices-onnxruntime.txt": os.path.join(rel, "ThirdPartyNotices.txt")}
        for src, dst in libs.items():
            files[dst] = os.path.join(rel, "lib", src)
        path = os.path.join(out, archive)
        if archive.endswith(".zip"):
            with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
                for name, src in files.items():
                    z.write(src, name)
                z.writestr("README.txt", README)
        else:
            with tarfile.open(path, "w:gz") as t:
                for name, src in files.items():
                    t.add(src, name, recursive=False)
                data = README.encode()
                ti = tarfile.TarInfo("README.txt")
                ti.size = len(data)
                t.addfile(ti, io.BytesIO(data))
        h = hashlib.sha256(open(path, "rb").read()).hexdigest()
        with open(path + ".sha256", "w") as f:
            f.write("%s  %s\n" % (h, archive))
        print("%s  %.1f MB  %s" % (archive, os.path.getsize(path) / 1048576.0, h))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2], sys.argv[3])
