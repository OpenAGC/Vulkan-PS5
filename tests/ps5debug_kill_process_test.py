#!/usr/bin/env python3
"""Unit tests for qualification-process identity matching."""

import importlib.util
import pathlib
import sys
import types
import unittest


sys.modules.setdefault("ps4debug", types.SimpleNamespace(PS4Debug=object))
SCRIPT = pathlib.Path(__file__).parents[1] / "examples" / "ps5debug_kill_process.py"
SPEC = importlib.util.spec_from_file_location("ps5debug_kill_process", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(MODULE)


class Process:
    def __init__(self, pid: int, name: str):
        self.pid = pid
        self.name = name


class ProcessMatchTest(unittest.TestCase):
    def test_pid_and_name_must_both_match(self):
        shell_ui = Process(158, "SceShellUI")
        self.assertFalse(MODULE.process_matches(shell_ui, 158, "eboot.bin"))

    def test_expected_native_process_matches(self):
        eboot = Process(157, "eboot.bin")
        self.assertTrue(MODULE.process_matches(eboot, 157, "eboot.bin"))

    def test_name_only_remains_exact(self):
        self.assertTrue(MODULE.process_matches(Process(157, "eboot.bin"), None, "eboot.bin"))
        self.assertFalse(MODULE.process_matches(Process(158, "SceShellUI"), None, "eboot.bin"))


if __name__ == "__main__":
    unittest.main()
