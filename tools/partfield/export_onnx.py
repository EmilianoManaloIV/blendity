# SPDX-License-Identifier: GPL-2.0-or-later
"""PartField (NVIDIA, 2025) as the ONNX model Part Unwrap's optional learned tree reads (task 0180; src/scene/partfield.h).

Run by you, with PartField's code and the weights you obtained from NVIDIA under their licence (non-commercial):
Blendity doesn't download or ship either. NOT RUN BY BLENDITY'S AUTHORS: no PartField checkout or weights were used to
write it, so treat it as a starting point (tools/partfield/README.md says what is and isn't known).

    python tools/partfield/export_onnx.py --partfield <checkout> --weights <model.ckpt> [--out ~/.blendity/ai/partfield/partfield.onnx]

The contract Blendity calls:
    input  "points"   float [1, N, 6]: each face's centroid and normal, the mesh scaled into the unit cube at the origin
    output "features" float [1, N, D]: PartField's feature at each point (D = its feature width)
"""
import argparse
import os
import sys


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--partfield", required=True, help="a checkout of NVIDIA's PartField repository")
    ap.add_argument("--weights", required=True, help="the checkpoint you obtained from NVIDIA")
    ap.add_argument("--out", default=os.path.join(os.path.expanduser("~"), ".blendity", "ai", "partfield", "partfield.onnx"))
    ap.add_argument("--points", type=int, default=4096, help="the example size the export traces with (N is dynamic)")
    args = ap.parse_args()

    sys.path.insert(0, args.partfield)
    import torch  # noqa: E402  (PartField's own environment)

    try:
        # PartField's model class and its loading, as its own inference script does them. The names below are the
        # repository's at the time of writing; adjust them if your checkout differs.
        from partfield.model_trainer_pvcnn_only_demo import Model  # type: ignore  # noqa: E402
    except ImportError as e:
        raise SystemExit("PartField's model class wasn't found in %s (%s): adjust the import to your checkout" % (args.partfield, e))

    model = Model.load_from_checkpoint(args.weights, map_location="cpu")
    model.eval()

    class Wrapper(torch.nn.Module):
        """points [1, N, 6] -> features [1, N, D]: the triplane from the point cloud, sampled back at the points."""

        def __init__(self, m):
            super().__init__()
            self.m = m

        def forward(self, points):
            planes = self.m.encode(points)                 # (PartField's point-cloud encoder into its triplane)
            return self.m.sample_features(planes, points[..., :3])  # (its feature query at the same points)

    w = Wrapper(model)
    example = torch.rand(1, args.points, 6) - 0.5
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    torch.onnx.export(w, (example,), args.out, input_names=["points"], output_names=["features"],
                      dynamic_axes={"points": {1: "n"}, "features": {1: "n"}}, opset_version=17)
    print("wrote", args.out)


if __name__ == "__main__":
    main()
