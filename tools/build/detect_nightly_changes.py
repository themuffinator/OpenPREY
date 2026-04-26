#!/usr/bin/env python3
"""Detect whether a nightly release should run."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from nightly_release_common import (
    collect_releasable_changed_files,
    find_previous_nightly_tag,
)


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Detect OpenPrey nightly release changes.")
    parser.add_argument("--output", required=True, help="GitHub output file path.")
    parser.add_argument("--head-ref", default="HEAD", help="Git ref to compare (default: HEAD).")
    parser.add_argument(
        "--force",
        action="store_true",
        help="Force a release even when no releasable changes were detected.",
    )
    return parser.parse_args(argv[1:])


def append_output(path: Path, name: str, value: str) -> None:
    with path.open("a", encoding="utf-8") as handle:
        handle.write(f"{name}={value}\n")


def main(argv: list[str]) -> int:
    args = parse_args(argv)

    previous_tag = find_previous_nightly_tag()
    changed_files = collect_releasable_changed_files(previous_tag, head_ref=args.head_ref)
    auto_has_changes = bool(changed_files)
    has_changes = args.force or auto_has_changes

    if previous_tag:
        print(f"Previous nightly tag: {previous_tag}")
    else:
        print("Previous nightly tag: <none>")

    if changed_files:
        print("Detected releasable changes:")
        preview_limit = 20
        for path in changed_files[:preview_limit]:
            print(f"  - {path}")
        remaining = len(changed_files) - preview_limit
        if remaining > 0:
            print(f"  - ... plus {remaining} more changed path(s)")
    else:
        print("Detected releasable changes: none")

    if args.force and not auto_has_changes:
        print("Nightly release forced by manual dispatch.")

    output_path = Path(args.output)
    append_output(output_path, "previous_tag", previous_tag or "")
    append_output(output_path, "change_count", str(len(changed_files)))
    append_output(output_path, "has_changes", "true" if has_changes else "false")
    append_output(output_path, "forced", "true" if args.force else "false")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv))
    except RuntimeError as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1) from exc
