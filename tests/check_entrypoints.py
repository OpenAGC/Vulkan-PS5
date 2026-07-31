#!/usr/bin/env python3
import pathlib
import re
import subprocess
import sys
import xml.etree.ElementTree as ET


def core_commands(registry_path: str) -> list[str]:
    root = ET.parse(registry_path).getroot()
    commands: list[str] = []
    for feature in root.findall("./feature"):
        apis = feature.get("api", "").split(",")
        if "vulkan" not in apis or feature.get("number") not in (
                "1.0", "1.1", "1.2"):
            continue
        commands.extend(node.get("name") for node in feature.findall("./require/command"))
    return list(dict.fromkeys(commands))


def exported_symbols(library_path: str) -> set[str]:
    if sys.platform == "darwin":
        command = ["nm", "-gU", library_path]
    else:
        command = ["nm", "-D", "--defined-only", library_path]
    output = subprocess.check_output(command, text=True)
    return set(re.findall(r"_?(vk[A-Za-z0-9_]+)$", output, re.MULTILINE))


def dispatch_entries(source_path: str) -> set[str]:
    source = pathlib.Path(source_path).read_text(encoding="utf-8")
    entries = set(re.findall(r"ENTRY\((vk[A-Za-z0-9_]+)\)", source))
    entries.update(("vkGetInstanceProcAddr", "vkGetDeviceProcAddr"))
    return entries


def main() -> int:
    required = set(core_commands(sys.argv[1]))
    missing_exports = sorted(required - exported_symbols(sys.argv[2]))
    missing_dispatch = sorted(required - dispatch_entries(sys.argv[3]))
    if missing_exports or missing_dispatch:
        if missing_exports:
            print("Missing exports:", *missing_exports, sep="\n  ")
        if missing_dispatch:
            print("Missing proc-table entries:", *missing_dispatch, sep="\n  ")
        return 1
    print(f"Vulkan 1.0-1.2 core entrypoints covered: {len(required)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
