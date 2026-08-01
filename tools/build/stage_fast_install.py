#!/usr/bin/env python3
"""Incrementally stage local runtime files from builddir into .install."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from openq4_pak import copy_file_if_changed, is_relative_to


ROOT_RUNTIME_PATTERNS = (
    "openPREY-client_*.exe",
    "openPREY-client_*.pdb",
    "openPREY-ded_*.exe",
    "openPREY-ded_*.pdb",
    "OpenAL32.dll",
)
GAME_RUNTIME_PATTERNS = (
    "game_*.dll",
    "game_*.pdb",
    "game_*.so",
    "game_*.dylib",
    "mod.json",
    "pak0.pk4",
    "pak1.pk4",
)
NON_RUNTIME_PATTERNS = (
    "*.exp",
    "*.ilk",
    "*.lib",
    "*.map",
    "*.zip",
)


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Stage changed local build outputs into .install without running meson install."
    )
    parser.add_argument("--build-dir", required=True, help="Meson build directory.")
    parser.add_argument("--install-dir", required=True, help="Runtime staging root, usually .install.")
    parser.add_argument(
        "--source-root",
        default=str(Path(__file__).resolve().parents[2]),
        help="openPREY source root used to validate the fast staging target.",
    )
    return parser.parse_args(argv[1:])


def copy_if_changed(source: Path, destination: Path) -> bool:
    return copy_file_if_changed(source, destination)


def validate_stage_roots(source_root: Path, build_dir: Path, install_dir: Path) -> None:
    for label, path in (
        ("source root", source_root),
        ("build directory", build_dir),
        ("install directory", install_dir),
    ):
        if path.is_symlink():
            raise RuntimeError(f"fast staging {label} must not be a symlink: {path}")

    source_root = source_root.resolve()
    build_dir = build_dir.resolve()
    install_dir = install_dir.resolve()
    expected_install_dir = source_root / ".install"

    if install_dir != expected_install_dir:
        raise RuntimeError(f"fast staging install directory must be {expected_install_dir}: {install_dir}")
    if not is_relative_to(build_dir, source_root):
        raise RuntimeError(f"fast staging build directory must stay under {source_root}: {build_dir}")
    if build_dir == install_dir or is_relative_to(install_dir, build_dir) or is_relative_to(build_dir, install_dir):
        raise RuntimeError(f"fast staging build and install directories must not overlap: {build_dir}")


def remove_matches(root: Path, patterns: tuple[str, ...]) -> list[Path]:
    removed: list[Path] = []
    if not root.is_dir():
        return removed
    for pattern in patterns:
        for path in sorted(root.glob(pattern)):
            if path.is_file():
                path.unlink()
                removed.append(path)
    return removed


def copy_matches(source_root: Path, destination_root: Path, patterns: tuple[str, ...]) -> list[Path]:
    copied: list[Path] = []
    if not source_root.is_dir():
        return copied
    for pattern in patterns:
        for source in sorted(source_root.glob(pattern)):
            if not source.is_file():
                continue
            destination = destination_root / source.name
            if copy_if_changed(source, destination):
                copied.append(destination)
    return copied


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    source_root = Path(args.source_root)
    build_dir = Path(args.build_dir)
    install_dir = Path(args.install_dir)

    try:
        validate_stage_roots(source_root, build_dir, install_dir)
        source_root = source_root.resolve()
        build_dir = build_dir.resolve()
        install_dir = install_dir.resolve()
        build_game_dir = build_dir / "basepr"
        install_game_dir = install_dir / "basepr"
        install_dir.mkdir(parents=True, exist_ok=True)
        install_game_dir.mkdir(parents=True, exist_ok=True)

        removed = remove_matches(install_dir, NON_RUNTIME_PATTERNS)
        removed += remove_matches(install_game_dir, NON_RUNTIME_PATTERNS + ("*.so", "*.dylib"))
        copied = copy_matches(build_dir, install_dir, ROOT_RUNTIME_PATTERNS)
        copied += copy_matches(build_game_dir, install_game_dir, GAME_RUNTIME_PATTERNS)
    except RuntimeError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    print(
        f"fast-staged .install: copied={len(copied)} "
        f"removed_non_runtime={len(removed)}"
    )
    for path in copied[:20]:
        print(f"  copied {path}")
    if len(copied) > 20:
        print(f"  ... {len(copied) - 20} more")
    return 0


if __name__ == "__main__":
    import sys

    raise SystemExit(main(sys.argv))
