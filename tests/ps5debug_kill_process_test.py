#!/usr/bin/env python3
"""Unit tests for qualification-process identity matching."""

import importlib.util
import pathlib
import sys
import types
import unittest
from enum import IntEnum


class ResponseCode(IntEnum):
    SUCCESS = 0x80000000
    ERROR = 0xF0000001


sys.modules.setdefault(
    "ps4debug", types.SimpleNamespace(PS4Debug=object, ResponseCode=ResponseCode)
)
SCRIPT = pathlib.Path(__file__).parents[1] / "examples" / "ps5debug_kill_process.py"
SPEC = importlib.util.spec_from_file_location("ps5debug_kill_process", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(MODULE)


class Process:
    def __init__(self, pid: int, name: str):
        self.pid = pid
        self.name = name


class FakeDebugger:
    def __init__(self, status=ResponseCode.SUCCESS):
        self.status = status

    async def kill_process(self):
        return self.status


class FakeDebuggerContext:
    def __init__(self, debugger=None, error=None):
        self.debugger = debugger
        self.error = error

    async def __aenter__(self):
        if self.error is not None:
            raise self.error
        return self.debugger

    async def __aexit__(self, exc_type, exc, traceback):
        return False


class FakeClient:
    def __init__(self, process_lists, *, debugger=None, attach_error=None):
        self.process_lists = list(process_lists)
        self.debugger_instance = debugger or FakeDebugger()
        self.attach_error = attach_error

    async def get_processes(self):
        if not self.process_lists:
            raise AssertionError("unexpected process-list request")
        return self.process_lists.pop(0)

    def debugger(self, pid, *, resume):
        return FakeDebuggerContext(self.debugger_instance, self.attach_error)


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


class AbsenceConfirmationTest(unittest.IsolatedAsyncioTestCase):
    async def test_repeated_absence_is_accepted(self):
        client = FakeClient([[], []])
        self.assertTrue(await MODULE.confirm_absent(client, 157, "eboot.bin", interval=0))

    async def test_one_transient_absence_is_rejected(self):
        eboot = Process(157, "eboot.bin")
        client = FakeClient([[], [eboot]])
        self.assertFalse(await MODULE.confirm_absent(client, 157, "eboot.bin", interval=0))

    async def test_attach_race_accepts_only_repeated_absence(self):
        eboot = Process(157, "eboot.bin")
        client = FakeClient([[], []], attach_error=RuntimeError("non-suspendable"))
        result = await MODULE.kill_and_confirm_absent(client, eboot, 157, "eboot.bin")
        self.assertEqual(result, 0)

    async def test_attach_failure_with_live_process_fails_closed(self):
        eboot = Process(157, "eboot.bin")
        client = FakeClient([[eboot]], attach_error=RuntimeError("non-suspendable"))
        result = await MODULE.kill_and_confirm_absent(client, eboot, 157, "eboot.bin")
        self.assertEqual(result, 3)

    async def test_failed_kill_status_with_live_process_fails_closed(self):
        eboot = Process(157, "eboot.bin")
        client = FakeClient([[eboot]], debugger=FakeDebugger(ResponseCode.ERROR))
        result = await MODULE.kill_and_confirm_absent(client, eboot, 157, "eboot.bin")
        self.assertEqual(result, 3)

    async def test_successful_kill_requires_repeated_absence(self):
        eboot = Process(157, "eboot.bin")
        client = FakeClient([[], []])
        result = await MODULE.kill_and_confirm_absent(client, eboot, 157, "eboot.bin")
        self.assertEqual(result, 0)


if __name__ == "__main__":
    unittest.main()
