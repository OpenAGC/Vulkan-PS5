#!/usr/bin/env python3
"""Fail closed on Prospero-incompatible ICD relocations and symbol leakage."""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path


ALLOWED_RELOCATIONS = {
    "R_X86_64_RELATIVE",
    "R_X86_64_GLOB_DAT",
    "R_X86_64_JUMP_SLOT",
}
ALLOWED_NEEDED = {
    "libkernel.sprx",
    "libSceVideoOut.sprx",
    "libSceLibcInternal.sprx",
    "libSceNet.sprx",
}
REQUIRED_EXPORTS = {
    "vkGetInstanceProcAddr",
    "vkGetDeviceProcAddr",
    "vk_icdGetInstanceProcAddr",
    "vk_icdGetPhysicalDeviceProcAddr",
    "vk_icdNegotiateLoaderICDInterfaceVersion",
    "vkCmdBindVertexBuffers2",
}


def readelf(tool: str, library: Path, *args: str) -> str:
    completed = subprocess.run(
        [tool, *args, str(library)],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if completed.returncode != 0:
        print(completed.stderr, end="", file=sys.stderr)
        raise SystemExit(
            f"{tool} {' '.join(args)} failed for {library} "
            f"with status {completed.returncode}"
        )
    return completed.stdout


def main() -> int:
    if len(sys.argv) != 3:
        print(
            f"usage: {Path(sys.argv[0]).name} <llvm-readelf> <libvulkan_ps5.so>",
            file=sys.stderr,
        )
        return 2

    tool = sys.argv[1]
    library = Path(sys.argv[2])
    if not library.is_file():
        print(f"missing Prospero ICD: {library}", file=sys.stderr)
        return 2

    relocation_text = readelf(tool, library, "-rW")
    relocations = set(re.findall(r"\bR_X86_64_[A-Z0-9_]+\b", relocation_text))
    unsupported_relocations = sorted(relocations - ALLOWED_RELOCATIONS)
    if not relocations or unsupported_relocations:
        print(
            "unsupported or missing Prospero ICD relocations: "
            + (", ".join(unsupported_relocations) or "none found"),
            file=sys.stderr,
        )
        return 1

    dynamic_text = readelf(tool, library, "-dW")
    needed = set(re.findall(r"\(NEEDED\).*\[([^]]+)\]", dynamic_text))
    unexpected_needed = sorted(needed - ALLOWED_NEEDED)
    if unexpected_needed:
        print(
            "unexpected Prospero ICD dependencies: "
            + ", ".join(unexpected_needed),
            file=sys.stderr,
        )
        return 1
    if "(TEXTREL)" in dynamic_text or "(RPATH)" in dynamic_text or \
            "(RUNPATH)" in dynamic_text:
        print("Prospero ICD contains TEXTREL, RPATH, or RUNPATH", file=sys.stderr)
        return 1
    if not re.search(r"\(SONAME\).*\[libvulkan_ps5\.so\]", dynamic_text):
        print("Prospero ICD SONAME is not libvulkan_ps5.so", file=sys.stderr)
        return 1

    symbol_text = readelf(tool, library, "--dyn-syms", "--wide")
    exports: set[str] = set()
    for line in symbol_text.splitlines():
        fields = line.split()
        if len(fields) < 8 or not fields[0].rstrip(":").isdigit():
            continue
        if fields[4] != "GLOBAL" or fields[6] == "UND":
            continue
        exports.add(fields[7].split("@", 1)[0])

    missing_exports = sorted(REQUIRED_EXPORTS - exports)
    leaked_exports = sorted(
        name for name in exports if not re.match(r"^vk(?:[A-Z]|_icd)", name)
    )
    if missing_exports or leaked_exports:
        if missing_exports:
            print("missing ICD exports: " + ", ".join(missing_exports), file=sys.stderr)
        if leaked_exports:
            print("leaked ICD exports: " + ", ".join(leaked_exports), file=sys.stderr)
        return 1

    print(
        "prospero-icd: PASS "
        f"relocations={','.join(sorted(relocations))} "
        f"dependencies={len(needed)} exports={len(exports)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
