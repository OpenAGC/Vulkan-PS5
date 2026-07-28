#!/usr/bin/env python3
"""Bounded ps5debug-NG checks for an exact qualification PID or name."""

import argparse
import asyncio

from ps4debug import PS4Debug


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
    processes = await asyncio.wait_for(client.get_processes(), timeout=8.0)
    matches = [
        process
        for process in processes
        if (args.pid is not None and process.pid == args.pid)
        or (args.pid is None and process.name == args.name)
    ]
    description = f"pid {args.pid}" if args.pid is not None else repr(args.name)
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

    print(f"ps5debug-NG: killing pid {process.pid} ({process.name})")
    async with client.debugger(process.pid, resume=False) as debugger:
        await asyncio.wait_for(debugger.kill_process(), timeout=5.0)
    return 0


if __name__ == "__main__":
    raise SystemExit(asyncio.run(main()))
