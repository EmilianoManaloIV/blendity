#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Regression tests for the operations inventory, using disposable inputs and output."""
from contextlib import redirect_stderr, redirect_stdout
from io import StringIO
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest

import sweep_commands


class SweepCommandsTest(unittest.TestCase):
    def setUp(self):
        scratch = TemporaryDirectory()
        self.addCleanup(scratch.cleanup)
        self.root = Path(scratch.name)
        self.registry = self.root / "src" / "editor" / "editor_commands.cpp"
        self.output = self.root / "stress" / "sweep_commands.inc"
        self.registry.parent.mkdir(parents=True)
        self.output.parent.mkdir()
        self.registry.write_text('const char* kConsoleCommands[] = {"open", "window"};', encoding="utf-8")
        self.examples = ["window Inspector"]
        self.skips = {"open": "replaces the scene"}

    def generate(self, check=False):
        messages = StringIO()
        with redirect_stdout(messages), redirect_stderr(messages):
            status = sweep_commands.run(self.root, self.examples, self.skips, check)
        return status, messages.getvalue()

    def test_shorter_unit_test_example_keeps_same_table(self):
        self.assertEqual(self.generate()[0], 0)
        expected = self.output.read_bytes()
        tests = self.root / "tests"
        tests.mkdir()
        (tests / "test_editor.cpp").write_text('ed.command("window UV");', encoding="utf-8")
        self.assertEqual(self.generate(check=True)[0], 0)
        self.assertEqual(self.generate()[0], 0)
        self.assertEqual(self.output.read_bytes(), expected)
        self.assertIn(b'{"window", "window Inspector", ""}', expected)
        self.assertIn(b'{"open", "", "replaces the scene"}', expected)

    def test_adding_and_removing_verbs_reports_both_and_preserves_table(self):
        self.assertEqual(self.generate()[0], 0)
        expected = self.output.read_bytes()
        self.registry.write_text('const char* kConsoleCommands[] = {"open", "light"};', encoding="utf-8")
        for check in [True, False]:
            with self.subTest(check=check):
                status, message = self.generate(check)
                self.assertNotEqual(status, 0)
                self.assertIn("Missing sweep entry for registered verb: light", message)
                self.assertIn("Stale sweep entry for removed verb: window", message)
                self.assertEqual(self.output.read_bytes(), expected)

    def test_changed_example_reports_verb_until_regenerated(self):
        self.assertEqual(self.generate()[0], 0)
        previous = self.output.read_bytes()
        self.examples = ["window UV"]
        status, message = self.generate(check=True)
        self.assertNotEqual(status, 0)
        self.assertIn("window UV", message)
        self.assertEqual(previous, self.output.read_bytes())
        self.assertEqual(self.generate()[0], 0)
        self.assertIn(b'"window UV"', self.output.read_bytes())
        self.assertEqual(self.generate(check=True)[0], 0)


if __name__ == "__main__":
    unittest.main()
