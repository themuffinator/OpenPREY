#!/usr/bin/env python3
"""Atomically stage a generated runtime file into builddir/basepr."""

from __future__ import annotations

import argparse
import filecmp
import os
from pathlib import Path
import shutil
import tempfile


def atomic_copy_if_changed(source: Path, destination: Path) -> bool:
    if destination.is_file() and filecmp.cmp(source, destination, shallow=False):
        return False

    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary_path: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            dir=destination.parent,
            prefix=f".{destination.name}.",
            suffix=".tmp",
            delete=False,
        ) as temporary:
            temporary_path = Path(temporary.name)
        shutil.copy2(source, temporary_path)
        os.replace(temporary_path, destination)
        temporary_path = None
    finally:
        if temporary_path is not None:
            temporary_path.unlink(missing_ok=True)
    return True


def atomic_write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary_path: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w",
            encoding="utf-8",
            newline="\n",
            dir=path.parent,
            prefix=f".{path.name}.",
            suffix=".tmp",
            delete=False,
        ) as temporary:
            temporary.write(text)
            temporary_path = Path(temporary.name)
        os.replace(temporary_path, path)
        temporary_path = None
    finally:
        if temporary_path is not None:
            temporary_path.unlink(missing_ok=True)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--destination", required=True, type=Path)
    parser.add_argument("--stamp", required=True, type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    source = args.source.resolve()
    destination = args.destination.resolve()
    stamp = args.stamp.resolve()

    if not source.is_file():
        raise SystemExit(f"generated runtime file does not exist: {source}")
    if destination.exists() and not destination.is_file():
        raise SystemExit(f"direct-run runtime destination is not a file: {destination}")
    if source == destination:
        raise SystemExit("generated and direct-run runtime paths must differ")

    changed = atomic_copy_if_changed(source, destination)
    source_stat = source.stat()
    atomic_write_text(
        stamp,
        "\n".join(
            (
                f"source={source}",
                f"destination={destination}",
                f"size={source_stat.st_size}",
                f"mtime_ns={source_stat.st_mtime_ns}",
                "",
            )
        ),
    )
    state = "updated" if changed else "current"
    print(f"direct-run runtime file {state}: {destination}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
