#!/usr/bin/env python3
"""Build basepr/pak0.pk4, basepr/pak1.pk4, and the generated integrity header."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from openq4_pak import (
    PAK0_NAME,
    PAK1_NAME,
    copy_file_if_changed,
    create_game_pk4,
    format_openq4_paks_header,
    write_text_if_changed,
)


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Build the openPREY pak0.pk4 and pak1.pk4 runtime packs and generated checksum header."
    )
    parser.add_argument("--pak0-source-dir", required=True, help="Source content/basepr/pak0 directory.")
    parser.add_argument("--pak1-source-dir", required=True, help="Source content/basepr/pak1 directory.")
    parser.add_argument("--pak0-out", required=True, help="Output pak0.pk4 path.")
    parser.add_argument("--pak1-out", required=True, help="Output pak1.pk4 path.")
    parser.add_argument("--header-out", required=True, help="Generated C/C++ header path.")
    parser.add_argument(
        "--pak0-stage-out",
        default="",
        help="Optional additional basepr/pak0.pk4 path to copy for direct builddir launches.",
    )
    parser.add_argument(
        "--pak1-stage-out",
        default="",
        help="Optional additional basepr/pak1.pk4 path to copy for direct builddir launches.",
    )
    return parser.parse_args(argv[1:])


def copy_if_changed(source: Path, destination: Path) -> None:
    copy_file_if_changed(source, destination)


def require_pack_source_dir(path: Path, pak_name: str) -> Path:
    if path.is_symlink():
        raise RuntimeError(f"{pak_name} source directory must not be a symlink: {path}")
    resolved = path.resolve()
    if not resolved.is_dir():
        raise RuntimeError(f"{pak_name} source directory not found: {resolved}")
    return resolved


def build_pack(pak_name: str, source_dir: Path, pak_out: Path) -> object:
    result = create_game_pk4(source_dir, pak_out, pak_name=pak_name)
    if result.added_files == 0:
        print(f"error: {pak_name} packaging found no eligible files after filtering", file=sys.stderr)
        raise SystemExit(1)
    if result.missing_required:
        print(f"error: {pak_name} packaging is missing required runtime files:", file=sys.stderr)
        for relative_path in result.missing_required:
            print(f"  - {relative_path}", file=sys.stderr)
        raise SystemExit(1)
    return result


def main(argv: list[str]) -> int:
    args = parse_args(argv)

    try:
        pak0_source_dir = require_pack_source_dir(Path(args.pak0_source_dir), PAK0_NAME)
        pak1_source_dir = require_pack_source_dir(Path(args.pak1_source_dir), PAK1_NAME)
    except RuntimeError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    pak0_out = Path(args.pak0_out).resolve()
    pak1_out = Path(args.pak1_out).resolve()
    header_out = Path(args.header_out).resolve()

    pak0_result = build_pack(PAK0_NAME, pak0_source_dir, pak0_out)
    pak1_result = build_pack(PAK1_NAME, pak1_source_dir, pak1_out)

    write_text_if_changed(header_out, format_openq4_paks_header(pak0_result, pak1_result))

    if args.pak0_stage_out:
        copy_if_changed(pak0_out, Path(args.pak0_stage_out).resolve())
    if args.pak1_stage_out:
        copy_if_changed(pak1_out, Path(args.pak1_stage_out).resolve())

    print(
        f"built pak0.pk4: md5={pak0_result.md5_hex} "
        f"size={pak0_result.size_bytes} files={pak0_result.added_files}"
    )
    print(
        f"built pak1.pk4: md5={pak1_result.md5_hex} "
        f"size={pak1_result.size_bytes} files={pak1_result.added_files}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
