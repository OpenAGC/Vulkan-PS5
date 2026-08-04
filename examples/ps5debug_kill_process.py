#!/usr/bin/env python3
"""Bounded ps5debug-NG checks for an exact qualification PID or name."""

import argparse
import asyncio

from ps4debug import PS4Debug, ResponseCode


def process_matches(process, pid: int | None, name: str | None) -> bool:
    """Match a PID and optional name conjunctively to reject PID reuse."""
    if pid is not None:
        return process.pid == pid and (name is None or process.name == name)
    return process.name == name


def matching_processes(processes, pid: int | None, name: str | None):
    """Return every exact identity match from one authoritative process list."""
    return [process for process in processes if process_matches(process, pid, name)]


async def get_matches(client, pid: int | None, name: str | None):
    processes = await asyncio.wait_for(client.get_processes(), timeout=8.0)
    return matching_processes(processes, pid, name)


async def confirm_absent(
    client,
    pid: int | None,
    name: str | None,
    *,
    attempts: int = 2,
    interval: float = 0.1,
) -> bool:
    """Require repeated exact-name/PID absence; one transient empty list is insufficient."""
    if attempts < 2:
        raise ValueError("absence confirmation requires at least two attempts")

    for attempt in range(attempts):
        if await get_matches(client, pid, name):
            return False
        if attempt + 1 < attempts:
            await asyncio.sleep(interval)
    return True


async def kill_and_confirm_absent(client, process, pid: int | None, name: str | None) -> int:
    """Kill one exact match, accepting an attach race only after repeated absence proof."""
    print(f"ps5debug-NG: killing pid {process.pid} ({process.name})")
    try:
        async with client.debugger(process.pid, resume=False) as debugger:
            status = await asyncio.wait_for(debugger.kill_process(), timeout=5.0)
            if status != ResponseCode.SUCCESS:
                raise RuntimeError(f"kill command returned {status!r}")
    except Exception as error:
        if await confirm_absent(client, pid, name):
            print(
                "ps5debug-NG: process exited during debugger attach/kill; "
                "exact absence confirmed twice"
            )
            return 0
        print(f"ps5debug-NG: kill failed and exact process remains: {error}")
        return 3

    if not await confirm_absent(client, pid, name):
        print("ps5debug-NG: kill returned success but exact process remains")
        return 3
    print("ps5debug-NG: exact process absence confirmed twice after kill")
    return 0


async def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--assert-absent",
        action="store_true",
        help="fail if a matching process remains instead of killing it",
    )
    parser.add_argument("--pid", type=int, help="match this exact process ID")
    parser.add_argument("host")
    parser.add_argument("name", nargs="?", help="match this exact process name")
    args = parser.parse_args()
    if args.pid is None and args.name is None:
        parser.error("provide --pid or name")

    client = PS4Debug(args.host, timeout=5.0)
    matches = await get_matches(client, args.pid, args.name)
    description = (
        f"pid {args.pid} named {args.name!r}"
        if args.pid is not None and args.name is not None
        else f"pid {args.pid}"
        if args.pid is not None
        else repr(args.name)
    )
    if not matches:
        print(f"ps5debug-NG: no process matching {description}")
        return 0
    if len(matches) != 1:
        print(f"ps5debug-NG: refusing ambiguous match: {matches}")
        return 2

    process = matches[0]
    if args.assert_absent:
        print(f"ps5debug-NG: process still present: {process.pid} ({process.name})")
        return 1

    return await kill_and_confirm_absent(client, process, args.pid, args.name)


if __name__ == "__main__":
    raise SystemExit(asyncio.run(main()))
