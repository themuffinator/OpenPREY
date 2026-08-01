#!/usr/bin/env python3
"""Regression checks for the VS Code fast default build path."""

from __future__ import annotations

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def reject(haystack: str, needle: str, context: str) -> None:
    if needle in haystack:
        raise AssertionError(f"Unexpected {needle!r} in {context}")


def find_task(tasks: list[dict[str, object]], label: str) -> dict[str, object]:
    for task in tasks:
        if task.get("label") == label:
            return task
    raise AssertionError(f"Missing VS Code task {label!r}")


def validate_tasks() -> None:
    tasks = json.loads(read(".vscode/tasks.json"))["tasks"]
    default_tasks = [
        task
        for task in tasks
        if isinstance(task.get("group"), dict) and task["group"].get("kind") == "build" and task["group"].get("isDefault") is True
    ]
    if len(default_tasks) != 1:
        raise AssertionError(f"Expected exactly one default build task, found {len(default_tasks)}")

    fast_build = find_task(tasks, "Build openPREY (Meson Debug)")
    if fast_build is not default_tasks[0]:
        raise AssertionError("Build openPREY (Meson Debug) must be the default VS Code build task")
    if fast_build.get("dependsOn"):
        raise AssertionError("Fast default build must not depend on configure or full install tasks")
    if "fastbuild" not in fast_build.get("args", []):
        raise AssertionError("Fast default build must invoke meson-task.ps1 fastbuild")

    full_build = find_task(tasks, "Full Build and Stage openPREY (Meson Debug)")
    if full_build.get("dependsOrder") != "sequence":
        raise AssertionError("Full build task must keep ordered configure, compile, install steps")
    for label in (
        "Configure openPREY (Meson Debug)",
        "Compile openPREY (Meson Debug)",
        "Stage openPREY Install Tree (Meson Debug)",
    ):
        if label not in full_build.get("dependsOn", []):
            raise AssertionError(f"Full build task is missing dependency {label!r}")


def validate_wrapper() -> None:
    wrapper = read(".vscode/meson-task.ps1")
    require(wrapper, "[ValidateSet('setup', 'compile', 'install', 'fastbuild')]", "VS Code Meson wrapper actions")
    require(wrapper, "stage_fast_install.py", "VS Code fast build staging script")
    require(wrapper, "check_staged_content_edits.py", "VS Code fast build staged content guard")
    require(wrapper, "'compile',", "VS Code fast build compiles through Meson")
    require(wrapper, "'--install-dir',", "VS Code fast build stages .install incrementally")

    stager = read("tools/build/stage_fast_install.py")
    require(stager, "copy_file_if_changed", "fast install copy-if-changed behavior")
    require(stager, '"pak0.pk4"', "fast install stages pak0")
    require(stager, '"pak1.pk4"', "fast install stages pak1")
    reject(stager, '"*.lib",\n    "pak0.pk4"', "fast install must not copy linker artifacts as runtime content")


def validate_launch_configs() -> None:
    launch = json.loads(read(".vscode/launch.json"))
    for config in launch.get("configurations", []):
        if "preLaunchTask" in config:
            raise AssertionError(f"Launch config {config.get('name')!r} must not define preLaunchTask")
        args = config.get("args", [])
        if not isinstance(args, list) or not any(
            args[index : index + 3] == ["+set", "r_fullscreen", "0"]
            for index in range(max(0, len(args) - 2))
        ):
            raise AssertionError(f"Launch config {config.get('name')!r} must force windowed mode")
        if "basepr" not in args:
            raise AssertionError(f"Launch config {config.get('name')!r} must select basepr")


def validate_validation_coverage() -> None:
    validator = read("tools/validation/openq4_validate.py")
    push = read(".github/workflows/push-verification.yml")
    commit = read(".github/workflows/commit-validation.yml")
    for haystack, context in (
        (validator, "validation runner"),
        (push, "push verification workflow"),
        (commit, "commit validation workflow"),
    ):
        require(haystack, "vscode_fast_build.py", context)


def main() -> None:
    validate_tasks()
    validate_wrapper()
    validate_launch_configs()
    validate_validation_coverage()
    print("vscode_fast_build: ok")


if __name__ == "__main__":
    main()
