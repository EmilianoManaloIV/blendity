#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Acceptance checks for the animation benchmark fixtures and evidence gates."""

from collections import Counter, defaultdict
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import MagicMock, call, patch

import bench_animation_rigs as bench


class AnimationBenchmarkTests(unittest.TestCase):
    def test_fixture_is_deterministic_closed_and_connected(self):
        with tempfile.TemporaryDirectory() as scratch:
            original, duplicate = Path(scratch) / "body.obj", Path(scratch) / "copy.obj"
            dimensions = []
            for case in bench.CASES:
                with self.subTest(case=case):
                    geometry = bench.write_body(original, case)
                    self.assertEqual(bench.write_body(duplicate, case), geometry)
                    self.assertEqual(duplicate.read_bytes(), original.read_bytes())
                    points, faces = bench.read_obj(original)
                    self.assertEqual((len(points), len(faces)), (geometry["vertices"], geometry["quads"]))
                    self.assertEqual(len(set(points)), len(points))
                    self.assertEqual({len(set(face)) for face in faces}, {4})
                    self.assertEqual({v for face in faces for v in face}, set(range(1, len(points) + 1)))
                    edges = Counter()
                    neighbors = defaultdict(set)
                    for face in faces:
                        for a, b in zip(face, face[1:] + face[:1]):
                            edges[a, b] += 1
                            neighbors[a].add(b)
                    self.assertTrue(all(count == edges[b, a] == 1 for (a, b), count in edges.items()))
                    connected = {1}
                    pending = [1]
                    while pending:
                        unseen = neighbors[pending.pop()] - connected
                        pending.extend(unseen)
                        connected.update(unseen)
                    self.assertEqual(len(connected), len(points))
                    dimensions.append(len(points))
            self.assertGreater(min(dimensions[1:]), dimensions[0] * 10)

    def test_timing_evidence_requires_each_stage_and_the_requested_playback_batch(self):
        mark = "perf mark playback: 23.0 ms over 24 frame(s), the longest 2.1 ms"
        marks, _ = bench.read_timings(mark, ["playback"], 24)
        self.assertEqual(marks["playback"]["frames"], 24)
        with self.assertRaisesRegex(ValueError, "wrong number"):
            bench.read_timings(mark, ["playback"], 12)
        with self.assertRaisesRegex(ValueError, "missing or reordered"):
            bench.read_timings(mark, ["import", "playback"], 24)
        with self.assertRaisesRegex(ValueError, "duplicate timing stage"):
            bench.read_timings(f"{mark}\n{mark}", ["playback"], 24)

    def test_skinning_evidence_requires_a_completed_solve_for_the_selected_target(self):
        report = "Skin: Transfer Weights on 'Transfer': 4 vertices kept, 2 inpainted, 0 by distance (conjugate gradients, 3.5 ms)"
        name = "Skin: Transfer Weights"
        counts = bench.read_weight_solve(report, name, "Transfer", 6)
        self.assertEqual((counts["kept"], counts["inpainted"], counts["fallback"]), (4, 2, 0))
        with self.assertRaisesRegex(ValueError, "expected one completed"):
            bench.read_weight_solve(report, name, "Body", 6)
        with self.assertRaisesRegex(ValueError, "one result per vertex"):
            bench.read_weight_solve(report, name, "Transfer", 7)

    def test_weight_evidence_rejects_truncated_and_invalid_values(self):
        bench.verify_weight_vectors({"Bone": [1.0, 0.25, 0.0]}, 3, "painted")
        for values in ([1.0, 0.25], [1.0, 0.25, float("nan")], [1.0, 0.25, 1.05]):
            with self.subTest(values=values), self.assertRaisesRegex(ValueError, "painted"):
                bench.verify_weight_vectors({"Bone": values}, 3, "painted")

    def test_motion_evidence_rejects_stalled_playback_and_rigid_preview(self):
        with tempfile.TemporaryDirectory() as scratch:
            directory = Path(scratch)

            def write_sample(tag, stretch=1.0, travel=0.0):
                (directory / f"{tag}.obj").write_text(
                    f"v 0 0 {travel}\nv {stretch} 0 {travel}\nv 0 1 {travel}\nf 1 2 3\n",
                    encoding="ascii",
                )

            write_sample("preview-0")
            write_sample("preview-30", stretch=1.1)
            write_sample("preview-60", stretch=1.2)
            write_sample(f"preview-{bench.CLIP_END}", travel=bench.CLIP_TRAVEL)
            write_sample("playback", travel=0.2)
            motion = bench.verify_motion(directory)
            self.assertAlmostEqual(motion["playback"]["centroid_delta"][2], 0.2)
            write_sample("playback")
            with self.assertRaisesRegex(ValueError, "playback did not advance"):
                bench.verify_motion(directory)
            write_sample("playback", travel=0.2)
            write_sample("preview-30", travel=0.2)
            with self.assertRaisesRegex(ValueError, "sampled skin did not deform"):
                bench.verify_motion(directory)

    def test_transferred_skin_must_deform_when_the_source_joint_moves(self):
        objects = {
            1: {"name": "Body", "components": {"MeshFilter": {"mesh": "10"}}},
            2: {"name": "Transfer", "components": {"MeshFilter": {"mesh": "11"},
                  "SkinModifier": {"enabled": True, "armature": "Humanoid"}}},
        }
        mesh = {"vertices": 3, "faces": 1, "groups": {"Bone": (1.0, 1.0, 1.0)}}
        with tempfile.TemporaryDirectory() as scratch, patch.object(bench, "read_scene", return_value=(objects, {10: mesh, 11: mesh})):
            directory = Path(scratch)
            before = directory / "transfer-before.obj"
            after = directory / "transfer-after.obj"
            before.write_text("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n", encoding="ascii")
            after.write_text("v 0 0 0\nv 1.1 0 0\nv 0 1 0\nf 1 2 3\n", encoding="ascii")
            self.assertAlmostEqual(bench.verify_transfer("humanoid", directory)["max_deformation"], 0.1)
            for rigid_pose in ("v 0 0 1\nv 1 0 1\nv 0 1 1", "v 0 0 0\nv 0 1 0\nv -1 0 0"):
                with self.subTest(rigid_pose=rigid_pose):
                    after.write_text(f"{rigid_pose}\nf 1 2 3\n", encoding="ascii")
                    with self.assertRaisesRegex(ValueError, "deform"):
                        bench.verify_transfer("humanoid", directory)

    def test_editor_launch_uses_scratch_even_with_existing_blendity_environment(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            run = root / "run"
            outside = root / "existing"
            outside.mkdir()
            options = {"BLENDITY_PROJECT": run / "project", "BLENDITY_PREFS_DIR": run / "prefs",
                       "BLENDITY_TRASH": run / "trash", "BLENDITY_CACHE_DIR": run / "cache"}
            inherited = {name: str(outside) for name in options}
            inherited.update(BLENDITY_THREADS="3", BLENDITY_UNEXPECTED_SETTING="inherited")

            def capture(argv, *, cwd, env, **kwargs):
                self.assertEqual(cwd, run / "project")
                self.assertEqual(Path(argv[argv.index("--headless-screenshot") + 1]), run / "editor.png")
                for name, folder in options.items():
                    self.assertEqual(Path(env[name]), folder)
                    self.assertTrue(folder.is_dir())
                self.assertEqual(env["BLENDITY_VIEWPORT_DEVICE"], "cpu")
                self.assertEqual(env["BLENDITY_THREADS"], "3")
                self.assertNotIn("BLENDITY_UNEXPECTED_SETTING", env)
                raise FileNotFoundError("editor unavailable")

            with patch.dict(os.environ, inherited), patch.object(bench.subprocess, "Popen", side_effect=capture) as launch:
                result = bench.run_case(root / "unavailable-executable", run, "control", 12, None, 1)
            launch.assert_called_once()
            self.assertFalse(result["passed"])
            self.assertEqual(result["error"], "editor unavailable")
            self.assertFalse(list(outside.iterdir()))

    def test_cpu_affinity_is_restored_after_an_error(self):
        affinity = bench.CpuAffinity()
        prior = affinity.get()
        if prior is None or len(prior) < 2:
            self.skipTest("process does not have at least two assignable CPUs")
        with self.assertRaisesRegex(RuntimeError, "test error"):
            with affinity.inherit(prior[:1]):
                self.assertEqual(affinity.get(), prior[:1])
                raise RuntimeError("test error")
        self.assertEqual(affinity.get(), prior)

    def test_affinity_inspection_failure_restores_launcher_and_reaps_child(self):
        for needs_kill in (False, True):
            with self.subTest(needs_kill=needs_kill), tempfile.TemporaryDirectory() as scratch:
                affinity = bench.CpuAffinity()
                current = [0, 1]
                child = MagicMock(pid=1234)

                def inspect(pid=0):
                    if pid:
                        self.assertEqual(pid, child.pid)
                        self.assertEqual(current, [0])
                        raise OSError("cannot inspect child affinity")
                    return list(current)

                def assign(cpus):
                    current[:] = cpus

                def check_restored():
                    self.assertEqual(current, [0, 1])

                child.terminate.side_effect = check_restored
                child.kill.side_effect = check_restored
                child.wait.side_effect = [subprocess.TimeoutExpired("editor", 5), 0] if needs_kill else [0]
                path = Path(scratch)
                with patch.object(affinity, "get", side_effect=inspect), patch.object(affinity, "set", side_effect=assign), \
                     patch.object(bench, "CpuAffinity", return_value=affinity), patch.object(bench.subprocess, "Popen", return_value=child):
                    result = bench.run_case(path / "editor", path / "run", "control", 12, [0], 1)
                self.assertFalse(result["passed"])
                self.assertEqual(result["error"], "cannot inspect child affinity")
                self.assertEqual(current, [0, 1])
                child.terminate.assert_called_once_with()
                if needs_kill:
                    child.kill.assert_called_once_with()
                    self.assertEqual(child.wait.call_args_list, [call(timeout=5), call(timeout=5)])
                else:
                    child.kill.assert_not_called()
                    child.wait.assert_called_once_with(timeout=5)


if __name__ == "__main__":
    unittest.main()
