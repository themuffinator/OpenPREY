#!/usr/bin/env python3
"""Validate Windows CRT linkage and stage openPREY runtime payloads."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from windows_runtime import is_windows_host, stage_runtime_payloads


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Validate Windows CRT linkage and stage non-CRT openPREY runtime payloads into build and install directories."
    )
    parser.add_argument("--source-root", required=True, help="openPREY repository root.")
    parser.add_argument("--build-dir", required=True, help="Meson build directory.")
    parser.add_argument(
        "--install-dir",
        action="append",
        default=[],
        help="Additional output directory to stage (may be provided more than once).",
    )
    return parser.parse_args(argv[1:])


def main(argv: list[str]) -> int:
    args = parse_args(argv)

    if not is_windows_host():
        return 0

    source_root = Path(args.source_root)
    build_dir = Path(args.build_dir)
    targets = [build_dir] + [Path(path) for path in args.install_dir]
    result = stage_runtime_payloads(source_root, build_dir, targets)
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
