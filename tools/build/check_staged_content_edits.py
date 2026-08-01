#!/usr/bin/env python3
"""Prune stale loose basepr content before staged installs."""

from __future__ import annotations

import argparse
import os
import shutil
import sys
from pathlib import Path


STALE_LOOSE_ROOT_FILES = (
    "default.cfg",
    "openq4_defaults.cfg",
    "openq4_profile_steamdeck.cfg",
)

STALE_LOOSE_SUBDIRS = (
    "botfiles",
    "def",
    "env",
    "gfx",
    "glprogs",
    "guis",
    "maps",
    "materials",
    "scripts",
    "strings",
)


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Remove stale loose basepr content that is now compiled into openPREY PK4s "
            "before Meson stages a fresh install."
        )
    )
    parser.add_argument(
        "--source-root",
        default=".",
        help="Repository root (defaults to current directory).",
    )
    return parser.parse_args(argv[1:])


def assert_inside(path: Path, root: Path) -> Path:
    resolved_path = path.resolve()
    resolved_root = root.resolve()
    try:
        resolved_path.relative_to(resolved_root)
    except ValueError as exc:
        raise RuntimeError(f"refusing to remove path outside staged game directory: {resolved_path}") from exc
    return resolved_path


def assert_path_entry_inside(path: Path, root: Path) -> None:
    resolved_root = root.resolve()
    resolved_parent = path.parent.resolve()
    try:
        resolved_parent.relative_to(resolved_root)
    except ValueError as exc:
        raise RuntimeError(f"refusing to remove path outside staged game directory: {path}") from exc


def remove_stale_path(path: Path, staged_game_dir: Path) -> bool:
    if not path.exists() and not path.is_symlink():
        return False

    if path.is_symlink():
        assert_path_entry_inside(path, staged_game_dir)
        path.unlink()
        return True

    safe_path = assert_inside(path, staged_game_dir)
    if safe_path.is_file():
        safe_path.unlink()
        return True
    if safe_path.is_dir():
        shutil.rmtree(safe_path)
        return True

    raise RuntimeError(f"refusing to remove non-file staged content path: {safe_path}")


def prune_stale_loose_content(source_root: Path) -> list[Path]:
    staged_game_dir = source_root / ".install" / "basepr"
    if staged_game_dir.is_symlink():
        raise RuntimeError(f"refusing to prune symlinked staged game directory: {staged_game_dir}")
    if not staged_game_dir.is_dir():
        return []

    removed: list[Path] = []
    for name in STALE_LOOSE_ROOT_FILES:
        candidate = staged_game_dir / name
        if remove_stale_path(candidate, staged_game_dir):
            removed.append(Path(name))

    for name in STALE_LOOSE_SUBDIRS:
        candidate = staged_game_dir / name
        if remove_stale_path(candidate, staged_game_dir):
            removed.append(Path(name))

    return removed


def validate_source_root(path: Path) -> Path:
    if path.is_symlink():
        raise RuntimeError(f"source root must not be a symlink: {path}")
    resolved = path.resolve()
    if not resolved.is_dir():
        raise RuntimeError(f"source root not found: {resolved}")
    return resolved


def main(argv: list[str]) -> int:
    if os.environ.get("OPENQ4_INSTALL_KEEP_STALE_LOOSE_CONTENT") == "1":
        return 0

    args = parse_args(argv)

    try:
        source_root = validate_source_root(Path(args.source_root))
        removed = prune_stale_loose_content(source_root)
    except RuntimeError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    for relative_path in removed:
        print(f"Removing stale loose staged content '.install/basepr/{relative_path.as_posix()}'")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
