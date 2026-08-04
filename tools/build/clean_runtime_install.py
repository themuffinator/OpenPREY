#!/usr/bin/env python3
"""Remove build-only artifacts that Meson may install beside runtime files."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path


ROOT_NON_RUNTIME_PATTERNS = (
    "*.lib",
    "*.exp",
    "*.ilk",
    "*.map",
    "*.zip",
    "mgscope_sendinput.cfg",
    "scope_autotest*.cfg",
)

GAME_NON_RUNTIME_PATTERNS = (
    "*.lib",
    "*.exp",
    "*.ilk",
    "*.map",
)

WINDOWS_STALE_GAME_PATTERNS = (
    "*.so",
    "*.dylib",
)


def remove_matches(root: Path, patterns: tuple[str, ...]) -> list[Path]:
    removed: list[Path] = []
    if not root.is_dir() or root.is_symlink():
        return removed

    for pattern in patterns:
        for path in sorted(root.glob(pattern)):
            if path.is_file() and not path.is_symlink():
                path.unlink()
                removed.append(path)
    return removed


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--install-root", required=True)
    parser.add_argument("--game-dir", required=True)
    parser.add_argument("--host-system", required=True)
    return parser.parse_args(argv[1:])


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    install_root = Path(args.install_root)
    game_dir = Path(args.game_dir)

    removed = remove_matches(install_root, ROOT_NON_RUNTIME_PATTERNS)
    game_patterns = GAME_NON_RUNTIME_PATTERNS
    if args.host_system == "windows":
        game_patterns += WINDOWS_STALE_GAME_PATTERNS
    removed += remove_matches(game_dir, game_patterns)

    if removed:
        print(f"cleaned staged non-runtime artifacts: {len(removed)}")
        for path in removed[:20]:
            print(f"  removed {path}")
        if len(removed) > 20:
            print(f"  ... {len(removed) - 20} more")

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
