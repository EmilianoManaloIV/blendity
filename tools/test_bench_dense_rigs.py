#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Checks for interpreting dense-rig gesture and motion evidence."""

from pathlib import Path
import tempfile
import unittest

from bench_dense_rigs import verify_log, verify_motion


def gesture_log():
    rows = ["Views: CPU (Viewport Device)", "Live Link: off",
            "29 objects, 10242 verts, 10240 faces, scene memory ~2.7 MB",
            "perf mark row: idle: 12.8 ms over 8 frame(s), the longest 3.7 ms"]
    for command, label, frames in (("gizmo x", "ik", 13), ("ruler", "scrub", 12)):
        rows.append(f"perf drag {command}: press 3.7 ms, first move 4.4 ms, 7 drag frame(s) mean 4.26 ms "
                    "(longest 4.8 ms), release 3.8 ms, the frame after 1.3 ms")
        rows.append(f"perf mark row: {label}: 45.7 ms over {frames} frame(s), the longest 4.8 ms")
    rows.extend(["perf frames counts (a frame's mean): clip applies 1.0",
                 "perf mark row: playback: 36.1 ms over 8 frame(s), the longest 5.0 ms"])
    return "\n".join(rows)


class DenseRigBenchmarkTests(unittest.TestCase):
    def test_input_frames_include_the_separately_reported_first_move(self):
        log = gesture_log()
        scene, rows = verify_log(log, 8, "isolated")
        self.assertEqual(scene["objects"], 29)
        self.assertEqual([row["frames"] for row in rows], [8, 7, 7, 8])
        with self.assertRaisesRegex(ValueError, "ik did not finish"):
            verify_log(log.replace("7 drag frame", "6 drag frame", 1), 8, "isolated")
        with self.assertRaisesRegex(ValueError, "scrub did not finish"):
            verify_log(log.replace("perf drag ruler:", "perf drag orbit:"), 8, "isolated")
        with self.assertRaisesRegex(ValueError, "playback frames did not apply the clip"):
            verify_log(log.replace("clip applies 1.0", "clip applies 0.0"), 8, "isolated")

    def test_motion_requires_the_reference_scrub_and_playback_skin_deformation(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)

            def write_pose(name, stretch, travel):
                (root / f"{name}.obj").write_text(
                    f"v 0 0 {travel}\nv {stretch} 0 {travel}\nv 0 1 {travel}\nf 1 2 3\n", encoding="ascii"
                )

            write_pose("opening", 1.0, 0.0)
            write_pose("scrub", 1.3, 1.2)
            write_pose("reference", 1.3, 1.2)
            write_pose("playback", 1.1, 0.4)
            results = verify_motion(root)
            self.assertEqual(results["scrub"]["direct_sample_max_error"], 0.0)
            self.assertAlmostEqual(results["playback"]["centroid_z_delta"], 0.4)
            write_pose("playback", 1.0, 0.4)
            with self.assertRaisesRegex(ValueError, "playback sample showed no skin deformation"):
                verify_motion(root)
            write_pose("reference", 1.4, 1.2)
            with self.assertRaisesRegex(ValueError, "ruler drag did not reach"):
                verify_motion(root)


if __name__ == "__main__":
    unittest.main()
