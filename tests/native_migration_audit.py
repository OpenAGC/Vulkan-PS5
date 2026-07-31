#!/usr/bin/env python3
"""Require every direct hardware-facing OpenAGC call to have a migration owner."""

import csv
import pathlib
import re
import sys


CALL = re.compile(
    r"\b(?:sceAgc[A-Za-z0-9_]*|sce_agc_[A-Za-z0-9_]*|"
    r"agcCb[A-Za-z0-9_]*|agcGfx1013[A-Za-z0-9_]*|"
    r"agcGpuMemory[A-Za-z0-9_]*|agcVideoOut[A-Za-z0-9_]*)\s*\("
)
VALID_CATEGORIES = {
    "lifecycle", "memory", "resource", "shader", "pipeline", "command",
    "transition", "synchronization", "query", "presentation",
}


def main() -> int:
    root = pathlib.Path(sys.argv[1]).resolve()
    inventory_path = root / "analysis" / "native_runtime_calls.tsv"
    with inventory_path.open(newline="", encoding="utf-8") as stream:
        reader = csv.DictReader(stream, delimiter="\t")
        rows = list(reader)
    if set(reader.fieldnames or ()) != {
        "symbol", "category", "native_owner", "regression_gate"
    }:
        raise SystemExit("native migration inventory has an invalid schema")
    inventory = {}
    for row in rows:
        symbol = row["symbol"]
        if symbol in inventory:
            raise SystemExit(f"duplicate inventory symbol: {symbol}")
        if row["category"] not in VALID_CATEGORIES:
            raise SystemExit(f"invalid category for {symbol}: {row['category']}")
        if not row["native_owner"] or not row["regression_gate"]:
            raise SystemExit(f"missing migration owner or gate for {symbol}")
        inventory[symbol] = row

    observed = set()
    for source in sorted((root / "src").glob("*.c")):
        text = source.read_text(encoding="utf-8")
        observed.update(match.group(0).split("(", 1)[0].strip()
                        for match in CALL.finditer(text))
    missing = sorted(observed - inventory.keys())
    stale = sorted(inventory.keys() - observed)
    if missing or stale:
        if missing:
            print("unowned direct calls: " + ", ".join(missing))
        if stale:
            print("stale inventory calls: " + ", ".join(stale))
        return 1
    print(f"native-migration-audit: {len(observed)} direct calls owned")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
