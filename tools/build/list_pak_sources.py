#!/usr/bin/env python3
"""List openPREY PK4 source files for diagnostics and legacy Meson dependency checks."""

from __future__ import annotations

import sys
from pathlib import Path

from openq4_pak import _iter_pk4_entries, require_directory_inside


def normalize(path: str) -> str:
    return path.replace("\\", "/").strip("/")


def main(argv: list[str]) -> int:
    if len(argv) < 4:
        print(f"usage: {argv[0]} <source-root> <pak-name> <content-subdir>", file=sys.stderr)
        return 2

    raw_source_root = Path(argv[1])
    if raw_source_root.is_symlink():
        print(f"error: source root must not be a symlink: {raw_source_root}", file=sys.stderr)
        return 1
    source_root = raw_source_root.resolve()
    pak_name = argv[2].strip()
    subdir = normalize(argv[3])
    try:
        content_root = require_directory_inside(source_root / subdir, source_root, "pack source")
    except RuntimeError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    try:
        entries, _skipped_samples = _iter_pk4_entries(content_root, pak_name)
    except RuntimeError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    if not entries:
        print(f"error: pack source directory has no eligible files: {content_root}", file=sys.stderr)
        return 1

    for path, _arcname in entries:
        print(path.relative_to(source_root).as_posix())

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
