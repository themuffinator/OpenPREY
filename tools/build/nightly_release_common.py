#!/usr/bin/env python3
"""Shared helpers for OpenPrey nightly release tooling."""

from __future__ import annotations

import subprocess
from collections.abc import Sequence


NIGHTLY_TAG_PREFIX = "nightly-"
RELEASABLE_PATHS: tuple[str, ...] = (
    ".github/workflows/nightly-builds.yml",
    "assets",
    "basepy",
    "meson.build",
    "meson_options.txt",
    "src",
    "subprojects",
    "tools/build",
)


def run_git(args: Sequence[str]) -> str:
    try:
        result = subprocess.run(
            ["git", *args],
            check=True,
            text=True,
            capture_output=True,
        )
    except subprocess.CalledProcessError as exc:
        raise RuntimeError(exc.stderr.strip() or exc.stdout.strip()) from exc
    return result.stdout.strip()


def find_previous_nightly_tag(current_release_tag: str | None = None) -> str | None:
    tags = run_git(["tag", "--list", f"{NIGHTLY_TAG_PREFIX}*", "--sort=-creatordate"])
    for raw_tag in tags.splitlines():
        tag = raw_tag.strip()
        if not tag or tag == current_release_tag:
            continue
        return tag
    return None


def collect_releasable_changed_files(
    previous_tag: str | None,
    head_ref: str = "HEAD",
    releasable_paths: Sequence[str] = RELEASABLE_PATHS,
) -> list[str]:
    if previous_tag:
        output = run_git(
            ["diff", "--name-only", f"{previous_tag}..{head_ref}", "--", *releasable_paths]
        )
    else:
        output = run_git(["ls-files", "--", *releasable_paths])

    return [line.strip() for line in output.splitlines() if line.strip()]
