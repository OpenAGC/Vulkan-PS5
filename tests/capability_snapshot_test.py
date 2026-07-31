#!/usr/bin/env python3
"""Reject changes to the committed Vulkan capability baseline."""

import pathlib
import subprocess
import sys


def main() -> int:
    executable = pathlib.Path(sys.argv[1])
    baseline = pathlib.Path(sys.argv[2])
    result = subprocess.run(
        [str(executable)], check=False, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if result.returncode:
        sys.stderr.buffer.write(result.stderr)
        return result.returncode
    expected = baseline.read_bytes()
    if result.stdout != expected:
        actual_lines = result.stdout.decode("utf-8").splitlines()
        expected_lines = expected.decode("utf-8").splitlines()
        limit = max(len(actual_lines), len(expected_lines))
        for index in range(limit):
            actual = actual_lines[index] if index < len(actual_lines) else "<missing>"
            wanted = expected_lines[index] if index < len(expected_lines) else "<missing>"
            if actual != wanted:
                print(f"capability baseline drift at line {index + 1}", file=sys.stderr)
                print(f"expected: {wanted}", file=sys.stderr)
                print(f"actual:   {actual}", file=sys.stderr)
                break
        return 1
    print("vulkan-capability-baseline: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
